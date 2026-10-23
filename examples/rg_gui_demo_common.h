// Shared bootstrap for the rg_gui examples.

// Keep these large unity-build examples quick to compile. /O2 still applies
// the compiler's normal inlining heuristics to this internal-linkage code.
#ifndef RGINLINE
#define RGINLINE static inline
#endif
// Keep the examples self-contained by selecting rg_core's portable formatter.
#define RG_SPRINTF_NO_ASM 1
// Exercise optional geometry packing on targets that guarantee SSE2.
#if !defined(RG_GUI_GPU_USE_SSE2) && (defined(_M_X64) || defined(__SSE2__))
#define RG_GUI_GPU_USE_SSE2 1
#endif
#include "../src/rg_gui_gpu.h"
#include "../src/rg_gui_gpu_tiles.h"

#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#if defined(RG_GUI_ENABLE_VIEWPORTS)
/* The application owns optional viewport lists. Keep their allocations until
 * shutdown so reopening a detached window does not allocate again. */
typedef struct DemoViewportStorage
{
	RgGuiDrawCmd* blocks[RG_GUI_MAX_VIEWPORTS];
	u32 draw_capacities[RG_GUI_MAX_VIEWPORTS];
	u32 overlay_capacities[RG_GUI_MAX_VIEWPORTS];
	size_t reserved_bytes;
} DemoViewportStorage;

static int demo_viewport_storage_acquire(void* user, u32 slot,
                                         u32 draw_capacity, u32 overlay_capacity,
                                         RgGuiDrawCmd** draw_commands,
                                         RgGuiDrawCmd** overlay_commands)
{
	DemoViewportStorage* storage = (DemoViewportStorage*)user;
	if (!storage || slot >= RG_GUI_MAX_VIEWPORTS || !draw_commands ||
	    !overlay_commands || !draw_capacity || !overlay_capacity)
		return 0;
	if (!storage->blocks[slot])
	{
		u64 count = (u64)draw_capacity + overlay_capacity;
		if (count > SIZE_MAX / sizeof(RgGuiDrawCmd)) return 0;
		size_t bytes = (size_t)count * sizeof(RgGuiDrawCmd);
		if (bytes > SIZE_MAX - storage->reserved_bytes) return 0;
		RgGuiDrawCmd* block = (RgGuiDrawCmd*)malloc(bytes);
		if (!block) return 0;
		storage->blocks[slot] = block;
		storage->draw_capacities[slot] = draw_capacity;
		storage->overlay_capacities[slot] = overlay_capacity;
		storage->reserved_bytes += bytes;
	}
	if (storage->draw_capacities[slot] != draw_capacity ||
	    storage->overlay_capacities[slot] != overlay_capacity)
		return 0;
	*draw_commands = storage->blocks[slot];
	*overlay_commands = storage->blocks[slot] + draw_capacity;
	return 1;
}

static void demo_viewport_storage_destroy(DemoViewportStorage* storage)
{
	if (!storage) return;
	for (u32 i = 0u; i < RG_GUI_MAX_VIEWPORTS; i++) free(storage->blocks[i]);
	memset(storage, 0, sizeof(*storage));
}
#endif

#ifndef RG_GUI_DEMO_GPU_DEBUG
#define RG_GUI_DEMO_GPU_DEBUG 0
#endif

#define DEMO_FONT_GLYPH_CAPACITY 256u
#define DEMO_FONT_KERNING_CAPACITY 4096u
#define DEMO_FONT_ASSET_STEM "inter_medium_16"

typedef struct DemoFontAssets
{
	RgTextFont font;
	RgTextGlyph glyphs[DEMO_FONT_GLYPH_CAPACITY];
	RgTextKerning kernings[DEMO_FONT_KERNING_CAPACITY];
	RgGuiTextLookup text_lookup;
	u8* pixels;
	u32 atlas_width;
	u32 atlas_height;
} DemoFontAssets;

#define DEMO_PLATFORM_CURSOR_COUNT 5u

typedef struct DemoPlatformState
{
	SDL_Cursor* cursors[DEMO_PLATFORM_CURSOR_COUNT];
	SDL_Window* text_input_window;
	RgGuiMouseCursor applied_cursor;
	RgGuiRect ime_source_rect;
	int ime_pixel_width;
	int ime_pixel_height;
	int cursor_applied;
	int ime_area_valid;
} DemoPlatformState;

static SDL_SystemCursor demo_platform_sdl_cursor(RgGuiMouseCursor cursor)
{
	switch (cursor)
	{
		case RG_GUI_CURSOR_TEXT: return SDL_SYSTEM_CURSOR_TEXT;
		case RG_GUI_CURSOR_MOVE: return SDL_SYSTEM_CURSOR_MOVE;
		case RG_GUI_CURSOR_RESIZE_H: return SDL_SYSTEM_CURSOR_EW_RESIZE;
		case RG_GUI_CURSOR_RESIZE_V: return SDL_SYSTEM_CURSOR_NS_RESIZE;
		default: return SDL_SYSTEM_CURSOR_DEFAULT;
	}
}

static void demo_platform_init(DemoPlatformState* state)
{
	memset(state, 0, sizeof(*state));
	state->cursors[RG_GUI_CURSOR_DEFAULT] = SDL_GetDefaultCursor();
	for (u32 i = (u32)RG_GUI_CURSOR_TEXT; i < DEMO_PLATFORM_CURSOR_COUNT; i++)
	{
		state->cursors[i] = SDL_CreateSystemCursor(
		    demo_platform_sdl_cursor((RgGuiMouseCursor)i));
		if (!state->cursors[i]) SDL_ClearError();
	}
}

static int demo_platform_apply_cursor(DemoPlatformState* state,
                                      const RgGuiPlatformOutput* output)
{
	RgGuiMouseCursor cursor = output ? output->cursor : RG_GUI_CURSOR_DEFAULT;
	if ((u32)cursor >= DEMO_PLATFORM_CURSOR_COUNT) cursor = RG_GUI_CURSOR_DEFAULT;
	if (state->cursor_applied && state->applied_cursor == cursor) return 1;

	SDL_Cursor* sdl_cursor = state->cursors[cursor];
	if (!sdl_cursor) sdl_cursor = state->cursors[RG_GUI_CURSOR_DEFAULT];
	if (sdl_cursor && !SDL_SetCursor(sdl_cursor)) return 0;
	state->applied_cursor = cursor;
	state->cursor_applied = 1;
	return 1;
}

