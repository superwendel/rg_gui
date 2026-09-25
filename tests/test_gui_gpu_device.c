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
		return RG_GUI_GPU_IMAGE_BIND_CUSTOM;
	}
	state->failed_calls++;
	SDL_BindGPUGraphicsPipeline(info->pass, state->failure_pipeline);
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
	RgGuiDrawCmd commands[48] = {0};
	for (u32 i = 0u; i < 16u; i++)
	{
		commands[i].type = RG_GUI_CMD_TEXT;
		commands[i].data.text.text = i & 1u ? "????" : "AAAA";
		commands[i].data.text.pos = rg_vec2((f32)i * 4.0f, 48.0f);
		commands[i].data.text.scale = 1.0f;
		commands[i].data.text.color = rg_gui_color(1, 1, 1, 1);
	}
	for (u32 i = 0u; i < 32u; i++)
	{
		RgGuiDrawCmd* triangle = &commands[16u + i];
		f32 x = (f32)i * 2.0f;
		triangle->type = RG_GUI_CMD_TRIANGLE;
		triangle->data.triangle.a = rg_vec2(x, 32.0f);
		triangle->data.triangle.b = rg_vec2(x + 2.0f, 32.0f);
		triangle->data.triangle.c = rg_vec2(x, 34.0f);
	}
	RgGuiDrawList list = {commands, RG_ARRAY_COUNT(commands), RG_ARRAY_COUNT(commands)};
	rg_gui_renderer_clear_cache(text);
	for (u32 phase = 0u; phase < 2u; phase++)
	{
		/* Two distinct four-glyph strings fill both 32-quad cache pages. Their
		 * repeated occurrences also reach all 16 runs and 64 frame glyphs. */
		rg_gui_gpu_invalidate_cache(gpu);
		for (u32 i = 16u; i < RG_ARRAY_COUNT(commands); i++)
			commands[i].data.triangle.color = rg_gui_color(1, phase ? 0.0f : 1.0f, 0, 1);
		rg_gui_renderer_begin_frame(text);
		if (!rg_gui_gpu_prepare(gpu, text, &list, list.count)) goto cleanup;
		rg_gpu_upload_ring_begin(ring, 1);
		int staged = rg_gui_gpu_stage_upload(gpu, text, ring, &upload);
		u32 used = ring->offset;
		rg_gpu_upload_ring_end(ring);
		if (!staged || !upload.text.full_cache_upload || upload.text.run_count != 16u ||
		    upload.text.glyph_count != 64u || upload.vertex_count != 96u || upload.ref_count != 96u ||
		    upload.text.cached_quads.size != 64u * sizeof(RgGuiRendererCachedQuad) ||
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
	int stats_valid = stats.geometry_vertices == 69u && stats.text_instances == 1u &&
	                  stats.items == 10u && stats.draw_calls > 0u && stats.dispatches == 0u &&
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
		SDL_SetError("rg_gui GPU device stats did not match the encoded frame");
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
	    {49, 21, 255, 255, 0}, {54, 26, 0, 0, 0}};
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
	puts("Indexed material, clip, alpha, triangle, and overlay: 16 GPU pixel probes passed");
	if (!test_gpu_full_cache_ring(device, target_texture, readback, &renderer, &text_renderer, &ring)) goto cleanup;
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
