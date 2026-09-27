#include "PluginEditor.h"

//==============================================================================
OttLookAndFeel::OttLookAndFeel()
{
    setColour (juce::Slider::textBoxTextColourId, OttTheme::text);
    setColour (juce::Slider::textBoxBackgroundColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxOutlineColourId, juce::Colours::transparentBlack);
    setColour (juce::Slider::textBoxHighlightColourId, OttTheme::neutral.withAlpha (0.3f));
    setColour (juce::Label::textColourId, OttTheme::textDim);
    setColour (juce::TextButton::textColourOffId, OttTheme::textDim);
    setColour (juce::TextButton::textColourOnId, OttTheme::neutral);
}

void OttLookAndFeel::drawRotarySlider (juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider)
{
    const auto bounds = juce::Rectangle<int> (x, y, width, height).toFloat();
    const auto radius = juce::jmin (bounds.getWidth(), bounds.getHeight()) * 0.5f - 2.0f;
    const auto centreX = bounds.getCentreX();
    const auto centreY = bounds.getCentreY();
    const auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);
    const auto thickness = juce::jmax (2.0f, radius * 0.18f);

    const juce::Colour colour = [&slider]
    {
        const juce::var* property = slider.getProperties().getVarPointer ("knobColour");
        return property != nullptr ? juce::Colour ((juce::uint32) (juce::int64) *property)
                                   : OttTheme::neutral;
    }();

    // Track.
    juce::Path track;
    track.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                         rotaryStartAngle, rotaryEndAngle, true);
    g.setColour (OttTheme::outline);
    g.strokePath (track, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                               juce::PathStrokeType::rounded));

    // Value arc. Drawn from the start so the knob reads as "how much", not
    // "where in the range".
    if (sliderPos > 0.0001f)
    {
        const bool filled = (bool) slider.getProperties().getWithDefault ("knobFilledRing", false);

        juce::Path value;
        value.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                             rotaryStartAngle, angle, true);

        if (filled)
        {
            // The Depth knob is the plugin's headline control, so it gets the
            // filled-ring treatment rather than the same stroked arc as
            // everything else. Drawn from the centre outward to a slightly
            // smaller radius, which keeps the outer track readable.
            juce::Path wedge (value);
            wedge.lineTo (centreX, centreY);
            wedge.closeSubPath();

            const auto inner = radius * 0.62f;
            juce::Path hole;
            hole.addCentredArc (centreX, centreY, inner, inner, 0.0f,
                                rotaryStartAngle, angle, true);
            hole.lineTo (centreX, centreY);
            hole.closeSubPath();

            g.setGradientFill (juce::ColourGradient (colour.brighter (0.25f), centreX, centreY - radius,
                                                     colour.darker (0.25f),  centreX, centreY + radius,
                                                     false));
            g.fillPath (wedge);
            g.setColour (OttTheme::panelRaised);
            g.fillPath (hole);
        }
        else
        {
            g.setColour (colour);
            g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                       juce::PathStrokeType::rounded));
        }
    }

    // Pointer dot, which is what makes the exact position readable at a glance.
    const auto dotRadius = juce::jmax (2.0f, radius * 0.16f);
    const auto dotX = centreX + std::sin (angle) * (radius - thickness * 0.1f);
    const auto dotY = centreY - std::cos (angle) * (radius - thickness * 0.1f);
    g.setColour (colour.brighter (0.35f));
    g.fillEllipse (dotX - dotRadius, dotY - dotRadius, dotRadius * 2.0f, dotRadius * 2.0f);
}

//==============================================================================
BandMeter::BandMeter (int bandIndex)
    : band (bandIndex)
{
    setInterceptsMouseClicks (false, false);
}

