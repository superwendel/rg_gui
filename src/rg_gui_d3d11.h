// rg_gui_d3d11 - Direct3D 11 renderer for rg_gui frames
//
// Part of the Reverse Gravity (rg_) libraries.
// Draws the packed frames of rg_gui_frame.h with the shared indexed vertex-pull
// shaders (rg_gui_indexed.vert/.frag, compiled to DXBC in
// rg_gui_d3d11_shaders.h, so nothing is compiled at run time): one raw buffer
// holds the glyph-cache quads, one the packed frame, an index buffer expands
// each reference to a quad, and each batch is one DrawIndexed. Clipping is done
// in the vertex shader, so a frame needs no scissor changes.
//
// USAGE:
//   RgGuiD3d11Desc desc = {0};
//   desc.device = device;
//   desc.context = context;
//   desc.arena = &arena;                 // CPU storage, allocated once
//   desc.atlas_width = desc.atlas_height = 1024u;
//   desc.max_cached_quads = ...;         // limits as for rg_gui_gpu_create
//   rg_gui_d3d11_init(&renderer, &desc);
//
//   // each frame, with the render target and viewport bound:
//   rg_gui_renderer_begin_frame(&text);
//   rg_gui_d3d11_prepare(&renderer, &text, list, overlay_start);
//   rg_gui_d3d11_update_atlas(&renderer, pixels, pitch, x0, y0, x1, y1); // new glyphs
//   rg_gui_d3d11_draw(&renderer, &text, &draw);
//
//   // device lost: recreate the device, then
//   rg_gui_d3d11_reset_device(&renderer, device, context); // and upload the whole atlas
//   rg_gui_d3d11_shutdown(&renderer);
//
// NOTES:
//   - Include <windows.h> and <d3d11.h> first; calls go through lpVtbl, so
//     COBJMACROS is not required.
//   - RgGuiTexture values are handles: rg_gui_d3d11_atlas_texture() for the
//     glyph atlas, rg_gui_d3d11_texture_register() for images. A handle holds a
//     slot and a generation, so a stale handle draws nothing (its batch is
//     counted in stats.skipped_batches). 64-bit targets only.
//   - Image materials (rg_gui_push_image_material) have no D3D11 hook yet: their
//     batches are skipped and counted.
//   - The atlas is RGBA8, or R8 glyph coverage with atlas_coverage (glyphs draw
//     white at the coverage value; images still sample RGBA).
//   - rg_gui_d3d11_draw sets the pipeline state it needs (shaders, buffers,
//     blend, rasterizer, sampler) and does not restore the previous state.
//   - All functions have internal linkage and work in unity builds.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_GUI_D3D11_H
#define RG_GUI_D3D11_H

#include "rg_gui_frame.h"
#include "rg_gui_d3d11_shaders.h"

#include <string.h>

#ifndef RG_GUI_D3D11_MAX_TEXTURES
#define RG_GUI_D3D11_MAX_TEXTURES 64u
#endif

typedef char RgGuiD3d11NeedsWideTextureHandles[(sizeof(RgGuiTexture) >= 8u) ? 1 : -1];

typedef struct RgGuiD3d11Desc
{
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	/* CPU storage for the frame builder, batches, cache ranges, and the packed
	 * frame; taken once by rg_gui_d3d11_init. */
	RgArena* arena;
	u32 atlas_width;
	u32 atlas_height;
	/* Nonzero: an R8 glyph-coverage atlas; zero: RGBA8 texels. */
	u32 atlas_coverage;
	/* Nonzero rounds text run origins to whole pixels (RgGuiFrameBuilder). */
	u32 snap_text_origins;
	/* Nonzero samples textures with nearest filtering; zero, bilinear. */
	u32 nearest_filter;
	u32 max_cached_quads;
	u32 max_runs;
	u32 max_text_instances;
	u32 max_geometry_vertices;
	u32 max_items;
} RgGuiD3d11Desc;

typedef struct RgGuiD3d11DrawDesc
{
	u32 output_width;
	u32 output_height;
	/* Added to every position; whole pixels with snap_text_origins. */
	f32 offset_x;
	f32 offset_y;
} RgGuiD3d11DrawDesc;

