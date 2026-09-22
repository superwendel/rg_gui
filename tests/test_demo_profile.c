// CPU-only regression coverage for the demo's paired frame percentiles.
#define RGINLINE static inline
#include "../examples/rg_gui_demo_stats.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>

static int profile_oracle_compare(const void* left, const void* right)
{
	f32 a = *(const f32*)left;
	f32 b = *(const f32*)right;
	return (a > b) - (a < b);
}

static int profile_check_case(const f32 values[128], u32 count, u32 pattern)
{
	f32 before[128];
	f32 sorted[120];
	f32 p95 = -1.0f;
	f32 p99 = -1.0f;
	memcpy(before, values, sizeof(before));
	u32 used = count > 120u ? 120u : count;
	f32 expected95 = 0.0f;
	f32 expected99 = 0.0f;
	if (used)
	{
		memcpy(sorted, values, used * sizeof(*values));
		qsort(sorted, used, sizeof(*sorted), profile_oracle_compare);
		// Independent nearest-rank oracle using ceiling in floating point.
		u32 rank95 = (u32)ceil((double)used * 0.95);
		u32 rank99 = (u32)ceil((double)used * 0.99);
		expected95 = sorted[rank95 - 1u];
		expected99 = sorted[rank99 - 1u];
	}
	demo_profile_percentiles(count ? values : NULL, count, &p95, &p99);
	if (p95 != expected95 || p99 != expected99 ||
	    memcmp(before, values, sizeof(before)) != 0)
	{
		fprintf(stderr, "profile percentiles pattern=%u count=%u: "
		                "p95 %.9g expected %.9g; p99 %.9g expected %.9g\n",
		        pattern, count, (double)p95, (double)expected95,
		        (double)p99, (double)expected99);
		return 0;
	}
	return 1;
}

static u32 profile_random(u32* state)
{
	u32 value = *state;
	value ^= value << 13u;
	value ^= value >> 17u;
	value ^= value << 5u;
	*state = value;
	return value;
}

int main(void)
{
	u32 cases = 0u;
	for (u32 pattern = 0u; pattern < 40u; pattern++)
	{
		f32 values[128];
		u32 random = 0x6d2b79f5u ^ pattern;
		for (u32 i = 0u; i < RG_ARRAY_COUNT(values); i++)
		{
			switch (pattern)
			{
				case 0u: values[i] = (f32)i * 0.125f; break;
				case 1u: values[i] = (f32)(127u - i) * 0.125f; break;
				case 2u: values[i] = (f32)(i % 5u); break;
				case 3u: values[i] = 0.0f; break;
				case 4u: values[i] = i % 2u ? -0.0f : 0.0f; break;
				case 5u: values[i] = i % 17u ? 0.25f : FLT_MAX; break;
				case 6u: values[i] = i % 2u ? -FLT_MAX : FLT_MIN; break;
				case 7u: values[i] = i % 2u ? -(f32)i : (f32)i; break;
				default:
				{
					u32 bits = profile_random(&random);
					if ((bits & 0x7f800000u) == 0x7f800000u)
						bits &= ~0x00800000u;
					memcpy(&values[i], &bits, sizeof(bits));
					break;
				}
			}
		}
		for (u32 count = 0u; count <= RG_ARRAY_COUNT(values); count++)
		{
			if (!profile_check_case(values, count, pattern)) return 1;
			cases++;
		}
	}
	printf("Demo profile percentiles: %u oracle comparisons passed.\n", cases);
	return 0;
}
