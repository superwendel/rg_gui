// renderer segmented page-cache correctness tests

#define RG_GUI_RENDERER_TEST_REFERENCE 1
#define main test_renderer_reference_main
#include "test_renderer_reference.c"
#undef main

typedef struct TestRendererFixture
{
	TestRendererBaseFixture font_owner;
	void* memory;
	RgArena arena;
	RgGuiRenderer renderer;
} TestRendererFixture;

static int test_renderer_fixture_init_font(TestRendererFixture* fixture,
                                           const RgGuiRendererLimits* limits,
                                           u32 page_quads, const RgTextFont* font)
{
	memset(fixture, 0, sizeof(*fixture));
	test_font_init(&fixture->font_owner);
	if (font) fixture->font_owner.font = *font;
	size_t size = rg_gui_renderer_memory_required(limits, page_quads);
	if (size == SIZE_MAX) return 0;
	fixture->memory = malloc(size);
	if (!fixture->memory) return 0;
	fixture->arena.memory = (char*)fixture->memory;
	fixture->arena.capacity = size;
	fixture->arena.committed = size;
	RgGuiRendererInitDesc desc = {0};
	desc.font = &fixture->font_owner.font;
	if (limits) desc.limits = *limits;
	desc.page_quads = page_quads;
	return rg_gui_renderer_init(&fixture->renderer, &fixture->arena, &desc);
}

static int test_renderer_fixture_init(TestRendererFixture* fixture,
                                      const RgGuiRendererLimits* limits,
                                      u32 page_quads)
{
	return test_renderer_fixture_init_font(fixture, limits, page_quads, NULL);
}

static void test_renderer_fixture_free(TestRendererFixture* fixture)
{
	free(fixture->memory);
	memset(fixture, 0, sizeof(*fixture));
}

static void test_renderer_expand(const RgGuiRenderer* ctx,
                                 const RgGuiRendererPrepared* prepared,
                                 RgGuiRendererBaseInstance* output)
{
	for (u32 r = 0u; r < prepared->run_count; r++)
	{
		const RgGuiRendererRun* run = &prepared->runs[r];
		const RgGuiRendererBaseCachedQuad* quads =
		    ctx->core.cache_quads + run->first_cached_quad;
		for (u32 q = 0u; q < run->quad_count; q++)
		{
			RgGuiRendererBaseInstance* instance = &output[run->first_output_instance + q];
			instance->x = run->x + quads[q].x;
			instance->y = run->y + quads[q].y;
			instance->w = quads[q].w;
			instance->h = quads[q].h;
			instance->u0 = quads[q].u0;
			instance->v0 = quads[q].v0;
			instance->u1 = quads[q].u1;
			instance->v1 = quads[q].v1;
			instance->color = run->color;
			instance->padding = 0u;
		}
	}
}