/* The last frame's work. */
typedef struct RgGuiD3d11Stats
{
	u32 draw_calls;
	u32 indexed_refs;
	u32 text_instances;
	u32 geometry_vertices;
	u32 items;
	/* Batches not drawn: an image material, or a stale or unknown texture. */
	u32 skipped_batches;
	u32 cache_upload_ranges;
	u32 cache_upload_bytes;
	u32 full_cache_upload;
	u32 frame_upload_bytes;
} RgGuiD3d11Stats;

typedef struct RgGuiD3d11
{
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	ID3D11VertexShader* vertex_shader;
	ID3D11PixelShader* pixel_shader;
	ID3D11BlendState* blend;
	ID3D11RasterizerState* raster;
	ID3D11SamplerState* sampler;
	ID3D11Buffer* constants;
	ID3D11Buffer* cached_quads;
	ID3D11ShaderResourceView* cached_quads_view;
	ID3D11Buffer* frame_buffer;
	ID3D11ShaderResourceView* frame_view;
	ID3D11Buffer* index_buffer;
	ID3D11Texture2D* atlas;
	/* Slot 0 is the atlas view (owned through `atlas`); the others are
	 * registered views, each holding one reference. */
	ID3D11ShaderResourceView* textures[RG_GUI_D3D11_MAX_TEXTURES];
	u32 texture_generations[RG_GUI_D3D11_MAX_TEXTURES];

	/* CPU side. */
	RgGuiFrameBuilder frame;
	RgGuiGpuIndexedBatch* batches;
	u32 batch_capacity;
	RgGuiRendererRange* ranges;
	u32 range_capacity;
	u8* frame_bytes;
	u32 atlas_width;
	u32 atlas_height;
	u32 atlas_coverage;
	u32 nearest_filter;
	u32 prepared;
	/* The glyph-cache contents the GPU holds: which renderer, which revision. */
	const RgGuiRenderer* cache_context;
	u64 cache_revision;
	u32 cache_initialized;
	/* Atlas bytes uploaded over the renderer's lifetime. */
	u64 atlas_upload_bytes;
	RgGuiD3d11Stats stats;
} RgGuiD3d11;

#define RG_GUI_D3D11_RELEASE(object)                   \
	do                                                 \
	{                                                  \
		if (object)                                    \
		{                                              \
			(object)->lpVtbl->Release(object);         \
			(object) = NULL;                           \
		}                                              \
	} while (0)

RGINLINE RgGuiTexture rg_gui_d3d11_texture_handle(u32 slot, u32 generation)
{
	return ((RgGuiTexture)generation << 32u) | (RgGuiTexture)slot;
}

/** The handle text batches draw with; stable across rg_gui_d3d11_reset_device. */
RGINLINE RgGuiTexture rg_gui_d3d11_atlas_texture(const RgGuiD3d11* r)
{
	return rg_gui_d3d11_texture_handle(0u, r->texture_generations[0]);
}

/* A raw (ByteAddressBuffer) buffer the vertex shader reads, updated with
 * UpdateSubresource. */
RGINLINE int rg_gui_d3d11_raw_buffer(ID3D11Device* device, u32 bytes, ID3D11Buffer** buffer,
                                     ID3D11ShaderResourceView** view)
{
	D3D11_BUFFER_DESC bd;
	D3D11_SHADER_RESOURCE_VIEW_DESC vd;
	bytes = (bytes + 15u) & ~15u;
	memset(&bd, 0, sizeof(bd));
	bd.ByteWidth = bytes;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	bd.MiscFlags = D3D11_RESOURCE_MISC_BUFFER_ALLOW_RAW_VIEWS;
	if (FAILED(device->lpVtbl->CreateBuffer(device, &bd, NULL, buffer))) return 0;
	memset(&vd, 0, sizeof(vd));
	vd.Format = DXGI_FORMAT_R32_TYPELESS;
	vd.ViewDimension = D3D11_SRV_DIMENSION_BUFFEREX;
	vd.BufferEx.NumElements = bytes / 4u;
	vd.BufferEx.Flags = D3D11_BUFFEREX_SRV_FLAG_RAW;
	return SUCCEEDED(device->lpVtbl->CreateShaderResourceView(device, (ID3D11Resource*)*buffer, &vd, view));
}

