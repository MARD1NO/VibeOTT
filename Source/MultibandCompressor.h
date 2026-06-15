#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>

class MultibandCompressor
{
public:
    static constexpr int numBands = 3;

    struct BandCompressor
    {
        float upwardThreshold = -36.0f;
        float upwardRatio = 3.0f;
        float upwardAttack = 10.0f;
        float upwardRelease = 50.0f;
        float downwardThreshold = -18.0f;
        float downwardRatio = 3.0f;
        float downwardAttack = 10.0f;
        float downwardRelease = 50.0f;
        float gainDb = 0.0f;

        std::array<float, 2> downwardEnvelope = {0.0f, 0.0f};
        std::array<float, 2> upwardEnvelope = {0.0f, 0.0f};
    };

    MultibandCompressor() = default;

    void prepare(const juce::dsp::ProcessSpec& spec)
    {
        sampleRate = spec.sampleRate;

    for (int ch = 0; ch < 2; ++ch)
    {
        lowPass[static_cast<size_t>(ch)].prepare(spec);
        highPass[static_cast<size_t>(ch)].prepare(spec);
        midLowPass[static_cast<size_t>(ch)].prepare(spec);
        midHighPass[static_cast<size_t>(ch)].prepare(spec);
    }

        for (auto& comp : compressors)
        {
            for (auto& env : comp.downwardEnvelope)
                env = 0.0f;
            for (auto& env : comp.upwardEnvelope)
                env = 0.0f;
        }

        updateCrossoverFreqs();
    }

    void setCrossoverLowMid(float freq)
    {
        crossoverLowMid = freq;
        updateCrossoverFreqs();
    }

    void setCrossoverMidHigh(float freq)
    {
        crossoverMidHigh = freq;
        updateCrossoverFreqs();
    }

    void setDepth(float d) { depth = d; }

    BandCompressor& getBand(int band) { return compressors[static_cast<size_t>(band)]; }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numChannels = buffer.getNumChannels();
        const int numSamples = buffer.getNumSamples();

        juce::AudioBuffer<float> lowBand(numChannels, numSamples);
        juce::AudioBuffer<float> midBand(numChannels, numSamples);
        juce::AudioBuffer<float> highBand(numChannels, numSamples);

        {
            juce::dsp::AudioBlock<float> inputBlock(buffer);
            juce::dsp::AudioBlock<float> lowBlock(lowBand);
            juce::dsp::AudioBlock<float> midBlock(midBand);
            juce::dsp::AudioBlock<float> highBlock(highBand);

            const int chIdx = 0;
            juce::dsp::ProcessContextNonReplacing<float> lowContext(inputBlock, lowBlock);
            juce::dsp::ProcessContextNonReplacing<float> highContext(inputBlock, highBlock);

            lowPass[chIdx].process(lowContext);
            highPass[chIdx].process(highContext);

            juce::dsp::AudioBlock<float> highBandBlock(highBand);
            juce::dsp::ProcessContextNonReplacing<float> midLowContext(highBandBlock, midBlock);
            juce::dsp::ProcessContextNonReplacing<float> midHighContext(highBandBlock, highBlock);

            midLowPass[chIdx].process(midLowContext);
            midHighPass[chIdx].process(midHighContext);
        }

        for (int ch = 0; ch < numChannels; ++ch)
        {
            const size_t chIdx = static_cast<size_t>(juce::jmin(ch, 1));

            processBand(lowBand, ch, compressors[0], chIdx);
            processBand(midBand, ch, compressors[1], chIdx);
            processBand(highBand, ch, compressors[2], chIdx);

            auto* output = buffer.getWritePointer(ch);
            auto* low = lowBand.getReadPointer(ch);
            auto* mid = midBand.getReadPointer(ch);
            auto* high = highBand.getReadPointer(ch);

            for (int s = 0; s < numSamples; ++s)
                output[s] = low[s] + mid[s] + high[s];
        }
    }

    void reset()
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            lowPass[ch].reset();
            highPass[ch].reset();
            midLowPass[ch].reset();
            midHighPass[ch].reset();
        }
        for (auto& comp : compressors)
        {
            for (auto& env : comp.downwardEnvelope)
                env = 0.0f;
            for (auto& env : comp.upwardEnvelope)
                env = 0.0f;
        }
    }

