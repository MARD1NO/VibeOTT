# VibeOTT - JUCE Multiband Compressor Plugin

A VST3 + Standalone three-band upward/downward multiband compressor (the
"Ableton OTT" sound) built on JUCE.

The DSP is a port of the OTT compressor from Vital (`vital_dsp/compressor.cpp`),
cross-checked against the scalar-C port in
[schwung-ottx](https://github.com/legsmechanical/schwung-ottx). The previous
reverse-engineered core in `Source/MultibandCompressor.h` is gone.

## Project Structure

```
VibeOTT/
├── CMakeLists.txt                    # VibeOTTEngine + plugin + test targets
├── .github/workflows/build.yml       # CI: DSP tests, then Windows/macOS/Linux VST3
├── JUCE/                             # JUCE framework + VST3 SDK (git submodule)
└── Source/
    ├── dsp/
    │   ├── OttConfig.h               # tuning constants (bands, timing, limiter)
    │   ├── OttFilters.h              # Linkwitz-Riley crossover + RMS compressor
    │   └── OttModule.h/.cpp          # the engine: routing, smoothing, metering
    ├── tests/
    │   ├── OttTests.cpp              # offline DSP regression tests (no JUCE)
    │   ├── EditorSnapshot.cpp        # headless layout + host-contract checks
    │   └── PluginLoadTest.cpp        # loads the built VST3 as a host would
    ├── PluginProcessor.h/.cpp        # AudioProcessor, APVTS, legacy state migration
    └── PluginEditor.h/.cpp           # GUI: OTT look, knobs, band meters with dB readout
```

`Source/dsp/` is deliberately JUCE-free so the test harness can link it directly,
with no audio device or host involved. Anything the tests cannot reach is a bug
waiting to happen.

## Build

```bash
git clone --recurse-submodules https://github.com/MARD1NO/VibeOTT.git
cd VibeOTT
cmake -B build
cmake --build build --config Release
```

- **VST3**: `build/VibeOTT_artefacts/Release/VST3/VibeOTT.vst3` (all platforms)
- **Standalone**: `build/VibeOTT_artefacts/Release/Standalone/`

`--recurse-submodules` matters: JUCE and the VST3 SDK are submodules, and without
them CMake now fails immediately with the command to fix it rather than failing
deep inside the plugin build.

## Test

```bash
cmake --build build --config Release --target VibeOTTTests
ctest --test-dir build --output-on-failure
```

`Source/tests/OttTests.cpp` runs in under a second and is the regression net for
every bug this engine was rewritten to fix: silence explosions, startup clicks,
zipper noise under automation, hard clipping, stereo image collapse, and
per-channel detector drift. The README has the full symptom → cause → fix table.

The built VST3 is verified by **actually loading it** through JUCE's own VST3
host implementation (discover, instantiate, process audio, serialise state):

```bash
cmake -B build -DVIBEOTT_BUILD_LOAD_TEST=ON
cmake --build build --config Release --target VibeOTTPluginLoadTest
./build/VibeOTTPluginLoadTest_artefacts/Release/VibeOTTPluginLoadTest <bundle-path>
```

CI runs exactly this on Windows, macOS and Linux. It replaced an earlier check
that grepped `GetPluginFactory` out of `nm`/`dumpbin` output: the exported name
differs per object format, the available tools differ per platform, and that step
broke on Windows and macOS while passing on Linux. Anything that cannot be
verified on the platform it runs on should not be a check.

Editor layout and the plugin's host contract (parameter registration, bus
handling, mono/stereo, one-sample and over-large blocks, state round trip) are
checked by the snapshot tool:

```bash
cmake -B build -DVIBEOTT_BUILD_SNAPSHOTS=ON
cmake --build build --config Release --target VibeOTTSnapshot
./build/VibeOTTSnapshot_artefacts/Release/VibeOTTSnapshot ui-snapshots
```

It writes `editor-collapsed.png`, `editor-expanded.png`, `editor-meters.png` and
`editor-meters-closeup.png`, and exits non-zero if any control overlaps, escapes
its panel, or renders a value in the wrong units. The meter snapshot drives the
readouts with fixed values so the digits are rendered rather than all reading
zero. CI runs it under Xvfb on Linux and uploads the images.

## Plugin Parameters

All use the APVTS and are saved with the session. IDs live in `ParameterIDs`
(`Source/PluginProcessor.h`) and are the plugin's public ABI — do not rename them
once shipped.

| Parameter | ID | Range | Default |
|-----------|-----|-------|---------|
| Mix | `MIX` | 0–1 (wet amount) | 1.0 |
| Depth | `DEPTH` | 0–1 | 1.0 |
| Upward | `UPWARD` | 0–2 | 1.0 |
| Downward | `DOWNWARD` | 0–2 | 1.0 |
| Time | `TIME` | 0–1 | 0.5 |
| In Gain | `INPUT_GAIN` | −30…+30 dB | 0 dB |
| Out Gain | `OUTPUT_GAIN` | −30…+30 dB | 0 dB |
| Behavior | `BEHAVIOR` | −12…+12 dB | 0 dB |
| Lo / Mid | `LOW_CROSSOVER` | 30 Hz–18 kHz (log) | 120 Hz |
| Mid / Hi | `HIGH_CROSSOVER` | 30 Hz–18 kHz (log) | 2.5 kHz |
| `LOW`/`MID`/`HIGH` + `_UP_THRESHOLD` | | −80…0 dB | −35 / −36 / −35 |
| `_UP_RATIO` | | −1…1 | 0.80 |
| `_DOWN_THRESHOLD` | | −80…0 dB | −28 / −25 / −30 |
| `_DOWN_RATIO` | | 0…1 | 0.90 / 0.857 / 1.00 |
| `_GAIN` | | −30…+30 dB | +13.3 / +8.7 / +13.3 |

