#pragma once

#include <juce_audio_processors/juce_audio_processors.h>
#include "MultibandCompressor.h"

class VibeOTTProcessor : public juce::AudioProcessor
{
public:
    VibeOTTProcessor();
    ~VibeOTTProcessor() override;

    void prepareToPlay(double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported(const BusesLayout& layouts) const override;

    void processBlock(juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VibeOTT"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram(int) override {}
    const juce::String getProgramName(int) override { return {}; }
    void changeProgramName(int, const juce::String&) override {}

    void getStateInformation(juce::MemoryBlock& destData) override;
    void setStateInformation(const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

private:
    MultibandCompressor compressor;
    juce::AudioBuffer<float> dryBuffer;

    std::atomic<float>* depthParam = nullptr;
    std::atomic<float>* mixParam = nullptr;
    std::atomic<float>* crossoverLowMidParam = nullptr;
    std::atomic<float>* crossoverMidHighParam = nullptr;

    std::atomic<float>* upThresholdParam = nullptr;
    std::atomic<float>* upRatioParam = nullptr;
    std::atomic<float>* upAttackParam = nullptr;
    std::atomic<float>* upReleaseParam = nullptr;

    std::atomic<float>* downThresholdParam = nullptr;
    std::atomic<float>* downRatioParam = nullptr;
    std::atomic<float>* downAttackParam = nullptr;
    std::atomic<float>* downReleaseParam = nullptr;

    std::atomic<float>* lowGainParam = nullptr;
    std::atomic<float>* midGainParam = nullptr;
    std::atomic<float>* highGainParam = nullptr;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR(VibeOTTProcessor)
};