void BandMeter::setLevels (const ott::BandLevels& levels)
{
    const float newLevel = levels.inputDb[(size_t) band];
    const float newReduction = levels.gainReductionDb[(size_t) band];

    // Up instantly, down slowly. The previous version did this backwards, so
    // the meter lingered on the way up and dropped like a stone.
    levelDb = newLevel > levelDb ? newLevel : levelDb - 1.5f;
    levelDb = juce::jmax (minimumDb, levelDb);

    reductionDb += (newReduction - reductionDb) * 0.35f;

    if (levelDb >= peakDb)
    {
        peakDb = levelDb;
        peakHoldCounter = 30;
    }
    else if (--peakHoldCounter <= 0)
    {
        peakDb = juce::jmax (minimumDb, peakDb - 1.0f);
    }

    // Quantised to what is actually drawn, so the readout does not flicker
    // between values that render identically.
    displayedReductionDb = std::round (reductionDb * 10.0f) / 10.0f;
    haveReading = true;

    repaint();
}

void BandMeter::setLevelsForDisplay (float inputDb, float gainChangeDb)
{
    levelDb = inputDb;
    displayedReductionDb = gainChangeDb;
    reductionDb = gainChangeDb;
    peakDb = inputDb;
    peakHoldCounter = 30;
    haveReading = true;
    repaint();
}

void BandMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto labelArea = bounds.removeFromBottom (captionHeight);
    auto readoutArea = bounds.removeFromBottom (readoutHeight);
    auto meterArea = bounds.toFloat();

    // The reduction strip sits beside the level bar.
    const float stripWidth = juce::jmax (4.0f, meterArea.getWidth() * 0.22f);
    auto reductionArea = meterArea.removeFromRight (stripWidth).reduced (1.0f, 0.0f);
    meterArea.removeFromRight (4.0f);

    const auto colour = OttTheme::bandColour (band);

    //--- level bar -----------------------------------------------------------
    g.setColour (OttTheme::panel);
    g.fillRoundedRectangle (meterArea, 3.0f);

    g.setColour (OttTheme::outline);
    g.drawRoundedRectangle (meterArea, 3.0f, 1.0f);

    // Grid at -48, -36, -24, -12 and 0 dB.
    g.setColour (OttTheme::outline.withAlpha (0.8f));
    for (int db = -48; db <= 0; db += 12)
    {
        const float y = meterArea.getBottom() - meterArea.getHeight() * dbToProportion ((float) db);
        g.drawHorizontalLine ((int) y, meterArea.getX() + 1.0f, meterArea.getRight() - 1.0f);
    }

    const float levelY = meterArea.getBottom() - meterArea.getHeight() * dbToProportion (levelDb);

    if (levelY < meterArea.getBottom())
    {
        juce::Rectangle<float> fill (meterArea.getX() + 1.0f, levelY,
                                     meterArea.getWidth() - 2.0f,
                                     meterArea.getBottom() - levelY);

        g.setGradientFill (juce::ColourGradient (colour, meterArea.getCentreX(), meterArea.getBottom(),
                                                 colour.brighter (0.5f), meterArea.getCentreX(), meterArea.getY(),
                                                 false));
        g.fillRect (fill);
    }

    // Peak hold.
    if (peakDb > minimumDb)
    {
        const float peakY = meterArea.getBottom() - meterArea.getHeight() * dbToProportion (peakDb);
        g.setColour (colour.brighter (0.7f));
        g.fillRect (meterArea.getX() + 1.0f, peakY - 1.0f, meterArea.getWidth() - 2.0f, 2.0f);
    }

    //--- gain reduction strip ------------------------------------------------
    g.setColour (OttTheme::panel);
    g.fillRoundedRectangle (reductionArea, 2.0f);

    const float zeroY = reductionArea.getY() + reductionArea.getHeight() * 0.5f;

    g.setColour (OttTheme::outline);
    g.drawHorizontalLine ((int) zeroY, reductionArea.getX(), reductionArea.getRight());

    const float clamped = juce::jlimit (-maximumGainReductionDb, maximumGainReductionDb, reductionDb);
    const float span = reductionArea.getHeight() * 0.5f;

    if (clamped < -0.1f) // compressing: bar grows downward
    {
        const float height = span * (clamped / -maximumGainReductionDb);
        g.setColour (OttTheme::downward);
        g.fillRect (reductionArea.getX() + 1.0f, zeroY, reductionArea.getWidth() - 2.0f, height);
    }
    else if (clamped > 0.1f) // lifting: bar grows upward
    {
        const float height = span * (clamped / maximumGainReductionDb);
        g.setColour (OttTheme::upward);
        g.fillRect (reductionArea.getX() + 1.0f, zeroY - height, reductionArea.getWidth() - 2.0f, height);
    }

    //--- readout -------------------------------------------------------------
    // The number the bar cannot give: how much this band is being turned down
    // or lifted, in dB. Signed, because the upward stage runs the other way --
    // "-4.2" is compressing, "+3.0" is lifting.
    {
        // The unit is appended so the digits cannot be mistaken for a ratio.
        // It is dropped when idle, where "--" already says "nothing to report".
        const juce::String text = haveReading
            ? juce::String (displayedReductionDb, 1)
            : juce::String ("--");

        // Downward compression and upward lift get their own colour so the sign
        // is readable at a glance, not just from the digits.
        juce::Colour readoutColour = OttTheme::textDim;

        if (haveReading)
        {
            if (displayedReductionDb < -0.05f)
                readoutColour = OttTheme::downward;
            else if (displayedReductionDb > 0.05f)
                readoutColour = OttTheme::upward;
            else
                readoutColour = OttTheme::text;
        }

        g.setColour (readoutColour);
        g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
        g.drawText (text, readoutArea, juce::Justification::centred, false);
    }

    //--- label ---------------------------------------------------------------
    g.setColour (colour);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (ParameterIDs::bandNames[band], labelArea, juce::Justification::centred, false);
}

