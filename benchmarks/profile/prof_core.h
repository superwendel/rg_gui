/* Per-call wall-time accounting for a profiling build of the full showcase.
 * Include this header before any other include of rg_gui_demo_full.c; it
 * replaces every SDL GPU call with a timed call to the same function (the
 * parenthesized name defeats macro recursion). Each wrapped call adds its
 * duration to a named slot, prof_frame_end moves the per-frame totals into a
 * sample array, and prof_report prints per-frame medians at exit. GCC or
 * Clang statement expressions are required. See benchmarks/PROFILING.md. */
#ifndef PROF_CORE_H
#define PROF_CORE_H
#include <SDL3/SDL.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
enum { PROF_SLOTS = 64, PROF_FRAMES = 4096 };
typedef struct ProfSlot { const char* name; double frame_ns; unsigned frame_calls; double samples[PROF_FRAMES]; unsigned calls[PROF_FRAMES]; } ProfSlot;
static ProfSlot prof_slots[PROF_SLOTS];
static unsigned prof_slot_count = 0, prof_frame = 0;
static int prof_slot(const char* name) { for (unsigned i = 0; i < prof_slot_count; i++) if (prof_slots[i].name == name) return (int)i; if (prof_slot_count >= PROF_SLOTS) return -1; prof_slots[prof_slot_count].name = name; return (int)prof_slot_count++; }
static void prof_add(const char* name, Uint64 start) { int s = prof_slot(name); if (s < 0) return; prof_slots[s].frame_ns += (double)(SDL_GetTicksNS() - start); prof_slots[s].frame_calls++; }
static void prof_frame_end(void) { if (prof_frame < PROF_FRAMES) { for (unsigned i = 0; i < prof_slot_count; i++) { prof_slots[i].samples[prof_frame] = prof_slots[i].frame_ns; prof_slots[i].calls[prof_frame] = prof_slots[i].frame_calls; } } for (unsigned i = 0; i < prof_slot_count; i++) { prof_slots[i].frame_ns = 0; prof_slots[i].frame_calls = 0; } prof_frame++; }
static int prof_cmp(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }
static void prof_report(void) {
	unsigned n = prof_frame < PROF_FRAMES ? prof_frame : PROF_FRAMES, skip = n > 100 ? 100 : 0;
	fprintf(stderr, "PROF %u frames (first %u skipped); per-frame medians in us, calls per frame\n", n, skip);
	double order[PROF_SLOTS]; unsigned idx[PROF_SLOTS];
	for (unsigned i = 0; i < prof_slot_count; i++) { static double tmp[PROF_FRAMES]; unsigned m = 0; for (unsigned f = skip; f < n; f++) tmp[m++] = prof_slots[i].samples[f]; qsort(tmp, m, sizeof(double), prof_cmp); order[i] = m ? tmp[m / 2] : 0; idx[i] = i; }
	for (unsigned i = 0; i < prof_slot_count; i++) for (unsigned j = i + 1; j < prof_slot_count; j++) if (order[idx[j]] > order[idx[i]]) { unsigned t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
	for (unsigned k = 0; k < prof_slot_count; k++) { unsigned i = idx[k]; static double tmp[PROF_FRAMES]; unsigned m = 0; double callsum = 0; for (unsigned f = skip; f < n; f++) { tmp[m++] = prof_slots[i].samples[f]; callsum += prof_slots[i].calls[f]; } qsort(tmp, m, sizeof(double), prof_cmp); fprintf(stderr, "PROF  %-40s %8.1f us  p90 %8.1f  calls %.1f\n", prof_slots[i].name, tmp[m / 2] / 1000.0, tmp[m * 9 / 10] / 1000.0, m ? callsum / m : 0); }
}
#define PROF_CALL(name, expr) ({ Uint64 prof_t0__ = SDL_GetTicksNS(); __typeof__(expr) prof_r__ = (expr); prof_add(name, prof_t0__); prof_r__; })
#define PROF_VOID(name, expr) do { Uint64 prof_t0__ = SDL_GetTicksNS(); (expr); prof_add(name, prof_t0__); } while (0)
/* SDL GPU calls */
#define SDL_AcquireGPUCommandBuffer(...) PROF_CALL("SDL_AcquireGPUCommandBuffer", (SDL_AcquireGPUCommandBuffer)(__VA_ARGS__))
#define SDL_BeginGPUCopyPass(...) PROF_CALL("SDL_BeginGPUCopyPass", (SDL_BeginGPUCopyPass)(__VA_ARGS__))
#define SDL_UploadToGPUBuffer(...) PROF_VOID("SDL_UploadToGPUBuffer", (SDL_UploadToGPUBuffer)(__VA_ARGS__))
#define SDL_UploadToGPUTexture(...) PROF_VOID("SDL_UploadToGPUTexture", (SDL_UploadToGPUTexture)(__VA_ARGS__))
#define SDL_CopyGPUBufferToBuffer(...) PROF_VOID("SDL_CopyGPUBufferToBuffer", (SDL_CopyGPUBufferToBuffer)(__VA_ARGS__))
#define SDL_EndGPUCopyPass(...) PROF_VOID("SDL_EndGPUCopyPass", (SDL_EndGPUCopyPass)(__VA_ARGS__))
#define SDL_MapGPUTransferBuffer(...) PROF_CALL("SDL_MapGPUTransferBuffer", (SDL_MapGPUTransferBuffer)(__VA_ARGS__))
#define SDL_UnmapGPUTransferBuffer(...) PROF_VOID("SDL_UnmapGPUTransferBuffer", (SDL_UnmapGPUTransferBuffer)(__VA_ARGS__))
#define SDL_BeginGPURenderPass(...) PROF_CALL("SDL_BeginGPURenderPass", (SDL_BeginGPURenderPass)(__VA_ARGS__))
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
#define SDL_BeginGPUComputePass(...) PROF_CALL("SDL_BeginGPUComputePass", (SDL_BeginGPUComputePass)(__VA_ARGS__))
#define SDL_BindGPUComputePipeline(...) PROF_VOID("SDL_BindGPUComputePipeline", (SDL_BindGPUComputePipeline)(__VA_ARGS__))
#define SDL_DispatchGPUCompute(...) PROF_VOID("SDL_DispatchGPUCompute", (SDL_DispatchGPUCompute)(__VA_ARGS__))
#define SDL_EndGPUComputePass(...) PROF_VOID("SDL_EndGPUComputePass", (SDL_EndGPUComputePass)(__VA_ARGS__))
#define SDL_SubmitGPUCommandBuffer(...) PROF_CALL("SDL_SubmitGPUCommandBuffer", (SDL_SubmitGPUCommandBuffer)(__VA_ARGS__))
#define SDL_SubmitGPUCommandBufferAndAcquireFence(...) PROF_CALL("SDL_SubmitGPUCommandBufferAndAcquireFence", (SDL_SubmitGPUCommandBufferAndAcquireFence)(__VA_ARGS__))
#define SDL_WaitForGPUFences(...) PROF_CALL("SDL_WaitForGPUFences", (SDL_WaitForGPUFences)(__VA_ARGS__))
#define SDL_ReleaseGPUFence(...) PROF_VOID("SDL_ReleaseGPUFence", (SDL_ReleaseGPUFence)(__VA_ARGS__))
#define SDL_WaitAndAcquireGPUSwapchainTexture(...) PROF_CALL("SDL_WaitAndAcquireGPUSwapchainTexture", (SDL_WaitAndAcquireGPUSwapchainTexture)(__VA_ARGS__))
#endif
