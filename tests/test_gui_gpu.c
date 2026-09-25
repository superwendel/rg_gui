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

static int test_index_width_boundaries(void)
{
	const u32 capacities[] = {1u, 6144u, 16384u, 16385u};
	const u32 corners[] = {0u, 1u, 2u, 0u, 2u, 3u};
	for (u32 n = 0u; n < RG_ARRAY_COUNT(capacities); n++)
	{
		u32 capacity = capacities[n];
		u32 element_size = capacity <= 16384u ? 2u : 4u;
		size_t bytes = (size_t)capacity * 6u * element_size;
		TEST_CHECK(rg_gui_gpu_index_element_size(capacity) == element_size);
		u8* indices = (u8*)malloc(bytes + 16u);
		TEST_CHECK(indices != NULL);
		memset(indices, 0xcc, bytes + 16u);
		rg_gui_gpu_fill_indices(indices, capacity);
		for (u32 i = 0u; i < capacity; i++)
		{
			for (u32 j = 0u; j < 6u; j++)
			{
				u32 value = element_size == 2u ? ((u16*)indices)[i * 6u + j] : ((u32*)indices)[i * 6u + j];
				TEST_CHECK(value == i * 4u + corners[j]);
			}
		}
		for (u32 i = 0u; i < 16u; i++) TEST_CHECK(indices[bytes + i] == 0xccu);
		RgGuiGpuRenderer gpu = {0};
		gpu.ref_capacity = capacity;
		gpu.index_buffer = (SDL_GPUBuffer*)(uintptr_t)1u;
		TEST_CHECK(rg_gui_gpu_buffer_memory_reserved(&gpu) == (u64)bytes);
		free(indices);
	}
	return 1;
}

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
	fixture->gpu.text.run_capacity = 64u;
	fixture->gpu.text.instance_capacity = 64u;
	fixture->gpu.text.cached_quad_capacity = 64u;
	fixture->gpu.ref_capacity = 64u + fixture->gpu.vertex_capacity / 3u;
	fixture->gpu.frame_capacity = (u32)RG_ALIGN_UP(
	    64u * sizeof(RgGuiRendererRun) + fixture->gpu.vertex_capacity * sizeof(RgGuiGpuVertex), 8u) +
	    fixture->gpu.ref_capacity * 8u;
	fixture->gpu.frame_buffer_count = 1u;
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

