# Measured demo performance

The full showcase retained sub-millisecond median frame production with Mailbox
presentation on the tested Windows/D3D12 system. Reusing the tear-out window also
reduced the large frame stalls caused by repeated swapchain destruction.

These measurements were collected on **2026-09-22** from saved development
snapshots. They are examples of measured behavior, not guarantees for every
application or later revision. [Selected data and provenance](performance.json)
retain per-trial percentiles, paired native action samples, and input hashes.
Historical binaries, source snapshots, and raw captures are not distributed with
this checkout; hashes identify those inputs but cannot reconstruct them.

## Setup and interpretation

- Intel Core i7-12700KF, NVIDIA RTX 2060, Windows 11 build 26200.
- SDL 3.4.10, D3D12; MSVC 19.44.35219 x64, `/O2 /DNDEBUG /W4 /WX /std:c11`.
- Fixed rg_core `27d5475a4af221813977f4b7d62e4e3f88cffab2` and
  rg_text `4b98c6d38d4de1398ebb0970fc8e0e3db01bafa5`.
- Both demos enabled the shared ASCII lookup and SSE2 geometry packing, with the
  portable formatter. Instrumentation and displayed telemetry were included.
- Processes ran serially with normal OS CPU scheduling and no concurrent builds,
  tests, or benchmarks. Hardware and dependency inputs stayed fixed per comparison.

All times below are **CPU wall milliseconds**, including driver and swapchain
waits. They are not GPU execution times, visible display FPS, or input-to-photon
latency. No display scanout or visual-artifact measurement was made. Results cover
one Windows/D3D12 machine and short workloads, not other platforms or long sessions.

## Full showcase: presentation modes

The comparison used the animated showcase, either idle or receiving scripted edits
in an approximately 8 KiB text area. Before/Immediate, after/Immediate, and
after/Mailbox ran in rotating order for three trials each, with 120 warmup and
1,200 measured frames per process. VSync companions used one trial per workload,
with 120 warmup and 480 measured frames.

The before executable predates explicit Mailbox selection. All after rows use the
same executable with different presentation modes. No core rendering algorithm
changed between these snapshots. Percentiles use nearest-rank frame samples;
table values are medians of the trial percentiles. Brackets span the smallest and
largest trial p50, not confidence intervals.

| Workload | Snapshot / mode | Frame p50 [trial range] | Frame p95 | Frame p99 |
| --- | --- | ---: | ---: | ---: |
| Idle | Before / Immediate | 0.1885 [0.1847-0.1933] | 0.4469 | 7.1170 |
| Idle | After / Immediate | 0.2104 [0.2065-0.2149] | 0.4419 | 6.3830 |
| Idle | After / Mailbox | 0.1998 [0.1951-0.2095] | 0.4374 | 5.5104 |
| Idle | After / VSync, one trial | 16.6853 | 17.7542 | 18.1610 |
| Edit | Before / Immediate | 0.2700 [0.2586-0.2875] | 0.4708 | 4.6824 |
| Edit | After / Immediate | 0.2641 [0.2548-0.2715] | 0.4919 | 4.3839 |
| Edit | After / Mailbox | 0.2656 [0.2567-0.2876] | 0.5380 | 5.2645 |
| Edit | After / VSync, one trial | 16.6741 | 17.2816 | 17.7824 |

Mailbox idle was 6.0% slower than the saved before/Immediate snapshot; editing was
1.6% faster. Against the same after executable in Immediate mode, Mailbox changed
idle p50 by -0.0106 ms and edit p50 by +0.0015 ms. Edit p95/p99 were higher with
Mailbox. These mixed results establish no renderer speedup or uniform latency
improvement. Driver/presentation work dominates; VSync waits for display refresh.

For interactive use, **View > Fast, no tearing** selects Mailbox. It permits fast
frame production and can replace completed submissions before display; rendering
thousands of frames does not mean the display shows them all. Normal launches
default to VSync. Unsupported modes fail explicitly; unsupported menu choices are
disabled. [SDL presentation-mode semantics](https://wiki.libsdl.org/SDL3/SDL_GPUPresentMode).

## Native tear-out: destruction versus reuse

The before demo destroyed the native window and swapchain on redock/close. The
after demo parked one hidden claimed window and reused it. These saved snapshots
predate the presentation-mode selection change above. Three alternating process
pairs ran per mode, each with 120 warmup and 1,200 measured frames. The script
activated a 520x460 Inspector every 240 frames and closed it at cycle frame 180.

Action values pool 15 post-warmup occurrences per side and mode. Brackets span
individual action minimum/maximum, rather than trial percentiles.

| Mode | Action | Before median [range] | After median [range] |
| --- | --- | ---: | ---: |
| Immediate | Close lifecycle | 1.4497 [1.2724-3.4747] | 0.2896 [0.2535-0.4030] |
| Immediate | Reopen lifecycle | 7.3592 [6.1843-8.5135] | 0.7184 [0.6599-1.3827] |
| VSync | Close lifecycle | 35.7893 [34.2843-45.2400] | 0.3970 [0.2678-2.2388] |
| VSync | Reopen lifecycle | 6.0221 [5.0714-8.2089] | 1.1843 [0.7872-2.2738] |
| VSync | Whole close frame | 52.3485 [50.1560-62.0554] | 16.9860 [12.4636-19.2905] |
| VSync | Whole reopen frame | 22.3010 [17.1893-23.6566] | 17.6459 [8.5903-23.7349] |

The median close-plus-five-following-frame total stayed near six refresh periods
under VSync: 99.5919 ms before and 99.3654 ms after. Reuse improved pacing around
the close action, rather than increasing display refresh. Whole-run frame p50
changed from 0.2640 to 0.2677 ms in Immediate and 16.6965 to 16.7130 ms in VSync;
there is no general steady-frame speedup claim.

First creation remains in warmup. Reuse retains one native window and swapchain
until shutdown; retained allocation bytes were not measured. Resource cleanup was
outside the frame distributions: after medians were 34.1431 ms in Immediate and
64.6052 ms in VSync. The original executable had no separate cleanup timer, so no
shutdown improvement is claimed. Resizing/restoring windows can still incur work.

## Validation and measuring an application

The recorded release checks passed host/renderer/asset tests, GPU-device checks,
three demo smokes, and 82 native lifecycle checks. All 22,560 Full and 14,400 native
measurement frames passed capture validation without suppressed presentations,
dropped work, or unexpected diagnostics. Native trials matched main/secondary
presentation counts and verified final cleanup left no retained native window.

Build the current demos with the [normal dependency setup](../README.md), then
measure current workloads using your own dependency paths and new output folders:

```powershell
.\build.bat demo
python benchmarks/run_frames.py --scenario idle --scenario edit --present immediate --sdl-root C:/deps/SDL3 --core-root ../rg_core --text-root ../rg_text --output out/frames-immediate
python benchmarks/run_frames.py --scenario idle --scenario edit --present mailbox --sdl-root C:/deps/SDL3 --core-root ../rg_core --text-root ../rg_text --output out/frames-mailbox
python benchmarks/run_frames.py --scenario tearout --present vsync --sdl-root C:/deps/SDL3 --core-root ../rg_core --text-root ../rg_text --output out/frames-tearout
```

These commands measure the current checkout; they do not recreate the historical
before/after snapshots. For a new comparison, freeze each executable before the
next build, keep dependencies/options fixed, alternate trial order, and retain
the generated captures and manifests. See [profiling instructions](PROFILING.md)
and [CPU benchmark instructions](README.md) for workload definitions and tools.
