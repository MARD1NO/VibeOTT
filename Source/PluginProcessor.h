#pragma once

#include <juce_audio_processors/juce_audio_processors.h>

#include "dsp/OttModule.h"

//==============================================================================
/** Parameter identifiers.

    These are the plugin's public ABI: they appear in saved sessions, so they
    must not be renamed once shipped.
*/
namespace ParameterIDs
{
    inline constexpr const char* mix      = "MIX";
    inline constexpr const char* depth    = "DEPTH";
    inline constexpr const char* upward   = "UPWARD";
    inline constexpr const char* downward = "DOWNWARD";

    /** Single time macro. It drives both the attack and the release envelope
        coefficients, so it is the only timing control the plugin exposes —
        there is no separate attack/release pair to fall out of sync with it. */
    inline constexpr const char* time     = "TIME";

    inline constexpr const char* inputGain  = "INPUT_GAIN";
    inline constexpr const char* outputGain = "OUTPUT_GAIN";
    inline constexpr const char* behavior   = "BEHAVIOR";

    inline constexpr const char* lowCrossover  = "LOW_CROSSOVER";
    inline constexpr const char* highCrossover = "HIGH_CROSSOVER";

    inline const char* const bandNames[ott::numBands] = { "LOW", "MID", "HIGH" };

    inline juce::String upwardThreshold (int band)   { return juce::String (bandNames[band]) + "_UP_THRESHOLD"; }
    inline juce::String upwardRatio (int band)       { return juce::String (bandNames[band]) + "_UP_RATIO"; }
    inline juce::String downwardThreshold (int band) { return juce::String (bandNames[band]) + "_DOWN_THRESHOLD"; }
    inline juce::String downwardRatio (int band)     { return juce::String (bandNames[band]) + "_DOWN_RATIO"; }
    inline juce::String bandGain (int band)          { return juce::String (bandNames[band]) + "_GAIN"; }
}

//==============================================================================
class VibeOTTProcessor : public juce::AudioProcessor
{
public:
    VibeOTTProcessor();
    ~VibeOTTProcessor() override;

    //== AudioProcessor ========================================================
    void prepareToPlay (double sampleRate, int samplesPerBlock) override;
    void releaseResources() override;

    bool isBusesLayoutSupported (const BusesLayout& layouts) const override;

    void processBlock (juce::AudioBuffer<float>&, juce::MidiBuffer&) override;

    juce::AudioProcessorEditor* createEditor() override;
    bool hasEditor() const override { return true; }

    const juce::String getName() const override { return "VibeOTT"; }

    bool acceptsMidi() const override { return false; }
    bool producesMidi() const override { return false; }
    double getTailLengthSeconds() const override { return 0.0; }

    int getNumPrograms() override { return 1; }
    int getCurrentProgram() override { return 0; }
    void setCurrentProgram (int) override {}
    const juce::String getProgramName (int) override { return {}; }
    void changeProgramName (int, const juce::String&) override {}

    void getStateInformation (juce::MemoryBlock& destData) override;
    void setStateInformation (const void* data, int sizeInBytes) override;

    juce::AudioProcessorValueTreeState apvts;

    //== Editor access =========================================================
    /** The latest per-band metering snapshot. Safe to call from the message
        thread; the value is rebuilt once per block. */
    ott::BandLevels getBandLevels() const;

private:
    juce::AudioProcessorValueTreeState::ParameterLayout createParameterLayout();

    /** Reads every parameter into a plain struct. Called once per block so a
        parameter change cannot tear part-way through a block. */
    ott::Parameters readParameters() const;

    /** Applies the parameter set to the DSP engine. */
    void applyParameters();

    void cacheParameterPointers();

    /** Translates an old session's parameters onto the current set.

        Writing to the APVTS parameters directly (rather than setting properties
        on the state tree before replaceState) is deliberate: replaceState syncs
        a parameter only when the tree's value differs, so a migrated value equal
        to the parameter's current value is silently dropped. */
    void migrateLegacyState (const juce::ValueTree& state);

    ott::Module dsp;

    std::atomic<float>* mixParam          = nullptr;
    std::atomic<float>* depthParam        = nullptr;
    std::atomic<float>* upwardParam       = nullptr;
    std::atomic<float>* downwardParam     = nullptr;
    std::atomic<float>* inputGainParam    = nullptr;
    std::atomic<float>* outputGainParam   = nullptr;
    std::atomic<float>* behaviorParam     = nullptr;
    std::atomic<float>* timeParam         = nullptr;
    std::atomic<float>* lowCrossoverParam = nullptr;
    std::atomic<float>* highCrossoverParam = nullptr;

    std::atomic<float>* upwardThresholdParam[ott::numBands] {};
    std::atomic<float>* upwardRatioParam[ott::numBands] {};
    std::atomic<float>* downwardThresholdParam[ott::numBands] {};
    std::atomic<float>* downwardRatioParam[ott::numBands] {};
    std::atomic<float>* bandGainParam[ott::numBands] {};

    /** The metering snapshot, published under a lock so the editor can read it
        without touching the DSP. It is a handful of floats, so a spin lock
        costs less than the alternative of lock-free tripple buffering here. */
    mutable juce::SpinLock meteringLock;
    ott::BandLevels meteringSnapshot;

    JUCE_DECLARE_NON_COPYABLE_WITH_LEAK_DETECTOR (VibeOTTProcessor)
};
