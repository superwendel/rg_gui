// Test-only reference renderer correctness suite and shared fixtures.
//
// Build from an x64 Visual Studio Developer Command Prompt with SDL3 linked.

#ifndef RG_GUI_RENDERER_TEST_REFERENCE
#define RG_GUI_RENDERER_TEST_REFERENCE 1
#endif
#define RG_SPRINTF_NO_ASM 1
#include "../src/rg_gui_renderer.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static int g_tests_passed = 0;
static int g_tests_failed = 0;

#define TEST_ASSERT(condition, message)                          \
	do {                                                         \
		if (!(condition))                                        \
		{                                                        \
			printf("  FAIL: %s (line %d)\n", message, __LINE__); \
			g_tests_failed++;                                    \
			return;                                              \
		}                                                        \
	} while (0)

#define TEST_PASS()       \
	do {                  \
		g_tests_passed++; \
	} while (0)

typedef struct TestRendererBaseFixture
{
	RgTextGlyph glyphs[5];
	RgTextKerning kernings[1];
	RgTextFont font;
	void* memory;
	RgArena arena;
	RgGuiRendererBaseContext gui4;
} TestRendererBaseFixture;

static int test_float_equal(f32 a, f32 b)
{
	return fabsf(a - b) <= 0.0001f;
}

static void test_font_init(TestRendererBaseFixture* fixture)
{
	memset(&fixture->font, 0, sizeof(fixture->font));
	fixture->font.metrics.atlas_width = 64u;
	fixture->font.metrics.atlas_height = 32u;
	fixture->font.metrics.line_height = 10;
	fixture->font.metrics.ascent = 8;
	fixture->font.metrics.descent = 2;
	fixture->font.fallback_codepoint = '?';
	fixture->font.glyphs = fixture->glyphs;
	fixture->font.glyph_count = 5u;
	fixture->font.glyph_capacity = 5u;
	fixture->font.kernings = fixture->kernings;
	fixture->font.kerning_count = 1u;
	fixture->font.kerning_capacity = 1u;

	fixture->glyphs[0] = (RgTextGlyph){'?', 15, 0, 4, 7, 0, 1, 4};
	fixture->glyphs[1] = (RgTextGlyph){'A', 0, 0, 5, 7, 0, 1, 6};
	fixture->glyphs[2] = (RgTextGlyph){'B', 5, 0, 6, 7, 0, 1, 7};
	fixture->glyphs[3] = (RgTextGlyph){' ', 20, 0, 0, 0, 0, 0, 3};
	fixture->glyphs[4] = (RgTextGlyph){0xE9u, 11, 0, 4, 7, 0, 1, 5};
	fixture->kernings[0] = (RgTextKerning){'A', 'B', -1};
}

static int test_fixture_init_font(TestRendererBaseFixture* fixture,
                                  const RgGuiRendererBaseLimits* requested,
                                  const RgTextFont* font)
{
	memset(fixture, 0, sizeof(*fixture));
	test_font_init(fixture);
	if (font) fixture->font = *font;
	size_t memory_size = rg_gui_renderer_base_memory_required(requested);
	if (memory_size == SIZE_MAX)
	{
		return 0;
	}
	fixture->memory = malloc(memory_size);
	if (!fixture->memory)
	{
		return 0;
	}
	fixture->arena.memory = (char*)fixture->memory;
	fixture->arena.capacity = memory_size;
	fixture->arena.committed = memory_size;

	RgGuiRendererBaseInitDesc desc;
	memset(&desc, 0, sizeof(desc));
	desc.font = &fixture->font;
	if (requested) desc.limits = *requested;
	return rg_gui_renderer_base_init(&fixture->gui4, &fixture->arena, &desc);
}

static int test_fixture_init(TestRendererBaseFixture* fixture, const RgGuiRendererBaseLimits* requested)
{
	return test_fixture_init_font(fixture, requested, NULL);
}

static void test_fixture_free(TestRendererBaseFixture* fixture)
{
	free(fixture->memory);
	memset(fixture, 0, sizeof(*fixture));
}

static RgGuiDrawCmd test_text_cmd(const char* text, f32 x, f32 y, f32 scale,
                                  f32 r, f32 g, f32 b, f32 a)
{
	RgGuiDrawCmd cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.type = RG_GUI_CMD_TEXT;
	cmd.data.text.text = text;
	cmd.data.text.pos = (rg_vec2){.x = x, .y = y};
	cmd.data.text.scale = scale;
	cmd.data.text.color = (rg_vec4){.x = r, .y = g, .z = b, .w = a};
	return cmd;
}

static RgGuiDrawCmd test_text_cmd_identity(const char* text, f32 x, f32 y,
                                           f32 scale)
{
	RgGuiDrawCmd cmd = test_text_cmd(text, x, y, scale, 1, 1, 1, 1);
	cmd.data.text.cache_identity = (uintptr_t)text;
	return cmd;
}

