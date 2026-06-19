#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>

class OTTMultibandCompressor
{
public:
    static constexpr int numBands = 3;

    void prepare(double sampleRate, int /*blockSize*/)
    {
        currentSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 512;
        spec.numChannels = 2;

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 4; ++i)
                crossoverFilters[ch][i].prepare(spec);
        }

        for (int b = 0; b < numBands; ++b)
            envelopes[b] = 0.0;

        depthSmoothed = 0.0f;
        outputSmoothed = 1.0f;
    }

    void setDepth(float d) { depth = d; }
    void setUpwardRatio(float r) { upwardRatioRaw = r; }
    void setDownwardRatio(float r) { downwardRatioRaw = r; }
    void setBandGain(int band, float g) { bandGains[band] = g; }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        float upwardRatio = scaleRatio(upwardRatioRaw);
        float downwardRatio = scaleRatio(downwardRatioRaw);

        for (int s = 0; s < numSamples; ++s)
        {
            depthSmoothed += 0.01f * (depth - depthSmoothed);
            outputSmoothed += 0.005f * (1.0f - outputSmoothed);

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const int chIdx = juce::jmin(ch, 1);
                float input = buffer.getSample(ch, s);

                float lowBand, midBand, highBand;
                splitBands(chIdx, input, lowBand, midBand, highBand);

                float lowProcessed  = processBandCompression(0, lowBand,
                    upwardRatio, downwardRatio);
                float midProcessed  = processBandCompression(1, midBand,
                    upwardRatio, downwardRatio);
                float highProcessed = processBandCompression(2, highBand,
                    upwardRatio, downwardRatio);

                float lowGainLin  = juce::Decibels::decibelsToGain(bandGains[0], -100.0f);
                float midGainLin  = juce::Decibels::decibelsToGain(bandGains[1], -100.0f);
                float highGainLin = juce::Decibels::decibelsToGain(bandGains[2], -100.0f);

                float dry = lowBand * lowGainLin + midBand * midGainLin + highBand * highGainLin;
                float wet = lowProcessed * lowGainLin + midProcessed * midGainLin + highProcessed * highGainLin;

                float output = dry * (1.0f - depthSmoothed) + wet * depthSmoothed;
                output *= outputSmoothed;

                buffer.setSample(ch, s, output);
            }
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 4; ++i)
                crossoverFilters[ch][i].reset();
        for (int b = 0; b < numBands; ++b)
            envelopes[b] = 0.0;
        depthSmoothed = 0.0f;
        outputSmoothed = 1.0f;
    }

private:
    float scaleRatio(float raw) const
    {
        if (raw > 0.5f)
            return juce::jmin((raw - 0.5f) * 16.0f + 1.0f, 10.0f);
        return raw * 2.0f;
    }

    void splitBands(int chIdx, float input, float& low, float& mid, float& high)
    {
        auto& lp0 = crossoverFilters[chIdx][0];
        auto& hp0 = crossoverFilters[chIdx][1];
        auto& lp1 = crossoverFilters[chIdx][2];
        auto& hp1 = crossoverFilters[chIdx][3];

        low = lp0.processSample(0, input);
        float hpOut = hp0.processSample(0, input);

        mid = lp1.processSample(0, hpOut);
        high = hp1.processSample(0, hpOut);
    }

    float processBandCompression(int band, float sample, float upRatio, float downRatio)
    {
        float inputAbs = std::abs(sample);
        float inputDb = juce::Decibels::gainToDecibels(inputAbs, -100.0f);

        double& env = envelopes[band];
        double attackMs = (band == 0) ? 10.0 : (band == 1) ? 8.0 : 5.0;
        double releaseMs = (band == 0) ? 100.0 : (band == 1) ? 80.0 : 50.0;

        double attackCoeff = std::exp(-1.0 / (attackMs * 0.001 * currentSampleRate));
        double releaseCoeff = std::exp(-1.0 / (releaseMs * 0.001 * currentSampleRate));

        double coeff = (inputDb > env) ? attackCoeff : releaseCoeff;
        env = coeff * env + (1.0 - coeff) * inputDb;

        float envDb = (float)env;
        float upThreshold = -36.0f;
        float downThreshold = -18.0f;

        float gainDb = 0.0f;

        if (envDb < upThreshold && upRatio > 1.0f)
        {
            float underDb = upThreshold - envDb;
            float upGain = underDb * (1.0f - 1.0f / upRatio);
            upGain = juce::jmin(upGain, 36.0f);
            gainDb += upGain;
        }

        if (envDb > downThreshold && downRatio > 1.0f)
        {
            float overDb = envDb - downThreshold;
            float downGain = overDb * (1.0f - 1.0f / downRatio);
            downGain = juce::jmin(downGain, 36.0f);
            gainDb -= downGain;
        }

        float gainLin = juce::Decibels::decibelsToGain(gainDb, -100.0f);
        gainLin = juce::jmin(gainLin, 100.0f);
        return sample * gainLin;
    }

    double currentSampleRate = 44100.0;
    float depth = 1.0f;
    float upwardRatioRaw = 0.75f;
    float downwardRatioRaw = 0.75f;
    float bandGains[numBands] = {0.0f, 0.0f, 0.0f};
    float depthSmoothed = 0.0f;
    float outputSmoothed = 1.0f;

    juce::dsp::LinkwitzRileyFilter<float> crossoverFilters[2][4];
    double envelopes[numBands] = {0.0, 0.0, 0.0};
};
