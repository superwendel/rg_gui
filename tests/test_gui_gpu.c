// Ordered draw-list preparation checks for rg_gui_gpu

#define RG_SPRINTF_NO_ASM 1
#include "../src/rg_gui_gpu.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <float.h>

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

#define TEST_CHECK(condition) do { if (!(condition)) { \
	fprintf(stderr, "GPU preparation check failed at line %d: %s\n", __LINE__, #condition); \
	return 0; } } while (0)

typedef struct TestGpuFixture
{
	RgTextFont font;
	RgTextGlyph glyphs[2];
	void* memory;
	RgGuiRenderer text;
	RgGuiGpuRenderer gpu;
	RgGuiGpuVertex vertices[64];
	RgGuiGpuItem items[16];
} TestGpuFixture;

static int test_rect_vertex_packing(void)
{
	static const RgGuiRect rects[] = {
		{12.5f, -7.25f, 4.5f, 3.75f}, {-0.0f, 0.0f, 0.0f, -0.0f},
		{1.0e30f, -1.0e30f, -1.0e30f, 1.0e30f},
		{-FLT_MAX, -FLT_MAX, FLT_MAX, FLT_MAX},
		{0.0f, 0.0f, -2.0f, 3.0f}, {FLT_MIN, -FLT_MIN, FLT_MIN, FLT_MIN}
	};
	static const struct { u32 count, capacity; } bounds[] = {
		{0u, 0u}, {0u, 5u}, {0u, 6u}, {1u, 6u}, {1u, 7u},
		{4u, 10u}, {10u, 9u}, {UINT32_MAX, UINT32_MAX}
	};
	static const u32 colors[] = {0u, 0xffabcdefu, UINT32_MAX};
	// A 20-byte vertex stride exercises all four 16-byte alignment residues
	// while retaining the natural alignment required by RgGuiGpuVertex.
	for (u32 offset = 0u; offset < 4u; offset++)
	for (u32 r = 0u; r < RG_ARRAY_COUNT(rects); r++)
	for (u32 c = 0u; c < RG_ARRAY_COUNT(colors); c++)
	for (u32 b = 0u; b < RG_ARRAY_COUNT(bounds); b++)
	{
		RgGuiGpuVertex expected[16], actual[16];
		memset(expected, 0xa5, sizeof(expected));
		memset(actual, 0xa5, sizeof(actual));
		RgGuiGpuRenderer gpu = {0};
		gpu.vertices = actual + offset;
		gpu.vertex_count = bounds[b].count;
		gpu.vertex_capacity = bounds[b].capacity;
		RgGuiRect rect = rects[r], uv = rects[(r + 1u) % RG_ARRAY_COUNT(rects)];
		u32 color = colors[c];
		int fits = bounds[b].count <= bounds[b].capacity &&
		           bounds[b].capacity - bounds[b].count >= 6u;
		if (fits)
		{
			RgGuiGpuVertex* out = expected + offset + bounds[b].count;
			out[0] = (RgGuiGpuVertex){rect.x, rect.y, color, uv.x, uv.y};
			out[1] = (RgGuiGpuVertex){rect.x + rect.w, rect.y, color, uv.x + uv.w, uv.y};
			out[2] = (RgGuiGpuVertex){rect.x + rect.w, rect.y + rect.h, color, uv.x + uv.w, uv.y + uv.h};
			out[3] = out[0];
			out[4] = out[2];
			out[5] = (RgGuiGpuVertex){rect.x, rect.y + rect.h, color, uv.x, uv.y + uv.h};
		}
		TEST_CHECK(rg_gui_gpu_add_rect_vertices(&gpu, rect, uv, color) == fits);
		TEST_CHECK(gpu.vertex_count == bounds[b].count + (fits ? 6u : 0u));
		TEST_CHECK(memcmp(expected, actual, sizeof(actual)) == 0);
	}
	return 1;
}

static RgGuiRendererLimits test_limits(void)
{
	RgGuiRendererLimits limits = rg_gui_renderer_limits_default();
	limits.max_cached_runs = 16u;
	limits.hash_slot_count = 32u;
	limits.text_capacity = KB(4);
	limits.max_cached_quads = 64u;
	limits.max_frame_instances = 64u;
	limits.max_batches = 16u;
	return limits;
}

