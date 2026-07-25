<!-- Patch release. No image attachment: keep it plain so it doesn't compete with the promo video drops.
     No 2.0.0 workaround mentioned on purpose: the message is just update. Posted + edited to
     match 2026-07-25 (announcements 1530475256416829440, open-tools/EssentiaTD 1530475297831518249). -->

<!-- ============ POST 1: #announcements ============ -->

🔧 **EssentiaTD v2.0.1 is out. Small patch, worth grabbing.**

If you ever merged a realtime Essentia output with another CHOP and watched your frame rate fall off a cliff, that was a bug and it's fixed.

My operators were reporting the right amount of data (one sample per cook) but at the wrong position on the timeline. Middle click one in v2.0.0 and its Start and End climb every frame, while a native TD operator like Analyze sits at 0. That offset is the whole problem: Merge, Math with two inputs, and Join all cover the full span from the lowest Start across their inputs to the highest End. Put my operator at index 307966 next to a normal CHOP at index 0, and the merged result has to be 307967 samples wide. That merged result is what ate your CPU. My operator was only ever handing over a single value.

In v2.0.1 every realtime output is pinned at index 0, same as native TD operators, so a merge like that now comes out 1 sample wide.

Nothing else changed. No parameters, no channels, no algorithms. Drop in the new build and your networks work as they are.

Windows: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD-Setup.exe
macOS: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD.pkg

Full notes: https://github.com/DarienBrito/EssentiaTD/releases/tag/v2.0.1

<!-- ============ POST 2: open-tools / EssentiaTD thread ============ -->

**v2.0.1 patch is up**, in case you hit this one.

Merging a realtime Essentia output with another CHOP could stall the CPU. The sample count was never the problem: my operators output 1 sample and always did. The sample *index* was wrong. It tracked the timeline instead of staying pinned at 0, so anything that unions ranges had to span from 0 out to wherever my operator had drifted to.

Audio oscillator fanned out to a native Analyze CHOP and my four analyzers, on v2.0.0:

```
analyze1   n=1       start=0        end=0
rhythm1    n=1       start=307966   end=307966
spectral1  n=1       start=307966   end=307966
loud1      n=1       start=307966   end=307966
tonal1     n=1       start=307966   end=307966
merge1     n=307967  start=0        end=307966
```

Every analyzer at n=1. The 307967 is the merge, forced to cover 0 through 307966. In v2.0.1 all four sit at start=0 end=0 and that merge is n=1.

No parameter or channel changes, so nothing to rewire. Just update.

https://github.com/DarienBrito/EssentiaTD/releases/tag/v2.0.1
