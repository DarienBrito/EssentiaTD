Minor release with one new feature: drum triggers on the Rhythm CHOP. Nothing existing changes: the batch release matrix (24 configurations across all five operators) is bit-identical to 2.0.3 on every channel, and Rhythm's own six channels are bit-identical with drums on or off.

**Drums page on Rhythm.** Turn on **Enable Drums** and six channels appear after the usual rhythm channels, in both Realtime and Batch:

| channel | band | what it is |
|---|---|---|
| `low`, `mid`, `high` | 30-150 Hz, 200-2500 Hz, 10-20 kHz | band energy, 0 to 1 relative to its own recent peak |
| `kick`, `perc`, `hat` | the same three bands | 1 for one cook when a hit is detected, otherwise 0 |

The names follow the palette's audioAnalysis, so `low`, `mid`, `high` and `kick` slot in where you used those. Its `snare` is `perc` here, because the mid band fires on snares, claps, rims and toms alike.

Parameters: **Drum Window** (1024 or 2048, default 2048) is the detector's own FFT size; 2048 catches kicks better, 1024 reacts up to 10 ms sooner. **Kick / Perc / Hat Sensitivity** (0 to 1, default 0.5): higher fires more often, and at 0 only a hit louder than anything recent fires.

**What to expect.** The detector listens for sudden energy jumps in each band, relative to that band's own recent hits. It does not know what a drum sounds like, so on a full mix a bass line can fire `kick` and a guitar or vocal can fire `perc`. On drum-heavy material it does much better. Measured on the held-out half of a 171-track drum corpus (F-measure, 1 is perfect, hits within 50 ms), Realtime at 60 fps with the defaults:

| | kick | perc | hat |
|---|---|---|---|
| all recordings | 0.835 | 0.632 | 0.813 |
| median latency | 26.7 ms | 23.1 ms | 19.3 ms |

`perc` is the weak one, as snare is in every drum transcription system: 0.443 on full band mixes. In Batch mode, with the whole file available, scores are 0.882 / 0.645 / 0.825 at Hop Size 512, and within 0.002 of that at Hop Size 1024 and 2048 (the detector always steps at 512 or finer).

It adapts to the loudest recent hits, so after switching to a much quieter source give it up to half a minute, or toggle Enable Drums to reset it. Turning drums on costs about 0.05 ms per cook.

**Installers (recommended):** `EssentiaTD-Setup.exe` (Windows) or `EssentiaTD.pkg` (macOS). The zips are for manual installs.
See the [install guide](https://github.com/DarienBrito/EssentiaTD#install) for SmartScreen and Gatekeeper notes.
