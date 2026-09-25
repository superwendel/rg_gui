// Optional viewport storage: deferred acquisition, failure recovery and ownership.
#ifndef TEST_GUI_VIEWPORT_STORAGE_H
#define TEST_GUI_VIEWPORT_STORAGE_H

#define VIEWPORT_STORAGE_TEST_CAPACITY 32u
#define VIEWPORT_STORAGE_CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "Viewport storage check failed at line %d: %s\n", __LINE__, #condition); \
	goto cleanup; } checks++; } while (0)

typedef struct ViewportStorageProbe
{
	RgGuiContext* gui;
	void* blocks[RG_GUI_MAX_VIEWPORTS];
	RgGuiDrawCmd* draw[RG_GUI_MAX_VIEWPORTS];
	RgGuiDrawCmd* overlay[RG_GUI_MAX_VIEWPORTS];
	u32 calls;
	u32 allocations;
	u32 frees;
	u32 last_slot;
	u32 last_draw_capacity;
	u32 last_overlay_capacity;
	u32 failure_mode;
	size_t owned_bytes;
} ViewportStorageProbe;

static int viewport_storage_probe(void* user_data, u32 slot_index,
                                  u32 draw_capacity, u32 overlay_capacity,
                                  RgGuiDrawCmd** draw_commands,
                                  RgGuiDrawCmd** overlay_commands)
{
	ViewportStorageProbe* probe = (ViewportStorageProbe*)user_data;
	probe->calls++;
	probe->last_slot = slot_index;
	probe->last_draw_capacity = draw_capacity;
	probe->last_overlay_capacity = overlay_capacity;
	if (slot_index >= RG_GUI_MAX_VIEWPORTS ||
	    draw_capacity != VIEWPORT_STORAGE_TEST_CAPACITY ||
	    overlay_capacity != VIEWPORT_STORAGE_TEST_CAPACITY || probe->failure_mode == 1u)
		return 0;
	if (!probe->blocks[slot_index])
	{
		const size_t alignment = RG_ALIGNOF(RgGuiDrawCmd);
		const size_t bytes = ((size_t)draw_capacity + overlay_capacity) * sizeof(RgGuiDrawCmd);
		void* block = malloc(bytes + alignment - 1u);
		if (!block) return 0;
		uintptr_t aligned = ((uintptr_t)block + alignment - 1u) & ~(uintptr_t)(alignment - 1u);
		probe->blocks[slot_index] = block;
		probe->draw[slot_index] = (RgGuiDrawCmd*)aligned;
		probe->overlay[slot_index] = probe->draw[slot_index] + draw_capacity;
		memset(probe->draw[slot_index], 0, bytes);
		probe->owned_bytes += bytes;
		probe->allocations++;
	}
	*draw_commands = probe->draw[slot_index];
	*overlay_commands = probe->overlay[slot_index];
	switch (probe->failure_mode)
	{
		case 2u: *draw_commands = NULL; break;
		case 3u: *draw_commands = (RgGuiDrawCmd*)((char*)*draw_commands + 1u); break;
		case 4u: *overlay_commands = *draw_commands; break;
		case 5u: *draw_commands = probe->gui->draw_list.cmds; break;
		case 6u: *overlay_commands = probe->gui->overlay_list.cmds; break;
		case 7u: *draw_commands = probe->draw[0]; break;
		case 8u: *overlay_commands = probe->overlay[0]; break;
		case 9u: *draw_commands = probe->draw[0] + 1u; break;
		case 10u:
			*draw_commands = (RgGuiDrawCmd*)(UINTPTR_MAX & ~(uintptr_t)(RG_ALIGNOF(RgGuiDrawCmd) - 1u));
			break;
		default: break;
	}
	return 1;
}

static void viewport_storage_probe_destroy(ViewportStorageProbe* probe)
{
	for (u32 i = 0u; i < RG_GUI_MAX_VIEWPORTS; i++)
	{
		if (!probe->blocks[i]) continue;
		free(probe->blocks[i]);
		probe->blocks[i] = NULL;
		probe->draw[i] = probe->overlay[i] = NULL;
		probe->frees++;
	}
	probe->owned_bytes = 0u;
}