static void test_renderer_page_size_parity(u32 page_quads,
                                           u32 expected_segments,
                                           u32 expected_waste)
{
	RgGuiRendererLimits limits = {4u, 8u, 256u, 128u, 128u, 4u};
	TestRendererBaseFixture control;
	TestRendererFixture candidate;
	TEST_ASSERT(test_fixture_init(&control, &limits), "control fixture init");
	TEST_ASSERT(test_renderer_fixture_init(&candidate, &limits, page_quads),
	            "candidate fixture init");
	static const char text[] = "AAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAAA";
	RgGuiDrawCmd cmd = test_text_cmd(text, 12.0f, 34.0f, 1.5f,
	                                 0.25f, 0.5f, 1.0f, 1.0f);
	RgGuiDrawList list = test_draw_list(&cmd, 1u);
	rg_gui_renderer_base_begin_frame(&control.gui4);
	rg_gui_renderer_begin_frame(&candidate.renderer);
	TEST_ASSERT(rg_gui_renderer_base_prepare_draw_list(&control.gui4, &list, list.count),
	            "control prepare");
	TEST_ASSERT(rg_gui_renderer_prepare(&candidate.renderer, &list, list.count),
	            "segmented prepare");
	const RgGuiRendererBasePrepared* expected = rg_gui_renderer_base_prepared(&control.gui4);
	const RgGuiRendererPrepared* prepared = rg_gui_renderer_prepared(&candidate.renderer);
	const RgGuiRendererAllocatorStats* allocator =
	    rg_gui_renderer_allocator_stats(&candidate.renderer);
	RgGuiRendererBaseInstance actual[40];
	memset(actual, 0, sizeof(actual));
	test_renderer_expand(&candidate.renderer, prepared, actual);
	TEST_ASSERT(prepared->glyph_count == 40u &&
	                prepared->run_count == expected_segments,
	            "page segment count");
	TEST_ASSERT(prepared->batches[0].instance_count == expected_segments,
	            "batch spans every segment");
	TEST_ASSERT(memcmp(actual, expected->instances, sizeof(actual)) == 0,
	            "segmented geometry exactly matches control");
	TEST_ASSERT(allocator->allocated_pages == expected_segments &&
	                allocator->wasted_quads == expected_waste,
	            "page accounting and bounded waste");
	for (u32 i = 0u; i < prepared->run_count; i++)
	{
		u32 expected_first = i * page_quads;
		if (expected_first > 40u) expected_first = 40u;
		TEST_ASSERT(prepared->runs[i].first_output_instance == expected_first,
		            "segment output prefix");
	}
	u32 first_quad = prepared->runs[0].first_cached_quad;
	rg_gui_renderer_begin_frame(&candidate.renderer);
	TEST_ASSERT(rg_gui_renderer_prepare(&candidate.renderer, &list, list.count),
	            "warm segmented prepare");
	prepared = rg_gui_renderer_prepared(&candidate.renderer);
	TEST_ASSERT(prepared->runs[0].first_cached_quad == first_quad,
	            "warm page offset remains stable");
	TEST_ASSERT(candidate.renderer.dirty_page_count == 0u &&
	                rg_gui_renderer_stats(&candidate.renderer)->frame_cache_hits == 1u,
	            "warm hit performs no cache upload");
	test_renderer_fixture_free(&candidate);
	test_fixture_free(&control);
}

static void test_renderer_page_sizes_and_geometry(void)
{
	test_renderer_page_size_parity(16u, 3u, 8u);
	if (g_tests_failed) return;
	test_renderer_page_size_parity(32u, 2u, 24u);
	if (g_tests_failed) return;
	TEST_PASS();
}

