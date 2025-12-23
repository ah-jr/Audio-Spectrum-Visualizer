# Audio Spectrum Visualizer & EQ

A real-time audio spectrum visualizer with 5-band parametric EQ, built in C++ using FFT. Available as both a **standalone application** and a **VST3 plugin**.

![Spectrum Visualizer](./demo.png)

## Features

### Spectrum Analyzer
- **Real-time FFT Analysis**: Cooley-Tukey radix-2 algorithm (20Hz - 20kHz)
- **Multiple Visualization Styles**: Line graph, Bars, Waves, Circles, Particles, Mirror
- **Color Themes**: Cyberpunk, Neon, Sunset, Ocean, Monochrome
- **Audio Formats**: MP3, WAV, FLAC, OGG, M4A, AAC (standalone only)
- **dB Scale**: Proper logarithmic display (-60dB to 0dB)

### 5-Band Parametric EQ
- **Bands**: 60Hz, 250Hz, 1kHz, 4kHz, 12kHz (fully adjustable)
- **Controls per band**:
  - **Gain**: -12dB to +12dB
  - **Frequency**: 20Hz to 20kHz (drag horizontally)
  - **Q Factor**: 0.1 to 10.0 (mouse scroll)
- **Visual EQ Curve**: Real-time response visualization
- **Biquad Filters**: High-quality parametric EQ processing

## Building

### Quick Start (Windows)

```bash
mkdir build && cd build

# Standalone only (default)
cmake .. -G "Visual Studio 17 2022"
cmake --build . --config Release

# VST3 only (requires VST3 SDK - see below)
cmake .. -G "Visual Studio 17 2022" -DBUILD_VST3=ON -DBUILD_STANDALONE=OFF
cmake --build . --config Release

# Both standalone + VST3
cmake .. -G "Visual Studio 17 2022" -DBUILD_ALL=ON
cmake --build . --config Release
```

### Prerequisites
- CMake 3.16+
- C++17 compatible compiler
- (VST3 only) VST3 SDK (must be downloaded manually)

### Standalone Application

```bash
mkdir build && cd build

# Windows (Visual Studio)
cmake .. -G "Visual Studio 17 2022" -DBUILD_STANDALONE=ON
cmake --build . --config Release

# Linux/macOS
cmake .. -DBUILD_STANDALONE=ON -DCMAKE_BUILD_TYPE=Release
cmake --build .
```

Run: `./Release/AudioSpectrumVisualizer.exe [audio_file.mp3]`

### VST3 Plugin

The VST3 plugin uses a custom OpenGL renderer (no VSTGUI dependency), providing the same visual appearance as the standalone application.

**Step 1: Download VST3 SDK** (required)

```bash
# Clone into external folder (auto-detected)
cd fft
git clone --recursive https://github.com/steinbergmedia/vst3sdk.git external/vst3sdk
```

> **Important**: The `--recursive` flag is required to fetch the base/pluginterfaces submodules!

> **Note**: VSTGUI is NOT required. The plugin uses custom OpenGL rendering.

**Step 2: Build the plugin**

```bash
# Clean any previous build first
rd /s /q build
mkdir build && cd build

# Windows (Visual Studio)
cmake .. -G "Visual Studio 17 2022" -DBUILD_VST3=ON -DBUILD_STANDALONE=OFF
cmake --build . --config Release

# Or specify SDK path explicitly if not in external/vst3sdk
cmake .. -G "Visual Studio 17 2022" -DBUILD_VST3=ON -DVST3_SDK_ROOT="C:/path/to/vst3sdk"
cmake --build . --config Release
```

**Step 3: Install the plugin**

The plugin is built at: `build/VST3/Release/SpectrumEQ.vst3`

