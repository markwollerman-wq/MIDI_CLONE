#include "PluginProcessor.h"
#include "PluginEditor.h"

namespace
{
juce::String notesToString (const std::vector<mc::Note>& notes)
{
    juce::StringArray parts;
    for (auto& n : notes)
        parts.add (juce::String (n.start, 5) + ":" + juce::String (n.length, 5) + ":"
                   + juce::String (n.pitch) + ":" + juce::String (n.velocity));
    return parts.joinIntoString (";");
}

std::vector<mc::Note> notesFromString (const juce::String& s)
{
    std::vector<mc::Note> out;
    for (auto& part : juce::StringArray::fromTokens (s, ";", ""))
    {
        auto f = juce::StringArray::fromTokens (part, ":", "");
        if (f.size() == 4)
            out.push_back ({ f[0].getDoubleValue(), f[1].getDoubleValue(), f[2].getIntValue(), f[3].getIntValue() });
    }
    return out;
}
} // namespace

MidiCloneProcessor::MidiCloneProcessor()
    : AudioProcessor (BusesProperties().withOutput ("Output", juce::AudioChannelSet::stereo(), true)),
      params (*this, nullptr, "PARAMS", createLayout())
{
}

juce::AudioProcessorValueTreeState::ParameterLayout MidiCloneProcessor::createLayout()
{
    using namespace juce;
    AudioProcessorValueTreeState::ParameterLayout l;
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "type", 1 }, "MIDI Type",
                                                   StringArray { "Bass", "Melody / Lead", "Arp / Pluck", "Stab / Riff", "Chords" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "rhythm", 1 }, "Rhythm",
                                                   StringArray { "Locked", "Same Feel" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Engine",
                                                   StringArray { "Grid Snapped", "Slide" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "slide", 1 }, "Slide Amount",
                                                  NormalisableRange<float> (0.0f, 100.0f, 1.0f), 25.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "variation", 1 }, "Variation", 1, 10, 5));
    l.add (std::make_unique<AudioParameterBool> (ParameterID { "shape", 1 }, "Keep Shape", false));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "newnotes", 1 }, "New Notes",
                                                  NormalisableRange<float> (0.0f, 30.0f, 1.0f), 0.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "match", 1 }, "Match",
                                                   StringArray { "Note count", "Playing time" }, 0));
    return l;
}

void MidiCloneProcessor::prepareToPlay (double sr, int)
{
    sampleRate = sr > 0 ? sr : 44100.0;
    sounding.reset();
    lastPpqEnd = -1.0;
}

bool MidiCloneProcessor::isBusesLayoutSupported (const BusesLayout& layouts) const
{
    const auto out = layouts.getMainOutputChannelSet();
    return out == juce::AudioChannelSet::stereo() || out == juce::AudioChannelSet::mono();
}

// ---------------------------------------------------------------------------
// MIDI output

void MidiCloneProcessor::allNotesOff (int sample, std::vector<std::pair<int, juce::MidiMessage>>& out)
{
    for (int p = 0; p < 128; ++p)
        if (sounding[(size_t) p])
            out.push_back ({ sample, juce::MidiMessage::noteOff (1, p) });
    sounding.reset();
}

void MidiCloneProcessor::renderRange (int laneIndex, double a, double b, double blockStartPpq, double samplesPerBeat,
                                      int numSamples, std::vector<std::pair<int, juce::MidiMessage>>& out)
{
    if (laneIndex < 0 || laneIndex > sourcePreview || b <= a) return;
    const auto& notes = laneIndex == sourcePreview ? previewNotes : lanes[(size_t) laneIndex].notes;
    const double L = analysis.lengthBeats;
    if (notes.empty() || L <= 0.0) return;

    auto toSample = [&] (double ppq)
    { return juce::jlimit (0, numSamples - 1, (int) ((ppq - blockStartPpq) * samplesPerBeat)); };

    const long kFirst = (long) std::floor (a / L) - 1;
    const long kLast  = (long) std::floor (b / L);
    for (long k = kFirst; k <= kLast; ++k)
    {
        for (auto& n : notes)
        {
            const double on = (double) k * L + n.start;
            const double off = on + n.length;
            if (on >= a && on < b)
            {
                const int s = toSample (on);
                if (sounding[(size_t) n.pitch])
                    out.push_back ({ s, juce::MidiMessage::noteOff (1, n.pitch) });
                out.push_back ({ s, juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity) });
                sounding[(size_t) n.pitch] = true;
            }
            // only end notes that are sounding (a note we joined halfway was never started)
            if (off >= a && off < b && sounding[(size_t) n.pitch])
            {
                out.push_back ({ toSample (off), juce::MidiMessage::noteOff (1, n.pitch) });
                sounding[(size_t) n.pitch] = false;
            }
        }
    }
}

