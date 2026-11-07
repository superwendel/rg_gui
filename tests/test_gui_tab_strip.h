// Tab strip (rg_gui_tab_strip.h): activation on press, close by button and by
// middle click, drag to reorder, the new-tab button, scrolling when the tabs
// overflow, and labels cut with an ellipsis. Driven frame by frame through
// RgInputState with the monospace test provider.
#ifndef TEST_GUI_TAB_STRIP_H
#define TEST_GUI_TAB_STRIP_H

#include "../src/rg_gui_tab_strip.h"

#define STRIP_CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "Tab strip check failed at line %d: %s\n", __LINE__, #condition); \
	goto cleanup; } checks++; } while (0)

typedef struct StripRig
{
	RgGuiContext* gui;
	RgInputState input;
	RgInputEventQueue events;
	RgInputEvent event_storage[16];
	char event_text[64];
	RgGuiTabStripState state;
	RgGuiTabStripItem items[24];
	const char* labels[24];
	u32 count;
	u32 active;
	u32 flags;
	RgGuiRect rect;
	RgGuiTabStripResult result;
} StripRig;

static u32 strip_frame(StripRig* rig, f32 x, f32 y, int left, int middle, f32 wheel)
{
	RgInputState* in = &rig->input;
	u32 i;
	memcpy(in->previous_mouse, in->current_mouse, sizeof(in->current_mouse));
	in->current_mouse[RG_MOUSE_BUTTON_LEFT] = left != 0;
	in->current_mouse[RG_MOUSE_BUTTON_MIDDLE] = middle != 0;
	in->mouse_x = (int)x;
	in->mouse_y = (int)y;
	in->mouse_scroll_y = wheel;
	rg_input_event_queue_reset(&rig->events, RG_MOD_NONE);
	for (i = 0; i < rig->count; i++) rig->items[i].label = rig->labels[i];
	rg_gui_begin_frame_ex(rig->gui, in, &rig->events, 1u, 1.0f / 60.0f);
	rg_gui_tab_strip(rig->gui, &rig->state, rig->items, rig->count, rig->active, rig->flags, rig->rect, 0x7AB5u, &rig->result);
	rg_gui_end_frame(rig->gui);
	/* The caller applies what happened, as an application would. */
	if (rig->result.events & RG_GUI_TAB_STRIP_ACTIVATE) rig->active = rig->result.index;
	if (rig->result.events & RG_GUI_TAB_STRIP_MOVE)
	{
		const char* moved = rig->labels[rig->result.from];
		rig->labels[rig->result.from] = rig->labels[rig->result.to];
		rig->labels[rig->result.to] = moved;
		if (rig->active == rig->result.from) rig->active = rig->result.to;
		else if (rig->active == rig->result.to) rig->active = rig->result.from;
	}
	return rig->result.events;
}

/* Strip-relative x of tab i's left edge and its width, as the widget lays out. */
static f32 strip_tab_x(StripRig* rig, u32 index, f32* width)
{
	f32 x = 0.0f;
	u32 i;
	for (i = 0; i < index; i++) x += rg_gui_tab_strip_tab_width(rig->gui, &rig->items[i], rig->rect.h) + 1.0f;
	if (width) *width = rg_gui_tab_strip_tab_width(rig->gui, &rig->items[index], rig->rect.h);
	return x;
}

static rg_vec2 strip_tab_center(StripRig* rig, u32 index)
{
	f32 w, x = strip_tab_x(rig, index, &w);
	return rg_vec2(rig->rect.x + x - rig->state.scroll + w * 0.4f, rig->rect.y + rig->rect.h * 0.5f);
}

static rg_vec2 strip_close_center(StripRig* rig, u32 index)
{
	f32 w, x = strip_tab_x(rig, index, &w);
	RgGuiRect tab = rg_gui_make_rect(rig->rect.x + x - rig->state.scroll, rig->rect.y, w, rig->rect.h);
	RgGuiRect close = rg_gui_tab_strip_close_rect(rig->gui, tab);
	return rg_vec2(close.x + close.w * 0.5f, close.y + close.h * 0.5f);
}

/* A press and a release at p with the given button. */
static u32 strip_click(StripRig* rig, rg_vec2 p, int middle, u32* press_events)
{
	u32 first = strip_frame(rig, p.x, p.y, !middle, middle, 0.0f);
	if (press_events) *press_events = first;
	return strip_frame(rig, p.x, p.y, 0, 0, 0.0f);
}

