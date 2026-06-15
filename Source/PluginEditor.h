#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class OTTLookAndFeel : public juce::LookAndFeel_V4
{
public:
    OTTLookAndFeel();
    void drawRotarySlider(juce::Graphics&, int x, int y, int width, int height,
                          float sliderPos, float rotaryStartAngle,
                          float rotaryEndAngle, juce::Slider&) override;
};

class VibeOTTEditor : public juce::AudioProcessorEditor
{
public:
    VibeOTTEditor(VibeOTTProcessor&);
    ~VibeOTTEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;

private:
    VibeOTTProcessor& processorRef;
    OTTLookAndFeel ottLookAndFeel;

    struct Knob
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    Knob depthSlider;
    Knob timeSlider;
    Knob upwardRatioSlider;
    Knob downwardRatioSlider;
    Knob lowGainSlider;
    Knob midGainSlider;
    Knob highGainSlider;

    Knob createKnob(const juce::String& paramId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VibeOTTEditor)
};
