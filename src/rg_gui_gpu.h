// rg_gui_gpu - SDL3 GPU renderer for rg_gui draw lists
//
// Part of the Reverse Gravity (rg_) libraries.
// Renders rectangles, triangles, images, clips, and page-cached rg_text glyphs
// while preserving draw-list order. Indexed references fetch cached glyph pages
// directly. The standalone text renderer also supports compute expansion.
// Frame building (prepare and packing) is renderer-neutral: rg_gui_frame.h.
//
// NOTES:
//   - RgGuiTexture values are SDL_GPUTexture pointers stored as uintptr_t.
//   - Shader binaries are loaded through rg_gpu from the supplied shader root.
//   - All functions have internal linkage and work in unity builds.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_GUI_GPU_H
#define RG_GUI_GPU_H

#include "rg_gui_frame.h"
#include "rg_gpu.h"

#include <SDL3/SDL.h>
#include <string.h>


#ifndef RG_GUI_GPU_RUN_ASSERT
#include <assert.h>
#define RG_GUI_GPU_RUN_ASSERT(x) assert(x)
#endif

#ifndef RG_GUI_GPU_UPLOAD_PACKET_COUNT
#define RG_GUI_GPU_UPLOAD_PACKET_COUNT 3u
#endif

typedef struct RgGuiGpuUploadPacketSlot
{
	RgGuiRendererRange* ranges;
	RgGuiRendererBatch* batches;
	u32 range_capacity;
	u32 batch_capacity;
	u64 token;
	u32 active;
	u32 encoded;
} RgGuiGpuUploadPacketSlot;

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
	u32 cache_upload_ranges;
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
	const RgGuiRenderer* cache_context;
	u64 cache_revision;
	u64 next_upload_token;
	u64 last_acknowledged_token;
	u64 cache_tracking_epoch;
	RgGuiGpuUploadPacketSlot upload_packets[RG_GUI_GPU_UPLOAD_PACKET_COUNT];
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
	for (u32 i = 0u; i < RG_GUI_GPU_UPLOAD_PACKET_COUNT; i++)
	{
		SDL_free(renderer->upload_packets[i].ranges);
		SDL_free(renderer->upload_packets[i].batches);
	}
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
	u32 glyph_count;
	u32 batch_count;
	/* Immutable clip batches expressed in glyph offsets, not page segments. */
	const RgGuiRendererBatch* glyph_batches;
	u32 cache_range_count;
	u32 full_cache_upload;
	const RgGuiRendererRange* cache_ranges;
	const RgGuiRenderer* cache_context;
	u64 cache_revision;
	u64 packet_token;
	u64 cache_tracking_epoch;
	u32 packet_slot;
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

/** Invalidate the submitted GPU cache mirror after CPU renderer reinitialization
 * or font/atlas replacement. Rebuild the CPU layout cache separately when its
 * font data changes. This does not replace the atlas texture/dimensions or
 * refresh a borrowed text lookup. Existing staged text packets fail upload_ready and must be
 * cancelled/aborted before restaging; their snapshots remain owned until then.
 * Already submitted work may finish on the same SDL queue without a GPU wait. */
RGINLINE void rg_gui_gpu_text_invalidate_cache(RgGuiGpuRunRenderer* renderer)
{
	if (!renderer) return;
	renderer->cache_initialized = 0u;
	renderer->cache_context = NULL;
	renderer->cache_revision = 0u;
	renderer->cache_tracking_epoch++;
}

/** A packet owns immutable CPU range and glyph-batch metadata until commit or abort.
 * Transfer slices must remain valid until encoding, using the upload ring's normal
 * submission/cycling rules. Keep prepare/encode/draw/submit on the same SDL queue.
 * Encoding alone never advances the persistent cache mirror's submitted revision. */
RGINLINE int rg_gui_gpu_text_stage_upload(RgGuiGpuRunRenderer* renderer,
                                          const RgGuiRenderer* context,
                                          const RgGuiRendererPrepared* prepared,
                                          RgGpuUploadRing* ring,
                                          RgGuiGpuTextUpload* upload)
{
	if (!upload) return 0;
	memset(upload, 0, sizeof(*upload));
	if (!renderer || !context || !context->initialized || !prepared || !ring ||
	    !prepared->run_count || !prepared->glyph_count || !prepared->runs ||
	    !prepared->batch_count || !prepared->batches ||
	    prepared->batch_count > prepared->run_count ||
	    prepared->run_count > renderer->run_capacity ||
	    prepared->glyph_count > renderer->instance_capacity ||
	    context->total_pages * context->page_quads > renderer->cached_quad_capacity)
		return 0;
	u32 slot_index = 0u;
	while (slot_index < RG_GUI_GPU_UPLOAD_PACKET_COUNT && renderer->upload_packets[slot_index].active)
		slot_index++;
	if (slot_index == RG_GUI_GPU_UPLOAD_PACKET_COUNT) return 0;
	RgGuiGpuUploadPacketSlot* slot = &renderer->upload_packets[slot_index];
	int full = !renderer->cache_initialized || renderer->cache_context != context;
	u64 after_revision = full ? 0u : renderer->cache_revision;
	u32 quad_count = 0u;
	u32 range_count = 0u;
	if (full || after_revision != context->cache_revision)
		range_count = rg_gui_renderer_upload_ranges(context, after_revision, NULL, 0u, &quad_count);
	if (range_count == UINT32_MAX) return 0;
	if (range_count > slot->range_capacity)
	{
		RgGuiRendererRange* ranges = (RgGuiRendererRange*)SDL_realloc(
		    slot->ranges, sizeof(RgGuiRendererRange) * (size_t)range_count);
		if (!ranges) return 0;
		slot->ranges = ranges;
		slot->range_capacity = range_count;
	}
	if (range_count && rg_gui_renderer_upload_ranges(context, after_revision,
	    slot->ranges, slot->range_capacity, NULL) != range_count) return 0;
	if (prepared->batch_count > slot->batch_capacity)
	{
		RgGuiRendererBatch* batches = (RgGuiRendererBatch*)SDL_realloc(
		    slot->batches, sizeof(RgGuiRendererBatch) * (size_t)prepared->batch_count);
		if (!batches) return 0;
		slot->batches = batches;
		slot->batch_capacity = prepared->batch_count;
	}
	for (u32 i = 0u; i < prepared->batch_count; i++)
	{
		const RgGuiRendererBatch* batch = &prepared->batches[i];
		if (!batch->instance_count || batch->first_instance >= prepared->run_count ||
		    batch->instance_count > prepared->run_count - batch->first_instance) return 0;
		const RgGuiRendererRun* first = &prepared->runs[batch->first_instance];
		const RgGuiRendererRun* last = &prepared->runs[batch->first_instance + batch->instance_count - 1u];
		if (first->first_output_instance >= prepared->glyph_count ||
		    last->first_output_instance > prepared->glyph_count ||
		    last->quad_count > prepared->glyph_count - last->first_output_instance ||
		    last->first_output_instance + last->quad_count <= first->first_output_instance) return 0;
		slot->batches[i] = *batch;
		slot->batches[i].first_instance = first->first_output_instance;
		slot->batches[i].instance_count = last->first_output_instance + last->quad_count - first->first_output_instance;
	}

	u32 initial_offset = ring->offset;
	u32 run_bytes = prepared->run_count * (u32)sizeof(RgGuiRendererRun);
	if (!rg_gpu_upload_ring_alloc(ring, run_bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN,
	                              &upload->runs)) goto failure;
	memcpy(rg_gpu_upload_ring_ptr(ring, &upload->runs), prepared->runs, run_bytes);
	if (quad_count)
	{
		u32 cache_bytes = quad_count * (u32)sizeof(RgGuiRendererCachedQuad);
		if (!rg_gpu_upload_ring_alloc(ring, cache_bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN,
		                              &upload->cached_quads)) goto failure;
		u8* destination = (u8*)rg_gpu_upload_ring_ptr(ring, &upload->cached_quads);
		for (u32 i = 0u; i < range_count; i++)
		{
			const RgGuiRendererRange* range = &slot->ranges[i];
			size_t bytes = sizeof(RgGuiRendererCachedQuad) * range->quad_count;
			memcpy(destination, context->core.cache_quads + range->first_quad, bytes);
			destination += bytes;
		}
	}
	slot->token = ++renderer->next_upload_token;
	if (!slot->token) slot->token = ++renderer->next_upload_token;
	slot->active = 1u;
	slot->encoded = 0u;
	upload->run_count = prepared->run_count;
	upload->glyph_count = prepared->glyph_count;
	upload->batch_count = prepared->batch_count;
	upload->glyph_batches = slot->batches;
	upload->cache_range_count = range_count;
	upload->full_cache_upload = full ? 1u : 0u;
	upload->cache_ranges = slot->ranges;
	upload->cache_context = context;
	upload->cache_revision = context->cache_revision;
	upload->packet_slot = slot_index;
	upload->packet_token = slot->token;
	upload->cache_tracking_epoch = renderer->cache_tracking_epoch;
	return 1;
failure:
	ring->offset = initial_offset;
	memset(upload, 0, sizeof(*upload));
	return 0;
}

