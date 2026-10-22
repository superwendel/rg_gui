// Optional shared ASCII lookup table regression tests. Included by test_gui.c.
static unsigned lookup_test_checks;
#define LOOKUP_TEST_CHECK(x) do { lookup_test_checks++; if (!(x)) { fprintf(stderr, "FAIL line %d: %s\n", __LINE__, #x); exit(1); } } while (0)

static void lookup_test_measure(RgGuiContext* ctx, const char* text, size_t length)
{
    f32 expected = rg_text_measure(ctx->font, text, length, rg_gui_text_base_scale(ctx)).width;
    f32 actual = rg_gui_text_measure_prefix(ctx, text, length);
    LOOKUP_TEST_CHECK(memcmp(&expected, &actual, sizeof(expected)) == 0);
}

static void lookup_test_font(RgTextFont* font)
{
    RgGuiTextLookup* lookup = (RgGuiTextLookup*)malloc(sizeof(*lookup));
    LOOKUP_TEST_CHECK(lookup);
    LOOKUP_TEST_CHECK(rg_gui_text_lookup_init(lookup, font));
    LOOKUP_TEST_CHECK(lookup->font == font);
    RgGuiContext ctx;
    memset(&ctx, 0, sizeof(ctx));
    ctx.font = font;
    ctx.text_lookup = lookup;
    for (u32 left = 0u; left < 128u; left++) {
        LOOKUP_TEST_CHECK(lookup->ascii_glyphs[left] == rg_text_find_glyph(font, left));
        LOOKUP_TEST_CHECK(rg_gui_text_find_glyph(&ctx, left) == rg_text_find_glyph(font, left));
        for (u32 right = 0u; right < 128u; right++) {
            LOOKUP_TEST_CHECK(lookup->ascii_kerning[left * 128u + right] == rg_text_find_kerning(font, left, right));
            LOOKUP_TEST_CHECK(rg_gui_text_find_kerning(&ctx, left, right) == rg_text_find_kerning(font, left, right));
        }
        LOOKUP_TEST_CHECK(rg_gui_text_find_kerning(&ctx, left, 0xFFFDu) == rg_text_find_kerning(font, left, 0xFFFDu));
        LOOKUP_TEST_CHECK(rg_gui_text_find_kerning(&ctx, 0xE9u, left) == rg_text_find_kerning(font, 0xE9u, left));
    }
    for (u32 cp = 128u; cp < 0x20000u; cp += 117u) {
        LOOKUP_TEST_CHECK(rg_gui_text_find_glyph(&ctx, cp) == rg_text_find_glyph(font, cp));
    }
    /* Every glyph codepoint, neighbors, and pairs with non-ASCII members must
     * match rg_text through the general tables and through their disabled path. */
    for (u32 i = 0u; i < font->glyph_count; i++) {
        u32 cp = font->glyphs[i].codepoint;
        for (u32 delta = 0u; delta < 3u; delta++) {
            LOOKUP_TEST_CHECK(rg_gui_text_find_glyph(&ctx, cp + delta) == rg_text_find_glyph(font, cp + delta));
            LOOKUP_TEST_CHECK(rg_gui_text_lookup_glyph(lookup, cp + delta) == rg_text_find_glyph(font, cp + delta));
        }
        for (u32 j = 0u; j < font->glyph_count; j++) {
            u32 other = font->glyphs[j].codepoint;
            LOOKUP_TEST_CHECK(rg_gui_text_find_kerning(&ctx, cp, other) == rg_text_find_kerning(font, cp, other));
            LOOKUP_TEST_CHECK(rg_gui_text_find_kerning(&ctx, cp, other + 1u) == rg_text_find_kerning(font, cp, other + 1u));
        }
    }
    for (u32 i = 0u; font->kernings && i < font->kerning_count; i++) {
        u32 left = font->kernings[i].left, right = font->kernings[i].right;
        LOOKUP_TEST_CHECK(rg_gui_text_lookup_kerning(lookup, left, right) == rg_text_find_kerning(font, left, right));
        LOOKUP_TEST_CHECK(rg_gui_text_lookup_kerning(lookup, right, left) == rg_text_find_kerning(font, right, left));
        LOOKUP_TEST_CHECK(rg_gui_text_lookup_kerning(lookup, left, 0x10FFFFu) == rg_text_find_kerning(font, left, 0x10FFFFu));
    }
    LOOKUP_TEST_CHECK(rg_gui_text_lookup_glyph(lookup, 0x10FFFFu) == rg_text_find_glyph(font, 0x10FFFFu));
    LOOKUP_TEST_CHECK(rg_gui_text_lookup_glyph(lookup, font->fallback_codepoint) == rg_text_find_glyph(font, font->fallback_codepoint));
    static const char* corpus[] = {
        "", "A", "AA", "A B", "A\tB", "A?B", "AXAXB", "A\rB", "A\nB", "A\r\nB",
        "\n\r\n\r", "A\xC3\xA9" "A", "\xEF\xBF\xBD" "AA", "A\xF0\x9F\x98\x80" "B",
        "A\xFF" "AB", "A\xC0\x80" "AB", "A\xED\xA0\x80" "AB", "A\xF4\x90\x80\x80" "AB"
    };
    static const f32 heights[] = {0.0f, -0.0f, 16.0f, 8.0f, 11.2f, -8.0f, 8000.0f, 0.001f, INFINITY, NAN};
    for (size_t scale = 0u; scale < sizeof(heights) / sizeof(heights[0]); scale++) {
        ctx.style.text_height = heights[scale];
        for (size_t item = 0u; item < sizeof(corpus) / sizeof(corpus[0]); item++) {
            for (size_t prefix = 0u; prefix <= strlen(corpus[item]); prefix++) lookup_test_measure(&ctx, corpus[item], prefix);
        }
        char bytes[128];
        for (u32 seed = 0u; seed < 64u; seed++) {
            u32 state = seed + 1234u;
            for (size_t i = 0u; i < sizeof(bytes); i++) {
                state = state * 1664525u + 1013904223u;
                bytes[i] = (char)(state >> 24u);
            }
            for (size_t length = 0u; length <= sizeof(bytes); length += 8u) lookup_test_measure(&ctx, bytes, length);
        }
    }
    ctx.style.text_height = 16.0f;
    ctx.text_lookup = NULL;
    lookup_test_measure(&ctx, "A?B", 3u);
    LOOKUP_TEST_CHECK(rg_gui_text_find_glyph(&ctx, 'A') == rg_text_find_glyph(font, 'A'));
    LOOKUP_TEST_CHECK(rg_gui_text_find_kerning(&ctx, 'A', '?') == rg_text_find_kerning(font, 'A', '?'));
    RgTextFont other = *font;
    ctx.font = &other;
    ctx.text_lookup = lookup;
    lookup_test_measure(&ctx, "A?B", 3u);
    LOOKUP_TEST_CHECK(rg_gui_text_find_glyph(&ctx, 'A') == rg_text_find_glyph(&other, 'A'));
    LOOKUP_TEST_CHECK(rg_gui_text_find_kerning(&ctx, 'A', '?') == rg_text_find_kerning(&other, 'A', '?'));
    ctx.font = NULL;
    lookup_test_measure(&ctx, "A?B", 3u);
    lookup_test_measure(&ctx, NULL, 3u);
    free(lookup);
}

