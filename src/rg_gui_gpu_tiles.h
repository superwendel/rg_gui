// rg_gui_gpu_tiles - Dirty-tile change detection for rg_gui draw lists
//
// Part of the Reverse Gravity (rg_) libraries.
// Optional single-header pass that bins each prepared draw command into a grid
// of screen tiles, digests every tile's command list in order, and compares the
// digests with the previously presented frame. Tiles whose digest changed are
// returned as scissor rectangles; a caller that keeps its frame in a persistent
// color target can redraw only those rectangles and blit the target to the
// swapchain. A frame with no dirty tiles needs no drawing at all.
//
// This is the render-side cache described for immediate-mode UIs by rxi and
// Blick: the UI still rebuilds its description every frame; only repainting is
// skipped. The pass sees rg_gui's inputs only. The caller must bump
// `external_revision` when a texture referenced by image commands changes, and
// must invalidate the cache when the atlas, clear color, or draw offset change.
//
// NOTES:
//   - Include after rg_gui_gpu.h, after rg_gui_gpu_prepare has run for the list.
//   - Storage is caller-owned and fixed; a frame needing more tiles or more
//     dirty rectangles than provided falls back to a full redraw.
//   - All functions have internal linkage and work in unity builds.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_GUI_GPU_TILES_H
#define RG_GUI_GPU_TILES_H

#include "rg_gui_gpu.h"

#ifndef RG_GUI_TILE_SIZE
#define RG_GUI_TILE_SIZE 64u
#endif

typedef char RgGuiTileSizeMustBePowerOfTwo[(RG_GUI_TILE_SIZE & (RG_GUI_TILE_SIZE - 1u)) == 0u && RG_GUI_TILE_SIZE >= 8u ? 1 : -1];

typedef enum RgGuiTileRedraw
{
	RG_GUI_TILE_REDRAW_NONE = 0,    /* Every tile matches the presented frame. */
	RG_GUI_TILE_REDRAW_PARTIAL = 1, /* Redraw the rectangles in `rects`. */
	RG_GUI_TILE_REDRAW_FULL = 2     /* Redraw everything (first frame, resize, overflow, invalidation). */
} RgGuiTileRedraw;

typedef struct RgGuiTileCacheStats
{
	u32 total_tiles;
	u32 dirty_tiles;
	u32 rects;
	u32 mode;
	u32 commands;      /* drawing commands binned this frame */
	u32 tile_updates;  /* command-tile pairs digested this frame */
	u64 total_full_redraws;
	u64 total_partial_redraws;
	u64 total_skipped_frames;
} RgGuiTileCacheStats;

typedef struct RgGuiTileCache
{
	u64* hashes;        /* this frame's digest per tile */
	u64* previous;      /* the presented frame's digest per tile */
	SDL_Rect* rects;    /* dirty rectangles in pixels, after update */
	u32 tile_capacity;
	u32 rect_capacity;
	u32 columns;
	u32 rows;
	u32 width;
	u32 height;
	f32 offset_x;
	f32 offset_y;
	/* Geometry of the committed frame; `previous` is comparable only to a frame
	 * with the same output size, grid, and draw offset. */
	u32 previous_columns;
	u32 previous_rows;
	u32 previous_width;
	u32 previous_height;
	f32 previous_offset_x;
	f32 previous_offset_y;
	int previous_valid;
	int frame_valid;                /* this frame's digests cover the whole output */
	u64 external_revision;          /* caller-maintained; see header notes */
	u64 previous_external_revision;
	RgGuiTileCacheStats stats;
} RgGuiTileCache;

/** Tiles needed to cover an output of this size. */
RGINLINE u32 rg_gui_tile_cache_tiles_required(u32 width, u32 height)
{
	u32 columns = (width + RG_GUI_TILE_SIZE - 1u) / RG_GUI_TILE_SIZE;
	u32 rows = (height + RG_GUI_TILE_SIZE - 1u) / RG_GUI_TILE_SIZE;
	return columns * rows;
}

/** Attach caller-owned storage: two digest arrays of tile_capacity and rect_capacity rectangles. */
RGINLINE int rg_gui_tile_cache_init(RgGuiTileCache* cache, u64* hashes, u64* previous,
                                    SDL_Rect* rects, u32 tile_capacity, u32 rect_capacity)
{
	if (!cache) return 0;
	memset(cache, 0, sizeof(*cache));
	if (!hashes || !previous || !rects || !tile_capacity || !rect_capacity) return 0;
	cache->hashes = hashes;
	cache->previous = previous;
	cache->rects = rects;
	cache->tile_capacity = tile_capacity;
	cache->rect_capacity = rect_capacity;
	return 1;
}

