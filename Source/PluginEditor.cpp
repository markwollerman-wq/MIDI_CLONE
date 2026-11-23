#include "PluginEditor.h"

namespace
{
juce::Font font (float size, bool bold = false)
{
    return juce::Font (juce::FontOptions (size).withStyle (bold ? "Bold" : "Regular"));
}

// Soft neon glow: a few wide, faint outlines under a crisp one
void glowOutline (juce::Graphics& g, juce::Rectangle<float> r, float radius, juce::Colour c, float strength = 1.0f)
{
    for (int i = 4; i >= 1; --i)
    {
        g.setColour (c.withAlpha (0.055f * strength * (5 - i)));
        g.drawRoundedRectangle (r.expanded (i * 1.4f), radius + i * 1.4f, 2.2f);
    }
    g.setColour (c);
    g.drawRoundedRectangle (r, radius, 1.3f);
}

// Glossy black panel: slight top-to-bottom shine and a thin highlight on the top edge
void glossPanel (juce::Graphics& g, juce::Rectangle<float> r, float radius)
{
    g.setGradientFill (juce::ColourGradient (Pal::panelHi, r.getX(), r.getY(), Pal::panel, r.getX(), r.getBottom(), false));
    g.fillRoundedRectangle (r, radius);
    g.setColour (juce::Colours::white.withAlpha (0.06f));
    g.drawLine (r.getX() + radius, r.getY() + 0.8f, r.getRight() - radius, r.getY() + 0.8f, 1.0f);
}

juce::String percent (double share)
{
    auto s = juce::String (share * 100.0, 1);
    if (s.endsWith (".0")) s = s.dropLastCharacters (2);
    return s + "%";
}

const char* typeName (mc::MidiType t)
{
    switch (t)
    {
        case mc::MidiType::bass:   return "Bass";
        case mc::MidiType::melody: return "Melody / Lead";
        case mc::MidiType::arp:    return "Arp / Pluck";
    }
    return "";
}
} // namespace

// ===========================================================================
NeonLook::NeonLook()
{
    setColour (juce::ComboBox::textColourId, Pal::text);
    setColour (juce::ComboBox::arrowColourId, Pal::red);
    setColour (juce::PopupMenu::backgroundColourId, Pal::panel);
    setColour (juce::PopupMenu::textColourId, Pal::text);
    setColour (juce::PopupMenu::highlightedBackgroundColourId, Pal::redDim);
    setColour (juce::PopupMenu::highlightedTextColourId, Pal::text);
    setColour (juce::Slider::textBoxTextColourId, Pal::dim);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::TooltipWindow::backgroundColourId, Pal::panel);
    setColour (juce::TooltipWindow::textColourId, Pal::text);
    setColour (juce::TooltipWindow::outlineColourId, Pal::red);
}

void NeonLook::drawButtonBackground (juce::Graphics& g, juce::Button& b, const juce::Colour&, bool over, bool down)
{
    auto r = b.getLocalBounds().toFloat().reduced (3.0f);
    const bool primary = b.getComponentID() == "generate";
    if (primary)
    {
        auto fill = down ? Pal::redSoft : (over ? Pal::red.brighter (0.15f) : Pal::red);
        glowOutline (g, r, 6.0f, Pal::red, over ? 1.6f : 1.1f);
        g.setColour (fill);
        g.fillRoundedRectangle (r, 6.0f);
        g.setColour (juce::Colours::white.withAlpha (0.18f));
        g.fillRoundedRectangle (r.withHeight (r.getHeight() * 0.45f).reduced (2.0f, 1.5f), 5.0f);
    }
    else
    {
        glossPanel (g, r, 5.0f);
        if (over) glowOutline (g, r, 5.0f, Pal::red, 0.7f);
        else { g.setColour (Pal::redDim); g.drawRoundedRectangle (r, 5.0f, 1.0f); }
    }
}

void NeonLook::drawButtonText (juce::Graphics& g, juce::TextButton& b, bool, bool)
{
    const bool primary = b.getComponentID() == "generate";
    g.setColour (primary ? juce::Colour (0xff120205) : Pal::redSoft);
    g.setFont (font (primary ? 17.0f : 13.0f, true));
    g.drawText (b.getButtonText(), b.getLocalBounds(), juce::Justification::centred);
}

