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
        case mc::MidiType::stab:   return "Stab / Riff";
        case mc::MidiType::chords: return "Chords";
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
    setColour (juce::AlertWindow::backgroundColourId, Pal::panel);
    setColour (juce::AlertWindow::textColourId, Pal::text);
    setColour (juce::AlertWindow::outlineColourId, Pal::red);
    setColour (juce::TextEditor::backgroundColourId, Pal::bg);
    setColour (juce::TextEditor::textColourId, Pal::text);
    setColour (juce::TextEditor::outlineColourId, Pal::redDim);
    setColour (juce::TextEditor::focusedOutlineColourId, Pal::red);
    setColour (juce::TextEditor::highlightColourId, Pal::redDim);
    setColour (juce::CaretComponent::caretColourId, Pal::red);
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
    else if (b.getClickingTogglesState() && b.getToggleState())
    {
        glowOutline (g, r, 5.0f, Pal::red, over ? 1.4f : 1.0f);
        g.setColour (Pal::redDim);
        g.fillRoundedRectangle (r, 5.0f);
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
    const bool lit = b.getClickingTogglesState() && b.getToggleState();
    g.setColour (primary ? juce::Colour (0xff120205) : (lit ? Pal::text : (b.isEnabled() ? Pal::redSoft : Pal::dim)));
    g.setFont (font (primary ? 17.0f : 12.5f, true));
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
void PianoRoll::setNotes (std::vector<mc::Note> n, double len, int bpb, int lowPitch, int highPitch, std::vector<int> st)
{
    notes = std::move (n);
    styles = std::move (st);
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

    for (size_t i = 0; i < notes.size(); ++i)
    {
        const auto& n = notes[i];
        const int style = i < styles.size() ? styles[i] : 0;
        const float x = b.getX() + (float) (n.start / lengthBeats) * b.getWidth();
        const float w = juce::jmax (2.0f, (float) (n.length / lengthBeats) * b.getWidth() - 1.0f);
        const float yCentre = b.getBottom() - 4.0f - ((float) (n.pitch - low) + 0.5f) * rowH;
        juce::Rectangle<float> r (x, yCentre - noteH / 2, w, noteH);
        if (style == 1)
        {
            // out of key: hollow white note
            g.setColour (juce::Colours::white.withAlpha (0.15f));
            g.fillRoundedRectangle (r.expanded (1.8f), 3.0f);
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r, 1.5f, 1.4f);
            continue;
        }
        g.setColour (Pal::red.withAlpha (0.22f));
        g.fillRoundedRectangle (r.expanded (1.8f), 3.0f);
        g.setColour (Pal::red);
        g.fillRoundedRectangle (r, 1.5f);
        g.setColour (juce::Colours::white.withAlpha (0.25f));
        g.fillRect (r.withHeight (1.0f).reduced (1.0f, 0.0f));
        if (style >= 2)
        {
            // moved by Fix Key: white outline (and a dot when it now repeats a neighbour)
            g.setColour (juce::Colours::white);
            g.drawRoundedRectangle (r.expanded (1.0f), 2.0f, 1.2f);
            if (style == 3) g.fillEllipse (r.getCentreX() - 2.0f, r.getY() - 7.0f, 4.0f, 4.0f);
        }
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
    else if (kind == Kind::similar)
    {
        // two overlapping squares: "more like this"
        auto back = c.withTrimmedRight (c.getWidth() * 0.3f).withTrimmedBottom (c.getHeight() * 0.3f);
        auto front = c.withTrimmedLeft (c.getWidth() * 0.3f).withTrimmedTop (c.getHeight() * 0.3f);
        g.drawRoundedRectangle (back, 1.5f, 1.5f);
        g.fillRoundedRectangle (front, 1.5f);
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
    for (auto* c : std::initializer_list<juce::Component*> { &browse, &roll, &keyBox, &moveBox, &fixButton, &preview })
        addAndMakeVisible (c);
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

    fixButton.setClickingTogglesState (true);
    fixButton.setTooltip ("Fix Key: moves out-of-key notes into the key (the way the line was heading). The clones are built from the fixed version; your file isn't changed.");
    fixButton.onClick = [this] { proc.setFix (fixButton.getToggleState()); };

    keyBox.setTooltip ("Key: the best match is picked for you. Choose another to see what it would change.");
    keyBox.onChange = [this]
    {
        const int id = keyBox.getSelectedId();
        if (id == 20000) proc.setAutoKey();
        else if (id >= 10000 && id - 10000 < (int) bestKeys.size()) proc.setKey (bestKeys[(size_t) (id - 10000)]);
        else if (id > 0 && id < 10000) proc.setKey ({ (id - 1) / 64, (id - 1) % 64 });
    };

    moveBox.addItem ("Same key", 100);
    for (int r = 0; r < 12; ++r) moveBox.addItem ("Move to " + juce::String (mc::noteName (r)), r + 1);
    moveBox.setTooltip ("Move To: shifts the source (and the clones) to a new home note, same scale.");
    moveBox.onChange = [this]
    {
        const int id = moveBox.getSelectedId();
        proc.setMoveTo (id >= 1 && id <= 12 ? id - 1 : -1);
    };

    preview.setTooltip ("Preview the source: sends it to your synth track, like the lanes");
    setTooltip ("Hold and drag the piano roll into Ableton to keep the source (fixed / moved if those are on)");
    preview.onClick = [this]
    {
        if (! proc.hasSource()) return;
        proc.setPreview (proc.getRequestedPreview() == MidiCloneProcessor::sourcePreview ? -1 : MidiCloneProcessor::sourcePreview);
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

void SourcePane::rebuildKeyMenu()
{
    const auto report = proc.getKeyReport();
    const auto v = proc.getSourceView();
    keyBox.clear (juce::dontSendNotification);
    bestKeys.clear();

    auto* menu = keyBox.getRootMenu();
    const auto autoKey = report.ranked.empty() ? mc::Key {} : report.ranked.front().key;
    keyBox.addItem ("Auto: " + juce::String (mc::keyName (autoKey)), 20000);
    menu->addSectionHeader ("Best matches");
    for (size_t i = 0; i < report.ranked.size() && i < 6; ++i)
    {
        const auto& c = report.ranked[i];
        bestKeys.push_back (c.key);
        keyBox.addItem (juce::String (mc::keyName (c.key)) + "   -   "
                        + (c.offKey == 0 ? juce::String ("fits every note") : juce::String (c.offKey) + (c.offKey == 1 ? " change" : " changes")),
                        10000 + (int) i);
    }
    menu->addSectionHeader ("All keys");
    for (int r = 0; r < 12; ++r)
    {
        juce::PopupMenu sub;
        for (int sc = 0; sc < (int) mc::scales().size(); ++sc)
            sub.addItem (1 + r * 64 + sc, mc::keyName ({ r, sc }));
        menu->addSubMenu (mc::noteName (r), sub);
    }
    keyMenuVersion = proc.keyListVersion();
    juce::ignoreUnused (v);
}

void SourcePane::syncKeyControls()
{
    const auto v = proc.getSourceView();
    int id = 20000;
    if (! v.keyAuto)
    {
        id = 1 + v.key.root * 64 + v.key.scale;
        for (size_t i = 0; i < bestKeys.size(); ++i)
            if (bestKeys[i].root == v.key.root && bestKeys[i].scale == v.key.scale) id = 10000 + (int) i;
    }
    if (keyBox.getSelectedId() != id) keyBox.setSelectedId (id, juce::dontSendNotification);
    if (id < 10000 && ! v.keyAuto) keyBox.setText (mc::keyName (v.key), juce::dontSendNotification);

    const int moveId = v.moveTo >= 0 ? v.moveTo + 1 : 100;
    if (moveBox.getSelectedId() != moveId) moveBox.setSelectedId (moveId, juce::dontSendNotification);
    fixButton.setToggleState (v.fixOn, juce::dontSendNotification);
    fixButton.setButtonText (v.fixOn ? "FIXED" : "FIX KEY");

    if (v.key.scale == mc::chromaticScaleIndex())
        keyStatus = "Chromatic: every note counts as in key", keyStatusWarn = false;
    else if (v.offKey == 0)
        keyStatus = "All " + juce::String (v.total) + " notes are in key", keyStatusWarn = false;
    else if (v.fixOn)
    {
        keyStatus = juce::String (v.offKey) + (v.offKey == 1 ? " note moved" : " notes moved") + " into " + mc::keyName (v.key);
        if (v.repeats > 0) keyStatus += "  (" + juce::String (v.repeats) + (v.repeats == 1 ? " now repeats a neighbour: dot)" : " now repeat a neighbour: dots)");
        keyStatusWarn = false;
    }
    else
        keyStatus = "! " + juce::String (v.offKey) + " of " + juce::String (v.total) + " notes out of key (white)", keyStatusWarn = true;
}

void SourcePane::refresh()
{
    const auto a = proc.getAnalysis();
    const bool has = a.valid;
    for (auto* c : std::initializer_list<juce::Component*> { &roll, &keyBox, &moveBox, &fixButton, &preview })
        c->setVisible (has);
    if (! has)
    {
        summary.clear();
        repaint();
        return;
    }
    if (keyMenuVersion != proc.keyListVersion()) rebuildKeyMenu();
    syncKeyControls();

    const auto v = proc.getSourceView();
    roll.setNotes (v.notes, v.lengthBeats, v.beatsPerBar, v.low - 1, v.high + 1, v.styles);

    juce::StringArray parts;
    const bool chordMode = (int) proc.params.getRawParameterValue ("type")->load() == (int) mc::MidiType::chords;
    if (chordMode)
        for (auto& c : a.chordShares) parts.add (juce::String (c.name) + " " + percent (c.share));
    else
        for (auto& s : a.shares) parts.add (juce::String (mc::noteNameWithOctave (s.pitch)) + " " + percent (s.share));
    summary = parts.joinIntoString ("   ") + "      |      " + juce::String (a.bars) + (a.bars == 1 ? " bar" : " bars")
              + "  -  " + (chordMode ? juce::String ((int) a.chords.size()) + " chords" : juce::String ((int) a.notes.size()) + " notes")
              + "  -  looks like: " + typeName (a.guessedType);
    if (v.target.valid() && v.target.scale != mc::chromaticScaleIndex())
    {
        summary += "  -  " + juce::String (mc::keyName (v.target));
        const auto same = mc::sameNotesAs (v.target);
        if (! same.empty()) summary += " (same notes as " + juce::String (same) + ")";
    }
    if (a.hadChords && ! chordMode) summary += "  -  chords: lowest note used (try Chords)";
    repaint();
}

void SourcePane::mouseMove (const juce::MouseEvent& e)
{
    setMouseCursor (proc.hasSource() && roll.getBounds().contains (e.getPosition()) ? juce::MouseCursor::DraggingHandCursor
                                                                                    : juce::MouseCursor::NormalCursor);
}

void SourcePane::mouseDown (const juce::MouseEvent&) { dragStarted = false; }

void SourcePane::mouseDrag (const juce::MouseEvent& e)
{
    if (dragStarted || ! proc.hasSource() || e.getDistanceFromDragStart() < 6
        || ! roll.getBounds().contains (e.getMouseDownPosition())) return;
    dragStarted = true;
    const auto file = proc.writeSourceToTempFile();
    if (file.existsAsFile())
        juce::DragAndDropContainer::performExternalDragDropOfFiles ({ file.getFullPathName() }, false, this);
}

void SourcePane::updateState (int requested, int active, double playPos)
{
    const bool on = requested == MidiCloneProcessor::sourcePreview;
    preview.setToggleState (on, juce::dontSendNotification);
    roll.setHighlighted (on);
    roll.setPlayhead (active == MidiCloneProcessor::sourcePreview ? playPos : -1.0);
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
        g.drawText ("or click Browse  -  bass, melody, arp, stab or chords", getLocalBounds().withTrimmedTop (34), juce::Justification::centred);
        return;
    }

    auto top = getLocalBounds().reduced (14, 8).removeFromTop (24);
    g.setColour (Pal::red);
    g.setFont (font (14.0f, true));
    g.drawText ("SOURCE", top.removeFromLeft (70), juce::Justification::centredLeft);
    g.setColour (Pal::text);
    g.drawText (proc.getSourceName(), top.withTrimmedRight (150), juce::Justification::centredLeft);

    // key row: label and status text
    g.setFont (font (11.0f, true));
    g.setColour (Pal::dim);
    g.drawText ("KEY", keyRow.withWidth (34), juce::Justification::centredLeft);
    auto status = keyRow.withLeft (fixButton.getRight() + 12).withRight (moveBox.getX() - 12);
    g.setFont (font (13.0f, keyStatusWarn));
    g.setColour (keyStatusWarn ? Pal::redSoft : Pal::dim);
    g.drawText (keyStatus, status, juce::Justification::centredLeft);

    g.setColour (Pal::dim);
    g.setFont (font (13.0f));
    g.drawText (summary, getLocalBounds().reduced (14, 8).removeFromBottom (18), juce::Justification::centredLeft);
}

void SourcePane::resized()
{
    auto r = getLocalBounds().reduced (14, 8);
    if (proc.hasSource())
    {
        auto top = r.removeFromTop (24);
        browse.setBounds (top.removeFromRight (92));
        top.removeFromRight (8);
        preview.setBounds (top.removeFromRight (30).withSizeKeepingCentre (30, 28));
        r.removeFromTop (6);
        keyRow = r.removeFromTop (28);
        auto row = keyRow;
        row.removeFromLeft (36);
        keyBox.setBounds (row.removeFromLeft (250));
        row.removeFromLeft (8);
        fixButton.setBounds (row.removeFromLeft (96));
        moveBox.setBounds (row.removeFromRight (130));
        r.removeFromTop (6);
    }
    else
    {
        browse.setBounds (getWidth() / 2 - 50, getHeight() / 2 + 32, 100, 28);
        r.removeFromTop (60);
    }
    r.removeFromBottom (22);
    roll.setBounds (r);
}

// ===========================================================================
LaneComponent::LaneComponent (MidiCloneProcessor& p, int i) : proc (p), index (i)
{
    addAndMakeVisible (roll);
    addAndMakeVisible (preview);
    addAndMakeVisible (lock);
    addAndMakeVisible (similar);
    roll.setInterceptsMouseClicks (false, false);
    similar.setTooltip ("More like this: keep this lane and make the other unlocked lanes variations of it");
    similar.setMouseCursor (juce::MouseCursor::PointingHandCursor);
    similar.onClick = [this] { if (! empty) proc.moreLikeThis (index); };
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
    newNotes = lane.newNoteCount;
    lock.setToggleState (lane.locked && ! empty, juce::dontSendNotification);
    int lo = a.lowest, hi = a.highest;   // full range of every note (chords included)
    for (auto& n : a.polyNotes) { lo = juce::jmin (lo, n.pitch); hi = juce::jmax (hi, n.pitch); }
    roll.setNotes (empty ? std::vector<mc::Note>() : lane.notes, a.lengthBeats, a.beatsPerBar, lo - 3, hi + 3);
    preview.setEnabled (! empty);
    lock.setEnabled (! empty);
    similar.setEnabled (! empty);
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
        auto info = getLocalBounds().removeFromRight (110).removeFromBottom (30).translated (0, -3);
        g.drawText (juce::String (juce::roundToInt (overlapValue * 100)) + "% like source", info.removeFromTop (15), juce::Justification::centred);
        if (newNotes > 0)
        {
            g.setColour (Pal::redSoft);
            g.drawText ("+" + juce::String (newNotes) + " new", info, juce::Justification::centred);
        }
    }
}

void LaneComponent::resized()
{
    auto r = getLocalBounds().reduced (6);
    r.removeFromLeft (28);
    auto right = r.removeFromRight (104);
    auto buttons = right.withSizeKeepingCentre (104, 32).translated (0, -10);
    preview.setBounds (buttons.removeFromLeft (35));
    lock.setBounds (buttons.removeFromLeft (35));
    similar.setBounds (buttons);
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

    typeBox.addItemList ({ "Bass", "Melody / Lead", "Arp / Pluck", "Stab / Riff", "Chords" }, 1);
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
        proc.applyTypeDefaults ((mc::MidiType) juce::jmax (0, typeBox.getSelectedItemIndex()));
        source.refresh();
    };

    matchBox.addItemList ({ "Note count", "Playing time" }, 1);
    matchBox.setTooltip ("Match the note percentages by how many notes there are, or by how long each note plays");
    matchAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "match", matchBox);
    addAndMakeVisible (matchBox);

    presetBox.setTextWhenNothingSelected ("Choose a preset");
    presetBox.setTooltip ("Presets set the options in the top bar (not your clip or lanes)");
    presetBox.onChange = [this]
    {
        const int idx = presetBox.getSelectedItemIndex();
        if (idx >= 0) { proc.applyPreset (idx); source.refresh(); setStatus ("Preset: " + presetBox.getText()); }
    };
    addAndMakeVisible (presetBox);
    savePresetButton.setTooltip ("Save the current options as your own preset");
    savePresetButton.onClick = [this] { askPresetName(); };
    addAndMakeVisible (savePresetButton);
    refreshPresets();

    rhythmAtt = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "rhythm", rhythmBox);
    modeAtt   = std::make_unique<juce::AudioProcessorValueTreeState::ComboBoxAttachment> (proc.params, "mode", modeBox);

    shapeButton.setClickingTogglesState (true);
    shapeButton.setTooltip ("Keep Shape: clones go up where your clip goes up and down where it goes down");
    shapeAtt = std::make_unique<juce::AudioProcessorValueTreeState::ButtonAttachment> (proc.params, "shape", shapeButton);
    addAndMakeVisible (shapeButton);

    for (auto* s : { &slideKnob, &variationKnob, &newNotesKnob })
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
    newNotesKnob.setTooltip ("New Notes: share of notes swapped for in-key notes your clip doesn't use (0% = only your notes)");
    newNotesAtt  = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (proc.params, "newnotes", newNotesKnob);
    newNotesKnob.setTextValueSuffix ("%");

    undoButton.setTooltip ("Undo: bring back the previous 10 clips (press again to redo)");
    undoButton.onClick = [this]
    {
        if (proc.undoGenerate()) setStatus ("Switched back to the other batch. Press Undo again to swap.");
        else setStatus ("Nothing to undo yet.", true);
    };
    addAndMakeVisible (undoButton);

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
    setResizeLimits (1060, 760, 1800, 1340);
    setSize (1180, 860);
    refreshAll();
    startTimerHz (30);
}