/** Forget the presented frame: the next update redraws everything. */
RGINLINE void rg_gui_tile_cache_invalidate(RgGuiTileCache* cache)
{
	if (cache) cache->previous_valid = 0;
}

RGINLINE u64 rg_gui_tile_mix(u64 hash, u64 value)
{
	return rg_gui_renderer_base_mix(hash, value);
}

RGINLINE u64 rg_gui_tile_mix_rect(u64 hash, RgGuiRect rect)
{
	return rg_gui_gpu_signature_bytes(hash, &rect, sizeof(rect));
}

/* Pixel columns/rows a command can touch: the renderer's clipping floors the
 * minimum and ceils the maximum, so bin with the same rounding and never with
 * exact float intersections, which can be empty while the GPU still paints. */
typedef struct RgGuiTilePixelRect
{
	int x0;
	int y0;
	int x1; /* exclusive */
	int y1; /* exclusive */
} RgGuiTilePixelRect;

/* Pixel bounds from edges that already include the draw offset, rounded like
 * the renderer's clipping: floor the minimum, ceil the maximum. NaN and
 * out-of-range values clamp to the output. */
RGINLINE RgGuiTilePixelRect rg_gui_tile_pixel_edges(const RgGuiTileCache* cache,
                                                    f32 x0, f32 y0, f32 x1, f32 y1)
{
	RgGuiTilePixelRect result;
	f32 w = (f32)cache->width;
	f32 h = (f32)cache->height;
	if (!(x0 > 0.0f)) x0 = 0.0f;
	if (!(y0 > 0.0f)) y0 = 0.0f;
	if (!(x1 < w)) x1 = w;
	if (!(y1 < h)) y1 = h;
	if (x0 > w) x0 = w;
	if (y0 > h) y0 = h;
	result.x0 = (int)SDL_floorf(x0);
	result.y0 = (int)SDL_floorf(y0);
	result.x1 = (int)SDL_ceilf(x1);
	result.y1 = (int)SDL_ceilf(y1);
	return result;
}

/* A rectangle's pixel bounds. The far edges are formed as (x + w) + offset,
 * the association rg_gui_gpu_scissor and the geometry vertices use; a
 * different order can round one pixel differently at a tile boundary. */
RGINLINE RgGuiTilePixelRect rg_gui_tile_pixel_rect(const RgGuiTileCache* cache, RgGuiRect rect)
{
	return rg_gui_tile_pixel_edges(cache,
	                               rect.x + cache->offset_x,
	                               rect.y + cache->offset_y,
	                               rect.x + rect.w + cache->offset_x,
	                               rect.y + rect.h + cache->offset_y);
}

RGINLINE RgGuiTilePixelRect rg_gui_tile_pixel_intersect(RgGuiTilePixelRect a, RgGuiTilePixelRect b)
{
	RgGuiTilePixelRect r;
	r.x0 = a.x0 > b.x0 ? a.x0 : b.x0;
	r.y0 = a.y0 > b.y0 ? a.y0 : b.y0;
	r.x1 = a.x1 < b.x1 ? a.x1 : b.x1;
	r.y1 = a.y1 < b.y1 ? a.y1 : b.y1;
	return r;
}

/* Digest one drawing command into every tile its pixel bounds touch, in order. */
RGINLINE void rg_gui_tile_bin(RgGuiTileCache* cache, RgGuiTilePixelRect pixels, u64 digest)
{
	if (pixels.x1 <= pixels.x0 || pixels.y1 <= pixels.y0) return;
	u32 tx0 = (u32)pixels.x0 / RG_GUI_TILE_SIZE;
	u32 ty0 = (u32)pixels.y0 / RG_GUI_TILE_SIZE;
	u32 tx1 = (u32)(pixels.x1 - 1) / RG_GUI_TILE_SIZE;
	u32 ty1 = (u32)(pixels.y1 - 1) / RG_GUI_TILE_SIZE;
	if (tx1 >= cache->columns) tx1 = cache->columns - 1u;
	if (ty1 >= cache->rows) ty1 = cache->rows - 1u;
	for (u32 ty = ty0; ty <= ty1; ty++)
	{
		u64* row = cache->hashes + (size_t)ty * cache->columns;
		for (u32 tx = tx0; tx <= tx1; tx++)
		{
			row[tx] = rg_gui_tile_mix(row[tx], digest);
			cache->stats.tile_updates++;
		}
	}
	cache->stats.commands++;
}

