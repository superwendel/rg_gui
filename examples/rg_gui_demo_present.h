// Shared presentation options for the demos. Include after rg_gui_demo_common.h.
#ifndef RG_GUI_DEMO_PRESENT_H
#define RG_GUI_DEMO_PRESENT_H

RGINLINE const char* demo_present_name(SDL_GPUPresentMode mode)
{
	switch (mode)
	{
	case SDL_GPU_PRESENTMODE_VSYNC: return "vsync";
	case SDL_GPU_PRESENTMODE_IMMEDIATE: return "immediate";
	case SDL_GPU_PRESENTMODE_MAILBOX: return "mailbox";
	default: return "unknown";
	}
}

RGINLINE int demo_present_parse_arg(int argc, char** argv, int* index,
                                    SDL_GPUPresentMode* mode)
{
	const char* arg = argv[*index];
	if (strcmp(arg, "--no-vsync") == 0)
	{
		*mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
		return 1;
	}
	if (strcmp(arg, "--present") != 0) return 0;
	if (*index + 1 >= argc)
	{
		SDL_SetError("Missing value for --present (vsync, mailbox, or immediate)");
		return -1;
	}
	const char* value = argv[++*index];
	if (strcmp(value, "vsync") == 0) *mode = SDL_GPU_PRESENTMODE_VSYNC;
	else if (strcmp(value, "mailbox") == 0) *mode = SDL_GPU_PRESENTMODE_MAILBOX;
	else if (strcmp(value, "immediate") == 0) *mode = SDL_GPU_PRESENTMODE_IMMEDIATE;
	else
	{
		SDL_SetError("Invalid presentation mode: %s (expected vsync, mailbox, or immediate)", value);
		return -1;
	}
	return 1;
}

RGINLINE int demo_present_apply(SDL_GPUDevice* device, SDL_Window* window,
                                 SDL_GPUPresentMode mode)
{
	if (!SDL_WindowSupportsGPUPresentMode(device, window, mode))
	{
		SDL_SetError("Presentation mode %s is not supported for this window", demo_present_name(mode));
		return 0;
	}
	return SDL_SetGPUSwapchainParameters(device, window, SDL_GPU_SWAPCHAINCOMPOSITION_SDR, mode);
}

#endif
