#include "PluginEditor.h"

namespace OTT
{
    static const juce::Colour bg         { 25, 25, 25 };
    static const juce::Colour panel      { 33, 33, 33 };
    static const juce::Colour separator  { 55, 55, 55 };
    static const juce::Colour track      { 50, 50, 55 };
    static const juce::Colour knobFill   { 200, 200, 210 };
    static const juce::Colour text       { 160, 160, 170 };
    static const juce::Colour textDim    { 90, 90, 100 };
    static const juce::Colour lowColour  { 70, 145, 255 };
    static const juce::Colour midColour  { 150, 90, 255 };
    static const juce::Colour highColour { 255, 170, 50 };
    static const juce::Colour upColour   { 255, 110, 30 };
    static const juce::Colour downColour { 50, 200, 150 };
}

OTTLookAndFeel::OTTLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, OTT::text);
    setColour(juce::Slider::textBoxBackgroundColourId, OTT::bg);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0));
    setColour(juce::Slider::textBoxHighlightColourId, OTT::knobFill);
}

void OTTLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider)
{
    auto radius = (float)juce::jmin(width / 2, height / 2) - 3.0f;
    auto cx = (float)x + (float)width * 0.5f;
    auto cy = (float)y + (float)height * 0.5f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const float lineW = 2.0f;

    juce::Path bgArc;
    bgArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(OTT::track);
    g.strokePath(bgArc, juce::PathStrokeType(lineW));

    auto* prop = slider.getProperties().getVarPointer("knobColour");
    juce::Colour colour = prop ? juce::Colour((juce::uint32)(juce::int64)*prop) : OTT::knobFill;

    if (sliderPos > 0.001f)
    {
        juce::Path fillArc;
        fillArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(colour);
        g.strokePath(fillArc, juce::PathStrokeType(lineW));
    }

    float dotR = radius + 1.0f;
    float dx = cx + std::sin(angle) * dotR;
    float dy = cy - std::cos(angle) * dotR;
    g.setColour(colour);
    g.fillEllipse(dx - 3.0f, dy - 3.0f, 6.0f, 6.0f);
}

BandMeter::BandMeter(VibeOTTProcessor& p, int bandIndex, juce::Colour c)
    : processor(p), band(bandIndex), colour(c)
{
    startTimerHz(30);
}

void BandMeter::timerCallback()
{
    auto& levels = processor.getBandLevels();
    float newLevel = levels.inputDb[band];
    float newGR = levels.gainReductionDb[band];

    level = juce::jmax(level * 0.85f, newLevel);
    gainReduction = gainReduction * 0.8f + newGR * 0.2f;
    if (newLevel > peakLevel) peakLevel = newLevel;
    else peakLevel = juce::jmax(-100.0f, peakLevel - 0.5f);

    repaint();
}

void BandMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float meterWidth = bounds.getWidth() * 0.5f;
    float meterX = bounds.getCentreX() - meterWidth * 0.5f;
    float meterHeight = bounds.getHeight() - 16.0f;

    float minDb = -60.0f;
    float maxDb = 0.0f;

    auto dbToY = [&](float db) {
        float norm = (db - minDb) / (maxDb - minDb);
        norm = juce::jlimit(0.0f, 1.0f, norm);
        return bounds.getY() + meterHeight * (1.0f - norm);
    };

    g.setColour(OTT::panel);
    g.fillRect(meterX, bounds.getY(), meterWidth, meterHeight);

    g.setColour(OTT::separator);
    for (int db = -60; db <= 0; db += 12)
    {
        float y = dbToY((float)db);
        g.drawHorizontalLine((int)y, meterX, meterX + meterWidth);
    }

    float levelY = dbToY(level);
    g.setColour(colour);
    g.fillRect(meterX, levelY, meterWidth, bounds.getY() + meterHeight - levelY);

    float peakY = dbToY(peakLevel);
    g.setColour(colour.withAlpha(0.6f));
    g.drawHorizontalLine((int)peakY, meterX, meterX + meterWidth);

    if (gainReduction < -0.1f)
    {
        float grY = dbToY(0.0f);
        float grEndY = dbToY(juce::jmax((float)-36.0f, gainReduction));
        g.setColour(OTT::downColour);
        g.fillRect(meterX + meterWidth + 2.0f, grY, 3.0f, grEndY - grY);
    }
    else if (gainReduction > 0.1f)
    {
        float grEndY = dbToY(juce::jmin(36.0f, gainReduction));
        g.setColour(OTT::upColour);
        g.fillRect(meterX - 5.0f, grEndY, 3.0f, dbToY(0.0f) - grEndY);
    }

    g.setColour(colour);
    g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
    auto labels = { "L", "M", "H" };
    int labelIdx = juce::jmin(band, 2);
    g.drawText(*(labels.begin() + labelIdx), bounds.removeFromBottom(14.0f).toNearestInt(),
               juce::Justification::centred, false);
}

