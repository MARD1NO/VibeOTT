# VibeOTT

A VST3 + Standalone three-band **upward/downward multiband compressor** — the
"Ableton OTT" sound — built on JUCE.

The DSP is a port of the OTT compressor from
[Vital](https://github.com/mtytel/vital) (`vital_dsp/compressor.cpp`), cross-checked
against the scalar-C port in
[schwung-ottx](https://github.com/legsmechanical/schwung-ottx). Upward compression
lifts quiet detail toward each band's lower threshold; downward compression tames
peaks above the upper threshold. Together they give the dense, forward character
OTT is known for.

## Build

```bash
git clone --recurse-submodules https://github.com/MARD1NO/VibeOTT.git
cd VibeOTT
cmake -B build
cmake --build build --config Release
```

`--recurse-submodules` matters: JUCE and the VST3 SDK live in submodules, and a
plain clone configures fine and then fails deep inside the plugin build. CMake now
checks for the SDK up front and tells you to run
`git submodule update --init --recursive` if it is missing.

Artefacts:

| Platform | VST3 |
|----------|------|
| Windows  | `build/VibeOTT_artefacts/Release/VST3/VibeOTT.vst3` |
| macOS    | `build/VibeOTT_artefacts/Release/VST3/VibeOTT.vst3` |
| Linux    | `build/VibeOTT_artefacts/Release/VST3/VibeOTT.vst3` |

A Standalone build lands in `build/VibeOTT_artefacts/Release/Standalone/`.

## Tests

```bash
cmake --build build --config Release --target VibeOTTTests
ctest --test-dir build --output-on-failure
```

`Source/tests/OttTests.cpp` links the DSP engine directly — no JUCE, no audio
device, no host — and runs in well under a second. It is the regression net for
the failures this engine was rewritten to fix:

- the three-band split reconstructs the input magnitude (allpass sum),
- quiet signals are lifted, hot signals are compressed,
- **digital silence stays silent** — no noise-floor explosion, no startup burst,
- the detector is **stereo-linked**, so a hard-panned signal is not pulled
  differently from a centred one,
- **no clicks or zipper noise** while any parameter is automated (each macro is
  swept and the jump at every block boundary is compared against the waveform's
  own slew),
- the classic curve does not clip full scale,
- mono is processed like centred stereo and never writes the second channel,
- a 4096-sample host block matches a 128-sample one (chunking is transparent).

### Plugin load test

```bash
cmake -B build -DVIBEOTT_BUILD_LOAD_TEST=ON
cmake --build build --config Release --target VibeOTTPluginLoadTest
./build/VibeOTTPluginLoadTest_artefacts/Release/VibeOTTPluginLoadTest \
    build/VibeOTT_artefacts/Release/VST3/VibeOTT.vst3
```

Opens the built VST3 through JUCE's own VST3 host implementation, confirms it
reports a plugin, instantiates it, pushes audio through it and round-trips its
state. This is what CI uses to verify the shipped artefact on all three
platforms, because it proves the thing that actually matters — that a host can
open the file and get a working `AudioProcessor` — with one command and no
platform-specific tooling.

### Editor snapshots

```bash
cmake -B build -DVIBEOTT_BUILD_SNAPSHOTS=ON
cmake --build build --config Release --target VibeOTTSnapshot
./build/VibeOTTSnapshot_artefacts/Release/VibeOTTSnapshot ui-snapshots
```

Renders the editor to PNG without a host, an audio device or a window server, and
asserts the layout invariants the previous UI violated: no two controls may
overlap, no child may escape its panel, the knob readouts must show the
parameter's units, and both the collapsed and expanded panels must fit.

It also writes `editor-meters.png` and `editor-meters-closeup.png` with the three
band meters driven by fixed values — one compressing, one lifting, one idle — so
the digits are actually rendered and inspectable. A screenshot of three meters
all reading `0.0` would prove nothing.

## Parameters

Everything is automatable and saved with the session.

### Macros

Depth is the largest control in the middle of the window — it is drawn as a
filled ring to mark it out as the master "amount" — with Up / Down / Mix / Time
beside it. Every control is a standard rotary: drag vertically, double-click to
reset to the default, or right-click for a text entry box.

| Parameter | Range | Default | Description |
|-----------|-------|---------|-------------|
| Mix        | 0–100%   | 100% | Dry/wet. At 0% the dry path is the crossover reconstruction (allpass, so energy-preserving). |
| Depth      | 0–100%   | 100% | Scales **every** band's ratios at once — the master "amount". |
| Upward     | 0–2×     | 1.0× | Multiplies every band's upward ratio. |
| Downward   | 0–2×     | 1.0× | Multiplies every band's downward ratio. |
| Time       | 0–100%   | 50%  | Writes Attack and Release together (higher = slower). |
| In Gain    | −30…+30 dB | 0 dB | Input trim. |
| Out Gain   | −30…+30 dB | 0 dB | Output trim. |
| Behavior   | −12…+12 dB | 0 dB | Moves the upward-expansion noise floor. At −12 dB the gate is off entirely (stock vitOTTx behaviour). |
| Lo / Mid   | 30 Hz–18 kHz | 120 Hz | Low ↔ mid crossover. |
| Mid / Hi   | 30 Hz–18 kHz | 2.5 kHz | Mid ↔ high crossover. |

### Per-band detail (ADVANCED)

Each band has an upward threshold/ratio pair, a downward threshold/ratio pair and
a makeup gain. The defaults are the classic OTT curve:

| Parameter | Low | Mid | High |
|-----------|-----|-----|------|
| Up Thr    | −35 dB | −36 dB | −35 dB |
| Up Ratio  | 0.80 | 0.80 | 0.80 |
| Dn Thr    | −28 dB | −25 dB | −30 dB |
| Dn Ratio  | 0.90 | 0.857 | 1.00 |
| Gain      | +13.3 dB | +8.7 dB | +13.3 dB |

Ratios are "amount" controls in 0–1 (downward) / −1…1 (upward) that the
compressor halves internally, so 0.90 downward is a 1.818:1 ratio and 0.80 upward
is a 1.667:1 expansion. A negative upward ratio flips that stage into downward
*expansion*.

## Signal flow

```
input gain
  → 4th-order Linkwitz-Riley split at the low crossover
  → the high leg is split again at the high crossover into mid / high
  → per-band dual-envelope RMS compressor (detector linked across channels)
  → per-band makeup gain
  → dry/wet mix against the phase-compensated crossover sum
  → output gain → soft limiter
```

## Why the rewrite

The previous version had audible bugs. Each one is now covered by a test:

| Symptom | Cause | Fix |
|---------|-------|-----|
| **Meters pinned / constant loud hiss the moment it loads** | The noise-floor gate sat at −120 dBFS, so an idle track's dither and converter noise (−90 to −70 dBFS) was never gated and got amplified by 35 dB. This is *not* a bug inherited from the reference — **upstream has no noise-floor handling at all**, and measured through it an idle −70 dBFS track is lifted by +38 dB to −36 dBFS. | Gate at −76 dBFS (fade −70 to −58). An idle −70 dBFS track now comes out at −61.7 dBFS, and anything from −58 dBFS up is bit-identical to upstream. `Behavior` moves the window; −12 dB disables it. |
| Loud bursts / "exploding" output | The wet buffer was never zeroed between blocks, so every band summed on top of the previous block; the output ran away within a few buffers. | Wet scratch is cleared per chunk. |
| Blasting, no audible compression | Mix was applied as the **dry** amount, so full-wet played the raw input. | Mix is the wet amount; full wet is full compression. |
| Startup click / pop | Envelopes started far below the band level, so the upward stage slammed up to +30 dB on the first block. | Envelopes park at a silence floor; input trim ramps across the block instead of stepping. |
| Zipper noise on every knob move | `Depth`, the Up/Down macros and the per-band ratios all land in the compressor's *exponent*, which stepped at each block boundary. | The exponent is interpolated across the block, starting exactly where the previous block ended; makeup gain, mix and trims ramp per sample. |
| Harsh clipping | A hard ±1 clamp put a corner in the waveform. | Smooth tanh limiter above −6 dBFS; transparent below it. |
| Thin, hollow sound | The Linkwitz-Riley **high-pass used the wrong denominator sign**, so its Q was 0.5 instead of 0.707 and the bands did not sum to an allpass — a −3 dB dip at each crossover. | Correct Butterworth Q for both outputs. |
| Crackle between notes | The upward stage amplified the residual noise between phrases. | Covered by the noise-floor gate; see the meters-pinned row above for how its level was chosen. |
| Stereo image shifting | The detector ran per channel. | Linked detection: the loudest channel drives both. |
| Rattle when sweeping a crossover | Sweeping across the collapse threshold flipped the band topology on alternate blocks, discarding the filter and envelope state each time. | Hysteresis on the collapse decision, and the crossover controls stop above the collapse range. |
| Real-time glitches | `juce::dsp::LinkwitzRileyFilter` crossed an allocation boundary on the audio thread; the look-ahead delay line also reported ~0.74 s of latency. | Allocation-free engine, no look-ahead, **zero reported latency**. |
| UI overlap, wrong meter ballistics | Hard-coded coordinates collided, and the meter decay was inverted (slow up, instant down). | Derived layout with a snapshot test; meters fall slowly and snap up, with peak hold. |

## Deviations from the reference

Two deliberate departures, both measured rather than assumed.

**The Linkwitz-Riley high-pass has the correct Q.** `schwung-ottx` derives it with
the same denominator sign as the low-pass, which silently drops the high-pass to
Q = 0.5 instead of 0.707. The three bands then do *not* sum to an allpass and
there is a ~3 dB dip at every crossover. This build uses the correct Butterworth
Q for both outputs; `testCrossoverReconstruction` asserts the flat magnitude.

**There is a noise-floor gate, and upstream does not have one.** vitOTT/vitOTTx's
`compressor.cpp` never mentions noise, floors or gates — the only limit on the
upward stage is `clamp(upper * lower, 0, 32)`, and its envelopes reset to zero.
Measured through unmodified upstream:

| idle track | upstream lift | this plugin (−76 dBFS floor) |
|------------|---------------|------------------------------|
| −70 dBFS   | +38.2 dB      | +13.1 dB                     |
| −80 dBFS   | +17.4 dB      | +13.1 dB                     |
| −90 dBFS   | +9.9 dB       | +13.1 dB                     |

Upstream's worst case is the one that matters: +38 dB on an idle track puts it at
−36 dBFS, which is plainly audible and pins the meters. A plugin living in a DAW
cannot do that, so "match the reference exactly" was never an available option
here — the value had to be chosen, and −76 dBFS is where the gate fully closes
before an idle track's noise while everything from −58 dBFS upward stays
bit-identical to upstream.

The per-band makeup gains are 3 dB below upstream's for the same reason: its
numbers assume an int16 pipeline and peak at 0 dBFS RMS on ordinary material.

## Licence

The DSP derives from Vital, which is GPLv3; this project is therefore
distributed under the GPLv3. Note that no `LICENSE` file is currently committed.
