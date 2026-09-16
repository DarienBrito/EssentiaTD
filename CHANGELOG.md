# Changelog

## [2.0.3] - 2026-09-16

### Fixed
- **Realtime tempo clock (Rhythm)**: the onset detection function was timed by the cook rate TouchDesigner reports, not by the audio each cook carried. With frames dropping, the timeslice lengthens while the reported rate stays nominal, so tempo read high by the drop ratio (120 BPM track reported 144 BPM at a sustained 50 fps). BPM, beat phase and tick timing now come from the audio.
- **Silence handling (Rhythm)**: gated frames were still pushed into the tracker history, so after ~8.5 s of quiet the tracker normalized its own noise floor, and exact silence pinned BPM to Max BPM at confidence 0.95. Silence now holds the last tempo at confidence 0, and a cook whose input did not advance no longer advances the beat clock.
- **Batch parameters did nothing until Compute** (Spectral, Tonal, Rhythm, Loudness): only the audio invalidated a cached result, so FFT size, hop size, mel bands, key profile and the feature toggles were silently ignored. Each operator now compares live parameters against the ones its result was computed with and warns.
- **Batch result lifecycle**: the cook that collected a result carried the previous result's frame count, sample rate, channel count and names; Tonal and Loudness could latch an empty result and declare a 0 sample CHOP; a Compute pulse pressed during a run was dropped; a Batch to Realtime to Batch trip could label cached rows with realtime channel names.
- **Spectral flux first frame**: a rebuilt chain has no previous spectrum, so frame 0 returned the spectrum's L2 norm (0.61 measured against 0.0001 steady) instead of 0, in both modes. With every feature off, Spectral also declared a channel it never wrote; it is now zeroed and warned about. Contrast channel names were read from a member written later in the cook.
- **Tonal `pitch_note`**: quantized the smoothed pitch, walking through intermediate semitones for two or three frames on every note change and disagreeing with batch.
- **Realtime config failures scoped per feature (Tonal)**: one rejected parameter aborted the whole chain, so a zero Pitch Min Freq took key, chroma, dissonance and inharmonicity down even with pitch disabled. Each algorithm is now guarded on its own, and an operator whose enabled features are all dead reports the error instead of emitting zeros.
- **Cross-thread logger race**: Essentia's logger is one unguarded structure shared by every thread in a plugin, so a batch worker could corrupt it during a realtime cook. Warning and info logging is disabled at init; it only ever reached stderr, which TouchDesigner does not surface.

### Changed
- **Tonal Smoothing** range stops at 0.99. At 1.0 the smoothing coefficient is zero and every smoothed channel froze.
- **Tonal HPCP Size, Reference Frequency and Musical Labels** stay enabled when HPCP output is off, because key detection and pitch note naming still use them.

## [2.0.2] - 2026-08-12

### Fixed
- **Config failures stayed visible** (Tonal, Loudness, Rhythm): a failure to build the analysis chain appeared for one frame and then cleared itself, leaving the operator looking healthy while producing nothing. The message now persists until the settings work and clears on its own when they do.
- **Spectral reports what failed**: a failure carries Essentia's own explanation, which usually names the limit and the way out, instead of only the name of the feature.
- Tonal and Loudness retried the failing setup every frame, and two operators could read past the end of a buffer while in that state.

## [2.0.1] - 2026-07-25

### Fixed
- **Realtime outputs anchored at sample index 0**: they sat at an ever-advancing index, so any operator that unions sample ranges (Merge, Math with two inputs, Join) had to span from 0 out to that index, producing hundreds of thousands of samples and stalling the CPU. Each analyzer always output a single sample; only the index was wrong.

## [2.0.0] - 2026-07-22