MidiCloneEditor::~MidiCloneEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

void MidiCloneEditor::refreshPresets()
{
    const auto names = proc.getPresetNames();
    presetBox.clear (juce::dontSendNotification);
    auto* menu = presetBox.getRootMenu();
    menu->addSectionHeader ("Factory");
    for (int i = 0; i < names.size(); ++i)
    {
        if (i == proc.numFactoryPresets()) menu->addSectionHeader ("Yours");
        presetBox.addItem (names[i], i + 1);
    }
}

void MidiCloneEditor::askPresetName()
{
    presetDialog = std::make_unique<juce::AlertWindow> ("Save preset", "Name your preset", juce::MessageBoxIconType::NoIcon, this);
    presetDialog->addTextEditor ("name", "My preset");
    presetDialog->addButton ("Save", 1, juce::KeyPress (juce::KeyPress::returnKey));
    presetDialog->addButton ("Cancel", 0, juce::KeyPress (juce::KeyPress::escapeKey));
    presetDialog->enterModalState (true, juce::ModalCallbackFunction::create ([this] (int result)
    {
        if (result == 1 && presetDialog != nullptr)
        {
            const auto name = presetDialog->getTextEditorContents ("name");
            if (proc.saveUserPreset (name))
            {
                refreshPresets();
                const auto names = proc.getPresetNames();
                presetBox.setSelectedItemIndex (names.indexOf (juce::File::createLegalFileName (name.trim())), juce::dontSendNotification);
                setStatus ("Saved preset \"" + name + "\"");
            }
            else setStatus ("Couldn't save that preset name.", true);
        }
        presetDialog.reset();
    }), false);
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
    source.updateState (req, act, pos);
}