RGINLINE void rg_gui_d3d11_update_buffer(ID3D11DeviceContext* context, ID3D11Buffer* buffer, u32 offset,
                                         const void* data, u32 bytes)
{
	D3D11_BOX box;
	if (!bytes) return;
	box.left = offset;
	box.right = offset + bytes;
	box.top = 0u;
	box.bottom = 1u;
	box.front = 0u;
	box.back = 1u;
	context->lpVtbl->UpdateSubresource(context, (ID3D11Resource*)buffer, 0u, &box, data, 0u, 0u);
}

/* Release every device object; registered textures go stale (their slots'
 * generations advance). CPU storage is kept. */
RGINLINE void rg_gui_d3d11_release_device_objects(RgGuiD3d11* r)
{
	for (u32 i = 1u; i < RG_GUI_D3D11_MAX_TEXTURES; i++)
	{
		if (r->textures[i]) r->texture_generations[i]++;
		RG_GUI_D3D11_RELEASE(r->textures[i]);
	}
	RG_GUI_D3D11_RELEASE(r->textures[0]);
	RG_GUI_D3D11_RELEASE(r->atlas);
	RG_GUI_D3D11_RELEASE(r->index_buffer);
	RG_GUI_D3D11_RELEASE(r->frame_view);
	RG_GUI_D3D11_RELEASE(r->frame_buffer);
	RG_GUI_D3D11_RELEASE(r->cached_quads_view);
	RG_GUI_D3D11_RELEASE(r->cached_quads);
	RG_GUI_D3D11_RELEASE(r->constants);
	RG_GUI_D3D11_RELEASE(r->sampler);
	RG_GUI_D3D11_RELEASE(r->raster);
	RG_GUI_D3D11_RELEASE(r->blend);
	RG_GUI_D3D11_RELEASE(r->pixel_shader);
	RG_GUI_D3D11_RELEASE(r->vertex_shader);
	r->device = NULL;
	r->context = NULL;
	r->cache_initialized = 0u;
	r->cache_context = NULL;
	r->prepared = 0u;
}

/* Fill the index buffer with 0,1,2,0,2,3 per reference, a chunk at a time. */
RGINLINE int rg_gui_d3d11_create_indices(RgGuiD3d11* r)
{
	const u32 refs = r->frame.ref_capacity;
	const u32 wide = refs > 16384u; /* 16-bit indices address 65536 vertices */
	const u32 element = wide ? 4u : 2u;
	D3D11_BUFFER_DESC bd;
	memset(&bd, 0, sizeof(bd));
	bd.ByteWidth = refs * 6u * element;
	bd.Usage = D3D11_USAGE_DEFAULT;
	bd.BindFlags = D3D11_BIND_INDEX_BUFFER;
	if (FAILED(r->device->lpVtbl->CreateBuffer(r->device, &bd, NULL, &r->index_buffer))) return 0;
	{
		union
		{
			u32 wide[6u * 512u];
			u16 narrow[6u * 512u];
		} chunk;
		for (u32 first = 0u; first < refs; first += 512u)
		{
			const u32 count = refs - first < 512u ? refs - first : 512u;
			for (u32 i = 0u; i < count; i++)
			{
				const u32 base = (first + i) * 4u;
				const u32 q[6] = {base, base + 1u, base + 2u, base, base + 2u, base + 3u};
				for (u32 k = 0u; k < 6u; k++)
				{
					if (wide) chunk.wide[i * 6u + k] = q[k];
					else chunk.narrow[i * 6u + k] = (u16)q[k];
				}
			}
			rg_gui_d3d11_update_buffer(r->context, r->index_buffer, first * 6u * element, &chunk, count * 6u * element);
		}
	}
	return 1;
}