static int test_upload_packets_and_revisions(void)
{
	TestGpuFixture fixture;
	TEST_CHECK(test_fixture_init(&fixture, test_limits()));
	RgGuiGpuRunRenderer gpu = {0};
	gpu.run_capacity = 64u;
	gpu.instance_capacity = 64u;
	gpu.cached_quad_capacity = 64u;
	u8 memory[16384];
	RgGpuUploadRing ring = {0};
	ring.mapped = memory;
	ring.size = sizeof(memory);
	RgGuiDrawCmd commands[3] = {
		test_text_command("A", 1.0f, 0), test_text_command("AA", 1.0f, 0),
		test_text_command("AAA", 1.0f, 0)};
	RgGuiDrawList list = {0};
	list.cmds = commands;
	list.count = 3u;
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_renderer_prepare(&fixture.text, &list, list.count));
	RgGuiGpuTextUpload first = {0}, second = {0}, third = {0}, rejected = {0};
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &first));
	TEST_CHECK(first.full_cache_upload && first.cache_range_count == 1u);
	TEST_CHECK(first.cache_ranges[0].first_quad == 0u && first.cache_ranges[0].quad_count == 8u);
	TEST_CHECK(first.cached_quads.size == 8u * sizeof(RgGuiRendererCachedQuad));
	RgGuiRendererCachedQuad zero = {0};
	TEST_CHECK(!memcmp(&fixture.text.core.cache_quads[1], &zero, sizeof(zero)));
	TEST_CHECK(!memcmp(&fixture.text.core.cache_quads[7], &zero, sizeof(zero)));
	TEST_CHECK(!gpu.cache_initialized);
	TEST_CHECK(!rg_gui_gpu_text_upload_ready(&gpu, &first));
	RgGuiRendererRange saved_range = first.cache_ranges[0];
	u8 saved_payload[8u * sizeof(RgGuiRendererCachedQuad)];
	memcpy(saved_payload, memory + first.cached_quads.offset, sizeof(saved_payload));

	/* Mutate every source bookkeeping structure after staging. The old packet's
	 * destination ranges and copied bytes must remain independent of that state. */
	rg_gui_renderer_clear_cache(&fixture.text);
	rg_gui_renderer_begin_frame(&fixture.text);
	list.count = 1u;
	TEST_CHECK(rg_gui_renderer_prepare(&fixture.text, &list, list.count));
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &second));
	TEST_CHECK(!memcmp(first.cache_ranges, &saved_range, sizeof(saved_range)));
	TEST_CHECK(!memcmp(memory + first.cached_quads.offset, saved_payload, sizeof(saved_payload)));
	rg_gui_gpu_text_upload_abort(&gpu, &first);
	/* CPU-only tests emulate completion of encoding; the device test encodes real commands. */
	gpu.upload_packets[second.packet_slot].encoded = 1u;
	TEST_CHECK(!rg_gui_gpu_text_upload_ready(&gpu, &second));
	rg_gui_gpu_text_upload_commit(&gpu, &second);
	TEST_CHECK(!gpu.cache_initialized); /* stale pre-abort epoch cannot certify the mirror */
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &first));
	TEST_CHECK(first.full_cache_upload && first.cache_range_count == 1u);
	gpu.upload_packets[first.packet_slot].encoded = 1u;
	TEST_CHECK(rg_gui_gpu_text_upload_ready(&gpu, &first));
	rg_gui_gpu_text_upload_commit(&gpu, &first);
	TEST_CHECK(gpu.cache_initialized && gpu.cache_revision == fixture.text.cache_revision);

	/* Changes survive a skipped/cancelled frame even though frame diagnostics reset. */
	rg_gui_renderer_begin_frame(&fixture.text);
	list.cmds = &commands[1];
	TEST_CHECK(rg_gui_renderer_prepare(&fixture.text, &list, 1u));
	u64 changed_revision = fixture.text.cache_revision;
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(fixture.text.dirty_page_count == 0u);
	TEST_CHECK(rg_gui_renderer_prepare(&fixture.text, &list, 1u));
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &first));
	TEST_CHECK(!first.full_cache_upload && first.cache_range_count == 1u);
	TEST_CHECK(first.cache_revision == changed_revision && first.cache_ranges[0].first_quad == 2u);
	gpu.upload_packets[first.packet_slot].encoded = 1u;
	rg_gui_gpu_text_upload_commit(&gpu, &first);

	/* Three staged packets have independent lifetimes; bounded exhaustion rolls back. */
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &first));
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &second));
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &third));
	u32 offset = ring.offset;
	TEST_CHECK(!rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &rejected));
	TEST_CHECK(ring.offset == offset && !first.cache_range_count && !second.cache_range_count);
	gpu.upload_packets[first.packet_slot].encoded = 1u;
	gpu.upload_packets[second.packet_slot].encoded = 1u;
	gpu.upload_packets[third.packet_slot].encoded = 1u;
	TEST_CHECK(rg_gui_gpu_text_upload_ready(&gpu, &first));
	TEST_CHECK(!rg_gui_gpu_text_upload_ready(&gpu, &second));
	TEST_CHECK(!rg_gui_gpu_text_upload_ready(&gpu, &third));
	rg_gui_gpu_text_upload_commit(&gpu, &first);
	TEST_CHECK(rg_gui_gpu_text_upload_ready(&gpu, &second));
	rg_gui_gpu_text_upload_commit(&gpu, &second);
	TEST_CHECK(rg_gui_gpu_text_upload_ready(&gpu, &third));
	rg_gui_gpu_text_upload_commit(&gpu, &third);
	TEST_CHECK(gpu.cache_initialized);

	/* Out-of-order acknowledgement forces a fresh full upload instead of advancing
	 * over an earlier packet whose submission status is unknown. */
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &first));
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &second));
	gpu.upload_packets[first.packet_slot].encoded = 1u;
	gpu.upload_packets[second.packet_slot].encoded = 1u;
	TEST_CHECK(!rg_gui_gpu_text_upload_ready(&gpu, &second));
	rg_gui_gpu_text_upload_commit(&gpu, &second);
	TEST_CHECK(!rg_gui_gpu_text_upload_ready(&gpu, &first));
	rg_gui_gpu_text_upload_commit(&gpu, &first);
	TEST_CHECK(!gpu.cache_initialized);
	u64 revision = gpu.cache_revision;
	ring.offset = 0u;
	ring.size = 32u;
	TEST_CHECK(!rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &rejected));
	TEST_CHECK(ring.offset == 0u && gpu.cache_revision == revision && !rejected.packet_token);
	ring.size = sizeof(memory);
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &first));
	TEST_CHECK(first.full_cache_upload && first.cache_ranges[0].quad_count == 4u);
	rg_gui_gpu_text_upload_commit(&gpu, &first); /* never encoded: must not initialize */
	TEST_CHECK(!gpu.cache_initialized);
	/* A partial packet cannot follow a committed switch to another text cache. */
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &first));
	gpu.upload_packets[first.packet_slot].encoded = 1u;
	rg_gui_gpu_text_upload_commit(&gpu, &first);
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_text_stage_upload(&gpu, &fixture.text,
	    rg_gui_renderer_prepared(&fixture.text), &ring, &first));
	TEST_CHECK(!first.full_cache_upload);
	gpu.upload_packets[first.packet_slot].encoded = 1u;
	gpu.cache_context = NULL;
	TEST_CHECK(!rg_gui_gpu_text_upload_ready(&gpu, &first));
	rg_gui_gpu_text_upload_abort(&gpu, &first);
	TEST_CHECK(rg_gui_gpu_text_upload_memory_reserved(&gpu) <=
	           (sizeof(RgGuiRendererRange) * fixture.text.total_pages +
	            sizeof(RgGuiRendererBatch) * gpu.run_capacity) * RG_GUI_GPU_UPLOAD_PACKET_COUNT);
	rg_gui_gpu_run_destroy(&gpu);
	free(fixture.memory);
	return 1;
}

