#pragma once

#include <juce_dsp/juce_dsp.h>
#include <juce_audio_basics/juce_audio_basics.h>
#include <vector>
#include <cmath>
#include <algorithm>
#include <cstring>

namespace OTT
{
    static constexpr float ENVELOPE_DECAY_RATE    = 2.49999994e-5f;
    static constexpr float COMPRESSION_SCALING   = 0.519999981f;
    static constexpr float UPWARD_MULT_1         = 2.27304697f;
    static constexpr float UPWARD_MULT_2         = 0.927524984f;
    static constexpr int   DELAY_BUFFER_SIZE      = 32768;
    static constexpr int   NUM_BANDS             = 3;
    static constexpr double NOISE_FLOOR          = 1e-25;
    static constexpr double LOG_SCALE_FACTOR     = 9.21034037;
    static constexpr double UNITY_GAIN           = 1.0;
    static constexpr double MIN_GAIN_THRESHOLD   = 0.01;
    static constexpr double MAX_COMP_RATIO      = 36.0;
    static constexpr double NEGATIVE_THRESHOLD  = -0.001;
    static constexpr double ENVELOPE_TIME_CONST = 0.115;
}

struct BiquadFilter
{
    float b0 = 1.0f, b1 = 0.0f;
    float state1 = 0.0f, state2 = 0.0f;
    float coeffA1 = 0.0f, coeffA2 = 0.0f, coeffB2 = 0.0f;
    float inputStore = 0.0f;
    float intermediate = 0.0f;
    float output = 0.0f;
    float processedInput = 0.0f;

    void initialize()
    {
        state1 = state2 = 0.0f;
        inputStore = intermediate = output = processedInput = 0.0f;
        b0 = 1.0f; b1 = 0.0f;
        coeffA1 = coeffA2 = coeffB2 = 0.0f;
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

    float process(float input)
    {
        float w2 = state2;
        float w1 = state1;
        inputStore = input;

        float temp1 = w1 * coeffA1;
        float procInput = input - w2;
        processedInput = procInput;

        float temp2 = procInput * coeffA2;
        float feedback = procInput * coeffB2;

        intermediate = temp1 + temp2;
        float out = w1 * coeffA2 + w2 + feedback;
        output = out;

        state1 = intermediate + intermediate - w1;
        state2 = out + out - w2;
        return out;
    }

    float getLowpass() const { return output; }
    float getHighpass() const { return inputStore - (intermediate * b1) - output; }
};

struct CompressorState
{
    double rmsSmoother = 0.0;
    double rmsSmoothingCoeff = 0.1;
    double logEnvelope = 0.0;
    double threshold = -20.0;
    double ratioState = 0.0;
    double gainReduction = 0.0;
    double attackCoeff = 0.1;
    double releaseCoeff = 0.01;
    double releaseTime = 1.0;
    double upwardRatio = 2.0;
    double envelopeOutput = 1.0;
    double processedEnvelope = 1.0;
    float linearCoeff = 1.0f;
    float kneeCoeff = 0.5f;

    void initialize()
    {
        rmsSmoother = 0.0;
        rmsSmoothingCoeff = 0.1;
        logEnvelope = 0.0;
        threshold = -20.0;
        ratioState = 0.0;
        gainReduction = 0.0;
        attackCoeff = 0.1;
        releaseCoeff = 0.01;
        releaseTime = 1.0;
        upwardRatio = 2.0;
        envelopeOutput = 1.0;
        processedEnvelope = 1.0;
        linearCoeff = 1.0f;
        kneeCoeff = 0.5f;
    }

    void setParameters(double thresh, double ratio, double attack, double release, double upRatio)
    {
        threshold = thresh;
        ratioState = ratio;
        attackCoeff = attack;
        releaseCoeff = release;
        upwardRatio = upRatio;
        rmsSmoothingCoeff = std::min(0.5, attack * 10.0);
    }

