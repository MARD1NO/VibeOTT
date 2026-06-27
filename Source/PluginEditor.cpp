#include "PluginEditor.h"

namespace OTT
{
    static const juce::Colour bg         { 18, 18, 18 };
    static const juce::Colour panel      { 28, 28, 28 };
    static const juce::Colour separator  { 45, 45, 45 };
    static const juce::Colour track      { 45, 45, 50 };
    static const juce::Colour knobFill   { 210, 210, 220 };
    static const juce::Colour text       { 170, 170, 180 };
    static const juce::Colour textDim    { 95, 95, 105 };
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
}

void OTTLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider)
{
    auto radius = (float)juce::jmin(width / 2, height / 2) - 3.0f;
    auto cx = (float)x + (float)width * 0.5f;
    auto cy = (float)y + (float)height * 0.5f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    juce::Path bgArc;
    bgArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(OTT::track);
    g.strokePath(bgArc, juce::PathStrokeType(2.5f));

    auto* prop = slider.getProperties().getVarPointer("knobColour");
    juce::Colour colour = prop ? juce::Colour((juce::uint32)(juce::int64)*prop) : OTT::knobFill;

    if (sliderPos > 0.001f)
    {
        juce::Path fillArc;
        fillArc.addCentredArc(cx, cy, radius, radius, 0.0f, rotaryStartAngle, angle, true);
        g.setColour(colour);
        g.strokePath(fillArc, juce::PathStrokeType(2.5f));
    }

    float dotR = radius + 1.5f;
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

    level = juce::jmax(level - 0.5f, newLevel);
    gainReduction = gainReduction * 0.8f + newGR * 0.2f;
    if (newLevel > peakLevel) peakLevel = newLevel;
    else peakLevel = juce::jmax(-100.0f, peakLevel - 0.5f);

    repaint();
}

void BandMeter::paint(juce::Graphics& g)
{
    auto bounds = getLocalBounds().toFloat();
    float barW = bounds.getWidth() * 0.6f;
    float barX = bounds.getCentreX() - barW * 0.5f;
    float barH = bounds.getHeight() - 18.0f;

    auto dbToY = [&](float db) {
        float norm = (db + 60.0f) / 60.0f;
        norm = juce::jlimit(0.0f, 1.0f, norm);
        return bounds.getY() + barH * (1.0f - norm);
    };

    g.setColour(OTT::panel);
    g.fillRoundedRectangle(barX, bounds.getY(), barW, barH, 3.0f);

    g.setColour(OTT::separator);
    for (int db = -60; db <= 0; db += 12)
    {
        float y = dbToY((float)db);
        g.drawHorizontalLine((int)y, barX, barX + barW);
    }

    float levelY = dbToY(level);
    g.setColour(colour);
    g.fillRoundedRectangle(barX, levelY, barW, bounds.getY() + barH - levelY, 2.0f);

    float peakY = dbToY(peakLevel);
    g.setColour(colour.withAlpha(0.5f));
    g.drawHorizontalLine((int)peakY, barX, barX + barW);

    float zeroY = dbToY(0.0f);
    if (gainReduction < -0.1f)
    {
        float grEndY = dbToY(juce::jmax(-36.0f, gainReduction));
        g.setColour(OTT::downColour);
        g.fillRoundedRectangle(barX + barW + 3.0f, zeroY, 4.0f, grEndY - zeroY, 1.0f);
    }
    else if (gainReduction > 0.1f)
    {
        float grEndY = dbToY(juce::jmin(36.0f, gainReduction));
        g.setColour(OTT::upColour);
        g.fillRoundedRectangle(barX - 7.0f, grEndY, 4.0f, zeroY - grEndY, 1.0f);
    }

    g.setColour(colour);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    auto labels = { "LOW", "MID", "HIGH" };
    g.drawText(*(labels.begin() + juce::jmin(band, 2)),
               bounds.removeFromBottom(16.0f).toNearestInt(),
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

    lowMeter = std::make_unique<BandMeter>(processorRef, 0, OTT::lowColour);
    midMeter = std::make_unique<BandMeter>(processorRef, 1, OTT::midColour);
    highMeter = std::make_unique<BandMeter>(processorRef, 2, OTT::highColour);

    addAndMakeVisible(*lowMeter);
    addAndMakeVisible(*midMeter);
    addAndMakeVisible(*highMeter);

    setSize(580, 320);
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
    k.label->setFont(juce::FontOptions(9.0f, juce::Font::bold));
    k.label->setColour(juce::Label::textColourId, OTT::textDim);
    addAndMakeVisible(*k.slider);
    addAndMakeVisible(*k.label);
    return k;
}

void VibeOTTEditor::paint(juce::Graphics& g)
{
    g.fillAll(OTT::bg);

    g.setColour(OTT::knobFill);
    g.setFont(juce::FontOptions(24.0f, juce::Font::bold));
    g.drawText("OTT", 0, 8, getWidth(), 30, juce::Justification::centred, false);

    g.setColour(OTT::textDim);
    g.setFont(juce::FontOptions(8.0f));
    g.drawText("VIBE AUDIO", 0, 32, getWidth(), 12, juce::Justification::centred, false);

    g.setColour(OTT::separator);
    g.drawHorizontalLine(48, 10.0f, (float)getWidth() - 10.0f);

    g.setColour(OTT::textDim);
    g.setFont(juce::FontOptions(8.0f));
    g.drawText("OUTPUT", getWidth() - 250, 48, 60, 12, juce::Justification::centred, false);
}

void VibeOTTEditor::resized()
{
    int knobSize = 52;
    int knobSpacing = 16;
    int knobY = 60;
    int labelH = 14;
    int startX = 12;

    auto knobs = { &depthSlider, &upwardRatioSlider, &downwardRatioSlider,
                   &lowGainSlider, &midGainSlider, &highGainSlider, &outputGainSlider };

    int i = 0;
    for (auto* k : knobs)
    {
        int x = startX + i * (knobSize + knobSpacing);
        k->slider->setBounds(x, knobY, knobSize, knobSize);
        k->label->setBounds(x, knobY + knobSize, knobSize, labelH);
        ++i;
    }

    int meterY = 60;
    int meterH = 220;
    int meterW = 36;
    int meterSpacing = 48;
    int meterStartX = getWidth() - 170;

    lowMeter->setBounds(meterStartX + meterSpacing * 0, meterY, meterW, meterH);
    midMeter->setBounds(meterStartX + meterSpacing * 1, meterY, meterW, meterH);
    highMeter->setBounds(meterStartX + meterSpacing * 2, meterY, meterW, meterH);
}