Copy the entire `SpectrumEQ.vst3` folder to your DAW's VST3 folder:
- **Windows**: `C:\Program Files\Common Files\VST3\`
- **macOS**: `~/Library/Audio/Plug-Ins/VST3/`
- **Linux**: `~/.vst3/`

Then rescan plugins in your DAW.

### Build Both (Standalone + VST3)

```bash
mkdir build && cd build

# Using BUILD_ALL (recommended)
cmake .. -G "Visual Studio 17 2022" -DBUILD_ALL=ON
cmake --build . --config Release

# Or enable both explicitly
cmake .. -G "Visual Studio 17 2022" -DBUILD_STANDALONE=ON -DBUILD_VST3=ON
cmake --build . --config Release
```

> **Note**: Building VST3 requires the VST3 SDK. See the [VST3 Plugin](#vst3-plugin) section for setup instructions.

## Standalone Controls

| Key | Action |
|-----|--------|
| `SPACE` | Play / Pause |
| `←` / `→` | Seek ±5 seconds |
| `S` | Cycle visualization styles |
| `T` | Cycle color themes |
| `E` | Toggle EQ on/off |
| `R` | Reset EQ to defaults |
| `O` | Open file dialog |
| `ESC` | Exit |

### EQ Controls (Line Mode)
- **Drag knobs** horizontally to change frequency
- **Drag knobs** vertically to change gain
- **Scroll wheel** on knobs to adjust Q (bandwidth)
- Gain snaps to 0dB when close

## Project Structure

```
fft/
├── CMakeLists.txt              # Unified build (standalone + VST)
├── src/
│   ├── main.cpp                # Standalone entry point
│   ├── fft.hpp/cpp             # FFT algorithm (shared)
│   ├── eq_processor.hpp        # EQ processor (shared, header-only)
│   ├── shared_colors.hpp       # Color themes (shared between standalone/VST)
│   ├── audio_analyzer.hpp/cpp  # Audio loading/playback
│   ├── spectrum_visualizer.*   # Visualization + EQ UI (raylib)
│   └── file_dialog.*           # Native file dialogs
├── vst/
│   ├── plugin_processor.*      # VST3 audio processor
│   ├── plugin_controller.*     # VST3 parameter controller
│   ├── plugin_editor.*         # VST3 editor UI (OpenGL)
│   ├── gl_renderer.*           # OpenGL renderer (raylib-compatible API)
│   ├── plugin_entry.cpp        # VST3 factory
│   └── plugin_ids.hpp          # Parameter IDs
└── external/
    ├── miniaudio.h             # Audio library (auto-downloaded)
    └── vst3sdk/                # VST3 SDK (manual download)
```

## Technical Details

### FFT
- **Size**: 8192 samples (configurable)
- **Window**: Hann
- **Frequency Resolution**: ~5.4 Hz at 44.1kHz

### EQ Processing
- **Filter Type**: Biquad peaking EQ
- **Algorithm**: Direct Form II Transposed
- **Processing**: 64-bit internal, 32-bit I/O

### VST3 Plugin
- **Format**: VST3
- **Channels**: Stereo (2 in / 2 out)
- **Parameters**: 16 (5 bands × 3 params + bypass)
- **Automation**: Full parameter automation support
- **State**: Preset save/load supported
- **UI**: Custom OpenGL renderer (no VSTGUI dependency)
- **Same visual appearance** as standalone application

## Dependencies

| Component | Library | Notes |
|-----------|---------|-------|
| Standalone Audio | miniaudio | Auto-downloaded |
| Standalone Graphics | raylib 5.5 | Auto-downloaded |
| VST3 Plugin | VST3 SDK | Manual download (base + pluginterfaces only) |
| VST3 UI | OpenGL | System library (no VSTGUI needed) |

## License

Open source. Feel free to use, modify, and distribute.

## Acknowledgments

- [raylib](https://www.raylib.com/) - Graphics library
- [miniaudio](https://miniaud.io/) - Audio library
- [Steinberg VST3 SDK](https://github.com/steinbergmedia/vst3sdk) - Plugin SDK
