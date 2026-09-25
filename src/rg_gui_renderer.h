// rg_gui_renderer - Stable cached-text preparation for rg_gui draw lists
//
// Part of the Reverse Gravity (rg_) libraries.
// Single-header C11/GNU11 renderer preparation layer using persistent, fixed-size
// glyph pages and compact frame-local run descriptors.
//
// This is an independent implementation informed by the public architectural
// ideas in Casey Muratori's RefTerm project: cache reusable text layout and
// upload compact glyph records. No RefTerm source code is included here.
// RefTerm: https://github.com/cmuratori/refterm
//
// NOTES:
//   - The generic page allocator defaults to 32 glyph quads per page.
//   - Cached pages remain stable until reclaimed; live geometry is never moved.
//   - All functions have internal linkage and work in unity builds.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_GUI_RENDERER_H
#define RG_GUI_RENDERER_H

#ifndef RG_GUI_INCLUDED
#include "rg_gui.h"
#endif
#include "rg_mem.h"
#include "rg_text.h"

#include <stddef.h>
#include <string.h>

#if !defined(RG_GUI_TEXT_CACHE_IDENTITY)
#error rg_gui_renderer requires text cache identity in rg_gui.h
#endif

#define RG_GUI_RENDERER_BASE_IDENTITY_TEXT_SIZE SIZE_MAX

#ifndef RG_GUI_RENDERER_BASE_CLIP_STACK_MAX
#define RG_GUI_RENDERER_BASE_CLIP_STACK_MAX 64u
#endif

typedef struct RgGuiRendererBaseLimits
{
	u32 max_cached_runs;
	u32 hash_slot_count;
	size_t text_capacity;
	u32 max_cached_quads;
	u32 max_frame_instances;
	u32 max_batches;
	/* Page-segment descriptors, independently bounded from expanded glyphs.
	 * Zero preserves the max_frame_instances bound for existing callers. */
	u32 max_frame_runs;
} RgGuiRendererBaseLimits;

typedef struct RgGuiRendererBaseInitDesc
{
	const RgTextFont* font;
	RgGuiRendererBaseLimits limits;
	const RgGuiTextLookup* text_lookup;
} RgGuiRendererBaseInitDesc;

/** Compact renderer-facing glyph record. RGBA occupies low-to-high bytes. */
typedef struct RgGuiRendererBaseInstance
{
	f32 x;
	f32 y;
	f32 w;
	f32 h;
	u16 u0;
	u16 v0;
	u16 u1;
	u16 v1;
	u32 color;
	u32 padding;
} RgGuiRendererBaseInstance;

typedef char RgGuiRendererBaseInstanceMustBe32Bytes[(sizeof(RgGuiRendererBaseInstance) == 32u) ? 1 : -1];

typedef struct RgGuiRendererBaseBatch
{
	RgGuiRect clip;
	u32 first_instance;
	u32 instance_count;
	u32 clip_enabled;
} RgGuiRendererBaseBatch;

#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
/** Test-only CPU-expanded output used as an independent correctness reference. */
typedef struct RgGuiRendererBasePrepared
{
	const RgGuiRendererBaseInstance* instances;
	u32 instance_count;
	const RgGuiRendererBaseBatch* batches;
	u32 batch_count;
} RgGuiRendererBasePrepared;
#endif

/** One frame-local reference to persistent cached glyph geometry. */
typedef struct RgGuiRendererBaseRunInstance
{
	u32 first_cached_quad;
	u32 quad_count;
	f32 x;
	f32 y;
	u32 color;
	u32 first_output_instance;
	/* Host metadata: padding[0] is the source draw-command index; padding[1]
	   is reserved. Both remain outside the shader's 24-byte payload. */
	u32 padding[2];
} RgGuiRendererBaseRunInstance;

typedef char RgGuiRendererBaseRunInstanceMustBe32Bytes[(sizeof(RgGuiRendererBaseRunInstance) == 32u) ? 1 : -1];

/** Source draw-list index for a prepared run or page segment. */
RGINLINE u32 rg_gui_renderer_run_command_index(const RgGuiRendererBaseRunInstance* run)
{
	return run->padding[0];
}

typedef struct RgGuiRendererBaseRunPrepared
{
	const RgGuiRendererBaseRunInstance* runs;
	u32 run_count;
	u32 glyph_count;
	const RgGuiRendererBaseBatch* batches;
	u32 batch_count;
} RgGuiRendererBaseRunPrepared;

typedef u32 RgGuiRendererBaseDiagnosticFlags;

enum
{
	RG_GUI_RENDERER_BASE_DIAGNOSTIC_NONE = 0u,
	RG_GUI_RENDERER_BASE_DIAGNOSTIC_INVALID_ARGUMENT = 1u << 0,
	RG_GUI_RENDERER_BASE_DIAGNOSTIC_INSTANCE_CAPACITY = 1u << 1,
	RG_GUI_RENDERER_BASE_DIAGNOSTIC_BATCH_CAPACITY = 1u << 2,
	RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_OVERFLOW = 1u << 3,
	RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_UNDERFLOW = 1u << 4,
	RG_GUI_RENDERER_BASE_DIAGNOSTIC_RUN_CACHE_REQUIRED = 1u << 5
};

typedef struct RgGuiRendererBaseStats
{
	RgGuiRendererBaseDiagnosticFlags diagnostic_flags;
	u32 frame_text_runs;
	u32 frame_glyphs;
	u32 frame_laid_out_glyphs;
	u32 frame_reused_glyphs;
	u32 frame_instances;
	u32 frame_batches;
	u32 frame_cache_hits;
	u32 frame_cache_misses;
	u32 frame_cache_resets;
	u32 frame_cache_compactions;
	u32 frame_cache_evictions;
	u32 frame_cache_bypasses;
	u32 frame_dropped_runs;
	u32 frame_dropped_glyphs;
	size_t frame_upload_bytes;
	u64 total_cache_hits;
	u64 total_cache_misses;
	u64 total_cache_resets;
	u64 total_cache_compactions;
	u64 total_cache_evictions;
	u64 total_cache_bypasses;
	u64 total_laid_out_glyphs;
	u64 total_reused_glyphs;
	u64 total_dropped_runs;
	u64 total_dropped_glyphs;
	u32 cache_runs;
	size_t cache_text_bytes;
	u32 cache_quads;
	u32 high_water_cache_runs;
	size_t high_water_cache_text_bytes;
	u32 high_water_cache_quads;
	u32 high_water_frame_instances;
	u32 high_water_frame_batches;
} RgGuiRendererBaseStats;

typedef struct RgGuiRendererBaseCachedQuad
{
	f32 x;
	f32 y;
	f32 w;
	f32 h;
	u16 u0;
	u16 v0;
	u16 u1;
	u16 v1;
} RgGuiRendererBaseCachedQuad;

typedef struct RgGuiRendererBaseCachedRun
{
	u64 hash;
	u32 scale_bits;
	size_t text_offset;
	size_t text_size;
	u32 first_quad;
	u32 quad_count;
} RgGuiRendererBaseCachedRun;

typedef struct RgGuiRendererBaseContext
{
	const RgTextFont* font;
	RgGuiRendererBaseLimits limits;
	RgGuiRendererBaseCachedRun* runs;
	u8* run_generations;
	u32* hash_slots;
	char* cache_text;
	RgGuiRendererBaseCachedQuad* cache_quads;
	RgGuiRendererBaseInstance* frame_instances;
	RgGuiRendererBaseBatch* frame_batches;
	const RgTextGlyph* const* ascii_glyphs;
	const i32* ascii_kerning;
	u32 run_count;
	u32 hash_slot_mask;
	size_t text_used;
	u32 cache_quad_count;
	u64 cache_rewrite_revision;
	u8 frame_index;
	u32 frame_instance_count;
	u32 frame_batch_count;
#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
	RgGuiRendererBasePrepared prepared;
#endif
	RgGuiRendererBaseRunPrepared run_prepared;
	RgGuiRendererBaseStats stats;
	int initialized;
} RgGuiRendererBaseContext;

/** Return the bounded capacities used when corresponding descriptor fields are zero. */
RGINLINE RgGuiRendererBaseLimits rg_gui_renderer_base_limits_default(void);

/** Return a safe upper bound for arena bytes, or SIZE_MAX on overflow. */
RGINLINE size_t rg_gui_renderer_base_memory_required(const RgGuiRendererBaseLimits* limits);

/** Initialize a fixed-font context. Arena usage is rolled back on failure. */
static int rg_gui_renderer_base_init(RgGuiRendererBaseContext* ctx, RgArena* arena, const RgGuiRendererBaseInitDesc* desc);

/** Invalidate all reusable text runs while retaining allocated storage. */
RGINLINE void rg_gui_renderer_base_clear_cache(RgGuiRendererBaseContext* ctx);

/** Reset per-frame output and counters. Cached layout remains valid. */
RGINLINE void rg_gui_renderer_base_begin_frame(RgGuiRendererBaseContext* ctx);

#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
/* Legacy preparation paths retained only as a correctness oracle for tests. */
static int rg_gui_renderer_base_prepare_draw_list(RgGuiRendererBaseContext* ctx, const RgGuiDrawList* list,
                                                  u32 overlay_start);
static int rg_gui_renderer_base_prepare_run_stream(RgGuiRendererBaseContext* ctx, const RgGuiDrawList* list,
                                                   u32 overlay_start);
RGINLINE const RgGuiRendererBasePrepared* rg_gui_renderer_base_prepared(const RgGuiRendererBaseContext* ctx);
RGINLINE const RgGuiRendererBaseRunPrepared* rg_gui_renderer_base_run_prepared(const RgGuiRendererBaseContext* ctx);
#endif

/** Get current frame, lifetime, cache, and high-water statistics. */
RGINLINE const RgGuiRendererBaseStats* rg_gui_renderer_base_stats(const RgGuiRendererBaseContext* ctx);

// =============================================================================
// IMPLEMENTATION
// =============================================================================

RGINLINE RgGuiRendererBaseLimits rg_gui_renderer_base_limits_default(void)
{
	RgGuiRendererBaseLimits limits;
	limits.max_cached_runs = 1024u;
	limits.hash_slot_count = 2048u;
	limits.text_capacity = 256u * 1024u;
	limits.max_cached_quads = 65536u;
	limits.max_frame_instances = 65536u;
	limits.max_batches = 256u;
	limits.max_frame_runs = 0u;
	return limits;
}

RGINLINE RgGuiRendererBaseLimits rg_gui_renderer_base_limits_resolve(const RgGuiRendererBaseLimits* supplied)
{
	RgGuiRendererBaseLimits result = rg_gui_renderer_base_limits_default();
	if (!supplied)
	{
		result.max_frame_runs = result.max_frame_instances;
		return result;
	}
	if (supplied->max_cached_runs) result.max_cached_runs = supplied->max_cached_runs;
	if (supplied->hash_slot_count) result.hash_slot_count = supplied->hash_slot_count;
	if (supplied->text_capacity) result.text_capacity = supplied->text_capacity;
	if (supplied->max_cached_quads) result.max_cached_quads = supplied->max_cached_quads;
	if (supplied->max_frame_instances) result.max_frame_instances = supplied->max_frame_instances;
	if (supplied->max_batches) result.max_batches = supplied->max_batches;
	result.max_frame_runs = supplied->max_frame_runs ? supplied->max_frame_runs : result.max_frame_instances;
	return result;
}

RGINLINE u32 rg_gui_renderer_base_frame_storage_count(const RgGuiRendererBaseLimits* limits)
{
#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
	return limits->max_frame_runs > limits->max_frame_instances ?
	       limits->max_frame_runs : limits->max_frame_instances;
#else
	return limits->max_frame_runs;
#endif
}