static RgGuiDrawCmd test_clip_cmd(RgGuiDrawCmdType type, f32 x, f32 y, f32 w, f32 h)
{
	RgGuiDrawCmd cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.type = type;
	cmd.data.clip.rect = (RgGuiRect){x, y, w, h};
	return cmd;
}

static RgGuiDrawList test_draw_list(RgGuiDrawCmd* cmds, u32 count)
{
	RgGuiDrawList list;
	list.cmds = cmds;
	list.count = count;
	list.capacity = count;
	return list;
}

static void test_ascii_lookup_table_equivalence(void)
{
	/* Manual fonts retain their first matching entry, even if its advance is zero. */
	RgTextGlyph glyphs[] = {
	    {'A', 0, 0, 5, 7, 0, 1, 0}, {0xFFFDu, 5, 0, 4, 7, 0, 1, 5},
	    {'?', 9, 0, 4, 7, 0, 1, 4}, {0u, 0, 0, 0, 0, 0, 0, 0},
	    {127u, 13, 0, 4, 7, 0, 1, 4}, {'A', 17, 0, 5, 7, 0, 1, 99},
	    {0xFFFDu, 22, 0, 4, 7, 0, 1, 99}};
	RgTextKerning pairs[] = {
	    {'A', '?', 0}, {127u, 0u, -3}, {'A', '?', 8}, {0u, 'A', 2},
	    {'A', 128u, -9}, {128u, 'A', 9}, {'A', 'A', -1},
	    {'A', 'A', 0}, {127u, 0u, 7}};
	TestRendererBaseFixture font_owner = {0};
	test_font_init(&font_owner);
	RgTextFont* font = &font_owner.font;
	font->glyphs = glyphs;
	font->glyph_count = font->glyph_capacity = (u32)RG_ARRAY_COUNT(glyphs);
	font->kernings = pairs;
	font->kerning_count = font->kerning_capacity = (u32)RG_ARRAY_COUNT(pairs);
	const u32 fallbacks[] = {'?', 0xFFFDu, 0u, UINT32_MAX};
	RgGuiRendererBaseLimits limits = {1u, 2u, 32u, 8u, 8u, 1u, 0u};
	for (u32 variant = 0u; variant < RG_ARRAY_COUNT(fallbacks) + 2u; variant++)
	{
		font->fallback_codepoint = fallbacks[variant % RG_ARRAY_COUNT(fallbacks)];
		if (variant == RG_ARRAY_COUNT(fallbacks)) font->kernings = NULL;
		if (variant == RG_ARRAY_COUNT(fallbacks) + 1u)
		{
			font->kernings = pairs;
			font->kerning_count = 0u;
		}
		TestRendererBaseFixture fixture;
		TEST_ASSERT(test_fixture_init_font(&fixture, &limits, font), "manual font init");
		TEST_ASSERT(fixture.gui4.ascii_glyphs['A'] == &glyphs[0], "first duplicate glyph wins");
		TEST_ASSERT(fixture.gui4.ascii_kerning['A' * 128u + '?'] == 0,
		            "zero first kerning is not overwritten by duplicate");
		for (u32 left = 0u; left < 128u; left++)
		{
			TEST_ASSERT(fixture.gui4.ascii_glyphs[left] == rg_text_find_glyph(font, left),
			            "ASCII glyph table matches public lookup");
			for (u32 right = 0u; right < 128u; right++)
				TEST_ASSERT(fixture.gui4.ascii_kerning[left * 128u + right] ==
				                rg_text_find_kerning(font, left, right),
				            "ASCII kerning table matches public lookup");
		}
		test_fixture_free(&fixture);
	}
	TEST_PASS();
}

static const char* const test_layout_texts[] = {
    "XA", "AX", "AXB", "A\xC3\xA9" "A", "A\rB\nA\r\nB",
    "A \tA", "A\xFF" "A", "A\xF0\x9F\x98\x80" "B"};
static const f32 test_layout_scales[] = {0.0f, 1.0f, 0.7f, -0.5f};
static const u32 test_layout_capacities[] = {0u, 1u, 2u, 8u};

static void test_layout_font_init(TestRendererBaseFixture* font_owner, u32 variant)
{
	static RgTextKerning pairs[] = {
	    {'A', 'B', -1}, {'?', 'A', -2}, {'A', '?', 1}, {'A', 'X', -9},
	    {'A', 0xE9u, -3}, {0xE9u, 'A', -4}, {' ', 'A', 2}, {'A', ' ', -2}};
	test_font_init(font_owner);
	font_owner->font.kernings = pairs;
	font_owner->font.kerning_count = font_owner->font.kerning_capacity = (u32)RG_ARRAY_COUNT(pairs);
	font_owner->font.glyph_count = variant == 2u ? 5u : 4u;
	font_owner->font.fallback_codepoint = variant == 0u ? '?' : variant == 1u ? UINT32_MAX : 0xE9u;
}