void MidiCloneProcessor::processBlock (juce::AudioBuffer<float>& buffer, juce::MidiBuffer& midi)
{
    juce::ScopedNoDenormals noDenormals;
    buffer.clear();
    midi.clear();   // MIDI Clone makes its own MIDI; incoming notes are not passed on

    const int numSamples = buffer.getNumSamples();
    if (numSamples <= 0) return;

    double bpm = 120.0, ppq = 0.0, barBeats = 4.0;
    bool playing = false;
    if (auto* ph = getPlayHead())
    {
        if (auto pos = ph->getPosition())
        {
            if (auto b = pos->getBpm()) bpm = juce::jlimit (20.0, 999.0, *b);
            if (auto p = pos->getPpqPosition()) { ppq = *p; playing = pos->getIsPlaying(); }
            if (auto ts = pos->getTimeSignature())
                if (ts->numerator > 0 && ts->denominator > 0)
                    barBeats = ts->numerator * 4.0 / ts->denominator;
        }
    }
    hostPlaying = playing;

    const double samplesPerBeat = sampleRate * 60.0 / bpm;
    const double blockBeats = numSamples / samplesPerBeat;
    std::vector<std::pair<int, juce::MidiMessage>> events;

    if (flushNotes.exchange (false))
        allNotesOff (0, events);

    if (playing != wasHostPlaying)
    {
        allNotesOff (0, events);
        freePpq = 0.0;
        lastPpqEnd = -1.0;
        wasHostPlaying = playing;
    }

    double p0 = playing ? ppq : freePpq;
    if (playing && lastPpqEnd >= 0.0 && std::abs (p0 - lastPpqEnd) > 0.01)
        allNotesOff (0, events);   // loop jump or relocate

    double p1 = p0 + blockBeats;

    const juce::SpinLock::ScopedTryLockType lock (dataLock);
    if (lock.isLocked())
    {
        int act = activePreview.load();
        const int req = requestedPreview.load();
        bool rendered = false;

        if (req != act)
        {
            if (req < 0)
            {
                allNotesOff (0, events);
                act = -1;
            }
            else if (act < 0)
            {
                act = req;
                if (! playing) { freePpq = 0.0; p0 = 0.0; p1 = blockBeats; }   // stopped: start from the top
            }
            else
            {
                // switch lanes on the next bar line so the groove doesn't break
                const double nextBar = std::ceil (p0 / barBeats - 1e-9) * barBeats;
                if (nextBar < p1)
                {
                    renderRange (act, p0, nextBar, p0, samplesPerBeat, numSamples, events);
                    allNotesOff (juce::jlimit (0, numSamples - 1, (int) ((nextBar - p0) * samplesPerBeat)), events);
                    act = req;
                    renderRange (act, nextBar, p1, p0, samplesPerBeat, numSamples, events);
                    rendered = true;
                }
            }
            activePreview = act;
        }

        if (! rendered && act >= 0)
            renderRange (act, p0, p1, p0, samplesPerBeat, numSamples, events);

        if (act >= 0 && analysis.lengthBeats > 0.0)
        {
            double pos = std::fmod (p1, analysis.lengthBeats);
            if (pos < 0) pos += analysis.lengthBeats;
            playPosition = pos / analysis.lengthBeats;
        }
    }

    if (! playing)
        freePpq = (activePreview.load() >= 0) ? p1 : 0.0;
    lastPpqEnd = p1;

    std::stable_sort (events.begin(), events.end(), [] (const auto& x, const auto& y)
    {
        if (x.first != y.first) return x.first < y.first;
        return x.second.isNoteOff() && ! y.second.isNoteOff();   // note-offs first
    });
    for (auto& e : events)
        midi.addEvent (e.second, e.first);
}

// ---------------------------------------------------------------------------
// Loading and generating

