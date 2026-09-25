/* CPU-only Unicode profiling. Synthetic loaded/sorted fonts isolate lookup and
 * layout costs; these are atlas glyphs, not a language shaping benchmark.
 * Run through run_unicode.py. No allocation/font load/upload is timed.
 */
#define main rg_gui_labels_benchmark_main
#include "bench_gui.c"
#undef main

#ifndef RG_GUI_BENCH_LOOKUP
#define RG_GUI_BENCH_LOOKUP 1
#endif

#define UNICODE_ASCII_COUNT 96u
#define UNICODE_LABEL_GLYPHS 40u
#define UNICODE_CJK_GLYPHS 36u
#define UNICODE_BASE 0x4e00u

typedef struct UnicodeBench {
    Bench base;
#if defined(RG_GUI_HAS_TEXT_LOOKUP)
    RgGuiTextLookup lookup;
#endif
    u32 glyph_count;
    u32 codepoints[LABEL_COUNT][UNICODE_LABEL_GLYPHS];
    const char* corpus;
    const char* font_name;
    int lookup_enabled;
    double lookup_sum;
} UnicodeBench;

static u32 unicode_codepoint(u32 index) {
    return index < UNICODE_ASCII_COUNT ? 32u + index : UNICODE_BASE + index - UNICODE_ASCII_COUNT;
}

static u32 unicode_index(u32 cp) {
    if (cp >= 32u && cp < 128u) return cp - 32u;
    if (cp < UNICODE_BASE) fail("unexpected corpus codepoint");
    return cp - UNICODE_BASE + UNICODE_ASCII_COUNT;
}

static char* unicode_font(u32 count, size_t* size) {
    const size_t capacity = (size_t)count * 128u + 256u;
    char* data = (char*)allocate(capacity);
    size_t used = (size_t)sprintf(data,
        "rgfont 1\natlas 2048 2048\nline_height 16\nascent 13\ndescent 3\nfallback 63\n");
    for (u32 i = 0u; i < count; ++i) {
        if (capacity - used < 128u) fail("Unicode font capacity");
        used += (size_t)sprintf(data + used, "glyph %u %u %u %u 12 0 1 %u\n",
            unicode_codepoint(i), (i % 128u) * 16u, (i / 128u) * 16u, 7u + i % 4u, 8u + i % 5u);
    }
    for (u32 i = 0u; i < count; ++i) {
        if (capacity - used < 128u) fail("Unicode kerning capacity");
        used += (size_t)sprintf(data + used, "kerning %u %u -1\n",
            unicode_codepoint(i), unicode_codepoint((i + 1u) % count));
    }
    *size = used;
    return data;
}

static void unicode_init(UnicodeBench* u, u32 count) {
    memset(u, 0, sizeof(*u));
    Bench* b = &u->base;
    u->glyph_count = count;
    u->font_name = count == 4096u ? "synthetic4096_cjk" : "synthetic16384_cjk";
    size_t data_size;
    char* data = unicode_font(count, &data_size);
    b->glyphs = (RgTextGlyph*)allocate(sizeof(RgTextGlyph) * count);
    b->kernings = (RgTextKerning*)allocate(sizeof(RgTextKerning) * count);
    RgTextFontLoadDesc load = {0};
    load.data = data; load.data_size = data_size;
    load.glyphs = b->glyphs; load.glyph_capacity = count;
    load.kernings = b->kernings; load.kerning_capacity = count;
    if (!rg_text_font_load_rgfont(&b->font, &load)) fail("Unicode font load");
    free(data);
    if (b->font.glyph_count != count || b->font.kerning_count != count ||
        !b->font.internal_lookup_flags) fail("font load count/sorted lookup");
    for (u32 i = 0u; i < count; ++i) {
        const RgTextGlyph* glyph = rg_text_find_glyph(&b->font, unicode_codepoint(i));
        if (!glyph || glyph->codepoint != unicode_codepoint(i) ||
            glyph->x_advance != (i32)(8u + i % 5u) ||
            rg_text_find_kerning(&b->font, unicode_codepoint(i), unicode_codepoint((i + 1u) % count)) != -1)
            fail("loaded font lookup validation");
    }
    RgGuiInitDesc gui_desc = {0};
    gui_desc.font = &b->font;
    gui_desc.max_draw_cmds = 1024u;
    gui_desc.text_buffer_size = 64u * 1024u;
    gui_desc.text_measure_cache_size = 1024u;
    gui_desc.text_length_cache_size = 1024u;
#if RG_GUI_BENCH_LOOKUP && defined(RG_GUI_HAS_TEXT_LOOKUP)
    if (!rg_gui_text_lookup_init(&u->lookup, &b->font)) fail("Unicode ASCII lookup init");
    gui_desc.text_lookup = &u->lookup;
    u->lookup_enabled = 1;
#endif
    size_t gui_size = rg_gui_memory_required(&gui_desc);
    b->gui_memory = allocate(gui_size);
    RgArena gui_arena = {(char*)b->gui_memory, gui_size, 0u, gui_size};
    if (!rg_gui_init(&b->gui, &gui_arena, &gui_desc)) fail("Unicode GUI init");
    RgGuiRendererLimits limits = {512u, 1024u, 128u * 1024u, 32768u, 32768u, 16u, 0u};
    b->renderer_memory_size = rg_gui_renderer_memory_required(&limits, 32u);
    if (b->renderer_memory_size == SIZE_MAX) fail("Unicode renderer size");
    b->renderer_memory = allocate(b->renderer_memory_size);
    init_renderer(b);
}

