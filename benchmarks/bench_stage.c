/* Host-only staging benchmark: rg_gui_gpu_stage_upload of one prepared frame
 * into a mapped ring, alone and together with rg_gui_gpu_prepare, for 128 and
 * 1,024 solid rectangles (one merged item) and images that alternate between
 * two textures (one item and batch each). No window or GPU device is used.
 * The source compiles against older headers, so a before/after comparison
 * builds it twice; see benchmarks/README.md. Emits one JSON object per case. */
#define RG_SPRINTF_NO_ASM 1
#define RGINLINE static inline
#include "rg_gui_gpu.h"
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define COMMANDS 1024u
static volatile u64 sink;
static double clock_scale;
static double now(void) { return (double)SDL_GetPerformanceCounter() * clock_scale; }
static void fail(const char* reason) { fprintf(stderr, "error: %s\n", reason); exit(1); }
static int cmp(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }
static u64 hash(const void* p, size_t n) { const u8* b = p; u64 h = 14695981039346656037ull; for (size_t i = 0; i < n; i++) h = (h ^ b[i]) * 1099511628211ull; return h; }

typedef struct Bench {
	RgGuiGpuRenderer gpu;
	RgGuiRenderer text;
	RgTextFont font;
	RgTextGlyph glyph;
	RgGuiDrawCmd commands[COMMANDS];
	RgGuiDrawList list;
	void* text_memory;
	u8* ring_memory;
	RgGpuUploadRing ring;
} Bench;

static void init(Bench* b, int images) {
	memset(b, 0, sizeof(*b));
	b->font.metrics.atlas_width = 64; b->font.metrics.atlas_height = 64; b->font.metrics.line_height = 16;
	b->glyph.codepoint = '?'; b->glyph.w = 8; b->glyph.h = 12; b->glyph.x_advance = 8;
	b->font.glyphs = &b->glyph; b->font.glyph_count = 1; b->font.fallback_codepoint = '?';
	RgGuiRendererInitDesc d = {0}; d.font = &b->font;
	d.limits = (RgGuiRendererLimits){.max_cached_runs = 8, .hash_slot_count = 16, .text_capacity = 1024,
	    .max_cached_quads = 64, .max_frame_instances = 8192, .max_batches = 16}; d.page_quads = 32;
	size_t n = rg_gui_renderer_memory_required(&d.limits, 32);
	b->text_memory = malloc(n); if (!b->text_memory) fail("allocation");
	RgArena arena = {(char*)b->text_memory, n, 0, n};
	if (!rg_gui_renderer_init(&b->text, &arena, &d)) fail("text init");
	b->gpu.vertex_capacity = COMMANDS * 6u; b->gpu.item_capacity = COMMANDS;
	b->gpu.vertices = calloc(b->gpu.vertex_capacity, sizeof(*b->gpu.vertices));
	b->gpu.items = calloc(b->gpu.item_capacity, sizeof(*b->gpu.items));
	if (!b->gpu.vertices || !b->gpu.items) fail("geometry allocation");
	b->gpu.text.run_capacity = 64u; b->gpu.text.instance_capacity = 64u; b->gpu.text.cached_quad_capacity = 64u;
	b->gpu.ref_capacity = 64u + b->gpu.vertex_capacity / 3u;
	b->gpu.frame_capacity = (u32)RG_ALIGN_UP(64u * sizeof(RgGuiRendererRun) +
	    b->gpu.vertex_capacity * sizeof(RgGuiGpuVertex), 8u) + b->gpu.ref_capacity * 8u;
	b->gpu.frame_buffer_count = 1u;
	b->ring_memory = malloc((size_t)b->gpu.frame_capacity + 4096u);
	if (!b->ring_memory) fail("ring allocation");
	memset(b->ring_memory, 0, (size_t)b->gpu.frame_capacity + 4096u);
	b->ring.mapped = b->ring_memory; b->ring.size = b->gpu.frame_capacity + 4096u;
	for (u32 i = 0; i < COMMANDS; i++) {
		RgGuiDrawCmd* cmd = &b->commands[i];
		RgGuiRect rect = {(f32)(i % 32) * 20.25f, (f32)(i / 32) * 10.5f, 17.75f, 8.25f};
		if (images) {
			cmd->type = RG_GUI_CMD_IMAGE;
			cmd->data.image.rect = rect;
			cmd->data.image.uv = (RgGuiRect){(f32)(i % 8) * 0.0625f, (f32)(i % 16) * 0.03125f, 0.125f, 0.25f};
			cmd->data.image.color = (rg_vec4){.x = 0.2f, .y = 0.3f, .z = 0.7f, .w = 0.9f};
			cmd->data.image.texture = (RgGuiTexture)(1u + i % 2);
		} else {
			cmd->type = RG_GUI_CMD_RECT;
			cmd->data.rect.rect = rect;
			cmd->data.rect.color = (rg_vec4){.x = 0.2f, .y = 0.3f, .z = 0.7f, .w = 0.9f};
		}
	}
	b->list = (RgGuiDrawList){b->commands, COMMANDS, COMMANDS};
}

