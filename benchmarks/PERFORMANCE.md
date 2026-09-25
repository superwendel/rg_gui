# Performance against Dear ImGui

In this workload suite, `rg_gui` has lower median frame time in **15 of 16
configurations**. Dear ImGui is faster in the mixed inspector with two frames
in flight: `rg_gui` takes **13.2% longer**. These are project measurements on
one Windows/D3D12 system; results depend on the workload and configuration.

The [result data](comparison.json) include every trial summary, CPU preparation
timings, frame-time ranges and tails, memory reservations, and source identities.

## Frame times

All times are **microseconds; lower is better**. p50 is the median and p95 is
the 95th percentile. Each table value is the median of six per-trial
percentiles. The change column compares the two frame p50 values.

Frame time includes CPU work and required GPU-fence waits. It does not measure
GPU execution time, display FPS, or input latency.

### One frame in flight

Each frame waits for GPU completion before the next frame begins.

| Workload | rg_gui p50 | Dear ImGui p50 | rg_gui median time | rg_gui p95 | Dear ImGui p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Static labels | 94.40 | 140.10 | 32.6% lower | 122.60 | 183.20 |
| Changing labels | 121.35 | 135.80 | 10.6% lower | 164.95 | 179.65 |
| Inspector | 105.10 | 121.40 | 13.4% lower | 144.95 | 168.45 |
| Virtualized list | 90.80 | 97.00 | 6.4% lower | 126.05 | 135.35 |
| Mixed inspector | 105.60 | 120.30 | 12.2% lower | 145.20 | 169.35 |
| Asset browser | 83.20 | 103.45 | 19.6% lower | 119.30 | 147.30 |
| Text editor | 100.80 | 148.85 | 32.3% lower | 144.85 | 200.05 |
| Docked workspace | 118.70 | 138.55 | 14.3% lower | 167.45 | 190.00 |

### Two frames in flight

Up to two frames can be pending. Frame time includes waiting when this queue
is full, allowing CPU preparation and GPU execution to overlap.

| Workload | rg_gui p50 | Dear ImGui p50 | rg_gui median time | rg_gui p95 | Dear ImGui p95 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Static labels | 52.80 | 73.10 | 27.8% lower | 72.45 | 101.60 |
| Changing labels | 69.45 | 75.85 | 8.4% lower | 87.90 | 99.35 |
| Inspector | 61.50 | 63.70 | 3.5% lower | 85.85 | 92.65 |
| Virtualized list | 55.65 | 59.40 | 6.3% lower | 70.80 | 71.90 |
| Mixed inspector | 60.90 | 53.80 | **13.2% higher** | 77.15 | 86.95 |
| Asset browser | 31.70 | 45.70 | 30.6% lower | 48.75 | 62.05 |
| Text editor | 53.50 | 86.65 | 38.3% lower | 90.05 | 123.00 |
| Docked workspace | 65.55 | 81.40 | 19.5% lower | 102.10 | 116.95 |

CPU widget submission and renderer preparation medians are lower for `rg_gui`
in all 16 configurations. Frame p95 is also lower in all 16, but later tails
and maxima do not uniformly improve: the two-frame list has p99 of **130.40
versus 120.35 microseconds**, and changing labels with two frames in flight
have a worst observed frame of **1.86 versus 1.28 milliseconds**. Small median
differences should be read alongside the trial ranges in the data; those ranges
are not confidence intervals.

This configuration reserves more CPU and GPU/upload-buffer memory in `rg_gui`
than in Dear ImGui. Persistent caches and configured capacities are part of the
tradeoff. The data count explicit reservations, excluding shared font/target
resources and opaque SDL/driver allocations; they are not process memory or
resident VRAM measurements.

## Workloads

| Workload | Content and interactions |
| --- | --- |
| Static labels | 128 unchanged labels at fixed positions. |
| Changing labels | 128 labels cycling through a fixed text corpus. |
| Inspector | 32 checkboxes and 32 sliders with changing numeric values. |
| Virtualized list | 32 visible text rows from 10,000 assets. |
| Mixed inspector | Checkboxes and sliders with replayed clicks and drags. |
| Asset browser | Filtering, selection, and scrolling through visible asset rows. |
| Text editor | An 8 KiB multiline document with editing and selection. |
| Docked workspace | Four panels with changing labels, splitter resizing, and tab rearrangement. |

Shared scene generation, filtering, and list virtualization occur outside the
timed frame. Native widget formatting and UI/render preparation are timed.
The docking workload restores its layout periodically during measurement.

## Configuration and limits

- **Hardware:** Intel Core i7-12700KF, NVIDIA RTX 2060, driver 595.97, Windows 11
  build 26200. Normal OS scheduling, with serial benchmark processes.
- **Rendering:** SDL 3.4.10, D3D12, 1280 x 800 offscreen target. No swapchain
  presentation is measured.
- **Libraries:** the `rg_gui` source identified in the data and Dear ImGui
  **1.93.0 WIP**. The first four workloads use its mainline branch; the other
  four use its docking branch. Exact source revisions are recorded in the data.
- **Build:** MSVC release optimization (`/O2 /DNDEBUG /MD`), C11 for `rg_gui`
  and C++17 for Dear ImGui, without link-time optimization.
- **Method:** six alternating process pairs per workload and buffering mode,
  with 1,024 warmup and 8,192 measured frames per process. All measured frames
  are retained. Initialization and font loading are outside timing.
- **Text and style:** the same prebaked Inter 16 atlas and glyph metrics, with
  kerning, shape antialiasing, and rounding disabled. Native widget shapes
  still differ. This differs from `rg_gui`'s default kerning-enabled text.
- **Caching:** `rg_gui` uses shared ASCII lookup, 32-glyph cache pages, and the
  optional editor layout cache. Dear ImGui uses its native widgets and official
  SDL_GPU renderer, importing the common immutable font through its font loader.
- **Validation:** core text images and inspector numeric regions match across
  libraries within two color levels. Tool interactions pass state checks and
  save reference frames; cross-library pixel equality is not claimed for native
  widgets or docking.

These warm workloads do not cover startup, native-window lifecycle costs,
complex Unicode shaping, other GPUs, or other operating systems.

The comparison harness and raw captures are maintained locally and are not
distributed in this repository. The linked data document the project
measurements; source hashes alone do not make the comparison publicly
reproducible. To measure `rg_gui` in your own setup, use the public
[CPU benchmark guide](README.md) and [demo profiling guide](PROFILING.md).