RGINLINE int rg_gui_d3d11_create_device_objects(RgGuiD3d11* r)
{
	ID3D11Device* device = r->device;
	const void* ps = r->atlas_coverage ? rg_gui_d3d11_indexed_ps_coverage : rg_gui_d3d11_indexed_ps;
	const SIZE_T ps_size = r->atlas_coverage ? sizeof(rg_gui_d3d11_indexed_ps_coverage) : sizeof(rg_gui_d3d11_indexed_ps);
	if (FAILED(device->lpVtbl->CreateVertexShader(device, rg_gui_d3d11_indexed_vs, sizeof(rg_gui_d3d11_indexed_vs),
	                                              NULL, &r->vertex_shader)) ||
	    FAILED(device->lpVtbl->CreatePixelShader(device, ps, ps_size, NULL, &r->pixel_shader)))
		return 0;
	{
		/* Straight alpha, as rg_gui_gpu.h blends. */
		D3D11_BLEND_DESC bd;
		memset(&bd, 0, sizeof(bd));
		bd.RenderTarget[0].BlendEnable = TRUE;
		bd.RenderTarget[0].SrcBlend = D3D11_BLEND_SRC_ALPHA;
		bd.RenderTarget[0].DestBlend = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOp = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].SrcBlendAlpha = D3D11_BLEND_ONE;
		bd.RenderTarget[0].DestBlendAlpha = D3D11_BLEND_INV_SRC_ALPHA;
		bd.RenderTarget[0].BlendOpAlpha = D3D11_BLEND_OP_ADD;
		bd.RenderTarget[0].RenderTargetWriteMask = D3D11_COLOR_WRITE_ENABLE_ALL;
		if (FAILED(device->lpVtbl->CreateBlendState(device, &bd, &r->blend))) return 0;
	}
	{
		D3D11_RASTERIZER_DESC rd;
		memset(&rd, 0, sizeof(rd));
		rd.FillMode = D3D11_FILL_SOLID;
		rd.CullMode = D3D11_CULL_NONE;
		rd.DepthClipEnable = TRUE;
		if (FAILED(device->lpVtbl->CreateRasterizerState(device, &rd, &r->raster))) return 0;
	}
	{
		D3D11_SAMPLER_DESC sd;
		memset(&sd, 0, sizeof(sd));
		sd.Filter = r->nearest_filter ? D3D11_FILTER_MIN_MAG_MIP_POINT : D3D11_FILTER_MIN_MAG_MIP_LINEAR;
		sd.AddressU = sd.AddressV = sd.AddressW = D3D11_TEXTURE_ADDRESS_CLAMP;
		sd.MaxLOD = D3D11_FLOAT32_MAX;
		if (FAILED(device->lpVtbl->CreateSamplerState(device, &sd, &r->sampler))) return 0;
	}
	{
		D3D11_BUFFER_DESC bd;
		memset(&bd, 0, sizeof(bd));
		bd.ByteWidth = 48u;
		bd.Usage = D3D11_USAGE_DYNAMIC;
		bd.BindFlags = D3D11_BIND_CONSTANT_BUFFER;
		bd.CPUAccessFlags = D3D11_CPU_ACCESS_WRITE;
		if (FAILED(device->lpVtbl->CreateBuffer(device, &bd, NULL, &r->constants))) return 0;
	}
	if (!rg_gui_d3d11_raw_buffer(device, r->frame.cached_quad_capacity * (u32)sizeof(RgGuiRendererCachedQuad),
	                             &r->cached_quads, &r->cached_quads_view) ||
	    !rg_gui_d3d11_raw_buffer(device, r->frame.frame_capacity, &r->frame_buffer, &r->frame_view) ||
	    !rg_gui_d3d11_create_indices(r))
		return 0;
	{
		D3D11_TEXTURE2D_DESC td;
		memset(&td, 0, sizeof(td));
		td.Width = r->atlas_width;
		td.Height = r->atlas_height;
		td.MipLevels = 1u;
		td.ArraySize = 1u;
		td.Format = r->atlas_coverage ? DXGI_FORMAT_R8_UNORM : DXGI_FORMAT_R8G8B8A8_UNORM;
		td.SampleDesc.Count = 1u;
		td.Usage = D3D11_USAGE_DEFAULT;
		td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
		if (FAILED(device->lpVtbl->CreateTexture2D(device, &td, NULL, &r->atlas)) ||
		    FAILED(device->lpVtbl->CreateShaderResourceView(device, (ID3D11Resource*)r->atlas, NULL, &r->textures[0])))
			return 0;
	}
	return 1;
}

