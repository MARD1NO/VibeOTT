#include "PluginEditor.h"

namespace Colours
{
    static const juce::Colour background { 30, 30, 35 };
    static const juce::Colour panel { 42, 42, 50 };
    static const juce::Colour panelLight { 55, 55, 65 };
    static const juce::Colour accent { 0, 180, 255 };
    static const juce::Colour accentDim { 0, 120, 180 };
    static const juce::Colour text { 220, 220, 230 };
    static const juce::Colour textDim { 140, 140, 155 };
    static const juce::Colour upColor { 255, 140, 50 };
    static const juce::Colour downColor { 50, 200, 150 };
    static const juce::Colour lowColor { 50, 180, 255 };
    static const juce::Colour midColor { 180, 120, 255 };
    static const juce::Colour highColor { 255, 200, 80 };
}

VibeOTTEditor::VibeOTTEditor(VibeOTTProcessor& p)
    : AudioProcessorEditor(&p), processor(p)
{
    setLookAndFeel(&lookAndFeel);

    depthSlider = createSlider("DEPTH", "Depth");
    mixSlider = createSlider("MIX", "Mix");
    crossoverLowMidSlider = createSlider("CROSSOVER_LOW_MID", "X-Over Lo/Mid");
    crossoverMidHighSlider = createSlider("CROSSOVER_MID_HIGH", "X-Over Mid/Hi");

    upThresholdSlider = createSlider("UP_THRESHOLD", "Threshold");
    upRatioSlider = createSlider("UP_RATIO", "Ratio");
    upAttackSlider = createSlider("UP_ATTACK", "Attack");
    upReleaseSlider = createSlider("UP_RELEASE", "Release");

    downThresholdSlider = createSlider("DOWN_THRESHOLD", "Threshold");
    downRatioSlider = createSlider("DOWN_RATIO", "Ratio");
    downAttackSlider = createSlider("DOWN_ATTACK", "Attack");
    downReleaseSlider = createSlider("DOWN_RELEASE", "Release");

    lowGainSlider = createSlider("LOW_GAIN", "Low");
    midGainSlider = createSlider("MID_GAIN", "Mid");
    highGainSlider = createSlider("HIGH_GAIN", "High");

    setSize(620, 520);
}

VibeOTTEditor::~VibeOTTEditor()
{
    setLookAndFeel(nullptr);
}

VibeOTTEditor::SliderWithAttachment VibeOTTEditor::createSlider(
    const juce::String& paramId, const juce::String& labelText)
{
    SliderWithAttachment swa;
    swa.slider = std::make_unique<juce::Slider>(juce::Slider::RotaryHorizontalVerticalDrag,
                                                  juce::Slider::TextBoxBelow);
    swa.slider->setSliderStyle(juce::Slider::RotaryHorizontalVerticalDrag);
    swa.slider->setTextBoxStyle(juce::Slider::TextBoxBelow, false, 60, 18);
    swa.slider->setColour(juce::Slider::rotarySliderFillColourId, Colours::accent);
    swa.slider->setColour(juce::Slider::rotarySliderOutlineColourId, Colours::panelLight);
    swa.slider->setColour(juce::Slider::thumbColourId, Colours::accent);
    swa.slider->setColour(juce::Slider::textBoxTextColourId, Colours::text);
    swa.slider->setColour(juce::Slider::textBoxBackgroundColourId, Colours::panel);
    swa.slider->setColour(juce::Slider::textBoxOutlineColourId, Colours::panelLight);

    swa.label = std::make_unique<juce::Label>(paramId + "_label", labelText);
    swa.label->setJustificationType(juce::Justification::centred);
    swa.label->setColour(juce::Label::textColourId, Colours::textDim);
    swa.label->setFont(juce::Font(11.0f));

    swa.attachment = std::make_unique<juce::AudioProcessorValueTreeState::SliderAttachment>(
        processor.apvts, paramId, *swa.slider);

    addAndMakeVisible(*swa.slider);
    addAndMakeVisible(*swa.label);

    return swa;
}

void VibeOTTEditor::paint(juce::Graphics& g)
{
    g.fillAll(Colours::background);

    auto bounds = getLocalBounds().reduced(10);
    auto topArea = bounds.removeFromTop(50);

    g.setColour(Colours::accent);
    g.setFont(juce::Font(24.0f, juce::Font::bold));
    g.drawText("VIBEOTT", topArea, juce::Justification::centred, true);

    g.setFont(juce::Font(10.0f));
    g.setColour(Colours::textDim);
    g.drawText("v1.0", topArea.removeFromRight(40), juce::Justification::centredRight, true);

    auto upArea = bounds.removeFromLeft(bounds.getWidth() / 3).reduced(5);
    g.setColour(Colours::panel);
    g.fillRoundedRectangle(upArea.toFloat(), 6.0f);
    g.setColour(Colours::upColor);
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText("UPWARD", upArea.removeFromTop(22), juce::Justification::centred, true);

    auto downArea = bounds.removeFromLeft(bounds.getWidth() / 2).reduced(5);
    g.setColour(Colours::panel);
    g.fillRoundedRectangle(downArea.toFloat(), 6.0f);
    g.setColour(Colours::downColor);
    g.setFont(juce::Font(13.0f, juce::Font::bold));
    g.drawText("DOWNWARD", downArea.removeFromTop(22), juce::Justification::centred, true);

    auto rightArea = bounds.reduced(5);
    g.setColour(Colours::panel);
    g.fillRoundedRectangle(rightArea.toFloat(), 6.0f);
}