juce::String MidiCloneProcessor::loadMidiFile (const juce::File& file)
{
    juce::FileInputStream in (file);
    if (! in.openedOk()) return "Couldn't open that file.";

    juce::MidiFile mf;
    if (! mf.readFrom (in)) return "That doesn't look like a MIDI file.";

    const int tpq = mf.getTimeFormat();
    if (tpq <= 0) return "This file uses SMPTE timing. Re-export it from your DAW as a normal MIDI clip.";

    int beatsPerBar = 4;
    bool foundTimeSig = false;
    std::vector<mc::Note> notes;

    for (int t = 0; t < mf.getNumTracks(); ++t)
    {
        juce::MidiMessageSequence seq (*mf.getTrack (t));
        seq.updateMatchedPairs();
        for (int i = 0; i < seq.getNumEvents(); ++i)
        {
            const auto& m = seq.getEventPointer (i)->message;
            if (m.isTimeSignatureMetaEvent() && ! foundTimeSig)
            {
                int num = 4, den = 4;
                m.getTimeSignatureInfo (num, den);
                if (num > 0 && den > 0)
                    beatsPerBar = juce::jmax (1, (int) std::lround (num * 4.0 / den));
                foundTimeSig = true;
            }
            if (m.isNoteOn())
            {
                const double on = m.getTimeStamp();
                double off = seq.getTimeOfMatchingKeyUp (i);
                if (off <= on) off = on + tpq / 4.0;
                notes.push_back ({ on / tpq, (off - on) / tpq, m.getNoteNumber(), m.getVelocity() });
            }
        }
    }

    auto check = mc::analyse (notes, beatsPerBar);
    if (! check.valid) return juce::String (check.problem);

    std::sort (notes.begin(), notes.end(), [] (const mc::Note& x, const mc::Note& y)
               { return x.start < y.start - 1e-9 || (std::abs (x.start - y.start) <= 1e-9 && x.pitch < y.pitch); });
    auto report = mc::detectKey (notes, beatsPerBar);

    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        rawNotes = notes;
        sourceBeatsPerBar = beatsPerBar;
        sourceName = file.getFileName();
        keyReport = report;
        keyAuto = true;
        fixOn = false;
        moveTo = -1;
        lanes = {};
        hasPrevious = false;
    }
    rebuild();
    ++keyVersion;
    requestedPreview = -1;
    flushNotes = true;

    applyTypeDefaults (getAnalysis().guessedType);   // the user can change the type afterwards
    ++changeCounter;
    return {};
}

void MidiCloneProcessor::rebuild()
{
    std::vector<mc::Note> raw;
    int bpb = 4;
    mc::KeyReport report;
    mc::Key key;
    bool fix = false, autoKey = true;
    int move = -1;
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        raw = rawNotes;
        bpb = sourceBeatsPerBar;
        report = keyReport;
        key = chosenKey;
        fix = fixOn;
        autoKey = keyAuto;
        move = moveTo;
    }
    if (raw.empty()) return;

    if (autoKey) key = report.ranked.empty() ? mc::Key {} : report.ranked.front().key;

    const auto fr = mc::fixToKey (raw, key);
    auto working = fix ? fr.notes : raw;

    // Move To: shift to the new root by the smallest step (-6..+5 semitones) so the register stays close
    int shift = 0;
    if (move >= 0 && key.valid())
    {
        shift = ((move - key.root) % 12 + 12) % 12;
        if (shift > 5) shift -= 12;
    }
    for (auto& n : working) n.pitch = juce::jlimit (0, 127, n.pitch + shift);
    const mc::Key target { key.valid() ? ((key.root + shift) % 12 + 12) % 12 : 0, key.scale };

    auto a = mc::analyse (working, bpb, key.valid() && key.scale != mc::chromaticScaleIndex() ? target.root : -1);

    SourceView v;
    v.lengthBeats = a.lengthBeats;
    v.beatsPerBar = a.beatsPerBar;
    v.total = (int) raw.size();
    v.offKey = (int) fr.offKey.size();
    v.repeats = (int) fr.repeats.size();
    v.fixOn = fix;
    v.keyAuto = autoKey;
    v.key = key;
    v.target = target;
    v.moveTo = move;
    v.low = 127; v.high = 0;
    std::vector<mc::Note> preview;
    for (size_t i = 0; i < working.size(); ++i)
    {
        auto n = working[i];
        n.start -= a.offsetBeats;
        const bool off = std::find (fr.offKey.begin(), fr.offKey.end(), (int) i) != fr.offKey.end();
        const bool rep = std::find (fr.repeats.begin(), fr.repeats.end(), (int) i) != fr.repeats.end();
        v.notes.push_back (n);
        v.styles.push_back (fix ? (off ? (rep ? 3 : 2) : 0) : (off ? 1 : 0));
        v.low = juce::jmin (v.low, n.pitch);
        v.high = juce::jmax (v.high, n.pitch);
        if (n.start >= -1e-9 && n.start < a.lengthBeats) preview.push_back (n);
    }

    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        analysis = a;
        view = v;
        previewNotes = preview;
        targetKey = target;
        if (autoKey) chosenKey = key;
    }
    flushNotes = true;
    ++changeCounter;
}