RGINLINE int rg_gui_gpu_text_upload_active(const RgGuiGpuRunRenderer* renderer,
                                           const RgGuiGpuTextUpload* upload)
{
	return renderer && upload && upload->packet_token &&
	       upload->packet_slot < RG_GUI_GPU_UPLOAD_PACKET_COUNT &&
	       renderer->upload_packets[upload->packet_slot].active &&
	       renderer->upload_packets[upload->packet_slot].token == upload->packet_token;
}

/** Check immediately before submission (before swapchain acquisition if the
 * caller must cancel on failure). Submit and acknowledge packets in stage order.
 * A false result requires cancellation/abort and restaging; a later commit cannot
 * repair a draw that already read cache pages overwritten out of order. */
RGINLINE int rg_gui_gpu_text_upload_ready(const RgGuiGpuRunRenderer* renderer,
                                          const RgGuiGpuTextUpload* upload)
{
	if (!rg_gui_gpu_text_upload_active(renderer, upload) ||
	    !renderer->upload_packets[upload->packet_slot].encoded ||
	    upload->packet_token <= renderer->last_acknowledged_token ||
	    upload->cache_tracking_epoch != renderer->cache_tracking_epoch) return 0;
	if (!upload->full_cache_upload &&
	    (!renderer->cache_initialized || renderer->cache_context != upload->cache_context ||
	     renderer->cache_revision > upload->cache_revision)) return 0;
	for (u32 i = 0u; i < RG_GUI_GPU_UPLOAD_PACKET_COUNT; i++)
		if (renderer->upload_packets[i].active &&
		    renderer->upload_packets[i].token < upload->packet_token) return 0;
	return 1;
}

/** Acknowledge successful SDL submission immediately, in submission order.
 * GPU completion is not required: ordered SDL buffer dependencies protect earlier
 * reads while subsequent writes update this persistent, non-cycled mirror. */
RGINLINE void rg_gui_gpu_text_upload_commit(RgGuiGpuRunRenderer* renderer,
                                            const RgGuiGpuTextUpload* upload)
{
	if (!rg_gui_gpu_text_upload_active(renderer, upload)) return;
	if (rg_gui_gpu_text_upload_ready(renderer, upload))
	{
		renderer->cache_context = upload->cache_context;
		renderer->cache_revision = upload->cache_revision;
		renderer->cache_initialized = 1u;
	}
	else
	{
		/* Ambiguous acknowledgement order must never skip unsent page revisions. */
		renderer->cache_initialized = 0u;
		renderer->cache_tracking_epoch++;
	}
	if (upload->packet_token > renderer->last_acknowledged_token)
		renderer->last_acknowledged_token = upload->packet_token;
	renderer->upload_packets[upload->packet_slot].active = 0u;
}

/** Cancel a staged/encoded packet or report a failed submission. Its revisions
 * remain newer than the last successful submission and are staged again. */
RGINLINE void rg_gui_gpu_text_upload_abort(RgGuiGpuRunRenderer* renderer,
                                           const RgGuiGpuTextUpload* upload)
{
	if (rg_gui_gpu_text_upload_active(renderer, upload))
	{
		renderer->upload_packets[upload->packet_slot].active = 0u;
		/* A failed submission may leave the device state uncertain. A fresh full
		 * upload is required; older staged packets cannot restore this epoch. */
		renderer->cache_initialized = 0u;
		renderer->cache_tracking_epoch++;
	}
}

/** Requested CPU bytes held for immutable range and glyph-batch snapshots.
 * Per slot, ranges are bounded by cache pages and batches by run_capacity. */
RGINLINE size_t rg_gui_gpu_text_upload_memory_reserved(const RgGuiGpuRunRenderer* renderer)
{
	size_t bytes = 0u;
	if (renderer)
		for (u32 i = 0u; i < RG_GUI_GPU_UPLOAD_PACKET_COUNT; i++)
			bytes += sizeof(RgGuiRendererRange) * renderer->upload_packets[i].range_capacity +
			         sizeof(RgGuiRendererBatch) * renderer->upload_packets[i].batch_capacity;
	return bytes;
}

/** Encode only the immutable staged ranges; never inspect mutable cache chains. */
RGINLINE void rg_gui_gpu_text_encode_upload(RgGuiGpuRunRenderer* renderer,
                                            SDL_GPUCopyPass* copy,
                                            const RgGpuUploadRing* ring,
                                            const RgGuiGpuTextUpload* upload)
{
	if (!copy || !ring || !rg_gui_gpu_text_upload_active(renderer, upload) || !upload->runs.size)
		return;
	SDL_GPUTransferBufferLocation source = {ring->buffer, upload->runs.offset};
	SDL_GPUBufferRegion destination = {renderer->run_buffer, 0u, upload->runs.size};
	SDL_UploadToGPUBuffer(copy, &source, &destination, true);
	u32 source_offset = upload->cached_quads.offset;
	for (u32 i = 0u; i < upload->cache_range_count; i++)
	{
		const RgGuiRendererRange* range = &upload->cache_ranges[i];
		u32 bytes = range->quad_count * (u32)sizeof(RgGuiRendererCachedQuad);
		source.offset = source_offset;
		destination.buffer = renderer->cached_quad_buffer;
		destination.offset = range->first_quad * (u32)sizeof(RgGuiRendererCachedQuad);
		destination.size = bytes;
		SDL_UploadToGPUBuffer(copy, &source, &destination, false);
		source_offset += bytes;
	}
	renderer->upload_packets[upload->packet_slot].encoded = 1u;
}

/** Expand all page segments with the production compute shader. */
RGINLINE int rg_gui_gpu_text_dispatch_upload(const RgGuiGpuRunRenderer* renderer,
                                      SDL_GPUCommandBuffer* command_buffer,
                                      const RgGuiGpuTextUpload* upload)
{
	if (!rg_gui_gpu_text_upload_active(renderer, upload) ||
	    !renderer->upload_packets[upload->packet_slot].encoded) return 0;
	RgGuiRendererPrepared counts = {0};
	counts.run_count = upload->run_count;
	counts.glyph_count = upload->glyph_count;
	return rg_gui_gpu_run_dispatch(renderer, command_buffer, &counts);
}