//==============================================================================
void VibeOTTEditor::Knob::setBoundsWithDial (juce::Rectangle<int> area, int preferredDial)
{
    // Grow the dial to `preferredDial` if the slot can afford it, keeping the
    // readout and caption at their natural height. Falls back to the normal
    // split when the space is not there.
    const int overhead = captionHeight_ + readoutHeight_;
    const int dial = juce::jmin (preferredDial, juce::jmax (24, area.getHeight() - overhead));

    setBounds (area.withSizeKeepingCentre (area.getWidth(), dial + overhead));
}

void VibeOTTEditor::Knob::setBounds (juce::Rectangle<int> area)
{
    // Three stacked strips: the dial, the numeric readout, then the caption.
    //
    // The readout is our own Label rather than JUCE's slider text box: the text
    // box caches its string and only rebuilds it when textFromValueFunction or
    // the value changes, so a formatter attached after construction silently
    // never reaches the screen. Drawing it here is both simpler and immune to
    // that.
    constexpr int captionHeight = Knob::captionHeight_;
    constexpr int readoutHeight = Knob::readoutHeight_;

    auto captionArea = area.removeFromBottom (captionHeight);
    auto readoutArea = area.removeFromBottom (readoutHeight);

    slider.setBounds (area);
    readout.setBounds (readoutArea);
    label.setBounds (captionArea.reduced (2, 0));
}

