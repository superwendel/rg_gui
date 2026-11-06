// Opt-in text provider: measuring, wrapping, caret placement and the character
// width reference all go through one layout callback, and no font is needed.
#ifndef TEST_GUI_TEXT_PROVIDER_H
#define TEST_GUI_TEXT_PROVIDER_H
#define TEXT_PROVIDER_CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "Text provider check failed at line %d: %s\n", __LINE__, #condition); \
	goto cleanup; } checks++; } while (0)

typedef struct MonoProvider
{
	u32 generation;
	u32 layout_calls;
	u32 emitted;
	int rtl; /* lay the text out right-to-left: byte i at pen (n-1-i) units, emitted in visual order */
} MonoProvider;

/* Six pixels per codepoint at scale 1, five-by-eight ink quads, blanks without ink. */
static f32 mono_provider_layout(void* user, const char* text, size_t length, f32 scale,
                                RgGuiTextEmitFn emit, void* emit_user)
{
	MonoProvider* mono = (MonoProvider*)user;
	mono->layout_calls++;
	f32 pen = 0.0f;
	f32 max_width = 0.0f;
	f32 line_y = 0.0f;
	u32 line = 0u;
	size_t offset = 0u;
	if (mono->rtl)
	{
		/* One line of single-byte text, mirrored: visual position v holds byte n-1-v. */
		size_t v;
		for (v = 0u; v < length; v++)
		{
			size_t byte = length - 1u - v;
			RgGuiTextGlyph glyph;
			memset(&glyph, 0, sizeof(glyph));
			glyph.x = pen;
			glyph.w = 5.0f * scale;
			glyph.h = 8.0f * scale;
			glyph.u1 = 5u;
			glyph.v1 = 8u;
			glyph.pen_x = pen;
			glyph.advance = 6.0f * scale;
			glyph.cluster = (u32)byte;
			glyph.ink = text[byte] != ' ';
			if (emit)
			{
				mono->emitted++;
				if (!emit(emit_user, &glyph)) break;
			}
			pen += glyph.advance;
		}
		return pen;
	}
	while (offset < length)
	{
		size_t cluster = offset;
		u32 cp = rg_text_decode_utf8(text, length, &offset);
		if (cp == '\n')
		{
			if (pen > max_width) max_width = pen;
			pen = 0.0f;
			line_y += 10.0f * scale;
			line++;
			continue;
		}
		RgGuiTextGlyph glyph;
		memset(&glyph, 0, sizeof(glyph));
		glyph.x = pen;
		glyph.y = line_y;
		glyph.w = 5.0f * scale;
		glyph.h = 8.0f * scale;
		glyph.u1 = 5u;
		glyph.v1 = 8u;
		glyph.pen_x = pen;
		glyph.advance = 6.0f * scale;
		glyph.cluster = (u32)cluster;
		glyph.line = line;
		glyph.ink = cp != ' ';
		/* '~' stands for a spacing vowel sign and U+0301 is a combining acute: no caret stop before either */
		glyph.continues = cp == '~' || cp == 0x0301u;
		if (emit)
		{
			mono->emitted++;
			if (!emit(emit_user, &glyph)) break;
		}
		pen += glyph.advance;
	}
	return pen > max_width ? pen : max_width;
}

/* RTL caret and hit test for the mirrored layout: offset b sits at (n-b) units. */
static f32 mono_rtl_caret_x(void* user, const char* text, size_t length, f32 scale, size_t byte_offset)
{
	(void)user;
	(void)text;
	return (f32)(length - byte_offset) * 6.0f * scale;
}

static size_t mono_rtl_hit_test(void* user, const char* text, size_t length, f32 scale, f32 x, f32* out_caret_x)
{
	f32 unit = 6.0f * scale;
	f32 from_right = (f32)length * unit - x;
	size_t b;
	(void)user;
	(void)text;
	b = from_right <= 0.0f ? 0u : (size_t)(from_right / unit + 0.5f);
	if (b > length) b = length;
	if (out_caret_x) *out_caret_x = (f32)(length - b) * unit;
	return b;
}

