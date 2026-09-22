// CPU-only demo helper comparison. Each reported iteration is one paired
// percentile calculation over 120 samples or the Full demo's ten telemetry lines.
// Compile once against the selected core revision; no window or GPU is created.
// format_rg uses rg for five integer/string lines and retains SDL for all five
// decimal lines, matching the demo and preserving SDL's rounding behavior.
// Usage: bench_demo_helpers --kernel percentile_qsort|percentile_rg_algo|format_sdl|format_rg --iterations 100000
//        bench_demo_helpers --validate
#define RGINLINE static inline
#include "../examples/rg_gui_demo_stats.h"

#if defined(RG_GUI_DEMO_TEST_ASM_C) && defined(RG_GUI_DEMO_TEST_ASM_NATIVE)
#error Choose only one benchmark formatter backend.
#endif
#if defined(RG_GUI_DEMO_TEST_ASM_NATIVE)
#define RG_SPRINTF_HAS_ASM 1
#include "rg_sprintf_asm.h"
#define HELPER_BACKEND "asm_native"
#elif defined(RG_GUI_DEMO_TEST_ASM_C)
#define RG_SPRINTF_NO_ASM 1
#include "rg_sprintf_asm.h"
#define HELPER_BACKEND "asm_c"
#else
#define RG_SPRINTF_NO_ASM 1
#include "rg_sprintf_hybrid.h"
#define HELPER_BACKEND "portable"
#endif

#include <SDL3/SDL.h>
#include <errno.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define HELPER_CORPUS 256u
#define HELPER_SAMPLES 120u
#define HELPER_LINES 10u

typedef struct HelperTelemetry
{
	f32 frame, fps, p95, p99;
	double ui, prepare, upload, encode, wait, submit;
	u32 commands, count, draws, dispatches, glyphs, run_bytes, cache_bytes, geometry_bytes, hits, misses, evictions;
	const char* present;
} HelperTelemetry;

typedef struct HelperFormatOutput
{
	char text[HELPER_LINES][96];
	int lengths[HELPER_LINES];
} HelperFormatOutput;

static f32 helper_samples[HELPER_CORPUS][HELPER_SAMPLES];
static HelperTelemetry helper_telemetry[HELPER_CORPUS];
static f32 helper_percentiles[HELPER_CORPUS][2];
static HelperFormatOutput helper_output[HELPER_CORPUS];
static volatile u64 helper_sink;

static int helper_compare(const void* a, const void* b)
{
	f32 left = *(const f32*)a, right = *(const f32*)b;
	return (left > right) - (left < right);
}

static RG_NOINLINE void helper_qsort(const f32* values, f32* out)
{
	for (u32 i = 0u; i < 2u; i++)
	{
		f32 sorted[HELPER_SAMPLES];
		memcpy(sorted, values, sizeof(sorted));
		qsort(sorted, HELPER_SAMPLES, sizeof(*sorted), helper_compare);
		u32 rank = (HELPER_SAMPLES * (i ? 99u : 95u) + 99u) / 100u;
		out[i] = sorted[rank - 1u];
	}
}

static RG_NOINLINE void helper_rg_algo(const f32* values, f32* out)
{
	demo_profile_percentiles(values, HELPER_SAMPLES, &out[0], &out[1]);
}

#define HELPER_FORMAT(formatter) \
	out->lengths[0] = SDL_snprintf(out->text[0], 64u, "Frame %.2f ms / %.0f FPS", v->frame, v->fps); \
	out->lengths[1] = formatter(out->text[1], 64u, "Present: %s", v->present); \
	out->lengths[2] = formatter(out->text[2], 96u, "Draw commands (previous frame): %u", v->commands); \
	out->lengths[3] = SDL_snprintf(out->text[3], 96u, "Frame p95 %.2f / p99 %.2f ms (last %u)", v->p95, v->p99, v->count); \
	out->lengths[4] = SDL_snprintf(out->text[4], 96u, "CPU UI %.3f / prepare %.3f ms", v->ui, v->prepare); \
	out->lengths[5] = SDL_snprintf(out->text[5], 96u, "CPU upload %.3f / encode %.3f ms", v->upload, v->encode); \
	out->lengths[6] = SDL_snprintf(out->text[6], 96u, "Present wait %.3f / submit %.3f ms", v->wait, v->submit); \
	out->lengths[7] = formatter(out->text[7], 96u, "Draws %u / dispatches %u / glyphs %u", v->draws, v->dispatches, v->glyphs); \
	out->lengths[8] = formatter(out->text[8], 96u, "Upload run/cache/geometry: %u / %u / %u B", v->run_bytes, v->cache_bytes, v->geometry_bytes); \
	out->lengths[9] = formatter(out->text[9], 96u, "Text cache hits %u / misses %u / evictions %u", v->hits, v->misses, v->evictions)

