#pragma once

#include "OttFilters.h"

#include <array>
#include <cstddef>
#include <vector>

namespace ott
{

//==============================================================================
/** Everything the DSP engine reads for one block.

    Plain values, copied field by field from the host's parameter atomics on the
    audio thread — no allocation, no locks, and each field read exactly once so
    a mid-block parameter change can never tear.
*/
struct Parameters
{
    float mix        = defaultMix;
    float depth      = defaultDepth;
    float upward     = 1.0f;
    float downward   = 1.0f;
    float attack     = defaultTime;
    float release    = defaultTime;
    float inGainDb   = 0.0f;
    float outGainDb  = 0.0f;
    float behaviorDb = 0.0f;

    /** Where the upward-expansion gate starts fading in, in dBFS. Anything at
        or below this level is treated as "nothing playing" and is passed
        through rather than lifted. See OttConfig.h for why this cannot simply
        be the 16-bit LSB. */
    float silenceFloorDb = defaultSilenceFloorDb;
    float lowCrossoverHz  = defaultLowCrossoverHz;
    float highCrossoverHz = defaultHighCrossoverHz;

    std::array<float, numBands> upwardThresholdDb {};
    std::array<float, numBands> upwardRatio {};
    std::array<float, numBands> downwardThresholdDb {};
    std::array<float, numBands> downwardRatio {};
    std::array<float, numBands> bandGainDb {};

    /** Fills every band with the classic OTT curve inherited from vitOTTx. */
    static Parameters makeDefault()
    {
        Parameters p;

        for (std::size_t b = 0; b < (std::size_t) numBands; ++b)
        {
            const auto& d = bandDefaults[b];
            p.upwardThresholdDb[b]   = d.upwardThresholdDb;
            p.upwardRatio[b]         = d.upwardRatio;
            p.downwardThresholdDb[b] = d.downwardThresholdDb;
            p.downwardRatio[b]       = d.downwardRatio;
            p.bandGainDb[b]          = d.gainDb;
        }

        return p;
    }
};

//==============================================================================
/** Per-band metering snapshot, published once per block for the editor. */
struct BandLevels
{
    std::array<float, numBands> inputDb {};         // band level at the compressor input
    std::array<float, numBands> gainReductionDb {}; // average applied gain, dB (negative = compressing)

    void reset() noexcept
    {
        inputDb.fill (-100.0f);
        gainReductionDb.fill (0.0f);
    }

    BandLevels() { reset(); }
};

//==============================================================================
/** VibeOTT's DSP engine: three-band upward + downward RMS compression.

    Signal flow, per block, per channel:

        input gain
          -> 4th-order Linkwitz-Riley split at the low crossover
          -> the high leg is split again at the high crossover into mid / high
          -> per-band dual-envelope RMS compressor (detector linked across channels)
          -> per-band makeup gain
          -> dry/wet mix against the phase-compensated crossover sum
          -> output gain

    Every user-facing parameter passes through a 5 Hz one-pole smoother that is
    advanced once per block, and the band makeup gains, the dry/wet mix and the
    output gain are additionally interpolated sample by sample across the block.
    That second ramp is what keeps parameter moves click-free: the smoother
    alone still steps at every block boundary, which is audible as a click per
    encoder detent (measured upstream at ~28% of peak for one 0.02 depth step).
*/
class Module
{
public:
    Module();

    /** Prepares for playback. Safe with any block size: an internal chunk size
        bounds the scratch buffers, so an over-large host block is processed in
        pieces rather than triggering an audio-thread allocation. */
    void prepare (double sampleRate, int maximumBlockSize, int numChannels);

    /** Clears filters, envelopes and metering without touching user
        parameters, so a transport stop or bypass cannot produce a transient. */
    void reset();

    void setParameters (const Parameters& p);

    /** Processes an in-place, non-interleaved buffer of one or two channels. */
    void process (float* const* channels, int numChannels, int numSamples);

    const BandLevels& getBandLevels() const noexcept { return levels; }