/** Draw immutable staged clip batches, independent of subsequent CPU preparation. */
RGINLINE RgGuiGpuRunStats rg_gui_gpu_text_draw(
    const RgGuiGpuRunRenderer* renderer, SDL_GPUCommandBuffer* command_buffer,
    SDL_GPURenderPass* pass, const RgGuiGpuRunDrawDesc* desc,
    const RgGuiGpuTextUpload* upload)
{
	RgGuiGpuRunStats stats = {0};
	if (!command_buffer || !pass || !desc || !desc->output_width || !desc->output_height ||
	    !rg_gui_gpu_text_upload_active(renderer, upload) ||
	    !renderer->upload_packets[upload->packet_slot].encoded) return stats;
	f32 uniforms[8] = {(f32)desc->output_width, (f32)desc->output_height,
	    desc->offset_x, desc->offset_y, 1.0f / (f32)renderer->atlas_width,
	    1.0f / (f32)renderer->atlas_height, 0, 0};
	SDL_BindGPUGraphicsPipeline(pass, renderer->pipeline);
	SDL_PushGPUVertexUniformData(command_buffer, 0u, uniforms, sizeof(uniforms));
	SDL_GPUTextureSamplerBinding atlas = {renderer->atlas_texture, renderer->sampler};
	SDL_BindGPUFragmentSamplers(pass, 0u, &atlas, 1u);
	for (u32 i = 0u; i < upload->batch_count; i++)
	{
		const RgGuiRendererBatch* batch = &upload->glyph_batches[i];
		SDL_Rect scissor = rg_gui_gpu_run_batch_scissor(batch, desc);
		if (scissor.w <= 0 || scissor.h <= 0) continue;
		SDL_SetGPUScissor(pass, &scissor);
		SDL_GPUBufferBinding binding = {renderer->instance_buffer,
		    batch->first_instance * (u32)sizeof(RgGuiRendererGlyph)};
		SDL_BindGPUVertexBuffers(pass, 0u, &binding, 1u);
		SDL_DrawGPUPrimitives(pass, 6u, batch->instance_count, 0u, 0u);
		stats.instances += batch->instance_count;
		stats.batches++; stats.draw_calls++;
	}
	stats.dispatches = 1u;
	stats.run_upload_bytes = upload->runs.size;
	stats.cache_upload_bytes = upload->cached_quads.size;
	stats.full_cache_upload = upload->full_cache_upload;
	stats.cache_upload_ranges = upload->cache_range_count;
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
 * after a partial bind. After CUSTOM or FAILED rg_gui invalidates its cached
 * pipeline, texture, and scissor state, so a callback may also set a scissor.
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
	/* Zero derives max_text_instances + resolved geometry capacity / 3. */
	u32 max_frame_refs;
	/* GPU-only packed buffers; zero selects one. Valid range is 1..3. */
	u32 frame_buffer_count;
	/* Nonzero computes rg_gui_gpu_frame_signature during preparation. Off by
	 * default: it costs a few nanoseconds per command, only useful to callers
	 * that skip unchanged frames. */
	u32 frame_signature;
} RgGuiGpuDesc;

typedef RgGuiGpuRunDrawDesc RgGuiGpuDrawDesc;


typedef struct RgGuiGpuFramePacketSlot
{
	RgGuiGpuItem* items;
	RgGuiGpuIndexedBatch* batches;
	u32 item_capacity;
	u32 batch_capacity;
	u64 token;
	u32 active;
	u32 encoded;
} RgGuiGpuFramePacketSlot;

typedef struct RgGuiGpuUpload
{
	RgGuiGpuTextUpload text;
	RgGpuUploadSlice geometry;
	u32 has_text;
	u32 has_geometry;
	const RgGuiGpuItem* items;
	u32 item_count;
	u32 vertex_count;
	u32 packet_slot;
	u64 packet_token;
	RgGpuUploadSlice frame;
	const RgGuiGpuIndexedBatch* batches;
	u32 batch_count;
	u32 ref_count;
	u32 geometry_offset;
	u32 refs_offset;
	u32 frame_buffer_index;
	/* The clip table: clip_count RgGuiGpuFrameClip entries at clips_offset. */
	u32 clip_count;
	u32 clips_offset;
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
	u32 cache_upload_ranges;
	/* Submitted indexed references and indices; vertices includes custom draws. */
	u32 indexed_refs;
	u32 vertices;
	u32 indices;
	u32 copy_calls;
} RgGuiGpuStats;