void MidiCloneProcessor::setParam (const char* id, float value)
{
    if (auto* p = params.getParameter (id))
        p->setValueNotifyingHost (p->convertTo0to1 (value));
}

void MidiCloneProcessor::applyTypeDefaults (mc::MidiType t)
{
    setParam ("type", (float) (int) t);
    setParam ("rhythm", (float) (int) mc::defaultRhythmFor (t));
    setParam ("shape", mc::defaultKeepShapeFor (t) ? 1.0f : 0.0f);
    if (t == mc::MidiType::stab || t == mc::MidiType::chords) setParam ("mode", 0.0f);   // stabs and chords: Grid Snapped
}

mc::Settings MidiCloneProcessor::currentSettings() const
{
    mc::Settings s;
    s.type        = (mc::MidiType) juce::jlimit (0, 4, (int) params.getRawParameterValue ("type")->load());
    s.rhythm      = (mc::RhythmMode) (int) params.getRawParameterValue ("rhythm")->load();
    s.slide       = params.getRawParameterValue ("mode")->load() > 0.5f;
    s.slideAmount = params.getRawParameterValue ("slide")->load() / 100.0f;
    s.variation   = juce::jlimit (1, 10, (int) params.getRawParameterValue ("variation")->load());
    s.keepShape   = params.getRawParameterValue ("shape")->load() > 0.5f;
    s.newNotes    = params.getRawParameterValue ("newnotes")->load() / 100.0f;
    s.matchTime   = params.getRawParameterValue ("match")->load() > 0.5f;
    const juce::SpinLock::ScopedLockType lock (dataLock);
    s.keyRoot  = targetKey.root;
    s.keyScale = targetKey.scale;
    return s;
}

void MidiCloneProcessor::generate()
{
    mc::Analysis a;
    mc::LaneSet working;
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        if (! analysis.valid) return;
        a = analysis;
        working = lanes;
    }
    const auto before = working;

    mc::generate (a, currentSettings(), working,
                  (std::uint32_t) juce::Random::getSystemRandom().nextInt());

    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        previousLanes = before;
        hasPrevious = true;
        lanes = working;
    }
    flushNotes = true;   // the previewing lane may have new notes
    ++changeCounter;
}

bool MidiCloneProcessor::undoGenerate()
{
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        if (! hasPrevious) return false;
        std::swap (lanes, previousLanes);
    }
    flushNotes = true;
    ++changeCounter;
    return true;
}

bool MidiCloneProcessor::canUndo() const
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return hasPrevious;
}

MidiCloneProcessor::SourceView MidiCloneProcessor::getSourceView() const
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return view;
}

mc::KeyReport MidiCloneProcessor::getKeyReport() const
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return keyReport;
}

void MidiCloneProcessor::setKey (mc::Key k)
{
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        chosenKey = k;
        keyAuto = false;
    }
    rebuild();
}

void MidiCloneProcessor::setAutoKey()
{
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        keyAuto = true;
    }
    rebuild();
}

void MidiCloneProcessor::setFix (bool on)
{
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        fixOn = on;
    }
    rebuild();
}

void MidiCloneProcessor::setMoveTo (int root)
{
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        moveTo = (root >= 0 && root < 12) ? root : -1;
    }
    rebuild();
}

void MidiCloneProcessor::setLocked (int lane, bool locked)
{
    if (lane < 0 || lane >= mc::numLanes) return;
    const juce::SpinLock::ScopedLockType lock (dataLock);
    lanes[(size_t) lane].locked = locked;
    ++changeCounter;
}