static int test_fixture_init(TestGpuFixture* fixture, RgGuiRendererLimits limits)
{
	memset(fixture, 0, sizeof(*fixture));
	test_font_init(&fixture->font, fixture->glyphs);
	fixture->font.fallback_codepoint = UINT32_MAX;
	size_t size = rg_gui_renderer_memory_required(&limits, 2u);
	fixture->memory = malloc(size);
	TEST_CHECK(fixture->memory);
	RgArena arena = {(char*)fixture->memory, size, 0u, size};
	RgGuiRendererInitDesc desc = {0};
	desc.font = &fixture->font;
	desc.limits = limits;
	desc.page_quads = 2u;
	TEST_CHECK(rg_gui_renderer_init(&fixture->text, &arena, &desc));
	fixture->gpu.vertices = fixture->vertices;
	fixture->gpu.vertex_capacity = RG_ARRAY_COUNT(fixture->vertices);
	fixture->gpu.items = fixture->items;
	fixture->gpu.item_capacity = RG_ARRAY_COUNT(fixture->items);
	return 1;
}

static RgGuiDrawCmd test_text_command(const char* text, f32 scale, int identity)
{
	RgGuiDrawCmd cmd = {0};
	cmd.type = RG_GUI_CMD_TEXT;
	cmd.data.text.text = text;
	cmd.data.text.scale = scale;
	cmd.data.text.pos = rg_vec2(10.0f, 20.0f);
	cmd.data.text.color = rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f);
	cmd.data.text.cache_identity = identity ? (uintptr_t)text : 0u;
	return cmd;
}

static RgGuiDrawCmd test_rect_command(void)
{
	RgGuiDrawCmd cmd = {0};
	cmd.type = RG_GUI_CMD_RECT;
	cmd.data.rect.rect = rg_gui_make_rect(0.0f, 0.0f, 10.0f, 10.0f);
	cmd.data.rect.color = rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f);
	return cmd;
}

static int test_item(const RgGuiGpuItem* item, u32 type, u32 first, u32 count,
                     int clipped)
{
	return item->type == type && item->first == first && item->count == count &&
	       item->clip_enabled == (u32)clipped &&
	       (!clipped || (item->clip.x == 1.0f && item->clip.y == 2.0f &&
	                     item->clip.w == 30.0f && item->clip.h == 40.0f));
}

static int test_text_command_association(void)
{
	TestGpuFixture fixture;
	TEST_CHECK(test_fixture_init(&fixture, test_limits()));
	RgGuiDrawCmd cmds[16] = {0};
	cmds[0] = test_text_command(NULL, 1.0f, 0);
	cmds[1] = test_text_command("", 1.0f, 1);
	cmds[2] = test_text_command("\r\nX", 1.0f, 0); /* No visible glyphs. */
	cmds[3] = test_text_command("A", 0.0f, 1);
	cmds[4] = test_rect_command();
	cmds[5] = test_text_command("A", 1.0f, 1);
	cmds[6] = test_text_command("AAA", 1.0f, 0);
	cmds[7].type = RG_GUI_CMD_CLIP_PUSH;
	cmds[7].data.clip.rect = rg_gui_make_rect(1.0f, 2.0f, 30.0f, 40.0f);
	cmds[8] = test_text_command("AA", 1.0f, 1);
	cmds[9] = test_rect_command();
	cmds[10] = test_text_command("AAAAA", 1.0f, 0);
	cmds[11] = test_text_command("A", 1.0f, 0);
	cmds[12] = test_text_command("A", 1.0f, 1); /* Overlay resets clip. */
	cmds[13].type = RG_GUI_CMD_CLIP_POP;
	cmds[14] = test_text_command("AA", -1.0f, 0);
	cmds[15] = test_rect_command();
	RgGuiDrawList list = {cmds, RG_ARRAY_COUNT(cmds), RG_ARRAY_COUNT(cmds)};
	/* All text uses the same position/color; only its source command can
	   distinguish skipped text and split pages around intervening geometry. */
	for (u32 frame = 0u; frame < 3u; frame++)
	{
		if (frame == 2u) rg_gui_renderer_clear_cache(&fixture.text);
		rg_gui_renderer_begin_frame(&fixture.text);
		TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, 12u));
		TEST_CHECK(fixture.gpu.item_count == 7u && fixture.gpu.vertex_count == 18u);
		TEST_CHECK(fixture.gpu.text_prepared->glyph_count == 15u);
		TEST_CHECK(fixture.gpu.text_prepared->run_count == 10u);
		TEST_CHECK(test_item(&fixture.items[0], RG_GUI_GPU_ITEM_SOLID, 0u, 6u, 0));
		TEST_CHECK(test_item(&fixture.items[1], RG_GUI_GPU_ITEM_TEXT, 0u, 4u, 0));
		TEST_CHECK(test_item(&fixture.items[2], RG_GUI_GPU_ITEM_TEXT, 4u, 2u, 1));
		TEST_CHECK(test_item(&fixture.items[3], RG_GUI_GPU_ITEM_SOLID, 6u, 6u, 1));
		TEST_CHECK(test_item(&fixture.items[4], RG_GUI_GPU_ITEM_TEXT, 6u, 6u, 1));
		TEST_CHECK(test_item(&fixture.items[5], RG_GUI_GPU_ITEM_TEXT, 12u, 3u, 0));
		TEST_CHECK(test_item(&fixture.items[6], RG_GUI_GPU_ITEM_SOLID, 12u, 6u, 0));
		TEST_CHECK(frame != 1u || (fixture.text.dirty_page_count == 0u &&
		                          fixture.text.core.stats.frame_cache_hits > 0u));
	}
	free(fixture.memory);
	return 1;
}

