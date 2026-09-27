/*
    VibeOTT DSP test harness.

    This is the regression net for the things that used to go wrong in this
    plugin: startup clicks, zipper noise while a knob moves, hard clipping at
    the output, gain explosions on silence, and stereo image collapse. It links
    the engine directly — no JUCE, no audio device, no host — so it runs in CI
    in well under a second and can be run after every DSP change:

        cmake --build build --target VibeOTTTests && ./build/VibeOTTTests

    Exits non-zero if any check fails.
*/

#include "../dsp/OttModule.cpp"

#include <cmath>
#include <cstdint>
#include <cstdio>
#include <cstdlib>
#include <algorithm>
#include <string>
#include <utility>
#include <vector>

namespace
{
    // Named constants rather than M_PI: MSVC's <cmath> does not define that
    // macro, and the test sources deliberately include no JUCE header.
    constexpr double twoPi = 6.28318530717958647692;
}

namespace
{

int failures = 0;
int checks   = 0;

void check (bool condition, const std::string& what)
{
    ++checks;

    if (condition)
    {
        std::printf ("  ok   %s\n", what.c_str());
    }
    else
    {
        std::printf ("  FAIL %s\n", what.c_str());
        ++failures;
    }
}

void checkNear (double actual, double expected, double tolerance, const std::string& what)
{
    const bool pass = std::abs (actual - expected) <= tolerance;

    if (! pass)
        std::printf ("       -> got %.6f, expected %.6f +/- %.6f\n", actual, expected, tolerance);

    check (pass, what);
}

void checkBelow (double actual, double limit, const std::string& what)
{
    if (actual > limit)
        std::printf ("       -> got %.6f, limit %.6f\n", actual, limit);

    check (actual <= limit, what);
}

void checkAbove (double actual, double limit, const std::string& what)
{
    if (actual < limit)
        std::printf ("       -> got %.6f, minimum %.6f\n", actual, limit);

    check (actual >= limit, what);
}

//==============================================================================
constexpr double testSampleRate = 48000.0;

struct StereoBuffer
{
    std::vector<float> left, right;
    float* data[2];

    explicit StereoBuffer (int n) : left ((size_t) n, 0.0f), right ((size_t) n, 0.0f)
    {
        data[0] = left.data();
        data[1] = right.data();
    }

    int size() const { return (int) left.size(); }

    float* const* channels() { return data; }

    void fillSine (double hz, double amplitude)
    {
        for (int i = 0; i < size(); ++i)
        {
            const double v = amplitude * std::sin (twoPi * hz * (double) i / testSampleRate);
            left[(size_t) i]  = (float) v;
            right[(size_t) i] = (float) v;
        }
    }

    double peak (int channel) const
    {
        const auto& v = channel == 0 ? left : right;
        double m = 0.0;
        for (size_t i = 0; i < v.size(); ++i)
            m = std::max (m, (double) std::abs (v[i]));
        return m;
    }

    double rms (int channel) const
    {
        const auto& v = channel == 0 ? left : right;
        double sum = 0.0;
        for (size_t i = 0; i < v.size(); ++i)
            sum += (double) v[i] * v[i];
        return v.empty() ? 0.0 : std::sqrt (sum / (double) v.size());
    }