RGINLINE void rg_gui_d3d11_shutdown(RgGuiD3d11* r);
RGINLINE int rg_gui_d3d11_reset_device(RgGuiD3d11* r, ID3D11Device* device, ID3D11DeviceContext* context);

/** Take CPU storage from desc->arena and create the device objects. Returns 0
 * on failure (the renderer is then empty). */
RGINLINE int rg_gui_d3d11_init(RgGuiD3d11* r, const RgGuiD3d11Desc* desc)
{
	u64 refs, frame_bytes;
	if (!r) return 0;
	memset(r, 0, sizeof(*r));
	if (!desc || !desc->device || !desc->context || !desc->arena || !desc->atlas_width || !desc->atlas_height ||
	    !desc->max_cached_quads || !desc->max_runs || !desc->max_text_instances || !desc->max_items ||
	    desc->max_items >= RG_GUI_GPU_FRAME_CLIP_LIMIT ||
	    desc->max_cached_quads > UINT32_MAX / (u32)sizeof(RgGuiRendererCachedQuad))
		return 0;
	r->frame.vertex_capacity = desc->max_geometry_vertices ? desc->max_geometry_vertices : 64u * 1024u;
	refs = (u64)desc->max_text_instances + r->frame.vertex_capacity / 3u;
	frame_bytes = rg_gui_gpu_frame_bytes(desc->max_runs, r->frame.vertex_capacity, refs, (u64)desc->max_items + 1u);
	if (refs > UINT32_MAX / 24u || frame_bytes > UINT32_MAX - 16u) return 0;
	r->frame.item_capacity = desc->max_items;
	r->frame.clip_capacity = desc->max_items + 1u;
	r->frame.run_capacity = desc->max_runs;
	r->frame.cached_quad_capacity = desc->max_cached_quads;
	r->frame.ref_capacity = (u32)refs;
	r->frame.frame_capacity = (u32)frame_bytes;
	r->frame.snap_text_origins = desc->snap_text_origins ? 1u : 0u;
	r->batch_capacity = desc->max_items;
	r->range_capacity = desc->max_cached_quads / 32u + 16u;
	r->frame.vertices = RG_ARENA_PUSH_ARRAY(desc->arena, RgGuiGpuVertex, r->frame.vertex_capacity);
	r->frame.items = RG_ARENA_PUSH_ARRAY(desc->arena, RgGuiGpuItem, r->frame.item_capacity);
	r->frame.clips = RG_ARENA_PUSH_ARRAY(desc->arena, RgGuiGpuFrameClip, r->frame.clip_capacity);
	r->batches = RG_ARENA_PUSH_ARRAY(desc->arena, RgGuiGpuIndexedBatch, r->batch_capacity);
	r->ranges = RG_ARENA_PUSH_ARRAY(desc->arena, RgGuiRendererRange, r->range_capacity);
	r->frame_bytes = RG_ARENA_PUSH_ARRAY(desc->arena, u8, r->frame.frame_capacity);
	if (!r->frame.vertices || !r->frame.items || !r->frame.clips || !r->batches || !r->ranges || !r->frame_bytes)
	{
		memset(r, 0, sizeof(*r));
		return 0;
	}
	r->atlas_width = desc->atlas_width;
	r->atlas_height = desc->atlas_height;
	r->atlas_coverage = desc->atlas_coverage ? 1u : 0u;
	r->nearest_filter = desc->nearest_filter ? 1u : 0u;
	for (u32 i = 0u; i < RG_GUI_D3D11_MAX_TEXTURES; i++) r->texture_generations[i] = 1u;
	r->frame.atlas_texture = rg_gui_d3d11_atlas_texture(r);
	if (!rg_gui_d3d11_reset_device(r, desc->device, desc->context))
	{
		rg_gui_d3d11_shutdown(r);
		return 0;
	}
	return 1;
}