typedef struct RgGuiGpuRenderer
{
	SDL_GPUDevice* device;
	RgGuiGpuRunRenderer text;
	SDL_GPUGraphicsPipeline* solid_pipeline;
	SDL_GPUGraphicsPipeline* image_pipeline;
	SDL_GPUBuffer* geometry_buffer;
	/* The frame being prepared and packed (vertices, items, clips, capacities). */
	RgGuiFrameBuilder frame;
	RgGuiGpuImageBindFn image_bind;
	void* image_bind_user;
	RgGuiGpuFramePacketSlot upload_packets[RG_GUI_GPU_UPLOAD_PACKET_COUNT];
	u64 next_upload_token;
	u64 last_acknowledged_token;
	SDL_GPUGraphicsPipeline* indexed_pipeline;
	SDL_GPUBuffer* index_buffer;
	SDL_GPUBuffer* frame_buffers[3];
	u32 frame_buffer_count;
	u32 next_frame_buffer;
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

/* Each reference addresses four vertices. Index count may exceed 65535 while
 * every index value still fits in 16 bits. Larger reference limits use 32 bits. */
RGINLINE u32 rg_gui_gpu_index_element_size(u32 ref_capacity)
{
	return ref_capacity <= 16384u ? (u32)sizeof(u16) : (u32)sizeof(u32);
}

RGINLINE void rg_gui_gpu_fill_indices(void* memory, u32 ref_capacity)
{
	if (rg_gui_gpu_index_element_size(ref_capacity) == sizeof(u16))
	{
		u16* indices = (u16*)memory;
		for (u32 i = 0u; i < ref_capacity; i++)
		{
			u32 base = i * 4u;
			u16* out = indices + i * 6u;
			out[0] = (u16)base;
			out[1] = (u16)(base + 1u);
			out[2] = (u16)(base + 2u);
			out[3] = (u16)base;
			out[4] = (u16)(base + 2u);
			out[5] = (u16)(base + 3u);
		}
	}
	else
	{
		u32* indices = (u32*)memory;
		for (u32 i = 0u; i < ref_capacity; i++)
		{
			u32 base = i * 4u;
			u32* out = indices + i * 6u;
			out[0] = base;
			out[1] = base + 1u;
			out[2] = base + 2u;
			out[3] = base;
			out[4] = base + 2u;
			out[5] = base + 3u;
		}
	}
}

RGINLINE int rg_gui_gpu_create_indices(RgGuiGpuRenderer* renderer)
{
	SDL_GPUTransferBufferCreateInfo transfer_desc = {0};
	transfer_desc.usage = SDL_GPU_TRANSFERBUFFERUSAGE_UPLOAD;
	transfer_desc.size = renderer->frame.ref_capacity * 6u * rg_gui_gpu_index_element_size(renderer->frame.ref_capacity);
	SDL_GPUTransferBuffer* transfer = SDL_CreateGPUTransferBuffer(renderer->device, &transfer_desc);
	if (!transfer) return 0;
	void* indices = SDL_MapGPUTransferBuffer(renderer->device, transfer, false);
	if (!indices)
	{
		SDL_ReleaseGPUTransferBuffer(renderer->device, transfer);
		return 0;
	}
	rg_gui_gpu_fill_indices(indices, renderer->frame.ref_capacity);
	SDL_UnmapGPUTransferBuffer(renderer->device, transfer);
	SDL_GPUCommandBuffer* command = SDL_AcquireGPUCommandBuffer(renderer->device);
	SDL_GPUCopyPass* copy = command ? SDL_BeginGPUCopyPass(command) : NULL;
	if (!copy)
	{
		if (command) SDL_CancelGPUCommandBuffer(command);
		SDL_ReleaseGPUTransferBuffer(renderer->device, transfer);
		return 0;
	}
	SDL_GPUTransferBufferLocation source = {transfer, 0u};
	SDL_GPUBufferRegion destination = {renderer->index_buffer, 0u, transfer_desc.size};
	SDL_UploadToGPUBuffer(copy, &source, &destination, false);
	SDL_EndGPUCopyPass(copy);
	SDL_GPUFence* fence = SDL_SubmitGPUCommandBufferAndAcquireFence(command);
	int result = fence && SDL_WaitForGPUFences(renderer->device, true, &fence, 1u);
	if (fence) SDL_ReleaseGPUFence(renderer->device, fence);
	SDL_ReleaseGPUTransferBuffer(renderer->device, transfer);
	return result;
}

RGINLINE int rg_gui_gpu_create_indexed_pipeline(RgGuiGpuRenderer* renderer,
                                                const RgGuiGpuDesc* desc)
{
	RgGpuShaderDesc vertex_desc = {0};
	vertex_desc.name = "rg_gui_indexed.vert";
	vertex_desc.stage = SDL_GPU_SHADERSTAGE_VERTEX;
	vertex_desc.uniform_buffer_count = 1u;
	vertex_desc.storage_buffer_count = 2u;
	RgGpuShaderDesc fragment_desc = {0};
	fragment_desc.name = "rg_gui_indexed.frag";
	fragment_desc.stage = SDL_GPU_SHADERSTAGE_FRAGMENT;
	fragment_desc.sampler_count = 1u;
	SDL_GPUShader* vertex = rg_gpu_shader_load(desc->device, desc->shader_root, &vertex_desc);
	SDL_GPUShader* fragment = rg_gpu_shader_load(desc->device, desc->shader_root, &fragment_desc);
	SDL_GPUColorTargetDescription target = {0};
	target.format = desc->target_format;
	target.blend_state.enable_blend = true;
	target.blend_state.src_color_blendfactor = SDL_GPU_BLENDFACTOR_SRC_ALPHA;
	target.blend_state.dst_color_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	target.blend_state.color_blend_op = SDL_GPU_BLENDOP_ADD;
	target.blend_state.src_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE;
	target.blend_state.dst_alpha_blendfactor = SDL_GPU_BLENDFACTOR_ONE_MINUS_SRC_ALPHA;
	target.blend_state.alpha_blend_op = SDL_GPU_BLENDOP_ADD;
	SDL_GPUGraphicsPipelineCreateInfo pipeline = {0};
	pipeline.vertex_shader = vertex;
	pipeline.fragment_shader = fragment;
	pipeline.primitive_type = SDL_GPU_PRIMITIVETYPE_TRIANGLELIST;
	pipeline.target_info.color_target_descriptions = &target;
	pipeline.target_info.num_color_targets = 1u;
	if (vertex && fragment)
		renderer->indexed_pipeline = SDL_CreateGPUGraphicsPipeline(renderer->device, &pipeline);
	if (vertex) SDL_ReleaseGPUShader(renderer->device, vertex);
	if (fragment) SDL_ReleaseGPUShader(renderer->device, fragment);
	return renderer->indexed_pipeline != NULL;
}

/** Create the indexed GUI renderer. The standalone text API retains compute rendering. */
RGINLINE int rg_gui_gpu_create(RgGuiGpuRenderer* renderer, const RgGuiGpuDesc* desc)
{
	if (!renderer || !desc || !desc->device || !desc->shader_root || !desc->atlas_texture ||
	    desc->target_format == SDL_GPU_TEXTUREFORMAT_INVALID ||
	    desc->atlas_width == 0u || desc->atlas_height == 0u ||
	    desc->max_cached_quads == 0u || desc->max_runs == 0u ||
	    desc->max_text_instances == 0u || desc->frame_buffer_count > 3u ||
	    desc->max_cached_quads > UINT32_MAX / (u32)sizeof(RgGuiRendererCachedQuad) ||
	    desc->max_runs > 0x3fffffffu)
	{
		return 0;
	}

	memset(renderer, 0, sizeof(*renderer));
	renderer->device = desc->device;
	renderer->image_bind = desc->image_bind;
	renderer->image_bind_user = desc->image_bind_user;
	renderer->frame.vertex_capacity = desc->max_geometry_vertices ? desc->max_geometry_vertices : RG_GUI_GPU_DEFAULT_MAX_GEOMETRY_VERTICES;
	renderer->frame.item_capacity = desc->max_items ? desc->max_items : RG_GUI_GPU_DEFAULT_MAX_ITEMS;
	renderer->frame_buffer_count = desc->frame_buffer_count ? desc->frame_buffer_count : 1u;
	renderer->frame.frame_signature_enabled = desc->frame_signature ? 1u : 0u;
	u64 refs = desc->max_frame_refs ? desc->max_frame_refs : (u64)desc->max_text_instances + renderer->frame.vertex_capacity / 3u;
	u64 frame_bytes = rg_gui_gpu_frame_bytes(desc->max_runs, renderer->frame.vertex_capacity, refs,
	                                         (u64)renderer->frame.item_capacity + 1u);
	/* Each item adds at most one clip-table entry after the unclipped entry 0. */
	if (!refs || !renderer->frame.item_capacity || renderer->frame.item_capacity >= RG_GUI_GPU_FRAME_CLIP_LIMIT ||
	    refs > UINT32_MAX / 24u || frame_bytes > UINT32_MAX ||
	    renderer->frame.vertex_capacity > UINT32_MAX / (u32)sizeof(RgGuiGpuVertex) ||
	    sizeof(RgGuiGpuIndexedBatch) > SIZE_MAX / (size_t)renderer->frame.item_capacity ||
	    sizeof(RgGuiGpuItem) > SIZE_MAX / (size_t)renderer->frame.item_capacity ||
	    sizeof(RgGuiGpuFrameClip) > SIZE_MAX / ((size_t)renderer->frame.item_capacity + 1u))
	{
		return 0;
	}
	renderer->frame.ref_capacity = (u32)refs;
	renderer->frame.frame_capacity = (u32)frame_bytes;

	renderer->frame.vertices = (RgGuiGpuVertex*)SDL_malloc(
	    sizeof(RgGuiGpuVertex) * renderer->frame.vertex_capacity);
	renderer->frame.items = (RgGuiGpuItem*)SDL_malloc(
	    sizeof(RgGuiGpuItem) * renderer->frame.item_capacity);
	renderer->frame.clip_capacity = renderer->frame.item_capacity + 1u;
	renderer->frame.clips = (RgGuiGpuFrameClip*)SDL_malloc(
	    sizeof(RgGuiGpuFrameClip) * renderer->frame.clip_capacity);
	if (!renderer->frame.vertices || !renderer->frame.items || !renderer->frame.clips)
	{
		rg_gui_gpu_destroy(renderer);
		return 0;
	}

	/* Only the shared cache, sampler, and logical capacities are needed here. */
	renderer->text.device = desc->device;
	renderer->text.atlas_texture = desc->atlas_texture;
	renderer->text.atlas_width = desc->atlas_width;
	renderer->text.atlas_height = desc->atlas_height;
	renderer->text.cached_quad_capacity = desc->max_cached_quads;
	renderer->text.run_capacity = desc->max_runs;
	renderer->text.instance_capacity = desc->max_text_instances;
	renderer->frame.atlas_texture = (RgGuiTexture)(uintptr_t)desc->atlas_texture;
	renderer->frame.run_capacity = desc->max_runs;
	renderer->frame.cached_quad_capacity = desc->max_cached_quads;
	SDL_GPUBufferCreateInfo cache_desc = {0};
	cache_desc.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ;
	cache_desc.size = desc->max_cached_quads * (u32)sizeof(RgGuiRendererCachedQuad);
	renderer->text.cached_quad_buffer = SDL_CreateGPUBuffer(desc->device, &cache_desc);
	SDL_GPUSamplerCreateInfo sampler_desc = {0};
	sampler_desc.min_filter = desc->min_filter;
	sampler_desc.mag_filter = desc->mag_filter;
	sampler_desc.mipmap_mode = SDL_GPU_SAMPLERMIPMAPMODE_NEAREST;
	sampler_desc.address_mode_u = sampler_desc.address_mode_v = sampler_desc.address_mode_w =
	    SDL_GPU_SAMPLERADDRESSMODE_CLAMP_TO_EDGE;
	renderer->text.sampler = SDL_CreateGPUSampler(desc->device, &sampler_desc);
	if (!renderer->text.cached_quad_buffer || !renderer->text.sampler)
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
	buffer_info.usage = SDL_GPU_BUFFERUSAGE_GRAPHICS_STORAGE_READ | SDL_GPU_BUFFERUSAGE_VERTEX;
	buffer_info.size = renderer->frame.frame_capacity;
	for (u32 i = 0u; i < renderer->frame_buffer_count; i++)
	{
		renderer->frame_buffers[i] = SDL_CreateGPUBuffer(desc->device, &buffer_info);
		if (!renderer->frame_buffers[i])
		{
			rg_gui_gpu_destroy(renderer);
			return 0;
		}
	}
	buffer_info.usage = SDL_GPU_BUFFERUSAGE_INDEX;
	buffer_info.size = renderer->frame.ref_capacity * 6u * rg_gui_gpu_index_element_size(renderer->frame.ref_capacity);
	renderer->index_buffer = SDL_CreateGPUBuffer(desc->device, &buffer_info);
	if (!renderer->index_buffer || !rg_gui_gpu_create_indices(renderer) ||
	    !rg_gui_gpu_create_indexed_pipeline(renderer, desc))
	{
		rg_gui_gpu_destroy(renderer);
		return 0;
	}
	return 1;
}

RGINLINE void rg_gui_gpu_destroy(RgGuiGpuRenderer* renderer)
{
	if (!renderer) return;
	for (u32 i = 0u; i < RG_GUI_GPU_UPLOAD_PACKET_COUNT; i++)
	{
		SDL_free(renderer->upload_packets[i].items);
		SDL_free(renderer->upload_packets[i].batches);
	}
	for (u32 i = 0u; i < 3u; i++)
		if (renderer->frame_buffers[i]) SDL_ReleaseGPUBuffer(renderer->device, renderer->frame_buffers[i]);
	if (renderer->index_buffer) SDL_ReleaseGPUBuffer(renderer->device, renderer->index_buffer);
	if (renderer->indexed_pipeline) SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->indexed_pipeline);
	if (renderer->geometry_buffer)
		SDL_ReleaseGPUBuffer(renderer->device, renderer->geometry_buffer);
	if (renderer->image_pipeline)
		SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->image_pipeline);
	if (renderer->solid_pipeline)
		SDL_ReleaseGPUGraphicsPipeline(renderer->device, renderer->solid_pipeline);
	rg_gui_gpu_text_destroy(&renderer->text);
	if (renderer->frame.items) SDL_free(renderer->frame.items);
	if (renderer->frame.clips) SDL_free(renderer->frame.clips);
	if (renderer->frame.vertices) SDL_free(renderer->frame.vertices);
	memset(renderer, 0, sizeof(*renderer));
}