void NeonLook::drawComboBox (juce::Graphics& g, int w, int h, bool, int, int, int, int, juce::ComboBox& box)
{
    auto r = juce::Rectangle<float> (0, 0, (float) w, (float) h).reduced (1.5f);
    glossPanel (g, r, 5.0f);
    if (box.isMouseOver (true) || box.isPopupActive()) glowOutline (g, r, 5.0f, Pal::red, 0.8f);
    else { g.setColour (Pal::redDim); g.drawRoundedRectangle (r, 5.0f, 1.0f); }

    juce::Path arrow;
    const float ax = (float) w - 16.0f, ay = h * 0.5f;
    arrow.addTriangle (ax - 4.0f, ay - 2.0f, ax + 4.0f, ay - 2.0f, ax, ay + 3.0f);
    g.setColour (Pal::red);
    g.fillPath (arrow);
}

void NeonLook::positionComboBoxText (juce::ComboBox& box, juce::Label& label)
{
    label.setBounds (8, 0, box.getWidth() - 28, box.getHeight());
    label.setFont (getComboBoxFont (box));
}

juce::Font NeonLook::getComboBoxFont (juce::ComboBox&) { return font (14.0f); }

void NeonLook::drawRotarySlider (juce::Graphics& g, int x, int y, int w, int h, float pos, float start, float end, juce::Slider& s)
{
    auto r = juce::Rectangle<float> ((float) x, (float) y, (float) w, (float) h).reduced (4.0f);
    const float size = juce::jmin (r.getWidth(), r.getHeight());
    auto c = r.withSizeKeepingCentre (size, size);
    const float angle = start + pos * (end - start);
    const auto accent = s.isEnabled() ? Pal::red : Pal::redDim;

    g.setColour (Pal::panelHi);
    g.fillEllipse (c.reduced (5.0f));

    juce::Path track, value;
    track.addCentredArc (c.getCentreX(), c.getCentreY(), size / 2 - 2, size / 2 - 2, 0, start, end, true);
    value.addCentredArc (c.getCentreX(), c.getCentreY(), size / 2 - 2, size / 2 - 2, 0, start, angle, true);
    g.setColour (Pal::redDeep.brighter (0.4f));
    g.strokePath (track, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    if (s.isEnabled())
    {
        g.setColour (accent.withAlpha (0.25f));
        g.strokePath (value, juce::PathStrokeType (7.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));
    }
    g.setColour (accent);
    g.strokePath (value, juce::PathStrokeType (3.0f, juce::PathStrokeType::curved, juce::PathStrokeType::rounded));

    const float px = c.getCentreX() + std::sin (angle) * (size / 2 - 9);
    const float py = c.getCentreY() - std::cos (angle) * (size / 2 - 9);
    g.drawLine (c.getCentreX(), c.getCentreY(), px, py, 2.0f);
}

void NeonLook::drawPopupMenuBackground (juce::Graphics& g, int w, int h)
{
    g.fillAll (Pal::panel);
    g.setColour (Pal::red);
    g.drawRect (0, 0, w, h, 1);
}

// ===========================================================================
void PianoRoll::setNotes (std::vector<mc::Note> n, double len, int bpb, int lowPitch, int highPitch)
{
    notes = std::move (n);
    lengthBeats = juce::jmax (1.0, len);
    beatsPerBar = juce::jmax (1, bpb);
    low = lowPitch;
    high = juce::jmax (lowPitch, highPitch);
    repaint();
}

void PianoRoll::paint (juce::Graphics& g)
{
    auto b = getLocalBounds().toFloat();
    g.setColour (juce::Colour (0xff09090b));
    g.fillRoundedRectangle (b, 4.0f);

    // beat and bar lines
    const int beats = (int) std::lround (lengthBeats);
    for (int i = 1; i < beats; ++i)
    {
        const float x = b.getX() + b.getWidth() * (float) i / (float) beats;
        const bool bar = i % beatsPerBar == 0;
        g.setColour (bar ? Pal::red.withAlpha (0.28f) : juce::Colours::white.withAlpha (0.05f));
        g.drawVerticalLine ((int) x, b.getY() + 2, b.getBottom() - 2);
    }

    const int rows = high - low + 1;
    const float inner = b.getHeight() - 8.0f;
    const float rowH = inner / (float) rows;
    const float noteH = juce::jlimit (3.0f, 9.0f, rowH * 0.8f);

    for (auto& n : notes)
    {
        const float x = b.getX() + (float) (n.start / lengthBeats) * b.getWidth();
        const float w = juce::jmax (2.0f, (float) (n.length / lengthBeats) * b.getWidth() - 1.0f);
        const float yCentre = b.getBottom() - 4.0f - ((float) (n.pitch - low) + 0.5f) * rowH;
        juce::Rectangle<float> r (x, yCentre - noteH / 2, w, noteH);
        g.setColour (Pal::red.withAlpha (0.22f));
        g.fillRoundedRectangle (r.expanded (1.8f), 3.0f);
        g.setColour (Pal::red);
        g.fillRoundedRectangle (r, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (r.withHeight (1.0f).reduced (1.0f, 0.0f));
    }

    if (playhead >= 0.0)
    {
        const float x = b.getX() + (float) playhead * b.getWidth();
        g.setColour (Pal::redSoft.withAlpha (0.25f));
        g.fillRect (x - 2.0f, b.getY(), 4.0f, b.getHeight());
        g.setColour (Pal::text);
        g.drawVerticalLine ((int) x, b.getY(), b.getBottom());
    }

    if (highlighted)
    {
        g.setColour (Pal::red);
        g.drawRoundedRectangle (b.reduced (0.5f), 4.0f, 1.0f);
    }
}

// ===========================================================================
void IconButton::paintButton (juce::Graphics& g, bool over, bool down)
{
    auto r = getLocalBounds().toFloat().reduced (3.0f);
    const bool on = getToggleState();
    if (on)
    {
        glowOutline (g, r, 5.0f, Pal::red, 1.3f);
        g.setColour (down ? Pal::redSoft : Pal::red);
        g.fillRoundedRectangle (r, 5.0f);
    }
    else
    {
        glossPanel (g, r, 5.0f);
        g.setColour (over ? Pal::red : Pal::redDim);
        g.drawRoundedRectangle (r, 5.0f, 1.0f);
    }

    const auto ink = on ? juce::Colour (0xff120205) : (over ? Pal::red : Pal::redSoft);
    g.setColour (ink);
    auto c = r.reduced (r.getWidth() * 0.28f);

    if (kind == Kind::play)
    {
        juce::Path p;
        p.addTriangle (c.getX() + 1, c.getY(), c.getX() + 1, c.getBottom(), c.getRight(), c.getCentreY());
        g.fillPath (p);
    }
    else
    {
        auto body = c.withTrimmedTop (c.getHeight() * 0.45f);
        g.fillRoundedRectangle (body, 1.5f);
        const float sw = body.getWidth() * 0.6f;               // shackle width
        const float sx = body.getCentreX() - sw / 2;
        const float lift = on ? 0.0f : c.getHeight() * 0.14f;   // raised when unlocked
        const float topY = c.getY() + sw / 2 - lift;
        juce::Path shackle;
        shackle.startNewSubPath (sx, body.getY() - lift);
        shackle.lineTo (sx, topY);
        shackle.addCentredArc (body.getCentreX(), topY, sw / 2, sw / 2, 0,
                               -juce::MathConstants<float>::halfPi, juce::MathConstants<float>::halfPi, false);
        shackle.lineTo (sx + sw, on ? body.getY() : topY + sw * 0.25f);
        g.strokePath (shackle, juce::PathStrokeType (1.8f));
    }
}

// ===========================================================================
SourcePane::SourcePane (MidiCloneProcessor& p) : proc (p)
{
    addAndMakeVisible (browse);
    addAndMakeVisible (roll);
    roll.setInterceptsMouseClicks (false, false);
    browse.onClick = [this]
    {
        chooser = std::make_unique<juce::FileChooser> ("Choose a MIDI clip",
                      juce::File::getSpecialLocation (juce::File::userMusicDirectory), "*.mid;*.midi");
        chooser->launchAsync (juce::FileBrowserComponent::openMode | juce::FileBrowserComponent::canSelectFiles,
                              [this] (const juce::FileChooser& fc)
                              {
                                  auto f = fc.getResult();
                                  if (f.existsAsFile()) load (f);
                              });
    };
}

bool SourcePane::isInterestedInFileDrag (const juce::StringArray& files)
{
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
            return true;
    return false;
}

void SourcePane::filesDropped (const juce::StringArray& files, int, int)
{
    dragOver = false;
    for (auto& f : files)
        if (f.endsWithIgnoreCase (".mid") || f.endsWithIgnoreCase (".midi"))
        {
            load (juce::File (f));
            break;
        }
    repaint();
}

void SourcePane::load (const juce::File& f)
{
    const auto err = proc.loadMidiFile (f);
    if (onStatus) onStatus (err.isEmpty() ? "Loaded " + f.getFileName() + ". Set your options and press Generate." : err);
    refresh();
}

void SourcePane::refresh()
{
    const auto a = proc.getAnalysis();
    if (! a.valid)
    {
        roll.setVisible (false);
        summary.clear();
        repaint();
        return;
    }
    roll.setVisible (true);
    roll.setNotes (a.notes, a.lengthBeats, a.beatsPerBar, a.lowest - 1, a.highest + 1);

    juce::StringArray parts;
    for (auto& s : a.shares)
        parts.add (juce::String (mc::noteNameWithOctave (s.pitch)) + " " + percent (s.share));
    summary = parts.joinIntoString ("   ") + "      |      " + juce::String (a.bars) + (a.bars == 1 ? " bar" : " bars")
              + "  -  " + juce::String ((int) a.notes.size()) + " notes  -  looks like: " + typeName (a.guessedType);
    if (a.hadChords) summary += "  -  chords: lowest note used";
    repaint();
}

void SourcePane::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    glossPanel (g, r, 8.0f);

    if (dragOver) glowOutline (g, r, 8.0f, Pal::red, 1.5f);
    else if (! proc.hasSource())
    {
        g.setColour (Pal::redDim.brighter (0.3f));
        const float dash[] = { 6.0f, 5.0f };
        juce::Path p;
        p.addRoundedRectangle (r.reduced (1.0f), 8.0f);
        juce::Path dashed;
        juce::PathStrokeType (1.2f).createDashedStroke (dashed, p, dash, 2);
        g.fillPath (dashed);
    }
    else { g.setColour (Pal::redDim); g.drawRoundedRectangle (r, 8.0f, 1.0f); }

    if (! proc.hasSource())
    {
        g.setColour (Pal::text);
        g.setFont (font (20.0f, true));
        g.drawText ("Drop a MIDI clip here", getLocalBounds().withTrimmedBottom (24), juce::Justification::centred);
        g.setColour (Pal::dim);
        g.setFont (font (13.0f));
        g.drawText ("or click Browse  -  bass, melody or arp", getLocalBounds().withTrimmedTop (34), juce::Justification::centred);
        return;
    }

    auto top = getLocalBounds().reduced (14, 8).removeFromTop (22);
    g.setColour (Pal::red);
    g.setFont (font (14.0f, true));
    g.drawText ("SOURCE", top.removeFromLeft (70), juce::Justification::centredLeft);
    g.setColour (Pal::text);
    g.drawText (proc.getSourceName(), top.withTrimmedRight (100), juce::Justification::centredLeft);

    g.setColour (Pal::dim);
    g.setFont (font (13.0f));
    g.drawText (summary, getLocalBounds().reduced (14, 8).removeFromBottom (18), juce::Justification::centredLeft);
}

void SourcePane::resized()
{
    auto r = getLocalBounds().reduced (14, 8);
    if (proc.hasSource())
        browse.setBounds (r.getRight() - 92, r.getY(), 92, 24);
    else
        browse.setBounds (getWidth() / 2 - 50, getHeight() / 2 + 32, 100, 28);
    r.removeFromTop (28);
    r.removeFromBottom (22);
    roll.setBounds (r);
}

// ===========================================================================
LaneComponent::LaneComponent (MidiCloneProcessor& p, int i) : proc (p), index (i)
{
    addAndMakeVisible (roll);
    addAndMakeVisible (preview);
    addAndMakeVisible (lock);
    roll.setInterceptsMouseClicks (false, false);
    preview.setTooltip ("Preview: send this lane's MIDI to your synth track");
    lock.setTooltip ("Lock: keep this lane when you press Generate");
    setMouseCursor (juce::MouseCursor::DraggingHandCursor);
    preview.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    lock.setMouseCursor (juce::MouseCursor::PointingHandCursor);

    preview.onClick = [this]
    {
        if (empty) return;
        proc.setPreview (proc.getRequestedPreview() == index ? -1 : index);
    };
    lock.onClick = [this]
    {
        if (empty) return;
        const bool now = ! lock.getToggleState();
        lock.setToggleState (now, juce::dontSendNotification);
        proc.setLocked (index, now);
    };
}

void LaneComponent::setLane (const mc::Lane& lane, const mc::Analysis& a)
{
    empty = lane.empty || ! a.valid;
    overlapValue = lane.overlapWithSource;
    lock.setToggleState (lane.locked && ! empty, juce::dontSendNotification);
    roll.setNotes (empty ? std::vector<mc::Note>() : lane.notes, a.lengthBeats, a.beatsPerBar, a.lowest - 1, a.highest + 1);
    preview.setEnabled (! empty);
    lock.setEnabled (! empty);
    repaint();
}

void LaneComponent::updateState (int requested, int active, double playPos)
{
    preview.setToggleState (requested == index, juce::dontSendNotification);
    roll.setHighlighted (requested == index);
    roll.setPlayhead (active == index ? playPos : -1.0);
}

void LaneComponent::paint (juce::Graphics& g)
{
    auto r = getLocalBounds().toFloat().reduced (2.0f);
    glossPanel (g, r, 7.0f);
    const bool previewing = proc.getRequestedPreview() == index;
    if (previewing) glowOutline (g, r, 7.0f, Pal::red, 1.2f);
    else { g.setColour (lock.getToggleState() ? Pal::red.withAlpha (0.6f) : Pal::redDim); g.drawRoundedRectangle (r, 7.0f, 1.0f); }

    g.setColour (previewing ? Pal::red : Pal::dim);
    g.setFont (font (18.0f, true));
    g.drawText (juce::String (index + 1), getLocalBounds().removeFromLeft (34), juce::Justification::centred);

    if (empty)
    {
        g.setColour (Pal::dim.withAlpha (0.6f));
        g.setFont (font (12.0f));
        g.drawText ("Press Generate", roll.getBounds(), juce::Justification::centred);
    }
    else
    {
        g.setColour (Pal::dim);
        g.setFont (font (11.0f));
        auto info = getLocalBounds().removeFromRight (76).removeFromBottom (20).translated (0, -4);
        g.drawText (juce::String (juce::roundToInt (overlapValue * 100)) + "% like source", info, juce::Justification::centred);
    }
}

void LaneComponent::resized()
{
    auto r = getLocalBounds().reduced (6);
    r.removeFromLeft (28);
    auto right = r.removeFromRight (70);
    auto buttons = right.withSizeKeepingCentre (70, 32).translated (0, -8);
    preview.setBounds (buttons.removeFromLeft (35));
    lock.setBounds (buttons);
    roll.setBounds (r.reduced (2, 2));
}

void LaneComponent::mouseDown (const juce::MouseEvent&) { dragStarted = false; }

void LaneComponent::mouseDrag (const juce::MouseEvent& e)
{
    if (empty || dragStarted || e.getDistanceFromDragStart() < 6) return;
    dragStarted = true;
    const auto file = proc.writeLaneToTempFile (index);
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void LaneComponent::mouseUp (const juce::MouseEvent&)
{
    // A click (no drag) on the lane toggles preview too
    if (! dragStarted && ! empty)
        proc.setPreview (proc.getRequestedPreview() == index ? -1 : index);
    dragStarted = false;
}

// ===========================================================================
MidiCloneEditor::MidiCloneEditor (MidiCloneProcessor& p)
    : AudioProcessorEditor (&p), proc (p), source (p)
{
    setLookAndFeel (&look);

    typeBox.addItemList ({ "Bass", "Melody / Lead", "Arp / Pluck" }, 1);
    rhythmBox.addItemList ({ "Locked", "Same Feel" }, 1);
    modeBox.addItemList ({ "Grid Snapped", "Slide" }, 1);
    for (auto* b : { &typeBox, &rhythmBox, &modeBox }) addAndMakeVisible (*b);

    typeBox.setTooltip ("What kind of part this is. Each type sets its own rhythm default.");
    rhythmBox.setTooltip ("Locked: your exact rhythm, notes reshuffled.  Same Feel: new rhythm, same note lengths.");
    modeBox.setTooltip ("Grid Snapped: clean steps.  Slide: some notes overlap so your synth glides.");

    // The type box is handled by hand so only a user's pick changes the rhythm default
    typeBox.setSelectedItemIndex ((int) proc.params.getRawParameterValue ("type")->load(), juce::dontSendNotification);
    typeBox.onChange = [this]
    {
        const int idx = typeBox.getSelectedItemIndex();
        if (auto* prm = proc.params.getParameter ("type"))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) idx));
        if (auto* prm = proc.params.getParameter ("rhythm"))
            prm->setValueNotifyingHost (prm->convertTo0to1 ((float) (int) mc::defaultRhythmFor ((mc::MidiType) idx)));
    };

    rhythmAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "rhythm", rhythmBox);
    modeAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "mode", modeBox);

    for (auto* s : { &slideKnob, &variationKnob })
    {
        s->setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
        s->setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
        s->setColour (juce::Slider::textBoxTextColourId, Pal::redSoft);
        s->setColour (juce::Slider::textBoxHighlightColourId, Pal::redDim);
        s->setTextBoxStyle (juce::Slider::TextBoxRight, false, 38, 18);
        addAndMakeVisible (*s);
    }
    slideKnob.setTooltip ("Slide amount: how many notes glide into the next one");
    variationKnob.setTooltip ("Variation: how far the clones move away from your clip");
    slideAtt     = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "slide", slideKnob);
    variationAtt = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "variation", variationKnob);
    slideKnob.setTextValueSuffix ("%");

    generateButton.setComponentID ("generate");
    generateButton.setTooltip ("Make 10 new clips. Locked lanes are kept.");
    generateButton.onClick = [this]
    {
        if (! proc.hasSource()) { setStatus ("Load a MIDI clip first.", true); return; }
        proc.generate();
        setStatus ("10 clones ready. Click a lane to preview it, then hold and drag it into Ableton.");
    };
    addAndMakeVisible (generateButton);

    source.onStatus = [this] (const juce::String& s) { setStatus (s, ! s.startsWith ("Loaded")); };
    addAndMakeVisible (source);

    for (int i = 0; i < mc::numLanes; ++i)
    {
        laneComps.push_back (std::make_unique<LaneComponent> (proc, i));
        addAndMakeVisible (*laneComps.back());
    }

    setResizable (true, true);
    setResizeLimits (880, 660, 1700, 1200);
    setSize (1000, 760);
    refreshAll();
    startTimerHz (30);
}

