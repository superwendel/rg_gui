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

gprof later proved to mislabel functions in the showcase binary (see "Many
docked panels"). The conclusions in this section stand on the before-and-after
measurements below, which confirmed them, not on the profile alone.

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

### 5. Four vertices per rectangle

Rectangles and images were written as two triangles, six vertices, and staging
then compared vertices 0 and 3, and 2 and 4, with two library `memcmp` calls
per primitive to recover the quad for the indexed shader. They are now written
as four vertices in corner order. Solid and image items are quads by
definition, and triangles have their own `RG_GUI_GPU_ITEM_TRIANGLES` item, so
packing never reads vertex contents. The indexed vertex shader maps quad corner
3 to raw vertex 3 instead of 5. Custom image materials draw the same four
vertices through the shared quad index buffer.

A host-only staging benchmark stages one prepared frame into a mapped ring.
Seven alternating pinned trials, median of process medians:

| Workload | Before | After | Change |
| --- | ---: | ---: | ---: |
| Stage 1,024 solid rectangles | 17.99 us | 3.10 us | -83% |
| Stage 1,024 images, alternating textures | 32.67 us | 24.74 us | -24% |
| Prepare and stage 1,024 solid rectangles | 31.59 us | 18.76 us | -41% |
| Prepare and stage 1,024 images | 50.47 us | 41.17 us | -18% |

The showcase's own timers agree. These runs predate the showcase layout fix
described under "Many docked panels". Immediate presentation, three
alternating pairs of 1,200 measured frames, median of run medians:

| Scenario | Stage | Prepare | Geometry upload |
| --- | ---: | ---: | ---: |
| idle, before | 30.1 us | 45.6 us | 60.7 KB |
| idle, after | 17.6 us | 41.6 us | 41.5 KB |
| images, before | 86.6 us | 87.2 us | 122.3 KB |
| images, after | 56.1 us | 72.7 us | 82.6 KB |

Draw calls (18 and 531) and glyph counts are unchanged. The GPU device test
renders byte-identical pixels with the old and new code, across the 672 painted
pixels of its 64x64 target. Total frame work did not change measurably: at 1.6
to 2.5 ms per frame SDL and the driver dominate, and the 16 to 45 us saved is
inside the run-to-run spread.

### 6. Clipping in the shader

The indexed renderer set a scissor per batch, so every clip change ended a
draw call: 23 per idle showcase frame, 42 with 60 docked panels. The vertex
shader now clips each primitive itself. Preparation gives each distinct clip
an entry in a frame clip table, once per clip change rather than per command;
each rectangle, image, or triangle reference carries its entry in 29 spare
bits, and each text run's record carries it for the run's glyphs. The shader
writes four clip distances from the entry, rounding the edges exactly as the
scissor did: floor the near edges and ceil the far ones after the draw
offset. The rasterizer drops the pixels outside. A draw call now ends only
where the texture changes or an image material needs its own pipeline;
material batches keep a scissor. Text run records in the frame upload shrank
from 32 to 16 bytes, and each clip entry adds 16 bytes.

SDL_GPU enables clip distances by default
(`SDL_PROP_GPU_DEVICE_CREATE_FEATURE_CLIP_DISTANCE_BOOLEAN`), and the shader
also compiles to DXIL and Metal.

| Scenario | idle | text-idle, edit, scroll | docks, 3 panels | docks, 60 panels | images | images-grouped |
| --- | ---: | ---: | ---: | ---: | ---: | ---: |
| Draw calls before | 23 | 18 | 32 | 42 | 536 | 26 |
| Draw calls after | 1 | 1 | 1 | 1 | 513 | 3 |

CPU time per frame, median of per-run medians, alternating builds. With VSync,
the normal mode, four rounds of 600 frames; every difference below was lower
in all four rounds, except image staging, which was equal:

| VSync | Draw encoding | Prepare | Stage |
| --- | ---: | ---: | ---: |
| idle | 102.3 to 82.3 us | 56.7 to 51.0 us | 25.6 to 22.4 us |
| docks, 60 panels | 122.6 to 81.9 us | 70.5 to 65.0 us | 37.2 to 29.6 us |
| images | 684.6 to 616.0 us | 141.1 to 133.8 us | 98.7 to 99.5 us |

With Immediate presentation, six rounds of 1,200 frames, draw encoding fell
from 88.5 to 67.9 us idle and from 104.1 to 66.1 us with 60 panels (6/6
rounds each), and preparation and staging fell 2 to 8 us. The image scenario
was the exception: 23 fewer draw calls saved 11 us of encoding, but
preparation and staging measured 7 and 10 us slower in every round, and frame
work was 10 us lower at the median, in 3 of 6 rounds. The cause was not
isolated. Timers inside the packer put about 2.5 us in the vertex copy, which
copies the same bytes as before, and 2 us in the item loop. Moving the vertices to the start of the
frame or aligning them to 64 bytes did not change it, and Clang builds showed
the same pattern. It does not appear with VSync or in the device-free staging
benchmark, which measures the new code at about 1.2 ns per image item. Under
Immediate presentation the 10th-percentile frame interval also rose about 50
us, idle and with 60 panels, while the mean frame interval, and so the frame
rate, stayed the same: about 550 frames per second, limited by presentation.

GPU time was measured without a profiler by drawing each frame 30 extra times
offscreen, which makes the GPU the bottleneck at 27 to 51 ms per frame. Over
four rounds the two builds stayed within 0.1 percent of each other in idle,
docks, and images (28.246 against 28.221 ms idle), so the clip distances cost
no measurable GPU time.

Pixels are unchanged. A scratch build fixed the timestep and zeroed the
statistics panel's live numbers so that runs repeat exactly; 100 frames
across ten scenarios read back byte-identical between the old and new
builds. A new device test draws rectangles, an image, a triangle, and text
under fractional, nested, shared, empty, and off-screen clips at four draw
offsets and in a reduced viewport, and checks all 20,480 pixels against
`rg_gui_gpu_scissor`. It fails when the shader drops the draw offset from the
clip or ignores a glyph's clip.

### 7. Dock tab title widths

A visible docked window lays out its node's tab bar every frame, and that
measured every tab's title: a glyph and kerning lookup per character, about
0.1 to 0.3 us per title. Titles are ordinary mutable strings, so the pointer
cannot key a cache. Each dock tab now keeps the width it measured with a copy
of the title's bytes, up to `RG_GUI_DOCK_TAB_TITLE_CACHE` (32 by default), and
measures again only when the bytes or the text scale change. Comparing the
bytes is cheaper than measuring them, and a title edited in place, even to
another string of the same length, is measured again. Longer titles are
measured every frame, as before. The copy grows each dock tab from 40 to 88
bytes, 24 KB at the default 512 tabs; setting the macro to 0 removes it.

UI build time, median of per-run medians, alternating builds:

| Scenario | Immediate, 6 rounds | VSync, 4 rounds |
| --- | ---: | ---: |
| docks, 60 panels (64 tabs measured per frame) | 119.6 to 112.4 us (6/6) | 91.9 to 85.9 us (4/4) |
| idle (4 tabs) | 88.4 to 88.8 us | 71.1 to 70.2 us |

The idle showcase measures only four titles per frame, and the change there is
within noise. A new test edits a docked window's title in place across
same-length and longer strings and checks the tab bar's layout against fresh
measurements; it fails if the cache compares only the length.

### 8. Visual-line lookup by bisection

`rg_gui_text_area_find_visual_line`, which finds the cursor's wrapped line a
few times per frame, scanned the lines from the top. It now bisects to the
first line that does not end before the cursor and continues the same scan
from there. The scan passes over every line it skips, so the result is the
same for any lines in text order; a randomized test compares the two over
2,000 layouts with empty lines, soft wraps, and gaps between lines, at every
cursor position. At the 1,024-line cap a lookup takes 13 ns instead of 631 ns
with the cursor at the end, and 65 ns instead of 322 ns at random positions.
The showcase's notes wrap to about 180 lines, so its gain is a fraction of a
microsecond per frame; the change matters if the line cap is raised.

### 9. A 56-byte draw command

`RgGuiDrawCmd` was 80 bytes: each payload's `rg_vec4` color is 16-byte
aligned, which also left 12 bytes of padding after the type. The color is now
stored once, after the type and outside the union, as the packed RGBA8 value
the GPU reads, packed at push time after the disabled tint. The union is
8-byte aligned: a rectangle or clip is 16 bytes, a triangle 24, text 32 (it
gains a 32-bit byte length), and an image 48, so a command is 56 bytes.
Readers that packed colors themselves (GPU preparation, the text renderer,
the frame signature, and the tile digest) now read the stored value, and the
renderer reads a copied text's length instead of calling `strlen` up to three
times per command.

Two first attempts lost time in the UI build and show where the cost moved.
Calling `strlen` at push time for every text made static labels 49 percent
slower in the device-free suite, because static text is keyed by its address
and never needed a length; the length is now recorded only where the copy
measures it anyway. Packing with SSE2 from a 16-byte load of a color built by
four scalar stores was slower than the scalar code, because the load waits on
store forwarding; building the vector from the four values in registers fixed
it. Packing is now a handful of SSE2 instructions where available, with a
scalar fallback that a test checks for identical results over a million
random colors and every 8-bit boundary, NaN, infinities, and signed zeros.

CPU time per frame, median of per-run medians, alternating builds against the
previous commit:

| Scenario | UI build | Prepare | Stage |
| --- | ---: | ---: | ---: |
| idle, Immediate, 6 rounds | 93.1 to 92.3 us | 72.9 to 59.1 us (6/6) | 36.3 to 34.0 us |
| docks, 60 panels, Immediate | 115.5 to 116.2 us | 101.3 to 82.7 us (6/6) | 50.8 to 46.7 us |
| images, Immediate | 143.6 to 137.9 us | 92.8 to 83.0 us (6/6) | 73.7 to 74.2 us |
| idle, VSync, 4 rounds | 71.8 to 72.7 us | 51.2 to 41.4 us (4/4) | 22.1 to 22.1 us |
| docks, 60 panels, VSync | 86.0 to 88.1 us | 64.8 to 51.9 us (4/4) | 29.0 to 28.9 us |

Preparation is 19 to 20 percent faster, staging a little faster or equal, and
the UI build within 2.5 percent either way, since it now does the packing
that preparation did. Together the library's CPU time per frame fell from
145.1 to 136.2 us idle and from 179.8 to 168.9 us with 60 panels under VSync,
about 6 percent, and 8 percent idle under Immediate presentation. The audit
had projected about 7 us at 60 panels from a padding experiment; the measured
saving is 11 us under VSync, partly because the renderer no longer measures
copied text. In the device-free suite static labels are 15 to 17 percent
slower, about 2.5 ns per label, the packing moved into the push; dynamic labels
are unchanged.

Pixels are unchanged: the 100 frames across ten scenarios read back
byte-identical to the frames recorded before any of this audit's GPU changes,
and the device tests pass. Code that builds or reads commands changes: see
the changelog's migration notes.

### 10. Paragraph-local text-area relayout

A keystroke in a text area with a layout cache re-wrapped the whole document:
the cache compares the text with its snapshot and, on any difference, rebuilt
every visual line. In a tight loop a keystroke in the showcase's 8 KiB notes
cost 112 us in the widget, 110 of them the wrap, and the cost grew with the
document: 28 us at 2 KiB, 207 us at 15 KiB. The showcase's edit scenario spent
240 us more UI build on a keystroke frame than on the frame after it.

The cache now finds the common prefix and suffix of the old and new text,
widens them to paragraph boundaries (the newline before the first changed
byte, the newline at or after the last one), keeps the lines of the paragraphs
before, wraps the paragraphs between afresh, and keeps the lines after, moved
by the length change. The result is exact because a paragraph's wrapping
depends on nothing but its own bytes, and `rg_gui_text_area_build_visual_lines`
itself wraps the changed region, so pieces, soft-wrap flags, and empty-line
rules are the ones a full build applies. When either layout reached the
1,024-line cap, where a layout may be cut short, the full build runs as before.
The prefix and suffix are compared 64 bytes at a time, the paragraph bounds
come from `memchr` after a short byte-by-byte look-back, and the kept lines
are found by bisection, so the fixed cost is under a microsecond for 8 KiB. A
`relayouts` counter records the partial path; `rebuilds` now counts only full
builds.

The saving is the wrapping of the untouched paragraphs. A document that is one
long paragraph still wraps in full on every keystroke, plus about 1.5 us for
the comparison and bookkeeping: alternating old and new builds three times on
an 8 KiB paragraph gave 115 to 117 us for both. What remains for ordinary text
is the wrap of the edited paragraph and a memory pass over the document, so
the cost still grows slowly with size: 3.0 us at 8 KiB and 4.5 us at 15 KiB.

An oracle test applies 3,000 random insertions, deletions, and replacements,
with newlines, tabs, carriage returns, multi-byte and malformed UTF-8, to
documents of 600 to 1,500 bytes at six widths including one glyph per line and
one pass of 700-byte paragraphs, and compares every result with a full build;
it also requires the partial path whenever both layouts are under the cap, the
full path when the old one is at it, and exactly one of them when the result
lands on the cap (a relayout proceeds there only when it can tell the layout
is complete). A list of 45 hand-written transitions covers empty texts,
trailing newlines added and removed, paragraphs merged and split, and `\r\n`,
and the paragraph-start search is checked against a plain scan at every
position of texts with no, dense, and sparse newlines. Ten deliberately broken
relayouts (each boundary comparison off by one, no widening to the paragraph
start or end, unshifted line ends, a dropped soft-wrap flag, a suffix allowed
to overlap the prefix, a missing cap guard, and two mistakes in the chunked
paragraph-start search) all fail the test.

| Measure | Before | After |
| --- | ---: | ---: |
| device-free suite, `textarea_edit`, Inter | 91.03 us | 2.08 us |
| device-free suite, `textarea_edit`, 4,096-glyph mixed UTF-8 | 253.45 us | 2.71 us |
| widget per keystroke, 8 KiB notes, tight loop | 112 us | 3.0 us |
| widget per keystroke, 15 KiB | 210 us | 4.5 us |
| showcase edit scenario, UI build on keystroke frames | 330 us | 114 us |
| showcase edit scenario, UI build on other frames | 89 us | 86 us |

The suite's `textarea_edit` case is new: one keystroke per frame, alternately
inserting a character and deleting it, in a cached 8 KiB document of one label
per line. Its other cases are unchanged within noise; the dynamic-label case on
the synthetic font moved 7.5 percent, in code this change does not touch. The
showcase figures are medians of three alternating rounds under Immediate
presentation. There the widget itself takes 38 us on a keystroke frame and
14 us on other frames, several times its tight-loop cost. Sampling
`/proc/cpuinfo` every 50 ms during the runs put the busiest core at a median
1.27 GHz under the demo, which waits on the GPU most of each frame, and at
3.40 GHz under the tight loop; the old wrap was inflated the same way, 240 us
there against 110 us in a loop. What remains per keystroke is mostly memory
traffic proportional to the document: the prefix and suffix comparison and the
copy of the text and lines into the cache. Relayout in place in the cache would
roughly halve that traffic and is left for later.

### 11. Two fragment input slots in the indexed shader

A Linux/Vulkan comparison with Dear ImGui (`benchmarks/third_party`,
2026-10-26) rendered both libraries offscreen with the same glyph bitmaps and
two frames in flight. rg_gui used less CPU in every workload, 1.3 to 11x, but
its frame time lost the inspector workloads by 5 to 8 percent. Every cell showed
a fence wait of tens of microseconds for both libraries, so both loops were paced
by the GPU queue rather than by their CPU work, and the frame column is the
per-frame cost of that queue: render pass, upload, and submission together.
SDL_GPU exposes no timestamp queries, so none of what follows is a hardware
GPU time.

A `--replicate N` flag was added to the harness: it encodes the prepared frame
N times into one render pass, and a line fitted through N = 1, 8, 16 separates
the per-replica frame cost (the slope) from the fixed per-frame cost (the
intercept). A replica adds one draw's worth of GPU work and its CPU encoding;
the CPU side grew from 56 to 149 us at N = 16 against a 1,480 us frame, so the
slope is almost all queue time, but it is a frame-time slope, not a GPU timer.
Three alternating rounds of 1,024 frames each, two flights, microseconds:

| Case | Library or shader variant | Per-replica (slope) | Fixed (intercept) |
| --- | --- | ---: | ---: |
| Inspector | rg_gui, shipping shader | 90.9 | 29.8 |
| Inspector | Dear ImGui | 75.1 | 42.4 |
| Static labels | rg_gui, shipping shader | 126.1 | 25.2 |
| Static labels | Dear ImGui | 113.2 | 44.5 |

rg_gui's fixed cost was lower in every case and its per-replica cost 11 to 22
percent higher, in text and in widgets alike. Variants of the indexed shaders,
compiled with shadercross and selected through an environment variable in the
harness, located the difference. Slopes are the median-of-rounds fits; the
shipping shader measured 89.8 to 93.2 (inspector) and 125.2 to 126.5 (labels)
across the five sweeps.

| Variant | Inspector slope | Labels slope |
| --- | ---: | ---: |
| shipping: `nointerpolation uint sampled`, branch in the fragment shader | 90.9 | 126.1 |
| no clip distances, no clip-table load | 90.0 | 126.2 |
| branch replaced by a select on the flag | 90.0 | 126.3 |
| flag as an interpolated float, lerp | 91.8 | 126.8 |
| flag as an interpolated float, select | 92.6 | 127.8 |
| flag exported but not read by the fragment shader | 76.8 | 109.7 |
| fragment shader always samples (no flag) | 76.7 | 110.8 |
| flag packed into `uv.z`, select (adopted) | 77.4 | 110.8 |
| flag encoded as negative `uv`, select | 77.2 | 110.7 |
| Dear ImGui | 75.1 | 113.2 |

Neither the clip distances nor the branch cost anything measurable. What cost
about 15 percent of the pass was a third fragment input slot being read, flat
or interpolated, integer or float, branched or selected. The adopted shader
widens `uv` to a float4 whose z is 1 for sampled geometry and 0 for solid,
samples unconditionally, and selects; solid geometry carries uv (0, 0), so its
discarded texel is one cached fetch. The device test's readback checks, 389
pixel probes and 20,480 clip-rule pixels covering materials, alpha, mutable
text, and the clip distances, pass unchanged, and the
comparison harness's verification found the label captures byte-identical to
Dear ImGui's after the change as before it.

With the adopted shader the comparison was rerun under its full protocol (six
alternating pairs, 2,048 measured frames, two flights), microseconds per frame,
with the pairs in which rg_gui's median was the lower:

| Workload | rg_gui before | rg_gui after | Dear ImGui | Pairs |
| --- | ---: | ---: | ---: | ---: |
| Static labels | 152.1 | 131.2 | 175.3 | 6/6 |
| Changing labels | 182.7 | 168.3 | 179.6 | 5/6 |
| Inspector | 124.2 | 108.3 | 125.7 | 6/6 |
| Virtualized list | 61.2 | 56.0 | 77.1 | 5/6 |
| Mixed inspector | 125.4 | 114.8 | 115.9 | 5/6 |
| Asset browser | 64.0 | 58.7 | 81.9 | 6/6 |
| Text editor | 356.8 | 357.5 | 388.7 | 6/6 |
| Docked workspace | 469.3 | 436.1 | 732.3 | 6/6 |

rg_gui now has the lower frame-time median in all 16 cells at both queue
depths, and the lower CPU time in all 16 as before. The machine was loaded
during this run (load average 4.3 falling to 2.5), which moved Dear ImGui's
numbers between runs; rg_gui's before and after columns moved by what the
slopes predicted.

In the full showcase under Immediate presentation, three alternating rounds of
1,200 frames with the old and new indexed shaders swapped in place, frame work
went from 1,810 to 1,749 us in the idle scenario and from 1,751 to 1,677 us in
the edit scenario, 3 to 4 percent; one idle round with the new shader overlapped
the old range. The showcase's frame is mostly the whole-window fill and the
present path, which the slot change does not touch.

The same sweep placed the changing-label penalty: with identical geometry the
dynamic case had the same slope as the static one (125.2 against 125.7) but a
fixed cost of 55 us against 25. That difference is not isolated to the GPU: the
CPU staging of the upload is part of it (23 against 9 us of upload time), and
the remainder was not attributed. An upload-statistics dump in the harness
adapter showed each changing frame uploading 98,304 bytes of glyph quads in
one range: 128 labels of 26 glyphs padded to 32-quad pages at 24 bytes per
quad. A 16-byte quad (pixel-snapped 16-bit position and size) would cut that
by a third; it is left for later.

### 12. Hidden-draw culling

With change 11 the comparison's render pass matched Dear ImGui's, but the
showcase's own frame was still 1.75 ms, and the editor workload had not moved
with the shader change while the label workloads had. The difference between
them is a full-window background fill. Two measurements made on 2026-10-26
located the frame:

- A `--coverage` option in the full showcase rasterizes one frame's rectangles
  into a layer-count buffer. The idle scenario drew 735 commands, 571 of them
  rectangles covering 3.40 Mpx of a 1.02 Mpx window: 3.3 layers per pixel on
  average, 61 percent of pixels painted exactly three times. Painting the list
  back to front showed 2.37 Mpx of that, 70 percent, under a later opaque
  rectangle, and 43 rectangles entirely hidden. The 20-panel docks scenario was
  3.2 layers and 68 percent hidden.
- A `--size` option runs the showcase at other window sizes. Frame work under
  Immediate presentation at 640x400, 905x566, 1280x800, and 1568x980 fitted
  frame = 114 + 1,568 us per megapixel of window: linear in pixels, with a
  small fixed part. The frame is fill, and the fill is memory traffic on
  blended full-window layers.
- Skipping the two full-window fills in a scratch build (the backdrop and the
  dockspace panel, 1.98 Mpx of hidden paint) took the idle frame from 1,757 to
  1,280 us: about 240 us per megapixel of blended paint, in line with the slope.

`rg_gui_end_frame` now culls hidden draws. It walks the finished list
backwards, keeping the 16 largest opaque rectangles passed so far as occluders
(each at least 4,096 px). A rectangle of at least 1,024 px whose painted area
lies inside an occluder is dropped; one of at least 16,384 px that an occluder
covers by a quarter or more is replaced by its remainder, up to eight pieces,
subtracting further occluders from the pieces. Only rectangles are dropped or
split and only opaque rectangles occlude. An occludee's area is its rectangle
inside its clip widened by one unit on each side, which overstates it under
any draw offset (the renderers round the clip to pixels after adding the
offset, so the pixel edge lies within one unit of the clip edge); an
occluder's is its rectangle inside its unrounded clip, which understates it.
A clip pushed with a negative size is the empty clip the renderers draw.
Pieces share their edges with the occluder, so each pixel center lands in
exactly one under the rasterizer's rule, and a piece is only emitted when its
position plus size, which is how the renderers rebuild its far edge, is that
edge exactly. The decisions are made in one backward pass; then the list is
written into the overlay list's storage, free once merged, and copied back,
with the overlay start moved to the same boundary, since the renderers reset
their clip stacks there. The culled list is never longer than the pushed one:
a split spends the slots that dropped rectangles returned, the splits saving
the most paint first, so geometry sized for the pushed list still fits.

Verification: a reference rasterizer in the CPU suite paints 3,000 random lists
of up to 60 rectangles, clips, and triangles at 256x256 with the GPU path's
rules, three lists in four under a random fractional draw offset, one clip in
ten pushed with a negative size, and one list in eight with its x coordinates
hundreds of thousands of units to the left on mixed float grids; the culled
list must match byte for byte, keep every non-rectangle command in order, and
never be longer than the pushed list. 29,100 rectangles were culled and 5,674
split across the run, 2,040 of them in the far lists. It found two defects
before the pass first shipped (the overlay start was not moved, and a split
could take slots that later commands still needed), and a fixed far case from
the review below is checked as well. The device test renders a 24-command
layered list with clips and translucency as pushed and as culled into a
256x256 target and compares the readbacks (65,536 pixels identical with 12
commands dropped and 2 split), checks that the culled list prepares within the
vertex and item counts the pushed list used, and renders the review's four
cases (a 0.75 offset, a thin rectangle under a fractional clip at 0.6, a
negative clip width, and far coordinates) with no pixel changed.

| Showcase scenario, Immediate, 3 alternating rounds | kept | culled |
| --- | ---: | ---: |
| idle: frame work | 1,749 us | 1,328 us |
| idle: layers per pixel, hidden paint | 3.32, 2.37 Mpx | 1.51, 0.52 Mpx |
| edit: frame work | 1,750 us | 1,298 us |
| docks (20 panels): frame work | 1,777 us | 1,538 us |
| idle: UI build (includes the pass) | 78 us | 102 us |
| docks: UI build | 110 us | 139 us |

The pass costs 20 to 30 us of UI time per frame at the demo's clocks (about
1.27 GHz; see change 10), against 420 to 450 us of frame work saved in the
idle and edit scenarios and 240 us in docks. Timed alone on the dumped lists
(`--dump-commands`) in a tight loop at full clock, it is 8.0 us for the idle
scenario's 735 commands and 10.3 us for the docks scenario's 942, of which the
copy back is 1 us; it drops 7 and splits 2 rectangles in the idle list, which
is all it takes to remove the backdrop's and the dockspace's hidden paint. On
128 non-overlapping opaque 64x64 rectangles, which have nothing to cull, the
UI build goes from 0.6 to about 6.6 us. Ten mutants of the pass are run
against the oracle and all are caught: a one-unit slack in containment, an
occludee's clip rounded instead of widened, a negative clip size kept, an
inexact fragment edge emitted (caught only by the fixed far case), the budget
ignored, the budget's priority reversed, a missing remainder strip, a wrong
strip edge, translucent occluders, and a misplaced overlay boundary.

