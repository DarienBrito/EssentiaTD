# Essentia CHOP Suite

<p align="center">
  <img src="assets/icon.svg" width="120" alt="EssentiaTD">
</p>

Real-time and offline audio analysis for [TouchDesigner](https://derivative.ca/) powered by [Essentia](https://essentia.upf.edu/). Five C++ CHOP plugins expose spectrum analysis, mel bands, MFCCs, pitch detection, key estimation, onset/BPM tracking, and EBU R128 loudness metering — with both real-time (per-frame) and batch (full-file) analysis modes running natively inside TD.

[![Guide](https://img.shields.io/badge/📖_Interactive_Guide-darienbrito.github.io/EssentiaTD-e5484d?style=for-the-badge)](https://darienbrito.github.io/EssentiaTD/)

# Install

Copy all `.dll` files from [Releases](https://github.com/DarienBrito/EssentiaTD/releases) to your TouchDesigner plugins folder:

```bash
cp src/build/Release/*.dll "C:/Users/<you>/Documents/Derivative/Plugins/"
```

Or into a subfolder — TD scans subdirectories of the Plugins folder.

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

## Recommended Settings

Default parameter values match Essentia's [Music Extractor](https://essentia.upf.edu/streaming_extractor_music.html) recommendations — the canonical reference for music analysis. All defaults work well out of the box.

### Quick Reference (Essentia Music Extractor Defaults)

| Setting | Spectral / Tonal (Batch) | Rhythm (Batch) | Spectrum (Realtime) |
|---|---|---|---|
| FFT Size | 2048 | 2048 | 1024 |
| Hop Size | 1024 | 256 | 512 |
| Window | Blackman-Harris 62 | Blackman-Harris 62 | Blackman-Harris 62 |

### Per-Feature Defaults

| Feature | Parameter | Default | Essentia Recommendation |
|---|---|---|---|
| MFCC | Coefficients / Bands / Freq Range | 13 / 40 / 0–11000 Hz | Standard for music and speech |
| Mel Bands | Count / Type / Freq Range | 40 / power / 0–22050 Hz | 40 bands matches Music Extractor; power type is standard (matches librosa) |
| Centroid | Range | sampleRate / 2 | Normalizes output to frequency range |
| Flux | Norm / Half Rectify | L2 / Off | L2 is more commonly used (Tzanetakis & Cook) |
| Rolloff | Cutoff | 0.85 | 85% energy threshold — standard for brightness estimation |
| Spectral Contrast | Bands | 6 | Octave-based sub-bands |
| HFC | Type | Masri | Default; Jensen/Brossier for stronger high-frequency emphasis |
| Spectral Complexity | Threshold | 0.005 | Captures significant peaks, filters noise |

### Tuning for Specific Use Cases

- **Deep learning / ML features**: Set Mel Bands to 128, enable Log Mel (dB Scale), FFT 2048 / Hop 1024
- **Voice-only analysis**: Narrow MFCC freq to 80–3400 Hz
- **Tonal analysis (pitch, key)**: Use FFT 4096 for better frequency resolution
- **Percussive / onset-heavy material**: Use HFC Type = Jensen or Brossier for stronger transient emphasis; enable Flux Half Rectify
- **Electronic / dance music**: Use Key Profile = EDMA in Tonal CHOP

## Spectrum: Analysis, Not Visualization

Essentia Spectrum outputs a linear-bin FFT magnitude spectrum designed for feeding downstream analysis algorithms (Spectral, Tonal, Rhythm). Its bins are uniformly spaced in Hz, which is what Essentia's algorithms expect but looks bottom-heavy when plotted directly — most musical detail is crammed into the lower bins.

For spectral visualization, use TouchDesigner's built-in **Audio Spectrum CHOP**, which provides a perceptually scaled output suited for display. Note that TD's Audio Spectrum cannot be used as input to the Essentia analysis CHOPs — they require the linear-bin format that Essentia Spectrum provides.

## Mono by Design

The suite processes a single audio channel. This is intentional — stereo analysis would double every output channel (e.g., `mfcc0_L`, `mfcc0_R`, `spectral_centroid_L`, `spectral_centroid_R`), making the output unwieldy and harder to map in a visual context.

If you need stereo-aware analysis, select each channel independently using a **Select CHOP** and run two separate analysis chains. This keeps the output organized and lets you choose which features to extract per channel.

**Recommended approach for stereo sources** — In most audio-reactive scenarios, collapsing to mono before analysis preserves all relevant information. Sum left and right with a **Math CHOP** (Combine Channels = Average) before feeding into Essentia Spectrum. This captures the full frequency content of both channels without phase cancellation artifacts that a simple channel pick might miss.

## Output Reference & Use Cases

### Essentia Spectrum

| Channel | Range | What it measures | Suggested use in TouchDesigner |
|---|---|---|---|
| `spectrum` | 0 – fftSize/2+1 samples | FFT magnitude per bin | Audio-reactive bar graphs, 3D terrain from frequency data, custom spectral visualizers, input for external analysis |

### Essentia Spectral

| Channel | Range | What it measures | Suggested use in TouchDesigner |
|---|---|---|---|
| `mfcc0`–`mfcc12` | unbounded (typically -50 to 50) | Timbral fingerprint coefficients | Timbre classification — cluster similar sounds, drive visual style transitions based on tonal character, distinguish instruments |
| `spectral_centroid` | 0 – sampleRate/2 Hz | Center of mass of the spectrum (brightness) | Map to color temperature (warm/cool), particle speed, lighting intensity — high centroid = bright, shimmery sounds |
| `spectral_flux` | 0+ | Frame-to-frame spectral change | Detect timbral transitions, trigger visual events on sudden textural shifts, novelty detection |
| `spectral_rolloff` | 0 – sampleRate/2 Hz | Frequency below which 85% of energy lies | Distinguish bright vs warm sounds, control high-pass/low-pass visual filtering, EQ-style visuals |
| `spectral_contrast0`–`5` | unbounded | Peak-to-valley ratio in 6 sub-bands | Multi-band visual layering, texture detection (tonal vs noisy content), per-band glow/intensity |
| `hfc` | 0+ | High-frequency content energy | Hi-hat/cymbal reactivity, percussive high-end triggers, sparkle/shimmer effects |
| `spectral_complexity` | 0+ | Number of significant spectral peaks | Visual density — simple tones = sparse, complex sounds = dense particle fields or geometry |
| `mel0`–`melN` | 0+ | Energy in perceptual frequency bands | Multi-band audio visualizers, frequency-mapped color gradients, per-band particle systems, ML input features |

### Essentia Tonal

| Channel | Range | What it measures | Suggested use in TouchDesigner |
|---|---|---|---|
| `pitch` | 0+ Hz | Fundamental frequency (YinFFT) | Pitch-to-note mapping for generative music visuals, vocal tracking, pitch-controlled animation speed or position |
| `pitch_confidence` | 0 – 1 | Reliability of pitch estimate | Gate pitch-driven effects — only apply when confidence is high, crossfade between pitched/unpitched visual modes |
| `note_a`–`note_gs` | 0 – 1 | Chroma energy per pitch class (A through G#, bin 0 = reference freq) | Harmony wheels, chord visualization, map each note to a color, detect chord changes for scene transitions |
| `key` | 0 – 11 (encoded) | Detected musical key | Key-adaptive color palettes, scene theming per key, generative pattern selection |
| `key_scale` | 0 = major, 1 = minor | Major or minor tonality | Mood-driven visuals — major = warm/bright palette, minor = cool/dark palette |
| `key_strength` | 0 – 1 | Confidence of key estimate | Gate key-driven effects, blend strength into color saturation |
| `dissonance` | 0 – 1 | Sensory roughness | Glitch/distortion intensity, visual chaos/turbulence, tension indicators |
| `inharmonicity` | 0 – 1 | Deviation from harmonic series | Distinguish percussion from melody, drive material textures (metallic vs organic) |

### Essentia Rhythm

| Channel | Range | What it measures | Suggested use in TouchDesigner |
|---|---|---|---|
| `onset` | 0 or 1 | Transient attack detected this frame | Flash/strobe triggers, particle bursts, camera cuts, step-sequenced events |
| `onset_strength` | 0+ | Continuous onset detection function | Velocity-sensitive triggers — scale burst intensity by strength, smooth onset envelope |
| `bpm` | BPM min – max | Estimated tempo | Sync animation speeds, LFO rates, generative pattern timing to the music's tempo |
| `beat` | 0 or 1 | Beat trigger on the estimated grid | Rhythmic pulsing, beat-locked transitions, synchronized step-sequencing |
| `beat_phase` | 0 – 1 (sawtooth) | Position within current beat | Smooth beat-synced animation — feed into easing curves, continuous rhythmic motion, pendulum effects |
| `beat_confidence` | 0 – 1 | Reliability of beat tracking | Fade in beat-synced effects only when tracking is stable, crossfade to onset-only mode when low |

### Essentia Loudness

| Channel | Range | What it measures | Suggested use in TouchDesigner |
|---|---|---|---|
| `loudness` | dB (typically -100 to 0) | Instantaneous perceived loudness | Fast VU-meter response, frame-level reactive scaling of geometry or opacity |
| `loudness_momentary` | dB | EBU R128 momentary (400 ms window) | Smooth level metering, dynamic scaling of visual elements, responsive but stable |
| `loudness_shortterm` | dB | EBU R128 short-term (3 s window) | Scene-level intensity, ambient lighting adjustments, macro-level energy |
| `loudness_integrated` | dB | Running gated average (EBU R128) | Overall show level monitoring, normalization reference, long-term gain tracking |
| `dynamic_range` | dB (0+) | Peak-to-valley swing of short-term loudness | Detect builds and drops, drive contrast-based transitions, tension/release mapping |
| `rms` | 0 – 1 | Root mean square energy | Simple amplitude-reactive scaling — size, opacity, displacement. The classic "audio reactive" signal |
| `zcr` | 0 – 1 | Zero crossing rate (noisiness) | Distinguish noise from pitched content — high ZCR = noisy/percussive, low ZCR = tonal. Drive grain/static effects |

## Parameters

### Essentia Spectrum

| Parameter | Type | Default | Description |
|---|---|---|---|
| FFT Size | Menu | 1024 | 512 / 1024 / 2048 / 4096 / 8192 / 16384 |
| Hop Size | Int | 512 | 64–16384, controls analysis overlap |
| Window Type | Menu | Blackman-Harris 62 | Hann / Hamming / Triangular / Blackman-Harris 62/70/74/92 |
| Zero Padding | Menu | None | None / Half FFT / Full FFT — interpolates the spectrum for better frequency resolution |

**FFT Size and Quality** — Larger FFT sizes improve frequency resolution (more bins, better at distinguishing close pitches) at the cost of time resolution (each frame covers more audio, smearing transients). For tonal analysis (pitch, key, HPCP), 2048–4096 is the sweet spot. For rhythm/onset detection, 1024 responds faster to transients. Going beyond 8192 has diminishing returns and adds latency. The default 1024 is a good general-purpose balance for realtime responsiveness.

**Window Type** — Each window trades main-lobe width for side-lobe suppression. Blackman-Harris 62 (the default, matching Essentia's Music Extractor) offers strong side-lobe suppression for cleaner spectral features. Hann is a lighter alternative. The BH 74/92 variants offer progressively stronger suppression at the cost of wider main lobes.

**Zero Padding** — Appends zeros to the windowed frame before FFT, which interpolates spectral bins without changing frequency resolution. This improves the accuracy of peak-based descriptors (centroid, rolloff, pitch) and produces smoother spectrum plots. "Half FFT" adds fftSize/2 zeros; "Full FFT" doubles the frame.

### Essentia Spectral

| Parameter | Type | Default | Mode | Description |
|---|---|---|---|---|
| Mode | Menu | Realtime | Both | Realtime or Batch analysis |
| Compute | Pulse | — | Batch | Trigger batch computation |
| Auto Compute | Toggle | On | Batch | Recompute when input changes |
| FFT Size | Menu | 2048 | Batch | 512–16384 |
| Hop Size | Int | 1024 | Batch | 64–16384 |
| Window Type | Menu | Blackman-Harris 62 | Batch | Hann / Hamming / Triangular / Blackman-Harris |
| Zero Padding | Menu | None | Batch | None / Half FFT / Full FFT |
| Enable MFCC | Toggle | On | Both | Enable/disable MFCC output channels |
| MFCC Count | Int | 13 | Both | Number of MFCC coefficients (1–20) |
| MFCC Low Freq | Float | 0 Hz | Both | Lower frequency bound for MFCC mel filters |
| MFCC High Freq | Float | 11000 Hz | Both | Upper frequency bound for MFCC mel filters |
| Enable Centroid | Toggle | On | Both | Enable spectral centroid |
| Enable Flux | Toggle | Off | Both | Enable spectral flux |
| Flux Half Rectify | Toggle | Off | Both | Only count energy increases (onset emphasis) |
| Flux Norm | Menu | L2 | Both | L1 or L2 norm for difference computation |
| Enable Rolloff | Toggle | Off | Both | Enable spectral rolloff |
| Rolloff Cutoff | Float | 0.85 | Both | Energy fraction threshold (0.5 = median, 0.85 = standard, 0.95 = brightness) |
| Enable Contrast | Toggle | Off | Both | Enable spectral contrast |
| Contrast Bands | Menu | 6 | Both | Number of octave sub-bands (4 / 6 / 8) |
| Enable HFC | Toggle | On | Both | Enable high-frequency content |
| HFC Type | Menu | Masri | Both | Masri / Jensen / Brossier — different HFC formulations |
| Enable Complexity | Toggle | On | Both | Enable spectral complexity |
| Complexity Threshold | Float | 0.005 | Both | Minimum peak magnitude to count (0–0.1) |
| Enable Mel Bands | Toggle | On | Both | Enable mel band output channels |
| Mel Bands Count | Menu | 40 | Both | 24 / 40 / 60 / 80 / 128 |
| Mel Low Freq | Float | 0 Hz | Both | Lower frequency bound for mel filters |
| Mel High Freq | Float | 22050 Hz | Both | Upper frequency bound for mel filters |
| Mel Freq Names | Toggle | On | Both | Include frequency ranges in channel names |
| Log Mel (dB Scale) | Toggle | Off | Both | Convert mel band output to dB scale |

**MFCC Frequency Bounds** — The default 0–11000 Hz covers the full speech/music range. For voice-only analysis, narrow to 80–3400 Hz to exclude sub-bass and high-frequency noise. For full-band analysis, set High Freq to the Nyquist (sampleRate/2).

**HFC Type** — Masri weights by energy×frequency (default), Jensen by amplitude×frequency² (stronger high-frequency emphasis), Brossier by amplitude×frequency (linear). Jensen and Brossier respond more aggressively to transients in the upper spectrum.

### Essentia Tonal

| Parameter | Type | Default | Mode | Description |
|---|---|---|---|---|
| Mode | Menu | Realtime | Both | Realtime or Batch analysis |
| Compute | Pulse | — | Batch | Trigger batch computation |
| Auto Compute | Toggle | On | Batch | Recompute when input changes |
| FFT Size | Menu | 4096 | Batch | 512–16384 (4096 for tonal frequency resolution) |
| Hop Size | Int | 2048 | Batch | 64–16384 |
| Window Type | Menu | Blackman-Harris 62 | Batch | Hann / Hamming / Triangular / Blackman-Harris |
| Zero Padding | Menu | None | Batch | None / Half FFT / Full FFT |
| Pitch Algorithm | Menu | YinFFT | Both | YinFFT / YinProbabilistic |
| HPCP Size | Menu | 12 | Both | 12 / 24 / 36 bins |
| Enable Pitch | Toggle | On | Both | Enable pitch detection |
| Pitch Min Freq | Float | 20 Hz | Both | Minimum detectable frequency (constrain to instrument range) |
| Pitch Max Freq | Float | 22050 Hz | Both | Maximum detectable frequency |
| Pitch Tolerance | Float | 1.0 | Both | Peak detection strictness (lower = fewer octave errors, more unvoiced frames) |
| Enable HPCP | Toggle | On | Both | Enable chroma output |
| HPCP Harmonics | Int | 0 | Both | Harmonic contributions (0 = fundamental only, 3–5 for harmonic instruments) |
| Reference Freq | Float | 440 Hz | Both | Tuning reference (415 = Baroque, 432 = alternative, 440 = standard) |
| HPCP Non-Linear | Toggle | Off | Both | Apply peak-sharpening post-processing |
| HPCP Normalized | Menu | Unit Max | Both | Unit Max / Unit Sum / None |
| Enable Key | Toggle | On | Both | Enable key detection |
| Key Frames | Int | 8 | RT | HPCP frames to average for key detection (1–300) |
| Key Mode | Menu | Global | Batch | Global (whole file) or Windowed key detection |
| Key Window Size | Int | 8 | Batch | Frames to average for windowed key (1–300) |
| Key Profile | Menu | Temperley | Both | Temperley / Bgate / Krumhansl / EDMA / Diatonic / Gomez |
| Peak Threshold | Float | 0.00001 | Both | Minimum spectral peak magnitude — filters noise-floor peaks |
| Peak Max Freq | Float | 3500 Hz | Both | Upper frequency limit for spectral peak detection |
| Enable Dissonance | Toggle | On | Both | Enable dissonance output |
| Enable Inharmonicity | Toggle | On | Both | Enable inharmonicity output |
| Musical Labels | Toggle | On | Both | Use note names (A through G#) instead of indices for HPCP channels |
| Enable Pitch Note | Toggle | Off | Both | Output pitch-to-note-class channel |
| Smoothing | Float | 0.5 | RT | EMA smoothing coefficient (0 = none, 1 = maximum) |

**Key Profile** — Different profiles are tuned for different genres. Bgate (default) works well for polyphonic pop/rock. Temperley and Krumhansl are classical music research standards. EDMA is designed for electronic/dance music. Diatonic is the simplest model. Gomez is optimized for guitar-heavy material.

**Pitch Frequency Range** — Constraining to instrument-appropriate bands eliminates octave errors. Common ranges: guitar 80–1200 Hz, voice 80–800 Hz, bass 30–300 Hz.

**HPCP Harmonics** — When set to 0 (default), only the fundamental contributes to chroma. Setting to 3–5 makes HPCP more robust for harmonic instruments (piano, guitar, voice) where overtones reinforce the pitch class.

**Peak Threshold** — Gates noise peaks from polluting all downstream tonal algorithms. Increase from 0 when working with noisy signals to improve HPCP, Key, Dissonance, and Inharmonicity accuracy.

### Essentia Rhythm

| Parameter | Type | Default | Mode | Description |
|---|---|---|---|---|
| Mode | Menu | Realtime | Both | Realtime or Batch analysis |
| Compute | Pulse | — | Batch | Trigger batch computation |
| Auto Compute | Toggle | On | Batch | Recompute when input changes |
| FFT Size | Menu | 2048 | Batch | 512–16384 |
| Hop Size | Int | 256 | Batch | 64–16384 |
| Window Type | Menu | Blackman-Harris 62 | Batch | Hann / Hamming / Triangular / Blackman-Harris |
| Zero Padding | Menu | None | Batch | None / Half FFT / Full FFT |
| Rhythm Method | Menu | Degara | Batch | Degara / Multi-Feature — RhythmExtractor2013 method |
| Onset Method | Menu | Complex | Both | HFC / Complex / Flux / Mel Flux / RMS / SuperFlux |
| Onset Sensitivity | Float | 0.5 | Both | 0.0 (rare triggers) – 1.0 (frequent). Maps to Onsets alpha in batch mode |
| BPM Min / Max | Int | 60 / 180 | Both | BPM search range (clamped to [40,180] / [60,250] internally) |

**Onset Method** — Complex (default) uses both magnitude and phase for the most accurate general-purpose detection. HFC emphasizes high-frequency transients, good for percussive material. Flux measures overall spectral change. Mel Flux applies mel-weighted spectral difference — more robust for harmonic/melodic content. RMS uses simple energy change — fast and reliable for broadband signals. SuperFlux uses TriangularBands + SuperFluxNovelty for music with soft/gradual onsets.

**Beat Detection** — Realtime: TempoTapDegara runs periodically (~1.5 s) on accumulated onset detection history. BPM is derived from median tick intervals; beat phase is anchored to tick positions for audio-synchronized animation. Batch: RhythmExtractor2013 (auto-resampled to 44100 Hz) with autocorrelation fallback when the extractor fails.

**Best Quality Settings** — Onset Method = Complex, Rhythm Method = Degara, FFT 2048, Hop 256 (all defaults). For best results, set the narrowest BPM range that covers your material (e.g., 100–140 for house, 80–160 for pop). For the realtime path, set the upstream SpectrumCHOP to FFT 2048 / Hop 256 for matching temporal resolution. Use SuperFlux for music with soft or gradual onsets.

### Essentia Loudness

| Parameter | Type | Default | Mode | Description |
|---|---|---|---|---|
| Mode | Menu | Realtime | Both | Realtime or Batch analysis |
| Compute | Pulse | — | Batch | Trigger batch computation |
| Auto Compute | Toggle | On | Batch | Recompute when input changes |
| Frame Size | Menu | 1024 | Both | 512 / 1024 / 2048 |
| Gate Threshold | Float | -70 dB | Both | Absolute gate for EBU R128 integration |
| Normalize | Toggle | Off | Both | Map dB outputs to 0–1 range |
| dB Floor | Float | -60 dB | Both | Lower bound for normalization (enabled when Normalize is on) |
| dB Ceiling | Float | 0 dB | Both | Upper bound for normalization (enabled when Normalize is on) |
| ZCR Threshold | Float | 0 | Both | Dead-band around zero for ZCR (0–0.1). Increase to filter noise-floor chatter on quiet signals |

# Build from source

## Prerequisites

### 1. Essentia Static Library

Build `essentia.lib` (MSVC x64 static) and place headers + lib under:

```
src/vendor/essentia/
  ├── include/essentia/   # Essentia headers
  └── lib/essentia.lib    # Static library
```

See [docs/building-essentia.md](docs/building-essentia.md) for detailed build instructions.

### 2. TouchDesigner SDK Headers

Copy from your TouchDesigner installation:

```
C:/Program Files/Derivative/TouchDesigner/Samples/CPlusPlus/
  CHOP_CPlusPlusBase.h
  CPlusPlus_Common.h
```

Into `src/` alongside the plugin source files.

## Build

```bash
cd src
cmake -B build -G "Visual Studio 17 2022" -A x64
cmake --build build --config Release
```

Produces 5 DLLs in `src/build/Release/`.

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