static void unicode_corpus(UnicodeBench* u, int dispersed) {
    Bench* b = &u->base;
    u->corpus = dispersed ? "dispersed" : "repeated32";
    const u32 non_ascii = u->glyph_count - UNICODE_ASCII_COUNT;
    for (u32 i = 0u; i < LABEL_COUNT; ++i) {
        size_t length = 0u;
        for (u32 c = 0u; c < UNICODE_CJK_GLYPHS; ++c) {
            u32 cp = UNICODE_BASE + (dispersed ? (i * (non_ascii / LABEL_COUNT) + c) % non_ascii : c % 32u);
            u->codepoints[i][c] = cp;
            length += utf8_encode(b->labels[i] + length, cp);
        }
        memcpy(b->labels[i] + length, "0000", 5u);
        b->lengths[i] = length + 4u;
        if (b->lengths[i] >= LABEL_BYTES) fail("Unicode label capacity");
        RgGuiDrawCmd* cmd = &b->commands[i];
        memset(cmd, 0, sizeof(*cmd));
        cmd->type = RG_GUI_CMD_TEXT;
        cmd->data.text.text = b->labels[i];
        cmd->data.text.pos.x = (f32)(i % 4u) * 400.0f;
        cmd->data.text.pos.y = (f32)(i / 4u) * 20.0f;
        cmd->data.text.scale = 1.0f;
        cmd->data.text.color = (rg_vec4){.x = 1, .y = 1, .z = 1, .w = 1};
    }
    b->list = (RgGuiDrawList){b->commands, LABEL_COUNT, LABEL_COUNT};
    change_labels(b, 0u);
    for (u32 i = 0u; i < LABEL_COUNT; ++i)
        for (u32 c = UNICODE_CJK_GLYPHS; c < UNICODE_LABEL_GLYPHS; ++c)
            u->codepoints[i][c] = (u32)(unsigned char)b->labels[i][b->lengths[i] - 4u + c - UNICODE_CJK_GLYPHS];
}

