# VibeOTT - JUCE Multiband Compressor Plugin

A VST3 audio plugin implementing OTT-style multiband upward/downward compression.
DSP core reverse-engineered from Xfer OTT via the xtractedott project.

## Project Structure

```
VibeOTT/
├── CMakeLists.txt              # CMake build config (requires JUCE submodule)
├── .gitmodules                 # JUCE submodule declaration
├── .github/workflows/build.yml # CI: Windows VST3 build (manual trigger)
├── JUCE/                       # JUCE framework (git submodule)
└── Source/
    ├── MultibandCompressor.h   # DSP core: biquad crossover + RMS compression (from xtractedott)
    ├── PluginProcessor.h/cpp   # JUCE AudioProcessor + APVTS parameters
    └── PluginEditor.h/cpp      # GUI: dark theme, rotary knobs, band meters
```

## Build

```bash
git clone --recurse-submodules https://github.com/MARD1NO/VibeOTT.git
cmake -B build -DCMAKE_BUILD_TYPE=Release
cmake --build build --config Release
```

- **Windows**: `build/VibeOTT_artefacts/Release/VST3/VibeOTT.vst3`
- **macOS**: `build/VibeOTT_artefacts/VST3/VibeOTT.vst3`

## Plugin Parameters

All parameters use 0.0-1.0 internal range (matching original OTT VST convention).

| Parameter | ID | Range | Default |
|-----------|-----|-------|---------|
| Depth | DEPTH | 0–1 | 0.5 |
| Upward Ratio | UPWARD_RATIO | 0–1 | 0.6 |
| Downward Ratio | DOWNWARD_RATIO | 0–1 | 0.7 |
| Low Gain | LOW_GAIN | 0–1 | 0.5 |
| Mid Gain | MID_GAIN | 0–1 | 0.5 |
| High Gain | HIGH_GAIN | 0–1 | 0.5 |
| Output Gain | OUTPUT_GAIN | 0–1 | 0.5 |

## DSP Architecture (from xtractedott)

- **Crossover**: 6 custom biquad filters (dual LP/HP output), 200 Hz and 2000 Hz split
- **Compression**: RMS-based detection with log-domain processing
  - Low band: threshold -20 dB, ratio 2:1, attack 10ms, release 100ms
  - Mid band: threshold -15 dB, ratio 3:1, attack 8ms, release 80ms
  - High band: threshold -10 dB, ratio 4:1, attack 5ms, release 50ms
- **Delay compensation**: 32768-sample look-ahead buffer
- **Depth**: scales compression via `depth * 0.52 + 1.0` processing gain
- Band meters show RMS input level and gain reduction per band

## CI

GitHub Actions builds Windows VST3 on manual trigger (workflow_dispatch).

## Known Issues

- JUCE submodule must be added via `git submodule add` on a machine with GitHub access
- 32768 samples latency (~0.74s at 44.1kHz) due to look-ahead buffer