RGINLINE int rg_gui_renderer_base_size_add_array(size_t* total, size_t count, size_t element_size,
                                                 size_t alignment)
{
	if (!total || alignment == 0u || count > SIZE_MAX / element_size)
	{
		return 0;
	}
	size_t bytes = count * element_size;
	size_t padding = alignment - 1u;
	if (*total > SIZE_MAX - padding || *total + padding > SIZE_MAX - bytes)
	{
		return 0;
	}
	*total += padding + bytes;
	return 1;
}

RGINLINE size_t rg_gui_renderer_base_memory_required_internal(
    const RgGuiRendererBaseLimits* supplied, int own_lookup)
{
	RgGuiRendererBaseLimits limits = rg_gui_renderer_base_limits_resolve(supplied);
	size_t total = 0u;
	if (!rg_gui_renderer_base_size_add_array(&total, limits.max_cached_runs, sizeof(RgGuiRendererBaseCachedRun), RG_ALIGNOF(RgGuiRendererBaseCachedRun)) ||
	    !rg_gui_renderer_base_size_add_array(&total, limits.max_cached_runs, sizeof(u8), RG_ALIGNOF(u8)) ||
	    !rg_gui_renderer_base_size_add_array(&total, limits.hash_slot_count, sizeof(u32), RG_ALIGNOF(u32)) ||
	    !rg_gui_renderer_base_size_add_array(&total, limits.text_capacity, sizeof(char), RG_ALIGNOF(char)) ||
	    !rg_gui_renderer_base_size_add_array(&total, limits.max_cached_quads, sizeof(RgGuiRendererBaseCachedQuad), RG_ALIGNOF(RgGuiRendererBaseCachedQuad)) ||
	    !rg_gui_renderer_base_size_add_array(&total, rg_gui_renderer_base_frame_storage_count(&limits), sizeof(RgGuiRendererBaseInstance), RG_ALIGNOF(RgGuiRendererBaseInstance)) ||
	    !rg_gui_renderer_base_size_add_array(&total, limits.max_batches, sizeof(RgGuiRendererBaseBatch), RG_ALIGNOF(RgGuiRendererBaseBatch)) ||
	    (own_lookup && !rg_gui_renderer_base_size_add_array(
	        &total, 1u, sizeof(RgGuiTextLookup), RG_ALIGNOF(RgGuiTextLookup))))
	{
		return SIZE_MAX;
	}
	return total;
}

RGINLINE size_t rg_gui_renderer_base_memory_required(const RgGuiRendererBaseLimits* supplied)
{
	return rg_gui_renderer_base_memory_required_internal(supplied, 1);
}

RGINLINE int rg_gui_renderer_base_font_supported(const RgTextFont* font)
{
	if (!font || !font->glyphs || font->glyph_count == 0u ||
	    font->metrics.atlas_width == 0u || font->metrics.atlas_height == 0u ||
	    font->metrics.atlas_width > UINT16_MAX || font->metrics.atlas_height > UINT16_MAX)
	{
		return 0;
	}
	for (u32 i = 0u; i < font->glyph_count; i++)
	{
		const RgTextGlyph* glyph = &font->glyphs[i];
		if (glyph->x < 0 || glyph->y < 0 || glyph->w < 0 || glyph->h < 0 ||
		    (u32)glyph->x > font->metrics.atlas_width ||
		    (u32)glyph->y > font->metrics.atlas_height ||
		    (u32)glyph->w > font->metrics.atlas_width - (u32)glyph->x ||
		    (u32)glyph->h > font->metrics.atlas_height - (u32)glyph->y)
		{
			return 0;
		}
	}
	return 1;
}

static int rg_gui_renderer_base_init(RgGuiRendererBaseContext* ctx, RgArena* arena, const RgGuiRendererBaseInitDesc* desc)
{
	if (!ctx || !arena || !arena->memory || !desc || !rg_gui_renderer_base_font_supported(desc->font))
	{
		if (ctx) memset(ctx, 0, sizeof(*ctx));
		return 0;
	}

	size_t arena_used = arena->used;
	RgGuiRendererBaseLimits limits = rg_gui_renderer_base_limits_resolve(&desc->limits);
	const RgGuiTextLookup* lookup = desc->text_lookup;
	int own_lookup = !lookup || lookup->font != desc->font;
	if (rg_gui_renderer_base_memory_required_internal(&limits, own_lookup) == SIZE_MAX)
	{
		memset(ctx, 0, sizeof(*ctx));
		return 0;
	}

	memset(ctx, 0, sizeof(*ctx));
	ctx->font = desc->font;
	ctx->limits = limits;
	ctx->hash_slot_mask = (limits.hash_slot_count & (limits.hash_slot_count - 1u)) == 0u ? limits.hash_slot_count - 1u : UINT32_MAX;
	ctx->runs = (RgGuiRendererBaseCachedRun*)rg_arena_alloc_array(
	    arena, sizeof(RgGuiRendererBaseCachedRun), limits.max_cached_runs, RG_ALIGNOF(RgGuiRendererBaseCachedRun));
	ctx->run_generations = (u8*)rg_arena_alloc_array(
	    arena, sizeof(u8), limits.max_cached_runs, RG_ALIGNOF(u8));
	ctx->hash_slots = (u32*)rg_arena_alloc_array(
	    arena, sizeof(u32), limits.hash_slot_count, RG_ALIGNOF(u32));
	ctx->cache_text = (char*)rg_arena_alloc_array(
	    arena, sizeof(char), limits.text_capacity, RG_ALIGNOF(char));
	ctx->cache_quads = (RgGuiRendererBaseCachedQuad*)rg_arena_alloc_array(
	    arena, sizeof(RgGuiRendererBaseCachedQuad), limits.max_cached_quads, RG_ALIGNOF(RgGuiRendererBaseCachedQuad));
	ctx->frame_instances = (RgGuiRendererBaseInstance*)rg_arena_alloc_array(
	    arena, sizeof(RgGuiRendererBaseInstance), rg_gui_renderer_base_frame_storage_count(&limits), RG_ALIGNOF(RgGuiRendererBaseInstance));
	ctx->frame_batches = (RgGuiRendererBaseBatch*)rg_arena_alloc_array(
	    arena, sizeof(RgGuiRendererBaseBatch), limits.max_batches, RG_ALIGNOF(RgGuiRendererBaseBatch));
	if (own_lookup)
	{
		RgGuiTextLookup* owned = (RgGuiTextLookup*)rg_arena_alloc_array(
		    arena, sizeof(RgGuiTextLookup), 1u, RG_ALIGNOF(RgGuiTextLookup));
		lookup = owned && rg_gui_text_lookup_init(owned, desc->font) ? owned : NULL;
	}
	ctx->ascii_glyphs = lookup ? lookup->ascii_glyphs : NULL;
	ctx->ascii_kerning = lookup ? lookup->ascii_kerning : NULL;

	if (!ctx->runs || !ctx->run_generations || !ctx->hash_slots ||
	    !ctx->cache_text || !ctx->cache_quads ||
	    !ctx->frame_instances || !ctx->frame_batches || !ctx->ascii_glyphs || !ctx->ascii_kerning)
	{
		arena->used = arena_used;
		memset(ctx, 0, sizeof(*ctx));
		return 0;
	}

	memset(ctx->hash_slots, 0, sizeof(u32) * limits.hash_slot_count);

#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
	ctx->prepared.instances = ctx->frame_instances;
	ctx->prepared.batches = ctx->frame_batches;
#endif
	ctx->run_prepared.runs = (const RgGuiRendererBaseRunInstance*)ctx->frame_instances;
	ctx->run_prepared.batches = ctx->frame_batches;
	ctx->initialized = 1;
	return 1;
}

RGINLINE void rg_gui_renderer_base_sync_cache_stats(RgGuiRendererBaseContext* ctx)
{
	ctx->stats.cache_runs = ctx->run_count;
	ctx->stats.cache_text_bytes = ctx->text_used;
	ctx->stats.cache_quads = ctx->cache_quad_count;
}

RGINLINE void rg_gui_renderer_base_clear_cache_internal(RgGuiRendererBaseContext* ctx, int count_reset)
{
	memset(ctx->hash_slots, 0, sizeof(u32) * ctx->limits.hash_slot_count);
	ctx->run_count = 0u;
	ctx->text_used = 0u;
	ctx->cache_quad_count = 0u;
	ctx->cache_rewrite_revision++;
	if (count_reset)
	{
		ctx->stats.frame_cache_resets++;
		ctx->stats.total_cache_resets++;
	}
	rg_gui_renderer_base_sync_cache_stats(ctx);
}

RGINLINE void rg_gui_renderer_base_clear_cache(RgGuiRendererBaseContext* ctx)
{
	if (ctx && ctx->initialized)
	{
		rg_gui_renderer_base_clear_cache_internal(ctx, 1);
	}
}

RGINLINE void rg_gui_renderer_base_begin_frame(RgGuiRendererBaseContext* ctx)
{
	if (!ctx || !ctx->initialized)
	{
		return;
	}
	ctx->frame_instance_count = 0u;
	ctx->frame_batch_count = 0u;
#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
	ctx->prepared.instance_count = 0u;
	ctx->prepared.batch_count = 0u;
#endif
	ctx->run_prepared.run_count = 0u;
	ctx->run_prepared.glyph_count = 0u;
	ctx->run_prepared.batch_count = 0u;
	ctx->stats.diagnostic_flags = RG_GUI_RENDERER_BASE_DIAGNOSTIC_NONE;
	ctx->stats.frame_text_runs = 0u;
	ctx->stats.frame_glyphs = 0u;
	ctx->stats.frame_laid_out_glyphs = 0u;
	ctx->stats.frame_reused_glyphs = 0u;
	ctx->stats.frame_instances = 0u;
	ctx->stats.frame_batches = 0u;
	ctx->stats.frame_cache_hits = 0u;
	ctx->stats.frame_cache_misses = 0u;
	ctx->stats.frame_cache_resets = 0u;
	ctx->stats.frame_cache_compactions = 0u;
	ctx->stats.frame_cache_evictions = 0u;
	ctx->stats.frame_cache_bypasses = 0u;
	ctx->stats.frame_dropped_runs = 0u;
	ctx->stats.frame_dropped_glyphs = 0u;
	ctx->stats.frame_upload_bytes = 0u;
	ctx->frame_index++;
	rg_gui_renderer_base_sync_cache_stats(ctx);
}

RGINLINE const RgTextGlyph* rg_gui_renderer_base_find_glyph(const RgGuiRendererBaseContext* ctx, u32 cp)
{
	return cp < 128u ? ctx->ascii_glyphs[cp] : rg_text_find_glyph(ctx->font, cp);
}

RGINLINE u32 rg_gui_renderer_base_u32_add_saturate(u32 left, u32 right)
{
	return right > UINT32_MAX - left ? UINT32_MAX : left + right;
}

RGINLINE u64 rg_gui_renderer_base_u64_add_saturate(u64 left, u64 right)
{
	return right > UINT64_MAX - left ? UINT64_MAX : left + right;
}

RGINLINE i32 rg_gui_renderer_base_find_kerning(const RgGuiRendererBaseContext* ctx, u32 left, u32 right)
{
	if (left < 128u && right < 128u)
	{
		return ctx->ascii_kerning[left * 128u + right];
	}
	return rg_text_find_kerning(ctx->font, left, right);
}

RGINLINE u32 rg_gui_renderer_base_count_quads(const RgGuiRendererBaseContext* ctx, const char* text, size_t text_size)
{
	size_t offset = 0u;
	u32 count = 0u;
	while (offset < text_size)
	{
		u32 cp = rg_text_decode_utf8(text, text_size, &offset);
		if (cp == '\r' || cp == '\n')
		{
			if (cp == '\r' && offset < text_size && text[offset] == '\n') offset++;
			continue;
		}
		const RgTextGlyph* glyph = rg_gui_renderer_base_find_glyph(ctx, cp);
		if (glyph && glyph->w > 0 && glyph->h > 0)
		{
			if (count == UINT32_MAX) return UINT32_MAX;
			count++;
		}
	}
	return count;
}