/* Left-to-right mono text where byte 30 has a second place on screen, as at a
 * direction boundary: caret_x puts it at the line start, while a click on its
 * own spot (180 px at scale 1) reports that spot. */
static f32 mono_split_caret_x(void* user, const char* text, size_t length, f32 scale, size_t byte_offset)
{
	(void)user;
	(void)text;
	(void)length;
	return byte_offset == 30u ? 0.0f : (f32)byte_offset * 6.0f * scale;
}

static size_t mono_split_hit_test(void* user, const char* text, size_t length, f32 scale, f32 x, f32* out_caret_x)
{
	f32 unit = 6.0f * scale;
	size_t b = x <= 0.0f ? 0u : (size_t)(x / unit + 0.5f);
	(void)user;
	(void)text;
	if (b > length) b = length;
	if (out_caret_x) *out_caret_x = (f32)b * unit;
	return b;
}

static f32 mono_provider_line_height(void* user, f32 scale)
{
	(void)user;
	return 10.0f * scale;
}

static u32 mono_provider_generation(void* user)
{
	return ((MonoProvider*)user)->generation;
}

static RgGuiTextProvider mono_provider_make(MonoProvider* mono)
{
	RgGuiTextProvider provider;
	memset(&provider, 0, sizeof(provider));
	provider.user = mono;
	provider.layout = mono_provider_layout;
	provider.line_height = mono_provider_line_height;
	provider.atlas_generation = mono_provider_generation;
	provider.atlas_width = 16u;
	provider.atlas_height = 16u;
	return provider;
}

