// End-to-end SDL_GPU submission checks for rg_gui_gpu

#define RG_SPRINTF_NO_ASM 1
#define RGINLINE static inline
#include "../src/rg_gui_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_gui_gpu_text_lifetime.h"

static void test_font_init(RgTextFont* font, RgTextGlyph glyphs[2])
{
	memset(font, 0, sizeof(*font));
	memset(glyphs, 0, sizeof(RgTextGlyph) * 2u);
	font->metrics.atlas_width = 1u;
	font->metrics.atlas_height = 1u;
	font->metrics.line_height = 10;
	font->glyphs = glyphs;
	font->glyph_count = 2u;
	font->glyph_capacity = 2u;
	font->fallback_codepoint = '?';
	glyphs[0].codepoint = '?';
	glyphs[0].w = 1;
	glyphs[0].h = 1;
	glyphs[0].x_advance = 1;
	glyphs[1] = glyphs[0];
	glyphs[1].codepoint = 'A';
}

typedef struct TestImageBindState
{
	SDL_GPUGraphicsPipeline* pipeline;
	SDL_GPUGraphicsPipeline* failure_pipeline;
	u32 default_calls;
	u32 custom_calls;
	u32 failed_calls;
} TestImageBindState;

static RgGuiGpuImageBindResult test_image_bind(
	void* user, const RgGuiGpuImageBindInfo* info)
{
	TestImageBindState* state = (TestImageBindState*)user;
	if (!state || !info) return RG_GUI_GPU_IMAGE_BIND_FAILED;
	if (info->material == 10u)
	{
		state->default_calls++;
		return RG_GUI_GPU_IMAGE_BIND_DEFAULT;
	}
	if (info->material == 20u)
	{
		state->custom_calls++;
		SDL_BindGPUGraphicsPipeline(info->pass, state->pipeline);
		SDL_GPUTextureSamplerBinding image = {
		    (SDL_GPUTexture*)(uintptr_t)info->texture, info->sampler};
		SDL_BindGPUFragmentSamplers(info->pass, 0u, &image, 1u);
		/* A custom material may narrow the scissor to its own quad; the stock
		 * image after it must still draw. */
		SDL_Rect own = {24, 0, 8, 8};
		SDL_SetGPUScissor(info->pass, &own);
		return RG_GUI_GPU_IMAGE_BIND_CUSTOM;
	}
	state->failed_calls++;
	/* A partial bind before failing, including a scissor that would hide every
	 * later stock draw if rg_gui kept its cached viewport scissor. */
	SDL_BindGPUGraphicsPipeline(info->pass, state->failure_pipeline);
	SDL_Rect corner = {63, 63, 1, 1};
	SDL_SetGPUScissor(info->pass, &corner);
	return RG_GUI_GPU_IMAGE_BIND_FAILED;
}

static int test_gpu_full_cache_ring(SDL_GPUDevice* device, SDL_GPUTexture* target,
                                    SDL_GPUTransferBuffer* readback,
                                    RgGuiGpuRenderer* gpu, RgGuiRenderer* text,
                                    RgGpuUploadRing* ring)
{
	int passed = 0;
	SDL_GPUCommandBuffer* command = NULL;
	RgGuiGpuUpload upload = {0};
	/* Eight 8-pixel columns, each with its own text clip and triangle clip: all
	 * 16 items and all 17 clip-table entries, so the frame fills its bound, and
	 * the shader-side clips still let it draw in one call. */
	RgGuiDrawCmd commands[80] = {0};
	u32 command_count = 0u;
	for (u32 column = 0u; column < 8u; column++)
	{
		RgGuiDrawCmd* clip = &commands[command_count++];
		clip->type = RG_GUI_CMD_CLIP_PUSH;
		clip->data.clip.rect = rg_gui_make_rect((f32)column * 8.0f, 40.0f, 8.0f, 20.0f);
		for (u32 i = column * 2u; i < column * 2u + 2u; i++)
		{
			RgGuiDrawCmd* text_cmd = &commands[command_count++];
			text_cmd->type = RG_GUI_CMD_TEXT;
			text_cmd->data.text.text = i & 1u ? "????" : "AAAA";
			text_cmd->data.text.pos = rg_vec2((f32)i * 4.0f, 48.0f);
			text_cmd->data.text.scale = 1.0f;
			text_cmd->color = rg_gui_pack_color(rg_gui_color(1, 1, 1, 1));
		}
		commands[command_count++].type = RG_GUI_CMD_CLIP_POP;
		clip = &commands[command_count++];
		clip->type = RG_GUI_CMD_CLIP_PUSH;
		clip->data.clip.rect = rg_gui_make_rect((f32)column * 8.0f, 30.0f, 8.0f, 6.0f);
		for (u32 i = column * 4u; i < column * 4u + 4u; i++)
		{
			RgGuiDrawCmd* triangle = &commands[command_count++];
			f32 x = (f32)i * 2.0f;
			triangle->type = RG_GUI_CMD_TRIANGLE;
			triangle->data.triangle.a = rg_vec2(x, 32.0f);
			triangle->data.triangle.b = rg_vec2(x + 2.0f, 32.0f);
			triangle->data.triangle.c = rg_vec2(x, 34.0f);
		}
		commands[command_count++].type = RG_GUI_CMD_CLIP_POP;
	}
	RgGuiDrawList list = {commands, command_count, command_count};
	rg_gui_renderer_clear_cache(text);
	for (u32 phase = 0u; phase < 2u; phase++)
	{
		/* Two distinct four-glyph strings fill both 32-quad cache pages. Their
		 * repeated occurrences also reach all 16 runs and 64 frame glyphs. */
		rg_gui_gpu_invalidate_cache(gpu);
		for (u32 i = 0u; i < command_count; i++)
			if (commands[i].type == RG_GUI_CMD_TRIANGLE)
				commands[i].color = rg_gui_pack_color(rg_gui_color(1, phase ? 0.0f : 1.0f, 0, 1));
		rg_gui_renderer_begin_frame(text);
		if (!rg_gui_gpu_prepare(gpu, text, &list, list.count)) goto cleanup;
		rg_gpu_upload_ring_begin(ring, 1);
		int staged = rg_gui_gpu_stage_upload(gpu, text, ring, &upload);
		u32 used = ring->offset;
		rg_gpu_upload_ring_end(ring);
		if (!staged || !upload.text.full_cache_upload || upload.text.run_count != 16u ||
		    upload.text.glyph_count != 64u || upload.vertex_count != 96u || upload.ref_count != 96u ||
		    upload.text.cached_quads.size != 64u * sizeof(RgGuiRendererCachedQuad) ||
		    upload.item_count != 16u || upload.clip_count != 17u ||
		    upload.frame.size != gpu->frame_capacity || used != ring->size)
		{
			SDL_SetError("Full-cache packet did not exactly fill its configured upload bound");
			goto cleanup;
		}
		command = SDL_AcquireGPUCommandBuffer(device);
		if (!command) goto cleanup;
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
		if (!copy) goto cleanup;
		rg_gui_gpu_encode_upload(gpu, copy, ring, &upload);
		SDL_EndGPUCopyPass(copy);
		SDL_GPUColorTargetInfo color = {0};
		color.texture = target;
		color.clear_color = (SDL_FColor){0, 0, 0, 1};
		color.load_op = SDL_GPU_LOADOP_CLEAR;
		color.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command, &color, 1u, NULL);
		if (!pass) goto cleanup;
		RgGuiGpuDrawDesc draw = {0};
		draw.output_width = draw.output_height = 64u;
		draw.viewport = (SDL_Rect){0, 0, 64, 64};
		RgGuiGpuStats stats = rg_gui_gpu_draw(gpu, command, pass, &draw, &upload);
		SDL_EndGPURenderPass(pass);
		if (stats.geometry_vertices != 96u || stats.text_instances != 64u ||
		    stats.draw_calls != 1u || stats.indexed_refs != 96u || !rg_gui_gpu_upload_ready(gpu, &upload))
		{
			SDL_SetError("Full-cache GPU submission did not preserve all configured work");
			goto cleanup;
		}
		copy = SDL_BeginGPUCopyPass(command);
		if (!copy) goto cleanup;
		SDL_GPUTextureRegion source = {0};
		source.texture = target; source.w = source.h = 64u; source.d = 1u;
		SDL_GPUTextureTransferInfo destination = {0};
		destination.transfer_buffer = readback;
		destination.pixels_per_row = destination.rows_per_layer = 64u;
		SDL_DownloadFromGPUTexture(copy, &source, &destination);
		SDL_EndGPUCopyPass(copy);
		int submitted = SDL_SubmitGPUCommandBuffer(command);
		command = NULL;
		if (!submitted) goto cleanup;
		rg_gui_gpu_upload_commit(gpu, &upload);
		if (!SDL_WaitForGPUIdle(device)) goto cleanup;
		const u8* pixels = (const u8*)SDL_MapGPUTransferBuffer(device, readback, false);
		if (!pixels) goto cleanup;
		int valid = 1;
		for (u32 x = 0u; x < 64u; x++)
		{
			const u8* pixel = pixels + (48u * 64u + x) * 4u;
			if (pixel[0] != 64u || pixel[1] != 128u || pixel[2] != 192u || pixel[3] != 255u) valid = 0;
		}
		for (u32 x = 0u; x < 64u; x += 2u)
		{
			const u8* pixel = pixels + (32u * 64u + x) * 4u;
			if (pixel[0] != 255u || pixel[1] != (phase ? 0u : 255u) || pixel[2] != 0u || pixel[3] != 255u) valid = 0;
		}
		SDL_UnmapGPUTransferBuffer(device, readback);
		if (!valid) { SDL_SetError("Full-cache cold/invalidation GPU pixel regression"); goto cleanup; }
	}
	puts("Helper-sized upload ring: full cache, frame capacity, and invalidation passed 192 GPU pixel probes");
	passed = 1;