static int test_cached_quad_matches(const RgGuiRendererBaseCachedQuad* actual,
                                    const RgTextQuad* expected)
{
	return test_float_equal(actual->x, expected->x0) &&
	       test_float_equal(actual->y, expected->y0) &&
	       test_float_equal(actual->w, expected->x1 - expected->x0) &&
	       test_float_equal(actual->h, expected->y1 - expected->y0) &&
	       actual->u0 == (u16)(expected->u0 * 64.0f + 0.5f) &&
	       actual->v0 == (u16)(expected->v0 * 32.0f + 0.5f) &&
	       actual->u1 == (u16)(expected->u1 * 64.0f + 0.5f) &&
	       actual->v1 == (u16)(expected->v1 * 32.0f + 0.5f);
}

static void test_layout_fallbacks_and_capacity(void)
{
	RgGuiRendererBaseLimits limits = {1u, 2u, 64u, 8u, 8u, 1u, 0u};
	for (u32 variant = 0u; variant < 3u; variant++)
	{
		TestRendererBaseFixture font_owner = {0}, fixture;
		test_layout_font_init(&font_owner, variant);
		TEST_ASSERT(test_fixture_init_font(&fixture, &limits, &font_owner.font), "layout fixture init");
		for (u32 t = 0u; t < RG_ARRAY_COUNT(test_layout_texts); t++)
		for (u32 s = 0u; s < RG_ARRAY_COUNT(test_layout_scales); s++)
		for (u32 c = 0u; c < RG_ARRAY_COUNT(test_layout_capacities); c++)
		{
			const char* text = test_layout_texts[t];
			f32 scale = test_layout_scales[s];
			u32 capacity = test_layout_capacities[c];
			RgTextQuad expected[8];
			RgGuiRendererBaseCachedQuad cached[9], guard;
			RgGuiRendererBaseInstance instances[9], instance_guard;
			memset(cached, 0xA5, sizeof(cached));
			memset(&guard, 0xA5, sizeof(guard));
			memset(instances, 0xA5, sizeof(instances));
			memset(&instance_guard, 0xA5, sizeof(instance_guard));
			size_t count = rg_text_build_quads(&fixture.font, text, strlen(text), 0, 0, scale,
			                                  (RgTextColor){1, 1, 1, 1}, expected, capacity);
			TEST_ASSERT(rg_gui_renderer_base_build_cached(&fixture.gui4, text, strlen(text), scale,
			                cached, capacity) == count, "cached layout count matches rg_text");
			for (u32 q = 0u; q < count; q++)
				TEST_ASSERT(test_cached_quad_matches(&cached[q], &expected[q]),
				            "cached layout geometry matches resolved glyph semantics");
			const rg_vec2 origin = {.x = 17.25f, .y = -3.5f};
			TEST_ASSERT(rg_text_build_quads(&fixture.font, text, strlen(text), origin.x, origin.y, scale,
			                (RgTextColor){1, 1, 1, 1}, expected, capacity) == count, "translated count");
			TEST_ASSERT(rg_gui_renderer_base_build_instances(&fixture.gui4, text, strlen(text), scale,
			                origin, UINT32_MAX, instances, capacity) == count, "reference instance count");
			for (u32 q = 0u; q < count; q++)
			{
				const RgGuiRendererBaseInstance* actual = &instances[q];
				TEST_ASSERT(test_float_equal(actual->x, expected[q].x0) &&
				                test_float_equal(actual->y, expected[q].y0) &&
				                test_float_equal(actual->w, expected[q].x1 - expected[q].x0) &&
				                test_float_equal(actual->h, expected[q].y1 - expected[q].y0),
				            "reference instance geometry matches rg_text");
				TEST_ASSERT(actual->u0 == cached[q].u0 && actual->v0 == cached[q].v0 &&
				                actual->u1 == cached[q].u1 && actual->v1 == cached[q].v1 &&
				                actual->color == UINT32_MAX && actual->padding == 0u,
				            "instance texture coordinates and color preserved");
			}
			for (u32 q = (u32)count; q < RG_ARRAY_COUNT(cached); q++)
				TEST_ASSERT(memcmp(&cached[q], &guard, sizeof(guard)) == 0 &&
				                memcmp(&instances[q], &instance_guard, sizeof(instance_guard)) == 0,
				            "layout writes only emitted quads within capacity");
		}
		test_fixture_free(&fixture);
	}
	TEST_PASS();
}

