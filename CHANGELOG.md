# Changelog

## Unreleased

`rg_gui` is a pre-1.0 C UI library. Public APIs and serialized state may change
between 0.x releases.

### Added

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

- Batch compatible text, shapes, and images through an indexed GPU stream.
- Share optional ASCII lookup tables between the frontend and text renderers,
  and offer optional SSE2 geometry packing alongside the portable path.
- Bound frame-run capacity independently of glyph capacity and expose GPU
  cache invalidation for font or CPU renderer reinitialization.
- Add **View > Fast, no tearing** (Mailbox) to the full showcase. Full and
  tear-out demos default to VSync; `--no-vsync` requests Immediate presentation.
- Reuse one hidden native tear-out window and swapchain until shutdown.

### Fixed

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