RGINLINE u32 rg_gui_renderer_base_build_cached(const RgGuiRendererBaseContext* ctx, const char* text,
                                               size_t text_size, f32 scale,
                                               RgGuiRendererBaseCachedQuad* output, u32 capacity)
{
	if (!text || text_size == 0u || scale == 0.0f)
	{
		return 0u;
	}

	size_t offset = 0u;
	f32 line_y = 0.0f;
	f32 pen_x = 0.0f;
	u32 count = 0u;
	const RgTextGlyph* previous = NULL;
	while (offset < text_size && count < capacity)
	{
		u32 cp = rg_text_decode_utf8(text, text_size, &offset);
		if (cp == '\r' || cp == '\n')
		{
			if (cp == '\r' && offset < text_size && text[offset] == '\n') offset++;
			line_y += (f32)ctx->font->metrics.line_height * scale;
			pen_x = 0.0f;
			previous = NULL;
			continue;
		}

		const RgTextGlyph* glyph = rg_gui_renderer_base_find_glyph(ctx, cp);
		if (!glyph)
		{
			previous = NULL;
			continue;
		}
		if (previous)
		{
			pen_x += (f32)rg_gui_renderer_base_find_kerning(ctx, previous->codepoint, glyph->codepoint) * scale;
		}
		if (glyph->w > 0 && glyph->h > 0)
		{
			RgGuiRendererBaseCachedQuad* quad = &output[count++];
			quad->x = pen_x + (f32)glyph->x_offset * scale;
			quad->y = line_y + (f32)glyph->y_offset * scale;
			quad->w = (f32)glyph->w * scale;
			quad->h = (f32)glyph->h * scale;
			quad->u0 = (u16)glyph->x;
			quad->v0 = (u16)glyph->y;
			quad->u1 = (u16)(glyph->x + glyph->w);
			quad->v1 = (u16)(glyph->y + glyph->h);
		}

		pen_x += (f32)glyph->x_advance * scale;
		previous = glyph;
	}
	return count;
}

#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
RGINLINE u32 rg_gui_renderer_base_build_instances(const RgGuiRendererBaseContext* ctx, const char* text,
                                                  size_t text_size, f32 scale, rg_vec2 position,
                                                  u32 color, RgGuiRendererBaseInstance* output,
                                                  u32 capacity)
{
	if (!text || text_size == 0u || scale == 0.0f)
	{
		return 0u;
	}

	size_t offset = 0u;
	f32 line_y = 0.0f;
	f32 pen_x = 0.0f;
	u32 count = 0u;
	const RgTextGlyph* previous = NULL;
	while (offset < text_size && count < capacity)
	{
		u32 cp = rg_text_decode_utf8(text, text_size, &offset);
		if (cp == '\r' || cp == '\n')
		{
			if (cp == '\r' && offset < text_size && text[offset] == '\n') offset++;
			line_y += (f32)ctx->font->metrics.line_height * scale;
			pen_x = 0.0f;
			previous = NULL;
			continue;
		}

		const RgTextGlyph* glyph = rg_gui_renderer_base_find_glyph(ctx, cp);
		if (!glyph)
		{
			previous = NULL;
			continue;
		}
		if (previous)
		{
			pen_x += (f32)rg_gui_renderer_base_find_kerning(ctx, previous->codepoint, glyph->codepoint) * scale;
		}
		if (glyph->w > 0 && glyph->h > 0)
		{
			RgGuiRendererBaseInstance* instance = &output[count++];
			instance->x = position.x + pen_x + (f32)glyph->x_offset * scale;
			instance->y = position.y + line_y + (f32)glyph->y_offset * scale;
			instance->w = (f32)glyph->w * scale;
			instance->h = (f32)glyph->h * scale;
			instance->u0 = (u16)glyph->x;
			instance->v0 = (u16)glyph->y;
			instance->u1 = (u16)(glyph->x + glyph->w);
			instance->v1 = (u16)(glyph->y + glyph->h);
			instance->color = color;
			instance->padding = 0u;
		}

		pen_x += (f32)glyph->x_advance * scale;
		previous = glyph;
	}
	return count;
}
#endif

RGINLINE u64 rg_gui_renderer_base_key_hash(const char* text, size_t text_size,
                                           u32 scale_bits, uintptr_t cache_identity)
{
	if (cache_identity)
	{
		u64 hash = (u64)cache_identity ^ ((u64)scale_bits << 32u);
		hash ^= hash >> 33u;
		hash *= 0xff51afd7ed558ccdull;
		hash ^= hash >> 33u;
		hash *= 0xc4ceb9fe1a85ec53ull;
		hash ^= hash >> 33u;
		return hash;
	}
	return rg_hash_bytes(text, text_size, (u64)scale_bits);
}

RGINLINE u32 rg_gui_renderer_base_float_bits(f32 value)
{
	u32 bits;
	memcpy(&bits, &value, sizeof(bits));
	return bits;
}

RGINLINE int rg_gui_renderer_base_run_equal(const RgGuiRendererBaseContext* ctx, const RgGuiRendererBaseCachedRun* run,
                                            u64 hash, const char* text, size_t text_size,
                                            u32 scale_bits, uintptr_t cache_identity)
{
	if (run->hash != hash || run->scale_bits != scale_bits) return 0;
	if (cache_identity)
	{
		return run->text_size == RG_GUI_RENDERER_BASE_IDENTITY_TEXT_SIZE &&
		       run->text_offset == (size_t)cache_identity;
	}
	return run->text_size != RG_GUI_RENDERER_BASE_IDENTITY_TEXT_SIZE &&
	       run->text_size == text_size &&
	       (text_size == 0u || memcmp(ctx->cache_text + run->text_offset, text, text_size) == 0);
}

RGINLINE u32 rg_gui_renderer_base_find_slot(const RgGuiRendererBaseContext* ctx, u64 hash,
                                            const char* text, size_t text_size, u32 scale_bits,
                                            uintptr_t cache_identity,
                                            u32* out_run_index)
{
	u32 start = ctx->hash_slot_mask != UINT32_MAX ? (u32)hash & ctx->hash_slot_mask : (u32)(hash % ctx->limits.hash_slot_count);
	for (u32 probe = 0u; probe < ctx->limits.hash_slot_count; probe++)
	{
		u32 slot = ctx->hash_slot_mask != UINT32_MAX ? (start + probe) & ctx->hash_slot_mask : (u32)(((u64)start + (u64)probe) % (u64)ctx->limits.hash_slot_count);
		u32 stored = ctx->hash_slots[slot];
		if (stored == 0u)
		{
			if (out_run_index) *out_run_index = UINT32_MAX;
			return slot;
		}
		u32 run_index = stored - 1u;
		if (rg_gui_renderer_base_run_equal(ctx, &ctx->runs[run_index], hash, text, text_size,
		                                   scale_bits, cache_identity))
		{
			if (out_run_index) *out_run_index = run_index;
			return slot;
		}
	}
	if (out_run_index) *out_run_index = UINT32_MAX;
	return UINT32_MAX;
}

#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
RGINLINE void rg_gui_renderer_base_compact_cache(RgGuiRendererBaseContext* ctx)
{
	u32 old_count = ctx->run_count;
	u32 kept_count = 0u;
	size_t kept_text = 0u;
	u32 kept_quads = 0u;

	for (u32 i = 0u; i < old_count; i++)
	{
		RgGuiRendererBaseCachedRun run = ctx->runs[i];
		u8 generation = ctx->run_generations[i];
		if (generation != ctx->frame_index &&
		    generation != (u8)(ctx->frame_index - 1u))
		{
			continue;
		}

		int identity_key = run.text_size == RG_GUI_RENDERER_BASE_IDENTITY_TEXT_SIZE;
		if (!identity_key && run.text_size)
		{
			memmove(ctx->cache_text + kept_text,
			        ctx->cache_text + run.text_offset, run.text_size);
		}
		if (run.quad_count)
		{
			memmove(ctx->cache_quads + kept_quads,
			        ctx->cache_quads + run.first_quad,
			        sizeof(RgGuiRendererBaseCachedQuad) * run.quad_count);
		}
		if (!identity_key) run.text_offset = kept_text;
		run.first_quad = kept_quads;
		ctx->runs[kept_count++] = run;
		ctx->run_generations[kept_count - 1u] = generation;
		if (!identity_key) kept_text += run.text_size;
		kept_quads += run.quad_count;
	}

	memset(ctx->hash_slots, 0, sizeof(u32) * ctx->limits.hash_slot_count);
	for (u32 i = 0u; i < kept_count; i++)
	{
		u32 start = ctx->hash_slot_mask != UINT32_MAX ? (u32)ctx->runs[i].hash & ctx->hash_slot_mask : (u32)(ctx->runs[i].hash % ctx->limits.hash_slot_count);
		for (u32 probe = 0u; probe < ctx->limits.hash_slot_count; probe++)
		{
			u32 slot = ctx->hash_slot_mask != UINT32_MAX ? (start + probe) & ctx->hash_slot_mask : (u32)(((u64)start + (u64)probe) % (u64)ctx->limits.hash_slot_count);
			if (ctx->hash_slots[slot] == 0u)
			{
				ctx->hash_slots[slot] = i + 1u;
				break;
			}
		}
	}

	ctx->run_count = kept_count;
	ctx->text_used = kept_text;
	ctx->cache_quad_count = kept_quads;
	ctx->cache_rewrite_revision++;
	ctx->stats.frame_cache_compactions++;
	ctx->stats.total_cache_compactions++;
	ctx->stats.frame_cache_evictions = rg_gui_renderer_base_u32_add_saturate(
	    ctx->stats.frame_cache_evictions, old_count - kept_count);
	ctx->stats.total_cache_evictions = rg_gui_renderer_base_u64_add_saturate(
	    ctx->stats.total_cache_evictions, old_count - kept_count);
	rg_gui_renderer_base_sync_cache_stats(ctx);
}
#endif

typedef enum RgGuiRendererBaseRunResult
{
	RG_GUI_RENDERER_BASE_RUN_CACHED,
	RG_GUI_RENDERER_BASE_RUN_BYPASS
} RgGuiRendererBaseRunResult;

