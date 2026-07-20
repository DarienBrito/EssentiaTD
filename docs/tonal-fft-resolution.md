# Tonal: FFT resolution and key detection

Why `EssentiaTonalCHOP` warns below 4096, what was measured, and what the warning does
**not** claim. All figures here are measured, not derived.

---

## 1. The mechanism

`EssentiaTonalCHOP` places spectral energy into 12 pitch classes (HPCP) and correlates that
against key profiles. That requires the spectrum to **separate adjacent semitones**.

A semitone at frequency *f* is `f × (2^(1/12) − 1)` ≈ `f × 0.0595` Hz wide. A spectrum with bin
spacing Δ can therefore only separate semitones above `Δ / 0.0595`. Below that, neighbouring
pitches fall in the same bin, HPCP attributes energy to the wrong class, and the key can come
out wrong.

Bin spacing is `sampleRate / fftSize`, so **the requirement is in Hz, not in FFT size**:

| Rate | Min fftSize | → power of 2 | Hz/bin | Window |
|---|---|---|---|---|
| 44100 | 2835 | **4096** | 10.77 | 93 ms |
| 48000 | 3085 | **4096** | 11.72 | 85 ms |
| 88200 | 5669 | **8192** | 10.77 | 93 ms |
| 96000 | 6171 | **8192** | 11.72 | 85 ms |

The warning threshold is bin spacing ≤ **15.56 Hz**, the semitone width at middle C (261.63 Hz).
Middle C was chosen because it sits in the register that carries a piece's harmony; resolving
semitones only *above* middle C is not enough.

**Affects `key` and `note_*` (HPCP) only.** `pitch` (YinFFT), `dissonance` and `inharmonicity`
do not fail this way, so the warning is gated on `Enablehpcp || Enablekey`.

---

## 2. Measured: where key detection actually breaks

**Method.** Three tracks of different material and sample rate. Ground truth from an
independent numpy Krumhansl-Kessler implementation at FFT 8192 (no Essentia involved, so it
cannot inherit the bug under test). Then `EssentiaTonalCHOP` in **batch** mode, `Keymode=global`,
`Keyprofile=temperley`, hop fixed at 50 % overlap, sweeping FFT size. Batch was used
deliberately: it runs its own FFT over the whole file, isolating FFT size from realtime
topology, ring-buffer warm-up and playhead position.

**Ground truth**

| Track | Rate | Key | Margin over runner-up |
|---|---|---|---|
| Tonal (Fantasia in D Minor) | 48 k | D minor | +0.238 (strong) |
| candidate1 | 44.1 k | G minor | +0.148 (moderate) |
| Spectrum | 48 k | A# minor | +0.067 (**weak** — runner-up is A# *major*) |

**Result** (o = matches ground truth)

| Track | 512 | 1024 | 2048 | 4096 | 8192 |
|---|---|---|---|---|---|
| Tonal | x B min | **o D min** | o D min | o D min | o D min |
| candidate1 | x F min | x C min | **o G min** | o G min | o G min |
| Spectrum | x B min | x B min | x F# min | **o A# min** | o A# min |
| **correct** | 0/3 | 1/3 | 2/3 | **3/3** | 3/3 |
| **plugin warns** | yes | yes | yes | no | no |

### What this establishes

- **The warning boundary is empirically right.** 4096 is the smallest size at which all three
  tracks are correct, and that is exactly where the warning stops. The threshold was derived
  from semitone physics before this sweep was run, so the agreement is corroboration, not
  curve-fitting. It also matches the 4096/2048 configuration Essentia's own MusicExtractor uses
  for tonal analysis.
- **The failure is CONTENT-DEPENDENT.** The first adequate size differs per track: Tonal 1024,
  candidate1 2048, Spectrum 4096. So the warning means *"this may be unreliable"*, not
  *"this is wrong"* — material with little low-register content can be fine below 4096.
- **`key_strength` cannot be used to detect the failure.** Spectrum at 1024 reports the WRONG
  key (B minor) at strength **0.608**, while the CORRECT answer at 4096 reports **0.531**. The
  wrong answer is more confident than the right one. Do not gate on strength.

### Realtime: 4096 helps, but 1024 is not catastrophic

Measured A/B, both chains from the same audio in the same pass, `Keyframes=240`, over the
musical body of Tonal.wav (2875 analysis frames):

| upstream fft | reads D minor | key_strength | distinct runs |
|---|---|---|---|
| 1024 | 81.7 % | mean 0.725, sd 0.102 | 23 |
| 4096 | **91.3 %** | mean 0.798, sd 0.072 | **10** |

4096 is worth +9.6 points of correctness and less than half the key changes. But realtime at
1024 still reads D minor most of the time on this track — it is degraded, not broken.

> **Retracted.** An earlier version of this document reported realtime at 1024 producing
> *D# minor 91 % of the time with `key_strength` frozen at 0.614*. That was a **measurement
> artefact, not plugin behaviour**. Nothing pulled the probe chain, so it only cooked when an
> external call happened to poke it: the `Keyframes` accumulator was filling with HPCP frames
> taken from **discontinuous audio positions** (the playhead jumped between sporadic cooks),
> which scrambles the estimate, and the `trailCHOP` recorded held values, manufacturing the
> "frozen" strength. Fixed by forcing a cook every frame with an Execute DAT and recording the
> playhead as a channel so sample → track-time is exact. (Root lesson: an operator nothing
> pulls does not cook — probe chains need a consumer or a forced cook.)