### Changed
- **Breaking: analyzers take raw audio**. Spectral, Tonal and Rhythm consume audio directly and run their own FFT in both modes. A Spectrum CHOP wired into an analyzer now raises a migration error in both modes. A shared spectrum forced one FFT size onto operators with conflicting needs: Tonal needs 4096 to resolve semitones, while feeding Rhythm 4096 instead of 1024 cut onset F1 from 0.47 to 0.13 on a 263 onset reference.
- **Five unified plugins** with a Mode parameter (Realtime or Batch), replacing the previous nine plugin layout. FFT Size and Window Type live on each operator's Analysis page.

### Fixed
- **Batch BPM on non 44.1 kHz files**: `RhythmExtractor2013` requires 44100 Hz and the internal resampler never built, so batch BPM ran at the native rate and came out scaled by roughly rate/44100 (about 9 percent high on 48 kHz). Tonal.wav at 48 kHz corrects from 153.34 to 165.76.
- **Batch mel channel names** followed the Mel Freq Names toggle.

## [1.1.8-beta] - 2026-07-12

### Fixed
- **macOS installer path**: the .pkg installed into `Derivative/TouchDesigner/Plugins`, which is not the folder TouchDesigner scans on macOS. It now installs into `Derivative/TouchDesigner099/Plugins`, so the plugins actually load.

### Changed
- **Tidy install layout**: both installers now place the plugins in an `Essentia` subfolder of the Plugins directory (Windows: `Documents\Derivative\Plugins\Essentia`, macOS: `.../TouchDesigner099/Plugins/Essentia`) instead of loose in the Plugins root. TouchDesigner scans subdirectories, so they still load. Manual zip and build-from-source instructions updated to match.

## [1.1.6-beta] - 2026-07-11

### Added
- **Installers**: releases now include a Windows installer (Inno Setup, per-user, no admin rights) and a macOS .pkg (per-user, no admin password) alongside the zips
  - Windows installer detects a running TouchDesigner (locked DLLs) and cleans up stale plugin copies from older layouts
  - macOS pkg payload carries no quarantine flag, so plugins load without per-bundle Gatekeeper prompts
  - Unversioned asset names (`EssentiaTD-Setup.exe`, `EssentiaTD.pkg`) so the latest-release download links in the README stay stable

## [1.1.0-beta] — 2026-04-05

### Added
- **PCA dimensionality reduction** for SpectralCHOP — reduces high-dimensional spectral features (MFCCs, mel bands, spectral descriptors) to a compact set of principal components
  - Realtime: circular buffer with throttled eigendecomposition, sign-flip correction for temporal coherence
  - Batch: post-processes the full feature matrix with static PCA
  - New parameter page: Enable PCA, Components (2–16), Window Size (128–4096, RT only), Update Rate (1–60 Hz, RT only), Variance output toggle
  - Output channels: `pc0..pcN` and optional `pc_var0..pcN` appended after spectral channels
- **Cross-platform CI** via GitHub Actions — builds Windows DLLs and macOS dylibs automatically on every push
  - Essentia built from source with aggressive caching (~20s on cache hit)
  - Tagged pushes (`v*`) create GitHub Releases with both platform zips
- **macOS support** — CMake build system, install instructions, and documentation updated for macOS (arm64)

### Changed
- README updated with macOS install and build instructions
- Documentation (`building-essentia.md`) covers both Windows and macOS builds

### Fixed
- Minor rhythm parameter visibility fixes

## [1.0.0-beta]

Initial beta release with 5 unified CHOP plugins:
- **Essentia Spectrum** — FFT magnitude + phase
- **Essentia Spectral** — MFCC, centroid, flux, rolloff, contrast, HFC, complexity, mel bands
- **Essentia Tonal** — pitch (YinFFT), HPCP chroma, key/scale, dissonance, inharmonicity
- **Essentia Rhythm** — onset detection, BPM (TempoTapDegara), beat phase/confidence
- **Essentia Loudness** — EBU R128 loudness, RMS, zero-crossing rate

Each operator supports Realtime and Batch analysis modes.
