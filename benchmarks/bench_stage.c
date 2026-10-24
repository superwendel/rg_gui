/* Host-only staging benchmark: rg_gui_gpu_stage_upload of one prepared frame
 * into a mapped ring, alone and together with rg_gui_gpu_prepare, for 128 and
 * 1,024 solid rectangles (one merged item), images that alternate between
 * two textures (one item and batch each), and the same images inside one clip
 * rectangle. No window or GPU device is used.
 * The source compiles against older headers, so a before/after comparison
 * builds it twice; see benchmarks/README.md. Before timing each case, an
 * untimed check decodes the staged frame as rg_gui_indexed.vert does and
 * compares every corner with its source command. Emits one JSON object per
 * case. */
#define RG_SPRINTF_NO_ASM 1
#define RGINLINE static inline
#include "rg_gui_gpu.h"
#include <SDL3/SDL_timer.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

/* Commands store one packed color since RG_GUI_DRAW_CMD_PACKED_COLOR; older
 * headers keep a float color per payload. */
#if defined(RG_GUI_DRAW_CMD_PACKED_COLOR)
#define BENCH_CMD_COLOR(cmd, member, value) ((cmd)->color = rg_gui_pack_color(value))
#define BENCH_CMD_PACKED(cmd, member) ((cmd)->color)
#else
#define BENCH_CMD_COLOR(cmd, member, value) ((cmd)->data.member.color = (value))
#define BENCH_CMD_PACKED(cmd, member) rg_gui_renderer_base_pack_color((cmd)->data.member.color)
#endif

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
	/* The images behind one clip push, for the clipped workload. */
	RgGuiDrawCmd clipped[COMMANDS + 1u];
	RgGuiDrawList list;
	int clip;
	void* text_memory;
	u8* ring_memory;
	RgGpuUploadRing ring;
} Bench;

static void init(Bench* b, int images, int clip) {
	memset(b, 0, sizeof(*b));
	b->clip = clip;
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
#if defined(RG_GUI_GPU_FRAME_CLIP_LIMIT)
	b->gpu.clip_capacity = b->gpu.item_capacity + 1u;
	b->gpu.clips = calloc(b->gpu.clip_capacity, sizeof(*b->gpu.clips));
	if (!b->gpu.clips) fail("clip allocation");
#endif
	b->gpu.text.run_capacity = 64u; b->gpu.text.instance_capacity = 64u; b->gpu.text.cached_quad_capacity = 64u;
	b->gpu.ref_capacity = 64u + b->gpu.vertex_capacity / 3u;
#if defined(RG_GUI_GPU_FRAME_CLIP_LIMIT)
	b->gpu.frame_capacity = (u32)rg_gui_gpu_frame_bytes(64u, b->gpu.vertex_capacity, b->gpu.ref_capacity,
	                                                    b->gpu.clip_capacity);
#else
	b->gpu.frame_capacity = (u32)RG_ALIGN_UP(64u * sizeof(RgGuiRendererRun) +
	    b->gpu.vertex_capacity * sizeof(RgGuiGpuVertex), 8u) + b->gpu.ref_capacity * 8u;
#endif
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
			BENCH_CMD_COLOR(cmd, image, ((rg_vec4){.x = 0.2f, .y = 0.3f, .z = 0.7f, .w = 0.9f}));
			cmd->data.image.texture = (RgGuiTexture)(1u + i % 2);
		} else {
			cmd->type = RG_GUI_CMD_RECT;
			cmd->data.rect.rect = rect;
			BENCH_CMD_COLOR(cmd, rect, ((rg_vec4){.x = 0.2f, .y = 0.3f, .z = 0.7f, .w = 0.9f}));
		}
	}
	b->list = (RgGuiDrawList){b->commands, COMMANDS, COMMANDS};
	b->clipped[0].type = RG_GUI_CMD_CLIP_PUSH;
	b->clipped[0].data.clip.rect = (RgGuiRect){4.5f, 3.25f, 600.0f, 300.0f};
	memcpy(b->clipped + 1, b->commands, sizeof(b->commands));
	if (clip) b->list = (RgGuiDrawList){b->clipped, COMMANDS + 1u, COMMANDS + 1u};
}

static void prepare(Bench* b, u32 count) {
	b->list.count = count + (b->clip ? 1u : 0u);
	rg_gui_renderer_begin_frame(&b->text);
	if (!rg_gui_gpu_prepare(&b->gpu, &b->text, &b->list, b->list.count)) fail("prepare");
}

