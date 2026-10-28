/* Matched raw-Vulkan replay of one rg_gui showcase frame (audit change 17).
 * Reads a frame written by the full showcase's --dump-frame DIR option and records
 * it again on raw Vulkan with the shipped SPIR-V shaders: the same bytes in the
 * same two buffer copies, one render pass, the same indexed draws and scissors,
 * one submit, two frames in flight, each call timed the way prof_core.h times
 * the SDL_GPU calls. GPU time comes from a timestamp pair around the command
 * buffer. The target is an offscreen image like --offscreen; nothing is presented.
 * Build and usage: see benchmarks/PROFILING.md ("Matched raw-Vulkan replay").
 *   vk_frame <dump dir> <frames> [out.ppm|-] [shader dir]
 * Exit status: 0 when the output matches the dump's reference.bin, 1 when any
 * pixel differs, 2 when the dump has no usable reference or setup fails. */
#include <vulkan/vulkan.h>
#include <dlfcn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#include "prof_clock.h"

#define CHECK(x) ({ VkResult r__ = (x); if (r__ != VK_SUCCESS) { fprintf(stderr, "%s failed: %d (line %d)\n", #x, (int)r__, __LINE__); exit(1); } r__; })
#define FLIGHTS 2u
#define STAGING_ALIGN 256u

#define MAX_DRAWS 256
typedef struct Draw { uint32_t indices, first_index; int32_t scissor[4]; } Draw;
typedef struct Meta {
	uint32_t ref_capacity, frame_capacity, cached_quad_capacity, frame_size, geometry_offset, refs_offset, clips_offset, ref_count, cache_bytes, range_first, range_count, viewport[4], atlas_w, atlas_h, total_pages, page_quads, draw_count;
	float uniforms[12]; /* output size, draw offset, atlas inverse size, geometry/refs/clips offsets as uints, padding */
	float clear[4];
	VkFormat target_format;
	Draw draws[MAX_DRAWS];
} Meta;