static void demo_platform_process_event(DemoPlatformState* state,
                                        const SDL_Event* event)
{
	if (!state->text_input_window || !event) return;
	if (event->type != SDL_EVENT_WINDOW_RESIZED &&
	    event->type != SDL_EVENT_WINDOW_PIXEL_SIZE_CHANGED &&
	    event->type != SDL_EVENT_WINDOW_DISPLAY_CHANGED &&
	    event->type != SDL_EVENT_WINDOW_DISPLAY_SCALE_CHANGED)
		return;
	if (event->window.windowID == SDL_GetWindowID(state->text_input_window))
		state->ime_area_valid = 0;
}

static void demo_platform_forget_text_window(DemoPlatformState* state,
                                             RgInputState* input,
                                             SDL_Window* window)
{
	if (!window || state->text_input_window != window) return;
	SDL_SetTextInputArea(window, NULL, 0);
	if (SDL_StopTextInput(window)) input->text_input_active = false;
	state->text_input_window = NULL;
	state->ime_area_valid = 0;
}

static int demo_platform_set_text_input(RgInputState* input,
                                        SDL_Window* window, int enabled)
{
	if (!input || !window)
	{
		SDL_SetError("Text input requires an input state and SDL window");
		return 0;
	}
	if (enabled)
	{
		if (!SDL_StartTextInput(window)) return 0;
	}
	else
	{
		if (!SDL_StopTextInput(window)) return 0;
	}
	input->text_input_active = enabled != 0;
	return 1;
}

static int demo_platform_update_text_input(DemoPlatformState* state,
                                           RgInputState* input,
                                           SDL_Window* window,
                                           const RgGuiPlatformOutput* output,
                                           int pixel_width, int pixel_height)
{
	SDL_Window* desired = output && output->wants_text_input ? window : NULL;
	if (state->text_input_window != desired)
	{
		if (state->text_input_window)
		{
			if (!SDL_SetTextInputArea(state->text_input_window, NULL, 0)) return 0;
			state->ime_area_valid = 0;
			if (!demo_platform_set_text_input(input, state->text_input_window, 0)) return 0;
		}
		state->text_input_window = NULL;
		state->ime_area_valid = 0;
		if (desired)
		{
			if (!demo_platform_set_text_input(input, desired, 1)) return 0;
			state->text_input_window = desired;
		}
	}
	if (!desired) return 1;

	if (!output->ime_caret_valid)
	{
		if (state->ime_area_valid)
		{
			if (!SDL_SetTextInputArea(desired, NULL, 0)) return 0;
			state->ime_area_valid = 0;
		}
		return 1;
	}

	RgGuiRect source = output->ime_caret_rect;
	if (state->ime_area_valid && state->ime_pixel_width == pixel_width &&
	    state->ime_pixel_height == pixel_height &&
	    state->ime_source_rect.x == source.x && state->ime_source_rect.y == source.y &&
	    state->ime_source_rect.w == source.w && state->ime_source_rect.h == source.h)
		return 1;

	int window_width = 0;
	int window_height = 0;
	if (pixel_width <= 0 || pixel_height <= 0 ||
	    !SDL_GetWindowSize(desired, &window_width, &window_height) ||
	    window_width <= 0 || window_height <= 0)
		return 0;
	f32 scale_x = (f32)window_width / (f32)pixel_width;
	f32 scale_y = (f32)window_height / (f32)pixel_height;
	int x0 = (int)SDL_floorf(source.x * scale_x);
	int y0 = (int)SDL_floorf(source.y * scale_y);
	int x1 = (int)SDL_ceilf((source.x + source.w) * scale_x);
	int y1 = (int)SDL_ceilf((source.y + source.h) * scale_y);
	SDL_Rect ime_rect = {x0, y0, x1 > x0 ? x1 - x0 : 1, y1 > y0 ? y1 - y0 : 1};
	if (!SDL_SetTextInputArea(desired, &ime_rect, 0)) return 0;

	state->ime_source_rect = source;
	state->ime_pixel_width = pixel_width;
	state->ime_pixel_height = pixel_height;
	state->ime_area_valid = 1;
	return 1;
}

static void demo_platform_destroy(DemoPlatformState* state, RgInputState* input)
{
	if (state->text_input_window)
		demo_platform_forget_text_window(state, input, state->text_input_window);
	if (state->cursors[RG_GUI_CURSOR_DEFAULT])
		SDL_SetCursor(state->cursors[RG_GUI_CURSOR_DEFAULT]);
	for (u32 i = (u32)RG_GUI_CURSOR_TEXT; i < DEMO_PLATFORM_CURSOR_COUNT; i++)
		if (state->cursors[i]) SDL_DestroyCursor(state->cursors[i]);
	memset(state, 0, sizeof(*state));
}

static int demo_read_file(const char* path, void** out_data, size_t* out_size)
{
	FILE* file = fopen(path, "rb");
	if (!file)
	{
		SDL_SetError("Could not open required demo asset '%s': %s", path, strerror(errno));
		return 0;
	}
	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		SDL_SetError("Could not determine the size of demo asset '%s'", path);
		return 0;
	}
	long length = ftell(file);
	if (length <= 0 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		SDL_SetError("Required demo asset '%s' is empty or unreadable", path);
		return 0;
	}
	void* data = malloc((size_t)length);
	if (!data)
	{
		fclose(file);
		SDL_SetError("Out of memory while reading demo asset '%s'", path);
		return 0;
	}
	size_t read = fread(data, 1u, (size_t)length, file);
	fclose(file);
	if (read != (size_t)length)
	{
		free(data);
		SDL_SetError("Could not read the complete demo asset '%s'", path);
		return 0;
	}
	*out_data = data;
	*out_size = read;
	return 1;
}

