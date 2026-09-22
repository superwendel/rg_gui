# Demo and Unicode profiling

The full and tear-out demos record real window frames, including SDL GPU
submission and presentation waits. The Unicode harness separately measures CPU
text work. These complement the [CPU before/after benchmarks](README.md).
Keep builds, tests, and other benchmarks separate from measurement runs.

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
bytes, cache behavior, diagnostics, and dropped work. A submitted frame can have
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

`--no-vsync` is sent to the demos by default, requesting immediate presentation.
Use runner option `--present vsync`, `--present mailbox`, or `--present immediate`
to compare presentation modes; `--vsync` remains an alias for `--present vsync`.
These options are mutually exclusive. An unsupported requested mode fails the
profile, and the runner checks the actual mode reported by the demo. Direct demo
launches still default to VSync. CPU affinity is unchanged by default so the
OS schedules the real application; `--cpu-zero` explicitly pins it to logical
CPU 0. Keep presentation mode, affinity, power settings, compiler, dependencies,
and hardware consistent when comparing runs.

For manual Full-demo testing, choose **View > Fast, no tearing** or launch
`.\rg_gui_demo_full.exe --present mailbox`. Mailbox may replace completed
submissions before display; its frame-production rate is not the display's
refresh rate. The [performance summary](PERFORMANCE.md) compares saved Immediate,
Mailbox, and VSync snapshots and native-window reuse; [selected data](performance.json)
retain the per-trial evidence and provenance.

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
compiler flags from an executable.

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
| `manual` | Direct demo launch only; interact normally while recording. Both full and tear-out demos support it. |

The image control deliberately changes texture order while preserving geometry
and image content. It demonstrates the renderer's existing adjacent batching;
it does not add automatic draw reordering. Arbitrary UI commands cannot in
general be reordered without changing appearance.

The tear-out scenario uses the same panel/window requests as the demo controls.
The current demo parks one hidden claimed window on redock/close, then reuses it
on the next activation. It retains that window and swapchain until final cleanup.
Initial creation, parking, and reuse are included in `lifecycle_ms`; older demo
binaries instead destroy/recreate the window and include their GPU-idle waits.
Post-warmup lifecycle spikes remain in the reported maximum and percentiles.
Initial warmup and process shutdown are not part of the main summary. The
current demo reports separate physical creation/reuse/parking/destruction counts
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

Freeze the previous tear-out executable before rebuilding. The dedicated runner
accepts two prebuilt executables and alternates their process order, with three
trials each in both immediate and VSync modes by default:

```powershell
python benchmarks/run_native_lifecycle.py --before-bin out/before/rg_gui_demo_tearout.exe --after-bin rg_gui_demo_tearout.exe --sdl-root C:/deps/SDL3 --output out/native-comparison
```

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

The Windows runner uses the existing MSVC build setup. It performs correctness
checks before timing, then five serial process trials on logical CPU 0. Each
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

RenderDoc 1.44's D3D12 GPU counter collection requires Windows Developer Mode.
If it reports that requirement, counters are unavailable; ordinary GUI rendering
and CPU profiling do not require Developer Mode. Setting the process environment
`SDL_GPU_DRIVER=vulkan` requests a separate Vulkan experiment, and the script
records that request and verifies the captured API. Keep such GPU results
separate from D3D12 measurements. Only report GPU durations when the captured API
and required counter coverage have been verified.
