// Tables: virtualized rows, mouse and keyboard selection, scrolling to the
// cursor, header sorting and focus. Driven frame by frame through RgInputState
// with the monospace test provider (no font).
#ifndef TEST_GUI_TABLE_H
#define TEST_GUI_TABLE_H
#define TABLE_CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "Table check failed at line %d: %s\n", __LINE__, #condition); \
	goto cleanup; } checks++; } while (0)

#define TABLE_ROWS 10000u

typedef struct TableRig
{
	RgGuiContext* gui;
	RgInputState input;
	RgGuiTableState table;
	RgGuiSelectionState selection;
	f32 widths[3];
	u8 selected[TABLE_ROWS];
	u32 selected_count;
	u32 row_count;
	u32 row_calls;
	u32 row_min;
	u32 row_max;
	u32 result;
	RgGuiRect rect;
	f32 row_h;
	/* Ordered events: a left or right press pushes a button-down event with
	 * this click count (the platform's; 2 is a double-click). */
	RgInputEventQueue events;
	RgInputEvent event_storage[16];
	char event_text[64];
	u8 press_clicks;
} TableRig;

static const RgGuiTableColumn table_rig_columns[3] = {
	{"Name", 1u, 20.0f, RG_GUI_TABLE_COLUMN_RESIZE | RG_GUI_TABLE_COLUMN_SORT},
	{"Size", 2u, 20.0f, RG_GUI_TABLE_COLUMN_RESIZE | RG_GUI_TABLE_COLUMN_SORT},
	{"Kind", 3u, 20.0f, RG_GUI_TABLE_COLUMN_NONE},
};

static int table_rig_is_selected(const void* user, u32 index)
{
	const TableRig* rig = (const TableRig*)user;
	return index < rig->row_count && rig->selected[index];
}

static void table_rig_set_selected(void* user, u32 index, int selected)
{
	TableRig* rig = (TableRig*)user;
	if (index >= rig->row_count || (rig->selected[index] != 0) == (selected != 0)) return;
	rig->selected[index] = (u8)(selected != 0);
	if (selected) rig->selected_count++;
	else rig->selected_count--;
}

static void table_rig_clear(void* user)
{
	TableRig* rig = (TableRig*)user;
	memset(rig->selected, 0, sizeof(rig->selected));
	rig->selected_count = 0u;
}

static const RgGuiSelectionOps table_rig_ops = {table_rig_is_selected, table_rig_set_selected, NULL, table_rig_clear};

static void table_rig_row(RgGuiContext* ctx, u32 row_index, const RgGuiTableColumn* columns, const f32* widths,
                          u32 column_count, RgGuiRect row_rect, const void* user)
{
	TableRig* rig = (TableRig*)(void*)(uintptr_t)user;
	(void)ctx;
	(void)columns;
	(void)widths;
	(void)column_count;
	(void)row_rect;
	rig->row_calls++;
	if (row_index < rig->row_min) rig->row_min = row_index;
	if (row_index > rig->row_max) rig->row_max = row_index;
}

/* One frame: the mouse at (x, y) with the left button `left` and `right`, and
 * `keys` held (terminated by RG_KEY_UNKNOWN). Edges come from the previous frame. */
static u32 table_rig_frame_ex(TableRig* rig, f32 x, f32 y, int left, int right, const RgKey* keys)
{
	RgInputState* in = &rig->input;
	memcpy(in->previous_keyboard, in->current_keyboard, sizeof(in->current_keyboard));
	memcpy(in->previous_mouse, in->current_mouse, sizeof(in->current_mouse));
	memset(in->current_keyboard, 0, sizeof(in->current_keyboard));
	for (; keys && *keys != RG_KEY_UNKNOWN; keys++) in->current_keyboard[*keys] = true;
	in->current_mouse[RG_MOUSE_BUTTON_LEFT] = left != 0;
	in->current_mouse[RG_MOUSE_BUTTON_RIGHT] = right != 0;
	in->mouse_x = (int)x;
	in->mouse_y = (int)y;
	rig->row_calls = 0u;
	rig->row_min = ~0u;
	rig->row_max = 0u;
	rg_input_event_queue_reset(&rig->events, RG_MOD_NONE);
	for (int b = 0; b < 2; b++)
	{
		int button = b ? RG_MOUSE_BUTTON_RIGHT : RG_MOUSE_BUTTON_LEFT;
		if (in->current_mouse[button] && !in->previous_mouse[button])
		{
			RgInputEvent* ev = rg_input_event_queue_push(&rig->events);
			if (ev)
			{
				ev->kind = RG_INPUT_EVENT_MOUSE_BUTTON_DOWN;
				ev->window_id = 1u;
				ev->data.button.button = (i8)button;
				ev->data.button.clicks = rig->press_clicks ? rig->press_clicks : 1u;
				ev->data.button.x = x;
				ev->data.button.y = y;
			}
		}
	}
	rg_gui_begin_frame_ex(rig->gui, in, &rig->events, 1u, 1.0f / 60.0f);
	rig->result = rg_gui_table_multi(rig->gui, &rig->table, table_rig_columns, rig->widths, 3u, rig->row_count,
	                                 rig->row_h, &rig->selection, &table_rig_ops, rig, rig->rect, 0x7AB1Eu,
	                                 table_rig_row, rig);
	rg_gui_end_frame(rig->gui);
	return rig->result;
}

