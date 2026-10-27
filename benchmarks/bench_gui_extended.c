/* Additional CPU workloads. Build the identical source against both header trees. */
#define main rg_gui_labels_benchmark_main
#include "bench_gui.c"
#undef main
#include "rg_gui_gpu.h"

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
    /* Keystrokes into a cached document of one label per line. */
    char edit_document[8192];
    size_t edit_length;
    RgGuiTextAreaState edit_area;
    RgGuiTextAreaLayoutCache cache;
    char cache_text[8192];
    RgGuiTextAreaVisualLine cache_lines[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
    RgInputEvent event_storage[8];
    char event_text[64];
    RgInputEventQueue events;
    u32 edit_frame;
    /* A layered list for the hidden-draw culling pass: backdrop, three columns,
     * panels, and widgets, as an editor layout paints them. */
    RgGuiDrawCmd cull_source[1024];
    RgGuiDrawCmd cull_working[1024];
    RgGuiDrawCmd cull_scratch[1024];
    RgGuiRect cull_clips[1024];
    u32 cull_count, cull_overlay, cull_result_count, cull_culled, cull_split;
} ExtendedBench;

static void extended_init(Bench* b, ExtendedBench* e) {
    memset(e, 0, sizeof(*e));
    e->gpu.item_capacity = LABEL_COUNT;
    e->gpu.items = (RgGuiGpuItem*)allocate(sizeof(*e->gpu.items) * LABEL_COUNT);
#if defined(RG_GUI_GPU_FRAME_CLIP_LIMIT)
    /* Preparation builds the frame clip table: one entry per item plus entry 0. */
    e->gpu.clip_capacity = LABEL_COUNT + 1u;
    e->gpu.clips = (RgGuiGpuFrameClip*)allocate(sizeof(*e->gpu.clips) * e->gpu.clip_capacity);
#endif
    for (u32 i = 0u; ; i++) {
        const char* text = b->labels[i % LABEL_COUNT];
        size_t length = strlen(text);
        if (e->document_length + length + 2u >= sizeof(e->document)) break;
        memcpy(e->document + e->document_length, text, length);
        e->document_length += length;
        e->document[e->document_length++] = ' ';
    }
    e->document[e->document_length] = 0;
    for (u32 i = 0u; ; i++) {
        const char* text = b->labels[i % LABEL_COUNT];
        size_t length = strlen(text);
        if (e->edit_length + length + 2u >= sizeof(e->edit_document)) break;
        memcpy(e->edit_document + e->edit_length, text, length);
        e->edit_length += length;
        e->edit_document[e->edit_length++] = '\n';
    }
    e->edit_document[e->edit_length] = 0;
    rg_gui_text_area_cache_init(&e->cache, e->cache_text, sizeof(e->cache_text),
                                e->cache_lines, RG_ARRAY_COUNT(e->cache_lines));
    e->edit_area.layout_cache = &e->cache;
    {
        u32 n = 0u;
        RgGuiDrawCmd* c = e->cull_source;
#define CULL_RECT(X, Y, W, H, COLOR) do { memset(&c[n], 0, sizeof(c[n])); c[n].type = RG_GUI_CMD_RECT; c[n].color = (COLOR); c[n].data.rect.rect = rg_gui_make_rect((f32)(X), (f32)(Y), (f32)(W), (f32)(H)); n++; } while (0)
#define CULL_CLIP(X, Y, W, H) do { memset(&c[n], 0, sizeof(c[n])); c[n].type = RG_GUI_CMD_CLIP_PUSH; c[n].data.clip.rect = rg_gui_make_rect((f32)(X), (f32)(Y), (f32)(W), (f32)(H)); n++; } while (0)
#define CULL_POP() do { memset(&c[n], 0, sizeof(c[n])); c[n].type = RG_GUI_CMD_CLIP_POP; n++; } while (0)
        CULL_RECT(0, 0, 1280, 800, 0xff202020u);                 /* backdrop */
        CULL_RECT(0, 0, 1280, 28, 0xff303030u);                  /* menu bar */
        CULL_RECT(10, 38, 1260, 752, 0xff2a2a2au);               /* dockspace */
        for (u32 column = 0u; column < 3u; column++) {
            const f32 x = 12.0f + 420.0f * (f32)column;
            CULL_RECT(x, 40, 416, 748, 0xff353535u);             /* window */
            CULL_RECT(x, 40, 416, 24, 0xff404040u);              /* title bar */
            CULL_CLIP(x + 1.5f, 66.25f, 413.0f, 720.5f);
            CULL_RECT(x + 4, 70, 408, 712, 0xff2e2e2eu);         /* panel */
            for (u32 row = 0u; row < 28u; row++) {
                const f32 y = 76.0f + 25.0f * (f32)row;
                CULL_RECT(x + 12, y, 180, 20, 0xff454545u);      /* label background */
                CULL_RECT(x + 200, y, 200, 20, 0xff3a3a3au);     /* slider track */
                CULL_RECT(x + 200, y, 1, 20, 0xff606060u);       /* outline strips */
                CULL_RECT(x + 399, y, 1, 20, 0xff606060u);
                CULL_RECT(x + 200 + (f32)((row * 37u) % 180u), y + 2, 12, 16, 0xff80a0ffu); /* grab */
                CULL_RECT(x + 210, y + 4, 60, 12, 0x60ffffffu);  /* translucent highlight */
            }
            CULL_POP();
        }
        CULL_RECT(400, 300, 480, 200, 0xff505050u);              /* a dialog over everything */
        CULL_RECT(408, 332, 464, 160, 0xff606060u);
        e->cull_overlay = n;
        CULL_RECT(600, 500, 200, 120, 0xe0303030u);              /* overlay tooltip */
#undef CULL_RECT
#undef CULL_CLIP
#undef CULL_POP
        e->cull_count = n;
    }
    rg_input_event_queue_init(&e->events, e->event_storage, RG_ARRAY_COUNT(e->event_storage),
                              e->event_text, sizeof(e->event_text));
}