cleanup:
	if (command) SDL_CancelGPUCommandBuffer(command);
	rg_gui_gpu_upload_abort(gpu, &upload);
	return passed;
}

/* Render a list into the 64x64 target and read it back. */
static int test_gpu_render_sized(SDL_GPUDevice* device, SDL_GPUTexture* target, SDL_GPUTransferBuffer* readback,
                                 RgGuiGpuRenderer* gpu, RgGuiRenderer* text, RgGpuUploadRing* ring,
                                 const RgGuiDrawList* list, const RgGuiGpuDrawDesc* draw, u8* pixels,
                                 u32* clip_count, RgGuiGpuStats* stats, u32 size)
{
	int passed = 0;
	SDL_GPUCommandBuffer* command = NULL;
	RgGuiGpuUpload upload = {0};
	rg_gui_renderer_begin_frame(text);
	if (!rg_gui_gpu_prepare(gpu, text, list, list->count)) return 0;
	rg_gpu_upload_ring_begin(ring, 1);
	int staged = rg_gui_gpu_stage_upload(gpu, text, ring, &upload);
	rg_gpu_upload_ring_end(ring);
	if (!staged) return 0;
	*clip_count = upload.clip_count;
	command = SDL_AcquireGPUCommandBuffer(device);
	if (!command) goto cleanup;
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
	if (!copy) goto cleanup;
	rg_gui_gpu_encode_upload(gpu, copy, ring, &upload);
	SDL_EndGPUCopyPass(copy);
	SDL_GPUColorTargetInfo color = {0};
	color.texture = target;
	color.clear_color = (SDL_FColor){0, 0, 0, 1};
	color.load_op = SDL_GPU_LOADOP_CLEAR;
	color.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command, &color, 1u, NULL);
	if (!pass) goto cleanup;
	*stats = rg_gui_gpu_draw(gpu, command, pass, draw, &upload);
	SDL_EndGPURenderPass(pass);
	copy = SDL_BeginGPUCopyPass(command);
	if (!copy) goto cleanup;
	SDL_GPUTextureRegion source = {0};
	source.texture = target; source.w = source.h = size; source.d = 1u;
	SDL_GPUTextureTransferInfo destination = {0};
	destination.transfer_buffer = readback;
	destination.pixels_per_row = destination.rows_per_layer = size;
	SDL_DownloadFromGPUTexture(copy, &source, &destination);
	SDL_EndGPUCopyPass(copy);
	if (!rg_gui_gpu_upload_ready(gpu, &upload)) goto cleanup;
	int submitted = SDL_SubmitGPUCommandBuffer(command);
	command = NULL;
	if (!submitted) goto cleanup;
	rg_gui_gpu_upload_commit(gpu, &upload);
	if (!SDL_WaitForGPUIdle(device)) goto cleanup;
	const u8* mapped = (const u8*)SDL_MapGPUTransferBuffer(device, readback, false);
	if (!mapped) goto cleanup;
	memcpy(pixels, mapped, (size_t)size * size * 4u);
	SDL_UnmapGPUTransferBuffer(device, readback);
	passed = 1;
cleanup:
	if (command) SDL_CancelGPUCommandBuffer(command);
	rg_gui_gpu_upload_abort(gpu, &upload);
	return passed;
}

static int test_gpu_render(SDL_GPUDevice* device, SDL_GPUTexture* target, SDL_GPUTransferBuffer* readback,
                           RgGuiGpuRenderer* gpu, RgGuiRenderer* text, RgGpuUploadRing* ring,
                           const RgGuiDrawList* list, const RgGuiGpuDrawDesc* draw, u8* pixels,
                           u32* clip_count, RgGuiGpuStats* stats)
{
	return test_gpu_render_sized(device, target, readback, gpu, text, ring, list, draw, pixels, clip_count, stats, 64u);
}

/* The cases an independent review found: a fractional draw offset, which the
 * renderers add before rounding the clip; a thin rectangle under a fractional
 * clip; a clip pushed with a negative width, which draws nothing; and
 * coordinates so far left that a fragment's position plus size no longer lands
 * on its edge. Each list is rendered as pushed and as culled and no pixel may
 * change. */