void MidiCloneProcessor::setPreview (int lane)
{
    requestedPreview = (lane >= 0 && lane <= sourcePreview) ? lane : -1;
    ++changeCounter;
}

mc::Analysis MidiCloneProcessor::getAnalysis() const
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return analysis;
}

mc::LaneSet MidiCloneProcessor::getLanes() const
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return lanes;
}

juce::String MidiCloneProcessor::getSourceName() const
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return sourceName;
}

bool MidiCloneProcessor::hasSource() const
{
    const juce::SpinLock::ScopedLockType lock (dataLock);
    return analysis.valid;
}

juce::File MidiCloneProcessor::writeNotesToTempFile (const juce::String& clipName, const std::vector<mc::Note>& notes,
                                                     double lengthBeats, int beatsPerBar) const
{
    if (notes.empty()) return {};
    const int tpq = 960;
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, clipName), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (beatsPerBar, 4), 0.0);
    for (auto& n : notes)
    {
        if (n.start < -1e-9 || n.start >= lengthBeats) continue;
        const double end = juce::jmin (n.start + n.length, lengthBeats - 0.001);
        seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity), n.start * tpq);
        seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), end * tpq);
    }
    seq.updateMatchedPairs();
    // Marks the clip's full length (so a 4-bar clip stays 4 bars even if it ends with a rest)
    seq.addEvent (juce::MidiMessage::endOfTrack(), lengthBeats * tpq);

    juce::MidiFile mf;
    mf.setTicksPerQuarterNote (tpq);
    mf.addTrack (seq);

    auto dir = juce::File::getSpecialLocation (juce::File::tempDirectory).getChildFile ("WOLLER MIDI Clone");
    dir.createDirectory();
    auto file = dir.getChildFile (juce::File::createLegalFileName (clipName) + ".mid");
    file.deleteFile();
    juce::FileOutputStream out (file);
    if (! out.openedOk() || ! mf.writeTo (out)) return {};
    out.flush();
    return file;
}

juce::File MidiCloneProcessor::writeLaneToTempFile (int laneIndex) const
{
    mc::Lane lane;
    mc::Analysis a;
    juce::String base;
    mc::Key key;
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        if (laneIndex < 0 || laneIndex >= mc::numLanes) return {};
        lane = lanes[(size_t) laneIndex];
        a = analysis;
        key = targetKey;
        base = sourceName.containsChar ('.') ? sourceName.upToLastOccurrenceOf (".", false, false) : sourceName;
    }
    if (lane.empty) return {};
    if (base.isEmpty()) base = "MIDI";
    juce::String clipName = base + " Clone " + juce::String (laneIndex + 1);
    if (key.valid() && key.scale != mc::chromaticScaleIndex())
        clipName += " (" + juce::String (mc::keyName (key)) + ")";
    return writeNotesToTempFile (clipName, lane.notes, a.lengthBeats, a.beatsPerBar);
}

juce::File MidiCloneProcessor::writeSourceToTempFile() const
{
    SourceView v;
    juce::String base;
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        if (! analysis.valid) return {};
        v = view;
        base = sourceName.containsChar ('.') ? sourceName.upToLastOccurrenceOf (".", false, false) : sourceName;
    }
    if (base.isEmpty()) base = "MIDI";
    juce::String tag;
    const bool keyed = v.target.valid() && v.target.scale != mc::chromaticScaleIndex();
    if (v.fixOn && keyed)            tag = juce::String (mc::keyName (v.target)) + " fixed";
    else if (v.moveTo >= 0 && keyed) tag = juce::String (mc::keyName (v.target));
    else                             tag = "original";
    return writeNotesToTempFile (base + " (" + tag + ")", v.notes, v.lengthBeats, v.beatsPerBar);
}

