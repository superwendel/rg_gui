// Real input transitions must release mouse selection without ending editing.
#ifndef TEST_GUI_TEXT_CAPTURE_H
#define TEST_GUI_TEXT_CAPTURE_H

#define TEXT_CAPTURE_CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "Text capture check failed at line %d: %s\n", __LINE__, #condition); \
	goto cleanup; } checks++; } while (0)

static void text_capture_input_frame(RgInputState* input, RgInputEventQueue* events,
                                    int x, int y, int down)
{
	rg_input_begin_frame(input);
	rg_input_event_queue_reset(events, SDL_KMOD_NONE);
	SDL_Event event = {0};
	event.type = SDL_EVENT_MOUSE_MOTION;
	event.motion.x = (f32)x;
	event.motion.y = (f32)y;
	event.motion.xrel = (f32)(x - input->mouse_x);
	event.motion.yrel = (f32)(y - input->mouse_y);
	event.motion.state = down ? SDL_BUTTON_LMASK : 0u;
	rg_input_process_event_ex(input, &event, events);
	if (down != (int)input->current_mouse[RG_MOUSE_BUTTON_LEFT])
	{
		memset(&event, 0, sizeof(event));
		event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
		event.button.button = SDL_BUTTON_LEFT;
		event.button.down = down != 0;
		event.button.clicks = 1u;
		event.button.x = (f32)x;
		event.button.y = (f32)y;
		rg_input_process_event_ex(input, &event, events);
	}
	// Supply the sampled snapshot alongside the ordered SDL events. Sampling a
	// real OS window is unnecessary for this deterministic frontend regression.
	input->mouse_x = x;
	input->mouse_y = y;
	input->current_mouse[RG_MOUSE_BUTTON_LEFT] = down != 0;
}

static int text_capture_widget(RgGuiContext* gui, RgGuiTextAreaState* area,
                               char* buffer, size_t capacity, int multiline)
{
	RgGuiRect rect = rg_gui_make_rect(20, 20, 200, 80);
	if (multiline)
		return rg_gui_text_area(gui, area, buffer, capacity, rect, 0x7131u);
	return rg_gui_text_input(gui, NULL, buffer, capacity, rect, 0x7131u, NULL);
}