static RG_NOINLINE void helper_sdl_format(const HelperTelemetry* v, HelperFormatOutput* out)
{
	HELPER_FORMAT(SDL_snprintf);
}

static RG_NOINLINE void helper_rg_format(const HelperTelemetry* v, HelperFormatOutput* out)
{
	HELPER_FORMAT(rg_snprintf);
}

#undef HELPER_FORMAT

static void helper_init(void)
{
	static const f32 bases[] = {0.1875f, 0.28125f, 16.6875f, 33.375f};
	static const char* modes[] = {"VSync", "Mailbox (no tearing)", "Immediate (may tear)"};
	for (u32 n = 0u; n < HELPER_CORPUS; n++)
	{
		for (u32 j = 0u; j < HELPER_SAMPLES; j++)
		{
			u32 phase = (j * 37u + n * 13u) % HELPER_SAMPLES;
			f32 value = bases[n % RG_ARRAY_COUNT(bases)] + (f32)(phase % 17u) * 0.000137f;
			if (phase == 0u) value += 2.1875f;
			if (phase == 1u) value += 0.84375f;
			helper_samples[n][j] = value;
		}
		HelperTelemetry* v = &helper_telemetry[n];
		v->frame = helper_samples[n][HELPER_SAMPLES - 1u];
		v->fps = 1000.0f / v->frame;
		v->p95 = v->frame * 1.2713f;
		v->p99 = v->frame * 2.1347f;
		v->ui = 0.011131 + (double)n * 0.0000173;
		v->prepare = 0.007173 + (double)n * 0.0000071;
		v->upload = 0.001313 + (double)n * 0.0000017;
		v->encode = 0.014717 + (double)n * 0.0000103;
		v->wait = (double)v->frame * 0.6317;
		v->submit = 0.025733 + (double)n * 0.0000391;
		v->commands = 500u + n;
		v->count = HELPER_SAMPLES;
		v->draws = 12u + n % 17u;
		v->dispatches = 1u;
		v->glyphs = 1500u + n * 7u;
		v->run_bytes = 1536u + n * 32u;
		v->cache_bytes = n % 3u ? 0u : 4096u;
		v->geometry_bytes = 32000u + n * 120u;
		v->hits = 96u + n % 23u;
		v->misses = n % 3u;
		v->evictions = n % 31u == 0u ? 1u : 0u;
		v->present = modes[n % RG_ARRAY_COUNT(modes)];
	}
}

static int helper_validate(void)
{
	for (u32 n = 0u; n < HELPER_CORPUS; n++)
	{
		f32 expected[2], actual[2];
		helper_qsort(helper_samples[n], expected);
		helper_rg_algo(helper_samples[n], actual);
		if (memcmp(expected, actual, sizeof(expected)) != 0)
		{
			fprintf(stderr, "Percentile corpus mismatch at %u\n", n);
			return 0;
		}
		HelperFormatOutput sdl = {0}, rg = {0};
		helper_sdl_format(&helper_telemetry[n], &sdl);
		helper_rg_format(&helper_telemetry[n], &rg);
		for (u32 line = 0u; line < HELPER_LINES; line++)
		{
			if (sdl.lengths[line] != rg.lengths[line] || strcmp(sdl.text[line], rg.text[line]) != 0)
			{
				fprintf(stderr, "Format corpus mismatch at %u line %u\nSDL: %s\nrg: %s\n",
				        n, line, sdl.text[line], rg.text[line]);
				return 0;
			}
		}
	}
	return 1;
}

static u64 helper_hash(u64 hash, const void* bytes, size_t count)
{
	const u8* data = (const u8*)bytes;
	for (size_t i = 0u; i < count; i++) hash = (hash ^ data[i]) * 1099511628211ull;
	return hash;
}