static int test_full_upload_packet_lifetime(void)
{
	TestGpuFixture fixture;
	TEST_CHECK(test_fixture_init(&fixture, test_limits()));
	fixture.gpu.text.run_capacity = 64u;
	fixture.gpu.text.instance_capacity = 64u;
	fixture.gpu.text.cached_quad_capacity = 64u;
	u8 memory[16384];
	RgGpuUploadRing ring = {0};
	ring.mapped = memory; ring.size = sizeof(memory);
	RgGuiDrawCmd commands[4] = {0};
	commands[0].type = RG_GUI_CMD_CLIP_PUSH;
	commands[0].data.clip.rect = rg_gui_make_rect(1, 2, 30, 40);
	commands[1] = test_rect_command();
	commands[2] = test_text_command("AAAAA", 1, 0);
	commands[3].type = RG_GUI_CMD_CLIP_POP;
	RgGuiDrawList list = {0}; list.cmds = commands; list.count = 4u;
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, list.count));
	RgGuiGpuUpload first = {0}, second = {0}, third = {0}, rejected = {0};
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &first));
	TEST_CHECK(first.has_geometry && first.has_text && first.item_count == 2u && first.vertex_count == 6u);
	TEST_CHECK(first.text.glyph_count == 5u && first.text.run_count == 3u);
	TEST_CHECK(!first.text.batch_count && !first.text.glyph_batches);
	TEST_CHECK(first.batch_count == 1u && first.ref_count == 6u &&
	           first.batches[0].first_ref == 0u && first.batches[0].ref_count == 6u &&
	           first.batches[0].glyph_count == 5u && first.batches[0].geometry_vertices == 6u);
	TEST_CHECK(first.batches[0].clip_enabled && first.batches[0].clip.x == 1.0f &&
	           first.batches[0].clip.y == 2.0f && first.batches[0].clip.w == 30.0f &&
	           first.batches[0].clip.h == 40.0f);
	for (u32 glyph = 0u; glyph < 5u; glyph++) {
		u64 reference;
		memcpy(&reference, memory + first.frame.offset + first.refs_offset + (glyph + 1u) * 8u, sizeof(reference));
		TEST_CHECK((u32)reference == glyph && (u32)(reference >> 32u) == glyph / 2u);
	}
	TEST_CHECK(test_item(&first.items[0], RG_GUI_GPU_ITEM_SOLID, 0, 6, 1));
	TEST_CHECK(test_item(&first.items[1], RG_GUI_GPU_ITEM_TEXT, 0, 5, 1));
	RgGuiGpuItem saved_items[2]; memcpy(saved_items, first.items, sizeof(saved_items));
	RgGuiGpuIndexedBatch saved_batch = first.batches[0];
	u8 saved_frame[512];
	TEST_CHECK(first.frame.size <= sizeof(saved_frame));
	memcpy(saved_frame, memory + first.frame.offset, first.frame.size);
	u8 saved_vertices[6u * sizeof(RgGuiGpuVertex)];
	memcpy(saved_vertices, memory + first.geometry.offset, sizeof(saved_vertices));
	commands[0] = test_text_command("A", 2, 0); list.count = 1u;
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, list.count));
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &second));
	TEST_CHECK(second.item_count == 1u && second.text.glyph_count == 1u && second.text.run_count == 1u);
	RgGuiGpuItem saved_second_item = second.items[0];
	RgGuiGpuIndexedBatch saved_second_batch = second.batches[0];
	TEST_CHECK(!memcmp(first.items, saved_items, sizeof(saved_items)));
	TEST_CHECK(!memcmp(first.batches, &saved_batch, sizeof(saved_batch)));
	TEST_CHECK(!memcmp(memory + first.geometry.offset, saved_vertices, sizeof(saved_vertices)));
	commands[0] = test_rect_command();
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, list.count));
	TEST_CHECK(!memcmp(first.items, saved_items, sizeof(saved_items)) &&
	           !memcmp(first.batches, &saved_batch, sizeof(saved_batch)) &&
	           !memcmp(memory + first.frame.offset, saved_frame, first.frame.size));
	TEST_CHECK(!memcmp(second.items, &saved_second_item, sizeof(saved_second_item)) &&
	           !memcmp(second.batches, &saved_second_batch, sizeof(saved_second_batch)));
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &third));
	TEST_CHECK(third.has_geometry && !third.has_text && third.item_count == 1u);
	u32 offset = ring.offset;
	TEST_CHECK(!rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &rejected));
	TEST_CHECK(!rejected.packet_token && ring.offset == offset);
	TEST_CHECK(!rg_gui_gpu_upload_ready(&fixture.gpu, &first));
	fixture.gpu.upload_packets[first.packet_slot].encoded = 1u;
	fixture.gpu.text.upload_packets[first.text.packet_slot].encoded = 1u;
	fixture.gpu.upload_packets[second.packet_slot].encoded = 1u;
	fixture.gpu.text.upload_packets[second.text.packet_slot].encoded = 1u;
	fixture.gpu.upload_packets[third.packet_slot].encoded = 1u;
	TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &first));
	TEST_CHECK(!rg_gui_gpu_upload_ready(&fixture.gpu, &second));
	TEST_CHECK(!rg_gui_gpu_upload_ready(&fixture.gpu, &third));
	rg_gui_gpu_upload_commit(&fixture.gpu, &first);
	TEST_CHECK(!rg_gui_gpu_upload_active(&fixture.gpu, &first));
	TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &second));
	rg_gui_gpu_upload_commit(&fixture.gpu, &second);
	TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &third));
	rg_gui_gpu_upload_abort(&fixture.gpu, &third);
	TEST_CHECK(!rg_gui_gpu_upload_ready(&fixture.gpu, &third));
	/* A reused slot has a new generation; a stale handle must not abort it. */
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &rejected));
	rg_gui_gpu_upload_abort(&fixture.gpu, &first);
	TEST_CHECK(rg_gui_gpu_upload_active(&fixture.gpu, &rejected));
	rg_gui_gpu_upload_abort(&fixture.gpu, &rejected);
	TEST_CHECK(!rg_gui_gpu_dispatch_upload(&fixture.gpu, (SDL_GPUCommandBuffer*)(uintptr_t)1u, &first));
	TEST_CHECK(rg_gui_gpu_upload_memory_reserved(&fixture.gpu) > rg_gui_gpu_text_upload_memory_reserved(&fixture.gpu.text));
	TEST_CHECK(rg_gui_gpu_upload_memory_reserved(&fixture.gpu) <=
	    ((sizeof(RgGuiGpuItem) + sizeof(RgGuiGpuIndexedBatch)) * fixture.gpu.item_capacity +
	     sizeof(RgGuiRendererRange) * fixture.text.total_pages +
	     sizeof(RgGuiRendererBatch) * fixture.gpu.text.run_capacity) * RG_GUI_GPU_UPLOAD_PACKET_COUNT);
	/* The fixture borrows these two arrays; the renderer owns only packet arrays. */
	fixture.gpu.items = NULL; fixture.gpu.vertices = NULL;
	rg_gui_gpu_destroy(&fixture.gpu);
	free(fixture.memory);
	return 1;
}

