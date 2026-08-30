// Ordered draw-list preparation checks for rg_gui_gpu

#define RG_SPRINTF_NO_ASM 1
#include "../src/rg_gui_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static void test_font_init(RgTextFont* font, RgTextGlyph glyphs[2])
{
	memset(font, 0, sizeof(*font));
	memset(glyphs, 0, sizeof(RgTextGlyph) * 2u);
	font->metrics.atlas_width = 64u;
	font->metrics.atlas_height = 32u;
	font->metrics.line_height = 10;
	font->glyphs = glyphs;
	font->glyph_count = 2u;
	font->glyph_capacity = 2u;
	font->fallback_codepoint = '?';
	glyphs[0].codepoint = '?';
	glyphs[0].w = 4;
	glyphs[0].h = 8;
	glyphs[0].x_advance = 5;
	glyphs[1].codepoint = 'A';
	glyphs[1].x = 4;
	glyphs[1].w = 7;
	glyphs[1].h = 8;
	glyphs[1].x_advance = 8;
}

int main(void)
{
	RgTextFont font;
	RgTextGlyph glyphs[2];
	test_font_init(&font, glyphs);

	void* gui_memory = malloc(MB(16));
	if (!gui_memory) return 1;
	RgArena gui_arena = {(char*)gui_memory, MB(16), 0u, MB(16)};
	RgGuiContext gui;
	RgGuiInitDesc gui_desc = {0};
	gui_desc.font = &font;
	gui_desc.max_draw_cmds = 64u;
	gui_desc.text_buffer_size = KB(4);
	if (!rg_gui_init(&gui, &gui_arena, &gui_desc)) return 1;

	RgGuiRendererLimits limits = rg_gui_renderer_limits_default();
	limits.max_cached_runs = 16u;
	limits.hash_slot_count = 32u;
	limits.text_capacity = KB(4);
	limits.max_cached_quads = 64u;
	limits.max_frame_instances = 64u;
	limits.max_batches = 16u;
	size_t renderer_size = rg_gui_renderer_memory_required(&limits, 32u);
	void* renderer_memory = malloc(renderer_size);
	if (!renderer_memory) return 1;
	RgArena renderer_arena = {(char*)renderer_memory, renderer_size, 0u, renderer_size};
	RgGuiRenderer text_renderer;
	RgGuiRendererInitDesc renderer_desc = {0};
	renderer_desc.font = &font;
	renderer_desc.limits = limits;
	renderer_desc.page_quads = 32u;
	if (!rg_gui_renderer_init(&text_renderer, &renderer_arena, &renderer_desc)) return 1;

	rg_vec4 white = rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f);
	rg_gui_push_rect(&gui, rg_gui_make_rect(0.0f, 0.0f, 10.0f, 10.0f), white);
	rg_gui_push_clip(&gui, rg_gui_make_rect(2.0f, 2.0f, 20.0f, 20.0f));
	rg_gui_push_triangle(&gui, rg_vec2(2.0f, 2.0f), rg_vec2(8.0f, 2.0f),
	                     rg_vec2(5.0f, 8.0f), white);
	rg_gui_pop_clip(&gui);
	rg_gui_push_image(&gui, rg_gui_make_rect(10.0f, 0.0f, 8.0f, 8.0f),
	                  rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                  (RgGuiTexture)(uintptr_t)1u);
	char mutable_text[8] = "A";
	u32 mutable_command = rg_gui_draw_list(&gui)->count;
	rg_gui_label(&gui, mutable_text, rg_gui_make_rect(20.0f, 0.0f, 24.0f, 10.0f));
	memcpy(mutable_text, "AAAA", 5u);
	const RgGuiDrawList* mutable_list = rg_gui_draw_list(&gui);
	if (mutable_command >= mutable_list->count ||
	    strcmp(mutable_list->cmds[mutable_command].data.text.text, "A") != 0 ||
	    mutable_list->cmds[mutable_command].data.text.cache_identity != 0u)
	{
		fprintf(stderr, "mutable label text was not isolated from its source buffer\n");
		return 1;
	}
	rg_gui_push_text_static(&gui, "A", rg_vec2(20.0f, 4.0f), white);

	RgGuiGpuVertex vertices[32];
	RgGuiGpuItem items[16];
	RgGuiGpuRenderer gpu = {0};
	gpu.vertices = vertices;
	gpu.vertex_capacity = RG_ARRAY_COUNT(vertices);
	gpu.items = items;
	gpu.item_capacity = RG_ARRAY_COUNT(items);

	rg_gui_renderer_begin_frame(&text_renderer);
	const RgGuiDrawList* list = rg_gui_draw_list(&gui);
	if (!rg_gui_gpu_prepare(&gpu, &text_renderer, list, list->count) ||
	    gpu.vertex_count != 15u || gpu.item_count != 4u ||
	    gpu.items[0].type != RG_GUI_GPU_ITEM_SOLID ||
	    gpu.items[1].type != RG_GUI_GPU_ITEM_SOLID ||
	    !gpu.items[1].clip_enabled ||
	    gpu.items[2].type != RG_GUI_GPU_ITEM_IMAGE ||
	    gpu.items[3].type != RG_GUI_GPU_ITEM_TEXT ||
	    gpu.items[3].count != 2u)
	{
		fprintf(stderr, "ordered draw-list preparation failed (vertices=%u items=%u",
		        gpu.vertex_count, gpu.item_count);
		for (u32 i = 0u; i < gpu.item_count; i++)
			fprintf(stderr, " item[%u]={type=%u,count=%u}", i,
			        gpu.items[i].type, gpu.items[i].count);
		fprintf(stderr, ")\n");
		return 1;
	}

	free(renderer_memory);
	free(gui_memory);
	puts("rg_gui_gpu draw-list preparation checks passed");
	return 0;
}
