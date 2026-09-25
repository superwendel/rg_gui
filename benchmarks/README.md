# CPU benchmarks

`bench_gui.c` uses a fixed 16-frame corpus for changing text. It measures 128 labels
with the shipped Inter font and a synthetic 4,096-glyph / 4,096-kerning font.
Checks use explicit error exits, so `/DNDEBUG` does not disable validation.

For real-window frame measurements and presentation modes, see
[Profiling the demos and Unicode](PROFILING.md).
For measured comparisons with another GUI library, see
[Performance against Dear ImGui](PERFORMANCE.md).

## Requirements

These runners require Windows, Python 3.9+, Visual Studio C++ tools, rg_core,
rg_text, and an x64 SDL3 development package. Follow the repository's
[dependency setup](../README.md#dependencies) first. Nothing is downloaded by
the benchmark runners. They use an existing x64 Developer Command Prompt when
`cl.exe` is available; otherwise they locate Visual Studio with `vswhere` and
initialize `VsDevCmd.bat`.

Run commands from the repository root, replacing `C:/deps/SDL3` with your SDL3
installation. Defaults use `RG_CORE_DIR`, `RG_TEXT_DIR`, and `SDL3_DIR` when set;
core/text otherwise default to sibling repositories. For a split SDL installation,
use `--sdl-include` and `--sdl-lib` (the full `SDL3.lib` path), plus `--sdl-bin`
for the DLL directory if needed. The DLL directory can also be on `PATH`.

## Measure the current checkout

Build the label benchmark, then run it from the repository root so it can find
the shipped font. This example uses the SDL3 VC development package layout:

```powershell
python benchmarks/run.py build --gui-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --output out/perf-current.exe
$env:PATH = "C:/deps/SDL3/lib/x64;C:/deps/SDL3/bin;$env:PATH"
.\out\perf-current.exe
```

For a vcpkg installation, the DLL is normally in its `bin` directory. The
executable prints JSON lines with font metadata, timing samples, output checks,
and per-case results. A direct launch is one process trial with normal OS
scheduling; it does not produce the comparison runner's manifests or pin CPU
affinity. Use the paired workflow below to evaluate a change.

## Compare a change

Before editing, build an executable to preserve the baseline:

```powershell
python benchmarks/run.py build --gui-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --output out/perf-before.exe
```

After editing, build the same harness with the changed headers, then compare:

```powershell
python benchmarks/run.py build --gui-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --output out/perf-after.exe
python benchmarks/run.py compare --before-bin out/perf-before.exe --after-bin out/perf-after.exe --sdl-root C:/deps/SDL3 --output out/perf-comparison
```

Keep each executable's adjacent `.build.json` file. Keep dependency roots
unchanged when measuring a GUI-only change. To compare dependencies, build each
executable separately with its intended dependency roots and pass both binaries
to `compare`.

Alternatively, compare two GUI header trees you have preserved:

```powershell
python benchmarks/run.py compare --before-root out/baseline/src --after-root src --core-root ../rg_core --text-root ../rg_text --sdl-root C:/deps/SDL3 --output out/perf-comparison
```

`out/baseline/src` is an example directory you create from the version you want
to compare; it is not included in the repository. Header roots accept either a
repository or its `src` directory. Comparison output must name a new directory
so samples from different runs are not mixed.

## Extended GUI workloads

`--suite extended` adds CPU preparation through `rg_gui_gpu_prepare`, 8 KiB
text-area wrapping at widths 300 and 1200, and a repeatable click-and-hold
text-area frame that performs picking, selection handling, and drawing.
There is no GPU execution. The label workloads remain included.

Add `--define RG_GUI_BENCH_LOOKUP=1` to a build to attach the optional shared
ASCII lookup tables. The default build leaves the option off. Lookup
initialization and its 66,568-byte allocation are outside the measured intervals.
Record this option when reporting results and verify the lookup metadata when
comparing different header versions.

To measure renderer lookup sharing, also add
`--define RG_GUI_BENCH_RENDERER_LOOKUP=1` and select `--case renderer_init`.
This requires `RG_GUI_BENCH_LOOKUP=1` and uses
`RgGuiRendererInitDesc.text_lookup` and `rg_gui_renderer_memory_required_ex`.
Its default is off. Build variants with a renderer-owned table and a shared
table separately, then compare their binaries:

```powershell
python benchmarks/run.py build --suite extended --gui-root src --define RG_GUI_BENCH_LOOKUP=1 --output out/renderer-owned.exe
python benchmarks/run.py build --suite extended --gui-root src --define RG_GUI_BENCH_LOOKUP=1 --define RG_GUI_BENCH_RENDERER_LOOKUP=1 --output out/renderer-shared.exe
python benchmarks/run.py compare --suite extended --case renderer_init --before-bin out/renderer-owned.exe --after-bin out/renderer-shared.exe --output out/renderer-lookup
```

These commands use the dependency environment variables described above. The
shared table is initialized outside timing; the renderer uses the smaller
descriptor-sized arena. Result fields `renderer_arena_bytes` and
`renderer_lookup_shared` record that choice separately from output signatures.
These fields describe the renderer arena, not total fixture memory. With
`RG_GUI_BENCH_LOOKUP=1`, both variants keep one frontend lookup in the benchmark
fixture. `fixture_bytes` and `frontend_lookup_count` verify that setup; sharing
changes the renderer descriptor and its arena allocation.

For a complete extended-suite comparison against a preserved baseline:

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

## Method and outputs

The `run.py compare` command uses the following protocol:

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

## Demo helper benchmarks

`bench_demo_helpers.c` compares paired p95/p99 calculation using two `qsort`
calls or `rg_algo`, and formatting the Full demo's ten telemetry lines using
all-SDL formatting or the demos' mixed strategy: `rg_snprintf` for five
integer/string lines and SDL for five decimal lines. Decimal rounding stays
with SDL because the rg backends round some exact halfway values differently.
The fixed 256-input corpus validates matching outputs before timing.

From the repository root in an x64 Developer Command Prompt, set `RG_CORE_DIR`
and `SDL3_DIR` to your dependency roots. For the SDL3 VC development package:

```bat
if not exist out mkdir out
cl /nologo /std:c11 /O2 /DNDEBUG /W4 /WX /D_CRT_SECURE_NO_WARNINGS /I "%RG_CORE_DIR%\src" /I "%SDL3_DIR%\include" benchmarks\bench_demo_helpers.c /Foout\bench_demo_helpers.obj /Feout\bench_demo_helpers.exe /link /LIBPATH:"%SDL3_DIR%\lib\x64" SDL3.lib
set "PATH=%SDL3_DIR%\lib\x64;%SDL3_DIR%\bin;%PATH%"
out\bench_demo_helpers.exe --validate
out\bench_demo_helpers.exe --kernel percentile_qsort --iterations 100000
out\bench_demo_helpers.exe --kernel percentile_rg_algo --iterations 100000
out\bench_demo_helpers.exe --kernel format_sdl --iterations 100000
out\bench_demo_helpers.exe --kernel format_rg --iterations 100000
```

For a vcpkg SDL3 installation, use `%SDL3_DIR%\lib` for `/LIBPATH`.
Each timed batch calculates both percentiles over 120 samples or formats all
ten lines. Each invocation emits one JSON measurement and a checksum; repeat
serial process trials in alternating order and compare checksums within each
pair. Record the compiler, dependency revision, flags, and trial ranges.
This helper does not set CPU affinity or produce application frame timings.