static u64 test_packed_reference(const u8* memory, const RgGuiGpuUpload* upload, u32 index)
{
	u64 value;
	memcpy(&value, memory + upload->frame.offset + upload->refs_offset + index * 8u, sizeof(value));
	return value;
}

static int test_indexed_triangle_packing_and_empty(void)
{
	TestGpuFixture fixture;
	TEST_CHECK(test_fixture_init(&fixture, test_limits()));
	RgGuiDrawCmd commands[5] = {0};
	commands[0] = test_rect_command();
	for (u32 i = 1u; i < 5u; i++) {
		f32 x = i <= 2u ? 20.0f : 40.0f;
		commands[i].type = RG_GUI_CMD_TRIANGLE;
		commands[i].data.triangle.a = rg_vec2(x, 0);
		commands[i].data.triangle.b = (i & 1u) ? rg_vec2(x + 10, 0) : rg_vec2(x + 10, 10);
		commands[i].data.triangle.c = (i & 1u) ? rg_vec2(x + 10, 10) : rg_vec2(x, 10);
		commands[i].data.triangle.color = i == 4u ? rg_gui_color(1, 0, 0, 1) : rg_gui_color(1, 1, 1, 1);
	}
	RgGuiDrawList list = {commands, 5u, 5u};
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, list.count));
	TEST_CHECK(fixture.gpu.vertex_count == 18u && fixture.gpu.item_count == 1u);
	u8 memory[4096]; memset(memory, 0xA5, sizeof(memory));
	RgGpuUploadRing ring = {0}; ring.mapped = memory; ring.size = sizeof(memory);
	RgGuiGpuUpload first = {0}, second = {0};
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &first));
	TEST_CHECK(first.ref_count == 4u && first.batch_count == 1u && !first.has_text);
	const u64 solid = (u64)1u << 62u;
	const u64 quad = (u64)1u << 32u;
	TEST_CHECK(test_packed_reference(memory, &first, 0u) == (solid | quad));
	TEST_CHECK(test_packed_reference(memory, &first, 1u) == (solid | quad | 6u));
	TEST_CHECK(test_packed_reference(memory, &first, 2u) == (solid | 12u));
	TEST_CHECK(test_packed_reference(memory, &first, 3u) == (solid | 15u));
	TEST_CHECK(first.batches[0].geometry_vertices == 18u && first.batches[0].source_items == 1u);
	u8 saved_frame[512];
	TEST_CHECK(first.frame.size <= sizeof(saved_frame));
	memcpy(saved_frame, memory + first.frame.offset, first.frame.size);
	// XY equality alone is insufficient: changing a shared corner's UV prevents
	// reduction, just as the color mismatch prevented reduction above.
	fixture.vertices[9].u = 0.25f;
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &second));
	TEST_CHECK(second.ref_count == 5u &&
	           test_packed_reference(memory, &second, 1u) == (solid | 6u) &&
	           test_packed_reference(memory, &second, 2u) == (solid | 9u));
	TEST_CHECK(!memcmp(saved_frame, memory + first.frame.offset, first.frame.size));
	rg_gui_gpu_upload_abort(&fixture.gpu, &first);
	rg_gui_gpu_upload_abort(&fixture.gpu, &second);

	list.cmds = &commands[1]; list.count = 1u;
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, 1u));
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &first));
	TEST_CHECK(first.geometry.size == 60u && first.refs_offset == 64u && first.ref_count == 1u);
	for (u32 i = 60u; i < 64u; i++) TEST_CHECK(memory[first.frame.offset + i] == 0u);
	rg_gui_gpu_upload_abort(&fixture.gpu, &first);

	// Empty packets still take a bounded CPU slot and preserve submission order,
	// but require no transfer allocation, copy pass, or compute dispatch.
	list.count = 0u;
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, 0u));
	u32 offset = ring.offset;
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &first));
	TEST_CHECK(!first.frame.size && !first.ref_count && !first.batch_count && ring.offset == offset);
	TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &first));
	TEST_CHECK(rg_gui_gpu_dispatch_upload(&fixture.gpu, (SDL_GPUCommandBuffer*)(uintptr_t)1u, &first));
	rg_gui_gpu_upload_commit(&fixture.gpu, &first);
	TEST_CHECK(!rg_gui_gpu_upload_active(&fixture.gpu, &first));
	fixture.gpu.items = NULL; fixture.gpu.vertices = NULL;
	rg_gui_gpu_destroy(&fixture.gpu);
	free(fixture.memory);
	return 1;
}

