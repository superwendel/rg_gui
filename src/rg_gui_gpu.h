// rg_gui_gpu - SDL3 GPU renderer for rg_gui draw lists
//
// Part of the Reverse Gravity (rg_) libraries.
// Renders rectangles, triangles, images, clips, and page-cached rg_text glyphs
// while preserving draw-list order. Cached glyph pages are expanded on the GPU.
//
// NOTES:
//   - RgGuiTexture values are SDL_GPUTexture pointers stored as uintptr_t.
//   - Shader binaries are loaded through rg_gpu from the supplied shader root.
//   - All functions have internal linkage and work in unity builds.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_GUI_GPU_H
#define RG_GUI_GPU_H

#include "rg_gui_renderer.h"
#include "rg_gpu.h"

#include <SDL3/SDL.h>
#include <string.h>

#ifndef RG_GUI_GPU_RUN_ASSERT
#include <assert.h>
#define RG_GUI_GPU_RUN_ASSERT(x) assert(x)
#endif

typedef struct RgGuiGpuRunDesc
{
	SDL_GPUDevice* device;
	SDL_GPUTextureFormat target_format;
	const char* shader_root;
	SDL_GPUTexture* atlas_texture;
	u32 atlas_width;
	u32 atlas_height;
	u32 max_cached_quads;
	u32 max_runs;
	u32 max_instances;
	SDL_GPUFilter min_filter;
	SDL_GPUFilter mag_filter;
} RgGuiGpuRunDesc;

typedef struct RgGuiGpuRunDrawDesc
{
	u32 output_width;
	u32 output_height;
	f32 offset_x;
	f32 offset_y;
	SDL_Rect viewport;
} RgGuiGpuRunDrawDesc;

typedef struct RgGuiGpuRunStats
{
	u32 instances;
	u32 batches;
	u32 draw_calls;
	u32 dispatches;
	u32 run_upload_bytes;
	u32 cache_upload_bytes;
	u32 full_cache_upload;
} RgGuiGpuRunStats;

typedef struct RgGuiGpuRunRenderer
{
	SDL_GPUDevice* device;
	SDL_GPUGraphicsPipeline* pipeline;
	SDL_GPUComputePipeline* compute_pipeline;
	SDL_GPUBuffer* cached_quad_buffer;
	SDL_GPUBuffer* run_buffer;
	SDL_GPUBuffer* instance_buffer;
	SDL_GPUSampler* sampler;
	SDL_GPUTexture* atlas_texture;
	u32 atlas_width;
	u32 atlas_height;
	u32 cached_quad_capacity;
	u32 run_capacity;
	u32 instance_capacity;
	u32 cache_initialized;
	u32 owns_compute_pipeline;
} RgGuiGpuRunRenderer;

RGINLINE void rg_gui_gpu_run_destroy(RgGuiGpuRunRenderer* renderer);

RGINLINE int rg_gui_gpu_run_create_graphics_pipeline(
    RgGuiGpuRunRenderer* renderer, const RgGuiGpuRunDesc* desc,
    SDL_GPUShader* vertex_shader, SDL_GPUShader* fragment_shader)
{
	SDL_GPUVertexBufferDescription vertex_buffer;
	memset(&vertex_buffer, 0, sizeof(vertex_buffer));
	vertex_buffer.slot = 0u;
	vertex_buffer.pitch = sizeof(RgGuiRendererBaseInstance);
	vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_INSTANCE;

	SDL_GPUVertexAttribute attributes[4];
	memset(attributes, 0, sizeof(attributes));
	attributes[0].location = 0u;
	attributes[0].buffer_slot = 0u;
	attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	attributes[0].offset = 0u;
	attributes[1].location = 1u;
	attributes[1].buffer_slot = 0u;
	attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	attributes[1].offset = sizeof(f32) * 2u;
	attributes[2].location = 2u;
	attributes[2].buffer_slot = 0u;
	attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_USHORT4;
	attributes[2].offset = sizeof(f32) * 4u;
	attributes[3].location = 3u;
	attributes[3].buffer_slot = 0u;
	attributes[3].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
	attributes[3].offset = sizeof(f32) * 4u + sizeof(u16) * 4u;

	SDL_GPUVertexInputState vertex_input;
	memset(&vertex_input, 0, sizeof(vertex_input));
	vertex_input.num_vertex_buffers = 1u;
	vertex_input.vertex_buffer_descriptions = &vertex_buffer;
	vertex_input.num_vertex_attributes = 4u;
	vertex_input.vertex_attributes = attributes;

	SDL_GPUColorTargetBlendState blend;
	memset(&blend, 0, sizeof(blend));
	blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
	blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
	                         SDL_GPU_COLORCOMPONENT_G |
	                         SDL_GPU_COLORCOMPONENT_B |
	                         SDL_GPU_COLORCOMPONENT_A;
	blend.enable_blend = true;
	blend.enable_color_write_mask = true;

	SDL_GPUColorTargetDescription target;
	memset(&target, 0, sizeof(target));
	target.format = desc->target_format;
	target.blend_state = blend;

	SDL_GPUGraphicsPipelineCreateInfo info;
	memset(&info, 0, sizeof(info));
	info.target_info.num_color_targets = 1u;
	info.target_info.color_target_descriptions = &target;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.vertex_shader = vertex_shader;
	info.fragment_shader = fragment_shader;
	info.vertex_input_state = vertex_input;
	renderer->pipeline = SDL_CreateGPUGraphicsPipeline(renderer->device, &info);
	return renderer->pipeline != NULL;
}

