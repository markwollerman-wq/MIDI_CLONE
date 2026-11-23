// Test tool for MIDI Clone (not part of the plugin).
// usage: MidiCloneTest <input.mid> <output folder>
#include <juce_audio_utils/juce_audio_utils.h>
#include "../Source/PluginProcessor.h"
#include "../Source/PluginEditor.h"
#include <iostream>
#include <map>

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

void setParam (MidiCloneProcessor& p, const char* id, float value)
{
    auto* prm = p.params.getParameter (id);
    prm->setValueNotifyingHost (prm->convertTo0to1 (value));
}

void reportLanes (MidiCloneProcessor& p, const juce::File& dir, const juce::String& label)
{
    dir.createDirectory();
    const auto a = p.getAnalysis();
    const auto lanes = p.getLanes();
    std::cout << "\n== " << label << " ==\n";
    bool countsOk = true, rangeOk = true, lengthOk = true, distinct = true;
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
    check (countsOk, "every lane keeps the source note percentages (within one note)");
    check (rangeOk, "no lane goes above or below the source's notes");
    check (lengthOk, "every lane fits the source length");
    check (distinct, "all 10 lanes are different from each other");
}
} // namespace

int main (int argc, char** argv)
{
    if (argc < 3) { std::cout << "usage: MidiCloneTest <input.mid> <outdir>\n"; return 1; }
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
