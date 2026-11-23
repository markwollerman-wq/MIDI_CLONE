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
                                                   StringArray { "Bass", "Melody / Lead", "Arp / Pluck" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "rhythm", 1 }, "Rhythm",
                                                   StringArray { "Locked", "Same Feel" }, 0));
    l.add (std::make_unique<AudioParameterChoice> (ParameterID { "mode", 1 }, "Engine",
                                                   StringArray { "Grid Snapped", "Slide" }, 0));
    l.add (std::make_unique<AudioParameterFloat> (ParameterID { "slide", 1 }, "Slide Amount",
                                                  NormalisableRange<float> (0.0f, 100.0f, 1.0f), 25.0f,
                                                  AudioParameterFloatAttributes().withLabel ("%")));
    l.add (std::make_unique<AudioParameterInt> (ParameterID { "variation", 1 }, "Variation", 1, 10, 5));
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
    if (laneIndex < 0 || laneIndex >= mc::numLanes || b <= a) return;
    const auto& lane = lanes[(size_t) laneIndex];
    const double L = analysis.lengthBeats;
    if (lane.empty || L <= 0.0) return;

    auto toSample = [&] (double ppq)
    { return juce::jlimit (0, numSamples - 1, (int) ((ppq - blockStartPpq) * samplesPerBeat)); };

    const long kFirst = (long) std::floor (a / L) - 1;
    const long kLast  = (long) std::floor (b / L);
    for (long k = kFirst; k <= kLast; ++k)
    {
        for (auto& n : lane.notes)
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

    auto a = mc::analyse (notes, beatsPerBar);
    if (! a.valid) return juce::String (a.problem);

    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        sourceNotes = notes;
        sourceBeatsPerBar = beatsPerBar;
        analysis = a;
        lanes = {};
        sourceName = file.getFileName();
    }
    requestedPreview = -1;
    flushNotes = true;

    // Pick the guessed type and its rhythm default (the user can change both)
    if (auto* p = params.getParameter ("type"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) a.guessedType));
    if (auto* p = params.getParameter ("rhythm"))
        p->setValueNotifyingHost (p->convertTo0to1 ((float) (int) mc::defaultRhythmFor (a.guessedType)));

    ++changeCounter;
    return {};
}

mc::Settings MidiCloneProcessor::currentSettings() const
{
    mc::Settings s;
    s.type        = (mc::MidiType) (int) params.getRawParameterValue ("type")->load();
    s.rhythm      = (mc::RhythmMode) (int) params.getRawParameterValue ("rhythm")->load();
    s.slide       = params.getRawParameterValue ("mode")->load() > 0.5f;
    s.slideAmount = params.getRawParameterValue ("slide")->load() / 100.0f;
    s.variation   = juce::jlimit (1, 10, (int) params.getRawParameterValue ("variation")->load());
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

    mc::generate (a, currentSettings(), working,
                  (std::uint32_t) juce::Random::getSystemRandom().nextInt());

    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        lanes = working;
    }
    flushNotes = true;   // the previewing lane may have new notes
    ++changeCounter;
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
    requestedPreview = (lane >= 0 && lane < mc::numLanes) ? lane : -1;
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

juce::File MidiCloneProcessor::writeLaneToTempFile (int laneIndex) const
{
    mc::Lane lane;
    mc::Analysis a;
    juce::String base;
    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        if (laneIndex < 0 || laneIndex >= mc::numLanes) return {};
        lane = lanes[(size_t) laneIndex];
        a = analysis;
        base = juce::File (sourceName).getFileNameWithoutExtension();
    }
    if (lane.empty) return {};
    if (base.isEmpty()) base = "MIDI";

    const int tpq = 960;
    const juce::String clipName = base + " Clone " + juce::String (laneIndex + 1);
    juce::MidiMessageSequence seq;
    seq.addEvent (juce::MidiMessage::textMetaEvent (3, clipName), 0.0);
    seq.addEvent (juce::MidiMessage::timeSignatureMetaEvent (a.beatsPerBar, 4), 0.0);
    for (auto& n : lane.notes)
    {
        seq.addEvent (juce::MidiMessage::noteOn (1, n.pitch, (juce::uint8) n.velocity), n.start * tpq);
        seq.addEvent (juce::MidiMessage::noteOff (1, n.pitch), (n.start + n.length) * tpq);
    }
    seq.updateMatchedPairs();
    // Marks the clip's full length (so a 4-bar clip stays 4 bars even if it ends with a rest)
    seq.addEvent (juce::MidiMessage::endOfTrack(), a.lengthBeats * tpq);

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
        src.setProperty ("notes", notesToString (sourceNotes), nullptr);
        root.appendChild (src, nullptr);

        juce::ValueTree ls ("Lanes");
        for (auto& lane : lanes)
        {
            juce::ValueTree l ("Lane");
            l.setProperty ("locked", lane.locked, nullptr);
            l.setProperty ("overlap", lane.overlapWithSource, nullptr);
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
    auto a = mc::analyse (notes, bpb);

    mc::LaneSet restored;
    auto ls = root.getChildWithName ("Lanes");
    for (int i = 0; i < juce::jmin (mc::numLanes, ls.getNumChildren()); ++i)
    {
        auto l = ls.getChild (i);
        auto& lane = restored[(size_t) i];
        lane.notes = notesFromString (l.getProperty ("notes").toString());
        lane.locked = (bool) l.getProperty ("locked", false);
        lane.overlapWithSource = (double) l.getProperty ("overlap", 0.0);
        lane.empty = lane.notes.empty();
    }

    {
        const juce::SpinLock::ScopedLockType lock (dataLock);
        sourceNotes = notes;
        sourceBeatsPerBar = bpb;
        analysis = a;
        sourceName = src.getProperty ("name").toString();
        lanes = restored;
    }
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
