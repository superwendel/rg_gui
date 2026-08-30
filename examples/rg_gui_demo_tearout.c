// Native multi-window and cross-viewport docking demo.

#define RG_GUI_ENABLE_VIEWPORTS 1
#include "rg_gui_demo_common.h"

#define TEAROUT_PANEL_COUNT 3u
#define TEAROUT_PLOT_SAMPLES 90u

typedef enum TearoutPanel
{
	TEAROUT_PANEL_INSPECTOR = 0,
	TEAROUT_PANEL_CONSOLE,
	TEAROUT_PANEL_FRAME_STATS
} TearoutPanel;

typedef struct TearoutMouseOrigin
{
	f32 x;
	f32 y;
	int valid;
} TearoutMouseOrigin;

typedef struct TearoutNativeWindow
{
	SDL_Window* window;
	SDL_WindowID window_id;
	TearoutMouseOrigin origin;
	int pixel_w;
	int pixel_h;
	int claimed;
} TearoutNativeWindow;

typedef struct TearoutDragState
{
	int active;
	u32 panel;
	RgGuiWindowState* window;
	RgGuiId window_id;
	const char* title;
	RgGuiRect global_rect;
} TearoutDragState;

typedef struct TearoutDockRequest
{
	int pending;
	u32 panel;
} TearoutDockRequest;

typedef struct TearoutDemoState
{
	RgGuiDockSpaceState main_dockspace;
	RgGuiDockSpaceState native_dockspace;
	RgGuiWindowState panels[TEAROUT_PANEL_COUNT];
	int panel_host[TEAROUT_PANEL_COUNT];
	int dock_initialized;

	TearoutNativeWindow native;
	TearoutDockRequest dock_request;
	TearoutDragState drag;
	int spawn_panel;
	int return_all;
	int close_after_return;
	int start_torn_out;
	int native_created_count;

	char object_name[64];
	char console_text[KB(4)];
	RgGuiTextAreaState console_area;
	int live_preview;
	f32 exposure;
	f32 activity;
	f32 frame_ms[TEAROUT_PLOT_SAMPLES];
	u32 plot_count;
	char status[128];
} TearoutDemoState;

static const char* tearout_panel_titles[TEAROUT_PANEL_COUNT] =
    {
        "Inspector",
        "Console",
        "Frame Stats"};

static RgGuiId tearout_main_viewport_id(void)
{
	return rg_gui_id_str("tearout_main_viewport");
}

static RgGuiId tearout_native_viewport_id(void)
{
	return rg_gui_id_str("tearout_native_viewport");
}

static RgGuiId tearout_main_dockspace_id(void)
{
	return rg_gui_id_str("tearout_main_dockspace");
}

static RgGuiId tearout_native_dockspace_id(void)
{
	return rg_gui_id_str("tearout_native_dockspace");
}

static RgGuiId tearout_panel_id(u32 panel)
{
	return rg_gui_id_combine(rg_gui_id_str("tearout_panel"), (u64)(panel + 1u));
}

static void tearout_state_init(TearoutDemoState* state)
{
	memset(state, 0, sizeof(*state));
	state->spawn_panel = -1;
	state->live_preview = 1;
	state->exposure = 1.0f;
	SDL_strlcpy(state->object_name, "Player Camera", sizeof(state->object_name));
	SDL_strlcpy(state->console_text,
	            "Tear-out console\n\n"
	            "Edit this text, then move the panel between native windows.\n"
	            "Ordered keyboard and clipboard input follows the focused viewport.\n",
	            sizeof(state->console_text));
	SDL_strlcpy(state->status, "Drag a panel tab or title bar outside the native windows.",
	            sizeof(state->status));
}

static void tearout_update_stats(TearoutDemoState* state, f32 frame_time, f32 delta_time)
{
	for (u32 i = 1u; i < TEAROUT_PLOT_SAMPLES; i++)
		state->frame_ms[i - 1u] = state->frame_ms[i];
	state->frame_ms[TEAROUT_PLOT_SAMPLES - 1u] = frame_time * 1000.0f;
	if (state->plot_count < TEAROUT_PLOT_SAMPLES) state->plot_count++;
	state->activity += delta_time * 0.35f;
	if (state->activity > 1.0f) state->activity -= 1.0f;
}

static int tearout_refresh_window_origin(SDL_Window* window, TearoutMouseOrigin* origin)
{
	if (!window || !origin) return 0;
	if (origin->valid) return 1;
	int window_x = 0;
	int window_y = 0;
	int border_top = 0;
	int border_left = 0;
	int border_bottom = 0;
	int border_right = 0;
	if (!SDL_GetWindowPosition(window, &window_x, &window_y) ||
	    !SDL_GetWindowBordersSize(window, &border_top, &border_left,
	                              &border_bottom, &border_right))
		return 0;
	origin->x = (f32)(window_x + border_left);
	origin->y = (f32)(window_y + border_top);
	origin->valid = 1;
	return 1;
}

