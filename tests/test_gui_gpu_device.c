// End-to-end SDL_GPU submission checks for rg_gui_gpu

#define RG_SPRINTF_NO_ASM 1
#include "../src/rg_gui_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

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

int main(void)
{
	int result = 1;
	int window_claimed = 0;
	SDL_Window* window = NULL;
	SDL_GPUDevice* device = NULL;
	SDL_GPUTexture* atlas = NULL;
	SDL_GPUTexture* target_texture = NULL;
	RgGuiGpuRenderer renderer = {0};
	RgGpuUploadRing ring = {0};
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
	desc.max_geometry_vertices = 64u;
	desc.max_items = 16u;
	desc.min_filter = SDL_GPU_FILTER_NEAREST;
	desc.mag_filter = SDL_GPU_FILTER_NEAREST;
	if (!rg_gui_gpu_create(&renderer, &desc)) goto cleanup;
	if (!rg_gpu_upload_ring_init(&ring, device, MB(1))) goto cleanup;

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
	rg_gui_push_text_static(&gui, "A", rg_vec2(16.0f, 4.0f), white);
	const RgGuiDrawList* list = rg_gui_draw_list(&gui);
	rg_gui_renderer_begin_frame(&text_renderer);
	if (!rg_gui_gpu_prepare(&renderer, &text_renderer, list,
	                        rg_gui_draw_list_overlay_start(&gui)))
		goto cleanup;

	RgGuiGpuUpload upload = {0};
	rg_gpu_upload_ring_begin(&ring, 1);
	int staged = rg_gui_gpu_stage_upload(&renderer, &text_renderer, &ring, &upload);
	rg_gpu_upload_ring_end(&ring);
	if (!staged) goto cleanup;

	SDL_GPUCommandBuffer* command_buffer = SDL_AcquireGPUCommandBuffer(device);
	if (!command_buffer) goto cleanup;
	SDL_GPUCopyPass* copy = SDL_BeginGPUCopyPass(command_buffer);
	if (!copy)
	{
		SDL_CancelGPUCommandBuffer(command_buffer);
		goto cleanup;
	}
	rg_gui_gpu_encode_upload(&renderer, &text_renderer, copy, &ring, &upload);
	SDL_EndGPUCopyPass(copy);
	if (!rg_gui_gpu_dispatch(&renderer, command_buffer))
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
	int stats_valid = stats.geometry_vertices == 12u &&
	                  stats.text_instances == 1u && stats.draw_calls == 3u;
	if (!SDL_SubmitGPUCommandBuffer(command_buffer)) goto cleanup;
	if (!stats_valid)
	{
		SDL_SetError("rg_gui GPU device stats did not match the encoded frame");
		goto cleanup;
	}
	rg_gpu_wait_idle(device);

	puts("rg_gui_gpu end-to-end SDL_GPU checks passed");
	result = 0;

cleanup:
	if (result != 0) fprintf(stderr, "rg_gui_gpu device check failed: %s\n", SDL_GetError());
	if (device) rg_gpu_wait_idle(device);
	free(text_memory);
	free(gui_memory);
	rg_gpu_upload_ring_destroy(&ring);
	rg_gui_gpu_destroy(&renderer);
	if (target_texture) SDL_ReleaseGPUTexture(device, target_texture);
	if (atlas) SDL_ReleaseGPUTexture(device, atlas);
	if (window_claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
	if (device) rg_gpu_device_destroy(device);
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
