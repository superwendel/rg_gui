// rg_gui_frame - renderer-neutral frame building for rg_gui draw lists
//
// Part of the Reverse Gravity (rg_) libraries.
// Turns an rg_gui draw list into one packed buffer that a GPU backend uploads
// and draws with the indexed vertex-pull shader (rg_gui_indexed.vert/.frag):
// text runs, geometry vertices, 8-byte primitive references, and a clip table,
// plus painter-ordered batches that split only where the texture or an image
// material changes. Pure CPU; no graphics API, no allocation.
//
// USAGE:
//   RgGuiFrameBuilder builder = {0};     // point vertices/items/clips at storage,
//                                        // set the capacities and atlas_texture
//   rg_gui_renderer_begin_frame(&text);
//   rg_gui_frame_prepare(&builder, &text, list, overlay_start);
//   RgGuiFramePacket packet = {0};
//   packet.batches = batches;            // at least item_capacity entries
//   packet.batch_capacity = item_capacity;
//   rg_gui_frame_pack(&builder, bytes, builder.frame_capacity, &packet);
//   // upload packet.size bytes and the glyph cache, then per batch draw
//   // ref_count * 6 indices from first_ref * 6 (indices 0,1,2,0,2,3 per quad).
//
// NOTES:
//   - The record types keep their RgGuiGpu* names: they are the buffer format
//     shared by every backend (SDL GPU in rg_gui_gpu.h, D3D11 in rg_gui_d3d11.h).
//   - All functions have internal linkage and work in unity builds.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_GUI_FRAME_H
#define RG_GUI_FRAME_H

#include "rg_gui_renderer.h"

#include <string.h>

// Optional rectangle/image packing backend. SSE2 is baseline on x64; 32-bit
// x86 builds must enable SSE2 in their compiler target. Other targets use C.
#ifndef RG_GUI_GPU_USE_SSE2
#define RG_GUI_GPU_USE_SSE2 0
#endif

#if RG_GUI_GPU_USE_SSE2 && (defined(__SSE2__) || defined(_M_X64) || \
    (defined(_M_IX86_FP) && _M_IX86_FP >= 2))
#include <emmintrin.h>
#define RG_GUI_GPU_SSE2_ENABLED 1
#else
#define RG_GUI_GPU_SSE2_ENABLED 0
#endif

typedef struct RgGuiGpuVertex
{
	f32 x;
	f32 y;
	u32 color;
	f32 u;
	f32 v;
} RgGuiGpuVertex;

typedef char RgGuiGpuVertexMustBe20Bytes[(sizeof(RgGuiGpuVertex) == 20u) ? 1 : -1];

/* SOLID and IMAGE items hold quads of four vertices (two triangles each);
 * TRIANGLES items hold a plain triangle list. */
typedef enum RgGuiGpuItemType
{
	RG_GUI_GPU_ITEM_SOLID = 0,
	RG_GUI_GPU_ITEM_IMAGE = 1,
	RG_GUI_GPU_ITEM_TEXT = 2,
	RG_GUI_GPU_ITEM_TRIANGLES = 3
} RgGuiGpuItemType;

#define RG_GUI_GPU_QUAD_VERTICES 4u

typedef struct RgGuiGpuItem
{
	RgGuiRect clip;
	RgGuiTexture texture;
	RgGuiImageMaterial material;
	u32 first;
	u32 count;
	u32 type;
	/* Zero when unclipped; otherwise the item's entry in the frame clip table. */
	u32 clip_index;
} RgGuiGpuItem;

/* The indexed frame buffer holds, in order: one RgGuiGpuFrameRun per prepared
 * text run, the geometry vertices, the 8-byte references, and the clip table.
 * Every reference names a clip-table entry (a text glyph through its run), so
 * the vertex shader clips each primitive and a clip change does not end a
 * draw call. */
typedef struct RgGuiGpuFrameRun
{
	f32 x;
	f32 y;
	u32 color;
	u32 clip;
} RgGuiGpuFrameRun;

typedef char RgGuiGpuFrameRunMustBe16Bytes[(sizeof(RgGuiGpuFrameRun) == 16u) ? 1 : -1];

/* A clip-table entry: the clip rectangle's edges in draw-list coordinates. The
 * shader adds the draw offset, floors the minimum, and ceils the maximum, the
 * same rounding as rg_gui_gpu_scissor. Entry 0 is unclipped. */