An independent review of the first version (2026-10-27) found four defects,
each reproduced on the GPU before it was fixed:

- The occludee's clip was rounded to pixels at offset zero, while the
  renderers round it after adding the draw offset; with an offset of 0.75 a
  column of 200 visible pixels was culled. The clip is now widened by one unit
  on each side instead, which bounds the pixel clip under any offset.
- A clip pushed with a width in [-1, 0) read as the pass's "unclipped" marker,
  so an opaque rectangle under an empty clip, which paints nothing, became an
  occluder and hid 16,384 pixels. Negative sizes are now stored as zero.
- A fragment's far edge is rebuilt by the renderers as position plus size,
  which for coordinates around 1.6 million moved an edge from 200.5 to
  200.5625 and painted 200 extra pixels. A split is now abandoned unless every
  piece's position plus size is its edge exactly.
- Splitting could lengthen the list (the idle showcase went from 735 to 736
  commands), so geometry sized for the pushed list could fail to prepare. The
  culled list is now never longer than the pushed one: the pass decides first,
  then spends the slots dropped rectangles returned on the splits that save the
  most paint, revoking the least valuable. In the idle list the revoked split
  is a small one; the backdrop keeps its two pieces. The pass also skips
  occluders that do not touch a fragment before testing containment, which is
  where the 10.0 to 8.0 us came from.