/* A frame with one keystroke: a click focuses the editor, then frames alternate
 * inserting a character and deleting it again. */
static void extended_edit_frame(Bench* b, ExtendedBench* e) {
    rg_input_begin_frame(&b->input);
    rg_input_event_queue_reset(&e->events, SDL_KMOD_NONE);
    memset(b->input.current_mouse, 0, sizeof(b->input.current_mouse));
    memset(b->input.previous_mouse, 0, sizeof(b->input.previous_mouse));
    b->input.mouse_x = 50;
    b->input.mouse_y = 50;
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    if (e->edit_frame < 2u) {
        const int down = e->edit_frame == 0u;
        b->input.current_mouse[RG_MOUSE_BUTTON_LEFT] = down;
        b->input.previous_mouse[RG_MOUSE_BUTTON_LEFT] = !down;
        event.type = down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = down != 0;
        event.button.clicks = 1u;
        event.button.x = 50.0f;
        event.button.y = 50.0f;
        rg_input_process_event_ex(&b->input, &event, &e->events);
    } else if (e->edit_frame % 2u == 0u) {
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = "x";
        rg_input_process_event_ex(&b->input, &event, &e->events);
    } else {
        event.type = SDL_EVENT_KEY_DOWN;
        event.key.scancode = SDL_SCANCODE_BACKSPACE;
        event.key.key = SDLK_BACKSPACE;
        event.key.down = true;
        rg_input_process_event_ex(&b->input, &event, &e->events);
        event.type = SDL_EVENT_KEY_UP;
        event.key.down = false;
        rg_input_process_event_ex(&b->input, &event, &e->events);
    }
    rg_gui_begin_frame_ex(&b->gui, &b->input, &e->events, 0, 1.0f / 60.0f);
    b->gui.cursor_visible = 1;
    b->gui.cursor_blink_timer = 0.0f;
    rg_gui_text_area(&b->gui, &e->edit_area, e->edit_document, sizeof(e->edit_document),
                     (RgGuiRect){0, 0, 640, 360}, (RgGuiId)174u);
    rg_gui_end_frame(&b->gui);
    if (b->gui.diagnostics.flags) fail("text area edit diagnostics");
    e->edit_frame++;
    sink += (double)b->gui.draw_list.count + b->gui.text_edit_state->cursor;
}

