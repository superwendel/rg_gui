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
		if (emit)
		{
			mono->emitted++;
			if (!emit(emit_user, &glyph)) break;
		}
		pen += glyph.advance;
	}
	return pen > max_width ? pen : max_width;
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

#endif
