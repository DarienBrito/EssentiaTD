Patch release. It improves what happens when an operator cannot build its analysis chain. Nothing changes for a network that already works: no new parameters, no channel changes, no algorithm changes.

**Errors no longer vanish after one frame.** If Tonal, Loudness or Rhythm failed to configure, for instance because the input sample rate does not suit the current settings, the error appeared for a single frame and then cleared itself. The operator looked healthy while producing nothing. The message now stays until the settings work again, and the operator picks up on its own as soon as they do.

**Spectral now says what actually went wrong.** A failure used to read `Feature config failed: MelBands`. It now carries Essentia's own explanation, which usually names both the limit you hit and the way out, such as the FFT size being too small for the number of mel bands requested.

Also in this release: Tonal and Loudness retried the failing setup on every single frame, which is fixed, and two operators could read past the end of a buffer while in that state.

**Installers (recommended):** `EssentiaTD-Setup.exe` (Windows) or `EssentiaTD.pkg` (macOS). The zips are for manual installs.
See the [install guide](https://github.com/DarienBrito/EssentiaTD#install) for SmartScreen and Gatekeeper notes.