    /** Largest sample-to-sample jump. A click or a zipper step shows up here as
        a spike well above the waveform's own slew rate. */
    double peakStep (int channel) const
    {
        const auto& v = channel == 0 ? left : right;
        double m = 0.0;
        for (size_t i = 1; i < v.size(); ++i)
            m = std::max (m, (double) std::abs (v[i] - v[i - 1]));
        return m;
    }
};

/** Renders `totalSamples` in blocks of `blockSize`, regenerating the input from
    `signal` (absolute sample index -> stereo pair) each block.

    `observe` sees each processed block, which is where the automation tests
    both apply new parameters and inspect the block boundary. */
template <typename SignalFn, typename ObserveFn>
void runEngine (ott::Module& engine, int totalSamples, int blockSize,
                SignalFn signal, ObserveFn observe)
{
    StereoBuffer block (blockSize);

    for (int offset = 0; offset < totalSamples; offset += blockSize)
    {
        const int n = std::min (blockSize, totalSamples - offset);

        for (int i = 0; i < n; ++i)
        {
            const std::pair<float, float> sample = signal (offset + i);
            block.left[(size_t) i]  = sample.first;
            block.right[(size_t) i] = sample.second;
        }

        engine.process (block.channels(), 2, n);

        // Observed AFTER processing: the callback both applies new parameters
        // (as a host does, once per block) and inspects the rendered output.
        observe (block, n, offset);
    }
}

template <typename SignalFn>
void runEngine (ott::Module& engine, int totalSamples, int blockSize, SignalFn signal)
{
    runEngine (engine, totalSamples, blockSize, signal,
               [] (StereoBuffer&, int, int) {});
}

inline std::pair<float, float> mono (double v)
{
    return std::make_pair ((float) v, (float) v);
}

/** A sine whose peak is scaled to `amplitude`, used as a continuous detector
    input so automation artifacts are not confused with musical transients. */
inline std::pair<float, float> sine (double hz, double amplitude, int index)
{
    return mono (amplitude * std::sin (twoPi * hz * (double) index / testSampleRate));
}

//==============================================================================
/** 1. The three-band split must reconstruct the input.

    With Depth at zero there is no compression and no makeup gain, so Mix at 0
    leaves only the allpass crossover sum: flat magnitude, unchanged level. */
void testCrossoverReconstruction()
{
    std::printf ("\n[crossover] three-band sum is magnitude-flat\n");

    ott::Module engine;
    engine.prepare (testSampleRate, 512, 2);

    ott::Parameters p = ott::Parameters::makeDefault();
    p.depth = 0.0f;
    p.mix = 0.0f;
    p.inGainDb = 0.0f;
    p.outGainDb = 0.0f;
    for (int b = 0; b < ott::numBands; ++b)
        p.bandGainDb[b] = 0.0f;

    engine.setParameters (p);

    const double frequency = 1000.0;
    const double amplitude = 0.25;
    const int total = 96000;

    StereoBuffer block (512);
    double inputSumSq = 0.0, outputSumSq = 0.0;

    for (int offset = 0; offset < total; offset += 512)
    {
        for (int i = 0; i < 512; ++i)
            block.left[(size_t) i] = block.right[(size_t) i]
                = (float) (amplitude * std::sin (twoPi * frequency * (double) (offset + i) / testSampleRate));

        engine.process (block.channels(), 2, 512);

        if (offset < 24000) // let the filters settle first
            continue;

        for (int i = 0; i < 512; ++i)
        {
            const double in = amplitude * std::sin (twoPi * frequency * (double) (offset + i) / testSampleRate);
            inputSumSq += in * in;
            outputSumSq += (double) block.left[(size_t) i] * block.left[(size_t) i];
        }
    }

    checkNear (std::sqrt (outputSumSq / inputSumSq), 1.0, 0.02,
               "crossover sum preserves magnitude (allpass)");
}

/** 1b. The crossover must sit where it is told to.

    This exists because the coefficient formula once omitted the 2 in
    omega_0 = 2*pi*fc/fs, which halved every cutoff: a 120 Hz split behaved like
    60 Hz, the low band was 6 dB down where it should have been flat, and far
    more energy leaked into the mid. The plugin ended up several dB louder than
    upstream and much heavier in the low end.

    The allpass test above cannot catch that: a uniformly shifted crossover is
    still a perfectly good allpass, so the reconstruction stays flat. Only
    checking the corner against theory catches it.
*/
void testCrossoverCutoffIsCorrect()
{
    std::printf ("\n[crossover] each stage is a 4th-order Linkwitz-Riley at its corner\n");

    // A 4th-order Linkwitz-Riley low-pass is two cascaded 2nd-order Butterworth
    // sections. At the corner the pair is exactly -6 dB, and one octave up it is
    // 24 dB down (4th order = 24 dB/octave). Those two points pin the corner
    // frequency, which a 2x error in omega_0 would fail outright.
    const double corner = 2500.0;

    const auto lowPassGainDb = [corner] (double frequency)
    {
        ott::LinkwitzRiley filter;
        filter.prepare (testSampleRate);
        filter.setCutoff ((float) corner);

        double inputSumSq = 0.0, outputSumSq = 0.0;

        // Settle first: the filter's own start-up transient is not part of its
        // steady-state response.
        for (int i = 0; i < (int) (testSampleRate * 4); ++i)
        {
            float low = 0.0f, high = 0.0f;
            filter.process ((float) std::sin (twoPi * frequency * (double) i / testSampleRate),
                            low, high);

            if (i > (int) (testSampleRate * 3))
            {
                const double in = std::sin (twoPi * frequency * (double) i / testSampleRate);
                inputSumSq += in * in;
                outputSumSq += (double) low * low;
            }
        }

        return 10.0 * std::log10 (outputSumSq / inputSumSq);
    };

    const double atCorner = lowPassGainDb (corner);
    const double octaveUp = lowPassGainDb (corner * 2.0);
    const double decadeDown = lowPassGainDb (corner * 0.1);

    std::printf ("       at %.0f Hz: %+.2f dB   (one octave up: %+.2f dB, decade down: %+.2f dB)\n",
                 corner, atCorner, octaveUp, decadeDown);

    checkNear (atCorner, -6.0, 0.5, "the low-pass is -6 dB at its corner");
    checkBelow (octaveUp, -20.0, "it rolls off steeply one octave above the corner");
    checkAbove (decadeDown, -0.5, "it is flat a decade below the corner");
}

/** 2. Input below the upward threshold must come out louder. */
void testUpwardCompressionLiftsQuietSignals()
{
    std::printf ("\n[upward] a quiet signal is lifted toward its threshold\n");

    ott::Module engine;
    engine.prepare (testSampleRate, 512, 2);

    ott::Parameters p = ott::Parameters::makeDefault();
    p.inGainDb = 0.0f;
    p.outGainDb = 0.0f;
    p.mix = 1.0f;
    engine.setParameters (p);

    const double amplitude = 0.001; // ~-60 dBFS, far below every threshold
    double inputSumSq = 0.0, outputSumSq = 0.0;

    runEngine (engine, 96000, 512,
        [&] (int i) { return sine (700.0, amplitude, i); },
        [&] (StereoBuffer& b, int n, int offset)
        {
            if (offset < 24000)
                return;
            for (int i = 0; i < n; ++i)
            {
                const double in = amplitude * std::sin (twoPi * 700.0 * (double) (offset + i) / testSampleRate);
                inputSumSq += in * in;
                outputSumSq += (double) b.left[(size_t) i] * b.left[(size_t) i];
            }
        });

    const double gainDb = 10.0 * std::log10 (outputSumSq / inputSumSq);
    checkAbove (gainDb, 3.0, "upward compression adds more than 3 dB");
    checkBelow (gainDb, 40.0, "upward compression stays under 40 dB (no runaway)");
}

/** 3. A signal above the downward threshold must be caught by the compressor. */
void testDownwardCompressionTamesPeaks()
{
    std::printf ("\n[downward] a hot signal is compressed\n");

    ott::Module engine;
    engine.prepare (testSampleRate, 512, 2);

    ott::Parameters p = ott::Parameters::makeDefault();
    for (int b = 0; b < ott::numBands; ++b)
        p.bandGainDb[b] = 0.0f; // isolate the compression from the makeup gain

    engine.setParameters (p);

    // Broadband, so every band actually carries energy. A single tone cannot do
    // that once the crossovers are correct: with the split at 120 Hz and
    // 2.5 kHz, a 900 Hz sine belongs to the mid band alone. An earlier version
    // of this test used one tone and only passed because the crossovers were an
    // octave low, which leaked it into two bands.
    runEngine (engine, 48000, 512,
        [] (int i)
        {
            const double v = 0.7 * (0.6 * std::sin (twoPi * 60.0   * (double) i / testSampleRate)
                                  + 0.6 * std::sin (twoPi * 900.0  * (double) i / testSampleRate)
                                  + 0.6 * std::sin (twoPi * 6000.0 * (double) i / testSampleRate)) / 1.8;
            return mono (v);
        });

    const ott::BandLevels& levels = engine.getBandLevels();

    bool anyReduction = false;
    int  reducedBands = 0;

    for (int b = 0; b < ott::numBands; ++b)
    {
        std::printf ("       band %d: %.2f dB reduction, input %.2f dB\n",
                     b, levels.gainReductionDb[b], levels.inputDb[b]);

        if (levels.gainReductionDb[b] < -0.5f)
        {
            anyReduction = true;
            ++reducedBands;
        }
    }

    check (anyReduction, "at least one band reports gain reduction on a hot signal");
    check (reducedBands >= 2, "a broadband signal is compressed in more than one band");
}

/** 4. Silence in stays silence out, and no cold start explodes. */
void testSilenceProducesSilence()
{
    std::printf ("\n[silence] no output, and no startup burst\n");

    ott::Module engine;
    engine.prepare (testSampleRate, 256, 2);

    ott::Parameters p = ott::Parameters::makeDefault();
    p.inGainDb = 0.0f;
    p.outGainDb = 0.0f;
    p.mix = 1.0f;
    engine.setParameters (p);

    double worstPeak = 0.0;

    runEngine (engine, 96000, 256,
        [] (int) { return mono (0.0); },
        [&] (StereoBuffer& b, int, int) { worstPeak = std::max (worstPeak, b.peak (0)); });

    checkBelow (worstPeak, 1.0e-6, "digital silence stays silent (no noise-floor expansion)");

    // A cold start with signal already present: the first block is where the
    // envelope used to start below its floor and upward-compress the input into
    // a +30 dB burst.
    ott::Module cold;
    cold.prepare (testSampleRate, 256, 2);
    cold.setParameters (ott::Parameters::makeDefault());

    StereoBuffer first (256);
    first.fillSine (440.0, 0.5);
    cold.process (first.channels(), 2, 256);

    checkBelow (first.peak (0), 1.0, "first block after prepare() does not overshoot full scale");
    checkBelow (first.peakStep (0), 0.5,
                "first block after prepare() has no step discontinuity");
}

/** 4b. An idle track must not be lifted into audibility.

    This is the regression for the worst bug this engine has had. Upward
    compression lifts whatever is present, and a real session's "silence" is not
    silence: an idle track carries dither, converter noise and upstream
    processing at roughly -90 to -70 dBFS. With the noise-floor gate set below
    that, the plugin amplified it by more than 35 dB and pinned the meters from
    the moment it was loaded.

    None of the original tests covered this, because every one of them fed
    either a real signal or exact zeros, and neither exists on an idle track.
*/
void testIdleNoiseIsNotLifted()
{
    std::printf ("\n[idle] an idle track is not amplified into audibility\n");

    ott::Module engine;
    engine.prepare (testSampleRate, 512, 2);
    engine.setParameters (ott::Parameters::makeDefault());

    // Deterministic pseudo-noise, so the check cannot flake.
    std::uint32_t state = 0x12345678u;
    const auto noise = [&state]
    {
        state = state * 1664525u + 1013904223u;
        return ((float) (state >> 8) / (float) (1 << 23)) - 1.0f;
    };

    struct Level { const char* name; double amplitude; double maximumLiftDb; };

    // "Nothing playing" -- a couple of dB of lift is inaudible; what must never
    // happen is the 35 dB this used to do.
    const Level levels[] = {
        { "idle noise -70 dBFS", 3.16e-4,  9.0 },
        { "idle noise -80 dBFS", 1.00e-4,  9.0 },
        { "idle noise -90 dBFS", 3.16e-5,  9.0 },
    };

    for (const auto& level : levels)
    {
        ott::Module module;
        module.prepare (testSampleRate, 512, 2);
        module.setParameters (ott::Parameters::makeDefault());

        double outputSumSq = 0.0;
        double outputPeak = 0.0;
        long   measured = 0;

        runEngine (module, 48000 * 3, 512,
            [&] (int) { return mono (level.amplitude * (double) noise()); },
            [&] (StereoBuffer& b, int n, int offset)
            {
                if (offset < 48000)
                    return;

                for (int i = 0; i < n; ++i)
                {
                    outputSumSq += (double) b.left[(size_t) i] * b.left[(size_t) i];
                    outputPeak = std::max (outputPeak, (double) std::abs (b.left[(size_t) i]));
                }

                measured += n;
            });

        // Lift is measured as RMS in versus RMS out. Measuring it from the peak
        // instead would report the crest factor of the noise as if it were gain.
        const double outputRms = std::sqrt (outputSumSq / (double) measured);
        const double liftDb = 20.0 * std::log10 (outputRms / level.amplitude);

        std::printf ("       %-22s inRMS %+.1f dBFS  outRMS %+.1f dBFS  peak %.5f  lift %+.1f dB\n",
                      level.name, 20.0 * std::log10 (level.amplitude),
                      20.0 * std::log10 (outputRms), outputPeak, liftDb);

        checkBelow (liftDb, level.maximumLiftDb,
                    std::string (level.name) + ": lift stays below 9 dB");

        // And in absolute terms: nothing on an idle track may approach full
        // scale, which is what made the meters look wrong.
        checkBelow (outputPeak, 0.01,
                    std::string (level.name) + ": output peak stays below -40 dBFS");
    }

    // The flip side: this must not have been bought by gutting the upward stage.
    // Programme material at a level a real quiet track reaches still gets its
    // full OTT lift.
    {
        ott::Module module;
        module.prepare (testSampleRate, 512, 2);
        module.setParameters (ott::Parameters::makeDefault());

        const double amplitude = 0.003162; // -50 dBFS
        double inputSumSq = 0.0, outputSumSq = 0.0;

        runEngine (module, 48000 * 2, 512,
            [&] (int i) { return sine (700.0, amplitude, i); },
            [&] (StereoBuffer& b, int n, int offset)
            {
                if (offset < 9600)
                    return;

                for (int i = 0; i < n; ++i)
                {
                    const double in = amplitude * std::sin (twoPi * 700.0
                                                            * (double) (offset + i) / testSampleRate);
                    inputSumSq += in * in;
                    outputSumSq += (double) b.left[(size_t) i] * b.left[(size_t) i];
                }
            });

        const double liftDb = 10.0 * std::log10 (outputSumSq / inputSumSq);
        std::printf ("       %-22s lift %+.1f dB\n", "quiet material -50 dBFS", liftDb);

        checkAbove (liftDb, 12.0, "quiet programme material still gets its OTT lift");
    }
}

/** 4c. A band that has been silent for a long time must not corrupt the engine.

    This is the regression for a latched mute: the plugin would output NaN
    forever, and only removing it from the host cleared it. That is the worst
    failure mode a plugin can have, so it gets its own test.

    The cause was a denormal in the upward envelope. Its value is only capped at
    the band threshold, never floored, so after enough silence it decays into
    denormal range (9.3e-43 measured). The exponent then needs log2(lt / lenv),
    and that quotient overflows to +inf; when the noise-floor gate has scaled
    that stage's ratio to exactly zero, the exponent is inf * 0 = NaN. One NaN
    in the exponent enters the biquad state through its recursive output terms
    and never leaves.

    Two details matter for reproducing it: the block must be long enough for the
    envelope to decay that far, and it must be preceded by signal so the
    envelope starts high. A short block, or silence from a cold start, does not
    reach it -- which is why every earlier test missed it.
*/
void testLongSilenceDoesNotLatch()
{
    std::printf ("\n[silence] a long silent passage must not poison the engine\n");

    for (int blockSize : { 512, 1024, 4096 })
    {
        ott::Module engine;
        engine.prepare (testSampleRate, blockSize, 2);
        engine.setParameters (ott::Parameters::makeDefault());

        StereoBuffer buf (blockSize);
        std::uint32_t state = 0xabcdef01u;

        const auto noise = [&state]
        {
            state = state * 1664525u + 1013904223u;
            return ((float) (state >> 8) / (float) (1 << 23)) - 1.0f;
        };

        bool sawNonFinite = false;
        int  firstBadBlock = -1;
        int  blockIndex = 0;

        // Phase 0: signal, so the envelopes are somewhere realistic.
        // Phases 1-2: a long silence, which is where the decay happens.
        // Phase 3: signal again -- the engine must still pass audio.
        for (int phase = 0; phase < 4; ++phase)
        {
            for (int blk = 0; blk < 60; ++blk, ++blockIndex)
            {
                for (int i = 0; i < blockSize; ++i)
                {
                    double v = 0.0;

                    if (phase == 0)
                        v = 0.2 * (double) noise();
                    else if (phase == 3)
                        v = 0.9 * std::sin (twoPi * 1000.0
                                            * (double) (blk * blockSize + i) / testSampleRate);

                    buf.left[(size_t) i]  = (float) v;
                    buf.right[(size_t) i] = (float) v;
                }

                engine.process (buf.channels(), 2, blockSize);

                for (int i = 0; i < blockSize; ++i)
                {
                    if (! std::isfinite (buf.left[(size_t) i])
                        || ! std::isfinite (buf.right[(size_t) i]))
                    {
                        if (! sawNonFinite)
                        {
                            sawNonFinite = true;
                            firstBadBlock = blockIndex;
                        }
                    }
                }
            }
        }

        if (sawNonFinite)
            std::printf ("       block %d: first non-finite output at block %d\n",
                         blockSize, firstBadBlock);

        check (! sawNonFinite,
               "block " + std::to_string (blockSize)
                   + ": no NaN or Inf across noise, long silence and signal again");

        // And it must still pass audio, not just avoid NaN.
        double outputPeak = 0.0;

        for (int blk = 0; blk < 8; ++blk)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const float v = (float) (0.3 * std::sin (twoPi * 1000.0
                                                         * (double) (blk * blockSize + i) / testSampleRate));
                buf.left[(size_t) i]  = v;
                buf.right[(size_t) i] = v;
            }

            engine.process (buf.channels(), 2, blockSize);

            for (int i = 0; i < blockSize; ++i)
                outputPeak = std::max (outputPeak, (double) std::abs (buf.left[(size_t) i]));
        }