MidiCloneEditor::~MidiCloneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MidiCloneEditor::setStatus (const juce::String& s, bool isError)
{
    status = s;
    statusIsError = isError;
    repaint (footer);
}

void MidiCloneEditor::refreshAll()
{
    source.refresh();
    source.resized();
    const auto a = proc.getAnalysis();
    const auto lanes = proc.getLanes();
    for (int i = 0; i < mc::numLanes; ++i)
        laneComps[(size_t) i]->setLane (lanes[(size_t) i], a);
    lastChange = proc.changeCounter.load();
    repaint();
}

void MidiCloneEditor::timerCallback()
{
    if (proc.changeCounter.load() != lastChange) refreshAll();

    const int typeIdx = (int) proc.params.getRawParameterValue ("type")->load();
    if (! typeBox.isPopupActive() && typeBox.getSelectedItemIndex() != typeIdx)
        typeBox.setSelectedItemIndex (typeIdx, juce::dontSendNotification);
    slideKnob.setEnabled (proc.params.getRawParameterValue ("mode")->load() > 0.5f);

    const int req = proc.getRequestedPreview(), act = proc.getActivePreview();
    const double pos = proc.getPlayPosition();
    for (auto& l : laneComps) l->updateState (req, act, pos);
}

void MidiCloneEditor::paint (juce::Graphics& g)
{
    g.fillAll (Pal::bg);

    auto bar = topBar.toFloat();
    glossPanel (g, bar, 9.0f);
    glowOutline (g, bar, 9.0f, Pal::red, 0.6f);

    // Title with a neon glow
    auto title = topBar.reduced (16, 0).removeFromLeft (190);
    g.setFont (font (26.0f, true));
    for (int i = 3; i >= 1; --i)
    {
        g.setColour (Pal::red.withAlpha (0.12f));
        g.drawText ("MIDI CLONE", title.translated (0, -7).expanded (i, i), juce::Justification::centredLeft);
    }
    g.setColour (Pal::red);
    g.drawText ("MIDI CLONE", title.translated (0, -7), juce::Justification::centredLeft);
    g.setColour (Pal::dim);
    g.setFont (font (11.0f, true));
    g.drawText ("W O L L E R", title.translated (1, 17), juce::Justification::centredLeft);

    // Labels above the controls
    g.setFont (font (11.0f, true));
    g.setColour (Pal::dim);
    auto label = [&] (juce::Component& c, const juce::String& t)
    { g.drawText (t, c.getX(), labelsRow.getY(), c.getWidth() + 20, labelsRow.getHeight(), juce::Justification::centredLeft); };
    label (typeBox, "MIDI TYPE");
    label (rhythmBox, "RHYTHM");
    label (modeBox, "ENGINE");
    label (slideKnob, "SLIDE");
    label (variationKnob, "VARIATION");

    // Footer: how-to on the left, status on the right
    g.setFont (font (12.0f));
    g.setColour (Pal::dim);
    g.drawText ("Synth track: MIDI From = MIDI Clone, Monitor = In   -   Click a lane to preview   -   Hold and drag a lane into Ableton to keep it",
                footer, juce::Justification::centredLeft);
    if (status.isNotEmpty())
    {
        g.setColour (Pal::bg);
        auto f = footer;
        auto sr = f.removeFromRight (juce::jmin (f.getWidth(), (int) juce::GlyphArrangement::getStringWidth (font (12.0f), status) + 30));
        g.fillRect (sr);
        g.setColour (statusIsError ? Pal::redSoft : Pal::text);
        g.drawText (status, sr, juce::Justification::centredRight);
    }
}