/** Release the device objects and create them on `device`: after device loss,
 * or to move to another adapter. CPU storage and the atlas handle are kept;
 * the atlas starts empty (upload it all again), the glyph cache is uploaded in
 * full on the next draw, and registered textures must be registered again. */
RGINLINE int rg_gui_d3d11_reset_device(RgGuiD3d11* r, ID3D11Device* device, ID3D11DeviceContext* context)
{
	if (!r || !r->frame_bytes || !device || !context) return 0;
	rg_gui_d3d11_release_device_objects(r);
	r->device = device;
	r->context = context;
	if (!rg_gui_d3d11_create_device_objects(r))
	{
		rg_gui_d3d11_release_device_objects(r);
		return 0;
	}
	return 1;
}

/** Release the device objects. CPU storage belongs to the caller's arena. */
RGINLINE void rg_gui_d3d11_shutdown(RgGuiD3d11* r)
{
	if (!r) return;
	rg_gui_d3d11_release_device_objects(r);
	memset(r, 0, sizeof(*r));
}

/** Register an image's view for RG_GUI_CMD_IMAGE; holds a reference until
 * unregistered. Returns 0 when every slot is taken. */
RGINLINE RgGuiTexture rg_gui_d3d11_texture_register(RgGuiD3d11* r, ID3D11ShaderResourceView* view)
{
	if (!r || !view || !r->device) return 0u;
	for (u32 i = 1u; i < RG_GUI_D3D11_MAX_TEXTURES; i++)
	{
		if (r->textures[i]) continue;
		view->lpVtbl->AddRef(view);
		r->textures[i] = view;
		return rg_gui_d3d11_texture_handle(i, r->texture_generations[i]);
	}
	return 0u;
}

/** Release a registered view; its handle (and any copy of it) goes stale. */
RGINLINE void rg_gui_d3d11_texture_unregister(RgGuiD3d11* r, RgGuiTexture texture)
{
	const u32 slot = (u32)(texture & 0xffffffffu);
	if (!r || slot == 0u || slot >= RG_GUI_D3D11_MAX_TEXTURES || !r->textures[slot] ||
	    r->texture_generations[slot] != (u32)(texture >> 32u))
		return;
	RG_GUI_D3D11_RELEASE(r->textures[slot]);
	r->texture_generations[slot]++;
}

RGINLINE ID3D11ShaderResourceView* rg_gui_d3d11_texture_view(const RgGuiD3d11* r, RgGuiTexture texture)
{
	const u32 slot = (u32)(texture & 0xffffffffu);
	if (slot >= RG_GUI_D3D11_MAX_TEXTURES || r->texture_generations[slot] != (u32)(texture >> 32u)) return NULL;
	return r->textures[slot];
}

/** Copy the rectangle [x0, x1) x [y0, y1) of the CPU atlas to the GPU atlas.
 * `pixels` is the whole atlas (1 byte per texel with atlas_coverage, else 4). */
RGINLINE void rg_gui_d3d11_update_atlas(RgGuiD3d11* r, const void* pixels, u32 pitch,
                                        u32 x0, u32 y0, u32 x1, u32 y1)
{
	D3D11_BOX box;
	const u32 texel = r && r->atlas_coverage ? 1u : 4u;
	if (!r || !r->atlas || !pixels) return;
	if (x1 > r->atlas_width) x1 = r->atlas_width;
	if (y1 > r->atlas_height) y1 = r->atlas_height;
	if (x1 <= x0 || y1 <= y0) return;
	box.left = x0;
	box.right = x1;
	box.top = y0;
	box.bottom = y1;
	box.front = 0u;
	box.back = 1u;
	r->context->lpVtbl->UpdateSubresource(r->context, (ID3D11Resource*)r->atlas, 0u, &box,
	                                      (const u8*)pixels + (size_t)y0 * pitch + (size_t)x0 * texel, pitch, 0u);
	r->atlas_upload_bytes += (u64)(x1 - x0) * (y1 - y0) * texel;
}

