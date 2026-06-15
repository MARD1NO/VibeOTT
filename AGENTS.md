# VibeOTT - JUCE Multiband Compressor Plugin

A VST3 audio plugin implementing OTT-style multiband upward/downward compression.

## Project Structure

```
VibeOTT/
├── CMakeLists.txt              # CMake build config (requires JUCE submodule)
├── .gitmodules                 # JUCE submodule declaration
├── .github/workflows/build.yml # CI: Windows VST3 build
├── JUCE/                       # JUCE framework (git submodule)
└── Source/
    ├── MultibandCompressor.h   # DSP core: 3-band crossover + upward/downward compression
    ├── PluginProcessor.h/cpp   # JUCE AudioProcessor + APVTS parameters
    └── PluginEditor.h/cpp      # GUI: dark theme, rotary sliders
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

Copy `build/VibeOTT_artefacts/Release/VST3/VibeOTT.vst3` to your VST3 plugin directory.

## Plugin Parameters

| Parameter | ID | Range | Default |
|-----------|-----|-------|---------|
| Depth | DEPTH | 0–1 | 1.0 |
| Mix | MIX | 0–1 | 1.0 |
| Crossover Low/Mid | CROSSOVER_LOW_MID | 20–2000 Hz | 880 Hz |
| Crossover Mid/High | CROSSOVER_MID_HIGH | 200–16000 Hz | 4400 Hz |
| Upward Threshold | UP_THRESHOLD | -60–0 dB | -36 dB |
| Upward Ratio | UP_RATIO | 1–10 | 3.0 |
| Upward Attack | UP_ATTACK | 0.1–200 ms | 10 ms |
| Upward Release | UP_RELEASE | 0.1–500 ms | 50 ms |
| Downward Threshold | DOWN_THRESHOLD | -60–0 dB | -18 dB |
| Downward Ratio | DOWN_RATIO | 1–10 | 3.0 |
| Downward Attack | DOWN_ATTACK | 0.1–200 ms | 10 ms |
| Downward Release | DOWN_RELEASE | 0.1–500 ms | 50 ms |
| Low Gain | LOW_GAIN | -12–12 dB | 0 dB |
| Mid Gain | MID_GAIN | -12–12 dB | 0 dB |
| High Gain | HIGH_GAIN | -12–12 dB | 0 dB |

## DSP Architecture

- **Crossover**: Linkwitz-Riley 4th-order filters (2x2 array for stereo)
- **Upward compression**: Boosts signal below threshold (classic OTT characteristic)
- **Downward compression**: Attenuates signal above threshold
- **Depth** scales compression amount; **Mix** is dry/wet crossfade
- Per-band gain applied after compression

## CI

GitHub Actions builds Windows VST3 on push to `main`. Requires JUCE submodule to be properly registered (mode 160000 in git index).

## Known Issues

- JUCE submodule must be added via `git submodule add` on a machine with GitHub access; manual `.gitmodules` creation alone is insufficient
- Some sign-conversion warnings during build (non-fatal)
