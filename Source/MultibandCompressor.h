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

    struct BandLevels
    {
        float inputDb[numBands]   = {-100.0f, -100.0f, -100.0f};
        float gainReductionDb[numBands] = {0.0f, 0.0f, 0.0f};
    };

    void prepare(double sampleRate, int /*blockSize*/)
    {
        currentSampleRate = sampleRate;
        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 512;
        spec.numChannels = 2;

        for (int ch = 0; ch < 2; ++ch)
        {
            crossoverFilters[ch][0].prepare(spec);
            crossoverFilters[ch][0].setCutoffFrequency(200.0f);
            crossoverFilters[ch][1].prepare(spec);
            crossoverFilters[ch][1].setCutoffFrequency(2000.0f);
        }

        for (int b = 0; b < numBands; ++b)
        {
            envelopes[b] = thresholds[b];
            inputLevels[b] = thresholds[b];
            gainReductions[b] = 0.0f;
        }

        depthSmoothed = 0.0f;
        outputSmoothed = 1.0f;
    }

    void setDepth(float d) { depth = d; }
    void setUpwardRatio(float r) { upwardRatioRaw = r; }
    void setDownwardRatio(float r) { downwardRatioRaw = r; }
    void setBandGain(int band, float g) { bandGains[band] = g; }
    void setUpwardThreshold(int band, float t) { thresholds[band] = t; }
    void setDownwardThreshold(int band, float t) { thresholds[band] = t; }

    const BandLevels& getBandLevels() const { return bandLevels; }

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

            float bandSamples[2][numBands] = {};

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const int chIdx = juce::jmin(ch, 1);
                float input = buffer.getSample(ch, s);

                float lowBand, midBand, highBand;
                splitBands(chIdx, input, lowBand, midBand, highBand);

                bandSamples[chIdx][0] = lowBand;
                bandSamples[chIdx][1] = midBand;
                bandSamples[chIdx][2] = highBand;
            }

            float bandGainsDb[numBands] = {0.0f, 0.0f, 0.0f};
            float bandInputs[numBands]  = {0.0f, 0.0f, 0.0f};

            for (int b = 0; b < numBands; ++b)
            {
                float bandSample = 0.0f;
                int activeCh = 0;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const int chIdx = juce::jmin(ch, 1);
                    bandSample += bandSamples[chIdx][b];
                    ++activeCh;
                }
                bandSample /= (float)activeCh;

                bandInputs[b] = bandSample;

                computeBandGain(b, bandSample, upwardRatio, downwardRatio);
                bandGainsDb[b] = gainReductions[b];

                float inDb = juce::Decibels::gainToDecibels(std::abs(bandSample), -100.0f);
                inputLevels[b] = juce::jmax(inputLevels[b] * 0.9f, inDb);
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const int chIdx = juce::jmin(ch, 1);
                float lowGainLin  = juce::Decibels::decibelsToGain(bandGains[0], -100.0f);
                float midGainLin  = juce::Decibels::decibelsToGain(bandGains[1], -100.0f);
                float highGainLin = juce::Decibels::decibelsToGain(bandGains[2], -100.0f);

                float lowGainCompLin  = juce::Decibels::decibelsToGain(bandGainsDb[0], -100.0f);
                float midGainCompLin  = juce::Decibels::decibelsToGain(bandGainsDb[1], -100.0f);
                float highGainCompLin = juce::Decibels::decibelsToGain(bandGainsDb[2], -100.0f);

                float lowIn  = bandSamples[chIdx][0] * lowGainLin;
                float midIn  = bandSamples[chIdx][1] * midGainLin;
                float highIn = bandSamples[chIdx][2] * highGainLin;

                float lowOut  = bandSamples[chIdx][0] * lowGainCompLin * lowGainLin;
                float midOut  = bandSamples[chIdx][1] * midGainCompLin * midGainLin;
                float highOut = bandSamples[chIdx][2] * highGainCompLin * highGainLin;

                float dry = lowIn + midIn + highIn;
                float wet = lowOut + midOut + highOut;

                float output = dry * (1.0f - depthSmoothed) + wet * depthSmoothed;
                output *= outputSmoothed;

                buffer.setSample(ch, s, output);
            }
        }

        for (int b = 0; b < numBands; ++b)
        {
            bandLevels.inputDb[b] = inputLevels[b];
            bandLevels.gainReductionDb[b] = gainReductions[b];
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 2; ++i)
                crossoverFilters[ch][i].reset();
        for (int b = 0; b < numBands; ++b)
        {
            envelopes[b] = thresholds[b];
            inputLevels[b] = thresholds[b];
            gainReductions[b] = 0.0f;
        }
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
        float lowLp = crossoverFilters[chIdx][0].processSample(0, input);
        low = lowLp;
        float hpOut = input - lowLp;

        float midLp = crossoverFilters[chIdx][1].processSample(0, hpOut);
        mid = midLp;
        high = hpOut - midLp;
    }

    void computeBandGain(int band, float sample, float upRatio, float downRatio)
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
        float threshold = thresholds[band];

        float gainDb = 0.0f;

        if (envDb < threshold && upRatio > 1.0f)
        {
            float underDb = threshold - envDb;
            float upGain = underDb * (1.0f - 1.0f / upRatio);
            upGain = juce::jmin(upGain, 36.0f);
            gainDb += upGain;
        }

        if (envDb > threshold && downRatio > 1.0f)
        {
            float overDb = envDb - threshold;
            float downGain = overDb * (1.0f - 1.0f / downRatio);
            downGain = juce::jmin(downGain, 36.0f);
            gainDb -= downGain;
        }

        gainReductions[band] = gainDb;
    }

    double currentSampleRate = 44100.0;
    float depth = 1.0f;
    float upwardRatioRaw = 0.75f;
    float downwardRatioRaw = 0.75f;
    float bandGains[numBands] = {0.0f, 0.0f, 0.0f};
    float thresholds[numBands] = {-30.0f, -30.0f, -30.0f};
    float depthSmoothed = 0.0f;
    float outputSmoothed = 1.0f;

    juce::dsp::LinkwitzRileyFilter<float> crossoverFilters[2][2];
    double envelopes[numBands] = {-100.0, -100.0, -100.0};
    float inputLevels[numBands] = {-100.0f, -100.0f, -100.0f};
    float gainReductions[numBands] = {0.0f, 0.0f, 0.0f};

    BandLevels bandLevels;
};