/** rg_gui_frame_prepare on the renderer's frame builder. */
RGINLINE int rg_gui_gpu_prepare(RgGuiGpuRenderer* renderer, RgGuiRenderer* text_renderer,
                                const RgGuiDrawList* list, u32 overlay_start)
{
	return renderer ? rg_gui_frame_prepare(&renderer->frame, text_renderer, list, overlay_start) : 0;
}

/** rg_gui_frame_signature of the renderer's frame builder; enabled through
 * RgGuiGpuDesc.frame_signature or rg_gui_gpu_set_frame_signature. */
RGINLINE u64 rg_gui_gpu_frame_signature(const RgGuiGpuRenderer* renderer)
{
	return renderer ? rg_gui_frame_signature(&renderer->frame) : 0u;
}

/** Enable or disable signature computation; RgGuiGpuDesc.frame_signature sets the initial state. */
RGINLINE void rg_gui_gpu_set_frame_signature(RgGuiGpuRenderer* renderer, int enabled)
{
	if (renderer) rg_gui_frame_set_signature(&renderer->frame, enabled);
}

/** Pack immutable frame data directly into a mapped caller-owned ring. Multiple
 * packets may be staged while CPU preparation continues. Encode/draw each packet
 * atomically, then submit/acknowledge in stage order. Ring bytes must remain valid
 * through GPU consumption. Failures restore the ring offset and publish no token.
 */
