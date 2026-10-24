# rg_gui integration

The stack has three layers:

1. `rg_gui.h` owns UI state and emits an ordered `RgGuiDrawList`.
2. `rg_gui_renderer.h` turns text commands into persistent cached glyph pages
   and compact frame-local runs.
3. `rg_gui_gpu.h` uploads geometry and text changes, combines compatible draws,
   and renders indexed geometry through SDL3 GPU.

`rg_gui.h` uses `rg_text.h` for text measurement and retains the supplied
`RgTextFont` pointer. SDL3 is required by the input layer. The GPU layer uses
`rg_gpu.h` and stores an `SDL_GPUTexture*` in `RgGuiTexture` through
`uintptr_t`.

To build with the latest `rg_core` and `rg_text` default branches, follow the
[Windows setup instructions](../README.md#clean-windows-setup).

## Include and initialize

For the SDL3 GPU renderer, one include brings in the stack:

```c
#include "rg_gui_gpu.h"
```

On an SSE2-capable x86 target, define `RG_GUI_GPU_USE_SSE2=1` before that include
to enable rectangle/image vertex packing with intrinsics. The default is the
portable C path; no assembly object or extra library is needed. Both paths use
the same vertex layout and capacity checks. The demos select SSE2 on x64 and
other targets that advertise SSE2 support. This affects CPU geometry packing;
text vertices read the persistent glyph cache on the GPU.

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

For frequent measurement of changing labels, an optional shared lookup table
avoids glyph and kerning searches. Initialize it once per immutable font and
attach it to the descriptor **before** calling `rg_gui_init`:

```c
// Store this alongside the font in application-owned state.
RgGuiTextLookup text_lookup;
if (!rg_gui_text_lookup_init(&text_lookup, &font))
{
	return 1;
}
gui_desc.text_lookup = &text_lookup;
```

The table occupies 99,352 bytes on 64-bit targets and can serve multiple GUI
contexts using the same font. It adds no arena allocation. Leave `text_lookup`
null to use ordinary measurement without allocating a frontend lookup. The font,
glyph/kerning arrays, and lookup object must stay alive and unchanged while
attached; a lookup for a different font is ignored. The supplied demos enable
this option.

ASCII glyphs and kerning pairs use direct arrays. Every other glyph codepoint
and kerning pair is indexed in fixed open-addressed tables sized by
`RG_GUI_TEXT_LOOKUP_GLYPH_SLOTS` and `RG_GUI_TEXT_LOOKUP_KERNING_SLOTS` (8,192
each by default, 16 KiB apiece). A font with more than half that many glyphs or
pairs, or one whose arrays disagree with `rg_text`'s own search results, leaves
the corresponding table disabled and keeps `rg_text`'s binary searches for
non-ASCII lookups. Results are identical either way; only the lookup cost
changes. Define a slot count of zero to omit a table.

Text-area wrapping accumulates advances in one pass and reuses visual lines
within a widget invocation. Edits, font/scale changes, and IME display text
invalidate that reuse. Layout is rebuilt on the next invocation, including
after external buffer edits.

Initialize the persistent text renderer with the same font:

```c
RgGuiRendererLimits limits = rg_gui_renderer_limits_default();
limits.max_frame_runs = 2048u;
RgGuiRendererInitDesc renderer_desc = {0};
renderer_desc.font = &font;
renderer_desc.limits = limits;
renderer_desc.page_quads = 0; // 32-glyph default
renderer_desc.text_lookup = &text_lookup; // Optional: share the frontend table.
size_t renderer_bytes = rg_gui_renderer_memory_required_ex(&renderer_desc);
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
if (!rg_gui_renderer_init(&text_renderer, &renderer_arena, &renderer_desc))
{
	free(renderer_memory);
	return 1;
}
```

The renderer borrows `renderer_desc.text_lookup` when its `font` pointer equals
`renderer_desc.font`: both must reference the same font object. Initialize the
table with `rg_gui_text_lookup_init` first, then keep the table, font, and its
glyph/kerning arrays alive at stable addresses and unchanged for the renderer
lifetime. A null or mismatched lookup selects an arena-owned table initialized
by the renderer; the font and its arrays remain caller-owned in either case.

Use the same descriptor for `rg_gui_renderer_memory_required_ex` and
`rg_gui_renderer_init`. The size accounts for whether the lookup is borrowed;
it returns `SIZE_MAX` for an invalid descriptor or size overflow. The simpler
`rg_gui_renderer_memory_required(limits, page_quads)` remains a conservative
bound for either case. Sharing saves about 65 KiB per renderer on x64.

The page size must be a power of two and divide `max_cached_quads`. Zero selects
the 32-glyph default. Cache storage remains at stable addresses until a page is
reclaimed; ordinary allocation does not relocate live glyph geometry.

`max_frame_runs` bounds the page-segment descriptors produced in one frame,
independently of the expanded glyph limit `max_frame_instances`. A string can
need several descriptors when it spans multiple cache pages. Size both limits
for the application's visible content; a zero `max_frame_runs` retains the
`max_frame_instances` bound for existing callers. The frontend's draw-command
and frame-text capacities remain separate limits.

## Create the SDL3 GPU renderer

Run `build.bat shaders` and deploy the resulting `shaders/Compiled` directory
with the application. `shader_root` is the directory containing `Compiled`.
Upload the font's straight-alpha RGBA8 atlas to a caller-created SDL GPU
texture, as the demos do. It remains caller-owned and must outlive
`RgGuiGpuRenderer`. The text shader and blend state expect straight-alpha
pixels. Do not directly reuse a texture uploaded by
`rg_text_gpu_upload_atlas`: that uploader premultiplies RGB, which would darken
translucent edges with this renderer. The original atlas pixel bytes can be
shared between the two upload paths.

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
gpu_desc.max_runs = limits.max_frame_runs;
gpu_desc.max_text_instances = limits.max_frame_instances;
gpu_desc.frame_buffer_count = 2; // Explicit GPU frame buffers; 0 defaults to 1.
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

The high-level renderer uses `rg_gui_indexed.vert` and `rg_gui_indexed.frag` to
draw text, shapes, and stock images in as few calls as possible. The vertex
shader clips each primitive to its own clip rectangle with clip distances,
rounding the edges as a scissor would, so a draw call ends only where the
texture changes or an image material needs its own pipeline. It reads cached
glyph quads directly and uses a persistent index buffer, with no per-frame
compute expansion. Clip distances are an SDL_GPU device feature that SDL
enables by default; keep `SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN`
enabled when creating the device. The standalone text-only API retains its
compute path. Custom image materials keep the 20-byte `RgGuiGpuVertex` layout
and existing vertex uniforms, and are clipped by a scissor.

`max_frame_refs` bounds indexed glyph/shape references; zero derives a safe
limit from the glyph and geometry capacities. `frame_buffer_count` selects
one to three explicitly allocated GPU frame buffers. Ordered uploads make
buffer reuse safe on the same queue; upload-ring reuse still requires the
caller's normal cycling or fence handling. The two-frame configuration above
does not itself throttle submissions.

## Image materials

`RgGuiTexture` remains a direct `SDL_GPUTexture*` stored through `uintptr_t`.
The ordinary image, image-button, icon, and push APIs emit material zero and
therefore retain the stock image-pipeline behavior. Applications that need an
alternate fragment path can use `rg_gui_image_material`,
`rg_gui_image_ex_material`, `rg_gui_image_button_material`,
`rg_gui_image_button_ex_material`, `rg_gui_icon_make_material`,
`rg_gui_push_image_material`, or `rg_gui_push_image_material_to` with an
application-defined `RgGuiImageMaterial` value.

Materials are opaque 64-bit values. They are stored in image draw commands and
included in GPU item batching: adjacent images combine only when texture,
material, clip, and geometry continuity all match. A nonzero material still
uses the stock image path when no callback is installed.

Install the optional callback when creating the GPU renderer:

```c
static RgGuiGpuImageBindResult bind_image_material(
    void* user, const RgGuiGpuImageBindInfo* info)
{
    AppImagePipelines* app = (AppImagePipelines*)user;
    if (!app || info->material != APP_IMAGE_MATERIAL_PALETTE)
        return RG_GUI_GPU_IMAGE_BIND_DEFAULT;

    SDL_BindGPUGraphicsPipeline(info->pass, app->palette_pipeline);
    SDL_GPUTextureSamplerBinding bindings[2] = {
        {(SDL_GPUTexture*)(uintptr_t)info->texture, info->sampler},
        {app->palette_texture, info->sampler}
    };
    SDL_BindGPUFragmentSamplers(info->pass, 0, bindings, 2);
    SDL_PushGPUFragmentUniformData(
        info->command_buffer, 0, &app->palette_uniforms,
        sizeof(app->palette_uniforms));
    return RG_GUI_GPU_IMAGE_BIND_CUSTOM;
}

RgGuiGpuDesc gpu_desc = {0};
// Fill the normal required fields first.
gpu_desc.image_bind = bind_image_material;
gpu_desc.image_bind_user = &app_image_pipelines;
```

The callback runs once per prepared item with a nonzero material. Return
`RG_GUI_GPU_IMAGE_BIND_DEFAULT` without changing GPU state to have rg_gui bind
its stock indexed rendering and the item's texture at fragment sampler slot zero.
Return `RG_GUI_GPU_IMAGE_BIND_CUSTOM` after binding a graphics pipeline
compatible with `RgGuiGpuVertex` and all of its fragment resources; rg_gui
still supplies vertex uniform slot zero, the geometry vertex buffer, and the
shared quad index buffer. Each image is four vertices (top-left, top-right,
bottom-right, bottom-left) drawn as an indexed triangle list, so the pipeline
must use a triangle-list primitive and must not derive corners from the vertex
index. Return
`RG_GUI_GPU_IMAGE_BIND_FAILED` to skip the item. CUSTOM and FAILED invalidate
rg_gui's cached pipeline and texture state, so following stock solid, image, or
text items rebind correctly even if the callback partially changed GPU state.

`RgGuiGpuStats.image_bind_calls`, `custom_image_draw_calls`, and
`image_bind_failures` expose the callback decisions. Treat failures as a
diagnostic condition in validation or telemetry builds.

## Ordered input

Use `rg_gui_begin_frame_ex` with an `RgInputEventQueue` for text editors. Reset
the queue before polling each frame, feed every SDL event through
`rg_input_process_event_ex`, and keep its storage alive through
`rg_gui_end_frame`. This preserves shortcut, clipboard, composition, and text
transitions that a keyboard snapshot cannot represent:

```c
rg_input_begin_frame(&input);
rg_input_event_queue_reset(&input_events, SDL_GetModState());
while (SDL_PollEvent(&event))
{
	rg_input_process_event_ex(&input, &event, &input_events);
}
rg_input_sample(&input); // Sample current keyboard/mouse after SDL pumps events.

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

The native tear-out demo currently targets Windows and Linux X11. Its global
hit testing uses the client-area position returned by `SDL_GetWindowPosition`;
do not add window-decoration offsets. Wayland does not provide the global
positioning and pointer behavior assumed by this demo. On X11, restoring a
cached maximized/minimized window keeps that state until mapping, then waits
for the normal state before applying its new geometry. Clearing hidden restore
flags too early can leave older SDL versions out of sync with the window
manager. A bounded retry loop handles this exceptional path; ordinary cache
reuse avoids this additional restoration wait.

## Frame and GPU flow

For each completed draw list:

1. Call `rg_gui_renderer_begin_frame`.
2. Call `rg_gui_gpu_prepare` with the draw list and its overlay offset.
3. Map an `RgGpuUploadRing`, call `rg_gui_gpu_stage_upload`, then unmap it.
4. In a GPU copy pass, call `rg_gui_gpu_encode_upload(&gpu, copy, &ring, &upload)`.
5. `rg_gui_gpu_dispatch_upload(&gpu, command_buffer, &upload)` remains available
   as a packet validation step; the high-level indexed renderer dispatches no
   compute work.
6. Check `rg_gui_gpu_upload_ready(&gpu, &upload)`. If it returns zero, cancel the
   command buffer, call `rg_gui_gpu_upload_abort`, and prepare a fresh packet.
   Check before acquiring a swapchain texture: SDL does not allow cancellation
   after that acquisition.
7. In the render pass, call `rg_gui_gpu_draw` with the same `upload`, output
   dimensions, viewport, and optional drawing offset.
8. Submit the command buffer. Immediately call `rg_gui_gpu_upload_commit` on
   success, or `rg_gui_gpu_upload_abort` on failure. Abort every staged packet
   discarded on an earlier error path as well.

For example, after encoding the uploads, before acquiring a
swapchain texture:

```c
if (!rg_gui_gpu_upload_ready(&gpu, &upload))
{
    SDL_CancelGPUCommandBuffer(command_buffer);
    rg_gui_gpu_upload_abort(&gpu, &upload);
    return 0;
}

// Acquire the swapchain and encode rg_gui_gpu_draw here.
// Keep this renderer's staging/submission state unchanged until submitting.
bool submitted = SDL_SubmitGPUCommandBuffer(command_buffer);
if (submitted)
    rg_gui_gpu_upload_commit(&gpu, &upload);
else
    rg_gui_gpu_upload_abort(&gpu, &upload);
```

Submit and acknowledge each renderer's packets in staging order on the same SDL
GPU queue. Commit records successful submission, so it does not require a GPU
wait; two or three frames may remain in flight. Encoding alone never marks the
glyph cache initialized. Cancellation or failed submission of a text packet causes a full cache
resynchronization on the next packet. A readiness failure must be handled before
submission: acknowledging an out-of-order command afterward cannot repair a draw
that already read the wrong cached geometry.

Upload packets snapshot sorted destination ranges, copied page/run/geometry
contents, indexed references, the clip table, and ordered draw batches.
Text-only packets also
own clip batches resolved to glyph offsets. Encoding and drawing do not
reread the latest prepared frame. Every
allocated page, including its unused tail, is initialized before whole adjacent
pages are combined into one copy. Page revisions survive `begin_frame`, allowing
skipped frames to retain unsent changes. The persistent glyph buffer is never
blindly cycled for partial writes; SDL orders its reads and writes. Indexed
frame data is uploaded once into the packet's selected GPU frame buffer.

Call `rg_gui_gpu_invalidate_cache(&gpu)` when reinitializing the CPU text renderer,
including reuse of the same address, or when replacing its font/atlas data. The
text-only equivalent is `rg_gui_gpu_text_invalidate_cache`. Reset or rebuild the
CPU layout cache separately when font data changes. These functions invalidate
the GPU mirror without releasing resources: outstanding text packets fail their
readiness check and must be cancelled and aborted before staging replacements.
The next text packet uploads all current cache pages. Already submitted work may
finish on the same queue. Ordinary `rg_gui_renderer_clear_cache` preserves
monotonic revisions and does not itself require explicit GPU invalidation.
Invalidation does not replace the atlas texture or its dimensions, or refresh a
borrowed text lookup. Update or recreate those resources separately when changing
fonts, keeping resources used by submitted work alive until it finishes.

Keep a staged packet's upload-ring slices valid until it has been encoded. Do
not reset or remap that ring between staging and encoding; several packets may
append slices to one mapped ring before it is unmapped. Once encoded, retain
the transfer resource until submission and follow the ring's cycling/fence rules
before reusing storage. CPU snapshots remain valid until commit or abort, so
another frame may be prepared after staging without changing the earlier
packet. Keep each packet's GPU upload and draws together in one
command buffer; do not interleave another packet's GPU encoding between them.
The standalone text-only path also keeps its compute dispatch in that sequence.

The GPU renderer permits three outstanding CPU packets by default, configured
with `RG_GUI_GPU_UPLOAD_PACKET_COUNT`, including geometry-only packets. Their
range, draw-batch, and item arrays grow only when a larger snapshot is needed;
each remains bounded by the configured cache, run, or item capacity. Include
`rg_gui_gpu_upload_memory_reserved(&gpu)` when accounting for all retained CPU
packet memory; the text-only equivalent is
`rg_gui_gpu_text_upload_memory_reserved`. These packet slots are distinct from
GPU frames in flight: successful
submission releases the CPU slot immediately. See
[`examples/rg_gui_demo_common.h`](../examples/rg_gui_demo_common.h) for complete
rendering and error handling.

`rg_gui_gpu_buffer_memory_reserved(&gpu)` reports explicitly allocated GPU
buffer bytes, including every selected frame buffer and the shared index/cache
buffers. Count caller-owned upload rings, font textures, and targets separately.
The shared index buffer uses 16-bit elements for up to 16,384 references and
32-bit elements for larger capacities.
This is a reservation count, not driver or total process memory. Draw statistics
include actual `vertices`, `indices`, `copy_calls`, and `indexed_refs`;
`geometry_vertices` counts the vertices rg_gui wrote: four per rectangle or
image and three per triangle.

After creating the GPU renderer, use
`rg_gui_gpu_upload_ring_size_required(&gpu)` to size a caller-owned upload ring
for one packet starting at offset zero. The bound includes the maximum packed
frame, alignment, and a full cache upload after initialization or invalidation.
It preserves the configured cache and geometry capacities. A zero result means
the capacities or alignment are invalid or the size cannot fit in a `u32`.
If several packets or other resources share a ring before it is reset, budget
for all of them; this helper only covers one packet. Continue to honor GPU
completion or buffer-cycling requirements before reusing upload storage.

The prepared item stream preserves rectangles, triangles, images, text, clip
stack changes, and overlay ordering. Adjacent compatible items are combined,
but items are not moved across unlike commands.

### Skipping unchanged frames

An immediate-mode application rebuilds its draw list every frame, but it does
not have to repaint when that list would produce the pixels already on screen.
Set `gpu_desc.frame_signature = 1` (or call
`rg_gui_gpu_set_frame_signature(&gpu, 1)`) and, after a successful
`rg_gui_gpu_prepare`, `rg_gui_gpu_frame_signature(&gpu)` returns a 64-bit
digest of the prepared list: command types and order, rectangles, triangles,
image rectangles/UVs/textures/materials, clip pushes, packed colors, text
positions, text scale, text bytes (or static-text identity), the command count,
and the overlay boundary. The digest is off by default because it adds about
10 to 15 nanoseconds per command to preparation (roughly 2 us for 128 images),
which only pays off for callers that skip frames; text bytes are hashed once by
the text renderer, which already hashes them for its cache.

```c
gpu_desc.frame_signature = 1u;
/* ... rg_gui_gpu_create ... */
rg_gui_renderer_begin_frame(&text_renderer);
if (!rg_gui_gpu_prepare(&gpu, &text_renderer, list, overlay_start)) { /* ... */ }
u64 signature = rg_gui_gpu_frame_signature(&gpu);
if (presented_once && signature == presented_signature &&
    pixel_width == presented_width && pixel_height == presented_height)
{
	/* Identical pixels are already on screen: stage, encode, draw, and present
	 * nothing. Wait for input or the next animation step instead of spinning. */
	SDL_WaitEventTimeout(NULL, 16);
	return;
}
/* ... stage, encode, draw, submit ... */
presented_signature = signature;
presented_width = width;
presented_height = height;
presented_once = 1;
```

The digest describes rg_gui's inputs only. The caller must also require an
unchanged output size, draw offset, clear color, atlas texture, and the
contents of every texture referenced by image commands; include an
application revision for render-target previews or animated images. Static
text is keyed by address, as its contract already requires unchanged bytes.
Skipped frames leave the text cache in a consistent state: new runs stay
dirty until the next staged packet uploads them. Equal digests for different
lists are possible only with 64-bit collision probability. The minimal demo
skips unchanged frames and reports the count on exit; `--no-skip` disables it.

### Repainting only changed tiles

`rg_gui_gpu_tiles.h` is an opt-in header for applications that keep their
frame in a persistent color target and blit it to the swapchain. After
`rg_gui_gpu_prepare`, `rg_gui_tile_cache_update` bins every drawing command
into 64-pixel tiles (`RG_GUI_TILE_SIZE`), digests each tile's command list in
order (payload, packed color, clip in effect, and for text the glyph quads),
and compares the digests with the frame last committed. It returns
`RG_GUI_TILE_REDRAW_NONE` (draw nothing, present the target as is),
`RG_GUI_TILE_REDRAW_PARTIAL` (redraw the rectangles in `cache->rects`), or
`RG_GUI_TILE_REDRAW_FULL` (first frame, output size or draw offset change,
`external_revision` change, or storage overflow). Call
`rg_gui_tile_cache_commit` once the frame has reached the target.

```c
u64 hashes[1024], previous[1024];
SDL_Rect rects[256];
RgGuiTileCache tiles;
rg_gui_tile_cache_init(&tiles, hashes, previous, rects, 1024u, 256u);

/* per frame, after rg_gui_gpu_prepare and upload encoding */
RgGuiTileRedraw mode = rg_gui_tile_cache_update(&tiles, &gpu, &text_renderer, list,
                                                overlay_start, width, height, 0.0f, 0.0f);
if (mode == RG_GUI_TILE_REDRAW_FULL) { /* LOADOP_CLEAR, draw with the full viewport */ }
else if (mode == RG_GUI_TILE_REDRAW_PARTIAL)
{
	/* LOADOP_LOAD, then for each rect: draw_desc.viewport = tiles.rects[i];
	 * the viewport becomes the scissor, so only the rectangle is repainted. */
}
/* blit the target to the swapchain, submit, then: */
rg_gui_tile_cache_commit(&tiles);
```

Three contracts make this exact rather than approximate:

- The draw list must paint every pixel it owns, starting with an opaque
  backdrop, because a dirty rectangle is repainted from the list alone.
- Binning uses the same floor/ceil pixel rounding as the renderer's clipping,
  so a command whose exact float intersection with its clip is empty but which
  still rasterizes a pixel is binned.
- Texture contents are outside the digest: bump `tiles.external_revision` when
  an image or atlas texture changes, and `rg_gui_tile_cache_invalidate` after a
  clear-color or target change.

Treat `RG_GUI_TILE_REDRAW_NONE` as "do not present": on the desktop measured,
the per-present path (uploads, blit, submission, and presentation) costs more
GPU activity than a partial repaint saves, and a frame that changes nothing
needs neither (see the
[audit's static measurement](performance-audit-2026-10.md#the-static-case-not-presenting-is-the-lever)).
The full demo's `--tile-cache` flag uses the cache, `--tile-cache-skip-present`
also skips drawing and presenting unchanged frames, and `--tile-cache-verify`
renders a full repaint every frame and compares both by GPU readback, failing
the run on any difference or incomplete comparison. Storage is caller-owned and fixed;
tiles beyond `tile_capacity` or more dirty rectangles than `rect_capacity`
fall back to a full redraw. Measured results are in the
[performance audit](performance-audit-2026-10.md#level-two-dirty-tiles).

## Capacity and diagnostics

Frontend, cache, and frame geometry capacities are fixed at initialization.
Upload packet metadata arrays grow to their bounded high-water size as described
above. Capacity exhaustion has layer-specific behavior:

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
  prepared frame. Resolve outstanding packets if the packet pool is full;
  otherwise check upload-ring space and the corresponding GPU limits.

Diagnostics are inexpensive counters over bounded storage. Checking them in a
debug overlay or telemetry build does not enable a separate slow path.

## Native tear-out windows

Define `RG_GUI_ENABLE_VIEWPORTS` before including `rg_gui.h` to enable
secondary draw lists and cross-viewport docking. The application remains
responsible for native-window lifetime and swapchains: create and claim an SDL
window, route global input through `RgGuiInputRouter`, build its UI with
`rg_gui_viewport_begin_ordered_ex`, and render the returned viewport draw list
to that window. A renderer pipeline is created for one target format, so every
native swapchain served by it must use that same format. After the renderer has
prepared, uploaded, and submitted the secondary draw list for the last time,
call `rg_gui_viewport_release` when that viewport closes so its fixed CPU slot
can be reused. This does not destroy GPU resources; their lifetime remains the
application's responsibility.

By default, initialization reserves draw and overlay arrays for every configured
viewport in the frontend arena. To defer that storage, set
`RgGuiInitDesc.viewport_storage` and `viewport_storage_user` before calculating
the arena size and initializing the context. The callback receives a stable
slot index and the required draw/overlay capacities, and returns two aligned,
nonoverlapping command arrays. It is called when a slot first needs storage,
not during initialization. The application owns the arrays and must keep them
alive until the context is no longer used. `rg_gui_viewport_release` retains
them so another window can reuse the slot without allocating again.

The callback may return zero on allocation failure. Invalid or unavailable
storage sets `RG_GUI_DIAGNOSTIC_VIEWPORT_STORAGE`; viewport creation returns
null without claiming the slot, and the application may retry. The frontend
does not free callback storage, including rejected callback outputs. Include
those allocations separately from the frontend arena when reporting memory.
The tear-out demo shows an owner that allocates each slot on first use and
releases all its arrays at shutdown.

[`examples/rg_gui_demo_tearout.c`](../examples/rg_gui_demo_tearout.c) is the
platform example. It filters ordered text events by SDL window ID, maintains
the platform output for both native windows, and parks one empty tear-out as a
hidden, still-claimed window after its panels are docked back. Reopening reuses
the window and swapchain; closing clears viewport and IME ownership. This keeps
SDL's blocking swapchain release out of interactive redocking, at the cost of
retaining that window's resources until final cleanup. The demo handles main
window close explicitly and disables SDL's automatic last-visible-window quit
inference, so a repeated close event for the parked window cannot quit the app.

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
  If a text renderer survives that GUI context, call
  `rg_gui_renderer_clear_cache(&text_renderer)` before reusing or changing those
  string addresses. GPU invalidation alone does not clear pointer-keyed layouts.
- Ordinary labels, numeric values, and editor text snapshot their bytes into
  the frontend frame-text buffer before returning. Later edits to the caller's
  buffer or reuse of a numeric-cache slot cannot change an earlier command.
  Clipped text already owned by that frame uses the existing snapshot once.
- `RG_GUI_ASSUME_STATIC_LABELS` is rejected. `RG_GUI_LABEL_COPY` and
  `RG_GUI_COPY_DYNAMIC_TEXT` must be `1` if defined; disabling either is rejected
  because mutable text must not acquire immutable pointer identity. Use explicit
  `*_static` APIs for immutable labels that should skip copying.
- Low-level `rg_gui_push_text*_ex` calls with `copy = 0` borrow text through
  renderer preparation and still use content-based cache identity. These calls
  are intended for text already owned by the current frame, such as substrings
  returned by `rg_gui_copy_text_range`.
- The library provides no internal synchronization. Use a context from one
  thread at a time. The SDL window, input, cursor, and IME calls shown here are
  main-thread operations; follow SDL's documented requirements for GPU work.

Numeric formatting uses rg_core's `rg_sprintf_hybrid.h`. Builds that select
its native assembly backend must also link the matching helper object.
`RG_SPRINTF_NO_ASM` selects the portable C formatter, as used by the demos.

## Persistent text-area layout

For larger editable documents, attach an optional caller-owned layout cache to
`RgGuiTextAreaState`. It avoids rebuilding wrapped lines on unchanged frames:

```c
/* Keep these with the editor state, alive across frames. */
RgGuiTextAreaLayoutCache cache;
char cached_text[16 * 1024];
RgGuiTextAreaVisualLine cached_lines[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];

/* Initialize once, before attaching to the editor. */
rg_gui_text_area_cache_init(&cache, cached_text, sizeof(cached_text),
                            cached_lines, RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
area.layout_cache = &cache;
```

The cache compares an owned snapshot of the actual text bytes and layout inputs.
Changing text in place remains safe even when its address and length stay the
same. Cursor, selection, scrolling, and IME updates still run every frame. There
are no cache allocations inside the widget; the example reserves about 40 KiB
on a 64-bit build. A missing or undersized cache uses the normal layout path
without reducing its line capacity.

Use separate, suitably aligned storage for each live editor. The cache object,
snapshot bytes, line descriptors, mutable input buffer, and GUI frame-text arena
must not overlap. Reinitialize or replace storage only between widget calls.
Call `rg_gui_text_area_cache_invalidate(&cache)` after in-place changes to font,
glyph advances, kerning, shared lookups, or reinitialization of the GUI context.
Rebuild borrowed lookups and renderer caches separately when their font data
changes. GPU cache invalidation does not invalidate the editor's wrapped lines.
