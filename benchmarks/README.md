# CPU before/after benchmark

`bench_gui.c` uses a fixed 16-frame corpus for changing text. It measures 128 labels
with the shipped Inter font and a synthetic 4,096-glyph / 4,096-kerning font.
Checks use explicit error exits, so `/DNDEBUG` does not disable validation.

For measured application frames, see the [demo performance summary](PERFORMANCE.md)
and its [selected data and provenance](performance.json). The CPU harness below
supports new comparisons using source trees or executables that you preserve.

## Extended GUI workloads

`--suite extended` adds CPU preparation through `rg_gui_gpu_prepare`, 8 KiB
text-area wrapping at widths 300 and 1200, and a repeatable click-and-hold
text-area frame that performs picking, selection handling, and drawing.
There is no GPU execution. The original label workloads remain included.

Add `--define RG_GUI_BENCH_LOOKUP=1` to a build to attach the optional shared
ASCII lookup tables when the header supports them. The default build leaves
the option off. Lookup initialization and its 66,568-byte allocation are outside
the measured intervals; before headers without this API still use ordinary
measurement. Record this option when reporting results.

```powershell
python benchmarks/run.py compare --suite extended --define RG_GUI_BENCH_LOOKUP=1 --before-root out/baseline/src --after-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --output out/perf-extended
```

For an isolated experiment, repeat `--case NAME` to select extended-suite
workloads, for example `--case gpu_prepare_static_warm --case gpu_prepare_cold`.
These selections also work with previously built extended-suite executables.
The runner checks exactly the selected outputs and still alternates five trials.
Set `RG_GUI_BENCH_VERIFY=1` when invoking an extended binary directly to check
the six additional workloads without timing them. The comparison runner clears
this environment variable before collecting measurements.

Requirements: Windows, Python 3.9+, Visual Studio C++ tools, rg_core, rg_text,
and an x64 SDL3 development package. Nothing is downloaded. An existing x64
Developer Command Prompt is used when `cl.exe` is available; otherwise the
runner locates Visual Studio with `vswhere` and initializes `VsDevCmd.bat`.

## Build the baseline before editing

Run from the repository root, using your own dependency paths:

```powershell
python benchmarks/run.py build --gui-root . --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --output out/perf-before.exe
```

After making the change, build an identical harness with the changed headers:

```powershell
python benchmarks/run.py build --gui-root . --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --output out/perf-after.exe
python benchmarks/run.py compare --before-bin out/perf-before.exe --after-bin out/perf-after.exe --sdl-root C:/deps/SDL3 --output out/perf-comparison
```

Alternatively, compare two preserved GUI header trees directly:

```powershell
python benchmarks/run.py compare --before-root out/baseline/src --after-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --output out/perf-comparison
```

`--output` for comparison must name a new directory, preventing samples from
different runs from being mixed. Header roots accept either a repository or its
`src` directory. Defaults use `RG_CORE_DIR`, `RG_TEXT_DIR`, and `SDL3_DIR` when
set; core/text otherwise default to sibling repositories. For a split SDL
installation, use `--sdl-include` and `--sdl-lib` (the full `SDL3.lib` path), plus
`--sdl-bin` for the DLL directory if needed. The DLL directory can also already
be on `PATH`. Keep dependency roots unchanged between builds when measuring a
GUI-only change. To compare dependencies, build each executable separately
with its intended dependency roots and pass both binaries to `compare`.

## Method and outputs

- Same `/std:c11 /O2 /DNDEBUG /W4 /WX` flags for both variants; no LTO.
- Five process trials per variant, serial execution with alternating pair
  order: before/after, after/before, before/after, after/before, before/after.
- Logical CPU 0 affinity, inherited by each child process. Failure to set
  affinity stops the comparison. Normal OS scheduling and power settings apply.
- Seven calibrated samples per workload per process. Dynamic measurements
  cycle exactly 16 fixed frames, so differing calibration counts do not change
  the distribution of text. Static and dynamic frontend cases explicitly
  measure widths before emitting draw commands.
- Raw `before_*.jsonl` / `after_*.jsonl` retain every sample. `results.json`
  contains process medians; `summary.json` and `summary.md` report medians of
  those five medians. JSON also includes their minimum/maximum range.
- Exact output signatures compare widths, geometry/table hashes, glyph counts,
  cache counters, and font metadata across all runs. Incomplete or mismatched
  output fails the run. `signatures.json` records the verified signature.
- `manifest.json` records executable/font/source hashes and completion status.
  Runner-built binaries have adjacent `.build.json` files recording compiler
  command and header hashes, copied into the comparison manifest when present.
  Stale metadata whose executable hash differs from the binary is rejected.
  Prebuilt binaries without these files have no verified build provenance.

The default label suite covers frontend measurement/emission, warm renderer identity/content
cache hits, cold renderer preparation, and renderer initialization. They do
not exercise text-area wrapping or layout reuse. Initialization reuses warm
allocated memory and excludes allocation, font loading, and GPU setup. Cold
preparation excludes cache clearing and pays per-frame timer overhead. Fonts
are loaded before timing; corpus characters are supported by their fonts, so
fallback correctness requires separate tests.

These are CPU microbenchmarks, not GPU timings, FPS estimates, or end-to-end
application measurements. Avoid concurrent builds, tests, benchmarks, or heavy
background activity. Small differences should be interpreted alongside the
process-median ranges, and warm-memory initialization savings are one-time
costs. Use the same harness, compiler, flags, dependencies, power settings,
and hardware for each side of a comparison.

## Optional SSE2 geometry packing

`bench_geometry.c` compares the portable rectangle/image packer with
`RG_GUI_GPU_USE_SSE2=1`, using identical current GUI and dependency headers.
The six cases cover direct packing and complete CPU preparation for 1, 128,
and 1,024 images. Both executables run guarded geometry checks before timing.
This benchmark requires an x64 Windows host and uses the same MSVC setup as
the label runner; it does not submit GPU work.

```powershell
python benchmarks/run_geometry.py --gui-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --output out/perf-geometry
```

To finish compilation before other validation or timing, use `--build-only`:

```powershell
python benchmarks/run_geometry.py --gui-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --build-only --output out/geometry-build
python benchmarks/run_geometry.py --portable-bin out/geometry-build/build/portable.exe --sse2-bin out/geometry-build/build/sse2.exe --sdl-root C:/deps/SDL3 --output out/geometry-results
```

Prebuilt executables require their adjacent `.build.json` files, which are
checked against executable/source hashes, backend defines, and identical header
hashes. Comparison output directories must be new. Five serial process trials
per backend alternate order on logical CPU 0. Raw trial JSONL, verified geometry
signatures and vertex/item counts, process-median ranges, summaries, and build
provenance remain in the output directory. Each process reports seven-sample
timing minimum/median/maximum values for every case.

For real-window frame percentiles, demo renderer counters, repeatable interaction
scenarios, and loaded-font Unicode workloads, see [Profiling the demos and Unicode](PROFILING.md).

The [demo performance summary](PERFORMANCE.md) covers presentation modes and
native-window reuse, including frame-time variation and the memory/cleanup
tradeoff. Its historical snapshots are distinct from a new comparison of the
current source tree.