/* Minimum edges in x/y and maximum edges in w/h (edges, not sizes). */
RGINLINE RgGuiRect rg_gui_tile_triangle_extents(rg_vec2 a, rg_vec2 b, rg_vec2 c)
{
	f32 x0 = a.x < b.x ? a.x : b.x;
	f32 y0 = a.y < b.y ? a.y : b.y;
	f32 x1 = a.x > b.x ? a.x : b.x;
	f32 y1 = a.y > b.y ? a.y : b.y;
	if (c.x < x0) x0 = c.x;
	if (c.y < y0) y0 = c.y;
	if (c.x > x1) x1 = c.x;
	if (c.y > y1) y1 = c.y;
	return rg_gui_make_rect(x0, y0, x1, y1);
}

/* Merge dirty tiles into row spans, then merge identical spans in adjacent rows. */
RGINLINE int rg_gui_tile_build_rects(RgGuiTileCache* cache)
{
	u32 count = 0u;
	for (u32 ty = 0u; ty < cache->rows; ty++)
	{
		const u64* current = cache->hashes + (size_t)ty * cache->columns;
		const u64* previous = cache->previous + (size_t)ty * cache->columns;
		u32 tx = 0u;
		while (tx < cache->columns)
		{
			if (current[tx] == previous[tx])
			{
				tx++;
				continue;
			}
			u32 start = tx;
			while (tx < cache->columns && current[tx] != previous[tx]) tx++;
			cache->stats.dirty_tiles += tx - start;
			int x = (int)(start * RG_GUI_TILE_SIZE);
			int w = (int)((tx - start) * RG_GUI_TILE_SIZE);
			int y = (int)(ty * RG_GUI_TILE_SIZE);
			if (x + w > (int)cache->width) w = (int)cache->width - x;
			int h = (int)RG_GUI_TILE_SIZE;
			if (y + h > (int)cache->height) h = (int)cache->height - y;
			/* Extend the span directly above when it has the same columns. */
			if (count > 0u)
			{
				SDL_Rect* last = &cache->rects[count - 1u];
				if (last->x == x && last->w == w && last->y + last->h == y)
				{
					last->h += h;
					continue;
				}
			}
			if (count >= cache->rect_capacity) return 0;
			cache->rects[count].x = x;
			cache->rects[count].y = y;
			cache->rects[count].w = w;
			cache->rects[count].h = h;
			count++;
		}
	}
	cache->stats.rects = count;
	return 1;
}

/**
 * Digest the prepared list into tiles and compare with the presented frame.
 * Call after a successful rg_gui_gpu_prepare for the same list and renderers.
 * `width`/`height` are the output size in pixels and `offset_x`/`offset_y` the
 * draw offset that will be passed to rg_gui_gpu_draw. Returns an RgGuiTileRedraw
 * value; for PARTIAL, `cache->rects[0 .. stats.rects)` are the regions to redraw.
 */