Re-measured after the fixes with the same executable, culling toggled at run
time, four alternating rounds of 900 frames under Immediate presentation on a
desktop with a load average near 2:

| Showcase scenario | kept | culled |
| --- | ---: | ---: |
| idle: frame work | 1,946 us | 1,570 us |
| idle: GPU render total | 1,767 us | 1,336 us |
| edit: frame work | 2,053 us | 1,460 us |
| edit: GPU render total | 1,828 us | 1,256 us |
| docks (20 panels): frame work | 1,871 us | 1,633 us |
| docks: GPU render total | 1,672 us | 1,407 us |

The absolute times are higher than the first table's because the desktop was
busier; the reductions, 19 to 29 percent of frame work and 13 to 31 percent of
the render total, match it.

The review's remaining objection was the default: an interface with nothing to
cull paid about 6 us per frame for nothing. The pass now paces itself. Each
run reports the paint it removed (the visible area of dropped rectangles plus
the area the splits gave up); when that is at least 32,768 px, about the
pass's own cost in fill on this GPU, the pass runs on the next frame too, and
when it is less, it waits eight frames or a change in the command count, which
is how a window opening or a popup arriving shows up. The frames it skips are
drawn as pushed, so pixels are the same on every frame; a context-level test
drives 40 frames of flat, layered, and flat again content, checks that the
pass ran on exactly the 19 frames the rule predicts, and compares every
frame's pixels with a context that keeps hidden draws. Per-frame UI build
times on one pinned core, 4,096 frames each, pass paced, forced every frame,
and off:

| Workload | off | paced, mean | paced, p50 / p90 | every frame, p50 |
| --- | ---: | ---: | ---: | ---: |
| 128 non-overlapping 64x64 rectangles | 0.51 us | 1.28 us | 0.44 / 6.05 us | 6.35 us |
| layered, 164 commands, 1.2 Mpx saved | 1.00 us | 2.60 us | 2.24 / 3.35 us | 2.83 us |

The flat workload runs the pass on one frame in eight, which is the p90
spike, and the layered one on every frame. Switching from flat to layered
content is culled on the first layered frame, since the count changes; the
reviewer's own control (five alternating pinned pairs) moved from 0.63 to 1.48
us for 128 rectangles and 2.41 to 2.83 us for 512. Content that layers only
on frames the pacing skips, without changing the command count, is the case
the rule misses; it is drawn as pushed until the next sampled frame.

The Dear ImGui comparison's workloads have no layered backgrounds, so culling
removes nothing there and its only effect is the pass's own 1 to 3 us of CPU;
a rerun on 2026-10-26 with culling on moved no cell beyond its noise. A
workload with a backdrop under panels under widgets, as real editors have,
would show the difference and is a candidate for the harness. The CPU
benchmark suite keeps hidden draws (`RgGuiInitDesc.keep_hidden_draws`) so
that its draw-list signatures stay comparable across versions, and times the
pass in its own extended case, `cull_hidden`, over a synthetic layered list of
525 commands (backdrop, menu, dockspace, three windows with clipped panels of
28 widget rows, a dialog, and an overlay tooltip): 12.3 us per frame before
the review's fixes and 9.0 us after (five alternating process pairs), with a
deterministic output signature that the runner records but, unlike the other
cases', does not compare across versions, since it is the pass's own output.
What remains hidden, 0.52 Mpx, is in rectangles below the thresholds or
covered by more than eight pieces' worth of occluders. VSync pacing is
unchanged at 16.67 ms.

### 13. Backdrop lift

After culling, the showcase's frame is still fill: one full layer of visible
paint plus the translucent widgets. Two fill-side ideas were measured on
2026-10-27 with scratch builds of the demo, idle and docks scenarios, four
rotating rounds of 900 frames under Immediate presentation:

| Variant | idle frame work | idle render total | docks frame work |
| --- | ---: | ---: | ---: |
| baseline | 1,370 us | 1,162 us | 1,636 us |
| no backdrop rectangle | 1,232 us | 1,035 us | 1,522 us |
| blending disabled in every pipeline | 1,379 us | 1,161 us | 1,544 us |
| both | 1,246 us | 1,044 us | 1,526 us |

Disabling blending changed nothing measurable in these scenarios on this GPU:
whatever the destination read costs here is hidden behind the rest of the
fill, so no separate opaque pipeline was built. That is one GPU and two
scenarios, not a general result, and a depth-tested opaque pass, which would
also skip hidden fragments, was not tried. Leaving out the backdrop rectangle saved about a tenth of the idle
frame, more than its culled remainder (two margin pieces, 0.29 Mpx) would
suggest at 240 us per megapixel, because the render pass already clears the
target to the same color: the rectangle was repainting the clear.

The frame's first command is almost always that rectangle, and the render pass
almost always clears, so the two are now merged. `rg_gui_lift_backdrop(ctx,
target)`, called before `rg_gui_end_frame`, asks the frame to lift its
backdrop: if the first command is an opaque rectangle covering the whole
render target (in list coordinates), `rg_gui_end_frame` records its color and
removes the rectangle from the list, and `rg_gui_backdrop_lifted` hands the
color to the caller for the clear. The request is per frame, because the
decision belongs to the render path: the demo's direct path lifts, and its
tile cache does not, since a dirty rectangle repainted over a kept target
needs the backdrop painted. Nothing is lifted for a smaller or translucent
first rectangle or any other first command. A context-level test drives seven
frames through those cases against a context that does not lift and checks
that clearing to the lifted color and drawing the rest paints the same pixels
as the full list.