/** Create persistent buffers plus compute and graphics pipelines. */
RGINLINE int rg_gui_gpu_run_create(RgGuiGpuRunRenderer* renderer,
                                   const RgGuiGpuRunDesc* desc)
{
	RG_GUI_GPU_RUN_ASSERT(renderer != NULL);
	RG_GUI_GPU_RUN_ASSERT(desc != NULL);
	if (!renderer || !desc || !desc->device || !desc->atlas_texture || !desc->shader_root ||
	    desc->target_format == SDL_GPU_TEXTUREFORMAT_INVALID ||
	    desc->atlas_width == 0u || desc->atlas_height == 0u ||
	    desc->max_cached_quads == 0u || desc->max_runs == 0u ||
	    desc->max_instances == 0u || sizeof(RgGuiRendererBaseCachedQuad) != 24u ||
	    sizeof(RgGuiRendererBaseRunInstance) != 32u || sizeof(RgGuiRendererBaseInstance) != 32u ||
	    desc->max_cached_quads > UINT32_MAX / (u32)sizeof(RgGuiRendererBaseCachedQuad) ||
	    desc->max_runs > UINT32_MAX / (u32)sizeof(RgGuiRendererBaseRunInstance) ||
	    desc->max_instances > UINT32_MAX / (u32)sizeof(RgGuiRendererBaseInstance))
	{
		return 0;
	}

	memset(renderer, 0, sizeof(*renderer));
	renderer->device = desc->device;
	renderer->atlas_texture = desc->atlas_texture;
	renderer->atlas_width = desc->atlas_width;
	renderer->atlas_height = desc->atlas_height;
	renderer->cached_quad_capacity = desc->max_cached_quads;
	renderer->run_capacity = desc->max_runs;
	renderer->instance_capacity = desc->max_instances;

	RgGpuShaderDesc vertex_desc = {0};
	vertex_desc.name = "rg_gui_text.vert";
	vertex_desc.stage = SDL_GPU_SHADERSTAGE_VERTEX;
	vertex_desc.uniform_buffer_count = 1u;
	SDL_GPUShader* vertex_shader = rg_gpu_shader_load(desc->device, desc->shader_root, &vertex_desc);

	RgGpuShaderDesc fragment_desc = {0};
	fragment_desc.name = "rg_gui_text.frag";
	fragment_desc.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	fragment_desc.sampler_count = 1u;
	SDL_GPUShader* fragment_shader = rg_gpu_shader_load(desc->device, desc->shader_root, &fragment_desc);

	RgGpuComputePipelineDesc compute_desc = {0};
	compute_desc.name = "rg_gui_expand.comp";
	compute_desc.readonly_storage_buffer_count = 2u;
	compute_desc.readwrite_storage_buffer_count = 1u;
	compute_desc.threadcount_x = 64u;
	compute_desc.threadcount_y = 1u;
	compute_desc.threadcount_z = 1u;
	renderer->compute_pipeline = rg_gpu_compute_pipeline_load(desc->device, desc->shader_root, &compute_desc);
	renderer->owns_compute_pipeline = renderer->compute_pipeline != NULL;

	int graphics_ok = vertex_shader && fragment_shader &&
	                  rg_gui_gpu_run_create_graphics_pipeline(renderer, desc,
	                                                          vertex_shader, fragment_shader);
	if (vertex_shader) SDL_ReleaseGPUShader(desc->device, vertex_shader);
	if (fragment_shader) SDL_ReleaseGPUShader(desc->device, fragment_shader);
	if (!graphics_ok || !renderer->compute_pipeline)
	{
		rg_gui_gpu_run_destroy(renderer);
		return 0;
	}

	SDL_GPUBufferCreateInfo buffer_info;
	memset(&buffer_info, 0, sizeof(buffer_info));
	buffer_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_READ;
	buffer_info.size = (u32)(sizeof(RgGuiRendererBaseCachedQuad) * desc->max_cached_quads);
	renderer->cached_quad_buffer = SDL_CreateGPUBuffer(desc->device, &buffer_info);
	buffer_info.size = (u32)(sizeof(RgGuiRendererBaseRunInstance) * desc->max_runs);
	renderer->run_buffer = SDL_CreateGPUBuffer(desc->device, &buffer_info);
	buffer_info.usage = SDL_GPU_BUFFERUSAGE_COMPUTE_STORAGE_WRITE |
	                    SDL_GPU_BUFFERUSAGE_VERTEX;
	buffer_info.size = (u32)(sizeof(RgGuiRendererBaseInstance) * desc->max_instances);
	renderer->instance_buffer = SDL_CreateGPUBuffer(desc->device, &buffer_info);

	SDL_GPUSamplerCreateInfo sampler_info;
	memset(&sampler_info, 0, sizeof(sampler_info));
	sampler_info.min_filter = desc->min_filter;
	sampler_info.mag_filter = desc->mag_filter;
	sampler_info.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sampler_info.address_mode_u = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampler_info.address_mode_v = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	sampler_info.address_mode_w = SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	renderer->sampler = SDL_CreateGPUSampler(desc->device, &sampler_info);
	if (!renderer->cached_quad_buffer || !renderer->run_buffer ||
	    !renderer->instance_buffer || !renderer->sampler)
	{
		rg_gui_gpu_run_destroy(renderer);
		return 0;
	}
	return 1;
}

RGINLINE void rg_gui_gpu_run_destroy(RgGuiGpuRunRenderer* renderer)
{
	if (!renderer) return;
	if (renderer->sampler) SDL_ReleaseGPUSampler(renderer->device, renderer->sampler);
	if (renderer->instance_buffer)
		SDL_ReleaseGPUBuffer(renderer->device, renderer->instance_buffer);
	if (renderer->run_buffer) SDL_ReleaseGPUBuffer(renderer->device, renderer->run_buffer);
	if (renderer->cached_quad_buffer)
		SDL_ReleaseGPUBuffer(renderer->device, renderer->cached_quad_buffer);
	if (renderer->pipeline)
		SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->pipeline);
	if (renderer->compute_pipeline && renderer->owns_compute_pipeline)
		SDL_ReleaseGPUComputePipeline(renderer->device, renderer->compute_pipeline);
	memset(renderer, 0, sizeof(*renderer));
}

/** Expand every run into its assigned output range in one compute dispatch. */
RGINLINE int rg_gui_gpu_run_dispatch(const RgGuiGpuRunRenderer* renderer,
                                     SDL_GPUCommandBuffer* command_buffer,
                                     const RgGuiRendererBaseRunPrepared* prepared)
{
	if (!renderer || !command_buffer || !prepared || !prepared->run_count ||
	    !prepared->glyph_count || prepared->run_count > renderer->run_capacity ||
	    prepared->glyph_count > renderer->instance_capacity)
	{
		return 0;
	}
	SDL_GPUStorageBufferReadWriteBinding output;
	memset(&output, 0, sizeof(output));
	output.buffer = renderer->instance_buffer;
	// The compute pass rewrites every text instance each frame. Immediate
	// presentation can leave earlier frames in flight, so cycle the backing
	// buffer rather than overwriting data still referenced by their draws.
	output.cycle = true;
	SDL_GPUComputePass* pass = SDL_BeginGPUComputePass(command_buffer, NULL, 0u,
	                                                   &output, 1u);
	if (!pass) return 0;
	SDL_BindGPUComputePipeline(pass, renderer->compute_pipeline);
	SDL_GPUBuffer* inputs[2] = {renderer->cached_quad_buffer, renderer->run_buffer};
	SDL_BindGPUComputeStorageBuffers(pass, 0u, inputs, 2u);
	SDL_DispatchGPUCompute(pass, prepared->run_count, 1u, 1u);
	SDL_EndGPUComputePass(pass);
	return 1;
}

RGINLINE SDL_Rect rg_gui_gpu_run_batch_scissor(const RgGuiRendererBaseBatch* batch,
                                               const RgGuiGpuRunDrawDesc* desc)
{
	SDL_Rect result = desc->viewport;
	if (!batch->clip_enabled) return result;
	int x0 = (int)SDL_floorf(batch->clip.x + desc->offset_x);
	int y0 = (int)SDL_floorf(batch->clip.y + desc->offset_y);
	int x1 = (int)SDL_ceilf(batch->clip.x + batch->clip.w + desc->offset_x);
	int y1 = (int)SDL_ceilf(batch->clip.y + batch->clip.h + desc->offset_y);
	int vx1 = result.x + result.w;
	int vy1 = result.y + result.h;
	if (x0 < result.x) x0 = result.x;
	if (y0 < result.y) y0 = result.y;
	if (x1 > vx1) x1 = vx1;
	if (y1 > vy1) y1 = vy1;
	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;
	result.x = x0;
	result.y = y0;
	result.w = x1 - x0;
	result.h = y1 - y0;
	return result;
}

