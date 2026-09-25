// Draw-command lifetime regressions: renderer preparation happens after widgets
// return and must see their submitted bytes, including after numeric-cache reuse.
#ifndef TEST_GUI_TEXT_LIFETIME_H
#define TEST_GUI_TEXT_LIFETIME_H

#define LIFETIME_CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "Text lifetime check failed at line %d: %s\n", __LINE__, #condition); \
	goto cleanup; } checks++; } while (0)

static const RgGuiDrawCmd* lifetime_text_at(const RgGuiContext* gui, u32 index)
{
	const RgGuiDrawList* list = rg_gui_draw_list(gui);
	for (u32 i = 0u; i < list->count; i++)
	{
		if (list->cmds[i].type != RG_GUI_CMD_TEXT) continue;
		if (index == 0u) return &list->cmds[i];
		index--;
	}
	return NULL;
}

static int lifetime_snapshot_is(const RgGuiContext* gui, u32 index, const char* expected)
{
	const RgGuiDrawCmd* cmd = lifetime_text_at(gui, index);
	if (!cmd || cmd->data.text.cache_identity != 0u ||
	    strcmp(cmd->data.text.text, expected) != 0) return 0;
	uintptr_t address = (uintptr_t)cmd->data.text.text;
	uintptr_t base = (uintptr_t)gui->text_buffer;
	return address >= base && address - base < gui->text_buffer_used;
}