static u32 table_rig_frame(TableRig* rig, f32 x, f32 y, int left, const RgKey* keys)
{
	return table_rig_frame_ex(rig, x, y, left, 0, keys);
}

/* Press and release the left button over view row `row` (as laid out last frame). */
static u32 table_rig_click(TableRig* rig, u32 row, const RgKey* keys)
{
	const RgGuiRect body = rig->table.panel.inner_rect;
	f32 x = body.x + 30.0f;
	f32 y = body.y + ((f32)row + 0.5f) * rig->row_h - rig->table.panel.scroll_y;
	u32 result = table_rig_frame(rig, x, y, 1, keys);
	(void)table_rig_frame(rig, x, y, 0, keys);
	return result;
}

/* Press and release the right button at (x, y). */
static u32 table_rig_right_click_at(TableRig* rig, f32 x, f32 y)
{
	u32 result = table_rig_frame_ex(rig, x, y, 0, 1, NULL);
	(void)table_rig_frame_ex(rig, x, y, 0, 0, NULL);
	return result;
}

static u32 table_rig_right_click(TableRig* rig, u32 row)
{
	const RgGuiRect body = rig->table.panel.inner_rect;
	return table_rig_right_click_at(rig, body.x + 30.0f, body.y + ((f32)row + 0.5f) * rig->row_h - rig->table.panel.scroll_y);
}

/* Press and release `key` (with `mods` held throughout) away from the table. */
static u32 table_rig_key(TableRig* rig, RgKey key, const RgKey* mods)
{
	RgKey down[8];
	u32 n = 0u, result;
	for (; mods && *mods != RG_KEY_UNKNOWN && n < 6u; mods++) down[n++] = *mods;
	down[n] = key;
	down[n + 1u] = RG_KEY_UNKNOWN;
	result = table_rig_frame(rig, 0.0f, 0.0f, 0, down);
	down[n] = RG_KEY_UNKNOWN;
	(void)table_rig_frame(rig, 0.0f, 0.0f, 0, down);
	return result;
}

static int table_rig_only(const TableRig* rig, u32 first, u32 last)
{
	u32 i;
	if (rig->selected_count != last - first + 1u) return 0;
	for (i = first; i <= last; i++)
		if (!rig->selected[i]) return 0;
	return 1;
}

