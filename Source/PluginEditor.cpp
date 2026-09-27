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
        juce::Path value;
        value.addCentredArc (centreX, centreY, radius, radius, 0.0f,
                             rotaryStartAngle, angle, true);
        g.setColour (colour);
        g.strokePath (value, juce::PathStrokeType (thickness, juce::PathStrokeType::curved,
                                                   juce::PathStrokeType::rounded));
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

    repaint();
}

void BandMeter::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds();
    auto labelArea = bounds.removeFromBottom (14);
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

    //--- label ---------------------------------------------------------------
    g.setColour (colour);
    g.setFont (juce::FontOptions (10.0f, juce::Font::bold));
    g.drawText (ParameterIDs::bandNames[band], labelArea, juce::Justification::centred, false);
}

//==============================================================================
DepthDisplay::DepthDisplay (juce::Slider& depthSliderToDrive)
    : depthSlider (depthSliderToDrive)
{
    setInterceptsMouseClicks (true, false);
}

void DepthDisplay::refresh()
{
    const float newDepth = (float) depthSlider.getValue();

    if (! juce::approximatelyEqual (newDepth, depth))
    {
        depth = newDepth;
        repaint();
    }
}

void DepthDisplay::paint (juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    const auto diameter = juce::jmin (bounds.getWidth(), bounds.getHeight()) - 4.0f;
    const auto radius = diameter * 0.5f;
    const auto centreX = bounds.getCentreX();
    const auto centreY = bounds.getCentreY();

    constexpr float startAngle = juce::MathConstants<float>::pi * 0.75f;
    constexpr float endAngle   = juce::MathConstants<float>::pi * 2.25f;

    g.setColour (OttTheme::panelRaised);
    g.fillEllipse (centreX - radius, centreY - radius, diameter, diameter);

    // The filled arc is the Depth amount: at 0 the device is bypassed and the
    // ring is empty, at 1 it is a full sweep of OTT.
    const float angle = startAngle + depth * (endAngle - startAngle);

    if (depth > 0.001f)
    {
        juce::Path fill;
        fill.addCentredArc (centreX, centreY, radius * 0.86f, radius * 0.86f, 0.0f,
                            startAngle, angle, true);
        fill.lineTo (centreX, centreY);
        fill.closeSubPath();

        g.setGradientFill (juce::ColourGradient (OttTheme::upward.withAlpha (0.85f), centreX, centreY - radius,
                                                 OttTheme::downward.withAlpha (0.85f), centreX, centreY + radius,
                                                 false));
        g.fillPath (fill);
    }

    g.setColour (OttTheme::outline);
    g.drawEllipse (centreX - radius, centreY - radius, diameter, diameter, 1.5f);

    g.setColour (OttTheme::text);
    g.setFont (juce::FontOptions (radius * 0.42f, juce::Font::bold));
    g.drawText (juce::String (juce::roundToInt (depth * 100.0f)) + "%",
                getLocalBounds().withTrimmedBottom (juce::roundToInt (radius * 0.5f)),
                juce::Justification::centred, false);

    g.setColour (OttTheme::textDim);
    g.setFont (juce::FontOptions (radius * 0.19f, juce::Font::bold));
    g.drawText ("DEPTH", getLocalBounds().withTrimmedTop (juce::roundToInt (radius * 0.85f)),
                juce::Justification::centred, false);
}

void DepthDisplay::setDepthFromPosition (juce::Point<int> position)
{
    const auto centre = getLocalBounds().toFloat().getCentre();

    // Angle measured from straight up, mapped over the same sweep the arc uses.
    float angle = std::atan2 ((float) position.x - centre.x, centre.y - (float) position.y);

    constexpr float startAngle = -juce::MathConstants<float>::pi * 0.75f;
    constexpr float sweep      = juce::MathConstants<float>::pi * 1.5f;

    float normalised = (angle - startAngle) / sweep;

    // The ring has a gap at the bottom; clamp into it rather than wrapping, so
    // dragging round the bottom edge pins to an end instead of jumping across.
    depthSlider.setValue (juce::jlimit (0.0f, 1.0f, normalised), juce::sendNotificationSync);
    refresh();
}

void DepthDisplay::mouseDown (const juce::MouseEvent& event)
{
    setDepthFromPosition (event.getPosition());
}

void DepthDisplay::mouseDrag (const juce::MouseEvent& event)
{
    setDepthFromPosition (event.getPosition());
}

void DepthDisplay::mouseDoubleClick (const juce::MouseEvent&)
{
    depthSlider.setValue (ott::defaultDepth, juce::sendNotificationSync);
    refresh();
}

//==============================================================================
void VibeOTTEditor::Knob::setBounds (juce::Rectangle<int> area)
{
    // Three stacked strips: the dial, the numeric readout, then the caption.
    //
    // The readout is our own Label rather than JUCE's slider text box: the text
    // box caches its string and only rebuilds it when textFromValueFunction or
    // the value changes, so a formatter attached after construction silently
    // never reaches the screen. Drawing it here is both simpler and immune to
    // that.
    constexpr int captionHeight = 15;
    constexpr int readoutHeight = 15;

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

    depthDisplay = std::make_unique<DepthDisplay> (depthKnob.slider);
    addAndMakeVisible (*depthDisplay);

    // Hidden on purpose: the ring forwards drags to this slider and paints the
    // value, so showing the slider as well would put a second hit target on top
    // of it and let the two disagree.
    depthKnob.slider.setVisible (false);

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

    if (depthDisplay != nullptr)
        depthDisplay->refresh();
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
    g.drawText ("BANDS", mainArea.getRight() - 190, mainArea.getY() + 8, 170, 12,
                juce::Justification::right, false);

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
    constexpr int ringSize      = 112;

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

    //--- macros and the depth ring (centre) ---------------------------------
    {
        auto centreColumn = mainArea;

        auto ringRow = centreColumn.removeFromTop (ringSize);
        depthDisplay->setBounds (ringRow.withSizeKeepingCentre (ringSize, ringSize));

        // The ring IS this control and forwards drags to the slider, which is
        // hidden: one hit target means the two can never disagree.
        depthKnob.slider.setBounds (depthDisplay->getBounds());

        centreColumn.removeFromTop (12);

        auto macroRow = centreColumn.removeFromTop (slotHeight);
        const int macroWidth = juce::jmin (78, macroRow.getWidth() / 4);

        for (Knob* knob : { &upwardKnob, &downwardKnob, &mixKnob, &timeKnob })
            knob->setBounds (macroRow.removeFromLeft (macroWidth));
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