/** Upload the whole glyph cache on the next draw (the text renderer's cache was
 * cleared or replaced outside rg_gui_d3d11). */
RGINLINE void rg_gui_d3d11_invalidate_cache(RgGuiD3d11* r)
{
	if (r) r->cache_initialized = 0u;
}

/** Build the frame: rg_gui_frame_prepare over the draw list. Call
 * rg_gui_renderer_begin_frame(text) first. Glyphs the text provider rasterizes
 * here reach the GPU through rg_gui_d3d11_update_atlas before the draw. */
RGINLINE int rg_gui_d3d11_prepare(RgGuiD3d11* r, RgGuiRenderer* text, const RgGuiDrawList* list, u32 overlay_start)
{
	if (!r) return 0;
	r->prepared = r->device && rg_gui_frame_prepare(&r->frame, text, list, overlay_start) ? 1u : 0u;
	return (int)r->prepared;
}

/* Bring the GPU glyph cache up to date with `text`: the changed ranges, or all
 * of it the first time, after an invalidation, or when the ranges overflow. */
RGINLINE void rg_gui_d3d11_upload_cache(RgGuiD3d11* r, const RgGuiRenderer* text)
{
	const int full = !r->cache_initialized || r->cache_context != text;
	u32 ranges = 0u, quads = 0u;
	if (!full && r->cache_revision == text->cache_revision) return;
	ranges = rg_gui_renderer_upload_ranges(text, full ? 0u : r->cache_revision, r->ranges, r->range_capacity, &quads);
	if (ranges == UINT32_MAX)
	{
		u32 total = text->total_pages * text->page_quads;
		if (total > r->frame.cached_quad_capacity) total = r->frame.cached_quad_capacity;
		rg_gui_d3d11_update_buffer(r->context, r->cached_quads, 0u, text->core.cache_quads,
		                           total * (u32)sizeof(RgGuiRendererCachedQuad));
		r->stats.cache_upload_ranges = 1u;
		r->stats.cache_upload_bytes = total * (u32)sizeof(RgGuiRendererCachedQuad);
	}
	else
	{
		for (u32 i = 0u; i < ranges; i++)
		{
			const RgGuiRendererRange* span = &r->ranges[i];
			if (span->first_quad >= r->frame.cached_quad_capacity ||
			    span->quad_count > r->frame.cached_quad_capacity - span->first_quad)
				continue;
			rg_gui_d3d11_update_buffer(r->context, r->cached_quads,
			                           span->first_quad * (u32)sizeof(RgGuiRendererCachedQuad),
			                           text->core.cache_quads + span->first_quad,
			                           span->quad_count * (u32)sizeof(RgGuiRendererCachedQuad));
		}
		r->stats.cache_upload_ranges = ranges;
		r->stats.cache_upload_bytes = quads * (u32)sizeof(RgGuiRendererCachedQuad);
	}
	r->stats.full_cache_upload = full ? 1u : 0u;
	r->cache_context = text;
	r->cache_revision = text->cache_revision;
	r->cache_initialized = 1u;
}

/** Upload and draw the prepared frame into the bound render target. Returns 0
 * when nothing was prepared or the frame does not fit. */