A review of the first version found three interactions, all fixed before the
change was pushed. The rectangle must be the entire target the pass clears,
since the clear recolors everything, so the contract now says so (a lift for
a sub-viewport changed 3,072 pixels outside it in the reviewer's readback).
The emptied rectangle had been left in the list for the culling pass to drop,
which made the list, and the demos' frame signature, alternate on static
content as the paced pass ran or not; the rectangle now leaves the list at
once. And the demos' unchanged-frame skip keyed on the draw-list signature
alone, so a backdrop color change, which now reaches the target through the
clear, was skipped; the skip identity includes the effective clear color.

With the lift in the demo (`--keep-backdrop` turns it off), four alternating
rounds of 900 frames:

| Showcase scenario, Immediate | backdrop painted | backdrop lifted |
| --- | ---: | ---: |
| idle: frame work | 1,391 us | 1,237 us |
| idle: render total | 1,192 us | 1,057 us |
| edit: frame work | 1,339 us | 1,199 us |
| edit: render total | 1,110 us | 1,008 us |
| docks (20 panels): frame work | 1,543 us | 1,547 us |
| docks: render total | 1,315 us | 1,328 us |

The raw per-frame CSVs of both tables are kept under
`out/fill-experiments-2026-10-27/`. Idle and edit gain 10 to 11 percent. Docks gains nothing: its dockspace
covers the backdrop up to a 10-pixel frame, so culling had already reduced
the rectangle to that frame, and the lift removes only the frame. UI build
time falls about 6 us in every scenario because the culling pass no longer
has to split the backdrop. Relative to the frame before change 12, the idle
showcase has gone from 1,749 to about 1,240 us under Immediate presentation
with pixels unchanged.

### 14. Where the remaining frame goes

After changes 12 and 13 the idle showcase's frame was about 1,250 us under
Immediate presentation, of which culling and the lift accounted for the fill
they removed, but the fill that remained, about one layer at 240 us per
megapixel, explained only a quarter of what was left. The rest was attributed
on 2026-10-27 with two new showcase options: `--offscreen` draws into a
persistent texture of the window's size and paces on a fence two frames back
instead of acquiring and presenting a swapchain image (such frames count as
submitted, not presented, and the option refuses the tile cache and the smoke
test, which present), and `--drop KIND`
removes one kind of command (`text`, `translucent`, `rects`, `images`, `all`)
from the finished list. Idle scenario, three rotating rounds of 900 frames,
medians in microseconds:

| Configuration | frame work | fence or acquire wait | UI build |
| --- | ---: | ---: | ---: |
| present, full list | 1,273 | 764 | 105 |
| present, empty list | 664 | 400 | 97 |
| offscreen, full list | 347 | 88 | 62 |
| offscreen, no text | 314 | 54 | 74 |
| offscreen, no translucent rectangles | 347 | 52 | 74 |
| offscreen, no rectangles | 120 | 8 | 41 |
| offscreen, no images | 351 | 65 | 79 |
| offscreen, empty list | 97 | 7 | 50 |
| offscreen, full list, 905x566 | 237 | 82 | 37 |
| offscreen, full list, 640x400 | 147 | 15 | 32 |

These are frame-time throughputs with two frames in flight, not GPU
timings: SDL_GPU exposes no timestamp queries, so CPU and GPU work overlap
inside each number and the GPU's own share is not measured. With that
caveat, three readings. First, the presenting loop is the frame. The same
list runs at 347 us per frame offscreen and 1,273 us when each frame is
acquired from and presented to the X11 swapchain, and an empty list runs at
97 us against 664 us. The difference is the presentation path in this
configuration: acquire, present, and whatever synchronization and copying
Mesa's X11 path does, which this experiment does not look inside. Any
renderer presenting through the same window goes through the same path; its
cost for Dear ImGui was not measured. The comparison harness renders
offscreen, which is why its frame times are a third of the showcase's.
Second, rectangles are most of what remains. Dropping them takes the
offscreen frame from 347 to 120 us; dropping text takes it to 314 us for
620 glyphs. The idle list contains no images and almost no translucent
rectangles, so those two controls only confirm that. The 227 us the
rectangles cost is consistent with writing one opaque layer over 1.02 Mpx at
this GPU's memory bandwidth, and the size sweep's slope of 262 us per
megapixel points the same way, but the sweep is not a fixed-content sweep
(the showcase lays out fewer commands in a smaller window, 730 to 508) and
the GPU time is not measured, so the floor is inferred, not established. The
`--drop` filter itself adds a pass over the list, a microsecond or two.
Third, the fixed cost is CPU. The offscreen frame's recorded stages are
events 25, UI build 59, prepare 20, staging 10, upload encode 31, draw
encode 45, and submit 54 us, about 240 us of wall time per frame; these are
wall times and can include driver waits inside the calls. At 640x400 the
loop is CPU-bound. The UI build itself drops from 105 to 59 us when the loop
is not waiting on presentation, because the CPU clocks down while the GPU is
busy on this part. The raw per-frame CSVs are kept under
`out/attribution-2026-10-27/`.

The levers this leaves, in order: the per-frame fixed CPU cost (submit and
the two encodes first), profiled stage by stage, which also moves every cell
of the comparison harness; the glyph quad size for the text-heavy
workloads; and the present path, which is the platform's and can only be
avoided by presenting less (the present skip) or measured elsewhere (no
Wayland session was available on this desktop). Declaring the rendering at
its floor would need a fixed-content size sweep and a GPU timer, neither of
which this experiment has.

### 15. Per-call profile of the fixed CPU cost

With `perf` unavailable, the CPU stages of change 14 were split on
2026-10-27 by wrapping every SDL GPU call and every renderer entry point in a
timer (`benchmarks/profile/`, method in the profiling guide), running the
idle showcase offscreen at 1280x800 pinned to one core. The timers nest:
a renderer stage's inclusive time contains the SDL calls it makes, and its
exclusive time subtracts them frame by frame before the median is taken, so
the exclusive time is the library's own work. The wrapper itself costs 190
to 300 ns per wrapped call, about 5 us per frame over the 22 calls, and is
included in the inclusive numbers. The desktop was busy with other work
during these runs, load average near 7 on four logical CPUs, and the frame
was 408 to 410 us rather than the 347 us of change 14. Load does not
inflate every stage alike, so these numbers are the busy desktop's, not a
scaled quiet frame; two runs agreed within 2 us on every line. Per-frame
medians, two runs:

| Call or stage | inclusive | exclusive | note |
| --- | ---: | ---: | --- |
| `SDL_SubmitGPUCommandBufferAndAcquireFence` | 67 to 68 us | same | one call |
| `SDL_UploadToGPUBuffer` | 25 to 26 us | same | two calls: the frame data, and the text cache page the clock changes |
| `SDL_BeginGPURenderPass` | 24 to 26 us | same | one call |
| `SDL_DrawGPUIndexedPrimitives` | 19 us | same | the frame's single draw call |
| `SDL_AcquireGPUCommandBuffer` | 13 us | same | one call |
| `SDL_BindGPUGraphicsPipeline` | 5 us | same | one call |
| other SDL calls (binds, pushes, scissor, map, unmap, end) | about 8 us | same | twelve calls |
| `rg_gui_gpu_prepare` | 27 us | 27 us | no SDL calls inside: packing 673 commands |
| `rg_gui_gpu_stage_upload` | 10 to 11 us | 10 to 11 us | map and unmap are 1.7 us of it |
| `rg_gui_gpu_encode_upload` | 26 us | 0.6 us | the two uploads |
| `rg_gui_gpu_draw` | 29 us | 1.6 us | the draw, the pipeline bind, the small binds |
| `rg_gui_end_frame` | 21 to 23 us | same | culling, overlay merge, bookkeeping |
| `SDL_WaitForGPUFences` | 21 us | same | pacing, two frames in flight; p90 above 1 ms under this load |

Two readings. First, the time inside SDL entry points is the larger share.
Acquire, the two uploads, begin and end of the render pass, the pipeline
bind, the draw, and the submit come to about 165 us of the frame's wall time
under this load, against about 60 us exclusive in the renderer's own stages
(preparation 27, staging 10, end of frame 22, and under 3 us inside draw and
upload encoding) plus the widget build. rg_gui already issues the minimum
set of calls: one draw per frame, one render pass, one copy pass. The one
call it could still save is the second upload, the text cache page, by
giving the cache and the frame data one GPU buffer, which the frame buffer's
cycling makes non-trivial, for about 12 us. Second, within the library,
preparation is the largest single stage at 27 us, 40 ns per command for 673
commands; the full showcase does not enable the frame signature, so that is
vertex and item packing alone, and it and the widget build are where
rg_gui's own CPU work is left to find.

What the SDL time is made of was not separated. It contains the Vulkan
recording, barriers, descriptor updates, and queue submission that any
backend must do, plus whatever SDL_GPU does around them, and the profiler
cannot tell the two apart. The largest line, the submit at 67 us, is
probably mostly the former: on this offscreen path SDL 3.4.18's submit
takes its fence from a pool and performs no cleanup sweep (that runs inside
the fence wait, timed separately), so what it does is end the command
buffer and call `vkQueueSubmit`, which a native backend does too. The
hypothesis worth testing concerns the other lines: a single indexed draw
costing 19 us, a render pass begin 24 us, two buffer uploads 25 us, and a
command buffer acquire 13 us, about 80 us together, are each well above
what those operations cost when recorded directly on this class of driver,
and most of that difference would be SDL_GPU's descriptor, barrier, and
pool bookkeeping. A direct Vulkan backend might recover a good part of
those 80 us, but that is a prediction, not a measurement. The test is a
small matched prototype that records the same frame on raw Vulkan, with the
same two buffer updates, bytes, layouts, and synchronization, one render
pass, one draw, and one submit, timed the same way; upload consolidation,
if tried, is a separate measurement. That prototype should come before any
decision about a backend. The comparison harness runs both libraries on the
same SDL_GPU backend, where Dear ImGui's per-draw binds and scissors make
its share of this overhead larger; that share was not measured here
either.

### 16. Where a changed label's CPU time goes

The comparison's changing-label cell differs from its static one by 37 us
per frame at two flights, and the harness's own stage columns put 39 us of
that in the UI build (5.4 to 44.3 us), 11 us in staging, 8 us in the extra
upload call, and 11 us in submit. A smaller cached quad, the item on the
remaining list, was set aside and the UI-build delta profiled instead, on
2026-10-27, with call-level wrappers inside the renderer's production text
path. The first version of this section profiled the CPU benchmark's
fixture (41-byte labels of 35 glyphs, two 32-quad pages per run, kerning
on) and scaled the result to the comparison's label length; that is not a
valid substitute, since the one-page and multi-page layout branches differ,
and it is replaced here by a driver that reproduces the comparison
adapter's fixture exactly: `Item %03u: The quick brown fox %02u` (26 glyphs,
one page), `inter_medium_16` at text height 16, kerning disabled the way
the adapter disables it, the adapter's renderer limits (1,024 runs, 2,048
slots, 32-quad pages), the labels pushed with `rg_gui_label` at the
adapter's rectangles, then `rg_gui_renderer_prepare` on the list. The
driver asserts 3,328 glyphs and 128 cache misses (changing) or 128 hits
(static) every frame. One pinned core, 2,000 measured frames after 200,
load average 3 to 4 from other work on the machine.

The wrapper's cost was calibrated in place this time: a wrapped no-op,
called once per label per frame, measures 18 ns inside its own interval
(one clock read), while the whole wrapper costs 45 ns, so a parent's
exclusive time keeps about 27 ns per wrapped child call. The first
version subtracted the full cost from the leaves, which drove the cheap
stages to near zero, and left the residue in `get_run`'s exclusive time,
which it then reported as 13 us of bookkeeping. To avoid depending on the
correction at all, the same driver was also built against the
uninstrumented header with only the outer `prepare` wrapper, one call per
frame, as a control. The per-stage figures below are the instrumented
inclusive medians less 18 ns per call; the totals are the control's.

| 128 changing labels, 3,328 glyphs, comparison fixture | per frame | per label |
| --- | ---: | ---: |
| `rg_gui_renderer_prepare`, uninstrumented control | 33 us | 260 ns |
| of which `rg_gui_renderer_build_pages` (layout and quad write) | 26 us | 204 ns, 7.8 ns per glyph |
| of which `key_hash`, `allocate_pages`, `find_slot`, `strlen` | 5.6 us | 44 ns |
| remainder: `get_run` bookkeeping and the preparation loop | about 1 to 2 us | about 10 ns |
| `rg_gui_label` (frontend push) | 2.1 us | 16 ns |
| the same labels static, `prepare` control, all cache hits | 2.6 us | 20 ns |

The instrumented build's own `prepare` total was 71 us, more than twice
the control's 33 us, which is why the control exists: 640 wrapped calls
per frame at 45 ns each are not a perturbation this path can absorb. The
corrected leaves sum to 32 us against the control's 33, so the
decomposition is consistent to about a microsecond, and the remainder row
is the difference between two such numbers, an estimate rather than a
measurement. The static-to-changing difference in `prepare` is 30 us of
the harness's 39 us UI-build delta. The other 9 us is unattributed: the
harness formats its strings before the UI timer starts, so that is not it;
the GPU adapter's instance build after `prepare` and the label push are
candidates, but neither was measured on the changing cell.