static int test_text_lifetimes(const RgTextFont* font)
{
	int passed = 0;
	unsigned checks = 0u;
	void* memory = NULL;
	RgGuiContext gui;
	RgGuiInitDesc desc = {0};
	desc.font = font;
	desc.max_draw_cmds = 256u;
	desc.text_buffer_size = KB(4);
	desc.value_cache_size = 2u;
	size_t bytes = rg_gui_memory_required(&desc);
	LIFETIME_CHECK(bytes > 0u);
	memory = malloc(bytes);
	LIFETIME_CHECK(memory != NULL);
	RgArena arena = {(char*)memory, bytes, 0u, bytes};
	LIFETIME_CHECK(rg_gui_init(&gui, &arena, &desc));
	LIFETIME_CHECK(gui.value_cache_capacity == 2u);
	gui.style.text_height = 10.0f;
	RgInputState input;
	rg_input_init(&input);
	input.mouse_x = -100;
	input.mouse_y = -100;
	RgGuiRect label_rect = rg_gui_make_rect(0, 0, 200, 20);
	rg_vec4 white = rg_gui_color(1, 1, 1, 1);
	char mutable_text[32] = "A";
	static const char static_text[] = "AM";

	// Two submissions from the same caller-owned address keep independent bytes.
	rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
	rg_gui_label(&gui, mutable_text, label_rect);
	strcpy(mutable_text, "MMMM");
	rg_gui_push_text(&gui, mutable_text, rg_vec2(0, 30), white);
	strcpy(mutable_text, "?");
	LIFETIME_CHECK(lifetime_snapshot_is(&gui, 0u, "A"));
	LIFETIME_CHECK(lifetime_snapshot_is(&gui, 1u, "MMMM"));
	LIFETIME_CHECK(gui.text_buffer_used == 7u);
	uintptr_t first_frame_address = (uintptr_t)lifetime_text_at(&gui, 0u)->data.text.text;

	// The explicit static APIs retain zero-copy pointer identity, including the
	// list-targeted and scaled entry points used by overlays and node drawing.
	size_t used_before_static = gui.text_buffer_used;
	rg_gui_label_static(&gui, static_text, label_rect);
	rg_gui_push_text_static_to(&gui, &gui.draw_list, static_text, rg_vec2(0, 40), white);
	rg_gui_push_text_scaled_static(&gui, static_text, rg_vec2(0, 50), white, 2.0f);
	rg_gui_push_text_scaled_static_to(&gui, &gui.draw_list, static_text, rg_vec2(0, 60), white, 0.5f);
	for (u32 i = 2u; i < 6u; i++)
	{
		const RgGuiDrawCmd* cmd = lifetime_text_at(&gui, i);
		LIFETIME_CHECK(cmd && cmd->data.text.text == static_text &&
		               cmd->data.text.cache_identity == (uintptr_t)static_text);
	}
	LIFETIME_CHECK(gui.text_buffer_used == used_before_static);
	rg_gui_end_frame(&gui);
	LIFETIME_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);

	// Frame storage reuses addresses. That reuse must never become immutable
	// identity for ordinary or already-snapshotted substring text.
	rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
	strcpy(mutable_text, "MA");
	rg_gui_label(&gui, mutable_text, label_rect);
	strcpy(mutable_text, "?");
	LIFETIME_CHECK(lifetime_snapshot_is(&gui, 0u, "MA"));
	LIFETIME_CHECK((uintptr_t)lifetime_text_at(&gui, 0u)->data.text.text == first_frame_address);
	const char* substring = rg_gui_copy_text_range(&gui, "AAMM", 1u, 3u);
	size_t used_before_borrow = gui.text_buffer_used;
	rg_gui_push_text_ex(&gui, substring, rg_vec2(0, 20), white, 0);
	LIFETIME_CHECK(lifetime_snapshot_is(&gui, 1u, "AM"));
	LIFETIME_CHECK(gui.text_buffer_used == used_before_borrow);
	rg_gui_end_frame(&gui);
	LIFETIME_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);

	// More numeric widgets than value-cache slots force overwrites before render
	// preparation. A second frame changes every number at the same widget IDs.
	static const char* expected_numbers[2][6] = {
	    {"0.125", "1.125", "2.125", "3.125", "4.125", "5.125"},
	    {"0.875", "1.875", "2.875", "3.875", "4.875", "5.875"}};
	for (u32 frame = 0u; frame < 2u; frame++)
	{
		rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
		for (u32 i = 0u; i < 6u; i++)
		{
			f32 value = (f32)i + (frame ? 0.875f : 0.125f);
			(void)rg_gui_slider_float(&gui, NULL, &value, 0.0f, 10.0f,
			                          rg_gui_make_rect(0, (f32)i * 24.0f, 180, 20),
			                          (RgGuiId)(100u + i));
		}
		rg_gui_end_frame(&gui);
		LIFETIME_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);
		LIFETIME_CHECK(lifetime_text_at(&gui, 6u) == NULL);
		for (u32 i = 0u; i < 6u; i++)
			LIFETIME_CHECK(lifetime_snapshot_is(&gui, i, expected_numbers[frame][i]));
		LIFETIME_CHECK(gui.text_buffer_used == 36u);
	}

	// Unclipped editor text snapshots the caller's mutable buffer. Clipped text
	// is already copied by the range helper and must not be copied a second time.
	rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
	strcpy(mutable_text, "AAM");
	(void)rg_gui_text_input(&gui, NULL, mutable_text, sizeof(mutable_text),
	                        rg_gui_make_rect(0, 0, 100, 20), 300u, NULL);
	strcpy(mutable_text, "?");
	LIFETIME_CHECK(lifetime_snapshot_is(&gui, 0u, "AAM"));
	LIFETIME_CHECK(gui.text_buffer_used == 4u);
	rg_gui_end_frame(&gui);
	LIFETIME_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);
	for (u32 frame = 0u; frame < 2u; frame++)
	{
		rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
		strcpy(mutable_text, frame ? "AAAA" : "MMMM");
		(void)rg_gui_text_input(&gui, NULL, mutable_text, sizeof(mutable_text),
		                        rg_gui_make_rect(0, 0, 17, 20), 301u, NULL);
		strcpy(mutable_text, "?");
		LIFETIME_CHECK(lifetime_snapshot_is(&gui, 0u, frame ? "A" : "M"));
		LIFETIME_CHECK(gui.text_buffer_used == 2u);
		rg_gui_end_frame(&gui);
		LIFETIME_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);
	}

	RgGuiTextAreaState area = {0};
	for (u32 frame = 0u; frame < 2u; frame++)
	{
		rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
		strcpy(mutable_text, frame ? "M\nA" : "AA\nMM");
		(void)rg_gui_text_area(&gui, &area, mutable_text, sizeof(mutable_text),
		                       rg_gui_make_rect(0, 0, 200, 100), 400u);
		strcpy(mutable_text, "?");
		LIFETIME_CHECK(lifetime_snapshot_is(&gui, 0u, frame ? "M" : "AA"));
		LIFETIME_CHECK(lifetime_snapshot_is(&gui, 1u, frame ? "A" : "MM"));
		LIFETIME_CHECK(lifetime_text_at(&gui, 2u) == NULL);
		LIFETIME_CHECK(gui.text_buffer_used == (frame ? 4u : 6u));
		rg_gui_end_frame(&gui);
		LIFETIME_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);
	}

	// Node titles use the same frame-range optimization when clipped.
	RgGuiNodeEditorState editor = {0};
	for (u32 frame = 0u; frame < 2u; frame++)
	{
		rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
		(void)rg_gui_node_editor_begin(&gui, &editor, rg_gui_make_rect(0, 0, 200, 120), 500u);
		rg_vec2 node_position = rg_vec2(20, 20);
		strcpy(mutable_text, frame ? "AAAAAAAA" : "MMMMMMMM");
		(void)rg_gui_node_begin(&gui, &editor, mutable_text, &node_position,
		                        rg_vec2(40, 60), NULL, 501u);
		rg_gui_node_end(&gui, &editor);
		rg_gui_node_editor_end(&gui, &editor);
		strcpy(mutable_text, "?");
		LIFETIME_CHECK(lifetime_snapshot_is(&gui, 0u, frame ? "AAAA" : "MMM"));
		LIFETIME_CHECK(lifetime_text_at(&gui, 1u) == NULL);
		LIFETIME_CHECK(gui.text_buffer_used == (frame ? 5u : 4u));
		rg_gui_end_frame(&gui);
		LIFETIME_CHECK(rg_gui_diagnostics(&gui)->flags == 0u);
	}

	passed = 1;
	printf("Text lifetime regressions: %u checks passed\n", checks);
cleanup:
	free(memory);
	return passed;
}

#undef LIFETIME_CHECK
#endif