VibeOTTEditor::VibeOTTEditor(VibeOTTProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&ottLookAndFeel);

    depthSlider = createKnob("DEPTH", "DEPTH");
    upwardRatioSlider = createKnob("UPWARD_RATIO", "UP");
    downwardRatioSlider = createKnob("DOWNWARD_RATIO", "DOWN");
    lowGainSlider = createKnob("LOW_GAIN", "LOW");
    midGainSlider = createKnob("MID_GAIN", "MID");
    highGainSlider = createKnob("HIGH_GAIN", "HIGH");
    outputGainSlider = createKnob("OUTPUT_GAIN", "OUT");

    depthSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::knobFill.getARGB());
    upwardRatioSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::upColour.getARGB());
    downwardRatioSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::downColour.getARGB());
    lowGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::lowColour.getARGB());
    midGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::midColour.getARGB());
    highGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::highColour.getARGB());
    outputGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::knobFill.getARGB());

    lowThresh = createThresholdSlider("LOW_THRESH");
    midThresh = createThresholdSlider("MID_THRESH");
    highThresh = createThresholdSlider("HIGH_THRESH");
    lowRange = createThresholdSlider("LOW_RANGE");
    midRange = createThresholdSlider("MID_RANGE");
    highRange = createThresholdSlider("HIGH_RANGE");

    lowMeter = std::make_unique<BandMeter>(processorRef, 0, OTT::lowColour);
    midMeter = std::make_unique<BandMeter>(processorRef, 1, OTT::midColour);
    highMeter = std::make_unique<BandMeter>(processorRef, 2, OTT::highColour);

    addAndMakeVisible(*lowMeter);
    addAndMakeVisible(*midMeter);
    addAndMakeVisible(*highMeter);

    setSize(620, 400);
}

VibeOTTEditor::~VibeOTTEditor()
{
    setLookAndFeel(nullptr);
}

VibeOTTEditor::Knob VibeOTTEditor::createKnob(const juce::String& paramId, const juce::String& labelText)
{
    Knob k;
    k.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                               juce::Slider::NoTextBox);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, paramId, *k.slider);
    k.label = std::make_unique<juce::Label>();
    k.label->setText(labelText, juce::dontSendNotification);
    k.label->setJustificationType(juce::Justification::centred);
    k.label->setFont(juce::FontOptions(8.0f, juce::Font::bold));
    k.label->setColour(juce::Label::textColourId, OTT::textDim);
    addAndMakeVisible(*k.slider);
    addAndMakeVisible(*k.label);
    return k;
}

VibeOTTEditor::ThresholdSlider VibeOTTEditor::createThresholdSlider(const juce::String& paramId)
{
    ThresholdSlider ts;
    ts.slider = std::make_unique<juce::Slider>(juce::Slider::LinearVertical,
                                                juce::Slider::NoTextBox);
    ts.slider->setColour(juce::Slider::trackColourId, OTT::track);
    ts.slider->setColour(juce::Slider::thumbColourId, OTT::knobFill);
    ts.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, paramId, *ts.slider);
    addAndMakeVisible(*ts.slider);
    return ts;
}