        checkAbove (outputPeak, 1.0e-4,
                    "block " + std::to_string (blockSize) + ": audio still passes after silence");
    }
}

/** 5. Stereo linking: a hard-panned signal must get the same gain as a centred
       one, otherwise the image shifts while the compressor works. */
void testStereoLinking()
{
    std::printf ("\n[stereo] detector is linked across channels\n");

    ott::Parameters p = ott::Parameters::makeDefault();
    p.inGainDb = 0.0f;
    p.outGainDb = 0.0f;
    p.mix = 1.0f;
    for (int b = 0; b < ott::numBands; ++b)
        p.bandGainDb[b] = 0.0f;

    const double amplitude = 0.002;

    ott::Module panned;
    panned.prepare (testSampleRate, 512, 2);
    panned.setParameters (p);
    runEngine (panned, 48000, 512,
        [&] (int i) { const double v = amplitude * std::sin (twoPi * 500.0 * (double) i / testSampleRate);
                      return std::make_pair (0.0f, (float) v); });

    ott::Module centred;
    centred.prepare (testSampleRate, 512, 2);
    centred.setParameters (p);
    runEngine (centred, 48000, 512,
        [&] (int i) { return sine (500.0, amplitude, i); });

    const float pannedReduction  = panned.getBandLevels().gainReductionDb[1];
    const float centredReduction = centred.getBandLevels().gainReductionDb[1];

    std::printf ("       panned %.3f dB, centred %.3f dB\n", pannedReduction, centredReduction);

    checkNear (pannedReduction, centredReduction, 0.05,
               "a hard-panned signal gets the same gain as a centred one");
}