static void test_renderer_page_layout_fallbacks_and_capacity(void)
{
	RgGuiRendererLimits limits = {1u, 2u, 64u, 8u, 8u, 1u};
	const u32 pages[] = {3u, 1u, 0u, 2u};
	for (u32 variant = 0u; variant < 3u; variant++)
	{
		TestRendererBaseFixture font_owner = {0};
		TestRendererFixture fixture;
		test_layout_font_init(&font_owner, variant);
		TEST_ASSERT(test_renderer_fixture_init_font(&fixture, &limits, 2u, &font_owner.font),
		            "fallback page fixture init");
		RgGuiRenderer* ctx = &fixture.renderer;
		for (u32 p = 0u; p < RG_ARRAY_COUNT(pages); p++)
			ctx->page_next[pages[p]] = p + 1u < RG_ARRAY_COUNT(pages) ? pages[p + 1u] : UINT32_MAX;
		for (u32 t = 0u; t < RG_ARRAY_COUNT(test_layout_texts); t++)
		for (u32 s = 0u; s < RG_ARRAY_COUNT(test_layout_scales); s++)
		for (u32 c = 0u; c < RG_ARRAY_COUNT(test_layout_capacities); c++)
		{
			const char* text = test_layout_texts[t];
			f32 scale = test_layout_scales[s];
			u32 capacity = test_layout_capacities[c];
			RgTextQuad expected[8];
			RgGuiRendererBaseCachedQuad guard;
			memset(&guard, 0xA5, sizeof(guard));
			memset(ctx->core.cache_quads, 0xA5, 8u * sizeof(*ctx->core.cache_quads));
			size_t count = rg_text_build_quads(ctx->core.font, text, strlen(text), 0, 0, scale,
			                                  (RgTextColor){1, 1, 1, 1}, expected, capacity);
			TEST_ASSERT(rg_gui_renderer_build_pages(ctx, text, strlen(text), scale, pages[0], capacity) == count,
			            "scattered page count matches rg_text");
			for (u32 q = 0u; q < 8u; q++)
			{
				const RgGuiRendererBaseCachedQuad* actual = &ctx->core.cache_quads[pages[q / 2u] * 2u + q % 2u];
				if (q < count)
					TEST_ASSERT(test_cached_quad_matches(actual, &expected[q]),
					            "scattered page geometry matches resolved glyph semantics");
				else
					TEST_ASSERT(memcmp(actual, &guard, sizeof(guard)) == 0,
					            "page builder leaves un-emitted storage untouched");
			}
		}

		/* Also traverse the public cold/warm path, with kerning crossing page boundaries. */
		rg_gui_renderer_clear_cache(ctx);
		static const char text[] = "AXA\r\nA\xC3\xA9" "AXA";
		RgGuiDrawCmd cmd = test_text_cmd_identity(text, 12.0f, -4.0f, 1.5f);
		RgGuiDrawList list = test_draw_list(&cmd, 1u);
		RgTextQuad expected[8];
		size_t count = rg_text_build_quads(ctx->core.font, text, sizeof(text) - 1u, 12, -4, 1.5f,
		                                  (RgTextColor){1, 1, 1, 1}, expected, 8u);
		for (u32 frame = 0u; frame < 2u; frame++)
		{
			rg_gui_renderer_begin_frame(ctx);
			TEST_ASSERT(rg_gui_renderer_prepare(ctx, &list, 1u), "public fallback page prepare");
			const RgGuiRendererPrepared* prepared = rg_gui_renderer_prepared(ctx);
			TEST_ASSERT(prepared->glyph_count == count && prepared->run_count == (count + 1u) / 2u,
			            "fallback layout emits all page segments");
			RgGuiRendererBaseInstance actual[8];
			test_renderer_expand(ctx, prepared, actual);
			for (u32 q = 0u; q < count; q++)
				TEST_ASSERT(test_float_equal(actual[q].x, expected[q].x0) &&
				                test_float_equal(actual[q].y, expected[q].y0),
				            "cold and warm production geometry matches rg_text");
			if (frame)
				TEST_ASSERT(rg_gui_renderer_stats(ctx)->frame_cache_hits == 1u && ctx->dirty_page_count == 0u,
				            "fallback layout reuses warm pages without upload");
		}
		test_renderer_fixture_free(&fixture);
	}
	TEST_PASS();
}