`migrateLegacyState` in `PluginProcessor.cpp` translates sessions saved by the
pre-rewrite plugin (which used a 7-parameter 0–1 set) onto these IDs, so old
projects keep their Depth, Up/Down, Mix and per-band gains.

`TIME` drives both the attack and the release envelope coefficient. The engine
keeps them as two fields, so exposing them independently is a small change if it
is ever wanted — but there is deliberately only one registered control, because
two controls writing the same value is how they end up disagreeing.

## DSP Architecture

```
input gain
  → 4th-order Linkwitz-Riley split at the low crossover
  → the high leg is split again at the high crossover into mid / high
  → per-band dual-envelope RMS compressor (detector linked across channels)
  → per-band makeup gain
  → dry/wet mix against the phase-compensated crossover sum
  → output gain → soft limiter
```

Key properties, all asserted by tests:

- **Zero latency.** No look-ahead, so nothing to report to the host.
- **Allocation-free audio path.** The engine bounds its own scratch buffers and
  chunks any host block larger than `maxChunkSamples`.
- **5 Hz one-pole smoothing** on every parameter, advanced once per block, plus
  per-sample ramps for the makeup gain, dry/wet mix and input/output trims. The
  compressor's *exponent* — where Depth, the Up/Down macros and the per-band
  ratios all land — is interpolated across the block, starting exactly where the
  previous block ended. Letting it step is what caused the old zipper noise.
- **Stereo-linked detection** via the loudest channel, so the image never shifts.
- **Noise-floor gate** at −76 dBFS. Upward compression lifts whatever is
  present, and an idle DAW track is not silent: it carries −90 to −70 dBFS of
  dither and converter noise. A gate below that amplifies idle noise into
  audibility and pins the meters. `Behavior` moves the window; at −12 dB the
  gate is disabled entirely (stock vitOTTx behaviour). `testIdleNoiseIsNotLifted`
  covers this.
- **Envelope power is floored** at 1e-20 before the `log2(threshold / envelope)`
  in the compressor's exponent. Without it, a band that has been silent long
  enough decays into denormal range, the quotient overflows to +inf, and if the
  noise-floor gate has zeroed that stage's ratio the exponent is `inf * 0` =
  NaN — which then poisons the biquad state permanently. `testLongSilenceDoesNotLatch`.
- **Hysteresis** on the band-collapse decision, so sweeping a crossover across
  the threshold cannot flip the topology on alternate blocks.

### Crossover corners

`omega_0 = 2*pi*fc/fs`. The factor of 2 is easy to drop and impossible to notice
in the allpass test, because a uniformly shifted crossover is still a flat
allpass — the reconstruction stays perfect while every corner sits an octave low.
That made the plugin several dB louder than upstream with a 6 dB low-band lift.
`testCrossoverCutoffIsCorrect` pins the corner against theory (−6 dB at the
corner, −24 dB one octave up) so it cannot regress.

### Divergence from the reference port

`schwung-ottx` derives its Linkwitz-Riley high-pass with the same denominator
sign as the low-pass. That silently drops the high-pass to Q = 0.5 instead of
0.707, so the three bands do **not** sum to an allpass and there is a ~3 dB dip at
each crossover. This implementation uses the correct Butterworth Q for both
outputs; `testCrossoverReconstruction` asserts the flat magnitude.

**Upstream has no noise-floor handling at all.** vitOTT/vitOTTx's
`compressor.cpp` never mentions noise, floors or gates; the only limit on the
upward stage is `clamp(upper * lower, 0, 32)`. The gate is an addition by this
plugin and by the Schwung OTTx port. Measured through unmodified upstream, an
idle track at −70 dBFS is lifted by **+38 dB** (to −36 dBFS) — audible, and it
pins the meters. That is the bug the gate prevents, and it is why matching the
reference is not an option for a plugin in a DAW.

The Move port puts its floor at the 16-bit LSB (−90.3 dB) because Move audio
really does stall at ±1 LSB. A 32-bit float host is the opposite situation: its
residue sits *higher*, so the floor here is *higher* too (−76 dBFS, fade window
−70 to −58 dB). A band at −75 dBFS is then not lifted at all, while everything
from −58 dBFS up stays bit-identical to upstream.

The per-band makeup gains are 3 dB below upstream's because upstream's numbers
assume an int16 pipeline and peak at 0 dBFS RMS on ordinary material.

## CI

`.github/workflows/build.yml`:

1. **DSP tests** on Linux — the gate for everything else.
2. **Editor + wrapper checks** headless under Xvfb, with snapshots uploaded.
3. **VST3 builds** on Windows, macOS and Linux. Each one builds `VibeOTT_VST3`,
   then builds and runs `VibeOTTPluginLoadTest` against the bundle it just
   produced, and only then publishes it as an artefact. A bundle that a host
   cannot open fails the build rather than shipping.

## Known Limitations

- The engine runs at the host sample rate. OTT's character comes mostly from the
  multiband upward compression, but internal oversampling would be a worthwhile
  addition and is not implemented.
- The `Time` macro scales the nominal envelope times by `2^(8t − 4)`, i.e. 1/16×
  to 16×, matching Vital. The per-band base times (2.8/40, 1.4/28, 0.7/15 ms for
  low/mid/high) are constants in `OttConfig.h`, not parameters.