bool MidiCloneProcessor::moreLikeThis (int laneIndex)
{
    if (laneIndex < 0 || laneIndex >= mc::numLanes) return false;
    mc::Analysis original;
    mc::LaneSet working;
    int bpb = 4;
    mc::Key key;
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        if (! analysis.valid || lanes[(size_t) laneIndex].empty) return false;
        original = analysis;
        working = lanes;
        bpb = analysis.beatsPerBar;
        key = targetKey;
    }
    juce::ignoreUnused (bpb, key);
    const auto before = working;
    working[(size_t) laneIndex].locked = true;   // the chosen lane stays
    const auto settings = currentSettings();
    mc::varyFrom (laneIndex, settings, working, (std::uint32_t) juce::Random::getSystemRandom().nextInt());

    // "x% like source" still compares with your original clip
    for (int i = 0; i < mc::numLanes; ++i)
        if (i != laneIndex && ! before[(size_t) i].locked)
            working[(size_t) i].overlapWithSource = mc::overlap (settings.type == mc::MidiType::chords ? original.polyNotes
                                                                                                    : original.notes,
                                                                 working[(size_t) i].notes);
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        previousLanes = before;
        hasPrevious = true;
        lanes = working;
    }
    flushNotes = true;
    ++changeCounter;
    return true;
}

// ---------------------------------------------------------------------------
// Presets

namespace
{
struct FactoryPreset { const char* name; int type, rhythm, mode; float slide; int variation; bool shape; float newNotes; int match; };
const FactoryPreset factoryPresets[] = {
    { "Init",                  0, 0, 0, 25.0f, 5, false, 0.0f,  0 },
    { "Bass - Tight",          0, 0, 0, 25.0f, 3, false, 0.0f,  0 },
    { "Bass - Rolling Slide",  0, 0, 1, 35.0f, 5, false, 0.0f,  0 },
    { "Bass - Time Balanced",  0, 0, 0, 25.0f, 5, false, 0.0f,  1 },
    { "Rave Stab - Locked",    3, 0, 0, 25.0f, 4, true,  0.0f,  0 },
    { "Rave Stab - Loose",     3, 1, 0, 25.0f, 7, true,  10.0f, 0 },
    { "Melody - Close",        1, 1, 0, 25.0f, 3, true,  0.0f,  0 },
    { "Melody - Explore",      1, 1, 0, 25.0f, 8, false, 15.0f, 0 },
    { "Arp - Shifting",        2, 0, 0, 25.0f, 6, true,  0.0f,  0 },
    { "Chords - Smooth",       4, 0, 0, 25.0f, 4, true,  0.0f,  0 },
    { "Chords - New Chords",   4, 0, 0, 25.0f, 6, true,  20.0f, 0 },
};
constexpr int numFactory = (int) (sizeof (factoryPresets) / sizeof (factoryPresets[0]));
const char* const presetParamIds[] = { "type", "rhythm", "mode", "slide", "variation", "shape", "newnotes", "match" };
} // namespace

juce::File MidiCloneProcessor::presetFolder()
{
    return juce::File::getSpecialLocation (juce::File::userApplicationDataDirectory)
               .getChildFile ("WOLLER").getChildFile ("MIDI Clone").getChildFile ("Presets");
}

int MidiCloneProcessor::numFactoryPresets() const { return numFactory; }

juce::StringArray MidiCloneProcessor::getPresetNames() const
{
    juce::StringArray names;
    for (auto& p : factoryPresets) names.add (p.name);
    auto files = presetFolder().findChildFiles (juce::File::findFiles, false, "*.xml");
    files.sort();
    for (auto& f : files) names.add (f.getFileNameWithoutExtension());
    return names;
}

void MidiCloneProcessor::applyPreset (int index)
{
    if (index >= 0 && index < numFactory)
    {
        const auto& p = factoryPresets[index];
        setParam ("type", (float) p.type);
        setParam ("rhythm", (float) p.rhythm);
        setParam ("mode", (float) p.mode);
        setParam ("slide", p.slide);
        setParam ("variation", (float) p.variation);
        setParam ("shape", p.shape ? 1.0f : 0.0f);
        setParam ("newnotes", p.newNotes);
        setParam ("match", (float) p.match);
        return;
    }
    const auto names = getPresetNames();
    if (index < 0 || index >= names.size()) return;
    auto xml = juce::XmlDocument::parse (presetFolder().getChildFile (names[index] + ".xml"));
    if (xml == nullptr || ! xml->hasTagName ("MidiClonePreset")) return;
    for (auto* id : presetParamIds)
        if (xml->hasAttribute (id))
            setParam (id, (float) xml->getDoubleAttribute (id));
}