    void setTiming(double attackMs, double releaseMs, double sampleRate)
    {
        double attackSamples = attackMs * sampleRate / 1000.0;
        double releaseSamples = releaseMs * sampleRate / 1000.0;
        attackCoeff = 1.0 - std::exp(-1.0 / attackSamples);
        releaseCoeff = 1.0 - std::exp(-1.0 / releaseSamples);
        rmsSmoothingCoeff = attackCoeff * 0.5;
    }

    double process(double inputPower, double outputLevel, double bandGain, double timeConstant)
    {
        double rmsDiff = rmsSmoother - inputPower;
        rmsDiff *= rmsSmoothingCoeff;
        rmsSmoother = rmsDiff + inputPower;

        double envelopeExp = std::exp(logEnvelope * timeConstant);
        double envelopeSqrt = std::sqrt(std::abs(rmsSmoother));

        double finalGainReduction;

        if (ratioState <= OTT::NEGATIVE_THRESHOLD)
        {
            double thresholdValue = threshold;
            double logInput = std::log(envelopeSqrt + 1e-30) * OTT::LOG_SCALE_FACTOR;
            double overThreshold = logInput - thresholdValue;
            double maxReduction = std::fmax(0.0, overThreshold);

            double currentRatio = gainReduction;
            double ratioDiff = currentRatio - maxReduction;

            double compCoeff;
            if (maxReduction <= currentRatio)
                compCoeff = releaseCoeff;
            else
                compCoeff = attackCoeff;

            double compressedLevel = ratioDiff * compCoeff + maxReduction;
            gainReduction = compressedLevel;

            if (compressedLevel <= thresholdValue)
            {
                double releaseFactor = releaseTime - OTT::UNITY_GAIN;
                double upwardGain = releaseFactor * compressedLevel * timeConstant;
                finalGainReduction = std::exp(upwardGain);
                if (finalGainReduction <= OTT::MIN_GAIN_THRESHOLD)
                    finalGainReduction = OTT::MIN_GAIN_THRESHOLD;
            }
            else
            {
                double attackFactor = compressedLevel - thresholdValue;
                double downwardGain = attackFactor * timeConstant;
                double gainMultiplier = std::exp(downwardGain);
                double upwardFactor = gainMultiplier * upwardRatio;
                processedEnvelope = std::exp(downwardGain);
                if (upwardFactor <= OTT::MAX_COMP_RATIO)
                    finalGainReduction = upwardFactor * timeConstant;
                else
                    finalGainReduction = OTT::MAX_COMP_RATIO * timeConstant;
                finalGainReduction = std::exp(finalGainReduction);
            }
        }
        else
        {
            double linearThreshold = envelopeExp;
            double processedInput;

            if (envelopeSqrt <= linearThreshold)
                processedInput = kneeCoeff * linearThreshold;
            else
            {
                double aboveThresh = envelopeSqrt - linearThreshold;
                processedInput = linearCoeff * aboveThresh + linearThreshold;
            }

            double minLevel = 1e-30;
            double finalLevel = (processedInput >= minLevel) ? processedInput : minLevel;

            double logProcessed = std::log(processedInput + 1e-30) * OTT::LOG_SCALE_FACTOR;
            double logFinal = std::log(finalLevel + 1e-30) * OTT::LOG_SCALE_FACTOR;
            logEnvelope = logFinal;

            double thresholdDiff = logProcessed - threshold;

            if (thresholdDiff <= 0.0)
            {
                double expansionGain = thresholdDiff * timeConstant;
                processedEnvelope = std::exp(expansionGain);
                double releaseGain = releaseTime - OTT::UNITY_GAIN;
                double finalExpansion = releaseGain * thresholdDiff * timeConstant;
                finalGainReduction = std::exp(finalExpansion);
                if (finalGainReduction <= OTT::MIN_GAIN_THRESHOLD)
                    finalGainReduction = OTT::MIN_GAIN_THRESHOLD;
            }
            else
            {
                if (thresholdDiff <= -OTT::NEGATIVE_THRESHOLD)
                {
                    double standardComp = thresholdDiff * timeConstant;
                    finalGainReduction = std::exp(standardComp);
                }
                else
                    finalGainReduction = OTT::MIN_GAIN_THRESHOLD;
            }
        }

        envelopeOutput = finalGainReduction;
        return finalGainReduction * outputLevel * bandGain;
    }