static int test_gui_table(void)
{
	static TableRig rig;
	static const RgKey ctrl[] = {RG_KEY_LCTRL, RG_KEY_UNKNOWN};
	static const RgKey shift[] = {RG_KEY_LSHIFT, RG_KEY_UNKNOWN};
	int passed = 0;
	unsigned checks = 0u;
	void* memory = NULL;
	MonoProvider mono = {0};
	RgGuiTextProvider provider = mono_provider_make(&mono);
	RgGuiInitDesc desc = {0};
	u32 visible_rows;
	memset(&rig, 0, sizeof(rig));
	rig.gui = (RgGuiContext*)calloc(1u, sizeof(RgGuiContext));
	desc.text_provider = &provider;
	desc.max_draw_cmds = 1024u;
	desc.text_buffer_size = KB(16);
	TABLE_CHECK(rig.gui != NULL);
	size_t bytes = rg_gui_memory_required(&desc);
	memory = malloc(bytes);
	TABLE_CHECK(memory != NULL);
	RgArena arena = {(char*)memory, bytes, 0u, bytes};
	TABLE_CHECK(rg_gui_init(rig.gui, &arena, &desc));
	rg_input_init(&rig.input);
	rg_input_event_queue_init(&rig.events, rig.event_storage, 16u, rig.event_text, sizeof(rig.event_text));
	rig.rect = rg_gui_make_rect(10.0f, 10.0f, 400.0f, 300.0f);
	rig.row_h = 20.0f;
	rig.row_count = TABLE_ROWS;
	rig.widths[0] = 200.0f;
	rig.widths[1] = 80.0f;
	rig.widths[2] = 80.0f;
	rig.table.sort_column = -1;

	// Only the visible rows are produced, whatever the row count.
	table_rig_frame(&rig, -100.0f, -100.0f, 0, NULL);
	TABLE_CHECK(rig.table.panel.inner_rect.h > 100.0f);
	visible_rows = (u32)(rig.table.panel.inner_rect.h / rig.row_h);
	TABLE_CHECK(rig.row_calls >= visible_rows && rig.row_calls <= visible_rows + 2u);
	TABLE_CHECK(rig.row_min == 0u && rig.row_max < visible_rows + 2u);
	TABLE_CHECK(rig.selected_count == 0u && rig.result == RG_GUI_TABLE_RESULT_NONE);

	// A click selects one row, sets the cursor and focuses the table; a click
	// on the table while it has focus keeps it (with ordered events, the
	// end-of-frame rule used to drop the focus of a widget that was not active).
	TABLE_CHECK(table_rig_click(&rig, 3u, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(table_rig_only(&rig, 3u, 3u) && rig.selection.cursor == 3 && rig.selection.anchor == 3);
	TABLE_CHECK(rig.gui->focus_id != 0u);
	{
		RgGuiId table_focus = rig.gui->focus_id;
		TABLE_CHECK(table_rig_click(&rig, 4u, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
		TABLE_CHECK(rig.gui->focus_id == table_focus);
		TABLE_CHECK(table_rig_key(&rig, RG_KEY_UP, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
		TABLE_CHECK(table_rig_only(&rig, 3u, 3u) && rig.gui->focus_id == table_focus);
		// A click outside every widget still drops it.
		table_rig_frame(&rig, 600.0f, 600.0f, 1, NULL);
		table_rig_frame(&rig, 600.0f, 600.0f, 0, NULL);
		TABLE_CHECK(rig.gui->focus_id == 0u);
		(void)table_rig_click(&rig, 3u, NULL);
		TABLE_CHECK(rig.gui->focus_id == table_focus);
	}
	// Ctrl+click adds a row; Shift+click selects from the anchor (the last
	// Ctrl+clicked row) to the clicked row, replacing the rest.
	TABLE_CHECK(table_rig_click(&rig, 5u, ctrl) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(rig.selected_count == 2u && rig.selected[3] && rig.selected[5] && rig.selection.anchor == 5);
	TABLE_CHECK(table_rig_click(&rig, 8u, shift) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(table_rig_only(&rig, 5u, 8u) && rig.selection.cursor == 8 && rig.selection.anchor == 5);
	// Ctrl+click on a selected row deselects it.
	TABLE_CHECK(table_rig_click(&rig, 6u, ctrl) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(rig.selected_count == 3u && !rig.selected[6]);

	// Keys move the cursor and the selection; Shift extends from the anchor.
	TABLE_CHECK(table_rig_click(&rig, 2u, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(table_rig_key(&rig, RG_KEY_DOWN, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(table_rig_only(&rig, 3u, 3u) && rig.selection.cursor == 3);
	TABLE_CHECK(table_rig_key(&rig, RG_KEY_DOWN, shift) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(table_rig_key(&rig, RG_KEY_DOWN, shift) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(table_rig_only(&rig, 3u, 5u) && rig.selection.cursor == 5 && rig.selection.anchor == 3);
	TABLE_CHECK(table_rig_key(&rig, RG_KEY_UP, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(table_rig_only(&rig, 4u, 4u));

	// End goes to the last row and scrolls it into view; only rows near the
	// bottom are produced. Home comes back to the top.
	TABLE_CHECK(table_rig_key(&rig, RG_KEY_END, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(table_rig_only(&rig, TABLE_ROWS - 1u, TABLE_ROWS - 1u) && rig.selection.cursor == (int)TABLE_ROWS - 1);
	table_rig_frame(&rig, -100.0f, -100.0f, 0, NULL);
	TABLE_CHECK(rig.table.panel.scroll_y > (f32)(TABLE_ROWS - visible_rows - 2u) * rig.row_h);
	TABLE_CHECK(rig.row_max == TABLE_ROWS - 1u && rig.row_min > TABLE_ROWS - visible_rows - 3u);
	TABLE_CHECK(rig.row_calls <= visible_rows + 2u);
	TABLE_CHECK(table_rig_key(&rig, RG_KEY_HOME, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(table_rig_only(&rig, 0u, 0u) && rig.table.panel.scroll_y == 0.0f);
	// Page Down moves by about a page.
	TABLE_CHECK(table_rig_key(&rig, RG_KEY_PAGEDOWN, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
	TABLE_CHECK(rig.selection.cursor >= (int)visible_rows - 1 && rig.selection.cursor <= (int)visible_rows + 1);

	// Ctrl+A selects every row and keeps the cursor; Ctrl+Alt+A (AltGr on some
	// layouts) does not.
	{
		static const RgKey ctrl_alt[] = {RG_KEY_LCTRL, RG_KEY_LALT, RG_KEY_UNKNOWN};
		int cursor = rig.selection.cursor;
		TABLE_CHECK(table_rig_key(&rig, RG_KEY_A, ctrl_alt) == RG_GUI_TABLE_RESULT_NONE && rig.selected_count == 1u);
		TABLE_CHECK(table_rig_key(&rig, RG_KEY_A, ctrl) & RG_GUI_TABLE_RESULT_SELECTION);
		TABLE_CHECK(rig.selected_count == TABLE_ROWS && rig.selection.cursor == cursor);
		TABLE_CHECK(table_rig_key(&rig, RG_KEY_HOME, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
		TABLE_CHECK(table_rig_only(&rig, 0u, 0u));
	}

	// Enter activates the cursor row; Ctrl+Enter is left to the application.
	{
		static const RgKey ctrl_only[] = {RG_KEY_LCTRL, RG_KEY_UNKNOWN};
		rig.table.activated_row = -1;
		TABLE_CHECK(!(table_rig_key(&rig, RG_KEY_RETURN, ctrl_only) & RG_GUI_TABLE_RESULT_ACTIVATE));
		TABLE_CHECK((table_rig_key(&rig, RG_KEY_RETURN, NULL) & RG_GUI_TABLE_RESULT_ACTIVATE) && rig.table.activated_row == 0);
		TABLE_CHECK(table_rig_only(&rig, 0u, 0u));
	}
	// A double-click (the platform's click count) activates the row it hits; the
	// first click only selects.
	{
		u32 r = table_rig_click(&rig, 4u, NULL);
		TABLE_CHECK((r & RG_GUI_TABLE_RESULT_SELECTION) && !(r & RG_GUI_TABLE_RESULT_ACTIVATE));
		rig.press_clicks = 2u;
		r = table_rig_click(&rig, 4u, NULL);
		rig.press_clicks = 0u;
		TABLE_CHECK((r & RG_GUI_TABLE_RESULT_ACTIVATE) && rig.table.activated_row == 4 && table_rig_only(&rig, 4u, 4u));
	}

	// A right-click on an unselected row selects it alone; on a selected row it
	// keeps the selection and moves the cursor. Both report the row.
	{
		u32 r = table_rig_right_click(&rig, 6u);
		TABLE_CHECK((r & RG_GUI_TABLE_RESULT_CONTEXT) && (r & RG_GUI_TABLE_RESULT_SELECTION));
		TABLE_CHECK(rig.table.context_row == 6 && table_rig_only(&rig, 6u, 6u) && rig.selection.cursor == 6);
		TABLE_CHECK(table_rig_click(&rig, 8u, ctrl) & RG_GUI_TABLE_RESULT_SELECTION);
		r = table_rig_right_click(&rig, 6u);
		TABLE_CHECK((r & RG_GUI_TABLE_RESULT_CONTEXT) && !(r & RG_GUI_TABLE_RESULT_SELECTION));
		TABLE_CHECK(rig.table.context_row == 6 && rig.selected_count == 2u && rig.selected[6] && rig.selected[8]);
		TABLE_CHECK(rig.selection.cursor == 6);
		// Below the last row: row -1, the selection stays.
		rig.row_count = 10u;
		table_rig_frame(&rig, -100.0f, -100.0f, 0, NULL);
		r = table_rig_right_click_at(&rig, rig.table.panel.inner_rect.x + 30.0f,
		                             rig.table.panel.inner_rect.y + rig.table.panel.inner_rect.h - 4.0f);
		TABLE_CHECK((r & RG_GUI_TABLE_RESULT_CONTEXT) && rig.table.context_row == -1 && rig.selected_count == 2u);
		rig.row_count = TABLE_ROWS;
		// Left clicks never report a context request.
		r = table_rig_click(&rig, 4u, NULL);
		TABLE_CHECK(!(r & RG_GUI_TABLE_RESULT_CONTEXT) && table_rig_only(&rig, 4u, 4u));
	}

	// A table without focus ignores keys.
	rig.gui->focus_id = 0u;
	TABLE_CHECK(table_rig_key(&rig, RG_KEY_DOWN, NULL) == RG_GUI_TABLE_RESULT_NONE);
	TABLE_CHECK(rig.selected_count == 1u);

	// A header click sorts by that column; a second click reverses it. A column
	// without the sort flag does not sort.
	{
		f32 header_y = rig.rect.y + rig.gui->style.border_thickness + rig.row_h * 0.5f;
		f32 name_x = rig.table.panel.inner_rect.x + 30.0f;
		f32 kind_x = rig.table.panel.inner_rect.x + rig.widths[0] + rig.widths[1] + 30.0f;
		u32 r;
		table_rig_frame(&rig, name_x, header_y, 1, NULL);
		r = table_rig_frame(&rig, name_x, header_y, 0, NULL);
		TABLE_CHECK((r & RG_GUI_TABLE_RESULT_SORT) && rig.table.sort_column == 0 && !rig.table.sort_descending);
		table_rig_frame(&rig, name_x, header_y, 1, NULL);
		r = table_rig_frame(&rig, name_x, header_y, 0, NULL);
		TABLE_CHECK((r & RG_GUI_TABLE_RESULT_SORT) && rig.table.sort_column == 0 && rig.table.sort_descending);
		// The second click found the table focused and left it focused: a header
		// cell is a part with its own id, and the end-of-frame rule used to take
		// a click on it for a click elsewhere.
		TABLE_CHECK(rig.gui->focus_id != 0u);
		TABLE_CHECK(table_rig_key(&rig, RG_KEY_DOWN, NULL) & RG_GUI_TABLE_RESULT_SELECTION);
		// So does a press on the scrollbar.
		{
			RgGuiRect body = rig.table.panel.inner_rect;
			f32 bar_x = body.x + body.w + rig.gui->style.inner_spacing + rig.gui->style.scroll_bar_width * 0.5f;
			RgGuiId focus = rig.gui->focus_id;
			f32 scroll = rig.table.panel.scroll_y;
			table_rig_frame(&rig, bar_x, body.y + body.h * 0.5f, 1, NULL);
			table_rig_frame(&rig, bar_x, body.y + body.h * 0.5f, 0, NULL);
			TABLE_CHECK(rig.table.panel.scroll_y != scroll); // the press landed on the bar
			TABLE_CHECK(rig.gui->focus_id == focus);
		}
		table_rig_frame(&rig, kind_x, header_y, 1, NULL);
		r = table_rig_frame(&rig, kind_x, header_y, 0, NULL);
		TABLE_CHECK(!(r & RG_GUI_TABLE_RESULT_SORT) && rig.table.sort_column == 0);
		TABLE_CHECK(rig.selected_count == 1u); // header clicks leave the selection alone
	}

	// The wheel scrolls the hovered table (the scroll owner is last frame's
	// hovered panel) without touching the selection.
	{
		RgGuiRect body = rig.table.panel.inner_rect;
		f32 before = rig.table.panel.scroll_y;
		table_rig_frame(&rig, body.x + 30.0f, body.y + 30.0f, 0, NULL);
		rig.input.mouse_scroll_y = -3.0f;
		table_rig_frame(&rig, body.x + 30.0f, body.y + 30.0f, 0, NULL);
		rig.input.mouse_scroll_y = 0.0f;
		TABLE_CHECK(rig.table.panel.scroll_y > before && rig.selected_count == 1u);
	}

	passed = 1;
cleanup:
	if (!passed)
	{
		fprintf(stderr, "table tests failed after %u checks\n", checks);
	}
	else
	{
		printf("table tests: %u checks\n", checks);
	}
	free(memory);
	free(rig.gui);
	return passed;
}
#endif