/** Draw compute-expanded glyphs using the cached run stream's clip batches. */
RGINLINE RgGuiGpuRunStats rg_gui_gpu_run_draw(
    const RgGuiGpuRunRenderer* renderer, SDL_GPUCommandBuffer* command_buffer,
    SDL_GPURenderPass* pass, const RgGuiRendererBaseRunPrepared* prepared,
    const RgGuiGpuRunDrawDesc* desc)
{
	RgGuiGpuRunStats stats;
	memset(&stats, 0, sizeof(stats));
	if (!renderer || !command_buffer || !pass || !prepared || !desc ||
	    !prepared->runs || !prepared->batches || !prepared->run_count ||
	    !prepared->glyph_count || desc->output_width == 0u ||
	    desc->output_height == 0u)
	{
		return stats;
	}

	struct RgGuiGpuRunUniforms
	{
		f32 output_width;
		f32 output_height;
		f32 offset_x;
		f32 offset_y;
		f32 atlas_inv_width;
		f32 atlas_inv_height;
		f32 padding0;
		f32 padding1;
	} uniforms;
	uniforms.output_width = (f32)desc->output_width;
	uniforms.output_height = (f32)desc->output_height;
	uniforms.offset_x = desc->offset_x;
	uniforms.offset_y = desc->offset_y;
	uniforms.atlas_inv_width = 1.0f / (f32)renderer->atlas_width;
	uniforms.atlas_inv_height = 1.0f / (f32)renderer->atlas_height;
	uniforms.padding0 = 0.0f;
	uniforms.padding1 = 0.0f;

	SDL_BindGPUGraphicsPipeline(pass, renderer->pipeline);
	SDL_PushGPUVertexUniformData(command_buffer, 0u, &uniforms, sizeof(uniforms));
	SDL_GPUTextureSamplerBinding atlas = {renderer->atlas_texture, renderer->sampler};
	SDL_BindGPUFragmentSamplers(pass, 0u, &atlas, 1u);

	for (u32 i = 0u; i < prepared->batch_count; i++)
	{
		const RgGuiRendererBaseBatch* batch = &prepared->batches[i];
		if (!batch->instance_count || batch->first_instance >= prepared->run_count ||
		    batch->instance_count > prepared->run_count - batch->first_instance)
		{
			continue;
		}
		const RgGuiRendererBaseRunInstance* first = &prepared->runs[batch->first_instance];
		const RgGuiRendererBaseRunInstance* last =
		    &prepared->runs[batch->first_instance + batch->instance_count - 1u];
		u32 first_glyph = first->first_output_instance;
		u32 one_past_last = last->first_output_instance + last->quad_count;
		if (one_past_last <= first_glyph || one_past_last > prepared->glyph_count) continue;

		SDL_Rect scissor = rg_gui_gpu_run_batch_scissor(batch, desc);
		if (scissor.w <= 0 || scissor.h <= 0) continue;
		SDL_SetGPUScissor(pass, &scissor);
		SDL_GPUBufferBinding binding = {
		    renderer->instance_buffer,
		    first_glyph * (u32)sizeof(RgGuiRendererBaseInstance)};
		SDL_BindGPUVertexBuffers(pass, 0u, &binding, 1u);
		u32 glyph_count = one_past_last - first_glyph;
		SDL_DrawGPUPrimitives(pass, 6u, glyph_count, 0u, 0u);
		stats.instances += glyph_count;
		stats.batches++;
		stats.draw_calls++;
	}
	stats.dispatches = 1u;
	return stats;
}

// =============================================================================
// STABLE PAGE UPLOADS
// =============================================================================

typedef struct RgGuiGpuTextUpload
{
	RgGpuUploadSlice runs;
	RgGpuUploadSlice cached_quads;
	u32 run_count;
	u32 cache_range_count;
	u32 full_cache_upload;
} RgGuiGpuTextUpload;

/** Create the cached-text renderer using the stable compute and graphics ABI. */
RGINLINE int rg_gui_gpu_text_create(RgGuiGpuRunRenderer* renderer,
                                    const RgGuiGpuRunDesc* desc)
{
	return rg_gui_gpu_run_create(renderer, desc);
}

/** Release cached-text GPU resources. */
RGINLINE void rg_gui_gpu_text_destroy(RgGuiGpuRunRenderer* renderer)
{
	rg_gui_gpu_run_destroy(renderer);
}

/** Stage descriptors plus only initialized quads from new stable pages. */
RGINLINE int rg_gui_gpu_text_stage_upload(const RgGuiGpuRunRenderer* renderer,
                                          const RgGuiRenderer* context,
                                          const RgGuiRendererPrepared* prepared,
                                          RgGpuUploadRing* ring,
                                          RgGuiGpuTextUpload* upload)
{
	if (!upload) return 0;
	memset(upload, 0, sizeof(*upload));
	if (!renderer || !context || !prepared || !ring || !prepared->run_count ||
	    !prepared->glyph_count || prepared->run_count > renderer->run_capacity ||
	    prepared->glyph_count > renderer->instance_capacity ||
	    context->total_pages * context->page_quads > renderer->cached_quad_capacity)
	{
		return 0;
	}
	u32 run_bytes = prepared->run_count * (u32)sizeof(RgGuiRendererRun);
	if (!rg_gpu_upload_ring_alloc(ring, run_bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN,
	                              &upload->runs)) return 0;
	memcpy(rg_gpu_upload_ring_ptr(ring, &upload->runs), prepared->runs, run_bytes);
	upload->run_count = prepared->run_count;

	int full = !renderer->cache_initialized;
	u32 range_count = full ? context->allocated_page_count : context->dirty_page_count;
	u32 quad_count = full ? context->live_quad_count : 0u;
	if (!full)
	{
		for (u32 i = 0u; i < range_count; i++)
			quad_count += context->dirty_pages[i].quad_count;
	}
	if (quad_count)
	{
		u32 cache_bytes = quad_count * (u32)sizeof(RgGuiRendererBaseCachedQuad);
		if (!rg_gpu_upload_ring_alloc(ring, cache_bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN,
		                              &upload->cached_quads)) return 0;
		u8* destination = (u8*)rg_gpu_upload_ring_ptr(
		    ring, &upload->cached_quads);
		if (full)
		{
			for (u32 r = 0u; r < context->core.run_count; r++)
			{
				u32 page = context->run_first_pages[r];
				u32 remaining = context->core.runs[r].quad_count;
				while (remaining)
				{
					u32 count = remaining < context->page_quads ? remaining : context->page_quads;
					size_t size = (size_t)count * sizeof(RgGuiRendererBaseCachedQuad);
					memcpy(destination,
					       context->core.cache_quads + page * context->page_quads,
					       size);
					destination += size;
					remaining -= count;
					page = context->page_next[page];
				}
			}
		}
		else
		{
			for (u32 i = 0u; i < range_count; i++)
			{
				const RgGuiRendererRange* range = &context->dirty_pages[i];
				size_t size = (size_t)range->quad_count * sizeof(RgGuiRendererBaseCachedQuad);
				memcpy(destination, context->core.cache_quads + range->first_quad,
				       size);
				destination += size;
			}
		}
	}
	upload->cache_range_count = range_count;
	upload->full_cache_upload = full ? 1u : 0u;
	return 1;
}

