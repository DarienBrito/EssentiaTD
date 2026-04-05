# Essentia CHOP Suite
## Windows & macOS

<p align="center">
  <img src="assets/icon.svg" width="120" alt="EssentiaTD">
</p>

Real-time and offline audio analysis for [TouchDesigner](https://derivative.ca/) powered by [Essentia](https://essentia.upf.edu/). Five C++ CHOP plugins expose spectrum analysis, mel bands, MFCCs, pitch detection, key estimation, onset/BPM tracking, and EBU R128 loudness metering — with both real-time (per-frame) and batch (full-file) analysis modes running natively inside TD.

<div align="center">

### **[Interactive Guide — Parameters, Use Cases & Examples](https://darienbrito.github.io/EssentiaTD/)**

[![Guide](https://img.shields.io/badge/📖_Interactive_Guide-darienbrito.github.io/EssentiaTD-e5484d?style=for-the-badge)](https://darienbrito.github.io/EssentiaTD/)

</div>

# Install

Copy the plugin files from [Releases](https://github.com/DarienBrito/EssentiaTD/releases) to your TouchDesigner plugins folder (TD scans subdirectories too).

### Windows
```bash
cp src/build/Release/*.dll "C:/Users/<you>/Documents/Derivative/Plugins/"
```

### macOS
```bash
cp src/build/Release/*.dylib ~/Library/Application\ Support/Derivative/TouchDesigner/Plugins/
```

Restart TouchDesigner to load the new operators. They appear in the OP Create Dialog under their registered names (e.g., Tab > CHOP > "Essentia Spectrum").

## Operators

Each operator (except Spectrum) has a **Mode** parameter that switches between **Realtime** and **Batch** analysis:

| Operator | Realtime Input | Batch Input | Description |
|---|---|---|---|
| **Essentia Spectrum** | Audio CHOP | — | FFT magnitude spectrum as a static sample buffer (fftSize/2+1 bins) |
| **Essentia Spectral** | Spectrum CHOP | Audio CHOP | MFCC, centroid, flux, rolloff, contrast, HFC, complexity, mel bands |
| **Essentia Tonal** | Spectrum CHOP | Audio CHOP | Pitch (YinFFT), HPCP chroma, key/scale, dissonance, inharmonicity |
| **Essentia Rhythm** | Spectrum CHOP | Audio CHOP | Onset detection, BPM estimation, beat phase/confidence |
| **Essentia Loudness** | Audio CHOP | Audio CHOP | EBU R128 loudness, RMS energy, zero-crossing rate |

### Realtime vs Batch Mode

- **Realtime** (default): Per-frame analysis at TD's cook rate. Spectral, Tonal, and Rhythm read from Essentia Spectrum; Loudness reads raw audio. Output is 1 sample per channel.
- **Batch**: Full-file offline analysis on a background thread. All CHOPs take raw audio directly (no Spectrum CHOP needed — each handles its own FFT). Output is N samples (one per analysis frame). Triggered by a Compute pulse or Autocompute toggle.

## Signal Flow

```
Realtime mode:
  Audio CHOP
    ├── Essentia Spectrum ──┬── Essentia Spectral (Mode=Realtime)
    │                       ├── Essentia Tonal    (Mode=Realtime)
    │                       └── Essentia Rhythm   (Mode=Realtime)
    └── Essentia Loudness (Mode=Realtime)

Batch mode:
  File In CHOP ──┬── Essentia Spectral (Mode=Batch)
                 ├── Essentia Tonal    (Mode=Batch)
                 ├── Essentia Rhythm   (Mode=Batch)
                 └── Essentia Loudness (Mode=Batch)
```

**Realtime**: Spectrum is the shared upstream node for the three spectral-domain CHOPs. Loudness takes raw audio directly.
**Batch**: Each CHOP is self-contained — handles its own windowing and FFT internally.


## Spectrum: Analysis, Not Visualization

Essentia Spectrum outputs a linear-bin FFT magnitude spectrum designed for feeding downstream analysis algorithms (Spectral, Tonal, Rhythm). Its bins are uniformly spaced in Hz, which is what Essentia's algorithms expect but looks bottom-heavy when plotted directly — most musical detail is crammed into the lower bins.

For spectral visualization, use TouchDesigner's built-in **Audio Spectrum CHOP**, which provides a perceptually scaled output suited for display. Note that TD's Audio Spectrum cannot be used as input to the Essentia analysis CHOPs — they require the linear-bin format that Essentia Spectrum provides.

## Mono by Design

The suite processes a single audio channel. This is intentional — stereo analysis would double every output channel (e.g., `mfcc0_L`, `mfcc0_R`, `spectral_centroid_L`, `spectral_centroid_R`), making the output unwieldy and harder to map in a visual context.

If you need stereo-aware analysis, select each channel independently using a **Select CHOP** and run two separate analysis chains. This keeps the output organized and lets you choose which features to extract per channel.

**Recommended approach for stereo sources** — In most audio-reactive scenarios, collapsing to mono before analysis preserves all relevant information. Sum left and right with a **Math CHOP** (Combine Channels = Average) before feeding into Essentia Spectrum. This captures the full frequency content of both channels without phase cancellation artifacts that a simple channel pick might miss.


# Build from source

## Prerequisites

### 1. Essentia Static Library

Build the Essentia static library and place headers + lib under:

```
src/vendor/essentia/
  ├── include/essentia/   # Essentia headers
  └── lib/
      ├── essentia.lib    # Windows (MSVC x64 static)
      └── libessentia.a   # macOS (universal or arm64)
```

See [docs/building-essentia.md](docs/building-essentia.md) for build instructions on both platforms.

### 2. TouchDesigner SDK Headers

Copy from your TouchDesigner installation:

**Windows:**
```
C:/Program Files/Derivative/TouchDesigner/Samples/CPlusPlus/
  CHOP_CPlusPlusBase.h
  CPlusPlus_Common.h
```

**macOS:**
```
/Applications/TouchDesigner.app/Contents/Resources/Samples/CPlusPlus/
  CHOP_CPlusPlusBase.h
  CPlusPlus_Common.h
```

Into `src/` alongside the plugin source files.

## Build

### Windows
```bash
cd src
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

### macOS
```bash
cd src
cmake -B build
cmake --build build --config Release
```

Produces 5 plugins in `src/build/Release/` (`.dll` on Windows, `.dylib` on macOS).

## Architecture Notes

- Each unified CHOP (Spectral, Tonal, Rhythm, Loudness) supports both Realtime and Batch modes via a **Mode** parameter
- **Realtime mode**: per-frame analysis, `timeslice = false` (except Loudness which uses `timeslice = true`), output `sampleRate` = component FPS
- **Batch mode**: full-file analysis on a background thread, results cached until next computation, output `sampleRate` = audioRate / hopSize
- Batch computation is triggered by a Compute pulse or Autocompute toggle (detects input changes via audio fingerprinting)
- Internally, all four unified CHOPs inherit from `UnifiedCHOPBase<Derived>` (CRTP) which handles mode branching, async polling, and error/warning plumbing
- Spectrum outputs use `startIndex = 0` to produce a static indexed buffer
- All downstream CHOPs use `inputMatchIndex = -1` to avoid inheriting the audio sample rate from upstream
- Every Essentia `compute()` call is wrapped in try/catch to prevent crashes in TD
- All Essentia algorithm outputs must be bound before calling `compute()`, even if the output value is unused

## License

AGPL-3.0-or-later

---

<p align="center">
A project by <strong>Darien Brito</strong><br><br>
<a href="https://www.patreon.com/darienbrito">Patreon</a> · <a href="https://www.instagram.com/darien.brito/">Instagram</a><br><br>
<sub>Essentia CHOP Suite</sub>
</p>
