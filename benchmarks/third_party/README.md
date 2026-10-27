# Local Dear ImGui comparison

This is a public **Linux/Vulkan reconstruction**, not the private Windows harness
behind [the older results](../PERFORMANCE.md). It compares the current `rg_gui`
headers with a pinned Dear ImGui docking snapshot and its unmodified SDL_GPU
backend. It performs no swapchain presentation.

Requirements: Linux, Python 3.9+, GCC/G++, SDL 3.4+, Vulkan, sibling `rg_core` and
`rg_text` repositories, and the shadercross installation used by `build.sh`.
Run from the `rg_gui` repository root. Supply Dear ImGui revision
`3bae66c735670619baf51391eba7f3d90a25d125` (1.93.0 WIP docking). For example,
clone `https://github.com/ocornut/imgui` into an ignored dependency directory and
check out that revision; the runner does not download dependencies.

```sh
python3 benchmarks/third_party/run.py build \
  --imgui-root out/third-party-linux-2026-10-26/imgui-docking \
  --build-dir out/imgui-comparison-build

python3 benchmarks/third_party/run.py verify \
  --binary out/imgui-comparison-build/compare_gui \
  --output out/imgui-comparison-verification

python3 benchmarks/third_party/run.py measure \
  --binary out/imgui-comparison-build/compare_gui \
  --validation out/imgui-comparison-verification \
  --output out/imgui-comparison-measurement
```

Output directories must be new. Verification runs before timing and must match
its executable hash. By default it exercises 512 frames for both libraries,
all eight workloads, and both queue limits. It checks actual keystrokes,
checkbox toggles, slider drags, and row selections; saves complete readbacks;
compares core text within two color levels; and checks text is visible in each
native-widget region. Native widget/docking shapes are allowed to differ.

The measured suite uses six alternating library pairs, rotating workload order,
512 warmup and 2,048 measured frames per process. It pins processes to the first
allowed logical CPU. Every measured frame and each process summary is saved;
there is no percentile/performance filtering. Initialization, font import,
shared fixture generation, semantic inspection, capture, and CSV/JSON output
are outside the frame timer. The final GPU queue drain is included in completed
throughput, but outside individual frame percentiles.

The 1,280 by 800 RGBA8 target, font bitmaps/advances, font size, numeric precision,
visible content, and one/two outstanding-fence limit are shared. Kerning,
shape antialiasing, and rounding are disabled. Dear ImGui imports the shipped
Inter atlas through a small font loader; it does not rerasterize a different
font. RG uses its shipping indexed renderer, shared ASCII lookup, 32-glyph cache
pages, a 1,024-run / 65,536-quad cache, and a caller-owned text-area layout cache.
These cache reservations are part of its memory/performance tradeoff. Neither
library uses unchanged-frame suppression or a tile cache in this comparison.

The cases submit:

- 128 static labels, and the same labels changing through a 16-frame corpus.
- 32 native checkboxes and 32 native sliders, with changing numeric values.
- 32 application-virtualized rows from a 10,000-row index range.
- The inspector with replayed checkbox clicks and slider drags.
- An asset filter field and 32 application-filtered/virtualized selectable rows.
- An 8 KiB multiline native editor, with actual alternating insertion/backspace.
- Four native docked panels containing 32 visible labels each, with the layout
  restored every 256 frames. Dear ImGui's initial docking-window settlement is
  performed during initialization; later layout resets remain timed.

The browser/docking replay is narrower than the archived Windows workload:
filter/scroll fixtures come from the application, and docking uses periodic
programmatic restoration rather than replaying splitter/tab mouse gestures.
This run also uses the docking branch for **all** cases, and a different machine,
compiler, SDL version, backend and sample length. Do not splice these figures
into the Windows table or call them an exact rerun of that private harness.

Three experiment hooks are outside the measured protocol. `--replicate N`
encodes the prepared frame N times into one render pass, so a line fitted
through frame time against N separates per-content GPU cost (slope) from fixed
per-frame cost (intercept); the runner never passes it. `RG_BENCH_SHADER_ROOT`
points the rg_gui adapter at another directory holding `Compiled/SPIRV`, for
A/B tests of shader variants. `RG_BENCH_UPLOAD_STATS` prints the rg_gui
adapter's per-frame upload sizes and text-cache counters to stderr.

`ui` includes native widget submission plus CPU renderer preparation. `frame`
includes CPU submission and GPU-fence queue waits. These are CPU wall-time
measurements, **not hardware GPU timestamps, display FPS, input latency, or
power measurements**. Native widgets do not have cross-library pixel equality;
readback/semantic checks document what is actually comparable. Report process
ranges alongside small differences and record other desktop activity.
