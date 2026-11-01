/* Poison header: on the include path for test_gui_no_sdl only. */
#error "SDL3 header included: rg_gui.h and rg_gui_renderer.h must stay SDL-free (SDL glue belongs in rg_gui_sdl.h / rg_gui_gpu.h)"