private:
    void updateCrossoverFreqs()
    {
        for (size_t ch = 0; ch < 2; ++ch)
        {
            lowPass[ch].setCutoffFrequency(crossoverLowMid);
            highPass[ch].setCutoffFrequency(crossoverLowMid);
            midLowPass[ch].setCutoffFrequency(crossoverMidHigh);
            midHighPass[ch].setCutoffFrequency(crossoverMidHigh);
        }
    }

    void processBand(juce::AudioBuffer<float>& bandBuffer, int channel,
                     BandCompressor& comp, int chIdx)
    {
        const float d = depth;
        auto* data = bandBuffer.getWritePointer(channel);

        const float downAttackCoeff = computeTimeConstant(comp.downwardAttack);
        const float downReleaseCoeff = computeTimeConstant(comp.downwardRelease);
        const float upAttackCoeff = computeTimeConstant(comp.upwardAttack);
        const float upReleaseCoeff = computeTimeConstant(comp.upwardRelease);

        for (int s = 0; s < bandBuffer.getNumSamples(); ++s)
        {
            float inputSample = data[s];
            float inputLevel = std::abs(inputSample);
            float inputLevelDb = juce::Decibels::gainToDecibels(inputLevel, -120.0f);

            if (inputLevelDb > comp.downwardEnvelope[chIdx])
                comp.downwardEnvelope[chIdx] = downAttackCoeff * comp.downwardEnvelope[chIdx]
                    + (1.0f - downAttackCoeff) * inputLevelDb;
            else
                comp.downwardEnvelope[chIdx] = downReleaseCoeff * comp.downwardEnvelope[chIdx]
                    + (1.0f - downReleaseCoeff) * inputLevelDb;

            if (inputLevelDb > comp.upwardEnvelope[chIdx])
                comp.upwardEnvelope[chIdx] = upAttackCoeff * comp.upwardEnvelope[chIdx]
                    + (1.0f - upAttackCoeff) * inputLevelDb;
            else
                comp.upwardEnvelope[chIdx] = upReleaseCoeff * comp.upwardEnvelope[chIdx]
                    + (1.0f - upReleaseCoeff) * inputLevelDb;

            float downGainDb = computeDownwardGain(comp.downwardEnvelope[chIdx],
                comp.downwardThreshold, comp.downwardRatio);
            float upGainDb = computeUpwardGain(comp.upwardEnvelope[chIdx],
                comp.upwardThreshold, comp.upwardRatio);

            float totalGainDb = downGainDb * d + upGainDb * d + comp.gainDb;

            data[s] = inputSample * juce::Decibels::decibelsToGain(totalGainDb, -120.0f);
        }
    }

    float computeDownwardGain(float envelopeDb, float thresholdDb, float ratio) const
    {
        if (envelopeDb <= thresholdDb)
            return 0.0f;
        float overDb = envelopeDb - thresholdDb;
        return -overDb * (1.0f - 1.0f / ratio);
    }

    float computeUpwardGain(float envelopeDb, float thresholdDb, float ratio) const
    {
        if (envelopeDb >= thresholdDb)
            return 0.0f;
        float underDb = thresholdDb - envelopeDb;
        return underDb * (1.0f - 1.0f / ratio);
    }

    float computeTimeConstant(float timeMs) const
    {
        return static_cast<float>(std::exp(-1.0f / (timeMs * 0.001f * sampleRate)));
    }

    float crossoverLowMid = 880.0f;
    float crossoverMidHigh = 4400.0f;
    float depth = 1.0f;
    double sampleRate = 44100.0;

    std::array<BandCompressor, numBands> compressors;

    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> lowPass;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> highPass;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> midLowPass;
    std::array<juce::dsp::LinkwitzRileyFilter<float>, 2> midHighPass;
};
