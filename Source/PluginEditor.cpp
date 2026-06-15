#include "PluginEditor.h"

namespace OTT
{
    static const juce::Colour bg           { 25, 25, 25 };
    static const juce::Colour panel        { 33, 33, 33 };
    static const juce::Colour separator    { 55, 55, 55 };
    static const juce::Colour track        { 50, 50, 55 };
    static const juce::Colour knobFill     { 200, 200, 210 };
    static const juce::Colour text         { 160, 160, 170 };
    static const juce::Colour textDim      { 90, 90, 100 };
    static const juce::Colour lowColour    { 70, 145, 255 };
    static const juce::Colour midColour    { 150, 90, 255 };
    static const juce::Colour highColour   { 255, 170, 50 };
    static const juce::Colour upColour     { 255, 110, 30 };
    static const juce::Colour downColour   { 50, 200, 150 };
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
    g.fillEllipse(dx - 3.5f, dy - 3.5f, 7.0f, 7.0f);
}

VibeOTTEditor::VibeOTTEditor(VibeOTTProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&ottLookAndFeel);

    depthSlider = createKnob("DEPTH");
    timeSlider = createKnob("TIME");
    upwardRatioSlider = createKnob("UPWARD_RATIO");
    downwardRatioSlider = createKnob("DOWNWARD_RATIO");
    lowGainSlider = createKnob("LOW_GAIN");
    midGainSlider = createKnob("MID_GAIN");
    highGainSlider = createKnob("HIGH_GAIN");

    depthSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::knobFill.getARGB());
    timeSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::knobFill.getARGB());
    upwardRatioSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::upColour.getARGB());
    downwardRatioSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::downColour.getARGB());
    lowGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::lowColour.getARGB());
    midGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::midColour.getARGB());
    highGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTT::highColour.getARGB());

    setSize(350, 280);
}

VibeOTTEditor::~VibeOTTEditor()
{
    setLookAndFeel(nullptr);
}

VibeOTTEditor::Knob VibeOTTEditor::createKnob(const juce::String& paramId)
{
    Knob k;
    k.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                               juce::Slider::NoTextBox);
    k.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processorRef.apvts, paramId, *k.slider);
    addAndMakeVisible(*k.slider);
    return k;
}

void VibeOTTEditor::paint(juce::Graphics& g)
{
    g.fillAll(OTT::bg);

    auto w = (float)getWidth();
    (void)w;

    g.setColour(OTT::knobFill);
    g.setFont(juce::FontOptions(22.0f, juce::Font::bold));
    g.drawText("OTT", getLocalBounds().removeFromTop(36), juce::Justification::centred, false);

    g.setColour(OTT::textDim);
    g.setFont(juce::FontOptions(8.0f));
    g.drawText("VIBE AUDIO", getLocalBounds().withHeight(12).withY(30),
               juce::Justification::centred, false);

    g.setColour(OTT::separator);
    g.drawHorizontalLine(44, 8.0f, getWidth() - 8.0f);

    int labelY = 56;
    int knobW = 56;
    int knobH = 56;
    int spacing = 16;
    int startX = 12;

    struct LabelInfo { const char* name; juce::Colour colour; };
    LabelInfo labels[] = {
        { "DEPTH",  OTT::knobFill },
        { "TIME",   OTT::knobFill },
        { "UP",     OTT::upColour },
        { "DOWN",   OTT::downColour },
        { "LOW",    OTT::lowColour },
        { "MID",    OTT::midColour },
        { "HIGH",   OTT::highColour },
    };

    for (int i = 0; i < 7; ++i)
    {
        int x = startX + i * (knobW + spacing);
        g.setColour(labels[i].colour);
        g.setFont(juce::FontOptions(8.0f, juce::Font::bold));
        g.drawText(labels[i].name, juce::Rectangle<int>(x, labelY, knobW, 12),
                   juce::Justification::centred, false);
    }

    g.setColour(OTT::separator);
    g.drawHorizontalLine(130, 8.0f, getWidth() - 8.0f);
}

void VibeOTTEditor::resized()
{
    int knobSize = 56;
    int spacing = 16;
    int startX = 12;
    int knobY = 68;
    int secondRowY = 150;

    depthSlider.slider->setBounds(startX + 0 * (knobSize + spacing), knobY, knobSize, knobSize);
    timeSlider.slider->setBounds(startX + 1 * (knobSize + spacing), knobY, knobSize, knobSize);
    upwardRatioSlider.slider->setBounds(startX + 2 * (knobSize + spacing), knobY, knobSize, knobSize);
    downwardRatioSlider.slider->setBounds(startX + 3 * (knobSize + spacing), knobY, knobSize, knobSize);

    lowGainSlider.slider->setBounds(startX + 4 * (knobSize + spacing), knobY, knobSize, knobSize);
    midGainSlider.slider->setBounds(startX + 5 * (knobSize + spacing), knobY, knobSize, knobSize);
    highGainSlider.slider->setBounds(startX + 6 * (knobSize + spacing), knobY, knobSize, knobSize);
}
