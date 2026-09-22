/* CPU-only rg_gui/rg_text A/B benchmark. Run from the rg_gui repository root.
 * Compile this identical file twice, changing only the header include roots.
 * See benchmarks/README.md and run.py for a reproducible Windows comparison.
 * Frontend cases explicitly measure labels as auto-sized menus/tabs do; simple
 * rg_gui_label() calls alone do not measure. No window, GPU, or atlas upload.
 * All fonts go through the loader, activating optimized lookup when available.
 */
#define RG_SPRINTF_NO_ASM 1
#define RGINLINE static inline
#include "rg_gui_renderer.h"
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define LABEL_COUNT 128u
#define LABEL_BYTES 160u
#define SAMPLE_COUNT 7u
#define DYNAMIC_CYCLE 16u
#define SYNTH_GLYPHS 4096u
#define SYNTH_KERNINGS 4096u

typedef struct Bench {
    RgTextFont font;
    RgTextGlyph* glyphs;
    RgTextKerning* kernings;
    RgGuiContext gui;
    RgGuiRenderer renderer;
    RgInputState input;
    void* gui_memory;
    void* renderer_memory;
    size_t renderer_memory_size;
    char labels[LABEL_COUNT][LABEL_BYTES];
    size_t lengths[LABEL_COUNT];
    RgGuiDrawCmd commands[LABEL_COUNT];
    RgGuiDrawList list;
    double width_sum;
} Bench;

static volatile double sink;
static double timer_scale;

static double now_seconds(void) {
    LARGE_INTEGER count;
    QueryPerformanceCounter(&count);
    return (double)count.QuadPart * timer_scale;
}

static void fail(const char* message) {
    fprintf(stderr, "benchmark error: %s\n", message);
    exit(1);
}

static void* allocate(size_t bytes) {
    void* p = calloc(1u, bytes);
    if (!p) fail("allocation");
    return p;
}

static char* read_file(const char* path, size_t* size) {
    FILE* file = fopen(path, "rb");
    if (!file) fail("open font (run from rg_gui root)");
    if (fseek(file, 0, SEEK_END)) fail("seek font");
    long length = ftell(file);
    if (length <= 0) fail("font size");
    rewind(file);
    char* data = (char*)allocate((size_t)length + 1u);
    if (fread(data, 1u, (size_t)length, file) != (size_t)length) fail("read font");
    fclose(file);
    *size = (size_t)length;
    return data;
}

static char* synthetic_font(size_t* size) {
    const size_t capacity = 1024u * 1024u;
    char* data = (char*)allocate(capacity);
    size_t used = (size_t)sprintf(data,
        "rgfont 1\natlas 1024 1024\nline_height 16\nascent 13\ndescent 3\nfallback 63\n");
    for (u32 i = 0; i < SYNTH_GLYPHS; ++i) {
        used += (size_t)sprintf(data + used, "glyph %u %u %u 7 12 0 1 8\n",
            32u + i, (i % 64u) * 8u, (i / 64u) * 16u);
    }
    for (u32 i = 0; i < SYNTH_KERNINGS; ++i) {
        used += (size_t)sprintf(data + used, "kerning %u %u -1\n",
            32u + i, 32u + ((i + 1u) % SYNTH_GLYPHS));
    }
    if (used >= capacity) fail("synthetic font buffer");
    *size = used;
    return data;
}

static size_t utf8_encode(char* out, u32 cp) {
    if (cp < 128u) { out[0] = (char)cp; return 1u; }
    if (cp < 2048u) {
        out[0] = (char)(0xc0u | (cp >> 6u));
        out[1] = (char)(0x80u | (cp & 63u));
        return 2u;
    }
    out[0] = (char)(0xe0u | (cp >> 12u));
    out[1] = (char)(0x80u | ((cp >> 6u) & 63u));
    out[2] = (char)(0x80u | (cp & 63u));
    return 3u;
}

static void init_renderer(Bench* b) {
    RgGuiRendererInitDesc desc = {0};
    desc.font = &b->font;
    desc.limits = (RgGuiRendererLimits){512u, 1024u, 128u * 1024u,
                                       32768u, 32768u, 16u};
    desc.page_quads = 32u;
    RgArena arena = {(char*)b->renderer_memory, b->renderer_memory_size, 0u,
                    b->renderer_memory_size};
    if (!rg_gui_renderer_init(&b->renderer, &arena, &desc)) fail("renderer init");
}