static int test_text_provider(void)
{
	int passed = 0;
	unsigned checks = 0u;
	void* memory = NULL;
	MonoProvider mono = {0};
	RgGuiTextProvider provider = mono_provider_make(&mono);
	RgGuiContext* gui = (RgGuiContext*)calloc(1u, sizeof(RgGuiContext));
	RgGuiInitDesc desc = {0};
	desc.text_provider = &provider;
	desc.max_draw_cmds = 64u;
	desc.text_buffer_size = KB(4);
	TEXT_PROVIDER_CHECK(gui != NULL);
	size_t bytes = rg_gui_memory_required(&desc);
	TEXT_PROVIDER_CHECK(bytes > 0u);
	memory = malloc(bytes);
	TEXT_PROVIDER_CHECK(memory != NULL);
	RgArena arena = {(char*)memory, bytes, 0u, bytes};
	TEXT_PROVIDER_CHECK(rg_gui_init(gui, &arena, &desc));

	f32 scale = gui->style.text_height / 10.0f;
	f32 unit = 6.0f * scale;
	TEXT_PROVIDER_CHECK(gui->font == NULL);
	TEXT_PROVIDER_CHECK(fabsf(rg_gui_text_base_scale(gui) - scale) < 1e-6f);
	TEXT_PROVIDER_CHECK(fabsf(gui->style.char_width - unit) < 1e-4f);
	TEXT_PROVIDER_CHECK(fabsf(rg_gui_text_measure_prefix(gui, "abc", 3u) - 3.0f * unit) < 1e-4f);
	TEXT_PROVIDER_CHECK(fabsf(rg_gui_text_measure_prefix(gui, "ab\ncdef", 7u) - 4.0f * unit) < 1e-4f);
	TEXT_PROVIDER_CHECK(rg_gui_text_area_cache_key(gui, 100.0f).provider_user == &mono);

	static const char words[] = "aaaa bbbb cccc";
	size_t len = sizeof(words) - 1u;
	// Nine glyphs fit and the blank at byte 9 overflows: break after that blank.
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 9.5f * unit) == 10u);
	// Four glyphs fit and the blank at byte 4 overflows.
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 4.5f * unit) == 5u);
	// No blank before the overflow: the widest fitting prefix.
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 3.5f * unit) == 3u);
	// The first glyph alone overflows: still make progress.
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 1.0f) == 1u);
	// Everything fits.
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 14.0f * unit) == len);
	// A later segment: results are offsets into the whole buffer.
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 5u, len, 4.5f * unit) == 10u);
	// Wrapping stops laying out at the first glyph that does not fit.
	mono.emitted = 0u;
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 2.5f * unit) == 2u);
	TEXT_PROVIDER_CHECK(mono.emitted == 3u);

	// Caret placement rounds to the nearest boundary.
	TEXT_PROVIDER_CHECK(rg_gui_text_area_pick_column(gui, words, len, 2.9f * unit) == 3u);
	TEXT_PROVIDER_CHECK(rg_gui_text_area_pick_column(gui, words, len, 2.4f * unit) == 2u);
	// Without provider carets, offsets are prefix widths.
	TEXT_PROVIDER_CHECK(fabsf(rg_gui_text_caret_x(gui, words, len, 4u) - 4.0f * unit) < 1e-4f);
	TEXT_PROVIDER_CHECK(fabsf(rg_gui_text_offset_x(gui, words, len, 5u, 9u) - 4.0f * unit) < 1e-4f);

	// Right-to-left text through the provider's caret and hit test: the logical
	// start is at the right edge, so a click at the left edge is the end.
	mono.rtl = 1;
	gui->text_provider.caret_x = mono_rtl_caret_x;
	gui->text_provider.hit_test = mono_rtl_hit_test;
	TEXT_PROVIDER_CHECK(fabsf(rg_gui_text_caret_x(gui, words, len, 0u) - (f32)len * unit) < 1e-4f);
	TEXT_PROVIDER_CHECK(fabsf(rg_gui_text_caret_x(gui, words, len, len) - 0.0f) < 1e-4f);
	TEXT_PROVIDER_CHECK(fabsf(rg_gui_text_offset_x(gui, words, len, 5u, 5u) - 9.0f * unit) < 1e-4f);
	{
		RgGuiRect field = rg_gui_make_rect(10.0f, 0.0f, 400.0f, 20.0f);
		f32 origin = field.x + gui->style.padding;
		gui->mouse_pos = rg_vec2(origin + 0.5f, 5.0f);
		TEXT_PROVIDER_CHECK(rg_gui_text_edit_pick_cursor(gui, words, field) == len);
		gui->mouse_pos = rg_vec2(origin - 5.0f, 5.0f); /* left of the text: still the logical end */
		TEXT_PROVIDER_CHECK(rg_gui_text_edit_pick_cursor(gui, words, field) == len);
		gui->mouse_pos = rg_vec2(origin + (f32)len * unit - 0.5f, 5.0f);
		TEXT_PROVIDER_CHECK(rg_gui_text_edit_pick_cursor(gui, words, field) == 0u);
		gui->mouse_pos = rg_vec2(origin + 4.0f * unit, 5.0f); /* 4 units from the left: 10 from the right */
		TEXT_PROVIDER_CHECK(rg_gui_text_edit_pick_cursor(gui, words, field) == len - 4u);
	}
	TEXT_PROVIDER_CHECK(rg_gui_text_area_pick_column(gui, words, len, 0.5f) == len);
	TEXT_PROVIDER_CHECK(rg_gui_text_area_pick_column(gui, words, len, -2.0f) == len);
	// Wrapping a reordered line measures logical prefixes (each wrapped line is laid
	// out on its own), so the breaks match the left-to-right ones and every line fits.
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 9.5f * unit) == 10u);
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 4.5f * unit) == 5u);
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 3.5f * unit) == 3u);
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 1.0f) == 1u);
	TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 14.0f * unit) == len);
	{
		size_t end = rg_gui_text_area_wrap_line_end(gui, words, 0u, len, 1.7f * unit);
		TEXT_PROVIDER_CHECK(end == 1u && rg_gui_text_measure_prefix(gui, words, end) <= 1.7f * unit);
	}
	mono.rtl = 0;
	gui->text_provider.caret_x = NULL;
	gui->text_provider.hit_test = NULL;

	// Caret stops: '~' continues the previous grapheme, line breaks are stops.
	{
		static const char sign[] = "a~b";
		static const char lines[] = "ab\ncd";
		static const char crlf[] = "ab\r\ncd";
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, sign, 3u, 0u, 1) == 2u);
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, sign, 3u, 3u, -1) == 2u);
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, sign, 3u, 2u, -1) == 0u);
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, sign, 3u, 1u, 0) == 0u);
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, sign, 3u, 2u, 1) == 3u);
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, lines, 5u, 3u, -1) == 2u);
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, lines, 5u, 2u, 1) == 3u);
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, lines, 5u, 2u, -1) == 1u);
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, crlf, 6u, 4u, -1) == 2u);
		TEXT_PROVIDER_CHECK(rg_gui_text_caret_stop(gui, crlf, 6u, 2u, 1) == 4u);
		// Wrap breaks never split a sign from its base: at 1.5 units the first
		// grapheme "a~" is the unavoidable unit, at 2.5 units it is the fitting one.
		static const char signs[] = "a~a~a~";
		TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, signs, 0u, 6u, 1.5f * unit) == 2u);
		TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, signs, 0u, 6u, 2.5f * unit) == 2u);
		TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, signs, 0u, 6u, 4.5f * unit) == 4u);
		TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, signs, 0u, 6u, 0.0f) == 2u);
		// A break after a blank is a caret stop too: in "a" blank acute "b" the acute
		// continues the blank, so the line keeps all three and breaks before b (byte 4),
		// not between the blank and its mark (byte 2); otherwise an earlier blank.
		static const char blank_mark[] = "a \xCC\x81" "b";
		TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, blank_mark, 0u, 5u, 3.5f * unit) == 4u);
		static const char blanks[] = "ab cd \xCC\x81" "e";
		TEXT_PROVIDER_CHECK(rg_gui_text_area_wrap_line_end(gui, blanks, 0u, 9u, 7.5f * unit) == 3u);
		// The scroll window: spans are measured on their own and end on stops.
		TEXT_PROVIDER_CHECK(rg_gui_text_end_for_width(gui, signs, 6u, 0u, 3.5f * unit) == 2u);
		TEXT_PROVIDER_CHECK(rg_gui_text_end_for_width(gui, signs, 6u, 2u, 4.0f * unit) == 6u);
		TEXT_PROVIDER_CHECK(rg_gui_text_start_for_cursor_width(gui, signs, 6u, 0u, 6u, 2.5f * unit) == 4u);
		TEXT_PROVIDER_CHECK(fabsf(rg_gui_text_measure_span(gui, signs, 2u, 5u) - 3.0f * unit) < 1e-4f);
	}

	// A provider without layout is refused, and the bitmap default still needs a font.
	RgGuiTextProvider broken = provider;
	broken.layout = NULL;
	RgGuiInitDesc bad = desc;
	bad.text_provider = &broken;
	TEXT_PROVIDER_CHECK(rg_gui_memory_required(&bad) == 0u);
	bad.text_provider = NULL;
	TEXT_PROVIDER_CHECK(rg_gui_memory_required(&bad) == 0u);
	passed = 1;

