# rg_gui integration

The stack has three layers:

1. `rg_gui.h` owns UI state and emits an ordered `RgGuiDrawList`.
2. `rg_gui_renderer.h` turns text commands into persistent cached glyph pages
   and compact frame-local runs.
3. `rg_gui_gpu.h` uploads geometry and text changes, expands text runs with a
   compute shader, and renders the draw list through SDL3 GPU.

`rg_gui.h` uses `rg_text.h` for text measurement and retains the supplied
`RgTextFont` pointer. SDL3 is required by the input layer. The GPU layer uses
`rg_gpu.h` and stores an `SDL_GPUTexture*` in `RgGuiTexture` through
`uintptr_t`.

The exact dependency baseline and clean-checkout commands are in the
[repository README](../README.md#dependency-baseline).

## Include and initialize

For the SDL3 GPU renderer, one include brings in the stack:

```c
#include "rg_gui_gpu.h"
```

`rg_gui.h` is an include-once unity header. It intentionally has no guard, so
do not include it separately after another GUI header has included it.

`RgGuiInitDesc` is required because `font` is required. Descriptor fields other
than `font` may remain zero to select bounded defaults. Size an otherwise-empty
arena from the same descriptor rather than guessing:

```c
RgGuiContext gui;
RgGuiInitDesc gui_desc = {0};
gui_desc.font = &font;
gui_desc.max_draw_cmds = 4096;
gui_desc.text_buffer_size = KB(64);

size_t gui_bytes = rg_gui_memory_required(&gui_desc);
if (!gui_bytes)
{
	return 1; // invalid descriptor or size overflow
}
void* gui_memory = malloc(gui_bytes);
if (!gui_memory)
{
	return 1;
}
RgArena gui_arena = {(char*)gui_memory, gui_bytes, 0, gui_bytes};
if (!rg_gui_init(&gui, &gui_arena, &gui_desc))
{
	free(gui_memory);
	return 1;
}
```

`rg_gui_memory_required` returns an alignment-safe upper bound for an empty
arena and returns zero for invalid input or overflow. Initialization rolls
arena usage back on failure. There is no frontend destroy call: keep the arena
unchanged while the context is in use, then release the entire arena when the
context is no longer needed.

Initialize the persistent text renderer with the same font:

```c
RgGuiRendererLimits limits = rg_gui_renderer_limits_default();
size_t renderer_bytes = rg_gui_renderer_memory_required(&limits, 0);
if (renderer_bytes == SIZE_MAX)
{
	return 1;
}
void* renderer_memory = malloc(renderer_bytes);
if (!renderer_memory)
{
	return 1;
}
RgArena renderer_arena = {
	(char*)renderer_memory, renderer_bytes, 0, renderer_bytes
};

RgGuiRenderer text_renderer;
RgGuiRendererInitDesc renderer_desc = {0};
renderer_desc.font = &font;
renderer_desc.limits = limits;
renderer_desc.page_quads = 0; // 32-glyph default
if (!rg_gui_renderer_init(&text_renderer, &renderer_arena, &renderer_desc))
{
	free(renderer_memory);
	return 1;
}
```

The page size must be a power of two and divide `max_cached_quads`. Zero selects
the 32-glyph default. Cache storage remains at stable addresses until a page is
reclaimed; ordinary allocation does not relocate live glyph geometry.

## Create the SDL3 GPU renderer

Run `build.bat shaders` and deploy the resulting `shaders/Compiled` directory
with the application. `shader_root` is the directory containing `Compiled`.
The atlas may come from `rg_text_gpu.h` or from a caller-created SDL GPU
texture; it remains caller-owned and must outlive `RgGuiGpuRenderer`.

```c
RgGuiGpuRenderer gpu;
RgGuiGpuDesc gpu_desc = {0};
gpu_desc.device = device;
gpu_desc.target_format = rg_gpu_swapchain_format(device, window);
gpu_desc.shader_root = "shaders";
gpu_desc.atlas_texture = atlas_texture;
gpu_desc.atlas_width = font.metrics.atlas_width;
gpu_desc.atlas_height = font.metrics.atlas_height;
gpu_desc.max_cached_quads = limits.max_cached_quads;
gpu_desc.max_runs = limits.max_frame_instances;
gpu_desc.max_text_instances = limits.max_frame_instances;
gpu_desc.min_filter = SDL_GPU_FILTER_NEAREST;
gpu_desc.mag_filter = SDL_GPU_FILTER_NEAREST;

if (!rg_gui_gpu_create(&gpu, &gpu_desc))
{
	return 1;
}
```

Destroy the GPU renderer with `rg_gui_gpu_destroy` before destroying its SDL GPU
device. The fixed shader layout is:

```text
<shader_root>/Compiled/DXIL/<name>.dxil
<shader_root>/Compiled/SPIRV/<name>.spv
<shader_root>/Compiled/MSL/<name>.msl
```

Backend selection and shader entry points are handled by `rg_gpu.h`.

## Ordered input

Use `rg_gui_begin_frame_ex` with an `RgInputEventQueue` for text editors. Reset
the queue before polling each frame, feed every SDL event through
`rg_input_process_event_ex`, and keep its storage alive through
`rg_gui_end_frame`. This preserves shortcut, clipboard, composition, and text
transitions that a keyboard snapshot cannot represent:

```c
rg_input_update(&input);
rg_input_event_queue_reset(&input_events, SDL_GetModState());
while (SDL_PollEvent(&event))
{
	rg_input_process_event_ex(&input, &event, &input_events);
}

rg_gui_begin_frame_ex(&gui, &input, &input_events,
	                   SDL_GetWindowID(window), delta_time);
// Build the UI here.
rg_gui_end_frame(&gui);
```

## Platform output

After `rg_gui_end_frame`, read `rg_gui_platform_output(&gui)` and apply all
three requests:

- Map `cursor` to a native cursor. With SDL, create the five required system
  cursor objects once and call `SDL_SetCursor` only when the requested enum
  changes. Do not create cursors in the frame loop.
- Start or stop SDL text input when `wants_text_input` changes.
- When `ime_caret_valid` is set, pass `ime_caret_rect` to
  `SDL_SetTextInputArea` so composition and candidate windows follow the caret.
  The rectangle uses the pixel coordinate space supplied to the GUI; SDL takes
  window coordinates, so scale it by logical-window-size / pixel-size on
  high-density windows.

The checked-in demos deliberately use normal-density SDL windows, so input,
GUI bounds, and render targets share one coordinate scale. If an application
adds `SDL_WINDOW_HIGH_PIXEL_DENSITY`, it must transform SDL mouse positions,
motion deltas, viewport origins, and hit-test bounds into the GUI's pixel
coordinate space in addition to scaling the IME rectangle. The examples do not
claim to demonstrate that full high-density transform.

The shared code in
[`examples/rg_gui_demo_common.h`](../examples/rg_gui_demo_common.h) implements
this without per-frame allocation or redundant SDL cursor/IME calls.

For multiple native viewports, obtain each secondary result with
`rg_gui_viewport_platform_output`. Apply the cursor from the mouse-focused
viewport and text/IME requests from the keyboard-focused viewport. Text input
must be stopped on the old SDL window before it is started on another one.

## Frame and GPU flow

For each completed draw list:

1. Call `rg_gui_renderer_begin_frame`.
2. Call `rg_gui_gpu_prepare` with the draw list and its overlay offset.
3. Map an `RgGpuUploadRing`, call `rg_gui_gpu_stage_upload`, then unmap it.
4. In a GPU copy pass, call `rg_gui_gpu_encode_upload`.
5. Call `rg_gui_gpu_dispatch` after the copy pass and before rendering.
6. In the render pass, call `rg_gui_gpu_draw` with the output dimensions,
   viewport, and optional drawing offset.

The prepared item stream preserves rectangles, triangles, images, text, clip
stack changes, and overlay ordering. Adjacent compatible items are combined,
but items are not moved across unlike commands.

## Capacity and diagnostics

Capacities are fixed at initialization so normal frame preparation does not
grow storage. Capacity exhaustion has layer-specific behavior:

- The frontend drops commands, text bytes, or input events that do not fit and
  continues the frame. Inspect `rg_gui_diagnostics(&gui)` after the frame;
  `flags`, dropped counts, and high-water values identify which capacity needs
  adjustment.
- The persistent text renderer may drop only the affected text runs and
  continue preparing other runs. Inspect `rg_gui_renderer_stats` for
  `diagnostic_flags`, `frame_dropped_runs`, `frame_dropped_glyphs`, and cache and
  frame high-water values.
- `rg_gui_gpu_prepare` treats geometry/item capacity exhaustion as an
  all-or-nothing preparation failure: it clears the partial GPU item stream and
  returns zero. Upload-stage failures also return zero. Do not submit that
  prepared frame; increase the corresponding GPU limit.

Diagnostics are inexpensive counters over bounded storage. Checking them in a
debug overlay or telemetry build does not enable a separate slow path.

## Native tear-out windows

Define `RG_GUI_ENABLE_VIEWPORTS` before including `rg_gui.h` to enable
secondary draw lists and cross-viewport docking. The application remains
responsible for native-window lifetime and swapchains: create and claim an SDL
window, route global input through `RgGuiInputRouter`, build its UI with
`rg_gui_viewport_begin_ordered_ex`, and render the returned viewport draw list
to that window. A renderer pipeline is created for one target format, so every
native swapchain served by it must use that same format. Once GPU work using a
secondary draw list is complete, call `rg_gui_viewport_release` for its ID when
the native window closes so the fixed viewport slot can be reused.

[`examples/rg_gui_demo_tearout.c`](../examples/rg_gui_demo_tearout.c) is the
platform example. It filters ordered text events by SDL window ID, maintains
the platform output for both native windows, and destroys empty tear-outs after
their panels are docked back.

## Ownership, text identity, and threads

- The `RgGuiContext`, frontend arena, `RgTextFont`, and the font's glyph and
  kerning storage must remain alive and unmoved for the lifetime of the GUI.
- The renderer arena must remain alive and unmoved for the lifetime of
  `RgGuiRenderer`. The atlas texture is caller-owned and must outlive the GPU
  renderer.
- Draw lists and frame-local text remain valid only until the next begin-frame
  call. Prepare/render them before resetting their owning frame state.
- Strings passed to a `*_static` API are cached by pointer identity. Their
  address and bytes must remain unchanged for the lifetime of the GUI context.
  Use the ordinary copying API for mutable or temporary labels.
- Any dynamic-value path built with copying disabled must remain unchanged
  through renderer preparation. `RG_GUI_COPY_DYNAMIC_TEXT` opts those paths
  into the frontend frame-text buffer when the additional copies are preferred.
- The library provides no internal synchronization. Use a context from one
  thread at a time. The SDL window, input, cursor, and IME calls shown here are
  main-thread operations; follow SDL's documented requirements for GPU work.

On Windows x64, numeric formatting comes from rg_core's optimized
`rg_sprintf_hybrid.h` path when the matching assembly object is linked.
`RG_SPRINTF_NO_ASM` selects its C fallback for other builds.
