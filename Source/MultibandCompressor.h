#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <array>
#include <cmath>

class OTTCompressor
{
public:
    void initialize()
    {
        rmsSmoother = 0.0;
        rmsSmoothingCoeff = 0.1;
        logEnvelope = 0.0;
        threshold = -20.0;
        gainReduction = 0.0;
        attackCoeff = 0.1;
        releaseCoeff = 0.01;
        releaseTime = 1.0;
        upwardRatio = 2.0;
        envelopeOutput = 1.0;
        processedEnvelope = 1.0;
    }

    void setThreshold(double t) { threshold = t; }
    void setRatio(double r) { upwardRatio = r; }
    void setAttack(double a) { attackCoeff = a; }
    void setRelease(double r) { releaseCoeff = r; releaseTime = r; }

    double process(double inputPower, double outputLevel, double bandGain, double timeConstant)
    {
        double rmsDiff = rmsSmoother - inputPower;
        rmsDiff *= rmsSmoothingCoeff;
        rmsSmoother = rmsDiff + inputPower;

        double envelopeSqrt = std::sqrt(std::abs(rmsSmoother));
        constexpr double negativeThreshold = -0.001;
        constexpr double logScaleFactor = 9.21034037;
        constexpr double unityGain = 1.0;
        constexpr double minGainThreshold = 0.01;
        constexpr double maxCompressionRatio = 36.0;
        constexpr double noiseFloor = 1e-30;

        double finalGainReduction;

        double logInput = std::log(envelopeSqrt + noiseFloor) * logScaleFactor;
        double overThreshold = logInput - threshold;
        double maxReduction = std::max(0.0, overThreshold);

        double compressionCoeff;
        if (maxReduction <= gainReduction)
            compressionCoeff = releaseCoeff;
        else
            compressionCoeff = attackCoeff;

        double compressedLevel = (gainReduction - maxReduction) * compressionCoeff + maxReduction;
        gainReduction = compressedLevel;

        if (compressedLevel <= threshold)
        {
            double releaseFactor = releaseTime - unityGain;
            double upwardGain = releaseFactor * compressedLevel * timeConstant;
            finalGainReduction = std::exp(upwardGain);
            if (finalGainReduction <= minGainThreshold)
                finalGainReduction = minGainThreshold;
        }
        else
        {
            double attackFactor = compressedLevel - threshold;
            double downwardGain = attackFactor * timeConstant;
            double gainMultiplier = std::exp(downwardGain);
            double upwardFactor = gainMultiplier * upwardRatio;
            processedEnvelope = std::exp(downwardGain);
            if (upwardFactor <= maxCompressionRatio)
                finalGainReduction = upwardFactor * timeConstant;
            else
                finalGainReduction = maxCompressionRatio * timeConstant;
            finalGainReduction = std::exp(finalGainReduction);
        }

        envelopeOutput = finalGainReduction;
        return finalGainReduction * outputLevel * bandGain;
    }

    double getGainReduction() const { return envelopeOutput; }

private:
    double rmsSmoother = 0.0;
    double rmsSmoothingCoeff = 0.1;
    double logEnvelope = 0.0;
    double threshold = -20.0;
    double gainReduction = 0.0;
    double attackCoeff = 0.1;
    double releaseCoeff = 0.01;
    double releaseTime = 1.0;
    double upwardRatio = 2.0;
    double envelopeOutput = 1.0;
    double processedEnvelope = 1.0;
};

struct OTTBiquadFilter
{
    float b0 = 1.0f, b1 = 0.0f;
    float state1 = 0.0f, state2 = 0.0f;
    float coeffA1 = 0.0f, coeffA2 = 0.0f, coeffB2 = 0.0f;
    float inputStore = 0.0f;
    float intermediate = 0.0f;
    float lowpassOut = 0.0f;

    void reset()
    {
        state1 = state2 = 0.0f;
        inputStore = intermediate = lowpassOut = 0.0f;
    }

    void setCoefficients(float frequency, float sampleRate)
    {
        double normalizedFreq = frequency * juce::MathConstants<double>::pi / sampleRate;
        float tanHalf = std::tan((float)normalizedFreq);
        float recip = 1.0f / tanHalf;
        float denom = 1.0f / ((recip + tanHalf) * tanHalf + 1.0f);
        coeffA1 = recip;
        coeffA2 = denom * tanHalf;
        coeffB2 = coeffA2 * tanHalf;
        b0 = 1.0f;
        b1 = denom;
    }

    float processLowpass(float input)
    {
        inputStore = input;
        float temp1 = state1 * coeffA1;
        float processed = input - state2;
        float temp2 = processed * coeffA2;
        float feedback = processed * coeffB2;
        intermediate = temp1 + temp2;
        float output = state1 * coeffA2 + state2 + feedback;
        lowpassOut = output;
        state1 = intermediate + intermediate - state1;
        state2 = output + output - state2;
        return output;
    }

    float processHighpass(float input)
    {
        processLowpass(input);
        return inputStore - (intermediate * b1) - lowpassOut;
    }
};

class OTTMultibandCompressor
{
public:
    static constexpr int numBands = 3;
    static constexpr int delayBufferSize = 32768;

