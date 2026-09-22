// Real SDL/GPU lifecycle coverage for the demo's one-window tear-out cache.
// The visible windows are intentional: hiding/restoring a maximized or minimized
// native window behaves differently from operating on an always-hidden window.
#define main rg_gui_demo_tearout_main
#include "../examples/rg_gui_demo_tearout.c"
#undef main

#ifdef SDL_PLATFORM_WINDOWS
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#endif

#define TEAROUT_CHECK(condition)                                                \
	do                                                                         \
	{                                                                          \
		if (!(condition))                                                       \
		{                                                                      \
			fprintf(stderr, "tear-out lifecycle line %d: %s (%s)\n",             \
			        __LINE__, #condition, SDL_GetError());                        \
			goto cleanup;                                                       \
		}                                                                      \
		checks++;                                                               \
	} while (0)

static int lifecycle_geometry(SDL_Window* window, int x, int y, int width, int height)
{
	int actual_x = 0;
	int actual_y = 0;
	int actual_width = 0;
	int actual_height = 0;
	if (!SDL_SyncWindow(window) ||
	    !SDL_GetWindowPosition(window, &actual_x, &actual_y) ||
	    !SDL_GetWindowSize(window, &actual_width, &actual_height)) return 0;
	if (actual_x != x || actual_y != y || actual_width != width || actual_height != height)
	{
		SDL_SetError("Expected geometry %d,%d %dx%d; got %d,%d %dx%d",
		             x, y, width, height, actual_x, actual_y, actual_width, actual_height);
		return 0;
	}
	return (SDL_GetWindowFlags(window) &
	        (SDL_WINDOW_HIDDEN | SDL_WINDOW_MAXIMIZED | SDL_WINDOW_MINIMIZED)) == 0u;
}

static int lifecycle_submit_clear(SDL_GPUDevice* device, SDL_Window* window)
{
	SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(device);
	if (!command) return 0;
	SDL_GPUTexture* swapchain = NULL;
	if (!SDL_WaitAndAcquireGPUSwapchainTexture(command, window, &swapchain, NULL, NULL))
	{
		SDL_CancelGPUCommandBuffer(command);
		return 0;
	}
	if (!swapchain)
	{
		SDL_SubmitGPUCommandBuffer(command);
		SDL_SetError("Visible lifecycle test window did not acquire a swapchain");
		return 0;
	}
	SDL_GPUColorTargetInfo target = {0};
	target.texture = swapchain;
	target.clear_color = (SDL_FColor){0.06f, 0.08f, 0.12f, 1.0f};
	target.load_op = SDL_GPU_LOADOP_CLEAR;
	target.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command, &target, 1u, NULL);
	if (!pass)
	{
		SDL_SubmitGPUCommandBuffer(command);
		return 0;
	}
	SDL_EndGPURenderPass(pass);
	return SDL_SubmitGPUCommandBuffer(command) ? 1 : 0;
}

static int lifecycle_register_viewport(RgGuiContext* gui, RgInputState* input)
{
	rg_gui_begin_frame(gui, input, 1.0f / 60.0f);
	RgGuiViewport* viewport = rg_gui_viewport_begin(
	    gui, tearout_native_viewport_id(), rg_gui_make_rect(0, 0, 480, 360), input);
	if (!viewport)
	{
		rg_gui_end_frame(gui);
		return 0;
	}
	rg_gui_push_rect(gui, rg_gui_make_rect(0, 0, 20, 20), rg_gui_color(1, 1, 1, 1));
	rg_gui_viewport_end(gui, viewport);
	rg_gui_end_frame(gui);
	return tearout_find_viewport(gui, tearout_native_viewport_id()) != NULL;
}

static int lifecycle_text_input(DemoPlatformState* platform, RgInputState* input,
                                 SDL_Window* window)
{
	if (!demo_platform_set_text_input(input, window, 1)) return 0;
	SDL_Rect caret = {20, 30, 1, 18};
	if (!SDL_SetTextInputArea(window, &caret, 0)) return 0;
	platform->text_input_window = window;
	platform->ime_area_valid = 1;
	return SDL_TextInputActive(window) && input->text_input_active;
}

