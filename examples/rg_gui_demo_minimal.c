// Minimal interactive rg_gui example with an end-to-end SDL3 GPU frame loop.

#include "rg_gui_demo_common.h"

static void demo_build_ui(RgGuiContext* gui, int width, int height,
                          RgGuiWindowState* window_state, u32* active_tab,
                          int* enabled, f32* intensity, char* name,
                          size_t name_capacity, int* click_count, f32 progress)
{
	RgGuiRect bounds = rg_gui_make_rect(0.0f, 0.0f, (f32)width, (f32)height);
	rg_gui_window_set_bounds(gui, bounds);

	RgGuiRect banner = rg_gui_make_rect(0.0f, 0.0f, (f32)width, 52.0f);
	rg_gui_push_rect(gui, banner, rg_gui_color(0.055f, 0.075f, 0.11f, 1.0f));
	rg_gui_push_rect(gui, rg_gui_make_rect(0.0f, 50.0f, (f32)width, 2.0f),
	                 gui->style.color_accent);
	rg_gui_label_static(gui, "rg_gui / SDL3 GPU demo",
	                    rg_gui_make_rect(18.0f, 8.0f, 360.0f, 36.0f));

	RgGuiRect initial = rg_gui_make_rect(70.0f, 88.0f, 500.0f, 390.0f);
	u32 flags = RG_GUI_WINDOW_MOVABLE | RG_GUI_WINDOW_RESIZABLE |
	            RG_GUI_WINDOW_COLLAPSIBLE | RG_GUI_WINDOW_SNAP;
	if (!rg_gui_window_begin(gui, window_state, "Controls", initial,
	                         380.0f, 300.0f, 9.0f, flags,
	                         rg_gui_id_str("demo_window")))
		return;

	static const char* tab_labels[] = {"Widgets", "Renderer"};
	rg_gui_tabs(gui, tab_labels, (u32)RG_ARRAY_COUNT(tab_labels), active_tab,
	            rg_gui_layout_next(gui, 28.0f), rg_gui_id_str("demo_tabs"));

	if (*active_tab == 0u)
	{
		rg_gui_checkbox_static(gui, "Enable feature", enabled,
		                       rg_gui_layout_next(gui, 26.0f),
		                       rg_gui_id_str("enabled"));
		rg_gui_slider_float(gui, "Intensity", intensity, 0.0f, 1.0f,
		                    rg_gui_layout_next(gui, 28.0f),
		                    rg_gui_id_str("intensity"));
		int submitted = 0;
		rg_gui_text_input(gui, "Name", name, name_capacity,
		                  rg_gui_layout_next(gui, 28.0f),
		                  rg_gui_id_str("name"), &submitted);
		RG_GUI_UNUSED(submitted);
		rg_gui_progress_bar(gui, "Animated sample", progress, 0.0f, 1.0f,
		                    rg_gui_layout_next(gui, 24.0f));

		rg_gui_layout_row_begin(gui, 30.0f, 9.0f);
		if (rg_gui_button_static(gui, "Count click", rg_gui_layout_row_next(gui, 150.0f),
		                         rg_gui_id_str("count")))
			(*click_count)++;
		if (rg_gui_button_static(gui, "Reset", rg_gui_layout_row_next(gui, 110.0f),
		                         rg_gui_id_str("reset")))
		{
			*enabled = 1;
			*intensity = 0.65f;
			*click_count = 0;
			SDL_strlcpy(name, "Reverse Gravity", name_capacity);
		}
		rg_gui_layout_row_end(gui);

		char status[96];
		rg_snprintf(status, sizeof(status), "Clicks: %d / Hello, %s", *click_count, name);
		rg_gui_label(gui, status, rg_gui_layout_next(gui, 28.0f));
	}
	else
	{
		rg_gui_label_static(gui, "Immediate UI to cached text pages",
		                    rg_gui_layout_next(gui, 28.0f));
		rg_gui_label_static(gui, "Compute expansion plus ordered draw items",
		                    rg_gui_layout_next(gui, 28.0f));
		rg_gui_label_static(gui, "DXIL / SPIR-V / MSL shader outputs",
		                    rg_gui_layout_next(gui, 28.0f));
		rg_gui_progress_bar(gui, "Text cache warm-up", progress, 0.0f, 1.0f,
		                    rg_gui_layout_next(gui, 24.0f));
	}

	rg_gui_window_end(gui, window_state);
}