static void init_bench(Bench* b, int synthetic) {
    memset(b, 0, sizeof(*b));
    size_t data_size = 0u;
    char* data = synthetic ? synthetic_font(&data_size) :
        read_file("examples/assets/inter_medium_16.font", &data_size);
    b->glyphs = (RgTextGlyph*)allocate(sizeof(RgTextGlyph) * SYNTH_GLYPHS);
    b->kernings = (RgTextKerning*)allocate(sizeof(RgTextKerning) * SYNTH_KERNINGS);
    RgTextFontLoadDesc load = {0};
    load.data = data; load.data_size = data_size;
    load.glyphs = b->glyphs; load.glyph_capacity = SYNTH_GLYPHS;
    load.kernings = b->kernings; load.kerning_capacity = SYNTH_KERNINGS;
    if (!rg_text_font_load_rgfont(&b->font, &load)) fail("font load");
    free(data);

    RgGuiInitDesc gui_desc = {0};
    gui_desc.font = &b->font;
    gui_desc.max_draw_cmds = 1024u;
    gui_desc.text_buffer_size = 64u * 1024u;
    gui_desc.text_measure_cache_size = 1024u;
    gui_desc.text_length_cache_size = 1024u;
    size_t gui_size = rg_gui_memory_required(&gui_desc);
    b->gui_memory = allocate(gui_size);
    RgArena gui_arena = {(char*)b->gui_memory, gui_size, 0u, gui_size};
    if (!rg_gui_init(&b->gui, &gui_arena, &gui_desc)) fail("GUI init");

    RgGuiRendererInitDesc renderer_desc = {0};
    renderer_desc.limits = (RgGuiRendererLimits){512u, 1024u, 128u * 1024u,
                                               32768u, 32768u, 16u};
    renderer_desc.page_quads = 32u;
    size_t renderer_size = rg_gui_renderer_memory_required(&renderer_desc.limits, 32u);
    if (renderer_size == SIZE_MAX) fail("renderer size");
    b->renderer_memory = allocate(renderer_size);
    b->renderer_memory_size = renderer_size;
    init_renderer(b);

    for (u32 i = 0; i < LABEL_COUNT; ++i) {
        size_t n;
        if (synthetic) {
            n = (size_t)sprintf(b->labels[i], "Item %03u: ", i);
            for (u32 c = 0u; c < 24u; ++c)
                n += utf8_encode(b->labels[i] + n, 1024u + ((i * 17u + c) % 2048u));
            memcpy(b->labels[i] + n, " Value 0000", 12u);
        } else {
            sprintf(b->labels[i], "Layer %03u: Ambient visibility 100%% - 0000", i);
        }
        b->lengths[i] = strlen(b->labels[i]);
        RgGuiDrawCmd* cmd = &b->commands[i];
        cmd->type = RG_GUI_CMD_TEXT;
        cmd->data.text.text = b->labels[i];
        cmd->data.text.pos.x = (f32)(i % 4u) * 400.0f;
        cmd->data.text.pos.y = (f32)(i / 4u) * 20.0f;
        cmd->data.text.scale = 1.0f;
        cmd->data.text.color = (rg_vec4){.x = 1, .y = 1, .z = 1, .w = 1};
    }
    b->list = (RgGuiDrawList){b->commands, LABEL_COUNT, LABEL_COUNT};
}

static void change_labels(Bench* b, u32 frame) {
    /* Identical fixed-cost numeric update in old/new builds, no formatting I/O. */
    for (u32 i = 0; i < LABEL_COUNT; ++i) {
        char* last = b->labels[i] + b->lengths[i] - 4u;
        u32 value = (frame + i) % 10000u;
        last[3] = (char)('0' + value % 10u); value /= 10u;
        last[2] = (char)('0' + value % 10u); value /= 10u;
        last[1] = (char)('0' + value % 10u); value /= 10u;
        last[0] = (char)('0' + value % 10u);
    }
}

/* Modes: 0 frontend static, 1 frontend dynamic, 2 renderer static warm,
 * 3 renderer content warm, 4 renderer cold, 5 renderer initialization. */