    double getGainReductionDb() const
    {
        return 20.0 * std::log10(envelopeOutput + 1e-30);
    }

    double getRmsLevelDb() const
    {
        return 20.0 * std::log10(std::sqrt(std::abs(rmsSmoother)) + 1e-30);
    }
};

class OTTMultibandCompressor
{
public:
    static constexpr int numBands = OTT::NUM_BANDS;

    struct BandLevels
    {
        float inputDb[numBands]   = {-100.0f, -100.0f, -100.0f};
        float gainReductionDb[numBands] = {0.0f, 0.0f, 0.0f};
    };

    void prepare(double sampleRate, int /*blockSize*/)
    {
        currentSampleRate = sampleRate;
        setupCrossoverFilters();
        setupCompressors();

        for (int i = 0; i < 8; ++i)
        {
            delayBuffers[i].assign(OTT::DELAY_BUFFER_SIZE, 0.0f);
            bandBuffers[i].assign(OTT::DELAY_BUFFER_SIZE, 0.0f);
        }

        bufferIndex = 0;
        writeIndex = 0;
        peakEnvelopeLeft = 0.0f;
        peakEnvelopeRight = 0.0f;
        depthSmoothed = 0.0f;
        upwardSmoothed = 0.0f;
        outputSmoothed = 1.0f;
        finalGain = 0.0f;
        fadeSamples = 0;
    }

    int getLatencySamples() const
    {
        return OTT::DELAY_BUFFER_SIZE;
    }

    void setDepth(float d) { depth = d; }
    void setUpwardRatio(float r) { upwardRatio = r; }
    void setDownwardRatio(float r) { downwardRatio = r; }
    void setBandGain(int band, float g) { bandGains[band] = g; }
    void setOutputGain(float g) { outputGain = g; }

    const BandLevels& getBandLevels() const { return bandLevels; }

