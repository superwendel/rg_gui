# rg_core reuse measurements

The demos now reuse rg_core selection and bounded integer/string formatting, and
share the frontend font lookup with the renderer. The clearest gains are less
statistics work, faster renderer initialization, and about 65 KiB less renderer
arena storage. Steady-state results remain workload dependent.

Measured on 2026-09-22. [Selected data and input hashes](core_reuse.json) include
per-trial results and the controlled follow-up. [Benchmark commands](README.md)
and [frame capture instructions](PROFILING.md) support further comparisons.

## Method

- Intel Core i7-12700KF, RTX 2060, Windows 11 build 26200, SDL 3.4.10/D3D12.
- MSVC 19.44.35219 x64, `/std:c11 /O2 /DNDEBUG /W4 /WX`.
- Before: GUI `f9f7ab9`, rg_core `27d5475`; after: rg_core `d478715` plus the
  reuse changes. Both use rg_text `4b98c6d`.
- Core-only keeps the before GUI with the required demo input migration:
  `rg_input_begin_frame` before SDL polling, `rg_input_sample` afterward.
- CPU experiments used logical CPU 0; visible frame captures used ordinary OS
  scheduling. All processes ran serially, without concurrent builds or tests.
- Times are CPU wall durations, not GPU execution, visible FPS, or input latency.
  Results cover one machine and short workloads.

## Demo helpers

Five alternating process trials, 100,000 batches per process after 4,096 warmup
batches, using 256 fixed inputs. Entries are median microseconds per batch;
brackets span process results, not confidence intervals.

| Batch / implementation | Median µs [trial range] |
| --- | ---: |
| 120 samples, p95+p99: two qsort calls | 6.019 [5.949–6.100] |
| 120 samples, p95+p99: rg_algo selection | 0.261 [0.259–0.270] |
| Ten telemetry lines: all SDL | 2.790 [2.703–2.796] |
| Ten lines: portable rg integer/string + SDL decimal | 2.450 [2.419–2.507] |
| Ten lines: ASM-header C integer/string + SDL decimal | 2.288 [2.246–2.371] |
| Ten lines: native MASM integer/string + SDL decimal | 2.290 [2.259–2.368] |

The paired percentile calculation is **23× faster**, saving about 5.76 µs
per batch. It copies once and uses typed `rg_algo` selection instead of sorting
both copies. An independent qsort oracle checks 5,160 cases.

The shipped portable formatting change saves **0.34 µs per ten-line batch
(12.2%)**. Five decimal lines retain SDL formatting: all three rg backends
rounded `1.125` to `1.13` with `%.2f`, while this SDL runtime produced `1.12`.
The switched integer/string formats pass 533 bounded-output checks per backend.
The alternative C backend saved another 0.16 µs in this isolated workload;
native assembly added no measurable gain over that C backend. The demos retain
the portable formatter, including their existing widget formatting.

## Renderer controls

Eight rotating trials per variant, seven calibrated samples per trial. Every
variant uses one prebuilt frontend lookup and the same 129,472-byte benchmark
fixture. The shared table is initialized outside the renderer-init timing.
Allocation, font loading, and GPU startup are also excluded. Cold preparation
rebuilds uncached text; cache clearing is excluded. Values are median µs.

| Font / workload | Before | Core-only | Current private | Current shared |
| --- | ---: | ---: | ---: | ---: |
| Inter 95 glyphs / initialize | 1.542 | 1.497 | 1.377 | 0.180 |
| Inter 95 glyphs / cold text | 31.195 | 31.189 | 29.920 | 30.448 |
| Synthetic 4,096 glyphs / initialize | 9.268 | 9.049 | 8.977 | 4.712 |
| Synthetic 4,096 glyphs / cold text | 293.138 | 300.150 | 306.291 | 309.120 |

Shared initialization is **8.6× faster** for Inter and about **2× faster** for
the synthetic font versus before. This is renderer initialization with an
already-built lookup, not total application startup. The tested arena bound
drops from 2,076,649 to 2,010,079 bytes; the current private-table bound is
2,076,654 bytes. The optional descriptor field and `_ex` sizing API let callers
choose sharing; the legacy sizing function still covers private storage.

Cold text preparation is mixed: **Inter is 2.4% faster; the synthetic workload
is 5.5% slower than before**. Most of that synthetic difference remains with
the current private-table path. Shared versus current private differs by about
0.9% in the synthetic median, and individual trial differences split evenly.
The cause is not established; these results do not demonstrate uniformly faster
rendering. Output signatures match across all four variants.

An initial experiment used an extra lookup allocation only in its shared
fixture. Its results and a cold-case repeat are retained in the JSON, but the
corrected four-variant controls above determine the renderer comparisons.

## Full showcase with Mailbox

Three rotating trials per variant and scenario, 120 warmup plus 1,200 measured
frames per process. The window remained visible at 1280×800. All captures passed
presentation, interaction, diagnostic, and dropped-work checks. Each table value
is the median of three per-trial percentiles, not a pooled-frame percentile.
Times are milliseconds; brackets span trial p50s.

| Scenario / variant | Frame p50 [trial range] | p95 | p99 |
| --- | ---: | ---: | ---: |
| idle / before | 0.1736 [0.1569–0.3274] | 0.3155 | 6.4118 |
| idle / core-only | 0.1763 [0.1598–0.2928] | 0.3083 | 7.3488 |
| idle / after | 0.1646 [0.1543–0.3094] | 0.2852 | 5.8446 |
| edit / before | 0.2537 [0.2536–0.2562] | 0.4091 | 3.9156 |
| edit / core-only | 0.2542 [0.2487–0.2734] | 0.4460 | 5.4688 |
| edit / after | 0.2373 [0.2267–0.2893] | 0.4319 | 6.1522 |
| text-idle / before | 0.2489 [0.2258–0.4262] | 0.3982 | 6.1687 |
| text-idle / core-only | 0.2285 [0.2194–0.2445] | 0.3710 | 4.9842 |
| text-idle / after | 0.2202 [0.2135–0.2335] | 0.3713 | 4.9422 |

The final medians are lower, but substantial timing drift and mixed tails limit
whole-frame conclusions. Idle event/statistics p50 falls from 0.0101 to 0.0031 ms,
consistent with the isolated percentile saving. Editing frame p99 rises in this
capture, largely alongside swapchain waits; there is no demonstrated tail-latency
improvement. Mailbox behavior and the normal VSync launch default are preserved.

## Validation

`build.bat test_release` passed on Windows: frontend lookup checks, reference and
production renderer tests, portable/SSE2 GPU preparation, actual SDL_GPU execution,
asset validation, all three demo smoke tests, and 82 native-window lifecycle
checks. New percentile and bounded formatter tests are included in `test` and CI.
Strict Windows Clang syntax checks passed with and without `NDEBUG`. Linux
ASan/UBSan remains configured in CI and was not executed locally.