static void test_defaults_init_and_rollback(void)
{
	RgGuiRendererBaseLimits defaults = rg_gui_renderer_base_limits_default();
	TEST_ASSERT(defaults.max_cached_runs == 1024u, "default cached runs");
	TEST_ASSERT(defaults.hash_slot_count == 2048u, "default hash slots");
	TEST_ASSERT(defaults.text_capacity == 256u * 1024u, "default text bytes");
	TEST_ASSERT(defaults.max_cached_quads == 65536u, "default cached quads");
	TEST_ASSERT(defaults.max_frame_instances == 65536u, "default instances");
	TEST_ASSERT(defaults.max_batches == 256u, "default batches");
	TEST_ASSERT(rg_gui_renderer_base_memory_required(NULL) != SIZE_MAX, "default memory required");

	RgGuiRendererBaseLimits overflow = defaults;
	overflow.text_capacity = SIZE_MAX;
	TEST_ASSERT(rg_gui_renderer_base_memory_required(&overflow) == SIZE_MAX, "memory overflow rejected");

	TestRendererBaseFixture fixture;
	TEST_ASSERT(test_fixture_init(&fixture, NULL), "default context init");
	TEST_ASSERT(fixture.gui4.ascii_glyphs['A'] == &fixture.glyphs[1], "ASCII direct glyph table");
	TEST_ASSERT(fixture.gui4.ascii_kerning['A' * 128u + 'B'] == -1, "ASCII kerning table");
	TEST_ASSERT(fixture.gui4.hash_slot_mask == 2047u, "default power-of-two slot mask");
	test_fixture_free(&fixture);

	RgGuiRendererBaseLimits odd_slots = defaults;
	odd_slots.hash_slot_count = 3u;
	TEST_ASSERT(test_fixture_init(&fixture, &odd_slots), "non-power-of-two context init");
	TEST_ASSERT(fixture.gui4.hash_slot_mask == UINT32_MAX, "non-power-of-two modulo fallback");
	RgGuiDrawCmd fallback_cmd = test_text_cmd_identity("A", 0, 0, 1);
	RgGuiDrawList fallback_list = test_draw_list(&fallback_cmd, 1u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &fallback_list, 1u),
	            "modulo fallback cold prepare");
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &fallback_list, 1u),
	            "modulo fallback warm prepare");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_cache_hits == 1u,
	            "modulo fallback cache hit");
	test_fixture_free(&fixture);

	memset(&fixture, 0, sizeof(fixture));
	test_font_init(&fixture);
	unsigned char tiny_memory[64];
	fixture.arena.memory = (char*)tiny_memory;
	fixture.arena.capacity = sizeof(tiny_memory);
	fixture.arena.used = 7u;
	fixture.arena.committed = sizeof(tiny_memory);
	RgGuiRendererBaseInitDesc desc = {&fixture.font, defaults, NULL};
	TEST_ASSERT(!rg_gui_renderer_base_init(&fixture.gui4, &fixture.arena, &desc), "small arena rejected");
	TEST_ASSERT(fixture.arena.used == 7u, "arena usage rolled back");

	TEST_PASS();
}

static void test_geometry_parity_and_warm_cache(void)
{
	TestRendererBaseFixture fixture;
	TEST_ASSERT(test_fixture_init(&fixture, NULL), "fixture init");

	static const char text[] = "AB\n\xC3\xA9?";
	RgGuiDrawCmd cmd = test_text_cmd(text, 10.0f, 20.0f, 1.5f,
	                                 1.0f, 0.5f, 0.25f, 1.0f);
	RgGuiDrawList list = test_draw_list(&cmd, 1u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u), "prepare cold frame");

	RgTextQuad expected[8];
	RgTextColor color = {1.0f, 0.5f, 0.25f, 1.0f};
	size_t expected_count = rg_text_build_quads(&fixture.font, text, sizeof(text) - 1u,
	                                            10.0f, 20.0f, 1.5f, color, expected, 8u);
	const RgGuiRendererBasePrepared* prepared = rg_gui_renderer_base_prepared(&fixture.gui4);
	const RgGuiRendererBaseStats* stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(prepared && prepared->instance_count == expected_count, "glyph count parity");
	TEST_ASSERT(prepared->batch_count == 1u && !prepared->batches[0].clip_enabled,
	            "root batch");
	for (u32 i = 0u; i < prepared->instance_count; i++)
	{
		const RgGuiRendererBaseInstance* actual = &prepared->instances[i];
		TEST_ASSERT(test_float_equal(actual->x, expected[i].x0), "quad x0 parity");
		TEST_ASSERT(test_float_equal(actual->y, expected[i].y0), "quad y0 parity");
		TEST_ASSERT(test_float_equal(actual->w, expected[i].x1 - expected[i].x0), "quad width parity");
		TEST_ASSERT(test_float_equal(actual->h, expected[i].y1 - expected[i].y0), "quad height parity");
		TEST_ASSERT(actual->u0 == (u16)(expected[i].u0 * 64.0f + 0.5f), "atlas u0 parity");
		TEST_ASSERT(actual->v0 == (u16)(expected[i].v0 * 32.0f + 0.5f), "atlas v0 parity");
		TEST_ASSERT(actual->u1 == (u16)(expected[i].u1 * 64.0f + 0.5f), "atlas u1 parity");
		TEST_ASSERT(actual->v1 == (u16)(expected[i].v1 * 32.0f + 0.5f), "atlas v1 parity");
		TEST_ASSERT(actual->color == 0xFF4080FFu, "RGBA8 color packing");
	}
	TEST_ASSERT(stats->frame_cache_misses == 1u && stats->frame_cache_hits == 0u,
	            "cold cache counters");
	TEST_ASSERT(stats->frame_laid_out_glyphs == expected_count &&
	                stats->frame_reused_glyphs == 0u,
	            "cold layout telemetry");
	TEST_ASSERT(stats->frame_upload_bytes == expected_count * sizeof(RgGuiRendererBaseInstance),
	            "upload byte count");

	cmd.data.text.pos = (rg_vec2){.x = 100.0f, .y = 200.0f};
	cmd.data.text.color = (rg_vec4){.x = 0.0f, .y = 1.0f, .z = 0.0f, .w = 0.5f};
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u), "prepare warm frame");
	prepared = rg_gui_renderer_base_prepared(&fixture.gui4);
	stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(stats->frame_cache_hits == 1u && stats->frame_cache_misses == 0u,
	            "position and color excluded from key");
	TEST_ASSERT(stats->frame_reused_glyphs == expected_count &&
	                stats->frame_laid_out_glyphs == 0u,
	            "warm reuse telemetry");
	TEST_ASSERT(test_float_equal(prepared->instances[0].x, 100.0f), "cached relative x");
	TEST_ASSERT(test_float_equal(prepared->instances[0].y, 201.5f), "cached relative y");
	TEST_ASSERT(prepared->instances[0].color == 0x8000FF00u, "warm color changed");

	test_fixture_free(&fixture);
	TEST_PASS();
}

