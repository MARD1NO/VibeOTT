#pragma once

#include "OttConfig.h"

#include <algorithm>
#include <cmath>
#include <cstdint>
#include <limits>

/** The crossover's "did the frequency change at all" guard compares floats for
    exact equality on purpose: the parameter smoother snaps to its target, and a
    tolerance would keep re-deriving tan() forever as it converged. Silence the
    one warning that complains about it rather than weakening the check. */
#if defined(__clang__)
 #define VIBEOTT_FLOAT_EQUAL_PUSH \
     _Pragma ("clang diagnostic push") _Pragma ("clang diagnostic ignored \"-Wfloat-equal\"")
 #define VIBEOTT_FLOAT_EQUAL_POP _Pragma ("clang diagnostic pop")
#else
 #define VIBEOTT_FLOAT_EQUAL_PUSH
 #define VIBEOTT_FLOAT_EQUAL_POP
#endif

namespace ott
{

//==============================================================================
/** 4th-order Linkwitz-Riley crossover.

    Two cascaded 2nd-order Butterworth sections per output, matching
    vital_dsp/linkwitz_riley_filter.cpp. The low and high outputs sum to an
    allpass (flat magnitude, rotated phase), which is what makes the three-way
    reconstruction usable as a dry path.
*/
class LinkwitzRiley
{
public:
    void prepare (double newSampleRate) noexcept
    {
        sampleRate = newSampleRate;
        reset();
    }

    void reset() noexcept
    {
        lowStage1.reset();
        lowStage2.reset();
        highStage1.reset();
        highStage2.reset();
    }

    void setCutoff (float hz) noexcept
    {
        hz = std::clamp (hz, 1.0f, 0.49f * (float) sampleRate);

        // Exact compare on purpose: this is a "did the value change at all"
        // guard so the tan() and the coefficient maths can be skipped while the
        // crossover sits still, and the smoother snaps to its target exactly.
        // A tolerance would keep re-running tan() forever as it converged.
        VIBEOTT_FLOAT_EQUAL_PUSH
        const bool unchanged = (hz == cutoffHz);
        VIBEOTT_FLOAT_EQUAL_POP

        if (unchanged)
            return;

        cutoffHz = hz;

        // Robust (RBJ) bilinear-transform biquad at Q = 1/sqrt(2), i.e. a
        // 2nd-order Butterworth section. Two of these in cascade per output make
        // a 4th-order Linkwitz-Riley, and low + high is then an exact allpass.
        //
        // The high-pass denominator is the LP/HP dual of the low-pass one: the
        // linear term flips sign because s -> 1/s. Deriving it with a single
        // shared sign — which is what the reference scalar-C port does — drops
        // the high-pass to Q = 0.5, and the three bands no longer sum to an
        // allpass: there is a ~3 dB dip at every crossover, which changes the
        // sound and makes the fully-dry path not transparent.
        const float omega = float (M_PI) * hz / (float) sampleRate; // pi * fc / fs
        const float cosw  = std::cos (omega);
        const float sinw  = std::sin (omega);
        const float alpha = sinw / (2.0f * q);
        const float a0    = 1.0f + alpha;
        const float invA0 = 1.0f / a0;

        lowB0 = (1.0f - cosw) * 0.5f * invA0;
        lowB1 = (1.0f - cosw)         * invA0;
        lowB2 = lowB0;
        lowA1 = -2.0f * cosw          * invA0;
        lowA2 = (1.0f - alpha)        * invA0;

        highB0 = (1.0f + cosw) * 0.5f * invA0;
        highB1 = -(1.0f + cosw)       * invA0;
        highB2 = highB0;
        highA1 = lowA1;
        highA2 = lowA2;
    }

    /** Consumes one sample, emits the low and high outputs. */
    inline void process (float x, float& low, float& high) noexcept
    {
        low  = lowStage1.process  (x, lowB0, lowB1, lowB2, lowA1, lowA2);
        low  = lowStage2.process  (low, lowB0, lowB1, lowB2, lowA1, lowA2);
        high = highStage1.process (x, highB0, highB1, highB2, highA1, highA2);
        high = highStage2.process (high, highB0, highB1, highB2, highA1, highA2);
    }

    float getCutoff() const noexcept { return cutoffHz; }

private:
    /** Butterworth Q. */
    static constexpr float q = 0.70710678118654752440f;

    struct Biquad
    {
        float in1 = 0.0f, in2 = 0.0f, out1 = 0.0f, out2 = 0.0f;
        bool valid = false;

        void reset() noexcept { in1 = in2 = out1 = out2 = 0.0f; valid = false; }