    void process(juce::AudioBuffer<float>& buffer)
    {
        const int numSamples = buffer.getNumSamples();
        const int numChannels = buffer.getNumChannels();
        const int rightChIdx = (numChannels > 1) ? 1 : 0;

        float* inputs[2];
        float* outputs[2];
        inputs[0] = buffer.getWritePointer(0);
        inputs[1] = buffer.getWritePointer(rightChIdx);
        outputs[0] = buffer.getWritePointer(0);
        outputs[1] = buffer.getWritePointer(rightChIdx);

        if (numSamples <= 0) return;

        for (int s = 0; s < numSamples; ++s)
        {
            float leftSample = inputs[0][s];
            if (leftSample < peakEnvelopeLeft)
            {
                peakEnvelopeLeft -= OTT::ENVELOPE_DECAY_RATE;
                if (peakEnvelopeLeft < 0.0f) peakEnvelopeLeft = 0.0f;
            }
            else
                peakEnvelopeLeft = leftSample;

            float rightSample = inputs[1][s];
            if (rightSample < peakEnvelopeRight)
            {
                peakEnvelopeRight -= OTT::ENVELOPE_DECAY_RATE;
                if (peakEnvelopeRight < 0.0f) peakEnvelopeRight = 0.0f;
            }
            else
                peakEnvelopeRight = rightSample;
        }

        for (int s = 0; s < numSamples; ++s)
        {
            float depthTarget = depth;
            depthSmoothed = (depthTarget - depthSmoothed) * 0.01f + depthSmoothed;

            float upwardTarget = upwardRatio;
            upwardSmoothed = (upwardTarget - upwardSmoothed) * 0.01f + upwardSmoothed;
            currentGain = upwardSmoothed;

            float processingGain = depthSmoothed * OTT::COMPRESSION_SCALING + 1.0f;
            float leftInput = currentGain * inputs[0][s];
            float rightInput = currentGain * inputs[1][s];

            float lowLeft  = processFilterAndGetLP(0, leftInput)  * processingGain;
            float lowRight = processFilterAndGetLP(1, rightInput) * processingGain;
            float midLeft  = processFilterAndGetHP(2, processFilterAndGetLP(2, leftInput))  * processingGain;
            float midRight = processFilterAndGetHP(3, processFilterAndGetLP(3, rightInput)) * processingGain;
            float highLeft  = processFilterAndGetHP(4, leftInput)  * processingGain;
            float highRight = processFilterAndGetHP(5, rightInput) * processingGain;

            bandBuffers[0][s] = lowLeft;
            bandBuffers[1][s] = lowRight;
            bandBuffers[2][s] = midLeft;
            bandBuffers[3][s] = midRight;
            bandBuffers[4][s] = highLeft;
            bandBuffers[5][s] = highRight;

            int bufPos = bufferIndex;
            for (int band = 0; band < 6; ++band)
                delayBuffers[band][bufPos] = bandBuffers[band][s];
            delayBuffers[6][bufPos] = inputs[0][s];
            delayBuffers[7][bufPos] = inputs[1][s];

            bufferIndex++;
            if (bufferIndex >= OTT::DELAY_BUFFER_SIZE)
                bufferIndex = 0;
        }

        int currentBufPos = bufferIndex;
        int readPos = currentBufPos - numSamples;
        if (readPos < 0) readPos += OTT::DELAY_BUFFER_SIZE;
        writeIndex = readPos;

        for (int s = 0; s < numSamples; ++s)
        {
            outputSmoothed = (outputGain - outputSmoothed) * 0.005f + outputSmoothed;
            finalGain = outputSmoothed;

            int readIdx = writeIndex;

            float lowLeft   = delayBuffers[0][readIdx];
            float lowRight  = delayBuffers[1][readIdx];
            float midLeft   = delayBuffers[2][readIdx];
            float midRight  = delayBuffers[3][readIdx];
            float highLeft  = delayBuffers[4][readIdx];
            float highRight = delayBuffers[5][readIdx];

            float lowPower  = lowLeft * lowLeft + lowRight * lowRight + (float)OTT::NOISE_FLOOR;
            float midPower  = midLeft * midLeft + midRight * midRight + (float)OTT::NOISE_FLOOR;
            float highPower = highLeft * highLeft + highRight * highRight + (float)OTT::NOISE_FLOOR;

            float lowGR  = (float)compressors[0].process(lowPower, finalGain, bandGains[0], OTT::ENVELOPE_TIME_CONST);
            float midGR  = (float)compressors[1].process(midPower, finalGain, bandGains[1], OTT::ENVELOPE_TIME_CONST);
            float highGR = (float)compressors[2].process(highPower, finalGain, bandGains[2], OTT::ENVELOPE_TIME_CONST);

            lowLeft *= lowGR;   lowRight *= lowGR;
            midLeft *= midGR;   midRight *= midGR;
            highLeft *= highGR; highRight *= highGR;

            float fadeGain = 1.0f;
            if (fadeSamples < OTT::DELAY_BUFFER_SIZE)
            {
                fadeGain = (float)fadeSamples / (float)OTT::DELAY_BUFFER_SIZE;
                fadeSamples++;
            }

            float finalLeft  = (lowLeft + midLeft + highLeft) * finalGain * fadeGain;
            float finalRight = (lowRight + midRight + highRight) * finalGain * fadeGain;

            outputs[0][s] = finalLeft;
            outputs[rightChIdx][s] = finalRight;

            writeIndex++;
            if (writeIndex >= OTT::DELAY_BUFFER_SIZE)
                writeIndex = 0;
        }

        for (int b = 0; b < numBands; ++b)
        {
            bandLevels.inputDb[b] = (float)compressors[b].getRmsLevelDb();
            bandLevels.gainReductionDb[b] = (float)compressors[b].getGainReductionDb();
        }
    }