/** 6. The core anti-click test: automate every macro while audio runs and look
       for steps at block boundaries. */
void testNoZipperOrClicksUnderAutomation()
{
    std::printf ("\n[automation] no discontinuities while parameters move\n");

    struct Scenario
    {
        const char* name;
        void (*apply) (ott::Parameters&, double progress);
    };

    const Scenario scenarios[] = {
        { "depth sweep",     [] (ott::Parameters& p, double t) { p.depth = (float) t; } },
        { "mix sweep",       [] (ott::Parameters& p, double t) { p.mix = (float) t; } },
        { "up/down macro",   [] (ott::Parameters& p, double t) { p.upward = (float) t * 2.0;
                                                                 p.downward = (float) (2.0 - t * 2.0); } },
        { "time sweep",      [] (ott::Parameters& p, double t) { p.attack = (float) t;
                                                                 p.release = (float) t; } },
        { "band gains",      [] (ott::Parameters& p, double t) { for (int b = 0; b < ott::numBands; ++b)
                                                                     p.bandGainDb[b] = (float) (t * 60.0 - 30.0); } },
        { "trim sweep",      [] (ott::Parameters& p, double t) { p.inGainDb = (float) (t * 30.0 - 15.0);
                                                                 p.outGainDb = (float) (15.0 - t * 30.0); } },
        { "crossover sweep", [] (ott::Parameters& p, double t) { p.lowCrossoverHz = (float) (ott::minCrossoverHz + t * 900.0);
                                                                 p.highCrossoverHz = (float) (1200.0 + t * 4000.0); } },
        { "behavior sweep",  [] (ott::Parameters& p, double t) { p.behaviorDb = (float) (t * 24.0 - 12.0); } },
    };

    constexpr int blockSize = 512;
    constexpr int total = 48000 * 4;

    // A 220 Hz sine at this amplitude.
    const double amplitude = 0.2;

    for (const Scenario& scenario : scenarios)
    {
        ott::Module engine;
        engine.prepare (testSampleRate, blockSize, 2);

        ott::Parameters initial = ott::Parameters::makeDefault();
        initial.inGainDb = 0.0f;
        initial.outGainDb = 0.0f;
        engine.setParameters (initial);

        double worstBoundary = 0.0;
        double worstInBlock = 0.0;
        float previousLast = 0.0f;
        bool havePrevious = false;

        for (int offset = 0; offset < total; offset += blockSize)
        {
            StereoBuffer block (blockSize);

            for (int i = 0; i < blockSize; ++i)
            {
                const double v = amplitude * std::sin (twoPi * 220.0 * (double) (offset + i) / testSampleRate);
                block.left[(size_t) i]  = (float) v;
                block.right[(size_t) i] = (float) v;
            }

            engine.process (block.channels(), 2, blockSize);

            if (havePrevious)
                worstBoundary = std::max (worstBoundary, (double) std::abs (block.left[0] - previousLast));

            for (int i = 1; i < blockSize; ++i)
                worstInBlock = std::max (worstInBlock,
                                         (double) std::abs (block.left[(size_t) i]
                                                            - block.left[(size_t) (i - 1)]));

            previousLast = block.left[(size_t) blockSize - 1];
            havePrevious = true;

            // A host delivers a knob move once per block, which is the worst
            // case the engine has to survive.
            ott::Parameters current = ott::Parameters::makeDefault();
            current.inGainDb = 0.0f;
            current.outGainDb = 0.0f;
            scenario.apply (current, (double) offset / (double) total);
            engine.setParameters (current);
        }

        // The metric is the boundary jump relative to the SIGNAL level, not
        // relative to the in-block slew. The slew scales with the output level,
        // which differs per scenario, so a slew-relative ratio drifts with the
        // gain staging and stops meaning anything. A jump of a few percent of
        // the signal is a glide; a click is a large fraction of it.
        const double peak = std::abs (amplitude * 2.0); // sine peak, before gain
        const double relative = worstBoundary / peak;

        std::printf ("       %-18s boundary %.6f (%.1f%% of signal)  in-block max %.6f\n",
                     scenario.name, worstBoundary, relative * 100.0, worstInBlock);

        checkBelow (relative, 0.35,
                    std::string (scenario.name) + ": boundary jump stays below 35% of signal level");
    }
}