static int test_gpu_hidden_culling_edges(SDL_GPUDevice* device, RgGuiGpuRenderer* gpu, RgGuiRenderer* text,
                                         RgGpuUploadRing* ring)
{
	enum { SIZE = 256, CAP = 8 };
	int passed = 0;
	SDL_GPUTextureCreateInfo info = {0};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	info.width = info.height = SIZE;
	info.layer_count_or_depth = 1u;
	info.num_levels = 1u;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	SDL_GPUTexture* target = SDL_CreateGPUTexture(device, &info);
	SDL_GPUTransferBufferCreateInfo transfer = {0};
	transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	transfer.size = SIZE * SIZE * 4u;
	SDL_GPUTransferBuffer* readback = SDL_CreateGPUTransferBuffer(device, &transfer);
	static u8 plain_pixels[SIZE * SIZE * 4u], culled_pixels[SIZE * SIZE * 4u];
	if (!target || !readback) goto cleanup;
	typedef struct Edge { u32 type; RgGuiRect rect; u32 color; } Edge;
	static const Edge offset_case[] = {
	    {RG_GUI_CMD_CLIP_PUSH, {0.2f, 0, 200, 200}, 0u},
	    {RG_GUI_CMD_RECT, {-1, 0, 201, 200}, 0xff0000ffu},
	    {RG_GUI_CMD_CLIP_POP, {0, 0, 0, 0}, 0u},
	    {RG_GUI_CMD_RECT, {0, 0, 200, 200}, 0xffff0000u}};
	static const Edge thin_case[] = {
	    {RG_GUI_CMD_CLIP_PUSH, {0.1f, 0, 100, 100}, 0u},
	    {RG_GUI_CMD_RECT, {-0.3f, 0, 0.25f, 100}, 0xff0000ffu},
	    {RG_GUI_CMD_CLIP_POP, {0, 0, 0, 0}, 0u},
	    {RG_GUI_CMD_RECT, {240, 240, 8, 8}, 0xff00ff00u}};
	static const Edge negative_clip_case[] = {
	    {RG_GUI_CMD_RECT, {0, 0, 128, 128}, 0xff0000ffu},
	    {RG_GUI_CMD_CLIP_PUSH, {1, 0, -0.5f, 128}, 0u},
	    {RG_GUI_CMD_RECT, {0, 0, 128, 128}, 0xffff0000u},
	    {RG_GUI_CMD_CLIP_POP, {0, 0, 0, 0}, 0u}};
	static const Edge far_case[] = {
	    {RG_GUI_CMD_RECT, {-1600000.0f, 0, 1600200.5f, 200}, 0xff0000ffu},
	    {RG_GUI_CMD_RECT, {-1600000.0f, 0, 551600.0625f, 200}, 0xffff0000u}};
	const struct Case { const Edge* edges; u32 count; f32 offset_x; f32 offset_y; const char* name; } cases[] = {
	    {offset_case, 4u, 0.75f, 0.0f, "fractional offset"},
	    {thin_case, 4u, 0.6f, 0.0f, "thin rectangle under a fractional clip"},
	    {negative_clip_case, 4u, 0.0f, 0.0f, "negative clip width"},
	    {far_case, 2u, 0.0f, 0.0f, "far coordinates"}};
	for (u32 c = 0u; c < sizeof(cases) / sizeof(cases[0]); c++)
	{
		RgGuiDrawCmd plain[CAP], culled[CAP], scratch[CAP];
		RgGuiRect clips[CAP];
		memset(plain, 0, sizeof(plain));
		for (u32 i = 0u; i < cases[c].count; i++)
		{
			plain[i].type = cases[c].edges[i].type;
			plain[i].color = cases[c].edges[i].color;
			if (plain[i].type == RG_GUI_CMD_RECT) plain[i].data.rect.rect = cases[c].edges[i].rect;
			else if (plain[i].type == RG_GUI_CMD_CLIP_PUSH) plain[i].data.clip.rect = cases[c].edges[i].rect;
		}
		memcpy(culled, plain, sizeof(plain));
		RgGuiDrawList plain_list = {plain, cases[c].count, CAP};
		RgGuiDrawList culled_list = {culled, cases[c].count, CAP};
		u32 overlay = cases[c].count, dropped = 0u, split = 0u;
		rg_gui_draw_list_cull_hidden_ex(&culled_list, &overlay, clips, scratch, CAP, &dropped, &split);
		if (culled_list.count > cases[c].count || overlay != culled_list.count)
		{
			SDL_SetError("Hidden-draw culling (%s) produced %u commands from %u", cases[c].name, culled_list.count, cases[c].count);
			goto cleanup;
		}
		RgGuiGpuDrawDesc draw = {0};
		draw.output_width = draw.output_height = SIZE;
		draw.viewport = (SDL_Rect){0, 0, SIZE, SIZE};
		draw.offset_x = cases[c].offset_x;
		draw.offset_y = cases[c].offset_y;
		RgGuiGpuStats stats = {0};
		u32 clip_count = 0u;
		if (!test_gpu_render_sized(device, target, readback, gpu, text, ring, &plain_list, &draw, plain_pixels, &clip_count, &stats, SIZE) ||
		    !test_gpu_render_sized(device, target, readback, gpu, text, ring, &culled_list, &draw, culled_pixels, &clip_count, &stats, SIZE))
		{
			SDL_SetError("Hidden-draw culling (%s) render failed", cases[c].name);
			goto cleanup;
		}
		u32 differing = 0u;
		for (u32 i = 0u; i < SIZE * SIZE; i++) differing += memcmp(plain_pixels + i * 4u, culled_pixels + i * 4u, 4u) != 0;
		if (differing)
		{
			SDL_SetError("Hidden-draw culling (%s) changed %u pixels (dropped %u, split %u)", cases[c].name, differing, dropped, split);
			goto cleanup;
		}
	}
	printf("Hidden-draw culling: offset, thin, negative-clip and far-coordinate lists render identically culled\n");
	passed = 1;
cleanup:
	if (readback) SDL_ReleaseGPUTransferBuffer(device, readback);
	if (target) SDL_ReleaseGPUTexture(device, target);
	return passed;
}

/* Hidden-draw culling claims to leave every pixel unchanged. Render a layered
 * list, a backdrop under a panel under widgets with clips and translucency, as
 * pushed and as culled, into a 256-pixel target and compare the readbacks byte
 * for byte. The culled list must have dropped and split commands. */