RGINLINE RgGuiTileRedraw rg_gui_tile_cache_update(RgGuiTileCache* cache,
                                                  const RgGuiGpuRenderer* gpu,
                                                  const RgGuiRenderer* text_renderer,
                                                  const RgGuiDrawList* list, u32 overlay_start,
                                                  u32 width, u32 height,
                                                  f32 offset_x, f32 offset_y)
{
	if (!cache || !cache->hashes || !gpu || !text_renderer || !list || (list->count && !list->cmds) ||
	    !width || !height)
	{
		if (cache)
		{
			cache->previous_valid = 0;
			cache->frame_valid = 0;
		}
		return RG_GUI_TILE_REDRAW_FULL;
	}
	u32 columns = (width + RG_GUI_TILE_SIZE - 1u) / RG_GUI_TILE_SIZE;
	u32 rows = (height + RG_GUI_TILE_SIZE - 1u) / RG_GUI_TILE_SIZE;
	u32 tiles = columns * rows;
	RgGuiTileCacheStats* stats = &cache->stats;
	stats->dirty_tiles = 0u;
	stats->rects = 0u;
	stats->commands = 0u;
	stats->tile_updates = 0u;
	stats->total_tiles = tiles;
	if (tiles > cache->tile_capacity || tiles / columns != rows)
	{
		cache->previous_valid = 0;
		cache->frame_valid = 0;
		stats->mode = RG_GUI_TILE_REDRAW_FULL;
		stats->total_full_redraws++;
		return RG_GUI_TILE_REDRAW_FULL;
	}
	int comparable = cache->previous_valid &&
	                 cache->previous_width == width && cache->previous_height == height &&
	                 cache->previous_columns == columns && cache->previous_rows == rows &&
	                 cache->previous_offset_x == offset_x && cache->previous_offset_y == offset_y &&
	                 cache->previous_external_revision == cache->external_revision;
	cache->width = width;
	cache->height = height;
	cache->columns = columns;
	cache->rows = rows;
	cache->offset_x = offset_x;
	cache->offset_y = offset_y;
	for (u32 i = 0u; i < tiles; i++) cache->hashes[i] = 0x243f6a8885a308d3ull;
	cache->frame_valid = 1;

	if (overlay_start > list->count) overlay_start = list->count;
	const RgGuiRendererPrepared* prepared = rg_gui_renderer_prepared(text_renderer);
	const RgGuiRendererBaseCachedQuad* quads = text_renderer->core.cache_quads;
	RgGuiRect clip_stack[RG_GUI_RENDERER_BASE_CLIP_STACK_MAX];
	u32 clip_depth = 0u;
	u32 ignored_pushes = 0u;
	int clip_enabled = 0;
	RgGuiRect clip = {0};
	u32 run_cursor = 0u;

	for (u32 i = 0u; i < list->count; i++)
	{
		if (i == overlay_start)
		{
			clip_depth = 0u;
			ignored_pushes = 0u;
			clip_enabled = 0;
			memset(&clip, 0, sizeof(clip));
		}
		const RgGuiDrawCmd* cmd = &list->cmds[i];
		if (cmd->type == RG_GUI_CMD_CLIP_PUSH)
		{
			if (clip_depth >= RG_GUI_RENDERER_BASE_CLIP_STACK_MAX)
			{
				ignored_pushes++;
				continue;
			}
			clip = clip_enabled ? rg_gui_renderer_base_clip_intersect(clip, cmd->data.clip.rect) : cmd->data.clip.rect;
			clip_stack[clip_depth++] = clip;
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
				if (clip_enabled) clip = clip_stack[clip_depth - 1u];
			}
			continue;
		}

		/* Digest: type, payload (packed color as the GPU reads it), and the clip
		 * in effect. The clip is part of the pixels, so it is part of the digest. */
		u64 digest = rg_gui_tile_mix(0x9e3779b97f4a7c15ull, (u64)cmd->type);
		if (clip_enabled) digest = rg_gui_tile_mix_rect(digest, clip);
		RgGuiRect bounds;
		RgGuiTilePixelRect pixels;
		if (cmd->type == RG_GUI_CMD_RECT)
		{
			bounds = cmd->data.rect.rect;
			digest = rg_gui_tile_mix_rect(digest, bounds);
			digest = rg_gui_tile_mix(digest, cmd->color);
		}
		else if (cmd->type == RG_GUI_CMD_TRIANGLE)
		{
			/* bounds.w and bounds.h carry the maximum edges here, not sizes. */
			bounds = rg_gui_tile_triangle_extents(cmd->data.triangle.a, cmd->data.triangle.b, cmd->data.triangle.c);
			digest = rg_gui_gpu_signature_bytes(digest, &cmd->data.triangle.a, sizeof(cmd->data.triangle.a));
			digest = rg_gui_gpu_signature_bytes(digest, &cmd->data.triangle.b, sizeof(cmd->data.triangle.b));
			digest = rg_gui_gpu_signature_bytes(digest, &cmd->data.triangle.c, sizeof(cmd->data.triangle.c));
			digest = rg_gui_tile_mix(digest, cmd->color);
		}
		else if (cmd->type == RG_GUI_CMD_IMAGE)
		{
			if (!cmd->data.image.texture) continue;
			bounds = cmd->data.image.rect;
			digest = rg_gui_tile_mix_rect(digest, bounds);
			digest = rg_gui_tile_mix_rect(digest, cmd->data.image.uv);
			digest = rg_gui_tile_mix(digest, cmd->color);
			digest = rg_gui_tile_mix(digest, (u64)cmd->data.image.texture);
			digest = rg_gui_tile_mix(digest, (u64)cmd->data.image.material);
		}
		else if (cmd->type == RG_GUI_CMD_TEXT)
		{
			/* Glyph bounds come from the cached quads of this command's runs. */
			if (!prepared || run_cursor >= prepared->run_count ||
			    rg_gui_renderer_run_command_index(&prepared->runs[run_cursor]) != i)
			{
				continue; /* nothing was prepared for this command */
			}
			f32 x0 = 0.0f, y0 = 0.0f, x1 = 0.0f, y1 = 0.0f;
			int any = 0;
			digest = rg_gui_gpu_signature_bytes(digest, &cmd->data.text.pos, sizeof(cmd->data.text.pos));
			digest = rg_gui_tile_mix(digest, cmd->color);
			do
			{
				const RgGuiRendererRun* run = &prepared->runs[run_cursor++];
				digest = rg_gui_tile_mix(digest, ((u64)rg_gui_renderer_run_content_hash(run) << 32u) | run->quad_count);
				for (u32 q = 0u; q < run->quad_count; q++)
				{
					/* The glyph geometry and atlas coordinates are the pixels; a
					 * 32-bit content tag alone can collide between two strings. */
					const RgGuiRendererBaseCachedQuad* quad = &quads[run->first_cached_quad + q];
					digest = rg_gui_gpu_signature_bytes(digest, quad, sizeof(*quad));
					/* The vertex shader forms ((quad + run) + offset) + corner * size. */
					f32 qx0 = quad->x + run->x + cache->offset_x;
					f32 qy0 = quad->y + run->y + cache->offset_y;
					f32 qx1 = qx0 + quad->w;
					f32 qy1 = qy0 + quad->h;
					if (!any)
					{
						x0 = qx0; y0 = qy0; x1 = qx1; y1 = qy1;
						any = 1;
					}
					else
					{
						if (qx0 < x0) x0 = qx0;
						if (qy0 < y0) y0 = qy0;
						if (qx1 > x1) x1 = qx1;
						if (qy1 > y1) y1 = qy1;
					}
				}
			} while (run_cursor < prepared->run_count &&
			         rg_gui_renderer_run_command_index(&prepared->runs[run_cursor]) == i);
			if (!any) continue;
			pixels = rg_gui_tile_pixel_edges(cache, x0, y0, x1, y1);
			bounds = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
		}
		else
		{
			continue;
		}
		if (cmd->type == RG_GUI_CMD_TRIANGLE)
		{
			/* Per-vertex offset in the shader: offset the extreme vertices themselves. */
			pixels = rg_gui_tile_pixel_edges(cache, bounds.x + cache->offset_x, bounds.y + cache->offset_y,
			                                 bounds.w + cache->offset_x, bounds.h + cache->offset_y);
		}
		else if (cmd->type != RG_GUI_CMD_TEXT)
		{
			pixels = rg_gui_tile_pixel_rect(cache, bounds);
		}
		if (clip_enabled) pixels = rg_gui_tile_pixel_intersect(pixels, rg_gui_tile_pixel_rect(cache, clip));
		rg_gui_tile_bin(cache, pixels, digest);
	}

	if (!comparable)
	{
		stats->mode = RG_GUI_TILE_REDRAW_FULL;
		stats->total_full_redraws++;
		return RG_GUI_TILE_REDRAW_FULL;
	}
	if (!rg_gui_tile_build_rects(cache))
	{
		stats->rects = 0u;
		stats->mode = RG_GUI_TILE_REDRAW_FULL;
		stats->total_full_redraws++;
		return RG_GUI_TILE_REDRAW_FULL;
	}
	if (stats->dirty_tiles == 0u)
	{
		stats->mode = RG_GUI_TILE_REDRAW_NONE;
		stats->total_skipped_frames++;
		return RG_GUI_TILE_REDRAW_NONE;
	}
	stats->mode = RG_GUI_TILE_REDRAW_PARTIAL;
	stats->total_partial_redraws++;
	return RG_GUI_TILE_REDRAW_PARTIAL;
}

/** Record that the updated frame now fills the persistent target. After an
 * update that could not digest the frame (storage too small, bad arguments)
 * there is nothing to compare against, so the cache stays invalid. */
RGINLINE void rg_gui_tile_cache_commit(RgGuiTileCache* cache)
{
	if (!cache || !cache->hashes) return;
	if (!cache->frame_valid)
	{
		cache->previous_valid = 0;
		return;
	}
	u64* swap = cache->previous;
	cache->previous = cache->hashes;
	cache->hashes = swap;
	cache->previous_valid = 1;
	cache->frame_valid = 0;
	cache->previous_external_revision = cache->external_revision;
	cache->previous_width = cache->width;
	cache->previous_height = cache->height;
	cache->previous_columns = cache->columns;
	cache->previous_rows = cache->rows;
	cache->previous_offset_x = cache->offset_x;
	cache->previous_offset_y = cache->offset_y;
}

RGINLINE const RgGuiTileCacheStats* rg_gui_tile_cache_stats(const RgGuiTileCache* cache)
{
	return cache ? &cache->stats : NULL;
}

#endif // RG_GUI_GPU_TILES_H