typedef struct RgGuiGpuFrameClip
{
	f32 x0;
	f32 y0;
	f32 x1;
	f32 y1;
} RgGuiGpuFrameClip;

typedef char RgGuiGpuFrameClipMustBe16Bytes[(sizeof(RgGuiGpuFrameClip) == 16u) ? 1 : -1];

/* Geometry references carry the clip index in bits 33..61. */
#define RG_GUI_GPU_FRAME_CLIP_LIMIT 0x20000000u
#define RG_GUI_GPU_UNCLIPPED 1.0e30f

RGINLINE f32 rg_gui_gpu_clamp_edge(f32 edge)
{
	return edge < -RG_GUI_GPU_UNCLIPPED ? -RG_GUI_GPU_UNCLIPPED :
	       edge > RG_GUI_GPU_UNCLIPPED ? RG_GUI_GPU_UNCLIPPED : edge;
}

/** Clip-table edges for a clip rectangle. The far edges are formed as x + w, the
 * association rg_gui_gpu_scissor uses. A NaN edge draws nothing, as the empty
 * scissor it produced did; infinities clamp so the shader's distances stay finite. */
RGINLINE RgGuiGpuFrameClip rg_gui_gpu_frame_clip(RgGuiRect clip)
{
	RgGuiGpuFrameClip edges = {clip.x, clip.y, clip.x + clip.w, clip.y + clip.h};
	if (edges.x0 != edges.x0 || edges.y0 != edges.y0 || edges.x1 != edges.x1 || edges.y1 != edges.y1)
	{
		edges.x0 = edges.y0 = RG_GUI_GPU_UNCLIPPED;
		edges.x1 = edges.y1 = -RG_GUI_GPU_UNCLIPPED;
		return edges;
	}
	edges.x0 = rg_gui_gpu_clamp_edge(edges.x0);
	edges.y0 = rg_gui_gpu_clamp_edge(edges.y0);
	edges.x1 = rg_gui_gpu_clamp_edge(edges.x1);
	edges.y1 = rg_gui_gpu_clamp_edge(edges.y1);
	return edges;
}

/* Bitwise equality of two 16-byte clip records, without a library call. */
RGINLINE int rg_gui_gpu_clip_bits_equal(const void* a, const void* b)
{
	u64 a0, a1, b0, b1;
	memcpy(&a0, a, 8u);
	memcpy(&a1, (const u8*)a + 8u, 8u);
	memcpy(&b0, b, 8u);
	memcpy(&b1, (const u8*)b + 8u, 8u);
	return a0 == b0 && a1 == b1;
}

/** Bytes of one indexed frame buffer for the given capacities. A frame's clip
 * table has at most one entry per item after the unclipped entry 0. */
RGINLINE u64 rg_gui_gpu_frame_bytes(u64 runs, u64 vertices, u64 refs, u64 clips)
{
	u64 bytes = runs * sizeof(RgGuiGpuFrameRun) + vertices * sizeof(RgGuiGpuVertex);
	return ((bytes + 7u) & ~(u64)7u) + refs * 8u + clips * sizeof(RgGuiGpuFrameClip);
}

/** Immutable painter-ordered draw span. Nonzero material is a callback boundary.
 * A material batch is clipped by the scissor in clip/clip_enabled. A stock batch
 * has clip_enabled zero: the shader clips each primitive to its own clip, so one
 * batch can span any number of clip changes. */
typedef struct RgGuiGpuIndexedBatch
{
	RgGuiRect clip;
	RgGuiTexture texture;
	RgGuiImageMaterial material;
	u32 first_ref;
	u32 ref_count;
	u32 first_vertex;
	u32 geometry_vertices;
	u32 glyph_count;
	u32 source_items;
	u32 clip_enabled;
} RgGuiGpuIndexedBatch;

/** CPU state of one frame: what rg_gui_frame_prepare builds and
 * rg_gui_frame_pack writes out. The arrays are caller-owned. */