Batch and realtime still do not share a signal path — batch runs its own FFT
(`BatchFrameProcessor`), realtime consumes whatever the upstream `EssentiaSpectrumCHOP`
produces — so a batch result does not automatically validate the realtime path. But on the
evidence here they degrade *similarly*, not dramatically differently.

> **v2.0 note:** the realtime measurements above were taken on the v1.x architecture
> (upstream Spectrum CHOP). In v2.0 realtime runs its own FFT (`RTFrameProcessor`), but the
> analysis maths is unchanged — same Windowing/FFT/CartesianToPolar chain, same bin spacing
> per FFT size — so the resolution conclusions carry over. See §5.

---

## 3. Measured: do NOT share one Spectrum CHOP between Tonal and Rhythm (v1.x — designed away in v2.0, see §5)

Raising the FFT to 4096 for Tonal is not free if that Spectrum CHOP also feeds Rhythm. A longer
FFT window smears transients, which is exactly what onset detection depends on.

**Method.** Two complete chains from the SAME audio in the SAME playback pass, identical Rhythm
parameters, differing ONLY in upstream `Fftsize`:

    audiofilein -> math_mono(avg) -+-> EssentiaSpectrum(1024) -> EssentiaRhythm(RT) -.
                                   `-> EssentiaSpectrum(4096) -> EssentiaRhythm(RT) -+-> trail

Ground truth = batch Rhythm on the same file at Rhythm's own 2048/512 → **263 onsets** (matches
this track's long-documented onset count). Onset times are taken from a recorded **playhead
channel**, so trail-index → track-time is exact regardless of TD's cook rate. Match window 50 ms
(a 60 fps frame is 16.7 ms).

**Result** — candidate1.wav, full 59.9 s, 263 ground-truth onsets

| upstream fft | onsets | TP | FP | FN | precision | recall | **F1** | median err |
|---|---|---|---|---|---|---|---|---|
| **1024** | 146 | 97 | 49 | 166 | 0.664 | 0.369 | **0.474** | 40.7 ms |
| **4096** | 114 | 25 | 89 | 238 | 0.219 | 0.095 | **0.133** | 29.2 ms |

**F1 drops by 0.342 — recall −0.274, precision −0.445.** The mechanism shows up directly in the
onset detection function: crest factor falls **12.30 → 8.62** and mean |slope| **0.134 → 0.110**,
i.e. the transients the detector keys on are measurably blunted.

### Consequence for the signal-flow diagram

The README topology shares one Spectrum CHOP across Spectral, Tonal and Rhythm. **That topology
cannot serve Tonal and Rhythm at once**: Tonal needs ≥4096, Rhythm needs a short window. Give
Tonal its **own** Spectrum CHOP at 4096 and leave Rhythm's at the default. The operators' own
batch defaults already encode this tension — Rhythm 2048/512, Spectral 2048/1024, Tonal 4096/2048.

**Caveat on the absolute numbers:** RT and batch onset detection are different code paths with
different thresholding, so F1 ≈ 0.47 is not a verdict on realtime Rhythm in general. Only the
**relative** comparison is controlled here, and that is what the conclusion rests on. BPM was not
cleanly testable on this rig (both configs sat 10-24 BPM off with high variance; this probe was
not tuned like the production Rhythm network), so no BPM claim is made.

---

## 4. Limitations of the check

- It measures bin **spacing**, and zero-padding narrows spacing without improving true
  resolution. A heavily zero-padded spectrum can pass the check while still being too coarse.
  The common failure (small fftSize, no padding) is caught.
- The middle-C reference is a judgement call, not a measured boundary. It is conservative: at
  2048 two of three test tracks were still correct.
- Three tracks is a small sample. The direction of the effect is unambiguous; the exact
  per-track boundary is not generalisable.

---

## 5. v2.0 addendum: per-operator FFT

v2.0 removes the shared Spectrum CHOP from analysis chains: every analyzer takes raw audio in
both modes and runs its own FFT. Consequences for this document:

- **The §3 conflict no longer exists by construction.** Tonal resolves its window (default
  **auto**: 4096 at 44.1/48 kHz, 8192 at 88.2/96 kHz, straight from the §1 table) while Rhythm
  keeps its short realtime window (default 1024) on the same audio wire. No topology decision
  is left to the user.
- **The §3 measurement stands as the empirical rationale** for the redesign, not as a current
  operating constraint.
- **Conversion parity was measured, not assumed.** The converted Rhythm at window 1024 was
  A/B'd against the v1.x spectrum-fed build on the same track and ground truth (262-onset
  reference, Tonal.wav 48 kHz): onset F1 identical to three decimals (delta +0.000). A window
  sweep on the converted operator re-confirmed 1024 as the best realtime default
  (F1 0.461 vs 0.377 at 512, 0.409 at 2048, 0.028 at 4096, 60 fps).
- **The too-coarse warning survives unchanged** and is now computed from the operator's own
  true window (never zero-padded bins) in both modes.
- **New realtime coverage warning:** when one cook delivers more samples than the analysis
  window (`window < sampleRate / fps`), the excess is never analyzed and the operator says so.
  Measured impact and mechanism: 30 fps collapses onset F1 below 0.16 at every window
  (skipped audio at 512/1024 plus a 30 Hz detection function too coarse for onsets).