static void test_cache_copy_mutation_and_scale_key(void)
{
	TestRendererBaseFixture fixture;
	TEST_ASSERT(test_fixture_init(&fixture, NULL), "fixture init");
	char mutable_text[3] = "AB";
	RgGuiDrawCmd cmd = test_text_cmd(mutable_text, 0.0f, 0.0f, 1.0f, 1, 1, 1, 1);
	RgGuiDrawList list = test_draw_list(&cmd, 1u);

	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u), "first prepare");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_cache_misses == 1u, "first miss");

	mutable_text[1] = 'A';
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u), "mutated prepare");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_cache_misses == 1u, "mutation is new key");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->cache_runs == 2u, "copied keys retained");

	cmd.data.text.text = "AB";
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u), "original prepare");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_cache_hits == 1u, "original copy survives mutation");

	cmd.data.text.scale = 2.0f;
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u), "scaled prepare");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_cache_misses == 1u, "exact scale is part of key");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->cache_runs == 3u, "scaled run cached separately");

	test_fixture_free(&fixture);
	TEST_PASS();
}

static void test_static_identity_and_dynamic_fallback(void)
{
	TestRendererBaseFixture fixture;
	TEST_ASSERT(test_fixture_init(&fixture, NULL), "fixture init");
	char stable_a[3] = "AB";
	char stable_b[3] = "AB";
	RgGuiDrawCmd cmd = test_text_cmd_identity(stable_a, 0, 0, 1);
	RgGuiDrawList list = test_draw_list(&cmd, 1u);

	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u),
	            "identity cold prepare");
	const RgGuiRendererBaseStats* stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(stats->frame_cache_misses == 1u && stats->cache_text_bytes == 0u,
	            "identity key avoids cached text copy");

	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u),
	            "identity warm prepare");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_cache_hits == 1u,
	            "same identity reuses layout");

	cmd = test_text_cmd_identity(stable_b, 0, 0, 1);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u),
	            "distinct identity prepare");
	stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(stats->frame_cache_misses == 1u && stats->cache_runs == 2u,
	            "identity, not matching bytes, selects cached run");

	char mutable_text[3] = "AB";
	cmd = test_text_cmd(mutable_text, 0, 0, 1, 1, 1, 1, 1);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u),
	            "fallback cold prepare");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->cache_text_bytes == 2u,
	            "fallback copies key bytes");

	mutable_text[1] = 'A';
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u),
	            "fallback mutation prepare");
	stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(stats->frame_cache_misses == 1u && stats->cache_text_bytes == 4u,
	            "fallback detects mutation safely");

	test_fixture_free(&fixture);
	TEST_PASS();
}