static int demo_font_try_load(DemoFontAssets* assets, const char* asset_base)
{
	char metrics_path[1024];
	char atlas_path[1024];
	int metrics_length = SDL_snprintf(metrics_path, sizeof(metrics_path), "%s.font", asset_base);
	int atlas_length = SDL_snprintf(atlas_path, sizeof(atlas_path), "%s.rgba", asset_base);
	if (metrics_length < 0 || (size_t)metrics_length >= sizeof(metrics_path) ||
	    atlas_length < 0 || (size_t)atlas_length >= sizeof(atlas_path))
	{
		SDL_SetError("Demo font asset path is too long: '%s'", asset_base);
		return 0;
	}

	void* metrics_data = NULL;
	size_t metrics_size = 0u;
	if (!demo_read_file(metrics_path, &metrics_data, &metrics_size)) return 0;

	RgTextFontLoadDesc desc = {0};
	desc.data = metrics_data;
	desc.data_size = metrics_size;
	desc.glyphs = assets->glyphs;
	desc.glyph_capacity = DEMO_FONT_GLYPH_CAPACITY;
	desc.kernings = assets->kernings;
	desc.kerning_capacity = DEMO_FONT_KERNING_CAPACITY;
	int parsed = rg_text_font_load_rgfont(&assets->font, &desc);
	free(metrics_data);
	if (!parsed)
	{
		SDL_SetError("Malformed demo font metrics '%s'; regenerate the Inter assets with rg_text_bake",
		             metrics_path);
		return 0;
	}

	void* pixels = NULL;
	size_t pixel_size = 0u;
	if (!demo_read_file(atlas_path, &pixels, &pixel_size)) return 0;
	u64 atlas_width = (u64)assets->font.metrics.atlas_width;
	u64 atlas_height = (u64)assets->font.metrics.atlas_height;
	if (atlas_width == 0u || atlas_height == 0u ||
	    atlas_width > UINT64_MAX / atlas_height ||
	    atlas_width * atlas_height > UINT64_MAX / 4u)
	{
		free(pixels);
		SDL_SetError("Demo font metrics '%s' declare invalid atlas dimensions", metrics_path);
		return 0;
	}
	u64 expected_size = atlas_width * atlas_height * 4u;
	if (expected_size != (u64)pixel_size)
	{
		free(pixels);
		SDL_SetError("Demo font atlas '%s' has %zu bytes; metrics require %llu",
		             atlas_path, pixel_size, (unsigned long long)expected_size);
		return 0;
	}

	if (!rg_gui_text_lookup_init(&assets->text_lookup, &assets->font))
	{
		free(pixels);
		SDL_SetError("Could not build demo font lookup tables");
		return 0;
	}
	assets->pixels = (u8*)pixels;
	assets->atlas_width = assets->font.metrics.atlas_width;
	assets->atlas_height = assets->font.metrics.atlas_height;
	return 1;
}

static int demo_font_load(DemoFontAssets* assets)
{
	memset(assets, 0, sizeof(*assets));
	char asset_base[1024];
	const char* executable_dir = SDL_GetBasePath();
	if (executable_dir)
	{
		if ((SDL_snprintf(asset_base, sizeof(asset_base), "%sexamples/assets/%s",
		                  executable_dir, DEMO_FONT_ASSET_STEM) >= 0 &&
		     demo_font_try_load(assets, asset_base)) ||
		    (SDL_snprintf(asset_base, sizeof(asset_base), "%sassets/%s",
		                  executable_dir, DEMO_FONT_ASSET_STEM) >= 0 &&
		     demo_font_try_load(assets, asset_base)))
		{
			printf("Loaded Inter Medium demo font (%ux%u atlas)\n",
			       assets->atlas_width, assets->atlas_height);
			SDL_ClearError();
			return 1;
		}
	}
	if (demo_font_try_load(assets, "examples/assets/" DEMO_FONT_ASSET_STEM) ||
	    demo_font_try_load(assets, "assets/" DEMO_FONT_ASSET_STEM))
	{
		printf("Loaded Inter Medium demo font (%ux%u atlas)\n",
		       assets->atlas_width, assets->atlas_height);
		SDL_ClearError();
		return 1;
	}

	char cause[512];
	SDL_strlcpy(cause, SDL_GetError(), sizeof(cause));
	SDL_SetError(
	    "Required Inter Medium demo assets are missing or malformed. Keep both "
	    "'%s.font' and '%s.rgba' in examples/assets beside a repository-built "
	    "executable, or in assets beside a deployed executable. Last error: %s",
	    DEMO_FONT_ASSET_STEM, DEMO_FONT_ASSET_STEM,
	    cause[0] ? cause : "no candidate asset pair could be loaded");
	return 0;
}

static void demo_font_destroy(DemoFontAssets* assets)
{
	if (!assets) return;
	free(assets->pixels);
	memset(assets, 0, sizeof(*assets));
}

static int demo_shader_root(char* buffer, size_t capacity)
{
	if (!buffer || capacity == 0u)
	{
		SDL_SetError("Demo shader root requires writable path storage");
		return 0;
	}

	const char* executable_dir = SDL_GetBasePath();
	if (executable_dir)
	{
		int written = SDL_snprintf(buffer, capacity, "%sshaders", executable_dir);
		if (written >= 0 && (size_t)written < capacity)
		{
			char compiled[1024];
			SDL_PathInfo info;
			int compiled_written = SDL_snprintf(compiled, sizeof(compiled),
			                                    "%s/Compiled", buffer);
			if (compiled_written >= 0 && (size_t)compiled_written < sizeof(compiled) &&
			    SDL_GetPathInfo(compiled, &info) && info.type == SDL_PATHTYPE_DIRECTORY)
			{
				SDL_ClearError();
				return 1;
			}
		}
	}

	SDL_PathInfo info;
	if (SDL_GetPathInfo("shaders/Compiled", &info) && info.type == SDL_PATHTYPE_DIRECTORY)
	{
		int written = SDL_snprintf(buffer, capacity, "shaders");
		if (written >= 0 && (size_t)written < capacity)
		{
			SDL_ClearError();
			return 1;
		}
	}

	SDL_SetError("Could not find shaders/Compiled beside the executable or current directory");
	return 0;
}