RGINLINE int rg_gui_gpu_stage_upload(RgGuiGpuRenderer* renderer,
                                     const RgGuiRenderer* text, RgGpuUploadRing* ring,
                                     RgGuiGpuUpload* upload)
{
	if (!upload) return 0;
	memset(upload, 0, sizeof(*upload));
	if (!renderer || !text || !text->initialized || !ring || !ring->mapped ||
	    !renderer->frame.text_prepared || !renderer->frame_buffer_count || renderer->frame_buffer_count > 3u ||
	    !renderer->frame.ref_capacity || !renderer->frame.frame_capacity ||
	    renderer->frame.item_count > renderer->frame.item_capacity ||
	    renderer->frame.vertex_count > renderer->frame.vertex_capacity ||
	    renderer->frame.text_prepared->run_count > renderer->frame.run_capacity ||
	    renderer->frame.text_prepared->glyph_count > renderer->text.instance_capacity ||
	    (renderer->frame.item_count && !renderer->frame.items) || (renderer->frame.vertex_count && !renderer->frame.vertices) ||
	    (renderer->frame.text_prepared->run_count && !renderer->frame.text_prepared->runs) ||
	    text->total_pages * text->page_quads > renderer->frame.cached_quad_capacity)
		return 0;
	const RgGuiRendererPrepared* prepared = renderer->frame.text_prepared;
	if ((prepared->run_count != 0u) != (prepared->glyph_count != 0u)) return 0;
	u32 frame_slot = 0u, cache_slot = 0u;
	while (frame_slot < RG_GUI_GPU_UPLOAD_PACKET_COUNT && renderer->upload_packets[frame_slot].active)
		++frame_slot;
	if (frame_slot == RG_GUI_GPU_UPLOAD_PACKET_COUNT) return 0;
	RgGuiGpuFramePacketSlot* frame_packet = &renderer->upload_packets[frame_slot];
	if (renderer->frame.item_count > frame_packet->item_capacity)
	{
		RgGuiGpuItem* items = (RgGuiGpuItem*)SDL_realloc(frame_packet->items, sizeof(*items) * (size_t)renderer->frame.item_count);
		if (!items) return 0;
		frame_packet->items = items;
		frame_packet->item_capacity = renderer->frame.item_count;
	}
	if (renderer->frame.item_count > frame_packet->batch_capacity)
	{
		RgGuiGpuIndexedBatch* batches = (RgGuiGpuIndexedBatch*)SDL_realloc(frame_packet->batches,
		                                                                   sizeof(*batches) * (size_t)renderer->frame.item_count);
		if (!batches) return 0;
		frame_packet->batches = batches;
		frame_packet->batch_capacity = renderer->frame.item_count;
	}
	RgGuiGpuUploadPacketSlot* cache_packet = NULL;
	u32 ranges = 0u, quads = 0u;
	int full = 0;
	if (prepared->run_count)
	{
		while (cache_slot < RG_GUI_GPU_UPLOAD_PACKET_COUNT && renderer->text.upload_packets[cache_slot].active)
			++cache_slot;
		if (cache_slot == RG_GUI_GPU_UPLOAD_PACKET_COUNT) return 0;
		cache_packet = &renderer->text.upload_packets[cache_slot];
		full = !renderer->text.cache_initialized || renderer->text.cache_context != text;
		u64 revision = full ? 0u : renderer->text.cache_revision;
		if (full || revision != text->cache_revision)
			ranges = rg_gui_renderer_upload_ranges(text, revision, NULL, 0u, &quads);
		if (ranges == UINT32_MAX) return 0;
		if (ranges > cache_packet->range_capacity)
		{
			RgGuiRendererRange* spans = (RgGuiRendererRange*)SDL_realloc(cache_packet->ranges, sizeof(*spans) * (size_t)ranges);
			if (!spans) return 0;
			cache_packet->ranges = spans;
			cache_packet->range_capacity = ranges;
		}
		if (ranges && rg_gui_renderer_upload_ranges(text, revision, cache_packet->ranges, ranges, NULL) != ranges)
			return 0;
	}
	const u32 initial_offset = ring->offset;
	u64 maximum_refs = (u64)prepared->glyph_count + renderer->frame.vertex_count / 3u;
	if (maximum_refs > renderer->frame.ref_capacity) maximum_refs = renderer->frame.ref_capacity;
	u64 reserved = rg_gui_gpu_frame_bytes(prepared->run_count, renderer->frame.vertex_count, maximum_refs,
	                                      renderer->frame.item_count ? renderer->frame.clip_count : 0u);
	if (reserved > renderer->frame.frame_capacity) return 0;
	RgGpuUploadSlice packed = {0};
	u8* mapped = NULL;
	if (reserved)
	{
		if (!rg_gpu_upload_ring_alloc(ring, (u32)reserved, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN, &packed))
			goto failure;
		mapped = (u8*)rg_gpu_upload_ring_ptr(ring, &packed);
	}
	{
		RgGuiFramePacket packet;
		memset(&packet, 0, sizeof(packet));
		packet.batches = frame_packet->batches;
		packet.batch_capacity = frame_packet->batch_capacity;
		if (!rg_gui_frame_pack(&renderer->frame, mapped, (u32)reserved, &packet)) goto failure;
		upload->text.run_count = packet.run_count;
		upload->text.glyph_count = packet.glyph_count;
		upload->vertex_count = packet.vertex_count;
		upload->item_count = packet.item_count;
		upload->ref_count = packet.ref_count;
		upload->batch_count = packet.batch_count;
		upload->geometry_offset = packet.geometry_offset;
		upload->refs_offset = packet.refs_offset;
		upload->clips_offset = packet.clips_offset;
		upload->clip_count = packet.clip_count;
		upload->frame.size = packet.size;
	}
	packed.size = upload->frame.size;
	/* Return conservative triangle-reference slack before allocating cache data. */
	if (reserved) ring->offset = packed.offset + packed.size;
	if (quads)
	{
		const u32 bytes = quads * (u32)sizeof(RgGuiRendererCachedQuad);
		if (!rg_gpu_upload_ring_alloc(ring, bytes, RG_GPU_UPLOAD_RING_DEFAULT_ALIGN, &upload->text.cached_quads))
			goto failure;
		u8* destination = (u8*)rg_gpu_upload_ring_ptr(ring, &upload->text.cached_quads);
		for (u32 i = 0u; i < ranges; ++i)
		{
			const RgGuiRendererRange* span = &cache_packet->ranges[i];
			const size_t span_bytes = sizeof(RgGuiRendererCachedQuad) * span->quad_count;
			memcpy(destination, text->core.cache_quads + span->first_quad, span_bytes);
			destination += span_bytes;
		}
	}
	if (cache_packet)
	{
		cache_packet->token = ++renderer->text.next_upload_token;
		if (!cache_packet->token) cache_packet->token = ++renderer->text.next_upload_token;
		cache_packet->active = 1u;
		cache_packet->encoded = 0u;
		upload->has_text = 1u;
		upload->text.runs.offset = packed.offset;
		upload->text.runs.size = upload->geometry_offset;
		upload->text.run_count = prepared->run_count;
		upload->text.glyph_count = prepared->glyph_count;
		upload->text.cache_range_count = ranges;
		upload->text.full_cache_upload = full != 0;
		upload->text.cache_ranges = cache_packet->ranges;
		upload->text.cache_context = text;
		upload->text.cache_revision = text->cache_revision;
		upload->text.packet_slot = cache_slot;
		upload->text.packet_token = cache_packet->token;
		upload->text.cache_tracking_epoch = renderer->text.cache_tracking_epoch;
		/* Compute-era glyph clip batches are unnecessary; mixed batches own clipping. */
	}
	upload->has_geometry = renderer->frame.vertex_count != 0u;
	upload->geometry.offset = packed.offset + upload->geometry_offset;
	upload->geometry.size = renderer->frame.vertex_count * (u32)sizeof(RgGuiGpuVertex);
	if (renderer->frame.item_count) memcpy(frame_packet->items, renderer->frame.items, sizeof(RgGuiGpuItem) * renderer->frame.item_count);
	frame_packet->token = ++renderer->next_upload_token;
	if (!frame_packet->token) frame_packet->token = ++renderer->next_upload_token;
	frame_packet->active = 1u;
	frame_packet->encoded = packed.size == 0u && quads == 0u;
	upload->items = frame_packet->items;
	upload->item_count = renderer->frame.item_count;
	upload->vertex_count = renderer->frame.vertex_count;
	upload->packet_slot = frame_slot;
	upload->packet_token = frame_packet->token;
	upload->frame = packed;
	upload->batches = frame_packet->batches;
	upload->frame_buffer_index = renderer->next_frame_buffer;
	renderer->next_frame_buffer = (renderer->next_frame_buffer + 1u) % renderer->frame_buffer_count;
	return 1;
failure:
	ring->offset = initial_offset;
	memset(upload, 0, sizeof(*upload));
	return 0;
}

RGINLINE int rg_gui_gpu_upload_active(const RgGuiGpuRenderer* renderer, const RgGuiGpuUpload* upload)
{
	return renderer && upload && upload->packet_token &&
	       upload->packet_slot < RG_GUI_GPU_UPLOAD_PACKET_COUNT &&
	       renderer->upload_packets[upload->packet_slot].active &&
	       renderer->upload_packets[upload->packet_slot].token == upload->packet_token;
}

/** CPU preparation may run ahead of a staged packet. Keep this packet's GPU
 * encode, dispatch and draw together in one command buffer, then submit/commit
 * in stage order. Never interleave another packet's GPU encoding between them. */
