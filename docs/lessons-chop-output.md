# Lessons: CHOP Output Configuration

Hard-won lessons from building the Essentia CHOP Suite. These patterns apply to any C++ CHOP plugin in TouchDesigner.

---

## 1. Static Buffers vs Per-Frame Signals

A CHOP can output two fundamentally different kinds of data:

- **Static indexed buffer** — e.g., 513 spectrum bins, 40 mel bands. The sample indices represent array positions (bin 0, bin 1, ...), not time. The data is rewritten every cook but the indices don't advance.

- **Per-frame signal** — e.g., a single `pitch` value updated every cook frame. The sample index represents time, advancing at the cook rate.

### How to configure each:

**Static buffer** (Spectrum, Core):
```cpp
void getGeneralInfo(CHOP_GeneralInfo* ginfo, ...) {
    ginfo->timeslice       = false;
    ginfo->cookEveryFrame  = true;
    ginfo->inputMatchIndex = -1;   // don't inherit input format
}

bool getOutputInfo(CHOP_OutputInfo* info, ...) {
    info->numChannels = 1;
    info->numSamples  = 513;       // e.g., fftSize/2 + 1
    info->startIndex  = 0;         // KEY: pins buffer at index 0
    return true;
}
```

**Per-frame signal** (Spectral, Tonal, Rhythm, Loudness):
```cpp
void getGeneralInfo(CHOP_GeneralInfo* ginfo, ...) {
    ginfo->timeslice       = false;
    ginfo->cookEveryFrame  = true;
    ginfo->inputMatchIndex = -1;   // don't inherit input format
}

bool getOutputInfo(CHOP_OutputInfo* info, const OP_Inputs* inputs, ...) {
    info->numChannels = 6;
    info->numSamples  = 1;
    info->sampleRate  = static_cast<float>(inputs->getTimeInfo()->rate);
    return true;
}
```

---

## 2. `startIndex = 0` Prevents Timeline Scrolling

**Problem:** A CHOP with 513 samples shows its Start/End advancing every frame, as if the data is scrolling through time.

**Cause:** `CHOP_OutputInfo::startIndex` defaults to whatever TD's timeline position is. For a spectrum buffer, this makes the indices track time instead of staying at 0–512.

**Fix:** Set `info->startIndex = 0` in `getOutputInfo()`. This anchors the buffer so sample indices are always 0 to N-1.

**When to use:** Any CHOP whose samples represent array indices (spectrum bins, mel bands, HPCP bins) rather than time-domain data.

---

## 3. `sampleRate` Must Be Explicitly Set for Per-Frame Outputs

**Problem:** Analysis CHOPs that output 1 sample per cook frame show a sample rate of 44100 (or whatever the audio input's rate is) instead of the expected 60 fps.

**Cause:** Two interacting issues:

1. `inputMatchIndex = 0` tells TD to pre-fill `CHOP_OutputInfo` by matching input 0's format. If input 0 is an audio-rate CHOP (44100 Hz), the output inherits that sample rate.

2. Even with `inputMatchIndex = -1`, the default sample rate is the "component timeline FPS" — but this default doesn't always apply as expected when `getOutputInfo` returns `true`.

**Fix:** Explicitly set the sample rate from the timeline:
```cpp
info->sampleRate = static_cast<float>(inputs->getTimeInfo()->rate);
```

The `OP_TimeInfo::rate` field is the component timeline FPS (typically 60). This is the correct rate for "one value per cook frame" outputs.

**Do NOT set `sampleRate = 0`** — TD needs a valid sample rate. Zero causes downstream errors.

---

## 4. `inputMatchIndex = -1` for Analysis CHOPs

**Problem:** An analysis CHOP connected to an audio-rate input shows 44100 Hz sample rate and audio-style formatting.

**Cause:** `inputMatchIndex = 0` in `getGeneralInfo()` tells TD: "match my output format to input 0." This is useful for pass-through CHOPs but wrong for analysis CHOPs that transform audio into per-frame features.

**Fix:** Set `ginfo->inputMatchIndex = -1` so the CHOP's own `getOutputInfo()` has full control over format.

**Rule of thumb:**
- `inputMatchIndex = 0` — output has the same format as the input (filters, effects)
- `inputMatchIndex = -1` — output has a different format than the input (analysis, feature extraction)

---

## 5. `timeslice = false` for All Analysis CHOPs

**Problem:** A loudness CHOP set to `timeslice = true` produces "Sample rate is zero" errors.

**Cause:** Time-sliced CHOPs interact with TD's timeline differently. When `timeslice = true`, TD controls `numSamples` per cook (the "time slice"), and the sample rate relationship becomes critical. For analysis CHOPs that output per-frame values (not audio), this causes mismatches.

**Fix:** Use `timeslice = false` with `cookEveryFrame = true` and `numSamples = 1`. The CHOP still receives the full audio input every cook — `timeslice` only affects the *output* format, not the input.

**Key insight:** `timeslice` controls how TD manages the output buffer timeline. It does NOT affect whether you can read audio input. A non-time-sliced CHOP with `cookEveryFrame = true` still gets called every frame with full access to all input data.

---

## 6. Essentia: All Algorithm Outputs Must Be Bound

**Problem:** `Key error: FirstToSecondRelativeStrength Output not bound to concrete object`

**Cause:** Essentia's standard-mode algorithms require ALL declared outputs to be bound to variables via `.set()` before calling `compute()`, even if you don't need the value.

**Fix:** Declare a member variable for every output and bind it:
```cpp
myKey->output("key").set(myKeyStr);
myKey->output("scale").set(myScaleStr);
myKey->output("strength").set(myKeyStrength);
myKey->output("firstToSecondRelativeStrength").set(myUnusedFloat);  // must bind
myKey->compute();
```

**How to find all outputs:** Check the [Essentia algorithm reference](https://essentia.upf.edu/algorithms_reference.html) or look at the algorithm's source code for all `declareOutput()` calls.

---

## 7. Guard Against Degenerate Input (Silence)

**Problem:** `Inharmonicity error: fundamental frequency found at 0hz`

**Cause:** During silence or very low signal, `SpectralPeaks` may return peaks at 0 Hz. The `Inharmonicity` algorithm divides by the fundamental frequency and crashes on zero.

**Fix:** Check the first peak frequency before calling the algorithm:
```cpp
if (myPeakFreqs.empty() || myPeakFreqs[0] <= 0.0f) {
    // Skip — no valid fundamental
} else {
    myInharmonicity->compute();
}
```

**General rule:** Always guard Essentia algorithms that depend on spectral peaks against empty/zero-frequency inputs. This includes `Inharmonicity`, `Dissonance`, and `HPCP`.

---

## Summary Table

| Setting | Static Buffer | Per-Frame Signal |
|---|---|---|
| `timeslice` | `false` | `false` |
| `cookEveryFrame` | `true` | `true` |
| `inputMatchIndex` | `-1` | `-1` |
| `numSamples` | N (e.g., 513) | `1` |
| `startIndex` | `0` | (not set) |
| `sampleRate` | (not set, inherits) | `inputs->getTimeInfo()->rate` |