//==============================================================================
VibeOTTEditor::VibeOTTEditor (VibeOTTProcessor& p)
    : AudioProcessorEditor (&p), processorRef (p)
{
    setLookAndFeel (&lookAndFeel);

    addKnob (depthKnob,    *this, ParameterIDs::depth,    {},         OttTheme::neutral);
    addKnob (upwardKnob,   *this, ParameterIDs::upward,   "UP",       OttTheme::upward);
    addKnob (downwardKnob, *this, ParameterIDs::downward, "DOWN",     OttTheme::downward);
    addKnob (mixKnob,      *this, ParameterIDs::mix,      "MIX",      OttTheme::neutral);
    addKnob (timeKnob,     *this, ParameterIDs::time,     "TIME",     OttTheme::neutral);

    addKnob (inputKnob,    *this, ParameterIDs::inputGain,  "IN",       OttTheme::neutral);
    addKnob (outputKnob,   *this, ParameterIDs::outputGain, "OUT",      OttTheme::neutral);
    addKnob (behaviorKnob, *this, ParameterIDs::behavior,   "BEHAVIOR", OttTheme::neutral);
    addKnob (lowCrossoverKnob,  *this, ParameterIDs::lowCrossover,  "LO / MID", OttTheme::low);
    addKnob (highCrossoverKnob, *this, ParameterIDs::highCrossover, "MID / HI", OttTheme::high);

    // Depth is a normal rotary like every other control; it is just bigger and
    // drawn as a filled ring, which is the weight the old centre display had
    // without the drag behaviour that made it awkward to use.
    OttLookAndFeel::setFilledRing (depthKnob.slider, true);

    for (int band = 0; band < ott::numBands; ++band)
    {
        meters[band] = std::make_unique<BandMeter> (band);
        addAndMakeVisible (*meters[band]);
    }

    //== advanced panel =======================================================
    advancedButton.setClickingTogglesState (true);
    advancedButton.setColour (juce::TextButton::buttonColourId, OttTheme::panel);
    advancedButton.setColour (juce::TextButton::buttonOnColourId, OttTheme::panelRaised);
    advancedButton.setConnectedEdges (juce::Button::ConnectedOnLeft | juce::Button::ConnectedOnRight);
    addAndMakeVisible (advancedButton);

    advancedPanel = std::make_unique<juce::Component>();
    addChildComponent (*advancedPanel);

    for (int band = 0; band < ott::numBands; ++band)
    {
        const auto colour = OttTheme::bandColour (band);

        addKnob (upwardThresholdKnob[band],   *advancedPanel, ParameterIDs::upwardThreshold (band),
                 "UP THR", OttTheme::upward);
        addKnob (upwardRatioKnob[band],       *advancedPanel, ParameterIDs::upwardRatio (band),
                 "UP RATIO", OttTheme::upward);
        addKnob (downwardThresholdKnob[band], *advancedPanel, ParameterIDs::downwardThreshold (band),
                 "DN THR", OttTheme::downward);
        addKnob (downwardRatioKnob[band],     *advancedPanel, ParameterIDs::downwardRatio (band),
                 "DN RATIO", OttTheme::downward);
        addKnob (bandGainKnob[band],          *advancedPanel, ParameterIDs::bandGain (band),
                 "GAIN", colour);
    }

    advancedButton.onClick = [this] { setAdvancedVisible (advancedButton.getToggleState()); };

    // The size has to be set before the layout runs, or the first resized()
    // lays everything out into a zero-width window.
    setSize (windowWidth, collapsedHeight);
    setResizable (false, false);

    setAdvancedVisible (false);
    timerCallback();

    startTimerHz (30);
}

VibeOTTEditor::~VibeOTTEditor()
{
    stopTimer();
    setLookAndFeel (nullptr);
}

juce::String formatParameterValue (const juce::String& parameterId, double value)
{
    if (parameterId == ParameterIDs::lowCrossover || parameterId == ParameterIDs::highCrossover)
        return juce::String (juce::roundToInt (value)) + " Hz";

    if (parameterId == ParameterIDs::inputGain
        || parameterId == ParameterIDs::outputGain
        || parameterId == ParameterIDs::behavior
        || parameterId.endsWith ("_GAIN")
        || parameterId.endsWith ("_THRESHOLD"))
        return juce::String (value, 1) + " dB";

    if (parameterId == ParameterIDs::mix || parameterId == ParameterIDs::depth)
        return juce::String (juce::roundToInt (value * 100.0)) + "%";

    if (parameterId == ParameterIDs::upward || parameterId == ParameterIDs::downward)
        return juce::String (value, 2) + "x";

    if (parameterId.endsWith ("_RATIO"))
        return juce::String (juce::roundToInt (value * 100.0)) + "%";

    if (parameterId == ParameterIDs::time)
        return juce::String (juce::roundToInt (value * 100.0)) + "%";

    return juce::String (value, 2);
}

