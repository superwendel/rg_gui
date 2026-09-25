// CPU-only checks for bounded integer/string formats used by the three demos.
// Default matches the demos' portable formatter. The optional switches are
// only for comparing core backends; native ASM requires matching linked helpers.
// Decimal telemetry keeps SDL formatting: %.2f of 1.125 yields 1.12 with SDL
// and 1.13 with rg. This test therefore does not claim floating-point equivalence.
#define RGINLINE static inline
#if defined(RG_GUI_DEMO_TEST_ASM_C) && defined(RG_GUI_DEMO_TEST_ASM_NATIVE)
#error Choose only one test formatter backend.
#endif
#if defined(RG_GUI_DEMO_TEST_ASM_NATIVE)
#define RG_SPRINTF_HAS_ASM 1
#include "rg_sprintf_asm.h"
#define FORMAT_BACKEND "asm_native"
#elif defined(RG_GUI_DEMO_TEST_ASM_C)
#define RG_SPRINTF_NO_ASM 1
#include "rg_sprintf_asm.h"
#define FORMAT_BACKEND "asm_c"
#else
#define RG_SPRINTF_NO_ASM 1
#include "rg_sprintf_hybrid.h"
#define FORMAT_BACKEND "portable"
#endif

#include <SDL3/SDL.h>
#include <stdarg.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

static u32 format_checks;

static int format_compare(const char* name, const char* format, ...)
{
	static const size_t fixed_caps[] = {0u, 1u, 2u, 7u, 16u, 31u, 64u, 96u, 256u, 1024u};
	char full[2048];
	va_list original, args;
	va_start(original, format);
	va_copy(args, original);
	int expected_length = SDL_vsnprintf(full, sizeof(full), format, args);
	va_end(args);
	if (expected_length < 0 || (size_t)expected_length >= sizeof(full))
	{
		fprintf(stderr, "%s: invalid test oracle output\n", name);
		va_end(original);
		return 0;
	}
	for (u32 c = 0u; c < RG_ARRAY_COUNT(fixed_caps) + 3u; c++)
	{
		// Include exact fit, room for its terminator, and zero-capacity non-NULL.
		size_t capacity = c < RG_ARRAY_COUNT(fixed_caps) ? fixed_caps[c] :
		                  c == RG_ARRAY_COUNT(fixed_caps) ? (size_t)expected_length :
		                  c == RG_ARRAY_COUNT(fixed_caps) + 1u ? (size_t)expected_length + 1u : 0u;
		if (capacity > 1024u) continue;
		unsigned char rg_guarded[1056], sdl_guarded[1056];
		memset(rg_guarded, 0xa5, sizeof(rg_guarded));
		memset(sdl_guarded, 0xa5, sizeof(sdl_guarded));
		char* rg_buffer = c == 0u ? NULL : (char*)rg_guarded + 16u;
		char* sdl_buffer = c == 0u ? NULL : (char*)sdl_guarded + 16u;
		va_copy(args, original);
		int rg_length = rg_vsnprintf(rg_buffer, capacity, format, args);
		va_end(args);
		va_copy(args, original);
		int sdl_length = SDL_vsnprintf(sdl_buffer, capacity, format, args);
		va_end(args);
		size_t written = capacity ? (size_t)expected_length < capacity - 1u ?
		                 (size_t)expected_length : capacity - 1u : 0u;
		int ok = rg_length == expected_length && sdl_length == expected_length;
		if (capacity)
		{
			ok = ok && rg_buffer[written] == '\0' && sdl_buffer[written] == '\0' &&
			     memcmp(rg_buffer, full, written) == 0 && memcmp(sdl_buffer, full, written) == 0;
		}
		for (size_t i = 0u; i < sizeof(rg_guarded); i++)
		{
			if (i < 16u || i >= 16u + capacity)
				ok = ok && rg_guarded[i] == 0xa5u && sdl_guarded[i] == 0xa5u;
		}
		if (!ok)
		{
			fprintf(stderr, "%s cap=%zu: rg length=%d SDL=%d expected=%d\n",
			        name, capacity, rg_length, sdl_length, expected_length);
			if (capacity) fprintf(stderr, "rg: %.*s\nSDL: %.*s\n",
			                      (int)capacity, rg_buffer, (int)capacity, sdl_buffer);
			va_end(original);
			return 0;
		}
		format_checks++;
	}
	va_end(original);
	return 1;
}