void MidiCloneEditor::resized()
{
    auto r = getLocalBounds().reduced (14);
    topBar = r.removeFromTop (82);

    auto bar = topBar.reduced (16, 12);
    bar.removeFromLeft (190);
    generateButton.setBounds (bar.removeFromRight (170).withSizeKeepingCentre (170, 50));
    bar.removeFromRight (16);

    labelsRow = bar.removeFromTop (16);
    auto controls = bar.withTrimmedTop (4);
    auto place = [&] (juce::Component& c, int w)
    {
        c.setBounds (controls.removeFromLeft (w).withSizeKeepingCentre (w, 32));
        controls.removeFromLeft (12);
    };
    const int comboW = juce::jlimit (110, 160, (controls.getWidth() - 3 * 12 - 2 * 96) / 3);
    place (typeBox, comboW);
    place (rhythmBox, comboW);
    place (modeBox, comboW);
    slideKnob.setBounds (controls.removeFromLeft (92).withSizeKeepingCentre (92, 40));
    controls.removeFromLeft (8);
    variationKnob.setBounds (controls.removeFromLeft (92).withSizeKeepingCentre (92, 40));

    r.removeFromTop (12);
    source.setBounds (r.removeFromTop (160));
    r.removeFromTop (12);
    footer = r.removeFromBottom (22);
    r.removeFromBottom (6);
    lanesArea = r;

    const int gap = 10;
    const int colW = (lanesArea.getWidth() - gap) / 2;
    const int rowH = (lanesArea.getHeight() - 4 * 8) / 5;
    for (int i = 0; i < mc::numLanes; ++i)
    {
        const int col = i < 5 ? 0 : 1;
        const int row = i % 5;
        laneComps[(size_t) i]->setBounds (lanesArea.getX() + col * (colW + gap), lanesArea.getY() + row * (rowH + 8), colW, rowH);
    }
}
