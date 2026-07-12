# Changelog

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
