<!-- ATTACH when posting (do NOT paste this line): assets/final/essentiatd-release-1920x1080.png  ·  optional avatar: essentiatd-icon.png -->

🎛️ **EssentiaTD v2.0.0 is officially out. No more beta.**

v2.0 has graduated to a stable release, and I didn't just flip the label. Before tagging it I put every operator through a full verification campaign: 202 automated checks across both Realtime and Batch modes, measured against independent references (ffmpeg's EBU R128 meter, librosa, numpy, and pure test signals like sines, triads and click tracks). Everything passed. BPM lands exactly on ground-truth click tracks at 44.1, 48 and 96 kHz, key detection nails major vs minor, and loudness matches ffmpeg to a tenth of a dB.

**What v2.0 changes (breaking, and worth it)**
Audio goes straight into every operator now. All five take raw audio directly in both modes, and each runs its own FFT internally at the resolution its analysis needs.

- Wire Audio File In, Audio Device In, or any audio CHOP straight into Spectral, Tonal, Rhythm, Loudness. Done.
- Flipping between Realtime and Batch needs no rewiring.
- Fun fact: wiring audio directly was the most common "mistake" in v1.x, and it silently gave wrong values. Now it's the correct way and just works.

**Migrating from v1.x**
Old networks that fed Spectrum output into other Essentia CHOPs show a clear migration error telling you exactly what to rewire. The Spectrum CHOP stays as an output operator (magnitude + phase for your own GLSL and viz work).

**Also in this release**
- Batch BPM fixed for non-44.1 kHz files (a resampler was silently unavailable in v1.x).
- The verification pass caught one more: mel band channel names now match between Realtime and Batch when Mel Freq Names is on.
- Smarter warnings when an FFT is too coarse for key detection or a window too short for the incoming audio.
- Defaults match Essentia's own MusicExtractor recommendations; Tonal sizes its FFT automatically.

**Performance**
All five operators running together in realtime: about half a millisecond per frame at 60 fps. A full audio-analysis rig for ~3% of your frame budget.

**Download (always points to the latest)**
Windows: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD-Setup.exe
macOS: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD.pkg

Installers aren't code-signed yet, so SmartScreen or Gatekeeper may warn on first open. Install notes: https://github.com/DarienBrito/EssentiaTD#install

Interactive guide, fully rewritten for v2.0: https://darienbrito.github.io/EssentiaTD/

Free and open source, as always. If anything feels off, post here. And show me what you patch with it 👀