/* Raw vertex of each quad corner (top-left, top-right, bottom-right,
 * bottom-left), as rg_gui_indexed.vert reads it. Headers that predate
 * RG_GUI_GPU_QUAD_VERTICES wrote six vertices per quad, corner 3 being 5. */
#if defined(RG_GUI_GPU_QUAD_VERTICES)
static const u32 quad_corner[4] = {0u, 1u, 2u, 3u};
#else
static const u32 quad_corner[4] = {0u, 1u, 2u, 5u};
#endif

/* Fail unless the staged frame draws exactly the first `count` commands, in
 * order: one quad reference per command whose corners carry the command's
 * position, packed color, and UV, inside a batch with the image's texture. */
static void verify(const Bench* b, const RgGuiGpuUpload* upload, u32 count, int images) {
	const u8* frame = b->ring_memory + upload->frame.offset;
	if (upload->ref_count != count || upload->batch_count == 0u) fail("reference or batch count");
	u32 batch = 0u;
	for (u32 i = 0; i < count; i++) {
		while (batch < upload->batch_count &&
		       i >= upload->batches[batch].first_ref + upload->batches[batch].ref_count) batch++;
		if (batch == upload->batch_count || i < upload->batches[batch].first_ref) fail("batch coverage");
		u64 reference;
		memcpy(&reference, frame + upload->refs_offset + (size_t)i * 8u, sizeof(reference));
		if ((u32)(reference >> 62u) != (images ? 2u : 1u) || ((reference >> 32u) & 1u) == 0u)
			fail("reference kind or quad flag");
#if defined(RG_GUI_GPU_FRAME_CLIP_LIMIT)
		/* Every reference names the unclipped entry, or the one clip's entry. */
		if (((reference >> 33u) & 0x1fffffffu) != (b->clip ? 1u : 0u) ||
		    upload->clip_count != (b->clip ? 2u : 1u)) fail("clip index");
#endif
		const RgGuiDrawCmd* cmd = &b->commands[i];
		RgGuiRect r = images ? cmd->data.image.rect : cmd->data.rect.rect;
		RgGuiRect uv = images ? cmd->data.image.uv : (RgGuiRect){0};
		u32 color = images ? BENCH_CMD_PACKED(cmd, image) : BENCH_CMD_PACKED(cmd, rect);
		const f32 xs[4] = {r.x, r.x + r.w, r.x + r.w, r.x}, ys[4] = {r.y, r.y, r.y + r.h, r.y + r.h};
		const f32 us[4] = {uv.x, uv.x + uv.w, uv.x + uv.w, uv.x}, vs[4] = {uv.y, uv.y, uv.y + uv.h, uv.y + uv.h};
		for (u32 c = 0; c < 4u; c++) {
			RgGuiGpuVertex v;
			memcpy(&v, frame + upload->geometry_offset + ((size_t)(u32)reference + quad_corner[c]) * sizeof(v), sizeof(v));
			if (v.x != xs[c] || v.y != ys[c] || v.color != color || v.u != us[c] || v.v != vs[c]) fail("staged vertex");
		}
		if (images && upload->batches[batch].texture != cmd->data.image.texture) fail("batch texture");
	}
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
	for (int workload = 0; workload < 3; workload++) {
		const int images = workload != 0, clip = workload == 2;
		Bench* b = calloc(1, sizeof(*b)); if (!b) fail("bench"); init(b, images, clip);
		for (int full = 0; full < 2; full++) for (u32 k = 0; k < 2; k++) {
			u32 count = counts[k];
			prepare(b, count);
			RgGuiGpuUpload probe;
			b->ring.offset = 0u;
			if (!rg_gui_gpu_stage_upload(&b->gpu, &b->text, &b->ring, &probe)) fail("probe");
			verify(b, &probe, count, images);
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
			       full ? "prepare+stage" : "stage", clip ? "clipped_images" : images ? "images" : "rects", count, times[3], times[0], times[6],
			       refs, batches, bytes, b->gpu.vertex_count, (unsigned long long)packed);
		}
		free(b->gpu.vertices); free(b->gpu.items); free(b->text_memory); free(b->ring_memory);
		for (u32 i = 0; i < RG_GUI_GPU_UPLOAD_PACKET_COUNT; i++) {
			SDL_free(b->gpu.upload_packets[i].items); SDL_free(b->gpu.upload_packets[i].batches);
		}
#if defined(RG_GUI_GPU_FRAME_CLIP_LIMIT)
		free(b->gpu.clips);
#endif
		free(b);
	}
	return 0;
}
