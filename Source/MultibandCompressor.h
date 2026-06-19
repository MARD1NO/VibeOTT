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

        delayBufferSize = (int)(0.02 * sampleRate);
        if (delayBufferSize < 1) delayBufferSize = 1;

        for (int ch = 0; ch < 2; ++ch)
            for (int b = 0; b < numBands; ++b)
                delayBuffer[ch][b].assign(delayBufferSize, 0.0f);

        delayWriteIndex = 0;
        fadeSamples = 0;

        for (int b = 0; b < numBands; ++b)
        {
            envelopes[b] = 0.0;
            gainEnvelopes[b] = 0.0;
            inputLevels[b] = -100.0f;
            gainReductions[b] = 0.0f;
        }

        depthSmoothed = 0.0f;
    }

    int getLatencySamples() const { return delayBufferSize; }

    void setDepth(float d) { depth = d; }
    void setUpwardRatio(float r) { upwardRatioRaw = r; }
    void setDownwardRatio(float r) { downwardRatioRaw = r; }
    void setBandGain(int band, float g) { bandGains[band] = g; }
    void setOutputGain(float g) { outputGainDb = g; }
    void setUpwardThreshold(int band, float t) { thresholds[band] = t; }
    void setDownwardThreshold(int band, float t) { (void)t; }
    void setThresholdRange(int band, float r) { thresholdRanges[band] = r; }

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

            std::array<std::array<float, numBands>, 2> bandSamples {};

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

            for (int b = 0; b < numBands; ++b)
            {
                float sum = 0.0f;
                int activeCh = 0;
                for (int ch = 0; ch < numChannels; ++ch)
                {
                    const int chIdx = juce::jmin(ch, 1);
                    sum += bandSamples[chIdx][b];
                    ++activeCh;
                }
                float bandSampleAvg = sum / (float)activeCh;

                computeBandGain(b, bandSampleAvg, upwardRatio, downwardRatio);
                bandGainsDb[b] = gainReductions[b];

                float inDb = juce::Decibels::gainToDecibels(std::abs(bandSampleAvg), -100.0f);
                inputLevels[b] = juce::jmax(inputLevels[b] * 0.9f, inDb);
            }

            for (int ch = 0; ch < 2; ++ch)
                for (int b = 0; b < numBands; ++b)
                    delayBuffer[ch][b][delayWriteIndex] = bandSamples[ch][b];
            delayWriteIndex = (delayWriteIndex + 1) % delayBufferSize;

            int readIndex = delayWriteIndex;

            float fadeGain = 1.0f;
            if (fadeSamples < delayBufferSize)
            {
                fadeGain = (float)fadeSamples / (float)delayBufferSize;
                ++fadeSamples;
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

                float delayedLow  = delayBuffer[chIdx][0][readIndex];
                float delayedMid  = delayBuffer[chIdx][1][readIndex];
                float delayedHigh = delayBuffer[chIdx][2][readIndex];

                float dry = delayedLow + delayedMid + delayedHigh;

                float lowOut  = delayedLow * lowGainCompLin * lowGainLin;
                float midOut  = delayedMid * midGainCompLin * midGainLin;
                float highOut = delayedHigh * highGainCompLin * highGainLin;

                float wet = lowOut + midOut + highOut;

                float output = dry * (1.0f - depthSmoothed) + wet * depthSmoothed;
                output *= fadeGain;
                output *= juce::Decibels::decibelsToGain(outputGainDb, -100.0f);

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
        {
            for (int i = 0; i < 2; ++i)
                crossoverFilters[ch][i].reset();
            for (int b = 0; b < numBands; ++b)
                std::fill(delayBuffer[ch][b].begin(), delayBuffer[ch][b].end(), 0.0f);
        }
        for (int b = 0; b < numBands; ++b)
        {
            envelopes[b] = thresholds[b] + thresholdRanges[b] * 0.5f;
            gainEnvelopes[b] = 0.0;
            inputLevels[b] = -100.0f;
            gainReductions[b] = 0.0f;
        }
        depthSmoothed = 0.0f;
        delayWriteIndex = 0;
        fadeSamples = 0;
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
        float lowBoundary = thresholds[band];
        float highBoundary = thresholds[band] + thresholdRanges[band];

        float targetGainDb = 0.0f;

        if (envDb < lowBoundary && upRatio > 1.0f)
        {
            float underDb = lowBoundary - envDb;
            float upGain = underDb * (1.0f - 1.0f / upRatio);
            upGain = juce::jmin(upGain, 24.0f);
            targetGainDb += upGain;
        }

        if (envDb > highBoundary && downRatio > 1.0f)
        {
            float overDb = envDb - highBoundary;
            float downGain = overDb * (1.0f - 1.0f / downRatio);
            downGain = juce::jmin(downGain, 24.0f);
            targetGainDb -= downGain;
        }

        double& gainEnv = gainEnvelopes[band];
        double gainCoeff = (std::abs(targetGainDb) > std::abs(gainEnv))
            ? attackCoeff : releaseCoeff;
        gainEnv = gainCoeff * gainEnv + (1.0 - gainCoeff) * targetGainDb;

        gainReductions[band] = (float)gainEnv;
    }

    double currentSampleRate = 44100.0;
    int delayBufferSize = 1024;
    int delayWriteIndex = 0;
    int fadeSamples = 0;

    float depth = 0.5f;
    float upwardRatioRaw = 0.6f;
    float downwardRatioRaw = 0.7f;
    float bandGains[numBands] = {0.0f, 0.0f, 0.0f};
    float thresholds[numBands] = {-36.0f, -36.0f, -36.0f};
    float thresholdRanges[numBands] = {18.0f, 18.0f, 18.0f};
    float outputGainDb = 0.0f;
    float depthSmoothed = 0.0f;

    juce::dsp::LinkwitzRileyFilter<float> crossoverFilters[2][2];
    double envelopes[numBands] = {-20.0, -15.0, -10.0};
    double gainEnvelopes[numBands] = {0.0, 0.0, 0.0};
    float inputLevels[numBands] = {-100.0f, -100.0f, -100.0f};
    float gainReductions[numBands] = {0.0f, 0.0f, 0.0f};

    std::vector<float> delayBuffer[2][numBands];

    BandLevels bandLevels;
};