cleanup:
	if (passed)
	{
		printf("text provider checks passed (%u)\n", checks);
	}
	free(memory);
	free(gui);
	return passed;
}

// A shaped field is a window onto one layout of the line. Dragging past either
// edge scrolls that window toward the mouse, monotonically, until the line's end
// on that side shows, and the caret goes to the text at that edge; right-to-left
// text scrolls the same way on screen. The mono provider is 6 px per byte.
static int test_text_provider_drag_one(int rtl, unsigned* out_checks)
{
	int passed = 0;
	unsigned checks = 0u;
	void* memory = NULL;
	MonoProvider mono = {0};
	mono.rtl = rtl;
	RgGuiTextProvider provider = mono_provider_make(&mono);
	if (rtl)
	{
		provider.caret_x = mono_rtl_caret_x;
		provider.hit_test = mono_rtl_hit_test;
	}
	RgGuiContext* gui = (RgGuiContext*)calloc(1u, sizeof(RgGuiContext));
	RgGuiInitDesc desc = {0};
	desc.text_provider = &provider;
	desc.max_draw_cmds = 128u;
	desc.text_buffer_size = KB(4);
	TEXT_PROVIDER_CHECK(gui != NULL);
	size_t bytes = rg_gui_memory_required(&desc);
	memory = malloc(bytes);
	TEXT_PROVIDER_CHECK(memory != NULL);
	RgArena arena = {(char*)memory, bytes, 0u, bytes};
	TEXT_PROVIDER_CHECK(rg_gui_init(gui, &arena, &desc));
	gui->style.text_height = 10.0f; /* 6 px per byte */
	gui->style.padding = 0.0f;
	gui->style.border_thickness = 0.0f;
	{
		char buffer[64];
		memset(buffer, 'a', 40u);
		buffer[40] = '\0';
		RgGuiRect rect = rg_gui_make_rect(20, 20, 80, 20); /* 240 px of text, 80 px shown */
		const f32 max_view = 160.0f;
		RgInputState input;
		rg_input_init(&input);
		RgInputEvent event_storage[8];
		char event_text[32];
		RgInputEventQueue events;
		rg_input_event_queue_init(&events, event_storage, RG_ARRAY_COUNT(event_storage), event_text, sizeof(event_text));
		RgGuiTextEditState* edit = gui->text_edit_state;
		// At rest the field shows the logical start: the left end, or for
		// right-to-left text the right end. A click maps onto what is shown.
		for (int frame = 0; frame < 2; frame++)
		{
			text_capture_input_frame(&input, &events, frame == 0 ? 0 : (rtl ? 99 : 21), 25, frame);
			rg_gui_begin_frame_ex(gui, &input, &events, 0u, 1.0f / 60.0f);
			(void)rg_gui_text_input(gui, NULL, buffer, sizeof(buffer), rect, 0x7133u, NULL);
			rg_gui_end_frame(gui);
		}
		TEXT_PROVIDER_CHECK(edit->active && edit->cursor == 0u);
		TEXT_PROVIDER_CHECK(edit->view_x == (rtl ? max_view : 0.0f));
		// Toward the side the text starts on: nothing more to show, nothing moves.
		for (int frame = 0; frame < 10; frame++)
		{
			text_capture_input_frame(&input, &events, rtl ? 102 : 18, 25, 1);
			rg_gui_begin_frame_ex(gui, &input, &events, 0u, 1.0f / 60.0f);
			(void)rg_gui_text_input(gui, NULL, buffer, sizeof(buffer), rect, 0x7133u, NULL);
			rg_gui_end_frame(gui);
			TEXT_PROVIDER_CHECK(edit->cursor == 0u && edit->view_x == (rtl ? max_view : 0.0f));
		}
		// Then 40 px past the other edge and back: the view moves one way only.
		for (int leg = 0; leg < 2; leg++)
		{
			int toward_end = leg == 0;
			int mouse_x = (toward_end == !rtl) ? 140 : -20;
			f32 last_view = edit->view_x;
			int redraws = 0;
			for (int frame = 0; frame < 60; frame++)
			{
				text_capture_input_frame(&input, &events, mouse_x, 25, 1);
				rg_gui_begin_frame_ex(gui, &input, &events, 0u, 1.0f / 60.0f);
				(void)rg_gui_text_input(gui, NULL, buffer, sizeof(buffer), rect, 0x7133u, NULL);
				rg_gui_end_frame(gui);
				TEXT_PROVIDER_CHECK(mouse_x > 100 ? edit->view_x >= last_view : edit->view_x <= last_view);
				redraws += rg_gui_platform_output(gui)->wants_redraw;
				last_view = edit->view_x;
			}
			TEXT_PROVIDER_CHECK(edit->view_x == (mouse_x > 100 ? max_view : 0.0f));
			TEXT_PROVIDER_CHECK(edit->cursor == (toward_end ? 40u : 0u));
			TEXT_PROVIDER_CHECK(redraws > 0 && !rg_gui_platform_output(gui)->wants_redraw);
		}
		TEXT_PROVIDER_CHECK(edit->selection_anchor == 0u);
	}
	passed = 1;
	*out_checks += checks;

cleanup:
	free(memory);
	free(gui);
	return passed;
}