typedef struct RgGuiFrameBuilder
{
	RgGuiGpuVertex* vertices;
	RgGuiGpuItem* items;
	u32 vertex_count;
	u32 vertex_capacity;
	u32 item_count;
	u32 item_capacity;
	/* The prepared frame's clip table; holds item_capacity + 1 entries. */
	RgGuiGpuFrameClip* clips;
	u32 clip_count;
	u32 clip_capacity;
	const RgGuiRendererPrepared* text_prepared;
	/* Texture handle stamped on text batches: the glyph atlas. */
	RgGuiTexture atlas_texture;
	/* Text runs one frame may hold, and glyph-cache quads it may reference. */
	u32 run_capacity;
	u32 cached_quad_capacity;
	/* References and bytes one packed frame may hold (see rg_gui_gpu_frame_bytes). */
	u32 ref_capacity;
	u32 frame_capacity;
	/* Digest of the last successfully prepared draw list; see rg_gui_frame_signature. */
	u64 frame_signature;
	u32 frame_signature_enabled;
} RgGuiFrameBuilder;

/** One packed frame. batches/batch_capacity are caller-owned input (one batch
 * per item suffices); everything else is written by rg_gui_frame_pack. Byte
 * offsets are into the packed buffer: runs at 0, then geometry, references, and
 * the clip table (clip_count entries). */
typedef struct RgGuiFramePacket
{
	RgGuiGpuIndexedBatch* batches;
	u32 batch_capacity;
	u32 batch_count;
	u32 run_count;
	u32 glyph_count;
	u32 vertex_count;
	u32 item_count;
	u32 ref_count;
	u32 geometry_offset;
	u32 refs_offset;
	u32 clips_offset;
	u32 clip_count;
	u32 size;
} RgGuiFramePacket;

