# Changelog

## Unreleased

`rg_gui` is a pre-1.0 C UI library. Public APIs and serialized state may change
between 0.x releases.

### Added

- `rg_gui_gpu_frame_signature`: an opt-in digest of the prepared draw list
  (`RgGuiGpuDesc.frame_signature`) so applications can skip staging, drawing,
  and presenting frames that would reproduce the pixels already on screen. The
  minimal demo demonstrates it.
- General glyph and kerning hash tables in `RgGuiTextLookup`, so non-ASCII text
  measures and lays out as fast as ASCII for fonts of up to 4,096 glyphs and
  kerning pairs. The table grows from 66,568 to 99,352 bytes;
  `RG_GUI_TEXT_LOOKUP_GLYPH_SLOTS` and `RG_GUI_TEXT_LOOKUP_KERNING_SLOTS`
  adjust or remove the tables.
- A performance audit of the frame path with measurements and remaining
  opportunities in `docs/performance-audit-2026-10.md`.
- Linux GCC/Clang builds, SPIR-V dependency setup, sanitizer CI, and native
  X11/Vulkan validation through `build.sh`.
- Linux CPU/frame profiling, compiler and runtime provenance, upload-copy
  counters, and `--frame-buffers 1|2|3` controls in the profiling demos.
- Optional caller-owned viewport command storage, allocated on first use and
  retained for reopening. Eager arena allocation remains the default.
- `rg_gui_gpu_upload_ring_size_required` to size an upload buffer for a maximum
  frame and full cache upload.
- A caller-owned text-area layout cache for reusing wrapped lines while edits,
  selection, cursor movement, and IME remain interactive.
- Application-defined GPU image materials with draw-list order and clipping.
- Demo frame profiling and repeatable CPU, text, image, and native-window
  workloads. See the [benchmark guide](benchmarks/README.md) and
  [profiling guide](benchmarks/PROFILING.md).

### Changed

- Pointer-keyed text measurement and length caches probe at most
  `RG_GUI_TEXT_CACHE_MAX_PROBES` slots (32) and then replace the home slot, so a
  saturated cache no longer scans every slot on each miss.
- Text-area wrapping computes each UTF-8 boundary once, and boundary stepping
  skips the backward scan for ASCII and lead bytes.
- Batch compatible text, shapes, and images through an indexed GPU stream.
- Share optional ASCII lookup tables between the frontend and text renderers,
  and offer optional SSE2 geometry packing alongside the portable path.
- Bound frame-run capacity independently of glyph capacity and expose GPU
  cache invalidation for font or CPU renderer reinitialization.
- Add **View > Fast, no tearing** (Mailbox) to the full showcase. Full and
  tear-out demos default to VSync; `--no-vsync` requests Immediate presentation.
- Reuse one hidden native tear-out window and swapchain until shutdown.

### Fixed

- Strict GCC builds on LP64 Linux and decorated-window tear-out hit testing.
- Restore cached maximized/minimized tear-outs reliably on X11 before applying
  their requested size and position.
- Editor mouse capture release and stale numeric text.
- Fallback-glyph spacing and text-command association when earlier commands
  are skipped or dropped.
- Repeated native close events incorrectly quitting the tear-out demo.

### Migration notes

- Ordinary text APIs copy changing text. Use explicit `*_static` APIs only for
  immutable strings; see [text ownership](docs/rg_gui.md#ownership-text-identity-and-threads).
- GPU upload packets remain immutable until commit or abort. Pass the staged
  packet to `rg_gui_gpu_draw` and `rg_gui_gpu_dispatch_upload`. Acknowledge
  successful submission with
  `rg_gui_gpu_upload_commit`; abort failed or discarded packets. See the
  [frame and GPU flow](docs/rg_gui.md#frame-and-gpu-flow).
- Replace `rg_input_update` with `rg_input_begin_frame` before SDL event polling
  and `rg_input_sample` afterward. See [ordered input](docs/rg_gui.md#ordered-input).