/** Encode page-targeted uploads and mark the persistent mirror initialized. */
RGINLINE void rg_gui_gpu_text_encode_upload(RgGuiGpuRunRenderer* renderer,
                                            const RgGuiRenderer* context,
                                            SDL_GPUCopyPass* copy,
                                            const RgGpuUploadRing* ring,
                                            const RgGuiGpuTextUpload* upload)
{
	if (!renderer || !context || !copy || !ring || !upload || !upload->runs.size)
		return;
	SDL_GPUTransferBufferLocation source = {ring->buffer, upload->runs.offset};
	SDL_GPUBufferRegion destination = {renderer->run_buffer, 0u, upload->runs.size};
	SDL_UploadToGPUBuffer(copy, &source, &destination, true);

	u32 source_offset = upload->cached_quads.offset;
	if (upload->full_cache_upload)
	{
		for (u32 r = 0u; r < context->core.run_count; r++)
		{
			u32 page = context->run_first_pages[r];
			u32 remaining = context->core.runs[r].quad_count;
			while (remaining)
			{
				u32 count = remaining < context->page_quads ? remaining : context->page_quads;
				u32 size = count * (u32)sizeof(RgGuiRendererBaseCachedQuad);
				source.offset = source_offset;
				destination.buffer = renderer->cached_quad_buffer;
				destination.offset = page * context->page_quads *
				                     (u32)sizeof(RgGuiRendererBaseCachedQuad);
				destination.size = size;
				SDL_UploadToGPUBuffer(copy, &source, &destination, false);
				source_offset += size;
				remaining -= count;
				page = context->page_next[page];
			}
		}
	}
	else
	{
		for (u32 i = 0u; i < context->dirty_page_count; i++)
		{
			const RgGuiRendererRange* range = &context->dirty_pages[i];
			u32 size = range->quad_count * (u32)sizeof(RgGuiRendererBaseCachedQuad);
			source.offset = source_offset;
			destination.buffer = renderer->cached_quad_buffer;
			destination.offset = range->first_quad *
			                     (u32)sizeof(RgGuiRendererBaseCachedQuad);
			destination.size = size;
			SDL_UploadToGPUBuffer(copy, &source, &destination, false);
			source_offset += size;
		}
	}
	renderer->cache_initialized = 1u;
}

/** Expand all page segments with the production compute shader. */
RGINLINE int rg_gui_gpu_text_dispatch(const RgGuiGpuRunRenderer* renderer,
                                      SDL_GPUCommandBuffer* command_buffer,
                                      const RgGuiRendererPrepared* prepared)
{
	return rg_gui_gpu_run_dispatch(renderer, command_buffer, prepared);
}

/** Draw expanded instances using batches expressed in page descriptors. */
RGINLINE RgGuiGpuRunStats rg_gui_gpu_text_draw(
    const RgGuiGpuRunRenderer* renderer, SDL_GPUCommandBuffer* command_buffer,
    SDL_GPURenderPass* pass, const RgGuiRendererPrepared* prepared,
    const RgGuiGpuRunDrawDesc* desc, const RgGuiGpuTextUpload* upload)
{
	RgGuiGpuRunStats stats =
	    rg_gui_gpu_run_draw(renderer, command_buffer, pass, prepared, desc);
	stats.run_upload_bytes = upload ? upload->runs.size : 0u;
	stats.cache_upload_bytes = upload ? upload->cached_quads.size : 0u;
	stats.full_cache_upload = upload ? upload->full_cache_upload : 0u;
	return stats;
}

// =============================================================================
// ORDERED DRAW-LIST RENDERER
// =============================================================================

#ifndef RG_GUI_GPU_DEFAULT_MAX_GEOMETRY_VERTICES
#define RG_GUI_GPU_DEFAULT_MAX_GEOMETRY_VERTICES (64u * 1024u)
#endif

#ifndef RG_GUI_GPU_DEFAULT_MAX_ITEMS
#define RG_GUI_GPU_DEFAULT_MAX_ITEMS 4096u
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

typedef enum RgGuiGpuItemType
{
	RG_GUI_GPU_ITEM_SOLID = 0,
	RG_GUI_GPU_ITEM_IMAGE = 1,
	RG_GUI_GPU_ITEM_TEXT = 2
} RgGuiGpuItemType;

typedef struct RgGuiGpuItem
{
	RgGuiRect clip;
	RgGuiTexture texture;
	RgGuiImageMaterial material;
	u32 first;
	u32 count;
	u32 type;
	u32 clip_enabled;
} RgGuiGpuItem;

/** Result from an application image-material binding callback. */
typedef enum RgGuiGpuImageBindResult
{
	/** Draw with rg_gui's stock image pipeline and the texture in the item. */
	RG_GUI_GPU_IMAGE_BIND_DEFAULT = 0,
	/** The callback bound a compatible pipeline and all fragment resources. */
	RG_GUI_GPU_IMAGE_BIND_CUSTOM = 1,
	/** Skip this image item and report an image binding failure. */
	RG_GUI_GPU_IMAGE_BIND_FAILED = 2
} RgGuiGpuImageBindResult;

/** Resources supplied to an application image-material binding callback. */
typedef struct RgGuiGpuImageBindInfo
{
	SDL_GPUCommandBuffer* command_buffer;
	SDL_GPURenderPass* pass;
	SDL_GPUSampler* sampler;
	RgGuiTexture texture;
	RgGuiImageMaterial material;
} RgGuiGpuImageBindInfo;

/**
 * Bind an application-defined image material.
 *
 * RG_GUI_GPU_IMAGE_BIND_DEFAULT must not change GPU state. CUSTOM must bind a
 * graphics pipeline compatible with RgGuiGpuVertex and all fragment resources;
 * rg_gui binds the vertex uniforms and vertex buffer. FAILED may be returned
 * after a partial bind because rg_gui invalidates its cached state afterward.
 */
typedef RgGuiGpuImageBindResult (*RgGuiGpuImageBindFn)(
	void* user, const RgGuiGpuImageBindInfo* info);

typedef struct RgGuiGpuDesc
{
	SDL_GPUDevice* device;
	SDL_GPUTextureFormat target_format;
	const char* shader_root;
	SDL_GPUTexture* atlas_texture;
	u32 atlas_width;
	u32 atlas_height;
	u32 max_cached_quads;
	u32 max_runs;
	u32 max_text_instances;
	u32 max_geometry_vertices;
	u32 max_items;
	SDL_GPUFilter min_filter;
	SDL_GPUFilter mag_filter;
	RgGuiGpuImageBindFn image_bind;
	void* image_bind_user;
} RgGuiGpuDesc;