static SDL_GPUTexture* demo_atlas_create(SDL_GPUDevice* device, const u8* pixels,
                                         u32 atlas_width, u32 atlas_height)
{
	u64 upload_size_64 = (u64)atlas_width * (u64)atlas_height * 4u;
	if (!device || !pixels || atlas_width == 0u || atlas_height == 0u ||
	    upload_size_64 > UINT32_MAX)
	{
		SDL_SetError("Demo font atlas dimensions exceed the SDL_GPU upload limit");
		return NULL;
	}
	u32 upload_size = (u32)upload_size_64;

	SDL_GPUTextureCreateInfo texture_info = {0};
	texture_info.type = SDL_GPU_TEXTURETYPE_2D;
	texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	texture_info.width = atlas_width;
	texture_info.height = atlas_height;
	texture_info.layer_count_or_depth = 1u;
	texture_info.num_levels = 1u;
	SDL_GPUTexture* texture = SDL_CreateGPUTexture(device, &texture_info);
	if (!texture) return NULL;

	SDL_GPUTransferBufferCreateInfo transfer_info = {0};
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = upload_size;
	SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(device, &transfer_info);
	if (!transfer)
	{
		SDL_ReleaseGPUTexture(device, texture);
		return NULL;
	}

	void* mapped = SDL_MapGPUTransferBuffer(device, transfer, false);
	if (!mapped)
	{
		SDL_ReleaseGPUTransferBuffer(device, transfer);
		SDL_ReleaseGPUTexture(device, texture);
		return NULL;
	}
	memcpy(mapped, pixels, upload_size);
	SDL_UnmapGPUTransferBuffer(device, transfer);

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(device);
	if (!command_buffer)
	{
		SDL_ReleaseGPUTransferBuffer(device, transfer);
		SDL_ReleaseGPUTexture(device, texture);
		return NULL;
	}
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
	if (!copy)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		SDL_ReleaseGPUTransferBuffer(device, transfer);
		SDL_ReleaseGPUTexture(device, texture);
		return NULL;
	}
	SDL_GPUTextureTransferInfo source = {0};
	source.transfer_buffer = transfer;
	source.pixels_per_row = atlas_width;
	source.rows_per_layer = atlas_height;
	SDL_GPUTextureRegion destination = {0};
	destination.texture = texture;
	destination.w = atlas_width;
	destination.h = atlas_height;
	destination.d = 1u;
	SDL_UploadToGPUTexture(copy, &source, &destination, false);
	SDL_EndGPUCopyPass(copy);

	int submitted = SDL_SubmitGPUCommandBuffer(command_buffer) ? 1 : 0;
	SDL_ReleaseGPUTransferBuffer(device, transfer);
	if (!submitted)
	{
		SDL_ReleaseGPUTexture(device, texture);
		return NULL;
	}
	return texture;
}

/* CPU wall times only: encoding and submission do not measure GPU execution.
 * A missing swapchain leaves draw_stats zero; submitted/presented identify which
 * frames have usable draw counters. Text counters also describe failed prepares.
 */
typedef struct DemoRenderMetrics
{
	double prepare_ms;
	double stage_upload_ms;
	double encode_ms;
	double swapchain_wait_ms;
	double draw_encode_ms;
	double submit_ms;
	double total_ms;
	RgGuiGpuStats draw_stats;
	RgGuiRendererStats text_stats;
	u32 draw_commands;
	int submitted;
	int presented;
	/* The prepared frame matched the pixels already presented; no GPU work ran. */
	int skipped;
	/* Dirty-tile path: RgGuiTileRedraw mode, tile counts, rectangles, and the
	 * CPU cost of the tile pass. verified/mismatch describe the readback check. */
	u32 tile_mode;
	u32 tiles_dirty;
	u32 tiles_total;
	u32 tile_rects;
	double tile_update_ms;
	u32 tile_verified;
	u32 tile_mismatch;
} DemoRenderMetrics;

/* Persistent color target plus the dirty-tile cache that decides how much of it
 * to repaint each frame. The target is blitted to the swapchain, so a frame with
 * no dirty tiles draws nothing. Verify mode also repaints a second target in full
 * every frame and compares both by readback every verify_interval frames. */
typedef struct DemoTileTarget
{
	SDL_GPUTexture* color;
	SDL_GPUTexture* verify;
	SDL_GPUTransferBuffer* download;
	SDL_GPUTextureFormat format;
	u32 width;
	u32 height;
	u32 download_bytes;
	u64* hashes;
	u64* previous;
	SDL_Rect* rects;
	RgGuiTileCache cache;
	int enabled;
	int verify_enabled;
	u32 verify_interval;
	u64 frames;
	u32 verified_frames;
	u32 mismatched_frames;
	u32 verify_failures; /* a scheduled comparison could not be completed */
	u64 dirty_tile_sum;
	u64 total_tile_sum;
} DemoTileTarget;

/* Backdrop behind every demo frame. A dirty rectangle is repainted from the
 * draw list alone, so the list must paint every pixel: the full demo pushes a
 * rectangle of this color first, and the direct path clears to it. */
#define DEMO_CLEAR_R 0.035f
#define DEMO_CLEAR_G 0.043f
#define DEMO_CLEAR_B 0.059f

#define DEMO_TILE_MAX_TILES 16384u   /* 8192 x 8192 at 64-pixel tiles */
#define DEMO_TILE_MAX_RECTS 4096u