static int test_indexed_capacity_rollback(void)
{
	TestGpuFixture fixture;
	TEST_CHECK(test_fixture_init(&fixture, test_limits()));
	fixture.gpu.frame_buffer_count = 3u;
	RgGuiDrawCmd commands[2] = {test_text_command("AAAA", 1, 0), test_rect_command()};
	RgGuiDrawList list = {commands, 2u, 2u};
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, 2u));
	u8 memory[4096];
	RgGpuUploadRing ring = {0}; ring.mapped = memory; ring.size = sizeof(memory);
	RgGuiGpuUpload first = {0}, rejected = {0};
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &first));
	fixture.gpu.upload_packets[first.packet_slot].encoded = 1u;
	fixture.gpu.text.upload_packets[first.text.packet_slot].encoded = 1u;
	TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &first));
	TEST_CHECK(first.ref_count == 5u && first.batch_count == 1u &&
	           first.batches[0].source_items == 2u);
	u8 saved_frame[512];
	TEST_CHECK(first.frame.size <= sizeof(saved_frame));
	memcpy(saved_frame, memory + first.frame.offset, first.frame.size);
	u32 original_offset = ring.offset;
	u32 original_rotation = fixture.gpu.next_frame_buffer;
	u64 original_token = fixture.gpu.next_upload_token;
	u64 original_text_token = fixture.gpu.text.next_upload_token;
	u64 original_epoch = fixture.gpu.text.cache_tracking_epoch;
	u32 refs = fixture.gpu.ref_capacity, frame_bytes = fixture.gpu.frame_capacity;
	for (u32 scenario = 0u; scenario < 9u; scenario++) {
		fixture.gpu.ref_capacity = scenario == 0u ? 4u : refs;
		fixture.gpu.frame_capacity = scenario == 1u ? 1u : frame_bytes;
		ring.size = scenario == 2u ? original_offset + 1u : sizeof(memory);
		fixture.gpu.text.run_capacity = scenario == 3u ? 1u : 64u;
		fixture.gpu.text.instance_capacity = scenario == 4u ? 3u : 64u;
		fixture.gpu.text.cached_quad_capacity = scenario == 5u ? 1u : 64u;
		fixture.gpu.vertex_capacity = scenario == 6u ? 5u : RG_ARRAY_COUNT(fixture.vertices);
		fixture.gpu.item_capacity = scenario == 7u ? 1u : RG_ARRAY_COUNT(fixture.items);
		fixture.items[1].count = scenario == 8u ? 5u : 6u;
		TEST_CHECK(!rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &rejected));
		TEST_CHECK(!rejected.packet_token && !rejected.text.packet_token && ring.offset == original_offset &&
		           fixture.gpu.next_frame_buffer == original_rotation &&
		           fixture.gpu.next_upload_token == original_token &&
		           fixture.gpu.text.next_upload_token == original_text_token &&
		           fixture.gpu.text.cache_tracking_epoch == original_epoch);
		TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &first));
		TEST_CHECK(!memcmp(saved_frame, memory + first.frame.offset, first.frame.size));
		for (u32 i = 0u; i < RG_GUI_GPU_UPLOAD_PACKET_COUNT; i++) {
			TEST_CHECK(fixture.gpu.upload_packets[i].active == (u32)(i == first.packet_slot));
			TEST_CHECK(fixture.gpu.text.upload_packets[i].active == (u32)(i == first.text.packet_slot));
		}
	}
	fixture.items[1].count = 6u;
	// A later packet is blocked until the first completes. Cancelling the first
	// invalidates the later packet's cache epoch, requiring both to be restaged.
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &rejected));
	fixture.gpu.upload_packets[rejected.packet_slot].encoded = 1u;
	fixture.gpu.text.upload_packets[rejected.text.packet_slot].encoded = 1u;
	TEST_CHECK(!rg_gui_gpu_upload_ready(&fixture.gpu, &rejected));
	rg_gui_gpu_upload_abort(&fixture.gpu, &first);
	TEST_CHECK(!rg_gui_gpu_upload_ready(&fixture.gpu, &rejected));
	rg_gui_gpu_upload_abort(&fixture.gpu, &rejected);
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &first));
	TEST_CHECK(first.text.full_cache_upload && first.frame_buffer_index < 3u);
	rg_gui_gpu_upload_abort(&fixture.gpu, &first);
	fixture.gpu.items = NULL; fixture.gpu.vertices = NULL;
	rg_gui_gpu_destroy(&fixture.gpu);
	free(fixture.memory);
	return 1;
}