static int test_gpu_hidden_culling(SDL_GPUDevice* device, RgGuiGpuRenderer* gpu, RgGuiRenderer* text,
                                   RgGpuUploadRing* ring)
{
	enum { SIZE = 256 };
	int passed = 0;
	SDL_GPUTextureCreateInfo info = {0};
	info.type = SDL_GPU_TEXTURETYPE_2D;
	info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	info.width = info.height = SIZE;
	info.layer_count_or_depth = 1u;
	info.num_levels = 1u;
	info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	SDL_GPUTexture* target = SDL_CreateGPUTexture(device, &info);
	SDL_GPUTransferBufferCreateInfo transfer = {0};
	transfer.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	transfer.size = SIZE * SIZE * 4u;
	SDL_GPUTransferBuffer* readback = SDL_CreateGPUTransferBuffer(device, &transfer);
	static u8 plain_pixels[SIZE * SIZE * 4u], culled_pixels[SIZE * SIZE * 4u];
	if (!target || !readback) goto cleanup;
	typedef struct Layer { u32 type; RgGuiRect rect; u32 color; } Layer;
	/* Clips stay balanced; the fractional clip around the window exercises the
	 * widening of an occludee's clip, and the twelve rectangles under the
	 * window return the slots the two splits spend. */
	const Layer layers[] = {
	    {RG_GUI_CMD_RECT, {0, 0, 256, 256}, 0xff303030u},             /* backdrop: split into margins */
	    {RG_GUI_CMD_RECT, {12, 12, 232, 232}, 0xff505050u},           /* panel: mostly covered, split */
	    {RG_GUI_CMD_RECT, {20, 20, 100, 100}, 0x80ff4000u},           /* translucent, partly visible: kept */
	    {RG_GUI_CMD_RECT, {40, 40, 60, 60}, 0xff0080ffu},             /* under the window below: dropped */
	    {RG_GUI_CMD_RECT, {120, 60, 40, 40}, 0xff40c0ffu},            /* under the window: dropped */
	    {RG_GUI_CMD_RECT, {60, 140, 48, 30}, 0x60ffffffu},            /* translucent under the window: dropped */
	    {RG_GUI_CMD_RECT, {180, 120, 40, 40}, 0xffc04040u},           /* under the window: dropped */
	    {RG_GUI_CMD_RECT, {100, 190, 50, 30}, 0xff4040c0u},           /* under the window: dropped */
	    {RG_GUI_CMD_RECT, {40, 110, 40, 40}, 0xff80a040u},            /* under the window: dropped */
	    {RG_GUI_CMD_RECT, {90, 110, 40, 40}, 0x80ff8000u},            /* translucent under the window: dropped */
	    {RG_GUI_CMD_RECT, {140, 60, 40, 30}, 0xff2060a0u},            /* under the window: dropped */
	    {RG_GUI_CMD_RECT, {190, 60, 36, 36}, 0xffa02060u},            /* under the window: dropped */
	    {RG_GUI_CMD_RECT, {150, 150, 40, 40}, 0xff60a020u},           /* under the window: dropped */
	    {RG_GUI_CMD_RECT, {60, 200, 60, 24}, 0x40ffffffu},            /* translucent under the window: dropped */
	    {RG_GUI_CMD_CLIP_PUSH, {30.5f, 30.25f, 200.0f, 200.5f}, 0u},
	    {RG_GUI_CMD_RECT, {0, 0, 256, 256}, 0xff707070u},             /* window: clipped occluder */
	    {RG_GUI_CMD_RECT, {35, 35, 40, 40}, 0x40ffffffu},             /* translucent under the toolbar: dropped */
	    {RG_GUI_CMD_CLIP_POP, {0, 0, 0, 0}, 0u},
	    {RG_GUI_CMD_RECT, {32, 32, 180, 50}, 0xff906020u},            /* toolbar: occluder */
	    {RG_GUI_CMD_RECT, {150, 150, 90, 90}, 0xc0208040u},           /* translucent over the window: kept */
	    {RG_GUI_CMD_RECT, {34, 34, 176, 46}, 0xff20a0c0u},            /* inset over the toolbar: occluder */
	    {RG_GUI_CMD_RECT, {36.5f, 36.5f, 20, 20}, 0xffffffffu},       /* small, kept */
	    {RG_GUI_CMD_RECT, {200.25f, 10.75f, 50, 30}, 0x90ff00ffu},    /* spans panel edge, translucent */
	    {RG_GUI_CMD_RECT, {-20, 100, 60, 300}, 0xff804040u},          /* off the edge, occluder */
	};
	enum { LAYER_COUNT = sizeof(layers) / sizeof(layers[0]) };
	RgGuiDrawCmd plain[LAYER_COUNT], culled[LAYER_COUNT + 8u], scratch[LAYER_COUNT + 8u];
	RgGuiRect clips[LAYER_COUNT + 8u];
	memset(plain, 0, sizeof(plain));
	for (u32 i = 0u; i < LAYER_COUNT; i++)
	{
		plain[i].type = layers[i].type;
		plain[i].color = layers[i].color;
		if (layers[i].type == RG_GUI_CMD_RECT) plain[i].data.rect.rect = layers[i].rect;
		else if (layers[i].type == RG_GUI_CMD_CLIP_PUSH) plain[i].data.clip.rect = layers[i].rect;
	}
	memcpy(culled, plain, sizeof(plain));
	RgGuiDrawList plain_list = {plain, LAYER_COUNT, LAYER_COUNT};
	RgGuiDrawList culled_list = {culled, LAYER_COUNT, LAYER_COUNT + 8u};
	u32 overlay = LAYER_COUNT, dropped = 0u, split = 0u;
	rg_gui_draw_list_cull_hidden_ex(&culled_list, &overlay, clips, scratch, LAYER_COUNT + 8u, &dropped, &split);
	if (dropped < 12u || split < 2u || overlay != culled_list.count || culled_list.count > LAYER_COUNT)
	{
		SDL_SetError("Hidden-draw culling dropped %u and split %u commands into %u (expected at least 12 and 2, no longer than %u)",
		             dropped, split, culled_list.count, (u32)LAYER_COUNT);
		goto cleanup;
	}
	/* The culled list must fit the geometry the pushed list needed. */
	{
		rg_gui_renderer_begin_frame(text);
		if (!rg_gui_gpu_prepare(gpu, text, &plain_list, plain_list.count))
		{
			SDL_SetError("Hidden-draw culling: the pushed list did not prepare");
			goto cleanup;
		}
		const u32 vertex_capacity = gpu->vertex_capacity, item_capacity = gpu->item_capacity;
		gpu->vertex_capacity = gpu->vertex_count;
		gpu->item_capacity = gpu->item_count;
		rg_gui_renderer_begin_frame(text);
		const int fits = rg_gui_gpu_prepare(gpu, text, &culled_list, overlay);
		gpu->vertex_capacity = vertex_capacity;
		gpu->item_capacity = item_capacity;
		if (!fits)
		{
			SDL_SetError("Hidden-draw culling: the culled list needs more geometry than the pushed list");
			goto cleanup;
		}
	}
	RgGuiGpuDrawDesc draw = {0};
	draw.output_width = draw.output_height = SIZE;
	draw.viewport = (SDL_Rect){0, 0, SIZE, SIZE};
	RgGuiGpuStats stats = {0};
	u32 clip_count = 0u;
	if (!test_gpu_render_sized(device, target, readback, gpu, text, ring, &plain_list, &draw, plain_pixels, &clip_count, &stats, SIZE) ||
	    !test_gpu_render_sized(device, target, readback, gpu, text, ring, &culled_list, &draw, culled_pixels, &clip_count, &stats, SIZE))
	{
		SDL_SetError("Hidden-draw culling render failed");
		goto cleanup;
	}
	u32 differing = 0u, painted = 0u;
	for (u32 i = 0u; i < SIZE * SIZE; i++)
	{
		differing += memcmp(plain_pixels + i * 4u, culled_pixels + i * 4u, 4u) != 0;
		painted += plain_pixels[i * 4u] | plain_pixels[i * 4u + 1u] | plain_pixels[i * 4u + 2u] ? 1u : 0u;
	}
	if (differing || painted < SIZE * SIZE / 2u)
	{
		SDL_SetError("Hidden-draw culling changed %u of %u pixels (%u painted)", differing, SIZE * SIZE, painted);
		goto cleanup;
	}
	printf("Hidden-draw culling: %u GPU pixels identical with %u of %u commands dropped and %u split\n",
	       SIZE * SIZE, dropped, LAYER_COUNT, split);
	passed = 1;
cleanup:
	if (readback) SDL_ReleaseGPUTransferBuffer(device, readback);
	if (target) SDL_ReleaseGPUTexture(device, target);
	return passed;
}

/* The shader's clip distances must reproduce the scissor the renderer used to
 * set per batch, rg_gui_gpu_scissor, pixel for pixel: fractional, nested,
 * shared, empty, and off-screen clips over rectangles, an image, a triangle,
 * and text, at fractional draw offsets and in a reduced viewport. Each
 * primitive is drawn once unclipped to learn its coverage; the clipped frame
 * must equal that coverage inside the primitive's scissor and be empty outside. */