RGINLINE int demo_tile_target_init(DemoTileTarget* target, SDL_GPUTextureFormat format,
                                 int verify, u32 verify_interval)
{
	memset(target, 0, sizeof(*target));
	target->format = format;
	target->enabled = 1;
	target->verify_enabled = verify != 0;
	target->verify_interval = verify_interval ? verify_interval : 1u;
	target->hashes = (u64*)SDL_calloc(DEMO_TILE_MAX_TILES, sizeof(u64));
	target->previous = (u64*)SDL_calloc(DEMO_TILE_MAX_TILES, sizeof(u64));
	target->rects = (SDL_Rect*)SDL_calloc(DEMO_TILE_MAX_RECTS, sizeof(SDL_Rect));
	if (!target->hashes || !target->previous || !target->rects ||
	    !rg_gui_tile_cache_init(&target->cache, target->hashes, target->previous, target->rects,
	                            DEMO_TILE_MAX_TILES, DEMO_TILE_MAX_RECTS))
	{
		SDL_SetError("Could not allocate the dirty-tile cache");
		return 0;
	}
	return 1;
}

RGINLINE void demo_tile_target_release_textures(SDL_GPUDevice* device, DemoTileTarget* target)
{
	if (target->color) SDL_ReleaseGPUTexture(device, target->color);
	if (target->verify) SDL_ReleaseGPUTexture(device, target->verify);
	if (target->download) SDL_ReleaseGPUTransferBuffer(device, target->download);
	target->color = NULL;
	target->verify = NULL;
	target->download = NULL;
	target->download_bytes = 0u;
	target->width = 0u;
	target->height = 0u;
}

RGINLINE void demo_tile_target_destroy(SDL_GPUDevice* device, DemoTileTarget* target)
{
	if (!target) return;
	if (device) demo_tile_target_release_textures(device, target);
	SDL_free(target->hashes);
	SDL_free(target->previous);
	SDL_free(target->rects);
	memset(target, 0, sizeof(*target));
}

RGINLINE SDL_GPUTexture* demo_tile_create_color_target(SDL_GPUDevice* device,
                                                     SDL_GPUTextureFormat format, u32 width, u32 height)
{
	SDL_GPUTextureCreateInfo info = {0};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = format;
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET | SDL_GPU_TEXTUREUSAGE_SAMPLER;
	info.width = width;
	info.height = height;
	info.layer_count_or_depth = 1u;
	info.num_levels = 1u;
	return SDL_CreateGPUTexture(device, &info);
}

/* (Re)create targets for the swapchain size. A size change invalidates the cache. */
RGINLINE int demo_tile_target_ensure(SDL_GPUDevice* device, DemoTileTarget* target, u32 width, u32 height)
{
	if (target->color && target->width == width && target->height == height) return 1;
	demo_tile_target_release_textures(device, target);
	rg_gui_tile_cache_invalidate(&target->cache);
	target->color = demo_tile_create_color_target(device, target->format, width, height);
	if (!target->color) return 0;
	if (target->verify_enabled)
	{
		target->verify = demo_tile_create_color_target(device, target->format, width, height);
		if (!target->verify) return 0;
		u64 bytes = (u64)width * height * 4u * 2u;
		if (bytes > UINT32_MAX) return 0;
		SDL_GPUTransferBufferCreateInfo transfer = {0};
		transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
		transfer.size = (u32)bytes;
		target->download = SDL_CreateGPUTransferBuffer(device, &transfer);
		if (!target->download) return 0;
		target->download_bytes = (u32)bytes;
	}
	target->width = width;
	target->height = height;
	return 1;
}

/* Signature and swapchain size of the last presented frame. When a newly
 * prepared frame reproduces both, drawing it again cannot change the window
 * contents, so staging, encoding, drawing, and presenting are skipped. The
 * atlas and image textures used by these demos never change after startup;
 * an application that updates such textures must also reset `valid`. */
typedef struct DemoFrameSkip
{
	u64 signature;
	u32 width;
	u32 height;
	int valid;
	int enabled;
	u32 skipped_frames;
} DemoFrameSkip;