static int test_viewport_storage(const RgTextFont* font)
{
	int passed = 0;
	unsigned checks = 0u;
	void* eager_memory = NULL;
	void* deferred_memory = NULL;
	RgGuiContext eager = {0};
	RgGuiContext gui = {0};
	ViewportStorageProbe probe = {0};
	RgGuiInitDesc desc = {0};
	RgInputState input = {0};
	RgGuiDrawCmd saved[2];
	RgGuiViewport* first = NULL;
	RgGuiViewport* second = NULL;
	desc.font = font;
	desc.max_draw_cmds = VIEWPORT_STORAGE_TEST_CAPACITY;
	desc.text_buffer_size = 1024u;
	const size_t eager_bytes = rg_gui_memory_required(&desc);
	VIEWPORT_STORAGE_CHECK(eager_bytes > 0u);
	eager_memory = malloc(eager_bytes);
	VIEWPORT_STORAGE_CHECK(eager_memory != NULL);
	RgArena eager_arena = {(char*)eager_memory, eager_bytes, 0u, eager_bytes};
	VIEWPORT_STORAGE_CHECK(rg_gui_init(&eager, &eager_arena, &desc));
	VIEWPORT_STORAGE_CHECK(eager.viewport_storage == NULL);
	for (u32 i = 0u; i < eager.viewport_capacity; i++)
	{
		VIEWPORT_STORAGE_CHECK(eager.viewports[i].draw_list.cmds != NULL);
		VIEWPORT_STORAGE_CHECK(eager.viewports[i].overlay_list.cmds != NULL);
		VIEWPORT_STORAGE_CHECK(eager.viewports[i].draw_list.capacity == desc.max_draw_cmds);
	}

	probe.gui = &gui;
	desc.viewport_storage = viewport_storage_probe;
	desc.viewport_storage_user = &probe;
	const size_t deferred_bytes = rg_gui_memory_required(&desc);
	const size_t removed = (size_t)RG_GUI_MAX_VIEWPORTS * 2u *
	    ((size_t)desc.max_draw_cmds * sizeof(RgGuiDrawCmd) + RG_ALIGNOF(RgGuiDrawCmd) - 1u);
	VIEWPORT_STORAGE_CHECK(deferred_bytes > 0u && eager_bytes - deferred_bytes == removed);
	deferred_memory = malloc(deferred_bytes);
	VIEWPORT_STORAGE_CHECK(deferred_memory != NULL);
	RgArena arena = {(char*)deferred_memory, deferred_bytes, 0u, deferred_bytes};
	VIEWPORT_STORAGE_CHECK(rg_gui_init(&gui, &arena, &desc));
	VIEWPORT_STORAGE_CHECK(arena.used <= deferred_bytes && probe.calls == 0u && probe.owned_bytes == 0u);
	VIEWPORT_STORAGE_CHECK(gui.viewport_storage == viewport_storage_probe && gui.viewport_storage_user == &probe);
	for (u32 i = 0u; i < gui.viewport_capacity; i++)
	{
		VIEWPORT_STORAGE_CHECK(gui.viewports[i].id == 0u && !gui.viewports[i].draw_list.cmds &&
		                       !gui.viewports[i].overlay_list.cmds);
		VIEWPORT_STORAGE_CHECK(gui.viewports[i].draw_list.capacity == desc.max_draw_cmds &&
		                       gui.viewports[i].overlay_list.capacity == desc.max_draw_cmds);
	}

	// Failed callbacks and invalid arrays must not claim a slot or alter targets.
	for (u32 mode = 1u; mode <= 10u; mode++)
	{
		if (mode >= 7u && mode <= 9u) continue; // These need another live slot below.
		probe.failure_mode = mode;
		rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
		rg_gui_push_rect(&gui, rg_gui_make_rect(1, 2, 3, 4), rg_gui_color(1, 1, 1, 1));
		VIEWPORT_STORAGE_CHECK(rg_gui_viewport_begin(&gui, 101u, rg_gui_make_rect(0, 0, 80, 60), &input) == NULL);
		VIEWPORT_STORAGE_CHECK(gui.diagnostics.flags == RG_GUI_DIAGNOSTIC_VIEWPORT_STORAGE);
		VIEWPORT_STORAGE_CHECK(gui.viewports[0].id == 0u && !gui.viewports[0].draw_list.cmds &&
		                       !gui.viewports[0].overlay_list.cmds && gui.viewport_stack_top == 0u);
		VIEWPORT_STORAGE_CHECK(gui.draw_target == &gui.draw_list && gui.draw_list.count == 1u &&
		                       rg_gui_viewport_count(&gui) == 0u && probe.frees == 0u);
		if (mode == 1u) VIEWPORT_STORAGE_CHECK(probe.allocations == 0u);
		rg_gui_end_frame(&gui);
	}

	probe.failure_mode = 0u;
	rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
	first = rg_gui_viewport_begin(&gui, 101u, rg_gui_make_rect(0, 0, 80, 60), &input);
	VIEWPORT_STORAGE_CHECK(first == &gui.viewports[0]);
	VIEWPORT_STORAGE_CHECK(probe.last_slot == 0u && probe.last_draw_capacity == desc.max_draw_cmds &&
	                       probe.last_overlay_capacity == desc.max_draw_cmds);
	rg_gui_push_rect(&gui, rg_gui_make_rect(5, 6, 7, 8), rg_gui_color(1, 0, 0, 1));
	rg_gui_push_rect_to(&gui, rg_gui_overlay_target(&gui), rg_gui_make_rect(9, 10, 11, 12), rg_gui_color(0, 1, 0, 1));
	rg_gui_viewport_end(&gui, first);
	VIEWPORT_STORAGE_CHECK(first->draw_list.count == 2u && first->overlay_start == 1u);
	VIEWPORT_STORAGE_CHECK(first->draw_list.cmds[0].data.rect.rect.x == 5.0f &&
	                       first->draw_list.cmds[1].data.rect.rect.x == 9.0f);
	memcpy(saved, first->draw_list.cmds, sizeof(saved));
	VIEWPORT_STORAGE_CHECK(gui.draw_target == &gui.draw_list && gui.viewport_stack_top == 0u);

#if RG_GUI_MAX_VIEWPORTS > 1u
	{
		for (u32 mode = 7u; mode <= 9u; mode++)
		{
			probe.failure_mode = mode;
			VIEWPORT_STORAGE_CHECK(rg_gui_viewport_begin(&gui, 102u, rg_gui_make_rect(0, 0, 80, 60), &input) == NULL);
			VIEWPORT_STORAGE_CHECK(gui.viewports[1].id == 0u && !gui.viewports[1].draw_list.cmds &&
			                       !gui.viewports[1].overlay_list.cmds);
			VIEWPORT_STORAGE_CHECK(memcmp(saved, first->draw_list.cmds, sizeof(saved)) == 0);
		}
		probe.failure_mode = 0u;
		second = rg_gui_viewport_begin(&gui, 102u, rg_gui_make_rect(0, 0, 80, 60), &input);
		VIEWPORT_STORAGE_CHECK(second == &gui.viewports[1] && probe.last_slot == 1u);
		rg_gui_push_rect(&gui, rg_gui_make_rect(20, 21, 22, 23), rg_gui_color(0, 0, 1, 1));
		rg_gui_viewport_end(&gui, second);
		VIEWPORT_STORAGE_CHECK(memcmp(saved, first->draw_list.cmds, sizeof(saved)) == 0 &&
		                       second->draw_list.cmds != first->draw_list.cmds && rg_gui_viewport_count(&gui) == 2u);
	}
#endif
	rg_gui_end_frame(&gui);
	const u32 retained_calls = probe.calls;
	const u32 retained_allocations = probe.allocations;
	const size_t retained_bytes = probe.owned_bytes;
	first->focus_id = 77u;
	first->active_id = 88u;
	rg_gui_viewport_release(&gui, 101u);
	if (second) rg_gui_viewport_release(&gui, 102u);
	VIEWPORT_STORAGE_CHECK(first->id == 0u && !first->active && first->focus_id == 0u && first->active_id == 0u);
	VIEWPORT_STORAGE_CHECK(first->draw_list.cmds == probe.draw[0] && first->overlay_list.cmds == probe.overlay[0]);
	VIEWPORT_STORAGE_CHECK(probe.calls == retained_calls && probe.owned_bytes == retained_bytes && probe.frees == 0u);

	// A main-only frame and a new viewport ID retain the successful slot arrays.
	rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
	VIEWPORT_STORAGE_CHECK(rg_gui_viewport_count(&gui) == 0u && !gui.diagnostics.flags);
	rg_gui_end_frame(&gui);
	for (u32 cycle = 0u; cycle < 3u; cycle++)
	{
		rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
		first = rg_gui_viewport_begin(&gui, 201u + cycle, rg_gui_make_rect(0, 0, 80, 60), &input);
		VIEWPORT_STORAGE_CHECK(first == &gui.viewports[0] && first->draw_list.count == 0u);
		rg_gui_push_rect(&gui, rg_gui_make_rect(1, 2, 3, 4), rg_gui_color(1, 1, 1, 1));
		rg_gui_viewport_end(&gui, first);
		rg_gui_end_frame(&gui);
		VIEWPORT_STORAGE_CHECK(!gui.diagnostics.flags && probe.calls == retained_calls &&
		                       probe.allocations == retained_allocations && probe.owned_bytes == retained_bytes);
		rg_gui_viewport_release(&gui, 201u + cycle);
	}

	// Default arena ownership still produces the same ordered command content.
	rg_gui_begin_frame(&eager, &input, 1.0f / 60.0f);
	RgGuiViewport* eager_viewport = rg_gui_viewport_begin(&eager, 1u, rg_gui_make_rect(0, 0, 80, 60), &input);
	VIEWPORT_STORAGE_CHECK(eager_viewport != NULL);
	rg_gui_push_rect(&eager, rg_gui_make_rect(5, 6, 7, 8), rg_gui_color(1, 0, 0, 1));
	rg_gui_push_rect_to(&eager, rg_gui_overlay_target(&eager), rg_gui_make_rect(9, 10, 11, 12), rg_gui_color(0, 1, 0, 1));
	rg_gui_viewport_end(&eager, eager_viewport);
	rg_gui_end_frame(&eager);
	VIEWPORT_STORAGE_CHECK(eager_viewport->draw_list.count == 2u && eager_viewport->overlay_start == 1u);
	for (u32 i = 0u; i < 2u; i++)
	{
		const RgGuiDrawCmd* command = &eager_viewport->draw_list.cmds[i];
		VIEWPORT_STORAGE_CHECK(command->type == saved[i].type &&
		    memcmp(&command->data.rect.rect, &saved[i].data.rect.rect, sizeof(RgGuiRect)) == 0 &&
		    memcmp(&command->data.rect.color, &saved[i].data.rect.color, sizeof(rg_vec4)) == 0);
	}
	// Frontend teardown is caller-owned; release never freed these buffers.
	VIEWPORT_STORAGE_CHECK(probe.frees == 0u && probe.owned_bytes > 0u);
	viewport_storage_probe_destroy(&probe);
	VIEWPORT_STORAGE_CHECK(probe.frees == probe.allocations && probe.owned_bytes == 0u);
	passed = 1;
cleanup:
	viewport_storage_probe_destroy(&probe);
	free(deferred_memory);
	free(eager_memory);
	if (passed) printf("viewport storage checks passed (%u assertions)\n", checks);
	return passed;
}

#undef VIEWPORT_STORAGE_CHECK
#undef VIEWPORT_STORAGE_TEST_CAPACITY
#endif
