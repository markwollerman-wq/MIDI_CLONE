#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <bitset>
#include "Engine.h"
#include "Keys.h"

class MidiCloneProcessor : public juce::AudioProcessor
{
public:
    MidiCloneProcessor();
    ~MidiCloneProcessor() override = default;

    // ---- AudioProcessor ----
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override {}
    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;
    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;
    using juce::AudioProcessor::processBlock;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override                     { return true; }
    const juce::String getName() const override         { return "MIDI Clone"; }
    bool acceptsMidi() const override                   { return true; }
    bool producesMidi() const override                  { return true; }
    bool isMidiEffect() const override                  { return false; }
    double getTailLengthSeconds() const override        { return 0.0; }
    int getNumPrograms() override                       { return 1; }
    int getCurrentProgram() override                    { return 0; }
    void setCurrentProgram (int) override               {}
    const juce::String getProgramName (int) override    { return {}; }
    void changeProgramName (int, const juce::String&) override {}
    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    // ---- MIDI Clone ----
    juce::AudioProcessorValueTreeState params;
    static constexpr int sourcePreview = mc::numLanes;   // preview index for the source clip

    // Loads a .mid file. Returns an empty string on success, otherwise a message for the user.
    juce::String loadMidiFile (const juce::File& file);
    void generate();
    bool undoGenerate();                 // swaps back to the previous batch (press again to redo)
    bool canUndo() const;
    mc::Settings currentSettings() const;
    void applyTypeDefaults (mc::MidiType t);

    // Key Check
    struct SourceView
    {
        std::vector<mc::Note> notes;     // what the clones are built from (fixed / moved), from bar 1
        std::vector<int> styles;         // 0 normal, 1 out of key, 2 fixed, 3 fixed + repeat
        double lengthBeats = 4.0;
        int beatsPerBar = 4, low = 36, high = 48;
        int offKey = 0, total = 0, repeats = 0;
        bool fixOn = false, keyAuto = true;
        mc::Key key, target;             // detected / chosen key, and the key after Move To
        int moveTo = -1;
    };
    SourceView getSourceView() const;
    mc::KeyReport getKeyReport() const;
    void setKey (mc::Key k);             // user's choice from the Key menu
    void setAutoKey();
    void setFix (bool on);
    void setMoveTo (int root);           // -1 = keep the key
    int keyListVersion() const           { return keyVersion.load(); }

    // Thread-safe copies for the editor
    mc::Analysis getAnalysis() const;
    mc::LaneSet getLanes() const;
    juce::String getSourceName() const;
    bool hasSource() const;

    void setLocked (int lane, bool locked);
    void setPreview (int lane);          // -1 = off, 0..9 lanes, sourcePreview = source. One at a time.
    int getRequestedPreview() const      { return requestedPreview.load(); }
    int getActivePreview() const         { return activePreview.load(); }
    double getPlayPosition() const       { return playPosition.load(); }   // 0..1 inside the clip
    bool isHostPlaying() const           { return hostPlaying.load(); }

    // Writes one lane / the source (fixed or original, after Move To) to a temporary .mid file for dragging into Ableton
    juce::File writeLaneToTempFile (int lane) const;
    juce::File writeSourceToTempFile() const;

    // More like this: regenerates the other unlocked lanes as variations of one lane (which gets locked)
    bool moreLikeThis (int lane);

    // Presets (factory first, then the user's own)
    juce::StringArray getPresetNames() const;
    int numFactoryPresets() const;
    void applyPreset (int index);
    bool saveUserPreset (const juce::String& name);
    static juce::File presetFolder();

    std::atomic<int> changeCounter { 0 };   // editor repaints when this changes

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void rebuild();   // re-runs Key Check, Fix Key, Move To and the analysis
    void renderRange (int lane, double p0, double p1, double blockStartPpq, double samplesPerBeat,
                      int numSamples, std::vector<std::pair<int, juce::MidiMessage>>& out);
    void allNotesOff (int sample, std::vector<std::pair<int, juce::MidiMessage>>& out);
    void setParam (const char* id, float value);
    juce::File writeNotesToTempFile (const juce::String& clipName, const std::vector<mc::Note>& notes,
                                     double lengthBeats, int beatsPerBar) const;

    mutable juce::SpinLock dataLock;   // protects everything below up to the audio-thread section
    std::vector<mc::Note> rawNotes;    // the file as loaded (sorted)
    int sourceBeatsPerBar = 4;
    juce::String sourceName;
    mc::KeyReport keyReport;
    mc::Key chosenKey;
    bool keyAuto = true, fixOn = false;
    int moveTo = -1;
    mc::Key targetKey;
    SourceView view;
    std::vector<mc::Note> previewNotes;   // source preview, aligned to the clip
    mc::Analysis analysis;
    mc::LaneSet lanes, previousLanes;
    bool hasPrevious = false;

    std::atomic<int> keyVersion { 0 };
    std::atomic<int> requestedPreview { -1 };
    std::atomic<int> activePreview { -1 };
    std::atomic<double> playPosition { 0.0 };
    std::atomic<bool> hostPlaying { false };
    std::atomic<bool> flushNotes { false };

    // audio thread only
    double sampleRate = 44100.0;
    double freePpq = 0.0;
    double lastPpqEnd = -1.0;
    bool wasHostPlaying = false;
    std::bitset<128> sounding;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiCloneProcessor)
};
