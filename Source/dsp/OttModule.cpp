#include "OttModule.h"

#include <limits>

namespace ott
{

namespace
{
    /** Hosts are not required to keep their block size constant, so the engine
        bounds its own scratch buffers and processes larger blocks in chunks. */
    constexpr int maxChunkSamples = 512;

    constexpr int lowBandIndex  = 0;
    constexpr int midBandIndex  = 1;
    constexpr int highBandIndex = 2;

    /** -infinity: passed as the noise-floor edge to disable the upward gate
        entirely, which is bit-identical to stock vitOTTx. */
    const float gateDisabled = -std::numeric_limits<float>::infinity();

    /** Per-band nominal envelope times, indexed by band. */
    constexpr float bandAttackMs[numBands]  = { lowAttackMs,  midAttackMs,  highAttackMs };
    constexpr float bandReleaseMs[numBands] = { lowReleaseMs, midReleaseMs, highReleaseMs };
}

//==============================================================================
Module::Module()
{
    params = Parameters::makeDefault();
    levels.reset();

    computeTargets (params, smoothed);
    std::copy (std::begin (smoothed), std::end (smoothed), std::begin (blockValue));

    for (int b = 0; b < numBands; ++b)
        lastBandGain[b] = std::pow (10.0f, smoothed[smLowGain + b] * 0.05f);

    lastMix = smoothed[smMix];
    lastInGain  = std::pow (10.0f, std::clamp (params.inGainDb,  trimMinDb, trimMaxDb) * 0.05f);
    lastOutGain = std::pow (10.0f, std::clamp (params.outGainDb, trimMinDb, trimMaxDb) * 0.05f);
}

void Module::prepare (double newSampleRate, int maximumBlockSize, int numChannels)
{
    (void) numChannels; // the engine always runs stereo internally

    sampleRate = newSampleRate > 0.0 ? newSampleRate : 44100.0;
    chunkSize  = std::clamp (maximumBlockSize, 1, maxChunkSamples);

    ensureScratch (chunkSize);

    for (auto& f : splitLow)     f.prepare (sampleRate);
    for (auto& f : splitHigh)    f.prepare (sampleRate);
    for (auto& f : splitLowLeg)  f.prepare (sampleRate);

    setupCoefficients (params);

    for (auto& band : compressors)
        for (auto& c : band)
            c.reset();

    // Snap the smoothers so the first block plays at the recalled settings
    // instead of ramping up from wherever the state happened to be.
    needsSnap = true;
    reset();
}

void Module::reset()
{
    for (auto& f : splitLow)     f.reset();
    for (auto& f : splitHigh)    f.reset();
    for (auto& f : splitLowLeg)  f.reset();

    for (auto& band : compressors)
        for (auto& c : band)
            c.reset();

    for (int b = 0; b < numBands; ++b)
        lastBandGain[b] = std::pow (10.0f, blockValue[smLowGain + b] * 0.05f);

    lastMix     = blockValue[smMix];
    lastInGain  = std::pow (10.0f, std::clamp (params.inGainDb,  trimMinDb, trimMaxDb) * 0.05f);
    lastOutGain = std::pow (10.0f, std::clamp (params.outGainDb, trimMinDb, trimMaxDb) * 0.05f);

    hasWetHistory = false;
    pendingMode = mode;

    clearMetering();
    levels.reset();
}

void Module::setParameters (const Parameters& p)
{
    params = p;

    const Mode requested = currentMode (p);

    // A collapsed crossover changes which bands exist. Activating that here
    // would clear the filter and envelope state mid-block and put a step in the
    // output, so the switch is only recorded: the next process() call renders
    // the new topology and crossfades it against the old one.
    if (requested != mode)
        pendingMode = requested;
    else
        pendingMode = mode;
}

Module::Mode Module::currentMode (const Parameters& p) const
{
    const float low  = std::min (p.lowCrossoverHz, p.highCrossoverHz);
    const float high = std::max (p.lowCrossoverHz, p.highCrossoverHz);

    // Hysteresis: an already-collapsed band only comes back once the corner has
    // moved clear of the threshold by collapseHysteresisHz. Without this, a
    // crossover swept across the threshold flips the topology on alternate
    // blocks and each flip throws away the filter and envelope state.
    const bool lowWasCollapsed  = (mode == modeLow  || mode == modeSingle);
    const bool highWasCollapsed = (mode == modeHigh || mode == modeSingle);

    const bool lowCollapsed  = lowWasCollapsed  ? (low  <= lowCollapseHz  + collapseHysteresisHz)
                                                : (low  <= lowCollapseHz);
    const bool highCollapsed = highWasCollapsed ? (high >= highCollapseHz - collapseHysteresisHz)
                                                : (high >= highCollapseHz);

    if (lowCollapsed && highCollapsed) return modeSingle;
    if (lowCollapsed)                  return modeHigh;
    if (highCollapsed)                 return modeLow;
    return modeMulti;
}

void Module::updateMode()
{
    mode = currentMode (params);
}

void Module::ensureScratch (int numSamples)
{
    const int need = std::max ({ chunkSize, numSamples, 1 });

    if ((int) dryStorage.size() < need * 2)
    {
        // Sizing up mid-stream means allocating on the audio thread, so
        // prepare() already sized everything for the block size the host
        // promised; this only runs if a host breaks that promise.
        dryStorage.resize ((size_t) need * 2);
        wetStorage.resize ((size_t) need * 2);
        previousWet.resize ((size_t) need * 2);
        bandStorage.resize ((size_t) need * 2 * numBands);
    }
}

void Module::setupCoefficients (const Parameters& p)
{
    float low  = std::min (p.lowCrossoverHz,  p.highCrossoverHz);
    float high = std::max (p.lowCrossoverHz, p.highCrossoverHz);

    low  = std::clamp (low,  minCrossoverHz, maxCrossoverHz);
    high = std::clamp (high, minCrossoverHz, maxCrossoverHz);

    // Push a collapsed crossover out of the way, so the filter that is no
    // longer splitting anything costs nothing while the other still does.
    if (mode == modeLow)
        high = maxCrossoverHz;
    else if (mode == modeHigh)
        low = minCrossoverHz;

    for (auto& f : splitLow)     f.setCutoff (low);
    for (auto& f : splitHigh)    f.setCutoff (high);
    for (auto& f : splitLowLeg)  f.setCutoff (high);
}

void Module::computeTargets (const Parameters& p, float* targets) const
{
    const float depth    = std::clamp (p.depth, 0.0f, 1.0f);
    const float upward   = std::max (p.upward, 0.0f);
    const float downward = std::max (p.downward, 0.0f);

    for (int b = 0; b < numBands; ++b)
    {
        targets[smUpwardThresholdLow + b]   = std::clamp (p.upwardThresholdDb[b], -100.0f, 12.0f);
        targets[smDownwardThresholdLow + b] = std::clamp (p.downwardThresholdDb[b], -100.0f, 12.0f);

        // Depth and the Upward/Downward macros scale the ratio. Matching
        // vitOTTx's updParams(), it is the product that gets smoothed, not the
        // macros themselves.
        targets[smUpwardRatioLow + b]   = std::clamp (p.upwardRatio[b], -1.0f, 1.0f) * depth * upward;
        targets[smDownwardRatioLow + b] = std::clamp (p.downwardRatio[b], 0.0f, 1.0f) * depth * downward;
    }

    targets[smAttack]  = std::clamp (p.attack, 0.0f, 1.0f);
    targets[smRelease] = std::clamp (p.release, 0.0f, 1.0f);

    targets[smLowGain]  = std::clamp (p.bandGainDb[lowBandIndex],  trimMinDb, trimMaxDb);
    targets[smMidGain]  = std::clamp (p.bandGainDb[midBandIndex],  trimMinDb, trimMaxDb);
    targets[smHighGain] = std::clamp (p.bandGainDb[highBandIndex], trimMinDb, trimMaxDb);

    const float low  = std::min (p.lowCrossoverHz,  p.highCrossoverHz);
    const float high = std::max (p.lowCrossoverHz, p.highCrossoverHz);
    targets[smLowCrossover]  = std::clamp (low,  minCrossoverHz, maxCrossoverHz);
    targets[smHighCrossover] = std::clamp (high, minCrossoverHz, maxCrossoverHz);

    targets[smMix]      = std::clamp (p.mix, 0.0f, 1.0f);
    targets[smBehavior] = std::clamp (p.behaviorDb, behaviorMinDb, behaviorMaxDb);
}

void Module::advanceSmoothers (int numSamples)
{
    if (needsSnap)
    {
        snapSmoothers();
        return;
    }

    float targets[numSmoothed];
    computeTargets (params, targets);

    // Vital's SmoothValue iterates a one-pole n times per block and its
    // consumers read element 0: the value used is one sample of decay past the
    // previous state, while the state advances a full block. decay^n reproduces
    // that loop exactly, without the loop.
    const float decayBase = -2.0f * float (M_PI) * smoothCutoffHz / (float) sampleRate;
    const float decay1 = std::exp (decayBase);
    const float decayN = std::exp (decayBase * (float) numSamples);

    for (int i = 0; i < numSmoothed; ++i)
    {
        const float delta = smoothed[i] - targets[i];

        blockValue[i] = targets[i] + decay1 * delta;

        float next = targets[i] + decayN * delta;

        // Guarantee the ramp actually lands. Scale-aware so log-spaced values
        // like the crossover frequencies settle rather than creeping forever.
        if (std::abs (next - targets[i]) < 1.0e-5f * (std::abs (targets[i]) + 1.0f))
        {
            next = targets[i];
            blockValue[i] = targets[i];
        }

        smoothed[i] = next;
    }

    // Only re-derive the crossover coefficients while they are actually moving.
    const float lowEdge  = splitLow[0].getCutoff();
    const float highEdge = splitHigh[0].getCutoff();
    const float wantedLow  = std::clamp (std::min (blockValue[smLowCrossover],  blockValue[smHighCrossover]),
                                         minCrossoverHz, maxCrossoverHz);
    const float wantedHigh = std::clamp (std::max (blockValue[smLowCrossover],  blockValue[smHighCrossover]),
                                         minCrossoverHz, maxCrossoverHz);

    if (wantedLow != lowEdge || wantedHigh != highEdge)
        setupCoefficients (params);
}

void Module::snapSmoothers()
{
    computeTargets (params, smoothed);
    std::copy (std::begin (smoothed), std::end (smoothed), std::begin (blockValue));
    needsSnap = false;

    for (int b = 0; b < numBands; ++b)
        lastBandGain[b] = std::pow (10.0f, blockValue[smLowGain + b] * 0.05f);

    lastMix     = blockValue[smMix];
    lastInGain  = std::pow (10.0f, std::clamp (params.inGainDb,  trimMinDb, trimMaxDb) * 0.05f);
    lastOutGain = std::pow (10.0f, std::clamp (params.outGainDb, trimMinDb, trimMaxDb) * 0.05f);

    setupCoefficients (params);
}

//==============================================================================
void Module::process (float* const* inputChannels, int numInputChannels, int numSamples)
{
    if (numSamples <= 0 || numInputChannels <= 0)
        return;

    ensureScratch (std::min (numSamples, chunkSize));

    const float inGain  = std::pow (10.0f, std::clamp (params.inGainDb,  trimMinDb, trimMaxDb) * 0.05f);
    const float outGain = std::pow (10.0f, std::clamp (params.outGainDb, trimMinDb, trimMaxDb) * 0.05f);

    // Mono hosts still run the full stereo machinery — the detector sees the
    // same signal twice and every filter and envelope stays primed — but only
    // the channel the host actually supplied is written back.
    const int activeChannels = std::min (numInputChannels, 2);
    const bool foldMono = (activeChannels == 1);

    // Input trim is smoothed the same way as the output trim: across the block
    // rather than in one step at the boundary, because that step lands directly
    // in the detector and shows up as a click when the knob moves.
    const float inGainStart = lastInGain;
    lastInGain = inGain;

    for (int offset = 0; offset < numSamples; )
    {
        const int n = std::min (numSamples - offset, chunkSize);
        const float inGainStep = (inGain - inGainStart) / (float) numSamples;

        // Input gain, plus the mono fold. Never in place: the dry path needs
        // the gained input and the mixer reads both buffers at the end.
        float inRamp = inGainStart + inGainStep * (float) offset;

        for (int ch = 0; ch < activeChannels; ++ch)
        {
            const float* src = inputChannels[ch] + offset;
            float* dst = dryStorage.data() + (size_t) ch * chunkSize;

            float g = inRamp;
            for (int i = 0; i < n; ++i)
            {
                dst[i] = src[i] * g;
                g += inGainStep;
            }
        }

        if (foldMono)
            std::copy (dryStorage.data(), dryStorage.data() + n,
                       dryStorage.data() + chunkSize);

        // The wet buffer is the SUM of every band, so it must start this chunk
        // at zero. Without this the bands stack on top of the previous block's
        // output and the level runs away within a few buffers.
        std::fill (wetStorage.begin(), wetStorage.end(), 0.0f);

        advanceSmoothers (n);

        splitBandsIntoScratch (n);

        for (int b = 0; b < numBands; ++b)
            runBand (b, n);

        crossfadeModeChange (n);

        finishBlock (inputChannels, offset, activeChannels, n, outGain);

        // The reduction meter is a level, not an energy, so it is averaged once
        // per block — not once per channel — or it would read half strength.
        for (int b = 0; b < numBands; ++b)
            bandGainDbAccum[b] += (double) bandReductionDb[b];

        ++blockCount;

        offset += n;
    }

    publishMetering();
}

void Module::crossfadeModeChange (int numSamples)
{
    // Activating a pending collapse invalidates the filter and envelope state,
    // because the new topology does not use the same bands. Clearing happens
    // here — once, before the crossfade below hides it. setupCoefficients() then
    // pushes the now-unused crossover out of the way.
    bool topologyChanged = false;

    if (pendingMode != mode)
    {
        mode = pendingMode;

        for (auto& f : splitLow)     f.reset();
        for (auto& f : splitHigh)    f.reset();
        for (auto& f : splitLowLeg)  f.reset();

        for (auto& band : compressors)
            for (auto& c : band)
                c.reset();

        for (auto& state : compressorState)
            for (auto& st : state)
                st = BandCompressor::State();

        setupCoefficients (params);
        topologyChanged = true;
    }

    for (int ch = 0; ch < 2; ++ch)
    {
        float* wet = wetStorage.data() + (size_t) ch * chunkSize;
        float* history = previousWet.data() + (size_t) ch * chunkSize;

        if (topologyChanged && hasWetHistory)
        {
            // A collapse (or un-collapse) invalidated the filter and envelope
            // state, so the wet path itself is discontinuous here even though
            // every parameter is smoothed. Fade the previous block's wet sum out
            // against this block's across the WHOLE block: the first sample is
            // then the old signal and the last is the new one, so there is no
            // step at either boundary. A partial-block fade would leave the new
            // signal's first sample exposed, which is exactly the step.
            for (int i = 0; i < numSamples; ++i)
            {
                const float t = (float) (i + 1) / (float) numSamples;
                wet[i] = history[i] * (1.0f - t) + wet[i] * t;
            }
        }

        std::copy (wet, wet + numSamples, history);
    }

    hasWetHistory = true;
}

void Module::splitBandsIntoScratch (int numSamples)
{
    for (int ch = 0; ch < 2; ++ch)
    {
        const float* dry = dryStorage.data() + (size_t) ch * chunkSize;

        float* bandLow  = bandBuffer (ch, lowBandIndex);
        float* bandMid  = bandBuffer (ch, midBandIndex);
        float* bandHigh = bandBuffer (ch, highBandIndex);

        switch (mode)
        {
            case modeSingle:
                // Both crossovers collapsed: one band carries the whole signal.
                std::copy (dry, dry + numSamples, bandMid);
                accumulateBandLevel (midBandIndex, bandMid, numSamples);
                break;

            case modeLow:
                // Mid/high collapsed: LOW | everything above it.
                for (int i = 0; i < numSamples; ++i)
                    splitLow[ch].process (dry[i], bandLow[i], bandMid[i]);

                accumulateBandLevel (lowBandIndex, bandLow, numSamples);
                accumulateBandLevel (midBandIndex, bandMid, numSamples);
                break;

            case modeHigh:
                // Low/mid collapsed: everything below | HIGH.
                for (int i = 0; i < numSamples; ++i)
                    splitHigh[ch].process (dry[i], bandMid[i], bandHigh[i]);

                accumulateBandLevel (midBandIndex, bandMid, numSamples);
                accumulateBandLevel (highBandIndex, bandHigh, numSamples);
                break;

            case modeMulti:
            default:
                // Both legs of the first split run through the second crossover:
                // the high leg separates MID from HIGH, and the low leg is split
                // and summed back so that LOW carries the same group delay as the
                // other two. Skipping that second pass on the low leg (which is
                // the obvious "optimisation") destroys the phase alignment and
                // audibly changes the sound, so it is not optional.
                for (int i = 0; i < numSamples; ++i)
                {
                    float lowLeg = 0.0f, highLeg = 0.0f;
                    splitLow[ch].process (dry[i], lowLeg, highLeg);

                    float lowLow = 0.0f, lowHigh = 0.0f;
                    splitLowLeg[ch].process (lowLeg, lowLow, lowHigh);

                    splitHigh[ch].process (highLeg, bandMid[i], bandHigh[i]);

                    bandLow[i] = lowLow + lowHigh;
                }

                accumulateBandLevel (lowBandIndex, bandLow, numSamples);
                accumulateBandLevel (midBandIndex, bandMid, numSamples);
                accumulateBandLevel (highBandIndex, bandHigh, numSamples);
                break;
        }
    }
}

BandCompressor::Coefficients Module::bandCoefficients (int band) const
{
    return BandCompressor::makeCoefficients (bandAttackMs[band], bandReleaseMs[band],
                                             blockValue[smAttack], blockValue[smRelease],
                                             sampleRate);
}

void Module::runBand (int band, int numSamples)
{
    float* left  = bandBuffer (0, band);
    float* right = bandBuffer (1, band);

    // Upward-expansion gate. At the Behavior minimum the floor is disabled, so
    // the upward stage behaves exactly like stock vitOTTx.
    const float floorDb = blockValue[smBehavior] <= behaviorMinDb
                            ? gateDisabled
                            : silenceFloorDb + blockValue[smBehavior];

    float reductionDb = 0.0f;

    float* stereo[2] = { left, right };
    compressors[band][0].process (stereo, 2, numSamples,
                                  bandCoefficients (band),
                                  blockValue[smDownwardThresholdLow + band],
                                  blockValue[smDownwardRatioLow + band],
                                  blockValue[smUpwardThresholdLow + band],
                                  blockValue[smUpwardRatioLow + band],
                                  floorDb,
                                  compressorState[band][0],
                                  &reductionDb);

    bandReductionDb[band] = reductionDb;

    // Makeup gain, interpolated across the block. The ramp starts at the LINEAR
    // multiplier the previous block ended on, not at a re-derived value, so the
    // first sample continues the old gain exactly and there is no seam.
    const float gainTarget = std::pow (10.0f, blockValue[smLowGain + band] * 0.05f);
    const float gainStart  = lastBandGain[band];
    const float gainStep   = (gainTarget - gainStart) / (float) numSamples;
    lastBandGain[band]     = gainTarget;

    float* wetLeft  = wetStorage.data();
    float* wetRight = wetStorage.data() + chunkSize;

    float ramp = gainStart;

    for (int i = 0; i < numSamples; ++i)
    {
        ramp += gainStep;

        wetLeft[i]  += left[i]  * ramp;
        wetRight[i] += right[i] * ramp;
    }
}

void Module::finishBlock (float* const* channels, int offset, int activeChannels,
                          int numSamples, float outGain)
{
    // Mix and output trim ramp across the block too, and the first block after
    // a reset ramps up from silence: applying full gain on the very first
    // sample is exactly the startup click this engine is built to avoid.
    const float mixStart  = lastMix;
    const float mixTarget = blockValue[smMix];
    lastMix = mixTarget;
    const float mixStep = (mixTarget - mixStart) / (float) numSamples;

    const float gainStart = lastOutGain;
    lastOutGain = outGain;
    const float gainStep = (outGain - gainStart) / (float) numSamples;

    float mix   = mixStart;
    float outLin = gainStart;

    float* out[2] = { channels[0] + offset,
                      activeChannels > 1 ? channels[1] + offset : nullptr };

    for (int i = 0; i < numSamples; ++i)
    {
        mix    += mixStep;
        outLin += gainStep;

        // Mix is the WET amount: 1.0 is fully compressed, 0.0 is the raw
        // (gain-adjusted) input. Getting this backwards makes the whole engine
        // inaudible, which is exactly what it did before this was fixed.
        const float wetMix = mix;
        const float dryMix = 1.0f - mix;

        for (int ch = 0; ch < activeChannels; ++ch)
        {
            const float dry = dryStorage[(size_t) ch * chunkSize + i];
            const float wet = wetStorage[(size_t) ch * chunkSize + i];

            // Soft output limiter. With the classic OTT curve and a sane input
            // level this stays below its knee and is bit-transparent; it only
            // acts when an extreme setting would otherwise hand the host a
            // clipped signal.
            const float mixed = dry * dryMix + wet * wetMix;
            out[ch][i] = softLimit (mixed * outLin);
        }
    }
}

//==============================================================================
void Module::accumulateBandLevel (int bandIndex, const float* samples, int numSamples)
{
    float power = 0.0f;

    for (int i = 0; i < numSamples; ++i)
        power += samples[i] * samples[i];

    bandPowerAccum[bandIndex] += power;
    bandSampleCount[bandIndex] += numSamples;
}

void Module::publishMetering()
{
    for (int b = 0; b < numBands; ++b)
    {
        const int count = bandSampleCount[b];

        if (count > 0)
        {
            const float power = bandPowerAccum[b] / (float) count;

            levels.inputDb[b] = power > 1.0e-12f ? 10.0f * std::log10 (power) : -100.0f;
        }

        if (blockCount > 0)
            levels.gainReductionDb[b] = (float) (bandGainDbAccum[b] / (double) blockCount);
    }

    clearMetering();
}

void Module::clearMetering()
{
    bandPowerAccum.fill (0.0f);
    bandReductionDb.fill (0.0f);
    bandGainDbAccum.fill (0.0);
    bandSampleCount.fill (0);
    blockCount = 0;
}

} // namespace ott