void VibeOTTEditor::addKnob (Knob& knob, juce::Component& parent,
                              const juce::String& parameterId, const juce::String& labelText,
                              juce::Colour colour)
{
    auto* parameter = processorRef.apvts.getParameter (parameterId);
    jassert (parameter != nullptr);

    knob.slider.getProperties().set (knobParameterIdProperty, parameterId);
    knob.parameterId = parameterId;

    knob.slider.setSliderStyle (juce::Slider::RotaryHorizontalVerticalDrag);
    knob.slider.setTextBoxStyle (juce::Slider::NoTextBox, false, 0, 0);
    knob.slider.setPopupDisplayEnabled (true, false, this);
    knob.slider.setTextValueSuffix ({});

    if (parameter != nullptr)
        knob.slider.setDoubleClickReturnValue (true, (double) parameter->getDefaultValue());

    OttLookAndFeel::setKnobColour (knob.slider, colour);

    knob.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment> (
        processorRef.apvts, parameterId, knob.slider);

    // Our own readout, updated whenever the value moves.
    knob.readout.setJustificationType (juce::Justification::centred);
    knob.readout.setFont (juce::FontOptions (11.0f));
    knob.readout.setColour (juce::Label::textColourId, OttTheme::text);
    knob.readout.setInterceptsMouseClicks (false, false);
    knob.readout.setText (formatParameterValue (parameterId, knob.slider.getValue()),
                          juce::dontSendNotification);

    knob.slider.onValueChange = [&knob, parameterId]
    {
        knob.readout.setText (formatParameterValue (parameterId, knob.slider.getValue()),
                              juce::dontSendNotification);
    };

    parent.addAndMakeVisible (knob.slider);
    parent.addAndMakeVisible (knob.readout);

    if (labelText.isNotEmpty())
    {
        knob.label.setText (labelText, juce::dontSendNotification);
        knob.label.setJustificationType (juce::Justification::centred);
        knob.label.setFont (juce::FontOptions (10.0f, juce::Font::bold));
        knob.label.setColour (juce::Label::textColourId, OttTheme::textDim);
        parent.addAndMakeVisible (knob.label);
    }
}

void VibeOTTEditor::setAdvancedVisible (bool shouldBeVisible)
{
    advancedVisible = shouldBeVisible;
    advancedButton.setToggleState (shouldBeVisible, juce::dontSendNotification);
    advancedButton.setButtonText (shouldBeVisible ? "ADVANCED  v" : "ADVANCED  ^");

    // Showing the panel shows its children with it, so nothing else to toggle.
    if (advancedPanel != nullptr)
        advancedPanel->setVisible (shouldBeVisible);

    setSize (windowWidth, shouldBeVisible ? expandedHeight : collapsedHeight);
}

void VibeOTTEditor::timerCallback()
{
    const auto levels = processorRef.getBandLevels();

    for (int band = 0; band < ott::numBands; ++band)
        if (meters[band] != nullptr)
            meters[band]->setLevels (levels);

}