#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
static RgGuiRendererBaseRunResult rg_gui_renderer_base_get_run(RgGuiRendererBaseContext* ctx, const char* text,
                                                               size_t text_size, f32 scale, uintptr_t cache_identity,
                                                               const RgGuiRendererBaseCachedRun** out_run,
                                                               u32* out_quad_count)
{
	u32 scale_bits = rg_gui_renderer_base_float_bits(scale);
	u64 hash = rg_gui_renderer_base_key_hash(text, text_size, scale_bits, cache_identity);
	u32 run_index = UINT32_MAX;
	u32 slot = rg_gui_renderer_base_find_slot(ctx, hash, text, text_size, scale_bits,
	                                          cache_identity, &run_index);
	if (run_index != UINT32_MAX)
	{
		ctx->run_generations[run_index] = ctx->frame_index;
		ctx->stats.frame_cache_hits++;
		ctx->stats.total_cache_hits++;
		ctx->stats.frame_reused_glyphs = rg_gui_renderer_base_u32_add_saturate(
		    ctx->stats.frame_reused_glyphs, ctx->runs[run_index].quad_count);
		ctx->stats.total_reused_glyphs = rg_gui_renderer_base_u64_add_saturate(
		    ctx->stats.total_reused_glyphs, ctx->runs[run_index].quad_count);
		*out_run = &ctx->runs[run_index];
		*out_quad_count = ctx->runs[run_index].quad_count;
		return RG_GUI_RENDERER_BASE_RUN_CACHED;
	}

	ctx->stats.frame_cache_misses++;
	ctx->stats.total_cache_misses++;
	if (cache_identity && scale != 0.0f)
	{
		text_size = strlen(text);
	}
	u32 quad_count = scale == 0.0f ? 0u : rg_gui_renderer_base_count_quads(ctx, text, text_size);
	ctx->stats.frame_laid_out_glyphs = rg_gui_renderer_base_u32_add_saturate(
	    ctx->stats.frame_laid_out_glyphs, quad_count);
	ctx->stats.total_laid_out_glyphs = rg_gui_renderer_base_u64_add_saturate(
	    ctx->stats.total_laid_out_glyphs, quad_count);
	*out_quad_count = quad_count;

	size_t stored_text_size = cache_identity ? 0u : text_size;
	if (stored_text_size > ctx->limits.text_capacity ||
	    quad_count > ctx->limits.max_cached_quads)
	{
		ctx->stats.frame_cache_bypasses++;
		ctx->stats.total_cache_bypasses++;
		*out_run = NULL;
		return RG_GUI_RENDERER_BASE_RUN_BYPASS;
	}

	int needs_compaction = ctx->run_count >= ctx->limits.max_cached_runs ||
	                       stored_text_size > ctx->limits.text_capacity - ctx->text_used ||
	                       quad_count > ctx->limits.max_cached_quads - ctx->cache_quad_count ||
	                       slot == UINT32_MAX;
	if (needs_compaction)
	{
		rg_gui_renderer_base_compact_cache(ctx);
		slot = rg_gui_renderer_base_find_slot(ctx, hash, text, text_size, scale_bits,
		                                      cache_identity, &run_index);
	}

	if (slot == UINT32_MAX || ctx->run_count >= ctx->limits.max_cached_runs ||
	    stored_text_size > ctx->limits.text_capacity - ctx->text_used ||
	    quad_count > ctx->limits.max_cached_quads - ctx->cache_quad_count)
	{
		ctx->stats.frame_cache_bypasses++;
		ctx->stats.total_cache_bypasses++;
		*out_run = NULL;
		return RG_GUI_RENDERER_BASE_RUN_BYPASS;
	}

	u32 new_index = ctx->run_count;
	RgGuiRendererBaseCachedRun* run = &ctx->runs[new_index];
	run->hash = hash;
	ctx->run_generations[new_index] = ctx->frame_index;
	run->scale_bits = scale_bits;
	run->text_offset = cache_identity ? (size_t)cache_identity : ctx->text_used;
	run->text_size = cache_identity ? RG_GUI_RENDERER_BASE_IDENTITY_TEXT_SIZE : text_size;
	run->first_quad = ctx->cache_quad_count;
	run->quad_count = quad_count;
	if (stored_text_size) memcpy(ctx->cache_text + ctx->text_used, text, stored_text_size);
	u32 built = rg_gui_renderer_base_build_cached(ctx, text, text_size, scale,
	                                              ctx->cache_quads + ctx->cache_quad_count, quad_count);
	if (built != quad_count)
	{
		ctx->stats.frame_cache_bypasses++;
		ctx->stats.total_cache_bypasses++;
		*out_run = NULL;
		return RG_GUI_RENDERER_BASE_RUN_BYPASS;
	}

	ctx->hash_slots[slot] = new_index + 1u;
	ctx->run_count++;
	ctx->text_used += stored_text_size;
	ctx->cache_quad_count += quad_count;
	rg_gui_renderer_base_sync_cache_stats(ctx);
	if (ctx->run_count > ctx->stats.high_water_cache_runs)
		ctx->stats.high_water_cache_runs = ctx->run_count;
	if (ctx->text_used > ctx->stats.high_water_cache_text_bytes)
		ctx->stats.high_water_cache_text_bytes = ctx->text_used;
	if (ctx->cache_quad_count > ctx->stats.high_water_cache_quads)
		ctx->stats.high_water_cache_quads = ctx->cache_quad_count;

	*out_run = run;
	return RG_GUI_RENDERER_BASE_RUN_CACHED;
}
#endif

RGINLINE f32 rg_gui_renderer_base_clamp_color(f32 value)
{
	if (!(value > 0.0f)) return 0.0f;
	return value < 1.0f ? value : 1.0f;
}

RGINLINE u32 rg_gui_renderer_base_pack_color(rg_vec4 color)
{
	u32 r = (u32)(rg_gui_renderer_base_clamp_color(color.x) * 255.0f + 0.5f);
	u32 g = (u32)(rg_gui_renderer_base_clamp_color(color.y) * 255.0f + 0.5f);
	u32 b = (u32)(rg_gui_renderer_base_clamp_color(color.z) * 255.0f + 0.5f);
	u32 a = (u32)(rg_gui_renderer_base_clamp_color(color.w) * 255.0f + 0.5f);
	return r | (g << 8u) | (b << 16u) | (a << 24u);
}

RGINLINE RgGuiRect rg_gui_renderer_base_clip_intersect(RgGuiRect a, RgGuiRect b)
{
	f32 x0 = a.x > b.x ? a.x : b.x;
	f32 y0 = a.y > b.y ? a.y : b.y;
	f32 ax1 = a.x + a.w;
	f32 ay1 = a.y + a.h;
	f32 bx1 = b.x + b.w;
	f32 by1 = b.y + b.h;
	f32 x1 = ax1 < bx1 ? ax1 : bx1;
	f32 y1 = ay1 < by1 ? ay1 : by1;
	RgGuiRect result;
	result.x = x0;
	result.y = y0;
	result.w = x1 > x0 ? x1 - x0 : 0.0f;
	result.h = y1 > y0 ? y1 - y0 : 0.0f;
	return result;
}

RGINLINE int rg_gui_renderer_base_batch_matches(const RgGuiRendererBaseBatch* batch, int clip_enabled,
                                                const RgGuiRect* clip)
{
	return batch->clip_enabled == (u32)clip_enabled &&
	       (!clip_enabled || memcmp(&batch->clip, clip, sizeof(*clip)) == 0);
}

RGINLINE void rg_gui_renderer_base_drop_run(RgGuiRendererBaseContext* ctx, u32 glyph_count,
                                            RgGuiRendererBaseDiagnosticFlags flag)
{
	ctx->stats.diagnostic_flags |= flag;
	ctx->stats.frame_dropped_runs = rg_gui_renderer_base_u32_add_saturate(
	    ctx->stats.frame_dropped_runs, 1u);
	ctx->stats.frame_dropped_glyphs = rg_gui_renderer_base_u32_add_saturate(
	    ctx->stats.frame_dropped_glyphs, glyph_count);
	ctx->stats.total_dropped_runs = rg_gui_renderer_base_u64_add_saturate(
	    ctx->stats.total_dropped_runs, 1u);
	ctx->stats.total_dropped_glyphs = rg_gui_renderer_base_u64_add_saturate(
	    ctx->stats.total_dropped_glyphs, glyph_count);
}

#if defined(RG_GUI_RENDERER_TEST_REFERENCE)
RGINLINE const RgGuiRendererBaseCachedRun* rg_gui_renderer_base_lookup_run(
    const RgGuiRendererBaseContext* ctx, const char* text, size_t text_size, f32 scale,
    uintptr_t cache_identity)
{
	u32 scale_bits = rg_gui_renderer_base_float_bits(scale);
	u64 hash = rg_gui_renderer_base_key_hash(text, text_size, scale_bits, cache_identity);
	u32 run_index = UINT32_MAX;
	rg_gui_renderer_base_find_slot(ctx, hash, text, text_size, scale_bits, cache_identity, &run_index);
	return run_index != UINT32_MAX ? &ctx->runs[run_index] : NULL;
}

static int rg_gui_renderer_base_prepare_run_stream(RgGuiRendererBaseContext* ctx, const RgGuiDrawList* list,
                                                   u32 overlay_start)
{
	if (!ctx || !ctx->initialized || !list || (list->count && !list->cmds))
	{
		if (ctx && ctx->initialized)
			ctx->stats.diagnostic_flags |= RG_GUI_RENDERER_BASE_DIAGNOSTIC_INVALID_ARGUMENT;
		return 0;
	}
	if (overlay_start > list->count) overlay_start = list->count;

	// Stabilize cached offsets first: compaction can move every retained run.
	for (u32 i = 0u; i < list->count; i++)
	{
		const RgGuiDrawCmd* cmd = &list->cmds[i];
		if (cmd->type != RG_GUI_CMD_TEXT) continue;
		const char* text = cmd->data.text.text ? cmd->data.text.text : "";
		uintptr_t cache_identity = cmd->data.text.cache_identity;
		size_t text_size = cache_identity ? 0u : strlen(text);
		const RgGuiRendererBaseCachedRun* run = NULL;
		u32 quad_count = 0u;
		RgGuiRendererBaseRunResult result = rg_gui_renderer_base_get_run(
		    ctx, text, text_size, cmd->data.text.scale, cache_identity,
		    &run, &quad_count);
		ctx->stats.frame_text_runs++;
		ctx->stats.frame_glyphs = rg_gui_renderer_base_u32_add_saturate(
		    ctx->stats.frame_glyphs, quad_count);
		if (result != RG_GUI_RENDERER_BASE_RUN_CACHED && quad_count)
		{
			rg_gui_renderer_base_drop_run(ctx, quad_count, RG_GUI_RENDERER_BASE_DIAGNOSTIC_RUN_CACHE_REQUIRED);
		}
	}

	RgGuiRect clip_stack[RG_GUI_RENDERER_BASE_CLIP_STACK_MAX];
	u32 clip_depth = 0u;
	u32 ignored_pushes = 0u;
	int force_new_batch = 0;
	RgGuiRect current_clip;
	memset(&current_clip, 0, sizeof(current_clip));
	int clip_enabled = 0;
	RgGuiRendererBaseRunInstance* frame_runs = (RgGuiRendererBaseRunInstance*)ctx->frame_instances;
	u32 output_glyph_count = 0u;

	for (u32 i = 0u; i < list->count; i++)
	{
		if (i == overlay_start)
		{
			clip_depth = 0u;
			ignored_pushes = 0u;
			clip_enabled = 0;
			force_new_batch = 1;
			memset(&current_clip, 0, sizeof(current_clip));
		}
		const RgGuiDrawCmd* cmd = &list->cmds[i];
		if (cmd->type == RG_GUI_CMD_CLIP_PUSH)
		{
			if (clip_depth >= RG_GUI_RENDERER_BASE_CLIP_STACK_MAX)
			{
				ignored_pushes++;
				ctx->stats.diagnostic_flags |= RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_OVERFLOW;
				continue;
			}
			current_clip = clip_enabled ? rg_gui_renderer_base_clip_intersect(current_clip, cmd->data.clip.rect) : cmd->data.clip.rect;
			clip_stack[clip_depth++] = current_clip;
			clip_enabled = 1;
			continue;
		}
		if (cmd->type == RG_GUI_CMD_CLIP_POP)
		{
			if (ignored_pushes)
			{
				ignored_pushes--;
			}
			else if (clip_depth)
			{
				clip_depth--;
				clip_enabled = clip_depth != 0u;
				if (clip_enabled) current_clip = clip_stack[clip_depth - 1u];
			}
			else
			{
				ctx->stats.diagnostic_flags |= RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_UNDERFLOW;
			}
			continue;
		}
		if (cmd->type != RG_GUI_CMD_TEXT) continue;

		const char* text = cmd->data.text.text ? cmd->data.text.text : "";
		uintptr_t cache_identity = cmd->data.text.cache_identity;
		size_t text_size = cache_identity ? 0u : strlen(text);
		const RgGuiRendererBaseCachedRun* run = rg_gui_renderer_base_lookup_run(
		    ctx, text, text_size, cmd->data.text.scale, cache_identity);
		if (!run || run->quad_count == 0u) continue;
		if (ctx->frame_instance_count >= ctx->limits.max_frame_instances ||
		    run->quad_count > ctx->limits.max_frame_instances - output_glyph_count)
		{
			rg_gui_renderer_base_drop_run(ctx, run->quad_count,
			                              RG_GUI_RENDERER_BASE_DIAGNOSTIC_INSTANCE_CAPACITY);
			continue;
		}

		RgGuiRendererBaseBatch* batch = ctx->frame_batch_count ? &ctx->frame_batches[ctx->frame_batch_count - 1u] : NULL;
		if (!batch || force_new_batch ||
		    !rg_gui_renderer_base_batch_matches(batch, clip_enabled, &current_clip))
		{
			if (ctx->frame_batch_count >= ctx->limits.max_batches)
			{
				rg_gui_renderer_base_drop_run(ctx, run->quad_count,
				                              RG_GUI_RENDERER_BASE_DIAGNOSTIC_BATCH_CAPACITY);
				continue;
			}
			batch = &ctx->frame_batches[ctx->frame_batch_count++];
			memset(batch, 0, sizeof(*batch));
			batch->clip_enabled = (u32)clip_enabled;
			if (clip_enabled) batch->clip = current_clip;
			batch->first_instance = ctx->frame_instance_count;
		}
		force_new_batch = 0;

		RgGuiRendererBaseRunInstance* output = &frame_runs[ctx->frame_instance_count++];
		output->first_cached_quad = run->first_quad;
		output->quad_count = run->quad_count;
		output->x = cmd->data.text.pos.x;
		output->y = cmd->data.text.pos.y;
		output->color = rg_gui_renderer_base_pack_color(cmd->data.text.color);
		output->first_output_instance = output_glyph_count;
		output->padding[0] = i;
		output->padding[1] = 0u;
		output_glyph_count += run->quad_count;
		batch->instance_count++;
	}

	ctx->run_prepared.run_count = ctx->frame_instance_count;
	ctx->run_prepared.glyph_count = output_glyph_count;
	ctx->run_prepared.batch_count = ctx->frame_batch_count;
	ctx->stats.frame_instances = ctx->frame_instance_count;
	ctx->stats.frame_batches = ctx->frame_batch_count;
	ctx->stats.frame_upload_bytes =
	    (size_t)ctx->frame_instance_count * sizeof(RgGuiRendererBaseRunInstance);
	if (ctx->frame_instance_count > ctx->stats.high_water_frame_instances)
		ctx->stats.high_water_frame_instances = ctx->frame_instance_count;
	if (ctx->frame_batch_count > ctx->stats.high_water_frame_batches)
		ctx->stats.high_water_frame_batches = ctx->frame_batch_count;
	return 1;
}