    void prepare(double sampleRate, int blockSize)
    {
        currentSampleRate = sampleRate;
        setupCrossoverFilters();

        for (auto& comp : compressors)
            comp.initialize();

        for (int ch = 0; ch < 2; ++ch)
        {
            for (int b = 0; b < numBands; ++b)
            {
                bandBuffers[ch][b].assign(delayBufferSize, 0.0f);
                delayBuffers[ch][b].assign(delayBufferSize, 0.0f);
            }
        }

        writeIndex = 0;
        delaySamples = blockSize;

        depthSmoothed = 0.0f;
        upwardSmoothed = 0.0f;
        outputSmoothed = 1.0f;
    }

    void setDepth(float d) { depth = d; }
    void setTime(float t) { timeControl = t; }
    void setUpwardRatio(float r) { upwardRatio = r; }
    void setDownwardRatio(float r) { downwardRatio = r; }
    void setBandGain(int band, float g) { bandGains[band] = g; }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();

        auto smoothParam = [](float& current, float target, float coeff) {
            current += coeff * (target - current);
        };

        for (int s = 0; s < numSamples; ++s)
        {
            smoothParam(depthSmoothed, depth, 0.01f);
            smoothParam(upwardSmoothed, upwardRatio, 0.01f);
            smoothParam(outputSmoothed, 1.0f, 0.005f);

            float processingGain = depthSmoothed * 0.519999981f + 1.0f;

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const int chIdx = juce::jmin(ch, 1);
                float input = buffer.getSample(ch, s);

                float low  = crossoverFilters[0][chIdx].processLowpass(input);
                float midHp = crossoverFilters[0][chIdx].processHighpass(input);
                float mid  = crossoverFilters[1][chIdx].processLowpass(midHp);
                float high = crossoverFilters[2][chIdx].processHighpass(midHp);

                bandBuffers[chIdx][0][writeIndex] = low * processingGain;
                bandBuffers[chIdx][1][writeIndex] = mid * processingGain;
                bandBuffers[chIdx][2][writeIndex] = high * processingGain;
            }

            writeIndex = (writeIndex + 1) % delayBufferSize;
        }

        for (int s = 0; s < numSamples; ++s)
        {
            int readIdx = (writeIndex - delaySamples + delayBufferSize) % delayBufferSize;

            float outSamples[2] = {0.0f, 0.0f};

            for (int b = 0; b < numBands; ++b)
            {
                float bandLeft  = bandBuffers[0][b][readIdx];
                float bandRight = bandBuffers[1][b][readIdx];

                double power = (double)bandLeft * bandLeft
                             + (double)bandRight * bandRight
                             + 1e-25;

                double bandGainLin = std::pow(10.0, bandGains[b] / 20.0);
                double timeConstant = timeControl;

                double gainReduction = compressors[b].process(
                    power, 1.0, bandGainLin, timeConstant);

                float grLin = (float)gainReduction;

                if (numChannels > 0) outSamples[0] += bandLeft * grLin;
                if (numChannels > 1) outSamples[1] += bandRight * grLin;
            }

            for (int ch = 0; ch < numChannels; ++ch)
            {
                const int chIdx = juce::jmin(ch, 1);
                buffer.setSample(ch, s, outSamples[chIdx] * outputSmoothed);
            }

            readIdx = (readIdx + 1) % delayBufferSize;
        }
    }

    void reset()
    {
        for (int ch = 0; ch < 2; ++ch)
            for (int b = 0; b < numBands; ++b)
            {
                crossoverFilters[ch][b].reset();
                std::fill(bandBuffers[ch][b].begin(), bandBuffers[ch][b].end(), 0.0f);
                std::fill(delayBuffers[ch][b].begin(), delayBuffers[ch][b].end(), 0.0f);
            }
        for (auto& comp : compressors)
            comp.initialize();
        writeIndex = 0;
    }

private:
    void setupCrossoverFilters()
    {
        const float lowMidFreq = 200.0f;
        const float midHighFreq = 2000.0f;

        for (int ch = 0; ch < 2; ++ch)
        {
            crossoverFilters[0][ch].setCoefficients(lowMidFreq, (float)currentSampleRate);
            crossoverFilters[1][ch].setCoefficients(midHighFreq, (float)currentSampleRate);
            crossoverFilters[2][ch].setCoefficients(midHighFreq, (float)currentSampleRate);
        }
    }

    double currentSampleRate = 44100.0;
    int delaySamples = 0;
    int writeIndex = 0;

    float depth = 0.5f;
    float timeControl = 0.3f;
    float upwardRatio = 0.6f;
    float downwardRatio = 0.7f;
    float bandGains[numBands] = {0.0f, 0.0f, 0.0f};

    float depthSmoothed = 0.0f;
    float upwardSmoothed = 0.0f;
    float outputSmoothed = 1.0f;

    OTTBiquadFilter crossoverFilters[3][2];
    OTTCompressor compressors[numBands];

    std::vector<float> bandBuffers[2][numBands];
    std::vector<float> delayBuffers[2][numBands];
};
