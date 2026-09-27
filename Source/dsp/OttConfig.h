#pragma once

/*
    VibeOTT — tuning constants and parameter descriptors.

    The DSP is a faithful port of the OTT compressor from Vital
    (vital_dsp/compressor.cpp), the same algorithm used by vitOTT / vitOTTx /
    OTTx and by Ableton's OTT device. Values here are lifted from the reference
    scalar-C port (schwung-ottx, GPLv3) so the character matches sample for
    sample; only the noise-floor machinery is adapted for 32-bit float hosts.
*/

#include <algorithm>
#include <cmath>

/* MSVC's <cmath> does not define M_PI unless _USE_MATH_DEFINES is set before
   the first math header, and C++ itself only guarantees the value in
   <numbers>. Defining it here — with a guard, since Apple's and GNU's libc
   already provide it — keeps the DSP source free of platform #ifdefs. */
#ifndef M_PI
 #define M_PI 3.14159265358979323846
#endif

namespace ott
{

//==============================================================================
/** Master "Time" macro and the per-band base attack/release constants.

    Vital applies `exp2(time * 8 - 4) * base` to both attack and release, then
    floors the result at 5 samples. With the macro at 0.5 the multiplier is 1.0,
    so the numbers below are the nominal times.
*/
inline constexpr float lowAttackMs   = 2.8f;
inline constexpr float lowReleaseMs  = 40.0f;
inline constexpr float midAttackMs   = 1.4f;
inline constexpr float midReleaseMs  = 28.0f;
inline constexpr float highAttackMs  = 0.7f;
inline constexpr float highReleaseMs = 15.0f;

/** Hard floor on the envelope time constants, in samples (Vital's kMinEnv). */
inline constexpr float minEnvSamples = 5.0f;

/** Maximum combined upward expansion gain. Vital clamps here to keep the
    exponential from running away on digital silence. */
inline constexpr float maxExpansion = 32.0f;

/** One-pole cutoff used by Vital's SmoothValue for every DSP-facing
    parameter. 5 Hz is slow enough that a knob move is inaudible in isolation
    and fast enough that it never feels laggy. */
inline constexpr float smoothCutoffHz = 5.0f;

//==============================================================================
/** Noise-floor / upward-expansion gating.

    First, the thing that is easy to get wrong when reading the reference:
    **upstream has no noise-floor handling at all.** vitOTT/vitOTTx's
    compressor.cpp does not mention noise, floors, gates or LSBs anywhere; the
    only thing guarding the upward stage is `clamp(upper * lower, 0, 32)`, and
    its envelopes reset to zero (which is why a fresh instance starts by
    ramping up from nothing). The noise-floor gate is an addition made by the
    Schwung OTTx port for the Ableton Move, and by this plugin.

    So there is no upstream behaviour to match here -- the number below is a
    judgement call, and the honest framing is a comparison.

    Upward compression lifts *whatever is present*, so the question is what a
    band is allowed to lift. On an idle track (nothing playing, but dither and
    converter noise present), measured through the unmodified upstream
    algorithm:

        idle level    upstream lift     this plugin (-76 dBFS floor)
        -70 dBFS        +38.2 dB              +13.1 dB
        -80 dBFS        +17.4 dB              +13.1 dB
        -90 dBFS         +9.9 dB              +13.1 dB

    Upstream's own worst case is the one that bites: +38 dB on a -70 dBFS idle
    track puts it at -36 dBFS, which is plainly audible and pins the meters.
    That is the bug this gate exists to prevent, and it is why "match the
    reference" is not an option for a plugin living in a DAW.

    The Move port puts its floor at the 16-bit LSB (-90.3 dB) with the fade
    from -84 to -72 dB, because Move audio really does stall at +/-1 LSB. A
    32-bit float host is the opposite situation: its residue sits *higher* than
    that, not lower, so a directly borrowed floor lets idle noise through.

    -76 dBFS with the fade window at -70 to -58 dB is the calibrated result:
    the gate fully closes before an idle track's noise, while everything from
    -58 dBFS upward stays bit-identical to upstream, so real quiet material
    keeps its whole lift. A band at -75 dBFS is no longer lifted at all (gain
    reduction reads 0 dB), and raising the floor further changes nothing --
    there is nothing left to gate. Users with noisier sources can push
    `Behavior` up, which moves the same window.
*/
inline constexpr float defaultSilenceFloorDb = -76.0f;

inline constexpr float floorFadeStartDb   = 6.0f;
inline constexpr float floorFadeFullDb    = 18.0f;
inline constexpr float behaviorMinDb      = -12.0f;
inline constexpr float behaviorMaxDb      = 12.0f;

//==============================================================================
/** Per-band tuning: crossover corner and the two threshold/ratio pairs.

    The thresholds and ratios reproduce the classic OTT curve inherited from
    vitOTTx. The ratios are "amount" controls in 0..1 (downward) / -1..1
    (upward) which the compressor halves internally, so 0.9 downward is a
    1.818:1 ratio and 0.8 upward is a 1.667:1 expansion.

    The makeup gains are the same curve as upstream MINUS 3 dB. Upstream's
    numbers assume the 16-bit Move pipeline, and with them the classic curve
    peaks at 0 dBFS / ~-9 dBFS RMS on ordinary material — it was clipping the
    host on the first note. -3 dB lands full-range program at roughly
    -11 dBFS RMS with 4 dB of headroom, which is where a bus compressor
    belongs. Everything else about the curve is unchanged.
*/
struct BandDefaults
{
    float upwardThresholdDb;
    float upwardRatio;
    float downwardThresholdDb;
    float downwardRatio;
    float gainDb;
};

inline constexpr int numBands = 3;

inline constexpr BandDefaults bandDefaults[numBands] = {
    /* low  */ { -35.0f, 0.8f, -28.0f, 0.9f,   13.3f },
    /* mid  */ { -36.0f, 0.8f, -25.0f, 0.857f,  8.7f },
    /* high */ { -35.0f, 0.8f, -30.0f, 1.0f,   13.3f },
};

inline constexpr float defaultLowCrossoverHz  = 120.0f;
inline constexpr float defaultHighCrossoverHz = 2500.0f;

/** Crossover limits. The lower bound sits above `lowCollapseHz` on purpose:
    a corner that low has no musical use, and it is the one region where the
    band topology collapses, so keeping the control clear of it means a normal
    sweep never has to cross a topology change. */
inline constexpr float minCrossoverHz         = 30.0f;
inline constexpr float maxCrossoverHz         = 18000.0f;

inline constexpr float defaultMix   = 1.0f;
inline constexpr float defaultDepth = 1.0f;
inline constexpr float defaultTime  = 0.5f;

/** Crossover corners below/above these collapse a band into its neighbour,
    exactly as Vital's MultibandCompressor does.

    The collapse decision is hysteretic: once a band is collapsed the corner has
    to move `collapseHysteresisHz` past the threshold to bring it back. Without
    that, a swept crossover parked on the threshold toggles the band topology on
    alternate blocks, and every toggle discards the filter and envelope state.
    The audible result is a rattle rather than a sweep. */
inline constexpr float lowCollapseHz  = 21.0f;
inline constexpr float highCollapseHz = 17500.0f;
inline constexpr float collapseHysteresisHz = 2.0f;

inline constexpr float trimMinDb = -30.0f;
inline constexpr float trimMaxDb =  30.0f;

//==============================================================================
/** Output safety limiter.

    The wet path is a feed-forward compressor with substantial per-band makeup
    gain, so a deliberately extreme setting can produce more than full scale.
    Handing that to the host is not an option, but a hard clamp is itself a
    discontinuity — it puts a corner in the waveform and it made the automation
    tests pop. Instead the tail above `limiterSoftKnee` is folded through a
    scaled tanh, which is smooth, monotone, and transparent below the knee:
    at -6 dBFS it is still unity to within 0.1 dB, so normal material passes
    through untouched.
*/
inline constexpr float limiterSoftKnee   = 0.5f;   // -6 dBFS
inline constexpr float limiterCeiling    = 1.0f;
inline constexpr float limiterHeadroom   = 0.04f;  // 1 - output max after folding

/** Smoothly limits a sample. */
inline float softLimit (float x) noexcept
{
    const float magnitude = std::abs (x);

    if (magnitude <= limiterSoftKnee)
        return x;

    // Beyond the knee, map the excess through tanh. The argument is scaled so
    // that an input of 1.0 lands at limiterCeiling - limiterHeadroom.
    constexpr float excessRange = limiterCeiling - limiterSoftKnee;
    const float excess = magnitude - limiterSoftKnee;
    const float folded = limiterSoftKnee
                       + (excessRange - limiterHeadroom) * std::tanh (excess / (excessRange - limiterHeadroom));

    return std::copysign (std::min (folded, limiterCeiling), x);
}

} // namespace ott
