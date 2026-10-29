/* Per-call wall-time accounting for a profiling build of the full showcase.
 * Include this header before any other include of rg_gui_demo_full.c (the
 * demo does so when RG_GUI_DEMO_PROFILE_CALLS is defined; build.bat
 * demo_profile builds it that way); it replaces every SDL GPU call with a
 * timed call to the same function. Each wrapped call adds its duration to a
 * named slot, prof_frame_end moves the per-frame totals into a sample array,
 * and prof_report prints per-frame medians at exit. With GCC or Clang a
 * statement expression wraps any call (the parenthesized name defeats macro
 * recursion); MSVC has no statement expressions, so each value-returning call
 * gets a typed wrapper function there instead. See benchmarks/PROFILING.md. */
#ifndef PROF_CORE_H
#define PROF_CORE_H
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PROF_SLOTS = 64, PROF_FRAMES = 4096 };
/* Inclusive time is the wrapped call's wall time; exclusive time subtracts the
 * wrapped calls nested inside it on the same frame, so a renderer stage's
 * exclusive time is the library's own work and its inclusive time includes
 * the SDL calls it made. */
typedef struct ProfSlot { const char* name; double incl_ns, excl_ns; unsigned frame_calls; double incl[PROF_FRAMES], excl[PROF_FRAMES]; unsigned calls[PROF_FRAMES]; } ProfSlot;
static ProfSlot prof_slots[PROF_SLOTS];
static unsigned prof_slot_count = 0, prof_frame = 0;
static double prof_child_ns[64];
static unsigned prof_depth = 0;
static double prof_overhead_ns = 0;
/* Slots are keyed by the name's address, one literal per call under string
 * pooling (GCC, MSVC /O2); a compiler that gives each call site its own copy
 * of the literal still lands in one slot through the second comparison. */
static int prof_slot(const char* name) {
	for (unsigned i = 0; i < prof_slot_count; i++) if (prof_slots[i].name == name) return (int)i;
	for (unsigned i = 0; i < prof_slot_count; i++) if (strcmp(prof_slots[i].name, name) == 0) return (int)i;
	if (prof_slot_count >= PROF_SLOTS) return -1;
	prof_slots[prof_slot_count].name = name;
	return (int)prof_slot_count++;
}
static Uint64 prof_begin(void) { if (prof_depth < 64) prof_child_ns[prof_depth] = 0; prof_depth++; return SDL_GetTicksNS(); }
static void prof_end(const char* name, Uint64 start) {
	double incl = (double)(SDL_GetTicksNS() - start);
	prof_depth--;
	double child = prof_depth < 64 ? prof_child_ns[prof_depth] : 0;
	int s = prof_slot(name);
	if (s >= 0) { prof_slots[s].incl_ns += incl; prof_slots[s].excl_ns += incl - child; prof_slots[s].frame_calls++; }
	if (prof_depth > 0 && prof_depth - 1 < 64) prof_child_ns[prof_depth - 1] += incl;
}
/* Two per-frame totals kept as slots of their own, so that their medians are
 * medians of per-frame sums rather than sums of per-slot medians: the SDL
 * calls except the fence wait (recording and submission), and the library's
 * exclusive time (every slot that is not an SDL call). */
