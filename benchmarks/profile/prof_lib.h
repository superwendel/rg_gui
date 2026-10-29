/* Wrap the renderer's frame functions after they are defined and before the
 * code that calls them: include this after the rg_gui headers (the demo's
 * rg_gui_demo_common.h does so when RG_GUI_DEMO_PROFILE_CALLS is defined), so
 * the demo's calls into the renderer are timed as totals, with the SDL calls
 * inside them timed by prof_core.h. The difference is the library's own CPU
 * time. */
#ifndef PROF_LIB_H
#define PROF_LIB_H
#if defined(__GNUC__)
#define rg_gui_gpu_prepare(...) PROF_CALL("rg_gui_gpu_prepare (total)", (rg_gui_gpu_prepare)(__VA_ARGS__))
#define rg_gui_gpu_stage_upload(...) PROF_CALL("rg_gui_gpu_stage_upload (total)", (rg_gui_gpu_stage_upload)(__VA_ARGS__))
#define rg_gui_gpu_dispatch_upload(...) PROF_CALL("rg_gui_gpu_dispatch_upload (total)", (rg_gui_gpu_dispatch_upload)(__VA_ARGS__))
#define rg_gui_gpu_upload_ready(...) PROF_CALL("rg_gui_gpu_upload_ready (total)", (rg_gui_gpu_upload_ready)(__VA_ARGS__))
#define rg_gui_gpu_draw(...) PROF_CALL("rg_gui_gpu_draw (total)", (rg_gui_gpu_draw)(__VA_ARGS__))
#else
/* MSVC: typed wrappers (PROF_WRAP in prof_core.h) with the renderer's signatures. */
PROF_WRAP(int, rg_gui_gpu_prepare, "rg_gui_gpu_prepare (total)", (RgGuiGpuRenderer* renderer, RgGuiRenderer* text_renderer, const RgGuiDrawList* list, u32 overlay_start), (renderer, text_renderer, list, overlay_start))
PROF_WRAP(int, rg_gui_gpu_stage_upload, "rg_gui_gpu_stage_upload (total)", (RgGuiGpuRenderer* renderer, const RgGuiRenderer* text, RgGpuUploadRing* ring, RgGuiGpuUpload* upload), (renderer, text, ring, upload))
PROF_WRAP(int, rg_gui_gpu_dispatch_upload, "rg_gui_gpu_dispatch_upload (total)", (const RgGuiGpuRenderer* renderer, SDL_GPUCommandBuffer* command_buffer, const RgGuiGpuUpload* upload), (renderer, command_buffer, upload))
PROF_WRAP(int, rg_gui_gpu_upload_ready, "rg_gui_gpu_upload_ready (total)", (const RgGuiGpuRenderer* renderer, const RgGuiGpuUpload* upload), (renderer, upload))
PROF_WRAP(RgGuiGpuStats, rg_gui_gpu_draw, "rg_gui_gpu_draw (total)", (const RgGuiGpuRenderer* renderer, SDL_GPUCommandBuffer* command_buffer, SDL_GPURenderPass* pass, const RgGuiGpuDrawDesc* desc, const RgGuiGpuUpload* upload), (renderer, command_buffer, pass, desc, upload))
#define rg_gui_gpu_prepare(...) prof_rg_gui_gpu_prepare(__VA_ARGS__)
#define rg_gui_gpu_stage_upload(...) prof_rg_gui_gpu_stage_upload(__VA_ARGS__)
#define rg_gui_gpu_dispatch_upload(...) prof_rg_gui_gpu_dispatch_upload(__VA_ARGS__)
#define rg_gui_gpu_upload_ready(...) prof_rg_gui_gpu_upload_ready(__VA_ARGS__)
#define rg_gui_gpu_draw(...) prof_rg_gui_gpu_draw(__VA_ARGS__)
#endif
/* Entry points that return nothing, both compilers */
#define rg_gui_gpu_encode_upload(...) PROF_VOID("rg_gui_gpu_encode_upload (total)", (rg_gui_gpu_encode_upload)(__VA_ARGS__))
#define rg_gui_gpu_upload_commit(...) PROF_VOID("rg_gui_gpu_upload_commit (total)", (rg_gui_gpu_upload_commit)(__VA_ARGS__))
#define rg_gui_renderer_begin_frame(...) PROF_VOID("rg_gui_renderer_begin_frame (total)", (rg_gui_renderer_begin_frame)(__VA_ARGS__))
#define rg_gpu_upload_ring_begin(...) PROF_VOID("rg_gpu_upload_ring_begin (total)", (rg_gpu_upload_ring_begin)(__VA_ARGS__))
#define rg_gpu_upload_ring_end(...) PROF_VOID("rg_gpu_upload_ring_end (total)", (rg_gpu_upload_ring_end)(__VA_ARGS__))
#define rg_gui_end_frame(...) PROF_VOID("rg_gui_end_frame (total)", (rg_gui_end_frame)(__VA_ARGS__))
#define rg_gui_begin_frame_ex(...) PROF_VOID("rg_gui_begin_frame_ex (total)", (rg_gui_begin_frame_ex)(__VA_ARGS__))
#endif
