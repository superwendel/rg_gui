# Changelog

## Unreleased

`rg_gui` is a pre-1.0 C UI library. Public APIs and serialized state may change
between 0.x releases.

### Added

- A `docks` profiling scenario and `--dock-panels N` option in the full
  showcase, which dock up to 60 extra property panels as tabs in up to three
  new columns to measure how frame cost grows with the number of panels.
- `rg_gui_gpu_tiles.h`: an opt-in dirty-tile pass that bins prepared commands
  into 64-pixel tiles, digests each tile's command list, and reports the
  rectangles to repaint in a persistent target. The full demo's `--tile-cache`
  path uses it, `--tile-cache-skip-present` neither draws nor presents
  unchanged frames, and `--tile-cache-verify` checks it against full repaints
  by readback. A `static` profiling scenario freezes the showcase after warmup.
  Prepared text runs now carry their content hash in the previously reserved
  host field.
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

- `rg_gui_end_frame` now culls hidden draws: a rectangle that lies under a later
  opaque rectangle is dropped, and a large one that an opaque rectangle mostly
  covers is replaced by its visible remainder. Text, images, and triangles are
  untouched and keep their order, pixels are unchanged under any draw offset,
  the overlay start moves with the list, and the culled list is never longer
  than the pushed one. The full showcase painted every pixel 3.3 times with 70
  percent of its rectangle paint hidden; it now paints 1.5 times, and its frame
  work under Immediate presentation falls about a quarter in the idle and edit
  scenarios and 13 percent with 20 docked panels, for about 8 us more CPU per
  frame at full clock. The pass paces itself: when a run saves less than
  32,768 pixels of paint it waits eight frames, or a change in the command
  count, before running again, so an interface with nothing to cull pays under
  1 us per frame on average. `RgGuiInitDesc.keep_hidden_draws` or the context's
  `cull_hidden_draws` switch it off; `rg_gui_diagnostics` reports the frame's
  `culled_draw_commands`, `split_draw_commands`, and `cull_passes`. Context memory grows by 16
  bytes per draw command. The CPU benchmarks keep hidden draws so their output
  signatures stay comparable across versions; the pass has its own extended
  case, `cull_hidden`.
- `rg_gui_lift_backdrop` merges a frame's opaque full-target first rectangle
  with the render pass clear: `rg_gui_end_frame` removes the rectangle from
  the list and `rg_gui_backdrop_lifted` hands its color to the caller for the
  clear. The rectangle passed must be the whole target the pass clears. The
  request is per frame and the demos' direct path uses it (their tile cache
  does not, since dirty-rectangle repaints need the backdrop painted), and
  the demos' unchanged-frame skip keys on the clear color as well as the
  draw-list signature; `--keep-backdrop` turns the lift off in the full
  showcase. Idle and edit frame work under Immediate presentation fall another
  10 to 11 percent.
- The full showcase gains `--offscreen`, which renders without presenting to
  separate the GUI's cost from the platform's, and `--drop KIND`, which
  removes one kind of command from the finished list, for attribution runs.
  On the audit's laptop the GUI's own frame is 347 us at 1280x800 and X11
  presentation adds 900 us.
- The indexed fragment shader reads two input slots instead of three: the
  sampled-or-solid flag now rides in the `uv` slot's third component, and the
  shader samples unconditionally and selects. On an Intel UHD G1 the third
  slot cost about 15 percent of the frame's GPU-paced cost whether it was flat or
  interpolated, branched or selected; without it the pass costs the same as
  Dear ImGui's for the same content, and the comparison in
  `benchmarks/LINUX_IMGUI_2026-10-26.md` has rg_gui lower in every cell. The
  full showcase's frame work under Immediate presentation fell 3 to 4 percent.
  Shader sources only; rebuild the compiled shaders.
- An edit in a text area with a layout cache wraps only the paragraphs it
  touched; lines before them are kept and lines after them move by the length
  change. A keystroke in an 8 KiB document of short paragraphs costs about 3 us
  instead of a 110 us re-wrap; the cost is the edited paragraphs' wrapping plus
  a pass over the document's bytes, so one long paragraph still wraps in full.
  `RgGuiTextAreaLayoutCache`
  gains a `relayouts` counter for these; `rebuilds` now counts only complete
  re-wraps, after key changes, invalidation, or layouts at the line cap.
- Draw commands are 56 bytes instead of 80. The color is stored once, packed as
  the GPU reads it, outside the payload union, and copied text records its byte
  length, so preparation no longer packs colors or measures copied text. With
  VSync, preparation is 19 to 20 percent faster and the library's CPU time per
  frame about 6 percent lower, idle and with 60 docked panels, with identical
  pixels.