typedef RgGuiGpuRunDrawDesc RgGuiGpuDrawDesc;

typedef struct RgGuiGpuUpload
{
	RgGuiGpuTextUpload text;
	RgGpuUploadSlice geometry;
	u32 has_text;
	u32 has_geometry;
} RgGuiGpuUpload;

typedef struct RgGuiGpuStats
{
	u32 geometry_vertices;
	u32 text_instances;
	u32 items;
	u32 draw_calls;
	u32 dispatches;
	u32 run_upload_bytes;
	u32 cache_upload_bytes;
	u32 geometry_upload_bytes;
	u32 full_cache_upload;
	u32 image_bind_calls;
	u32 custom_image_draw_calls;
	u32 image_bind_failures;
} RgGuiGpuStats;

typedef struct RgGuiGpuRenderer
{
	SDL_GPUDevice* device;
	RgGuiGpuRunRenderer text;
	SDL_GPUGraphicsPipeline* solid_pipeline;
	SDL_GPUGraphicsPipeline* image_pipeline;
	SDL_GPUBuffer* geometry_buffer;
	RgGuiGpuVertex* vertices;
	RgGuiGpuItem* items;
	u32 vertex_count;
	u32 vertex_capacity;
	u32 item_count;
	u32 item_capacity;
	const RgGuiRendererPrepared* text_prepared;
	RgGuiGpuImageBindFn image_bind;
	void* image_bind_user;
} RgGuiGpuRenderer;

RGINLINE void rg_gui_gpu_destroy(RgGuiGpuRenderer* renderer);

RGINLINE int rg_gui_gpu_create_geometry_pipeline(RgGuiGpuRenderer* renderer,
                                                 SDL_GPUTextureFormat target_format,
                                                 SDL_GPUShader* vertex_shader,
                                                 SDL_GPUShader* fragment_shader,
                                                 SDL_GPUGraphicsPipeline** out_pipeline)
{
	SDL_GPUVertexBufferDescription vertex_buffer = {0};
	vertex_buffer.slot = 0u;
	vertex_buffer.pitch = sizeof(RgGuiGpuVertex);
	vertex_buffer.input_rate = SDL_GPU_VERTEXINPUTRATE_VERTEX;

	SDL_GPUVertexAttribute attributes[3] = {0};
	attributes[0].location = 0u;
	attributes[0].buffer_slot = 0u;
	attributes[0].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	attributes[0].offset = 0u;
	attributes[1].location = 1u;
	attributes[1].buffer_slot = 0u;
	attributes[1].format = SDL_GPU_VERTEXELEMENTFORMAT_UBYTE4_NORM;
	attributes[1].offset = sizeof(f32) * 2u;
	attributes[2].location = 2u;
	attributes[2].buffer_slot = 0u;
	attributes[2].format = SDL_GPU_VERTEXELEMENTFORMAT_FLOAT2;
	attributes[2].offset = sizeof(f32) * 2u + sizeof(u32);

	SDL_GPUVertexInputState vertex_input = {0};
	vertex_input.num_vertex_buffers = 1u;
	vertex_input.vertex_buffer_descriptions = &vertex_buffer;
	vertex_input.num_vertex_attributes = 3u;
	vertex_input.vertex_attributes = attributes;

	SDL_GPUColorTargetBlendState blend = {0};
	blend.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	blend.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blend.color_blend_op = SDL_GPU_BLENDOP_ADD;
	blend.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	blend.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	blend.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	blend.color_write_mask = SDL_GPU_COLORCOMPONENT_R |
	                         SDL_GPU_COLORCOMPONENT_G |
	                         SDL_GPU_COLORCOMPONENT_B |
	                         SDL_GPU_COLORCOMPONENT_A;
	blend.enable_blend = true;
	blend.enable_color_write_mask = true;

	SDL_GPUColorTargetDescription target = {0};
	target.format = target_format;
	target.blend_state = blend;

	SDL_GPUGraphicsPipelineCreateInfo info = {0};
	info.target_info.num_color_targets = 1u;
	info.target_info.color_target_descriptions = &target;
	info.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	info.vertex_shader = vertex_shader;
	info.fragment_shader = fragment_shader;
	info.vertex_input_state = vertex_input;
	*out_pipeline = SDL_CreateGPUGraphicsPipeline(renderer->device, &info);
	return *out_pipeline != NULL;
}

