// CPU-only frame-history statistics shared by the demos and their tests.
#ifndef RG_GUI_DEMO_STATS_H
#define RG_GUI_DEMO_STATS_H

#include "rg_defs.h"
#include "rg_algo.h"
#include <string.h>

RGINLINE int demo_profile_f32_less(const f32* left, const f32* right)
{
	return *left < *right;
}

// rg_algo also emits merge helpers with locals/parameters used only by asserts.
// Keep NDEBUG warning suppression confined to that generated declaration block.
#if defined(_MSC_VER)
#pragma warning(push)
#pragma warning(disable: 4100 4189)
#endif
#if defined(__clang__)
#pragma clang diagnostic push
#pragma clang diagnostic ignored "-Wunused-variable"
#pragma clang diagnostic ignored "-Wunused-parameter"
#elif defined(__GNUC__)
#pragma GCC diagnostic push
#pragma GCC diagnostic ignored "-Wunused-variable"
#pragma GCC diagnostic ignored "-Wunused-parameter"
#endif
RG_ALGO_DEFINE(f32, DemoProfileF32Algo, demo_profile_f32_less)
#if defined(__clang__)
#pragma clang diagnostic pop
#elif defined(__GNUC__)
#pragma GCC diagnostic pop
#endif
#if defined(_MSC_VER)
#pragma warning(pop)
#endif

// Finite frame durations; preserve the caller's history and nearest-rank values.
RGINLINE void demo_profile_percentiles(const f32* values, u32 count,
                                      f32* out_p95, f32* out_p99)
{
	f32 selected[120];
	if (count == 0u)
	{
		*out_p95 = 0.0f;
		*out_p99 = 0.0f;
		return;
	}
	if (count > RG_ARRAY_COUNT(selected)) count = RG_ARRAY_COUNT(selected);
	memcpy(selected, values, count * sizeof(*values));
	u32 index95 = (count * 95u + 99u) / 100u - 1u;
	u32 index99 = (count * 99u + 99u) / 100u - 1u;
	rg_algo_nth_element_DemoProfileF32Algo(selected, count, index95);
	*out_p95 = selected[index95];
	if (index99 != index95)
	{
		// The first selection leaves only the upper tail to inspect for p99.
		u32 tail = index95 + 1u;
		rg_algo_nth_element_DemoProfileF32Algo(
		    selected + tail, count - tail, index99 - tail);
	}
	*out_p99 = selected[index99];
}

#endif