static void test_renderer_fragmentation_eliminated(void)
{
	RgGuiRendererLimits limits = {4u, 8u, 64u, 6u, 8u, 4u};
	TestRendererFixture fixture;
	TEST_ASSERT(test_renderer_fixture_init(&fixture, &limits, 1u),
	            "fragment fixture init");
	RgGuiDrawCmd initial_cmds[3] =
	    {
	        test_text_cmd_identity("AAA", 0, 0, 1),
	        test_text_cmd_identity("B", 0, 0, 1),
	        test_text_cmd_identity("AA", 0, 0, 1)};
	RgGuiDrawList list = test_draw_list(initial_cmds, 3u);
	rg_gui_renderer_begin_frame(&fixture.renderer);
	TEST_ASSERT(rg_gui_renderer_prepare(&fixture.renderer, &list, list.count),
	            "fill page cache");
	u32 retained_quad = rg_gui_renderer_prepared(&fixture.renderer)->runs[3].first_cached_quad;

	RgGuiDrawCmd refresh = initial_cmds[1];
	list = test_draw_list(&refresh, 1u);
	rg_gui_renderer_begin_frame(&fixture.renderer);
	TEST_ASSERT(rg_gui_renderer_prepare(&fixture.renderer, &list, list.count),
	            "refresh middle page");

	RgGuiDrawCmd fragmented_cmds[2] =
	    {
	        refresh,
	        test_text_cmd_identity("AAAA", 0, 0, 1)};
	list = test_draw_list(fragmented_cmds, 2u);
	rg_gui_renderer_begin_frame(&fixture.renderer);
	TEST_ASSERT(rg_gui_renderer_prepare(&fixture.renderer, &list, list.count),
	            "allocate a chain from noncontiguous free pages");
	const RgGuiRendererPrepared* prepared = rg_gui_renderer_prepared(&fixture.renderer);
	const RgGuiRendererAllocatorStats* allocator = rg_gui_renderer_allocator_stats(&fixture.renderer);
	const RgGuiRendererStats* stats = rg_gui_renderer_stats(&fixture.renderer);
	TEST_ASSERT(prepared->glyph_count == 5u && prepared->run_count == 5u,
	            "fragmented pages form complete descriptors");
	TEST_ASSERT(prepared->runs[0].first_cached_quad == retained_quad,
	            "retained page never moves");
	TEST_ASSERT(allocator->free_pages == 1u && allocator->wasted_quads == 0u,
	            "all aggregate free pages remain usable");
	TEST_ASSERT(stats->frame_cache_bypasses == 0u &&
	                stats->frame_dropped_runs == 0u && !stats->diagnostic_flags,
	            "former external-fragmentation case succeeds");
	test_renderer_fixture_free(&fixture);
	TEST_PASS();
}

static void test_renderer_invalid_page_size(void)
{
	RgGuiRendererLimits limits = {4u, 8u, 64u, 64u, 64u, 4u};
	TEST_ASSERT(rg_gui_renderer_page_quads_resolve(0u, limits.max_cached_quads) == 32u,
	            "promoted default uses 32-quad pages");
	TEST_ASSERT(rg_gui_renderer_memory_required(&limits, 3u) == SIZE_MAX,
	            "non-power-of-two pages rejected");
	TEST_ASSERT(rg_gui_renderer_memory_required(&limits, 128u) == SIZE_MAX,
	            "oversized pages rejected");
	limits.max_cached_quads = 60u;
	TEST_ASSERT(rg_gui_renderer_memory_required(&limits, 16u) == SIZE_MAX,
	            "partial physical pages rejected");
	TEST_PASS();
}

static void test_renderer_zero_quad_run_uses_no_page(void)
{
	RgGuiRendererLimits limits = {4u, 8u, 64u, 64u, 64u, 4u};
	TestRendererFixture fixture;
	TEST_ASSERT(test_renderer_fixture_init(&fixture, &limits, 16u),
	            "zero-quad fixture init");
	RgGuiDrawCmd cmd = test_text_cmd_identity("   ", 0, 0, 1);
	RgGuiDrawList list = test_draw_list(&cmd, 1u);
	rg_gui_renderer_begin_frame(&fixture.renderer);
	TEST_ASSERT(rg_gui_renderer_prepare(&fixture.renderer, &list, list.count),
	            "zero-quad run cached");
	TEST_ASSERT(rg_gui_renderer_prepared(&fixture.renderer)->run_count == 0u &&
	                rg_gui_renderer_stats(&fixture.renderer)->cache_runs == 1u &&
	                rg_gui_renderer_allocator_stats(&fixture.renderer)->allocated_pages == 0u,
	            "zero-quad run does not address or allocate a page");
	test_renderer_fixture_free(&fixture);
	TEST_PASS();
}

