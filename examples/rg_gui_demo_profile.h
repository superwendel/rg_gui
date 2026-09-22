// Demo-only recording. Capture into preallocated memory; write after rendering.
// Include after rg_gui_demo_common.h. All durations here are CPU wall time.
typedef struct DemoProfileOptions
{
	const char* path;
	const char* scenario;
	u32 warmup;
} DemoProfileOptions;

typedef struct DemoProfileFrame
{
	u32 frame, width, height, diagnostic_flags, actions;
	double frame_interval_ms, event_ms, ui_ms, platform_ms, lifecycle_ms, frame_work_ms;
	DemoRenderMetrics main, secondary;
} DemoProfileFrame;

typedef struct DemoProfileCapture
{
	DemoProfileOptions options;
	DemoProfileFrame* frames;
	u32 count, capacity;
} DemoProfileCapture;

RGINLINE double demo_profile_elapsed_ms(u64 start)
{
	return (double)(SDL_GetTicksNS() - start) / 1000000.0;
}

RGINLINE void demo_profile_options_init(DemoProfileOptions* options)
{
	memset(options, 0, sizeof(*options));
	options->scenario = "manual";
	options->warmup = 120u;
}

RGINLINE int demo_profile_parse_arg(DemoProfileOptions* options, int argc, char** argv, int* index)
{
	const char* arg = argv[*index];
	if (strcmp(arg, "--profile") != 0 && strcmp(arg, "--scenario") != 0 &&
	    strcmp(arg, "--profile-warmup") != 0) return 0;
	if (*index + 1 >= argc)
	{
		fprintf(stderr, "Missing value for %s\n", arg);
		return -1;
	}
	const char* value = argv[++*index];
	if (strcmp(arg, "--profile") == 0) options->path = value;
	else if (strcmp(arg, "--scenario") == 0) options->scenario = value;
	else
	{
		char* end = NULL;
		errno = 0;
		unsigned long warmup = strtoul(value, &end, 10);
		if (errno || !value[0] || *end || warmup > 100000u)
		{
			fprintf(stderr, "Invalid profile warmup: %s\n", value);
			return -1;
		}
		options->warmup = (u32)warmup;
	}
	return 1;
}

RGINLINE int demo_profile_capture_begin(DemoProfileCapture* capture,
                                        const DemoProfileOptions* options, u32 capacity)
{
	memset(capture, 0, sizeof(*capture));
	capture->options = *options;
	if (!options->path) return 1;
	if (capacity == 0u || capacity > 100000u || capacity <= options->warmup)
	{
		SDL_SetError("Profile capture needs warmup < --frames <= 100000");
		return 0;
	}
	SDL_PathInfo existing;
	if (SDL_GetPathInfo(options->path, &existing))
	{
		SDL_SetError("Profile output already exists: %s", options->path);
		return 0;
	}
	SDL_ClearError();
	capture->frames = (DemoProfileFrame*)calloc(capacity, sizeof(*capture->frames));
	if (!capture->frames)
	{
		SDL_SetError("Could not allocate demo profile capture");
		return 0;
	}
	capture->capacity = capacity;
	return 1;
}

RGINLINE int demo_profile_capture_append(DemoProfileCapture* capture,
                                         const DemoProfileFrame* frame)
{
	if (!capture->options.path) return 1;
	if (capture->count >= capture->capacity)
	{
		SDL_SetError("Demo profile capture capacity exceeded");
		return 0;
	}
	capture->frames[capture->count++] = *frame;
	return 1;
}

RGINLINE void demo_profile_write_render_header(FILE* out, const char* prefix)
{
	static const char* names[] = {
	    "prepare_ms", "stage_upload_ms", "encode_ms", "swapchain_wait_ms",
	    "draw_encode_ms", "submit_ms", "render_total_ms", "submitted", "presented",
	    "draw_commands", "draw_calls", "dispatches", "geometry_vertices", "text_instances",
	    "items", "run_upload_bytes", "cache_upload_bytes", "geometry_upload_bytes",
	    "full_cache_upload", "cache_hits", "cache_misses", "cache_evictions",
	    "cache_bypasses", "dropped_runs", "dropped_glyphs", "renderer_diagnostics"};
	for (u32 i = 0u; i < RG_ARRAY_COUNT(names); i++) fprintf(out, ",%s_%s", prefix, names[i]);
}

