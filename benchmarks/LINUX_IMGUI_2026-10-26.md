# Linux comparison with Dear ImGui — 2026-10-26

This local run compares current `rg_gui` with Dear ImGui 1.93.0 WIP on Linux/Vulkan. `rg_gui` has lower frame-time medians in **13 of 16 configurations**, and lower native UI/CPU preparation medians in **all 16**. That is a result for these fixtures and this machine, not a whole-library ranking. The three losses were traced to one fragment-shader input slot and removed; the [rerun below](#rerun-after-the-fragment-slot-change) has `rg_gui` lower in all 16.

With two frames in flight, Dear ImGui leads the inspector by **5.4%** and mixed inspector by **8.0%**. Changing labels differ by only **0.6%** and have overlapping, noisy process ranges; treat that cell as effectively tied.

The original private Windows harness was unavailable on this host. This is a [new reproducible Linux harness](third_party/README.md), with matched content and font pixels, rather than an exact rerun of the [archived Windows comparison](PERFORMANCE.md). The old Windows numbers have not been replaced or mixed into this dataset.

## Two frames in flight

Times are **microseconds per frame, lower is better**. Values are medians of six process medians. Frame time includes CPU submission and GPU-fence waits; it is not a hardware GPU timestamp or display FPS.

|Workload|rg_gui frame|Dear ImGui frame|rg_gui UI/prep|Dear ImGui UI/prep|
|---|---:|---:|---:|---:|
|Static labels|152.12|174.51|5.41|58.11|
|Changing labels|182.73|181.57|46.63|63.81|
|Inspector|124.22|117.83|17.57|41.53|
|Virtualized list|61.17|73.08|3.02|23.87|
|Mixed inspector|125.37|116.09|16.18|42.02|
|Asset browser|64.01|74.66|3.85|24.87|
|Text editor|356.79|400.08|21.21|130.86|
|Docked workspace|469.29|711.35|70.67|138.39|

CPU widget submission/preparation was approximately **1.4–13.5x faster** across the complete one/two-flight matrix. That CPU advantage does not turn into a frame-time win for every workload. GPU-fence/driver timings do not establish the specific cause of the inspector losses.

## One frame in flight

|Workload|rg_gui frame|Dear ImGui frame|rg_gui UI/prep|Dear ImGui UI/prep|
|---|---:|---:|---:|---:|
|Static labels|476.10|727.12|6.52|87.84|
|Changing labels|679.09|732.23|59.55|96.32|
|Inspector|388.09|531.45|18.12|54.71|
|Virtualized list|187.60|375.33|3.54|25.16|
|Mixed inspector|495.61|533.24|20.57|59.58|
|Asset browser|247.15|372.78|4.06|30.04|
|Text editor|864.35|1079.56|29.98|131.15|
|Docked workspace|1121.02|1484.22|97.81|151.64|

## Repeatability and tails

These were **loaded-desktop** measurements: the load average was 3.64 at the beginning and 3.32 at the end on a four-logical-CPU machine. No fixed-frequency or quiet-desktop claim is made. The one-flight frame times varied substantially between processes. Ranges below are observed process-median ranges, not confidence intervals. Every measured frame was retained.

|Workload|Flights|rg_gui median range|ImGui median range|Pairs with lower rg_gui frame time|
|---|---:|---:|---:|---:|
|Static labels|1|298.74–568.47|690.71–838.75|6/6|
|Static labels|2|151.56–153.38|173.36–184.84|6/6|
|Changing labels|1|637.93–731.35|702.60–806.40|5/6|
|Changing labels|2|176.33–201.60|172.83–315.11|2/6|
|Inspector|1|298.80–529.35|514.64–552.97|6/6|
|Inspector|2|123.51–124.72|116.31–130.35|2/6|
|Virtualized list|1|162.66–275.60|281.66–387.27|6/6|
|Virtualized list|2|59.41–61.65|70.41–78.11|6/6|
|Mixed inspector|1|470.89–681.63|489.70–557.66|4/6|
|Mixed inspector|2|124.57–127.25|114.54–153.85|1/6|
|Asset browser|1|176.92–434.07|277.28–560.55|5/6|
|Asset browser|2|61.37–66.49|73.15–93.19|6/6|
|Text editor|1|796.39–1277.96|983.17–1374.08|6/6|
|Text editor|2|354.25–360.09|392.66–432.00|6/6|
|Docked workspace|1|1010.11–1251.06|1393.77–1573.09|6/6|
|Docked workspace|2|461.93–483.94|689.84–760.04|6/6|

The median p95 favors `rg_gui` in all 16 cells, but p99 and maxima do not uniformly improve. For example, one-flight list p99 is **1,499.58 vs 1,234.28 µs**, and the two-flight static-label median of process maxima is **2,972.40 vs 2,071.33 µs** (`rg_gui` vs ImGui). These are CPU wall-time tails on a shared desktop, not isolated library execution or input latency.

## Method and identity

- **Machine:** Intel(R) Core(TM) i3-1005G1 CPU @ 1.20GHz; Intel(R) UHD Graphics (ICL GT1); Intel open-source Mesa driver 25.2.8 (Mesa 25.2.8-0ubuntu0.24.04.1). Linux, SDL 3.4.18.
- **Sources:** `rg_gui` `af2462ab87b2114613731b23d1d457542531dacc`; Dear ImGui docking `3bae66c735670619baf51391eba7f3d90a25d125` for all workloads. Exact executable, dependency/header/font/shader hashes and compiler commands are in the manifest.
- **Build:** GCC/G++ 13.3, GNU C11 / C++17, `-O2 -DNDEBUG`, no LTO; unmodified Dear ImGui SDL_GPU backend. Both use the current installed SDL runtime.
- **Protocol:** six alternating pairs per case/queue depth, three RG-first and three ImGui-first; workload order rotates. 512 warmup plus 2,048 measured frames per process. **192 processes, 393,216 measured frames**, pinned to CPU 0.
- **Rendering:** 1,280×800 RGBA8 offscreen target, one or two outstanding GPU fences; one flight waits for its submitted frame, two flights wait before reusing a slot. No swapchain presentation, tile caching, or unchanged-frame skipping.
- **Shared text:** shipped Inter16 glyph bitmaps, dimensions and advances; white core text; kerning disabled. ImGui imports those glyph pixels through its font-loader interface. No different font is rasterized. Shape antialiasing and rounding are disabled; native widget colors/shapes still differ.
- **Caching:** RG shared ASCII lookup, 32-quad pages, 1,024 cached runs and 65,536 cached quads, plus caller-owned editor layout cache. These reservations are part of its memory/performance tradeoff; memory usage was not ranked.
- **Timing:** `ui` covers native widget submission and CPU renderer preparation. `frame` also covers upload, encoding, submission and fence waits. Initialization, common fixture generation, state inspection/readback and file output are excluded. Final queue drain is included in completed throughput, outside per-frame percentiles.

## Validation before timing

- All 32 verification processes succeeded: **16,384 replayed frames** across both libraries, all cases, and both queue depths.
- All four static/dynamic label capture pairs are **byte-identical over the complete RGBA image**, with all 128 labels visible.
- Both editors contain identical bytes after **every** replayed keystroke; each insertion changes the length and each backspace restores the expected original document.
- Checkbox toggles, slider drags, row selections and inspector values pass state checks. Input polling snapshots and queued events are both supplied, matching the native APIs.
- All 128 dock labels are visible in each library/flight capture: 16 text-line clusters in each of eight pane columns. Native widget images are checked for visible text, without claiming full cross-library pixel equality.
- An independent audit rederived every process percentile/mean from all raw CSV rows and all 16 aggregate cells. Executable hashes agree with build and verification records.

## Rerun after the fragment-slot change

The frame-time losses above were on the GPU side of the loop: every two-flight
cell shows a fence wait of tens of microseconds for both libraries, so both
were paced by the GPU queue and the CPU lead cannot appear in the frame column.
That column is frame time including submission and fence waits, not a hardware
GPU timer. A replication sweep (`--replicate`) and shader variants traced
rg_gui's higher per-replica frame cost to a third fragment input slot in the
indexed shader; change 11 in the
[performance audit](../docs/performance-audit-2026-10.md) has the sweep. With
the flag packed into the `uv` slot, the same protocol (six alternating pairs,
2,048 measured frames after 512 warmup, pinned to CPU 0) gives, at two flights:

|Workload|rg_gui frame before|rg_gui frame after|Dear ImGui frame|rg_gui UI/prep|Dear ImGui UI/prep|Pairs rg_gui lower|
|---|---:|---:|---:|---:|---:|---:|
|Static labels|152.1|131.2|175.3|5.2|60.4|6/6|
|Changing labels|182.7|168.3|179.6|44.8|65.5|5/6|
|Inspector|124.2|108.3|125.7|16.8|47.8|6/6|
|Virtualized list|61.2|56.0|77.1|2.9|24.8|5/6|
|Mixed inspector|125.4|114.8|115.9|24.9|43.2|5/6|
|Asset browser|64.0|58.7|81.9|4.6|29.0|6/6|
|Text editor|356.8|357.5|388.7|19.3|101.1|6/6|
|Docked workspace|469.3|436.1|732.3|57.6|138.2|6/6|

rg_gui has the lower frame-time median in **16 of 16** cells, both queue
depths, and the lower UI/preparation median in all 16, as before. The load
average was 4.31 at the start and 2.51 at the end, higher than the first run,
which inflated both libraries' one-flight times and Dear ImGui's two-flight
times in some cells; compare rg_gui's before and after columns, which moved by
the amount the replication sweep predicted, rather than the ImGui columns
across runs. Verification was repeated before timing: the label captures are
byte-identical to Dear ImGui's with the new shader, and all 16 pixel checks and
every state check passed.

## Differences from the old Windows suite

The same eight workload categories are represented, but the implementation is a reconstruction. It uses Vulkan on an integrated Intel GPU instead of D3D12 on an RTX2060, different SDL/compiler versions and shorter sample lengths, and ImGui docking for all cases. Browser filtering/virtualization is supplied by common application fixtures; docking restores the layout periodically rather than replaying splitter/tab gestures. The editor performs one actual insertion/backspace per frame after focus. These differences prevent interpreting the new table as a regression/improvement ratio against the historical Windows table.

## Reproduce and inspect

- [Harness source and commands](third_party/README.md).
- [Raw process/frame data and complete summary](../out/third-party-linux-2026-10-26/measurements/summary.md).
- [Machine, build, source hashes and protocol manifest](../out/third-party-linux-2026-10-26/measurements/manifest.json).
- [Verification manifest and independent validation](../out/third-party-linux-2026-10-26/verification-final/manifest.json).
- [Independent measurement audit](../out/third-party-linux-2026-10-26/audit/measurement-audit.txt).
- Rerun after the fragment-slot change: [raw data and summary](../out/third-party-linux-2026-10-26/rerun-f41e8b6/measurements/summary.md), [manifest](../out/third-party-linux-2026-10-26/rerun-f41e8b6/measurements/manifest.json), [verification](../out/third-party-linux-2026-10-26/rerun-f41e8b6/verification/manifest.json), and the [replication and shader-variant sweeps](../out/third-party-linux-2026-10-26/rerun-f41e8b6/sweeps/).

The first run above used `rg_gui` af2462a unchanged; the rerun section used the fragment-slot change that followed from it.
