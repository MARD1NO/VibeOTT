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
        float inputDb[numBands]      = {-100.0f, -100.0f, -100.0f};
        float gainReductionDb[numBands] = {0.0f, 0.0f, 0.0f};
    };

    void prepare(double sampleRate, int /*blockSize*/)
    {
        currentSampleRate = sampleRate;

        juce::dsp::ProcessSpec spec;
        spec.sampleRate = sampleRate;
        spec.maximumBlockSize = 512;
        spec.numChannels = 1;

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int i = 0; i < 2; ++i)
            {
                lrFilters[ch][i].prepare(spec);
            }
            lrFilters[ch][0].setCutoffFrequency(200.0f);
            lrFilters[ch][1].setCutoffFrequency(2000.0f);
        }

        for (int b = 0; b < numBands; ++b)
        {
            envDb[b]          = thresholds[b];
            smoothedGainDb[b] = 0.0f;
            inputLevelDb[b]   = -100.0f;
        }

        depthSmoothed  = 0.0f;
        outputSmoothed = 0.5f;
    }

    int getLatencySamples() const { return 0; }

    void setDepth(float d)       { depth = d; }
    void setUpwardRatio(float r) { upRatioParam = r; }
    void setDownwardRatio(float r) { downRatioParam = r; }
    void setBandGain(int band, float g) { bandGainParam[band] = g; }
    void setOutputGain(float g)  { outputGainParam = g; }

    const BandLevels& getBandLevels() const { return bandLevels; }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples  = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        const int rightCh     = (numChannels > 1) ? 1 : 0;

        if (numSamples <= 0) return;

        float* dataL = buffer.getWritePointer(0);
        float* dataR = buffer.getWritePointer(rightCh);

        float upRatio   = juce::jmax(1.0f, upRatioParam   * 2.0f);
        float downRatio = juce::jmax(1.0f, downRatioParam * 2.0f);

        for (int s = 0; s < numSamples; ++s)
        {
            depthSmoothed  += 0.005f * (depth - depthSmoothed);
            outputSmoothed += 0.005f * (outputGainParam - outputSmoothed);
            float outGainLin = juce::Decibels::decibelsToGain(outputSmoothed * 48.0f - 24.0f, -100.0f);

            float inL = dataL[s];
            float inR = dataR[s];

            float bands[2][numBands];
            splitBands(0, inL, bands[0][0], bands[0][1], bands[0][2]);
            splitBands(1, inR, bands[1][0], bands[1][1], bands[1][2]);

            float outL = 0.0f, outR = 0.0f;

            for (int b = 0; b < numBands; ++b)
            {
                float power = bands[0][b] * bands[0][b] + bands[1][b] * bands[1][b] + 1e-12f;
                float amp   = std::sqrt(power);
                float ampDb = juce::Decibels::gainToDecibels(amp, -100.0f);

                inputLevelDb[b] = juce::jmax(inputLevelDb[b] - 0.5f, ampDb);

                double atkMs  = (b == 0) ? 10.0 : (b == 1) ? 8.0 : 5.0;
                double relMs  = (b == 0) ? 100.0 : (b == 1) ? 80.0 : 50.0;
                double atkC   = std::exp(-1.0 / (atkMs * 0.001 * currentSampleRate));
                double relC   = std::exp(-1.0 / (relMs * 0.001 * currentSampleRate));

                double c = (ampDb > envDb[b]) ? atkC : relC;
                envDb[b] = c * envDb[b] + (1.0 - c) * ampDb;

                float thr = thresholds[b];
                float targetGain = 0.0f;

                if (envDb[b] < thr)
                {
                    float under = thr - (float)envDb[b];
                    targetGain = under * (1.0f - 1.0f / upRatio);
                    targetGain = juce::jmin(targetGain, 24.0f);
                }
                else if (envDb[b] > thr)
                {
                    float over = (float)envDb[b] - thr;
                    targetGain = -over * (1.0f - 1.0f / downRatio);
                    targetGain = juce::jmax(targetGain, -24.0f);
                }

                double gC = (std::abs(targetGain) > std::abs(smoothedGainDb[b])) ? atkC : relC;
                smoothedGainDb[b] = (float)(gC * smoothedGainDb[b] + (1.0 - gC) * targetGain);

                float compLin = juce::Decibels::decibelsToGain(smoothedGainDb[b], -100.0f);
                float bandGainLin = juce::Decibels::decibelsToGain(bandGainParam[b] * 24.0f - 12.0f, -100.0f);

                outL += bands[0][b] * compLin * bandGainLin;
                outR += bands[1][b] * compLin * bandGainLin;
            }

            float finalL = inL * (1.0f - depthSmoothed) + outL * depthSmoothed;
            float finalR = inR * (1.0f - depthSmoothed) + outR * depthSmoothed;

            finalL *= outGainLin;
            finalR *= outGainLin;

            finalL = juce::jlimit(-2.0f, 2.0f, finalL);
            finalR = juce::jlimit(-2.0f, 2.0f, finalR);

            dataL[s] = finalL;
            dataR[s] = finalR;
        }

        for (int b = 0; b < numBands; ++b)
        {
            bandLevels.inputDb[b]         = inputLevelDb[b];
            bandLevels.gainReductionDb[b] = smoothedGainDb[b];
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int i = 0; i < 2; ++i)
                lrFilters[ch][i].reset();
        for (int b = 0; b < numBands; ++b)
        {
            envDb[b]          = thresholds[b];
            smoothedGainDb[b] = 0.0f;
            inputLevelDb[b]   = -100.0f;
        }
        depthSmoothed  = 0.0f;
        outputSmoothed = 0.5f;
    }

private:
    void splitBands(int ch, float input, float& low, float& mid, float& high)
    {
        float lp200  = lrFilters[ch][0].processSample(0, input);
        low = lp200;
        float hp200  = input - lp200;
        float lp2000 = lrFilters[ch][1].processSample(0, hp200);
        mid  = lp2000;
        high = hp200 - lp2000;
    }

    double currentSampleRate = 44100.0;

    float depth          = 0.5f;
    float upRatioParam   = 0.6f;
    float downRatioParam = 0.7f;
    float bandGainParam[numBands] = {0.5f, 0.5f, 0.5f};
    float outputGainParam = 0.5f;

    float thresholds[numBands] = {-20.0f, -15.0f, -10.0f};

    double envDb[numBands]          = {-20.0, -15.0, -10.0};
    float  smoothedGainDb[numBands] = {0.0f, 0.0f, 0.0f};
    float  inputLevelDb[numBands]   = {-100.0f, -100.0f, -100.0f};

    float depthSmoothed  = 0.0f;
    float outputSmoothed = 0.5f;

    juce::dsp::LinkwitzRileyFilter<float> lrFilters[2][2];

    BandLevels bandLevels;
};