RGINLINE void rg_gui_gpu_encode_upload(RgGuiGpuRenderer* renderer,
                                       SDL_GPUCopyPass* copy,
                                       const RgGpuUploadRing* ring,
                                       const RgGuiGpuUpload* upload)
{
	if (!copy || !ring || !rg_gui_gpu_upload_active(renderer, upload) ||
	    upload->frame_buffer_index >= renderer->frame_buffer_count ||
	    upload->frame.offset > ring->size || upload->frame.size > ring->size - upload->frame.offset ||
	    upload->frame.size > renderer->frame.frame_capacity) return;
	if (upload->has_text && !rg_gui_gpu_text_upload_active(&renderer->text, &upload->text)) return;
	if (upload->has_text)
	{
		if (upload->text.cached_quads.offset > ring->size ||
		    upload->text.cached_quads.size > ring->size - upload->text.cached_quads.offset ||
		    (upload->text.cache_range_count && !upload->text.cache_ranges)) return;
		u64 bytes = 0u;
		for (u32 i = 0u; i < upload->text.cache_range_count; i++)
		{
			const RgGuiRendererRange* range = &upload->text.cache_ranges[i];
			if (range->first_quad > renderer->text.cached_quad_capacity ||
			    range->quad_count > renderer->text.cached_quad_capacity - range->first_quad) return;
			bytes += (u64)range->quad_count * sizeof(RgGuiRendererCachedQuad);
		}
		if (bytes != upload->text.cached_quads.size) return;
	}
	u32 offset = upload->text.cached_quads.offset;
	for (u32 i = 0u; upload->has_text && i < upload->text.cache_range_count; i++)
	{
		const RgGuiRendererRange* range = &upload->text.cache_ranges[i];
		u32 bytes = range->quad_count * (u32)sizeof(RgGuiRendererCachedQuad);
		SDL_GPUTransferBufferLocation source = {ring->buffer, offset};
		SDL_GPUBufferRegion destination = {renderer->text.cached_quad_buffer,
		                                   range->first_quad * (u32)sizeof(RgGuiRendererCachedQuad), bytes};
		SDL_UploadToGPUBuffer(copy, &source, &destination, false);
		offset += bytes;
	}
	if (upload->frame.size)
	{
		SDL_GPUTransferBufferLocation source = {ring->buffer, upload->frame.offset};
		SDL_GPUBufferRegion destination = {renderer->frame_buffers[upload->frame_buffer_index],
		                                   0u, upload->frame.size};
		SDL_UploadToGPUBuffer(copy, &source, &destination, false);
	}
	if (upload->has_text) renderer->text.upload_packets[upload->text.packet_slot].encoded = 1u;
	renderer->upload_packets[upload->packet_slot].encoded = 1u;
}

/** Validate the staged packet before submission; see text_upload_ready for order
 * and swapchain rules. No staging/abort may intervene between this and submit. */
RGINLINE int rg_gui_gpu_upload_ready(const RgGuiGpuRenderer* renderer, const RgGuiGpuUpload* upload)
{
	if (!rg_gui_gpu_upload_active(renderer, upload) ||
	    !renderer->upload_packets[upload->packet_slot].encoded ||
	    upload->packet_token <= renderer->last_acknowledged_token) return 0;
	for (u32 i = 0u; i < RG_GUI_GPU_UPLOAD_PACKET_COUNT; i++)
		if (renderer->upload_packets[i].active &&
		    renderer->upload_packets[i].token < upload->packet_token) return 0;
	return !upload->has_text || rg_gui_gpu_text_upload_ready(&renderer->text, &upload->text);
}

/** Call immediately after a successful SDL command-buffer submission. */
RGINLINE void rg_gui_gpu_upload_commit(RgGuiGpuRenderer* renderer, const RgGuiGpuUpload* upload)
{
	if (!rg_gui_gpu_upload_active(renderer, upload)) return;
	if (upload->has_text)
	{
		if (rg_gui_gpu_upload_ready(renderer, upload))
			rg_gui_gpu_text_upload_commit(&renderer->text, &upload->text);
		else rg_gui_gpu_text_upload_abort(&renderer->text, &upload->text);
	}
	if (upload->packet_token > renderer->last_acknowledged_token)
		renderer->last_acknowledged_token = upload->packet_token;
	renderer->upload_packets[upload->packet_slot].active = 0u;
}

/** Call after cancellation or a failed SDL submission, including early exits. */
RGINLINE void rg_gui_gpu_upload_abort(RgGuiGpuRenderer* renderer, const RgGuiGpuUpload* upload)
{
	if (!rg_gui_gpu_upload_active(renderer, upload)) return;
	if (upload->has_text) rg_gui_gpu_text_upload_abort(&renderer->text, &upload->text);
	renderer->upload_packets[upload->packet_slot].active = 0u;
}

/** Reset only the GPU text-cache mirror; see text_invalidate_cache for packet
 * cancellation and CPU renderer/font reload requirements. Geometry-only packets
 * remain valid, and GPU resources and packet allocations remain owned. */
RGINLINE void rg_gui_gpu_invalidate_cache(RgGuiGpuRenderer* renderer)
{
	if (renderer) rg_gui_gpu_text_invalidate_cache(&renderer->text);
}

/** Upload-ring capacity for one staged packet starting at offset zero, including
 * a full cold/invalidation cache upload. Use after successful renderer creation.
 * Multiple packets or other uploads before resetting the ring need a larger
 * caller budget. This does not change GPU lifetime/cycling requirements. Returns
 * zero for invalid capacities/alignment or a size that cannot fit in a u32. */
RGINLINE u32 rg_gui_gpu_upload_ring_size_required(const RgGuiGpuRenderer* renderer)
{
	const u64 alignment = RG_GPU_UPLOAD_RING_DEFAULT_ALIGN;
	if (!renderer || !renderer->frame.frame_capacity || !renderer->text.cached_quad_capacity ||
	    !alignment || alignment > UINT32_MAX || (alignment & (alignment - 1u))) return 0u;
	const u64 frame_bytes = ((u64)renderer->frame.frame_capacity + alignment - 1u) & ~(alignment - 1u);
	const u64 bytes = frame_bytes +
	                  (u64)renderer->text.cached_quad_capacity * sizeof(RgGuiRendererCachedQuad);
	return bytes <= UINT32_MAX ? (u32)bytes : 0u;
}

/** Requested CPU storage retained for all bounded immutable packet snapshots. */
RGINLINE size_t rg_gui_gpu_upload_memory_reserved(const RgGuiGpuRenderer* renderer)
{
	if (!renderer) return 0u;
	size_t bytes = rg_gui_gpu_text_upload_memory_reserved(&renderer->text);
	for (u32 i = 0u; i < RG_GUI_GPU_UPLOAD_PACKET_COUNT; i++)
		bytes += sizeof(RgGuiGpuItem) * renderer->upload_packets[i].item_capacity +
		         sizeof(RgGuiGpuIndexedBatch) * renderer->upload_packets[i].batch_capacity;
	return bytes;
}

/** Explicit persistent GPU buffer reservations, excluding caller upload rings,
 * shared textures, transient initialization transfers, and opaque driver state. */
RGINLINE u64 rg_gui_gpu_buffer_memory_reserved(const RgGuiGpuRenderer* renderer)
{
	if (!renderer) return 0u;
	u64 bytes = renderer->text.cached_quad_buffer ? (u64)renderer->text.cached_quad_capacity * sizeof(RgGuiRendererCachedQuad) : 0u;
	if (renderer->index_buffer)
		bytes += (u64)renderer->frame.ref_capacity * 6u * rg_gui_gpu_index_element_size(renderer->frame.ref_capacity);
	for (u32 i = 0u; i < 3u; i++)
		if (renderer->frame_buffers[i]) bytes += renderer->frame.frame_capacity;
	return bytes;
}