// A click keeps the caret on the side it chose: releasing the button must not
// scroll to the offset's other place on screen. With no padding the End caret
// stays inside the field's clip.
static int test_text_provider_click_side(unsigned* out_checks)
{
	int passed = 0;
	unsigned checks = 0u;
	void* memory = NULL;
	MonoProvider mono = {0};
	RgGuiTextProvider provider = mono_provider_make(&mono);
	provider.caret_x = mono_split_caret_x;
	provider.hit_test = mono_split_hit_test;
	RgGuiContext* gui = (RgGuiContext*)calloc(1u, sizeof(RgGuiContext));
	RgGuiInitDesc desc = {0};
	desc.text_provider = &provider;
	desc.max_draw_cmds = 128u;
	desc.text_buffer_size = KB(4);
	TEXT_PROVIDER_CHECK(gui != NULL);
	size_t bytes = rg_gui_memory_required(&desc);
	memory = malloc(bytes);
	TEXT_PROVIDER_CHECK(memory != NULL);
	RgArena arena = {(char*)memory, bytes, 0u, bytes};
	TEXT_PROVIDER_CHECK(rg_gui_init(gui, &arena, &desc));
	gui->style.text_height = 10.0f;
	gui->style.padding = 0.0f;
	gui->style.border_thickness = 0.0f;
	{
		char buffer[64];
		memset(buffer, 'a', 40u);
		buffer[40] = '\0';
		RgGuiRect rect = rg_gui_make_rect(20, 20, 80, 20);
		RgInputState input;
		rg_input_init(&input);
		RgInputEvent event_storage[8];
		char event_text[32];
		RgInputEventQueue events;
		rg_input_event_queue_init(&events, event_storage, RG_ARRAY_COUNT(event_storage), event_text, sizeof(event_text));
		RgGuiTextEditState* edit = gui->text_edit_state;
		// Rest, press at the start, drag 40 px past the right edge to the end, release.
		static const int xs[] = {0, 21, 140, 140};
		static const int downs[] = {0, 1, 1, 0};
		for (int step = 0; step < 4; step++)
		{
			for (int frame = 0; frame < (step == 2 ? 60 : 1); frame++)
			{
				text_capture_input_frame(&input, &events, xs[step], 25, downs[step]);
				rg_gui_begin_frame_ex(gui, &input, &events, 0u, 1.0f / 60.0f);
				(void)rg_gui_text_input(gui, NULL, buffer, sizeof(buffer), rect, 0x7134u, NULL);
				rg_gui_end_frame(gui);
			}
		}
		TEXT_PROVIDER_CHECK(edit->cursor == 40u && edit->view_x == 160.0f);
		{
			RgGuiRect caret = rg_gui_platform_output(gui)->ime_caret_rect;
			TEXT_PROVIDER_CHECK(caret.x >= rect.x && caret.x + caret.w <= rect.x + rect.w); /* inside the clip */
		}
		// Click byte 30 where it is drawn (20 px into the field, 180 px into the line)
		// and release there: the view stays, and so does the caret.
		for (int frame = 0; frame < 2; frame++)
		{
			text_capture_input_frame(&input, &events, 40, 25, frame == 0);
			rg_gui_begin_frame_ex(gui, &input, &events, 0u, 1.0f / 60.0f);
			(void)rg_gui_text_input(gui, NULL, buffer, sizeof(buffer), rect, 0x7134u, NULL);
			rg_gui_end_frame(gui);
			TEXT_PROVIDER_CHECK(edit->cursor == 30u && edit->view_x == 160.0f);
			TEXT_PROVIDER_CHECK(rg_gui_platform_output(gui)->ime_caret_rect.x == 40.0f);
		}
		// A text scale change (DPI) moves the glyph: the click is scaled and hit-tested
		// again, so the caret follows byte 30 to 360 px, and back at the old scale.
		for (int pass = 0; pass < 2; pass++)
		{
			f32 hint = 0.0f;
			gui->style.text_height = pass == 0 ? 20.0f : 10.0f;
			text_capture_input_frame(&input, &events, 40, 25, 0);
			rg_gui_begin_frame_ex(gui, &input, &events, 0u, 1.0f / 60.0f);
			(void)rg_gui_text_input(gui, NULL, buffer, sizeof(buffer), rect, 0x7134u, NULL);
			rg_gui_end_frame(gui);
			TEXT_PROVIDER_CHECK(edit->cursor == 30u && rg_gui_text_edit_caret_hint(edit, &hint));
			TEXT_PROVIDER_CHECK(hint == (pass == 0 ? 360.0f : 180.0f));
		}
		TEXT_PROVIDER_CHECK(edit->view_x == 160.0f);
		// Snapshot input (no event queue) handles every held key in one call: Left
		// and Right down together leave the cursor on byte 30, and the hint is gone.
		{
			f32 hint = 0.0f;
			rg_input_begin_frame(&input);
			rg_input_set_key(&input, RG_KEY_LEFT, true);
			rg_input_set_key(&input, RG_KEY_RIGHT, true);
			rg_gui_begin_frame_ex(gui, &input, NULL, 0u, 1.0f / 60.0f);
			(void)rg_gui_text_input(gui, NULL, buffer, sizeof(buffer), rect, 0x7134u, NULL);
			rg_gui_end_frame(gui);
			rg_input_set_key(&input, RG_KEY_LEFT, false);
			rg_input_set_key(&input, RG_KEY_RIGHT, false);
			TEXT_PROVIDER_CHECK(edit->cursor == 30u && !rg_gui_text_edit_caret_hint(edit, &hint));
			TEXT_PROVIDER_CHECK(edit->view_x == 0.0f);
		}
		// Click byte 30 again: scroll back to the end by dragging, release, click.
		{
			static const int xs2[] = {99, 140, 140, 40, 40}; /* press inside, drag out, release, click */
			static const int downs2[] = {1, 1, 0, 1, 0};
			for (int step = 0; step < 5; step++)
			{
				for (int frame = 0; frame < (step == 1 ? 60 : 1); frame++)
				{
					text_capture_input_frame(&input, &events, xs2[step], 25, downs2[step]);
					rg_gui_begin_frame_ex(gui, &input, &events, 0u, 1.0f / 60.0f);
					(void)rg_gui_text_input(gui, NULL, buffer, sizeof(buffer), rect, 0x7134u, NULL);
					rg_gui_end_frame(gui);
				}
			}
			f32 hint = 0.0f;
			TEXT_PROVIDER_CHECK(edit->cursor == 30u && edit->view_x == 160.0f && rg_gui_text_edit_caret_hint(edit, &hint));
		}
		// Ordered events: Left then Right in one frame brings the cursor back to byte 30 by keyboard:
		// the click's side is gone for good, and the view follows caret_x (0) again.
		{
			static const SDL_Scancode keys[] = {SDL_SCANCODE_LEFT, SDL_SCANCODE_RIGHT};
			text_capture_input_frame(&input, &events, 40, 25, 0);
			for (int k = 0; k < 2; k++)
			{
				SDL_Event key = {0};
				key.type = SDL_EVENT_KEY_DOWN;
				key.key.scancode = keys[k];
				key.key.down = true;
				rg_input_process_event_ex(&input, &key, &events);
				key.type = SDL_EVENT_KEY_UP;
				key.key.down = false;
				rg_input_process_event_ex(&input, &key, &events);
			}
			rg_gui_begin_frame_ex(gui, &input, &events, 0u, 1.0f / 60.0f);
			(void)rg_gui_text_input(gui, NULL, buffer, sizeof(buffer), rect, 0x7134u, NULL);
			rg_gui_end_frame(gui);
			f32 hint = 0.0f;
			TEXT_PROVIDER_CHECK(edit->cursor == 30u && !rg_gui_text_edit_caret_hint(edit, &hint));
			TEXT_PROVIDER_CHECK(edit->view_x == 0.0f);
		}
	}
	passed = 1;
	*out_checks += checks;

cleanup:
	free(memory);
	free(gui);
	return passed;
}

static int test_text_provider_drag(void)
{
	unsigned checks = 0u;
	if (!test_text_provider_drag_one(0, &checks) || !test_text_provider_drag_one(1, &checks) ||
	    !test_text_provider_click_side(&checks))
	{
		return 0;
	}
	printf("text provider drag checks passed (%u)\n", checks);
	return 1;
}

#endif