static const char* const prof_frame_sdl = "frame: SDL calls except the fence wait";
static const char* const prof_frame_lib = "frame: library exclusive";
static void prof_frame_end(void) {
	double sdl_ns = 0, lib_ns = 0;
	for (unsigned i = 0; i < prof_slot_count; i++) {
		const char* name = prof_slots[i].name;
		if (strncmp(name, "frame:", 6) == 0) continue;
		if (strncmp(name, "SDL_", 4) == 0) { if (strcmp(name, "SDL_WaitForGPUFences") != 0) sdl_ns += prof_slots[i].incl_ns; }
		else lib_ns += prof_slots[i].excl_ns;
	}
	int s = prof_slot(prof_frame_sdl);
	if (s >= 0) { prof_slots[s].incl_ns = sdl_ns; prof_slots[s].excl_ns = sdl_ns; prof_slots[s].frame_calls = 1; }
	s = prof_slot(prof_frame_lib);
	if (s >= 0) { prof_slots[s].incl_ns = lib_ns; prof_slots[s].excl_ns = lib_ns; prof_slots[s].frame_calls = 1; }
	if (prof_frame < PROF_FRAMES) for (unsigned i = 0; i < prof_slot_count; i++) { prof_slots[i].incl[prof_frame] = prof_slots[i].incl_ns; prof_slots[i].excl[prof_frame] = prof_slots[i].excl_ns; prof_slots[i].calls[prof_frame] = prof_slots[i].frame_calls; }
	for (unsigned i = 0; i < prof_slot_count; i++) { prof_slots[i].incl_ns = 0; prof_slots[i].excl_ns = 0; prof_slots[i].frame_calls = 0; }
	prof_frame++;
}
static int prof_cmp(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }
static double prof_median(const double* values, unsigned n, unsigned skip, double* p90) { static double tmp[PROF_FRAMES]; unsigned m = 0; for (unsigned f = skip; f < n; f++) tmp[m++] = values[f]; if (!m) { *p90 = 0; return 0; } qsort(tmp, m, sizeof(double), prof_cmp); *p90 = tmp[m * 9 / 10]; return tmp[m / 2]; }
static void prof_report(void) {
	unsigned n = prof_frame < PROF_FRAMES ? prof_frame : PROF_FRAMES, skip = n > 100 ? 100 : 0;
	/* What the demo printed to stdout precedes the report when both go to one file. */
	fflush(stdout);
	/* The wrapper's own cost: a timed empty call, repeated. */
	Uint64 t0 = SDL_GetTicksNS();
	for (unsigned i = 0; i < 100000; i++) { Uint64 s = prof_begin(); prof_end("overhead probe", s); }
	prof_overhead_ns = (double)(SDL_GetTicksNS() - t0) / 100000.0;
	fprintf(stderr, "PROF %u frames (first %u skipped); per-frame medians in us: inclusive, exclusive of nested wrapped calls, p90 inclusive, calls per frame; wrapper overhead %.0f ns per wrapped call\n", n, skip, prof_overhead_ns);
	double order[PROF_SLOTS]; unsigned idx[PROF_SLOTS];
	for (unsigned i = 0; i < prof_slot_count; i++) { double p90; order[i] = prof_median(prof_slots[i].incl, n, skip, &p90); idx[i] = i; }
	for (unsigned i = 0; i < prof_slot_count; i++) for (unsigned j = i + 1; j < prof_slot_count; j++) if (order[idx[j]] > order[idx[i]]) { unsigned t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
	for (unsigned k = 0; k < prof_slot_count; k++) {
		unsigned i = idx[k]; double p90, p90e, callsum = 0;
		if (prof_slots[i].name[0] == 'o' && strcmp(prof_slots[i].name, "overhead probe") == 0) continue;
		for (unsigned f = skip; f < n; f++) callsum += prof_slots[i].calls[f];
		double incl = prof_median(prof_slots[i].incl, n, skip, &p90), excl = prof_median(prof_slots[i].excl, n, skip, &p90e);
		fprintf(stderr, "PROF  %-42s incl %8.1f  excl %8.1f  p90 %8.1f  calls %.1f\n", prof_slots[i].name, incl / 1000.0, excl / 1000.0, p90 / 1000.0, n > skip ? callsum / (n - skip) : 0);
	}
}
#define PROF_VOID(name, expr) do { Uint64 prof_t0__ = prof_begin(); (expr); prof_end(name, prof_t0__); } while (0)
#if defined(__GNUC__)
#define PROF_CALL(name, expr) ({ Uint64 prof_t0__ = prof_begin(); __typeof__(expr) prof_r__ = (expr); prof_end(name, prof_t0__); prof_r__; })
/* SDL GPU calls that return a value */
#define SDL_AcquireGPUCommandBuffer(...) PROF_CALL("SDL_AcquireGPUCommandBuffer", (SDL_AcquireGPUCommandBuffer)(__VA_ARGS__))
#define SDL_BeginGPUCopyPass(...) PROF_CALL("SDL_BeginGPUCopyPass", (SDL_BeginGPUCopyPass)(__VA_ARGS__))
#define SDL_MapGPUTransferBuffer(...) PROF_CALL("SDL_MapGPUTransferBuffer", (SDL_MapGPUTransferBuffer)(__VA_ARGS__))
#define SDL_BeginGPURenderPass(...) PROF_CALL("SDL_BeginGPURenderPass", (SDL_BeginGPURenderPass)(__VA_ARGS__))
#define SDL_BeginGPUComputePass(...) PROF_CALL("SDL_BeginGPUComputePass", (SDL_BeginGPUComputePass)(__VA_ARGS__))
#define SDL_SubmitGPUCommandBuffer(...) PROF_CALL("SDL_SubmitGPUCommandBuffer", (SDL_SubmitGPUCommandBuffer)(__VA_ARGS__))
#define SDL_SubmitGPUCommandBufferAndAcquireFence(...) PROF_CALL("SDL_SubmitGPUCommandBufferAndAcquireFence", (SDL_SubmitGPUCommandBufferAndAcquireFence)(__VA_ARGS__))
#define SDL_WaitForGPUFences(...) PROF_CALL("SDL_WaitForGPUFences", (SDL_WaitForGPUFences)(__VA_ARGS__))
#define SDL_WaitAndAcquireGPUSwapchainTexture(...) PROF_CALL("SDL_WaitAndAcquireGPUSwapchainTexture", (SDL_WaitAndAcquireGPUSwapchainTexture)(__VA_ARGS__))
#else
/* PROF_WRAP(ret, name, slot, params, args) defines prof_name, a timed call to
 * name with name's signature, recorded under slot. The wrappers are defined
 * before the macros that route calls to them, so their bodies call SDL. */
#define PROF_WRAP(ret, name, slot, params, args) static inline ret prof_##name params { Uint64 prof_t0__ = prof_begin(); ret prof_r__ = name args; prof_end(slot, prof_t0__); return prof_r__; }
PROF_WRAP(SDL_GPUCommandBuffer*, SDL_AcquireGPUCommandBuffer, "SDL_AcquireGPUCommandBuffer", (SDL_GPUDevice* device), (device))
PROF_WRAP(SDL_GPUCopyPass*, SDL_BeginGPUCopyPass, "SDL_BeginGPUCopyPass", (SDL_GPUCommandBuffer* command_buffer), (command_buffer))
PROF_WRAP(void*, SDL_MapGPUTransferBuffer, "SDL_MapGPUTransferBuffer", (SDL_GPUDevice* device, SDL_GPUTransferBuffer* transfer_buffer, bool cycle), (device, transfer_buffer, cycle))
PROF_WRAP(SDL_GPURenderPass*, SDL_BeginGPURenderPass, "SDL_BeginGPURenderPass", (SDL_GPUCommandBuffer* command_buffer, const SDL_GPUColorTargetInfo* color_target_infos, Uint32 num_color_targets, const SDL_GPUDepthStencilTargetInfo* depth_stencil_target_info), (command_buffer, color_target_infos, num_color_targets, depth_stencil_target_info))
PROF_WRAP(SDL_GPUComputePass*, SDL_BeginGPUComputePass, "SDL_BeginGPUComputePass", (SDL_GPUCommandBuffer* command_buffer, const SDL_GPUStorageTextureReadWriteBinding* storage_texture_bindings, Uint32 num_storage_texture_bindings, const SDL_GPUStorageBufferReadWriteBinding* storage_buffer_bindings, Uint32 num_storage_buffer_bindings), (command_buffer, storage_texture_bindings, num_storage_texture_bindings, storage_buffer_bindings, num_storage_buffer_bindings))
PROF_WRAP(bool, SDL_SubmitGPUCommandBuffer, "SDL_SubmitGPUCommandBuffer", (SDL_GPUCommandBuffer* command_buffer), (command_buffer))
PROF_WRAP(SDL_GPUFence*, SDL_SubmitGPUCommandBufferAndAcquireFence, "SDL_SubmitGPUCommandBufferAndAcquireFence", (SDL_GPUCommandBuffer* command_buffer), (command_buffer))
PROF_WRAP(bool, SDL_WaitForGPUFences, "SDL_WaitForGPUFences", (SDL_GPUDevice* device, bool wait_all, SDL_GPUFence* const* fences, Uint32 num_fences), (device, wait_all, fences, num_fences))
PROF_WRAP(bool, SDL_WaitAndAcquireGPUSwapchainTexture, "SDL_WaitAndAcquireGPUSwapchainTexture", (SDL_GPUCommandBuffer* command_buffer, SDL_Window* window, SDL_GPUTexture** swapchain_texture, Uint32* swapchain_texture_width, Uint32* swapchain_texture_height), (command_buffer, window, swapchain_texture, swapchain_texture_width, swapchain_texture_height))
#define SDL_AcquireGPUCommandBuffer(...) prof_SDL_AcquireGPUCommandBuffer(__VA_ARGS__)
#define SDL_BeginGPUCopyPass(...) prof_SDL_BeginGPUCopyPass(__VA_ARGS__)
#define SDL_MapGPUTransferBuffer(...) prof_SDL_MapGPUTransferBuffer(__VA_ARGS__)
#define SDL_BeginGPURenderPass(...) prof_SDL_BeginGPURenderPass(__VA_ARGS__)
#define SDL_BeginGPUComputePass(...) prof_SDL_BeginGPUComputePass(__VA_ARGS__)
#define SDL_SubmitGPUCommandBuffer(...) prof_SDL_SubmitGPUCommandBuffer(__VA_ARGS__)
#define SDL_SubmitGPUCommandBufferAndAcquireFence(...) prof_SDL_SubmitGPUCommandBufferAndAcquireFence(__VA_ARGS__)
#define SDL_WaitForGPUFences(...) prof_SDL_WaitForGPUFences(__VA_ARGS__)
#define SDL_WaitAndAcquireGPUSwapchainTexture(...) prof_SDL_WaitAndAcquireGPUSwapchainTexture(__VA_ARGS__)
#endif
/* SDL GPU calls that return nothing, both compilers */
#define SDL_UploadToGPUBuffer(...) PROF_VOID("SDL_UploadToGPUBuffer", (SDL_UploadToGPUBuffer)(__VA_ARGS__))
#define SDL_UploadToGPUTexture(...) PROF_VOID("SDL_UploadToGPUTexture", (SDL_UploadToGPUTexture)(__VA_ARGS__))
#define SDL_CopyGPUBufferToBuffer(...) PROF_VOID("SDL_CopyGPUBufferToBuffer", (SDL_CopyGPUBufferToBuffer)(__VA_ARGS__))
#define SDL_EndGPUCopyPass(...) PROF_VOID("SDL_EndGPUCopyPass", (SDL_EndGPUCopyPass)(__VA_ARGS__))
#define SDL_UnmapGPUTransferBuffer(...) PROF_VOID("SDL_UnmapGPUTransferBuffer", (SDL_UnmapGPUTransferBuffer)(__VA_ARGS__))
#define SDL_EndGPURenderPass(...) PROF_VOID("SDL_EndGPURenderPass", (SDL_EndGPURenderPass)(__VA_ARGS__))
#define SDL_BindGPUGraphicsPipeline(...) PROF_VOID("SDL_BindGPUGraphicsPipeline", (SDL_BindGPUGraphicsPipeline)(__VA_ARGS__))
#define SDL_BindGPUVertexStorageBuffers(...) PROF_VOID("SDL_BindGPUVertexStorageBuffers", (SDL_BindGPUVertexStorageBuffers)(__VA_ARGS__))
#define SDL_BindGPUVertexBuffers(...) PROF_VOID("SDL_BindGPUVertexBuffers", (SDL_BindGPUVertexBuffers)(__VA_ARGS__))
#define SDL_BindGPUIndexBuffer(...) PROF_VOID("SDL_BindGPUIndexBuffer", (SDL_BindGPUIndexBuffer)(__VA_ARGS__))
#define SDL_BindGPUFragmentSamplers(...) PROF_VOID("SDL_BindGPUFragmentSamplers", (SDL_BindGPUFragmentSamplers)(__VA_ARGS__))
#define SDL_PushGPUVertexUniformData(...) PROF_VOID("SDL_PushGPUVertexUniformData", (SDL_PushGPUVertexUniformData)(__VA_ARGS__))
#define SDL_PushGPUFragmentUniformData(...) PROF_VOID("SDL_PushGPUFragmentUniformData", (SDL_PushGPUFragmentUniformData)(__VA_ARGS__))
#define SDL_SetGPUScissor(...) PROF_VOID("SDL_SetGPUScissor", (SDL_SetGPUScissor)(__VA_ARGS__))
#define SDL_SetGPUViewport(...) PROF_VOID("SDL_SetGPUViewport", (SDL_SetGPUViewport)(__VA_ARGS__))
#define SDL_DrawGPUIndexedPrimitives(...) PROF_VOID("SDL_DrawGPUIndexedPrimitives", (SDL_DrawGPUIndexedPrimitives)(__VA_ARGS__))
#define SDL_DrawGPUPrimitives(...) PROF_VOID("SDL_DrawGPUPrimitives", (SDL_DrawGPUPrimitives)(__VA_ARGS__))
#define SDL_BindGPUComputePipeline(...) PROF_VOID("SDL_BindGPUComputePipeline", (SDL_BindGPUComputePipeline)(__VA_ARGS__))
#define SDL_DispatchGPUCompute(...) PROF_VOID("SDL_DispatchGPUCompute", (SDL_DispatchGPUCompute)(__VA_ARGS__))
#define SDL_EndGPUComputePass(...) PROF_VOID("SDL_EndGPUComputePass", (SDL_EndGPUComputePass)(__VA_ARGS__))
#define SDL_ReleaseGPUFence(...) PROF_VOID("SDL_ReleaseGPUFence", (SDL_ReleaseGPUFence)(__VA_ARGS__))
#endif