static int test_gpu_clip_distances(SDL_GPUDevice* device, SDL_GPUTexture* target,
                                   SDL_GPUTransferBuffer* readback, RgGuiGpuRenderer* gpu,
                                   RgGuiRenderer* text, RgGpuUploadRing* ring)
{
	typedef struct ClipCase
	{
		u32 type;
		RgGuiRect rect; /* rectangle, image, or text region (text: pos is rect.x, rect.y + 4) */
		rg_vec4 color;
		u32 clips;      /* 0, 1, or 2 nested clips */
		RgGuiRect clip[2];
	} ClipCase;
	const rg_vec4 red = rg_gui_color(1, 0, 0, 1), green = rg_gui_color(0, 1, 0, 1);
	const rg_vec4 blue = rg_gui_color(0, 0, 1, 1), white = rg_gui_color(1, 1, 1, 1);
	const rg_vec4 yellow = rg_gui_color(1, 1, 0, 1), cyan = rg_gui_color(0, 1, 1, 1);
	const rg_vec4 magenta = rg_gui_color(1, 0, 1, 1);
	const RgGuiRect shared = {22.5f, 40.5f, 20.0f, 12.25f};
	const ClipCase cases[] = {
	    {RG_GUI_CMD_RECT, {2, 2, 14, 12}, red, 1u, {{3.5f, 3.25f, 9.75f, 7.5f}}},
	    {RG_GUI_CMD_IMAGE, {20, 2, 14, 12}, green, 1u, {{21.25f, 2.75f, 10.0f, 8.5f}}},
	    {RG_GUI_CMD_TRIANGLE, {38, 1, 24, 16}, blue, 1u, {{39.5f, 2.5f, 12.25f, 6.75f}}},
	    {RG_GUI_CMD_TEXT, {0, 19, 28, 16}, white, 1u, {{4.5f, 23.5f, 15.25f, 2.75f}}},
	    {RG_GUI_CMD_RECT, {32, 22, 28, 16}, yellow, 2u, {{30, 22, 30, 20}, {35.5f, 25.25f, 40, 7.5f}}},
	    {RG_GUI_CMD_RECT, {21, 42, 8, 12}, cyan, 1u, {shared}},
	    {RG_GUI_CMD_RECT, {2, 42, 12, 8}, magenta, 0u, {{0, 0, 0, 0}}},
	    {RG_GUI_CMD_RECT, {33, 42, 12, 14}, white, 1u, {shared}},
	    {RG_GUI_CMD_RECT, {48, 42, 6, 8}, white, 1u, {{50, 44, 0, 10}}},
	    {RG_GUI_CMD_RECT, {56, 50, 14, 20}, yellow, 1u, {{56.5f, 50.5f, 20, 20}}},
	    {RG_GUI_CMD_RECT, {0, 54, 10, 12}, cyan, 1u, {{-3.25f, 54.5f, 10.5f, 20}}}};
	enum { CASES = sizeof(cases) / sizeof(cases[0]) };
	RgGuiDrawCmd clipped[CASES * 5u], plain[CASES];
	u32 clipped_count = 0u;
	RgGuiRect effective[CASES];
	for (u32 i = 0u; i < CASES; i++)
	{
		const ClipCase* c = &cases[i];
		RgGuiDrawCmd cmd = {0};
		cmd.type = c->type;
		if (c->type == RG_GUI_CMD_RECT) { cmd.data.rect.rect = c->rect; cmd.color = rg_gui_pack_color(c->color); }
		else if (c->type == RG_GUI_CMD_IMAGE)
		{
			cmd.data.image.rect = c->rect;
			cmd.data.image.uv = rg_gui_make_rect(0, 0, 1, 1);
			cmd.color = rg_gui_pack_color(c->color);
			cmd.data.image.texture = (RgGuiTexture)(uintptr_t)gpu->text.atlas_texture;
		}
		else if (c->type == RG_GUI_CMD_TRIANGLE)
		{
			cmd.data.triangle.a = rg_vec2(c->rect.x, c->rect.y);
			cmd.data.triangle.b = rg_vec2(c->rect.x + c->rect.w, c->rect.y);
			cmd.data.triangle.c = rg_vec2(c->rect.x, c->rect.y + c->rect.h);
			cmd.color = rg_gui_pack_color(c->color);
		}
		else
		{
			cmd.data.text.text = "AAAAAAAA";
			cmd.data.text.pos = rg_vec2(c->rect.x + 2.0f, c->rect.y + 4.0f);
			cmd.data.text.scale = 3.0f;
			cmd.color = rg_gui_pack_color(c->color);
		}
		plain[i] = cmd;
		for (u32 k = 0u; k < c->clips; k++)
		{
			clipped[clipped_count].type = RG_GUI_CMD_CLIP_PUSH;
			clipped[clipped_count++].data.clip.rect = c->clip[k];
		}
		effective[i] = c->clip[0];
		if (c->clips == 2u) effective[i] = rg_gui_renderer_base_clip_intersect(c->clip[0], c->clip[1]);
		clipped[clipped_count++] = cmd;
		for (u32 k = 0u; k < c->clips; k++) clipped[clipped_count++].type = RG_GUI_CMD_CLIP_POP;
	}
	const RgGuiDrawList plain_list = {plain, CASES, CASES};
	const RgGuiDrawList clipped_list = {clipped, clipped_count, clipped_count};
	const struct { f32 x, y; SDL_Rect viewport; } draws[] = {
	    {0.0f, 0.0f, {0, 0, 64, 64}}, {0.5f, 0.25f, {0, 0, 64, 64}},
	    {-0.75f, 1.5f, {0, 0, 64, 64}}, {0.3f, -0.6f, {0, 0, 64, 64}},
	    {0.5f, 0.5f, {8, 6, 40, 41}}};
	static u8 coverage[64u * 64u * 4u], result[64u * 64u * 4u];
	u32 checked = 0u;
	for (u32 d = 0u; d < sizeof(draws) / sizeof(draws[0]); d++)
	{
		RgGuiGpuDrawDesc draw = {0};
		draw.output_width = draw.output_height = 64u;
		draw.offset_x = draws[d].x;
		draw.offset_y = draws[d].y;
		draw.viewport = (SDL_Rect){0, 0, 64, 64};
		RgGuiGpuStats stats = {0};
		u32 clip_count = 0u;
		if (!test_gpu_render(device, target, readback, gpu, text, ring, &plain_list, &draw,
		                     coverage, &clip_count, &stats) || clip_count != 1u)
		{
			SDL_SetError("Clip-distance coverage pass failed");
			return 0;
		}
		draw.viewport = draws[d].viewport;
		if (!test_gpu_render(device, target, readback, gpu, text, ring, &clipped_list, &draw,
		                     result, &clip_count, &stats))
		{
			SDL_SetError("Clip-distance clipped pass failed");
			return 0;
		}
		/* Nine distinct clips plus the unclipped entry; the shared clip is one entry
		 * even with an unclipped primitive between its users. One draw call. */
		if (clip_count != 10u || stats.draw_calls != 1u)
		{
			SDL_SetError("Clipped pass used %u clip entries and %u draw calls, expected 10 and 1",
			             clip_count, stats.draw_calls);
			return 0;
		}
		u32 visible[CASES] = {0};
		for (u32 y = 0u; y < 64u; y++)
		for (u32 x = 0u; x < 64u; x++)
		{
			const u8* covered = coverage + (y * 64u + x) * 4u;
			const u8* got = result + (y * 64u + x) * 4u;
			const f32 cx = (f32)x + 0.5f, cy = (f32)y + 0.5f;
			int owner = -1;
			for (u32 i = 0u; i < CASES; i++)
			{
				const RgGuiRect r = cases[i].rect;
				if (cx >= r.x - 1.0f + draw.offset_x && cx < r.x + r.w + 1.0f + draw.offset_x &&
				    cy >= r.y - 1.0f + draw.offset_y && cy < r.y + r.h + 1.0f + draw.offset_y)
					owner = (int)i;
			}
			const int background = covered[0] == 0u && covered[1] == 0u && covered[2] == 0u;
			int inside = 0;
			if (owner >= 0)
			{
				SDL_Rect scissor = rg_gui_gpu_scissor(effective[owner], cases[owner].clips != 0u, &draw);
				inside = (int)x >= scissor.x && (int)x < scissor.x + scissor.w &&
				         (int)y >= scissor.y && (int)y < scissor.y + scissor.h;
			}
			else if (!background)
			{
				SDL_SetError("Primitive coverage escaped its region at %u,%u", x, y);
				return 0;
			}
			const u8 expected[4] = {inside ? covered[0] : 0u, inside ? covered[1] : 0u,
			                        inside ? covered[2] : 0u, 255u};
			if (memcmp(got, expected, 4u))
			{
				SDL_SetError("Clip-distance pixel %u,%u (draw %u): got %u,%u,%u,%u, expected %u,%u,%u,%u",
				             x, y, d, got[0], got[1], got[2], got[3],
				             expected[0], expected[1], expected[2], expected[3]);
				return 0;
			}
			if (owner >= 0 && inside && !background) visible[owner]++;
			checked++;
		}
		/* Every primitive must show pixels, or the comparison above proves
		 * nothing about it. The zero-width clip (case 8) shows none at integral
		 * offsets; at a fractional one the scissor's floor and ceil give it a
		 * one-pixel column, which the comparison above checked. The reduced
		 * viewport hides some cases entirely, so it is exempt. */
		for (u32 i = 0u; draws[d].viewport.w == 64 && i < CASES; i++)
			if (i == 8u ? (d == 0u && visible[i] != 0u) : visible[i] == 0u)
			{
				SDL_SetError("Clip case %u showed %u pixels in draw %u", i, visible[i], d);
				return 0;
			}
	}
	printf("Shader clip distances: %u pixels matched the scissor rule across 4 draw offsets and a reduced viewport\n",
	       checked);
	return 1;
}