static int test_gpu_cache_invalidation(void)
{
	TestGpuFixture fixture;
	RgGuiRendererLimits limits = test_limits();
	TEST_CHECK(test_fixture_init(&fixture, limits));
	fixture.gpu.text.run_capacity = 64u;
	fixture.gpu.text.instance_capacity = 64u;
	fixture.gpu.text.cached_quad_capacity = 64u;
	u8 memory[4096];
	RgGpuUploadRing ring = {0};
	ring.mapped = memory; ring.size = sizeof(memory);
	RgGuiDrawCmd command = test_text_command("A", 1, 0);
	RgGuiDrawList list = {0}; list.cmds = &command; list.count = 1u;
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, 1u));
	RgGuiGpuUpload first = {0}, pending = {0}, replacement = {0};
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &first));
	fixture.gpu.upload_packets[first.packet_slot].encoded = 1u;
	fixture.gpu.text.upload_packets[first.text.packet_slot].encoded = 1u;
	TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &first));
	rg_gui_gpu_upload_commit(&fixture.gpu, &first);
	u64 original_revision = fixture.gpu.text.cache_revision;
	TEST_CHECK(fixture.gpu.text.cache_initialized);
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &pending));
	TEST_CHECK(!pending.text.full_cache_upload && !pending.text.cached_quads.size);
	fixture.gpu.upload_packets[pending.packet_slot].encoded = 1u;
	fixture.gpu.text.upload_packets[pending.text.packet_slot].encoded = 1u;
	TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &pending));
	size_t reserved = rg_gui_gpu_upload_memory_reserved(&fixture.gpu);
	rg_gui_gpu_invalidate_cache(&fixture.gpu);
	TEST_CHECK(!fixture.gpu.text.cache_initialized && !fixture.gpu.text.cache_context);
	TEST_CHECK(rg_gui_gpu_upload_active(&fixture.gpu, &pending));
	TEST_CHECK(!rg_gui_gpu_upload_ready(&fixture.gpu, &pending));
	TEST_CHECK(rg_gui_gpu_upload_memory_reserved(&fixture.gpu) == reserved);
	rg_gui_gpu_upload_abort(&fixture.gpu, &pending);

	/* Reinitialize the same CPU context and arena with a changed atlas position.
	 * Its address and final revision match the previous cache, so only explicit
	 * invalidation distinguishes the new lifetime from the submitted GPU mirror. */
	fixture.glyphs[1].x = 18;
	size_t bytes = rg_gui_renderer_memory_required(&limits, 2u);
	RgArena arena = {(char*)fixture.memory, bytes, 0u, bytes};
	RgGuiRendererInitDesc desc = {0};
	desc.font = &fixture.font; desc.limits = limits; desc.page_quads = 2u;
	TEST_CHECK(rg_gui_renderer_init(&fixture.text, &arena, &desc));
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, 1u));
	TEST_CHECK(fixture.text.cache_revision == original_revision);
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &replacement));
	TEST_CHECK(replacement.text.full_cache_upload && replacement.text.cache_range_count == 1u);
	TEST_CHECK(replacement.text.cached_quads.size == 2u * sizeof(RgGuiRendererCachedQuad));
	RgGuiRendererCachedQuad first_quad;
	memcpy(&first_quad, memory + replacement.text.cached_quads.offset, sizeof(first_quad));
	TEST_CHECK(first_quad.u0 == 18u && first_quad.u1 == 25u);
	fixture.gpu.upload_packets[replacement.packet_slot].encoded = 1u;
	fixture.gpu.text.upload_packets[replacement.text.packet_slot].encoded = 1u;
	TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &replacement));
	rg_gui_gpu_upload_commit(&fixture.gpu, &replacement);
	TEST_CHECK(fixture.gpu.text.cache_initialized);

	/* The low-level text-only reset has the same lifecycle and remains safe
	 * without an active packet; no renderer destruction or allocation is needed. */
	rg_gui_gpu_text_invalidate_cache(&fixture.gpu.text);
	ring.offset = 0u;
	TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &replacement));
	TEST_CHECK(replacement.text.full_cache_upload && replacement.text.cached_quads.size != 0u);
	rg_gui_gpu_upload_abort(&fixture.gpu, &replacement);
	rg_gui_gpu_invalidate_cache(NULL);
	rg_gui_gpu_text_invalidate_cache(NULL);
	fixture.gpu.items = NULL; fixture.gpu.vertices = NULL;
	rg_gui_gpu_destroy(&fixture.gpu);
	TEST_CHECK(rg_gui_gpu_upload_memory_reserved(&fixture.gpu) == 0u);
	free(fixture.memory);
	return 1;
}

static int test_independent_frame_capacity(void)
{
	RgGuiRendererLimits limits = test_limits();
	size_t old_bytes = rg_gui_renderer_memory_required(&limits, 2u);
	limits.max_frame_runs = 2u;
	size_t new_bytes = rg_gui_renderer_memory_required(&limits, 2u);
	TEST_CHECK(old_bytes - new_bytes == 62u * sizeof(RgGuiRendererRun));
	TestGpuFixture fixture;
	TEST_CHECK(test_fixture_init(&fixture, limits));
	RgGuiDrawCmd command = test_text_command("AAA", 1.0f, 0);
	RgGuiDrawList list = {0}; list.cmds = &command; list.count = 1u;
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_renderer_prepare(&fixture.text, &list, 1u));
	TEST_CHECK(rg_gui_renderer_prepared(&fixture.text)->run_count == 2u);
	TEST_CHECK(rg_gui_renderer_prepared(&fixture.text)->glyph_count == 3u);
	command = test_text_command("AAAAA", 1.0f, 0);
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_renderer_prepare(&fixture.text, &list, 1u));
	TEST_CHECK(!rg_gui_renderer_prepared(&fixture.text)->run_count);
	TEST_CHECK(rg_gui_renderer_stats(&fixture.text)->frame_dropped_runs == 1u);
	free(fixture.memory);
	return 1;
}