- `rg_gui_text_area_find_visual_line` bisects to the cursor's line instead of
  scanning from the top, with identical results: 13 ns instead of 631 ns at
  the end of a 1,024-line text area.
- Dock tab bars keep each tab's measured title width with a copy of its bytes
  and measure again only when the title or text scale changes, so the UI build
  with 60 tabbed panels is 6 to 7 us faster per frame. Titles longer than
  `RG_GUI_DOCK_TAB_TITLE_CACHE` (32 bytes) are measured every frame. Each dock
  tab grows from 40 to 88 bytes, 24 KB at the default 512 tabs; set the macro
  to 0 to remove the cache.
- The indexed shader clips each primitive itself, so a clip change no longer
  ends a draw call: the idle showcase draws in one call instead of 23 and a
  60-panel dock layout in one instead of 42. With VSync that saves 29 and
  54 us of CPU per frame across draw encoding, preparation, and staging, with
  identical pixels and unchanged GPU time. Text run records in the frame
  upload shrink from 32 to 16 bytes.
- Rectangles and images are four vertices instead of six, and triangles are a
  separate `RG_GUI_GPU_ITEM_TRIANGLES` item, so staging no longer compares
  vertices to find quads. In the idle showcase, staging is 41% cheaper,
  preparation 9% cheaper, and the geometry upload 32% smaller, with the same
  draw calls and identical pixels.
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

- The full showcase docks Inspector, the Workspace and Node Graph tabs, and
  Frame Stats into three columns. It previously left half its dock space
  empty, because `rg_gui_dockspace_dock` splits the first leaf.
- Strict GCC builds on LP64 Linux and decorated-window tear-out hit testing.
- Restore cached maximized/minimized tear-outs reliably on X11 before applying
  their requested size and position.
- Editor mouse capture release and stale numeric text.
- Fallback-glyph spacing and text-command association when earlier commands
  are skipped or dropped.
- Repeated native close events incorrectly quitting the tear-out demo.

### Migration notes

- `RgGuiDrawCmd` stores its color once as `cmd->color`, a packed RGBA8 value
  (red in the low byte) instead of `cmd->data.rect.color`, `.triangle.color`,
  `.text.color`, and `.image.color`. Build it with `rg_gui_pack_color`; a
  renderer that wants floats reads `rg_gui_unpack_color(cmd->color)`. Colors are
  rounded to 8 bits per channel when the command is pushed. Text
  commands gain `data.text.length`; zero means the string's length is not
  recorded. `RG_GUI_DRAW_CMD_PACKED_COLOR` is defined for code that supports
  both layouts.
- Ordinary text APIs copy changing text. Use explicit `*_static` APIs only for
  immutable strings; see [text ownership](docs/rg_gui.md#ownership-text-identity-and-threads).
- GPU upload packets remain immutable until commit or abort. Pass the staged
  packet to `rg_gui_gpu_draw` and `rg_gui_gpu_dispatch_upload`. Acknowledge
  successful submission with
  `rg_gui_gpu_upload_commit`; abort failed or discarded packets. See the
  [frame and GPU flow](docs/rg_gui.md#frame-and-gpu-flow).
- Replace `rg_input_update` with `rg_input_begin_frame` before SDL event polling
  and `rg_input_sample` afterward. See [ordered input](docs/rg_gui.md#ordered-input).
- Recompile shaders after updating: `rg_gui_indexed.vert` now reads four
  vertices per quad and clips with clip distances, and an older compiled
  shader draws incorrectly.
- The indexed renderer needs SDL_GPU clip distances: keep
  `SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN` enabled (SDL's
  default) when creating the device. `max_items` must be below 2^29.
- `RgGuiGpuItem.clip_enabled` is now `clip_index`, zero when unclipped and
  otherwise the item's entry in the frame clip table (`RgGuiGpuUpload`
  `clip_count` and `clips_offset`). Stock `RgGuiGpuIndexedBatch` entries have
  `clip_enabled` zero; material batches keep their scissor clip. A renderer
  set up by hand rather than with `rg_gui_gpu_create` must provide `clips` and
  `clip_capacity` (item capacity plus one).
- Code that reads `RgGuiGpuItem` must accept `RG_GUI_GPU_ITEM_TRIANGLES`.
  Solid and image items hold four vertices per primitive.
- Custom image-material pipelines receive each image as four vertices through
  the shared quad index buffer; see [image materials](docs/rg_gui.md#image-materials).