What this says about the text path is a judgment, not a measured floor.
The layout loop takes 7.8 ns per glyph, about 25 cycles if the core held
its 3.4 GHz turbo (the clock under this load was not recorded), for an
ASCII decode, a glyph table read, two float adds, and a
24-byte quad store per glyph. No cycle-level bound was measured for that
loop and no faster version was built to compare against, so how much of
the 26 us is removable is not established here. The bookkeeping around a
miss, which the first version put at 100 ns per run, is about 10 ns
together with the preparation loop, and the cache-hit path at 20 ns per
label has nothing worth taking. The renderer's CPU preparation for this
cell is 33 us of a frame of about 168 us, and the layout is 26 us of it;
that bounds what a change to the preparation code can recover, not what a
change to the text format can, since a smaller quad also changes the
staged bytes and the GPU's vertex reads. A 16-byte
quad would change the layout's stores, page memory and cache footprint,
the staged bytes, and the GPU's vertex reads; the earlier measurement
bounded only the staging part (a few microseconds) and the rest remains
unmeasured.

The first version also proposed a width memo for applications that
measure a label and then draw it, on the strength of a synthetic driver
that measured all 128 labels with a text copy each frame (49 us). The
showcase does not do that. Counting calls in an instrumented showcase
build, `--offscreen`, 240 frames each: the idle and docks scenarios run 11
width traversals per frame and the edit scenario 6, over 30 to 86 bytes in
total, about a microsecond; plain labels only push text, menu and tab
measurements go through the existing pointer-keyed measure cache and the
dock tab title cache. There is no width-memo opportunity in the showcase.
The driver, the profiler header, the raw instrumented and control runs,
the showcase counts, and the earlier mismatched run are kept under
`out/profile-2026-10-27/`.

### 17. Matched raw-Vulkan prototype

Change 15 left a hypothesis: that about 80 us of the SDL GPU entry points
in the idle frame (the draw, the render pass begin, the two uploads, the
acquire) was SDL_GPU's own bookkeeping rather than driver work, and that a
direct Vulkan backend might recover it. On 2026-10-28 the test it asked for
was built: the full showcase gained `--dump-frame DIR`, which writes one
frame's staged bytes (the 61,960-byte frame buffer upload and the 768-byte
text cache page the clock changes), the resident text cache, the atlas, and
the draw arguments (uniforms, viewport, every batch's index range and
scissor); and `benchmarks/prototype/vk_frame.c` records that frame again on
raw Vulkan with the shipped SPIR-V shaders, the same two buffer copies into
the same buffer layouts, one render pass with the same clear and store, the
same indexed draw with the same blend state and flipped viewport, the
same clear color and target format (the lifted backdrop, BGRA8), one
submit with a fence, two frames in flight, and SDL_GPU's descriptor
convention (two vertex storage buffers, a dynamic uniform, a combined
sampler). Descriptor sets are written once at start, which is what a
native backend would do and what SDL_GPU does per bind. The shared
resources get the dependencies SDL_GPU also records: a barrier from the
previous frame's vertex reads of the text cache to this frame's copy into
it, one from the copies to vertex reads, and a render pass dependency
ordering each frame's target writes after the last frame's. When the dump
is taken on the offscreen path the showcase also reads its own target back,
and the replay compares its output with it: 0 of 1,024,000 pixels differ.
Each Vulkan call is timed the way `prof_core.h` times the SDL calls, and a
timestamp pair around the command buffer gives the GPU's execution time.
The profiled showcase and the replay ran back to back, pinned to one core,
three rounds, 1,000 measured frames each, load average about 5 with the
desktop busy; the table has the medians of the rounds, and the ranges in
the text cover the other interleaved sets of the day (load 3 to 9).

| Idle frame, same bytes and draw | SDL_GPU 3.4.18 | raw Vulkan (Mesa ANV 25.2.8) |
| --- | ---: | ---: |
| acquire command buffer / begin command buffer | 10.9 us | 8.3 us |
| two buffer uploads / two copies and two barriers | 21.2 us | 18.5 us |
| begin render pass | 24.4 us | 14.0 us |
| bind pipeline | 3.9 us | 3.4 us |
| binds, uniform push, viewport and scissor | 3.1 us | 1.8 us |
| indexed draw | 15.5 us | 11.6 us |
| end render pass / end command buffer | 2.0 us | 1.9 us |
| submit with fence (and fence release) | 56.1 us | 47.1 us |
| recording and submission together | 137 us | 107 us |
| fence wait (pacing, two in flight) | 27 to 114 us | 243 to 250 us |
| GPU execution, timestamp to timestamp | not measured | 350 to 369 us |

Two results. First, the hypothesis was wrong in size. On this call
sequence SDL_GPU's layer costs 30 to 50 us per frame across the day's
sets (31 in the table, 37 in a set at load 5, about 50 at load 9), a fifth
to a quarter of its call time and about a tenth of the frame, spread over
the submit (9 to 13 us), the pass begin (9 to 10), the draw (4 to 6), the
uploads (3), the acquire (3), and the small binds (1 to 2). The rest, 107
to 135 us, is the driver: Mesa's submit alone is 47 to 59 us (the kernel
execbuf), its render pass begin 14 to 18, its first draw 12 to 14, a
buffer copy 15 to 20. A backend that issued the same sequence directly
would save only the layer's share. That is a bound on this sequence, not
on every backend: one that also changed the frame's structure, say a
host-visible frame buffer written in place with no copy and no barrier,
would be measured separately, and nothing here says what it would save.
What this does settle is that the per-call overhead section 15 suspected
is mostly the driver, and that a second renderer is not justified by it.

Second, and larger: the GPU takes 343 to 387 us to execute this frame as
drawn (the spread follows the integrated GPU's clock under the desktop's
load: 343 at load 3, 350 to 369 in the table's rounds, up to 387 in
others). That is the whole offscreen frame of change 14 (347 us on a quiet
machine). The replay's CPU finishes in about 115 us and then waits about
245 us on the fence two frames back, and the SDL path shows the same once
the desktop quiets: in two of the three rounds its fence wait rose from
27 to 110 us as its CPU work shrank. The idle frame on this machine is
GPU-bound, and no CPU change can take it below what the GPU needs. What
the timestamp measures is this frame with these shaders on this driver,
not a hardware limit: fewer painted layers, a cheaper fragment path, or a
different driver could lower it, and none of those was measured here.
What CPU savings still buy is headroom: time for the application's own
work and lower power, not a shorter frame. Lowering the frame means
drawing less, which culling already started, or a faster GPU. The replay,
the dump with its reference readback, the readback image, and the
interleaved runs are kept under `out/prototype-2026-10-28/`; the method is
in the profiling guide.

### 18. The same frame on a Windows desktop: D3D12 and Vulkan

Changes 15 and 17 measured one machine, a four-core laptop with an
integrated GPU on Mesa. On 2026-10-28 the per-call profile became a build
target (`demo_profile`) that also compiles with MSVC, and the same idle
frame was profiled on a Windows 11 desktop: i7-12700KF, GeForce RTX 2060
(driver 32.0.16.1088, Vulkan through the NVIDIA ICD), SDL 3.4.10, MSVC
19.44, Power saver plan, mains power, other work running (a compiler was
finishing before the first run). Offscreen, 1,200 frames, immediate
present, pinned to one logical CPU (the second thread of a P-core), three
rounds alternating D3D12 and Vulkan. The table has the medians of the
rounds; the rounds agree within 2 us on every line. The two `frame:`
totals are per-frame sums with the median taken over frames, so they are
not the sums of the lines above them. The Linux column is the laptop on
the same build the same day (load 2.5), with the change 15 and 17 ranges
where they differ. Every total includes the wrapper's own 100 to 135 ns
per wrapped call, about 3 to 4 us per frame.

| Idle frame, offscreen, per-frame medians | Linux laptop, SDL_GPU Vulkan (Mesa ANV) | Windows desktop, SDL_GPU Vulkan (NVIDIA) | Windows desktop, SDL_GPU D3D12 (NVIDIA) |
| --- | ---: | ---: | ---: |
| acquire command buffer | 9 to 11 us | 2.0 us | 0.2 us |
| buffer uploads (1.3 to 1.8 calls) | 16 to 21 us | 1.3 us | 3.5 us |
| begin render pass | 20 to 24 us | 2.0 us | 3.0 us |
| bind pipeline | 3.9 us | 0.8 us | 1.5 us |
| binds, uniform push, scissor, map, unmap | about 3 us | about 1 us | about 1.5 us |
| indexed draw | 13 to 16 us | 0.6 us | 2.0 us |
| end render pass | 2.0 us | 1.0 us | 0.3 us |
| submit with fence | 47 to 56 us | 15.1 us | 34.7 us |
| frame: SDL calls except the fence wait | 119 us (137 at load 5) | 24.0 us | 47.0 us |
| frame: library exclusive | 45.6 us | 45.4 us | 43.6 us |
| of which `rg_gui_gpu_prepare` | 18.7 us | 9.9 us | 10.2 us |
| of which `rg_gui_end_frame` | 14.8 us | 27.7 us | 27.8 us |
| of which `rg_gui_gpu_stage_upload` | about 10 us | 6.8 us | 4.7 us |
| fence wait (pacing, two in flight) | 27 to 141 us | 1.9 us | 2.8 us |
| CPU per frame, the three totals together | about 300 us | about 71 us | about 93 us |