static void prepare(Bench* b, u32 count) {
	b->list.count = count;
	rg_gui_renderer_begin_frame(&b->text);
	if (!rg_gui_gpu_prepare(&b->gpu, &b->text, &b->list, count)) fail("prepare");
}

static RG_NOINLINE void stage(Bench* b) {
	RgGuiGpuUpload upload;
	b->ring.offset = 0u;
	if (!rg_gui_gpu_stage_upload(&b->gpu, &b->text, &b->ring, &upload)) fail("stage");
	sink += upload.ref_count + upload.frame.size + upload.batch_count;
	rg_gui_gpu_upload_abort(&b->gpu, &upload);
}

static RG_NOINLINE void frame(Bench* b, u32 count, int full) {
	if (full) prepare(b, count);
	stage(b);
}

int main(void) {
	Uint64 freq = SDL_GetPerformanceFrequency(); if (!freq) fail("counter"); clock_scale = 1.0 / (double)freq;
	static const u32 counts[] = {128, 1024};
	for (int images = 0; images < 2; images++) {
		Bench* b = calloc(1, sizeof(*b)); if (!b) fail("bench"); init(b, images);
		for (int full = 0; full < 2; full++) for (u32 k = 0; k < 2; k++) {
			u32 count = counts[k];
			prepare(b, count);
			RgGuiGpuUpload probe;
			b->ring.offset = 0u;
			if (!rg_gui_gpu_stage_upload(&b->gpu, &b->text, &b->ring, &probe)) fail("probe");
			u64 packed = hash(b->ring_memory + probe.frame.offset, probe.frame.size);
			u32 refs = probe.ref_count, batches = probe.batch_count, bytes = probe.frame.size;
			rg_gui_gpu_upload_abort(&b->gpu, &probe);
			for (u32 i = 0; i < 100; i++) frame(b, count, full);
			double start = now(); for (u32 i = 0; i < 1024; i++) frame(b, count, full); double duration = now() - start;
			u32 frames = (u32)(0.04 / duration * 1024); if (frames < 100) frames = 100; if (frames > 10000000) frames = 10000000;
			double times[7];
			for (u32 s = 0; s < 7; s++) { start = now(); for (u32 i = 0; i < frames; i++) frame(b, count, full); times[s] = (now() - start) * 1e9 / (double)frames; }
			qsort(times, 7, sizeof(double), cmp);
			printf("{\"case\":\"%s_%s_%u\",\"median_ns\":%.3f,\"min_ns\":%.3f,\"max_ns\":%.3f,\"refs\":%u,\"batches\":%u,\"frame_bytes\":%u,\"vertices\":%u,\"packed\":\"%016llx\"}\n",
			       full ? "prepare+stage" : "stage", images ? "images" : "rects", count, times[3], times[0], times[6],
			       refs, batches, bytes, b->gpu.vertex_count, (unsigned long long)packed);
		}
		free(b->gpu.vertices); free(b->gpu.items); free(b->text_memory); free(b->ring_memory);
		for (u32 i = 0; i < RG_GUI_GPU_UPLOAD_PACKET_COUNT; i++) { SDL_free(b->gpu.upload_packets[i].items); SDL_free(b->gpu.upload_packets[i].batches); }
		free(b);
	}
	return 0;
}
