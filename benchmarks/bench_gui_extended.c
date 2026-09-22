/* Additional CPU workloads. Build the identical source against both header trees. */
#define main rg_gui_labels_benchmark_main
#include "bench_gui.c"
#undef main
#include "rg_gui_gpu.h"

#ifndef RG_GUI_BENCH_LOOKUP
#define RG_GUI_BENCH_LOOKUP 0
#endif

static int extended_selected(const char* name) {
    const char* filter = getenv("RG_GUI_BENCH_CASES");
    if (!filter || !*filter) return 1;
    size_t length = strlen(name);
    for (const char* p = filter; *p; ) {
        const char* end = strchr(p, ',');
        size_t part = end ? (size_t)(end - p) : strlen(p);
        if (part == length && memcmp(p, name, length) == 0) return 1;
        if (!end) break;
        p = end + 1;
    }
    return 0;
}

typedef struct ExtendedBench {
    RgGuiGpuRenderer gpu;
    char document[8192];
    size_t document_length;
    RgGuiTextAreaVisualLine lines[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
    u32 line_count;
    RgGuiTextAreaState area;
} ExtendedBench;

static void extended_init(Bench* b, ExtendedBench* e) {
    memset(e, 0, sizeof(*e));
    e->gpu.item_capacity = LABEL_COUNT;
    e->gpu.items = (RgGuiGpuItem*)allocate(sizeof(*e->gpu.items) * LABEL_COUNT);
    for (u32 i = 0u; ; i++) {
        const char* text = b->labels[i % LABEL_COUNT];
        size_t length = strlen(text);
        if (e->document_length + length + 2u >= sizeof(e->document)) break;
        memcpy(e->document + e->document_length, text, length);
        e->document_length += length;
        e->document[e->document_length++] = ' ';
    }
    e->document[e->document_length] = 0;
}

__declspec(noinline) static void extended_frame(Bench* b, ExtendedBench* e, int mode) {
    if (mode < 3) {
        rg_gui_renderer_begin_frame(&b->renderer);
        if (!rg_gui_gpu_prepare(&e->gpu, &b->renderer, &b->list, b->list.count))
            fail("combined GUI CPU preparation");
        sink += (double)e->gpu.item_count + b->renderer.core.run_prepared.glyph_count;
    } else if (mode < 5) {
        e->line_count = rg_gui_text_area_build_visual_lines(&b->gui, e->document,
            e->document_length, mode == 3 ? 300.0f : 1200.0f,
            e->lines, RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
        if (!e->line_count || e->lines[e->line_count - 1u].end != e->document_length)
            fail("wrapped document was truncated");
        sink += e->line_count + (double)e->lines[e->line_count - 1u].start;
    } else {
        /* Deterministic click-and-hold frame exercises picking, dragging, and drawing. */
        b->input.mouse_x = 50;
        b->input.mouse_y = 50;
        b->input.previous_mouse[RG_MOUSE_BUTTON_LEFT] = false;
        b->input.current_mouse[RG_MOUSE_BUTTON_LEFT] = true;
        rg_gui_begin_frame(&b->gui, &b->input, 1.0f / 60.0f);
        b->gui.cursor_visible = 1;
        b->gui.cursor_blink_timer = 0.0f;
        b->gui.text_edit_state->last_click_id = 0u;
        b->gui.text_edit_state->click_count = 0u;
        rg_gui_text_area(&b->gui, &e->area, e->document, sizeof(e->document),
                        (RgGuiRect){0, 0, 640, 360}, (RgGuiId)173u);
        rg_gui_end_frame(&b->gui);
        if (b->gui.diagnostics.flags) fail("text area diagnostics");
        sink += (double)b->gui.draw_list.count + b->gui.text_edit_state->cursor;
    }
}

static double extended_sample(Bench* b, ExtendedBench* e, int mode, u32 frames) {
    if (mode != 2) {
        double start = now_seconds();
        for (u32 i = 0u; i < frames; i++) extended_frame(b, e, mode);
        return now_seconds() - start;
    }
    double elapsed = 0.0;
    for (u32 i = 0u; i < frames; i++) {
        rg_gui_renderer_clear_cache(&b->renderer);
        double start = now_seconds();
        extended_frame(b, e, mode);
        elapsed += now_seconds() - start;
    }
    return elapsed;
}

static u64 extended_signature(Bench* b, ExtendedBench* e, int mode) {
    u64 hash = 1469598103934665603ull;
    if (mode < 3) {
        const RgGuiRendererStats* stats = rg_gui_renderer_stats(&b->renderer);
        const RgGuiRendererPrepared* prepared = rg_gui_renderer_prepared(&b->renderer);
        if (stats->diagnostic_flags || stats->frame_cache_bypasses || stats->frame_dropped_runs ||
            stats->frame_cache_hits != (mode == 2 ? 0u : LABEL_COUNT) ||
            stats->frame_cache_misses != (mode == 2 ? LABEL_COUNT : 0u) ||
            e->gpu.item_count != 1u || e->gpu.items[0].type != RG_GUI_GPU_ITEM_TEXT ||
            e->gpu.items[0].first != 0u || e->gpu.items[0].count != prepared->glyph_count)
            fail("combined GUI preparation output");
        return geometry_checksum(b);
    }
    if (mode < 5) {
        for (u32 i = 0u; i < e->line_count; i++) {
            hash = hash_u32(hash, (u32)e->lines[i].start);
            hash = hash_u32(hash, (u32)e->lines[i].end);
        }
    } else {
        hash = hash_u32(hash, (u32)b->gui.text_edit_state->cursor);
        hash = hash_u32(hash, b->gui.draw_list.count);
        for (u32 i = 0u; i < b->gui.draw_list.count; i++) {
            const RgGuiDrawCmd* cmd = &b->gui.draw_list.cmds[i];
            hash = hash_u32(hash, cmd->type);
            if (cmd->type == RG_GUI_CMD_TEXT) {
                hash = hash_float(hash, cmd->data.text.pos.x);
                hash = hash_float(hash, cmd->data.text.pos.y);
                for (const unsigned char* p = (const unsigned char*)cmd->data.text.text; *p; p++)
                    hash = hash_u32(hash, *p);
            } else if (cmd->type == RG_GUI_CMD_RECT) {
                hash = hash_float(hash, cmd->data.rect.rect.x);
                hash = hash_float(hash, cmd->data.rect.rect.y);
                hash = hash_float(hash, cmd->data.rect.rect.w);
                hash = hash_float(hash, cmd->data.rect.rect.h);
            }
        }
    }
    return hash;
}

static void extended_case(Bench* b, ExtendedBench* e, const char* font, int mode) {
    static const char* names[] = {"gpu_prepare_static_warm", "gpu_prepare_content_warm",
        "gpu_prepare_cold", "textarea_wrap_300", "textarea_wrap_1200", "textarea_click_drag"};
    change_labels(b, 0u);
    rg_gui_renderer_clear_cache(&b->renderer);
    for (u32 i = 0u; i < LABEL_COUNT; i++)
        b->commands[i].data.text.cache_identity = mode == 0 ? (uintptr_t)b->labels[i] : 0u;
    for (u32 i = 0u; i < 4u; i++) {
        if (mode == 2) rg_gui_renderer_clear_cache(&b->renderer);
        extended_frame(b, e, mode);
    }
    if (getenv("RG_GUI_BENCH_VERIFY")) {
        printf("verified %s %s %llu\n", font, names[mode], extended_signature(b, e, mode));
        return;
    }
    double calibration = extended_sample(b, e, mode, 2u);
    u32 frames = calibration > 0.0 ? (u32)(0.035 * 2.0 / calibration) : 2u;
    if (frames < 2u) frames = 2u;
    if (frames > 20000u) frames = 20000u;
    double times[SAMPLE_COUNT];
    printf("{\"kind\":\"samples\",\"font\":\"%s\",\"case\":\"%s\",\"labels\":%u,\"frames_per_sample\":%u,\"document_bytes\":%zu,\"ns_per_frame\":[",
        font, names[mode], mode < 3 ? LABEL_COUNT : 1u, frames, mode < 3 ? 0u : e->document_length);
    for (u32 i = 0u; i < SAMPLE_COUNT; i++) {
        times[i] = extended_sample(b, e, mode, frames) * 1e9 / frames;
        printf("%s%.3f", i ? "," : "", times[i]);
    }
    printf("]}\n");
    qsort(times, SAMPLE_COUNT, sizeof(double), compare_double);
    if (mode == 2) rg_gui_renderer_clear_cache(&b->renderer);
    extended_frame(b, e, mode);
    u64 hash = extended_signature(b, e, mode);
    const RgGuiRendererStats* stats = rg_gui_renderer_stats(&b->renderer);
    printf("{\"kind\":\"result\",\"font\":\"%s\",\"case\":\"%s\",\"median_ns_per_frame\":%.3f,\"min_ns_per_frame\":%.3f,\"max_ns_per_frame\":%.3f,\"width_checksum\":%.3f,\"geometry_checksum\":\"%llu\",\"cache_hits\":%u,\"cache_misses\":%u,\"glyphs\":%u}\n",
        font, names[mode], times[3], times[0], times[6], mode >= 3 ? (double)e->document_length : 0.0,
        hash, mode < 3 ? stats->frame_cache_hits : 0u, mode < 3 ? stats->frame_cache_misses : 0u,
        mode < 3 ? stats->frame_glyphs : 0u);
    fflush(stdout);
}

int main(void) {
    static const char* label_cases[] = {"frontend_measured_labels_static_warm", "frontend_measured_labels_dynamic",
        "renderer_static_warm", "renderer_content_warm", "renderer_cold", "renderer_init"};
    static const char* more_cases[] = {"gpu_prepare_static_warm", "gpu_prepare_content_warm",
        "gpu_prepare_cold", "textarea_wrap_300", "textarea_wrap_1200", "textarea_click_drag"};
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) fail("performance counter");
    timer_scale = 1.0 / (double)frequency.QuadPart;
    Bench* b = (Bench*)allocate(sizeof(Bench));
    ExtendedBench* e = (ExtendedBench*)allocate(sizeof(ExtendedBench));
#if RG_GUI_BENCH_LOOKUP && defined(RG_GUI_HAS_TEXT_LOOKUP)
    RgGuiTextLookup* lookup = (RgGuiTextLookup*)allocate(sizeof(RgGuiTextLookup));
#endif
    for (int synthetic = 0; synthetic <= 1; synthetic++) {
        init_bench(b, synthetic);
#if RG_GUI_BENCH_LOOKUP && defined(RG_GUI_HAS_TEXT_LOOKUP)
        if (!rg_gui_text_lookup_init(lookup, &b->font)) fail("text lookup initialization");
        b->gui.text_lookup = lookup;
#endif
        extended_init(b, e);
        const char* font = synthetic ? "synthetic4096_mixed_utf8" : "inter95_ascii";
        printf("{\"kind\":\"font\",\"font\":\"%s\",\"glyph_count\":%u,\"kerning_count\":%u,\"label_copy_default\":%u}\n",
            font, b->font.glyph_count, b->font.kerning_count, (unsigned)RG_GUI_LABEL_COPY);
        if (!getenv("RG_GUI_BENCH_VERIFY"))
            for (int mode = 0; mode < 6; mode++) if (extended_selected(label_cases[mode])) run_case(b, font, mode);
        for (int mode = 0; mode < 6; mode++) if (extended_selected(more_cases[mode])) extended_case(b, e, font, mode);
        free(e->gpu.items);
        free(b->gui_memory); free(b->renderer_memory); free(b->glyphs); free(b->kernings);
    }
#if RG_GUI_BENCH_LOOKUP && defined(RG_GUI_HAS_TEXT_LOOKUP)
    free(lookup);
#endif
    free(e); free(b);
    return sink == 0.0 ? 1 : 0;
}