/** Compatibility no-op: high-level indexed rendering requires no compute pass. */
RGINLINE int rg_gui_gpu_dispatch_upload(const RgGuiGpuRenderer* renderer,
                                        SDL_GPUCommandBuffer* command_buffer,
                                        const RgGuiGpuUpload* upload)
{
	if (!command_buffer || !rg_gui_gpu_upload_active(renderer, upload) ||
	    !renderer->upload_packets[upload->packet_slot].encoded) return 0;
	return !upload->has_text ||
	       (rg_gui_gpu_text_upload_active(&renderer->text, &upload->text) &&
	        renderer->text.upload_packets[upload->text.packet_slot].encoded);
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

/** Draw only this packet's immutable batches and packed GPU buffer. */
RGINLINE RgGuiGpuStats rg_gui_gpu_draw(const RgGuiGpuRenderer* renderer,
                                       SDL_GPUCommandBuffer* command_buffer,
                                       SDL_GPURenderPass* pass,
                                       const RgGuiGpuDrawDesc* desc,
                                       const RgGuiGpuUpload* upload)
{
	RgGuiGpuStats stats = {0};
	if (!command_buffer || !pass || !desc || !rg_gui_gpu_upload_active(renderer, upload) ||
	    !renderer->upload_packets[upload->packet_slot].encoded ||
	    !desc->output_width || !desc->output_height ||
	    upload->frame_buffer_index >= renderer->frame_buffer_count ||
	    (upload->batch_count && !upload->batches) || upload->geometry.size > upload->frame.size)
		return stats;

	struct RgGuiGpuIndexedUniforms
	{
		f32 output_width, output_height;
		f32 offset_x, offset_y;
		f32 atlas_inv_width, atlas_inv_height;
		u32 geometry_offset, refs_offset;
		u32 clips_offset, padding[3];
	} uniforms = {
	    (f32)desc->output_width, (f32)desc->output_height, desc->offset_x, desc->offset_y,
	    1.0f / (f32)renderer->text.atlas_width, 1.0f / (f32)renderer->text.atlas_height,
	    upload->geometry_offset, upload->refs_offset, upload->clips_offset, {0u, 0u, 0u}};
	f32 geometry_uniforms[4] = {
	    (f32)desc->output_width, (f32)desc->output_height, desc->offset_x, desc->offset_y};
	SDL_GPUBuffer* frame = renderer->frame_buffers[upload->frame_buffer_index];
	int indexed_bound = 0;
	RgGuiTexture bound_texture = 0u;
	/* Stock batches clip in the shader and share the viewport scissor; only a
	 * material batch sets its own clip or runs a callback that may change it,
	 * and the next stock batch restores it. */
	int viewport_scissor = 0;
	for (u32 i = 0u; i < upload->batch_count; i++)
	{
		const RgGuiGpuIndexedBatch* batch = &upload->batches[i];
		SDL_Rect scissor = rg_gui_gpu_scissor(batch->clip, batch->clip_enabled != 0u, desc);
		if (scissor.w <= 0 || scissor.h <= 0) continue;
		if (batch->clip_enabled || !viewport_scissor)
		{
			SDL_SetGPUScissor(pass, &scissor);
			viewport_scissor = !batch->clip_enabled;
		}
		int custom = 0;
		if (batch->material && renderer->image_bind)
		{
			RgGuiGpuImageBindInfo info = {0};
			info.command_buffer = command_buffer;
			info.pass = pass;
			info.sampler = renderer->text.sampler;
			info.texture = batch->texture;
			info.material = batch->material;
			stats.image_bind_calls++;
			RgGuiGpuImageBindResult result = renderer->image_bind(renderer->image_bind_user, &info);
			if (result != RG_GUI_GPU_IMAGE_BIND_DEFAULT)
			{
				/* CUSTOM and FAILED may leave any pass state behind, the scissor
				 * included; the next stock batch rebinds all of it. */
				indexed_bound = 0;
				bound_texture = 0u;
				viewport_scissor = 0;
			}
			if (result == RG_GUI_GPU_IMAGE_BIND_CUSTOM)
			{
				custom = 1;
				stats.custom_image_draw_calls++;
			}
			else if (result != RG_GUI_GPU_IMAGE_BIND_DEFAULT)
			{
				stats.image_bind_failures++;
				continue;
			}
		}
		if (custom)
		{
			/* Preserve the established 20-byte vertex / 16-byte uniform ABI. A
			 * material batch holds only image quads, so the persistent quad index
			 * buffer (0,1,2,0,2,3 per four vertices) expands them for the custom
			 * pipeline without a vertex-input change. */
			SDL_PushGPUVertexUniformData(command_buffer, 0u, geometry_uniforms, sizeof(geometry_uniforms));
			SDL_GPUBufferBinding vertices = {frame,
			                                 upload->geometry_offset + batch->first_vertex * (u32)sizeof(RgGuiGpuVertex)};
			SDL_BindGPUVertexBuffers(pass, 0u, &vertices, 1u);
			SDL_GPUBufferBinding indices = {renderer->index_buffer, 0u};
			SDL_BindGPUIndexBuffer(pass, &indices,
			                       rg_gui_gpu_index_element_size(renderer->frame.ref_capacity) == sizeof(u16) ? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT);
			const u32 index_count = batch->geometry_vertices / RG_GUI_GPU_QUAD_VERTICES * 6u;
			SDL_DrawGPUIndexedPrimitives(pass, index_count, 1u, 0u, 0, 0u);
			stats.vertices += batch->geometry_vertices;
			stats.indices += index_count;
		}
		else
		{
			if (!indexed_bound)
			{
				SDL_BindGPUGraphicsPipeline(pass, renderer->indexed_pipeline);
				SDL_GPUBuffer* buffers[2] = {renderer->text.cached_quad_buffer, frame};
				SDL_BindGPUVertexStorageBuffers(pass, 0u, buffers, 2u);
				SDL_GPUBufferBinding indices = {renderer->index_buffer, 0u};
				SDL_BindGPUIndexBuffer(pass, &indices,
				                       rg_gui_gpu_index_element_size(renderer->frame.ref_capacity) == sizeof(u16) ? SDL_GPU_INDEXELEMENTSIZE_16BIT : SDL_GPU_INDEXELEMENTSIZE_32BIT);
				SDL_PushGPUVertexUniformData(command_buffer, 0u, &uniforms, sizeof(uniforms));
				indexed_bound = 1;
				bound_texture = 0u;
			}
			if (bound_texture != batch->texture)
			{
				SDL_GPUTextureSamplerBinding image = {
				    (SDL_GPUTexture*)(uintptr_t)batch->texture, renderer->text.sampler};
				SDL_BindGPUFragmentSamplers(pass, 0u, &image, 1u);
				bound_texture = batch->texture;
			}
			SDL_DrawGPUIndexedPrimitives(pass, batch->ref_count * 6u, 1u, batch->first_ref * 6u, 0, 0u);
			stats.indexed_refs += batch->ref_count;
			stats.indices += batch->ref_count * 6u;
			stats.vertices += batch->ref_count * 4u;
		}
		stats.geometry_vertices += batch->geometry_vertices;
		stats.text_instances += batch->glyph_count;
		stats.items += batch->source_items;
		stats.draw_calls++;
	}
	stats.geometry_upload_bytes = upload->geometry.size;
	stats.run_upload_bytes = upload->frame.size - upload->geometry.size;
	stats.cache_upload_bytes = upload->text.cached_quads.size;
	stats.full_cache_upload = upload->text.full_cache_upload;
	stats.cache_upload_ranges = upload->text.cache_range_count;
	stats.copy_calls = (upload->frame.size != 0u) + upload->text.cache_range_count;
	return stats;
}

#endif // RG_GUI_GPU_H