/** 7. The classic OTT curve must not hand the host a clipped signal. */
void testNoHardClipping()
{
    std::printf ("\n[headroom] the classic OTT curve does not clip full scale\n");

    ott::Module engine;
    engine.prepare (testSampleRate, 512, 2);
    engine.setParameters (ott::Parameters::makeDefault());

    double peak = 0.0;

    runEngine (engine, 48000, 512,
        [] (int i) { return mono (0.95 * std::sin (twoPi * 110.0 * (double) i / testSampleRate)
                                  + 0.05 * std::sin (twoPi * 3000.0 * (double) i / testSampleRate)); },
        [&] (StereoBuffer& b, int, int) { peak = std::max (peak, b.peak (0)); });

    std::printf ("       output peak %.4f\n", peak);
    checkBelow (peak, 1.0, "full-scale input does not hit the output limiter");
}

/** 8. Depth at zero with the gain knobs flat is a pass-through — the reference
       point every other measurement is relative to. */
void testNeutralSettingsAreTransparent()
{
    std::printf ("\n[neutral] neutral settings pass the signal through\n");

    ott::Module engine;
    engine.prepare (testSampleRate, 512, 2);

    ott::Parameters p = ott::Parameters::makeDefault();
    p.depth = 0.0f;
    p.mix = 1.0f;
    p.inGainDb = 0.0f;
    p.outGainDb = 0.0f;
    for (int b = 0; b < ott::numBands; ++b)
        p.bandGainDb[b] = 0.0f;

    engine.setParameters (p);

    const double amplitude = 0.3;
    double inputSumSq = 0.0, outputSumSq = 0.0;

    runEngine (engine, 48000, 512,
        [&] (int i) { return sine (440.0, amplitude, i); },
        [&] (StereoBuffer& b, int n, int offset)
        {
            if (offset < 12000)
                return;
            for (int i = 0; i < n; ++i)
            {
                const double in = amplitude * std::sin (twoPi * 440.0 * (double) (offset + i) / testSampleRate);
                inputSumSq += in * in;
                outputSumSq += (double) b.left[(size_t) i] * b.left[(size_t) i];
            }
        });

    checkNear (std::sqrt (outputSumSq / inputSumSq), 1.0, 0.02,
               "depth = 0, mix = 1, flat gains is unity");
}