void VibeOTTEditor::paint(juce::Graphics& g)
{
    g.fillAll(OTT::bg);

    g.setColour(OTT::knobFill);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("OTT", getLocalBounds().removeFromTop(36), juce::Justification::centred, false);

    g.setColour(OTT::textDim);
    g.setFont(juce::FontOptions(8.0f));
    g.drawText("VIBE AUDIO", 0, 30, getWidth(), 12, juce::Justification::centred, false);

    g.setColour(OTT::separator);
    g.drawHorizontalLine(44, 8.0f, (float)getWidth() - 8.0f);

    g.setColour(OTT::textDim);
    g.setFont(juce::FontOptions(8.0f));
    g.drawText("BAND THRESHOLDS", 8, 236, 200, 12, juce::Justification::left, false);
}

void VibeOTTEditor::resized()
{
    int knobSize = 50;
    int knobSpacing = 14;
    int knobY = 56;
    int startX = 10;

    depthSlider.slider->setBounds(startX + 0 * (knobSize + knobSpacing), knobY, knobSize, knobSize);
    depthSlider.label->setBounds(startX + 0 * (knobSize + knobSpacing), knobY + knobSize, knobSize, 14);

    upwardRatioSlider.slider->setBounds(startX + 1 * (knobSize + knobSpacing), knobY, knobSize, knobSize);
    upwardRatioSlider.label->setBounds(startX + 1 * (knobSize + knobSpacing), knobY + knobSize, knobSize, 14);

    downwardRatioSlider.slider->setBounds(startX + 2 * (knobSize + knobSpacing), knobY, knobSize, knobSize);
    downwardRatioSlider.label->setBounds(startX + 2 * (knobSize + knobSpacing), knobY + knobSize, knobSize, 14);

    lowGainSlider.slider->setBounds(startX + 3 * (knobSize + knobSpacing), knobY, knobSize, knobSize);
    lowGainSlider.label->setBounds(startX + 3 * (knobSize + knobSpacing), knobY + knobSize, knobSize, 14);

    midGainSlider.slider->setBounds(startX + 4 * (knobSize + knobSpacing), knobY, knobSize, knobSize);
    midGainSlider.label->setBounds(startX + 4 * (knobSize + knobSpacing), knobY + knobSize, knobSize, 14);

    highGainSlider.slider->setBounds(startX + 5 * (knobSize + knobSpacing), knobY, knobSize, knobSize);
    highGainSlider.label->setBounds(startX + 5 * (knobSize + knobSpacing), knobY + knobSize, knobSize, 14);

    outputGainSlider.slider->setBounds(startX + 6 * (knobSize + knobSpacing), knobY, knobSize, knobSize);
    outputGainSlider.label->setBounds(startX + 6 * (knobSize + knobSpacing), knobY + knobSize, knobSize, 14);

    int meterY = 56;
    int meterH = 170;
    int meterW = 28;
    int meterX = getWidth() - 140;

    int meterSpacing = 36;
    lowMeter->setBounds(meterX + meterSpacing * 0, meterY, meterW, meterH);
    midMeter->setBounds(meterX + meterSpacing * 1, meterY, meterW, meterH);
    highMeter->setBounds(meterX + meterSpacing * 2, meterY, meterW, meterH);

    int threshY = 256;
    int threshH = 120;
    int threshW = 24;
    int threshSpacing = 40;

    for (int b = 0; b < 3; ++b)
    {
        int x = 40 + b * (threshW + threshSpacing) * 2;
        auto colour = b == 0 ? OTT::lowColour : b == 1 ? OTT::midColour : OTT::highColour;

        auto& t = (b == 0) ? lowThresh : (b == 1) ? midThresh : highThresh;
        t.slider->setBounds(x, threshY, threshW, threshH);
        t.slider->setColour(juce::Slider::thumbColourId, colour);
        t.slider->setColour(juce::Slider::trackColourId, colour.withAlpha(0.3f));

        auto& r = (b == 0) ? lowRange : (b == 1) ? midRange : highRange;
        r.slider->setBounds(x + threshW + 4, threshY, threshW, threshH);
        r.slider->setColour(juce::Slider::thumbColourId, colour.withAlpha(0.6f));
        r.slider->setColour(juce::Slider::trackColourId, colour.withAlpha(0.2f));
    }
}
