// rg_gui_sdl - SDL3 platform glue for rg_gui
//
// Part of the Reverse Gravity (rg_) libraries.
// rg_gui.h itself has no SDL dependency: it reads rg_input_core state and asks
// the host for clipboard access through RgGuiPlatformServices. This header is
// the SDL3 host side: the clipboard services, the system cursors that mirror
// RgGuiMouseCursor, and the text-input / IME-area bookkeeping that follows
// RgGuiPlatformOutput. It also includes rg_input.h, the SDL3 event adapter.
//
// USAGE:
//   #include "rg_gui_sdl.h"
//
//   RgGuiPlatformServices services = rg_gui_sdl_platform_services();
//   gui_desc.platform = &services;          // copied by rg_gui_init
//
//   RgGuiSdlPlatform platform;
//   rg_gui_sdl_platform_init(&platform);
//   // per event:  rg_gui_sdl_process_event(&platform, &event);
//   // per frame, after rg_gui_end_frame:
//   const RgGuiPlatformOutput* out = rg_gui_platform_output(&gui);
//   rg_gui_sdl_apply_cursor(&platform, out);
//   rg_gui_sdl_update_text_input(&platform, &input, window, out, pixel_w, pixel_h);
//   // at exit: rg_gui_sdl_platform_destroy(&platform, &input);
//
// NOTES:
//   - All functions have internal linkage and work in unity builds.
//   - Functions returning int return 0 on SDL failure; SDL_GetError() says why.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_GUI_SDL_H
#define RG_GUI_SDL_H

#include "rg_input.h"

#ifndef RG_GUI_INCLUDED
#error rg_gui_sdl.h requires rg_gui.h (or rg_gui_gpu.h) to be included first; rg_gui.h is include-once
#endif

#include <SDL3/SDL.h>

// =============================================================================
// CLIPBOARD SERVICES
// =============================================================================

RGINLINE int rg_gui_sdl_clipboard_set(void* user, const char* utf8)
{
	(void)user;
	return SDL_SetClipboardText(utf8) ? 1 : 0;
}

RGINLINE char* rg_gui_sdl_clipboard_get(void* user)
{
	(void)user;
	return SDL_GetClipboardText(); // never NULL; "" when empty (freed below either way)
}

RGINLINE void rg_gui_sdl_clipboard_free(void* user, char* text)
{
	(void)user;
	SDL_free(text);
}

/** The services an SDL3 host passes as RgGuiInitDesc.platform. */
RGINLINE RgGuiPlatformServices rg_gui_sdl_platform_services(void)
{
	RgGuiPlatformServices services;
	memset(&services, 0, sizeof(services));
	services.clipboard_set = rg_gui_sdl_clipboard_set;
	services.clipboard_get = rg_gui_sdl_clipboard_get;
	services.clipboard_free = rg_gui_sdl_clipboard_free;
	return services;
}

// =============================================================================
// CURSORS AND TEXT INPUT
// =============================================================================

#define RG_GUI_SDL_CURSOR_COUNT 5u

typedef struct RgGuiSdlPlatform
{
	SDL_Cursor* cursors[RG_GUI_SDL_CURSOR_COUNT];
	SDL_Window* text_input_window;
	RgGuiMouseCursor applied_cursor;
	RgGuiRect ime_source_rect;
	int ime_pixel_width;
	int ime_pixel_height;
	int cursor_applied;
	int ime_area_valid;
} RgGuiSdlPlatform;

RGINLINE SDL_SystemCursor rg_gui_sdl_system_cursor(RgGuiMouseCursor cursor)
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

/** Create the system cursors. Call after SDL_Init(SDL_INIT_VIDEO). */
RGINLINE void rg_gui_sdl_platform_init(RgGuiSdlPlatform* state)
{
	memset(state, 0, sizeof(*state));
	state->cursors[RG_GUI_CURSOR_DEFAULT] = SDL_GetDefaultCursor();
	for (u32 i = (u32)RG_GUI_CURSOR_TEXT; i < RG_GUI_SDL_CURSOR_COUNT; i++)
	{
		state->cursors[i] = SDL_CreateSystemCursor(rg_gui_sdl_system_cursor((RgGuiMouseCursor)i));
		if (!state->cursors[i]) SDL_ClearError();
	}
}