int main(void)
{
	int result = 1;
	int window_claimed = 0;
	SDL_Window* window = NULL;
	SDL_GPUDevice* device = NULL;
	SDL_GPUTexture* atlas = NULL;
	SDL_GPUTexture* target_texture = NULL;
	SDL_GPUTransferBuffer* atlas_upload = NULL;
	SDL_GPUTransferBuffer* readback = NULL;
	RgGuiGpuRenderer renderer = {0};
	RgGpuUploadRing ring = {0};
	TestImageBindState image_bind_state = {0};
	void* gui_memory = NULL;
	void* text_memory = NULL;

	if (!SDL_Init(SDL_INIT_VIDEO)) goto cleanup;
	window = SDL_CreateWindow("rg_gui GPU validation", 64, 64, SDL_WINDOW_HIDDEN);
	if (!window) goto cleanup;

	RgGpuDeviceDesc device_desc = {0};
	device_desc.shader_formats = RG_GPU_DEFAULT_SHADER_FORMATS;
	device_desc.enable_debug = 1;
	device = rg_gpu_device_create(&device_desc);
	if (!device || !rg_gpu_claim_window(device, window)) goto cleanup;
	window_claimed = 1;

	SDL_GPUTextureCreateInfo atlas_info = {0};
	atlas_info.type = SDL_GPU_TEXTURETYPE_2D;
	atlas_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	atlas_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	atlas_info.width = 1u;
	atlas_info.height = 1u;
	atlas_info.layer_count_or_depth = 1u;
	atlas_info.num_levels = 1u;
	atlas_info.sample_count = SDL_GPU_SAMPLECOUNT_1;
	atlas = SDL_CreateGPUTexture(device, &atlas_info);
	if (!atlas) goto cleanup;
	atlas_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	atlas_info.width = 64u;
	atlas_info.height = 64u;
	target_texture = SDL_CreateGPUTexture(device, &atlas_info);
	if (!target_texture) goto cleanup;
	SDL_GPUTransferBufferCreateInfo transfer_info = {0};
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = 256u;
	atlas_upload = SDL_CreateGPUTransferBuffer(device, &transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	transfer_info.size = 64u * 64u * 4u;
	readback = SDL_CreateGPUTransferBuffer(device, &transfer_info);
	if (!atlas_upload || !readback) goto cleanup;
	u8* atlas_pixel = (u8*)SDL_MapGPUTransferBuffer(device, atlas_upload, false);
	if (!atlas_pixel) goto cleanup;
	memset(atlas_pixel, 0, 256u);
	atlas_pixel[0] = 64u; atlas_pixel[1] = 128u;
	atlas_pixel[2] = 192u; atlas_pixel[3] = 255u;
	SDL_UnmapGPUTransferBuffer(device, atlas_upload);

	RgGuiGpuDesc desc = {0};
	desc.device = device;
	desc.target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	desc.shader_root = "shaders";
	desc.atlas_texture = atlas;
	desc.atlas_width = 1u;
	desc.atlas_height = 1u;
	desc.max_cached_quads = 64u;
	desc.max_runs = 16u;
	desc.max_text_instances = 64u;
	desc.max_geometry_vertices = 96u;
	desc.max_items = 16u;
	desc.frame_buffer_count = 2u;
	desc.min_filter = SDL_GPU_FILTER_NEAREST;
	desc.mag_filter = SDL_GPU_FILTER_NEAREST;
	desc.image_bind = test_image_bind;
	desc.image_bind_user = &image_bind_state;
	if (!rg_gui_gpu_create(&renderer, &desc)) goto cleanup;
	image_bind_state.pipeline = renderer.image_pipeline;
	image_bind_state.failure_pipeline = renderer.solid_pipeline;
	u32 ring_size = rg_gui_gpu_upload_ring_size_required(&renderer);
	if (!ring_size || !rg_gpu_upload_ring_init(&ring, device, ring_size)) goto cleanup;

	RgTextFont font;
	RgTextGlyph glyphs[2];
	test_font_init(&font, glyphs);
	gui_memory = malloc(MB(16));
	if (!gui_memory) goto cleanup;
	RgArena gui_arena = {(char*)gui_memory, MB(16), 0u, MB(16)};
	RgGuiContext gui;
	RgGuiInitDesc gui_desc = {0};
	gui_desc.font = &font;
	gui_desc.max_draw_cmds = 32u;
	gui_desc.text_buffer_size = KB(1);
	if (!rg_gui_init(&gui, &gui_arena, &gui_desc)) goto cleanup;

	RgGuiRendererLimits limits = rg_gui_renderer_limits_default();
	limits.max_cached_runs = 16u;
	limits.hash_slot_count = 32u;
	limits.text_capacity = KB(1);
	limits.max_cached_quads = 64u;
	limits.max_frame_instances = 64u;
	limits.max_batches = 16u;
	size_t text_memory_size = rg_gui_renderer_memory_required(&limits, 32u);
	text_memory = malloc(text_memory_size);
	if (!text_memory) goto cleanup;
	RgArena text_arena = {(char*)text_memory, text_memory_size, 0u, text_memory_size};
	RgGuiRenderer text_renderer;
	RgGuiRendererInitDesc text_desc = {0};
	text_desc.font = &font;
	text_desc.limits = limits;
	text_desc.page_quads = 32u;
	if (!rg_gui_renderer_init(&text_renderer, &text_arena, &text_desc)) goto cleanup;

	rg_vec4 white = rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f);
	rg_gui_push_rect(&gui, rg_gui_make_rect(0.0f, 0.0f, 8.0f, 8.0f), white);
	rg_gui_push_image(&gui, rg_gui_make_rect(8.0f, 0.0f, 8.0f, 8.0f),
	                  rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                  (RgGuiTexture)(uintptr_t)atlas);
	rg_gui_push_image_material(&gui, rg_gui_make_rect(16.0f, 0.0f, 8.0f, 8.0f),
	                           rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                           (RgGuiTexture)(uintptr_t)atlas, 10u);
	rg_gui_push_image_material(&gui, rg_gui_make_rect(24.0f, 0.0f, 8.0f, 8.0f),
	                           rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                           (RgGuiTexture)(uintptr_t)atlas, 20u);
	rg_gui_push_image(&gui, rg_gui_make_rect(32.0f, 0.0f, 8.0f, 8.0f),
	                  rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                  (RgGuiTexture)(uintptr_t)atlas);
	rg_gui_push_image_material(&gui, rg_gui_make_rect(40.0f, 0.0f, 8.0f, 8.0f),
	                           rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                           (RgGuiTexture)(uintptr_t)atlas, 30u);
	rg_gui_push_image(&gui, rg_gui_make_rect(48.0f, 0.0f, 8.0f, 8.0f),
	                  rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                  (RgGuiTexture)(uintptr_t)atlas);
	rg_gui_push_rect(&gui, rg_gui_make_rect(56.0f, 0.0f, 8.0f, 8.0f), white);
	rg_gui_push_text_static(&gui, "A", rg_vec2(56.0f, 12.0f), white);
	// The body clip deliberately remains active at the overlay boundary.
	// Overlay geometry must reset it, while a stock draw after a custom or failed
	// material must restore the indexed pipeline and texture state.
	rg_gui_push_clip(&gui, rg_gui_make_rect(0, 20, 8, 8));
	rg_gui_push_rect(&gui, rg_gui_make_rect(0, 16, 16, 16), rg_gui_color(1, 0, 0, 1));
	u32 overlay_start = gui.draw_list.count;
	rg_gui_push_rect(&gui, rg_gui_make_rect(16, 20, 8, 8), rg_gui_color(0, 1, 0, 1));
	rg_gui_push_rect(&gui, rg_gui_make_rect(32, 20, 8, 8), rg_gui_color(0, 0, 1, 1));
	rg_gui_push_rect(&gui, rg_gui_make_rect(32, 20, 8, 8), rg_gui_color(1, 0, 0, 0.5f));
	rg_gui_push_triangle(&gui, rg_vec2(48, 20), rg_vec2(56, 20), rg_vec2(48, 28),
	                     rg_gui_color(1, 1, 0, 1));
	const RgGuiDrawList* list = rg_gui_draw_list(&gui);
	rg_gui_renderer_begin_frame(&text_renderer);
	if (!rg_gui_gpu_prepare(&renderer, &text_renderer, list, overlay_start))
		goto cleanup;

	RgGuiGpuUpload upload = {0};
	rg_gpu_upload_ring_begin(&ring, 1);
	int staged = rg_gui_gpu_stage_upload(&renderer, &text_renderer, &ring, &upload);
	rg_gpu_upload_ring_end(&ring);
	if (!staged) goto cleanup;

	/* Encoding a command that is subsequently cancelled must not certify the
	 * persistent GPU cache. Retry after begin_frame has reset frame dirtiness. */
	SDL_GPUCommandBuffer* cancelled = SDL_AcquireGPUCommandBuffer(device);
	if (!cancelled) goto cleanup;
	SDL_GPUCopyPass* cancelled_copy = SDL_BeginGPUCopyPass(cancelled);
	if (!cancelled_copy) { SDL_CancelGPUCommandBuffer(cancelled); goto cleanup; }
	rg_gui_gpu_encode_upload(&renderer, cancelled_copy, &ring, &upload);
	SDL_EndGPUCopyPass(cancelled_copy);
	if (renderer.text.cache_initialized) {
		SDL_CancelGPUCommandBuffer(cancelled);
		SDL_SetError("Encoding incorrectly initialized the persistent cache");
		goto cleanup;
	}
	SDL_CancelGPUCommandBuffer(cancelled);
	rg_gui_gpu_upload_abort(&renderer, &upload);
	rg_gui_renderer_begin_frame(&text_renderer);
	if (!rg_gui_gpu_prepare(&renderer, &text_renderer, list, overlay_start)) goto cleanup;
	rg_gpu_upload_ring_begin(&ring, 1);
	staged = rg_gui_gpu_stage_upload(&renderer, &text_renderer, &ring, &upload);
	rg_gpu_upload_ring_end(&ring);
	if (!staged || !upload.text.full_cache_upload || upload.text.cache_range_count != 1u) {
		SDL_SetError("Cancelled initial upload was not restaged in full");
		goto cleanup;
	}

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(device);
	if (!command_buffer) goto cleanup;
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
	if (!copy)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		goto cleanup;
	}
	SDL_GPUTextureTransferInfo atlas_source = {0};
	atlas_source.transfer_buffer = atlas_upload;
	atlas_source.pixels_per_row = 64u; atlas_source.rows_per_layer = 1u;
	SDL_GPUTextureRegion atlas_destination = {0};
	atlas_destination.texture = atlas;
	atlas_destination.w = atlas_destination.h = atlas_destination.d = 1u;
	SDL_UploadToGPUTexture(copy, &atlas_source, &atlas_destination, false);
	rg_gui_gpu_encode_upload(&renderer, copy, &ring, &upload);
	SDL_EndGPUCopyPass(copy);
	if (!rg_gui_gpu_dispatch_upload(&renderer, command_buffer, &upload))
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		goto cleanup;
	}

	SDL_GPUColorTargetInfo target = {0};
	target.texture = target_texture;
	target.clear_color = (SDL_FColor){0.0f, 0.0f, 0.0f, 1.0f};
	target.load_op = SDL_GPU_LOADOP_CLEAR;
	target.store_op = SDL_GPU_STOREOP_STORE;
	SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command_buffer, &target, 1u, NULL);
	if (!pass)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		goto cleanup;
	}
	RgGuiGpuDrawDesc draw_desc = {0};
	draw_desc.output_width = 64u;
	draw_desc.output_height = 64u;
	draw_desc.viewport = (SDL_Rect){0, 0, 64, 64};
	RgGuiGpuStats stats = rg_gui_gpu_draw(&renderer, command_buffer, pass,
	                                      &draw_desc, &upload);
	SDL_EndGPURenderPass(pass);
	int stats_valid = stats.geometry_vertices == 47u && stats.text_instances == 1u &&
	                  stats.items == 11u && stats.draw_calls > 0u && stats.dispatches == 0u &&
	                  stats.image_bind_calls == 3u &&
	                  stats.custom_image_draw_calls == 1u &&
	                  stats.image_bind_failures == 1u &&
	                  image_bind_state.default_calls == 1u &&
	                  image_bind_state.custom_calls == 1u &&
	                  image_bind_state.failed_calls == 1u;
	if (!rg_gui_gpu_upload_ready(&renderer, &upload)) {
		SDL_CancelGPUCommandBuffer(command_buffer);
		rg_gui_gpu_upload_abort(&renderer, &upload);
		SDL_SetError("Upload packet was not ready for ordered submission");
		goto cleanup;
	}
	if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
		rg_gui_gpu_upload_abort(&renderer, &upload);
		goto cleanup;
	}
	rg_gui_gpu_upload_commit(&renderer, &upload);
	if (!renderer.text.cache_initialized) goto cleanup;
	if (!stats_valid)
	{
		SDL_SetError("rg_gui GPU device stats did not match the encoded frame "
		             "(geometry_vertices=%u text_instances=%u items=%u draw_calls=%u dispatches=%u "
		             "image_bind_calls=%u custom_image_draw_calls=%u image_bind_failures=%u)",
		             stats.geometry_vertices, stats.text_instances, stats.items, stats.draw_calls,
		             stats.dispatches, stats.image_bind_calls, stats.custom_image_draw_calls,
		             stats.image_bind_failures);
		goto cleanup;
	}
	/* Submit two more frames without a GPU wait, wrapping the two frame buffers. */
	for (u32 frame = 0u; frame < 2u; frame++) {
		rg_gui_renderer_begin_frame(&text_renderer);
		if (!rg_gui_gpu_prepare(&renderer, &text_renderer, list, overlay_start)) goto cleanup;
		rg_gpu_upload_ring_begin(&ring, 1);
		staged = rg_gui_gpu_stage_upload(&renderer, &text_renderer, &ring, &upload);
		rg_gpu_upload_ring_end(&ring);
		if (!staged || upload.text.cached_quads.size || upload.text.full_cache_upload) goto cleanup;
		command_buffer = SDL_AcquireGPUCommandBuffer(device);
		if (!command_buffer) goto cleanup;
		copy = SDL_BeginGPUCopyPass(command_buffer);
		if (!copy) { SDL_CancelGPUCommandBuffer(command_buffer); goto cleanup; }
		rg_gui_gpu_encode_upload(&renderer, copy, &ring, &upload);
		SDL_EndGPUCopyPass(copy);
		if (!rg_gui_gpu_dispatch_upload(&renderer, command_buffer, &upload)) {
			SDL_CancelGPUCommandBuffer(command_buffer); goto cleanup;
		}
		pass = SDL_BeginGPURenderPass(command_buffer, &target, 1u, NULL);
		if (!pass) { SDL_CancelGPUCommandBuffer(command_buffer); goto cleanup; }
		rg_gui_gpu_draw(&renderer, command_buffer, pass, &draw_desc, &upload);
		SDL_EndGPURenderPass(pass);
		if (frame == 1u) {
			copy = SDL_BeginGPUCopyPass(command_buffer);
			if (!copy) { SDL_CancelGPUCommandBuffer(command_buffer); goto cleanup; }
			SDL_GPUTextureRegion source = {0};
			source.texture = target_texture; source.w = 64u; source.h = 64u; source.d = 1u;
			SDL_GPUTextureTransferInfo destination = {0};
			destination.transfer_buffer = readback;
			destination.pixels_per_row = 64u; destination.rows_per_layer = 64u;
			SDL_DownloadFromGPUTexture(copy, &source, &destination);
			SDL_EndGPUCopyPass(copy);
		}
		if (!rg_gui_gpu_upload_ready(&renderer, &upload)) {
			SDL_CancelGPUCommandBuffer(command_buffer);
			rg_gui_gpu_upload_abort(&renderer, &upload);
			SDL_SetError("Upload packet was not ready for ordered submission");
			goto cleanup;
		}
		if (!SDL_SubmitGPUCommandBuffer(command_buffer)) {
			rg_gui_gpu_upload_abort(&renderer, &upload); goto cleanup;
		}
		rg_gui_gpu_upload_commit(&renderer, &upload);
	}
	rg_gpu_wait_idle(device);
	const u8* pixels = (const u8*)SDL_MapGPUTransferBuffer(device, readback, false);
	if (!pixels) goto cleanup;
	static const struct { u32 x, y; u8 r, g, b; } probes[] = {
	    {4, 4, 255, 255, 255}, {12, 4, 64, 128, 192},
	    {20, 4, 64, 128, 192}, {28, 4, 64, 128, 192},
	    {36, 4, 64, 128, 192}, {44, 4, 0, 0, 0},
	    {52, 4, 64, 128, 192}, {60, 4, 255, 255, 255},
	    {56, 12, 64, 128, 192}, {4, 18, 0, 0, 0}, {4, 24, 255, 0, 0},
	    {12, 24, 0, 0, 0}, {20, 24, 0, 255, 0}, {36, 24, 128, 0, 127},
	    {49, 21, 255, 255, 0}, {54, 26, 0, 0, 0},
	    /* Lower-left triangle of the custom-material quad: the custom pipeline
	       must receive both triangles of each four-vertex quad. */
	    {25, 6, 64, 128, 192}};
	int pixels_valid = 1;
	for (u32 i = 0u; i < RG_ARRAY_COUNT(probes); i++) {
		const u8* pixel = pixels + (probes[i].y * 64u + probes[i].x) * 4u;
		if (abs((int)pixel[0] - probes[i].r) > 1 || abs((int)pixel[1] - probes[i].g) > 1 ||
		    abs((int)pixel[2] - probes[i].b) > 1 || pixel[3] != 255u) {
			fprintf(stderr, "Material/clip/overlay pixel mismatch at %u,%u: RGBA %u,%u,%u,%u\n",
			        probes[i].x, probes[i].y, pixel[0], pixel[1], pixel[2], pixel[3]);
			pixels_valid = 0;
		}
	}
	SDL_UnmapGPUTransferBuffer(device, readback);
	if (!pixels_valid) { SDL_SetError("Indexed material/clip/alpha/overlay pixel regression"); goto cleanup; }
	puts("Indexed material, clip, alpha, triangle, and overlay: 17 GPU pixel probes passed");
	if (!test_gpu_full_cache_ring(device, target_texture, readback, &renderer, &text_renderer, &ring)) goto cleanup;
	if (!test_gpu_clip_distances(device, target_texture, readback, &renderer, &text_renderer, &ring)) goto cleanup;
	if (!test_gpu_hidden_culling(device, &renderer, &text_renderer, &ring)) goto cleanup;
	if (!test_gpu_hidden_culling_edges(device, &renderer, &text_renderer, &ring)) goto cleanup;
	if (!test_gpu_mutable_value_text(device, 2u) || !test_gpu_mutable_value_text(device, 3u)) goto cleanup;

	puts("rg_gui_gpu end-to-end SDL_GPU checks passed");
	result = 0;

cleanup:
	if (result != 0) fprintf(stderr, "rg_gui_gpu device check failed: %s\n", SDL_GetError());
	if (device) rg_gpu_wait_idle(device);
	free(text_memory);
	free(gui_memory);
	rg_gpu_upload_ring_destroy(&ring);
	rg_gui_gpu_destroy(&renderer);
	if (readback) SDL_ReleaseGPUTransferBuffer(device, readback);
	if (atlas_upload) SDL_ReleaseGPUTransferBuffer(device, atlas_upload);
	if (target_texture) SDL_ReleaseGPUTexture(device, target_texture);
	if (atlas) SDL_ReleaseGPUTexture(device, atlas);
	if (window_claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
	if (device) rg_gpu_device_destroy(device);
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