static void test_run_stream_output_and_compaction_offsets(void)
{
	RgGuiRendererBaseLimits limits = {2u, 4u, 16u, 4u, 8u, 4u, 0u};
	TestRendererBaseFixture fixture;
	TEST_ASSERT(test_fixture_init(&fixture, &limits), "run stream fixture init");
	RgGuiDrawCmd cmds[2] =
	    {
	        test_text_cmd_identity("B", 10, 20, 1),
	        test_text_cmd_identity("A", 30, 40, 1)};
	RgGuiDrawList list = test_draw_list(cmds, 2u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_run_stream(&fixture.gui4, &list, list.count),
	            "run stream cold prepare");
	const RgGuiRendererBaseRunPrepared* prepared = rg_gui_renderer_base_run_prepared(&fixture.gui4);
	TEST_ASSERT(prepared && prepared->run_count == 2u && prepared->glyph_count == 2u,
	            "run and glyph counts");
	TEST_ASSERT(prepared->batches[0].first_instance == 0u &&
	                prepared->batches[0].instance_count == 2u,
	            "batch addresses run descriptors");
	TEST_ASSERT(prepared->runs[0].first_cached_quad == 0u &&
	                prepared->runs[0].first_output_instance == 0u &&
	                prepared->runs[1].first_output_instance == 1u,
	            "cache and output prefixes");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_upload_bytes == 64u,
	            "compact descriptor upload telemetry");

	list.cmds = &cmds[1];
	list.count = 1u;
	list.capacity = 1u;
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_run_stream(&fixture.gui4, &list, list.count),
	            "refresh retained run");

	cmds[0] = test_text_cmd_identity("A", 50, 60, 1);
	cmds[1] = test_text_cmd_identity("C", 70, 80, 1);
	list = test_draw_list(cmds, 2u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_run_stream(&fixture.gui4, &list, list.count),
	            "run stream compaction prepare");
	prepared = rg_gui_renderer_base_run_prepared(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_cache_compactions == 1u,
	            "compaction recorded");
	TEST_ASSERT(prepared->runs[0].first_cached_quad == 0u &&
	                prepared->runs[1].first_cached_quad == 1u,
	            "post-compaction cache offsets emitted");

	u64 rewrite_revision = fixture.gui4.cache_rewrite_revision;
	rg_gui_renderer_base_clear_cache(&fixture.gui4);
	TEST_ASSERT(fixture.gui4.cache_rewrite_revision == rewrite_revision + 1u,
	            "clear advances persistent-cache rewrite revision");
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(fixture.gui4.cache_rewrite_revision == rewrite_revision + 1u,
	            "begin preserves rewrite revision after clearing reset telemetry");

	test_fixture_free(&fixture);
	TEST_PASS();
}

static void test_cache_compaction_bypass_and_lifetimes(void)
{
	RgGuiRendererBaseLimits limits = {2u, 2u, 4u, 3u, 8u, 4u, 0u};
	TestRendererBaseFixture fixture;
	TEST_ASSERT(test_fixture_init(&fixture, &limits), "small fixture init");

	RgGuiDrawCmd cmds[2] =
	    {
	        test_text_cmd("A", 0, 0, 1, 1, 1, 1, 1),
	        test_text_cmd("B", 10, 0, 1, 1, 1, 1, 1)};
	RgGuiDrawList list = test_draw_list(cmds, 2u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 2u), "fill cache");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->cache_runs == 2u, "run cache filled");

	list.count = 1u;
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u), "refresh hot run");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_cache_hits == 1u,
	            "hot run reused before compaction");

	cmds[0] = test_text_cmd("?", 0, 0, 1, 1, 1, 1, 1);
	cmds[1] = test_text_cmd("A", 10, 0, 1, 1, 1, 1, 1);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	list.count = 2u;
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 2u), "compact and retry");
	const RgGuiRendererBaseStats* stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(stats->frame_cache_compactions == 1u &&
	                stats->frame_cache_evictions == 1u &&
	                stats->frame_cache_resets == 0u && stats->cache_runs == 2u,
	            "cold run evicted without resetting hot cache");
	TEST_ASSERT(stats->frame_cache_hits == 1u && stats->frame_cache_misses == 1u,
	            "retained hot run remains a hit");

	cmds[0] = test_text_cmd("AAAA", 0, 0, 1, 1, 1, 1, 1);
	list.count = 1u;
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u), "oversize bypass prepare");
	stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(stats->frame_cache_bypasses == 1u, "oversize run bypasses cache");
	TEST_ASSERT(stats->cache_runs == 2u, "bypass does not flush useful cache");
	TEST_ASSERT(rg_gui_renderer_base_prepared(&fixture.gui4)->instance_count == 4u, "bypass still renders");
	RgTextQuad bypass_expected[4];
	RgTextColor bypass_color = {1, 1, 1, 1};
	TEST_ASSERT(rg_text_build_quads(&fixture.font, "AAAA", 4u, 0, 0, 1,
	                                bypass_color, bypass_expected, 4u) == 4u,
	            "bypass reference geometry");
	for (u32 i = 0u; i < 4u; i++)
	{
		const RgGuiRendererBaseInstance* actual = &rg_gui_renderer_base_prepared(&fixture.gui4)->instances[i];
		TEST_ASSERT(test_float_equal(actual->x, bypass_expected[i].x0), "bypass x parity");
		TEST_ASSERT(test_float_equal(actual->y, bypass_expected[i].y0), "bypass y parity");
		TEST_ASSERT(test_float_equal(actual->w, bypass_expected[i].x1 - bypass_expected[i].x0),
		            "bypass width parity");
		TEST_ASSERT(actual->u0 == (u16)(bypass_expected[i].u0 * 64.0f + 0.5f),
		            "bypass atlas parity");
	}
	const RgGuiRendererBasePrepared* stable = rg_gui_renderer_base_prepared(&fixture.gui4);
	const RgGuiRendererBaseInstance* stable_instances = stable->instances;
	TEST_ASSERT(stable->instance_count == 4u, "prepared lifetime before begin");

	u64 total_resets = stats->total_cache_resets;
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepared(&fixture.gui4)->instance_count == 0u, "begin invalidates frame count");
	TEST_ASSERT(rg_gui_renderer_base_prepared(&fixture.gui4)->instances == stable_instances,
	            "caller-owned prepared storage remains stable");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->total_cache_resets == total_resets,
	            "begin preserves lifetime totals");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->cache_runs == 2u, "begin preserves cache");

	rg_gui_renderer_base_clear_cache(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->cache_runs == 0u, "explicit clear empties cache");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->frame_cache_resets == 1u, "explicit clear counted");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->high_water_cache_runs == 2u,
	            "cache high-water preserved");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->high_water_frame_instances == 4u,
	            "frame high-water preserved");

	test_fixture_free(&fixture);
	TEST_PASS();
}