RGINLINE int rg_gui_gpu_create(RgGuiGpuRenderer* renderer, const RgGuiGpuDesc* desc)
{
	if (!renderer || !desc || !desc->device || !desc->shader_root || !desc->atlas_texture ||
	    desc->target_format == SDL_GPU_TEXTUREFORMAT_INVALID ||
	    desc->atlas_width == 0u || desc->atlas_height == 0u ||
	    desc->max_cached_quads == 0u || desc->max_runs == 0u ||
	    desc->max_text_instances == 0u)
	{
		return 0;
	}

	memset(renderer, 0, sizeof(*renderer));
	renderer->device = desc->device;
	renderer->image_bind = desc->image_bind;
	renderer->image_bind_user = desc->image_bind_user;
	renderer->vertex_capacity = desc->max_geometry_vertices ? desc->max_geometry_vertices : RG_GUI_GPU_DEFAULT_MAX_GEOMETRY_VERTICES;
	renderer->item_capacity = desc->max_items ? desc->max_items : RG_GUI_GPU_DEFAULT_MAX_ITEMS;
	if (renderer->vertex_capacity > UINT32_MAX / (u32)sizeof(RgGuiGpuVertex))
	{
		return 0;
	}

	renderer->vertices = (RgGuiGpuVertex*)SDL_malloc(
	    sizeof(RgGuiGpuVertex) * renderer->vertex_capacity);
	renderer->items = (RgGuiGpuItem*)SDL_malloc(
	    sizeof(RgGuiGpuItem) * renderer->item_capacity);
	if (!renderer->vertices || !renderer->items)
	{
		rg_gui_gpu_destroy(renderer);
		return 0;
	}

	RgGuiGpuRunDesc text_desc = {0};
	text_desc.device = desc->device;
	text_desc.target_format = desc->target_format;
	text_desc.shader_root = desc->shader_root;
	text_desc.atlas_texture = desc->atlas_texture;
	text_desc.atlas_width = desc->atlas_width;
	text_desc.atlas_height = desc->atlas_height;
	text_desc.max_cached_quads = desc->max_cached_quads;
	text_desc.max_runs = desc->max_runs;
	text_desc.max_instances = desc->max_text_instances;
	text_desc.min_filter = desc->min_filter;
	text_desc.mag_filter = desc->mag_filter;
	if (!rg_gui_gpu_text_create(&renderer->text, &text_desc))
	{
		rg_gui_gpu_destroy(renderer);
		return 0;
	}

	RgGpuShaderDesc geometry_vertex_desc = {0};
	geometry_vertex_desc.name = "rg_gui_geometry.vert";
	geometry_vertex_desc.stage = SDL_GPU_SHADERSTAGE_VERTEX;
	geometry_vertex_desc.uniform_buffer_count = 1u;
	SDL_GPUShader* geometry_vertex = rg_gpu_shader_load(
	    desc->device, desc->shader_root, &geometry_vertex_desc);

	RgGpuShaderDesc solid_fragment_desc = {0};
	solid_fragment_desc.name = "rg_gui_solid.frag";
	solid_fragment_desc.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	SDL_GPUShader* solid_fragment = rg_gpu_shader_load(
	    desc->device, desc->shader_root, &solid_fragment_desc);

	RgGpuShaderDesc image_fragment_desc = {0};
	image_fragment_desc.name = "rg_gui_image.frag";
	image_fragment_desc.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	image_fragment_desc.sampler_count = 1u;
	SDL_GPUShader* image_fragment = rg_gpu_shader_load(
	    desc->device, desc->shader_root, &image_fragment_desc);

	int pipelines_ok = geometry_vertex && solid_fragment && image_fragment &&
	                   rg_gui_gpu_create_geometry_pipeline(renderer, desc->target_format,
	                                                       geometry_vertex, solid_fragment,
	                                                       &renderer->solid_pipeline) &&
	                   rg_gui_gpu_create_geometry_pipeline(renderer, desc->target_format,
	                                                       geometry_vertex, image_fragment,
	                                                       &renderer->image_pipeline);
	if (geometry_vertex) SDL_ReleaseGPUShader(desc->device, geometry_vertex);
	if (solid_fragment) SDL_ReleaseGPUShader(desc->device, solid_fragment);
	if (image_fragment) SDL_ReleaseGPUShader(desc->device, image_fragment);
	if (!pipelines_ok)
	{
		rg_gui_gpu_destroy(renderer);
		return 0;
	}

	SDL_GPUBufferCreateInfo buffer_info = {0};
	buffer_info.usage = SDL_GPU_BUFFERUSAGE_VERTEX;
	buffer_info.size = (u32)(sizeof(RgGuiGpuVertex) * renderer->vertex_capacity);
	renderer->geometry_buffer = SDL_CreateGPUBuffer(desc->device, &buffer_info);
	if (!renderer->geometry_buffer)
	{
		rg_gui_gpu_destroy(renderer);
		return 0;
	}
	return 1;
}

RGINLINE void rg_gui_gpu_destroy(RgGuiGpuRenderer* renderer)
{
	if (!renderer) return;
	if (renderer->geometry_buffer)
		SDL_ReleaseGPUBuffer(renderer->device, renderer->geometry_buffer);
	if (renderer->image_pipeline)
		SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->image_pipeline);
	if (renderer->solid_pipeline)
		SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->solid_pipeline);
	rg_gui_gpu_text_destroy(&renderer->text);
	if (renderer->items) SDL_free(renderer->items);
	if (renderer->vertices) SDL_free(renderer->vertices);
	memset(renderer, 0, sizeof(*renderer));
}

RGINLINE int rg_gui_gpu_clip_equal(RgGuiRect a, RgGuiRect b)
{
	return a.x == b.x && a.y == b.y && a.w == b.w && a.h == b.h;
}

RGINLINE int rg_gui_gpu_add_item(RgGuiGpuRenderer* renderer, u32 type,
                                 RgGuiTexture texture, RgGuiImageMaterial material,
                                 u32 first, u32 count,
                                 int clip_enabled, RgGuiRect clip)
{
	if (!count) return 1;
	if (renderer->item_count)
	{
		RgGuiGpuItem* previous = &renderer->items[renderer->item_count - 1u];
		if (previous->type == type && previous->texture == texture &&
		    previous->material == material &&
		    previous->clip_enabled == (u32)clip_enabled &&
		    (!clip_enabled || rg_gui_gpu_clip_equal(previous->clip, clip)) &&
		    previous->first + previous->count == first)
		{
			previous->count += count;
			return 1;
		}
	}
	if (renderer->item_count >= renderer->item_capacity) return 0;
	RgGuiGpuItem* item = &renderer->items[renderer->item_count++];
	item->clip = clip;
	item->texture = texture;
	item->material = material;
	item->first = first;
	item->count = count;
	item->type = type;
	item->clip_enabled = (u32)clip_enabled;
	return 1;
}

RGINLINE void rg_gui_gpu_set_vertex(RgGuiGpuVertex* vertex, f32 x, f32 y,
                                    u32 color, f32 u, f32 v)
{
	vertex->x = x;
	vertex->y = y;
	vertex->color = color;
	vertex->u = u;
	vertex->v = v;
}

RGINLINE int rg_gui_gpu_add_rect_vertices(RgGuiGpuRenderer* renderer,
                                          RgGuiRect rect, RgGuiRect uv, u32 color)
{
	if (renderer->vertex_count > renderer->vertex_capacity ||
	    renderer->vertex_capacity - renderer->vertex_count < 6u)
		return 0;
	RgGuiGpuVertex* out = &renderer->vertices[renderer->vertex_count];
	rg_gui_gpu_set_vertex(&out[0], rect.x, rect.y, color, uv.x, uv.y);
	rg_gui_gpu_set_vertex(&out[1], rect.x + rect.w, rect.y, color, uv.x + uv.w, uv.y);
	rg_gui_gpu_set_vertex(&out[2], rect.x + rect.w, rect.y + rect.h, color, uv.x + uv.w, uv.y + uv.h);
	rg_gui_gpu_set_vertex(&out[3], rect.x, rect.y, color, uv.x, uv.y);
	rg_gui_gpu_set_vertex(&out[4], rect.x + rect.w, rect.y + rect.h, color, uv.x + uv.w, uv.y + uv.h);
	rg_gui_gpu_set_vertex(&out[5], rect.x, rect.y + rect.h, color, uv.x, uv.y + uv.h);
	renderer->vertex_count += 6u;
	return 1;
}

