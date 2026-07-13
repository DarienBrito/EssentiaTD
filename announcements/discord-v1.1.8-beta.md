<!-- ATTACH when posting (do NOT paste this line): assets/final/essentiatd-release-1920x1080.png  ·  optional avatar: essentiatd-icon.png -->

🎧 **EssentiaTD v1.1.8-beta is out**

Quick update on the free audio-analysis suite. This round is mostly down to you all: a bunch of these fixes came straight out of bug reports from people testing the betas. Genuinely appreciate it 🙏

**What's new**
- **One-click installers.** No more copying files by hand. Grab `EssentiaTD-Setup.exe` on Windows or `EssentiaTD.pkg` on macOS, run it, restart TD, done.
- **Universal macOS build.** Runs native on both Apple Silicon (M-series) and Intel Macs.
- **Loudness fix.** The intermittent "Sample rate is zero" error on the Loudness CHOP is gone.
- **Spectral accuracy fix.** Batch and realtime now agree. A sine-wave test caught a centroid bug (and a few friends), all sorted.
- **General hardening.** A full pass over the DSP internals (bounds, gating, buffers) from a code audit.

**Download (always points to the latest)**
Windows: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD-Setup.exe
macOS: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD.pkg

Heads up: the installers aren't code-signed yet, so Windows SmartScreen or macOS Gatekeeper may warn on first open. Install notes here: https://github.com/DarienBrito/EssentiaTD#install

New to it, or want a refresher? The interactive guide walks through every operator with examples: https://darienbrito.github.io/EssentiaTD/

Free as always. If you make something with it, drop it in the channel, I'd love to see it 👀
