<!-- POST IMAGE: assets/final/essentiatd-release-1920x1080.png  ·  optional inline: essentiatd-icon.png / operators-diagram.png -->

**Title:** EssentiaTD v1.1.8-beta: one-click installers, macOS support, and fixes from your testing (free)

---

Hey everyone,

A new version of EssentiaTD is out, and this one is a big quality-of-life jump. If you're new here: EssentiaTD is a suite of five C++ CHOP plugins that bring professional audio analysis into TouchDesigner, powered by the Essentia library from the Music Technology Group at Universitat Pompeu Fabra. And it's free.

**What's new in this release**

- **One-click installers.** No more copying DLLs by hand. There's now a proper Windows installer (EssentiaTD-Setup.exe) and a macOS package (EssentiaTD.pkg). Run it, restart TouchDesigner, done.
- **macOS support, universal.** The suite now runs natively on both Apple Silicon (M-series) and Intel Macs, right alongside Windows.
- **Fixes straight from your reports.** Beta testers caught real bugs and they're fixed: the intermittent "Sample rate is zero" error on the Loudness CHOP is gone, and Spectral analysis now gives matching results in Realtime and Batch (a sine-wave test surfaced a centroid bug, now corrected).
- **Hardening pass.** A full audit of the DSP internals (bounds, gating, buffers) for more reliable output across the board.

**What is it?**

EssentiaTD gives you access to the same audio analysis algorithms used in music information retrieval research: MFCCs, spectral descriptors, pitch detection, key estimation, beat tracking, EBU R128 loudness metering, all running natively inside TouchDesigner at cook rate. No Python overhead, no external processes.

The suite is built around a modular signal flow:

- **Essentia Spectrum** computes the FFT once from your Audio CHOP
- **Essentia Spectral** extracts timbral features (MFCCs, centroid, flux, rolloff, contrast, HFC, complexity, mel bands)
- **Essentia Tonal** handles pitch, HPCP chroma, key/scale detection, dissonance, and inharmonicity
- **Essentia Rhythm** gives you onset detection, BPM estimation, and beat tracking
- **Essentia Loudness** works directly on the audio signal for EBU R128 loudness, RMS, and zero-crossing rate

Every operator has both Realtime (per-frame) and Batch (full-file offline) modes. All algorithm defaults are aligned with Essentia's own MusicExtractor, the reference pipeline used in music information retrieval research. Every feature outputs as a CHOP channel, ready to map, filter, and drive your visuals without any extra wiring.

**How to install**

The easiest way:

1. Download the installer for your platform:
   - Windows: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD-Setup.exe
   - macOS: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD.pkg
2. Run it. It installs the plugins into the right TouchDesigner folder for you (no admin rights needed). Close TouchDesigner first; the installer will remind you if it's open.
3. Restart TouchDesigner. Press Tab in your network and search "Essentia" to find the operators.

Heads up: the installers aren't code-signed yet, so Windows SmartScreen or macOS Gatekeeper may warn you on first open. It's safe to proceed (More info, then Run anyway on Windows; Open Anyway under Privacy & Security on macOS). Full notes are in the install guide: https://github.com/DarienBrito/EssentiaTD#install

Prefer to copy files yourself? The zip downloads and manual steps are on the releases page, and the .toe test patch is attached to this post.

**Where to find it**

You can grab the files attached to this post, but I recommend following the GitHub repository so you're always notified when there's a new release, this is a project in active development.

GitHub: https://github.com/DarienBrito/EssentiaTD

**Quick start**

Interactive guide: https://darienbrito.github.io/EssentiaTD/

The interactive guide covers every operator, parameter, and output channel, with suggested use cases and recommended settings for TouchDesigner, plus an explanation of the signal flow. For a practical setup, open the attached test patch (.toe) and explore.

**Tutorials**

I'll release a short series of tutorials covering how these tools work, but it might take me a little while. For now, the best path is to experiment with the test patch and ask any questions here, or on Discord.

**What you can do with it**

Some ideas to get started:

- Map spectral centroid to color temperature for brightness-reactive palettes
- Use onset detection to trigger particle bursts or camera cuts
- Drive animation speed with BPM estimation and sync to beat phase for smooth rhythmic motion
- Build chord visualizers with the HPCP chroma output (Musical Labels are on by default, giving you named channels like note_a through note_gs)
- Create mood-adaptive scenes using key/scale detection (major = warm, minor = cool)
- Use mel bands as multi-band input for frequency-mapped visual layers
- Monitor show levels with EBU R128 loudness metering

The interactive guide includes recommended settings for common use cases: key detection accuracy, pitch tracking, chord analysis, and responsive live visuals.

**Open source**

The project is open source under AGPL-3.0. If you want to build from source or contribute, the repo includes full build instructions and the CMake setup.

I'd love to hear what you make with it. If you have questions or run into issues, open an issue on GitHub or drop a comment here. It's still in active development, so some things may not fully work as expected yet, and your reports genuinely shape what gets fixed next (this release is proof of that).

Thanks for the support. It's what makes projects like this possible.

All the best,
Darien
