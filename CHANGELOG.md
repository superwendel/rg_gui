# Changelog

## Unreleased

`rg_gui` is a pre-1.0 C UI library. Public APIs and serialized state may change
between 0.x releases.

- Add application-defined GPU image materials while preserving draw-list order
  and clipping.
- Reduce repeated text preparation, text-area wrapping, and layout work. Add
  optional shared ASCII lookup tables and optional SSE2 image packing; the
  portable paths remain available.
- Correct fallback-glyph spacing and text-command association when earlier
  commands are skipped or dropped.
- Reuse one hidden native tear-out window to reduce close/reopen stalls. Release
  its resources at shutdown, and handle repeated native close events safely.
- Add demo frame profiling and repeatable CPU, text, image, and native-window
  benchmark workloads.
- Add Mailbox presentation to the full and tear-out demos. The full showcase
  exposes it as **View > Fast, no tearing**. Normal launches keep VSync enabled;
  `--no-vsync` continues to request Immediate presentation.
- Share the frontend ASCII lookup with text renderers, with descriptor-aware
  arena sizing and an owned fallback for existing callers.
- Reuse `rg_algo` selection for paired demo frame percentiles, copying the
  history once. Use `rg_snprintf` for demo integer/string telemetry and status;
  retain SDL decimal formatting to preserve its rounding behavior.
- Update the `rg_core` baseline to `d478715`; demo input loops now call
  `rg_input_begin_frame` before event polling and `rg_input_sample` afterward.
  Integrations using the removed `rg_input_update` must migrate likewise.
- Pin `rg_core`, `rg_text`, and CI tooling revisions for reproducible builds.

The demos enable the optional ASCII lookup and SSE2 paths on supported targets.
Library consumers opt in explicitly. A shared lookup occupies 66,568 bytes per
font on x64; its caller-owned font must remain immutable while attached. Native
window reuse retains one window and swapchain until shutdown.

Local validation covers Windows x64/MSVC with SDL 3.4.10/D3D12, including GPU
device execution and native-window lifecycle checks. Linux Clang ASan/UBSan is
configured in CI; local GPU results do not establish Linux or macOS GPU support.

See the [integration guide](docs/rg_gui.md) for API ownership rules and the
[performance report](benchmarks/PERFORMANCE.md) for measurements and their scope.
