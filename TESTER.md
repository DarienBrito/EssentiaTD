# EssentiaTD experimental: drum triggers on the Rhythm CHOP

Thanks for testing this. It's an experimental build, so please keep it out of anything you ship.

## What's new

The Rhythm CHOP has a new **Drums** page. Turn on **Enable Drums** and six channels appear after the usual rhythm channels:

| channel | what it is |
|---|---|
| `kick`, `snare`, `hat` | 1 for one cook when a hit is detected, otherwise 0 |
| `kick_level`, `snare_level`, `hat_level` | band energy, 0 to 1 relative to its own recent peak (adapts over ~15-20 seconds) |

It works in both Realtime and Batch mode. The existing rhythm channels (onset, bpm, beat...) are unchanged.

Parameters:
- **Drum Window** (1024 / 2048, default 2048): the drum detector's own FFT size, separate from Window Size. 2048 catches kicks better. 1024 reacts about 10 ms faster.
- **Kick / Snare / Hat Sensitivity** (0 to 1, default 0.5): higher fires more often. At 0, only a hit louder than anything recent fires.

## How it works (so you know what to expect)

It splits the spectrum into three bands (kick 30-150 Hz, snare 200-2500 Hz, hats 10-20 kHz) and fires when a band's energy jumps relative to its own recent peaks. It doesn't know what a drum *sounds* like, so:
- A bass line can fire `kick` and a guitar or vocal can fire `snare` on a full mix. On drum-heavy material it behaves much better.
- Snare is the hardest class, just like in every drum transcription system.
- The first second or so after audio starts can fire a stray hit before the detector has heard anything loud.
- It adapts to the loudest recent hits. If you switch to a much quieter source, give it up to half a minute before quiet kicks start firing (or toggle Enable Drums off and on to reset it).
- In Batch mode, changing a drum setting needs a new Compute, like any other batch parameter.
- Nothing fires on near-silence, which also means very quiet input (around 30-40 dB below a normally mastered track) starts losing quiet hats. Bring the level up if hats go missing.

Measured on a held-out set of drum recordings (F-measure, 1 = perfect): kick 0.84, snare 0.62, hat 0.81 in Realtime at the default settings.

## Install

Replace only the Rhythm plugin, keep a backup of yours:
- **Windows**: close TouchDesigner, rename `Documents\Derivative\Plugins\Essentia\EssentiaRhythmCHOP.dll` to `EssentiaRhythmCHOP.dll.bak`, copy the new one in.
- **macOS**: close TouchDesigner, move `~/Library/Application Support/Derivative/TouchDesigner099/Plugins/Essentia/EssentiaRhythmCHOP.plugin` somewhere safe, copy the new one in.

To go back, delete the new file and restore the backup.

## What I'd love to hear

- What material you used it on (electronic, live band, DJ set...) and whether the triggers felt right.
- Misfires: which class fired when it shouldn't have, and on what sound.
- Misses: which hits it skipped.
- Drum Window 1024 vs 2048: which one felt better for your use.
- Your platform and TouchDesigner build.
