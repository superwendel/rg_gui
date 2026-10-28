/* Wall-clock profiler for call-level CPU attribution in standalone drivers
 * (benchmarks/prototype/vk_frame.c, the text-path drivers of audit change 16).
 * Same slot model as prof_core.h, on clock_gettime instead of SDL: a wrapped
 * call adds its duration to a named slot, prof_frame_end moves the per-frame
 * totals into a sample array, prof_report prints per-frame medians and p90.
 * Inclusive time is the call's own interval; exclusive subtracts wrapped calls
 * nested inside it. Calibration is in situ: wrap prof_noop with PROF_CALL a few
 * times per frame under prof_probe_name, and the report prints what the wrapper
 * costs inside a measured interval (one clock read, subtract it from a leaf's
 * inclusive time) and in full (what a parent's exclusive time retains per
 * wrapped child). GCC or Clang statement expressions are required. */
#ifndef PROF_CLOCK_H
#define PROF_CLOCK_H
#include <time.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <stdint.h>
static inline uint64_t prof_now(void) { struct timespec t; clock_gettime(CLOCK_MONOTONIC, &t); return (uint64_t)t.tv_sec * 1000000000ull + (uint64_t)t.tv_nsec; }
enum { PROF_SLOTS = 64, PROF_FRAMES = 8192 };
typedef struct ProfSlot { const char* name; double incl_ns, excl_ns; unsigned frame_calls; double incl[PROF_FRAMES], excl[PROF_FRAMES]; unsigned calls[PROF_FRAMES]; } ProfSlot;
static ProfSlot prof_slots[PROF_SLOTS];
static unsigned prof_slot_count = 0, prof_frame = 0, prof_depth = 0;
static double prof_child_ns[64];
static int prof_slot(const char* name) { for (unsigned i = 0; i < prof_slot_count; i++) if (prof_slots[i].name == name) return (int)i; if (prof_slot_count >= PROF_SLOTS) return -1; prof_slots[prof_slot_count].name = name; return (int)prof_slot_count++; }
static uint64_t prof_begin(void) { if (prof_depth < 64) prof_child_ns[prof_depth] = 0; prof_depth++; return prof_now(); }
static void prof_end(const char* name, uint64_t start) { double incl = (double)(prof_now() - start); prof_depth--; double child = prof_depth < 64 ? prof_child_ns[prof_depth] : 0; int s = prof_slot(name); if (s >= 0) { prof_slots[s].incl_ns += incl; prof_slots[s].excl_ns += incl - child; prof_slots[s].frame_calls++; } if (prof_depth > 0 && prof_depth - 1 < 64) prof_child_ns[prof_depth - 1] += incl; }
static void prof_frame_end(void) { if (prof_frame < PROF_FRAMES) for (unsigned i = 0; i < prof_slot_count; i++) { prof_slots[i].incl[prof_frame] = prof_slots[i].incl_ns; prof_slots[i].excl[prof_frame] = prof_slots[i].excl_ns; prof_slots[i].calls[prof_frame] = prof_slots[i].frame_calls; } for (unsigned i = 0; i < prof_slot_count; i++) { prof_slots[i].incl_ns = 0; prof_slots[i].excl_ns = 0; prof_slots[i].frame_calls = 0; } prof_frame++; }
static int prof_cmp(const void* a, const void* b) { double x = *(const double*)a, y = *(const double*)b; return (x > y) - (x < y); }
static double prof_median(const double* v, unsigned n, unsigned skip, double* p90) { static double tmp[PROF_FRAMES]; unsigned m = 0; for (unsigned f = skip; f < n; f++) tmp[m++] = v[f]; if (!m) { *p90 = 0; return 0; } qsort(tmp, m, sizeof(double), prof_cmp); *p90 = tmp[m * 9 / 10]; return tmp[m / 2]; }
/* In-situ calibration: the driver wraps this no-op the same way it wraps real calls, once per label per frame.
 * Its inclusive time is the part of the wrapper that lands INSIDE a measured interval (one clock read).
 * The full wrapper cost (both clock reads plus slot bookkeeping) is measured separately; the difference is what a
 * parent's exclusive time retains per wrapped child call, since the child's bookkeeping sits inside the parent's interval. */
__attribute__((noinline)) static int prof_noop(int x) { __asm__ volatile("" : "+r"(x)); return x; }
static const char* const prof_probe_name = "probe: empty wrapped call";
static void prof_report(const char* title) {
	unsigned n = prof_frame < PROF_FRAMES ? prof_frame : PROF_FRAMES, skip = n > 200 ? 200 : 0;
	uint64_t t0 = prof_now(); for (unsigned i = 0; i < 100000; i++) { uint64_t s = prof_begin(); prof_end("overhead probe", s); } double full = (double)(prof_now() - t0) / 100000.0;
	double interval = 0, probe_calls = 0; int ps = -1;
	for (unsigned i = 0; i < prof_slot_count; i++) if (prof_slots[i].name == prof_probe_name) ps = (int)i;
	if (ps >= 0) { double p; for (unsigned f = skip; f < n; f++) probe_calls += prof_slots[ps].calls[f]; probe_calls = n > skip ? probe_calls / (n - skip) : 0; interval = probe_calls > 0 ? prof_median(prof_slots[ps].incl, n, skip, &p) / probe_calls : 0; }
	fprintf(stderr, "PROF %s: %u frames (first %u skipped); per-frame medians in us: inclusive, exclusive, p90 inclusive, calls per frame\n", title, n, skip);
	fprintf(stderr, "PROF  calibration: full wrapper %.1f ns per call; inside a measured interval (no-op median, in situ) %.1f ns per call; retained in parent exclusive per child call %.1f ns\n", full, interval, full - interval);
	unsigned idx[PROF_SLOTS]; double order[PROF_SLOTS];
	for (unsigned i = 0; i < prof_slot_count; i++) { double p; order[i] = prof_median(prof_slots[i].incl, n, skip, &p); idx[i] = i; }
	for (unsigned i = 0; i < prof_slot_count; i++) for (unsigned j = i + 1; j < prof_slot_count; j++) if (order[idx[j]] > order[idx[i]]) { unsigned t = idx[i]; idx[i] = idx[j]; idx[j] = t; }
	for (unsigned k = 0; k < prof_slot_count; k++) { unsigned i = idx[k]; if (!strcmp(prof_slots[i].name, "overhead probe")) continue; double p, pe, calls = 0; for (unsigned f = skip; f < n; f++) calls += prof_slots[i].calls[f]; double incl = prof_median(prof_slots[i].incl, n, skip, &p), excl = prof_median(prof_slots[i].excl, n, skip, &pe); fprintf(stderr, "PROF  %-38s incl %8.2f  excl %8.2f  p90 %8.2f  calls %.1f\n", prof_slots[i].name, incl / 1000.0, excl / 1000.0, p / 1000.0, n > skip ? calls / (n - skip) : 0); }
}
#define PROF_CALL(name, expr) ({ uint64_t prof_t0__ = prof_begin(); __typeof__(expr) prof_r__ = (expr); prof_end(name, prof_t0__); prof_r__; })
#define PROF_VOID(name, expr) do { uint64_t prof_t0__ = prof_begin(); (expr); prof_end(name, prof_t0__); } while (0)
#endif
