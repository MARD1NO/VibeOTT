#include "PluginEditor.h"

namespace OTTColours
{
    static const juce::Colour background    { 20, 20, 20 };
    static const juce::Colour panel         { 30, 30, 30 };
    static const juce::Colour division       { 45, 45, 45 };
    static const juce::Colour track         { 50, 50, 55 };
    static const juce::Colour fill          { 210, 210, 220 };
    static const juce::Colour text          { 170, 170, 180 };
    static const juce::Colour textDim       { 100, 100, 110 };
    static const juce::Colour lowColour     { 80, 160, 255 };
    static const juce::Colour midColour     { 160, 100, 255 };
    static const juce::Colour highColour    { 255, 180, 60 };
    static const juce::Colour upColour      { 255, 120, 40 };
    static const juce::Colour downColour    { 60, 200, 160 };
}

OTTLookAndFeel::OTTLookAndFeel()
{
    setColour(juce::Slider::textBoxTextColourId, OTTColours::text);
    setColour(juce::Slider::textBoxBackgroundColourId, OTTColours::panel);
    setColour(juce::Slider::textBoxOutlineColourId, juce::Colour(0));
    setColour(juce::Slider::textBoxHighlightColourId, OTTColours::fill);
}

void OTTLookAndFeel::drawRotarySlider(juce::Graphics& g, int x, int y, int width, int height,
                                       float sliderPos, float rotaryStartAngle,
                                       float rotaryEndAngle, juce::Slider& slider)
{
    auto radius = (float)juce::jmin(width / 2, height / 2) - 4.0f;
    auto centreX = (float)x + (float)width * 0.5f;
    auto centreY = (float)y + (float)height * 0.5f;
    auto angle = rotaryStartAngle + sliderPos * (rotaryEndAngle - rotaryStartAngle);

    const float arcThickness = 2.5f;

    juce::Path trackArc;
    trackArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                           rotaryStartAngle, rotaryEndAngle, true);
    g.setColour(OTTColours::track);
    g.strokePath(trackArc, juce::PathStrokeType(arcThickness));

    auto* knobColour = slider.getProperties().getVarPointer("knobColour");
    juce::Colour fillColour = knobColour
        ? juce::Colour((uint32_t)(juce::int64)*knobColour)
        : OTTColours::fill;

    juce::Path fillArc;
    fillArc.addCentredArc(centreX, centreY, radius, radius, 0.0f,
                          rotaryStartAngle, angle, true);
    g.setColour(fillColour);
    g.strokePath(fillArc, juce::PathStrokeType(arcThickness));

    float innerRadius = radius * 0.35f;
    juce::Path dot;
    dot.addEllipse(centreX - innerRadius * 0.5f, centreY - radius - innerRadius * 0.5f,
                   innerRadius, innerRadius);
    g.setColour(fillColour);
    g.fillPath(dot);

    auto thumbAngle = angle;
    auto thumbX = centreX + std::sin(thumbAngle) * (radius - 1.0f);
    auto thumbY = centreY - std::cos(thumbAngle) * (radius - 1.0f);
    g.setColour(fillColour);
    g.fillEllipse(thumbX - 3.0f, thumbY - 3.0f, 6.0f, 6.0f);
}

