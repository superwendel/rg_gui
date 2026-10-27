/* Wrap the renderer's frame functions after they are defined: include this
 * after the last include of rg_gui_demo_common.h, so the demo's calls into the
 * renderer are timed as totals, with the SDL calls inside them timed by
 * prof_core.h. The difference is the library's own CPU time. */
#define rg_gui_gpu_prepare(...) PROF_CALL("rg_gui_gpu_prepare (total)", (rg_gui_gpu_prepare)(__VA_ARGS__))
#define rg_gui_gpu_stage_upload(...) PROF_CALL("rg_gui_gpu_stage_upload (total)", (rg_gui_gpu_stage_upload)(__VA_ARGS__))
#define rg_gui_gpu_encode_upload(...) PROF_VOID("rg_gui_gpu_encode_upload (total)", (rg_gui_gpu_encode_upload)(__VA_ARGS__))
#define rg_gui_gpu_dispatch_upload(...) PROF_CALL("rg_gui_gpu_dispatch_upload (total)", (rg_gui_gpu_dispatch_upload)(__VA_ARGS__))
#define rg_gui_gpu_upload_ready(...) PROF_CALL("rg_gui_gpu_upload_ready (total)", (rg_gui_gpu_upload_ready)(__VA_ARGS__))
#define rg_gui_gpu_draw(...) PROF_CALL("rg_gui_gpu_draw (total)", (rg_gui_gpu_draw)(__VA_ARGS__))
#define rg_gui_gpu_upload_commit(...) PROF_VOID("rg_gui_gpu_upload_commit (total)", (rg_gui_gpu_upload_commit)(__VA_ARGS__))
#define rg_gui_renderer_begin_frame(...) PROF_VOID("rg_gui_renderer_begin_frame (total)", (rg_gui_renderer_begin_frame)(__VA_ARGS__))
#define rg_gpu_upload_ring_begin(...) PROF_VOID("rg_gpu_upload_ring_begin (total)", (rg_gpu_upload_ring_begin)(__VA_ARGS__))
#define rg_gpu_upload_ring_end(...) PROF_VOID("rg_gpu_upload_ring_end (total)", (rg_gpu_upload_ring_end)(__VA_ARGS__))
#define rg_gui_end_frame(...) PROF_VOID("rg_gui_end_frame (total)", (rg_gui_end_frame)(__VA_ARGS__))
#define rg_gui_begin_frame_ex(...) PROF_VOID("rg_gui_begin_frame_ex (total)", (rg_gui_begin_frame_ex)(__VA_ARGS__))
