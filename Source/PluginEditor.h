#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include <juce_gui_extra/juce_gui_extra.h>
#include "PluginProcessor.h"

namespace Pal
{
const juce::Colour bg       { 0xff050506 };
const juce::Colour panel    { 0xff0d0d0f };
const juce::Colour panelHi  { 0xff18181b };
const juce::Colour red      { 0xffff2340 };
const juce::Colour redSoft  { 0xffff5c70 };
const juce::Colour redDim   { 0xff5c0d17 };
const juce::Colour redDeep  { 0xff1c0307 };
const juce::Colour text     { 0xfff2f2f4 };
const juce::Colour dim      { 0xff8c8c93 };
}

class NeonLook : public juce::LookAndFeel_V4
{
public:
    NeonLook();
    void drawButtonBackground (juce::Graphics&, juce::Button&, const juce::Colour&, bool over, bool down) override;
    void drawButtonText (juce::Graphics&, juce::TextButton&, bool over, bool down) override;
    void drawComboBox (juce::Graphics&, int w, int h, bool down, int bx, int by, int bw, int bh, juce::ComboBox&) override;
    void positionComboBoxText (juce::ComboBox&, juce::Label&) override;
    juce::Font getComboBoxFont (juce::ComboBox&) override;
    void drawRotarySlider (juce::Graphics&, int x, int y, int w, int h, float pos, float start, float end, juce::Slider&) override;
    void drawPopupMenuBackground (juce::Graphics&, int w, int h) override;
};

// Draws notes on a bar grid. Shared by the source pane and the lanes.
class PianoRoll : public juce::Component
{
public:
    // styles (optional, one per note): 0 normal, 1 out of key, 2 fixed, 3 fixed + repeat
    void setNotes (std::vector<mc::Note> n, double lengthBeats, int beatsPerBar, int lowPitch, int highPitch,
                   std::vector<int> styles = {});
    void setPlayhead (double pos01) { playhead = pos01; repaint(); }
    void setHighlighted (bool h)    { highlighted = h; repaint(); }
    void paint (juce::Graphics&) override;
private:
    std::vector<mc::Note> notes;
    std::vector<int> styles;
    double lengthBeats = 4.0;
    int beatsPerBar = 4, low = 36, high = 48;
    double playhead = -1.0;
    bool highlighted = false;
};

class IconButton : public juce::Button
{
public:
    enum class Kind { play, lock, similar };
    IconButton (Kind k) : juce::Button (k == Kind::play ? "Preview" : (k == Kind::lock ? "Lock" : "More like this")), kind (k) { setClickingTogglesState (false); }
    void paintButton (juce::Graphics&, bool over, bool down) override;
private:
    Kind kind;
};

class SourcePane : public juce::Component, public juce::FileDragAndDropTarget, public juce::SettableTooltipClient
{
public:
    explicit SourcePane (MidiCloneProcessor&);
    void refresh();
    void updateState (int requestedPreview, int activePreview, double playPos);
    void paint (juce::Graphics&) override;
    void resized() override;
    bool isInterestedInFileDrag (const juce::StringArray&) override;
    void fileDragEnter (const juce::StringArray&, int, int) override { dragOver = true; repaint(); }
    void fileDragExit (const juce::StringArray&) override              { dragOver = false; repaint(); }
    void filesDropped (const juce::StringArray&, int, int) override;
    void mouseMove (const juce::MouseEvent&) override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    std::function<void (const juce::String&)> onStatus;
private:
    void load (const juce::File&);
    void rebuildKeyMenu();
    void syncKeyControls();
    MidiCloneProcessor& proc;
    juce::TextButton browse { "Browse" }, fixButton { "FIX KEY" };
    juce::ComboBox keyBox, moveBox;
    IconButton preview { IconButton::Kind::play };
    PianoRoll roll;
    std::unique_ptr<juce::FileChooser> chooser;
    bool dragOver = false, dragStarted = false;
    juce::String summary, keyStatus;
    bool keyStatusWarn = false;
    int keyMenuVersion = -1;
    std::vector<mc::Key> bestKeys;
    juce::Rectangle<int> keyRow;
};

class LaneComponent : public juce::Component
{
public:
    LaneComponent (MidiCloneProcessor&, int index);
    void setLane (const mc::Lane&, const mc::Analysis&);
    void updateState (int requestedPreview, int activePreview, double playPos);
    void paint (juce::Graphics&) override;
    void resized() override;
    void mouseDown (const juce::MouseEvent&) override;
    void mouseDrag (const juce::MouseEvent&) override;
    void mouseUp (const juce::MouseEvent&) override;
private:
    MidiCloneProcessor& proc;
    int index;
    PianoRoll roll;
    IconButton preview { IconButton::Kind::play }, lock { IconButton::Kind::lock }, similar { IconButton::Kind::similar };
    bool empty = true, dragStarted = false;
    double overlapValue = 0.0;
    int newNotes = 0;
};

class MidiCloneEditor : public juce::AudioProcessorEditor, private juce::Timer
{
public:
    explicit MidiCloneEditor (MidiCloneProcessor&);
    ~MidiCloneEditor() override;
    void paint (juce::Graphics&) override;
    void resized() override;
private:
    void timerCallback() override;
    void refreshAll();
    void setStatus (const juce::String& s, bool isError = false);
    void refreshPresets();
    void askPresetName();

    MidiCloneProcessor& proc;
    NeonLook look;

    juce::TooltipWindow tooltips { this, 500 };
    juce::ComboBox typeBox, rhythmBox, modeBox;
    juce::TextButton shapeButton { "KEEP SHAPE" };
    juce::Slider slideKnob, variationKnob, newNotesKnob;
    juce::TextButton generateButton { "GENERATE" }, undoButton { "UNDO" }, savePresetButton { "SAVE" };
    juce::ComboBox presetBox, matchBox;
    std::unique_ptr<juce::AlertWindow> presetDialog;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ComboBoxAttachment> rhythmAtt, modeAtt, matchAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::ButtonAttachment> shapeAtt;
    std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> slideAtt, variationAtt, newNotesAtt;

    SourcePane source;
    std::vector<std::unique_ptr<LaneComponent>> laneComps;

    juce::String status;
    bool statusIsError = false;
    int lastChange = -1;
    juce::Rectangle<int> topBar, labelsRow, row2, lanesArea, footer;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (MidiCloneEditor)
};