static int test_fragmented_upload_ranges(void)
{
	u64 revisions[8] = {1u, 3u, 3u, 0u, 4u, 1u, 4u, 4u};
	RgGuiRenderer context = {0};
	context.initialized = 1;
	context.page_quads = 2u;
	context.next_fresh_page = 8u;
	context.page_revisions = revisions;
	RgGuiRendererRange ranges[3];
	u32 quads = 0u;
	TEST_CHECK(rg_gui_renderer_upload_ranges(&context, 1u, ranges, 3u, &quads) == 3u);
	TEST_CHECK(quads == 10u);
	TEST_CHECK(ranges[0].first_quad == 2u && ranges[0].quad_count == 4u);
	TEST_CHECK(ranges[1].first_quad == 8u && ranges[1].quad_count == 2u);
	TEST_CHECK(ranges[2].first_quad == 12u && ranges[2].quad_count == 4u);
	TEST_CHECK(rg_gui_renderer_upload_ranges(&context, 1u, ranges, 2u, NULL) == UINT32_MAX);
	TEST_CHECK(rg_gui_renderer_upload_ranges(&context, 0u, ranges, 3u, &quads) == 2u);
	TEST_CHECK(quads == 14u && ranges[0].first_quad == 0u && ranges[0].quad_count == 6u);
	TEST_CHECK(ranges[1].first_quad == 8u && ranges[1].quad_count == 8u);
	return 1;
}

static int test_upload_ring_size_boundaries(void)
{
	RgGuiGpuRenderer gpu = {0};
	TEST_CHECK(!rg_gui_gpu_upload_ring_size_required(NULL));
	TEST_CHECK(!rg_gui_gpu_upload_ring_size_required(&gpu));
	gpu.frame_capacity = 1u;
	TEST_CHECK(!rg_gui_gpu_upload_ring_size_required(&gpu));
	gpu.text.cached_quad_capacity = 1u;
	gpu.frame_capacity = 0u;
	TEST_CHECK(!rg_gui_gpu_upload_ring_size_required(&gpu));

	/* Exercise the real allocator at every alignment residue, independently of
	 * the sizing expression. Neither allocation may exceed the returned bound. */
	u8 memory[4096];
	const u32 cached_quads[] = {1u, 3u, 64u};
	for (u32 frame_bytes = 1u; frame_bytes <= 2u * RG_GPU_UPLOAD_RING_DEFAULT_ALIGN + 1u; frame_bytes++)
	for (u32 n = 0u; n < RG_ARRAY_COUNT(cached_quads); n++)
	{
		gpu.frame_capacity = frame_bytes;
		gpu.text.cached_quad_capacity = cached_quads[n];
		u32 required = rg_gui_gpu_upload_ring_size_required(&gpu);
		TEST_CHECK(required && required <= sizeof(memory));
		RgGpuUploadRing ring = {0}; ring.mapped = memory; ring.size = required;
		RgGpuUploadSlice frame = {0}, cache = {0};
		TEST_CHECK(rg_gpu_upload_ring_alloc(&ring, frame_bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN, &frame));
		TEST_CHECK(frame.offset == 0u && frame.size == frame_bytes);
		u32 cache_bytes = cached_quads[n] * (u32)sizeof(RgGuiRendererCachedQuad);
		TEST_CHECK(rg_gpu_upload_ring_alloc(&ring, cache_bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN, &cache));
		TEST_CHECK(cache.offset >= frame.size && cache.offset % RG_GPU_UPLOAD_RING_DEFAULT_ALIGN == 0u);
		TEST_CHECK(ring.offset == required && cache.offset + cache.size == required);
		ring.offset = 0u; ring.size = required - 1u;
		TEST_CHECK(rg_gpu_upload_ring_alloc(&ring, frame_bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN, &frame));
		TEST_CHECK(!rg_gpu_upload_ring_alloc(&ring, cache_bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN, &cache));
		TEST_CHECK(ring.offset == frame_bytes);
	}

	gpu.frame_capacity = UINT32_MAX;
	gpu.text.cached_quad_capacity = 1u;
	TEST_CHECK(!rg_gui_gpu_upload_ring_size_required(&gpu));
	gpu.frame_capacity = 1u;
	gpu.text.cached_quad_capacity = UINT32_MAX;
	TEST_CHECK(!rg_gui_gpu_upload_ring_size_required(&gpu));
	gpu.frame_capacity = 32u;
	gpu.text.cached_quad_capacity = UINT32_MAX / (u32)sizeof(RgGuiRendererCachedQuad);
	TEST_CHECK(!rg_gui_gpu_upload_ring_size_required(&gpu));
	/* Exercise a large valid capacity without allocating it. */
#if RG_GPU_UPLOAD_RING_DEFAULT_ALIGN <= 64u
	gpu.frame_capacity = UINT32_MAX - 63u;
	gpu.text.cached_quad_capacity = 1u;
	TEST_CHECK(rg_gui_gpu_upload_ring_size_required(&gpu) == UINT32_MAX - 39u);
#endif
	return 1;
}