static int test_text_mouse_capture(const RgTextFont* font)
{
	int passed = 0;
	unsigned checks = 0u;
	void* memory = NULL;
	RgGuiInitDesc desc = {0};
	desc.font = font;
	desc.max_draw_cmds = 128u;
	desc.text_buffer_size = KB(4);
	size_t bytes = rg_gui_memory_required(&desc);
	TEXT_CAPTURE_CHECK(bytes > 0u);
	memory = malloc(bytes);
	TEXT_CAPTURE_CHECK(memory != NULL);

	for (int multiline = 0; multiline < 2; multiline++)
	{
		// A plain click, drag/release outside, disable during capture, and removal
		// during capture cover distinct ownership transitions for both editors.
		for (int scenario = 0; scenario < 4; scenario++)
		{
			RgArena arena = {(char*)memory, bytes, 0u, bytes};
			RgGuiContext gui;
			TEXT_CAPTURE_CHECK(rg_gui_init(&gui, &arena, &desc));
			gui.style.text_height = 10.0f;
			gui.style.padding = 0.0f;
			gui.style.border_thickness = 0.0f;
			gui.style.scroll_bar_width = 0.0f;
			RgGuiTextAreaState area = {0};
			char buffer[64];
			strcpy(buffer, multiline ? "AAAA\nAAAA" : "AAAA");
			RgInputState input;
			rg_input_init(&input);
			RgInputEvent event_storage[8];
			char event_text[32];
			RgInputEventQueue events;
			rg_input_event_queue_init(&events, event_storage, RG_ARRAY_COUNT(event_storage),
			                          event_text, sizeof(event_text));

			text_capture_input_frame(&input, &events, scenario == 1 ? 20 : 28, 25, 1);
			rg_gui_begin_frame_ex(&gui, &input, &events, 0u, 1.0f / 60.0f);
			TEXT_CAPTURE_CHECK(gui.mouse_pressed && gui.mouse_down);
			TEXT_CAPTURE_CHECK(!text_capture_widget(&gui, &area, buffer, sizeof(buffer), multiline));
			TEXT_CAPTURE_CHECK(gui.active_id == 0x7131u && gui.focus_id == 0x7131u);
			TEXT_CAPTURE_CHECK(gui.text_edit_state->active);
			TEXT_CAPTURE_CHECK(gui.text_edit_state->cursor == (scenario == 1 ? 0u : 1u));
			rg_gui_end_frame(&gui);
			TEXT_CAPTURE_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);

			if (scenario == 1)
			{
				text_capture_input_frame(&input, &events, 44, 25, 1);
				rg_gui_begin_frame_ex(&gui, &input, &events, 0u, 1.0f / 60.0f);
				TEXT_CAPTURE_CHECK(!text_capture_widget(&gui, &area, buffer, sizeof(buffer), multiline));
				TEXT_CAPTURE_CHECK(gui.active_id == 0x7131u);
				TEXT_CAPTURE_CHECK(gui.text_edit_state->selection_start == 0u &&
				                   gui.text_edit_state->selection_end == 3u);
				rg_gui_end_frame(&gui);
				TEXT_CAPTURE_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);
			}
			else if (scenario == 2)
			{
				text_capture_input_frame(&input, &events, 28, 25, 1);
				rg_gui_begin_frame_ex(&gui, &input, &events, 0u, 1.0f / 60.0f);
				rg_gui_begin_disabled(&gui, 1);
				TEXT_CAPTURE_CHECK(!text_capture_widget(&gui, &area, buffer, sizeof(buffer), multiline));
				rg_gui_end_disabled(&gui);
				TEXT_CAPTURE_CHECK(gui.active_id == 0u && !gui.text_edit_state->active);
				rg_gui_end_frame(&gui);
				TEXT_CAPTURE_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);
			}

			text_capture_input_frame(&input, &events, scenario == 1 ? 300 : 28,
			                         scenario == 1 ? 120 : 25, 0);
			rg_gui_begin_frame_ex(&gui, &input, &events, 0u, 1.0f / 60.0f);
			TEXT_CAPTURE_CHECK(gui.mouse_released && !gui.mouse_down);
			if (scenario == 3)
			{
				// Removing an active widget still requires the existing recovery
				// diagnostic. Fixing normal release must not hide genuine recovery.
				rg_gui_end_frame(&gui);
				TEXT_CAPTURE_CHECK(gui.active_id == 0u);
				TEXT_CAPTURE_CHECK(rg_gui_diagnostics(&gui)->flags == RG_GUI_DIAGNOSTIC_ACTIVE_ID_RECOVERED);
				TEXT_CAPTURE_CHECK(rg_gui_diagnostics(&gui)->recovered_active_ids == 1u);
				continue;
			}
			if (scenario == 2) rg_gui_begin_disabled(&gui, 1);
			TEXT_CAPTURE_CHECK(!text_capture_widget(&gui, &area, buffer, sizeof(buffer), multiline));
			if (scenario == 2) rg_gui_end_disabled(&gui);
			TEXT_CAPTURE_CHECK(gui.active_id == 0u);
			rg_gui_end_frame(&gui);
			TEXT_CAPTURE_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);
			TEXT_CAPTURE_CHECK(rg_gui_diagnostics(&gui)->recovered_active_ids == 0u);
			if (scenario == 2) continue;
			TEXT_CAPTURE_CHECK(gui.focus_id == 0x7131u && gui.text_edit_state->active);
			TEXT_CAPTURE_CHECK(gui.text_edit_state->cursor == (scenario == 1 ? 3u : 1u));

			// Editing after release proves that mouse capture and keyboard focus
			// have separate lifetimes, and that a drag's selection is preserved.
			text_capture_input_frame(&input, &events, 300, 120, 0);
			SDL_Event text_event = {0};
			text_event.type = SDL_EVENT_TEXT_INPUT;
			text_event.text.text = "M";
			rg_input_process_event_ex(&input, &text_event, &events);
			rg_gui_begin_frame_ex(&gui, &input, &events, 0u, 1.0f / 60.0f);
			TEXT_CAPTURE_CHECK(text_capture_widget(&gui, &area, buffer, sizeof(buffer), multiline));
			const char* expected = scenario == 1
			                           ? (multiline ? "MA\nAAAA" : "MA")
			                           : (multiline ? "AMAAA\nAAAA" : "AMAAA");
			TEXT_CAPTURE_CHECK(strcmp(buffer, expected) == 0);
			TEXT_CAPTURE_CHECK(gui.active_id == 0u && gui.focus_id == 0x7131u);
			rg_gui_end_frame(&gui);
			TEXT_CAPTURE_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);
		}
	}
	passed = 1;
	printf("Text mouse capture regressions: %u checks passed\n", checks);
cleanup:
	free(memory);
	return passed;
}

#undef TEXT_CAPTURE_CHECK
#endif