RGINLINE int rg_gui_frame_clip_equal(RgGuiRect a, RgGuiRect b)
{
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

/* A clip in effect whose clip-table entry is not chosen yet. */
#define RG_GUI_FRAME_CLIP_PENDING UINT32_MAX

/* Choose the clip-table entry for a clip the first time something draws under
 * it: the latest entry when its edges match, else a new one. The table grows
 * only with clips that draw, by at most one entry per item, so items can merge
 * by comparing entries. Returns RG_GUI_FRAME_CLIP_PENDING when the table is full.
 * Kept out of line: it runs once per clip change, not per command. */
static RG_NOINLINE u32 rg_gui_frame_clip_entry(RgGuiFrameBuilder* builder, RgGuiRect clip)
{
	const RgGuiGpuFrameClip edges = rg_gui_gpu_frame_clip(clip);
	const u32 last = builder->clip_count - 1u;
	if (builder->clip_count > 1u && rg_gui_gpu_clip_bits_equal(&builder->clips[last], &edges)) return last;
	if (builder->clip_count >= builder->clip_capacity) return RG_GUI_FRAME_CLIP_PENDING;
	builder->clips[builder->clip_count] = edges;
	return builder->clip_count++;
}

/* clip_index is 0 when unclipped, else the clip's table entry (never pending). */
RGINLINE int rg_gui_frame_add_item(RgGuiFrameBuilder* builder, u32 type,
                                 RgGuiTexture texture, RgGuiImageMaterial material,
                                 u32 first, u32 count,
                                 u32 clip_index, RgGuiRect clip)
{
	if (!count) return 1;
	if (builder->item_count)
	{
		RgGuiGpuItem* previous = &builder->items[builder->item_count - 1u];
		if (previous->type == type && previous->texture == texture &&
		    previous->material == material && previous->clip_index == clip_index &&
		    previous->first + previous->count == first)
		{
			previous->count += count;
			return 1;
		}
	}
	if (builder->item_count >= builder->item_capacity) return 0;
	RgGuiGpuItem* item = &builder->items[builder->item_count++];
	item->clip = clip;
	item->texture = texture;
	item->material = material;
	item->first = first;
	item->count = count;
	item->type = type;
	item->clip_index = clip_index;
	return 1;
}

/* Resolve a pending clip before the first item drawn under it. */
#define RG_GUI_FRAME_RESOLVE_CLIP(builder, entry, clip)                                 \
	(RG_LIKELY((entry) != RG_GUI_FRAME_CLIP_PENDING) ||                                  \
	 ((entry) = rg_gui_frame_clip_entry((builder), (clip))) != RG_GUI_FRAME_CLIP_PENDING)

RGINLINE void rg_gui_frame_set_vertex(RgGuiGpuVertex* vertex, f32 x, f32 y,
                                    u32 color, f32 u, f32 v)
{
	vertex->x = x;
	vertex->y = y;
	vertex->color = color;
	vertex->u = u;
	vertex->v = v;
}

/* Emit one quad as four vertices in the order the indexed vertex shader expects:
 * top-left, top-right, bottom-right, bottom-left. */
RGINLINE int rg_gui_frame_add_rect_vertices(RgGuiFrameBuilder* builder,
                                          RgGuiRect rect, RgGuiRect uv, u32 color)
{
	if (builder->vertex_count > builder->vertex_capacity ||
	    builder->vertex_capacity - builder->vertex_count < RG_GUI_GPU_QUAD_VERTICES)
		return 0;
	RgGuiGpuVertex* out = &builder->vertices[builder->vertex_count];
#if RG_GUI_GPU_SSE2_ENABLED
	// Zero the unused lanes so packing does not perform extra arithmetic on
	// rectangle widths/heights (which could overflow even when x+w is finite).
	__m128 r = _mm_castsi128_ps(_mm_loadl_epi64((const __m128i*)&rect.x));
	__m128 r1 = _mm_add_ps(r, _mm_castsi128_ps(_mm_loadl_epi64((const __m128i*)&rect.w)));
	__m128 t = _mm_castsi128_ps(_mm_loadl_epi64((const __m128i*)&uv.x));
	__m128 t1 = _mm_add_ps(t, _mm_castsi128_ps(_mm_loadl_epi64((const __m128i*)&uv.w)));
	__m128 c = _mm_castsi128_ps(_mm_cvtsi32_si128((int)color));
	__m128 cu0 = _mm_unpacklo_ps(c, t);
	__m128 cu1 = _mm_unpacklo_ps(c, t1);
	__m128 v00 = _mm_movelh_ps(r, cu0);
	__m128 v10 = _mm_movelh_ps(_mm_move_ss(r, r1), cu1);
	__m128 v11 = _mm_movelh_ps(r1, cu1);
	__m128 v01 = _mm_movelh_ps(_mm_move_ss(r1, r), cu0);
	_mm_storeu_ps((f32*)&out[0], v00);
	out[0].v = uv.y;
	_mm_storeu_ps((f32*)&out[1], v10);
	out[1].v = uv.y;
	_mm_storeu_ps((f32*)&out[2], v11);
	out[2].v = uv.y + uv.h;
	_mm_storeu_ps((f32*)&out[3], v01);
	out[3].v = uv.y + uv.h;
#else
	rg_gui_frame_set_vertex(&out[0], rect.x, rect.y, color, uv.x, uv.y);
	rg_gui_frame_set_vertex(&out[1], rect.x + rect.w, rect.y, color, uv.x + uv.w, uv.y);
	rg_gui_frame_set_vertex(&out[2], rect.x + rect.w, rect.y + rect.h, color, uv.x + uv.w, uv.y + uv.h);
	rg_gui_frame_set_vertex(&out[3], rect.x, rect.y + rect.h, color, uv.x, uv.y + uv.h);
#endif
	builder->vertex_count += RG_GUI_GPU_QUAD_VERTICES;
	return 1;
}

/** Mix 4-byte-multiple payload bytes (rects, colors, positions) into a digest. */
RGINLINE u64 rg_gui_frame_signature_bytes(u64 hash, const void* data, size_t size)
{
	const u8* bytes = (const u8*)data;
	while (size >= 8u)
	{
		u64 word;
		memcpy(&word, bytes, sizeof(word));
		hash = rg_gui_renderer_base_mix(hash, word);
		bytes += 8u;
		size -= 8u;
	}
	if (size)
	{
		u32 tail = 0u;
		memcpy(&tail, bytes, size < 4u ? size : 4u);
		hash = rg_gui_renderer_base_mix(hash, tail);
	}
	return hash;
}

/** Digest of the last successful rg_gui_frame_prepare, when enabled through
 * frame_signature_enabled or rg_gui_frame_set_signature (zero when
 * disabled or after a failed prepare): command types and order,
 * geometry, colors, clips, textures, materials, text scale/content/identity,
 * command count, and the overlay boundary. Two frames with equal signatures
 * draw identical pixels for the same output size, offset, clear color, atlas,
 * and image texture contents, so the caller may skip staging, encoding,
 * drawing, and presenting. Text keyed by identity (static labels) is covered
 * by its address; changing the bytes behind a static label is already outside
 * the API contract. Distinct lists collide with 64-bit probability only. */
RGINLINE u64 rg_gui_frame_signature(const RgGuiFrameBuilder* builder)
{
	return builder && builder->frame_signature_enabled ? builder->frame_signature : 0u;
}

/** Enable or disable signature computation (frame_signature_enabled). */
RGINLINE void rg_gui_frame_set_signature(RgGuiFrameBuilder* builder, int enabled)
{
	if (builder)
	{
		builder->frame_signature_enabled = enabled ? 1u : 0u;
		builder->frame_signature = 0u;
	}
}

/** Turn a draw list into this frame's vertices, items, and clip table, and
 * prepare its text through text_renderer. Returns 0 when a capacity is exceeded
 * (the builder is then empty). */
RGINLINE int rg_gui_frame_prepare(RgGuiFrameBuilder* builder, RgGuiRenderer* text_renderer,
                                  const RgGuiDrawList* list, u32 overlay_start)
{
	/* Any failure, including rejected arguments, leaves no stale digest behind. */
	if (builder) builder->frame_signature = 0u;
	if (!builder || !text_renderer || !list || (list->count && !list->cmds) ||
	    !builder->clips || !builder->clip_capacity) return 0;
	builder->vertex_count = 0u;
	builder->item_count = 0u;
	builder->text_prepared = NULL;
	const RgGuiGpuFrameClip unclipped = {-RG_GUI_GPU_UNCLIPPED, -RG_GUI_GPU_UNCLIPPED,
	                                     RG_GUI_GPU_UNCLIPPED, RG_GUI_GPU_UNCLIPPED};
	builder->clips[0] = unclipped;
	builder->clip_count = 1u;
	if (!rg_gui_renderer_prepare(text_renderer, list, overlay_start)) return 0;
	builder->text_prepared = rg_gui_renderer_prepared(text_renderer);
	if (!builder->text_prepared) return 0;

	if (overlay_start > list->count) overlay_start = list->count;
	RgGuiRect clip_stack[RG_GUI_RENDERER_BASE_CLIP_STACK_MAX];
	u32 clip_depth = 0u;
	u32 ignored_pushes = 0u;
	int clip_enabled = 0;
	RgGuiRect clip = {0};
	u32 clip_entry = 0u;
	u32 run_cursor = 0u;
	const int sign = builder->frame_signature_enabled != 0u;
	u64 signature = rg_gui_renderer_base_mix(0x9e3779b97f4a7c15ull, ((u64)list->count << 32u) | overlay_start);

	for (u32 i = 0u; i < list->count; i++)
	{
		if (i == overlay_start)
		{
			clip_depth = 0u;
			ignored_pushes = 0u;
			clip_enabled = 0;
			memset(&clip, 0, sizeof(clip));
			clip_entry = 0u;
		}

		const RgGuiDrawCmd* cmd = &list->cmds[i];
		if (sign) signature = rg_gui_renderer_base_mix(signature, (u64)cmd->type);
		if (cmd->type == RG_GUI_CMD_CLIP_PUSH)
		{
			if (sign) signature = rg_gui_frame_signature_bytes(signature, &cmd->data.clip.rect, sizeof(cmd->data.clip.rect));
			if (clip_depth >= RG_GUI_RENDERER_BASE_CLIP_STACK_MAX)
			{
				ignored_pushes++;
				continue;
			}
			clip = clip_enabled ? rg_gui_renderer_base_clip_intersect(clip, cmd->data.clip.rect) : cmd->data.clip.rect;
			clip_stack[clip_depth++] = clip;
			clip_enabled = 1;
			clip_entry = RG_GUI_FRAME_CLIP_PENDING;
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
				clip_entry = clip_enabled ? RG_GUI_FRAME_CLIP_PENDING : 0u;
			}
			continue;
		}

		if (cmd->type == RG_GUI_CMD_RECT)
		{
			u32 first = builder->vertex_count;
			u32 color = cmd->color;
			if (sign)
			{
				/* The packed color is what the GPU reads, so it is what the digest covers. */
				signature = rg_gui_frame_signature_bytes(signature, &cmd->data.rect.rect, sizeof(cmd->data.rect.rect));
				signature = rg_gui_renderer_base_mix(signature, color);
			}
			RgGuiRect uv = {0};
			if (!RG_GUI_FRAME_RESOLVE_CLIP(builder, clip_entry, clip) ||
			    !rg_gui_frame_add_rect_vertices(builder, cmd->data.rect.rect, uv, color) ||
			    !rg_gui_frame_add_item(builder, RG_GUI_GPU_ITEM_SOLID, 0u, 0u,
			                         first, RG_GUI_GPU_QUAD_VERTICES,
			                         clip_entry, clip))
				goto capacity_failure;
			continue;
		}
		if (cmd->type == RG_GUI_CMD_TRIANGLE)
		{
			if (builder->vertex_count > builder->vertex_capacity ||
			    builder->vertex_capacity - builder->vertex_count < 3u)
				goto capacity_failure;
			u32 first = builder->vertex_count;
			u32 color = cmd->color;
			if (sign)
			{
				signature = rg_gui_frame_signature_bytes(signature, &cmd->data.triangle.a, sizeof(cmd->data.triangle.a));
				signature = rg_gui_frame_signature_bytes(signature, &cmd->data.triangle.b, sizeof(cmd->data.triangle.b));
				signature = rg_gui_frame_signature_bytes(signature, &cmd->data.triangle.c, sizeof(cmd->data.triangle.c));
				signature = rg_gui_renderer_base_mix(signature, color);
			}
			RgGuiGpuVertex* out = &builder->vertices[first];
			rg_gui_frame_set_vertex(&out[0], cmd->data.triangle.a.x, cmd->data.triangle.a.y, color, 0.0f, 0.0f);
			rg_gui_frame_set_vertex(&out[1], cmd->data.triangle.b.x, cmd->data.triangle.b.y, color, 0.0f, 0.0f);
			rg_gui_frame_set_vertex(&out[2], cmd->data.triangle.c.x, cmd->data.triangle.c.y, color, 0.0f, 0.0f);
			builder->vertex_count += 3u;
			if (!RG_GUI_FRAME_RESOLVE_CLIP(builder, clip_entry, clip) ||
			    !rg_gui_frame_add_item(builder, RG_GUI_GPU_ITEM_TRIANGLES, 0u, 0u,
			                         first, 3u,
			                         clip_entry, clip))
				goto capacity_failure;
			continue;
		}
		if (cmd->type == RG_GUI_CMD_IMAGE)
		{
			if (!cmd->data.image.texture) continue;
			u32 first = builder->vertex_count;
			u32 color = cmd->color;
			if (sign)
			{
				signature = rg_gui_frame_signature_bytes(signature, &cmd->data.image.rect, sizeof(cmd->data.image.rect));
				signature = rg_gui_frame_signature_bytes(signature, &cmd->data.image.uv, sizeof(cmd->data.image.uv));
				signature = rg_gui_renderer_base_mix(signature, color);
				signature = rg_gui_renderer_base_mix(signature, (u64)cmd->data.image.texture);
				signature = rg_gui_renderer_base_mix(signature, (u64)cmd->data.image.material);
			}
			if (!RG_GUI_FRAME_RESOLVE_CLIP(builder, clip_entry, clip) ||
			    !rg_gui_frame_add_rect_vertices(builder, cmd->data.image.rect,
			                                  cmd->data.image.uv, color) ||
			    !rg_gui_frame_add_item(builder, RG_GUI_GPU_ITEM_IMAGE,
			                         cmd->data.image.texture, cmd->data.image.material,
			                         first, RG_GUI_GPU_QUAD_VERTICES,
			                         clip_entry, clip))
				goto capacity_failure;
			continue;
		}
		if (cmd->type == RG_GUI_CMD_TEXT)
		{
			if (sign)
			{
				signature = rg_gui_frame_signature_bytes(signature, &cmd->data.text.pos, sizeof(cmd->data.text.pos));
				signature = rg_gui_renderer_base_mix(signature, cmd->color);
			}
			if (run_cursor >= builder->text_prepared->run_count) continue;
			const RgGuiRendererRun* first_run = &builder->text_prepared->runs[run_cursor];
			/* A skipped command has no segments. Match the recorded source index
			   so identical positions/colors cannot consume another command's text. */
			if (rg_gui_renderer_run_command_index(first_run) != i) continue;

			u32 glyph_count = 0u;
			u32 first_glyph = first_run->first_output_instance;
			do
			{
				glyph_count += builder->text_prepared->runs[run_cursor++].quad_count;
			} while (run_cursor < builder->text_prepared->run_count &&
			         rg_gui_renderer_run_command_index(&builder->text_prepared->runs[run_cursor]) == i);
			if (glyph_count &&
			    (!RG_GUI_FRAME_RESOLVE_CLIP(builder, clip_entry, clip) ||
			     !rg_gui_frame_add_item(builder, RG_GUI_GPU_ITEM_TEXT, 0u, 0u,
			                          first_glyph, glyph_count, clip_entry, clip)))
				goto capacity_failure;
		}
	}
	if (sign)
	{
		/* Text scale and bytes (or identity) enter through the builder's key digest. */
		signature = rg_gui_renderer_base_mix(signature, text_renderer->core.frame_content_hash);
		signature ^= signature >> 33u;
		signature *= 0xff51afd7ed558ccdull;
		signature ^= signature >> 33u;
		builder->frame_signature = signature ? signature : 1u;
	}
	return 1;

capacity_failure:
	builder->vertex_count = 0u;
	builder->item_count = 0u;
	builder->clip_count = 0u;
	builder->text_prepared = NULL;
	builder->frame_signature = 0u;
	return 0;
}