static int strip_drew_ellipsis(const RgGuiContext* ctx)
{
	u32 i;
	for (i = 0; i < ctx->draw_list.count; i++)
	{
		const RgGuiDrawCmd* cmd = &ctx->draw_list.cmds[i];
		if (cmd->type == RG_GUI_CMD_TEXT && cmd->data.text.text && strstr(cmd->data.text.text, "\xE2\x80\xA6")) return 1;
	}
	return 0;
}

static int test_gui_tab_strip(void)
{
	static StripRig rig;
	static const char* const names[3] = {"One", "Two", "Three"};
	int passed = 0;
	unsigned checks = 0u;
	void* memory = NULL;
	MonoProvider mono = {0};
	RgGuiTextProvider provider = mono_provider_make(&mono);
	RgGuiInitDesc desc = {0};
	u32 events, i;
	memset(&rig, 0, sizeof(rig));
	rig.gui = (RgGuiContext*)calloc(1u, sizeof(RgGuiContext));
	desc.text_provider = &provider;
	desc.max_draw_cmds = 1024u;
	desc.text_buffer_size = KB(16);
	STRIP_CHECK(rig.gui != NULL);
	size_t bytes = rg_gui_memory_required(&desc);
	memory = malloc(bytes);
	STRIP_CHECK(memory != NULL);
	RgArena arena = {(char*)memory, bytes, 0u, bytes};
	STRIP_CHECK(rg_gui_init(rig.gui, &arena, &desc));
	rg_input_init(&rig.input);
	rg_input_event_queue_init(&rig.events, rig.event_storage, 16u, rig.event_text, sizeof(rig.event_text));
	rig.rect = rg_gui_make_rect(10.0f, 10.0f, 600.0f, 24.0f);
	rig.count = 3u;
	for (i = 0; i < 3u; i++)
	{
		rig.labels[i] = names[i];
		rig.items[i].closable = 1u;
	}

	// Nothing happens without input.
	STRIP_CHECK(strip_frame(&rig, -100.0f, -100.0f, 0, 0, 0.0f) == 0u);

	// A press on a tab activates it at once; the release adds nothing.
	events = strip_click(&rig, strip_tab_center(&rig, 1u), 0, &i);
	STRIP_CHECK(i == RG_GUI_TAB_STRIP_ACTIVATE && rig.active == 1u && events == 0u);

	// The close button closes on release over it, without activating; a release
	// elsewhere cancels.
	events = strip_click(&rig, strip_close_center(&rig, 1u), 0, &i);
	STRIP_CHECK(i == 0u && events == RG_GUI_TAB_STRIP_CLOSE && rig.result.index == 1u);
	strip_frame(&rig, strip_close_center(&rig, 1u).x, strip_close_center(&rig, 1u).y, 1, 0, 0.0f);
	STRIP_CHECK(strip_frame(&rig, -100.0f, -100.0f, 0, 0, 0.0f) == 0u);
	// Off the button onto its own tab's label: no close either.
	strip_frame(&rig, strip_close_center(&rig, 1u).x, strip_close_center(&rig, 1u).y, 1, 0, 0.0f);
	STRIP_CHECK(strip_frame(&rig, strip_tab_center(&rig, 1u).x - 10.0f, strip_tab_center(&rig, 1u).y, 0, 0, 0.0f) == 0u);

	// Middle click closes the tab under the pointer; not a tab that cannot close.
	events = strip_click(&rig, strip_tab_center(&rig, 2u), 1, &i);
	STRIP_CHECK(i == 0u && events == RG_GUI_TAB_STRIP_CLOSE && rig.result.index == 2u && rig.active == 1u);
	rig.items[2].closable = 0u;
	STRIP_CHECK(strip_click(&rig, strip_tab_center(&rig, 2u), 1, NULL) == 0u);
	rig.items[2].closable = 1u;

	// "+" opens a tab on release.
	{
		f32 w, x = strip_tab_x(&rig, 2u, &w);
		rg_vec2 plus = rg_vec2(rig.rect.x + x + w + 1.0f + rig.rect.h * 0.5f, rig.rect.y + rig.rect.h * 0.5f);
		STRIP_CHECK(strip_click(&rig, plus, 0, &i) == RG_GUI_TAB_STRIP_NEW && i == 0u);
		rig.flags = RG_GUI_TAB_STRIP_NO_NEW_BUTTON;
		STRIP_CHECK(strip_click(&rig, plus, 0, NULL) == 0u);
		rig.flags = 0u;
	}

	// Dragging: a small wobble is still a click; past a neighbor's center the
	// tab moves one place per step, and keeps going while held.
	{
		rg_vec2 p = strip_tab_center(&rig, 0u);
		f32 w1, x1 = strip_tab_x(&rig, 1u, &w1);
		f32 w2, x2 = strip_tab_x(&rig, 2u, &w2);
		STRIP_CHECK((strip_frame(&rig, p.x, p.y, 1, 0, 0.0f) & RG_GUI_TAB_STRIP_ACTIVATE) && rig.active == 0u);
		STRIP_CHECK(strip_frame(&rig, p.x + 2.0f, p.y, 1, 0, 0.0f) == 0u);
		/* The grabbed tab's center passes tab 1's center: One moves to index 1. */
		events = strip_frame(&rig, p.x + (x1 + w1 * 0.5f) - (p.x - rig.rect.x) + 4.0f, p.y, 1, 0, 0.0f);
		STRIP_CHECK(events == RG_GUI_TAB_STRIP_MOVE && rig.result.from == 0u && rig.result.to == 1u);
		STRIP_CHECK(strcmp(rig.labels[0], "Two") == 0 && strcmp(rig.labels[1], "One") == 0 && rig.active == 1u);
		/* Farther: past Three's center. */
		events = strip_frame(&rig, rig.rect.x + x2 + w2, p.y, 1, 0, 0.0f);
		STRIP_CHECK(events == RG_GUI_TAB_STRIP_MOVE && rig.result.from == 1u && rig.result.to == 2u);
		STRIP_CHECK(strcmp(rig.labels[2], "One") == 0 && rig.active == 2u);
		/* Release: no click, no close. */
		STRIP_CHECK(strip_frame(&rig, rig.rect.x + x2 + w2, p.y, 0, 0, 0.0f) == 0u);
	}

	// Too many tabs to fit: the active one is scrolled into view, the wheel
	// scrolls, and the scroll stays within the tabs.
	{
		static const char* many = "Folder with a name";
		f32 w, x;
		rig.count = 20u;
		for (i = 0; i < 20u; i++)
		{
			rig.labels[i] = many;
			rig.items[i].closable = 1u;
		}
		rig.active = 19u;
		strip_frame(&rig, -100.0f, -100.0f, 0, 0, 0.0f);
		x = strip_tab_x(&rig, 19u, &w);
		STRIP_CHECK(rig.state.scroll > 0.0f && x + w <= rig.state.scroll + rig.rect.w - rig.rect.h + 1.0f);
		STRIP_CHECK(x >= rig.state.scroll);
		strip_frame(&rig, rig.rect.x + 50.0f, rig.rect.y + 5.0f, 0, 0, 3.0f);
		STRIP_CHECK(rig.state.scroll < x);
		strip_frame(&rig, rig.rect.x + 50.0f, rig.rect.y + 5.0f, 0, 0, 1000.0f);
		STRIP_CHECK(rig.state.scroll == 0.0f);
		strip_frame(&rig, rig.rect.x + 50.0f, rig.rect.y + 5.0f, 0, 0, -1000.0f);
		STRIP_CHECK(rig.state.scroll > 0.0f && rig.state.scroll <= strip_tab_x(&rig, 19u, &w) + w + 1.0f);
		/* A click on a tab in view activates that tab, not one under it before the scroll. */
		events = strip_click(&rig, strip_tab_center(&rig, 18u), 0, &i);
		STRIP_CHECK((i & RG_GUI_TAB_STRIP_ACTIVATE) && rig.active == 18u);
	}

	// A label wider than the widest tab is cut with an ellipsis.
	{
		static const char* long_label = "A folder name far too long to fit in any single tab of this strip";
		rig.count = 1u;
		rig.labels[0] = long_label;
		rig.active = 0u;
		rig.state.scroll = 0.0f;
		strip_frame(&rig, -100.0f, -100.0f, 0, 0, 0.0f);
		STRIP_CHECK(rg_gui_tab_strip_tab_width(rig.gui, &rig.items[0], rig.rect.h) == rig.rect.h * 8.0f);
		STRIP_CHECK(strip_drew_ellipsis(rig.gui));
		rig.labels[0] = "Short";
		strip_frame(&rig, -100.0f, -100.0f, 0, 0, 0.0f);
		STRIP_CHECK(!strip_drew_ellipsis(rig.gui));
	}

	passed = 1;
	printf("tab strip checks: %u\n", checks);
cleanup:
	free(memory);
	free(rig.gui);
	return passed;
}

#endif