static int tearout_window_local_mouse(SDL_Window* window, f32 global_x, f32 global_y,
                                      SDL_Window* mouse_focus, f32 focus_local_x,
                                      f32 focus_local_y, int pixel_w, int pixel_h,
                                      TearoutMouseOrigin* origin)
{
	if (!window) return 0;

	f32 origin_x = 0.0f;
	f32 origin_y = 0.0f;
	int origin_valid = origin && origin->valid;
	if (origin_valid)
	{
		origin_x = origin->x;
		origin_y = origin->y;
	}
	if (mouse_focus == window)
	{
		origin_x = global_x - focus_local_x;
		origin_y = global_y - focus_local_y;
		origin_valid = 1;
	}
	if (!origin_valid)
	{
		if (!tearout_refresh_window_origin(window, origin)) return 0;
		origin_x = origin->x;
		origin_y = origin->y;
	}
	if (origin)
	{
		origin->x = origin_x;
		origin->y = origin_y;
		origin->valid = 1;
	}

	f32 local_x = global_x - origin_x;
	f32 local_y = global_y - origin_y;
	return pixel_w > 0 && pixel_h > 0 && local_x >= 0.0f && local_y >= 0.0f &&
	       local_x < (f32)pixel_w && local_y < (f32)pixel_h;
}

static int tearout_point_in_window(f32 x, f32 y, const TearoutMouseOrigin* origin,
                                   int pixel_w, int pixel_h)
{
	return origin && origin->valid && pixel_w > 0 && pixel_h > 0 &&
	       x >= origin->x && y >= origin->y &&
	       x < origin->x + (f32)pixel_w && y < origin->y + (f32)pixel_h;
}

static void tearout_track_drag(RgGuiContext* gui, TearoutDemoState* state, u32 panel,
                               rg_vec2 origin)
{
	RgGuiId window_id = tearout_panel_id(panel);
	RgGuiId drag_id = rg_gui_id_combine(rg_gui_id_scoped(gui, window_id), 1u);
	if (gui->active_id != drag_id) return;

	state->drag.active = 1;
	state->drag.panel = panel;
	state->drag.window = &state->panels[panel];
	state->drag.window_id = window_id;
	state->drag.title = tearout_panel_titles[panel];
	state->drag.global_rect = state->panels[panel].rect;
	state->drag.global_rect.x += origin.x;
	state->drag.global_rect.y += origin.y;
}

static void tearout_draw_panel(RgGuiContext* gui, TearoutDemoState* state,
                               u32 panel, u32 flags)
{
	static const RgGuiRect initial[TEAROUT_PANEL_COUNT] =
	    {
	        {28.0f, 94.0f, 300.0f, 500.0f},
	        {350.0f, 94.0f, 500.0f, 500.0f},
	        {872.0f, 94.0f, 300.0f, 500.0f}};
	if (panel >= TEAROUT_PANEL_COUNT) return;
	if (!rg_gui_window_begin(gui, &state->panels[panel], tearout_panel_titles[panel],
	                         initial[panel], 260.0f, 220.0f, 8.0f, flags,
	                         tearout_panel_id(panel)))
		return;

	if (panel == TEAROUT_PANEL_INSPECTOR)
	{
		int submitted = 0;
		rg_gui_label_static(gui, "This state survives every host transition.",
		                    rg_gui_layout_next(gui, 26.0f));
		rg_gui_text_input(gui, "Object", state->object_name, sizeof(state->object_name),
		                  rg_gui_layout_next(gui, 30.0f), rg_gui_id_str("tearout_object"),
		                  &submitted);
		RG_GUI_UNUSED(submitted);
		rg_gui_checkbox_static(gui, "Live preview", &state->live_preview,
		                       rg_gui_layout_next(gui, 26.0f),
		                       rg_gui_id_str("tearout_live_preview"));
		rg_gui_slider_float_input(gui, "Exposure", &state->exposure, 0.0f, 4.0f,
		                          rg_gui_layout_next(gui, 30.0f),
		                          rg_gui_id_str("tearout_exposure"));
		rg_gui_progress_bar(gui, "Animated sample", state->activity, 0.0f, 1.0f,
		                    rg_gui_layout_next(gui, 24.0f));
	}
	else if (panel == TEAROUT_PANEL_CONSOLE)
	{
		rg_gui_label_static(gui, "Clipboard, selection, and typing are window-filtered.",
		                    rg_gui_layout_next(gui, 26.0f));
		rg_gui_text_area(gui, &state->console_area, state->console_text,
		                 sizeof(state->console_text), rg_gui_layout_next(gui, 330.0f),
		                 rg_gui_id_str("tearout_console_text"));
	}
	else
	{
		char timing[64];
		f32 latest = state->frame_ms[TEAROUT_PLOT_SAMPLES - 1u];
		SDL_snprintf(timing, sizeof(timing), "Frame %.2f ms / %.0f FPS", latest,
		             latest > 0.0f ? 1000.0f / latest : 0.0f);
		rg_gui_label(gui, timing, rg_gui_layout_next(gui, 26.0f));
		rg_gui_plot_lines(gui, "Frame time",
		                  state->frame_ms + (TEAROUT_PLOT_SAMPLES - state->plot_count),
		                  state->plot_count, 0.0f, 34.0f,
		                  rg_gui_layout_next(gui, 150.0f));
		rg_gui_label_static(gui, "The same GPU renderer serves both swapchains.",
		                    rg_gui_layout_next(gui, 28.0f));
	}

	rg_gui_window_end(gui, &state->panels[panel]);
}

