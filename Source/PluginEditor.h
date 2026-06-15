#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "PluginProcessor.h"

class VibeOTTEditor : public juce::AudioProcessorEditor,
                      public juce::Slider::Listener
{
public:
    VibeOTTEditor(VibeOTTProcessor&);
    ~VibeOTTEditor() override;

    void paint(juce::Graphics&) override;
    void resized() override;
    void sliderValueChanged(juce::Slider*) override {}

private:
    VibeOTTProcessor& processorRef;

    struct SliderWithAttachment
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
        std::unique_ptr<juce::Label> label;
    };

    SliderWithAttachment createSlider(const juce::String& paramId, const juce::String& labelText);

    SliderWithAttachment depthSlider;
    SliderWithAttachment mixSlider;
    SliderWithAttachment crossoverLowMidSlider;
    SliderWithAttachment crossoverMidHighSlider;

    SliderWithAttachment upThresholdSlider;
    SliderWithAttachment upRatioSlider;
    SliderWithAttachment upAttackSlider;
    SliderWithAttachment upReleaseSlider;

    SliderWithAttachment downThresholdSlider;
    SliderWithAttachment downRatioSlider;
    SliderWithAttachment downAttackSlider;
    SliderWithAttachment downReleaseSlider;

    SliderWithAttachment lowGainSlider;
    SliderWithAttachment midGainSlider;
    SliderWithAttachment highGainSlider;

    juce::LookAndFeel_V4 lookAndFeel;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VibeOTTEditor)
};
