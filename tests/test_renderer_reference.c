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

static int test_fixture_init(TestRendererBaseFixture* fixture, const RgGuiRendererBaseLimits* requested)
{
	memset(fixture, 0, sizeof(*fixture));
	test_font_init(fixture);
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
	cmd.data.text.pos = (rg_vec2){x, y};
	cmd.data.text.scale = scale;
	cmd.data.text.color = (rg_vec4){r, g, b, a};
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
	RgGuiRendererBaseInitDesc desc = {&fixture.font, defaults};
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

	cmd.data.text.pos = (rg_vec2){100.0f, 200.0f};
	cmd.data.text.color = (rg_vec4){0.0f, 1.0f, 0.0f, 0.5f};
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
	RgGuiRendererBaseLimits limits = {2u, 4u, 16u, 4u, 8u, 4u};
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
	RgGuiRendererBaseLimits limits = {2u, 2u, 4u, 3u, 8u, 4u};
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
	RgGuiRendererBaseLimits instance_limits = {8u, 16u, 64u, 16u, 2u, 4u};
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

	RgGuiRendererBaseLimits batch_limits = {8u, 16u, 64u, 16u, 8u, 1u};
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