static int test_text_lookup(void)
{
    RgTextGlyph glyphs[] = {
        {65u, 0, 0, 1, 1, 0, 0, 0, 0u, 0u},
        {0xFFFDu, 0, 0, 1, 1, 0, 0, 8, 0u, 0u},
        {63u, 0, 0, 1, 1, 0, 0, 9, 0u, 0u},
        {0u, 0, 0, 0, 0, 0, 0, -12, 0u, 0u},
        {127u, 0, 0, 1, 1, 0, 0, 1, 0u, 0u},
        {65u, 0, 0, 1, 1, 0, 0, 99, 0u, 0u},
        {0xFFFDu, 0, 0, 1, 1, 0, 0, 51, 0u, 0u},
        {66u, 0, 0, 1, 1, 0, 0, -3, 0u, 0u},
        {0xE9u, 0, 0, 1, 1, 0, 0, 13, 0u, 0u}
    };
    RgTextKerning pairs[] = {
        {65u, 63u, 0}, {127u, 0u, -3}, {65u, 63u, 8},
        {0u, 65u, 2}, {65u, 128u, -9}, {128u, 65u, 9},
        {65u, 65u, -1}, {65u, 65u, 0}, {127u, 0u, 7},
        {65u, 0xFFFDu, 1}, {0xE9u, 65u, -2}
    };
    RgTextFont font = {0};
    font.metrics.atlas_width = 64u;
    font.metrics.atlas_height = 64u;
    font.metrics.line_height = 16;
    font.glyphs = glyphs;
    font.glyph_count = (u32)(sizeof(glyphs) / sizeof(glyphs[0]));
    font.kernings = pairs;
    font.kerning_count = (u32)(sizeof(pairs) / sizeof(pairs[0]));
    const u32 fallbacks[] = {63u, 0xFFFDu, 0u, UINT32_MAX, 65u};
    for (size_t i = 0u; i < sizeof(fallbacks) / sizeof(fallbacks[0]); i++) {
        font.fallback_codepoint = fallbacks[i];
        lookup_test_font(&font);
    }
    font.kernings = NULL;
    lookup_test_font(&font);
    font.kernings = pairs;
    font.kerning_count = 0u;
    lookup_test_font(&font);
    font.metrics.line_height = 0;
    lookup_test_font(&font);
    font.metrics.line_height = 16;
    static const char data[] =
        "rgfont 1\natlas 64 64\nline_height 16\nascent 13\ndescent 3\nfallback 63\n"
        "glyph 65 0 0 5 7 0 1 6\n"
        "glyph 233 5 0 4 7 0 1 5\n"
        "glyph 63 10 0 4 7 0 1 4\n"
        "glyph 0 0 0 0 0 0 0 0\n"
        "glyph 127 15 0 4 7 0 1 4\n"
        "kerning 65 63 -1\n"
        "kerning 0 65 0\n"
        "kerning 127 0 2\n"
        "kerning 233 65 -2\n";
    RgTextFontLoadDesc load = {data, sizeof(data) - 1u, glyphs, 9u, pairs, 11u};
    LOOKUP_TEST_CHECK(rg_text_font_load_rgfont(&font, &load));
    lookup_test_font(&font);

    RgGuiTextLookup* lookup = (RgGuiTextLookup*)malloc(sizeof(*lookup));
    LOOKUP_TEST_CHECK(lookup);
    size_t lookup_bytes = sizeof(*lookup);
    LOOKUP_TEST_CHECK(lookup_bytes >= sizeof(void*) * 130u + sizeof(i32) * 128u * 128u + 2u * sizeof(u32) +
                      sizeof(u16) * (RG_GUI_TEXT_LOOKUP_GLYPH_SLOTS + RG_GUI_TEXT_LOOKUP_KERNING_SLOTS));
    LOOKUP_TEST_CHECK(lookup_bytes <= sizeof(void*) * 130u + sizeof(i32) * 128u * 128u + 2u * sizeof(u32) +
                      sizeof(u16) * (RG_GUI_TEXT_LOOKUP_GLYPH_SLOTS + RG_GUI_TEXT_LOOKUP_KERNING_SLOTS) + 16u);
    LOOKUP_TEST_CHECK(rg_gui_text_lookup_init(lookup, &font));
    RgGuiInitDesc desc = {0}, normalized;
    desc.font = &font;
    desc.max_draw_cmds = 16u;
    desc.text_buffer_size = 64u;
    size_t memory_without = rg_gui_memory_required(&desc);
    desc.text_lookup = lookup;
    LOOKUP_TEST_CHECK(rg_gui_init_desc_resolve(&desc, &normalized));
    LOOKUP_TEST_CHECK(normalized.text_lookup == lookup);
    LOOKUP_TEST_CHECK(memory_without == rg_gui_memory_required(&desc));
    void* memory = malloc(memory_without);
    LOOKUP_TEST_CHECK(memory);
    RgArena arena = {(char*)memory, memory_without, 0u, memory_without};
    RgGuiContext ctx;
    LOOKUP_TEST_CHECK(rg_gui_init(&ctx, &arena, &desc));
    LOOKUP_TEST_CHECK(ctx.text_lookup == lookup);
    lookup_test_measure(&ctx, "A?B", 3u);
    free(memory);
    LOOKUP_TEST_CHECK(!rg_gui_text_lookup_init(NULL, &font));
    LOOKUP_TEST_CHECK(!rg_gui_text_lookup_init(lookup, NULL));
    LOOKUP_TEST_CHECK(!lookup->font && !lookup->ascii_glyphs[65] && !lookup->ascii_kerning[65u * 128u + 63u]);
    RgTextFont invalid = {0};
    LOOKUP_TEST_CHECK(!rg_gui_text_lookup_init(lookup, &invalid));
    invalid.glyphs = glyphs;
    LOOKUP_TEST_CHECK(!rg_gui_text_lookup_init(lookup, &invalid));
    LOOKUP_TEST_CHECK(rg_gui_text_find_glyph(NULL, 65u) == NULL);
    LOOKUP_TEST_CHECK(rg_gui_text_find_kerning(NULL, 65u, 63u) == 0);
    LOOKUP_TEST_CHECK(rg_gui_text_measure_prefix(NULL, "A", 1u) == 0.0f);
    free(lookup);

    /* Loaded fonts of several sizes: within the general tables and beyond them. */
    static const u32 sizes[] = {300u, RG_GUI_TEXT_LOOKUP_GLYPH_SLOTS / 2u, RG_GUI_TEXT_LOOKUP_GLYPH_SLOTS / 2u + 1u};
    for (size_t n = 0u; n < sizeof(sizes) / sizeof(sizes[0]); n++) {
        u32 count = sizes[n];
        size_t capacity = 64u + (size_t)count * 96u;
        char* text = (char*)malloc(capacity);
        RgTextGlyph* big_glyphs = (RgTextGlyph*)malloc(sizeof(*big_glyphs) * count);
        RgTextKerning* big_pairs = (RgTextKerning*)malloc(sizeof(*big_pairs) * count);
        LOOKUP_TEST_CHECK(text && big_glyphs && big_pairs);
        size_t used = (size_t)sprintf(text, "rgfont 1\natlas 1024 1024\nline_height 16\nascent 13\ndescent 3\nfallback 63\n");
        for (u32 i = 0u; i < count; i++)
            used += (size_t)sprintf(text + used, "glyph %u %u %u 7 12 0 1 %u\n", 32u + i * 3u, (i % 64u) * 8u, ((i / 64u) % 64u) * 16u, 5u + i % 7u);
        for (u32 i = 0u; i < count; i++)
            used += (size_t)sprintf(text + used, "kerning %u %u %d\n", 32u + i * 3u, 32u + ((i * 7u + 1u) % count) * 3u, -(int)(i % 5u) - 1);
        LOOKUP_TEST_CHECK(used < capacity);
        RgTextFont big = {0};
        RgTextFontLoadDesc big_load = {text, used, big_glyphs, count, big_pairs, count};
        LOOKUP_TEST_CHECK(rg_text_font_load_rgfont(&big, &big_load));
        RgGuiTextLookup* big_lookup = (RgGuiTextLookup*)malloc(sizeof(*big_lookup));
        LOOKUP_TEST_CHECK(big_lookup && rg_gui_text_lookup_init(big_lookup, &big));
#if RG_GUI_TEXT_LOOKUP_GLYPH_SLOTS > 0
        LOOKUP_TEST_CHECK(big_lookup->glyph_table_enabled == (count <= RG_GUI_TEXT_LOOKUP_GLYPH_SLOTS / 2u));
#endif
#if RG_GUI_TEXT_LOOKUP_KERNING_SLOTS > 0
        LOOKUP_TEST_CHECK(big_lookup->kerning_table_enabled == (count <= RG_GUI_TEXT_LOOKUP_KERNING_SLOTS / 2u));
#endif
        for (u32 cp = 0u; cp < 32u + count * 3u + 8u; cp++)
            LOOKUP_TEST_CHECK(rg_gui_text_lookup_glyph(big_lookup, cp) == rg_text_find_glyph(&big, cp));
        for (u32 i = 0u; i < count; i++) {
            LOOKUP_TEST_CHECK(rg_gui_text_lookup_kerning(big_lookup, big_pairs[i].left, big_pairs[i].right) == big_pairs[i].x_advance);
            LOOKUP_TEST_CHECK(rg_gui_text_lookup_kerning(big_lookup, big_pairs[i].right, big_pairs[i].left) ==
                              rg_text_find_kerning(&big, big_pairs[i].right, big_pairs[i].left));
            LOOKUP_TEST_CHECK(rg_gui_text_lookup_kerning(big_lookup, big_pairs[i].left, big_pairs[i].right + 1u) ==
                              rg_text_find_kerning(&big, big_pairs[i].left, big_pairs[i].right + 1u));
        }
        RgGuiContext big_ctx;
        memset(&big_ctx, 0, sizeof(big_ctx));
        big_ctx.font = &big;
        big_ctx.text_lookup = big_lookup;
        big_ctx.style.text_height = 16.0f;
        char sample[256];
        size_t sample_len = 0u;
        for (u32 i = 0u; i < 40u; i++) {
            u32 cp = 32u + ((i * 37u) % count) * 3u;
            if (cp < 0x80u) sample[sample_len++] = (char)cp;
            else if (cp < 0x800u) { sample[sample_len++] = (char)(0xC0u | (cp >> 6u)); sample[sample_len++] = (char)(0x80u | (cp & 63u)); }
            else { sample[sample_len++] = (char)(0xE0u | (cp >> 12u)); sample[sample_len++] = (char)(0x80u | ((cp >> 6u) & 63u)); sample[sample_len++] = (char)(0x80u | (cp & 63u)); }
        }
        for (size_t prefix = 0u; prefix <= sample_len; prefix++) lookup_test_measure(&big_ctx, sample, prefix);
        free(big_lookup);
        free(big_pairs);
        free(big_glyphs);
        free(text);
    }
    printf("Text lookup checks passed: %u\n", lookup_test_checks);
    return 1;
}

#undef LOOKUP_TEST_CHECK
