<!-- PATCH RELEASE. Settings when posting:
       Visibility  = PUBLIC (the tool is free; the people who need the fix are mostly not patrons)
       Email/notify = OFF (patrons already got the Discord #announcements ping, which notifies all
                      patrons; the promo video rollout runs through 10 Aug and owns the email budget)
       Image        = assets/final/essentiatd-release-1920x1080.png (header)
                      assets/final/essentiatd-release-1080x1080.png (1:1, for feeds that centre-crop)
     No 2.0.0 workaround mentioned on purpose: the message is just update. -->

<!-- ============ PATREON POST ============ -->

**Title:** EssentiaTD v2.0.1: a small but annoying bug, fixed

Quick one, no new features.

If you ever merged a realtime Essentia output with another CHOP and your frame rate collapsed, that was my bug, and v2.0.1 fixes it.

My operators were outputting the right amount of data, one sample per cook, but at the wrong position on the timeline. Middle click one in v2.0.0 and you'd see its Start and End climbing every frame, while a native TouchDesigner operator like Analyze sits at 0. That offset is the whole problem. Merge, Math with two inputs, and Join all cover the full span from the lowest Start across their inputs to the highest End, so putting my operator at index 307966 next to a normal CHOP at index 0 forced the merged result to be 307967 samples wide. That merged result is what ate the CPU. My operator itself was only ever handing over a single value, which is why the symptom was so confusing to read: it looked like Essentia had produced hundreds of thousands of samples when it hadn't.

Every realtime output is now pinned at index 0, same as native operators.

Nothing else changed: no parameters, no channels, no algorithms. Install over the top and your networks keep working exactly as they are.

Windows: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD-Setup.exe
macOS: https://github.com/DarienBrito/EssentiaTD/releases/latest/download/EssentiaTD.pkg

Full notes: https://github.com/DarienBrito/EssentiaTD/releases/tag/v2.0.1

Thanks for the support, as always. The operator videos continue this week.

<!-- ============ NEWSLETTER LINE (next issue, wherever the roundup goes) ============ -->

**EssentiaTD v2.0.1** patches a bug where merging a realtime analyzer output with another CHOP could stall the CPU. The analyzers were emitting one sample, as intended, but at a timeline-tracking index instead of 0, so a Merge had to span everything in between. All realtime outputs are pinned at 0 now. No parameter or channel changes, so networks need no edits. Grab it at https://github.com/DarienBrito/EssentiaTD/releases/latest
