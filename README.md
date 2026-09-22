# rg_gui

Immediate-mode C UI with docking, native viewports, persistent text caching,
and SDL3 GPU rendering.

`rg_gui` is currently a pre-1.0 library. The public API and serialized state
formats may change between 0.x releases; the [changelog](CHANGELOG.md) records
changes and compatibility notes.

The library depends directly on
[`rg_core`](https://github.com/superwendel/rg_core),
[`rg_text`](https://github.com/superwendel/rg_text), and SDL3. It provides the
frontend, persistent text preparation, and SDL3 GPU backend used by the example
applications in this repository.

## Contents

- `src/rg_gui.h` - widgets, layout, interaction, docking, viewports, and ordered
  draw-list generation.
- `src/rg_gui_renderer.h` - persistent page-cached text preparation with
  32-glyph pages by default.
- `src/rg_gui_gpu.h` - SDL3 GPU rendering for rectangles, triangles, images,
  application-defined image materials, text, clipping, and draw-list order.
- `shaders/` - HLSL sources for the graphics and compute paths. Generated
  backend binaries are intentionally ignored.

The frontend uses `RgTextFont` directly for measurement. The renderer caches
`rg_text` glyph geometry and sends compact run descriptors to a compute shader,
which expands final text instances on the GPU. The steady-state frontend and
text-preparation paths use fixed-capacity storage selected at initialization;
they do not grow their arenas during a frame.

`rg_gui_init` requires a descriptor with a non-null font. Use
`rg_gui_memory_required` with that same descriptor to allocate the frontend
arena exactly; the demos do this rather than relying on a guessed reserve.

Changing text can opt into a shared `RgGuiTextLookup` through
`RgGuiInitDesc.text_lookup`. It costs about 65 KiB per font and accelerates ASCII
measurement. The renderer can borrow the same table via
`RgGuiRendererInitDesc.text_lookup`; use `rg_gui_renderer_memory_required_ex`
to omit its duplicate storage. The demos share one table per font. See the
[lookup ownership rules](docs/rg_gui.md#include-and-initialize).

See [the API and integration notes](docs/rg_gui.md) for the complete lifecycle,
capacity behavior, platform output, and ownership rules.

## Dependency baseline

Use these pinned dependency revisions for reproducible builds:

| Dependency | Revision or version |
| --- | --- |
| `rg_core` | `d4787158faa3366357d16ba2e1e466ee40276749` |
| `rg_text` | `4b98c6d38d4de1398ebb0970fc8e0e3db01bafa5` |
| vcpkg ports | baseline `91e8cb4be8195112ea3a9c7e5846bd0b3ff74673` |
| SDL3 | 3.4.10 used for local Windows release validation; 3.4.14 configured in Linux sanitizer CI |

The Windows manifest install resolves SDL3 through the vcpkg baseline. Local
GPU and performance measurements used a separate SDL3 3.4.10 installation.

The optional Windows atlas baker at that baseline uses FreeType `2.14.3#0`
and HarfBuzz `14.4.0#0`; neither is a runtime dependency. The exact feature
selection, source-font hash, command, and output hashes are recorded in
[`examples/assets/README.md`](examples/assets/README.md).

## Clean Windows setup

Use a Visual Studio 2022 Developer Command Prompt with the Desktop development
with C++ workload. Place the three repositories beside one another so the
default dependency paths resolve:

```bat
git clone https://github.com/superwendel/rg_core.git
git clone https://github.com/superwendel/rg_text.git
git clone https://github.com/superwendel/rg_gui.git
git clone https://github.com/microsoft/vcpkg.git

git -C rg_core checkout d4787158faa3366357d16ba2e1e466ee40276749
git -C rg_text checkout 4b98c6d38d4de1398ebb0970fc8e0e3db01bafa5
git -C vcpkg checkout 91e8cb4be8195112ea3a9c7e5846bd0b3ff74673
call vcpkg\bootstrap-vcpkg.bat -disableMetrics

cd rg_gui
set "VCPKG_ROOT=%CD%\..\vcpkg"
"%VCPKG_ROOT%\vcpkg.exe" install --triplet x64-windows
build.bat test_ci
```

The manifest install creates `vcpkg_installed\x64-windows`. `build.bat` detects
that layout. For an existing SDL installation, set `SDL3_DIR`; advanced builds
can instead set `SDL3_INCLUDE_DIR`, `SDL3_LIB_DIR`, and `SDL3_BIN_DIR`.
`SHADERCROSS_EXE` selects an explicit SDL_shadercross executable.

If the repositories are not siblings, set `RG_CORE_DIR` and `RG_TEXT_DIR` to
their repository roots before invoking `build.bat`.

## Build and test

From the `rg_gui` directory:

```bat
build.bat test
build.bat test_ci
build.bat shaders
build.bat test_gpu_device
build.bat demo
build.bat demo_smoke
build.bat test_release
```

- `test` runs renderer-neutral and GPU-preparation tests without executing a GPU
  command buffer.
- `test_ci` also translates every shader format, compiles the device target,
  validates the demo assets and their recorded SHA-256 hashes, and builds all
  demos. Hosted CI does not claim to execute a GPU device.
- `test_gpu_device` executes the hidden-window SDL_GPU device test.
- `test_demo_lifecycle` exercises native tear-out window reuse, restoration,
  text-input cleanup, and final destruction with actual SDL GPU windows.
- `demo_smoke` builds all three demos, briefly opens their windows, and requires
  three actual main-window presentations from each. The tear-out smoke also
  creates a secondary native window and presents its viewport at least once.
- `test_release` is the local release gate: it includes the device test and
  runs those presentation-verified demo smokes and native lifecycle checks.

`build.bat clean` removes only generated files rooted in this checkout.

See the [performance report](benchmarks/PERFORMANCE.md) for measured results
and their limits, and the [benchmark instructions](benchmarks/README.md) to
run repeatable CPU comparisons.

## Demos

`build.bat demo` produces three examples using the checked-in Inter Medium
assets in `examples/assets`:

- `rg_gui_demo_minimal.exe` shows one movable window and a small set of controls
  in an end-to-end SDL3 GPU loop.
- `rg_gui_demo_full.exe` is a dockable showcase with menus, inspector widgets,
  plots, assets, hierarchy, notes, editors, and a node graph. Its View menu offers
  VSync, **Fast, no tearing** (Mailbox), and **Uncapped, may tear** (Immediate),
  with unsupported modes disabled. Frame Stats shows
  CPU stage timings, recent frame percentiles, draw calls, uploads, and text-cache counters.
- `rg_gui_demo_tearout.exe` demonstrates native multi-window viewports and
  moving docked panels between windows. It reuses one hidden native window to
  reduce close and reopen delays, releasing its resources at shutdown.

Run them with `build.bat run_demo_minimal`, `build.bat run_demo_full`, or
`build.bat run_demo_tearout`. `build.bat run_demo` aliases the full demo.
The full and tear-out demos default to VSync. Use `--present mailbox` for
tear-free presentation that allows rendering faster than the display refresh;
for example, `.\rg_gui_demo_full.exe --present mailbox`. Mailbox displays the
latest completed frame at refresh, so submitted frames can be replaced before
they become visible. `--present vsync` selects VSync; `--present immediate`
or its alias `--no-vsync` selects Immediate, which can tear. An unsupported
requested mode reports an error. `--start-torn-out` opens the tear-out demo's
secondary window. Every demo also
accepts `--smoke-test`, which briefly opens its windows, verifies swapchain
presentation, and exits. The tear-out smoke creates and renders its secondary
window as well. Use `--smoke-test` separately from `--profile`; smoke tests end
after only a few presented frames. See the [profiling guide](benchmarks/PROFILING.md)
for CSV capture, repeatable frame workloads, and interpreting the telemetry.

The demos compile with `/O2` and `NDEBUG` and use the portable C numeric
formatter. The live FPS display measures frame production; visible updates
also depend on the presentation mode and display refresh rate.

When deploying a demo, copy its two font assets and `shaders/Compiled` beside
it using the layouts described in [the asset notes](examples/assets/README.md)
and [the integration guide](docs/rg_gui.md). Demo shader lookup checks beside
the executable first and then the current working directory.

## Build model and platform status

The headers use internal linkage and are written for unity builds.
`rg_gui.h` intentionally has no include guard and emits a diagnostic if it is
included more than once in a translation unit. Include it once, normally
through `rg_gui_gpu.h` when using the SDL3 GPU renderer.

The tested language modes are C11 with MSVC and GNU11 with Clang. `rg_core`
uses compiler extensions for type-safe utility macros, so this stack does not
claim ISO C99 conformance.

| Platform | Current verification |
| --- | --- |
| Windows x64 / MSVC | Full build, shader translation, demos, and local SDL_GPU device execution |
| Linux x64 / Clang | CI configured for ASan/UBSan host tests and SDL_GPU device-target compilation; hosted results not yet verified; GPU execution untested |
| macOS | Not currently tested; emitting MSL shader output is not a macOS support claim |

Windows x64 applications should assemble and link rg_core's
`src/asm/sprintf/win_x64/rg_sprintf_asm_x64.asm` to retain the optimized
numeric-formatting path. `RG_SPRINTF_NO_ASM` selects the portable C fallback.

## License and trademark

The software and documentation are available under the [MIT License](LICENSE).
The bundled Inter-derived demo font assets are licensed under the
[SIL Open Font License 1.1](examples/assets/LICENSE-INTER.txt). Trademark terms
are separate from the software license; see [TRADEMARKS.md](TRADEMARKS.md).
