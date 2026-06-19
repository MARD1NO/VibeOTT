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

class BandMeter : public juce::Component,
                   private juce::Timer
{
public:
    BandMeter(VibeOTTProcessor& p, int bandIndex, juce::Colour colour);

    void paint(juce::Graphics&) override;

    float getLevel() const { return level; }
    float getGainReduction() const { return gainReduction; }

private:
    void timerCallback() override;

    VibeOTTProcessor& processor;
    int band;
    juce::Colour colour;
    float level = -100.0f;
    float gainReduction = 0.0f;
    float peakLevel = -100.0f;
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
        std::unique_ptr<juce::Label> label;
    };

    Knob depthSlider;
    Knob upwardRatioSlider;
    Knob downwardRatioSlider;
    Knob lowGainSlider;
    Knob midGainSlider;
    Knob highGainSlider;

    struct ThresholdSlider
    {
        std::unique_ptr<juce::Slider> slider;
        std::unique_ptr<juce::AudioProcessorValueTreeState::SliderAttachment> attachment;
    };

    ThresholdSlider lowUpThresh, midUpThresh, highUpThresh;
    ThresholdSlider lowDownThresh, midDownThresh, highDownThresh;

    std::unique_ptr<BandMeter> lowMeter;
    std::unique_ptr<BandMeter> midMeter;
    std::unique_ptr<BandMeter> highMeter;

    Knob createKnob(const juce::String& paramId, const juce::String& labelText);
    ThresholdSlider createThresholdSlider(const juce::String& paramId);

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VibeOTTEditor)
};