    void reset()
    {
        for (auto& f : crossoverFilters)
            f.initialize();
        for (auto& c : compressors)
            c.initialize();
        for (int i = 0; i < 8; ++i)
        {
            std::fill(delayBuffers[i].begin(), delayBuffers[i].end(), 0.0f);
            std::fill(bandBuffers[i].begin(), bandBuffers[i].end(), 0.0f);
        }
        bufferIndex = 0;
        writeIndex = 0;
        peakEnvelopeLeft = 0.0f;
        peakEnvelopeRight = 0.0f;
        depthSmoothed = 0.0f;
        upwardSmoothed = 0.0f;
        outputSmoothed = 1.0f;
        finalGain = 0.0f;
        fadeSamples = 0;
    }

private:
    void setupCrossoverFilters()
    {
        const float lowMidFreq = 200.0f;
        const float midHighFreq = 2000.0f;

        for (int i = 0; i < 6; ++i)
            crossoverFilters[i].initialize();

        crossoverFilters[0].setCoefficients(lowMidFreq, (float)currentSampleRate);
        crossoverFilters[1].setCoefficients(lowMidFreq, (float)currentSampleRate);
        crossoverFilters[2].setCoefficients(midHighFreq, (float)currentSampleRate);
        crossoverFilters[3].setCoefficients(midHighFreq, (float)currentSampleRate);
        crossoverFilters[4].setCoefficients(midHighFreq, (float)currentSampleRate);
        crossoverFilters[5].setCoefficients(midHighFreq, (float)currentSampleRate);
    }

    void setupCompressors()
    {
        compressors[0].initialize();
        compressors[1].initialize();
        compressors[2].initialize();

        compressors[0].setParameters(-20.0, 2.0, 0.1, 0.01, 2.0);
        compressors[1].setParameters(-15.0, 3.0, 0.08, 0.015, 2.5);
        compressors[2].setParameters(-10.0, 4.0, 0.05, 0.02, 3.0);

        compressors[0].setTiming(10.0, 100.0, currentSampleRate);
        compressors[1].setTiming(8.0, 80.0, currentSampleRate);
        compressors[2].setTiming(5.0, 50.0, currentSampleRate);
    }

    float processFilterAndGetLP(int idx, float input)
    {
        crossoverFilters[idx].process(input);
        return crossoverFilters[idx].getLowpass();
    }

    float processFilterAndGetHP(int idx, float input)
    {
        crossoverFilters[idx].process(input);
        return crossoverFilters[idx].getHighpass();
    }

    double currentSampleRate = 44100.0;

    float depth = 0.5f;
    float upwardRatio = 0.6f;
    float downwardRatio = 0.7f;
    float bandGains[numBands] = {0.5f, 0.5f, 0.5f};
    float outputGain = 0.0f;

    float depthSmoothed = 0.0f;
    float upwardSmoothed = 0.0f;
    float outputSmoothed = 1.0f;
    float finalGain = 0.0f;
    float currentGain = 0.0f;

    float peakEnvelopeLeft = 0.0f;
    float peakEnvelopeRight = 0.0f;

    int bufferIndex = 0;
    int writeIndex = 0;
    int fadeSamples = 0;

    BiquadFilter crossoverFilters[6];
    CompressorState compressors[numBands];

    std::vector<float> delayBuffers[8];
    std::vector<float> bandBuffers[8];

    BandLevels bandLevels;
};