        /** Direct Form I with the standard (non-negated) a1/a2 convention. */
        inline float process (float x, float b0, float b1, float b2, float a1, float a2) noexcept
        {
            float y;
            if (valid)
                y = x * b0 + in1 * b1 + in2 * b2 - out1 * a1 - out2 * a2;
            else
            {
                // First sample after a reset: start from zero history instead of
                // evaluating it, so a reset can never emit a stale transient.
                y = x * b0;
                valid = true;
            }

            in2 = in1; in1 = x;
            out2 = out1; out1 = y;
            return y;
        }
    };

    double sampleRate = 44100.0;
    float  cutoffHz   = -1.0f;

    float lowB0 = 0.0f, lowB1 = 0.0f, lowB2 = 0.0f, lowA1 = 0.0f, lowA2 = 0.0f;
    float highB0 = 0.0f, highB1 = 0.0f, highB2 = 0.0f, highA1 = 0.0f, highA2 = 0.0f;

    Biquad lowStage1, lowStage2;
    Biquad highStage1, highStage2;
};

//==============================================================================
/** Vital's RMS compressor stage: two log-domain envelope followers, one for
    the downward (peak-taming) path and one for the upward (quiet-lifting)
    path, combined into a single gain multiplier.

    Detection works in the power domain and is linked across all channels of
    the band (the maximum instantaneous power), so a hard-panned transient
    ducks both sides and the stereo image never shifts.
*/
class BandCompressor
{
public:
    struct Coefficients
    {
        float attackScale  = 1.0f;  // 1 / (envAtt + 1)
        float releaseScale = 1.0f;  // 1 / (envRel + 1)
        float attackAmount  = 0.0f; // envAtt
        float releaseAmount = 0.0f; // envRel
    };

    /** Precomputes the envelope coefficients for one band.

        @param attackMs    nominal attack time at Attack = 0.5
        @param releaseMs   nominal release time at Release = 0.5
        @param attackMacro 0..1, higher = slower
        @param releaseMacro 0..1, higher = slower
    */
    static Coefficients makeCoefficients (float attackMs, float releaseMs,
                                          float attackMacro, float releaseMacro,
                                          double sampleRate) noexcept
    {
        const float samplesPerMs = (float) sampleRate / 1000.0f;

        const float attackMul  = std::exp2 (std::clamp (attackMacro,  0.0f, 1.0f) * 8.0f - 4.0f);
        const float releaseMul = std::exp2 (std::clamp (releaseMacro, 0.0f, 1.0f) * 8.0f - 4.0f);

        Coefficients c;
        c.attackAmount  = std::max (attackMs  * attackMul  * samplesPerMs, minEnvSamples);
        c.releaseAmount = std::max (releaseMs * releaseMul * samplesPerMs, minEnvSamples);
        c.attackScale   = 1.0f / (c.attackAmount  + 1.0f);
        c.releaseScale  = 1.0f / (c.releaseAmount + 1.0f);
        return c;
    }

    void reset() noexcept
    {
        highEnv = silenceFloorPower;
        lowEnv  = silenceFloorPower;
    }

    /** Where the envelope followers park while the band is silent.

        Ramping up from a smaller value would make the upward path see a
        near-zero envelope on the first block and slam +30 dB of gain onto it;
        parking at the floor means silence maps to unity gain.
    */
    static constexpr float silenceFloorPower = 1.0e-10f; // -100 dB

    /** Per-band state carried between blocks. `numerators[0]` is the exponent
        the previous block ended on; a non-finite value means there is no
        history yet (just after a reset) and the ramp should be skipped. */
    struct State
    {
        float numerators[1] { std::numeric_limits<float>::quiet_NaN() };
    };