RG_NOINLINE static void extended_frame(Bench* b, ExtendedBench* e, int mode) {
    if (mode == 7) {
#if defined(RG_GUI_CULL_OCCLUDER_MAX)
        memcpy(e->cull_working, e->cull_source, sizeof(*e->cull_working) * e->cull_count);
        RgGuiDrawList list = {e->cull_working, e->cull_count, (u32)RG_ARRAY_COUNT(e->cull_working)};
        u32 overlay = e->cull_overlay;
        rg_gui_draw_list_cull_hidden_ex(&list, &overlay, e->cull_clips, e->cull_scratch,
                                        (u32)RG_ARRAY_COUNT(e->cull_scratch), &e->cull_culled, &e->cull_split);
        e->cull_result_count = list.count;
        sink += (double)list.count + overlay;
#else
        fail("hidden-draw culling is not available in this rg_gui");
#endif
    } else if (mode == 6) {
        extended_edit_frame(b, e);
    } else if (mode < 3) {
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
    } else if (mode == 7) {
        hash = hash_u32(hash, e->cull_result_count);
        hash = hash_u32(hash, e->cull_culled);
        hash = hash_u32(hash, e->cull_split);
        for (u32 i = 0u; i < e->cull_result_count; i++) {
            const RgGuiDrawCmd* cmd = &e->cull_working[i];
            hash = hash_u32(hash, cmd->type);
            hash = hash_u32(hash, cmd->color);
            if (cmd->type == RG_GUI_CMD_RECT) {
                hash = hash_float(hash, cmd->data.rect.rect.x);
                hash = hash_float(hash, cmd->data.rect.rect.y);
                hash = hash_float(hash, cmd->data.rect.rect.w);
                hash = hash_float(hash, cmd->data.rect.rect.h);
            }
        }
    } else if (mode == 6) {
        /* Finish an insertion's deletion so the document is back to its text,
         * then the whole cached layout and the editor state are deterministic. */
        if (e->edit_frame % 2u) extended_frame(b, e, mode);
        if (strlen(e->edit_document) != e->edit_length) fail("edited document did not return to its text");
        hash = hash_u32(hash, (u32)b->gui.text_edit_state->cursor);
        hash = hash_u32(hash, e->cache.count);
        for (u32 i = 0u; i < e->cache.count; i++) {
            hash = hash_u32(hash, (u32)e->cache.lines[i].start);
            hash = hash_u32(hash, (u32)e->cache.lines[i].end);
            hash = hash_u32(hash, (u32)e->cache.lines[i].soft_wrap_after);
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
        "gpu_prepare_cold", "textarea_wrap_300", "textarea_wrap_1200", "textarea_click_drag", "textarea_edit", "cull_hidden"};
    const size_t document_length = mode == 6 ? e->edit_length : e->document_length;
    change_labels(b, 0u);
    rg_gui_renderer_clear_cache(&b->renderer);
    for (u32 i = 0u; i < LABEL_COUNT; i++)
        b->commands[i].data.text.cache_identity = mode == 0 ? (uintptr_t)b->labels[i] : 0u;
    for (u32 i = 0u; i < 4u; i++) {
        if (mode == 2) rg_gui_renderer_clear_cache(&b->renderer);
        extended_frame(b, e, mode);
    }
    if (getenv("RG_GUI_BENCH_VERIFY")) {
        printf("verified %s %s %llu\n", font, names[mode], (unsigned long long)extended_signature(b, e, mode));
        return;
    }
    double calibration = extended_sample(b, e, mode, 2u);
    u32 frames = calibration > 0.0 ? (u32)(0.035 * 2.0 / calibration) : 2u;
    if (frames < 2u) frames = 2u;
    if (frames > 20000u) frames = 20000u;
    double times[SAMPLE_COUNT];
    printf("{\"kind\":\"samples\",\"font\":\"%s\",\"case\":\"%s\",\"labels\":%u,\"frames_per_sample\":%u,\"document_bytes\":%zu,\"ns_per_frame\":[",
        font, names[mode], mode < 3 ? LABEL_COUNT : 1u, frames, mode < 3 ? 0u : document_length);
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
    printf("{\"kind\":\"result\",\"font\":\"%s\",\"case\":\"%s\",\"median_ns_per_frame\":%.3f,\"min_ns_per_frame\":%.3f,\"max_ns_per_frame\":%.3f,\"width_checksum\":%.3f,\"geometry_checksum\":\"%llu\",\"cache_hits\":%u,\"cache_misses\":%u,\"glyphs\":%u,\"renderer_arena_bytes\":%zu,\"renderer_lookup_shared\":%u,\"fixture_bytes\":%zu,\"frontend_lookup_count\":%u}\n",
        font, names[mode], times[3], times[0], times[6], mode >= 3 ? (double)document_length : 0.0,
        (unsigned long long)hash, mode < 3 ? stats->frame_cache_hits : 0u, mode < 3 ? stats->frame_cache_misses : 0u,
        mode < 3 ? stats->frame_glyphs : 0u, b->renderer_memory_size,
        (unsigned)RG_GUI_BENCH_RENDERER_LOOKUP, sizeof(*b), (unsigned)RG_GUI_BENCH_HAS_LOOKUP);
    fflush(stdout);
}

int main(void) {
    static const char* label_cases[] = {"frontend_measured_labels_static_warm", "frontend_measured_labels_dynamic",
        "renderer_static_warm", "renderer_content_warm", "renderer_cold", "renderer_init"};
    static const char* more_cases[] = {"gpu_prepare_static_warm", "gpu_prepare_content_warm",
        "gpu_prepare_cold", "textarea_wrap_300", "textarea_wrap_1200", "textarea_click_drag", "textarea_edit", "cull_hidden"};
    Uint64 frequency = SDL_GetPerformanceFrequency();
    if (!frequency) fail("performance counter");
    timer_scale = 1.0 / (double)frequency;
    Bench* b = (Bench*)allocate(sizeof(Bench));
    ExtendedBench* e = (ExtendedBench*)allocate(sizeof(ExtendedBench));
    for (int synthetic = 0; synthetic <= 1; synthetic++) {
        init_bench(b, synthetic);
        extended_init(b, e);
        const char* font = synthetic ? "synthetic4096_mixed_utf8" : "inter95_ascii";
        printf("{\"kind\":\"font\",\"font\":\"%s\",\"glyph_count\":%u,\"kerning_count\":%u,\"label_copy_default\":%u}\n",
            font, b->font.glyph_count, b->font.kerning_count, (unsigned)RG_GUI_LABEL_COPY);
        if (!getenv("RG_GUI_BENCH_VERIFY"))
            for (int mode = 0; mode < 6; mode++) if (extended_selected(label_cases[mode])) run_case(b, font, mode);
        for (int mode = 0; mode < 8; mode++) if (extended_selected(more_cases[mode])) extended_case(b, e, font, mode);
        free(e->gpu.items);
#if defined(RG_GUI_GPU_FRAME_CLIP_LIMIT)
        free(e->gpu.clips);
#endif
        free(b->gui_memory); free(b->renderer_memory); free(b->glyphs); free(b->kernings);
    }
    free(e); free(b);
    return sink == 0.0 ? 1 : 0;
}