static void* read_file(const char* dir, const char* name, size_t* size) { char p[1024]; snprintf(p, sizeof(p), "%s/%s", dir, name); FILE* f = fopen(p, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", p); exit(1); } fseek(f, 0, SEEK_END); long n = ftell(f); fseek(f, 0, SEEK_SET); void* d = malloc((size_t)n ? (size_t)n : 1); if (n && fread(d, 1, (size_t)n, f) != (size_t)n) { fprintf(stderr, "short read %s\n", p); exit(1); } fclose(f); *size = (size_t)n; return d; }
static void read_meta(const char* dir, Meta* m) {
	size_t n; char* t = read_file(dir, "meta.txt", &n); t = realloc(t, n + 1); t[n] = 0; char* line = strtok(t, "\n");
	while (line) {
		unsigned a, b, c, d; float u[4]; unsigned g, r, cl; int sc[4]; char fmt[32];
		if (sscanf(line, "ref_capacity %u", &a) == 1) m->ref_capacity = a;
		else if (sscanf(line, "frame_capacity %u", &a) == 1) m->frame_capacity = a;
		else if (sscanf(line, "cached_quad_capacity %u", &a) == 1) m->cached_quad_capacity = a;
		else if (sscanf(line, "frame_size %u", &a) == 1) m->frame_size = a;
		else if (sscanf(line, "geometry_offset %u", &a) == 1) m->geometry_offset = a;
		else if (sscanf(line, "refs_offset %u", &a) == 1) m->refs_offset = a;
		else if (sscanf(line, "clips_offset %u", &a) == 1) m->clips_offset = a;
		else if (sscanf(line, "ref_count %u", &a) == 1) m->ref_count = a;
		else if (sscanf(line, "cache_bytes %u", &a) == 1) m->cache_bytes = a;
		else if (sscanf(line, "range %u %u", &a, &b) == 2) { m->range_first = a; m->range_count = b; }
		else if (sscanf(line, "text_total_pages %u page_quads %u", &a, &b) == 2) { m->total_pages = a; m->page_quads = b; }
		else if (sscanf(line, "uniforms %f %f %f %f %u %u %u", &u[0], &u[1], &u[2], &u[3], &g, &r, &cl) == 7) { memcpy(m->uniforms, u, sizeof(float) * 4); uint32_t gi = g, ri = r, ci = cl; memcpy(&m->uniforms[6], &gi, 4); memcpy(&m->uniforms[7], &ri, 4); memcpy(&m->uniforms[8], &ci, 4); }
		else if (sscanf(line, "atlas %u %u", &a, &b) == 2) { m->atlas_w = a; m->atlas_h = b; }
		else if (sscanf(line, "clear %f %f %f %f", &u[0], &u[1], &u[2], &u[3]) == 4) memcpy(m->clear, u, sizeof(m->clear));
		else if (sscanf(line, "target_format %31s", fmt) == 1) m->target_format = !strcmp(fmt, "bgra8") ? VK_FORMAT_B8G8R8A8_UNORM : !strcmp(fmt, "rgba8_srgb") ? VK_FORMAT_R8G8B8A8_SRGB : !strcmp(fmt, "bgra8_srgb") ? VK_FORMAT_B8G8R8A8_SRGB : VK_FORMAT_R8G8B8A8_UNORM;
		else if (sscanf(line, "draw %u indices %u first_index %u scissor %d %d %d %d", &a, &b, &c, &sc[0], &sc[1], &sc[2], &sc[3]) == 7 && m->draw_count < MAX_DRAWS) { Draw* dr = &m->draws[m->draw_count++]; dr->indices = b; dr->first_index = c; memcpy(dr->scissor, sc, sizeof(sc)); }
		else if (sscanf(line, "viewport %u %u %u %u", &a, &b, &c, &d) == 4) { m->viewport[0] = a; m->viewport[1] = b; m->viewport[2] = c; m->viewport[3] = d; }
		line = strtok(NULL, "\n");
	}
	free(t);
	m->uniforms[4] = m->atlas_w ? 1.0f / (float)m->atlas_w : 0.0f; m->uniforms[5] = m->atlas_h ? 1.0f / (float)m->atlas_h : 0.0f;
	if (!m->target_format) m->target_format = VK_FORMAT_R8G8B8A8_UNORM;
	if (!m->draw_count || !m->frame_size || !m->viewport[2] || !m->viewport[3]) { fprintf(stderr, "meta.txt is incomplete: did the showcase reach the dumped frame?\n"); exit(1); }
}

static VkPhysicalDevice phys; static VkDevice dev; static VkQueue queue; static uint32_t qfam; static VkPhysicalDeviceMemoryProperties memprops; static float timestamp_period;

static uint32_t find_memory(uint32_t type_bits, VkMemoryPropertyFlags want) {
	for (uint32_t i = 0; i < memprops.memoryTypeCount; i++) if ((type_bits & (1u << i)) && (memprops.memoryTypes[i].propertyFlags & want) == want) return i;
	fprintf(stderr, "no memory type for %x/%x\n", type_bits, want); exit(1);
}
typedef struct Buffer { VkBuffer buffer; VkDeviceMemory memory; void* mapped; VkDeviceSize size; } Buffer;
static Buffer make_buffer(VkDeviceSize size, VkBufferUsageFlags usage, VkMemoryPropertyFlags props, int map) {
	Buffer b = {0}; b.size = size;
	VkBufferCreateInfo ci = {VK_STRUCTURE_TYPE_BUFFER_CREATE_INFO}; ci.size = size; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE;
	CHECK(vkCreateBuffer(dev, &ci, NULL, &b.buffer));
	VkMemoryRequirements req; vkGetBufferMemoryRequirements(dev, b.buffer, &req);
	VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = find_memory(req.memoryTypeBits, props);
	CHECK(vkAllocateMemory(dev, &ai, NULL, &b.memory)); CHECK(vkBindBufferMemory(dev, b.buffer, b.memory, 0));
	if (map) CHECK(vkMapMemory(dev, b.memory, 0, size, 0, &b.mapped));
	return b;
}
typedef struct Image { VkImage image; VkDeviceMemory memory; VkImageView view; } Image;
static Image make_image(uint32_t w, uint32_t h, VkFormat format, VkImageUsageFlags usage) {
	Image im = {0};
	VkImageCreateInfo ci = {VK_STRUCTURE_TYPE_IMAGE_CREATE_INFO}; ci.imageType = VK_IMAGE_TYPE_2D; ci.format = format; ci.extent = (VkExtent3D){w, h, 1}; ci.mipLevels = 1; ci.arrayLayers = 1; ci.samples = VK_SAMPLE_COUNT_1_BIT; ci.tiling = VK_IMAGE_TILING_OPTIMAL; ci.usage = usage; ci.sharingMode = VK_SHARING_MODE_EXCLUSIVE; ci.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED;
	CHECK(vkCreateImage(dev, &ci, NULL, &im.image));
	VkMemoryRequirements req; vkGetImageMemoryRequirements(dev, im.image, &req);
	VkMemoryAllocateInfo ai = {VK_STRUCTURE_TYPE_MEMORY_ALLOCATE_INFO}; ai.allocationSize = req.size; ai.memoryTypeIndex = find_memory(req.memoryTypeBits, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT);
	CHECK(vkAllocateMemory(dev, &ai, NULL, &im.memory)); CHECK(vkBindImageMemory(dev, im.image, im.memory, 0));
	VkImageViewCreateInfo vi = {VK_STRUCTURE_TYPE_IMAGE_VIEW_CREATE_INFO}; vi.image = im.image; vi.viewType = VK_IMAGE_VIEW_TYPE_2D; vi.format = ci.format; vi.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	CHECK(vkCreateImageView(dev, &vi, NULL, &im.view));
	return im;
}
static VkShaderModule load_shader(const char* path) {
	size_t n; FILE* f = fopen(path, "rb"); if (!f) { fprintf(stderr, "cannot open %s\n", path); exit(1); } fseek(f, 0, SEEK_END); n = (size_t)ftell(f); fseek(f, 0, SEEK_SET); uint32_t* code = malloc(n); if (fread(code, 1, n, f) != n) exit(1); fclose(f);
	VkShaderModuleCreateInfo ci = {VK_STRUCTURE_TYPE_SHADER_MODULE_CREATE_INFO}; ci.codeSize = n; ci.pCode = code; VkShaderModule m; CHECK(vkCreateShaderModule(dev, &ci, NULL, &m)); free(code); return m;
}
static VkCommandPool pool;
static VkCommandBuffer one_shot_begin(void) { VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = 1; VkCommandBuffer cb; CHECK(vkAllocateCommandBuffers(dev, &ai, &cb)); VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT; CHECK(vkBeginCommandBuffer(cb, &bi)); return cb; }
static void one_shot_end(VkCommandBuffer cb) { CHECK(vkEndCommandBuffer(cb)); VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &cb; CHECK(vkQueueSubmit(queue, 1, &si, VK_NULL_HANDLE)); CHECK(vkQueueWaitIdle(queue)); vkFreeCommandBuffers(dev, pool, 1, &cb); }
static void upload_once(Buffer* dst, VkDeviceSize dst_offset, const void* data, VkDeviceSize size) {
	Buffer st = make_buffer(size, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1); memcpy(st.mapped, data, size);
	VkCommandBuffer cb = one_shot_begin(); VkBufferCopy region = {0, dst_offset, size}; vkCmdCopyBuffer(cb, st.buffer, dst->buffer, 1, &region); one_shot_end(cb);
	vkDestroyBuffer(dev, st.buffer, NULL); vkFreeMemory(dev, st.memory, NULL);
}

int main(int argc, char** argv)
{
	if (argc < 3) { fprintf(stderr, "usage: vk_frame <dump dir> <frames> [out.ppm]\n"); return 2; }
	const char* dir = argv[1]; unsigned frames = (unsigned)atoi(argv[2]); const char* ppm = argc > 3 && strcmp(argv[3], "-") ? argv[3] : NULL;
	Meta m = {0}; read_meta(dir, &m);
	size_t frame_bytes, cache_bytes, cache_full_bytes, atlas_bytes;
	void* frame_data = read_file(dir, "frame.bin", &frame_bytes); void* cache_data = read_file(dir, "cache.bin", &cache_bytes); void* cache_full = read_file(dir, "cache_full.bin", &cache_full_bytes); void* atlas = read_file(dir, "atlas.bin", &atlas_bytes);
	if (frame_bytes != m.frame_size || cache_bytes != m.cache_bytes || atlas_bytes != (size_t)m.atlas_w * m.atlas_h * 4u) { fprintf(stderr, "dump sizes disagree with meta\n"); return 1; }
	const uint32_t index_size = m.ref_capacity <= 16384u ? 2u : 4u;

	/* instance, device */
	VkApplicationInfo app = {VK_STRUCTURE_TYPE_APPLICATION_INFO}; app.pApplicationName = "rg_vk_frame"; app.apiVersion = VK_API_VERSION_1_1;
	VkInstanceCreateInfo ici = {VK_STRUCTURE_TYPE_INSTANCE_CREATE_INFO}; ici.pApplicationInfo = &app;
	VkInstance inst; CHECK(vkCreateInstance(&ici, NULL, &inst));
	uint32_t count = 0; CHECK(vkEnumeratePhysicalDevices(inst, &count, NULL)); VkPhysicalDevice devs[8]; if (count > 8) count = 8; CHECK(vkEnumeratePhysicalDevices(inst, &count, devs));
	phys = devs[0]; for (uint32_t i = 0; i < count; i++) { VkPhysicalDeviceProperties p; vkGetPhysicalDeviceProperties(devs[i], &p); if (p.deviceType == VK_PHYSICAL_DEVICE_TYPE_INTEGRATED_GPU || p.deviceType == VK_PHYSICAL_DEVICE_TYPE_DISCRETE_GPU) { phys = devs[i]; break; } }
	VkPhysicalDeviceProperties props; vkGetPhysicalDeviceProperties(phys, &props); timestamp_period = props.limits.timestampPeriod;
	fprintf(stderr, "device: %s (driver %u.%u.%u, api %u.%u)\n", props.deviceName, VK_VERSION_MAJOR(props.driverVersion), VK_VERSION_MINOR(props.driverVersion), VK_VERSION_PATCH(props.driverVersion), VK_VERSION_MAJOR(props.apiVersion), VK_VERSION_MINOR(props.apiVersion));
	vkGetPhysicalDeviceMemoryProperties(phys, &memprops);
	uint32_t qcount = 0; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qcount, NULL); VkQueueFamilyProperties qprops[16]; if (qcount > 16) qcount = 16; vkGetPhysicalDeviceQueueFamilyProperties(phys, &qcount, qprops);
	qfam = UINT32_MAX; for (uint32_t i = 0; i < qcount; i++) if (qprops[i].queueFlags & VK_QUEUE_GRAPHICS_BIT) { qfam = i; break; } if (qfam == UINT32_MAX) { fprintf(stderr, "no graphics queue\n"); return 1; }
	float prio = 1.0f; VkDeviceQueueCreateInfo qci = {VK_STRUCTURE_TYPE_DEVICE_QUEUE_CREATE_INFO}; qci.queueFamilyIndex = qfam; qci.queueCount = 1; qci.pQueuePriorities = &prio;
	VkPhysicalDeviceFeatures features = {0}; features.shaderClipDistance = VK_TRUE; /* SV_ClipDistance0 in the vertex shader */
	VkDeviceCreateInfo dci = {VK_STRUCTURE_TYPE_DEVICE_CREATE_INFO}; dci.queueCreateInfoCount = 1; dci.pQueueCreateInfos = &qci; dci.pEnabledFeatures = &features;
	CHECK(vkCreateDevice(phys, &dci, NULL, &dev)); vkGetDeviceQueue(dev, qfam, 0, &queue);
	VkCommandPoolCreateInfo pci = {VK_STRUCTURE_TYPE_COMMAND_POOL_CREATE_INFO}; pci.flags = VK_COMMAND_POOL_CREATE_RESET_COMMAND_BUFFER_BIT; pci.queueFamilyIndex = qfam; CHECK(vkCreateCommandPool(dev, &pci, NULL, &pool));

	/* resources, sized like the showcase's renderer */
	Buffer cache = make_buffer(m.cached_quad_capacity * 24ull, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	Buffer frame_buf[FLIGHTS]; for (unsigned i = 0; i < FLIGHTS; i++) frame_buf[i] = make_buffer(m.frame_capacity, VK_BUFFER_USAGE_STORAGE_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	Buffer index = make_buffer((VkDeviceSize)m.ref_capacity * 6u * index_size, VK_BUFFER_USAGE_INDEX_BUFFER_BIT | VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_DEVICE_LOCAL_BIT, 0);
	{ void* idx = malloc(index.size); for (uint32_t i = 0; i < m.ref_capacity; i++) { uint32_t base = i * 4u, pat[6] = {base, base + 1, base + 2, base, base + 2, base + 3}; if (index_size == 4) memcpy((uint32_t*)idx + i * 6, pat, 24); else { uint16_t* o = (uint16_t*)idx + i * 6; for (int k = 0; k < 6; k++) o[k] = (uint16_t)pat[k]; } } upload_once(&index, 0, idx, index.size); free(idx); }
	upload_once(&cache, 0, cache_full, cache_full_bytes);
	VkDeviceSize staging_cache_offset = (m.frame_size + STAGING_ALIGN - 1) / STAGING_ALIGN * STAGING_ALIGN;
	Buffer staging[FLIGHTS]; for (unsigned i = 0; i < FLIGHTS; i++) staging[i] = make_buffer(staging_cache_offset + m.cache_bytes + STAGING_ALIGN, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1);
	Buffer uniform = make_buffer(256u * FLIGHTS, VK_BUFFER_USAGE_UNIFORM_BUFFER_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1);
	Image target = make_image(m.viewport[2], m.viewport[3], m.target_format, VK_IMAGE_USAGE_COLOR_ATTACHMENT_BIT | VK_IMAGE_USAGE_TRANSFER_SRC_BIT);
	Image atlas_img = make_image(m.atlas_w, m.atlas_h, VK_FORMAT_R8G8B8A8_UNORM, VK_IMAGE_USAGE_SAMPLED_BIT | VK_IMAGE_USAGE_TRANSFER_DST_BIT);
	{ Buffer st = make_buffer(atlas_bytes, VK_BUFFER_USAGE_TRANSFER_SRC_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1); memcpy(st.mapped, atlas, atlas_bytes);
	  VkCommandBuffer cb = one_shot_begin();
	  VkImageMemoryBarrier b = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; b.srcAccessMask = 0; b.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.oldLayout = VK_IMAGE_LAYOUT_UNDEFINED; b.newLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.srcQueueFamilyIndex = b.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; b.image = atlas_img.image; b.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
	  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
	  VkBufferImageCopy region = {0}; region.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; region.imageExtent = (VkExtent3D){m.atlas_w, m.atlas_h, 1};
	  vkCmdCopyBufferToImage(cb, st.buffer, atlas_img.image, VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL, 1, &region);
	  b.srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; b.dstAccessMask = VK_ACCESS_SHADER_READ_BIT; b.oldLayout = VK_IMAGE_LAYOUT_TRANSFER_DST_OPTIMAL; b.newLayout = VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL;
	  vkCmdPipelineBarrier(cb, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT, 0, 0, NULL, 0, NULL, 1, &b);
	  one_shot_end(cb); vkDestroyBuffer(dev, st.buffer, NULL); vkFreeMemory(dev, st.memory, NULL); }
	VkSamplerCreateInfo sci = {VK_STRUCTURE_TYPE_SAMPLER_CREATE_INFO}; sci.magFilter = sci.minFilter = VK_FILTER_LINEAR; sci.mipmapMode = VK_SAMPLER_MIPMAP_MODE_NEAREST; sci.addressModeU = sci.addressModeV = sci.addressModeW = VK_SAMPLER_ADDRESS_MODE_CLAMP_TO_EDGE; sci.maxLod = 1.0f;
	VkSampler sampler; CHECK(vkCreateSampler(dev, &sci, NULL, &sampler));

	/* render pass + framebuffer: clear, store, like the showcase's offscreen pass */
	VkAttachmentDescription att = {0}; att.format = m.target_format; att.samples = VK_SAMPLE_COUNT_1_BIT; att.loadOp = VK_ATTACHMENT_LOAD_OP_CLEAR; att.storeOp = VK_ATTACHMENT_STORE_OP_STORE; att.stencilLoadOp = VK_ATTACHMENT_LOAD_OP_DONT_CARE; att.stencilStoreOp = VK_ATTACHMENT_STORE_OP_DONT_CARE; att.initialLayout = VK_IMAGE_LAYOUT_UNDEFINED; att.finalLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL;
	VkAttachmentReference ref = {0, VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL}; VkSubpassDescription sub = {0}; sub.pipelineBindPoint = VK_PIPELINE_BIND_POINT_GRAPHICS; sub.colorAttachmentCount = 1; sub.pColorAttachments = &ref;
	/* Both flights render into the one target: order this pass's clear and writes after the previous frame's writes. */
	VkSubpassDependency dep = {0}; dep.srcSubpass = VK_SUBPASS_EXTERNAL; dep.dstSubpass = 0; dep.srcStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep.dstStageMask = VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT; dep.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; dep.dstAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT | VK_ACCESS_COLOR_ATTACHMENT_READ_BIT;
	VkRenderPassCreateInfo rpci = {VK_STRUCTURE_TYPE_RENDER_PASS_CREATE_INFO}; rpci.attachmentCount = 1; rpci.pAttachments = &att; rpci.subpassCount = 1; rpci.pSubpasses = &sub; rpci.dependencyCount = 1; rpci.pDependencies = &dep;
	VkRenderPass rp; CHECK(vkCreateRenderPass(dev, &rpci, NULL, &rp));
	VkFramebufferCreateInfo fci = {VK_STRUCTURE_TYPE_FRAMEBUFFER_CREATE_INFO}; fci.renderPass = rp; fci.attachmentCount = 1; fci.pAttachments = &target.view; fci.width = m.viewport[2]; fci.height = m.viewport[3]; fci.layers = 1;
	VkFramebuffer fb; CHECK(vkCreateFramebuffer(dev, &fci, NULL, &fb));

	/* descriptor layouts in SDL_GPU's SPIR-V convention: set 0 vertex storage buffers, set 1 vertex uniform (dynamic), set 2 fragment combined sampler */
	VkDescriptorSetLayoutBinding b0[2] = {{0, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}, {1, VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL}};
	VkDescriptorSetLayoutBinding b1 = {0, VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1, VK_SHADER_STAGE_VERTEX_BIT, NULL};
	VkDescriptorSetLayoutBinding b2 = {0, VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1, VK_SHADER_STAGE_FRAGMENT_BIT, NULL};
	VkDescriptorSetLayout layouts[3]; VkDescriptorSetLayoutCreateInfo lci = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_LAYOUT_CREATE_INFO};
	lci.bindingCount = 2; lci.pBindings = b0; CHECK(vkCreateDescriptorSetLayout(dev, &lci, NULL, &layouts[0]));
	lci.bindingCount = 1; lci.pBindings = &b1; CHECK(vkCreateDescriptorSetLayout(dev, &lci, NULL, &layouts[1]));
	lci.bindingCount = 1; lci.pBindings = &b2; CHECK(vkCreateDescriptorSetLayout(dev, &lci, NULL, &layouts[2]));
	VkPipelineLayoutCreateInfo plci = {VK_STRUCTURE_TYPE_PIPELINE_LAYOUT_CREATE_INFO}; plci.setLayoutCount = 3; plci.pSetLayouts = layouts;
	VkPipelineLayout pl; CHECK(vkCreatePipelineLayout(dev, &plci, NULL, &pl));
	VkDescriptorPoolSize psz[3] = {{VK_DESCRIPTOR_TYPE_STORAGE_BUFFER, 2 * FLIGHTS}, {VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC, 1}, {VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER, 1}};
	VkDescriptorPoolCreateInfo dpci = {VK_STRUCTURE_TYPE_DESCRIPTOR_POOL_CREATE_INFO}; dpci.maxSets = FLIGHTS + 2; dpci.poolSizeCount = 3; dpci.pPoolSizes = psz;
	VkDescriptorPool dpool; CHECK(vkCreateDescriptorPool(dev, &dpci, NULL, &dpool));
	VkDescriptorSet set0[FLIGHTS], set1, set2;
	{ VkDescriptorSetLayout l0[FLIGHTS]; for (unsigned i = 0; i < FLIGHTS; i++) l0[i] = layouts[0]; VkDescriptorSetAllocateInfo ai = {VK_STRUCTURE_TYPE_DESCRIPTOR_SET_ALLOCATE_INFO}; ai.descriptorPool = dpool; ai.descriptorSetCount = FLIGHTS; ai.pSetLayouts = l0; CHECK(vkAllocateDescriptorSets(dev, &ai, set0));
	  ai.descriptorSetCount = 1; ai.pSetLayouts = &layouts[1]; CHECK(vkAllocateDescriptorSets(dev, &ai, &set1)); ai.pSetLayouts = &layouts[2]; CHECK(vkAllocateDescriptorSets(dev, &ai, &set2)); }
	{ VkWriteDescriptorSet w[2 * FLIGHTS + 2]; VkDescriptorBufferInfo bi[2 * FLIGHTS + 1]; memset(w, 0, sizeof(w)); unsigned n = 0;
	  for (unsigned i = 0; i < FLIGHTS; i++) { bi[2 * i] = (VkDescriptorBufferInfo){cache.buffer, 0, VK_WHOLE_SIZE}; bi[2 * i + 1] = (VkDescriptorBufferInfo){frame_buf[i].buffer, 0, VK_WHOLE_SIZE};
	    w[n] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w[n].dstSet = set0[i]; w[n].dstBinding = 0; w[n].descriptorCount = 1; w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[n].pBufferInfo = &bi[2 * i]; n++;
	    w[n] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w[n].dstSet = set0[i]; w[n].dstBinding = 1; w[n].descriptorCount = 1; w[n].descriptorType = VK_DESCRIPTOR_TYPE_STORAGE_BUFFER; w[n].pBufferInfo = &bi[2 * i + 1]; n++; }
	  bi[2 * FLIGHTS] = (VkDescriptorBufferInfo){uniform.buffer, 0, 64};
	  w[n] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w[n].dstSet = set1; w[n].dstBinding = 0; w[n].descriptorCount = 1; w[n].descriptorType = VK_DESCRIPTOR_TYPE_UNIFORM_BUFFER_DYNAMIC; w[n].pBufferInfo = &bi[2 * FLIGHTS]; n++;
	  VkDescriptorImageInfo ii = {sampler, atlas_img.view, VK_IMAGE_LAYOUT_SHADER_READ_ONLY_OPTIMAL};
	  w[n] = (VkWriteDescriptorSet){VK_STRUCTURE_TYPE_WRITE_DESCRIPTOR_SET}; w[n].dstSet = set2; w[n].dstBinding = 0; w[n].descriptorCount = 1; w[n].descriptorType = VK_DESCRIPTOR_TYPE_COMBINED_IMAGE_SAMPLER; w[n].pImageInfo = &ii; n++;
	  vkUpdateDescriptorSets(dev, n, w, 0, NULL); }

	/* pipeline: the shipped SPIR-V, no vertex input, the SDL path's blend state, dynamic viewport and scissor */
	char vpath[1024], fpath[1024]; snprintf(vpath, sizeof(vpath), "%s/rg_gui_indexed.vert.spv", argc > 4 ? argv[4] : "shaders/Compiled/SPIRV"); snprintf(fpath, sizeof(fpath), "%s/rg_gui_indexed.frag.spv", argc > 4 ? argv[4] : "shaders/Compiled/SPIRV");
	VkShaderModule vs = load_shader(vpath), fs = load_shader(fpath);
	VkPipelineShaderStageCreateInfo stages[2] = {{VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}, {VK_STRUCTURE_TYPE_PIPELINE_SHADER_STAGE_CREATE_INFO}};
	stages[0].stage = VK_SHADER_STAGE_VERTEX_BIT; stages[0].module = vs; stages[0].pName = "main"; stages[1].stage = VK_SHADER_STAGE_FRAGMENT_BIT; stages[1].module = fs; stages[1].pName = "main";
	VkPipelineVertexInputStateCreateInfo vin = {VK_STRUCTURE_TYPE_PIPELINE_VERTEX_INPUT_STATE_CREATE_INFO};
	VkPipelineInputAssemblyStateCreateInfo ia = {VK_STRUCTURE_TYPE_PIPELINE_INPUT_ASSEMBLY_STATE_CREATE_INFO}; ia.topology = VK_PRIMITIVE_TOPOLOGY_TRIANGLE_LIST;
	VkPipelineViewportStateCreateInfo vp = {VK_STRUCTURE_TYPE_PIPELINE_VIEWPORT_STATE_CREATE_INFO}; vp.viewportCount = 1; vp.scissorCount = 1;
	VkPipelineRasterizationStateCreateInfo rs = {VK_STRUCTURE_TYPE_PIPELINE_RASTERIZATION_STATE_CREATE_INFO}; rs.polygonMode = VK_POLYGON_MODE_FILL; rs.cullMode = VK_CULL_MODE_NONE; rs.frontFace = VK_FRONT_FACE_COUNTER_CLOCKWISE; rs.lineWidth = 1.0f;
	VkPipelineMultisampleStateCreateInfo ms = {VK_STRUCTURE_TYPE_PIPELINE_MULTISAMPLE_STATE_CREATE_INFO}; ms.rasterizationSamples = VK_SAMPLE_COUNT_1_BIT;
	VkPipelineColorBlendAttachmentState ba = {0}; ba.blendEnable = VK_TRUE; ba.srcColorBlendFactor = VK_BLEND_FACTOR_SRC_ALPHA; ba.dstColorBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.colorBlendOp = VK_BLEND_OP_ADD; ba.srcAlphaBlendFactor = VK_BLEND_FACTOR_ONE; ba.dstAlphaBlendFactor = VK_BLEND_FACTOR_ONE_MINUS_SRC_ALPHA; ba.alphaBlendOp = VK_BLEND_OP_ADD; ba.colorWriteMask = 0xf;
	VkPipelineColorBlendStateCreateInfo cb = {VK_STRUCTURE_TYPE_PIPELINE_COLOR_BLEND_STATE_CREATE_INFO}; cb.attachmentCount = 1; cb.pAttachments = &ba;
	VkDynamicState dyn[2] = {VK_DYNAMIC_STATE_VIEWPORT, VK_DYNAMIC_STATE_SCISSOR}; VkPipelineDynamicStateCreateInfo ds = {VK_STRUCTURE_TYPE_PIPELINE_DYNAMIC_STATE_CREATE_INFO}; ds.dynamicStateCount = 2; ds.pDynamicStates = dyn;
	VkGraphicsPipelineCreateInfo gp = {VK_STRUCTURE_TYPE_GRAPHICS_PIPELINE_CREATE_INFO}; gp.stageCount = 2; gp.pStages = stages; gp.pVertexInputState = &vin; gp.pInputAssemblyState = &ia; gp.pViewportState = &vp; gp.pRasterizationState = &rs; gp.pMultisampleState = &ms; gp.pColorBlendState = &cb; gp.pDynamicState = &ds; gp.layout = pl; gp.renderPass = rp;
	VkPipeline pipe; CHECK(vkCreateGraphicsPipelines(dev, VK_NULL_HANDLE, 1, &gp, NULL, &pipe));

	/* per-flight command buffers, fences, timestamps */
	VkCommandBuffer cmd[FLIGHTS]; { VkCommandBufferAllocateInfo ai = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_ALLOCATE_INFO}; ai.commandPool = pool; ai.level = VK_COMMAND_BUFFER_LEVEL_PRIMARY; ai.commandBufferCount = FLIGHTS; CHECK(vkAllocateCommandBuffers(dev, &ai, cmd)); }
	VkFence fence[FLIGHTS]; for (unsigned i = 0; i < FLIGHTS; i++) { VkFenceCreateInfo fi = {VK_STRUCTURE_TYPE_FENCE_CREATE_INFO}; fi.flags = VK_FENCE_CREATE_SIGNALED_BIT; CHECK(vkCreateFence(dev, &fi, NULL, &fence[i])); }
	VkQueryPool qp; { VkQueryPoolCreateInfo qi = {VK_STRUCTURE_TYPE_QUERY_POOL_CREATE_INFO}; qi.queryType = VK_QUERY_TYPE_TIMESTAMP; qi.queryCount = 2 * FLIGHTS; CHECK(vkCreateQueryPool(dev, &qi, NULL, &qp)); }
	int timestamps = qprops[qfam].timestampValidBits > 0;
	static double gpu_ns[PROF_FRAMES]; unsigned gpu_samples = 0; int pending[FLIGHTS] = {0};
	for (unsigned i = 0; i < FLIGHTS; i++) memcpy((char*)uniform.mapped + 256 * i, m.uniforms, sizeof(m.uniforms));

	/* the frame loop: the same work the SDL path's acquire..submit does, call by call */
	for (unsigned f = 0; f < frames; f++) {
		unsigned slot = f % FLIGHTS;
		for (unsigned i = 0; i < 16; i++) (void)PROF_CALL(prof_probe_name, prof_noop((int)i));
		PROF_VOID("vk: wait fence (pacing)", (CHECK(vkWaitForFences(dev, 1, &fence[slot], VK_TRUE, UINT64_MAX)), CHECK(vkResetFences(dev, 1, &fence[slot]))));
		if (timestamps && pending[slot]) { uint64_t ts[2]; if (vkGetQueryPoolResults(dev, qp, 2 * slot, 2, sizeof(ts), ts, 8, VK_QUERY_RESULT_64_BIT) == VK_SUCCESS && gpu_samples < PROF_FRAMES) gpu_ns[gpu_samples++] = (double)(ts[1] - ts[0]) * timestamp_period; }
		PROF_VOID("vk: stage (memcpy to mapped staging)", (memcpy(staging[slot].mapped, frame_data, m.frame_size), memcpy((char*)staging[slot].mapped + staging_cache_offset, cache_data, m.cache_bytes)));
		VkCommandBuffer c = cmd[slot];
		VkCommandBufferBeginInfo bi = {VK_STRUCTURE_TYPE_COMMAND_BUFFER_BEGIN_INFO}; bi.flags = VK_COMMAND_BUFFER_USAGE_ONE_TIME_SUBMIT_BIT;
		PROF_VOID("vk: begin command buffer (acquire)", CHECK(vkBeginCommandBuffer(c, &bi)));
		if (timestamps) { vkCmdResetQueryPool(c, qp, 2 * slot, 2); vkCmdWriteTimestamp(c, VK_PIPELINE_STAGE_TOP_OF_PIPE_BIT, qp, 2 * slot); }
		VkBufferCopy frame_copy = {0, 0, m.frame_size}, cache_copy = {staging_cache_offset, (VkDeviceSize)m.range_first * 24u, m.cache_bytes};
		/* The text cache is shared by both flights: the previous frame's vertex reads of it must complete before this copy overwrites the page. */
		VkBufferMemoryBarrier before = {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}; before.srcAccessMask = VK_ACCESS_SHADER_READ_BIT; before.dstAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; before.srcQueueFamilyIndex = before.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; before.buffer = cache.buffer; before.size = VK_WHOLE_SIZE;
		PROF_VOID("vk: pipeline barrier (prior vertex reads to copies)", vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 1, &before, 0, NULL));
		PROF_VOID("vk: copy buffer (frame data)", vkCmdCopyBuffer(c, staging[slot].buffer, frame_buf[slot].buffer, 1, &frame_copy));
		PROF_VOID("vk: copy buffer (text cache page)", vkCmdCopyBuffer(c, staging[slot].buffer, cache.buffer, 1, &cache_copy));
		VkBufferMemoryBarrier bb[2] = {{VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}, {VK_STRUCTURE_TYPE_BUFFER_MEMORY_BARRIER}};
		for (int i = 0; i < 2; i++) { bb[i].srcAccessMask = VK_ACCESS_TRANSFER_WRITE_BIT; bb[i].dstAccessMask = VK_ACCESS_SHADER_READ_BIT; bb[i].srcQueueFamilyIndex = bb[i].dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; bb[i].size = VK_WHOLE_SIZE; }
		bb[0].buffer = frame_buf[slot].buffer; bb[1].buffer = cache.buffer;
		PROF_VOID("vk: pipeline barrier (copies to vertex reads)", vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_TRANSFER_BIT, VK_PIPELINE_STAGE_VERTEX_SHADER_BIT, 0, 0, NULL, 2, bb, 0, NULL));
		VkClearValue clear; memcpy(clear.color.float32, m.clear, sizeof(m.clear));
		VkRenderPassBeginInfo rb = {VK_STRUCTURE_TYPE_RENDER_PASS_BEGIN_INFO}; rb.renderPass = rp; rb.framebuffer = fb; rb.renderArea = (VkRect2D){{0, 0}, {m.viewport[2], m.viewport[3]}}; rb.clearValueCount = 1; rb.pClearValues = &clear;
		PROF_VOID("vk: begin render pass", vkCmdBeginRenderPass(c, &rb, VK_SUBPASS_CONTENTS_INLINE));
		VkViewport vpv = {(float)m.viewport[0], (float)(m.viewport[1] + m.viewport[3]), (float)m.viewport[2], -(float)m.viewport[3], 0.0f, 1.0f}; /* SDL_GPU flips Vulkan this way too */ VkRect2D sc = {{m.draws[0].scissor[0], m.draws[0].scissor[1]}, {(uint32_t)m.draws[0].scissor[2], (uint32_t)m.draws[0].scissor[3]}};
		PROF_VOID("vk: set viewport + scissor", (vkCmdSetViewport(c, 0, 1, &vpv), vkCmdSetScissor(c, 0, 1, &sc)));
		PROF_VOID("vk: bind pipeline", vkCmdBindPipeline(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pipe));
		VkDescriptorSet sets[3] = {set0[slot], set1, set2}; uint32_t dyn_off = 256u * slot;
		PROF_VOID("vk: bind descriptor sets (storage, uniform, sampler)", vkCmdBindDescriptorSets(c, VK_PIPELINE_BIND_POINT_GRAPHICS, pl, 0, 3, sets, 1, &dyn_off));
		PROF_VOID("vk: bind index buffer", vkCmdBindIndexBuffer(c, index.buffer, 0, index_size == 4 ? VK_INDEX_TYPE_UINT32 : VK_INDEX_TYPE_UINT16));
		for (uint32_t d = 0; d < m.draw_count; d++) {
			const Draw* dr = &m.draws[d];
			if (d > 0 && memcmp(dr->scissor, m.draws[d - 1].scissor, sizeof(dr->scissor))) { VkRect2D dsc = {{dr->scissor[0], dr->scissor[1]}, {(uint32_t)dr->scissor[2], (uint32_t)dr->scissor[3]}}; PROF_VOID("vk: set scissor (batch)", vkCmdSetScissor(c, 0, 1, &dsc)); }
			PROF_VOID("vk: draw indexed", vkCmdDrawIndexed(c, dr->indices, 1, dr->first_index, 0, 0));
		}
		PROF_VOID("vk: end render pass", vkCmdEndRenderPass(c));
		if (timestamps) vkCmdWriteTimestamp(c, VK_PIPELINE_STAGE_BOTTOM_OF_PIPE_BIT, qp, 2 * slot + 1);
		PROF_VOID("vk: end command buffer", CHECK(vkEndCommandBuffer(c)));
		VkSubmitInfo si = {VK_STRUCTURE_TYPE_SUBMIT_INFO}; si.commandBufferCount = 1; si.pCommandBuffers = &c;
		PROF_VOID("vk: queue submit (with fence)", CHECK(vkQueueSubmit(queue, 1, &si, fence[slot])));
		pending[slot] = 1;
		prof_frame_end();
	}
	CHECK(vkDeviceWaitIdle(dev));
	fprintf(stderr, "replayed %u frames: %u bytes frame data + %u bytes cache page per frame, %u draw%s, %ux%u target\n", frames, m.frame_size, m.cache_bytes, m.draw_count, m.draw_count == 1 ? "" : "s", m.viewport[2], m.viewport[3]);
	prof_report("raw Vulkan replay of the dumped showcase frame");
	if (gpu_samples > 200) { double p; double med = prof_median(gpu_ns, gpu_samples, 200, &p); fprintf(stderr, "GPU time between top-of-pipe and bottom-of-pipe timestamps: median %.1f us, p90 %.1f us (%u samples)\n", med / 1000.0, p / 1000.0, gpu_samples - 200); }
	{
		Buffer rb = make_buffer((VkDeviceSize)m.viewport[2] * m.viewport[3] * 4u, VK_BUFFER_USAGE_TRANSFER_DST_BIT, VK_MEMORY_PROPERTY_HOST_VISIBLE_BIT | VK_MEMORY_PROPERTY_HOST_COHERENT_BIT, 1);
		VkCommandBuffer c = one_shot_begin();
		VkImageMemoryBarrier ib = {VK_STRUCTURE_TYPE_IMAGE_MEMORY_BARRIER}; ib.srcAccessMask = VK_ACCESS_COLOR_ATTACHMENT_WRITE_BIT; ib.dstAccessMask = VK_ACCESS_TRANSFER_READ_BIT; ib.oldLayout = VK_IMAGE_LAYOUT_COLOR_ATTACHMENT_OPTIMAL; ib.newLayout = VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL; ib.srcQueueFamilyIndex = ib.dstQueueFamilyIndex = VK_QUEUE_FAMILY_IGNORED; ib.image = target.image; ib.subresourceRange = (VkImageSubresourceRange){VK_IMAGE_ASPECT_COLOR_BIT, 0, 1, 0, 1};
		vkCmdPipelineBarrier(c, VK_PIPELINE_STAGE_COLOR_ATTACHMENT_OUTPUT_BIT, VK_PIPELINE_STAGE_TRANSFER_BIT, 0, 0, NULL, 0, NULL, 1, &ib);
		VkBufferImageCopy region = {0}; region.imageSubresource = (VkImageSubresourceLayers){VK_IMAGE_ASPECT_COLOR_BIT, 0, 0, 1}; region.imageExtent = (VkExtent3D){m.viewport[2], m.viewport[3], 1};
		vkCmdCopyImageToBuffer(c, target.image, VK_IMAGE_LAYOUT_TRANSFER_SRC_OPTIMAL, rb.buffer, 1, &region); one_shot_end(c);
		const unsigned char* px = rb.mapped; size_t pixels = (size_t)m.viewport[2] * m.viewport[3]; int status = 2;
		int bgr = m.target_format == VK_FORMAT_B8G8R8A8_UNORM || m.target_format == VK_FORMAT_B8G8R8A8_SRGB;
		/* compare with the SDL_GPU path's readback of the same frame, if the dump has one */
		{ size_t ref_size = 0; char rp[1024]; snprintf(rp, sizeof(rp), "%s/reference.bin", dir); FILE* rf = fopen(rp, "rb");
		  if (rf) { fseek(rf, 0, SEEK_END); ref_size = (size_t)ftell(rf); fseek(rf, 0, SEEK_SET); unsigned char* ref = malloc(ref_size ? ref_size : 1); if (fread(ref, 1, ref_size, rf) != ref_size) ref_size = 0; fclose(rf);
		    if (ref_size != pixels * 4u) fprintf(stderr, "reference.bin is %zu bytes, expected %zu; not compared\n", ref_size, pixels * 4u);
		    else { size_t differing = 0; unsigned max_delta = 0; for (size_t i = 0; i < pixels * 4u; i++) { unsigned d = px[i] > ref[i] ? px[i] - ref[i] : ref[i] - px[i]; if (d > max_delta) max_delta = d; } for (size_t i = 0; i < pixels; i++) if (memcmp(px + i * 4u, ref + i * 4u, 4u)) differing++;
		      fprintf(stderr, "reference comparison: %zu of %zu pixels differ from the SDL_GPU path's readback (max channel delta %u): %s\n", differing, pixels, max_delta, differing ? "FAIL" : "match"); status = differing ? 1 : 0; }
		    free(ref); }
		  else fprintf(stderr, "no reference.bin in the dump (dump with --offscreen to get one); output not compared\n"); }
		if (ppm) { FILE* o = fopen(ppm, "wb"); if (o) { fprintf(o, "P6\n%u %u\n255\n", m.viewport[2], m.viewport[3]); for (size_t i = 0; i < pixels; i++) { unsigned char rgb[3] = {px[i * 4 + (bgr ? 2 : 0)], px[i * 4 + 1], px[i * 4 + (bgr ? 0 : 2)]}; fwrite(rgb, 1, 3, o); } fclose(o); fprintf(stderr, "wrote %s\n", ppm); } }
		return status;
	}
}
