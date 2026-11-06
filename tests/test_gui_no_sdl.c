/* Compiles rg_gui.h, rg_gui_renderer.h and rg_gui_frame.h with tests/poison
 * first on the include path and no SDL3 include directory: any SDL include
 * fails the build. Proves the core headers and the renderer-neutral frame
 * builder stay SDL-free (SDL glue lives in rg_gui_sdl.h and rg_gui_gpu.h).
 * Compile-only target: test_gui_no_sdl. */

#define RGINLINE static inline
#define RG_SPRINTF_NO_ASM 1
#include "../src/rg_gui.h"
#include "../src/rg_gui_renderer.h"
#include "../src/rg_gui_frame.h"

int main(void)
{
	RgGuiPlatformServices services;
	RgGuiInitDesc desc;
	RgGuiFrameBuilder builder;
	volatile int ok;
	memset(&services, 0, sizeof(services));
	memset(&desc, 0, sizeof(desc));
	memset(&builder, 0, sizeof(builder));
	desc.platform = &services;
	ok = desc.platform == &services && rg_key_name(RG_KEY_RETURN)[0] == 'R' &&
	     rg_gui_frame_signature(&builder) == 0u && rg_gui_gpu_frame_bytes(1u, 4u, 1u, 1u) == 120u;
	return ok ? 0 : 1;
}
