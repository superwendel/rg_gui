/* Native rg_gui widget submission and its shipping indexed SDL_GPU renderer. */
#define RG_SPRINTF_NO_ASM 1
#define RGINLINE static inline
#include "bench_api.h"
#include "rg_gui_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

typedef struct RgBench {
    RgTextFont font;
    RgTextGlyph glyphs[256];
    RgTextKerning kernings[2048];
    RgGuiTextLookup lookup;
    RgGuiContext gui;
    RgGuiRenderer text;
    RgGuiGpuRenderer gpu;
    RgGpuUploadRing rings[3];
    RgGuiGpuUpload uploads[3];
    uint32_t flights, active_slot;
    void* gui_memory;
    void* text_memory;
    RgInputState input;
    RgInputEvent event_storage[16];
    char event_text[128];
    RgInputEventQueue events;
    int previous_mouse, backspace_down;
    int checks[32];
    float values[32];
    int checks_initialized, selected;
    char filter[64];
    char document[BENCH_DOCUMENT_CAPACITY];
    char snapshot[BENCH_DOCUMENT_CAPACITY];
    RgGuiTextAreaVisualLine lines[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
    RgGuiTextAreaLayoutCache editor_cache;
    RgGuiTextAreaState editor;
    RgGuiDockSpaceState dockspace;
    RgGuiWindowState windows[4];
    int docks_initialized;
    enum BenchCase current_case;
} RgBench;

static const char* const rg_bench_titles[4] = {"Panel 0", "Panel 1", "Panel 2", "Panel 3"};
static const u32 rg_bench_dock_leaves[4] = {4u, 6u, 5u, 7u};

static int rg_bench_error(const char* message)
{
    SDL_SetError("rg_gui comparison: %s", message);
    return 0;
}

static void rg_bench_event(RgBench* state, SDL_Event* event)
{
    rg_input_process_event_ex(&state->input, event, &state->events);
}

static void rg_bench_input(RgBench* state, const BenchScene* scene)
{
    rg_input_begin_frame(&state->input);
    rg_input_event_queue_reset(&state->events, SDL_KMOD_NONE);
    /* The event API fills the ordered queue; rg_input_sample normally supplies
     * this polled snapshot. Replay both halves without reading real desktop
     * input, so hover/press handling and ordered text see the same events. */
    state->input.mouse_x = (int)scene->mouse_x;
    state->input.mouse_y = (int)scene->mouse_y;
    state->input.current_mouse[RG_MOUSE_BUTTON_LEFT] = scene->mouse_down != 0;
    state->input.current_keyboard[SDL_SCANCODE_BACKSPACE] = scene->text_action == 2;
    SDL_Event event;
    memset(&event, 0, sizeof(event));
    event.type = SDL_EVENT_MOUSE_MOTION;
    event.motion.x = scene->mouse_x;
    event.motion.y = scene->mouse_y;
    rg_bench_event(state, &event);
    if (state->previous_mouse != scene->mouse_down) {
        memset(&event, 0, sizeof(event));
        event.type = scene->mouse_down ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
        event.button.button = SDL_BUTTON_LEFT;
        event.button.down = scene->mouse_down != 0;
        event.button.clicks = 1u;
        event.button.x = scene->mouse_x;
        event.button.y = scene->mouse_y;
        rg_bench_event(state, &event);
    }
    state->previous_mouse = scene->mouse_down;
    const int want_backspace = scene->text_action == 2;
    if (state->backspace_down != want_backspace) {
        memset(&event, 0, sizeof(event));
        event.type = want_backspace ? SDL_EVENT_KEY_DOWN : SDL_EVENT_KEY_UP;
        event.key.scancode = SDL_SCANCODE_BACKSPACE;
        event.key.key = SDLK_BACKSPACE;
        event.key.down = want_backspace != 0;
        rg_bench_event(state, &event);
    }
    state->backspace_down = want_backspace;
    if (scene->text_action == 1) {
        memset(&event, 0, sizeof(event));
        event.type = SDL_EVENT_TEXT_INPUT;
        event.text.text = "x";
        rg_bench_event(state, &event);
    }
}

static int rg_bench_dock_layout(RgBench* state)
{
    RgGuiDockLayout layout = {state->dockspace.id, 1u, 7u, 4u};
    RgGuiDockLayoutNode nodes[8] = {0};
    RgGuiDockLayoutTab tabs[5] = {0};
    nodes[1] = (RgGuiDockLayoutNode){.child_a=2u, .child_b=3u,
        .split_ratio=0.5f, .split=RG_GUI_DOCK_SPLIT_VERT};
    nodes[2] = (RgGuiDockLayoutNode){.parent=1u, .child_a=4u, .child_b=5u,
        .split_ratio=0.5f, .split=RG_GUI_DOCK_SPLIT_HORZ};
    nodes[3] = (RgGuiDockLayoutNode){.parent=1u, .child_a=6u, .child_b=7u,
        .split_ratio=0.5f, .split=RG_GUI_DOCK_SPLIT_HORZ};
    for (u32 i = 0u; i < 4u; i++) {
        const u32 node = rg_bench_dock_leaves[i];
        nodes[node].parent = node < 6u ? 2u : 3u;
        nodes[node].tab_head = i + 1u;
        nodes[node].tab_count = 1u;
        nodes[node].active_tab = i + 1u;
        tabs[i + 1u].window_id = (RgGuiId)(600u + i);
    }
    if (!rg_gui_dock_layout_load(&state->gui, &state->dockspace, &layout, nodes, tabs))
        return rg_bench_error("could not restore the four-panel dock layout");
    /* Serialized layouts contain stable window IDs, not process pointers.
     * Supply the corresponding caller-owned public window states on restore. */
    for (u32 i = 0u; i < 4u; i++) {
        RgGuiWindowState* window = &state->windows[i];
        window->docked = 1;
        window->dockspace_id = state->dockspace.id;
        window->dock_node = rg_bench_dock_leaves[i];
        window->dock_tab = i + 1u;
        window->dock_drag_pending = 0;
        window->open = 1;
    }
    return 1;
}

static int rg_bench_build(void* opaque, const BenchScene* scene, BenchStats* stats)
{
    RgBench* state = (RgBench*)opaque;
    RgGuiContext* gui = &state->gui;
    state->current_case = scene->case_id;
    rg_bench_input(state, scene);
    rg_gui_begin_frame_ex(gui, &state->input, &state->events, 0u, 1.0f / 60.0f);
    gui->cursor_visible = 1;
    gui->cursor_blink_timer = 0.0f;
    rg_gui_window_set_bounds(gui, (RgGuiRect){0, 0, BENCH_WIDTH, BENCH_HEIGHT});
    switch (scene->case_id) {
    case BENCH_STATIC:
    case BENCH_DYNAMIC:
        for (u32 i = 0u; i < BENCH_LABELS; i++) {
            /* Native labels add style.padding on X. Compensate in the rect so
             * text begins at exactly the same (16+310*column,12+22*row). */
            RgGuiRect rect = {12.0f + 310.0f * (f32)(i / 32u),
                             12.0f + 22.0f * (f32)(i % 32u), 300.0f, 16.0f};
            if (scene->case_id == BENCH_STATIC) rg_gui_label_static(gui, scene->labels[i], rect);
            else rg_gui_label(gui, scene->labels[i], rect);
        }
        stats->widgets = BENCH_LABELS;
        break;
    case BENCH_INSPECTOR:
    case BENCH_MIXED:
        if (scene->case_id == BENCH_INSPECTOR || !state->checks_initialized) {
            memcpy(state->checks, scene->checks, sizeof(state->checks));
            state->checks_initialized = 1;
        }
        memcpy(state->values, scene->values, sizeof(state->values));
        for (u32 i = 0u; i < 32u; i++) {
            char label[32];
            snprintf(label, sizeof(label), "Enable%02u", i);
            rg_gui_checkbox(gui, label, &state->checks[i],
                            (RgGuiRect){16, 16.0f + 22.0f * i, 280, 20}, (RgGuiId)(100u + i));
            rg_gui_slider_float(gui, NULL, &state->values[i], 0.0f, 1.0f,
                                (RgGuiRect){340, 16.0f + 22.0f * i, 300, 20}, (RgGuiId)(200u + i));
            snprintf(label, sizeof(label), "Value%02u", i);
            rg_gui_label(gui, label, (RgGuiRect){642, 16.0f + 22.0f * i, 180, 20});
        }
        stats->widgets = 64u;
        break;
    case BENCH_LIST:
    case BENCH_BROWSER: {
        const int browser = scene->case_id == BENCH_BROWSER;
        if (browser) {
            SDL_strlcpy(state->filter, scene->filter, sizeof(state->filter));
            int submit = 0;
            rg_gui_text_input_ex(gui, NULL, state->filter, sizeof(state->filter),
                                (RgGuiRect){16, 12, 600, 24}, 300u,
                                RG_GUI_TEXT_INPUT_NO_LABEL, &submit);
        }
        for (u32 i = 0u; i < BENCH_ROWS; i++) {
            int row = (int)(scene->first_row + i);
            if (rg_gui_selectable(gui, scene->rows[i], state->selected == row,
                                  (RgGuiRect){16, (browser ? 48.0f : 32.0f) + 22.0f * i, 600, 20},
                                  (RgGuiId)(1000u + (u32)row)))
                state->selected = row;
        }
        stats->widgets = BENCH_ROWS + (browser ? 1u : 0u);
        stats->visible_rows = BENCH_ROWS;
        break;
    }
    case BENCH_EDITOR: {
        /* The native RG editor includes two paddings in its line advance.
         * With zero editor padding both libraries advance by the same 16px;
         * its 1px border then matches ImGui's editor-only FramePadding(1,1). */
        const f32 saved_padding = gui->style.padding;
        gui->style.padding = 0.0f;
        rg_gui_text_area(gui, &state->editor, state->document, sizeof(state->document),
                         (RgGuiRect){16, 16, 1248, 768}, 400u);
        gui->style.padding = saved_padding;
        stats->widgets = 1u;
        break;
    }
    case BENCH_DOCKS:
        rg_gui_dockspace_begin(gui, &state->dockspace,
                              (RgGuiRect){0, 0, BENCH_WIDTH, BENCH_HEIGHT}, 500u);
        if (!state->docks_initialized || scene->frame % 256u == 0u) {
            if (!rg_bench_dock_layout(state)) return 0;
            state->docks_initialized = 1;
            /* Loading changes topology; recompute this frame's bounds. */
            rg_gui_dock_layout(gui, state->dockspace.root, state->dockspace.rect);
        }
        rg_gui_dockspace_end(gui, &state->dockspace);
        for (u32 panel = 0u; panel < 4u; panel++) {
            if (!rg_gui_window_begin(gui, &state->windows[panel], rg_bench_titles[panel],
                    (RgGuiRect){0, 0, 640, 400}, 100, 100, 2,
                    RG_GUI_WINDOW_DOCKABLE | RG_GUI_WINDOW_MOVABLE | RG_GUI_WINDOW_RESIZABLE,
                    (RgGuiId)(600u + panel)))
                return rg_bench_error("a dock panel was not visible");
            const f32 origin_x = gui->layout.x;
            const f32 origin_y = gui->layout.cursor_y;
            for (u32 item = 0u; item < 32u; item++) {
                RgGuiRect rect = {origin_x + 310.0f * (f32)(item / 16u),
                                 origin_y + 22.0f * (f32)(item % 16u), 300, 16};
                rg_gui_label(gui, scene->labels[panel * 32u + item], rect);
                stats->widgets++;
            }
            rg_gui_window_end(gui, &state->windows[panel]);
        }
        break;
    default:
        return rg_bench_error("unknown workload");
    }
    rg_gui_end_frame(gui);
    if (gui->diagnostics.flags) return rg_bench_error("native UI diagnostics");
    rg_gui_renderer_begin_frame(&state->text);
    const RgGuiDrawList* list = rg_gui_draw_list(gui);
    if (!rg_gui_gpu_prepare(&state->gpu, &state->text, list, list->count))
        return rg_bench_error("indexed preparation failed");
    const RgGuiRendererStats* prepared = rg_gui_renderer_stats(&state->text);
    if (!prepared || prepared->diagnostic_flags || prepared->frame_dropped_runs ||
        prepared->frame_dropped_glyphs)
        return rg_bench_error("renderer dropped text or reported diagnostics");
    stats->commands = list->count;
    stats->glyphs = prepared->frame_glyphs;
    return 1;
}

static int rg_bench_upload(void* opaque, SDL_GPUCommandBuffer* command, uint32_t slot)
{
    RgBench* state = (RgBench*)opaque;
    if (slot >= state->flights) return rg_bench_error("invalid flight slot");
    state->active_slot = slot;
    RgGpuUploadRing* ring = &state->rings[slot];
    RgGuiGpuUpload* upload = &state->uploads[slot];
    rg_gpu_upload_ring_begin(ring, 1);
    const int staged = rg_gui_gpu_stage_upload(&state->gpu, &state->text, ring, upload);
    rg_gpu_upload_ring_end(ring);
    if (!staged) return rg_bench_error("upload staging failed");
    if (upload->frame_buffer_index != slot) return rg_bench_error("renderer and flight slots diverged");
    if (getenv("RG_BENCH_UPLOAD_STATS")) {
        const RgGuiRendererStats* rs = rg_gui_renderer_stats(&state->text);
        fprintf(stderr, "upload runs=%u quads=%u ranges=%u full=%u geometry=%u frame=%u hits=%u misses=%u evictions=%u\n",
                upload->text.runs.size, upload->text.cached_quads.size, upload->text.cache_range_count,
                upload->text.full_cache_upload, upload->geometry.size, upload->frame.size,
                rs->frame_cache_hits, rs->frame_cache_misses, rs->frame_cache_evictions);
    }
    if (upload->has_text || upload->has_geometry) {
        SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
        if (!copy) return 0;
        rg_gui_gpu_encode_upload(&state->gpu, copy, ring, upload);
        SDL_EndGPUCopyPass(copy);
    }
    if (!rg_gui_gpu_dispatch_upload(&state->gpu, command, upload) ||
        !rg_gui_gpu_upload_ready(&state->gpu, upload))
        return rg_bench_error("upload packet is not ready");
    return 1;
}

static int rg_bench_draw(void* opaque, SDL_GPUCommandBuffer* command,
                         SDL_GPURenderPass* pass, BenchStats* stats)
{
    RgBench* state = (RgBench*)opaque;
    RgGuiGpuDrawDesc desc = {0};
    desc.output_width = BENCH_WIDTH;
    desc.output_height = BENCH_HEIGHT;
    desc.viewport = (SDL_Rect){0, 0, BENCH_WIDTH, BENCH_HEIGHT};
    RgGuiGpuStats draw = rg_gui_gpu_draw(&state->gpu, command, pass, &desc,
                                        &state->uploads[state->active_slot]);
    if (!draw.draw_calls) return rg_bench_error("no indexed draws");
    stats->draw_calls = draw.draw_calls;
    stats->vertices = draw.vertices;
    return 1;
}

static void rg_bench_commit(void* opaque, uint32_t slot)
{
    RgBench* state = (RgBench*)opaque;
    rg_gui_gpu_upload_commit(&state->gpu, &state->uploads[slot]);
}

static int rg_bench_inspect(void* opaque, BenchStats* stats)
{
    RgBench* state = (RgBench*)opaque;
    stats->checked_bits = 0u;
    for (u32 i = 0u; i < 32u; i++)
        if (state->checks[i]) stats->checked_bits |= UINT32_C(1) << i;
    stats->first_slider = state->values[0];
    stats->selected_row = state->selected;
    if (state->current_case == BENCH_EDITOR) {
        const size_t length = strlen(state->document);
        stats->editor_length = (uint32_t)length;
        stats->editor_hash = bench_hash_bytes(state->document, length);
        if (!state->editor_cache.valid || state->editor_cache.length != length ||
            memcmp(state->editor_cache.text, state->document, length))
            return rg_bench_error("editor snapshot does not match live text");
    }
    return 1;
}

static void rg_bench_destroy(void* opaque)
{
    RgBench* state = (RgBench*)opaque;
    if (!state) return;
    for (u32 slot = 0u; slot < state->flights; slot++)
        rg_gpu_upload_ring_destroy(&state->rings[slot]);
    rg_gui_gpu_destroy(&state->gpu);
    free(state->text_memory);
    free(state->gui_memory);
    free(state);
}

int bench_rg_init(BenchAdapter* adapter, const BenchInit* init)
{
    if (!adapter || !init || !init->device || !init->atlas || !init->font ||
        !init->flights || init->flights > 3u)
        return rg_bench_error("invalid initialization arguments");
    RgBench* state = (RgBench*)calloc(1u, sizeof(*state));
    if (!state) return rg_bench_error("out of memory");
    state->flights = init->flights;
    state->selected = -1;
    RgTextFontLoadDesc load = {0};
    load.data = init->font->font_data;
    load.data_size = init->font->font_data_size;
    load.glyphs = state->glyphs;
    load.glyph_capacity = RG_ARRAY_COUNT(state->glyphs);
    load.kernings = state->kernings;
    load.kerning_capacity = RG_ARRAY_COUNT(state->kernings);
    if (!rg_text_font_load_rgfont(&state->font, &load)) goto failed;
    /* Font import is outside timing. Preserve loader indexes while disabling
     * all kerning in both the direct and shared-lookup paths. */
    state->font.kernings = NULL;
    state->font.kerning_count = 0u;
    for (u32 i = 0u; i < state->font.glyph_count; i++) {
        state->glyphs[i].kerning_first = 0u;
        state->glyphs[i].kerning_count = 0u;
    }
    if (!rg_gui_text_lookup_init(&state->lookup, &state->font)) goto failed;
    RgGuiInitDesc gui_desc = {0};
    gui_desc.font = &state->font;
    gui_desc.text_lookup = &state->lookup;
    gui_desc.max_draw_cmds = 4096u;
    gui_desc.text_buffer_size = 128u * 1024u;
    const size_t gui_bytes = rg_gui_memory_required(&gui_desc);
    state->gui_memory = malloc(gui_bytes);
    if (!gui_bytes || !state->gui_memory) goto failed;
    RgArena gui_arena = {(char*)state->gui_memory, gui_bytes, 0u, gui_bytes};
    if (!rg_gui_init(&state->gui, &gui_arena, &gui_desc)) goto failed;
    state->gui.style.color_text = rg_gui_color(1, 1, 1, 1);
    state->gui.style.text_height = 16;
    state->gui.style.value_decimals = 3;
    RgGuiRendererInitDesc text_desc = {0};
    text_desc.font = &state->font;
    text_desc.text_lookup = &state->lookup;
    text_desc.page_quads = 32u;
    text_desc.limits = rg_gui_renderer_limits_default();
    text_desc.limits.max_cached_runs = 1024u;
    text_desc.limits.hash_slot_count = 2048u;
    text_desc.limits.max_cached_quads = 65536u;
    text_desc.limits.max_frame_instances = 32768u;
    text_desc.limits.max_frame_runs = 1024u;
    text_desc.limits.max_batches = 1024u;
    text_desc.limits.text_capacity = 256u * 1024u;
    const size_t text_bytes = rg_gui_renderer_memory_required_ex(&text_desc);
    if (text_bytes == SIZE_MAX) goto failed;
    state->text_memory = malloc(text_bytes);
    if (!state->text_memory) goto failed;
    RgArena text_arena = {(char*)state->text_memory, text_bytes, 0u, text_bytes};
    if (!rg_gui_renderer_init(&state->text, &text_arena, &text_desc)) goto failed;
    RgGuiGpuDesc gpu_desc = {0};
    gpu_desc.device = init->device;
    gpu_desc.target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
    gpu_desc.shader_root = init->shader_root;
    gpu_desc.atlas_texture = init->atlas;
    gpu_desc.atlas_width = (u32)init->font->width;
    gpu_desc.atlas_height = (u32)init->font->height;
    gpu_desc.max_cached_quads = text_desc.limits.max_cached_quads;
    gpu_desc.max_runs = text_desc.limits.max_frame_runs;
    gpu_desc.max_text_instances = text_desc.limits.max_frame_instances;
    gpu_desc.max_geometry_vertices = 32768u;
    gpu_desc.max_items = 4096u;
    gpu_desc.frame_buffer_count = init->flights;
    gpu_desc.min_filter = SDL_GPU_FILTER_LINEAR;
    gpu_desc.mag_filter = SDL_GPU_FILTER_LINEAR;
    if (!rg_gui_gpu_create(&state->gpu, &gpu_desc)) goto failed;
    for (u32 slot = 0u; slot < state->flights; slot++)
        if (!rg_gpu_upload_ring_init(&state->rings[slot], init->device,
                                     rg_gui_gpu_upload_ring_size_required(&state->gpu))) goto failed;
    rg_input_init(&state->input);
    rg_input_event_queue_init(&state->events, state->event_storage,
                              RG_ARRAY_COUNT(state->event_storage),
                              state->event_text, sizeof(state->event_text));
    SDL_strlcpy(state->document, init->initial_document ? init->initial_document : "",
                sizeof(state->document));
    rg_gui_text_area_cache_init(&state->editor_cache, state->snapshot, sizeof(state->snapshot),
                                state->lines, RG_ARRAY_COUNT(state->lines));
    state->editor.layout_cache = &state->editor_cache;
    *adapter = (BenchAdapter){state, rg_bench_build, rg_bench_upload, rg_bench_draw,
                              rg_bench_commit, rg_bench_inspect, rg_bench_destroy};
    return 1;
failed:
    rg_bench_destroy(state);
    return 0;
}