typedef void (*HelperPercentileFn)(const f32*, f32*);
typedef void (*HelperFormatFn)(const HelperTelemetry*, HelperFormatOutput*);

static RG_NOINLINE void helper_run_percentiles(HelperPercentileFn fn, u32 iterations)
{
	for (u32 i = 0u; i < iterations; i++)
	{
		u32 index = i % HELPER_CORPUS;
		fn(helper_samples[index], helper_percentiles[index]);
	}
}

static RG_NOINLINE void helper_run_formats(HelperFormatFn fn, u32 iterations)
{
	for (u32 i = 0u; i < iterations; i++)
	{
		u32 index = i % HELPER_CORPUS;
		fn(&helper_telemetry[index], &helper_output[index]);
	}
}

int main(int argc, char** argv)
{
	const char* kernel = NULL;
	u32 iterations = 100000u;
	int validate_only = 0;
	for (int i = 1; i < argc; i++)
	{
		if (strcmp(argv[i], "--validate") == 0) validate_only = 1;
		else if (strcmp(argv[i], "--kernel") == 0 && i + 1 < argc) kernel = argv[++i];
		else if (strcmp(argv[i], "--iterations") == 0 && i + 1 < argc)
		{
			char* end = NULL;
			errno = 0;
			unsigned long parsed = strtoul(argv[++i], &end, 10);
			if (errno || !argv[i][0] || *end || parsed == 0u || parsed > 100000000u)
			{
				fprintf(stderr, "Expected 1 <= iterations <= 100000000\n");
				return 1;
			}
			iterations = (u32)parsed;
		}
		else { fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]); return 1; }
	}
	helper_init();
	if (!helper_validate()) return 1;
	if (validate_only) { printf("{\"validated\":true,\"corpus\":%u}\n", HELPER_CORPUS); return 0; }
	HelperPercentileFn percentile_fn = NULL;
	HelperFormatFn format_fn = NULL;
	if (kernel && strcmp(kernel, "percentile_qsort") == 0) percentile_fn = helper_qsort;
	else if (kernel && strcmp(kernel, "percentile_rg_algo") == 0) percentile_fn = helper_rg_algo;
	else if (kernel && strcmp(kernel, "format_sdl") == 0) format_fn = helper_sdl_format;
	else if (kernel && strcmp(kernel, "format_rg") == 0) format_fn = helper_rg_format;
	else { fprintf(stderr, "Specify --kernel percentile_qsort|percentile_rg_algo|format_sdl|format_rg\n"); return 1; }
	if (percentile_fn) helper_run_percentiles(percentile_fn, 4096u);
	else helper_run_formats(format_fn, 4096u);
	u64 start = SDL_GetTicksNS();
	if (percentile_fn) helper_run_percentiles(percentile_fn, iterations);
	else helper_run_formats(format_fn, iterations);
	u64 elapsed = SDL_GetTicksNS() - start;
	u64 checksum = 14695981039346656037ull;
	if (percentile_fn) checksum = helper_hash(checksum, helper_percentiles, sizeof(helper_percentiles));
	else
	{
		for (u32 i = 0u; i < HELPER_CORPUS; i++)
		{
			for (u32 line = 0u; line < HELPER_LINES; line++)
			{
				checksum = helper_hash(checksum, &helper_output[i].lengths[line], sizeof(int));
				checksum = helper_hash(checksum, helper_output[i].text[line], strlen(helper_output[i].text[line]) + 1u);
			}
		}
	}
	helper_sink = checksum;
	printf("{\"kernel\":\"%s\",\"formatter_backend\":\"%s\",\"iterations\":%u,\"items_per_batch\":%u,"
	       "\"total_ns\":%llu,\"ns_per_batch\":%.6f,\"us_per_batch\":%.6f,\"checksum\":\"%016llx\"}\n",
	       kernel, HELPER_BACKEND, iterations, percentile_fn ? HELPER_SAMPLES : HELPER_LINES,
	       (unsigned long long)elapsed, (double)elapsed / iterations,
	       (double)elapsed / iterations / 1000.0, (unsigned long long)helper_sink);
	return 0;
}
