/* Shifting a draw list down by one 56-byte command: the C runtime's memmove
 * against a rep movsb string copy and a per-command copy loop. Audit change 18
 * found the backdrop lift's memmove of a 41 KB list costing 8.6 us on an
 * i7-12700KF under the Windows runtime, one byte per cycle, which is what rep
 * movsb does when source and destination overlap by less than a cache line;
 * glibc's memmove takes 0.4 us for the same shift. rg_gui no longer shifts the
 * list, so this only documents the behavior each runtime has.
 *
 *   gcc -O2 benchmarks/prototype/memmove_shift.c -o memmove_shift && ./memmove_shift
 *   cl /O2 benchmarks\prototype\memmove_shift.c && memmove_shift.exe
 */
#if !defined(_WIN32) && !defined(_POSIX_C_SOURCE)
#define _POSIX_C_SOURCE 199309L /* clock_gettime under -std=c11 */
#endif
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
#if defined(_WIN32)
#include <windows.h>
#include <intrin.h>
static uint64_t now_ns(void) { LARGE_INTEGER f, c; QueryPerformanceFrequency(&f); QueryPerformanceCounter(&c); return (uint64_t)((double)c.QuadPart * 1e9 / (double)f.QuadPart); }
static void rep_movsb(void* d, const void* s, size_t n) { __movsb((unsigned char*)d, (const unsigned char*)s, n); }
#define NOINLINE __declspec(noinline)
#else
#include <time.h>
static uint64_t now_ns(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
static void rep_movsb(void* d, const void* s, size_t n) { __asm__ volatile("rep movsb" : "+D"(d), "+S"(s), "+c"(n) : : "memory"); }
#define NOINLINE __attribute__((noinline))
#endif
typedef struct Cmd { unsigned char bytes[56]; } Cmd;
/* The compiler may turn this loop back into a memmove call; the result says whether it did. */
static NOINLINE void copy_loop(Cmd* cmds, unsigned count) { for (unsigned i = 1u; i < count; i++) cmds[i - 1u] = cmds[i]; }
int main(void)
{
	enum { COUNT = 735, ROUNDS = 2000 };
	Cmd* cmds = (Cmd*)malloc(sizeof(Cmd) * (COUNT + 8));
	if (!cmds) return 1;
	memset(cmds, 1, sizeof(Cmd) * (COUNT + 8));
	double best[3] = {1e18, 1e18, 1e18};
	for (int r = 0; r < ROUNDS; r++)
	{
		uint64_t t0 = now_ns();
		memmove(cmds, cmds + 1, sizeof(Cmd) * (COUNT - 1));
		uint64_t t1 = now_ns();
		rep_movsb(cmds, cmds + 1, sizeof(Cmd) * (COUNT - 1));
		uint64_t t2 = now_ns();
		copy_loop(cmds, COUNT);
		uint64_t t3 = now_ns();
		double a = (double)(t1 - t0), b = (double)(t2 - t1), c = (double)(t3 - t2);
		if (a < best[0]) best[0] = a;
		if (b < best[1]) best[1] = b;
		if (c < best[2]) best[2] = c;
	}
	printf("shift %d commands of %u bytes (%u KB) down by one, best of %d:\n", COUNT, (unsigned)sizeof(Cmd), (unsigned)(sizeof(Cmd) * (COUNT - 1) / 1024), ROUNDS);
	printf("  runtime memmove   %7.2f us\n  rep movsb         %7.2f us\n  per-command loop  %7.2f us\n", best[0] / 1000.0, best[1] / 1000.0, best[2] / 1000.0);
	int keep = cmds[3].bytes[0];
	free(cmds);
	return keep == 1 ? 0 : 2;
}