Four results. First, the desktop's frame is CPU-bound, the opposite of
the laptop's. Its fence wait is 2 to 3 us (p90 8 to 13 us on D3D12, 2 us
on Vulkan): with two frames in flight, the GPU had finished the frame two
back before the CPU asked, every frame, so the GPU's time per frame is
below the CPU's 71 to 93 us. The GPU execution time itself is not measured
on the SDL path, and the change 17 conclusion, that no CPU change can
shorten the frame, holds for the laptop and not here. On this desktop every
CPU microsecond is frame time.

Second, the backend layer plus driver is 24 us on NVIDIA Vulkan and 47 us
on D3D12 for the same calls, against 119 to 137 us on Mesa on the laptop.
The D3D12 excess is almost all the submit, 35 us against 15, with smaller
differences in the uploads (3.5 against 1.3), the draw (2.0 against 0.6),
and the pass begin (3.0 against 2.0); D3D12's acquire is the cheaper one.
How much of the 47 us is SDL_GPU's D3D12 backend and how much the NVIDIA
driver is not known: the raw replay exists only for Vulkan, and was not run
on Windows. The change 17 split (SDL_GPU's layer 30 to 50 us of 137 on
Mesa) does not carry over, since the layer's share cannot exceed 24 us on
NVIDIA Vulkan here, and the laptop's large per-call numbers were the Mesa
driver and the slower CPU more than the layer.

Third, the library's own time is now the larger half: 45 us exclusive on
the desktop, 65 percent of the Vulkan frame and 48 percent of the D3D12
frame, where on the laptop it was about a quarter. It is also, within a
microsecond, the same 45 us on both machines, and that is the finding that
needs following up. The desktop's P-core takes preparation from 18.7 to
9.9 us and staging from about 10 to 5 to 7 us, roughly the CPU and
compiler difference one would expect, but `rg_gui_end_frame` goes the other
way, 14.8 us on the laptop and 27.7 us on the desktop. In the idle scenario
that function is the hidden-draw culling pass of change 12 (nested loops
over fragments and occluders with rectangle arithmetic), an overlay merge,
and bookkeeping; nothing in it calls SDL or the OS, and the pacing depends
only on the draw list, which is the same on both machines. The candidates
are MSVC's code for the culling pass against GCC's, and the Power saver
plan's clock on a branchy loop, and they are separable: split the function
in the profiled build (a scope hook inside `rg_gui_end_frame` that the
profiled build defines and other builds compile away) and rebuild once with
clang-cl. Until that is done the 27 us is the largest unexplained item in
the desktop frame.

Fourth, the two backends paint the same picture. The D3D12 dump's readback
and the laptop's Vulkan readback of the same dumped frame index differ in
8,439 of 1,024,000 pixels, every one of them inside x 1014 to 1263, y 73 to
564: the statistics panel, whose frame-time text and plot differ between
the machines (the dumps' geometry bytes differ there and nowhere else; the
clip bytes are identical). The other 900,000 pixels, every widget and
glyph outside that panel, are byte-identical across NVIDIA D3D12 and Mesa
Vulkan. That is evidence of matching rendering, not a per-pixel proof,
since the frames' content differs; a strict check would replay the Linux
dump on D3D12, and the dump and its readback are kept for that. The six
profiles, the machine description, and the D3D12 dump are under
`out/windows-profile/`, force-added since `out/` is ignored.

The caveats: SDL 3.4.10 on the desktop against 3.4.18 on the laptop; the
Power saver plan, which may hold the desktop's clocks below what the same
runs would see on Balanced; and background work, moderate, during the
runs. The ordering of the lines would not change under any of them, the
sizes might.

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

## Level two: dirty tiles

The level-two design sketched above was built on 2026-10-23 as the opt-in
header `rg_gui_gpu_tiles.h` and a `--tile-cache` path in the full showcase.
Each drawing command is binned into 64-pixel tiles with the renderer's own
floor/ceil scissor rounding; each tile's command list is digested in order
(payload, packed color, clip in effect, glyph quads for text); tiles whose
digest differs from the committed frame become row-span rectangles; the list is
drawn once per rectangle into a persistent color target with `LOADOP_LOAD`,
and the target is blitted to the swapchain. The showcase now paints an opaque
backdrop first so the list owns every pixel.

Correctness was gated by readback: verify mode renders a full repaint every
frame and compares both targets byte for byte. Across idle, edit, scroll, and
resize, 1,600 compared frames matched, including 14 full-redraw fallbacks on
resize. An external review (Codex, 2026-10-23) found three defects in the
first version, all reproduced and fixed with regression tests: exact float
clip intersections missed pixels the integer scissor still painted; a 32-bit
text tag could collide between two strings, so the glyph quads are digested
instead; and a commit after a storage-overflow fallback could revalidate stale
digests.

Measured on the showcase, which animates a progress bar and churns statistics
text every frame, so no frame is ever fully skipped:

| Idle scenario, 1,200 measured frames | direct | tile cache |
| --- | ---: | ---: |
| tiles repainted per frame (of 260) | 260 | 14 (median) |
| frame work, Immediate mode | 1.69 to 1.84 ms | 0.97 to 0.99 ms |
| GPU busy per frame, Immediate mode (RC6 residency) | 1.78 ms | 0.99 ms |
| package energy per frame, Immediate mode (RAPL) | 24.4 mJ | 12.7 mJ |
| tile pass CPU cost | | 0.04 to 0.06 ms |

Both energy-side columns need qualification. "GPU busy" is wall time minus
the i915 RC6 residency counter: time the GPU spent outside its deepest idle
state, which includes every GPU client on the system and the cost of entering
and leaving that state, not this process's render pass alone. Package RAPL is
the whole package, CPU and GPU and whatever else was running. Neither isolates
rg_gui's pass; what they show is that in Immediate mode, where the demo is the
dominant GPU client, both moved together with the frame-work reduction and did
so in all three alternating pairs (GPU busy 1.67 to 1.85 ms against 0.95 to
1.02 ms). `benchmarks/measure_tiles.py` reproduces the measurement.

At VSync the counters tell a different story: the GPU is outside idle about
5.5 ms per 16.7 ms frame in both modes, and package energy on this shared
laptop varied 2x between identical runs. Edit showed the cache lower in 4 of 4
pairs (6.20 to 5.50 ms); idle showed no difference within noise. A plausible
explanation, not established by these measurements, is that the X11 compositor
repaints the window on every present regardless of how little changed, which
would make per-present work dominate whatever the render pass saves. So what
is demonstrated is the showcase's frame-work reduction in Immediate mode, with
the whole-system indicators moving the same way; whether any of it reaches the
battery under a compositor is not demonstrated here.

### The static case: not presenting is the lever

The `static` scenario freezes the showcase after warmup so every frame is
identical, and `--tile-cache-skip-present` makes a frame with no dirty tiles
neither draw nor present: the swapchain keeps the last image and the loop
waits for input with a 16 ms timeout. Verify mode confirmed the frozen frames
identical by readback (400 compared, 0 mismatches).

Measured on 2026-10-24 on a quiet machine (load average 0.8 to 1.05, no other
GPU application), in six alternating rounds at VSync, 600 measured frames
each, three variants in rotating order
(`benchmarks/measure_tiles.py static vsync 600 6 ...`):

| static scenario, VSync | direct | tile cache, present every frame | tile cache, skip unchanged presents |
| --- | ---: | ---: | ---: |
| frames presented (of 600) | 600 | 600 | 1 |
| average package power | 3.32 W [2.75-5.12] | 2.93 W [2.69-3.03] | 1.93 W [1.75-2.00] |
| GPU time outside RC6 idle per frame | 3.53 ms [3.49-3.86] | 3.09 ms [3.06-3.10] | 0.35 ms [0.31-0.39] |
| lower than direct | | 6/6 pairs | 6/6 pairs |

The idle desktop, measured right afterwards with nothing else running, drew
1.92 to 2.23 W, with the GPU outside idle 2.8 to 3.0 percent of the time.

- Skipping unchanged presents brought the static showcase down to the idle
  desktop's power. Repainting every frame cost about 1.2 W above idle. The
  tile cache, still presenting every frame, removed a third of that (0.4 W),
  and skipping presents removed the rest. The skip variant was also below the
  tile cache in all six rounds.
- A background burst at the start inflated the first direct round (5.12 W)
  and an idle reading taken just before the run (4.5 to 5.1 W). The medians
  and the other five direct rounds (2.75 to 3.42 W) agree.
- Power is the fair unit. The skip variant is paced by its 16 ms wait rather
  than by the swapchain (a 16.55 ms frame period against 16.71 ms), which
  biases its energy per frame low. Earlier runs on a busy machine, with
  another GPU application active, could not separate the energy at all; their
  "29 percent, 1.5 W" figure should not be quoted, and this run supersedes it.
- The GPU split agrees with the earlier runs, which measured 4.25, 3.71, and
  1.65 ms (4.61, 4.05, and 2.06 ms in an independent rerun by Codex) with more
  background activity in every column. The render pass the tile cache skips
  is about 0.4 ms of GPU activity per frame. The per-present path, which
  skipping removes, is about 2.7 ms: rg_gui's upload encoding, the full-window
  blit, the submission, the present, and whatever the compositor does with
  it. How much of that is the compositor was not isolated.