void MidiCloneEditor::paint (juce::Graphics& g)
{
    g.fillAll (Pal::bg);

    auto bar = topBar.toFloat();
    glossPanel (g, bar, 9.0f);
    glowOutline (g, bar, 9.0f, Pal::red, 0.6f);

    // Title with a neon glow
    auto title = topBar.withHeight (82).reduced (16, 0).removeFromLeft (190);
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
    label (newNotesKnob, (int) proc.params.getRawParameterValue ("type")->load() == (int) mc::MidiType::chords ? "NEW CHORDS" : "NEW NOTES");
    label (shapeButton, "SHAPE");
    g.drawText ("PRESET", row2.getX(), row2.getY(), 60, row2.getHeight(), juce::Justification::centredLeft);
    g.drawText ("MATCH", matchBox.getX() - 52, row2.getY(), 50, row2.getHeight(), juce::Justification::centredLeft);

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
    topBar = r.removeFromTop (124);

    auto bar = topBar.reduced (16, 12);
    row2 = bar.removeFromBottom (30).withTrimmedLeft (190);
    {
        auto rr = row2;
        rr.removeFromLeft (60);
        presetBox.setBounds (rr.removeFromLeft (250).withSizeKeepingCentre (250, 28));
        rr.removeFromLeft (8);
        savePresetButton.setBounds (rr.removeFromLeft (70).withSizeKeepingCentre (70, 28));
        rr.removeFromLeft (70);
        matchBox.setBounds (rr.removeFromLeft (150).withSizeKeepingCentre (150, 28));
    }
    bar.removeFromBottom (4);
    bar.removeFromLeft (190);
    auto right = bar.removeFromRight (230);
    undoButton.setBounds (right.removeFromRight (68).withSizeKeepingCentre (68, 34));
    right.removeFromRight (6);
    generateButton.setBounds (right.withSizeKeepingCentre (right.getWidth(), 50));
    bar.removeFromRight (12);

    labelsRow = bar.removeFromTop (16);
    auto controls = bar.withTrimmedTop (4);
    auto place = [&] (juce::Component& c, int w, int h)
    {
        c.setBounds (controls.removeFromLeft (w).withSizeKeepingCentre (w, h));
        controls.removeFromLeft (10);
    };
    const int knobW = 84;
    const int comboW = juce::jlimit (104, 150, (controls.getWidth() - 3 * knobW - 104 - 7 * 10) / 3);
    place (typeBox, comboW, 32);
    place (rhythmBox, comboW, 32);
    place (modeBox, comboW, 32);
    place (shapeButton, 104, 32);
    place (slideKnob, knobW, 40);
    place (variationKnob, knobW, 40);
    place (newNotesKnob, knobW, 40);

    r.removeFromTop (12);
    source.setBounds (r.removeFromTop (196));
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