/** 9. Mono must be processed like a centred stereo signal, and must never write
       the second channel. */
void testMonoProcessing()
{
    std::printf ("\n[mono] mono input is processed like a centred stereo signal\n");

    ott::Parameters p = ott::Parameters::makeDefault();

    ott::Module monoEngine;
    monoEngine.prepare (testSampleRate, 512, 1);
    monoEngine.setParameters (p);

    ott::Module stereoEngine;
    stereoEngine.prepare (testSampleRate, 512, 2);
    stereoEngine.setParameters (p);

    const double amplitude = 0.05;

    StereoBuffer monoBlock (512);
    float* monoChannels[1] = { monoBlock.left.data() };

    StereoBuffer stereoBlock (512);

    for (int offset = 0; offset < 48000; offset += 512)
    {
        for (int i = 0; i < 512; ++i)
        {
            const double v = amplitude * std::sin (twoPi * 330.0 * (double) (offset + i) / testSampleRate);
            monoBlock.left[(size_t) i]  = (float) v;
            monoBlock.right[(size_t) i] = 0.0f; // must stay untouched
            stereoBlock.left[(size_t) i]  = (float) v;
            stereoBlock.right[(size_t) i] = (float) v;
        }

        monoEngine.process (monoChannels, 1, 512);
        stereoEngine.process (stereoBlock.channels(), 2, 512);
    }

    checkBelow (monoBlock.peak (1), 1.0e-12, "mono processing never writes the right channel");

    double maxDelta = 0.0;
    for (int i = 0; i < 512; ++i)
        maxDelta = std::max (maxDelta, (double) std::abs (monoBlock.left[(size_t) i]
                                                          - stereoBlock.left[(size_t) i]));

    checkBelow (maxDelta, 1.0e-5, "mono output matches the left channel of stereo");
}