static int demo_render_draw_list_skippable(SDL_GPUDevice* device, SDL_Window* window,
                                           RgGuiGpuRenderer* gpu, RgGuiRenderer* text_renderer,
                                           RgGpuUploadRing* upload_ring,
                                           const RgGuiDrawList* draw_list, u32 overlay_start,
                                           DemoFrameSkip* skip,
                                           int* out_presented, DemoRenderMetrics* metrics)
{
	u64 render_start = metrics ? SDL_GetTicksNS() : 0u;
	u64 stage_start = render_start;
	int result = 0;
	RgGuiGpuUpload upload = {0};
	if (metrics)
	{
		memset(metrics, 0, sizeof(*metrics));
		metrics->draw_commands = draw_list ? draw_list->count : 0u;
	}
	if (out_presented) *out_presented = 0;
	SDL_ClearError();
	rg_gui_renderer_begin_frame(text_renderer);
	int prepared = rg_gui_gpu_prepare(gpu, text_renderer, draw_list, overlay_start);
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->prepare_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}
	if (!prepared)
	{
		SDL_SetError("rg_gui GPU preparation exceeded the configured demo limits");
		goto done;
	}

	u64 signature = rg_gui_gpu_frame_signature(gpu);
	if (skip && skip->enabled && skip->valid && skip->signature == signature)
	{
		int pixel_width = 0;
		int pixel_height = 0;
		if (SDL_GetWindowSizeInPixels(window, &pixel_width, &pixel_height) &&
		    pixel_width > 0 && pixel_height > 0 &&
		    (u32)pixel_width == skip->width && (u32)pixel_height == skip->height)
		{
			skip->skipped_frames++;
			if (metrics) metrics->skipped = 1;
			result = 1;
			goto done;
		}
	}

	rg_gpu_upload_ring_begin(upload_ring, 1);
	int staged = rg_gui_gpu_stage_upload(gpu, text_renderer, upload_ring, &upload);
	rg_gpu_upload_ring_end(upload_ring);
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->stage_upload_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}
	if (!staged)
	{
		SDL_SetError("rg_gui GPU upload staging exceeded the configured demo limits");
		goto done;
	}

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(device);
	if (!command_buffer) goto encode_failed;
	if (upload.has_text || upload.has_geometry)
	{
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
		if (!copy)
		{
			SDL_CancelGPUCommandBuffer(command_buffer);
			goto encode_failed;
		}
		rg_gui_gpu_encode_upload(gpu, copy, upload_ring, &upload);
		SDL_EndGPUCopyPass(copy);
	}
	if (!rg_gui_gpu_dispatch_upload(gpu, command_buffer, &upload))
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		SDL_SetError("rg_gui GPU text dispatch rejected the prepared frame");
		goto encode_failed;
	}
	/* SDL forbids cancellation after swapchain acquisition. Validate packet order
	 * first; no further renderer staging/acknowledgement occurs before submission. */
	if (!rg_gui_gpu_upload_ready(gpu, &upload))
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		SDL_SetError("rg_gui upload packet must be restaged before submission");
		goto encode_failed;
	}
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->encode_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}

	SDL_GPUTexture* swapchain = NULL;
	u32 width = 0u;
	u32 height = 0u;
	int acquired = SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain,
	                                                     &width, &height);
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->swapchain_wait_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}
	if (!acquired)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		goto done;
	}
	if (swapchain)
	{
		SDL_GPUColorTargetInfo target = {0};
		target.texture = swapchain;
		target.clear_color = (SDL_FColor){DEMO_CLEAR_R, DEMO_CLEAR_G, DEMO_CLEAR_B, 1.0f};
		target.load_op = SDL_GPU_LOADOP_CLEAR;
		target.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, &target, 1u, NULL);
		if (!pass)
		{
			char render_error[512];
			SDL_strlcpy(render_error, SDL_GetError(), sizeof(render_error));
			if (metrics)
			{
				u64 now = SDL_GetTicksNS();
				metrics->draw_encode_ms = (double)(now - stage_start) / 1000000.0;
				stage_start = now;
			}
			int submitted = SDL_SubmitGPUCommandBuffer(command_buffer) ? 1 : 0;
			if (submitted) rg_gui_gpu_upload_commit(gpu, &upload);
			if (metrics)
			{
				metrics->submit_ms = (double)(SDL_GetTicksNS() - stage_start) / 1000000.0;
				metrics->submitted = submitted;
			}
			if (!submitted)
			{
				char submit_error[512];
				SDL_strlcpy(submit_error, SDL_GetError(), sizeof(submit_error));
				SDL_SetError("%s; command submission also failed: %s",
				             render_error[0] ? render_error : "Could not begin the demo render pass",
				             submit_error[0] ? submit_error : "unknown submission error");
			}
			else
			{
				SDL_SetError("%s", render_error[0] ? render_error : "Could not begin the demo render pass");
			}
			goto done;
		}
		RgGuiGpuDrawDesc draw_desc = {0};
		draw_desc.output_width = width;
		draw_desc.output_height = height;
		draw_desc.viewport = (SDL_Rect){0, 0, (int)width, (int)height};
		RgGuiGpuStats draw_stats = rg_gui_gpu_draw(gpu, command_buffer, pass, &draw_desc, &upload);
		if (metrics) metrics->draw_stats = draw_stats;
		SDL_EndGPURenderPass(pass);
	}
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->draw_encode_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}
	int submitted = SDL_SubmitGPUCommandBuffer(command_buffer) ? 1 : 0;
	if (submitted) rg_gui_gpu_upload_commit(gpu, &upload);
	if (metrics)
	{
		metrics->submit_ms = (double)(SDL_GetTicksNS() - stage_start) / 1000000.0;
		metrics->submitted = submitted;
	}
	if (!submitted) goto done;
	if (out_presented) *out_presented = swapchain != NULL;
	if (metrics) metrics->presented = swapchain != NULL;
	if (skip && swapchain)
	{
		skip->signature = signature;
		skip->width = width;
		skip->height = height;
		skip->valid = 1;
	}
	result = 1;
	goto done;

encode_failed:
	if (metrics) metrics->encode_ms = (double)(SDL_GetTicksNS() - stage_start) / 1000000.0;
done:
	rg_gui_gpu_upload_abort(gpu, &upload);
	if (metrics)
	{
		const RgGuiRendererStats* text_stats = rg_gui_renderer_stats(text_renderer);
		if (text_stats) metrics->text_stats = *text_stats;
		metrics->total_ms = (double)(SDL_GetTicksNS() - render_start) / 1000000.0;
	}
	return result;
}

static int demo_render_draw_list_profiled(SDL_GPUDevice* device, SDL_Window* window,
                                          RgGuiGpuRenderer* gpu, RgGuiRenderer* text_renderer,
                                          RgGpuUploadRing* upload_ring,
                                          const RgGuiDrawList* draw_list, u32 overlay_start,
                                          int* out_presented, DemoRenderMetrics* metrics)
{
	return demo_render_draw_list_skippable(device, window, gpu, text_renderer, upload_ring,
	                                       draw_list, overlay_start, NULL, out_presented, metrics);
}

/* Render through a persistent target, repainting only dirty tiles. Byte-identical
 * output to the direct path is the contract; verify mode checks it by readback. */