    /** The engine is fully feed-forward, so it reports no latency. */
    int getLatencySamples() const noexcept { return 0; }

private:
    //== smoothed parameter slots (gains are contiguous, as are the per-band
    //   threshold and ratio blocks) ==========================================
    enum SmoothedParam
    {
        smUpwardThresholdLow, smUpwardThresholdMid, smUpwardThresholdHigh,
        smDownwardThresholdLow, smDownwardThresholdMid, smDownwardThresholdHigh,
        smUpwardRatioLow, smUpwardRatioMid, smUpwardRatioHigh,
        smDownwardRatioLow, smDownwardRatioMid, smDownwardRatioHigh,
        smAttack, smRelease,
        smLowGain, smMidGain, smHighGain,
        smLowCrossover, smHighCrossover,
        smMix,
        smBehavior,
        numSmoothed
    };

    enum Mode { modeMulti = 0, modeLow, modeHigh, modeSingle };

    Mode currentMode (const Parameters& p) const;
    void updateMode();
    void setupCoefficients (const Parameters& p);
    void computeTargets (const Parameters& p, float* targets) const;
    void advanceSmoothers (int numSamples);
    void snapSmoothers();
    void ensureScratch (int numSamples);
    void crossfadeModeChange (int numSamples);

    void splitBandsIntoScratch (int numSamples);
    void runBand (int band, int numSamples);
    void finishBlock (float* const* channels, int offset, int activeChannels,
                      int numSamples, float outGain);

    BandCompressor::Coefficients bandCoefficients (int band) const;
    void accumulateBandLevel (int bandIndex, const float* samples, int numSamples);
    void publishMetering();
    void clearMetering();

    //== buffer accessors =====================================================
    inline float* bandBuffer (int channel, int band) noexcept
    {
        return bandStorage.data() + ((size_t) channel * numBands + (size_t) band) * (size_t) chunkSize;
    }

    inline const float* bandBuffer (int channel, int band) const noexcept
    {
        return bandStorage.data() + ((size_t) channel * numBands + (size_t) band) * (size_t) chunkSize;
    }

    //== configuration ========================================================
    double sampleRate = 44100.0;
    int    chunkSize  = 512;

    //== smoothed values ======================================================
    float smoothed[numSmoothed] {};    // cross-block one-pole state
    float blockValue[numSmoothed] {};  // the value this block's DSP actually uses

    //== filter state [stage][channel] ========================================
    /** Stage 1 splits the input at the low crossover into a low leg and a high
        leg. Stage 2 then splits BOTH legs at the high crossover: the high leg
        yields MID and HIGH, while the low leg is split and the two halves are
        summed back into LOW so that all three bands carry the same group delay.
        That needs two independent stage-2 filters, since sharing one would
        interleave two different signals through a single filter history. */
    LinkwitzRiley splitLow[2];      // stage 1: input       -> low leg | high leg
    LinkwitzRiley splitHigh[2];     // stage 2: high leg    -> mid | high
    LinkwitzRiley splitLowLeg[2];   // stage 2: low leg     -> low (halves summed)

    BandCompressor compressors[numBands][2];
    BandCompressor::State compressorState[numBands][2];

    //== scratch buffers, each sized chunkSize per channel ====================
    std::vector<float> dryStorage;   // gained input                  [2][chunk]
    std::vector<float> wetStorage;   // compressed + gained band sum  [2][chunk]
    std::vector<float> bandStorage;  // raw bands for dry and detection [2][3][chunk]
    std::vector<float> previousWet;  // last block's wet sum, for the mode crossfade [2][chunk]

    //== parameter ramps that must be per-sample within a block ===============
    float lastBandGain[numBands] {};
    float lastMix = defaultMix;
    float lastInGain = 1.0f;
    float lastOutGain = 1.0f;

    //== metering accumulators ================================================
    /** Band levels are summed over both channels; the reduction meter holds an
        already-averaged dB value per band, so it is accumulated once per block. */
    BandLevels levels;
    std::array<float, numBands> bandPowerAccum {};
    std::array<float, numBands> bandReductionDb {};
    std::array<double, numBands> bandGainDbAccum {};
    std::array<int, numBands> bandSampleCount {};
    int blockCount = 0;

    //== live state ===========================================================
    Parameters params;
    Mode mode = modeMulti;
    Mode pendingMode = modeMulti;   // set when a collapse changed the topology
    bool needsSnap = true;
    bool hasWetHistory = false;
};

} // namespace ott