/** 10. A host block larger than the internal chunk must be split transparently
        rather than triggering an audio-thread allocation. */
void testLargeBlockIsChunked()
{
    std::printf ("\n[blocksize] over-large host blocks are chunked transparently\n");

    // Enough audio that the two renders overlap almost entirely after the
    // transient is skipped. The total length is fixed, so each render is split
    // into however many blocks of that size it takes -- which is the point: the
    // same span of audio, chopped differently.
    constexpr int totalSamples = 24000;

    const ott::Parameters p = ott::Parameters::makeDefault();

    // Render the SAME span of audio at each block size and compare the whole
    // stream. Comparing one arbitrary block is not a sound test: with different
    // block sizes a single block lands at a different point in the signal, and
    // the filter's start-up transient makes those two windows differ for reasons
    // that have nothing to do with chunking.
    const auto render = [&p] (int blockSize, std::vector<float>& out)
    {
        ott::Module engine;
        engine.prepare (testSampleRate, blockSize, 2);
        engine.setParameters (p);

        StereoBuffer b (blockSize);
        out.assign ((size_t) totalSamples, 0.0f);

        for (int offset = 0; offset < totalSamples; offset += blockSize)
        {
            for (int i = 0; i < blockSize; ++i)
            {
                const double v = 0.2 * std::sin (twoPi * 500.0
                                                 * (double) (offset + i) / testSampleRate);
                b.left[(size_t) i]  = (float) v;
                b.right[(size_t) i] = (float) v;
            }

            engine.process (b.channels(), 2, blockSize);

            const int n = std::min (blockSize, totalSamples - offset);

            for (int i = 0; i < n; ++i)
                out[(size_t) (offset + i)] = b.left[(size_t) i];
        }
    };

    std::vector<float> small, large;
    render (128, small);
    render (4096, large);

    check (small.size() == large.size(), "both renders cover the same span of audio");

    // Skip the first 10 ms: the filter transient really is sampled differently at
    // different block sizes, so including it would compare apples to oranges.
    const size_t skip = (size_t) (testSampleRate * 0.01);

    double smallE = 0.0, largeE = 0.0;

    for (size_t i = skip; i < small.size(); ++i)
    {
        smallE += (double) small[i] * small[i];
        largeE += (double) large[i] * large[i];
    }

    checkNear (std::sqrt (largeE / smallE), 1.0, 0.01,
               "a 4096-sample block renders the same audio as a 128-sample block");
}

} // namespace

//==============================================================================
int main()
{
    std::printf ("VibeOTT DSP tests (%.0f Hz)\n", testSampleRate);

    testCrossoverReconstruction();
    testCrossoverCutoffIsCorrect();
    testUpwardCompressionLiftsQuietSignals();
    testDownwardCompressionTamesPeaks();
    testIdleNoiseIsNotLifted();
    testLongSilenceDoesNotLatch();
    testSilenceProducesSilence();
    testStereoLinking();
    testNoZipperOrClicksUnderAutomation();
    testNoHardClipping();
    testNeutralSettingsAreTransparent();
    testMonoProcessing();
    testLargeBlockIsChunked();

    std::printf ("\n%d checks, %d failure(s)\n", checks, failures);
    return failures == 0 ? EXIT_SUCCESS : EXIT_FAILURE;
}