RGINLINE int demo_render_draw_list_tiled(SDL_GPUDevice* device, SDL_Window* window,
                                       RgGuiGpuRenderer* gpu, RgGuiRenderer* text_renderer,
                                       RgGpuUploadRing* upload_ring,
                                       const RgGuiDrawList* draw_list, u32 overlay_start,
                                       DemoTileTarget* tiles,
                                       int* out_presented, DemoRenderMetrics* metrics)
{
	u64 render_start = metrics ? SDL_GetTicksNS() : 0u;
	u64 stage_start = render_start;
	int result = 0;
	RgGuiGpuUpload upload = {0};
	if (metrics)
	{
		memset(metrics, 0, sizeof(*metrics));
		metrics->draw_commands = draw_list ? draw_list->count : 0u;
	}
	if (out_presented) *out_presented = 0;
	SDL_ClearError();
	rg_gui_renderer_begin_frame(text_renderer);
	int prepared = rg_gui_gpu_prepare(gpu, text_renderer, draw_list, overlay_start);
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->prepare_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}
	if (!prepared)
	{
		SDL_SetError("rg_gui GPU preparation exceeded the configured demo limits");
		goto done;
	}

	rg_gpu_upload_ring_begin(upload_ring, 1);
	int staged = rg_gui_gpu_stage_upload(gpu, text_renderer, upload_ring, &upload);
	rg_gpu_upload_ring_end(upload_ring);
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->stage_upload_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}
	if (!staged)
	{
		SDL_SetError("rg_gui GPU upload staging exceeded the configured demo limits");
		goto done;
	}

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(device);
	if (!command_buffer) goto encode_failed;
	if (upload.has_text || upload.has_geometry)
	{
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
		if (!copy)
		{
			SDL_CancelGPUCommandBuffer(command_buffer);
			goto encode_failed;
		}
		rg_gui_gpu_encode_upload(gpu, copy, upload_ring, &upload);
		SDL_EndGPUCopyPass(copy);
	}
	if (!rg_gui_gpu_dispatch_upload(gpu, command_buffer, &upload) ||
	    !rg_gui_gpu_upload_ready(gpu, &upload))
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		SDL_SetError("rg_gui upload packet was rejected before submission");
		goto encode_failed;
	}
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->encode_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}

	SDL_GPUTexture* swapchain = NULL;
	u32 width = 0u;
	u32 height = 0u;
	int acquired = SDL_WaitAndAcquireGPUSwapchainTexture(command_buffer, window, &swapchain,
	                                                     &width, &height);
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->swapchain_wait_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}
	if (!acquired)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		goto done;
	}
	if (!swapchain)
	{
		/* Nothing to present (hidden or minimized): submit the uploads only. */
		int submitted = SDL_SubmitGPUCommandBuffer(command_buffer) ? 1 : 0;
		if (submitted) rg_gui_gpu_upload_commit(gpu, &upload);
		if (metrics) metrics->submitted = submitted;
		result = submitted;
		goto done;
	}

	if (!demo_tile_target_ensure(device, tiles, width, height))
	{
		/* SDL forbids cancellation after acquisition; submit the uploads so the
		 * packet protocol stays consistent, then report the failure. */
		char error[512];
		SDL_strlcpy(error, SDL_GetError(), sizeof(error));
		if (SDL_SubmitGPUCommandBuffer(command_buffer)) rg_gui_gpu_upload_commit(gpu, &upload);
		SDL_SetError("%s", error[0] ? error : "Could not create the dirty-tile targets");
		goto done;
	}

	u64 tile_start = SDL_GetTicksNS();
	RgGuiTileRedraw mode = rg_gui_tile_cache_update(&tiles->cache, gpu, text_renderer, draw_list,
	                                                overlay_start, width, height, 0.0f, 0.0f);
	if (metrics)
	{
		metrics->tile_update_ms = (double)(SDL_GetTicksNS() - tile_start) / 1000000.0;
		metrics->tile_mode = (u32)mode;
		metrics->tiles_dirty = tiles->cache.stats.dirty_tiles;
		metrics->tiles_total = tiles->cache.stats.total_tiles;
		metrics->tile_rects = tiles->cache.stats.rects;
	}
	tiles->frames++;
	tiles->total_tile_sum += tiles->cache.stats.total_tiles;
	tiles->dirty_tile_sum += mode == RG_GUI_TILE_REDRAW_FULL ? tiles->cache.stats.total_tiles
	                                                          : tiles->cache.stats.dirty_tiles;

	RgGuiGpuDrawDesc draw_desc = {0};
	draw_desc.output_width = width;
	draw_desc.output_height = height;
	RgGuiGpuStats draw_stats = {0};
	const SDL_FColor clear_color = {DEMO_CLEAR_R, DEMO_CLEAR_G, DEMO_CLEAR_B, 1.0f};

	if (mode != RG_GUI_TILE_REDRAW_NONE)
	{
		SDL_GPUColorTargetInfo target = {0};
		target.texture = tiles->color;
		target.clear_color = clear_color;
		target.load_op = mode == RG_GUI_TILE_REDRAW_FULL ? SDL_GPU_LOADOP_CLEAR : SDL_GPU_LOADOP_LOAD;
		target.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, &target, 1u, NULL);
		if (!pass)
		{
			char error[512];
			SDL_strlcpy(error, SDL_GetError(), sizeof(error));
			if (SDL_SubmitGPUCommandBuffer(command_buffer)) rg_gui_gpu_upload_commit(gpu, &upload);
			SDL_SetError("%s", error[0] ? error : "Could not begin the dirty-tile render pass");
			goto done;
		}
		if (mode == RG_GUI_TILE_REDRAW_FULL)
		{
			draw_desc.viewport = (SDL_Rect){0, 0, (int)width, (int)height};
			draw_stats = rg_gui_gpu_draw(gpu, command_buffer, pass, &draw_desc, &upload);
		}
		else
		{
			/* A dirty rectangle must be repainted from its background up, so the
			 * whole list is drawn once per rectangle; batches outside it are
			 * skipped by the viewport scissor. */
			for (u32 r = 0u; r < tiles->cache.stats.rects; r++)
			{
				draw_desc.viewport = tiles->cache.rects[r];
				SDL_SetGPUScissor(pass, &draw_desc.viewport);
				RgGuiGpuStats part = rg_gui_gpu_draw(gpu, command_buffer, pass, &draw_desc, &upload);
				draw_stats.draw_calls += part.draw_calls;
				draw_stats.vertices += part.vertices;
				draw_stats.indices += part.indices;
				draw_stats.indexed_refs += part.indexed_refs;
				draw_stats.items += part.items;
				draw_stats.geometry_vertices = part.geometry_vertices;
				draw_stats.text_instances = part.text_instances;
			}
		}
		SDL_EndGPURenderPass(pass);
	}
	if (tiles->verify_enabled)
	{
		SDL_GPUColorTargetInfo target = {0};
		target.texture = tiles->verify;
		target.clear_color = clear_color;
		target.load_op = SDL_GPU_LOADOP_CLEAR;
		target.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, &target, 1u, NULL);
		if (pass)
		{
			draw_desc.viewport = (SDL_Rect){0, 0, (int)width, (int)height};
			rg_gui_gpu_draw(gpu, command_buffer, pass, &draw_desc, &upload);
			SDL_EndGPURenderPass(pass);
		}
		else
		{
			tiles->verify_failures++;
		}
	}

	SDL_GPUBlitInfo blit = {0};
	blit.source.texture = tiles->color;
	blit.source.w = width;
	blit.source.h = height;
	blit.destination.texture = swapchain;
	blit.destination.w = width;
	blit.destination.h = height;
	blit.load_op = SDL_GPU_LOADOP_DONT_CARE;
	blit.filter = SDL_GPU_FILTER_NEAREST;
	SDL_BlitGPUTexture(command_buffer, &blit);

	int verify_now = tiles->verify_enabled && tiles->download &&
	                 (tiles->frames % tiles->verify_interval) == 0u;
	if (verify_now)
	{
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
		if (!copy)
		{
			/* Never compare stale download contents. */
			tiles->verify_failures++;
			verify_now = 0;
		}
		else
		{
			SDL_GPUTextureRegion region = {0};
			region.w = width;
			region.h = height;
			region.d = 1u;
			SDL_GPUTextureTransferInfo destination = {0};
			destination.transfer_buffer = tiles->download;
			destination.pixels_per_row = width;
			destination.rows_per_layer = height;
			region.texture = tiles->color;
			destination.offset = 0u;
			SDL_DownloadFromGPUTexture(copy, &region, &destination);
			region.texture = tiles->verify;
			destination.offset = width * height * 4u;
			SDL_DownloadFromGPUTexture(copy, &region, &destination);
			SDL_EndGPUCopyPass(copy);
		}
	}
	if (metrics)
	{
		u64 now = SDL_GetTicksNS();
		metrics->draw_encode_ms = (double)(now - stage_start) / 1000000.0;
		stage_start = now;
	}

	int submitted;
	if (verify_now)
	{
		SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command_buffer);
		submitted = fence != NULL;
		if (fence)
		{
			int waited = SDL_WaitForGPUFences(device, true, &fence, 1u) ? 1 : 0;
			SDL_ReleaseGPUFence(device, fence);
			const u8* pixels = waited ? (const u8*)SDL_MapGPUTransferBuffer(device, tiles->download, false) : NULL;
			if (pixels)
			{
				size_t bytes = (size_t)width * height * 4u;
				int mismatch = memcmp(pixels, pixels + bytes, bytes) != 0;
				SDL_UnmapGPUTransferBuffer(device, tiles->download);
				tiles->verified_frames++;
				tiles->mismatched_frames += mismatch != 0;
				if (metrics)
				{
					metrics->tile_verified = 1u;
					metrics->tile_mismatch = (u32)mismatch;
				}
			}
			else
			{
				tiles->verify_failures++;
			}
		}
		else
		{
			tiles->verify_failures++;
		}
	}
	else
	{
		submitted = SDL_SubmitGPUCommandBuffer(command_buffer) ? 1 : 0;
	}
	if (submitted)
	{
		rg_gui_gpu_upload_commit(gpu, &upload);
		rg_gui_tile_cache_commit(&tiles->cache);
	}
	if (metrics)
	{
		metrics->submit_ms = (double)(SDL_GetTicksNS() - stage_start) / 1000000.0;
		metrics->submitted = submitted;
		metrics->draw_stats = draw_stats;
	}
	if (!submitted) goto done;
	if (out_presented) *out_presented = 1;
	if (metrics) metrics->presented = 1;
	result = 1;
	goto done;