static int test_dropped_text_command_association(void)
{
	/* Exercise cache bytes, cached-run count, frame glyph count, and batch
	   count independently. A later fitting/cached command must still draw. */
	for (u32 scenario = 0u; scenario < 4u; scenario++)
	{
		TestGpuFixture fixture;
		RgGuiRendererLimits limits = test_limits();
		RgGuiDrawCmd cmds[5] = {0};
		u32 count = 3u;
		cmds[0] = test_text_command("\r\r\rA", 1.0f, 0);
		cmds[1] = test_rect_command();
		cmds[2] = test_text_command("A", 1.0f, 1);
		if (scenario == 0u) limits.text_capacity = 3u;
		if (scenario == 1u)
		{
			limits.max_cached_runs = 1u;
			cmds[0] = test_text_command("A", 1.0f, 1);
			cmds[1] = test_text_command("?", 1.0f, 0);
			cmds[2] = test_rect_command();
			cmds[3] = cmds[0];
			count = 4u;
		}
		if (scenario == 2u)
		{
			limits.max_frame_instances = 1u;
			cmds[0] = test_text_command("AA", 1.0f, 1);
		}
		if (scenario == 3u)
		{
			limits.max_batches = 1u;
			cmds[0] = test_text_command("A", 1.0f, 1);
			cmds[1].type = RG_GUI_CMD_CLIP_PUSH;
			cmds[1].data.clip.rect = rg_gui_make_rect(1.0f, 2.0f, 30.0f, 40.0f);
			cmds[2] = test_text_command("A", 1.0f, 0);
			cmds[3].type = RG_GUI_CMD_CLIP_POP;
			cmds[4] = cmds[0];
			count = 5u;
		}
		TEST_CHECK(test_fixture_init(&fixture, limits));
		RgGuiDrawList list = {cmds, count, RG_ARRAY_COUNT(cmds)};
		for (u32 frame = 0u; frame < 2u; frame++)
		{
			rg_gui_renderer_begin_frame(&fixture.text);
			TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, count));
			if (fixture.text.core.stats.frame_dropped_runs != 1u)
				fprintf(stderr, "drop scenario %u frame %u: dropped=%u items=%u glyphs=%u\n",
				        scenario, frame, fixture.text.core.stats.frame_dropped_runs,
				        fixture.gpu.item_count, fixture.gpu.text_prepared->glyph_count);
			TEST_CHECK(fixture.text.core.stats.frame_dropped_runs == 1u);
			if (scenario == 3u)
			{
				TEST_CHECK(fixture.gpu.item_count == 1u);
				TEST_CHECK(test_item(&fixture.items[0], RG_GUI_GPU_ITEM_TEXT, 0u, 2u, 0));
			}
			else
			{
				u32 offset = scenario == 1u ? 1u : 0u;
				TEST_CHECK(fixture.gpu.item_count == 2u + offset);
				if (offset) TEST_CHECK(test_item(&fixture.items[0], RG_GUI_GPU_ITEM_TEXT, 0u, 1u, 0));
				TEST_CHECK(test_item(&fixture.items[offset], RG_GUI_GPU_ITEM_SOLID, 0u, 6u, 0));
				TEST_CHECK(test_item(&fixture.items[offset + 1u], RG_GUI_GPU_ITEM_TEXT, offset, 1u, 0));
			}
		}
		free(fixture.memory);
	}
	return 1;
}