static void test_renderer_clear_is_lazy_and_reuses_from_zero(void)
{
	RgGuiRendererLimits limits = {4u, 8u, 64u, 64u, 64u, 4u};
	TestRendererFixture fixture;
	TEST_ASSERT(test_renderer_fixture_init(&fixture, &limits, 16u),
	            "lazy-clear fixture init");
	for (u32 i = 0u; i < fixture.renderer.total_pages; i++)
		fixture.renderer.free_pages[i] = UINT32_C(0xA5A5A5A5);
	rg_gui_renderer_clear_cache(&fixture.renderer);
	TEST_ASSERT(fixture.renderer.next_fresh_page == 0u &&
	                fixture.renderer.recycled_page_count == 0u &&
	                fixture.renderer.free_page_count == fixture.renderer.total_pages,
	            "clear resets allocator with scalar state only");
	for (u32 i = 0u; i < fixture.renderer.total_pages; i++)
	{
		TEST_ASSERT(fixture.renderer.free_pages[i] == UINT32_C(0xA5A5A5A5),
		            "clear does not rebuild the free-page array");
	}

	RgGuiDrawCmd cmd = test_text_cmd_identity("AB", 0, 0, 1);
	RgGuiDrawList list = test_draw_list(&cmd, 1u);
	rg_gui_renderer_begin_frame(&fixture.renderer);
	TEST_ASSERT(rg_gui_renderer_prepare(&fixture.renderer, &list, list.count),
	            "post-clear bump allocation");
	TEST_ASSERT(rg_gui_renderer_prepared(&fixture.renderer)->runs[0].first_cached_quad == 0u &&
	                fixture.renderer.next_fresh_page == 1u &&
	                fixture.renderer.recycled_page_count == 0u,
	            "stale recycled entries are ignored after clear");
	test_renderer_fixture_free(&fixture);
	TEST_PASS();
}

static void test_renderer_single_page_recycles_directly(void)
{
	RgGuiRendererLimits limits = {1u, 2u, 64u, 64u, 64u, 4u};
	TestRendererFixture fixture;
	TEST_ASSERT(test_renderer_fixture_init(&fixture, &limits, 32u),
	            "single-page recycle fixture init");
	RgGuiDrawCmd cmd = test_text_cmd_identity("A", 0, 0, 1);
	RgGuiDrawList list = test_draw_list(&cmd, 1u);
	rg_gui_renderer_begin_frame(&fixture.renderer);
	TEST_ASSERT(rg_gui_renderer_prepare(&fixture.renderer, &list, list.count),
	            "fresh one-page allocation");
	TEST_ASSERT(fixture.renderer.next_fresh_page == 1u &&
	                fixture.renderer.dirty_page_count == 1u,
	            "fresh one-page fast path accounting");

	cmd = test_text_cmd_identity("B", 0, 0, 1);
	rg_gui_renderer_begin_frame(&fixture.renderer);
	TEST_ASSERT(rg_gui_renderer_prepare(&fixture.renderer, &list, list.count),
	            "recycled one-page allocation");
	TEST_ASSERT(rg_gui_renderer_prepared(&fixture.renderer)->runs[0].first_cached_quad == 0u &&
	                fixture.renderer.next_fresh_page == 1u &&
	                fixture.renderer.recycled_page_count == 0u &&
	                fixture.renderer.dirty_page_count == 1u,
	            "single page freed and reused without chain traversal");
	test_renderer_fixture_free(&fixture);
	TEST_PASS();
}

int main(int argc, char** argv)
{
	if (test_renderer_reference_main(argc, argv) != 0) return 1;
	g_tests_passed = 0;
	g_tests_failed = 0;
	printf("Running renderer segmented page-cache tests...\n\n");
	test_renderer_page_sizes_and_geometry();
	test_renderer_page_layout_fallbacks_and_capacity();
	test_renderer_fragmentation_eliminated();
	test_renderer_invalid_page_size();
	test_renderer_zero_quad_run_uses_no_page();
	test_renderer_clear_is_lazy_and_reuses_from_zero();
	test_renderer_single_page_recycles_directly();
	printf("\nResults: %d passed, %d failed\n", g_tests_passed, g_tests_failed);
	return g_tests_failed ? 1 : 0;
}