static void tearout_update_hosts(TearoutDemoState* state)
{
	RgGuiId main_id = tearout_main_dockspace_id();
	RgGuiId native_id = tearout_native_dockspace_id();
	for (u32 i = 0u; i < TEAROUT_PANEL_COUNT; i++)
	{
		if (state->panels[i].dockspace_id == main_id) state->panel_host[i] = 0;
		else if (state->panels[i].dockspace_id == native_id) state->panel_host[i] = 1;
	}
}

static void tearout_draw_main_shell(RgGuiContext* gui, TearoutDemoState* state,
                                    int width, int height)
{
	RgGuiRect whole = rg_gui_make_rect(0.0f, 0.0f, (f32)width, (f32)height);
	rg_gui_window_set_bounds(gui, whole);
	rg_gui_push_rect(gui, whole, gui->style.color_bg);
	rg_gui_push_rect(gui, rg_gui_make_rect(0.0f, 0.0f, (f32)width, 68.0f),
	                 rg_gui_color(0.055f, 0.075f, 0.11f, 1.0f));
	rg_gui_push_rect(gui, rg_gui_make_rect(0.0f, 66.0f, (f32)width, 2.0f),
	                 gui->style.color_accent);
	rg_gui_label_static(gui, "rg_gui native tear-out demo",
	                    rg_gui_make_rect(18.0f, 7.0f, 360.0f, 25.0f));
	rg_gui_label(gui, state->status,
	             rg_gui_make_rect(18.0f, 34.0f, (f32)width - 390.0f, 24.0f));

	f32 button_y = 18.0f;
	f32 return_x = (f32)width - 142.0f;
	f32 tear_x = return_x - 170.0f;
	if (rg_gui_button_static(gui, "Tear out Inspector",
	                         rg_gui_make_rect(tear_x, button_y, 158.0f, 32.0f),
	                         rg_gui_id_str("tearout_inspector_button")))
	{
		if (state->native.window && state->panel_host[TEAROUT_PANEL_INSPECTOR] == 1)
		{
			if (!SDL_RaiseWindow(state->native.window))
				SDL_snprintf(state->status, sizeof(state->status),
				             "Could not raise tear-out: %s", SDL_GetError());
		}
		else
			state->spawn_panel = TEAROUT_PANEL_INSPECTOR;
	}
	if (state->native.window &&
	    rg_gui_button_static(gui, "Return all",
	                         rg_gui_make_rect(return_x, button_y, 124.0f, 32.0f),
	                         rg_gui_id_str("tearout_return_button")))
	{
		state->return_all = 1;
		state->close_after_return = 1;
	}

	RgGuiRect dock_rect = rg_gui_make_rect(12.0f, 80.0f, (f32)width - 24.0f,
	                                       (f32)height - 92.0f);
	if (dock_rect.w < 0.0f) dock_rect.w = 0.0f;
	if (dock_rect.h < 0.0f) dock_rect.h = 0.0f;
	rg_gui_push_rect(gui, dock_rect, gui->style.color_panel);
	rg_gui_push_rect_outline(gui, dock_rect, gui->style.color_border,
	                         gui->style.border_thickness);
	rg_gui_dockspace_begin(gui, &state->main_dockspace, dock_rect,
	                       tearout_main_dockspace_id());
	if (!state->dock_initialized && dock_rect.w > 0.0f && dock_rect.h > 0.0f)
	{
		rg_gui_dockspace_dock(gui, &state->main_dockspace,
		                      &state->panels[TEAROUT_PANEL_INSPECTOR],
		                      tearout_panel_titles[TEAROUT_PANEL_INSPECTOR],
		                      tearout_panel_id(TEAROUT_PANEL_INSPECTOR),
		                      RG_GUI_DOCK_SLOT_LEFT);
		rg_gui_dockspace_dock(gui, &state->main_dockspace,
		                      &state->panels[TEAROUT_PANEL_CONSOLE],
		                      tearout_panel_titles[TEAROUT_PANEL_CONSOLE],
		                      tearout_panel_id(TEAROUT_PANEL_CONSOLE),
		                      RG_GUI_DOCK_SLOT_CENTER);
		rg_gui_dockspace_dock(gui, &state->main_dockspace,
		                      &state->panels[TEAROUT_PANEL_FRAME_STATS],
		                      tearout_panel_titles[TEAROUT_PANEL_FRAME_STATS],
		                      tearout_panel_id(TEAROUT_PANEL_FRAME_STATS),
		                      RG_GUI_DOCK_SLOT_RIGHT);
		state->dock_initialized = 1;
	}
	rg_gui_dockspace_end(gui, &state->main_dockspace);
}

static void tearout_draw_hosted_panels(RgGuiContext* gui, TearoutDemoState* state,
                                       int host, rg_vec2 origin)
{
	u32 flags = RG_GUI_WINDOW_MOVABLE | RG_GUI_WINDOW_RESIZABLE |
	            RG_GUI_WINDOW_COLLAPSIBLE | RG_GUI_WINDOW_SNAP |
	            RG_GUI_WINDOW_DOCKABLE;
	for (u32 panel = 0u; panel < TEAROUT_PANEL_COUNT; panel++)
	{
		if (state->panel_host[panel] != host) continue;
		tearout_draw_panel(gui, state, panel, flags);
		tearout_track_drag(gui, state, panel, origin);
	}
}