RGINLINE void demo_profile_write_render(FILE* out, const DemoRenderMetrics* m)
{
	fprintf(out, ",%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f,%d,%d,%u",
	        m->prepare_ms, m->stage_upload_ms, m->encode_ms, m->swapchain_wait_ms,
	        m->draw_encode_ms, m->submit_ms, m->total_ms, m->submitted, m->presented,
	        m->draw_commands);
	const RgGuiGpuStats* g = &m->draw_stats;
	fprintf(out, ",%u,%u,%u,%u,%u,%u,%u,%u,%u", g->draw_calls, g->dispatches,
	        g->geometry_vertices, g->text_instances, g->items, g->run_upload_bytes,
	        g->cache_upload_bytes, g->geometry_upload_bytes, g->full_cache_upload);
	const RgGuiRendererStats* t = &m->text_stats;
	fprintf(out, ",%u,%u,%u,%u,%u,%u,%u", t->frame_cache_hits, t->frame_cache_misses,
	        t->frame_cache_evictions, t->frame_cache_bypasses, t->frame_dropped_runs,
	        t->frame_dropped_glyphs, (u32)t->diagnostic_flags);
}

RGINLINE int demo_profile_capture_write(const DemoProfileCapture* capture)
{
	if (!capture->options.path) return 1;
	FILE* out = fopen(capture->options.path, "wb");
	if (!out)
	{
		SDL_SetError("Could not write profile output: %s", capture->options.path);
		return 0;
	}
	fputs("frame,scenario,warmup,width,height,diagnostic_flags,actions,frame_interval_ms,event_ms,ui_ms,platform_ms,lifecycle_ms,frame_work_ms", out);
	demo_profile_write_render_header(out, "main");
	demo_profile_write_render_header(out, "secondary");
	fputc('\n', out);
	for (u32 i = 0u; i < capture->count; i++)
	{
		const DemoProfileFrame* f = &capture->frames[i];
		fprintf(out, "%u,%s,%u,%u,%u,%u,%u,%.6f,%.6f,%.6f,%.6f,%.6f,%.6f",
		        f->frame, capture->options.scenario, f->frame < capture->options.warmup,
		        f->width, f->height, f->diagnostic_flags, f->actions, f->frame_interval_ms,
		        f->event_ms, f->ui_ms, f->platform_ms, f->lifecycle_ms, f->frame_work_ms);
		demo_profile_write_render(out, &f->main);
		demo_profile_write_render(out, &f->secondary);
		fputc('\n', out);
	}
	int ok = !ferror(out);
	if (fclose(out) != 0) ok = 0;
	if (!ok) SDL_SetError("Could not finish writing demo profile");
	else printf("Saved %u profile frames to %s (first %u warmup)\n",
	            capture->count, capture->options.path, capture->options.warmup);
	return ok;
}

RGINLINE void demo_profile_capture_destroy(DemoProfileCapture* capture)
{
	free(capture->frames);
	memset(capture, 0, sizeof(*capture));
}

static int demo_profile_compare_f32(const void* left, const void* right)
{
	f32 a = *(const f32*)left, b = *(const f32*)right;
	return (a > b) - (a < b);
}

RGINLINE f32 demo_profile_percentile(const f32* values, u32 count, u32 percent)
{
	f32 sorted[120];
	if (count == 0u) return 0.0f;
	if (count > RG_ARRAY_COUNT(sorted)) count = RG_ARRAY_COUNT(sorted);
	memcpy(sorted, values, count * sizeof(*values));
	qsort(sorted, count, sizeof(*sorted), demo_profile_compare_f32);
	u32 rank = (count * percent + 99u) / 100u;
	return sorted[rank ? rank - 1u : 0u];
}