int main(int argc, char** argv)
{
	int result = 1;
	int hidden = 0;
	int frame_limit = 0;
	int smoke_test = 0;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--hidden") == 0)
			hidden = 1;
		else if (strcmp(argv[i], "--smoke-test") == 0)
			smoke_test = 1;
		else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
		{
			frame_limit = atoi(argv[++i]);
			if (frame_limit < 1)
				frame_limit = 1;
		}
	}
	if (smoke_test)
	{
		hidden = 0;
		frame_limit = 3;
	}

	SDL_Window* window = NULL;
	SDL_GPUDevice* device = NULL;
	SDL_GPUTexture* atlas = NULL;
	RgGuiGpuRenderer gpu = {0};
	RgGpuUploadRing upload_ring = {0};
	DemoFontAssets demo_font = {0};
	DemoPlatformState platform_state = {0};
	RgInputState input = {0};
	void* gui_memory = NULL;
	void* text_memory = NULL;
	int window_claimed = 0;

	if (!SDL_Init(SDL_INIT_VIDEO))
		goto cleanup;
	demo_platform_init(&platform_state);
	SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
	if (hidden)
		window_flags |= SDL_WINDOW_HIDDEN;
	window = SDL_CreateWindow("rg_gui demo", 900, 620, window_flags);
	if (!window)
		goto cleanup;

	RgGpuDeviceDesc device_desc = {0};
	device_desc.shader_formats = RG_GPU_DEFAULT_SHADER_FORMATS;
	device_desc.enable_debug = RG_GUI_DEMO_GPU_DEBUG;
	device = rg_gpu_device_create(&device_desc);
	if (!device || !rg_gpu_claim_window(device, window))
		goto cleanup;
	window_claimed = 1;

	if (!demo_font_load(&demo_font))
		goto cleanup;
	atlas = demo_atlas_create(device, demo_font.pixels,
	                          demo_font.atlas_width, demo_font.atlas_height);
	if (!atlas)
		goto cleanup;

	RgGuiContext gui;
	RgGuiInitDesc gui_desc = {0};
	gui_desc.font = &demo_font.font;
	gui_desc.text_lookup = &demo_font.text_lookup;
	gui_desc.max_draw_cmds = 4096u;
	gui_desc.text_buffer_size = KB(64);
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
	limits.max_frame_runs = 512u;
	RgGuiRendererInitDesc text_desc = {0};
	text_desc.font = &demo_font.font;
	text_desc.limits = limits;
	text_desc.text_lookup = &demo_font.text_lookup;
	size_t text_memory_size = rg_gui_renderer_memory_required_ex(&text_desc);
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
	if (!rg_gui_renderer_init(&text_renderer, &text_arena, &text_desc))
	{
		SDL_SetError("Text-renderer initialization rejected the demo arena or limits");
		goto cleanup;
	}

	char shader_root[1024];
	if (!demo_shader_root(shader_root, sizeof(shader_root)))
		goto cleanup;
	RgGuiGpuDesc gpu_desc = {0};
	gpu_desc.device = device;
	gpu_desc.target_format = rg_gpu_swapchain_format(device, window);
	gpu_desc.shader_root = shader_root;
	gpu_desc.atlas_texture = atlas;
	gpu_desc.atlas_width = demo_font.atlas_width;
	gpu_desc.atlas_height = demo_font.atlas_height;
	gpu_desc.max_cached_quads = limits.max_cached_quads;
	gpu_desc.max_runs = limits.max_frame_runs;
	gpu_desc.max_text_instances = limits.max_frame_instances;
	gpu_desc.max_geometry_vertices = 16384u;
	gpu_desc.max_items = 2048u;
	gpu_desc.frame_buffer_count = 2u;
	gpu_desc.min_filter = SDL_GPU_FILTER_LINEAR;
	gpu_desc.mag_filter = SDL_GPU_FILTER_LINEAR;
	if (!rg_gui_gpu_create(&gpu, &gpu_desc))
		goto cleanup;
	if (!rg_gpu_upload_ring_init(&upload_ring, device,
	                            rg_gui_gpu_upload_ring_size_required(&gpu)))
		goto cleanup;

	rg_input_init(&input);
	RgInputEvent input_event_storage[256];
	char input_event_text[KB(8)];
	RgInputEventQueue input_events;
	rg_input_event_queue_init(&input_events, input_event_storage,
	                          RG_ARRAY_COUNT(input_event_storage),
	                          input_event_text, sizeof(input_event_text));
	SDL_WindowID window_id = SDL_GetWindowID(window);
	RgGuiWindowState window_state = {0};
	u32 active_tab = 0u;
	int enabled = 1;
	f32 intensity = 0.65f;
	char name[64] = "Reverse Gravity";
	int click_count = 0;
	f32 progress = 0.0f;
	u64 previous_ticks = SDL_GetTicksNS();
	int running = 1;
	int submitted_frames = 0;
	int presented_frames = 0;

	while (running)
	{
		rg_input_begin_frame(&input);
		rg_input_event_queue_reset(&input_events, SDL_GetModState());
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
				running = 0;
			demo_platform_process_event(&platform_state, &event);
			rg_input_process_event_ex(&input, &event, &input_events);
		}
		rg_input_sample(&input);
		if (rg_input_is_key_pressed(&input, SDL_SCANCODE_ESCAPE))
			running = 0;
		if (!running)
			break;

		u64 ticks = SDL_GetTicksNS();
		f32 delta_time = (f32)((f64)(ticks - previous_ticks) / 1000000000.0);
		previous_ticks = ticks;
		if (delta_time > 0.1f)
			delta_time = 0.1f;
		progress += delta_time * 0.25f;
		if (progress > 1.0f)
			progress -= 1.0f;

		int width = 0;
		int height = 0;
		if (!SDL_GetWindowSizeInPixels(window, &width, &height))
			goto cleanup;
		rg_gui_begin_frame_ex(&gui, &input, &input_events, window_id, delta_time);
		demo_build_ui(&gui, width, height, &window_state, &active_tab,
		              &enabled, &intensity, name, sizeof(name), &click_count, progress);
		rg_gui_end_frame(&gui);

		const RgGuiPlatformOutput* platform = rg_gui_platform_output(&gui);
		if (!demo_platform_apply_cursor(&platform_state, platform) ||
		    !demo_platform_update_text_input(&platform_state, &input, window, platform,
		                                     width, height))
			goto cleanup;

		int presented = 0;
		if (!demo_render_frame(device, window, &gpu, &text_renderer, &upload_ring, &gui,
		                       &presented))
			goto cleanup;
		submitted_frames++;
		presented_frames += presented;
		if (smoke_test && presented_frames >= frame_limit)
			running = 0;
		else if (smoke_test && submitted_frames >= 120)
		{
			SDL_SetError("Demo smoke test could not present three swapchain frames in 120 attempts");
			goto cleanup;
		}
		else if (!smoke_test && frame_limit > 0 && submitted_frames >= frame_limit)
			running = 0;
	}

	if (smoke_test && presented_frames < frame_limit)
	{
		SDL_SetError("Demo smoke test ended before presenting three frames");
		goto cleanup;
	}
	printf("rg_gui demo completed %d submitted frame%s (%d presented)\n",
	       submitted_frames, submitted_frames == 1 ? "" : "s", presented_frames);
	result = 0;

cleanup:
	if (result != 0)
		fprintf(stderr, "rg_gui demo failed: %s\n", SDL_GetError());
	if (device)
		rg_gpu_wait_idle(device);
	rg_gpu_upload_ring_destroy(&upload_ring);
	rg_gui_gpu_destroy(&gpu);
	free(text_memory);
	free(gui_memory);
	if (atlas)
		SDL_ReleaseGPUTexture(device, atlas);
	demo_font_destroy(&demo_font);
	demo_platform_destroy(&platform_state, &input);
	if (window_claimed)
		SDL_ReleaseWindowFromGPUDevice(device, window);
	if (device)
		rg_gpu_device_destroy(device);
	if (window)
		SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