__declspec(noinline) static void frame(Bench* b, int mode, u32 index) {
    if (mode == 5) {
        init_renderer(b);
        sink += (double)b->renderer.initialized + b->renderer.core.ascii_kerning['A' * 128u + 'V'];
    } else if (mode <= 1) {
        /* A complete fixed cycle per sample keeps old/new input distributions
         * identical even though their calibrated iteration counts differ. */
        if (mode == 1) change_labels(b, index % DYNAMIC_CYCLE);
        rg_gui_begin_frame(&b->gui, &b->input, 1.0f / 60.0f);
        double sum = 0.0;
        for (u32 i = 0; i < LABEL_COUNT; ++i) {
            size_t length = rg_gui_text_len_label(&b->gui, b->labels[i], mode);
            f32 width = rg_gui_text_measure_label(&b->gui, b->labels[i], length, mode);
            RgGuiRect rect = {(f32)(i % 4u) * 400.0f, (f32)(i / 4u) * 20.0f,
                             width + 8.0f, 20.0f};
            if (mode == 0) rg_gui_label_static(&b->gui, b->labels[i], rect);
            else rg_gui_label(&b->gui, b->labels[i], rect);
            sum += width;
        }
        rg_gui_end_frame(&b->gui);
        if (b->gui.diagnostics.flags) fail("GUI diagnostics");
        if (rg_gui_draw_list(&b->gui)->count != LABEL_COUNT) fail("GUI draw count");
        b->width_sum = sum;
        sink += sum + (double)rg_gui_draw_list(&b->gui)->count;
    } else {
        rg_gui_renderer_begin_frame(&b->renderer);
        if (!rg_gui_renderer_prepare(&b->renderer, &b->list, b->list.count))
            fail("renderer prepare");
        const RgGuiRendererPrepared* p = rg_gui_renderer_prepared(&b->renderer);
        const RgGuiRendererStats* stats = rg_gui_renderer_stats(&b->renderer);
        if (stats->diagnostic_flags || stats->frame_dropped_runs || stats->frame_cache_bypasses)
            fail("renderer diagnostic/drop/bypass");
        sink += (double)p->glyph_count + (double)p->run_count;
    }
}

static double sample(Bench* b, int mode, u32 frames) {
    if (mode != 4) {
        double start = now_seconds();
        for (u32 i = 0; i < frames; ++i) frame(b, mode, i);
        return now_seconds() - start;
    }
    /* No font init, allocation, cache reset, or setup charged to cold prepare. */
    double total = 0.0;
    for (u32 i = 0; i < frames; ++i) {
        rg_gui_renderer_clear_cache(&b->renderer);
        double start = now_seconds();
        frame(b, mode, i);
        total += now_seconds() - start;
    }
    return total;
}

static u64 hash_u32(u64 h, u32 value) { return (h ^ value) * 1099511628211ull; }
static u64 hash_float(u64 h, f32 value) {
    u32 bits;
    memcpy(&bits, &value, sizeof(bits));
    return hash_u32(h, bits);
}

static u64 geometry_checksum(Bench* b) {
    const RgGuiRendererPrepared* p = rg_gui_renderer_prepared(&b->renderer);
    u64 hash = 1469598103934665603ull;
    for (u32 r = 0; r < p->run_count; ++r) {
        const RgGuiRendererRun* run = &p->runs[r];
        for (u32 q = 0; q < run->quad_count; ++q) {
            const RgGuiRendererBaseCachedQuad* quad = b->renderer.core.cache_quads + run->first_cached_quad + q;
            hash = hash_float(hash, run->x + quad->x);
            hash = hash_float(hash, run->y + quad->y);
            hash = hash_float(hash, quad->w);
            hash = hash_float(hash, quad->h);
            hash = hash_u32(hash, (u32)quad->u0 | ((u32)quad->v0 << 16u));
            hash = hash_u32(hash, (u32)quad->u1 | ((u32)quad->v1 << 16u));
        }
    }
    return hash;
}

static u64 lookup_checksum(Bench* b) {
    u64 hash = 1469598103934665603ull;
    for (u32 i = 0; i < 128u; ++i) {
        const RgTextGlyph* glyph = b->renderer.core.ascii_glyphs[i];
        hash = hash_u32(hash, glyph ? glyph->codepoint : 0xffffffffu);
        hash = hash_u32(hash, glyph ? (u32)glyph->x_advance : 0u);
    }
    for (u32 i = 0; i < 128u * 128u; ++i)
        hash = hash_u32(hash, (u32)b->renderer.core.ascii_kerning[i]);
    return hash;
}

static int compare_double(const void* a, const void* b) {
    double av = *(const double*)a, bv = *(const double*)b;
    return (av > bv) - (av < bv);
}