bool MidiCloneProcessor::saveUserPreset (const juce::String& name)
{
    const auto clean = juce::File::createLegalFileName (name.trim());
    if (clean.isEmpty()) return false;
    presetFolder().createDirectory();
    juce::XmlElement xml ("MidiClonePreset");
    for (auto* id : presetParamIds)
        if (auto* p = params.getParameter (id))
            xml.setAttribute (id, (double) p->convertFrom0to1 (p->getValue()));
    return xml.writeTo (presetFolder().getChildFile (clean + ".xml"));
}

// ---------------------------------------------------------------------------
// Saving with the Ableton set

void MidiCloneProcessor::getStateInformation (juce::MemoryBlock& destData)
{
    juce::ValueTree root ("MidiCloneState");
    root.appendChild (params.copyState(), nullptr);
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        juce::ValueTree src ("Source");
        src.setProperty ("name", sourceName, nullptr);
        src.setProperty ("beatsPerBar", sourceBeatsPerBar, nullptr);
        src.setProperty ("notes", notesToString (rawNotes), nullptr);
        root.appendChild (src, nullptr);

        juce::ValueTree k ("Key");
        k.setProperty ("auto", keyAuto, nullptr);
        k.setProperty ("root", chosenKey.root, nullptr);
        k.setProperty ("scale", chosenKey.scale, nullptr);
        k.setProperty ("fix", fixOn, nullptr);
        k.setProperty ("moveTo", moveTo, nullptr);
        root.appendChild (k, nullptr);

        juce::ValueTree ls ("Lanes");
        for (auto& lane : lanes)
        {
            juce::ValueTree l ("Lane");
            l.setProperty ("locked", lane.locked, nullptr);
            l.setProperty ("overlap", lane.overlapWithSource, nullptr);
            l.setProperty ("newNotes", lane.newNoteCount, nullptr);
            l.setProperty ("notes", notesToString (lane.notes), nullptr);
            ls.appendChild (l, nullptr);
        }
        root.appendChild (ls, nullptr);
    }
    if (auto xml = root.createXml())
        copyXmlToBinary (*xml, destData);
}

void MidiCloneProcessor::setStateInformation (const void* data, int sizeInBytes)
{
    auto xml = getXmlFromBinary (data, sizeInBytes);
    if (xml == nullptr) return;
    auto root = juce::ValueTree::fromXml (*xml);
    if (! root.hasType ("MidiCloneState")) return;

    auto p = root.getChildWithName (params.state.getType());
    if (p.isValid()) params.replaceState (p);

    auto src = root.getChildWithName ("Source");
    auto notes = notesFromString (src.getProperty ("notes").toString());
    const int bpb = (int) src.getProperty ("beatsPerBar", 4);
    auto k = root.getChildWithName ("Key");

    mc::LaneSet restored;
    auto ls = root.getChildWithName ("Lanes");
    for (int i = 0; i < juce::jmin (mc::numLanes, ls.getNumChildren()); ++i)
    {
        auto l = ls.getChild (i);
        auto& lane = restored[(size_t) i];
        lane.notes = notesFromString (l.getProperty ("notes").toString());
        lane.locked = (bool) l.getProperty ("locked", false);
        lane.overlapWithSource = (double) l.getProperty ("overlap", 0.0);
        lane.newNoteCount = (int) l.getProperty ("newNotes", 0);
        lane.empty = lane.notes.empty();
    }

    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        rawNotes = notes;
        sourceBeatsPerBar = bpb;
        sourceName = src.getProperty ("name").toString();
        keyReport = mc::detectKey (notes, bpb);
        keyAuto = (bool) k.getProperty ("auto", true);
        chosenKey = { (int) k.getProperty ("root", 0), (int) k.getProperty ("scale", -1) };
        if (chosenKey.scale >= (int) mc::scales().size()) keyAuto = true;
        fixOn = (bool) k.getProperty ("fix", false);
        moveTo = (int) k.getProperty ("moveTo", -1);
        lanes = restored;
        hasPrevious = false;
        analysis = {};
    }
    if (! notes.empty()) rebuild();
    ++keyVersion;
    requestedPreview = -1;
    ++changeCounter;
}

juce::AudioProcessorEditor* MidiCloneProcessor::createEditor()
{
    return new MidiCloneEditor (*this);
}

juce::AudioProcessor* JUCE_CALLTYPE createPluginFilter()
{
    return new MidiCloneProcessor();
}