static int rg_gui_renderer_base_prepare_draw_list(RgGuiRendererBaseContext* ctx, const RgGuiDrawList* list,
                                                  u32 overlay_start)
{
	if (!ctx || !ctx->initialized || !list || (list->count && !list->cmds))
	{
		if (ctx && ctx->initialized) ctx->stats.diagnostic_flags |= RG_GUI_RENDERER_BASE_DIAGNOSTIC_INVALID_ARGUMENT;
		return 0;
	}
	if (overlay_start > list->count) overlay_start = list->count;

	RgGuiRect clip_stack[RG_GUI_RENDERER_BASE_CLIP_STACK_MAX];
	u32 clip_depth = 0u;
	u32 ignored_pushes = 0u;
	int force_new_batch = 0;
	RgGuiRect current_clip;
	memset(&current_clip, 0, sizeof(current_clip));
	int clip_enabled = 0;

	for (u32 i = 0u; i < list->count; i++)
	{
		if (i == overlay_start)
		{
			clip_depth = 0u;
			ignored_pushes = 0u;
			clip_enabled = 0;
			force_new_batch = 1;
			memset(&current_clip, 0, sizeof(current_clip));
		}

		const RgGuiDrawCmd* cmd = &list->cmds[i];
		if (cmd->type == RG_GUI_CMD_CLIP_PUSH)
		{
			if (clip_depth >= RG_GUI_RENDERER_BASE_CLIP_STACK_MAX)
			{
				ignored_pushes++;
				ctx->stats.diagnostic_flags |= RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_OVERFLOW;
				continue;
			}
			current_clip = clip_enabled ? rg_gui_renderer_base_clip_intersect(current_clip, cmd->data.clip.rect)
			                            : cmd->data.clip.rect;
			clip_stack[clip_depth++] = current_clip;
			clip_enabled = 1;
			continue;
		}
		if (cmd->type == RG_GUI_CMD_CLIP_POP)
		{
			if (ignored_pushes)
			{
				ignored_pushes--;
			}
			else if (clip_depth)
			{
				clip_depth--;
				clip_enabled = clip_depth != 0u;
				if (clip_enabled) current_clip = clip_stack[clip_depth - 1u];
			}
			else
			{
				ctx->stats.diagnostic_flags |= RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_UNDERFLOW;
			}
			continue;
		}
		if (cmd->type != RG_GUI_CMD_TEXT)
		{
			continue;
		}

		const char* text = cmd->data.text.text ? cmd->data.text.text : "";
		uintptr_t cache_identity = cmd->data.text.cache_identity;
		size_t text_size = cache_identity ? 0u : strlen(text);
		const RgGuiRendererBaseCachedRun* run = NULL;
		u32 quad_count = 0u;
		RgGuiRendererBaseRunResult run_result = rg_gui_renderer_base_get_run(ctx, text, text_size,
		                                                                     cmd->data.text.scale, cache_identity,
		                                                                     &run, &quad_count);
		ctx->stats.frame_text_runs++;
		ctx->stats.frame_glyphs = rg_gui_renderer_base_u32_add_saturate(ctx->stats.frame_glyphs, quad_count);
		if (quad_count == 0u)
		{
			continue;
		}

		if (quad_count > ctx->limits.max_frame_instances - ctx->frame_instance_count)
		{
			rg_gui_renderer_base_drop_run(ctx, quad_count, RG_GUI_RENDERER_BASE_DIAGNOSTIC_INSTANCE_CAPACITY);
			continue;
		}

		RgGuiRendererBaseBatch* batch = ctx->frame_batch_count ? &ctx->frame_batches[ctx->frame_batch_count - 1u] : NULL;
		if (!batch || force_new_batch ||
		    !rg_gui_renderer_base_batch_matches(batch, clip_enabled, &current_clip))
		{
			if (ctx->frame_batch_count >= ctx->limits.max_batches)
			{
				rg_gui_renderer_base_drop_run(ctx, quad_count, RG_GUI_RENDERER_BASE_DIAGNOSTIC_BATCH_CAPACITY);
				continue;
			}
			batch = &ctx->frame_batches[ctx->frame_batch_count++];
			memset(batch, 0, sizeof(*batch));
			batch->clip_enabled = (u32)clip_enabled;
			if (clip_enabled) batch->clip = current_clip;
			batch->first_instance = ctx->frame_instance_count;
		}
		force_new_batch = 0;

		u32 first_instance = ctx->frame_instance_count;
		u32 color = rg_gui_renderer_base_pack_color(cmd->data.text.color);
		if (run_result == RG_GUI_RENDERER_BASE_RUN_CACHED)
		{
			const RgGuiRendererBaseCachedQuad* quads = ctx->cache_quads + run->first_quad;
			for (u32 q = 0u; q < quad_count; q++)
			{
				RgGuiRendererBaseInstance* instance = &ctx->frame_instances[ctx->frame_instance_count++];
				instance->x = cmd->data.text.pos.x + quads[q].x;
				instance->y = cmd->data.text.pos.y + quads[q].y;
				instance->w = quads[q].w;
				instance->h = quads[q].h;
				instance->u0 = quads[q].u0;
				instance->v0 = quads[q].v0;
				instance->u1 = quads[q].u1;
				instance->v1 = quads[q].v1;
				instance->color = color;
				instance->padding = 0u;
			}
		}
		else
		{
			u32 built = rg_gui_renderer_base_build_instances(ctx, text, text_size,
			                                                 cmd->data.text.scale,
			                                                 cmd->data.text.pos, color,
			                                                 ctx->frame_instances + first_instance,
			                                                 quad_count);
			ctx->frame_instance_count += built;
		}
		batch->instance_count += ctx->frame_instance_count - first_instance;
	}

	ctx->prepared.instance_count = ctx->frame_instance_count;
	ctx->prepared.batch_count = ctx->frame_batch_count;
	ctx->stats.frame_instances = ctx->frame_instance_count;
	ctx->stats.frame_batches = ctx->frame_batch_count;
	ctx->stats.frame_upload_bytes = (size_t)ctx->frame_instance_count * sizeof(RgGuiRendererBaseInstance);
	if (ctx->frame_instance_count > ctx->stats.high_water_frame_instances)
		ctx->stats.high_water_frame_instances = ctx->frame_instance_count;
	if (ctx->frame_batch_count > ctx->stats.high_water_frame_batches)
		ctx->stats.high_water_frame_batches = ctx->frame_batch_count;
	return 1;
}

RGINLINE const RgGuiRendererBasePrepared* rg_gui_renderer_base_prepared(const RgGuiRendererBaseContext* ctx)
{
	return ctx && ctx->initialized ? &ctx->prepared : NULL;
}

RGINLINE const RgGuiRendererBaseRunPrepared* rg_gui_renderer_base_run_prepared(const RgGuiRendererBaseContext* ctx)
{
	return ctx && ctx->initialized ? &ctx->run_prepared : NULL;
}
#endif

RGINLINE const RgGuiRendererBaseStats* rg_gui_renderer_base_stats(const RgGuiRendererBaseContext* ctx)
{
	return ctx && ctx->initialized ? &ctx->stats : NULL;
}

// =============================================================================
// STABLE PAGE ALLOCATOR
// =============================================================================

#ifndef RG_GUI_RENDERER_DEFAULT_PAGE_QUADS
#define RG_GUI_RENDERER_DEFAULT_PAGE_QUADS 32u
#endif

// Bound speculative page reservation for cold text layout. Longer runs use an
// exact glyph count before allocation; zero disables speculative reservation.
#ifndef RG_GUI_RENDERER_OPTIMISTIC_TEXT_BYTES
#define RG_GUI_RENDERER_OPTIMISTIC_TEXT_BYTES 256u
#endif

typedef RgGuiRendererBaseLimits RgGuiRendererLimits;
typedef RgGuiRendererBaseInstance RgGuiRendererGlyph;
typedef RgGuiRendererBaseCachedQuad RgGuiRendererCachedQuad;
typedef RgGuiRendererBaseRunInstance RgGuiRendererRun;
typedef RgGuiRendererBaseBatch RgGuiRendererBatch;
typedef RgGuiRendererBaseRunPrepared RgGuiRendererPrepared;
typedef RgGuiRendererBaseStats RgGuiRendererStats;

typedef struct RgGuiRendererInitDesc
{
	const RgTextFont* font;
	RgGuiRendererLimits limits;
	u32 page_quads;
	/* Optional initialized table for this exact font; borrowed, never modified.
	 * Keep the table, font, and its arrays alive and unchanged while in use.
	 * Null or a different font selects a private table allocated in the arena. */
	const RgGuiTextLookup* text_lookup;
} RgGuiRendererInitDesc;

typedef struct RgGuiRendererRange
{
	u32 first_quad;
	u32 quad_count;
} RgGuiRendererRange;