static void run_case(Bench* b, const char* font_name, int mode) {
    static const char* names[] = {"frontend_measured_labels_static_warm", "frontend_measured_labels_dynamic",
        "renderer_static_warm", "renderer_content_warm", "renderer_cold", "renderer_init"};
    change_labels(b, 0u);
    memset(b->gui.text_measure_cache, 0, b->gui.text_measure_cache_capacity * sizeof(*b->gui.text_measure_cache));
    memset(b->gui.text_length_cache, 0, b->gui.text_length_cache_capacity * sizeof(*b->gui.text_length_cache));
    rg_gui_renderer_clear_cache(&b->renderer);
    for (u32 i = 0; i < LABEL_COUNT; ++i)
        b->commands[i].data.text.cache_identity = mode == 2 ? (uintptr_t)b->labels[i] : 0u;
    for (u32 i = 0; i < 4u; ++i) {
        if (mode == 4) rg_gui_renderer_clear_cache(&b->renderer);
        frame(b, mode, i);
    }
    double calibration = sample(b, mode, 8u);
    u32 frames = calibration > 0.0 ? (u32)(0.035 * 8.0 / calibration) : 8u;
    if (frames < 2u) frames = 2u;
    if (frames > 20000u) frames = 20000u;
    if (mode == 1) frames = ((frames + DYNAMIC_CYCLE - 1u) / DYNAMIC_CYCLE) * DYNAMIC_CYCLE;
    double times[SAMPLE_COUNT];
    printf("{\"kind\":\"samples\",\"font\":\"%s\",\"case\":\"%s\",\"labels\":%u,\"frames_per_sample\":%u,\"ns_per_frame\":[",
        font_name, names[mode], LABEL_COUNT, frames);
    for (u32 i = 0; i < SAMPLE_COUNT; ++i) {
        times[i] = sample(b, mode, frames) * 1e9 / (double)frames;
        printf("%s%.3f", i ? "," : "", times[i]);
    }
    printf("]}\n");
    qsort(times, SAMPLE_COUNT, sizeof(double), compare_double);
    /* Canonical untimed output checks are independent of calibration count.
     * Dynamic width checksums include every frame of the complete fixed cycle. */
    change_labels(b, 0u);
    if (mode == 4) rg_gui_renderer_clear_cache(&b->renderer);
    double width_checksum = 0.0;
    if (mode == 1) {
        for (u32 i = 0u; i < DYNAMIC_CYCLE; ++i) {
            frame(b, mode, i);
            width_checksum += b->width_sum;
        }
    } else {
        frame(b, mode, 0u);
        if (mode == 0) width_checksum = b->width_sum;
    }
    const RgGuiRendererStats* stats = rg_gui_renderer_stats(&b->renderer);
    if (mode >= 2 && mode <= 4 && ((mode == 4 && stats->frame_cache_misses != LABEL_COUNT) ||
                     (mode != 4 && stats->frame_cache_hits != LABEL_COUNT)))
        fail("unexpected cache state");
    printf("{\"kind\":\"result\",\"font\":\"%s\",\"case\":\"%s\",\"median_ns_per_frame\":%.3f,\"min_ns_per_frame\":%.3f,\"max_ns_per_frame\":%.3f,\"width_checksum\":%.3f,\"geometry_checksum\":\"%llu\",\"cache_hits\":%u,\"cache_misses\":%u,\"glyphs\":%u}\n",
        font_name, names[mode], times[SAMPLE_COUNT / 2u], times[0], times[SAMPLE_COUNT - 1u],
        width_checksum, mode == 5 ? lookup_checksum(b) : (mode >= 2 ? geometry_checksum(b) : 0ull),
        mode >= 2 ? stats->frame_cache_hits : 0u, mode >= 2 ? stats->frame_cache_misses : 0u,
        mode >= 2 ? stats->frame_glyphs : 0u);
    fflush(stdout);
}

int main(void) {
    LARGE_INTEGER frequency;
    if (!QueryPerformanceFrequency(&frequency)) fail("performance counter");
    timer_scale = 1.0 / (double)frequency.QuadPart;
    Bench* b = (Bench*)allocate(sizeof(Bench));
    for (int synthetic = 0; synthetic <= 1; ++synthetic) {
        init_bench(b, synthetic);
        printf("{\"kind\":\"font\",\"font\":\"%s\",\"glyph_count\":%u,\"kerning_count\":%u,\"label_copy_default\":%u}\n",
            synthetic ? "synthetic4096_mixed_utf8" : "inter95_ascii", b->font.glyph_count,
            b->font.kerning_count, (unsigned)RG_GUI_LABEL_COPY);
        for (int mode = 0; mode < 6; ++mode)
            run_case(b, synthetic ? "synthetic4096_mixed_utf8" : "inter95_ascii", mode);
        free(b->gui_memory); free(b->renderer_memory); free(b->glyphs); free(b->kernings);
    }
    free(b);
    return sink == 0.0 ? 1 : 0;
}