RGINLINE int rg_gui_gpu_prepare(RgGuiGpuRenderer* renderer, RgGuiRenderer* text_renderer,
                                const RgGuiDrawList* list, u32 overlay_start)
{
	if (!renderer || !text_renderer || !list || (list->count && !list->cmds)) return 0;
	renderer->vertex_count = 0u;
	renderer->item_count = 0u;
	renderer->text_prepared = NULL;
	if (!rg_gui_renderer_prepare(text_renderer, list, overlay_start)) return 0;
	renderer->text_prepared = rg_gui_renderer_prepared(text_renderer);
	if (!renderer->text_prepared) return 0;

	if (overlay_start > list->count) overlay_start = list->count;
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

		if (cmd->type == RG_GUI_CMD_RECT)
		{
			u32 first = renderer->vertex_count;
			u32 color = rg_gui_renderer_base_pack_color(cmd->data.rect.color);
			RgGuiRect uv = {0};
			if (!rg_gui_gpu_add_rect_vertices(renderer, cmd->data.rect.rect, uv, color) ||
			    !rg_gui_gpu_add_item(renderer, RG_GUI_GPU_ITEM_SOLID, 0u, 0u,
			                         first, 6u,
			                         clip_enabled, clip))
				goto capacity_failure;
			continue;
		}
		if (cmd->type == RG_GUI_CMD_TRIANGLE)
		{
			if (renderer->vertex_count > renderer->vertex_capacity ||
			    renderer->vertex_capacity - renderer->vertex_count < 3u)
				goto capacity_failure;
			u32 first = renderer->vertex_count;
			u32 color = rg_gui_renderer_base_pack_color(cmd->data.triangle.color);
			RgGuiGpuVertex* out = &renderer->vertices[first];
			rg_gui_gpu_set_vertex(&out[0], cmd->data.triangle.a.x, cmd->data.triangle.a.y, color, 0.0f, 0.0f);
			rg_gui_gpu_set_vertex(&out[1], cmd->data.triangle.b.x, cmd->data.triangle.b.y, color, 0.0f, 0.0f);
			rg_gui_gpu_set_vertex(&out[2], cmd->data.triangle.c.x, cmd->data.triangle.c.y, color, 0.0f, 0.0f);
			renderer->vertex_count += 3u;
			if (!rg_gui_gpu_add_item(renderer, RG_GUI_GPU_ITEM_SOLID, 0u, 0u,
			                         first, 3u,
			                         clip_enabled, clip))
				goto capacity_failure;
			continue;
		}
		if (cmd->type == RG_GUI_CMD_IMAGE)
		{
			if (!cmd->data.image.texture) continue;
			u32 first = renderer->vertex_count;
			u32 color = rg_gui_renderer_base_pack_color(cmd->data.image.color);
			if (!rg_gui_gpu_add_rect_vertices(renderer, cmd->data.image.rect,
			                                  cmd->data.image.uv, color) ||
			    !rg_gui_gpu_add_item(renderer, RG_GUI_GPU_ITEM_IMAGE,
			                         cmd->data.image.texture, cmd->data.image.material,
			                         first, 6u,
			                         clip_enabled, clip))
				goto capacity_failure;
			continue;
		}
		if (cmd->type == RG_GUI_CMD_TEXT)
		{
			const char* text = cmd->data.text.text ? cmd->data.text.text : "";
			u32 expected = rg_gui_renderer_base_count_quads(
			    &text_renderer->core, text, strlen(text));
			if (!expected || run_cursor >= renderer->text_prepared->run_count) continue;

			u32 packed_color = rg_gui_renderer_base_pack_color(cmd->data.text.color);
			const RgGuiRendererRun* first_run = &renderer->text_prepared->runs[run_cursor];
			if (first_run->x != cmd->data.text.pos.x || first_run->y != cmd->data.text.pos.y ||
			    first_run->color != packed_color)
				continue;

			u32 cursor = run_cursor;
			u32 glyph_count = 0u;
			u32 first_glyph = first_run->first_output_instance;
			while (cursor < renderer->text_prepared->run_count && glyph_count < expected)
			{
				const RgGuiRendererRun* run = &renderer->text_prepared->runs[cursor];
				if (run->x != cmd->data.text.pos.x || run->y != cmd->data.text.pos.y ||
				    run->color != packed_color || run->first_output_instance != first_glyph + glyph_count)
					break;
				glyph_count += run->quad_count;
				cursor++;
			}
			if (glyph_count == expected)
			{
				if (!rg_gui_gpu_add_item(renderer, RG_GUI_GPU_ITEM_TEXT, 0u, 0u,
				                         first_glyph, glyph_count, clip_enabled, clip))
					goto capacity_failure;
				run_cursor = cursor;
			}
		}
	}
	return 1;

capacity_failure:
	renderer->vertex_count = 0u;
	renderer->item_count = 0u;
	renderer->text_prepared = NULL;
	return 0;
}

RGINLINE int rg_gui_gpu_stage_upload(RgGuiGpuRenderer* renderer,
                                     const RgGuiRenderer* text_renderer,
                                     RgGpuUploadRing* ring,
                                     RgGuiGpuUpload* upload)
{
	if (!renderer || !text_renderer || !ring || !upload || !renderer->text_prepared) return 0;
	memset(upload, 0, sizeof(*upload));
	u32 initial_offset = ring->offset;
	if (renderer->text_prepared->run_count && renderer->text_prepared->glyph_count)
	{
		if (!rg_gui_gpu_text_stage_upload(&renderer->text, text_renderer,
		                                  renderer->text_prepared, ring, &upload->text))
			goto failure;
		upload->has_text = 1u;
	}
	if (renderer->vertex_count)
	{
		u32 bytes = renderer->vertex_count * (u32)sizeof(RgGuiGpuVertex);
		if (!rg_gpu_upload_ring_alloc(ring, bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN,
		                              &upload->geometry))
			goto failure;
		memcpy(rg_gpu_upload_ring_ptr(ring, &upload->geometry), renderer->vertices, bytes);
		upload->has_geometry = 1u;
	}
	return 1;

failure:
	ring->offset = initial_offset;
	memset(upload, 0, sizeof(*upload));
	return 0;
}

RGINLINE void rg_gui_gpu_encode_upload(RgGuiGpuRenderer* renderer,
                                       const RgGuiRenderer* text_renderer,
                                       SDL_GPUCopyPass* copy,
                                       const RgGpuUploadRing* ring,
                                       const RgGuiGpuUpload* upload)
{
	if (!renderer || !text_renderer || !copy || !ring || !upload) return;
	if (upload->has_text)
		rg_gui_gpu_text_encode_upload(&renderer->text, text_renderer, copy, ring, &upload->text);
	if (upload->has_geometry)
	{
		SDL_GPUTransferBufferLocation source = {ring->buffer, upload->geometry.offset};
		SDL_GPUBufferRegion destination = {renderer->geometry_buffer, 0u, upload->geometry.size};
		SDL_UploadToGPUBuffer(copy, &source, &destination, true);
	}
}

RGINLINE int rg_gui_gpu_dispatch(const RgGuiGpuRenderer* renderer,
                                 SDL_GPUCommandBuffer* command_buffer)
{
	if (!renderer || !command_buffer || !renderer->text_prepared) return 0;
	if (!renderer->text_prepared->run_count || !renderer->text_prepared->glyph_count) return 1;
	return rg_gui_gpu_text_dispatch(&renderer->text, command_buffer, renderer->text_prepared);
}