VibeOTTEditor::VibeOTTEditor(VibeOTTProcessor& p)
    : AudioProcessorEditor(&p), processorRef(p)
{
    setLookAndFeel(&ottLookAndFeel);

    depthSlider = createKnob("DEPTH");
    mixSlider = createKnob("MIX");

    upThresholdSlider = createKnob("UP_THRESHOLD");
    upRatioSlider = createKnob("UP_RATIO");
    upAttackSlider = createKnob("UP_ATTACK");
    upReleaseSlider = createKnob("UP_RELEASE");

    downThresholdSlider = createKnob("DOWN_THRESHOLD");
    downRatioSlider = createKnob("DOWN_RATIO");
    downAttackSlider = createKnob("DOWN_ATTACK");
    downReleaseSlider = createKnob("DOWN_RELEASE");

    lowGainSlider = createKnob("LOW_GAIN");
    midGainSlider = createKnob("MID_GAIN");
    highGainSlider = createKnob("HIGH_GAIN");

    upThresholdSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::upColour.getARGB());
    upRatioSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::upColour.getARGB());
    upAttackSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::upColour.getARGB());
    upReleaseSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::upColour.getARGB());

    downThresholdSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::downColour.getARGB());
    downRatioSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::downColour.getARGB());
    downAttackSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::downColour.getARGB());
    downReleaseSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::downColour.getARGB());

    lowGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::lowColour.getARGB());
    midGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::midColour.getARGB());
    highGainSlider.slider->getProperties().set("knobColour", (juce::int64)OTTColours::highColour.getARGB());

    setSize(380, 400);
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
    g.fillAll(OTTColours::background);

    auto bounds = getLocalBounds();

    g.setColour(OTTColours::fill);
    g.setFont(juce::FontOptions(18.0f, juce::Font::bold));
    g.drawText("OTT", bounds.removeFromTop(32), juce::Justification::centred, false);

    g.setColour(OTTColours::textDim);
    g.setFont(juce::FontOptions(8.0f));
    g.drawText("VIBE", bounds.removeFromTop(12), juce::Justification::centred, false);

    auto rowLabels = { "LOW", "MID", "HIGH" };
    auto colours = { OTTColours::lowColour, OTTColours::midColour, OTTColours::highColour };
    auto y = 48;

    auto coloursIter = colours.begin();
    for (auto& label : rowLabels)
    {
        auto c = *coloursIter;
        ++coloursIter;
        g.setColour(c);
        g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
        g.drawText(label, juce::Rectangle<int>(4, y, 40, 16), juce::Justification::centredLeft, false);
        y += 110;
    }

    g.setColour(OTTColours::division);
    g.drawHorizontalLine(44, 0, (float)getWidth());
    g.drawHorizontalLine(152, 0, (float)getWidth());
    g.drawHorizontalLine(262, 0, (float)getWidth());

    g.setColour(OTTColours::textDim);
    g.setFont(juce::FontOptions(7.0f));
    auto labelY = 48;
    auto colX = 52;
    auto colW = 48;

    const char* colLabels[] = { "THR", "RAT", "ATK", "RLS" };
    for (int c = 0; c < 4; ++c)
    {
        g.drawFittedText(colLabels[c], colX + c * colW, labelY, colW, 10, juce::Justification::centred, 1);
    }

    auto upY = 167;
    g.setColour(OTTColours::upColour);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText("UP", juce::Rectangle<int>(4, upY, 40, 14), juce::Justification::centredLeft, false);
    g.setColour(OTTColours::textDim);
    g.setFont(juce::FontOptions(7.0f));
    for (int c = 0; c < 4; ++c)
    {
        g.drawFittedText(colLabels[c], colX + c * colW, upY + 14, colW, 10, juce::Justification::centred, 1);
    }

    auto downY = 275;
    g.setColour(OTTColours::downColour);
    g.setFont(juce::FontOptions(9.0f, juce::Font::bold));
    g.drawText("DOWN", juce::Rectangle<int>(4, downY, 40, 14), juce::Justification::centredLeft, false);
    g.setColour(OTTColours::textDim);
    g.setFont(juce::FontOptions(7.0f));
    for (int c = 0; c < 4; ++c)
    {
        g.drawFittedText(colLabels[c], colX + c * colW, downY + 14, colW, 10, juce::Justification::centred, 1);
    }

    g.setColour(OTTColours::textDim);
    g.setFont(juce::FontOptions(7.0f));
    g.drawText("DPTH", getWidth() - 80, 48, 36, 10, juce::Justification::centred, 1);
    g.drawText("MIX", getWidth() - 40, 48, 36, 10, juce::Justification::centred, 1);

    g.drawText("GAIN", getWidth() - 50, 158 + 24, 44, 10, juce::Justification::centred, 1);
}

void VibeOTTEditor::resized()
{
    auto knobSize = 40;
    auto colX = 52;
    auto colW = 48;

    auto setKnobRow = [&](Knob& k1, Knob& k2, Knob& k3, Knob& k4, int y)
    {
        k1.slider->setBounds(colX, y, knobSize, knobSize);
        k2.slider->setBounds(colX + colW, y, knobSize, knobSize);
        k3.slider->setBounds(colX + colW * 2, y, knobSize, knobSize);
        k4.slider->setBounds(colX + colW * 3, y, knobSize, knobSize);
    };

    auto bandY = 58;
    setKnobRow(lowGainSlider, midGainSlider, highGainSlider, depthSlider, bandY);

    auto upY = 185;
    setKnobRow(upThresholdSlider, upRatioSlider, upAttackSlider, upReleaseSlider, upY);

    auto downY = 293;
    setKnobRow(downThresholdSlider, downRatioSlider, downAttackSlider, downReleaseSlider, downY);

    mixSlider.slider->setBounds(getWidth() - 50, 58, 40, 40);
}