typedef struct RgGuiRendererAllocatorStats
{
	u32 page_quads;
	u32 total_pages;
	u32 live_quads;
	u32 allocated_pages;
	u32 free_pages;
	u32 wasted_quads;
	u32 dirty_page_count;
	u32 frame_page_segments;
	u32 frame_extra_segments;
	u32 frame_reclaims;
	u32 frame_escalated_reclaims;
	u64 total_reclaims;
	u64 total_escalated_reclaims;
} RgGuiRendererAllocatorStats;

typedef struct RgGuiRenderer
{
	RgGuiRendererBaseContext core;
	u32* run_first_pages;
	u32* page_next;
	u32* free_pages;
	RgGuiRendererRange* dirty_pages;
	/* Zero is free; live pages carry the last content revision, surviving begin_frame. */
	u64* page_revisions;
	u64 cache_revision;
	u32 page_quads;
	u32 total_pages;
	u32 next_fresh_page;
	u32 recycled_page_count;
	u32 free_page_count;
	u32 dirty_page_count;
	u32 live_quad_count;
	u32 allocated_page_count;
	RgGuiRendererAllocatorStats allocator_stats;
	int initialized;
} RgGuiRenderer;

/** Return the bounded cache and frame capacities. */
RGINLINE RgGuiRendererLimits rg_gui_renderer_limits_default(void)
{
	return rg_gui_renderer_base_limits_default();
}

/** Return the resolved page size, or zero when the configuration is invalid. */
RGINLINE u32 rg_gui_renderer_page_quads_resolve(u32 supplied,
                                                u32 max_cached_quads)
{
	u32 page_quads = supplied ? supplied : RG_GUI_RENDERER_DEFAULT_PAGE_QUADS;
	if (!page_quads || page_quads > max_cached_quads ||
	    max_cached_quads % page_quads != 0u ||
	    (page_quads & (page_quads - 1u)) != 0u)
	{
		return 0u;
	}
	return page_quads;
}

RGINLINE size_t rg_gui_renderer_memory_required_internal(
    const RgGuiRendererLimits* supplied, u32 supplied_page_quads, int own_lookup)
{
	RgGuiRendererLimits limits = rg_gui_renderer_base_limits_resolve(supplied);
	u32 page_quads = rg_gui_renderer_page_quads_resolve(
	    supplied_page_quads, limits.max_cached_quads);
	if (!page_quads) return SIZE_MAX;
	u32 total_pages = limits.max_cached_quads / page_quads;
	size_t total = rg_gui_renderer_base_memory_required_internal(&limits, own_lookup);
	if (!total_pages || total == SIZE_MAX ||
	    !rg_gui_renderer_base_size_add_array(&total, limits.max_cached_runs,
	                                         sizeof(u32), RG_ALIGNOF(u32)) ||
	    !rg_gui_renderer_base_size_add_array(&total, total_pages,
	                                         sizeof(u32), RG_ALIGNOF(u32)) ||
	    !rg_gui_renderer_base_size_add_array(&total, total_pages,
	                                         sizeof(u32), RG_ALIGNOF(u32)) ||
	    !rg_gui_renderer_base_size_add_array(&total, total_pages,
	                                         sizeof(u64), RG_ALIGNOF(u64)) ||
	    !rg_gui_renderer_base_size_add_array(&total, total_pages,
	                                         sizeof(RgGuiRendererRange), RG_ALIGNOF(RgGuiRendererRange)))
	{
		return SIZE_MAX;
	}
	return total;
}

/** Safe arena bound including a private lookup, or SIZE_MAX on invalid limits.
 * This remains sufficient whether or not a shared table is attached later. */
RGINLINE size_t rg_gui_renderer_memory_required(const RgGuiRendererLimits* supplied,
                                                u32 supplied_page_quads)
{
	return rg_gui_renderer_memory_required_internal(supplied, supplied_page_quads, 1);
}

/** Safe arena bound for this descriptor, excluding a matching borrowed lookup.
 * Includes padding for any arena base alignment. Returns SIZE_MAX for a missing
 * descriptor/font, invalid page configuration, or overflow. Font contents are
 * validated by init. Use the same descriptor and immutable table when initializing. */
RGINLINE size_t rg_gui_renderer_memory_required_ex(const RgGuiRendererInitDesc* desc)
{
	if (!desc || !desc->font) return SIZE_MAX;
	int own_lookup = !desc->text_lookup || desc->text_lookup->font != desc->font;
	return rg_gui_renderer_memory_required_internal(&desc->limits, desc->page_quads, own_lookup);
}

RGINLINE void rg_gui_renderer_sync_allocator_stats(RgGuiRenderer* ctx)
{
	RgGuiRendererAllocatorStats* stats = &ctx->allocator_stats;
	stats->page_quads = ctx->page_quads;
	stats->total_pages = ctx->total_pages;
	stats->live_quads = ctx->live_quad_count;
	stats->allocated_pages = ctx->allocated_page_count;
	stats->free_pages = ctx->free_page_count;
	stats->wasted_quads = ctx->allocated_page_count * ctx->page_quads -
	                      ctx->live_quad_count;
	stats->dirty_page_count = ctx->dirty_page_count;
	ctx->core.cache_quad_count = ctx->live_quad_count;
	rg_gui_renderer_base_sync_cache_stats(&ctx->core);
}

/** Initialize a stable segmented-page cache. */
RGINLINE int rg_gui_renderer_init(RgGuiRenderer* ctx, RgArena* arena,
                                  const RgGuiRendererInitDesc* desc)
{
	if (!ctx || !arena || !desc)
	{
		if (ctx) memset(ctx, 0, sizeof(*ctx));
		return 0;
	}
	size_t arena_used = arena->used;
	memset(ctx, 0, sizeof(*ctx));
	RgGuiRendererLimits limits = rg_gui_renderer_base_limits_resolve(&desc->limits);
	u32 page_quads = rg_gui_renderer_page_quads_resolve(
	    desc->page_quads, limits.max_cached_quads);
	if (!page_quads || rg_gui_renderer_memory_required_ex(desc) == SIZE_MAX)
		return 0;

	RgGuiRendererBaseInitDesc core_desc;
	core_desc.font = desc->font;
	core_desc.limits = limits;
	core_desc.text_lookup = desc->text_lookup;
	if (!rg_gui_renderer_base_init(&ctx->core, arena, &core_desc)) return 0;

	ctx->page_quads = page_quads;
	ctx->total_pages = limits.max_cached_quads / page_quads;
	ctx->run_first_pages = (u32*)rg_arena_alloc_array(
	    arena, sizeof(u32), limits.max_cached_runs, RG_ALIGNOF(u32));
	ctx->page_next = (u32*)rg_arena_alloc_array(
	    arena, sizeof(u32), ctx->total_pages, RG_ALIGNOF(u32));
	ctx->free_pages = (u32*)rg_arena_alloc_array(
	    arena, sizeof(u32), ctx->total_pages, RG_ALIGNOF(u32));
	ctx->dirty_pages = (RgGuiRendererRange*)rg_arena_alloc_array(
	    arena, sizeof(RgGuiRendererRange), ctx->total_pages, RG_ALIGNOF(RgGuiRendererRange));
	ctx->page_revisions = (u64*)rg_arena_alloc_array(
	    arena, sizeof(u64), ctx->total_pages, RG_ALIGNOF(u64));
	if (!ctx->run_first_pages || !ctx->page_next || !ctx->free_pages ||
	    !ctx->dirty_pages || !ctx->page_revisions)
	{
		arena->used = arena_used;
		memset(ctx, 0, sizeof(*ctx));
		return 0;
	}
	ctx->free_page_count = ctx->total_pages;
	memset(ctx->page_revisions, 0, sizeof(u64) * ctx->total_pages);
	ctx->initialized = 1;
	rg_gui_renderer_sync_allocator_stats(ctx);
	return 1;
}

RGINLINE u32 rg_gui_renderer_pages_required(const RgGuiRenderer* ctx,
                                            u32 quad_count)
{
	return quad_count ? 1u + (quad_count - 1u) / ctx->page_quads : 0u;
}

RGINLINE u32 rg_gui_renderer_allocate_pages(RgGuiRenderer* ctx, u32 page_count)
{
	if (!page_count) return UINT32_MAX;
	if (page_count > ctx->free_page_count) return UINT32_MAX;
	if (page_count == 1u)
	{
		u32 page = ctx->recycled_page_count ? ctx->free_pages[--ctx->recycled_page_count] : ctx->next_fresh_page++;
		ctx->free_page_count--;
		ctx->page_next[page] = UINT32_MAX;
		memset(ctx->core.cache_quads + page * ctx->page_quads, 0,
		       sizeof(RgGuiRendererCachedQuad) * ctx->page_quads);
		ctx->allocated_page_count++;
		return page;
	}
	u32 first_page = UINT32_MAX;
	u32 previous_page = UINT32_MAX;
	for (u32 i = 0u; i < page_count; i++)
	{
		u32 page = ctx->recycled_page_count ? ctx->free_pages[--ctx->recycled_page_count] : ctx->next_fresh_page++;
		memset(ctx->core.cache_quads + page * ctx->page_quads, 0,
		       sizeof(RgGuiRendererCachedQuad) * ctx->page_quads);
		if (previous_page != UINT32_MAX) ctx->page_next[previous_page] = page;
		else first_page = page;
		previous_page = page;
	}
	ctx->free_page_count -= page_count;
	ctx->page_next[previous_page] = UINT32_MAX;
	ctx->allocated_page_count += page_count;
	return first_page;
}

RGINLINE void rg_gui_renderer_free_pages(RgGuiRenderer* ctx, u32 first_page,
                                         u32 page_count, u32 quad_count)
{
	if (page_count == 1u)
	{
		ctx->free_pages[ctx->recycled_page_count++] = first_page;
		ctx->page_revisions[first_page] = 0u;
		ctx->free_page_count++;
		ctx->allocated_page_count--;
		ctx->live_quad_count -= quad_count;
		return;
	}
	u32 page = first_page;
	for (u32 i = 0u; i < page_count; i++)
	{
		u32 next = ctx->page_next[page];
		ctx->free_pages[ctx->recycled_page_count++] = page;
		ctx->page_revisions[page] = 0u;
		page = next;
	}
	ctx->free_page_count += page_count;
	ctx->allocated_page_count -= page_count;
	ctx->live_quad_count -= quad_count;
}

RGINLINE void rg_gui_renderer_mark_pages_dirty(RgGuiRenderer* ctx, u32 first_page,
                                               u32 quad_count)
{
	if (!quad_count) return;
	u64 revision = ++ctx->cache_revision;
	if (quad_count <= ctx->page_quads)
	{
		ctx->page_revisions[first_page] = revision;
		RgGuiRendererRange* dirty = &ctx->dirty_pages[ctx->dirty_page_count++];
		dirty->first_quad = first_page * ctx->page_quads;
		dirty->quad_count = quad_count;
		return;
	}
	u32 page = first_page;
	u32 remaining = quad_count;
	while (remaining)
	{
		ctx->page_revisions[page] = revision;
		u32 count = remaining < ctx->page_quads ? remaining : ctx->page_quads;
		RgGuiRendererRange* dirty = &ctx->dirty_pages[ctx->dirty_page_count++];
		dirty->first_quad = page * ctx->page_quads;
		dirty->quad_count = count;
		remaining -= count;
		page = ctx->page_next[page];
	}
}