RGINLINE SDL_Rect rg_gui_gpu_scissor(RgGuiRect clip, int clip_enabled,
                                     const RgGuiGpuDrawDesc* desc)
{
	SDL_Rect result = desc->viewport;
	if (!clip_enabled) return result;
	int x0 = (int)SDL_floorf(clip.x + desc->offset_x);
	int y0 = (int)SDL_floorf(clip.y + desc->offset_y);
	int x1 = (int)SDL_ceilf(clip.x + clip.w + desc->offset_x);
	int y1 = (int)SDL_ceilf(clip.y + clip.h + desc->offset_y);
	int viewport_x1 = result.x + result.w;
	int viewport_y1 = result.y + result.h;
	if (x0 < result.x) x0 = result.x;
	if (y0 < result.y) y0 = result.y;
	if (x1 > viewport_x1) x1 = viewport_x1;
	if (y1 > viewport_y1) y1 = viewport_y1;
	if (x1 < x0) x1 = x0;
	if (y1 < y0) y1 = y0;
	result.x = x0;
	result.y = y0;
	result.w = x1 - x0;
	result.h = y1 - y0;
	return result;
}

RGINLINE RgGuiGpuStats rg_gui_gpu_draw(const RgGuiGpuRenderer* renderer,
                                       SDL_GPUCommandBuffer* command_buffer,
                                       SDL_GPURenderPass* pass,
                                       const RgGuiGpuDrawDesc* desc,
                                       const RgGuiGpuUpload* upload)
{
	RgGuiGpuStats stats = {0};
	if (!renderer || !command_buffer || !pass || !desc ||
	    desc->output_width == 0u || desc->output_height == 0u)
		return stats;

	struct RgGuiGpuGeometryUniforms
	{
		f32 output_width;
		f32 output_height;
		f32 offset_x;
		f32 offset_y;
	} geometry_uniforms;
	geometry_uniforms.output_width = (f32)desc->output_width;
	geometry_uniforms.output_height = (f32)desc->output_height;
	geometry_uniforms.offset_x = desc->offset_x;
	geometry_uniforms.offset_y = desc->offset_y;

	struct RgGuiGpuTextUniforms
	{
		f32 output_width;
		f32 output_height;
		f32 offset_x;
		f32 offset_y;
		f32 atlas_inv_width;
		f32 atlas_inv_height;
		f32 padding0;
		f32 padding1;
	} text_uniforms;
	text_uniforms.output_width = (f32)desc->output_width;
	text_uniforms.output_height = (f32)desc->output_height;
	text_uniforms.offset_x = desc->offset_x;
	text_uniforms.offset_y = desc->offset_y;
	text_uniforms.atlas_inv_width = 1.0f / (f32)renderer->text.atlas_width;
	text_uniforms.atlas_inv_height = 1.0f / (f32)renderer->text.atlas_height;
	text_uniforms.padding0 = 0.0f;
	text_uniforms.padding1 = 0.0f;

	u32 bound_type = UINT32_MAX;
	RgGuiTexture bound_texture = 0u;
	for (u32 i = 0u; i < renderer->item_count; i++)
	{
		const RgGuiGpuItem* item = &renderer->items[i];
		SDL_Rect scissor = rg_gui_gpu_scissor(item->clip, item->clip_enabled != 0u, desc);
		if (scissor.w <= 0 || scissor.h <= 0) continue;
		SDL_SetGPUScissor(pass, &scissor);

		if (item->type == RG_GUI_GPU_ITEM_TEXT)
		{
			if (bound_type != item->type)
			{
				SDL_BindGPUGraphicsPipeline(pass, renderer->text.pipeline);
				SDL_PushGPUVertexUniformData(command_buffer, 0u, &text_uniforms, sizeof(text_uniforms));
				SDL_GPUTextureSamplerBinding atlas = {
				    renderer->text.atlas_texture, renderer->text.sampler};
				SDL_BindGPUFragmentSamplers(pass, 0u, &atlas, 1u);
				bound_type = item->type;
				bound_texture = 0u;
			}
			SDL_GPUBufferBinding binding = {
			    renderer->text.instance_buffer,
			    item->first * (u32)sizeof(RgGuiRendererGlyph)};
			SDL_BindGPUVertexBuffers(pass, 0u, &binding, 1u);
			SDL_DrawGPUPrimitives(pass, 6u, item->count, 0u, 0u);
			stats.text_instances += item->count;
		}
		else
		{
			int custom_image = 0;
			if (item->type == RG_GUI_GPU_ITEM_IMAGE && item->material != 0u &&
			    renderer->image_bind)
			{
				RgGuiGpuImageBindInfo bind_info = {0};
				bind_info.command_buffer = command_buffer;
				bind_info.pass = pass;
				bind_info.sampler = renderer->text.sampler;
				bind_info.texture = item->texture;
				bind_info.material = item->material;
				stats.image_bind_calls++;
				RgGuiGpuImageBindResult bind_result = renderer->image_bind(
				    renderer->image_bind_user, &bind_info);
				if (bind_result == RG_GUI_GPU_IMAGE_BIND_CUSTOM)
				{
					custom_image = 1;
					stats.custom_image_draw_calls++;
				}
				else if (bind_result != RG_GUI_GPU_IMAGE_BIND_DEFAULT)
				{
					stats.image_bind_failures++;
					bound_type = UINT32_MAX;
					bound_texture = 0u;
					continue;
				}
			}

			if (custom_image)
			{
				SDL_PushGPUVertexUniformData(command_buffer, 0u,
				                             &geometry_uniforms, sizeof(geometry_uniforms));
			}
			else
			{
				if (bound_type != item->type)
				{
					SDL_BindGPUGraphicsPipeline(
					    pass, item->type == RG_GUI_GPU_ITEM_IMAGE ? renderer->image_pipeline : renderer->solid_pipeline);
					SDL_PushGPUVertexUniformData(command_buffer, 0u,
					                             &geometry_uniforms, sizeof(geometry_uniforms));
					bound_type = item->type;
					bound_texture = 0u;
				}
				if (item->type == RG_GUI_GPU_ITEM_IMAGE && bound_texture != item->texture)
				{
					SDL_GPUTextureSamplerBinding image = {
					    (SDL_GPUTexture*)(uintptr_t)item->texture, renderer->text.sampler};
					SDL_BindGPUFragmentSamplers(pass, 0u, &image, 1u);
					bound_texture = item->texture;
				}
			}
			SDL_GPUBufferBinding binding = {
			    renderer->geometry_buffer,
			    item->first * (u32)sizeof(RgGuiGpuVertex)};
			SDL_BindGPUVertexBuffers(pass, 0u, &binding, 1u);
			SDL_DrawGPUPrimitives(pass, item->count, 1u, 0u, 0u);
			stats.geometry_vertices += item->count;
			if (custom_image)
			{
				bound_type = UINT32_MAX;
				bound_texture = 0u;
			}
		}
		stats.items++;
		stats.draw_calls++;
	}

	stats.dispatches = renderer->text_prepared && renderer->text_prepared->glyph_count ? 1u : 0u;
	if (upload)
	{
		stats.run_upload_bytes = upload->text.runs.size;
		stats.cache_upload_bytes = upload->text.cached_quads.size;
		stats.geometry_upload_bytes = upload->geometry.size;
		stats.full_cache_upload = upload->text.full_cache_upload;
	}
	return stats;
}

#endif // RG_GUI_GPU_H