The conclusion for integrators follows from both results:
the article's level one matters more than its level two here. Detect unchanged
frames and present nothing. Both mechanisms in this library do that.
`rg_gui_gpu_frame_signature` is the cheap whole-frame test for applications
that draw straight into the swapchain; the tile cache adds partial repaints
for applications with a persistent target, whose `RG_GUI_TILE_REDRAW_NONE`
result should be treated as "do not present." A frame that changes a few tiles
still has to be presented, and on this desktop the per-present path costs more
GPU activity than the partial repaint saves. Wayland or an uncomposited session
may change that balance; it was not measured.

## Many docked panels

The `docks` scenario adds docked property panels to the showcase, 20 unless
`--dock-panels` says otherwise. They fill up to three new full-height columns
at the left, each holding an equal share of the panels as tabs, so at most
three are visible. Every column, the showcase's three included, gets the same
width: 210 px at the default window size. Each visible panel shows all seven
of its rows: a changing readout, three sliders, a checkbox, a meter, and a
button.

Another GPU application was running during these measurements, and it moved
the swapchain wait between 0.2 and 3.5 ms from run to run. Frame work is
therefore not compared, and the CPU phases use each run's 10th-percentile
frame, which preemption cannot lower. Immediate presentation, eight rotated
rounds of 1,200 measured frames, median across runs:

| Extra panels | 0 | 3 | 15 | 30 | 60 |
| --- | ---: | ---: | ---: | ---: | ---: |
| Visible, hidden extra panels | 0, 0 | 3, 0 | 3, 12 | 3, 27 | 3, 57 |
| Draw commands | 735 | 857 | 941 | 943 | 943 |
| Draw calls | 23 | 32 | 43 | 43 | 42 |
| UI build | 60.8 us | 65.9 us | 69.8 us | 72.1 us | 76.2 us |
| Prepare | 34.8 us | 39.9 us | 42.7 us | 44.0 us | 44.3 us |
| Stage | 14.0 us | 15.7 us | 17.1 us | 17.0 us | 17.2 us |

The first three panels add about 40 commands each and, per panel, about 1.7 us
of UI build, 1.7 us of preparation, and 0.6 us of staging, while the showcase
columns narrow to make room. Once the tab bars are full, hidden tabs add
almost no commands and nothing measurable to preparation or staging, only UI
build time.

Timers around the dock calls, in an instrumented build over three rotated
rounds of 10th-percentile frames, show where the UI time goes. At 60 extra
panels, per frame, dock-space layout and splitters take about 1.8 us, the six
visible windows' begin calls about 12 us, the 58 hidden tabs' begin calls
about 3.3 us, and the visible panels' own content about 4.3 us. A visible docked
window lays out its node's tab bar by measuring every tab's title each frame,
so its begin call grows by about 0.09 us per tab in its node; a hidden tab's
begin call costs about 0.06 us. Together the dock calls are about 17 us of a
76 us UI build at 60 extra panels: linear and modest, but not negligible.
Change 7 now keeps those title widths between frames.
A gprof profile was not used for this. In this binary it attributed 60 calls
per frame to the once-per-frame menu function, so its labels are unreliable.

Every draw call after the first in a frame began at a clip change, at 0, 3,
and 60 extra panels; none began at a texture or material change. Draw
encoding cost about 0.5 to 0.9 us of CPU per draw call. Change 6 moved
clipping into the shader, and every column of the table now draws in one
call.

Until 2026-10-23 the showcase's own layout left the right half of its dock
space empty. `rg_gui_dockspace_dock` always splits the first leaf, so docking
Frame Stats on the right split the Inspector's leaf, the original root leaf
was never filled, and Inspector, Workspace, and Node Graph became tabs of one
leaf. The demo now docks the center tabs first, then Frame Stats to their
right and Inspector to their left, giving the three columns its floating
layout implies. Showcase measurements in this document taken before that
change used the old layout, which drew fewer commands because Inspector was a
hidden tab.

## Remaining opportunities, in priority order

1. **Larger fonts.** Done on the rg_text side on 2026-10-22: its loader now
   builds an ASCII glyph table, a cached fallback index, and per-glyph kerning
   ranges, and aligned layout resolves each glyph once. rg_text's own paired
   benchmark improved 35 to 88 percent across every workload with identical
   output. For rg_gui this makes the no-lookup and over-4,096-glyph paths much
   cheaper; the hash tables here remain worthwhile because a non-ASCII glyph
   in rg_text is still a binary search (3 to 4x slower than the table on the
   4,096-glyph benchmark font). Raising the slot counts is the remaining knob
   for very large fonts.
2. **Text-area relayout on edit.** Done on 2026-10-24 (change 10): a
   keystroke wraps only its own paragraphs, 3 us instead of 110 us in an 8 KiB
   document of short paragraphs. A single long paragraph still wraps in full.
   The remaining per-keystroke cost is the memory traffic of comparing the
   text with the snapshot and copying text and lines into the cache; relayout
   in place would about halve it, a microsecond or two.
3. **Smaller cached glyph quads.** A changing-label frame uploads 98,304 bytes
   of 24-byte quads; a 16-byte quad would make that 65,536. Change 16 bounded
   the staging part of that at a few microseconds of a 168 us frame, and
   found the whole renderer text path for the cell to be 33 us, 26 us of it
   the layout loop that writes the quads. The quad format also touches the
   layout's stores, page memory, and the GPU's vertex reads, which were not
   measured, so this stays listed and low; the labels already fit one
   32-quad page each, so smaller pages would not help this workload.
4. **Overdraw under opaque panels.** Done on 2026-10-26 (change 12): hidden-draw
   culling drops and splits rectangles under later opaque rectangles, taking
   the showcase from 3.3 to 1.5 layers per pixel and its frame work down 24 to
   26 percent, and on 2026-10-27 (change 13) the backdrop lift merges the
   first full-viewport rectangle with the render pass clear for another tenth
   of the idle and edit frames. Disabling blending measured no change here.
   The per-call profile (change 15) then showed most of the fixed per-frame
   CPU cost inside SDL GPU entry points, about 165 us on a busy desktop for
   the minimum set of calls a frame needs, against about 60 us in the
   renderer's own stages, of which the submit is probably driver work any
   backend pays. The matched raw-Vulkan prototype (change 17, 2026-10-28)
   then measured it: SDL_GPU's layer is 30 to 50 us of a 137 to 174 us call
   total on this call sequence, the rest is the Mesa driver, and the GPU
   executes the frame as drawn in 343 to 387 us, so the offscreen idle
   frame is GPU-bound on this machine. What remains per frame is the visible fill, the fixed per-submit
   cost, and the present path; not presenting unchanged frames is the
   remaining lever, and both mechanisms for it already ship. On a Windows
   desktop with a discrete GPU (change 18, 2026-10-28) the frame is
   CPU-bound instead, 71 us on Vulkan and 93 us on D3D12, and the library's
   own 45 us is the larger half of it.
5. **`rg_gui_end_frame` under MSVC.** Open since 2026-10-28 (change 18).
   The function takes 14.8 us on the Linux laptop and 27.7 us on a much
   faster Windows desktop, while preparation and staging halve as expected.
   In the idle frame it is the hidden-draw culling pass plus bookkeeping.
   Split it in the profiled build and rebuild once with clang-cl to tell
   MSVC's code for the culling loops from the desktop's power plan; at 27 us
   it is 30 to 40 percent of that machine's CPU frame, and the largest
   unexplained item in it.
6. **The D3D12 submit.** 35 us against 15 us for the same frame on NVIDIA
   Vulkan (change 18), two thirds of the D3D12 backend's cost. Whether that
   is SDL_GPU's D3D12 backend or the driver needs a raw-D3D12 replay of the
   dumped frame, which the D3D12 dump under `out/windows-profile/` is the
   reference for; on D3D12 the submit is the only SDL_GPU line above 4 us.
## Verified and left alone

- A direct Vulkan backend issuing the same call sequence, measured on
  2026-10-28 (change 17) and not pursued. A raw-Vulkan replay of the
  identical idle frame, verified pixel-identical against the SDL_GPU path's
  readback, records and submits in 107 to 135 us against SDL_GPU's 137 to
  174 us, so the layer costs 30 to 50 us per frame, about a tenth of it;
  the driver's own submit, pass begin, draw, and copies are the rest, and
  the frame is GPU-bound at 343 to 387 us regardless. A backend with a
  different frame structure was not measured. The replay stays in
  `benchmarks/prototype/` for other drivers and platforms, where the split
  may differ: on a Windows desktop (change 18) the whole SDL_GPU Vulkan
  path is 24 us on NVIDIA, so the layer's share there is at most that, and
  the open backend question on that machine is the D3D12 submit.
- Text length in the draw command, tried on 2026-10-23 and not adopted.
  Recording the byte length at push time removes the renderer's `strlen` for
  each changing text command, an expected saving of about 2 us per 128 changing
  labels. The extended suite could not separate that from run-to-run noise.
  Builds carrying the change also measured 6 to 10% slower on image-only
  preparation in four paired runs, although image commands never reach the
  changed branch. Those builds differed in other code too, so the slowdown was
  not isolated to the branch, and it likely reflects code layout rather than
  work. The 56-byte command (change 9) now records the length of copied text
  as part of its layout change.

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
