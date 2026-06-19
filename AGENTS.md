# VibeOTT - JUCE Multiband Compressor Plugin

A VST3 audio plugin implementing OTT-style multiband upward/downward compression.

## Project Structure

```
VibeOTT/
├── CMakeLists.txt              # CMake build config (requires JUCE submodule)
├── .gitmodules                 # JUCE submodule declaration
├── .github/workflows/build.yml # CI: Windows VST3 build (manual trigger)
├── JUCE/                       # JUCE framework (git submodule)
└── Source/
    ├── MultibandCompressor.h   # DSP core: 3-band crossover + upward/downward compression
    ├── PluginProcessor.h/cpp   # JUCE AudioProcessor + APVTS parameters
    └── PluginEditor.h/cpp      # GUI: dark theme, rotary sliders, band meters
```

## Build

```bash
# Clone with submodule
git clone --recurse-submodules https://github.com/MARD1NO/VibeOTT.git

# If already cloned without submodule
git submodule update --init --recursive

# Configure and build
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

- **Windows output**: `build/VibeOTT_artefacts/Release/VST3/VibeOTT.vst3`
- **macOS output**: `build/VibeOTT_artefacts/VST3/VibeOTT.vst3`

### macOS Install

```bash
cp -r build/VibeOTT_artefacts/VST3/VibeOTT.vst3 ~/Library/Audio/Plug-Ins/VST3/
```

### Windows Install

Copy `build/VibeOTT_artefacts/Release/VST3/VibeOTT.vst3` to `C:\Program Files\Common Files\VST3\`.

## Plugin Parameters

| Parameter | ID | Range | Default |
|-----------|-----|-------|---------|
| Depth | DEPTH | 0–1 | 0.5 |
| Upward Ratio | UPWARD_RATIO | 0–1 | 0.6 |
| Downward Ratio | DOWNWARD_RATIO | 0–1 | 0.7 |
| Low Threshold | LOW_THRESH | -60–0 dB | -20 dB |
| Mid Threshold | MID_THRESH | -60–0 dB | -15 dB |
| High Threshold | HIGH_THRESH | -60–0 dB | -10 dB |
| Low Gain | LOW_GAIN | -12–12 dB | 0 dB |
| Mid Gain | MID_GAIN | -12–12 dB | 0 dB |
| High Gain | HIGH_GAIN | -12–12 dB | 0 dB |

Ratio parameter 0.5 = 1:1 (no compression), 1.0 = 9:1.

## DSP Architecture

- **Crossover**: Linkwitz-Riley 4th-order filters at 200 Hz and 2000 Hz
- **Per-band thresholds**: Below threshold = upward compression, above = downward
- **Depth** is dry/wet crossfade (0 = bypass, 1 = full OTT)
- Per-band gain applied to wet path only
- Per-band attack/release: Low 10/100ms, Mid 8/80ms, High 5/50ms
- Band meters show input level and gain reduction per band

## CI

GitHub Actions builds Windows VST3 on manual trigger (workflow_dispatch).

## Known Issues

- JUCE submodule must be added via `git submodule add` on a machine with GitHub access
- Some sign-conversion warnings during build (non-fatal)