/** Show the cursor the GUI asked for this frame (no-op when unchanged). */
RGINLINE int rg_gui_sdl_apply_cursor(RgGuiSdlPlatform* state, const RgGuiPlatformOutput* output)
{
	RgGuiMouseCursor cursor = output ? output->cursor : RG_GUI_CURSOR_DEFAULT;
	if ((u32)cursor >= RG_GUI_SDL_CURSOR_COUNT) cursor = RG_GUI_CURSOR_DEFAULT;
	if (state->cursor_applied && state->applied_cursor == cursor) return 1;

	SDL_Cursor* sdl_cursor = state->cursors[cursor];
	if (!sdl_cursor) sdl_cursor = state->cursors[RG_GUI_CURSOR_DEFAULT];
	if (sdl_cursor && !SDL_SetCursor(sdl_cursor)) return 0;
	state->applied_cursor = cursor;
	state->cursor_applied = 1;
	return 1;
}

/** Window geometry changes invalidate the IME area; feed every event here. */
RGINLINE void rg_gui_sdl_process_event(RgGuiSdlPlatform* state, const SDL_Event* event)
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

/** Stop text input on `window` if it is the one currently receiving it (call before destroying a window). */
RGINLINE void rg_gui_sdl_forget_text_window(RgGuiSdlPlatform* state, RgInputState* input, SDL_Window* window)
{
	if (!window || state->text_input_window != window) return;
	SDL_SetTextInputArea(window, NULL, 0);
	if (SDL_StopTextInput(window)) input->text_input_active = false;
	state->text_input_window = NULL;
	state->ime_area_valid = 0;
}

RGINLINE int rg_gui_sdl_set_text_input(RgInputState* input, SDL_Window* window, int enabled)
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

/** Start/stop text input to match `output->wants_text_input` and keep the IME
 *  candidate window beside the caret. `pixel_width/height` are the drawable
 *  size the GUI laid out in (window size when not high-DPI). `window` NULL
 *  stops text input. */
RGINLINE int rg_gui_sdl_update_text_input(RgGuiSdlPlatform* state, RgInputState* input, SDL_Window* window,
                                          const RgGuiPlatformOutput* output, int pixel_width, int pixel_height)
{
	SDL_Window* desired = output && output->wants_text_input ? window : NULL;
	if (state->text_input_window != desired)
	{
		if (state->text_input_window)
		{
			if (!SDL_SetTextInputArea(state->text_input_window, NULL, 0)) return 0;
			state->ime_area_valid = 0;
			if (!rg_gui_sdl_set_text_input(input, state->text_input_window, 0)) return 0;
		}
		state->text_input_window = NULL;
		state->ime_area_valid = 0;
		if (desired)
		{
			if (!rg_gui_sdl_set_text_input(input, desired, 1)) return 0;
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

/** Stop text input, restore the default cursor and free the created cursors. */
RGINLINE void rg_gui_sdl_platform_destroy(RgGuiSdlPlatform* state, RgInputState* input)
{
	if (state->text_input_window)
		rg_gui_sdl_forget_text_window(state, input, state->text_input_window);
	if (state->cursors[RG_GUI_CURSOR_DEFAULT])
		SDL_SetCursor(state->cursors[RG_GUI_CURSOR_DEFAULT]);
	for (u32 i = (u32)RG_GUI_CURSOR_TEXT; i < RG_GUI_SDL_CURSOR_COUNT; i++)
		if (state->cursors[i]) SDL_DestroyCursor(state->cursors[i]);
	memset(state, 0, sizeof(*state));
}

#endif // RG_GUI_SDL_H