static int test_gpu_capacity_recovery(void)
{
	TestGpuFixture fixture;
	TEST_CHECK(test_fixture_init(&fixture, test_limits()));
	RgGuiDrawCmd cmds[] = {test_text_command("AAA", 1.0f, 1), test_rect_command()};
	RgGuiDrawList list = {cmds, RG_ARRAY_COUNT(cmds), RG_ARRAY_COUNT(cmds)};
	for (u32 scenario = 0u; scenario < 3u; scenario++)
	{
		fixture.gpu.item_capacity = scenario == 0u ? 1u : RG_ARRAY_COUNT(fixture.items);
		fixture.gpu.vertex_capacity = scenario == 1u ? 5u : RG_ARRAY_COUNT(fixture.vertices);
		rg_gui_renderer_begin_frame(&fixture.text);
		int prepared = rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, list.count);
		if (scenario < 2u)
		{
			TEST_CHECK(!prepared && !fixture.gpu.item_count && !fixture.gpu.vertex_count &&
			           !fixture.gpu.text_prepared);
		}
		else
		{
			TEST_CHECK(prepared && fixture.gpu.item_count == 2u && fixture.gpu.vertex_count == 6u);
			TEST_CHECK(test_item(&fixture.items[0], RG_GUI_GPU_ITEM_TEXT, 0u, 3u, 0));
			TEST_CHECK(test_item(&fixture.items[1], RG_GUI_GPU_ITEM_SOLID, 0u, 6u, 0));
		}
	}
	free(fixture.memory);
	return 1;
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
	rg_gui_push_image_material(&gui, rg_gui_make_rect(18.0f, 0.0f, 8.0f, 8.0f),
	                           rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                           (RgGuiTexture)(uintptr_t)1u, 11u);
	rg_gui_image_ex_material(&gui, (RgGuiTexture)(uintptr_t)1u,
	                         rg_gui_make_rect(26.0f, 0.0f, 8.0f, 8.0f),
	                         rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white, 11u);
	rg_gui_push_image_material(&gui, rg_gui_make_rect(34.0f, 0.0f, 8.0f, 8.0f),
	                           rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                           (RgGuiTexture)(uintptr_t)1u, 22u);
	rg_gui_push_image_material(&gui, rg_gui_make_rect(42.0f, 0.0f, 8.0f, 8.0f),
	                           rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f), white,
	                           (RgGuiTexture)(uintptr_t)2u, 11u);
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

	RgGuiGpuVertex vertices[64];
	RgGuiGpuItem items[16];
	RgGuiGpuRenderer gpu = {0};
	gpu.vertices = vertices;
	gpu.vertex_capacity = RG_ARRAY_COUNT(vertices);
	gpu.items = items;
	gpu.item_capacity = RG_ARRAY_COUNT(items);

	rg_gui_renderer_begin_frame(&text_renderer);
	const RgGuiDrawList* list = rg_gui_draw_list(&gui);
	if (!rg_gui_gpu_prepare(&gpu, &text_renderer, list, list->count) ||
	    gpu.vertex_count != 39u || gpu.item_count != 7u ||
	    gpu.items[0].type != RG_GUI_GPU_ITEM_SOLID ||
	    gpu.items[1].type != RG_GUI_GPU_ITEM_SOLID ||
	    !gpu.items[1].clip_enabled ||
	    gpu.items[2].type != RG_GUI_GPU_ITEM_IMAGE ||
	    gpu.items[2].material != 0u ||
	    gpu.items[3].type != RG_GUI_GPU_ITEM_IMAGE ||
	    gpu.items[3].material != 11u || gpu.items[3].count != 12u ||
	    gpu.items[4].material != 22u || gpu.items[4].count != 6u ||
	    gpu.items[5].material != 11u || gpu.items[5].texture != 2u ||
	    gpu.items[6].type != RG_GUI_GPU_ITEM_TEXT ||
	    gpu.items[6].count != 2u)
	{
		fprintf(stderr, "ordered draw-list preparation failed (vertices=%u items=%u",
		        gpu.vertex_count, gpu.item_count);
		for (u32 i = 0u; i < gpu.item_count; i++)
			fprintf(stderr, " item[%u]={type=%u,material=%llu,count=%u}", i,
			        gpu.items[i].type,
			        (unsigned long long)gpu.items[i].material, gpu.items[i].count);
		fprintf(stderr, ")\n");
		return 1;
	}

	free(renderer_memory);
	free(gui_memory);
	if (!test_text_command_association() || !test_dropped_text_command_association() ||
	    !test_gpu_capacity_recovery() || !test_rect_vertex_packing()) return 1;
	puts("rg_gui_gpu draw-list preparation checks passed");
	return 0;
}