static void test_atomic_capacity_drops(void)
{
	RgGuiRendererBaseLimits instance_limits = {8u, 16u, 64u, 16u, 2u, 4u, 0u};
	TestRendererBaseFixture fixture;
	TEST_ASSERT(test_fixture_init(&fixture, &instance_limits), "instance fixture init");
	RgGuiDrawCmd cmds[2] =
	    {
	        test_text_cmd("AAA", 0, 0, 1, 1, 1, 1, 1),
	        test_text_cmd("A", 20, 0, 1, 1, 1, 1, 1)};
	RgGuiDrawList list = test_draw_list(cmds, 2u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 2u), "instance drop prepare");
	const RgGuiRendererBaseStats* stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepared(&fixture.gui4)->instance_count == 1u,
	            "oversize first run dropped atomically");
	TEST_ASSERT(stats->frame_dropped_runs == 1u && stats->frame_dropped_glyphs == 3u,
	            "exact instance drop counters");
	TEST_ASSERT(stats->diagnostic_flags & RG_GUI_RENDERER_BASE_DIAGNOSTIC_INSTANCE_CAPACITY,
	            "instance capacity diagnostic");
	test_fixture_free(&fixture);

	RgGuiRendererBaseLimits batch_limits = {8u, 16u, 64u, 16u, 8u, 1u, 0u};
	TEST_ASSERT(test_fixture_init(&fixture, &batch_limits), "batch fixture init");
	RgGuiDrawCmd clipped[6] =
	    {
	        test_clip_cmd(RG_GUI_CMD_CLIP_PUSH, 0, 0, 10, 10),
	        test_text_cmd("A", 0, 0, 1, 1, 1, 1, 1),
	        test_clip_cmd(RG_GUI_CMD_CLIP_POP, 0, 0, 0, 0),
	        test_clip_cmd(RG_GUI_CMD_CLIP_PUSH, 20, 0, 10, 10),
	        test_text_cmd("B", 20, 0, 1, 1, 1, 1, 1),
	        test_clip_cmd(RG_GUI_CMD_CLIP_POP, 0, 0, 0, 0)};
	list = test_draw_list(clipped, 6u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 6u), "batch drop prepare");
	stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepared(&fixture.gui4)->instance_count == 1u,
	            "new-clip run dropped atomically");
	TEST_ASSERT(stats->frame_dropped_runs == 1u && stats->frame_dropped_glyphs == 1u,
	            "exact batch drop counters");
	TEST_ASSERT(stats->diagnostic_flags & RG_GUI_RENDERER_BASE_DIAGNOSTIC_BATCH_CAPACITY,
	            "batch capacity diagnostic");
	test_fixture_free(&fixture);

	TEST_PASS();
}