/* Independent known-font oracle: no production glyph/kerning search or layout. */
static double unicode_reference(UnicodeBench* u, u64* geometry, u64* widths, u64* text_hash) {
    Bench* b = &u->base;
    const f32 scale = rg_gui_text_base_scale(&b->gui);
    double sum = 0.0;
    *geometry = *widths = *text_hash = 1469598103934665603ull;
    for (u32 i = 0u; i < LABEL_COUNT; ++i) {
        f32 pen = 0.0f, width = 0.0f;
        u32 previous = UINT32_MAX;
        size_t offset = 0u;
        for (u32 c = 0u; c < UNICODE_LABEL_GLYPHS; ++c) {
            u32 cp = c < UNICODE_CJK_GLYPHS ? u->codepoints[i][c] :
                (u32)(unsigned char)b->labels[i][b->lengths[i] - 4u + c - UNICODE_CJK_GLYPHS];
            if (rg_text_decode_utf8(b->labels[i], b->lengths[i], &offset) != cp)
                fail("Unicode corpus decode mismatch");
            u32 index = unicode_index(cp);
            if (index >= u->glyph_count) fail("Unicode corpus missing glyph");
            i32 kern = previous != UINT32_MAX && (previous + 1u) % u->glyph_count == index ? -1 : 0;
            pen += (f32)kern;
            width += (f32)kern * scale;
            u32 x = (index % 128u) * 16u, y = (index / 128u) * 16u, w = 7u + index % 4u;
            *geometry = hash_float(*geometry, b->commands[i].data.text.pos.x + pen);
            *geometry = hash_float(*geometry, b->commands[i].data.text.pos.y + 1.0f);
            *geometry = hash_float(*geometry, (f32)w);
            *geometry = hash_float(*geometry, 12.0f);
            *geometry = hash_u32(*geometry, x | (y << 16u));
            *geometry = hash_u32(*geometry, (x + w) | ((y + 12u) << 16u));
            pen += (f32)(8u + index % 5u);
            width += (f32)(8u + index % 5u) * scale;
            previous = index;
        }
        if (offset != b->lengths[i]) fail("Unicode corpus byte count");
        for (size_t c = 0u; c < b->lengths[i]; ++c)
            *text_hash = hash_u32(*text_hash, (u32)(unsigned char)b->labels[i][c]);
        *widths = hash_float(*widths, width);
        if (rg_gui_text_measure_prefix(&b->gui, b->labels[i], b->lengths[i]) != width)
            fail("Unicode exact width validation");
        sum += (double)width;
    }
    return sum;
}

__declspec(noinline) static void unicode_frame(UnicodeBench* u, int mode, u32 index) {
    if (mode != 6) { frame(&u->base, mode, index); return; }
    double sum = 0.0;
    for (u32 i = 0u; i < LABEL_COUNT; ++i) {
        const RgTextGlyph* previous = NULL;
        for (u32 c = 0u; c < UNICODE_LABEL_GLYPHS; ++c) {
            const RgTextGlyph* glyph = rg_gui_renderer_base_find_glyph(&u->base.renderer.core, u->codepoints[i][c]);
            if (!glyph) fail("lookup missing glyph");
            if (previous) sum += rg_gui_renderer_base_find_kerning(&u->base.renderer.core, previous->codepoint, glyph->codepoint);
            sum += glyph->x_advance;
            previous = glyph;
        }
    }
    u->lookup_sum = sum;
    sink += sum;
}

static double unicode_sample(UnicodeBench* u, int mode, u32 frames) {
    if (mode == 4) return sample(&u->base, mode, frames);
    double start = now_seconds();
    for (u32 i = 0u; i < frames; ++i) unicode_frame(u, mode, i);
    return now_seconds() - start;
}