static int test_upload_ring_size_full_cache(void)
{
	TestGpuFixture fixture;
	RgGuiRendererLimits limits = test_limits();
	limits.max_frame_runs = 32u;
	TEST_CHECK(test_fixture_init(&fixture, limits));
	fixture.gpu.text.run_capacity = 32u;
	fixture.gpu.vertex_capacity = 63u;
	fixture.gpu.ref_capacity = 85u;
	fixture.gpu.frame_capacity = (u32)RG_ALIGN_UP(
	    32u * sizeof(RgGuiRendererRun) + 63u * sizeof(RgGuiGpuVertex), 8u) + 85u * 8u;
	char text[65]; memset(text, 'A', 64u); text[64] = '\0';
	RgGuiDrawCmd commands[22] = {0};
	commands[0] = test_text_command(text, 1.0f, 0);
	for (u32 n = 1u; n < RG_ARRAY_COUNT(commands); n++)
	{
		f32 x = (f32)n * 4.0f;
		commands[n].type = RG_GUI_CMD_TRIANGLE;
		commands[n].data.triangle.a = rg_vec2(x, 0.0f);
		commands[n].data.triangle.b = rg_vec2(x + 1.0f, 0.0f);
		commands[n].data.triangle.c = rg_vec2(x, 1.0f);
		commands[n].data.triangle.color = rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f);
	}
	RgGuiDrawList list = {commands, RG_ARRAY_COUNT(commands), RG_ARRAY_COUNT(commands)};
	rg_gui_renderer_begin_frame(&fixture.text);
	TEST_CHECK(rg_gui_gpu_prepare(&fixture.gpu, &fixture.text, &list, list.count));
	TEST_CHECK(fixture.gpu.text_prepared->glyph_count == 64u && fixture.gpu.text_prepared->run_count == 32u);
	TEST_CHECK(fixture.gpu.vertex_count == 63u && fixture.text.next_fresh_page == fixture.text.total_pages);
	u32 required = rg_gui_gpu_upload_ring_size_required(&fixture.gpu);
	TEST_CHECK(required && required < UINT32_MAX - 32u);
	u8* memory = (u8*)malloc((size_t)required + 32u);
	TEST_CHECK(memory != NULL);
	RgGpuUploadRing ring = {0}; ring.mapped = memory;
	for (u32 phase = 0u; phase < 3u; phase++)
	{
		/* Cold cache, explicit invalidation, then retry after an aborted packet. */
		if (phase == 1u) rg_gui_gpu_invalidate_cache(&fixture.gpu);
		memset(memory, 0xcd, (size_t)required + 32u);
		ring.offset = 0u; ring.size = required - 1u;
		RgGuiGpuUpload rejected = {0}, upload = {0};
		u64 token = fixture.gpu.next_upload_token;
		TEST_CHECK(!rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &rejected));
		TEST_CHECK(!rejected.packet_token && ring.offset == 0u && fixture.gpu.next_upload_token == token);
		ring.size = required;
		TEST_CHECK(rg_gui_gpu_stage_upload(&fixture.gpu, &fixture.text, &ring, &upload));
		TEST_CHECK(upload.text.full_cache_upload && upload.text.cache_range_count == 1u);
		TEST_CHECK(upload.text.cache_ranges[0].first_quad == 0u && upload.text.cache_ranges[0].quad_count == 64u);
		TEST_CHECK(upload.text.cached_quads.size == 64u * sizeof(RgGuiRendererCachedQuad));
		TEST_CHECK(upload.frame.size == fixture.gpu.frame_capacity && upload.ref_count == 85u);
		TEST_CHECK(ring.offset == required && upload.text.cached_quads.offset % RG_GPU_UPLOAD_RING_DEFAULT_ALIGN == 0u);
		TEST_CHECK(!memcmp(memory + upload.geometry.offset, fixture.vertices, 63u * sizeof(RgGuiGpuVertex)));
		TEST_CHECK(!memcmp(memory + upload.frame.offset, fixture.gpu.text_prepared->runs, 32u * sizeof(RgGuiRendererRun)));
		TEST_CHECK(!memcmp(memory + upload.text.cached_quads.offset, fixture.text.core.cache_quads,
		                   64u * sizeof(RgGuiRendererCachedQuad)));
		for (u32 n = 0u; n < 32u; n++) TEST_CHECK(memory[required + n] == 0xcdu);
		if (phase == 1u) rg_gui_gpu_upload_abort(&fixture.gpu, &upload);
		else
		{
			/* CPU-only fixture: the device test covers actual GPU copy completion. */
			fixture.gpu.upload_packets[upload.packet_slot].encoded = 1u;
			fixture.gpu.text.upload_packets[upload.text.packet_slot].encoded = 1u;
			TEST_CHECK(rg_gui_gpu_upload_ready(&fixture.gpu, &upload));
			rg_gui_gpu_upload_commit(&fixture.gpu, &upload);
			TEST_CHECK(fixture.gpu.text.cache_initialized);
		}
	}
	fixture.gpu.items = NULL; fixture.gpu.vertices = NULL;
	rg_gui_gpu_destroy(&fixture.gpu);
	free(memory);
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
	if (!test_index_width_boundaries() || !test_text_command_association() || !test_dropped_text_command_association() ||
	    !test_gpu_capacity_recovery() || !test_rect_vertex_packing() ||
	    !test_upload_packets_and_revisions() || !test_full_upload_packet_lifetime() ||
	    !test_indexed_triangle_packing_and_empty() || !test_indexed_capacity_rollback() ||
	    !test_gpu_cache_invalidation() || !test_independent_frame_capacity() ||
	    !test_fragmented_upload_ranges() || !test_upload_ring_size_boundaries() ||
	    !test_upload_ring_size_full_cache()) return 1;
	puts("rg_gui_gpu draw-list preparation checks passed");
	return 0;
}