encode_failed:
	if (metrics) metrics->encode_ms = (double)(SDL_GetTicksNS() - stage_start) / 1000000.0;
done:
	rg_gui_gpu_upload_abort(gpu, &upload);
	if (metrics)
	{
		const RgGuiRendererStats* text_stats = rg_gui_renderer_stats(text_renderer);
		if (text_stats) metrics->text_stats = *text_stats;
		metrics->total_ms = (double)(SDL_GetTicksNS() - render_start) / 1000000.0;
	}
	return result;
}

RGINLINE int demo_render_frame_skippable(SDL_GPUDevice* device, SDL_Window* window,
                                         RgGuiGpuRenderer* gpu, RgGuiRenderer* text_renderer,
                                         RgGpuUploadRing* upload_ring, RgGuiContext* gui,
                                         DemoFrameSkip* skip, int* out_presented,
                                         DemoRenderMetrics* metrics)
{
	return demo_render_draw_list_skippable(device, window, gpu, text_renderer, upload_ring,
	                                       rg_gui_draw_list(gui),
	                                       rg_gui_draw_list_overlay_start(gui), skip,
	                                       out_presented, metrics);
}

RGINLINE int demo_render_draw_list(SDL_GPUDevice* device, SDL_Window* window,
                                 RgGuiGpuRenderer* gpu, RgGuiRenderer* text_renderer,
                                 RgGpuUploadRing* upload_ring,
                                 const RgGuiDrawList* draw_list, u32 overlay_start,
                                 int* out_presented)
{
	return demo_render_draw_list_profiled(device, window, gpu, text_renderer, upload_ring,
	                                      draw_list, overlay_start, out_presented, NULL);
}

RGINLINE int demo_render_frame_profiled(SDL_GPUDevice* device, SDL_Window* window,
                                      RgGuiGpuRenderer* gpu, RgGuiRenderer* text_renderer,
                                      RgGpuUploadRing* upload_ring, RgGuiContext* gui,
                                      int* out_presented, DemoRenderMetrics* metrics)
{
	return demo_render_draw_list_profiled(device, window, gpu, text_renderer, upload_ring,
	                                      rg_gui_draw_list(gui),
	                                      rg_gui_draw_list_overlay_start(gui), out_presented, metrics);
}

RGINLINE int demo_render_frame(SDL_GPUDevice* device, SDL_Window* window,
                             RgGuiGpuRenderer* gpu, RgGuiRenderer* text_renderer,
                             RgGpuUploadRing* upload_ring, RgGuiContext* gui,
                             int* out_presented)
{
	return demo_render_frame_profiled(device, window, gpu, text_renderer, upload_ring,
	                                  gui, out_presented, NULL);
}