static SDL_Window* tearout_create_native(SDL_GPUDevice* device, SDL_Window* main_window,
                                         int hidden, int width, int height,
                                         SDL_WindowID* out_window_id, int* out_claimed)
{
	SDL_WindowFlags flags = SDL_WINDOW_RESIZABLE;
	if (hidden) flags |= SDL_WINDOW_HIDDEN;
	SDL_Window* window = SDL_CreateWindow("rg_gui tear-out", width, height, flags);
	if (!window) return NULL;
	if (!SDL_SetWindowMinimumSize(window, 360, 280))
	{
		SDL_DestroyWindow(window);
		return NULL;
	}
	if (!rg_gpu_claim_window(device, window))
	{
		SDL_DestroyWindow(window);
		return NULL;
	}

	SDL_GPUTextureFormat main_format = rg_gpu_swapchain_format(device, main_window);
	SDL_GPUTextureFormat native_format = rg_gpu_swapchain_format(device, window);
	if (main_format != native_format)
	{
		SDL_ReleaseWindowFromGPUDevice(device, window);
		SDL_DestroyWindow(window);
		SDL_SetError("Tear-out swapchain format does not match the main window");
		return NULL;
	}
	if (!SDL_SetGPUSwapchainParameters(device, window,
	                                   SDL_GPU_SWAPCHAINCOMPOSITION_SDR,
	                                   SDL_GPU_PRESENTMODE_VSYNC))
	{
		SDL_ReleaseWindowFromGPUDevice(device, window);
		SDL_DestroyWindow(window);
		return NULL;
	}

	SDL_WindowID window_id = SDL_GetWindowID(window);
	if (window_id == 0u)
	{
		SDL_ReleaseWindowFromGPUDevice(device, window);
		SDL_DestroyWindow(window);
		return NULL;
	}
	if (out_window_id) *out_window_id = window_id;
	if (out_claimed) *out_claimed = 1;
	return window;
}

static void tearout_destroy_native(SDL_GPUDevice* device, RgGuiContext* gui,
                                   RgInputState* input, DemoPlatformState* platform_state,
                                   TearoutDemoState* state)
{
	if (!state->native.window) return;
	if (input && platform_state)
		demo_platform_forget_text_window(platform_state, input, state->native.window);
	if (device) rg_gpu_wait_idle(device);
	if (state->native.claimed)
		SDL_ReleaseWindowFromGPUDevice(device, state->native.window);
	SDL_DestroyWindow(state->native.window);
	rg_gui_viewport_release(gui, tearout_native_viewport_id());
	memset(&state->native, 0, sizeof(state->native));
	state->dock_request.pending = 0;
}

static int tearout_open_for_panel(SDL_GPUDevice* device, SDL_Window* main_window,
                                  TearoutDemoState* state, u32 panel, int hidden,
                                  int pos_x, int pos_y, int width, int height)
{
	if (panel >= TEAROUT_PANEL_COUNT) return 0;
	if (width < 360) width = 360;
	if (height < 280) height = 280;
	int created = 0;

	if (!state->native.window)
	{
		state->native.window = tearout_create_native(device, main_window, hidden,
		                                             width, height,
		                                             &state->native.window_id,
		                                             &state->native.claimed);
		if (!state->native.window) return 0;
		created = 1;
	}
	else
	{
		if (!SDL_SetWindowSize(state->native.window, width, height)) return 0;
	}
	if (!SDL_SetWindowPosition(state->native.window, pos_x, pos_y))
	{
		if (created)
		{
			SDL_ReleaseWindowFromGPUDevice(device, state->native.window);
			SDL_DestroyWindow(state->native.window);
			memset(&state->native, 0, sizeof(state->native));
		}
		return 0;
	}
	if (created) state->native_created_count++;
	state->native.origin.valid = 0;
	state->dock_request.pending = 1;
	state->dock_request.panel = panel;
	state->panel_host[panel] = 1;
	if (!hidden) SDL_RaiseWindow(state->native.window);
	SDL_snprintf(state->status, sizeof(state->status), "%s moved to a native tear-out.",
	             tearout_panel_titles[panel]);
	return 1;
}

static const RgGuiViewport* tearout_find_viewport(const RgGuiContext* gui, RgGuiId id)
{
	u32 count = rg_gui_viewport_count(gui);
	for (u32 i = 0u; i < count; i++)
	{
		const RgGuiViewport* viewport = rg_gui_viewport_at(gui, i);
		if (viewport && viewport->id == id) return viewport;
	}
	return NULL;
}