static void test_clips_overlay_and_batch_coalescing(void)
{
	TestRendererBaseFixture fixture;
	TEST_ASSERT(test_fixture_init(&fixture, NULL), "fixture init");
	RgGuiDrawCmd cmds[8] =
	    {
	        test_clip_cmd(RG_GUI_CMD_CLIP_PUSH, 0, 0, 100, 100),
	        test_clip_cmd(RG_GUI_CMD_CLIP_PUSH, 50, -10, 100, 30),
	        test_text_cmd("A", 50, 0, 1, 1, 1, 1, 1),
	        test_text_cmd("B", 60, 0, 1, 1, 1, 1, 1),
	        test_clip_cmd(RG_GUI_CMD_CLIP_POP, 0, 0, 0, 0),
	        test_text_cmd("A", 0, 0, 1, 1, 1, 1, 1),
	        test_text_cmd("B", 0, 0, 1, 1, 1, 1, 1),
	        test_clip_cmd(RG_GUI_CMD_CLIP_POP, 0, 0, 0, 0)};
	RgGuiDrawList list = test_draw_list(cmds, 8u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 6u), "clip prepare");
	const RgGuiRendererBasePrepared* prepared = rg_gui_renderer_base_prepared(&fixture.gui4);
	const RgGuiRendererBaseStats* stats = rg_gui_renderer_base_stats(&fixture.gui4);
	TEST_ASSERT(prepared->batch_count == 3u, "nested, outer, and overlay root batches");
	TEST_ASSERT(prepared->batches[0].instance_count == 2u, "same clip coalesced");
	TEST_ASSERT(prepared->batches[0].clip_enabled, "nested clip enabled");
	TEST_ASSERT(test_float_equal(prepared->batches[0].clip.x, 50.0f) &&
	                test_float_equal(prepared->batches[0].clip.y, 0.0f) &&
	                test_float_equal(prepared->batches[0].clip.w, 50.0f) &&
	                test_float_equal(prepared->batches[0].clip.h, 20.0f),
	            "nested clip intersection");
	TEST_ASSERT(prepared->batches[1].clip_enabled &&
	                test_float_equal(prepared->batches[1].clip.w, 100.0f),
	            "pop restores outer clip");
	TEST_ASSERT(!prepared->batches[2].clip_enabled, "overlay resets to root clip");
	TEST_ASSERT(stats->diagnostic_flags & RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_UNDERFLOW,
	            "overlay reset makes stray pop diagnosable");

	RgGuiDrawCmd root_overlay[2] =
	    {
	        test_text_cmd("A", 0, 0, 1, 1, 1, 1, 1),
	        test_text_cmd("B", 10, 0, 1, 1, 1, 1, 1)};
	list = test_draw_list(root_overlay, 2u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, 1u),
	            "root overlay prepare");
	prepared = rg_gui_renderer_base_prepared(&fixture.gui4);
	TEST_ASSERT(prepared->batch_count == 2u,
	            "overlay boundary splits matching root clips");
	TEST_ASSERT(prepared->batches[0].first_instance == 0u &&
	                prepared->batches[0].instance_count == 1u &&
	                prepared->batches[1].first_instance == 1u &&
	                prepared->batches[1].instance_count == 1u,
	            "overlay batches preserve instance order");

	RgGuiDrawCmd overflow_cmds[RG_GUI_RENDERER_BASE_CLIP_STACK_MAX + 2u];
	for (u32 i = 0u; i < RG_GUI_RENDERER_BASE_CLIP_STACK_MAX + 1u; i++)
		overflow_cmds[i] = test_clip_cmd(RG_GUI_CMD_CLIP_PUSH, 0, 0, 10, 10);
	overflow_cmds[RG_GUI_RENDERER_BASE_CLIP_STACK_MAX + 1u] = test_text_cmd("A", 0, 0, 1, 1, 1, 1, 1);
	list = test_draw_list(overflow_cmds, RG_GUI_RENDERER_BASE_CLIP_STACK_MAX + 2u);
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, &list, list.count), "overflow prepare");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->diagnostic_flags &
	                RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_OVERFLOW,
	            "clip overflow diagnostic");

	test_fixture_free(&fixture);
	TEST_PASS();
}

static void test_invalid_arguments(void)
{
	TestRendererBaseFixture fixture;
	TEST_ASSERT(test_fixture_init(&fixture, NULL), "fixture init");
	rg_gui_renderer_base_begin_frame(&fixture.gui4);
	TEST_ASSERT(!rg_gui_renderer_base_prepare_draw_list(&fixture.gui4, NULL, 0u), "null list rejected");
	TEST_ASSERT(rg_gui_renderer_base_stats(&fixture.gui4)->diagnostic_flags &
	                RG_GUI_RENDERER_BASE_DIAGNOSTIC_INVALID_ARGUMENT,
	            "invalid argument diagnostic");
	TEST_ASSERT(rg_gui_renderer_base_prepared(NULL) == NULL, "null prepared accessor");
	TEST_ASSERT(rg_gui_renderer_base_stats(NULL) == NULL, "null stats accessor");
	test_fixture_free(&fixture);
	TEST_PASS();
}

int main(int argc, char** argv)
{
	(void)argc;
	(void)argv;
	printf("Running test-only reference renderer CPU tests...\n\n");

	test_defaults_init_and_rollback();
	test_ascii_lookup_table_equivalence();
	test_layout_fallbacks_and_capacity();
	test_geometry_parity_and_warm_cache();
	test_cache_copy_mutation_and_scale_key();
	test_static_identity_and_dynamic_fallback();
	test_run_stream_output_and_compaction_offsets();
	test_cache_compaction_bypass_and_lifetimes();
	test_atomic_capacity_drops();
	test_clips_overlay_and_batch_coalescing();
	test_invalid_arguments();

	printf("\nResults: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
	return g_tests_failed ? 1 : 0;
}
