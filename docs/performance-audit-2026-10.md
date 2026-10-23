# Performance audit, October 2026

This audit measured where an `rg_gui` frame spends its time on Linux/X11/Vulkan,
read the frontend, text renderer, and GPU preparation hot paths, and compared
the result with the render-side caching described in Blick's
["Caching an immediate-mode UI"](https://blickeditor.com/blog/caching-an-immediate-mode-ui).
It records what was changed, what was measured before and after, and what
remains worth doing. All numbers come from one Intel Core i3-1005G1 laptop
(2 cores / 4 threads, UHD Graphics G1, Mesa 25.2.8, SDL 3.4.18, GCC 13.3,
`-O2 -DNDEBUG`), with normal desktop scheduling. Treat small differences as
noise; the machine's frame-time tails are wide.

## Where a frame goes

The full showcase was profiled with the repository runner in Immediate
presentation so that presentation waits do not hide the work
(`--scenario idle` and `--scenario edit`, 600 measured frames each). Medians:

| Stage (CPU wall time) | Idle | Edit | Owner |
| --- | ---: | ---: | --- |
| UI build (`rg_gui_begin_frame` to `rg_gui_end_frame`) | 0.071 ms | 0.200 ms | rg_gui |
| Prepare (`rg_gui_renderer_prepare` + `rg_gui_gpu_prepare`) | 0.050 ms | 0.047 ms | rg_gui |
| Stage upload (pack frame into the mapped ring) | 0.023 ms | 0.020 ms | rg_gui |
| Encode copy pass | 0.080 ms | 0.071 ms | SDL_GPU |
| Swapchain acquire | 0.130 ms | 0.094 ms | SDL_GPU / driver |
| Draw encode (18 draw calls) | 0.102 ms | 0.081 ms | SDL_GPU |
| Submit | 0.236 ms | 0.229 ms | SDL_GPU / driver |
| Frame work total | 1.60 ms | 1.79 ms | |

The showcase emits about 580 draw commands, 3,030 geometry vertices, and
480 glyphs per frame, batched into 18 draw calls. The library's own CPU work
is roughly 0.15 ms of a 1.6 ms frame; SDL_GPU encoding, submission, and the
driver account for the rest. On this iGPU the GPU repaints the whole 1280x800
window every frame.

Two things the profile cleared up:

- **Cache reclamation is not a hitch.** The text cache fills after about 450
  frames of churning statistics labels, then one frame evicts ~990 runs. That
  frame's prepare stage measured 0.05 to 0.09 ms, no worse than an ordinary
  frame. The multi-millisecond prepare outliers in the capture coincide with
  frames that had only one to three cache misses; they are scheduler noise.
- **Batching is already effective.** Eighteen draw calls for 580 commands, two
  copy calls per frame, and about 10 KB of run descriptors plus 60 KB of
  geometry uploaded per frame.

## CPU attribution

`gprof` (no inlining, so attribution is approximate) on the extended CPU
benchmark put the time in three places:

| Workload | Hot functions |
| --- | --- |
| Text-area wrapping and click/drag | `rg_gui_text_area_wrap_line_end` 27%, `rg_text_find_kerning` 23%, `rg_gui_utf8_boundary_before_or_at` 19%, `rg_text_find_glyph_exact` 12% |
| Changing labels and cold renderer | `rg_text_find_kerning` 21%, `rg_text_find_glyph_exact` 21%, `rg_gui_renderer_build_pages` 16%, `rg_text_decode_utf8` 11% |

Every non-ASCII glyph went through two binary searches (glyph, then kerning
pair) because `RgGuiTextLookup` only covered codepoints below 128. On the
synthetic 4,096-glyph font the changing-label workload was 10x slower than the
same workload in ASCII (349 vs 36 us per 128 labels). Any Greek, Cyrillic,
Hebrew, Arabic, or CJK interface paid that cost on every frame of changing text.

## Changes made

### 1. General glyph and kerning tables in `RgGuiTextLookup`

Two fixed-size open-addressed tables now index every glyph codepoint and every
kerning pair of fonts with up to 4,096 of each (`RG_GUI_TEXT_LOOKUP_GLYPH_SLOTS`
and `RG_GUI_TEXT_LOOKUP_KERNING_SLOTS`, 8,192 slots each). Each slot is a 16-bit
index into the font's own arrays, so a lookup is one hash, one or two loads, and
one compare. The ASCII arrays are untouched and stay inline in the measuring
loops; the hashed path lives in a separate non-inlined function so the ASCII
code does not grow. Larger fonts keep `rg_text`'s binary searches. The frontend
and the text renderer both use the shared helpers, so measurement, wrapping,
and glyph layout all benefit. The table grows from 66,568 to 99,352 bytes.

Results are identical by construction: first occurrences are inserted, which
matches `rg_text`'s linear scan for unsorted arrays, and a loader-sorted array
is checked to be strictly ascending before its table is trusted. The lookup
tests compare every glyph, neighbor codepoint, and pair against `rg_text`, for
fonts within and beyond the table capacity (468,757 checks).

### 2. Text-area wrapping

`rg_gui_text_area_wrap_line_end` computed every UTF-8 boundary twice per
character, and `rg_gui_utf8_next_boundary` always ran a backward scan first.
The boundary is now computed once per character and the backward scan runs only
when the current byte is a continuation byte. Visual-line output is unchanged;
the existing wrapping and layout-cache regression tests cover it. Wrapping
8 KiB of ASCII went from about 150 us to 90 us; the click-and-drag editor frame
from 155 us to 100 us.

### 3. Bounded probing in the pointer-keyed text caches

`rg_gui_text_measure_cached` and `rg_gui_text_len_cached` probed the entire
table on a miss once it was full, then overwrote one slot. An application whose
static-label set exceeds the configured capacity would pay a full-table scan
per label per frame. Probing is now limited to `RG_GUI_TEXT_CACHE_MAX_PROBES`,
after which the entry's home slot is replaced.

The budget matters. A first version used 8 probes, and a review (Codex,
2026-10-23) showed it evicting live entries once a table passed about half
occupancy: 768 static labels in a 1,024-entry cache measured 2.9x slower than
the unbounded version. The mean unsuccessful linear probe at 75 percent load is
about 8.5 slots, so 8 is simply too short. The default is now 32, measured on a
768/900/1000/2048-label sweep against the 8-probe and unbounded variants
(microseconds per pass, 1,024-entry cache):

| Labels | Unbounded | 8 probes | 32 probes |
| ---: | ---: | ---: | ---: |
| 768 | 9.5 | 16.0 | 9.3 |
| 900 | 15.1 | 30.3 | 19.7 |
| 1,000 | 36.7 | 48.0 | 37.7 |
| 2,048 (saturated) | 4,941 | 277 | 381 |

Hits that sit within the budget are unchanged; entries beyond it are evicted,
so a cache that is much more than three-quarters full still trades some
re-measurement for the bounded miss cost. Size the cache for the label set.

### 4. Frame signature for skipping unchanged frames

This is the part of the Blick article that applies directly. The article
separates "rebuild the UI description" from "repaint pixels" and skips the
whole painter when nothing changed (its level-1 skip), falling back to per-tile
hashing when something did (level 2).

`rg_gui_gpu_prepare` now produces `rg_gui_gpu_frame_signature(&gpu)`: an
order-sensitive 64-bit digest of every command's type, geometry, color, clip,
texture, material, text position and scale, the text bytes or static-text
identity, the command count, and the overlay boundary. Text bytes are already
hashed once by the renderer for its content-keyed cache; that digest is folded
in. A caller that sees the same signature and output size as the frame it last
presented can skip staging, encoding, drawing, and presenting. The text cache
stays consistent across skipped frames because page uploads are revision based.

The digest is opt-in (`RgGuiGpuDesc.frame_signature`). The first version
computed it unconditionally, and the same review measured the cost on the
geometry benchmark: 128 image commands went from 3.7 to 5.9 us of preparation,
about 17 ns per command, paid by every caller whether or not it skipped frames.
Colors now enter the digest as the packed 32-bit value the GPU reads, which is
both cheaper and more precise about what changes pixels.

The minimal demo uses it. Its progress bar now advances in 1/64 steps so idle
frames repeat, and it waits for input (16 ms timeout) instead of spinning when a
frame is skipped. Over 600 frames with VSync it presented 133 frames and skipped
467 that would have redrawn identical pixels. `--no-skip` restores the old
behavior for comparison.

The full showcase does not skip, because its frame-time plot and statistics
change every frame by design.

## Measured effect of the changes

Extended CPU suite, `benchmarks/run.py compare`, five alternating process
trials, median of process medians, microseconds per 128-label frame (or per
8 KiB text-area operation). Lower is better.

| Font | Case | Before | After | Change |
| --- | --- | ---: | ---: | ---: |
| inter95_ascii | frontend_measured_labels_static_warm | 3.179 | 3.011 | 5.3% |
| inter95_ascii | frontend_measured_labels_dynamic | 27.698 | 24.856 | 10.3% |
| inter95_ascii | renderer_static_warm | 3.767 | 3.560 | 5.5% |
| inter95_ascii | renderer_content_warm | 7.604 | 7.161 | 5.8% |
| inter95_ascii | renderer_cold | 81.225 | 67.922 | 16.4% |
| inter95_ascii | renderer_init | 3.038 | 6.448 | -112.3% |
| inter95_ascii | gpu_prepare_static_warm | 5.633 | 5.036 | 10.6% |
| inter95_ascii | gpu_prepare_content_warm | 10.608 | 8.916 | 16.0% |
| inter95_ascii | gpu_prepare_cold | 85.232 | 66.929 | 21.5% |
| inter95_ascii | textarea_wrap_300 | 160.700 | 96.152 | 40.2% |
| inter95_ascii | textarea_wrap_1200 | 144.649 | 83.098 | 42.6% |
| inter95_ascii | textarea_click_drag | 154.667 | 100.520 | 35.0% |
| synthetic4096_mixed_utf8 | frontend_measured_labels_static_warm | 4.185 | 4.237 | -1.2% |
| synthetic4096_mixed_utf8 | frontend_measured_labels_dynamic | 368.342 | 113.962 | 69.1% |
| synthetic4096_mixed_utf8 | renderer_static_warm | 4.128 | 3.651 | 11.5% |
| synthetic4096_mixed_utf8 | renderer_content_warm | 10.319 | 8.855 | 14.2% |
| synthetic4096_mixed_utf8 | renderer_cold | 404.724 | 192.449 | 52.4% |
| synthetic4096_mixed_utf8 | renderer_init | 30.465 | 88.706 | -191.2% |
| synthetic4096_mixed_utf8 | gpu_prepare_static_warm | 5.367 | 5.166 | 3.7% |
| synthetic4096_mixed_utf8 | gpu_prepare_content_warm | 9.972 | 10.030 | -0.6% |
| synthetic4096_mixed_utf8 | gpu_prepare_cold | 395.754 | 200.275 | 49.4% |
| synthetic4096_mixed_utf8 | textarea_wrap_300 | 332.758 | 154.410 | 53.6% |
| synthetic4096_mixed_utf8 | textarea_wrap_1200 | 347.049 | 165.791 | 52.2% |
| synthetic4096_mixed_utf8 | textarea_click_drag | 329.469 | 142.230 | 56.8% |

Unicode suite, `benchmarks/run_unicode.py`, same protocol; "Change" is the
time reduction (positive is faster). The 4,096-glyph font fits the new tables;
the 16,384-glyph font does not and keeps the old path, where its results differ
from the baseline by less than the trial-to-trial noise on this machine.

| Font | Corpus | Case | Before | After | Change |
| --- | --- | --- | ---: | ---: | ---: |
| synthetic4096_cjk | repeated32 | frontend_dynamic | 183.0 | 66.3 | 64% |
| synthetic4096_cjk | repeated32 | renderer_warm | 2.7 | 2.8 | -4% |
| synthetic4096_cjk | repeated32 | renderer_cold | 224.5 | 105.8 | 53% |
| synthetic4096_cjk | repeated32 | lookup_pairs | 170.7 | 39.0 | 77% |
| synthetic4096_cjk | dispersed | frontend_dynamic | 425.7 | 121.1 | 72% |
| synthetic4096_cjk | dispersed | renderer_warm | 2.8 | 2.8 | -1% |
| synthetic4096_cjk | dispersed | renderer_cold | 517.7 | 149.2 | 71% |
| synthetic4096_cjk | dispersed | lookup_pairs | 434.6 | 83.3 | 81% |
| synthetic16384_cjk | repeated32 | frontend_dynamic | 225.4 | 237.6 | -5% |
| synthetic16384_cjk | repeated32 | renderer_warm | 2.7 | 2.9 | -7% |
| synthetic16384_cjk | repeated32 | renderer_cold | 263.4 | 261.4 | 1% |
| synthetic16384_cjk | repeated32 | lookup_pairs | 204.4 | 216.5 | -6% |
| synthetic16384_cjk | dispersed | frontend_dynamic | 500.9 | 545.7 | -9% |
| synthetic16384_cjk | dispersed | renderer_warm | 2.7 | 2.7 | 0% |
| synthetic16384_cjk | dispersed | renderer_cold | 604.6 | 579.1 | 4% |
| synthetic16384_cjk | dispersed | lookup_pairs | 504.2 | 512.8 | -2% |

ASCII workloads should be read as unchanged within noise except where a change
above touches them (wrapping and click/drag). `renderer_init` is slower because
an arena-owned lookup now also builds the two tables; this is a one-time cost
per renderer, not per frame.

## The article's tile cache, assessed for rg_gui

Blick's renderer is a GPU compute painter: commands are binned into tiles, each
tile's command list is hashed, and only tiles whose hash changed are repainted
with a per-pixel signed-distance evaluation. That design does not transplant
onto rg_gui's indexed rasterization path, and rebuilding the GPU backend as a
compute painter is not justified by the measurements above: on a window that is
repainted anyway, the GPU draw of 18 batches is cheap, and the CPU side is
dominated by SDL_GPU encoding and submission, which a tile cache does not
remove.

What transfers is the two-level idea, and the signature above is level one.
Level two, repainting only changed regions, would need:

1. A persistent offscreen color target instead of drawing straight into the
   swapchain, since swapchain images do not preserve contents between frames.
2. Per-command screen bounds (rectangles, images, and text runs already have
   them; clip rectangles bound them further) binned into a coarse tile grid,
   with a per-tile hash of the commands that touch it, compared with the
   previous frame to produce a dirty-tile set.
3. A render pass with `LOAD_OP_LOAD`, scissored to the dirty rectangle(s), that
   draws only the commands intersecting dirty tiles, followed by a blit of the
   offscreen target to the swapchain.

The cost is one extra full-window copy per presented frame (about 4 MB at
1280x800) plus the binning work, and the benefit is proportional to how much of
the window is static while something small animates, such as a caret or a
status counter. For editor-style layouts that is most of the window. It should
be prototyped behind the signature API rather than inside the renderer: the
draw list already carries everything a binning pass needs. The stale-pixel
risk the article warns about (scroll, resize, texture changes) is handled by
treating any change in output size or external texture revision as a full
invalidation, as the level-one skip already requires.

## Remaining opportunities, in priority order

1. **Four vertices per rectangle instead of six.** Rectangles and images are
   emitted as two triangles (120 bytes) and the frame packer then detects the
   shared corners with two `memcmp`s to emit one quad reference. Emitting four
   vertices with an explicit quad item type removes the detection pass and a
   third of the geometry upload (60 KB to 40 KB per showcase frame). It needs
   a one-line change in `rg_gui_indexed.vert.hlsl` (quad corner 3 maps to raw
   vertex 3) and a per-item vertex stride. Expected gain: roughly 10 us of
   CPU per showcase frame and less upload bandwidth.
2. **A smaller `RgGuiDrawCmd`.** The command is 80 bytes because `rg_vec4`
   colors force 16-byte alignment and padding. Packing colors to 32-bit RGBA
   (the renderer converts them anyway) and tightening the union would bring a
   command to 32-48 bytes. The list is written once and read three times per
   frame (overlay merge, renderer prepare, GPU prepare), so this is cache
   footprint, not instructions. It changes a public struct and belongs in a
   1.0 break.
3. **Carry text length in the command.** The renderer calls `strlen` on every
   non-static text command each frame; the frontend already knows the length
   when it copies the text. There is unused padding in the text union member
   for a 32-bit length. Expected gain: a few microseconds per frame.
4. **Larger fonts.** Done on the rg_text side on 2026-10-22: its loader now
   builds an ASCII glyph table, a cached fallback index, and per-glyph kerning
   ranges, and aligned layout resolves each glyph once. rg_text's own paired
   benchmark improved 35 to 88 percent across every workload with identical
   output. For rg_gui this makes the no-lookup and over-4,096-glyph paths much
   cheaper; the hash tables here remain worthwhile because a non-ASCII glyph
   in rg_text is still a binary search (3 to 4x slower than the table on the
   4,096-glyph benchmark font). Raising the slot counts is the remaining knob
   for very large fonts.
5. **Text-area relayout on edit.** Each keystroke re-wraps the whole document.
   At the current 1,024-visual-line cap that is 100-200 us for 8 KiB of ASCII,
   acceptable, but a paragraph-local relayout would make it independent of
   document size if the cap is ever raised.
6. **`rg_gui_text_area_find_visual_line` is a linear scan** over up to 1,024
   lines, called a few times per frame. A binary search over line starts is a
   small, safe win.

## Verified and left alone

- Reclaim/eviction sweeps: 0.05 to 0.09 ms when they happen, roughly every 450
  frames in the showcase.
- Renderer hash table: sized at twice the run capacity and rebuilt on reclaim,
  so probe lengths stay short.
- Batching: 18 draws for 580 commands; items merge across compatible neighbors
  and batches merge across items.
- Content hashing of changing text (`rg_hash_bytes`, xxHash-style): about
  140 ns per 44-byte label including the cache probe and `memcmp`; this is the
  price of content-keyed reuse and is well below the layout cost it avoids.
- Upload ring packing writes each vertex once into mapped memory; the
  per-glyph 8-byte reference stream is sequential.