int main(int argc, char** argv)
{
	int result = 1;
	int hidden = 0;
	int frame_limit = 0;
	int smoke_test = 0;
	int start_torn_out = 0;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--hidden") == 0) hidden = 1;
		else if (strcmp(argv[i], "--smoke-test") == 0) smoke_test = 1;
		else if (strcmp(argv[i], "--start-torn-out") == 0) start_torn_out = 1;
		else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
		{
			frame_limit = atoi(argv[++i]);
			if (frame_limit < 1) frame_limit = 1;
		}
	}
	if (smoke_test)
	{
		hidden = 0;
		frame_limit = 3;
		start_torn_out = 1;
	}

	SDL_Window* main_window = NULL;
	SDL_GPUDevice* device = NULL;
	SDL_GPUTexture* atlas = NULL;
	RgGuiGpuRenderer gpu = {0};
	RgGpuUploadRing upload_ring = {0};
	DemoFontAssets demo_font = {0};
	DemoPlatformState platform_state = {0};
	RgGuiContext gui = {0};
	RgInputState input = {0};
	TearoutDemoState state = {0};
	void* gui_memory = NULL;
	void* text_memory = NULL;
	int main_claimed = 0;

	tearout_state_init(&state);
	state.start_torn_out = start_torn_out;

	if (!SDL_Init(SDL_INIT_VIDEO)) goto cleanup;
	demo_platform_init(&platform_state);
	SDL_WindowFlags main_flags = SDL_WINDOW_RESIZABLE;
	if (hidden) main_flags |= SDL_WINDOW_HIDDEN;
	main_window = SDL_CreateWindow("rg_gui native tear-out demo", 1220, 760, main_flags);
	if (!main_window) goto cleanup;
	if (!SDL_SetWindowMinimumSize(main_window, 760, 480)) goto cleanup;

	RgGpuDeviceDesc device_desc = {0};
	device_desc.shader_formats = RG_GPU_DEFAULT_SHADER_FORMATS;
	device_desc.enable_debug = RG_GUI_DEMO_GPU_DEBUG;
	device = rg_gpu_device_create(&device_desc);
	if (!device || !rg_gpu_claim_window(device, main_window)) goto cleanup;
	main_claimed = 1;

	if (!demo_font_load(&demo_font)) goto cleanup;
	atlas = demo_atlas_create(device, demo_font.pixels,
	                          demo_font.atlas_width, demo_font.atlas_height);
	if (!atlas) goto cleanup;

	RgGuiInitDesc gui_desc = {0};
	gui_desc.font = &demo_font.font;
	gui_desc.max_draw_cmds = 8192u;
	gui_desc.text_buffer_size = KB(128);
	size_t gui_memory_size = rg_gui_memory_required(&gui_desc);
	if (!gui_memory_size)
	{
		SDL_SetError("Invalid rg_gui initialization limits");
		goto cleanup;
	}
	gui_memory = malloc(gui_memory_size);
	if (!gui_memory)
	{
		SDL_SetError("Out of memory while allocating the rg_gui arena");
		goto cleanup;
	}
	RgArena gui_arena = {(char*)gui_memory, gui_memory_size, 0u, gui_memory_size};
	if (!rg_gui_init(&gui, &gui_arena, &gui_desc))
	{
		SDL_SetError("rg_gui initialization rejected the demo arena or limits");
		goto cleanup;
	}

	RgGuiRendererLimits limits = rg_gui_renderer_limits_default();
	limits.max_frame_instances = 32768u;
	limits.max_batches = 4096u;
	size_t text_memory_size = rg_gui_renderer_memory_required(&limits, 0u);
	if (text_memory_size == SIZE_MAX)
	{
		SDL_SetError("Invalid text-renderer limits");
		goto cleanup;
	}
	text_memory = malloc(text_memory_size);
	if (!text_memory)
	{
		SDL_SetError("Out of memory while allocating the text-renderer arena");
		goto cleanup;
	}
	RgArena text_arena = {(char*)text_memory, text_memory_size, 0u, text_memory_size};
	RgGuiRenderer text_renderer;
	RgGuiRendererInitDesc text_desc = {0};
	text_desc.font = &demo_font.font;
	text_desc.limits = limits;
	if (!rg_gui_renderer_init(&text_renderer, &text_arena, &text_desc))
	{
		SDL_SetError("Text-renderer initialization rejected the demo arena or limits");
		goto cleanup;
	}

	char shader_root[1024];
	if (!demo_shader_root(shader_root, sizeof(shader_root))) goto cleanup;
	RgGuiGpuDesc gpu_desc = {0};
	gpu_desc.device = device;
	gpu_desc.target_format = rg_gpu_swapchain_format(device, main_window);
	gpu_desc.shader_root = shader_root;
	gpu_desc.atlas_texture = atlas;
	gpu_desc.atlas_width = demo_font.atlas_width;
	gpu_desc.atlas_height = demo_font.atlas_height;
	gpu_desc.max_cached_quads = limits.max_cached_quads;
	gpu_desc.max_runs = limits.max_frame_instances;
	gpu_desc.max_text_instances = limits.max_frame_instances;
	gpu_desc.max_geometry_vertices = 65536u;
	gpu_desc.max_items = 8192u;
	gpu_desc.min_filter = SDL_GPU_FILTER_LINEAR;
	gpu_desc.mag_filter = SDL_GPU_FILTER_LINEAR;
	if (!rg_gui_gpu_create(&gpu, &gpu_desc)) goto cleanup;
	if (!rg_gpu_upload_ring_init(&upload_ring, device, MB(8))) goto cleanup;

	rg_input_init(&input);
	RgInputEvent input_event_storage[512];
	char input_event_text[KB(16)];
	RgInputEventQueue input_events;
	rg_input_event_queue_init(&input_events, input_event_storage,
	                          RG_ARRAY_COUNT(input_event_storage), input_event_text,
	                          sizeof(input_event_text));
	RgGuiInputRouter input_router;
	rg_gui_input_router_init(&input_router);

	SDL_WindowID main_window_id = SDL_GetWindowID(main_window);
	if (main_window_id == 0u) goto cleanup;
	TearoutMouseOrigin main_origin = {0};
	SDL_MouseButtonFlags previous_global_buttons = SDL_GetMouseState(NULL, NULL);
	u64 previous_ticks = SDL_GetTicksNS();
	int running = 1;
	int submitted_frames = 0;
	int main_presented_frames = 0;
	int native_presented_frames = 0;

	while (running)
	{
		rg_input_update(&input);
		rg_input_event_queue_reset(&input_events, SDL_GetModState());
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT) running = 0;
			else if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
			{
				if (event.window.windowID == main_window_id) running = 0;
				else if (state.native.window && event.window.windowID == state.native.window_id)
				{
					state.return_all = 1;
					state.close_after_return = 1;
					if (!SDL_HideWindow(state.native.window)) goto cleanup;
				}
			}
			else if (event.type == SDL_EVENT_WINDOW_MOVED)
			{
				if (event.window.windowID == main_window_id) main_origin.valid = 0;
				else if (state.native.window && event.window.windowID == state.native.window_id)
					state.native.origin.valid = 0;
			}
			demo_platform_process_event(&platform_state, &event);
			rg_input_process_event_ex(&input, &event, &input_events);
		}
		if (rg_input_is_key_pressed(&input, SDL_SCANCODE_ESCAPE)) running = 0;
		if (!running) break;

		int main_w = 0;
		int main_h = 0;
		if (!SDL_GetWindowSizeInPixels(main_window, &main_w, &main_h)) goto cleanup;
		if (state.native.window)
		{
			if (!SDL_GetWindowSizeInPixels(state.native.window,
			                               &state.native.pixel_w,
			                               &state.native.pixel_h))
				goto cleanup;
		}

		SDL_Window* mouse_focus = SDL_GetMouseFocus();
		SDL_Window* keyboard_focus = SDL_GetKeyboardFocus();
		f32 focus_local_x = 0.0f;
		f32 focus_local_y = 0.0f;
		SDL_MouseButtonFlags global_buttons = SDL_GetMouseState(&focus_local_x,
		                                                        &focus_local_y);
		if (!tearout_refresh_window_origin(main_window, &main_origin) ||
		    (state.native.window &&
		     !tearout_refresh_window_origin(state.native.window, &state.native.origin)))
			goto cleanup;
		f32 global_x = -1000000.0f;
		f32 global_y = -1000000.0f;
		if (mouse_focus == main_window)
		{
			global_x = main_origin.x + focus_local_x;
			global_y = main_origin.y + focus_local_y;
		}
		else if (state.native.window && mouse_focus == state.native.window)
		{
			global_x = state.native.origin.x + focus_local_x;
			global_y = state.native.origin.y + focus_local_y;
		}
		if (state.drag.active)
			global_buttons = SDL_GetGlobalMouseState(&global_x, &global_y);

		int main_inside = tearout_window_local_mouse(main_window, global_x, global_y,
		                                             mouse_focus, focus_local_x,
		                                             focus_local_y, main_w, main_h,
		                                             &main_origin);
		int native_inside = 0;
		if (state.native.window)
		{
			native_inside = tearout_window_local_mouse(state.native.window, global_x, global_y,
			                                           mouse_focus, focus_local_x,
			                                           focus_local_y, state.native.pixel_w,
			                                           state.native.pixel_h,
			                                           &state.native.origin);
		}

		if ((global_buttons & SDL_BUTTON_LMASK) == 0)
			input.current_mouse[RG_MOUSE_BUTTON_LEFT] = false;
		if ((global_buttons & SDL_BUTTON_RMASK) == 0)
			input.current_mouse[RG_MOUSE_BUTTON_RIGHT] = false;
		if ((global_buttons & SDL_BUTTON_MMASK) == 0)
			input.current_mouse[RG_MOUSE_BUTTON_MIDDLE] = false;
		int global_left_released =
		    (global_buttons & SDL_BUTTON_LMASK) == 0 &&
		    (previous_global_buttons & SDL_BUTTON_LMASK) != 0;
		previous_global_buttons = global_buttons;

		RgGuiId main_viewport = tearout_main_viewport_id();
		RgGuiId native_viewport = tearout_native_viewport_id();
		RgGuiId focus_viewport = 0u;
		RgGuiId mouse_viewport = 0u;
		if (keyboard_focus == main_window) focus_viewport = main_viewport;
		else if (state.native.window && keyboard_focus == state.native.window)
			focus_viewport = native_viewport;
		if (mouse_focus == main_window) mouse_viewport = main_viewport;
		else if (state.native.window && mouse_focus == state.native.window)
			mouse_viewport = native_viewport;
		int mouse_focus_inside = 0;
		if (mouse_focus == main_window) mouse_focus_inside = main_inside;
		else if (state.native.window && mouse_focus == state.native.window)
			mouse_focus_inside = native_inside;

		rg_gui_input_router_begin(&input_router, &input, rg_vec2(global_x, global_y),
		                          focus_viewport, mouse_viewport, mouse_focus_inside);
		RgInputState main_input;
		RgInputState native_input;
		rg_gui_input_router_route(&input_router, main_viewport,
		                          rg_vec2(main_origin.x, main_origin.y), main_w, main_h,
		                          &main_input);
		if (state.native.window)
		{
			rg_gui_input_router_route(&input_router, native_viewport,
			                          rg_vec2(state.native.origin.x, state.native.origin.y),
			                          state.native.pixel_w, state.native.pixel_h,
			                          &native_input);
		}
		else
		{
			memset(&native_input, 0, sizeof(native_input));
		}
		rg_gui_input_router_end(&input_router);

		u64 ticks = SDL_GetTicksNS();
		f32 frame_time = (f32)((f64)(ticks - previous_ticks) / 1000000000.0);
		previous_ticks = ticks;
		f32 delta_time = frame_time;
		if (delta_time > 0.1f) delta_time = 0.1f;
		tearout_update_stats(&state, frame_time, delta_time);

		rg_gui_begin_frame_ex(&gui, &main_input, &input_events, main_window_id, delta_time);
		rg_gui_set_mouse_focus_viewport(&gui, mouse_viewport, mouse_focus_inside);
		rg_gui_set_viewport_origin(&gui, rg_vec2(main_origin.x, main_origin.y));
		tearout_draw_main_shell(&gui, &state, main_w, main_h);

		const RgGuiViewport* built_native_viewport = NULL;
		if (state.native.window && state.native.pixel_w > 0 && state.native.pixel_h > 0)
		{
			RgGuiRect native_bounds = rg_gui_make_rect(0.0f, 0.0f,
			                                           (f32)state.native.pixel_w,
			                                           (f32)state.native.pixel_h);
			RgGuiViewport* viewport = rg_gui_viewport_begin_ordered_ex(
			    &gui, native_viewport, native_bounds, &native_input, &input_events,
			    state.native.window_id,
			    rg_vec2(state.native.origin.x, state.native.origin.y));
			if (viewport)
			{
				rg_gui_push_rect(&gui, native_bounds, gui.style.color_bg);
				rg_gui_push_rect(&gui,
				                 rg_gui_make_rect(0.0f, 0.0f, native_bounds.w, 42.0f),
				                 rg_gui_color(0.055f, 0.075f, 0.11f, 1.0f));
				rg_gui_label_static(&gui, "Native tear-out - drag a tab back to the main window",
				                    rg_gui_make_rect(14.0f, 8.0f, native_bounds.w - 28.0f, 26.0f));
				RgGuiRect dock_rect = rg_gui_make_rect(10.0f, 52.0f,
				                                       native_bounds.w - 20.0f,
				                                       native_bounds.h - 62.0f);
				if (dock_rect.w < 0.0f) dock_rect.w = 0.0f;
				if (dock_rect.h < 0.0f) dock_rect.h = 0.0f;
				rg_gui_push_rect(&gui, dock_rect, gui.style.color_panel);
				rg_gui_push_rect_outline(&gui, dock_rect, gui.style.color_border,
				                         gui.style.border_thickness);
				rg_gui_dockspace_begin(&gui, &state.native_dockspace, dock_rect,
				                       tearout_native_dockspace_id());
				if (state.dock_request.pending &&
				    state.dock_request.panel < TEAROUT_PANEL_COUNT)
				{
					u32 panel = state.dock_request.panel;
					rg_gui_dockspace_dock(&gui, &state.native_dockspace,
					                      &state.panels[panel], tearout_panel_titles[panel],
					                      tearout_panel_id(panel), RG_GUI_DOCK_SLOT_CENTER);
					state.panel_host[panel] = 1;
					state.dock_request.pending = 0;
				}
				rg_gui_dockspace_end(&gui, &state.native_dockspace);

				tearout_update_hosts(&state);
				if (state.return_all)
				{
					for (u32 panel = 0u; panel < TEAROUT_PANEL_COUNT; panel++)
					{
						if (state.panel_host[panel] != 1) continue;
						rg_gui_dockspace_dock(&gui, &state.main_dockspace,
						                      &state.panels[panel], tearout_panel_titles[panel],
						                      tearout_panel_id(panel), RG_GUI_DOCK_SLOT_CENTER);
						state.panel_host[panel] = 0;
					}
					state.return_all = 0;
					SDL_strlcpy(state.status, "All panels returned to the main dockspace.",
					            sizeof(state.status));
				}
				tearout_update_hosts(&state);
				tearout_draw_hosted_panels(&gui, &state, 1,
				                           rg_vec2(state.native.origin.x,
				                                   state.native.origin.y));
				rg_gui_viewport_end(&gui, viewport);
			}
		}

		tearout_update_hosts(&state);
		tearout_draw_hosted_panels(&gui, &state, 0,
		                           rg_vec2(main_origin.x, main_origin.y));
		rg_gui_end_frame(&gui);
		built_native_viewport = tearout_find_viewport(&gui, native_viewport);

		const RgGuiPlatformOutput* main_platform = rg_gui_platform_output(&gui);
		const RgGuiPlatformOutput* native_platform = built_native_viewport
		                                                 ? rg_gui_viewport_platform_output(built_native_viewport)
		                                                 : NULL;
		const RgGuiPlatformOutput* cursor_platform = NULL;
		if (mouse_focus == main_window) cursor_platform = main_platform;
		else if (state.native.window && mouse_focus == state.native.window)
			cursor_platform = native_platform;
		if (!demo_platform_apply_cursor(&platform_state, cursor_platform)) goto cleanup;
		if (state.native.window && keyboard_focus == state.native.window && native_platform)
		{
			if (!demo_platform_update_text_input(&platform_state, &input,
			                                     state.native.window, native_platform,
			                                     state.native.pixel_w, state.native.pixel_h))
				goto cleanup;
		}
		else if (keyboard_focus == main_window)
		{
			if (!demo_platform_update_text_input(&platform_state, &input, main_window,
			                                     main_platform, main_w, main_h))
				goto cleanup;
		}
		else
		{
			if (!demo_platform_update_text_input(&platform_state, &input, NULL, NULL, 0, 0))
				goto cleanup;
		}

		int main_presented = 0;
		if (!demo_render_frame(device, main_window, &gpu, &text_renderer,
		                       &upload_ring, &gui, &main_presented))
			goto cleanup;
		main_presented_frames += main_presented;
		if (state.native.window && built_native_viewport)
		{
			int native_presented = 0;
			if (!demo_render_draw_list(device, state.native.window, &gpu, &text_renderer,
			                           &upload_ring,
			                           rg_gui_viewport_draw_list(built_native_viewport),
			                           rg_gui_viewport_overlay_start(built_native_viewport),
			                           &native_presented))
				goto cleanup;
			native_presented_frames += native_presented;
		}

		if (global_left_released && state.drag.active && state.drag.window)
		{
			int inside = tearout_point_in_window(global_x, global_y, &main_origin,
			                                     main_w, main_h);
			if (state.native.window)
				inside |= tearout_point_in_window(global_x, global_y,
				                                  &state.native.origin,
				                                  state.native.pixel_w,
				                                  state.native.pixel_h);
			if (!inside)
			{
				int width = (int)state.drag.global_rect.w + 40;
				int height = (int)state.drag.global_rect.h + 80;
				int pos_x = (int)(global_x - (f32)width * 0.5f);
				int pos_y = (int)(global_y - 32.0f);
				if (!tearout_open_for_panel(device, main_window, &state, state.drag.panel,
				                            hidden, pos_x, pos_y, width, height))
					SDL_snprintf(state.status, sizeof(state.status),
					             "Could not create tear-out: %s", SDL_GetError());
			}
			state.drag.active = 0;
			state.drag.window = NULL;
			state.drag.title = NULL;
		}

		if (state.start_torn_out)
		{
			state.start_torn_out = 0;
			state.spawn_panel = TEAROUT_PANEL_INSPECTOR;
		}
		if (state.spawn_panel >= 0)
		{
			int main_x = 0;
			int main_y = 0;
			if (!SDL_GetWindowPosition(main_window, &main_x, &main_y)) goto cleanup;
			if (!tearout_open_for_panel(device, main_window, &state,
			                            (u32)state.spawn_panel, hidden,
			                            main_x + main_w + 20, main_y + 40, 520, 460))
				SDL_snprintf(state.status, sizeof(state.status),
				             "Could not create tear-out: %s", SDL_GetError());
			state.spawn_panel = -1;
		}

		int hosted_panels = 0;
		for (u32 panel = 0u; panel < TEAROUT_PANEL_COUNT; panel++)
			hosted_panels += state.panel_host[panel] == 1;
		if (state.native.window &&
		    ((state.close_after_return && !state.return_all) ||
		     (hosted_panels == 0 && !state.dock_request.pending && !state.drag.active)))
		{
			tearout_destroy_native(device, &gui, &input, &platform_state, &state);
			state.close_after_return = 0;
		}

		submitted_frames++;
		if (smoke_test && main_presented_frames >= frame_limit &&
		    native_presented_frames >= 1 && state.native_created_count >= 1)
			running = 0;
		else if (smoke_test && submitted_frames >= 180)
		{
			SDL_SetError("Tear-out smoke test did not present both native swapchains in 180 attempts");
			goto cleanup;
		}
		else if (!smoke_test && frame_limit > 0 && submitted_frames >= frame_limit)
			running = 0;
	}

	if (smoke_test && (main_presented_frames < frame_limit ||
	                   native_presented_frames < 1 || state.native_created_count < 1))
	{
		SDL_SetError("Tear-out smoke test ended before both swapchains were presented");
		goto cleanup;
	}
	printf("rg_gui tear-out demo completed %d submitted frame%s "
	       "(%d main and %d tear-out presentations; %d native window%s created)\n",
	       submitted_frames, submitted_frames == 1 ? "" : "s",
	       main_presented_frames, native_presented_frames,
	       state.native_created_count, state.native_created_count == 1 ? "" : "s");
	result = 0;

cleanup:
	if (result != 0) fprintf(stderr, "rg_gui tear-out demo failed: %s\n", SDL_GetError());
	if (device) rg_gpu_wait_idle(device);
	if (state.native.window)
		tearout_destroy_native(device, &gui, &input, &platform_state, &state);
	rg_gpu_upload_ring_destroy(&upload_ring);
	rg_gui_gpu_destroy(&gpu);
	free(text_memory);
	free(gui_memory);
	if (atlas) SDL_ReleaseGPUTexture(device, atlas);
	demo_font_destroy(&demo_font);
	demo_platform_destroy(&platform_state, &input);
	if (main_claimed) SDL_ReleaseWindowFromGPUDevice(device, main_window);
	if (device) rg_gpu_device_destroy(device);
	if (main_window) SDL_DestroyWindow(main_window);
	SDL_Quit();
	return result;
}
