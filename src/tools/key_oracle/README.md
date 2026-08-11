# key_oracle

Offline harness for key-detection accuracy. Console exe, no TouchDesigner needed.

Built for issue #12, where the plugin read the subdominant of the true key. The
old measurement path drove a real CHOP in TD in realtime and sampled the output,
which carried a 0.14 MIREX noise floor across repeats of one fixed config. This
replaces it: one compile, one run, deterministic.

## What it runs

Three chains over the same WAV, one JSONL row per file:

| chain | what |
|---|---|
| `plugin` | `EssentiaTonalCHOP::computeBatchAsync` called directly. The harness compiles the SHIPPED translation units, so this column cannot drift from what TD loads. |
| `oracle` | Essentia's own `standard::KeyExtractor`. Not in the pinned static lib (`ci/essentia-CMakeLists.txt` globs exclude `algorithms/extractor/`), so `keyextractor.cpp` is compiled straight into the exe and registered at startup. |
| `experiment` | Mirrors the batch path with per-divergence toggles (`--d1-refkey`, `--d2-whiten`, ...) so a sweep costs no recompile. Enabled with `--exp`. |

A drift guard runs the experiment chain at baseline settings and requires it to
equal the plugin chain (pitch class, mode, bit-exact strength) before any row is
trusted. It compares pitch CLASS, not spelling: the plugin decodes through
`Shared/Utils.h` (sharps) while the experiment chain reports Essentia's raw
string (flats), and `A#` vs `Bb` is not drift.

Expect the guard to fire whenever the shipped chain is changed but the mirrored
`create()` lists here are not. That is the guard working. Re-run with
`--no-selfcheck` only when reading the `plugin` column alone.

## Build

```bash
cd src
cmake -B build-oracle -G "Visual Studio 17 2022" -A x64 -DBUILD_KEY_ORACLE=ON
cmake --build build-oracle --config Release --target key_oracle
```

OFF by default, so plugin builds and CI are unaffected. `KEY_ORACLE_SRC_DIR`
defaults to this directory; `ESSENTIA_SRC_DIR` must be the same pinned checkout
the static lib was built from.

## Use

```bash
key_oracle.exe [flags] file1.wav [file2.wav ...]
key_oracle.exe --help
```

Corpora and sweep drivers live in `diag/key-accuracy/` (`make_key_corpus.py`,
`make_key_corpus_v2.py`, `corpus_sweep.py`, `combo_matrix.py`, `verify_fix.py`).
The corpora are generated, not committed.

## Result that closed issue #12

At n=224 synthetic (ground truth by construction) plus the three ground-truthed
demo tracks, the shipped chain went from 216/224 and 1/3 to 224/224 and 3/3,
agreeing with the oracle on all 227 files. Reproduce with
`diag/key-accuracy/verify_fix.py`.
