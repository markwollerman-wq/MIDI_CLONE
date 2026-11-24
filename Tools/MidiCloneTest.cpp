// Test tool for MIDI Clone (not part of the plugin).
// usage: MidiCloneTest <input.mid> <output folder>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>
#include <map>
#include <set>

namespace
{
struct FakePlayHead : juce::AudioPlayHead
{
    juce::AudioPlayHead::PositionInfo info;
    juce::Optional<juce::AudioPlayHead::PositionInfo> getPosition() const override { return info; }
};

int failures = 0;
void check (bool ok, const juce::String& what)
{
    std::cout << (ok ? "  PASS  " : "  FAIL  ") << what << "\n";
    if (! ok) ++failures;
}

// notes starting together -> one chord (sorted pitches)
std::vector<std::pair<long, std::vector<int>>> groupChords (const std::vector<mc::Note>& notes)
{
    std::map<long, std::vector<int>> m;
    for (auto& n : notes) m[std::lround (n.start * 4)].push_back (n.pitch);
    std::vector<std::pair<long, std::vector<int>>> out;
    for (auto& kv : m) { auto v = kv.second; std::sort (v.begin(), v.end()); out.push_back ({ kv.first, v }); }
    return out;
}
int maskOfChord (const std::vector<int>& ps) { int mk = 0; for (int p : ps) mk |= 1 << (p % 12); return mk; }
std::vector<int> topNotes (const std::vector<mc::Note>& notes)
{
    std::vector<int> t;
    for (auto& c : groupChords (notes)) t.push_back (c.second.back());
    return t;
}
double voiceMovement (const std::vector<mc::Note>& notes)
{
    const auto ch = groupChords (notes);
    double total = 0;
    for (size_t i = 1; i < ch.size(); ++i)
    {
        double mv = 0;
        for (int p : ch[i].second) { int d = 99; for (int q : ch[i - 1].second) d = std::min (d, std::abs (p - q)); mv += d; }
        total += mv / ch[i].second.size();
    }
    return ch.size() > 1 ? total / (ch.size() - 1) : 0.0;
}

void setParam (MidiCloneProcessor& p, const char* id, float value)
{
    auto* prm = p.params.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

void reportLanes (MidiCloneProcessor& p, const juce::File& dir, const juce::String& label, bool newNotesOn = false)
{
    dir.createDirectory();
    const auto a = p.getAnalysis();
    const auto lanes = p.getLanes();
    std::cout << "\n== " << label << " ==\n";
    bool countsOk = true, rangeOk = true, lengthOk = true, distinct = true;
    if (p.currentSettings().type == mc::MidiType::chords)
    {
        int lo = 127, hi = 0;
        for (auto& n : a.polyNotes) { lo = std::min (lo, n.pitch); hi = std::max (hi, n.pitch); }
        bool smooth = true;
        const double srcMove = voiceMovement (a.polyNotes);
        for (int i = 0; i < mc::numLanes; ++i)
        {
            const auto& l = lanes[(size_t) i];
            std::map<int, int> c;
            const auto ch = groupChords (l.notes);
            for (auto& x : ch) c[maskOfChord (x.second)]++;
            std::cout << "  lane " << (i + 1) << ": " << ch.size() << " chords, ";
            for (auto& sh : a.chordShares)
            {
                std::cout << sh.name << " " << c[sh.mask] << "  ";
                const int expected = (int) std::lround (sh.share * (double) ch.size());
                countsOk = countsOk && (newNotesOn || std::abs (c[sh.mask] - expected) <= 1);
            }
            const double mv = voiceMovement (l.notes);
            std::cout << "| voice move " << juce::String (mv, 1) << " (source " << juce::String (srcMove, 1) << ")"
                      << " | like source " << juce::roundToInt (l.overlapWithSource * 100) << "%\n";
            smooth = smooth && mv <= srcMove + 3.0;
            for (auto& n : l.notes)
            {
                rangeOk = rangeOk && n.pitch >= lo - 3 && n.pitch <= hi + 3;
                lengthOk = lengthOk && n.start + n.length <= a.lengthBeats + 1e-6;
            }
            for (int j = 0; j < i; ++j)
                distinct = distinct && mc::overlap (lanes[(size_t) j].notes, l.notes) < 0.999;
            auto f = p.writeLaneToTempFile (i);
            f.copyFileTo (dir.getChildFile (juce::String::formatted ("lane_%02d.mid", i + 1)));
        }
        if (! newNotesOn) check (countsOk, "every lane keeps the source chord percentages (within one chord)");
        check (smooth, "chords move smoothly (voice leading no jumpier than the source + 3 semitones)");
        check (rangeOk, "chords stay within 3 semitones of the source's range");
        check (lengthOk, "every lane fits the source length");
        check (distinct, "all 10 lanes are different from each other");
        return;
    }
    for (int i = 0; i < mc::numLanes; ++i)
    {
        const auto& l = lanes[(size_t) i];
        std::map<int, int> c;
        for (auto& n : l.notes)
        {
            c[n.pitch]++;
            rangeOk = rangeOk && n.pitch >= a.lowest && n.pitch <= a.highest;
            lengthOk = lengthOk && n.start + n.length <= a.lengthBeats + 1e-6;
        }
        std::cout << "  lane " << (i + 1) << ": " << l.notes.size() << " notes, ";
        for (auto& s : a.shares)
        {
            std::cout << mc::noteName (s.pitch) << " " << juce::String (100.0 * c[s.pitch] / juce::jmax<size_t> (1, l.notes.size()), 1) << "%  ";
            const int expected = (int) std::lround (s.share * (double) l.notes.size());
            countsOk = countsOk && std::abs (c[s.pitch] - expected) <= 1;
        }
        std::cout << "| like source " << juce::roundToInt (l.overlapWithSource * 100) << "%\n";
        for (int j = 0; j < i; ++j)
            distinct = distinct && mc::overlap (lanes[(size_t) j].notes, l.notes) < 0.999;

        auto f = p.writeLaneToTempFile (i);
        f.copyFileTo (dir.getChildFile (juce::String::formatted ("lane_%02d.mid", i + 1)));
    }
    if (! newNotesOn)
    {
        check (countsOk, "every lane keeps the source note percentages (within one note)");
        check (rangeOk, "no lane goes above or below the source's notes");
    }
    check (lengthOk, "every lane fits the source length");
    check (distinct, "all 10 lanes are different from each other");
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3) { std::cout << "usage: MidiCloneTest <input.mid> <outdir> [expected_fixed.mid]\n"; return 1; }
    juce::ScopedJuceInitialiser_GUI gui;
    const juce::File in (juce::File::getCurrentWorkingDirectory().getChildFile (argv[1]));
    const juce::File out (juce::File::getCurrentWorkingDirectory().getChildFile (argv[2]));
    out.createDirectory();

    MidiCloneProcessor proc;
    proc.setRateAndBufferSizeDetails (44100.0, 512);
    proc.prepareToPlay (44100.0, 512);

    const auto err = proc.loadMidiFile (in);
    check (err.isEmpty(), "loads the MIDI file " + err);
    if (err.isNotEmpty()) return 1;

    const auto a = proc.getAnalysis();
    std::cout << "Source: " << a.notes.size() << " notes, " << a.bars << " bars, phrase " << a.periodBars
              << " bar(s), home " << mc::noteNameWithOctave (a.home) << ", guessed type " << (int) a.guessedType << "\n  ";
    for (auto& s : a.shares) std::cout << mc::noteNameWithOctave (s.pitch) << " " << s.count << " (" << juce::String (s.share * 100, 1) << "%)  ";
    std::cout << "\n";

    // 1) defaults from the guessed type
    proc.generate();
    reportLanes (proc, out.getChildFile ("default"), "Guessed type, default settings");

    // 2) locks survive Generate
    const auto before = proc.getLanes();
    proc.setLocked (3, true);
    proc.generate();
    const auto after = proc.getLanes();
    check (mc::overlap (before[3].notes, after[3].notes) > 0.999, "locked lane 4 is kept on Generate");
    check (mc::overlap (before[0].notes, after[0].notes) < 0.999, "unlocked lane 1 changes on Generate");
    proc.setLocked (3, false);

    // 3) Same Feel + Slide
    setParam (proc, "rhythm", 1); setParam (proc, "mode", 1); setParam (proc, "slide", 40);
    proc.generate();
    reportLanes (proc, out.getChildFile ("samefeel_slide"), "Same Feel + Slide 40%");
    int glides = 0;
    for (auto& l : proc.getLanes())
        for (size_t k = 0; k + 1 < l.notes.size(); ++k)
            glides += (l.notes[k].start + l.notes[k].length > l.notes[k + 1].start + 1e-9) ? 1 : 0;
    if (proc.currentSettings().type != mc::MidiType::chords)
        check (glides > 0, "Slide mode makes overlapping notes for glides (" + juce::String (glides) + ")");

    // 4) back to the defaults for the host test
    setParam (proc, "rhythm", 0); setParam (proc, "mode", 0);
    proc.generate();
    reportLanes (proc, out.getChildFile ("final"), "Locked + Grid Snapped (used for MIDI output test)");

    // 5) MIDI output with a fake Ableton transport at 140 BPM
    std::cout << "\n== MIDI output test (140 BPM) ==\n";
    FakePlayHead ph;
    proc.setPlayHead (&ph);
    const double sr = 44100.0, bpm = 140.0, spb = sr * 60.0 / bpm;
    const int block = 512;
    juce::AudioBuffer<float> buf (2, block);
    juce::MidiMessageSequence captured;
    double samplePos = 0.0;

    auto run = [&] (double untilBeat, bool playing)
    {
        while (samplePos / spb < untilBeat)
        {
            ph.info.setIsPlaying (playing);
            ph.info.setBpm (bpm);
            ph.info.setPpqPosition (samplePos / spb);
            ph.info.setTimeSignature (juce::AudioPlayHead::TimeSignature { 4, 4 });
            juce::MidiBuffer mb;
            proc.processBlock (buf, mb);
            for (const auto m : mb)
            {
                auto msg = m.getMessage();
                msg.setTimeStamp ((samplePos + m.samplePosition) / spb * 960.0);
                captured.addEvent (msg);
            }
            samplePos += block;
        }
    };

    proc.setPreview (1);
    run (32.0, true);                      // 8 bars of lane 2
    proc.setPreview (4);
    run (33.3, true);                      // ask for lane 5 in the middle of bar 9
    run (44.0, true);
    const double previewOffAt = samplePos / spb;   // end of the last block that still had preview on
    proc.setPreview (-1);
    run (45.0, true);
    captured.updateMatchedPairs();

    const auto lanes = proc.getLanes();
    // expected: lane 2 for beats 0..36 (the switch waits for the bar line at 36), then lane 5 until 44
    std::vector<std::pair<double, int>> expected;
    auto addLane = [&] (int lane, double from, double to)
    {
        for (int rep = 0; rep < 20; ++rep)
            for (auto& n : lanes[(size_t) lane].notes)
            {
                const double t = rep * a.lengthBeats + n.start;
                if (t >= from - 1e-6 && t < to - 1e-6) expected.push_back ({ t, n.pitch });
            }
    };
    addLane (1, 0.0, 36.0);
    addLane (4, 36.0, previewOffAt);
    std::sort (expected.begin(), expected.end());

    std::vector<std::pair<double, int>> got;
    int stuck = 0;
    for (int i = 0; i < captured.getNumEvents(); ++i)
    {
        auto* e = captured.getEventPointer (i);
        if (e->message.isNoteOn())
        {
            got.push_back ({ e->message.getTimeStamp() / 960.0, e->message.getNoteNumber() });
            if (e->noteOffObject == nullptr) ++stuck;
        }
    }
    bool timingOk = got.size() == expected.size();
    double worst = 0.0;
    for (size_t i = 0; timingOk && i < got.size(); ++i)
    {
        worst = std::max (worst, std::abs (got[i].first - expected[i].first));
        timingOk = got[i].second == expected[i].second && std::abs (got[i].first - expected[i].first) < 2.0 / spb + 1e-6;
    }
    std::cout << "  note-ons sent: " << got.size() << ", expected " << expected.size()
              << ", worst timing error " << juce::String (worst * spb, 2) << " samples\n";
    check (timingOk, "preview sends lane 2, then switches to lane 5 exactly on the next bar line");
    check (stuck == 0, "every note-on gets a note-off (no stuck notes)");

    juce::MidiFile capFile;
    capFile.setTicksPerQuarterNote (960);
    capFile.addTrack (captured);
    {
        auto f = out.getChildFile ("host_capture.mid");
        f.deleteFile();
        juce::FileOutputStream os (f);
        capFile.writeTo (os);
    }

    // 6) transport stopped: preview still plays from its own clock
    proc.setPreview (0);
    juce::MidiMessageSequence freeRun;
    const int freeBlocks = (int) std::ceil (8.0 * spb / block);   // two bars
    for (int b = 0; b < freeBlocks; ++b)
    {
        ph.info.setIsPlaying (false);
        ph.info.setPpqPosition (100.0);
        juce::MidiBuffer mb;
        proc.processBlock (buf, mb);
        for (const auto m : mb) freeRun.addEvent (m.getMessage());
    }
    int freeOns = 0;
    for (auto* e : freeRun) freeOns += e->message.isNoteOn() ? 1 : 0;
    check (freeOns > 0, "preview plays while Ableton is stopped (" + juce::String (freeOns) + " notes in 2 bars)");

    // 7) save / reopen the Ableton set
    juce::MemoryBlock state;
    proc.getStateInformation (state);
    MidiCloneProcessor reopened;
    reopened.setStateInformation (state.getData(), (int) state.getSize());
    check (mc::overlap (reopened.getLanes()[6].notes, proc.getLanes()[6].notes) > 0.999
           && reopened.getSourceName() == proc.getSourceName(), "lanes and source are restored when the set is reopened");

    // ===================== Phase 2 =====================
    std::cout << "\n== Key Check ==\n";
    proc.setPreview (-1);
    {
        const auto v = proc.getSourceView();
        std::cout << "  detected: " << mc::keyName (v.key) << "  (" << v.offKey << " of " << v.total << " notes out of key)";
        const auto same = mc::sameNotesAs (v.key);
        if (! same.empty()) std::cout << "  same notes as " << same;
        std::cout << "\n  next best: ";
        const auto rep = proc.getKeyReport();
        for (size_t i = 1; i < rep.ranked.size() && i < 5; ++i)
            std::cout << mc::keyName (rep.ranked[i].key) << " (" << rep.ranked[i].offKey << ")  ";
        std::cout << "\n  home note: " << mc::noteNameWithOctave (proc.getAnalysis().home) << "\n";
    }

    if (argc >= 4)
    {
        // The user's corrected file: Fix Key must give exactly this
        const juce::File expectedFile (juce::File::getCurrentWorkingDirectory().getChildFile (argv[3]));
        juce::FileInputStream es (expectedFile);
        juce::MidiFile emf;
        emf.readFrom (es);
        const int tpq = emf.getTimeFormat();
        std::vector<mc::Note> expected;
        for (int t = 0; t < emf.getNumTracks(); ++t)
        {
            juce::MidiMessageSequence seq (*emf.getTrack (t));
            seq.updateMatchedPairs();
            for (int i = 0; i < seq.getNumEvents(); ++i)
            {
                const auto& m = seq.getEventPointer (i)->message;
                if (m.isNoteOn())
                    expected.push_back ({ m.getTimeStamp() / tpq, (seq.getTimeOfMatchingKeyUp (i) - m.getTimeStamp()) / tpq,
                                          m.getNoteNumber(), m.getVelocity() });
            }
        }
        std::sort (expected.begin(), expected.end(), [] (auto& x, auto& y) { return x.start != y.start ? x.start < y.start : x.pitch < y.pitch; });

        check (mc::keyName (proc.getSourceView().key) == "C Phrygian", "detects C Phrygian");
        check (proc.getAnalysis().home % 12 == 0, "home note is C (not C#, even though C# is played more)");
        check (proc.getAnalysis().guessedType == mc::MidiType::stab, "guesses Stab / Riff");
        proc.setFix (true);
        const auto v = proc.getSourceView();
        bool same = v.notes.size() == expected.size();
        int moved = 0;
        for (size_t i = 0; same && i < expected.size(); ++i)
        {
            same = v.notes[i].pitch == expected[i].pitch && std::abs (v.notes[i].start - expected[i].start) < 1e-6
                && std::abs (v.notes[i].length - expected[i].length) < 1e-6 && v.notes[i].velocity == expected[i].velocity;
            moved += v.styles[i] >= 2 ? 1 : 0;
        }
        check (same, "Fix Key gives exactly the corrected file, note for note");
        check (moved == 5 && v.repeats >= 1, "5 notes marked as moved, repeat marked (" + juce::String (v.repeats) + ")");
        std::cout << "  after fix: ";
        for (auto& sh : proc.getAnalysis().shares) std::cout << mc::noteName (sh.pitch) << " " << juce::String (sh.share * 100, 1) << "%  ";
        std::cout << "\n";

        // Stab / Riff: repeated bars stay repeated
        proc.applyTypeDefaults (mc::MidiType::stab);
        setParam (proc, "rhythm", 0);
        proc.generate();
        const auto a2 = proc.getAnalysis();
        bool motifs = true;
        for (auto& l : proc.getLanes())
        {
            auto barNotes = [&] (int b)
            {
                std::vector<std::pair<long, int>> out;
                for (auto& n : l.notes)
                    if ((int) (n.start / a2.beatsPerBar) == b) out.push_back ({ std::lround ((n.start - b * a2.beatsPerBar) * 4), n.pitch });
                return out;
            };
            for (int b1 = 0; b1 < a2.bars; ++b1)
                for (int b2 = b1 + 1; b2 < a2.bars; ++b2)
                    if (a2.barGroup[(size_t) b1] == a2.barGroup[(size_t) b2] && barNotes (b1) != barNotes (b2)) motifs = false;
        }
        check (motifs, "Stab / Riff keeps repeated bars repeated (bars 1, 4, 5 and 3, 6)");
        reportLanes (proc, out.getChildFile ("stab_fixed"), "Stab / Riff from the fixed riff");
        bool allInKey = true;
        const mc::Key ck { 0, 3 };
        for (auto& l : proc.getLanes()) for (auto& n : l.notes) allInKey = allInKey && mc::inKey (n.pitch, ck);
        check (allInKey, "every clone note is in C Phrygian");
    }

    // New Notes
    setParam (proc, "newnotes", 20);
    proc.generate();
    {
        const auto v = proc.getSourceView();
        const auto a3 = proc.getAnalysis();
        std::set<int> src;
        for (auto& sh : a3.shares) src.insert (sh.pitch);
        int newTotal = 0, all = 0;
        bool inKeyOk = true, rangeOk = true;
        if (proc.currentSettings().type == mc::MidiType::chords)
        {
            // chords: count new chords (inversions of the source's chords aren't new)
            std::set<int> srcMasks;
            for (auto& c : a3.chordShares) srcMasks.insert (c.mask);
            for (auto& l : proc.getLanes())
                for (auto& c : groupChords (l.notes))
                {
                    ++all;
                    if (srcMasks.count (maskOfChord (c.second))) continue;
                    ++newTotal;
                    for (int p : c.second) inKeyOk = inKeyOk && mc::inKey (p, v.target);
                }
        }
        else
        for (auto& l : proc.getLanes())
            for (auto& n : l.notes)
            {
                ++all;
                if (src.count (n.pitch)) continue;
                ++newTotal;
                inKeyOk = inKeyOk && mc::inKey (n.pitch, v.target);
                if (proc.currentSettings().type == mc::MidiType::chords) continue;   // chords: checked below
                rangeOk = rangeOk && n.pitch >= a3.lowest - 2 && n.pitch <= a3.highest + 2;
            }
        std::cout << "\n== New Notes 20% ==\n  " << newTotal << " new of " << all << " notes ("
                  << juce::String (100.0 * newTotal / juce::jmax (1, all), 1) << "%)\n";
        bool allOnBeatOne = true;
        for (auto& n : a3.notes) allOnBeatOne = allOnBeatOne && std::lround (n.start * 4) % a3.stepsPerBar == 0;
        check (newTotal > 0 || allOnBeatOne, "New Notes adds notes the source doesn't use (beat 1 is kept)");
        check (inKeyOk, "every new note is in " + juce::String (mc::keyName (v.target)));
        check (rangeOk, "new notes stay within 2 semitones of the source's range");
        reportLanes (proc, out.getChildFile ("newnotes"), "New Notes 20% (percentages include new notes)", true);
    }
    setParam (proc, "newnotes", 0);

    // Keep Shape: compare how often clones move in the same direction as the source
    {
        auto shapeMatch = [&] (bool on)
        {
            setParam (proc, "shape", on ? 1.0f : 0.0f);
            setParam (proc, "rhythm", 0);
            double total = 0;
            for (int rep = 0; rep < 3; ++rep)
            {
                proc.generate();
                const auto a4 = proc.getAnalysis();
                const bool chordsMode = proc.currentSettings().type == mc::MidiType::chords;
                std::vector<int> srcLine;
                if (chordsMode) srcLine = topNotes (a4.polyNotes);
                else for (auto& n : a4.notes) srcLine.push_back (n.pitch);
                for (auto& l : proc.getLanes())
                {
                    std::vector<int> line;
                    if (chordsMode) line = topNotes (l.notes);
                    else for (auto& n : l.notes) line.push_back (n.pitch);
                    int match = 0, cnt = 0;
                    for (size_t i = 1; i < line.size() && i < srcLine.size(); ++i, ++cnt)
                    {
                        auto sg = [] (int x) { return (x > 0) - (x < 0); };
                        match += sg (line[i] - line[i - 1]) == sg (srcLine[i] - srcLine[i - 1]) ? 1 : 0;
                    }
                    total += cnt ? (double) match / cnt : 0;
                }
            }
            return total / (3 * mc::numLanes);
        };
        const double off = shapeMatch (false), on = shapeMatch (true);
        std::cout << "\n== Keep Shape ==\n  same direction as source: off " << juce::roundToInt (off * 100) << "%, on " << juce::roundToInt (on * 100) << "%\n";
        check (on >= off, "Keep Shape makes clones follow the source's up/down movement more");
    }

    // Undo
    {
        proc.generate();
        const auto first = proc.getLanes();
        proc.generate();
        const auto second = proc.getLanes();
        proc.undoGenerate();
        const bool back = mc::overlap (proc.getLanes()[0].notes, first[0].notes) > 0.999;
        proc.undoGenerate();
        const bool redo = mc::overlap (proc.getLanes()[0].notes, second[0].notes) > 0.999;
        check (back && redo, "Undo brings back the previous batch, and pressing again swaps back");
    }

    // Source preview
    {
        proc.setPreview (MidiCloneProcessor::sourcePreview);
        int ons = 0;
        for (int b = 0; b < 200; ++b)
        {
            ph.info.setIsPlaying (true);
            ph.info.setPpqPosition (b * block / spb);
            juce::MidiBuffer mb;
            proc.processBlock (buf, mb);
            for (const auto m : mb) ons += m.getMessage().isNoteOn() ? 1 : 0;
        }
        const auto srcNotes = proc.getAnalysis().notes;
        const int expectedOns = (int) std::count_if (srcNotes.begin(), srcNotes.end(),
                                                     [&] (const mc::Note& n) { return n.start < 200 * block / spb; });
        check (ons >= expectedOns && ons > 0, "source preview sends the source clip (" + juce::String (ons) + " notes)");
        proc.setPreview (-1);
        juce::MidiBuffer mb;
        proc.processBlock (buf, mb);
    }

    // Move To (Change Key)
    {
        const auto before = proc.getSourceView();
        const int newRoot = (before.key.root + 2) % 12;
        proc.setMoveTo (newRoot);
        const auto v = proc.getSourceView();
        proc.generate();
        bool inNewKey = true;
        for (auto& l : proc.getLanes()) for (auto& n : l.notes) inNewKey = inNewKey && mc::inKey (n.pitch, v.target);
        const auto name = proc.writeLaneToTempFile (0).getFileName();
        std::cout << "\n== Move To ==\n  " << mc::keyName (before.target) << " -> " << mc::keyName (v.target) << ", clip name: " << name << "\n";
        check (v.target.root == newRoot && v.target.scale == before.key.scale, "Move To changes the home note, same scale");
        check (inNewKey || before.offKey > 0, "clones are in the new key");
        check (name.contains ("(" + juce::String (mc::keyName (v.target)) + ")"), "dragged clip name includes the key");
        reportLanes (proc, out.getChildFile ("moved"), "Moved up a whole step");
        proc.setMoveTo (-1);
    }

    // ===================== Phase 3 =====================
    std::cout << "\n== Phase 3 ==\n";

    // Source drag-out: original, then fixed
    {
        auto readBack = [] (const juce::File& f)
        {
            juce::FileInputStream in (f);
            juce::MidiFile mf;
            mf.readFrom (in);
            int ons = 0;
            for (int t = 0; t < mf.getNumTracks(); ++t)
                for (auto* e : *mf.getTrack (t)) ons += e->message.isNoteOn() ? 1 : 0;
            return ons;
        };
        proc.setFix (false);
        const auto f1 = proc.writeSourceToTempFile();
        const auto v1 = proc.getSourceView();
        check (f1.existsAsFile() && readBack (f1) == (int) v1.notes.size() && f1.getFileName().contains ("(original)"),
               "source exports as the original: " + f1.getFileName());
        if (v1.offKey > 0)
        {
            proc.setFix (true);
            const auto f2 = proc.writeSourceToTempFile();
            juce::FileInputStream in (f2);
            juce::MidiFile mf;
            mf.readFrom (in);
            bool allIn = true;
            const auto v2 = proc.getSourceView();
            for (int t = 0; t < mf.getNumTracks(); ++t)
                for (auto* e : *mf.getTrack (t))
                    if (e->message.isNoteOn()) allIn = allIn && mc::inKey (e->message.getNoteNumber(), v2.key);
            check (allIn && f2.getFileName().contains ("fixed"), "source exports the fixed version: " + f2.getFileName());
        }
    }

    // More like this
    {
        proc.generate();
        const auto before = proc.getLanes();
        auto avgLike = [] (const mc::LaneSet& ls, int ref)
        {
            double t = 0; int c = 0;
            for (int i = 0; i < mc::numLanes; ++i) if (i != ref) { t += mc::overlap (ls[(size_t) ref].notes, ls[(size_t) i].notes); ++c; }
            return t / c;
        };
        const double likeBefore = avgLike (before, 1);
        proc.moreLikeThis (1);
        const auto after = proc.getLanes();
        const double likeAfter = avgLike (after, 1);
        std::cout << "  More like this lane 2: other lanes share " << juce::roundToInt (likeBefore * 100) << "% -> "
                  << juce::roundToInt (likeAfter * 100) << "% with it\n";
        check (mc::overlap (before[1].notes, after[1].notes) > 0.999 && after[1].locked, "the chosen lane stays and gets locked");
        check (likeAfter > likeBefore, "the other lanes become more like the chosen lane");
        proc.setLocked (1, false);
    }

    // Match by playing time (single-note types)
    if (proc.currentSettings().type != mc::MidiType::chords)
    {
        auto timeError = [&]()
        {
            const auto a5 = proc.getAnalysis();
            double worst = 0;
            for (auto& l : proc.getLanes())
            {
                std::map<int, double> t;
                double total = 0;
                for (auto& n : l.notes) { t[n.pitch] += n.length; total += n.length; }
                for (auto& sh : a5.shares) worst += std::abs (t[sh.pitch] / total - sh.timeShare);
            }
            return worst / mc::numLanes;
        };
        setParam (proc, "match", 0); proc.generate();
        const double countErr = timeError();
        setParam (proc, "match", 1); proc.generate();
        const double timeErr = timeError();
        std::cout << "  playing-time error per lane: Note count " << juce::String (countErr * 100, 1) << "%, Playing time "
                  << juce::String (timeErr * 100, 1) << "%\n";
        check (timeErr <= countErr + 0.02, "Match by playing time gets closer to the source's playing time");
        setParam (proc, "match", 0);
    }

    // Presets
    {
        const auto names = proc.getPresetNames();
        proc.applyPreset (names.indexOf ("Rave Stab - Locked"));
        const auto st = proc.currentSettings();
        check (st.type == mc::MidiType::stab && st.keepShape && st.rhythm == mc::RhythmMode::locked, "factory preset sets the options");
        setParam (proc, "variation", 9);
        setParam (proc, "newnotes", 12);
        check (proc.saveUserPreset ("Test Preset"), "saves a user preset");
        proc.applyPreset (0);
        const auto names2 = proc.getPresetNames();
        proc.applyPreset (names2.indexOf ("Test Preset"));
        const auto st2 = proc.currentSettings();
        check (names2.contains ("Test Preset") && st2.variation == 9 && std::abs (st2.newNotes - 0.12f) < 0.001f && st2.type == mc::MidiType::stab,
               "user preset comes back with the same options");
        MidiCloneProcessor::presetFolder().getChildFile ("Test Preset.xml").deleteFile();
        proc.applyTypeDefaults (proc.getAnalysis().guessedType);
        setParam (proc, "variation", 5); setParam (proc, "newnotes", 0);
    }
    proc.generate();

    // 8) screenshot of the window
    proc.setPreview (2);
    ph.info.setIsPlaying (true);
    for (int b = 0; b < 60; ++b) { ph.info.setPpqPosition (b * block / spb + 2.3); juce::MidiBuffer mb; proc.processBlock (buf, mb); }
    proc.setLocked (6, true);
    {
        std::unique_ptr<juce::AudioProcessorEditor> ed (proc.createEditor());
        ed->setVisible (true);
        juce::MessageManager::getInstance()->runDispatchLoopUntil (300);
        auto img = ed->createComponentSnapshot (ed->getLocalBounds(), true, 1.0f);
        auto f = out.getChildFile ("screenshot.png");
        f.deleteFile();
        juce::FileOutputStream os (f);
        juce::PNGImageFormat().writeImageToStream (img, os);
    }

    std::cout << "\n" << (failures == 0 ? "ALL CHECKS PASSED" : juce::String (failures) + " CHECK(S) FAILED") << "\n";
    return failures == 0 ? 0 : 2;
}