static void unicode_case(UnicodeBench* u, int mode, const char* name, int verify_only) {
    Bench* b = &u->base;
    change_labels(b, 0u);
    memset(b->gui.text_measure_cache, 0, b->gui.text_measure_cache_capacity * sizeof(*b->gui.text_measure_cache));
    memset(b->gui.text_length_cache, 0, b->gui.text_length_cache_capacity * sizeof(*b->gui.text_length_cache));
    rg_gui_renderer_clear_cache(&b->renderer);
    for (u32 i = 0u; i < LABEL_COUNT; ++i)
        b->commands[i].data.text.cache_identity = mode == 2 ? (uintptr_t)b->labels[i] : 0u;
    for (u32 i = 0u; i < 4u; ++i) unicode_frame(u, mode, i);
    double times[SAMPLE_COUNT] = {0};
    if (!verify_only) {
        double calibration = unicode_sample(u, mode, 16u);
        double requested = calibration > 0.0 ? 0.035 * 16.0 / calibration : 16.0;
        u32 frames = requested >= 20000.0 ? 20000u : requested < 16.0 ? 16u : (u32)requested;
        if (mode == 1) frames = ((frames + 15u) / 16u) * 16u;
        printf("{\"kind\":\"samples\",\"font\":\"%s\",\"corpus\":\"%s\",\"case\":\"%s\",\"frames_per_sample\":%u,\"ns_per_frame\":[",
            u->font_name, u->corpus, name, frames);
        for (u32 i = 0u; i < SAMPLE_COUNT; ++i) {
            times[i] = unicode_sample(u, mode, frames) * 1e9 / (double)frames;
            printf("%s%.3f", i ? "," : "", times[i]);
        }
        printf("]}\n");
        qsort(times, SAMPLE_COUNT, sizeof(double), compare_double);
    }
    change_labels(b, 0u);
    u64 geometry = 0u, widths = 0u, text_hash = 0u, cycle_hash = 1469598103934665603ull;
    for (u32 f = 0u; f < (mode == 1 ? DYNAMIC_CYCLE : 1u); ++f) {
        if (mode == 4) rg_gui_renderer_clear_cache(&b->renderer);
        unicode_frame(u, mode, f);
        double reference_sum = unicode_reference(u, &geometry, &widths, &text_hash);
        if (mode == 1 && b->width_sum != reference_sum) fail("frontend width sum");
        if (mode == 6 && u->lookup_sum != reference_sum / rg_gui_text_base_scale(&b->gui)) fail("lookup sum");
        cycle_hash = hash_u32(hash_u32(cycle_hash, (u32)widths), (u32)(widths >> 32u));
        cycle_hash = hash_u32(hash_u32(cycle_hash, (u32)text_hash), (u32)(text_hash >> 32u));
    }
    const RgGuiRendererStats* stats = rg_gui_renderer_stats(&b->renderer);
    int renderer = mode == 2 || mode == 4;
    if (renderer && (geometry_checksum(b) != geometry || stats->frame_glyphs != LABEL_COUNT * UNICODE_LABEL_GLYPHS ||
        stats->frame_cache_hits != (mode == 2 ? LABEL_COUNT : 0u) ||
        stats->frame_cache_misses != (mode == 4 ? LABEL_COUNT : 0u))) fail("renderer geometry/glyph/cache validation");
    printf("{\"kind\":\"result\",\"font\":\"%s\",\"corpus\":\"%s\",\"case\":\"%s\",\"median_ns_per_frame\":%.3f,\"cycle_checksum\":\"%llu\",\"geometry_checksum\":\"%llu\",\"cache_hits\":%u,\"cache_misses\":%u,\"glyphs\":%u}\n",
        u->font_name, u->corpus, name, times[SAMPLE_COUNT / 2u], cycle_hash, geometry,
        renderer ? stats->frame_cache_hits : 0u, renderer ? stats->frame_cache_misses : 0u,
        LABEL_COUNT * UNICODE_LABEL_GLYPHS);
    fflush(stdout);
}

int main(int argc, char** argv) {
    if (argc > 2 || (argc == 2 && strcmp(argv[1], "check"))) fail("usage: bench_unicode.exe [check]");
    const int verify_only = argc == 2;
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) fail("performance counter");
    timer_scale = 1.0 / (double)frequency.QuadPart;
    UnicodeBench* u = (UnicodeBench*)allocate(sizeof(*u));
    for (u32 count = 4096u; count <= 16384u; count *= 4u) {
        unicode_init(u, count);
        printf("{\"kind\":\"font\",\"font\":\"%s\",\"glyph_count\":%u,\"kerning_count\":%u,\"labels\":128,\"codepoints_per_label\":40,\"non_ascii_per_label\":36,\"bytes_per_label\":112,\"dynamic_cycle\":16,\"lookup_enabled\":%d}\n",
            u->font_name, count, count, u->lookup_enabled);
        for (int dispersed = 0; dispersed <= 1; ++dispersed) {
            unicode_corpus(u, dispersed);
            unicode_case(u, 1, "frontend_dynamic", verify_only);
            unicode_case(u, 2, "renderer_warm", verify_only);
            unicode_case(u, 4, "renderer_cold", verify_only);
            unicode_case(u, 6, "lookup_pairs", verify_only);
        }
        free(u->base.gui_memory); free(u->base.renderer_memory);
        free(u->base.glyphs); free(u->base.kernings);
    }
    free(u);
    return sink == 0.0 ? 1 : 0;
}
