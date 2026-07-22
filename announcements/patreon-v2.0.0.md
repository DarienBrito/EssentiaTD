<!-- POST IMAGE: assets/final/essentiatd-release-1920x1080.png  ·  optional inline: essentiatd-icon.png / operators-diagram.png -->

**Title:** EssentiaTD v2.0.0 is official: out of beta, verified end to end, still free

---

Hey everyone,

EssentiaTD v2.0.0 is now an official, stable release. The beta phase is over, and I want to tell you both what v2.0 is and what "official" actually means here, because I didn't just remove a suffix from the version number.

If you're new: EssentiaTD is a suite of five C++ CHOP plugins that bring professional audio analysis into TouchDesigner, powered by the Essentia library from the Music Technology Group at Universitat Pompeu Fabra. And it's free.

**What "out of beta" means**

Before tagging the final release I ran the whole suite through a verification campaign: 202 automated checks covering every operator in both Realtime and Batch mode. Not against my own expectations, but against independent references:

- Loudness against ffmpeg's EBU R128 meter (matches to a tenth of a dB, on synthetic signals and real tracks)
- BPM against ground-truth click tracks at 44.1, 48 and 96 kHz (exact to two decimals on all three sample-rate paths)
- Key detection against an independent implementation (correctly separates A major from A minor triads, and agrees on real music)
- Spectral analysis against numpy (centroid matches to a few hundredths of a Hz)
- MFCCs against librosa (0.997 correlation on real music)

The campaign also included an adversarial audit pass that went hunting for blind spots in my own tests, which led to another 30 checks covering things like 96 kHz auto-FFT sizing, all six key profiles, and alternative HPCP resolutions. Everything passed. One real bug was found and fixed along the way (details below), and the documentation was corrected in a few places where it had drifted from the shipped operators. That's the release you're getting: not "seems fine", but measured.

**The big change in v2.0: raw audio into everything**

This is a breaking change, and a big simplification. All five operators take raw audio directly, in both Realtime and Batch mode. Each analyzer runs its own FFT internally, at the resolution that analysis actually needs.

In v1.x, realtime analysis required routing audio through the Spectrum CHOP first. It worked, but the most common mistake new users made was wiring audio straight into an analyzer, which silently produced wrong values. And one shared FFT meant every analyzer got the same resolution whether it suited the task or not (key detection wants a much finer spectrum than onset detection does).

Both problems are gone:

- Wire Audio File In, Audio Device In, or any raw audio CHOP straight into Spectral, Tonal, Rhythm, and Loudness. That's the whole setup.
- Each analyzer picks the FFT resolution appropriate for its job. Tonal sizes its FFT automatically from your sample rate, so key detection is reliable out of the box (verified up to 96 kHz).
- Switching an operator between Realtime and Batch needs no rewiring.
- The Spectrum CHOP is still there as a pure output operator: magnitude and phase spectra for visualization, GLSL, and custom processing.

**Migrating from v1.x**

If an old network feeds Spectrum output into other Essentia CHOPs, those operators show a clear migration error explaining what to change: wire the raw audio into each analyzer instead. It's a one-minute fix per network. Networks that already wired audio directly (the old "mistake") now simply produce correct output.

If you grabbed the v2.0.0-beta from GitHub: the final release contains one fix on top of it. Batch mode now honors the Mel Freq Names toggle, so mel band channel names match between Realtime and Batch (the verification campaign caught that batch always used plain names, which could break channel selects when flipping modes). Just run the new installer over the old one.

**Important fix carried from the v2.0 work: batch BPM on non-44.1 kHz files**

If you ever ran Batch BPM analysis on 48 kHz material in v1.x and the number felt off, it was. An internal resampler was silently unavailable, so BPM was computed at the wrong rate for anything that wasn't 44.1 kHz. Fixed, and now verified: the same rhythm analyzed natively at 44.1 kHz, resampled from 48 kHz, and resampled from 96 kHz reports the same correct BPM to two decimals.

**Smarter warnings**

The operators tell you when a setting will quietly hurt your results: a resolution warning when the FFT is too coarse for reliable key detection (both modes), and a realtime coverage warning when the analysis window is shorter than one frame's worth of incoming audio. Warnings persist correctly on cached batch results.

**Tuned defaults**

All analysis defaults match Essentia's own recommendations (the configuration of their reference MusicExtractor pipeline): Blackman-Harris 62 windowing, per-operator FFT sizes, automatic FFT sizing for Tonal. Fresh operators do the right thing without touching a parameter.

**Performance**

All five operators running simultaneously in realtime cost about half a millisecond per frame at 60 fps on my Windows reference machine, with the optional spectral features enabled. Timbre, pitch, key, beats, loudness and the raw spectrum, all at once, for roughly 3% of the frame budget.

**How to install**

1. Download the installer for your platform:
   - Windows: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD-Setup.exe
   - macOS: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD.pkg
2. Run it. It installs the plugins into the right TouchDesigner folder for you (no admin rights needed). Close TouchDesigner first; the installer will remind you if it's open.
3. Restart TouchDesigner. Press Tab in your network and search "Essentia" to find the operators.

The installers aren't code-signed yet, so Windows SmartScreen or macOS Gatekeeper may warn you on first open. It's safe to proceed (More info, then Run anyway on Windows; Open Anyway under Privacy & Security on macOS). Full notes: https://github.com/DarienBrito/EssentiaTD#install

**Docs**

The README and the interactive guide were fully rewritten for the v2.0 input model, and corrected once more during verification so they describe exactly what ships. The guide covers every operator, parameter, and output channel, with recommended settings for key detection, pitch tracking, and live visuals: https://darienbrito.github.io/EssentiaTD/

GitHub (follow for release notifications): https://github.com/DarienBrito/EssentiaTD

**What you can do with it**

- Map spectral centroid to color temperature for brightness-reactive palettes
- Trigger particle bursts or camera cuts from onset detection
- Drive animation speed with BPM and sync motion to beat phase
- Build chord visualizers from the HPCP chroma channels (named note_a through note_gs)
- Create mood-adaptive scenes with key detection (the major_minor channel is made for palette switching)
- Monitor show levels with EBU R128 loudness metering

**Open source**

The project remains open source under AGPL-3.0, with full build instructions and CMake setup in the repo.

This release exists because people used the earlier versions and told me where the friction was. The whole input redesign came out of watching what users naturally tried to do, and the verification campaign exists because I wanted "stable" to mean something before putting it on the label. So thank you, genuinely. If v2.0 breaks something in your network beyond the documented migration, open an issue on GitHub or drop a comment here.

Thanks for the support. It's what makes projects like this possible.

All the best,
Darien
