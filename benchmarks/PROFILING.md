# Demo and Unicode profiling

The full and tear-out demos record real window frames, including SDL GPU
submission and presentation waits. The Unicode harness separately measures CPU
text work. These complement the [CPU benchmarks](README.md).
Keep builds, tests, and other benchmarks separate from measurement runs.

## Requirements

The runners support Linux and Windows with Python 3.9+, rg_core, rg_text,
and SDL3 development files. The PowerShell examples below use the Windows
Visual Studio setup. Set up the
[normal demo dependencies](../README.md#dependencies) first. Run commands from
the repository root and replace `C:/deps/SDL3` with your SDL3 installation.
The real-window runner needs freshly built demos and a working SDL GPU backend;
the Unicode CPU harness builds its own executable. RenderDoc is optional and
used only by the capture workflow at the end of this guide.

For Linux X11/Vulkan, build first and select the native binary directory:

```sh
./build.sh test_release
SDL_VIDEODRIVER=x11 SDL_GPU_DRIVER=vulkan python3 benchmarks/run_frames.py --bin-dir out/linux --core-root ../rg_core --text-root ../rg_text --present vsync --output out/linux-frames-vsync
SDL_VIDEODRIVER=x11 SDL_GPU_DRIVER=vulkan python3 benchmarks/run_frames.py --bin-dir out/linux --core-root ../rg_core --text-root ../rg_text --present immediate --output out/linux-frames-immediate
```

Use a real display and hardware GPU for these profiles. The dummy video driver
is only suitable for host tests. Native Wayland tear-out support is outside the
current validation scope: global window placement and pointer coordinates are
assumed by that demo. Linux runtime details, affinity, and available CPU governor
information are recorded with the results. Keep the window manager, compositor,
display refresh, power settings, and background load consistent across comparisons.
Run `perf` sampling only when the host permits it; the frame stages still provide
useful CPU wall-time measurements when kernel profiling is unavailable.

## Inspect the interactive demo

Build with the same dependency environment used for the normal demos, then run
from the repository root:

```powershell
.\build.bat demo
.\rg_gui_demo_full.exe
```

The **Frame Stats** panel shows recent frame intervals, rolling p95/p99 values
over up to 120 samples, CPU UI/preparation/upload/encoding times, presentation
wait and submission time, draw calls, dispatches, glyphs, upload bytes, and text
cache hits/misses/evictions. Numeric renderer telemetry refreshes every 15 frames
to limit text churn caused by displaying the measurements themselves. The demo
still includes this instrumentation and the rest of the showcase in its frame
work.

| Measurement | Work included |
| --- | --- |
| UI | Build widgets and finish GUI draw lists. |
| Prepare | Begin the text-renderer frame and prepare the ordered GPU draw list on the CPU. |
| Stage upload | Map/stage/unmap the upload ring. This is CPU work, not GPU transfer duration. |
| Encode | Acquire the command buffer and encode copy/compute commands. The panel also includes render-pass draw encoding; CSV records them separately. |
| Swapchain wait | `SDL_WaitAndAcquireGPUSwapchainTexture`, including any wait imposed by presentation or the driver. |
| Submit | CPU time in command submission. This is not the time taken to finish GPU execution. |
| Frame work | Loop work through rendering and, for tear-outs, native-window lifecycle handling. |
| Frame interval | Time between successive loop starts. An interval spans the work of the preceding frame. |

CSV also separates input/event processing, platform cursor/text-input work, and
native-window lifecycle work. Main and secondary renderers have separate fields.
Counters include draw commands/calls, geometry vertices, text instances, upload
bytes, copy calls, cache upload ranges, cache behavior, diagnostics, and dropped work. A submitted frame can have
no swapchain presentation; its draw counters are not a complete rendered frame.

## Repeatable real-window runs

The runner does not build. Rebuild first, then supply a new output directory:

```powershell
python benchmarks/run_frames.py --output out/profile-frames --sdl-root C:/deps/SDL3 --core-root ../rg_core --text-root ../rg_text
```

Defaults are three serial trials of each scenario, with the scenario order
rotated between trials. Each process records 1,320 frames: 120 warmup frames and
1,200 measured frames. Windows stay visible. Avoid minimizing them, moving the
mouse over them, or using them while scripted measurements run.
The runner rejects unexpected measured window sizes, including external
maximization, so changing the amount of visible UI cannot silently change a
comparison. The full demo uses 1280×800, the tear-out main window 1220×760, and
the resize scenario alternates 1280×800 and 1024×720.

`--no-vsync` is sent to the demos by default, requesting immediate presentation.
Use runner option `--present vsync`, `--present mailbox`, or `--present immediate`
to compare presentation modes; `--vsync` remains an alias for `--present vsync`.
These options are mutually exclusive. An unsupported requested mode fails the
profile, and the runner checks the actual mode reported by the demo. Direct demo
launches still default to VSync. CPU affinity is unchanged by default so the
OS schedules the real application; `--pin-cpu` explicitly selects the first
allowed logical CPU (`--cpu-zero` remains an alias). Keep presentation mode, affinity, power settings, compiler, dependencies,
and hardware consistent when comparing runs.

For manual Full-demo testing, choose **View > Fast, no tearing** or launch
`.\rg_gui_demo_full.exe --present mailbox`. Mailbox may replace completed
submissions before display; its frame-production rate is not the display's
refresh rate.

Select workloads or adjust capture length with:

```powershell
python benchmarks/run_frames.py --output out/profile-edit-scroll --scenario edit --scenario scroll --trials 3 --warmup 120 --frames 1320 --sdl-root C:/deps/SDL3
python benchmarks/run_frames.py --output out/profile-tearout-vsync --scenario tearout --vsync --sdl-root C:/deps/SDL3
python benchmarks/run_frames.py --output out/profile-mailbox --scenario idle --scenario edit --present mailbox --sdl-root C:/deps/SDL3
```

`--bin-dir` selects the directory of the fresh executables. `--timeout` defaults
to 120 seconds per process. Dependency roots are optional declarations for
provenance hashing; they should match the roots used to build the demos. The
runner records source, executable, asset, shader, and declared dependency hashes
and refuses obviously stale demo binaries. These snapshots do not reconstruct
compiler flags from an executable. Linux builds supply hash-bound build sidecars
that the runner includes when present.

`--frame-buffers 1`, `2`, or `3` selects the renderer's packed frame-buffer count
in the full/tear-out demos and frame runner; the default remains two. Compare
these configurations using the same scenes and presentation mode. This controls
renderer storage reuse, not SDL's presentation queue limit. A lower upload time
alone does not establish a better frame time or lower input latency.

| Scenario | Executable and workload |
| --- | --- |
| `idle` | Full showcase with scripted input suppressed; its existing animation and statistics remain active. |
| `text-idle` | Full showcase with an approximately 8 KiB text area visible and unchanged. |
| `edit` | The same text area receives ordered text/backspace events. The demo verifies actual length changes. |
| `scroll` | The text area receives wheel events, reversing direction every 120 frames. The demo verifies scroll-position changes. |
| `resize` | Resize the actual window between 1280×800 and 1024×720 every 30 frames. CSV records actual pixel sizes. |
| `images` | Add 512 image quads whose texture alternates between two separately allocated, identical-content atlas textures. |
| `images-grouped` | Keep those 512 quads and their geometry, but use the first texture for 256 consecutive quads and the second for the remaining 256. |
| `tearout` | Tear-out demo: create a native Inspector window at each 240-frame cycle start, then redock and close it at cycle frame 180. |
| `static` | Full showcase frozen after warmup: statistics and animation stop, so every measured frame is identical. Direct launch only; with `--tile-cache-skip-present` unchanged frames are neither drawn nor presented. |
| `docks` | Full showcase plus extra docked property panels, 20 unless `--dock-panels N` (0 to 60) says otherwise, sharing up to three new full-height columns as tabs. Every column gets the same width. Direct launch only. |
| `manual` | Direct demo launch only; interact normally while recording. Both full and tear-out demos support it. |

The image control deliberately changes texture order while preserving geometry
and image content. It demonstrates the renderer's existing adjacent batching;
it does not add automatic draw reordering. Arbitrary UI commands cannot in
general be reordered without changing appearance.

The tear-out scenario uses the same panel/window requests as the demo controls.
The demo parks one hidden claimed window on redock/close, then reuses it
on the next activation. It retains that window and swapchain until final cleanup.
Initial creation, parking, and reuse are included in `lifecycle_ms`.
Post-warmup lifecycle spikes remain in the reported maximum and percentiles.
Initial warmup and process shutdown are not part of the main summary. The
demo reports separate physical creation/reuse/parking/destruction counts
and resource-cleanup duration (through device/window destruction, before
`SDL_Quit`); the runner checks that final cleanup retains no native window.

## Raw capture and interpretation

For manual interaction or a single scripted capture, invoke a demo directly.
The output parent directory must exist, and the CSV path must be new:

```powershell
New-Item -ItemType Directory -Force out | Out-Null
.\rg_gui_demo_full.exe --scenario manual --profile out/manual.csv --profile-warmup 120 --frames 1320
.\rg_gui_demo_tearout.exe --scenario tearout --profile out/tearout.csv --profile-warmup 120 --frames 1320 --no-vsync
```

`--profile` preallocates frame records and writes the CSV after the loop, avoiding
file I/O during measurement. If `--frames` is omitted with a profile path, the
demo defaults to warmup plus 1,200 frames. `--profile-warmup` marks initial rows;
it does not discard them from the file. Manual captures can end early when the
window closes; the repeatable runner requires the requested full frame count.
Do not combine `--smoke-test` with `--profile`: smoke tests deliberately stop
after a few presentations and do not provide a full warmup and measurement run.

Runner output includes raw CSVs, process stdout/stderr, `manifest.json`,
`summary.json`, and `summary.md`. Each trial is summarized separately using
nearest-rank p50/p95/p99/max over individual frames. Stage and counter summaries
also include combined main-plus-secondary values. Action-frame breakdowns
retain lifecycle spikes rather than treating them as outliers. The first 120
frames have a separate warmup summary and remain in CSV.

The runner requires at least 95% of measured main presentations, and the same
rate for attempted secondary presentations. Frames with suppressed presentation
are counted and excluded from distributions. It verifies actual editing and
scrolling, two resize dimensions, image geometry/draw counts, and native window
creation/presentation counts. Dropped work and renderer diagnostics fail a run.
The only allowed GUI diagnostic is the expected `ACTIVE_ID_RECOVERED` bit
(`128`) at setup mouse-release frame 3 in `edit`/`scroll`; the recovery count is
retained. Other GUI diagnostics fail validation.

All these durations are CPU wall time. A large wait can reflect presentation,
driver scheduling, OS scheduling, or GPU backpressure. Subtracting swapchain wait
does not remove every driver/OS wait and does not produce GPU execution time.
Use the stage split and counters to select further experiments, without treating
a reduced draw-call count as proof of a faster frame.

## Paired native-window lifecycle comparisons

To evaluate a lifecycle change, preserve a baseline tear-out executable before
rebuilding. The dedicated runner accepts two prebuilt executables and alternates
their process order, with three trials each in both immediate and VSync modes
by default:

```powershell
python benchmarks/run_native_lifecycle.py --before-bin out/before/rg_gui_demo_tearout.exe --after-bin rg_gui_demo_tearout.exe --sdl-root C:/deps/SDL3 --output out/native-comparison
```

`out/before` is an example directory you create to hold your baseline executable;
the repository does not supply a baseline. Use matching dependencies, assets,
and shaders for a GUI-only comparison.

Each process records 720 frames with 120 warmup frames. Use `--frames`,
`--warmup`, `--trials`, or repeated `--mode immediate`/`--mode vsync` to adjust
the run. Optional `--before-provenance` and `--after-provenance` JSON files bind
source/build records to each executable using a top-level `executable_sha256`.
The runner records executable/runtime/asset/shader hashes and checks that they
stay unchanged during timing.

Separate summaries cover activation (creation or reuse), redocking/closing
(destruction or parking), and the close frame plus its next five frames. This
checks whether a reduced close cost simply moved to a later frame. Every
post-warmup frame is retained in the primary comparison, including suppressed
presentations; a fully-presented-only summary is additional. The candidate's
physical lifecycle and final cleanup counts are validated when present. Final
cleanup stays outside frame distributions and has its own reported duration;
one retained swapchain is a deliberate memory tradeoff, not eliminated work.

## Dirty-tile energy and GPU indicators

`measure_tiles.py` runs the full demo's direct and `--tile-cache` paths in
alternating pairs and reads whole-system indicators around each run: readable
Intel RAPL energy counters (root-only by default) and the i915 RC6 residency
counter, reported as "GPU busy" time per measured frame. Startup and warmup are
removed by subtracting a warmup-only run. Both indicators include every process
on the machine; compare pairs taken under the same load, and prefer Immediate
presentation when the question is the demo's own GPU work. Energy differences
only separate on a quiet machine with no other GPU application running;
compare average power rather than energy per frame, and read the package
counter over a few idle seconds as a floor. See the
[performance audit](../docs/performance-audit-2026-10.md#level-two-dirty-tiles)
for measured results and their limits.

```sh
python3 benchmarks/measure_tiles.py idle immediate 1200 3
python3 benchmarks/measure_tiles.py static vsync 600 6 "direct=" "tiles=--tile-cache" "skip=--tile-cache --tile-cache-skip-present"
```

## Many docked panels

The `docks` scenario measures how frame cost grows with the number of docked
panels. Rotate the panel counts between rounds, because the counts change the
workload itself:

```sh
for n in 0 3 15 30 60; do
    out/linux/rg_gui_demo_full --scenario docks --dock-panels $n --present immediate \
        --frames 1320 --profile-warmup 120 --profile out/docks-$n.csv
done
```

Compare `ui_ms`, `main_prepare_ms`, `main_stage_upload_ms`, and
`main_draw_calls` across counts. When other GPU work is running, compare each
run's 10th-percentile frame rather than its median. See the
[performance audit](../docs/performance-audit-2026-10.md#many-docked-panels)
for results.

## Fill and overdraw

Four full-showcase options measure where a fill-bound frame goes. `--size WxH`
opens the window at another size; frame work against window area shows whether
the frame is fill-bound (on the audit's laptop it was 114 + 1,568 us per
megapixel). `--coverage` rasterizes the first measured frame's rectangles on
the CPU and prints the layers painted per pixel and how much paint lies under a
later opaque rectangle. `--keep-hidden-draws` turns off the end-of-frame
hidden-draw culling for an A/B of its effect, and `--dump-commands PATH` writes
that frame's raw draw commands for offline analysis, such as timing the culling
pass in a tight loop. See
[change 12](../docs/performance-audit-2026-10.md#12-hidden-draw-culling).

```sh
out/linux/rg_gui_demo_full --scenario idle --present immediate --coverage \
    --frames 160 --profile-warmup 120 --profile out/coverage.csv
for size in 640x400 905x566 1280x800 1568x980; do
    out/linux/rg_gui_demo_full --scenario idle --present immediate --size $size \
        --frames 1320 --profile-warmup 120 --profile out/size-$size.csv
done
```

## Loaded-font Unicode CPU workloads

`bench_unicode.c` generates and loads sorted synthetic 4,096- and 16,384-glyph
fonts through `rg_text_font_load_rgfont`. Each has a matching number of kerning
pairs. These exercise the loaded-font lookup path; they are not typographically
representative real fonts or language-shaping tests.

There are 16 cases: two font sizes × two corpora × four operations. Each corpus
has 128 labels with 36 CJK codepoints and four ASCII digits per label (40
codepoints, 112 UTF-8 bytes). The repeated corpus uses a 32-codepoint CJK
alphabet; the dispersed corpus spreads codepoints across the font. The cases
measure changing frontend text, warm renderer preparation, cold renderer
preparation, and isolated glyph/kerning lookup on predecoded codepoints.

```powershell
python benchmarks/run_unicode.py --output out/profile-unicode --gui-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3
```

The runner uses the platform's benchmark build setup. It performs correctness
checks before timing, then five serial process trials on the selected logical CPU. Each
case has seven calibrated approximately 35 ms batches per process. The main
summary is the median of five process medians; `p95_batch_mean_ns` is the p95 of
the 35 batch averages. **That p95 is not individual-frame latency** and is not
comparable to the demo runner's frame p95.

`--lookup 1` is the default and attaches the optional ASCII lookup where the
headers support it; use `--lookup 0` for a control. Font loading, allocation,
lookup initialization, and cold-cache clearing are outside measured intervals.
There is no GPU execution or shaping. Cold cases include per-frame timer
overhead. Known-font output checks, geometry/cycle signatures, raw samples, and
build provenance are retained.

Build and verify before scheduling timing separately:

```powershell
python benchmarks/run_unicode.py --output out/unicode-build --gui-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --build-only
python benchmarks/run_unicode.py --output out/unicode-results --current-bin out/unicode-build/build/current.exe --sdl-root C:/deps/SDL3
```

For a change under test, use `--before-root` and `--after-root`, or paired
`--before-bin` and `--after-bin` executables with their `.build.json` files.
Before/after variants alternate process order and must produce matching
workload/output signatures. Use a new output directory for each run.

## Optional GPU capture

`capture_gpu.py` is a separate RenderDoc capture/replay script for RenderDoc's
embedded Python environment. It is not run by `run_frames.py`, and ordinary
demo/Unicode profiling does not require RenderDoc. The script records capture
provenance, action lists, and supported GPU event-duration counters across
replays. The executable, argument string, and new output directory are supplied
through `RG_GUI_CAPTURE_EXE`, `RG_GUI_CAPTURE_ARGS`, and
`RG_GUI_CAPTURE_OUTPUT`; optional frame/count/timeout settings are documented in
the script.

Keep these results separate from native frame profiles. Replay instrumentation
can change scheduling and caches. Sums of measured leaf draw/dispatch/copy/clear
event durations are **not elapsed GPU frame time**, especially across queues,
and do not measure CPU work or presentation waiting. Unsupported event counters
remain unavailable rather than being reported as zero.

For example, after building the full demo:

```powershell
$env:RG_GUI_CAPTURE_EXE = "$PWD\rg_gui_demo_full.exe"
$env:RG_GUI_CAPTURE_ARGS = '--scenario idle --no-vsync --frames 480 --capture-wait-ms 2000'
$env:RG_GUI_CAPTURE_OUTPUT = "$PWD\out\gpu-idle"
Start-Process 'C:\Program Files\RenderDoc\qrenderdoc.exe' -ArgumentList '--python', "$PWD\benchmarks\capture_gpu.py" -WindowStyle Hidden -Wait
Get-Content "$env:RG_GUI_CAPTURE_OUTPUT\status.json"
```

The output directory must be new. Require `status.json` to say `complete`;
RenderDoc can return exit code zero even when its Python script fails.
`progress.log` records progress immediately. The startup delay lets target
control queue the requested frames before rendering begins; it precedes frame
measurement. `RG_GUI_CAPTURE_REPLAY_DIR` can reuse an earlier capture directory
without launching the demo again. Native driver calls can block outside the
script's polling timeout, so an automated launcher should impose an overall
process timeout and clean up only the processes it created.

If RenderDoc reports a missing prerequisite or unsupported GPU counters, treat
those counters as unavailable and follow the tool's diagnostics. A successful
capture alone does not establish that duration counters are available.
Setting the process environment
`SDL_GPU_DRIVER=vulkan` requests a separate Vulkan experiment, and the script
records that request and verifies the captured API. Keep such GPU results
separate from D3D12 measurements. Only report GPU durations when the captured API
and required counter coverage have been verified.

## Linux X11 baseline — 2026-10-02

The [baseline data](linux-validation.json) retain per-trial results, source and
executable hashes, build settings, runtime observations, and validation outcomes
for one Intel laptop. Raw captures, binaries, and the complete source snapshots
are not distributed. The public runners above can reproduce the workloads;
hashes alone do not reconstruct the exact measured checkout. These results do
not establish an operating-system comparison with the separate Windows/D3D12
Dear ImGui results.

Configuration: Linux Mint 22.1, kernel 6.8.0-60, Xfce/Xfwm4 X11, Intel
Core i3-1005G1 (2 cores/4 logical CPUs), Intel UHD Graphics ICL GT1, Mesa
25.2.8, SDL 3.2.18, and a 1920×1080 display at about 59.97 Hz and scale 1.
Demos use GCC 13.3 with `-O2 -DNDEBUG`; the CPU comparison also uses Clang
18.1.3 with matching optimization and ASCII lookup enabled. AC power and the
balanced power profile remained enabled (`intel_pstate`, `powersave`,
`balance_performance`). No power settings or kernel profiling permissions were
changed. CPU microbenchmarks pin themselves to CPU 0; frame captures retain
normal scheduling.

The reported measurements ran serially; an earlier set with overlapping
benchmarks was excluded. Normal desktop load and frequency variation remain,
so small changes and overlapping trial ranges should not be treated as firm
wins. `perf` hardware counters were
unavailable under the host's `perf_event_paranoid=4` policy. Frame stages are CPU
wall time, including driver/OS waits; no GPU execution or input latency is
claimed.

### Validation and fixes

The local GCC `./build.sh test_release` gate passes, including asset hashes,
host and GPU preparation tests, all eight SPIR-V shaders, GPU readback probes,
all three demo smoke tests, and native lifecycle checks. Strict GCC and Clang
builds and host ASan/UBSan checks pass. The final native lifecycle test passes
with both compilers, plus ten consecutive optimized GCC runs of 97 checks each.
Hosted CI is configured to build both compilers and sanitizer configurations;
that workflow has not been executed remotely here. GPU/display execution
remains a local gate. The baseline records working-tree source hashes rather
than claiming to represent an unmodified release. The dependency headers used
by rg_gui match published `rg_core` revision `c926c1d` and `rg_text` revision
`83991b4`; the full captured dependency snapshot also contains an experimental
change to an unused `rg_text_gpu.h` header. Older dependency checkouts may lack
the Linux portability fixes. The GCC release gate also passed from an isolated
checkout using those published dependency revisions.

Linux validation found and fixed strict LP64 compiler diagnostics, a duplicated
window-decoration offset in cross-window hit testing, and cached maximized/
minimized tear-outs reopening with stale SDL state or the wrong geometry on
X11. Regression checks cover client-area edges, exact restored geometry,
native close events, text-input cleanup, cache ownership, and GPU cleanup.
The exceptional restore path waits for normal state; ordinary cached reuse
does not add that wait.

Native Wayland and extended HiDPI coverage remain deferred. Automated text
editing and text-input lifecycle checks do not cover manual cross-application
clipboard operation or real IME composition. Windows execution was not repeated
on this Linux host.

### CPU tuning observations

Times below are microseconds, using the median of five process medians.
These workloads time CPU widget/text/preparation work without presentation.

| Workload | GCC | Clang | Clang wins among five pairs |
| --- | ---: | ---: | ---: |
| ASCII warm GPU preparation | 4.36 | 4.47 | 3 |
| ASCII changing labels | 23.53 | 28.82 | 1 |
| ASCII text-area click/drag | 104.21 | 84.96 | 4 |
| Mixed UTF-8 changing labels | 328.29 | 362.52 | 0 |
| Mixed UTF-8 warm renderer | 2.98 | 4.17 | 0 |
| Mixed UTF-8 cold GPU preparation | 344.27 | 377.02 | 0 |

GCC remains the default. Clang's ASCII editing result is a workload-specific
opportunity, with overlapping click/drag process ranges of 89.91–206.67 µs
(GCC) and 80.41–135.11 µs (Clang). It does not justify a blanket compiler switch.

SSE2 packing remains opt-in. GCC's isolated 1,024-quad packing regressed from
8.54 to 9.35 µs in all five pairs, while Clang's isolated 128- and 1,024-quad
packing improved in all five pairs. Full 1,024-quad preparation medians improved
from 24.98 to 19.16 µs with GCC and 17.96 to 15.71 µs with Clang, but process
ranges overlap and each has one losing pair. A full-demo comparison is needed
before changing the renderer default; a packing microbenchmark is insufficient.

For synthetic loaded Unicode fonts, warm renderer medians were 3.74–7.58 µs
versus 276.6–1,095.4 µs cold, per 128-label/5,120-glyph workload. Preserving
cache reuse matters much more here than small packing changes. These synthetic
fonts and corpora do not measure language shaping. Unicode p95 values in the
data summarize batch averages, not individual-frame latency.

### Presentation, images, and buffering

All 90 frame captures passed workload, size, presentation, and provenance
checks: 108,000 measured frames, no suppressed presentations, dropped
runs/glyphs, renderer diagnostics, cache bypasses, or full-cache uploads.
The separate two-process lifecycle A/A comparison also passes; both sides use
the same executable, so its timing difference is variation rather than a
before/after improvement.

Each cell below is **frame-work p50 / p99 in milliseconds**, taking the median
of the three per-trial percentiles. Per-trial p95, maxima, stage times, and
ranges remain in the linked data. The modes ran in separate consecutive
blocks, so time-varying desktop load and power behavior can affect comparisons.

| Scenario | Immediate | VSync | Mailbox |
| --- | ---: | ---: | ---: |
| Idle | 2.088 / 9.198 | 16.674 / 21.568 | 1.025 / 6.004 |
| Text idle | 1.951 / 8.364 | 16.670 / 21.468 | 1.037 / 5.709 |
| Edit | 1.983 / 9.511 | 16.720 / 21.591 | 1.119 / 5.506 |
| Scroll | 2.192 / 11.250 | 16.708 / 23.239 | 1.015 / 5.020 |
| Resize | 1.899 / 41.455 | 16.589 / 33.544 | 0.964 / 20.425 |
| Alternating images | 4.124 / 15.709 | 16.692 / 21.623 | 1.929 / 7.227 |
| Grouped images | 2.877 / 8.642 | 16.688 / 21.431 | 1.966 / 8.752 |
| Tear-out | 2.746 / 14.774 | 16.694 / 21.580 | 1.671 / 7.019 |

VSync remains the interactive default; its median follows the roughly 16.7 ms
display refresh interval. For editing, median UI/preparation cost is about
0.152/0.040 ms, while swapchain wait is 15.816 ms. Mailbox is an available
throughput option, but these results do not measure input-to-display latency,
visual smoothness, power use, or how many submitted frames reach the display.

Grouping the identical-content image textures reduces draws from **531 to 21**
with the same **6,108 vertices** (measured before rectangles moved to four
vertices; the same scene now uploads 4,128). Immediate-mode median draw-encoding cost drops
from 0.562 to 0.108 ms and median frame work from 4.124 to 2.877 ms. Draw encoding
also improves in VSync/Mailbox, but total frame time does not improve uniformly
across modes. Reuse textures/atlases and preserve adjacent compatible commands
where visual order allows; this experiment does not justify arbitrary draw
reordering.

The buffer sweep rotates configurations across three rounds, using Immediate
mode. Values again show median per-trial frame-work p50 / p99 in milliseconds:

| Packed frame buffers | Edit | Tear-out |
| --- | ---: | ---: |
| 1 | 1.312 / 3.636 | 1.925 / 7.598 |
| 2 (default) | 1.355 / 3.201 | 1.976 / 7.156 |
| 3 | 1.295 / 2.947 | 1.967 / 6.417 |

Three buffers improve tear-out p99 versus two in all three rounds, but this is
a modest, configuration-specific result with an extra packed-buffer allocation.
Two remain the default; `--frame-buffers 3` allows controlled tuning. These runs
occurred after the presentation sweep and have different desktop-load conditions;
compare configurations within this sweep rather than mixing their absolute
times with the presentation table. Buffer count controls renderer storage,
not SDL's presentation queue depth or a measured input-latency bound.

### Remaining interaction costs

The immediate-mode resize runs show a median frame cost of 29.76 ms on the 120
frames where the measured window dimensions actually change, with a maximum
of 82.52 ms. X11 applies the scripted resize 1–13 frames after its request,
so looking only at request/action frames misses these stalls. Of the 30 slowest
resize frames, 29 coincide with actual size changes. Swapchain acquisition,
submission, and event processing account for most of the total resize workload;
UI and preparation account for about 5.0% and 2.8% respectively.

Across the three immediate tear-out trials, the 15 cached reactivations have
lifecycle median/max of 63.41/114.30 ms. The 15 parks have lifecycle median/max
of 17.42/51.38 ms. Each process creates one physical window, reuses it five
times, parks it five times, and destroys it once at final cleanup. Caching
therefore avoids repeated creation/destruction but does not eliminate X11
mapping costs. Some work arrives later in event processing: preserve the
close-plus-following-five-frame summaries when evaluating a future change.

The official SDL 3.2.18 source contains blocking MapNotify/UnmapNotify waits in
[X11 show/hide](https://raw.githubusercontent.com/libsdl-org/SDL/release-3.2.18/src/video/x11/SDL_x11window.c)
and a device-idle wait during
[Vulkan swapchain recreation](https://raw.githubusercontent.com/libsdl-org/SDL/release-3.2.18/src/gpu/vulkan/SDL_gpu_vulkan.c).
These are plausible targets for controlled SDL-version,
window-manager/compositor, and backend comparisons. Per-call timing is still
needed to divide the lifecycle cost conclusively.

Cache eviction frames do not explain these immediate-mode stalls: combined
renderer preparation during eviction was 0.034–0.164 ms for resize and
0.035–0.104 ms for tear-outs. No eviction coincided with a tear-out lifecycle
action in these captures.