    /** Processes one band in place.

        @param channels      array of `numChannels` pointers, all written
        @param numChannels   at least 1
        @param upwardRatio   negative values turn the upward stage into an expander
        @param floorDb       upper edge of the noise-floor gate; pass -infinity
                             to disable the gate
        @param state         per-band crossover state, carried between blocks.
                             `numerators` holds the exponent the previous block
                             ended on, which this call interpolates away from so
                             the gain is continuous across the block boundary.
                             Depth, the Up/Down macros and the per-band ratios
                             all land in that exponent, so letting it step is an
                             audible click per encoder detent.
        @param gainDbOut     receives the mean gain applied this block
    */
    void process (float* const* channels, int numChannels, int numSamples,
                  const Coefficients& coeffs,
                  float downwardThresholdDb, float downwardRatio,
                  float upwardThresholdDb, float upwardRatio,
                  float floorDb,
                  State& state,
                  float* gainDbOut) noexcept
    {
        // Thresholds enter in dB and are compared against the envelope's
        // log2(power); converting once per block keeps log/exp out of the loop.
        const float utPow = std::pow (10.0f, std::clamp (downwardThresholdDb, -100.0f, 12.0f) * 0.05f);
        const float ut = utPow * utPow;

        const float ltPow = std::pow (10.0f, std::clamp (upwardThresholdDb, -100.0f, 12.0f) * 0.05f);
        const float lt = ltPow * ltPow;

        const float ur = std::clamp (downwardRatio, 0.0f, 1.0f) * 0.5f;
        const float lr = std::clamp (upwardRatio, -1.0f, 1.0f) * 0.5f;

        // Noise-floor fade window, expressed in log2(power) units.
        constexpr float dbToLog2Power = 0.33219281f;
        const bool  gateEnabled = std::isfinite (floorDb);
        const float floorLo  = (floorDb + floorFadeStartDb) * dbToLog2Power;
        const float floorInv = 1.0f / ((floorFadeFullDb - floorFadeStartDb) * dbToLog2Power);
        const bool  gateApplies = gateEnabled && lr > 0.0f;

        const float attAmount = coeffs.attackAmount;
        const float relAmount = coeffs.releaseAmount;
        const float attScale  = coeffs.attackScale;
        const float relScale  = coeffs.releaseScale;

        float henv = highEnv;
        float lenv = lowEnv;

        // The exponent the gain is raised to, ramped across the block. It starts
        // at whatever the previous block ended on, so the first sample continues
        // the previous gain exactly, and lands on this block's target by the
        // last. `nan` marks "no history yet", which happens right after a reset:
        // there the target is taken directly, since there is no seam to hide.
        float numerator = state.numerators[0];
        const bool snapped = ! std::isfinite (numerator);

        float lastTarget = numerator;

        double gainDbSum = 0.0;

        for (int i = 0; i < numSamples; ++i)
        {
            // Linked detection: the loudest channel drives both.
            float power = 0.0f;
            for (int ch = 0; ch < numChannels; ++ch)
            {
                const float s = channels[ch][i];
                power = std::max (power, s * s);
            }

            //--- downward: fast on the way up, clamped to the threshold -----
            if (power > henv)
                henv = (power + henv * attAmount) * attScale;
            else
                henv = (power + henv * relAmount) * relScale;

            if (henv < ut)
                henv = ut;

            //--- upward: clamped to the threshold (never above it) ----------
            if (power > lenv)
                lenv = (power + lenv * attAmount) * attScale;
            else
                lenv = (power + lenv * relAmount) * relScale;

            if (lenv > lt)
                lenv = lt;

            float effectiveUpwardRatio = lr;

            if (gateApplies)
            {
                // Smoothstep fade in as the band rises out of the noise floor.
                const float w = std::clamp ((std::log2 (lenv) - floorLo) * floorInv, 0.0f, 1.0f);
                effectiveUpwardRatio = lr * (w * w * (3.0f - 2.0f * w));
            }

            // This sample's exponent, in log2(power) units, with no pow() at
            // all: log2 of the ratio each stage asks for, scaled by that stage's
            // amount. The two stages multiply, so their exponents add.
            const float downLog = std::log2 (ut / henv);
            const float upLog   = std::log2 (lt / lenv);
            const float target  = ur * downLog + effectiveUpwardRatio * upLog;
            lastTarget = target;

            // Interpolate the exponent across the remaining samples. Smooth and
            // monotone, so a parameter move is a glide rather than a step, and
            // the first sample lands exactly where the previous block ended.
            // exp2 is monotone, so interpolating the exponent keeps the ramped
            // gain between the old and new gain at every sample.
            if (snapped)
                numerator = target;
            else
                numerator += (target - numerator) / (float) (numSamples - i);

            const float g = std::clamp (std::exp2 (numerator), 0.0f, maxExpansion);

            for (int ch = 0; ch < numChannels; ++ch)
                channels[ch][i] *= g;

            gainDbSum += g > 0.0f ? 20.0 * std::log10 ((double) g) : -100.0;
        }

        state.numerators[0] = std::isfinite (lastTarget) ? lastTarget : 0.0f;

        highEnv = henv;
        lowEnv  = lenv;

        if (gainDbOut != nullptr)
            *gainDbOut = numSamples > 0 ? (float) (gainDbSum / (double) numSamples) : 0.0f;
    }

private:
    float highEnv = silenceFloorPower;
    float lowEnv  = silenceFloorPower;
};

} // namespace ott