RGINLINE u32 rg_gui_renderer_build_pages(const RgGuiRenderer* ctx, const char* text,
                                         size_t text_size, f32 scale,
                                         u32 first_page, u32 capacity)
{
	if (!text || !text_size || scale == 0.0f) return 0u;
	if (!capacity) return 0u;
	if (capacity <= ctx->page_quads)
	{
		return rg_gui_renderer_base_build_cached(
		    &ctx->core, text, text_size, scale,
		    ctx->core.cache_quads + first_page * ctx->page_quads, capacity);
	}
	size_t offset = 0u;
	f32 line_y = 0.0f;
	f32 pen_x = 0.0f;
	u32 count = 0u;
	const RgTextGlyph* previous = NULL;
	u32 page = first_page;
	u32 page_offset = 0u;
	while (offset < text_size && count < capacity)
	{
		u32 cp = rg_text_decode_utf8(text, text_size, &offset);
		if (cp == '\r' || cp == '\n')
		{
			if (cp == '\r' && offset < text_size && text[offset] == '\n') offset++;
			line_y += (f32)ctx->core.font->metrics.line_height * scale;
			pen_x = 0.0f;
			previous = NULL;
			continue;
		}
		const RgTextGlyph* glyph = rg_gui_renderer_base_find_glyph(&ctx->core, cp);
		if (!glyph)
		{
			previous = NULL;
			continue;
		}
		if (previous)
		{
			pen_x += (f32)rg_gui_renderer_base_find_kerning(&ctx->core, previous->codepoint, glyph->codepoint) * scale;
		}
		if (glyph->w > 0 && glyph->h > 0)
		{
			if (page_offset == ctx->page_quads)
			{
				page = ctx->page_next[page];
				page_offset = 0u;
			}
			RgGuiRendererBaseCachedQuad* quad =
			    &ctx->core.cache_quads[page * ctx->page_quads + page_offset++];
			quad->x = pen_x + (f32)glyph->x_offset * scale;
			quad->y = line_y + (f32)glyph->y_offset * scale;
			quad->w = (f32)glyph->w * scale;
			quad->h = (f32)glyph->h * scale;
			quad->u0 = (u16)glyph->x;
			quad->v0 = (u16)glyph->y;
			quad->u1 = (u16)(glyph->x + glyph->w);
			quad->v1 = (u16)(glyph->y + glyph->h);
			count++;
		}
		pen_x += (f32)glyph->x_advance * scale;
		previous = glyph;
	}
	return count;
}

RGINLINE void rg_gui_renderer_rebuild_hash(RgGuiRenderer* ctx)
{
	RgGuiRendererBaseContext* core = &ctx->core;
	memset(core->hash_slots, 0, sizeof(u32) * core->limits.hash_slot_count);
	for (u32 i = 0u; i < core->run_count; i++)
	{
		u32 start = core->hash_slot_mask != UINT32_MAX ? (u32)core->runs[i].hash & core->hash_slot_mask : (u32)(core->runs[i].hash % core->limits.hash_slot_count);
		for (u32 probe = 0u; probe < core->limits.hash_slot_count; probe++)
		{
			u32 slot = core->hash_slot_mask != UINT32_MAX ? (start + probe) & core->hash_slot_mask : (u32)(((u64)start + probe) % core->limits.hash_slot_count);
			if (!core->hash_slots[slot])
			{
				core->hash_slots[slot] = i + 1u;
				break;
			}
		}
	}
}

RGINLINE void rg_gui_renderer_reclaim(RgGuiRenderer* ctx, int keep_previous)
{
	RgGuiRendererBaseContext* core = &ctx->core;
	u32 old_count = core->run_count;
	u32 kept_count = 0u;
	size_t kept_text = 0u;
	for (u32 i = 0u; i < old_count; i++)
	{
		RgGuiRendererBaseCachedRun run = core->runs[i];
		u8 generation = core->run_generations[i];
		int keep = generation == core->frame_index ||
		           (keep_previous &&
		            generation == (u8)(core->frame_index - 1u));
		if (!keep)
		{
			rg_gui_renderer_free_pages(ctx, ctx->run_first_pages[i],
			                           rg_gui_renderer_pages_required(ctx, run.quad_count),
			                           run.quad_count);
			continue;
		}
		int identity = run.text_size == RG_GUI_RENDERER_BASE_IDENTITY_TEXT_SIZE;
		if (!identity && run.text_size)
			memmove(core->cache_text + kept_text,
			        core->cache_text + run.text_offset, run.text_size);
		if (!identity) run.text_offset = kept_text;
		core->runs[kept_count] = run;
		core->run_generations[kept_count] = generation;
		ctx->run_first_pages[kept_count] = ctx->run_first_pages[i];
		kept_count++;
		if (!identity) kept_text += run.text_size;
	}
	core->run_count = kept_count;
	core->text_used = kept_text;
	rg_gui_renderer_rebuild_hash(ctx);
	core->stats.frame_cache_compactions++;
	core->stats.total_cache_compactions++;
	core->stats.frame_cache_evictions = rg_gui_renderer_base_u32_add_saturate(
	    core->stats.frame_cache_evictions, old_count - kept_count);
	core->stats.total_cache_evictions = rg_gui_renderer_base_u64_add_saturate(
	    core->stats.total_cache_evictions, old_count - kept_count);
	ctx->allocator_stats.frame_reclaims++;
	ctx->allocator_stats.total_reclaims++;
	if (!keep_previous)
	{
		ctx->allocator_stats.frame_escalated_reclaims++;
		ctx->allocator_stats.total_escalated_reclaims++;
	}
}

RGINLINE int rg_gui_renderer_run_metadata_fits(const RgGuiRendererBaseContext* core,
                                               size_t text_size, u32 slot)
{
	return slot != UINT32_MAX && core->run_count < core->limits.max_cached_runs &&
	       text_size <= core->limits.text_capacity &&
	       text_size <= core->limits.text_capacity - core->text_used;
}

static RgGuiRendererBaseRunResult rg_gui_renderer_get_run(RgGuiRenderer* ctx, const char* text,
                                                          size_t text_size, f32 scale,
                                                          uintptr_t cache_identity,
                                                          u32* out_run_index,
                                                          u32* out_quad_count)
{
	RgGuiRendererBaseContext* core = &ctx->core;
	u32 scale_bits = rg_gui_renderer_base_float_bits(scale);
	u64 hash = rg_gui_renderer_base_key_hash(text, text_size, scale_bits, cache_identity);
	u32 run_index = UINT32_MAX;
	u32 slot = rg_gui_renderer_base_find_slot(core, hash, text, text_size, scale_bits,
	                                          cache_identity, &run_index);
	if (run_index != UINT32_MAX)
	{
		core->run_generations[run_index] = core->frame_index;
		core->stats.frame_cache_hits++;
		core->stats.total_cache_hits++;
		core->stats.frame_reused_glyphs = rg_gui_renderer_base_u32_add_saturate(
		    core->stats.frame_reused_glyphs, core->runs[run_index].quad_count);
		core->stats.total_reused_glyphs = rg_gui_renderer_base_u64_add_saturate(
		    core->stats.total_reused_glyphs, core->runs[run_index].quad_count);
		*out_run_index = run_index;
		*out_quad_count = core->runs[run_index].quad_count;
		return RG_GUI_RENDERER_BASE_RUN_CACHED;
	}

	core->stats.frame_cache_misses++;
	core->stats.total_cache_misses++;
	if (cache_identity && scale != 0.0f) text_size = strlen(text);
	size_t stored_text_size = cache_identity ? 0u : text_size;
	u32 quad_count = 0u;
	u32 page_count = 0u;
	u32 first_page = UINT32_MAX;
	int built_optimistically = 0;
	u32 reclaim_level = 0u;
	// Every emitted glyph consumes at least one UTF-8 byte. Reserve this bounded
	// upper limit only from available pages; overestimation must not evict runs.
	u32 optimistic_pages = text_size && scale != 0.0f &&
	                       text_size <= RG_GUI_RENDERER_OPTIMISTIC_TEXT_BYTES &&
	                       text_size <= UINT32_MAX
	                           ? rg_gui_renderer_pages_required(ctx, (u32)text_size) : 0u;
	if (optimistic_pages && optimistic_pages <= ctx->total_pages &&
	    stored_text_size <= core->limits.text_capacity)
	{
		// Run/text/hash storage can require reclamation independently of glyph
		// count. Resolve that pressure first so short runs still build once after
		// reclamation; capacity failures retain the exact-count fallback below.
		while (!rg_gui_renderer_run_metadata_fits(core, stored_text_size, slot) && reclaim_level < 2u)
		{
			reclaim_level++;
			rg_gui_renderer_reclaim(ctx, reclaim_level == 1u);
			slot = rg_gui_renderer_base_find_slot(core, hash, text, text_size, scale_bits,
			                                      cache_identity, &run_index);
		}
	}
	if (optimistic_pages && optimistic_pages <= ctx->free_page_count &&
	    rg_gui_renderer_run_metadata_fits(core, stored_text_size, slot))
	{
		first_page = rg_gui_renderer_allocate_pages(ctx, optimistic_pages);
		if (first_page != UINT32_MAX)
		{
			quad_count = rg_gui_renderer_build_pages(
			    ctx, text, text_size, scale, first_page, optimistic_pages * ctx->page_quads);
			page_count = rg_gui_renderer_pages_required(ctx, quad_count);
			if (page_count < optimistic_pages)
			{
				u32 unused_page = first_page;
				if (page_count)
				{
					u32 last_page = first_page;
					for (u32 i = 1u; i < page_count; i++) last_page = ctx->page_next[last_page];
					unused_page = ctx->page_next[last_page];
					ctx->page_next[last_page] = UINT32_MAX;
				}
				else first_page = UINT32_MAX;
				// These glyphs have not entered live_quad_count yet. Free only the
				// unused pages, leaving initialized tails in all retained pages.
				rg_gui_renderer_free_pages(ctx, unused_page, optimistic_pages - page_count, 0u);
			}
			built_optimistically = 1;
		}
	}
	if (!built_optimistically)
	{
		quad_count = scale == 0.0f ? 0u : rg_gui_renderer_base_count_quads(core, text, text_size);
		page_count = rg_gui_renderer_pages_required(ctx, quad_count);
	}
	core->stats.frame_laid_out_glyphs = rg_gui_renderer_base_u32_add_saturate(
	    core->stats.frame_laid_out_glyphs, quad_count);
	core->stats.total_laid_out_glyphs = rg_gui_renderer_base_u64_add_saturate(
	    core->stats.total_laid_out_glyphs, quad_count);
	*out_quad_count = quad_count;
	if (!built_optimistically)
	{
		if (stored_text_size > core->limits.text_capacity ||
		    page_count > ctx->total_pages) goto bypass;
		while ((!rg_gui_renderer_run_metadata_fits(core, stored_text_size, slot) ||
		        page_count > ctx->free_page_count) && reclaim_level < 2u)
		{
			reclaim_level++;
			rg_gui_renderer_reclaim(ctx, reclaim_level == 1u);
			slot = rg_gui_renderer_base_find_slot(core, hash, text, text_size, scale_bits,
			                                      cache_identity, &run_index);
		}
		if (!rg_gui_renderer_run_metadata_fits(core, stored_text_size, slot) ||
		    page_count > ctx->free_page_count) goto bypass;

		first_page = rg_gui_renderer_allocate_pages(ctx, page_count);
		if (page_count && first_page == UINT32_MAX) goto bypass;
	}
	u32 new_index = core->run_count;
	RgGuiRendererBaseCachedRun* run = &core->runs[new_index];
	run->hash = hash;
	run->scale_bits = scale_bits;
	run->text_offset = cache_identity ? (size_t)cache_identity : core->text_used;
	run->text_size = cache_identity ? RG_GUI_RENDERER_BASE_IDENTITY_TEXT_SIZE : text_size;
	run->first_quad = page_count ? first_page * ctx->page_quads : 0u;
	run->quad_count = quad_count;
	core->run_generations[new_index] = core->frame_index;
	ctx->run_first_pages[new_index] = first_page;
	if (stored_text_size)
		memcpy(core->cache_text + core->text_used, text, stored_text_size);
	if (!built_optimistically)
	{
		u32 built = rg_gui_renderer_build_pages(ctx, text, text_size, scale,
		                                        first_page, quad_count);
		if (built != quad_count)
		{
			rg_gui_renderer_free_pages(ctx, first_page, page_count, 0u);
			goto bypass;
		}
	}
	ctx->live_quad_count += quad_count;
	core->hash_slots[slot] = new_index + 1u;
	core->run_count++;
	core->text_used += stored_text_size;
	rg_gui_renderer_mark_pages_dirty(ctx, first_page, quad_count);
	if (core->run_count > core->stats.high_water_cache_runs)
		core->stats.high_water_cache_runs = core->run_count;
	if (core->text_used > core->stats.high_water_cache_text_bytes)
		core->stats.high_water_cache_text_bytes = core->text_used;
	if (ctx->live_quad_count > core->stats.high_water_cache_quads)
		core->stats.high_water_cache_quads = ctx->live_quad_count;
	*out_run_index = new_index;
	return RG_GUI_RENDERER_BASE_RUN_CACHED;

bypass:
	core->stats.frame_cache_bypasses++;
	core->stats.total_cache_bypasses++;
	*out_run_index = UINT32_MAX;
	return RG_GUI_RENDERER_BASE_RUN_BYPASS;
}