//==============================================================================
void VibeOTTEditor::paint (juce::Graphics& g)
{
    g.fillAll (OttTheme::background);

    auto bounds = getLocalBounds();

    //--- header --------------------------------------------------------------
    auto header = bounds.removeFromTop (46);
    auto brandArea = header.removeFromLeft (150).reduced (14, 8);

    g.setColour (OttTheme::neutral);
    g.setFont (juce::FontOptions (22.0f, juce::Font::bold));
    g.drawText ("VibeOTT", brandArea.removeFromTop (24), juce::Justification::left, false);

    g.setColour (OttTheme::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("MULTIBAND UPWARD / DOWNWARD COMPRESSOR", brandArea, juce::Justification::left, false);

    g.setColour (OttTheme::outline);
    g.drawHorizontalLine (header.getBottom(), 12.0f, (float) getWidth() - 12.0f);

    //--- main panel ----------------------------------------------------------
    const int mainBottom = collapsedHeight - 34;
    juce::Rectangle<int> mainArea (12, 52, getWidth() - 24, mainBottom - 52);

    g.setColour (OttTheme::panel);
    g.fillRoundedRectangle (mainArea.toFloat(), 6.0f);
    g.setColour (OttTheme::outline);
    g.drawRoundedRectangle (mainArea.toFloat(), 6.0f, 1.0f);

    // Section captions, placed relative to the areas resized() lays out.
    g.setColour (OttTheme::textDim);
    g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
    g.drawText ("CROSSOVER", mainArea.getX() + 16, mainArea.getY() + 8, 100, 12,
                juce::Justification::left, false);
    // The number under each meter is a gain change in dB; say so once here
    // rather than repeating "dB" under all three bars.
    //
    // It has to stop short of the meters themselves: they are child components
    // and therefore paint on top of anything the editor draws underneath them,
    // which silently swallowed this caption when it was right-aligned to the
    // panel edge.
    {
        const juce::String caption ("GAIN CHANGE (dB)");
        const juce::Font font (juce::FontOptions (9.0f, juce::Font::bold));
        g.setFont (font);

        // JUCE 8 removed Font::getStringWidth; measure through GlyphArrangement.
        const int width = juce::GlyphArrangement::getStringWidthInt (font, caption) + 4;

        // The meter column is the rightmost 150pt of the panel, inset by 14.
        const int meterColumnLeft = getWidth() - 24 - 14 - 150;

        g.drawText (caption, meterColumnLeft - width - 8, mainArea.getY() + 8, width, 12,
                    juce::Justification::right, false);
    }

    //--- advanced panel ------------------------------------------------------
    if (advancedVisible)
    {
        juce::Rectangle<int> advancedArea (12, collapsedHeight + 4, getWidth() - 24,
                                            expandedHeight - collapsedHeight - 42);

        g.setColour (OttTheme::panel);
        g.fillRoundedRectangle (advancedArea.toFloat(), 6.0f);
        g.setColour (OttTheme::outline);
        g.drawRoundedRectangle (advancedArea.toFloat(), 6.0f, 1.0f);

        g.setColour (OttTheme::textDim);
        g.setFont (juce::FontOptions (9.0f, juce::Font::bold));
        g.drawText ("PER-BAND DETAIL", advancedArea.getX() + 16, advancedArea.getY() + 8, 200, 12,
                    juce::Justification::left, false);

        // One caption per band, above its column of knobs.
        const int bandWidth = advancedArea.getWidth() / ott::numBands;

        for (int band = 0; band < ott::numBands; ++band)
        {
            g.setColour (OttTheme::bandColour (band));
            g.setFont (juce::FontOptions (11.0f, juce::Font::bold));
            g.drawText (ParameterIDs::bandNames[band],
                        advancedArea.getX() + band * bandWidth, advancedArea.getY() + 20,
                        bandWidth, 14, juce::Justification::centred, false);
        }
    }

    //--- footer --------------------------------------------------------------
    g.setColour (OttTheme::textDim);
    g.setFont (juce::FontOptions (9.0f));
    g.drawText ("Depth scales every band's ratios. Up / Down scale the lift and the tame.",
                14, collapsedHeight - 24, getWidth() - 200, 14, juce::Justification::left, false);
}

void VibeOTTEditor::resized()
{
    //== geometry =============================================================
    // A knob occupies one slot: dial + numeric readout + caption. Every section
    // is expressed in slots, so the vertical budget can be checked at a glance.
    constexpr int dialHeight    = 54;
    constexpr int readoutHeight = 15;
    constexpr int captionHeight = 14;
    constexpr int slotHeight    = dialHeight + readoutHeight + captionHeight; // 83

    constexpr int margin  = 24;
    constexpr int panelTop = 64;
    constexpr int panelBottom = collapsedHeight - 30;

    /** The nth row of a column, stacked from its top. */
    auto rowOf = [] (juce::Rectangle<int> column, int row, int slot)
    {
        return column.withY (column.getY() + row * slot).withHeight (slot);
    };

    //== main panel ===========================================================
    auto mainArea = juce::Rectangle<int> (margin, panelTop, getWidth() - margin * 2,
                                          panelBottom - panelTop);
    mainArea.reduce (14, 6);

    //--- meters (right) ------------------------------------------------------
    {
        auto meterColumn = mainArea.removeFromRight (150);

        constexpr int meterWidth = 36;
        const int gap = (meterColumn.getWidth() - meterWidth * ott::numBands) / (ott::numBands + 1);

        for (int band = 0; band < ott::numBands; ++band)
            meters[band]->setBounds (meterColumn.getX() + gap * (band + 1) + meterWidth * band,
                                     meterColumn.getY(), meterWidth, meterColumn.getHeight());
    }

    //--- crossover and gain staging (left) ----------------------------------
    // Two short columns, each a few slots tall, rather than a deep 2x2 grid:
    // that is what keeps the left section the same height as the centre.
    {
        auto leftColumn = mainArea.removeFromLeft (206);

        auto crossoverColumn = leftColumn.removeFromLeft (106);
        auto gainColumn = leftColumn.removeFromLeft (100);

        lowCrossoverKnob.setBounds (rowOf (crossoverColumn, 0, slotHeight));
        inputKnob.setBounds (rowOf (gainColumn, 0, slotHeight));

        highCrossoverKnob.setBounds (rowOf (crossoverColumn, 1, slotHeight));
        outputKnob.setBounds (rowOf (gainColumn, 1, slotHeight));

        behaviorKnob.setBounds (rowOf (crossoverColumn, 2, slotHeight));
    }

    //--- depth and the macros (centre) --------------------------------------
    // One row: Depth first, larger than the rest because it is the master
    // "amount", then Up / Down / Mix. Grouping them keeps the centre as a
    // single control cluster rather than two that fight for the eye.
    {
        auto row = mainArea.removeFromTop (slotHeight);

        // Depth gets a taller slot so its dial can be bigger than the macros
        // beside it. The dial stops short of the readout -- a filled ring that
        // touches the text below reads as one crowded blob.
        constexpr int depthWidth = 100;
        depthKnob.setBoundsWithDial (row.removeFromLeft (depthWidth).withHeight (slotHeight + 26), 72);

        // The remaining width is split evenly between the four macros, so the
        // row fills the column instead of leaving a ragged gap after Mix.
        const int macroWidth = row.getWidth() / 4;

        for (Knob* knob : { &upwardKnob, &downwardKnob, &mixKnob, &timeKnob })
            knob->setBounds (row.removeFromLeft (macroWidth));
    }

    //== footer ===============================================================
    advancedButton.setBounds (getWidth() - margin - 140, panelBottom + 4, 140, 20);

    //== advanced =============================================================
    if (! advancedVisible)
        return;

    auto advancedArea = juce::Rectangle<int> (margin + 14, collapsedHeight + 22,
                                              getWidth() - (margin + 14) * 2,
                                              expandedHeight - collapsedHeight - 68);

    // The panel is the container for the per-band controls, so it has to be
    // given the area they live in; a zero-sized container is what let them
    // render outside the window.
    advancedPanel->setBounds (advancedArea);
    advancedArea = advancedPanel->getLocalBounds();

    const int columnWidth = advancedArea.getWidth() / ott::numBands;

    // Three stacked rows (threshold pair, ratio pair, gain) share the panel
    // height exactly, so the last row can never spill past the bottom.
    constexpr int rowGap = 10;
    const int rowHeight = (advancedArea.getHeight() - rowGap * 2) / 3;

    for (int band = 0; band < ott::numBands; ++band)
    {
        auto column = advancedArea.withX (advancedArea.getX() + band * columnWidth).withWidth (columnWidth);

        auto firstRow = column.removeFromTop (rowHeight);
        upwardThresholdKnob[band].setBounds (firstRow.removeFromLeft (firstRow.getWidth() / 2));
        upwardRatioKnob[band].setBounds (firstRow);

        column.removeFromTop (rowGap);

        auto secondRow = column.removeFromTop (rowHeight);
        downwardThresholdKnob[band].setBounds (secondRow.removeFromLeft (secondRow.getWidth() / 2));
        downwardRatioKnob[band].setBounds (secondRow);

        column.removeFromTop (rowGap);
        bandGainKnob[band].setBounds (column.removeFromTop (rowHeight)
                                            .withSizeKeepingCentre (juce::jmin (84, column.getWidth() / 2),
                                                                    rowHeight));
    }
}