RGINLINE int rg_gui_d3d11_draw(RgGuiD3d11* r, const RgGuiRenderer* text, const RgGuiD3d11DrawDesc* draw)
{
	RgGuiFramePacket packet;
	ID3D11DeviceContext* context;
	if (!r || !text || !draw || !draw->output_width || !draw->output_height) return 0;
	memset(&r->stats, 0, sizeof(r->stats));
	if (!r->prepared || !r->device) return 0;
	r->prepared = 0u;
	context = r->context;
	rg_gui_d3d11_upload_cache(r, text);
	memset(&packet, 0, sizeof(packet));
	packet.batches = r->batches;
	packet.batch_capacity = r->batch_capacity;
	if (!rg_gui_frame_pack(&r->frame, r->frame_bytes, r->frame.frame_capacity, &packet)) return 0;
	rg_gui_d3d11_update_buffer(context, r->frame_buffer, 0u, r->frame_bytes, packet.size);
	r->stats.frame_upload_bytes = packet.size;
	{
		struct
		{
			f32 output_width, output_height;
			f32 offset_x, offset_y;
			f32 atlas_inv_width, atlas_inv_height;
			u32 geometry_offset, refs_offset;
			u32 clips_offset, padding[3];
		} constants;
		D3D11_MAPPED_SUBRESOURCE mapped;
		ID3D11ShaderResourceView* views[2];
		const FLOAT blend_factor[4] = {0.0f, 0.0f, 0.0f, 0.0f};
		ID3D11ShaderResourceView* bound = NULL;
		memset(&constants, 0, sizeof(constants));
		constants.output_width = (f32)draw->output_width;
		constants.output_height = (f32)draw->output_height;
		constants.offset_x = draw->offset_x;
		constants.offset_y = draw->offset_y;
		constants.atlas_inv_width = 1.0f / (f32)r->atlas_width;
		constants.atlas_inv_height = 1.0f / (f32)r->atlas_height;
		constants.geometry_offset = packet.geometry_offset;
		constants.refs_offset = packet.refs_offset;
		constants.clips_offset = packet.clips_offset;
		if (FAILED(context->lpVtbl->Map(context, (ID3D11Resource*)r->constants, 0u, D3D11_MAP_WRITE_DISCARD, 0u, &mapped)))
			return 0;
		memcpy(mapped.pData, &constants, sizeof(constants));
		context->lpVtbl->Unmap(context, (ID3D11Resource*)r->constants, 0u);

		context->lpVtbl->RSSetState(context, r->raster);
		context->lpVtbl->OMSetBlendState(context, r->blend, blend_factor, 0xffffffffu);
		context->lpVtbl->IASetInputLayout(context, NULL);
		context->lpVtbl->IASetPrimitiveTopology(context, D3D11_PRIMITIVE_TOPOLOGY_TRIANGLELIST);
		context->lpVtbl->IASetIndexBuffer(context, r->index_buffer,
		                                  r->frame.ref_capacity > 16384u ? DXGI_FORMAT_R32_UINT : DXGI_FORMAT_R16_UINT, 0u);
		context->lpVtbl->VSSetShader(context, r->vertex_shader, NULL, 0u);
		context->lpVtbl->PSSetShader(context, r->pixel_shader, NULL, 0u);
		context->lpVtbl->VSSetConstantBuffers(context, 0u, 1u, &r->constants);
		views[0] = r->cached_quads_view;
		views[1] = r->frame_view;
		context->lpVtbl->VSSetShaderResources(context, 0u, 2u, views);
		context->lpVtbl->PSSetSamplers(context, 0u, 1u, &r->sampler);

		for (u32 i = 0u; i < packet.batch_count; i++)
		{
			const RgGuiGpuIndexedBatch* batch = &packet.batches[i];
			ID3D11ShaderResourceView* view;
			if (!batch->ref_count) continue;
			view = batch->material ? NULL : rg_gui_d3d11_texture_view(r, batch->texture);
			if (!view)
			{
				r->stats.skipped_batches++;
				continue;
			}
			if (view != bound)
			{
				context->lpVtbl->PSSetShaderResources(context, 0u, 1u, &view);
				bound = view;
			}
			context->lpVtbl->DrawIndexed(context, batch->ref_count * 6u, batch->first_ref * 6u, 0);
			r->stats.draw_calls++;
			r->stats.indexed_refs += batch->ref_count;
			r->stats.text_instances += batch->glyph_count;
			r->stats.geometry_vertices += batch->geometry_vertices;
			r->stats.items += batch->source_items;
		}
		/* Unbind the views so the next frame's updates and the caller's use of
		 * registered textures do not meet a bound resource. */
		views[0] = NULL;
		views[1] = NULL;
		context->lpVtbl->VSSetShaderResources(context, 0u, 2u, views);
		context->lpVtbl->PSSetShaderResources(context, 0u, 1u, views);
	}
	return 1;
}

#endif // RG_GUI_D3D11_H