RGINLINE int rg_gui_frame_pack(const RgGuiFrameBuilder* builder,
                               u8* frame, u32 capacity, RgGuiFramePacket* packet)
{
	packet->ref_count = packet->batch_count = packet->clip_count = packet->size = 0u;
	packet->clips_offset = 0u;
	if (!builder || !builder->text_prepared || builder->text_prepared->run_count > builder->run_capacity ||
	    builder->vertex_count > builder->vertex_capacity ||
	    !builder->clip_count || builder->clip_count > builder->clip_capacity ||
	    builder->clip_count > RG_GUI_GPU_FRAME_CLIP_LIMIT) return 0;
	const RgGuiRendererPrepared* text = builder->text_prepared;
	packet->run_count = text->run_count;
	packet->glyph_count = text->glyph_count;
	packet->vertex_count = builder->vertex_count;
	packet->item_count = builder->item_count;
	packet->geometry_offset = packet->run_count * (u32)sizeof(RgGuiGpuFrameRun);
	const u32 geometry_bytes = packet->vertex_count * (u32)sizeof(RgGuiGpuVertex);
	packet->refs_offset = (packet->geometry_offset + geometry_bytes + 7u) & ~7u;
	/* The clip table follows the references, which may not use its space. */
	const u32 clip_reserve = builder->item_count ? builder->clip_count * (u32)sizeof(RgGuiGpuFrameClip) : 0u;
	if (packet->refs_offset > capacity || clip_reserve > capacity - packet->refs_offset) return 0;
	u32 ref_limit = (capacity - packet->refs_offset - clip_reserve) / 8u;
	if (ref_limit > builder->ref_capacity) ref_limit = builder->ref_capacity;
	if (geometry_bytes) memcpy(frame + packet->geometry_offset, builder->vertices, geometry_bytes);
	/* Initialize the at-most-four-byte alignment pad included in the packet. */
	const u32 padding = packet->refs_offset - packet->geometry_offset - geometry_bytes;
	if (padding) memset(frame + packet->geometry_offset + geometry_bytes, 0, padding);
	RgGuiGpuFrameRun* runs = (RgGuiGpuFrameRun*)(void*)frame;
	u8* refs = frame ? frame + packet->refs_offset : NULL;
	u32 cursor = 0u;
	for (u32 n = 0u; n < builder->item_count; ++n)
	{
		const RgGuiGpuItem* source = &builder->items[n];
		const u32 first = packet->ref_count;
		const u32 clip = source->clip_index;
		if (clip >= builder->clip_count) goto failure;
		if (source->type == RG_GUI_GPU_ITEM_TEXT)
		{
			if (source->first > text->glyph_count || source->count > text->glyph_count - source->first)
				goto failure;
			const u32 end = source->first + source->count;
			while (cursor < text->run_count && text->runs[cursor].first_output_instance < end)
			{
				const u32 index = cursor++;
				const RgGuiRendererRun* run = &text->runs[index];
				if (run->first_output_instance < source->first ||
				    run->quad_count > end - run->first_output_instance ||
				    run->quad_count > ref_limit - packet->ref_count ||
				    run->first_cached_quad > builder->cached_quad_capacity ||
				    run->quad_count > builder->cached_quad_capacity - run->first_cached_quad)
					goto failure;
				RgGuiGpuFrameRun record = {run->x, run->y, run->color, clip};
				memcpy(&runs[index], &record, sizeof(record));
				/* Addition changes only the low cached-quad index. No floats. */
				const u64 base = ((u64)index << 32u) | run->first_cached_quad;
				for (u32 j = 0u; j < run->quad_count; ++j)
				{
					u64 reference = base + j;
					memcpy(refs + (packet->ref_count + j) * 8u, &reference, sizeof(reference));
				}
				packet->ref_count += run->quad_count;
			}
			if (packet->ref_count - first != source->count) goto failure;
		}
		else
		{
			/* SOLID and IMAGE items are quads of four vertices; TRIANGLES items
			 * are plain triangle lists. One reference per primitive. */
			const int quad = source->type != RG_GUI_GPU_ITEM_TRIANGLES;
			const u32 stride = quad ? RG_GUI_GPU_QUAD_VERTICES : 3u;
			if ((source->type != RG_GUI_GPU_ITEM_SOLID && source->type != RG_GUI_GPU_ITEM_IMAGE &&
			     source->type != RG_GUI_GPU_ITEM_TRIANGLES) ||
			    source->count % stride || source->first > builder->vertex_count ||
			    source->count > builder->vertex_count - source->first) goto failure;
			/* Word1: kind in bits 30..31, clip index in bits 1..29, quad flag in
			 * bit 0. Text keeps its run index in word1 and its clip in the run. */
			const u64 kind = (source->type == RG_GUI_GPU_ITEM_IMAGE ? (u64)2u << 62u : (u64)1u << 62u) |
			                 ((u64)clip << 33u) | ((u64)(quad != 0) << 32u);
			u32 offset = source->first, remaining = source->count;
			while (remaining)
			{
				if (packet->ref_count >= ref_limit) goto failure;
				u64 reference = kind | offset;
				memcpy(refs + packet->ref_count++ * 8u, &reference, sizeof(reference));
				offset += stride;
				remaining -= stride;
			}
		}
		const u32 count = packet->ref_count - first;
		if (!count) continue;
		const RgGuiTexture texture = source->type == RG_GUI_GPU_ITEM_IMAGE ? source->texture : builder->atlas_texture;
		RgGuiGpuIndexedBatch* batch = packet->batch_count ? &packet->batches[packet->batch_count - 1u] : NULL;
		const int custom = source->type == RG_GUI_GPU_ITEM_IMAGE && source->material != 0u;
		/* The shader clips stock batches per primitive, so only a texture or a
		 * material boundary ends one. A material batch keeps its scissor clip. */
		if (!batch || batch->material || custom || batch->texture != texture)
		{
			if (packet->batch_count >= packet->batch_capacity) goto failure;
			batch = &packet->batches[packet->batch_count++];
			memset(batch, 0, sizeof(*batch));
			batch->first_ref = first;
			batch->texture = texture;
			batch->material = custom ? source->material : 0u;
			batch->first_vertex = source->first;
			if (custom && clip)
			{
				batch->clip_enabled = 1u;
				batch->clip = source->clip;
			}
		}
		batch->ref_count += count;
		++batch->source_items;
		if (source->type == RG_GUI_GPU_ITEM_TEXT) batch->glyph_count += source->count;
		else batch->geometry_vertices += source->count;
	}
	/* Runs after the last text item are never referenced; keep their bytes defined. */
	for (; cursor < text->run_count; ++cursor)
	{
		const RgGuiRendererRun* run = &text->runs[cursor];
		RgGuiGpuFrameRun record = {run->x, run->y, run->color, 0u};
		memcpy(&runs[cursor], &record, sizeof(record));
	}
	packet->size = packet->refs_offset + packet->ref_count * 8u;
	if (builder->item_count)
	{
		if (packet->size > capacity || clip_reserve > capacity - packet->size) goto failure;
		packet->clips_offset = packet->size;
		memcpy(frame + packet->clips_offset, builder->clips, clip_reserve);
		packet->clip_count = builder->clip_count;
		packet->size += clip_reserve;
	}
	if (packet->size > capacity) goto failure;
	return 1;
failure:
	packet->ref_count = packet->batch_count = packet->clip_count = packet->size = 0u;
	packet->clips_offset = 0u;
	return 0;
}

#endif // RG_GUI_FRAME_H