void VibeOTTEditor::resized()
{
    auto bounds = getLocalBounds().reduced(10);
    bounds.removeFromTop(50);

    const int sliderHeight = 90;
    const int labelHeight = 16;
    const int gap = 8;
    const int colWidth = (bounds.getWidth() - 20) / 3;

    auto upArea = bounds.removeFromLeft(colWidth).reduced(5);
    upArea.removeFromTop(22);

    auto downArea = bounds.removeFromLeft(colWidth).reduced(5);
    downArea.removeFromTop(22);

    auto rightArea = bounds.reduced(5);

    auto layoutColumn = [&](auto& area, auto& s1, auto& s2, auto& s3, auto& s4)
    {
        auto row = area.removeFromTop(sliderHeight + labelHeight);
        int sw = row.getWidth() / 2;
        auto left = row.removeFromLeft(sw);
        auto right = row;

        auto r1 = left.removeFromTop(labelHeight);
        s1.label->setBounds(r1);
        s1.slider->setBounds(left.removeFromTop(sliderHeight).reduced(gap));

        auto r2 = right.removeFromTop(labelHeight);
        s2.label->setBounds(r2);
        s2.slider->setBounds(right.removeFromTop(sliderHeight).reduced(gap));

        row = area.removeFromTop(sliderHeight + labelHeight);
        left = row.removeFromLeft(sw);
        right = row;

        r1 = left.removeFromTop(labelHeight);
        s3.label->setBounds(r1);
        s3.slider->setBounds(left.removeFromTop(sliderHeight).reduced(gap));

        r2 = right.removeFromTop(labelHeight);
        s4.label->setBounds(r2);
        s4.slider->setBounds(right.removeFromTop(sliderHeight).reduced(gap));
    };

    layoutColumn(upArea, upThresholdSlider, upRatioSlider, upAttackSlider, upReleaseSlider);
    for (auto* s : { upThresholdSlider.slider.get(), upRatioSlider.slider.get(),
                     upAttackSlider.slider.get(), upReleaseSlider.slider.get() })
        s->setColour(juce::Slider::rotarySliderFillColourId, Colours::upColor);

    layoutColumn(downArea, downThresholdSlider, downRatioSlider, downAttackSlider, downReleaseSlider);
    for (auto* s : { downThresholdSlider.slider.get(), downRatioSlider.slider.get(),
                     downAttackSlider.slider.get(), downReleaseSlider.slider.get() })
        s->setColour(juce::Slider::rotarySliderFillColourId, Colours::downColor);

    {
        auto rightTop = rightArea.removeFromTop(22);
        auto row1 = rightArea.removeFromTop(sliderHeight + labelHeight);
        int sw = row1.getWidth() / 2;
        auto left = row1.removeFromLeft(sw);
        auto right = row1;

        auto r = left.removeFromTop(labelHeight);
        depthSlider.label->setBounds(r);
        depthSlider.slider->setBounds(left.removeFromTop(sliderHeight).reduced(gap));

        r = right.removeFromTop(labelHeight);
        mixSlider.label->setBounds(r);
        mixSlider.slider->setBounds(right.removeFromTop(sliderHeight).reduced(gap));

        auto row2 = rightArea.removeFromTop(sliderHeight + labelHeight);
        left = row2.removeFromLeft(sw);
        right = row2;

        r = left.removeFromTop(labelHeight);
        crossoverLowMidSlider.label->setBounds(r);
        crossoverLowMidSlider.slider->setBounds(left.removeFromTop(sliderHeight).reduced(gap));

        r = right.removeFromTop(labelHeight);
        crossoverMidHighSlider.label->setBounds(r);
        crossoverMidHighSlider.slider->setBounds(right.removeFromTop(sliderHeight).reduced(gap));
    }

    {
        auto bandArea = rightArea;
        auto row = bandArea.removeFromTop(sliderHeight + labelHeight);
        int bw = row.getWidth() / 3;
        auto lowCol = row.removeFromLeft(bw);
        auto midCol = row.removeFromLeft(bw);
        auto highCol = row;

        auto r = lowCol.removeFromTop(labelHeight);
        lowGainSlider.label->setBounds(r);
        lowGainSlider.slider->setBounds(lowCol.removeFromTop(sliderHeight).reduced(gap));
        lowGainSlider.slider->setColour(juce::Slider::rotarySliderFillColourId, Colours::lowColor);
        lowGainSlider.label->setColour(juce::Label::textColourId, Colours::lowColor);

        r = midCol.removeFromTop(labelHeight);
        midGainSlider.label->setBounds(r);
        midGainSlider.slider->setBounds(midCol.removeFromTop(sliderHeight).reduced(gap));
        midGainSlider.slider->setColour(juce::Slider::rotarySliderFillColourId, Colours::midColor);
        midGainSlider.label->setColour(juce::Label::textColourId, Colours::midColor);

        r = highCol.removeFromTop(labelHeight);
        highGainSlider.label->setBounds(r);
        highGainSlider.slider->setBounds(highCol.removeFromTop(sliderHeight).reduced(gap));
        highGainSlider.slider->setColour(juce::Slider::rotarySliderFillColourId, Colours::highColor);
        highGainSlider.label->setColour(juce::Label::textColourId, Colours::highColor);
    }
}