static int format_exact_heap_strings(void)
{
	static const size_t lengths[] = {
		0u, 1u, 2u, 3u, 4u, 5u, 6u, 7u, 8u, 9u,
		15u, 16u, 17u, 31u, 32u, 33u, 63u, 64u
	};
	for (size_t offset = 0u; offset < 8u; offset++)
	{
		for (u32 i = 0u; i < RG_ARRAY_COUNT(lengths); i++)
		{
			size_t length = lengths[i];
			// Alignment varies through a prefix, never through readable padding
			// after the NUL. ASan must see any widened load beyond the terminator.
			char* allocation = (char*)malloc(offset + length + 1u);
			if (!allocation)
			{
				fprintf(stderr, "Exact heap string allocation failed\n");
				return 0;
			}
			memset(allocation, 0x5a, offset);
			char* text = allocation + offset;
			for (size_t j = 0u; j < length; j++) text[j] = (char)('a' + j % 26u);
			text[length] = '\0';
			char name[80];
			SDL_snprintf(name, sizeof(name), "Exact heap string length=%zu offset=%zu", length, offset);
			int ok = format_compare(name, "%s", text) &&
			         format_compare(name, "prefix[%s]suffix", text);
			free(allocation);
			if (!ok) return 0;
		}
	}
	return 1;
}

#define FORMAT_CHECK(name, ...) do { if (!format_compare(name, __VA_ARGS__)) return 1; } while (0)

int main(void)
{
	if (!format_exact_heap_strings()) return 1;
	static const u32 counters[] = {0u, 1u, 120u, 4096u, 1048576u, UINT32_MAX};
	static const char* modes[] = {"VSync", "Mailbox (no tearing)", "Immediate (may tear)"};
	for (u32 i = 0u; i < RG_ARRAY_COUNT(counters); i++)
	{
		u32 a = counters[i], b = counters[(i + 1u) % RG_ARRAY_COUNT(counters)];
		FORMAT_CHECK("Full commands", "Draw commands (previous frame): %u", a);
		FORMAT_CHECK("Full draws", "Draws %u / dispatches %u / glyphs %u", a, b, a);
		FORMAT_CHECK("Full uploads", "Upload run/cache/geometry: %u / %u / %u B", a, b, a);
		FORMAT_CHECK("Full text cache", "Text cache hits %u / misses %u / evictions %u", a, b, a);
	}
	for (u32 i = 0u; i < RG_ARRAY_COUNT(modes); i++)
	{
		FORMAT_CHECK("Full present", "Present: %s", modes[i]);
		FORMAT_CHECK("Full selection", "Presentation: %s", modes[i]);
		FORMAT_CHECK("Full menu", "%s > %s", "View", modes[i]);
	}
	static const int clicks[] = {0, 1, 2147483647, (-2147483647 - 1)};
	static const char* names[] = {"", "Reverse Gravity", "A name with spaces and 100% punctuation", "caf\xc3\xa9"};
	for (u32 i = 0u; i < RG_ARRAY_COUNT(clicks); i++)
		FORMAT_CHECK("Minimal greeting", "Clicks: %d / Hello, %s", clicks[i], names[i]);
	char long_message[513];
	memset(long_message, 'x', sizeof(long_message) - 1u);
	long_message[sizeof(long_message) - 1u] = '\0';
	FORMAT_CHECK("Full error", "Present mode change failed: %s", long_message);
	FORMAT_CHECK("Tearout raise", "Could not raise tear-out: %s", long_message);
	FORMAT_CHECK("Tearout create", "Could not create tear-out: %s", long_message);
	FORMAT_CHECK("Tearout move", "%s moved to a native tear-out.", "Inspector");
	printf("Demo format (%s): %u bounded output comparisons passed.\n", FORMAT_BACKEND, format_checks);
	return 0;
}

#undef FORMAT_CHECK
