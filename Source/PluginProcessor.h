#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <atomic>
#include <bitset>
#include "Engine.h"

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

    // Loads a .mid file. Returns an empty string on success, otherwise a message for the user.
    juce::String loadMidiFile (const juce::File& file);
    void generate();
    mc::Settings currentSettings() const;

    // Thread-safe copies for the editor
    mc::Analysis getAnalysis() const;
    mc::LaneSet getLanes() const;
    juce::String getSourceName() const;
    bool hasSource() const;

    void setLocked (int lane, bool locked);
    void setPreview (int lane);          // -1 = off. Only one lane previews at a time.
    int getRequestedPreview() const      { return requestedPreview.load(); }
    int getActivePreview() const         { return activePreview.load(); }
    double getPlayPosition() const       { return playPosition.load(); }   // 0..1 inside the clip
    bool isHostPlaying() const           { return hostPlaying.load(); }

    // Writes one lane to a temporary .mid file for dragging into Ableton
    juce::File writeLaneToTempFile (int lane) const;

    std::atomic<int> changeCounter { 0 };   // editor repaints when this changes

private:
    static juce::AudioProcessorValueTreeState::ParameterLayout createLayout();
    void renderRange (int lane, double p0, double p1, double blockStartPpq, double samplesPerBeat,
                      int numSamples, std::vector<std::pair<int, juce::MidiMessage>>& out);
    void allNotesOff (int sample, std::vector<std::pair<int, juce::MidiMessage>>& out);

    mutable juce::SpinLock dataLock;   // protects everything below up to the audio-thread section
    std::vector<mc::Note> sourceNotes;
    int sourceBeatsPerBar = 4;
    mc::Analysis analysis;
    mc::LaneSet lanes;
    juce::String sourceName;

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