/** Invalidate all cached runs and return every page to the free stack. */
RGINLINE void rg_gui_renderer_clear_cache(RgGuiRenderer* ctx)
{
	if (!ctx || !ctx->initialized) return;
	rg_gui_renderer_base_clear_cache(&ctx->core);
	ctx->next_fresh_page = 0u;
	ctx->recycled_page_count = 0u;
	ctx->free_page_count = ctx->total_pages;
	ctx->dirty_page_count = 0u;
	ctx->live_quad_count = 0u;
	ctx->allocated_page_count = 0u;
	memset(ctx->page_revisions, 0, sizeof(u64) * ctx->total_pages);
	ctx->cache_revision++;
	rg_gui_renderer_sync_allocator_stats(ctx);
}

/** Reset frame output while preserving stable cached pages. */
RGINLINE void rg_gui_renderer_begin_frame(RgGuiRenderer* ctx)
{
	if (!ctx || !ctx->initialized) return;
	rg_gui_renderer_base_begin_frame(&ctx->core);
	ctx->dirty_page_count = 0u;
	ctx->allocator_stats.frame_page_segments = 0u;
	ctx->allocator_stats.frame_extra_segments = 0u;
	ctx->allocator_stats.frame_reclaims = 0u;
	ctx->allocator_stats.frame_escalated_reclaims = 0u;
}

/** Snapshot sorted, coalesced whole-page writes newer than a submitted revision.
 * Every allocated page is initialized in full, including its unused tail.
 * Pass NULL ranges to count storage. Returns UINT32_MAX for insufficient output. */
RGINLINE u32 rg_gui_renderer_upload_ranges(const RgGuiRenderer* ctx,
                                            u64 after_revision,
                                            RgGuiRendererRange* ranges,
                                            u32 capacity, u32* out_quads)
{
	u32 count = 0u, quads = 0u;
	if (!ctx || !ctx->initialized) return UINT32_MAX;
	for (u32 page = 0u; page < ctx->next_fresh_page;)
	{
		if (ctx->page_revisions[page] <= after_revision) { page++; continue; }
		u32 first = page++;
		while (page < ctx->next_fresh_page && ctx->page_revisions[page] > after_revision)
			page++;
		u32 length = (page - first) * ctx->page_quads;
		if (ranges)
		{
			if (count >= capacity) return UINT32_MAX;
			ranges[count].first_quad = first * ctx->page_quads;
			ranges[count].quad_count = length;
		}
		quads += length;
		count++;
	}
	if (out_quads) *out_quads = quads;
	return count;
}

/** Emit one compact descriptor for each cached page segment. */
RGINLINE int rg_gui_renderer_prepare(RgGuiRenderer* ctx,
                                     const RgGuiDrawList* list,
                                     u32 overlay_start)
{
	if (!ctx || !ctx->initialized || !list || (list->count && !list->cmds))
	{
		if (ctx && ctx->initialized)
			ctx->core.stats.diagnostic_flags |= RG_GUI_RENDERER_BASE_DIAGNOSTIC_INVALID_ARGUMENT;
		return 0;
	}
	if (overlay_start > list->count) overlay_start = list->count;
	RgGuiRendererBaseContext* core = &ctx->core;
	RgGuiRect clip_stack[RG_GUI_RENDERER_BASE_CLIP_STACK_MAX];
	u32 clip_depth = 0u;
	u32 ignored_pushes = 0u;
	int force_new_batch = 0;
	RgGuiRect current_clip;
	memset(&current_clip, 0, sizeof(current_clip));
	int clip_enabled = 0;
	RgGuiRendererRun* frame_runs = (RgGuiRendererRun*)core->frame_instances;
	u32 output_glyph_count = 0u;

	for (u32 i = 0u; i < list->count; i++)
	{
		if (i == overlay_start)
		{
			clip_depth = 0u;
			ignored_pushes = 0u;
			clip_enabled = 0;
			force_new_batch = 1;
			memset(&current_clip, 0, sizeof(current_clip));
		}
		const RgGuiDrawCmd* cmd = &list->cmds[i];
		if (cmd->type == RG_GUI_CMD_CLIP_PUSH)
		{
			if (clip_depth >= RG_GUI_RENDERER_BASE_CLIP_STACK_MAX)
			{
				ignored_pushes++;
				core->stats.diagnostic_flags |= RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_OVERFLOW;
				continue;
			}
			current_clip = clip_enabled ? rg_gui_renderer_base_clip_intersect(current_clip, cmd->data.clip.rect) : cmd->data.clip.rect;
			clip_stack[clip_depth++] = current_clip;
			clip_enabled = 1;
			continue;
		}
		if (cmd->type == RG_GUI_CMD_CLIP_POP)
		{
			if (ignored_pushes) ignored_pushes--;
			else if (clip_depth)
			{
				clip_depth--;
				clip_enabled = clip_depth != 0u;
				if (clip_enabled) current_clip = clip_stack[clip_depth - 1u];
			}
			else core->stats.diagnostic_flags |= RG_GUI_RENDERER_BASE_DIAGNOSTIC_CLIP_STACK_UNDERFLOW;
			continue;
		}
		if (cmd->type != RG_GUI_CMD_TEXT)
		{
			force_new_batch = 1;
			continue;
		}

		const char* text = cmd->data.text.text ? cmd->data.text.text : "";
		uintptr_t cache_identity = cmd->data.text.cache_identity;
		size_t text_size = cache_identity ? 0u : strlen(text);
		u32 run_index = UINT32_MAX;
		u32 quad_count = 0u;
		RgGuiRendererBaseRunResult result = rg_gui_renderer_get_run(
		    ctx, text, text_size, cmd->data.text.scale, cache_identity,
		    &run_index, &quad_count);
		core->stats.frame_text_runs++;
		core->stats.frame_glyphs = rg_gui_renderer_base_u32_add_saturate(
		    core->stats.frame_glyphs, quad_count);
		if (result != RG_GUI_RENDERER_BASE_RUN_CACHED)
		{
			if (quad_count)
				rg_gui_renderer_base_drop_run(core, quad_count,
				                              RG_GUI_RENDERER_BASE_DIAGNOSTIC_RUN_CACHE_REQUIRED);
			continue;
		}
		if (!quad_count) continue;
		u32 segment_count = rg_gui_renderer_pages_required(ctx, quad_count);
		if (segment_count > core->limits.max_frame_runs -
		                        core->frame_instance_count ||
		    quad_count > core->limits.max_frame_instances - output_glyph_count)
		{
			rg_gui_renderer_base_drop_run(core, quad_count, RG_GUI_RENDERER_BASE_DIAGNOSTIC_INSTANCE_CAPACITY);
			continue;
		}

		RgGuiRendererBatch* batch = core->frame_batch_count ? &core->frame_batches[core->frame_batch_count - 1u] : NULL;
		if (!batch || force_new_batch ||
		    !rg_gui_renderer_base_batch_matches(batch, clip_enabled, &current_clip))
		{
			if (core->frame_batch_count >= core->limits.max_batches)
			{
				rg_gui_renderer_base_drop_run(core, quad_count, RG_GUI_RENDERER_BASE_DIAGNOSTIC_BATCH_CAPACITY);
				continue;
			}
			batch = &core->frame_batches[core->frame_batch_count++];
			memset(batch, 0, sizeof(*batch));
			batch->clip_enabled = (u32)clip_enabled;
			if (clip_enabled) batch->clip = current_clip;
			batch->first_instance = core->frame_instance_count;
		}
		force_new_batch = 0;
		u32 page = ctx->run_first_pages[run_index];
		u32 packed_color = rg_gui_renderer_base_pack_color(cmd->data.text.color);
		if (segment_count == 1u)
		{
			RgGuiRendererRun* output = &frame_runs[core->frame_instance_count++];
			output->first_cached_quad = page * ctx->page_quads;
			output->quad_count = quad_count;
			output->x = cmd->data.text.pos.x;
			output->y = cmd->data.text.pos.y;
			output->color = packed_color;
			output->first_output_instance = output_glyph_count;
			output->padding[0] = i;
			output->padding[1] = 0u;
			output_glyph_count += quad_count;
			batch->instance_count++;
			ctx->allocator_stats.frame_page_segments++;
			continue;
		}
		u32 remaining = quad_count;
		while (remaining)
		{
			u32 count = remaining < ctx->page_quads ? remaining : ctx->page_quads;
			RgGuiRendererRun* output = &frame_runs[core->frame_instance_count++];
			output->first_cached_quad = page * ctx->page_quads;
			output->quad_count = count;
			output->x = cmd->data.text.pos.x;
			output->y = cmd->data.text.pos.y;
			output->color = packed_color;
			output->first_output_instance = output_glyph_count;
			output->padding[0] = i;
			output->padding[1] = 0u;
			output_glyph_count += count;
			remaining -= count;
			page = ctx->page_next[page];
		}
		batch->instance_count += segment_count;
		ctx->allocator_stats.frame_page_segments += segment_count;
		ctx->allocator_stats.frame_extra_segments += segment_count - 1u;
	}
	core->run_prepared.run_count = core->frame_instance_count;
	core->run_prepared.glyph_count = output_glyph_count;
	core->run_prepared.batch_count = core->frame_batch_count;
	core->stats.frame_instances = core->frame_instance_count;
	core->stats.frame_batches = core->frame_batch_count;
	core->stats.frame_upload_bytes =
	    (size_t)core->frame_instance_count * sizeof(RgGuiRendererRun);
	if (core->frame_instance_count > core->stats.high_water_frame_instances)
		core->stats.high_water_frame_instances = core->frame_instance_count;
	if (core->frame_batch_count > core->stats.high_water_frame_batches)
		core->stats.high_water_frame_batches = core->frame_batch_count;
	rg_gui_renderer_sync_allocator_stats(ctx);
	return 1;
}

/** Get prepared page-segment descriptors. */
RGINLINE const RgGuiRendererPrepared* rg_gui_renderer_prepared(const RgGuiRenderer* ctx)
{
	return ctx && ctx->initialized ? &ctx->core.run_prepared : NULL;
}

/** Get current frame and cache statistics. */
RGINLINE const RgGuiRendererStats* rg_gui_renderer_stats(const RgGuiRenderer* ctx)
{
	return ctx && ctx->initialized ? &ctx->core.stats : NULL;
}

/** Get page allocation, waste, segmentation, and reclamation statistics. */
RGINLINE const RgGuiRendererAllocatorStats* rg_gui_renderer_allocator_stats(
    const RgGuiRenderer* ctx)
{
	return ctx && ctx->initialized ? &ctx->allocator_stats : NULL;
}

#endif // RG_GUI_RENDERER_H