#ifdef SDL_PLATFORM_WINDOWS
static int lifecycle_native_close(SDL_Window* window)
{
	HWND handle = (HWND)SDL_GetPointerProperty(SDL_GetWindowProperties(window),
	                                           SDL_PROP_WINDOW_WIN32_HWND_POINTER, NULL);
	if (!handle || !PostMessageW(handle, WM_CLOSE, 0, 0)) return 0;
	SDL_WindowID id = SDL_GetWindowID(window);
	u64 deadline = SDL_GetTicksNS() + 1000000000ull;
	int received = 0;
	do
	{
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT)
			{
				SDL_SetError("A parked-window close generated an application quit");
				return 0;
			}
			if (event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED && event.window.windowID == id)
				received = 1;
		}
		if (received) return 1;
		SDL_Delay(1);
	} while (SDL_GetTicksNS() < deadline);
	SDL_SetError("Native close request did not reach the SDL event queue");
	return 0;
}
#endif

int main(void)
{
	int result = 1;
	int checks = 0;
	int main_claimed = 0;
	SDL_Window* main_window = NULL;
	SDL_GPUDevice* device = NULL;
	void* gui_memory = NULL;
	RgGuiContext gui = {0};
	RgInputState input = {0};
	DemoPlatformState platform = {0};
	TearoutDemoState state;
	RgTextGlyph glyph = {0};
	RgTextFont font = {0};
	SDL_WindowID cached_id = 0u;
	SDL_WindowID extra_id = 0u;
	SDL_WindowID main_id = 0u;
	tearout_state_init(&state);
	rg_input_init(&input);

	TEAROUT_CHECK(SDL_Init(SDL_INIT_VIDEO));
	TEAROUT_CHECK(tearout_configure_window_events());
	main_window = SDL_CreateWindow("rg_gui tear-out lifecycle test", 760, 520,
	                               SDL_WINDOW_RESIZABLE);
	TEAROUT_CHECK(main_window != NULL);
	main_id = SDL_GetWindowID(main_window);
	TEAROUT_CHECK(main_id != 0u);
	RgGpuDeviceDesc device_desc = {0};
	device_desc.shader_formats = RG_GPU_DEFAULT_SHADER_FORMATS;
	device_desc.enable_debug = 1;
	device = rg_gpu_device_create(&device_desc);
	TEAROUT_CHECK(device != NULL);
	TEAROUT_CHECK(rg_gpu_claim_window(device, main_window));
	main_claimed = 1;

	font.metrics.atlas_width = 1u;
	font.metrics.atlas_height = 1u;
	font.metrics.line_height = 16;
	font.glyphs = &glyph;
	font.glyph_count = 1u;
	font.glyph_capacity = 1u;
	font.fallback_codepoint = '?';
	glyph.codepoint = '?';
	glyph.w = 1;
	glyph.h = 1;
	glyph.x_advance = 8;
	RgGuiInitDesc gui_desc = {0};
	gui_desc.font = &font;
	gui_desc.max_draw_cmds = 32u;
	gui_desc.text_buffer_size = KB(1);
	size_t gui_size = rg_gui_memory_required(&gui_desc);
	TEAROUT_CHECK(gui_size != 0u);
	gui_memory = malloc(gui_size);
	TEAROUT_CHECK(gui_memory != NULL);
	RgArena gui_arena = {(char*)gui_memory, gui_size, 0u, gui_size};
	TEAROUT_CHECK(rg_gui_init(&gui, &gui_arena, &gui_desc));

	SDL_strlcpy(state.object_name, "Edited camera", sizeof(state.object_name));
	SDL_strlcpy(state.console_text, "Persistent user edits\n", sizeof(state.console_text));
	state.exposure = 2.75f;
	state.panels[TEAROUT_PANEL_INSPECTOR].collapsed = 1;
	TEAROUT_CHECK(!tearout_open_for_panel(device, main_window, &state,
	                                     TEAROUT_PANEL_COUNT, 0, 100, 90, 480, 360));
	TEAROUT_CHECK(!state.native.window && !state.parked.window &&
	              state.native_created_count == 0 && !state.dock_request.pending);
	TEAROUT_CHECK(tearout_open_for_panel(device, main_window, &state,
	                                    TEAROUT_PANEL_INSPECTOR, 0, 100, 90, 480, 360));
	cached_id = state.native.window_id;
	TEAROUT_CHECK(cached_id != 0u && state.native.claimed && !state.parked.window);
	TEAROUT_CHECK(state.native_created_count == 1 && state.native_reused_count == 0);
	TEAROUT_CHECK(lifecycle_geometry(state.native.window, 100, 90, 480, 360));
	TEAROUT_CHECK(lifecycle_register_viewport(&gui, &input));
	TEAROUT_CHECK(lifecycle_text_input(&platform, &input, state.native.window));
	TEAROUT_CHECK(lifecycle_submit_clear(device, main_window));
	TEAROUT_CHECK(lifecycle_submit_clear(device, state.native.window));

	TEAROUT_CHECK(tearout_park_native(&gui, &input, &platform, &state));
	TEAROUT_CHECK(!state.native.window && state.native.window_id == 0u && !state.native.claimed);
	TEAROUT_CHECK(state.parked.window && state.parked.window_id == cached_id && state.parked.claimed);
	TEAROUT_CHECK(SDL_GetWindowFromID(cached_id) == state.parked.window);
	TEAROUT_CHECK((SDL_GetWindowFlags(state.parked.window) & SDL_WINDOW_HIDDEN) != 0u);
	TEAROUT_CHECK(!tearout_find_viewport(&gui, tearout_native_viewport_id()));
	TEAROUT_CHECK(!platform.text_input_window && !platform.ime_area_valid &&
	              !input.text_input_active && !SDL_TextInputActive(state.parked.window));
	TEAROUT_CHECK(!state.dock_request.pending && state.native_parked_count == 1 &&
	              state.native_destroyed_count == 0);
	TEAROUT_CHECK(tearout_park_native(&gui, &input, &platform, &state));
	TEAROUT_CHECK(state.native_parked_count == 1 && state.parked.window_id == cached_id);
#ifdef SDL_PLATFORM_WINDOWS
	// Real WM_CLOSE exercises SDL's automatic quit inference; SDL_PushEvent
	// would bypass it. Repeated closes of the parked window must remain local.
	TEAROUT_CHECK(lifecycle_native_close(state.parked.window));
	TEAROUT_CHECK(lifecycle_native_close(state.parked.window));
	TEAROUT_CHECK(!state.native.window && SDL_GetWindowFromID(cached_id) == state.parked.window);
#endif
	TEAROUT_CHECK(!tearout_open_for_panel(device, main_window, &state,
	                                     TEAROUT_PANEL_COUNT, 0, 220, 170, 650, 500));
	TEAROUT_CHECK(!state.native.window && state.parked.window_id == cached_id);

	TEAROUT_CHECK(tearout_open_for_panel(device, main_window, &state,
	                                    TEAROUT_PANEL_INSPECTOR, 0, 220, 170, 650, 500));
	TEAROUT_CHECK(state.native.window_id == cached_id && !state.parked.window &&
	              state.native_created_count == 1 && state.native_reused_count == 1);
	TEAROUT_CHECK(lifecycle_geometry(state.native.window, 220, 170, 650, 500));
	TEAROUT_CHECK(lifecycle_register_viewport(&gui, &input));
	TEAROUT_CHECK(lifecycle_submit_clear(device, state.native.window));

	// Settle native OS transitions before checking flags and requested geometry.
	// A hidden restore followed by size/position changes can silently lose those
	// changes on Windows if the native window is still maximized when they run.
	TEAROUT_CHECK(SDL_SetWindowSize(state.native.window, 540, 400));
	TEAROUT_CHECK(SDL_MaximizeWindow(state.native.window));
	TEAROUT_CHECK(SDL_SyncWindow(state.native.window));
	TEAROUT_CHECK((SDL_GetWindowFlags(state.native.window) & SDL_WINDOW_MAXIMIZED) != 0u);
	TEAROUT_CHECK(tearout_park_native(&gui, &input, &platform, &state));
	TEAROUT_CHECK(tearout_open_for_panel(device, main_window, &state,
	                                    TEAROUT_PANEL_INSPECTOR, 0, 240, 180, 650, 500));
	TEAROUT_CHECK(state.native.window_id == cached_id && !state.parked.window);
	TEAROUT_CHECK(lifecycle_geometry(state.native.window, 240, 180, 650, 500));
	TEAROUT_CHECK(lifecycle_submit_clear(device, state.native.window));

	TEAROUT_CHECK(SDL_SetWindowSize(state.native.window, 560, 420));
	TEAROUT_CHECK(SDL_MinimizeWindow(state.native.window));
	TEAROUT_CHECK(SDL_SyncWindow(state.native.window));
	TEAROUT_CHECK((SDL_GetWindowFlags(state.native.window) & SDL_WINDOW_MINIMIZED) != 0u);
	TEAROUT_CHECK(tearout_park_native(&gui, &input, &platform, &state));
	TEAROUT_CHECK(tearout_open_for_panel(device, main_window, &state,
	                                    TEAROUT_PANEL_INSPECTOR, 0, 260, 190, 650, 500));
	TEAROUT_CHECK(state.native.window_id == cached_id && !state.parked.window);
	TEAROUT_CHECK(lifecycle_geometry(state.native.window, 260, 190, 650, 500));
	TEAROUT_CHECK(lifecycle_submit_clear(device, state.native.window));

	TEAROUT_CHECK(tearout_park_native(&gui, &input, &platform, &state));
	TEAROUT_CHECK(tearout_open_for_panel(device, main_window, &state,
	                                    TEAROUT_PANEL_CONSOLE, 1, 280, 200, 600, 440));
	TEAROUT_CHECK(state.native.window_id == cached_id && !state.parked.window);
	TEAROUT_CHECK((SDL_GetWindowFlags(state.native.window) & SDL_WINDOW_HIDDEN) != 0u);
	TEAROUT_CHECK(state.dock_request.pending && state.dock_request.panel == TEAROUT_PANEL_CONSOLE);
	TEAROUT_CHECK(state.panel_host[TEAROUT_PANEL_INSPECTOR] == 1 &&
	              state.panel_host[TEAROUT_PANEL_CONSOLE] == 1);
	TEAROUT_CHECK(strcmp(state.object_name, "Edited camera") == 0 &&
	              strcmp(state.console_text, "Persistent user edits\n") == 0 &&
	              state.exposure == 2.75f && state.panels[TEAROUT_PANEL_INSPECTOR].collapsed == 1);
	TEAROUT_CHECK(state.native_created_count == 1 && state.native_reused_count == 4 &&
	              state.native_parked_count == 4 && state.native_destroyed_count == 0);
	TEAROUT_CHECK(tearout_park_native(&gui, &input, &platform, &state));

	// Construct an occupied-cache error deliberately. The guard must preserve
	// both independently owned windows, then shutdown must destroy each once.
	state.native.window = tearout_create_native(device, main_window, 0, 0, 480, 360,
	                                             &state.native.window_id, &state.native.claimed);
	TEAROUT_CHECK(state.native.window != NULL);
	state.native_created_count++;
	extra_id = state.native.window_id;
	TEAROUT_CHECK(extra_id != cached_id && SDL_SyncWindow(state.native.window));
	TEAROUT_CHECK(lifecycle_register_viewport(&gui, &input));
	TEAROUT_CHECK(lifecycle_text_input(&platform, &input, state.native.window));
	TEAROUT_CHECK(lifecycle_submit_clear(device, state.native.window));
	TEAROUT_CHECK(!tearout_park_native(&gui, &input, &platform, &state));
	TEAROUT_CHECK(state.native.window_id == extra_id && state.parked.window_id == cached_id &&
	              state.native_parked_count == 5 && state.native_destroyed_count == 0);
	TEAROUT_CHECK(platform.text_input_window == state.native.window && input.text_input_active);
	TEAROUT_CHECK(tearout_find_viewport(&gui, tearout_native_viewport_id()) != NULL);
	TEAROUT_CHECK(SDL_GetWindowFromID(cached_id) == state.parked.window &&
	              SDL_GetWindowFromID(extra_id) == state.native.window);
	TEAROUT_CHECK(SDL_WaitForGPUIdle(device));
	tearout_destroy_native(device, &gui, &input, &platform, &state);
	tearout_destroy_window(device, &state.parked, &state.native_destroyed_count);
	TEAROUT_CHECK(!state.native.window && !state.parked.window && state.native_destroyed_count == 2);
	TEAROUT_CHECK(SDL_GetWindowFromID(cached_id) == NULL && SDL_GetWindowFromID(extra_id) == NULL);
	TEAROUT_CHECK(!platform.text_input_window && !input.text_input_active &&
	              !tearout_find_viewport(&gui, tearout_native_viewport_id()));
	tearout_destroy_native(device, &gui, &input, &platform, &state);
	tearout_destroy_window(device, &state.parked, &state.native_destroyed_count);
	TEAROUT_CHECK(state.native_destroyed_count == 2);
	SDL_ReleaseWindowFromGPUDevice(device, main_window);
	main_claimed = 0;
	SDL_DestroyWindow(main_window);
	main_window = NULL;
	TEAROUT_CHECK(SDL_GetWindowFromID(main_id) == NULL);
	result = 0;
	printf("tear-out lifecycle: %d checks passed (visible restore, cache ownership, IME, viewport, GPU cleanup)\n",
	       checks);

cleanup:
	if (device) SDL_WaitForGPUIdle(device);
	tearout_destroy_native(device, &gui, &input, &platform, &state);
	tearout_destroy_window(device, &state.parked, &state.native_destroyed_count);
	free(gui_memory);
	if (main_claimed) SDL_ReleaseWindowFromGPUDevice(device, main_window);
	if (device) SDL_DestroyGPUDevice(device);
	if (main_window) SDL_DestroyWindow(main_window);
	SDL_Quit();
	return result;
}
