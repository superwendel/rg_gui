// Numeric-widget lifetime regression through the real frontend, text cache,
// upload, compute, draw and GPU readback paths. Each digit samples a distinct
// atlas color, so stale text is observable without depending on a font asset.
#ifndef TEST_GUI_GPU_TEXT_LIFETIME_H
#define TEST_GUI_GPU_TEXT_LIFETIME_H

static int test_gpu_mutable_value_text(SDL_GPUDevice* device, u32 frame_buffer_count)
{
	int passed = 0;
	void* gui_memory = NULL;
	void* text_memory = NULL;
	SDL_GPUTexture* atlas = NULL;
	SDL_GPUTexture* target = NULL;
	SDL_GPUTransferBuffer* atlas_upload = NULL;
	SDL_GPUTransferBuffer* readbacks[3] = {0};
	SDL_GPUCommandBuffer* command = NULL;
	SDL_GPUFence* fences[3] = {0};
	rg_vec2 positions[3][6];
	RgGuiGpuRenderer gpu = {0};
	RgGpuUploadRing ring = {0};
	RgGuiGpuUpload upload = {0};
	RgTextFont font = {0};
	RgTextGlyph glyphs[12] = {0};
	font.metrics.atlas_width = 64u;
	font.metrics.atlas_height = 1u;
	font.metrics.line_height = 10;
	font.glyphs = glyphs;
	font.glyph_count = font.glyph_capacity = 12u;
	font.fallback_codepoint = '?';
	for (u32 i = 0u; i < 12u; i++)
	{
		glyphs[i].codepoint = i == 0u ? '.' : (i == 11u ? '?' : '0' + i - 1u);
		glyphs[i].x = (i32)i;
		glyphs[i].w = glyphs[i].h = 1;
		glyphs[i].x_advance = 2;
	}
	RgGuiInitDesc gui_desc = {0};
	gui_desc.font = &font;
	gui_desc.max_draw_cmds = 128u;
	gui_desc.text_buffer_size = KB(1);
	gui_desc.value_cache_size = 2u;
	size_t gui_bytes = rg_gui_memory_required(&gui_desc);
	if (!gui_bytes || !(gui_memory = malloc(gui_bytes))) goto cleanup;
	RgArena gui_arena = {(char*)gui_memory, gui_bytes, 0u, gui_bytes};
	RgGuiContext gui;
	if (!rg_gui_init(&gui, &gui_arena, &gui_desc)) goto cleanup;
	gui.style.text_height = 10.0f;
	gui.style.color_text = rg_gui_color(1, 1, 1, 1);
	gui.style.color_text_dim = gui.style.color_text;

	RgGuiRendererInitDesc text_desc = {0};
	text_desc.font = &font;
	text_desc.page_quads = 8u;
	text_desc.limits = rg_gui_renderer_limits_default();
	text_desc.limits.max_cached_runs = 32u;
	text_desc.limits.hash_slot_count = 64u;
	text_desc.limits.text_capacity = KB(1);
	text_desc.limits.max_cached_quads = 256u;
	text_desc.limits.max_frame_instances = 64u;
	text_desc.limits.max_frame_runs = 16u;
	text_desc.limits.max_batches = 16u;
	size_t text_bytes = rg_gui_renderer_memory_required_ex(&text_desc);
	if (text_bytes == SIZE_MAX || !(text_memory = malloc(text_bytes))) goto cleanup;
	RgArena text_arena = {(char*)text_memory, text_bytes, 0u, text_bytes};
	RgGuiRenderer text;
	if (!rg_gui_renderer_init(&text, &text_arena, &text_desc)) goto cleanup;

	SDL_GPUTextureCreateInfo texture_info = {0};
	texture_info.type = SDL_GPU_TEXTURETYPE_2D;
	texture_info.format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	texture_info.usage = SDL_GPU_TEXTUREUSAGE_SAMPLER;
	texture_info.width = 64u; texture_info.height = 1u;
	texture_info.layer_count_or_depth = texture_info.num_levels = 1u;
	atlas = SDL_CreateGPUTexture(device, &texture_info);
	texture_info.usage = SDL_GPU_TEXTUREUSAGE_COLOR_TARGET;
	texture_info.width = 192u; texture_info.height = 160u;
	target = SDL_CreateGPUTexture(device, &texture_info);
	if (!atlas || !target) goto cleanup;
	SDL_GPUTransferBufferCreateInfo transfer_info = {0};
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_info.size = 256u;
	atlas_upload = SDL_CreateGPUTransferBuffer(device, &transfer_info);
	transfer_info.usage = SDL_GPU_TRANSFERBUFFERUSAGE_DOWNLOAD;
	transfer_info.size = 192u * 160u * 4u;
	for (u32 frame = 0u; frame < 3u; frame++)
		if (!(readbacks[frame] = SDL_CreateGPUTransferBuffer(device, &transfer_info))) goto cleanup;
	if (!atlas_upload) goto cleanup;
	u8* atlas_pixels = (u8*)SDL_MapGPUTransferBuffer(device, atlas_upload, false);
	if (!atlas_pixels) goto cleanup;
	memset(atlas_pixels, 0, 256u);
	for (u32 i = 0u; i < 12u; i++)
	{
		atlas_pixels[i * 4u] = (u8)(16u + i * 16u);
		atlas_pixels[i * 4u + 1u] = (u8)(240u - i * 16u);
		atlas_pixels[i * 4u + 2u] = 17u;
		atlas_pixels[i * 4u + 3u] = 255u;
	}
	SDL_UnmapGPUTransferBuffer(device, atlas_upload);
	RgGuiGpuDesc gpu_desc = {0};
	gpu_desc.device = device;
	gpu_desc.target_format = SDL_GPU_TEXTUREFORMAT_R8G8B8A8_UNORM;
	gpu_desc.shader_root = "shaders";
	gpu_desc.atlas_texture = atlas;
	gpu_desc.atlas_width = 64u; gpu_desc.atlas_height = 1u;
	gpu_desc.max_cached_quads = 256u;
	gpu_desc.max_runs = 16u; gpu_desc.max_text_instances = 64u;
	gpu_desc.max_geometry_vertices = 512u; gpu_desc.max_items = 32u;
	gpu_desc.frame_buffer_count = frame_buffer_count;
	gpu_desc.min_filter = gpu_desc.mag_filter = SDL_GPU_FILTER_NEAREST;
	if (!rg_gui_gpu_create(&gpu, &gpu_desc) ||
	    !rg_gpu_upload_ring_init(&ring, device, KB(64))) goto cleanup;
	RgInputState input;
	rg_input_init(&input);
	input.mouse_x = input.mouse_y = -100;
	static const f32 fractions[3] = {0.125f, 0.875f, 0.625f};
	static const char* expected[3][6] = {
	    {"0.125", "1.125", "2.125", "3.125", "4.125", "5.125"},
	    {"0.875", "1.875", "2.875", "3.875", "4.875", "5.875"},
	    {"0.625", "1.625", "2.625", "3.625", "4.625", "5.625"}};
	for (u32 frame = 0u; frame < 3u; frame++)
	{
		rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
		for (u32 row = 0u; row < 6u; row++)
		{
			f32 value = (f32)row + fractions[frame];
			rg_gui_slider_float(&gui, NULL, &value, 0, 10,
			                    rg_gui_make_rect(0, (f32)row * 24.0f, 180, 20), 100u + row);
		}
		rg_gui_end_frame(&gui);
		if (rg_gui_diagnostics(&gui)->flags) goto cleanup;
		/* Read positions only from the commands. The expected text/pixel colors
		 * come from independent constants, not from cached or prepared glyphs. */
		u32 text_count = 0u;
		const RgGuiDrawList* list = rg_gui_draw_list(&gui);
		for (u32 i = 0u; i < list->count; i++)
		{
			if (list->cmds[i].type != RG_GUI_CMD_TEXT) continue;
			if (text_count >= 6u) goto cleanup;
			positions[frame][text_count++] = list->cmds[i].data.text.pos;
		}
		if (text_count != 6u) goto cleanup;
		/* Reuse the same cache pages for different digits while previous GPU
		 * submissions may still read them. The persistent non-cycled mirror must
		 * preserve queue dependencies rather than expose a later frame's bytes. */
		if (frame) rg_gui_renderer_clear_cache(&text);
		rg_gui_renderer_begin_frame(&text);
		if (!rg_gui_gpu_prepare(&gpu, &text, list, rg_gui_draw_list_overlay_start(&gui))) goto cleanup;
		const RgGuiRendererStats* text_stats = rg_gui_renderer_stats(&text);
		if (text_stats->diagnostic_flags || text_stats->frame_dropped_runs ||
		    text_stats->frame_dropped_glyphs || text_stats->frame_cache_bypasses) goto cleanup;
		rg_gpu_upload_ring_begin(&ring, 1);
		int staged = rg_gui_gpu_stage_upload(&gpu, &text, &ring, &upload);
		rg_gpu_upload_ring_end(&ring);
		if (!staged) goto cleanup;
		if (frame && (upload.text.full_cache_upload || upload.text.cache_range_count != 1u ||
		              upload.text.cache_ranges[0].first_quad != 0u ||
		              upload.text.cache_ranges[0].quad_count != 48u)) goto cleanup;
		/* Preparation of B is allowed while A waits for GPU encoding. B has a
		 * different item list, glyph count and dispatch count; rendering A must
		 * consume its immutable packet rather than any latest-prepared state. */
		rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
		rg_gui_push_text(&gui, "9", rg_vec2(1, 1), gui.style.color_text);
		rg_gui_end_frame(&gui);
		rg_gui_renderer_begin_frame(&text);
		if (!rg_gui_gpu_prepare(&gpu, &text, rg_gui_draw_list(&gui),
		                        rg_gui_draw_list_overlay_start(&gui))) goto cleanup;
		if (upload.text.glyph_count != 30u || upload.text.run_count != 6u ||
		    gpu.text_prepared->glyph_count != 1u || gpu.text_prepared->run_count != 1u) goto cleanup;
		command = SDL_AcquireGPUCommandBuffer(device);
		if (!command) goto cleanup;
		SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command);
		if (!copy) goto cleanup;
		if (frame == 0u)
		{
			SDL_GPUTextureTransferInfo source = {0};
			source.transfer_buffer = atlas_upload;
			source.pixels_per_row = 64u; source.rows_per_layer = 1u;
			SDL_GPUTextureRegion destination = {0};
			destination.texture = atlas; destination.w = 64u;
			destination.h = destination.d = 1u;
			SDL_UploadToGPUTexture(copy, &source, &destination, false);
		}
		rg_gui_gpu_encode_upload(&gpu, copy, &ring, &upload);
		SDL_EndGPUCopyPass(copy);
		if (!rg_gui_gpu_dispatch_upload(&gpu, command, &upload)) goto cleanup;
		SDL_GPUColorTargetInfo color = {0};
		color.texture = target; color.clear_color = (SDL_FColor){0, 0, 0, 1};
		color.load_op = SDL_GPU_LOADOP_CLEAR; color.store_op = SDL_GPU_STOREOP_STORE;
		SDL_GPURenderPass* pass = SDL_BeginGPURenderPass(command, &color, 1u, NULL);
		if (!pass) goto cleanup;
		RgGuiGpuDrawDesc draw = {0};
		draw.output_width = 192u; draw.output_height = 160u;
		draw.viewport = (SDL_Rect){0, 0, 192, 160};
		RgGuiGpuStats drawn = rg_gui_gpu_draw(&gpu, command, pass, &draw, &upload);
		SDL_EndGPURenderPass(pass);
		if (drawn.text_instances != 30u || !rg_gui_gpu_upload_ready(&gpu, &upload)) goto cleanup;
		copy = SDL_BeginGPUCopyPass(command);
		if (!copy) goto cleanup;
		SDL_GPUTextureRegion source = {0};
		source.texture = target; source.w = 192u; source.h = 160u; source.d = 1u;
		SDL_GPUTextureTransferInfo destination = {0};
		destination.transfer_buffer = readbacks[frame];
		destination.pixels_per_row = 192u; destination.rows_per_layer = 160u;
		SDL_DownloadFromGPUTexture(copy, &source, &destination);
		SDL_EndGPUCopyPass(copy);
		fences[frame] = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
		command = NULL;
		if (!fences[frame]) goto cleanup;
		rg_gui_gpu_upload_commit(&gpu, &upload);
		memset(&upload, 0, sizeof(upload));
	}
	/* No CPU wait or readback map is allowed until all three changing frames
	 * have been submitted. Read each result, not just the final rendered frame. */
	if (!SDL_WaitForGPUFences(device, true, fences, 3u)) goto cleanup;
	for (u32 frame = 0u; frame < 3u; frame++)
	{
		const u8* pixels = (const u8*)SDL_MapGPUTransferBuffer(device, readbacks[frame], false);
		if (!pixels) goto cleanup;
		int pixels_match = 1;
		for (u32 row = 0u; row < 6u && pixels_match; row++)
			for (u32 c = 0u; c < 5u; c++)
			{
				u32 x = (u32)positions[frame][row].x + c * 2u;
				u32 y = (u32)positions[frame][row].y;
				u32 glyph = expected[frame][row][c] == '.' ? 0u : (u32)(expected[frame][row][c] - '0' + 1);
				if (x >= 192u || y >= 160u) { pixels_match = 0; break; }
				const u8* pixel = pixels + (y * 192u + x) * 4u;
				if (pixel[0] != 16u + glyph * 16u || pixel[1] != 240u - glyph * 16u ||
				    pixel[2] != 17u || pixel[3] != 255u)
				{
					fprintf(stderr, "Numeric GPU pixel mismatch: frame %u row %u character %u (%c), RGBA %u,%u,%u,%u\n",
					        frame, row, c, expected[frame][row][c], pixel[0], pixel[1], pixel[2], pixel[3]);
					pixels_match = 0; break;
				}
			}
		SDL_UnmapGPUTransferBuffer(device, readbacks[frame]);
		if (!pixels_match) { SDL_SetError("Mutable numeric text rendered stale GPU glyphs"); goto cleanup; }
	}
	passed = 1;
	printf("Mutable numeric text: 90 GPU digit/decimal pixels passed across three queued frames, %u buffers, and cache-page reuse\n",
	       frame_buffer_count);
cleanup:
	if (command) SDL_CancelGPUCommandBuffer(command);
	rg_gui_gpu_upload_abort(&gpu, &upload);
	if (device) SDL_WaitForGPUIdle(device);
	for (u32 frame = 0u; frame < 3u; frame++)
		if (fences[frame]) SDL_ReleaseGPUFence(device, fences[frame]);
	rg_gpu_upload_ring_destroy(&ring);
	rg_gui_gpu_destroy(&gpu);
	for (u32 frame = 0u; frame < 3u; frame++)
		if (readbacks[frame]) SDL_ReleaseGPUTransferBuffer(device, readbacks[frame]);
	if (atlas_upload) SDL_ReleaseGPUTransferBuffer(device, atlas_upload);
	if (target) SDL_ReleaseGPUTexture(device, target);
	if (atlas) SDL_ReleaseGPUTexture(device, atlas);
	free(text_memory); free(gui_memory);
	return passed;
}

#endif
