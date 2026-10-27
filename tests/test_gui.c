// rg_gui direct rg_text integration tests

#define RG_SPRINTF_NO_ASM 1
#define RG_GUI_TEXT_MEASURE_CACHE_SIZE 256u
#define RG_GUI_TEXT_LENGTH_CACHE_SIZE 256u
#define RG_GUI_MENU_WIDTH_CACHE_SIZE 64u
#define RG_GUI_ENABLE_VIEWPORTS 1
#define RGINLINE static inline
#include "../src/rg_gui.h"

#include <float.h>
#include <math.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#include "test_gui_lookup.h"
#include "test_gui_text_lifetime.h"
#include "test_gui_text_capture.h"
#include "test_gui_text_area_cache.h"
#include "test_gui_viewport_storage.h"

static int nearly_equal(f32 a, f32 b)
{
	return fabsf(a - b) <= 0.0001f;
}

static void make_font(RgTextFont* font, RgTextGlyph glyphs[3])
{
	memset(font, 0, sizeof(*font));
	memset(glyphs, 0, sizeof(RgTextGlyph) * 3u);
	font->metrics.atlas_width = 64u;
	font->metrics.atlas_height = 32u;
	font->metrics.line_height = 10;
	font->metrics.ascent = 8;
	font->metrics.descent = 2;
	font->glyphs = glyphs;
	font->glyph_count = 3u;
	font->glyph_capacity = 3u;
	font->fallback_codepoint = '?';

	glyphs[0].codepoint = '?';
	glyphs[0].w = 4;
	glyphs[0].h = 8;
	glyphs[0].x_advance = 5;
	glyphs[1].codepoint = 'A';
	glyphs[1].w = 7;
	glyphs[1].h = 8;
	glyphs[1].x_advance = 8;
	glyphs[2].codepoint = 'M';
	glyphs[2].w = 8;
	glyphs[2].h = 8;
	glyphs[2].x_advance = 9;
}

typedef struct LargeListProbe
{
	u32 calls;
	u32 min_index;
	u32 max_index;
} LargeListProbe;

static const char* large_list_item(const void* user, u32 index)
{
	LargeListProbe* probe = (LargeListProbe*)user;
	if (probe->calls == 0u || index < probe->min_index) probe->min_index = index;
	if (probe->calls == 0u || index > probe->max_index) probe->max_index = index;
	probe->calls++;
	return "A";
}

static int test_init_sizing_and_int_parse(const RgGuiInitDesc* desc)
{
	size_t required = rg_gui_memory_required(desc);
	if (required == 0u || required != rg_gui_memory_required(desc))
	{
		fprintf(stderr, "deterministic GUI memory sizing failed\n");
		return 0;
	}

	char* allocation = (char*)malloc(required + 1u);
	if (!allocation)
	{
		return 0;
	}
	RgArena arena = {allocation + 1u, required, 0u, required};
	RgGuiContext gui;
	if (!rg_gui_init(&gui, &arena, desc) || arena.used > required)
	{
		fprintf(stderr, "reported GUI memory capacity was insufficient\n");
		free(allocation);
		return 0;
	}
	free(allocation);

	RgGuiContext invalid_ctx;
	memset(&invalid_ctx, 0x5a, sizeof(invalid_ctx));
	char invalid_memory[64];
	RgArena invalid_arena = {invalid_memory, sizeof(invalid_memory), 0u, sizeof(invalid_memory)};
	if (rg_gui_init(&invalid_ctx, &invalid_arena, NULL) || invalid_arena.used != 0u)
	{
		fprintf(stderr, "NULL init descriptor was not rejected\n");
		return 0;
	}
	RgArena null_arena = {0};
	if (rg_gui_init(&invalid_ctx, &null_arena, desc))
	{
		fprintf(stderr, "NULL arena storage was not rejected\n");
		return 0;
	}

	RgGuiInitDesc overflow_desc = *desc;
	overflow_desc.text_buffer_size = SIZE_MAX;
	if (rg_gui_memory_required(NULL) != 0u || rg_gui_memory_required(&overflow_desc) != 0u)
	{
		fprintf(stderr, "invalid GUI memory sizing input was accepted\n");
		return 0;
	}

	int parsed = 0;
	if (!rg_gui_parse_int_saturated("999999999999999999999999", &parsed) || parsed != INT_MAX ||
	    !rg_gui_parse_int_saturated("-999999999999999999999999", &parsed) || parsed != INT_MIN ||
	    !rg_gui_parse_int_saturated("+42trailing", &parsed) || parsed != 42 ||
	    rg_gui_parse_int_saturated("not-a-number", &parsed))
	{
		fprintf(stderr, "range-safe integer parsing failed\n");
		return 0;
	}

	if (RG_GUI_VERSION_MAJOR != 0 || RG_GUI_VERSION_MINOR != 1 || RG_GUI_VERSION_PATCH != 0 ||
	    strcmp(RG_GUI_VERSION_STRING, "0.1.0") != 0)
	{
		fprintf(stderr, "public version macros are inconsistent\n");
		return 0;
	}
	return 1;
}

static void make_valid_dock_layout(RgGuiDockLayout* layout,
                                   RgGuiDockLayoutNode nodes[4],
                                   RgGuiDockLayoutTab tabs[3])
{
	memset(layout, 0, sizeof(*layout));
	memset(nodes, 0, sizeof(RgGuiDockLayoutNode) * 4u);
	memset(tabs, 0, sizeof(RgGuiDockLayoutTab) * 3u);
	layout->dockspace_id = 0x1234u;
	layout->root = 1u;
	layout->node_count = 3u;
	layout->tab_count = 2u;

	nodes[1].child_a = 2u;
	nodes[1].child_b = 3u;
	nodes[1].split_ratio = 0.5f;
	nodes[1].split = RG_GUI_DOCK_SPLIT_VERT;
	nodes[2].parent = 1u;
	nodes[2].tab_head = 1u;
	nodes[2].tab_count = 2u;
	nodes[2].active_tab = 2u;
	nodes[3].parent = 1u;
	tabs[1].next = 2u;
	tabs[1].window_id = 0xa1u;
	tabs[2].window_id = 0xa2u;
}

static int dock_load_rejected_without_mutation(RgGuiContext* gui, RgGuiDockSpaceState* dockspace,
                                               const RgGuiDockLayout* layout,
                                               const RgGuiDockLayoutNode* nodes,
                                               const RgGuiDockLayoutTab* tabs)
{
	u32 node_count = gui->dock_node_count;
	u32 tab_count = gui->dock_tab_count;
	u32 root = dockspace->root;
	RgGuiDockNode saved_nodes[4];
	RgGuiDockTab saved_tabs[3];
	memcpy(saved_nodes, gui->dock_nodes, sizeof(saved_nodes));
	memcpy(saved_tabs, gui->dock_tabs, sizeof(saved_tabs));

	if (rg_gui_dock_layout_load(gui, dockspace, layout, nodes, tabs) ||
	    gui->dock_node_count != node_count || gui->dock_tab_count != tab_count ||
	    dockspace->root != root ||
	    memcmp(saved_nodes, gui->dock_nodes, sizeof(saved_nodes)) != 0 ||
	    memcmp(saved_tabs, gui->dock_tabs, sizeof(saved_tabs)) != 0)
	{
		return 0;
	}
	return 1;
}

static int test_dock_layout_validation(RgGuiContext* gui)
{
	RgGuiDockLayout layout;
	RgGuiDockLayoutNode nodes[4];
	RgGuiDockLayoutTab tabs[3];
	make_valid_dock_layout(&layout, nodes, tabs);
	RgGuiDockSpaceState dockspace;
	memset(&dockspace, 0, sizeof(dockspace));
	dockspace.id = layout.dockspace_id;

	if (!rg_gui_dock_layout_validate(&layout, nodes, tabs) ||
	    !rg_gui_dock_layout_load(gui, &dockspace, &layout, nodes, tabs) ||
	    dockspace.root != 1u || gui->dock_node_count != 3u || gui->dock_tab_count != 2u)
	{
		fprintf(stderr, "valid dock layout load failed\n");
		return 0;
	}

	RgGuiDockLayout saved_layout;
	RgGuiDockLayoutNode saved_nodes[4];
	RgGuiDockLayoutTab saved_tabs[3];
	if (!rg_gui_dock_layout_save(gui, &dockspace, &saved_layout,
	                             saved_nodes, 3u, saved_tabs, 2u) ||
	    !rg_gui_dock_layout_validate(&saved_layout, saved_nodes, saved_tabs))
	{
		fprintf(stderr, "dock layout save/validation round trip failed\n");
		return 0;
	}

	FILE* file = tmpfile();
	if (!file || !rg_gui_dock_layout_write(file, &saved_layout, saved_nodes, saved_tabs) ||
	    fflush(file) != 0)
	{
		fprintf(stderr, "dock layout write failed\n");
		if (file) fclose(file);
		return 0;
	}
	long blob_size = ftell(file);
	if (blob_size <= 0 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		return 0;
	}
	RgGuiDockLayout read_layout;
	RgGuiDockLayoutNode read_nodes[4];
	RgGuiDockLayoutTab read_tabs[3];
	if (!rg_gui_dock_layout_read(file, &read_layout, read_nodes, 3u, read_tabs, 2u) ||
	    !rg_gui_dock_layout_validate(&read_layout, read_nodes, read_tabs) ||
	    read_layout.dockspace_id != saved_layout.dockspace_id ||
	    read_layout.root != saved_layout.root ||
	    read_layout.node_count != saved_layout.node_count ||
	    read_layout.tab_count != saved_layout.tab_count ||
	    read_nodes[1].split != saved_nodes[1].split ||
	    !nearly_equal(read_nodes[1].split_ratio, saved_nodes[1].split_ratio) ||
	    read_nodes[2].active_tab != saved_nodes[2].active_tab ||
	    read_tabs[1].window_id != saved_tabs[1].window_id ||
	    read_tabs[2].window_id != saved_tabs[2].window_id)
	{
		fprintf(stderr, "dock layout file round trip failed\n");
		fclose(file);
		return 0;
	}

	u8 oversized_blob[RG_GUI_DOCK_LAYOUT_HEADER_SIZE + RG_GUI_DOCK_LAYOUT_NODE_SIZE] = {0};
	rg_bin_store_u32_le(oversized_blob + 0u, RG_GUI_DOCK_LAYOUT_MAGIC);
	rg_bin_store_u32_le(oversized_blob + 4u, RG_GUI_DOCK_LAYOUT_VERSION);
	rg_bin_store_u32_le(oversized_blob + 8u, RG_GUI_DOCK_LAYOUT_HEADER_SIZE);
	rg_bin_store_u32_le(oversized_blob + 12u, RG_GUI_MAX_DOCK_NODES + 1u);
	rg_bin_store_u32_le(oversized_blob + 20u, 1u);
	FILE* oversized = tmpfile();
	if (!oversized || fwrite(oversized_blob, 1u, sizeof(oversized_blob), oversized) != sizeof(oversized_blob) ||
	    fseek(oversized, 0, SEEK_SET) != 0 ||
	    rg_gui_dock_layout_read(oversized, &read_layout, read_nodes, UINT32_MAX, read_tabs, UINT32_MAX) ||
	    ftell(oversized) != (long)RG_GUI_DOCK_LAYOUT_HEADER_SIZE)
	{
		fprintf(stderr, "oversized dock layout was not rejected at the header boundary\n");
		if (oversized) fclose(oversized);
		fclose(file);
		return 0;
	}
	fclose(oversized);

	char* blob = (char*)malloc((size_t)blob_size);
	if (!blob || fseek(file, 0, SEEK_SET) != 0 || fread(blob, 1u, (size_t)blob_size, file) != (size_t)blob_size)
	{
		free(blob);
		fclose(file);
		return 0;
	}
	fclose(file);
	FILE* truncated = tmpfile();
	if (!truncated || fwrite(blob, 1u, (size_t)blob_size - 1u, truncated) != (size_t)blob_size - 1u ||
	    fseek(truncated, 0, SEEK_SET) != 0 ||
	    rg_gui_dock_layout_read(truncated, &read_layout, read_nodes, 3u, read_tabs, 2u))
	{
		fprintf(stderr, "truncated dock layout was accepted\n");
		free(blob);
		if (truncated) fclose(truncated);
		return 0;
	}
	fclose(truncated);

	// Split is serialized as u32; values that would truncate into a valid u8 must still fail.
	blob[RG_GUI_DOCK_LAYOUT_HEADER_SIZE + 28u] = 0u;
	blob[RG_GUI_DOCK_LAYOUT_HEADER_SIZE + 29u] = 1u;
	blob[RG_GUI_DOCK_LAYOUT_HEADER_SIZE + 30u] = 0u;
	blob[RG_GUI_DOCK_LAYOUT_HEADER_SIZE + 31u] = 0u;
	FILE* invalid_split = tmpfile();
	if (!invalid_split || fwrite(blob, 1u, (size_t)blob_size, invalid_split) != (size_t)blob_size ||
	    fseek(invalid_split, 0, SEEK_SET) != 0 ||
	    rg_gui_dock_layout_read(invalid_split, &read_layout, read_nodes, 3u, read_tabs, 2u))
	{
		fprintf(stderr, "out-of-range serialized dock split was accepted\n");
		free(blob);
		if (invalid_split) fclose(invalid_split);
		return 0;
	}
	free(blob);
	fclose(invalid_split);

	RgGuiDockLayout bad_layout = layout;
	RgGuiDockLayoutNode bad_nodes[4];
	RgGuiDockLayoutTab bad_tabs[3];
	memcpy(bad_nodes, nodes, sizeof(nodes));
	memcpy(bad_tabs, tabs, sizeof(tabs));
	bad_layout.root = 4u;
	if (!dock_load_rejected_without_mutation(gui, &dockspace, &bad_layout, bad_nodes, bad_tabs)) goto malformed;

	bad_layout = layout;
	memcpy(bad_nodes, nodes, sizeof(nodes));
	bad_nodes[1].child_a = 1u;
	if (!dock_load_rejected_without_mutation(gui, &dockspace, &bad_layout, bad_nodes, bad_tabs)) goto malformed;

	memcpy(bad_nodes, nodes, sizeof(nodes));
	bad_nodes[1].split_ratio = NAN;
	if (!dock_load_rejected_without_mutation(gui, &dockspace, &bad_layout, bad_nodes, bad_tabs)) goto malformed;

	memcpy(bad_nodes, nodes, sizeof(nodes));
	memcpy(bad_tabs, tabs, sizeof(tabs));
	bad_tabs[2].next = 1u;
	if (!dock_load_rejected_without_mutation(gui, &dockspace, &bad_layout, bad_nodes, bad_tabs)) goto malformed;

	memcpy(bad_tabs, tabs, sizeof(tabs));
	bad_nodes[2].active_tab = 0u;
	if (!dock_load_rejected_without_mutation(gui, &dockspace, &bad_layout, bad_nodes, bad_tabs)) goto malformed;

	bad_layout = layout;
	bad_layout.node_count = gui->dock_node_capacity + 1u;
	if (!dock_load_rejected_without_mutation(gui, &dockspace, &bad_layout, nodes, tabs)) goto malformed;

	return 1;

malformed:
	fprintf(stderr, "malformed dock layout mutated live state or was accepted\n");
	return 0;
}

static void count_deserialize(void* user, RgGuiId id, const u8* data, u32 size)
{
	int* count = (int*)user;
	(void)id;
	(void)data;
	(void)size;
	(*count)++;
}

static int dock_tab_text_x(RgGuiContext* gui, const char* text, f32* x)
{
	const RgGuiDrawList* list = rg_gui_draw_list(gui);
	for (u32 i = 0u; list && i < list->count; i++)
	{
		const RgGuiDrawCmd* cmd = &list->cmds[i];
		if (cmd->type == RG_GUI_CMD_TEXT && cmd->data.text.text && strcmp(cmd->data.text.text, text) == 0)
		{
			*x = cmd->data.text.pos.x;
			return 1;
		}
	}
	return 0;
}

/* A dock tab keeps its title's measured width with a copy of the bytes, so a
 * title edited in place re-measures, even to another string of the same
 * length, and a title too long for the copy is measured every frame. */
static int test_dock_tab_title_cache(RgGuiContext* gui)
{
	RgInputState input;
	rg_input_init(&input);
	input.mouse_x = -1000;
	input.mouse_y = -1000;
	RgGuiDockSpaceState dockspace;
	RgGuiWindowState first, second;
	memset(&dockspace, 0, sizeof(dockspace));
	memset(&first, 0, sizeof(first));
	memset(&second, 0, sizeof(second));
	/* Same-length changes follow each other (AA to MM, A?M to MMA) so a cache
	 * keyed on length alone would keep a stale width. */
	static const char* const titles[] = {
	    "AA", "MM", "AAAAAAAA", "A?M", "MMA", "????????????????????????????????????", "AA"};
	const u32 flags = RG_GUI_WINDOW_MOVABLE | RG_GUI_WINDOW_RESIZABLE | RG_GUI_WINDOW_DOCKABLE;
	const RgGuiRect rect = rg_gui_make_rect(20.0f, 20.0f, 300.0f, 200.0f);
	char title[48];
	f32 base_gap = 0.0f, base_width = 0.0f;
	for (u32 frame = 0u; frame < RG_ARRAY_COUNT(titles); frame++)
	{
		strcpy(title, titles[frame]);
		rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
		rg_gui_dockspace_begin(gui, &dockspace, rg_gui_make_rect(0.0f, 0.0f, 1200.0f, 400.0f), 0x7301u);
		if (frame == 0u)
		{
			rg_gui_dockspace_dock(gui, &dockspace, &first, title, 0x7302u, RG_GUI_DOCK_SLOT_CENTER);
			rg_gui_dockspace_dock(gui, &dockspace, &second, "MA", 0x7303u, RG_GUI_DOCK_SLOT_CENTER);
		}
		if (rg_gui_window_begin(gui, &first, title, rect, 50.0f, 50.0f, 4.0f, flags, 0x7302u))
			rg_gui_window_end(gui, &first);
		if (rg_gui_window_begin(gui, &second, "MA", rect, 50.0f, 50.0f, 4.0f, flags, 0x7303u))
			rg_gui_window_end(gui, &second);
		rg_gui_dockspace_end(gui, &dockspace);
		f32 first_x = 0.0f, second_x = 0.0f;
		const int found = dock_tab_text_x(gui, title, &first_x) && dock_tab_text_x(gui, "MA", &second_x);
		const f32 width = rg_gui_text_measure_prefix(gui, titles[frame], strlen(titles[frame]));
		rg_gui_end_frame(gui);
		if (!found || second_x <= first_x)
		{
			fprintf(stderr, "dock tab titles not drawn in docking order (frame %u)\n", frame);
			return 0;
		}
		if (frame == 0u)
		{
			base_gap = second_x - first_x;
			base_width = width;
		}
		else if (!nearly_equal(second_x - first_x - base_gap, width - base_width))
		{
			fprintf(stderr, "dock tab width did not follow an in-place title change (frame %u: %.3f vs %.3f)\n",
			        frame, (double)(second_x - first_x - base_gap), (double)(width - base_width));
			return 0;
		}
#if RG_GUI_DOCK_TAB_TITLE_CACHE > 0
		const size_t length = strlen(titles[frame]);
		for (u32 i = 1u; i <= gui->dock_tab_capacity; i++)
		{
			const RgGuiDockTab* tab = &gui->dock_tabs[i];
			if (tab->window != &first) continue;
			const int cached = length <= RG_GUI_DOCK_TAB_TITLE_CACHE;
			if ((tab->title_cached == (u32)length + 1u) != cached ||
			    (cached && (!nearly_equal(tab->title_width, width) ||
			                memcmp(tab->title_bytes, titles[frame], length) != 0)))
			{
				fprintf(stderr, "dock tab title cache state wrong (frame %u)\n", frame);
				return 0;
			}
			break;
		}
#endif
	}
	return 1;
}

/* The scan rg_gui_text_area_find_visual_line used before it bisected. */
static u32 reference_find_visual_line(const RgGuiTextAreaVisualLine* lines, u32 line_count, size_t cursor)
{
	if (!lines || line_count == 0u) return 0u;
	for (u32 i = 0u; i < line_count; i++)
	{
		if (cursor < lines[i].start) return i > 0u ? i - 1u : 0u;
		if (cursor < lines[i].end) return i;
		if (cursor == lines[i].end && !(lines[i].soft_wrap_after && i + 1u < line_count)) return i;
	}
	return line_count - 1u;
}

/* The bisecting lookup returns what the linear scan did for every cursor,
 * over layouts with empty lines, hard breaks, soft wraps, and gaps of zero to
 * two bytes between lines, so it holds for any lines in text order, not only
 * those the wrapper makes. */
static int test_visual_line_lookup(void)
{
	RgGuiTextAreaVisualLine lines[64];
	u32 seed = 12345u;
	u32 checked = 0u;
	for (u32 layout = 0u; layout < 2000u; layout++)
	{
		u32 count = 1u + layout % 64u;
		size_t at = 0u;
		for (u32 i = 0u; i < count; i++)
		{
			seed = seed * 1664525u + 1013904223u;
			size_t length = (seed >> 8) % 6u;
			int soft = ((seed >> 16) & 3u) == 0u;
			size_t gap = (seed >> 20) % 3u;
			lines[i].start = at;
			lines[i].end = at + length;
			lines[i].soft_wrap_after = soft;
			at = lines[i].end + gap;
		}
		for (size_t cursor = 0u; cursor <= at + 2u; cursor++)
		{
			if (rg_gui_text_area_find_visual_line(lines, count, cursor) !=
			    reference_find_visual_line(lines, count, cursor))
			{
				fprintf(stderr, "visual line lookup differs from the scan (layout %u, cursor %zu)\n",
				        layout, cursor);
				return 0;
			}
			checked++;
		}
	}
	return checked > 0u && rg_gui_text_area_find_visual_line(NULL, 4u, 0u) == 0u &&
	       rg_gui_text_area_find_visual_line(lines, 0u, 3u) == 0u;
}

/* Unpacking is exact at 0 and 1, matches the scalar path, and packing returns
 * every channel value of every channel unchanged. */

/* Hidden-draw culling must leave every pixel as it was. A reference
 * rasterizer paints each list with the GPU path's rules (pixel centers, the
 * draw offset added before the clip is rounded outwards, opaque replaces,
 * otherwise source-over) and the culled list must reproduce the original byte
 * for byte, keep every non-rectangle command in order, and never be longer
 * than the pushed list. */
#define CULL_TEST_SIZE 256
static u32 cull_test_random(u64* state)
{
	*state = *state * 6364136223846793005ull + 1442695040888963407ull;
	return (u32)(*state >> 33u);
}

static f32 cull_test_coord(u64* state, f32 low, f32 high)
{
	return low + (f32)(cull_test_random(state) % 1000u) * (high - low) / 1000.0f;
}

/* The renderers' clip intersection: near edges max, far edges min, never negative. */
static RgGuiRect cull_test_clip_intersect(RgGuiRect a, RgGuiRect b)
{
	const f32 x0 = a.x > b.x ? a.x : b.x, y0 = a.y > b.y ? a.y : b.y;
	const f32 ax1 = a.x + a.w, ay1 = a.y + a.h, bx1 = b.x + b.w, by1 = b.y + b.h;
	const f32 x1 = ax1 < bx1 ? ax1 : bx1, y1 = ay1 < by1 ? ay1 : by1;
	return rg_gui_make_rect(x0, y0, x1 > x0 ? x1 - x0 : 0.0f, y1 > y0 ? y1 - y0 : 0.0f);
}

/* The pixel rows or columns whose centers can lie in [low, high), clamped to the canvas. */
static int cull_test_span(f32 low, f32 high, int* first, int* last)
{
	if (!(high > low)) return 0;
	const f32 a = rg_floorf(low), b = rg_ceilf(high) + 1.0f;
	*first = a < 0.0f ? 0 : a > (f32)CULL_TEST_SIZE ? CULL_TEST_SIZE : (int)a;
	*last = b < 0.0f ? 0 : b > (f32)CULL_TEST_SIZE ? CULL_TEST_SIZE : (int)b;
	return *last > *first;
}

/* Paint `list` as the GPU path does under a draw offset: a rectangle covers
 * the pixel centers in the half-open quad at its edges plus the offset, the
 * clip in effect is the intersection of the pushed clips, rounded outwards to
 * whole pixels after the offset is added, and a top-level clip is used as
 * pushed, negative size and all. */
static void cull_test_raster(const RgGuiDrawList* list, u32 overlay_start, f32 ox, f32 oy, u8* rgba)
{
	memset(rgba, 0, (size_t)CULL_TEST_SIZE * CULL_TEST_SIZE * 4u);
	RgGuiRect stack[64];
	u32 depth = 0u;
	for (u32 i = 0u; i < list->count; i++)
	{
		if (i == overlay_start) depth = 0u;
		const RgGuiDrawCmd* cmd = &list->cmds[i];
		if (cmd->type == RG_GUI_CMD_CLIP_PUSH)
		{
			if (depth < 64u)
			{
				stack[depth] = depth ? cull_test_clip_intersect(stack[depth - 1u], cmd->data.clip.rect) : cmd->data.clip.rect;
				depth++;
			}
			continue;
		}
		if (cmd->type == RG_GUI_CMD_CLIP_POP) { if (depth) depth--; continue; }
		if (cmd->type != RG_GUI_CMD_RECT) continue;
		const RgGuiRect r = cmd->data.rect.rect;
		const f32 rx0 = r.x + ox, ry0 = r.y + oy, rx1 = (r.x + r.w) + ox, ry1 = (r.y + r.h) + oy;
		f32 cx0 = -1e9f, cy0 = -1e9f, cx1 = 1e9f, cy1 = 1e9f;
		if (depth)
		{
			const RgGuiRect c = stack[depth - 1u];
			cx0 = rg_floorf(c.x + ox); cy0 = rg_floorf(c.y + oy);
			cx1 = rg_ceilf(c.x + c.w + ox); cy1 = rg_ceilf(c.y + c.h + oy);
		}
		const u32 a = cmd->color >> 24u;
		const u32 src[3] = {cmd->color & 255u, (cmd->color >> 8u) & 255u, (cmd->color >> 16u) & 255u};
		int px0, px1, py0, py1;
		if (!cull_test_span(rx0, rx1, &px0, &px1) || !cull_test_span(ry0, ry1, &py0, &py1)) continue;
		for (int py = py0; py < py1; py++)
			for (int px = px0; px < px1; px++)
			{
				const f32 cx = (f32)px + 0.5f, cy = (f32)py + 0.5f;
				if (cx < rx0 || cx >= rx1 || cy < ry0 || cy >= ry1) continue;
				if (cx < cx0 || cx >= cx1 || cy < cy0 || cy >= cy1) continue;
				u8* dst = rgba + ((size_t)py * CULL_TEST_SIZE + (size_t)px) * 4u;
				if (a == 255u)
				{
					dst[0] = (u8)src[0]; dst[1] = (u8)src[1]; dst[2] = (u8)src[2]; dst[3] = 255u;
					continue;
				}
				for (u32 c = 0u; c < 3u; c++) dst[c] = (u8)((src[c] * a + dst[c] * (255u - a) + 127u) / 255u);
				dst[3] = (u8)((255u * a + dst[3] * (255u - a) + 127u) / 255u);
			}
	}
}

static int cull_test_same_rect(const RgGuiDrawCmd* cmd, f32 x, f32 y, f32 w, f32 h)
{
	return cmd->type == RG_GUI_CMD_RECT && cmd->data.rect.rect.x == x && cmd->data.rect.rect.y == y &&
	       cmd->data.rect.rect.w == w && cmd->data.rect.rect.h == h;
}

/* Random lists are drawn with a random draw offset three times in four, some
 * clips are pushed with a negative size, and one list in eight runs with its
 * x coordinates hundreds of thousands of units to the left, each rectangle by
 * a different amount so that the coordinates sit on different float grids and
 * a piece's position plus size no longer lands on its edge exactly. */
static f32 cull_test_far_shift(u64* state)
{
	static const f32 shifts[] = {1600000.0f, 1048000.0f, 600000.0f, 300000.0f};
	return shifts[cull_test_random(state) % 4u];
}

static int test_hidden_draw_culling(void)
{
	enum { CAPACITY = 128 };
	static RgGuiDrawCmd original[CAPACITY], working[CAPACITY], scratch[CAPACITY];
	static RgGuiRect clips[CAPACITY];
	static u8 expected[CULL_TEST_SIZE * CULL_TEST_SIZE * 4u], actual[CULL_TEST_SIZE * CULL_TEST_SIZE * 4u];
	u64 state = 0x5eed1234abcdull;
	u64 total_culled = 0u, total_split = 0u, total_commands = 0u, total_kept = 0u, far_split = 0u, offset_culled = 0u;
	for (u32 iteration = 0u; iteration < 3000u; iteration++)
	{
		const u32 count = 8u + cull_test_random(&state) % 53u;
		const int far = cull_test_random(&state) % 8u == 0u;
		const int offset = cull_test_random(&state) % 4u != 0u;
		const f32 ox = offset ? cull_test_coord(&state, -3.0f, 3.0f) : 0.0f;
		const f32 oy = offset ? cull_test_coord(&state, -3.0f, 3.0f) : 0.0f;
		u32 depth = 0u;
		for (u32 i = 0u; i < count; i++)
		{
			RgGuiDrawCmd cmd;
			memset(&cmd, 0, sizeof(cmd));
			const u32 roll = cull_test_random(&state) % 100u;
			if (roll < 12u)
			{
				RgGuiRect c = rg_gui_make_rect(cull_test_coord(&state, -20.0f, 220.0f), cull_test_coord(&state, -20.0f, 220.0f),
				                               cull_test_coord(&state, 0.0f, 240.0f), cull_test_coord(&state, 0.0f, 240.0f));
				if (cull_test_random(&state) % 10u == 0u) c.w = -cull_test_coord(&state, 0.0f, 2.0f);
				if (cull_test_random(&state) % 10u == 0u) c.h = -cull_test_coord(&state, 0.0f, 2.0f);
				if (far) { const f32 shift = cull_test_far_shift(&state); c.x = c.x - shift; c.w = c.w + shift; }
				cmd.type = RG_GUI_CMD_CLIP_PUSH;
				cmd.data.clip.rect = c;
				depth++;
			}
			else if (roll < 24u && depth)
			{
				cmd.type = RG_GUI_CMD_CLIP_POP;
				depth--;
			}
			else if (roll < 29u)
			{
				cmd.type = RG_GUI_CMD_TRIANGLE;
				cmd.data.triangle.a = rg_vec2(cull_test_coord(&state, 0.0f, 256.0f), cull_test_coord(&state, 0.0f, 256.0f));
				cmd.data.triangle.b = rg_vec2(cull_test_coord(&state, 0.0f, 256.0f), cull_test_coord(&state, 0.0f, 256.0f));
				cmd.data.triangle.c = rg_vec2(cull_test_coord(&state, 0.0f, 256.0f), cull_test_coord(&state, 0.0f, 256.0f));
				cmd.color = cull_test_random(&state) | 0xff000000u;
			}
			else
			{
				const int big = cull_test_random(&state) % 100u < 35u;
				RgGuiRect r = rg_gui_make_rect(cull_test_coord(&state, -20.0f, 260.0f), cull_test_coord(&state, -20.0f, 260.0f),
				                               cull_test_coord(&state, big ? 100.0f : 0.0f, big ? 290.0f : 80.0f),
				                               cull_test_coord(&state, big ? 100.0f : 0.0f, big ? 290.0f : 80.0f));
				if (far) { const f32 shift = cull_test_far_shift(&state); r.x = r.x - shift; r.w = r.w + shift; }
				const u32 alpha = cull_test_random(&state) % 100u < 45u ? 255u : cull_test_random(&state) % 255u;
				cmd.type = RG_GUI_CMD_RECT;
				cmd.data.rect.rect = r;
				cmd.color = (cull_test_random(&state) & 0x00ffffffu) | (alpha << 24u);
			}
			original[i] = cmd;
		}
		const u32 overlay_start = cull_test_random(&state) % (count + 1u);
		const u32 capacity = count + cull_test_random(&state) % 13u;
		memcpy(working, original, sizeof(*working) * count);
		RgGuiDrawList before = {original, count, count};
		RgGuiDrawList after = {working, count, capacity};
		u32 culled = 0u, split = 0u;
		u32 after_overlay = overlay_start;
		rg_gui_draw_list_cull_hidden_ex(&after, &after_overlay, clips, scratch, CAPACITY, &culled, &split);
		if (after.count > count || after_overlay > after.count)
		{
			fprintf(stderr, "hidden-draw culling: iteration %u produced %u commands from %u (overlay %u)\n", iteration, after.count, count, after_overlay);
			return 0;
		}
		cull_test_raster(&before, overlay_start, ox, oy, expected);
		cull_test_raster(&after, after_overlay, ox, oy, actual);
		if (memcmp(expected, actual, sizeof(expected)))
		{
			fprintf(stderr, "hidden-draw culling: iteration %u changed pixels (%u commands, overlay at %u, offset %g %g, far %d, culled %u, split %u)\n",
			        iteration, count, overlay_start, (double)ox, (double)oy, far, culled, split);
			return 0;
		}
		u32 j = 0u;
		for (u32 i = 0u; i < count; i++)
		{
			if (original[i].type == RG_GUI_CMD_RECT) continue;
			while (j < after.count && working[j].type == RG_GUI_CMD_RECT) j++;
			if (j >= after.count || memcmp(&working[j], &original[i], sizeof(original[i])))
			{
				fprintf(stderr, "hidden-draw culling: iteration %u lost or reordered a non-rectangle command\n", iteration);
				return 0;
			}
			j++;
		}
		total_culled += culled;
		total_split += split;
		total_commands += count;
		total_kept += after.count;
		far_split += far ? split : 0u;
		offset_culled += offset ? culled : 0u;
	}
	if (total_culled < 2000u || total_split < 200u || far_split < 10u || offset_culled < 1500u)
	{
		fprintf(stderr, "hidden-draw culling: too little coverage (culled %llu, split %llu, far splits %llu, culled under an offset %llu)\n",
		        (unsigned long long)total_culled, (unsigned long long)total_split, (unsigned long long)far_split, (unsigned long long)offset_culled);
		return 0;
	}
	/* A layered layout: the three small rectangles under the panel go, and
	 * the slots they return let the backdrop become its four margins; the
	 * panel and the widget on top stay. */
	RgGuiDrawCmd layered[7];
	memset(layered, 0, sizeof(layered));
	layered[0].type = RG_GUI_CMD_RECT; layered[0].color = 0xff202020u; layered[0].data.rect.rect = rg_gui_make_rect(0, 0, 256, 256);
	layered[1].type = RG_GUI_CMD_RECT; layered[1].color = 0xff404040u; layered[1].data.rect.rect = rg_gui_make_rect(30, 30, 40, 40);
	layered[2].type = RG_GUI_CMD_RECT; layered[2].color = 0x80ff0000u; layered[2].data.rect.rect = rg_gui_make_rect(100, 100, 40, 40);
	layered[3].type = RG_GUI_CMD_RECT; layered[3].color = 0xff404040u; layered[3].data.rect.rect = rg_gui_make_rect(150, 30, 40, 40);
	layered[4].type = RG_GUI_CMD_RECT; layered[4].color = 0xff505050u; layered[4].data.rect.rect = rg_gui_make_rect(16, 16, 224, 224);
	layered[5].type = RG_GUI_CMD_RECT; layered[5].color = 0xff606060u; layered[5].data.rect.rect = rg_gui_make_rect(60, 60, 100, 100);
	memcpy(working, layered, sizeof(*working) * 6u);
	RgGuiDrawList list = {working, 6u, CAPACITY};
	u32 culled = 0u, split = 0u, layered_overlay = 6u;
	rg_gui_draw_list_cull_hidden_ex(&list, &layered_overlay, clips, scratch, CAPACITY, &culled, &split);
	if (culled != 3u || split != 1u || list.count != 6u || layered_overlay != 6u ||
	    !cull_test_same_rect(&working[0], 0, 0, 16, 256))
	{
		fprintf(stderr, "hidden-draw culling: layered layout gave %u commands, culled %u, split %u (expected 6, 3, 1)\n", list.count, culled, split);
		return 0;
	}
	/* Two splits want six slots and three were returned: the backdrop's,
	 * which saves the whole panel area, is kept and the translucent sheet's,
	 * which saves only the small cover, is given up. */
	layered[4].type = RG_GUI_CMD_RECT; layered[4].color = 0xff505050u; layered[4].data.rect.rect = rg_gui_make_rect(8, 8, 240, 240);
	layered[5].type = RG_GUI_CMD_RECT; layered[5].color = 0x80ffffffu; layered[5].data.rect.rect = rg_gui_make_rect(20, 20, 180, 180);
	layered[6].type = RG_GUI_CMD_RECT; layered[6].color = 0xff606060u; layered[6].data.rect.rect = rg_gui_make_rect(50, 50, 100, 100);
	layered[1].data.rect.rect = rg_gui_make_rect(100, 100, 40, 40);
	layered[2].data.rect.rect = rg_gui_make_rect(150, 100, 40, 40);
	layered[3].data.rect.rect = rg_gui_make_rect(200, 100, 40, 40);
	memcpy(working, layered, sizeof(layered));
	list.count = 7u;
	layered_overlay = 7u;
	rg_gui_draw_list_cull_hidden_ex(&list, &layered_overlay, clips, scratch, CAPACITY, &culled, &split);
	if (culled != 3u || split != 1u || list.count != 7u || layered_overlay != 7u ||
	    !cull_test_same_rect(&working[0], 0, 0, 8, 256) || !cull_test_same_rect(&working[5], 20, 20, 180, 180))
	{
		fprintf(stderr, "hidden-draw culling: budgeted layout gave %u commands, culled %u, split %u (expected 7, 3, 1 with the sheet whole)\n", list.count, culled, split);
		return 0;
	}
	/* The review's far case: the occluder's far edge, -1048399.9375, cannot be
	 * rebuilt from the backdrop's position plus a width on the coarser grid
	 * around 1.6 million, so the split must be abandoned and both rectangles
	 * kept as pushed. */
	RgGuiDrawCmd far_case[2];
	memset(far_case, 0, sizeof(far_case));
	far_case[0].type = RG_GUI_CMD_RECT; far_case[0].color = 0xff0000ffu; far_case[0].data.rect.rect = rg_gui_make_rect(-1600000.0f, 0, 1600200.5f, 200);
	far_case[1].type = RG_GUI_CMD_RECT; far_case[1].color = 0xffff0000u; far_case[1].data.rect.rect = rg_gui_make_rect(-1600000.0f, 0, 551600.0625f, 200);
	memcpy(working, far_case, sizeof(far_case));
	RgGuiDrawList far_list = {working, 2u, CAPACITY};
	RgGuiDrawList far_before = {far_case, 2u, 2u};
	u32 far_overlay = 2u;
	rg_gui_draw_list_cull_hidden_ex(&far_list, &far_overlay, clips, scratch, CAPACITY, &culled, &split);
	cull_test_raster(&far_before, 2u, 0.0f, 0.0f, expected);
	cull_test_raster(&far_list, far_overlay, 0.0f, 0.0f, actual);
	if (memcmp(expected, actual, sizeof(expected)) || far_list.count != 2u || split != 0u)
	{
		fprintf(stderr, "hidden-draw culling: the far case gave %u commands, split %u, pixels %s\n", far_list.count, split,
		        memcmp(expected, actual, sizeof(expected)) ? "changed" : "unchanged");
		return 0;
	}
	printf("Hidden-draw culling: 3000 random lists matched the reference (%llu of %llu commands kept, %llu culled, %llu split, %llu far splits)\n",
	       (unsigned long long)total_kept, (unsigned long long)total_commands, (unsigned long long)total_culled, (unsigned long long)total_split,
	       (unsigned long long)far_split);
	return 1;
}

static int test_unpack_color(void)
{
	for (u32 channel = 0u; channel < 4u; channel++)
	{
		for (u32 value = 0u; value < 256u; value++)
		{
			const u32 others[] = {0x00000000u, 0xffffffffu, 0x80402010u};
			for (u32 k = 0u; k < 3u; k++)
			{
				u32 packed = (others[k] & ~(0xffu << (channel * 8u))) | (value << (channel * 8u));
				rg_vec4 color = rg_gui_unpack_color(packed);
				rg_vec4 scalar = rg_gui_unpack_color_scalar(packed);
				if (memcmp(color.data, scalar.data, sizeof(color.data)) != 0 ||
				    rg_gui_pack_color(color) != packed ||
				    color.data[channel] != (f32)value / 255.0f)
				{
					fprintf(stderr, "unpacked color differs for 0x%08x\n", packed);
					return 0;
				}
			}
		}
	}
	rg_vec4 white = rg_gui_unpack_color(0xffffffffu);
	rg_vec4 clear = rg_gui_unpack_color(0u);
	return white.x == 1.0f && white.y == 1.0f && white.z == 1.0f && white.w == 1.0f &&
	       clear.x == 0.0f && clear.y == 0.0f && clear.z == 0.0f && clear.w == 0.0f;
}

/* The packed command color is the same with or without SSE2: random colors,
 * every channel value near each 8-bit boundary, and NaN, infinities, signed
 * zeros, denormals, and out-of-range values. */
static int test_pack_color(void)
{
	static const f32 specials[] = {0.0f, -0.0f, 1.0f, -1.0f, 0.5f, 2.0f, 1e-45f, -1e-45f,
	                               1.0f / 510.0f, 0.99999994f, 1.00000012f, 1e30f, -1e30f};
	f32 values[256 * 3 + sizeof(specials) / sizeof(specials[0]) + 3];
	u32 count = 0u;
	for (u32 k = 0u; k < 256u; k++)
	{
		f32 boundary = ((f32)k + 0.5f) / 255.0f;
		values[count++] = boundary;
		values[count++] = nextafterf(boundary, 0.0f);
		values[count++] = nextafterf(boundary, 2.0f);
	}
	for (u32 i = 0u; i < sizeof(specials) / sizeof(specials[0]); i++) values[count++] = specials[i];
	values[count++] = NAN;
	values[count++] = INFINITY;
	values[count++] = -INFINITY;
	for (u32 i = 0u; i < count; i++)
	{
		for (u32 channel = 0u; channel < 4u; channel++)
		{
			rg_vec4 color = rg_gui_color(0.25f, 0.5f, 0.75f, 1.0f);
			color.data[channel] = values[i];
			if (rg_gui_pack_color(color) != rg_gui_pack_color_scalar(color))
			{
				fprintf(stderr, "packed color differs for %a in channel %u\n", (double)values[i], channel);
				return 0;
			}
		}
	}
	u32 seed = 99u;
	for (u32 i = 0u; i < 1000000u; i++)
	{
		rg_vec4 color;
		for (u32 channel = 0u; channel < 4u; channel++)
		{
			seed = seed * 1664525u + 1013904223u;
			color.data[channel] = (f32)(seed >> 8) / (f32)(1u << 23) * 1.5f - 0.25f;
		}
		if (rg_gui_pack_color(color) != rg_gui_pack_color_scalar(color))
		{
			fprintf(stderr, "packed color differs for a random color (%u)\n", i);
			return 0;
		}
	}
	return rg_gui_pack_color(rg_gui_color(1.0f, 0.0f, 0.5f, 1.0f)) == 0xff8000ffu;
}

static int test_node_graph_bundle_validation(void)
{
	RgGuiNodeGraph graph;
	RgGuiNodeGraphBundle bundle;
	memset(&graph, 0, sizeof(graph));
	memset(&bundle, 0, sizeof(bundle));
	bundle.node_count = 1u;
	bundle.nodes[0].id = 0x41u;
	bundle.nodes[0].size = rg_vec2(100.0f, 60.0f);
	int deserialize_count = 0;
	if (rg_gui_node_graph_bundle_apply(&graph, &bundle, rg_vec2(0.0f, 0.0f),
	                                   NULL, count_deserialize, &deserialize_count,
	                                   NULL, 0u, NULL, 0u, NULL) != 1u ||
	    graph.node_count != 1u || deserialize_count != 0)
	{
		fprintf(stderr, "zero-size/null bundle payload handling failed\n");
		return 0;
	}
	u8 valid_payload[2] = {7u, 9u};
	bundle.nodes[0].id = 0x42u;
	bundle.payload = valid_payload;
	bundle.payload_capacity = 2u;
	bundle.payload_bytes = 2u;
	bundle.payload_offset[0] = 0u;
	bundle.payload_size[0] = 2u;
	if (rg_gui_node_graph_bundle_apply(&graph, &bundle, rg_vec2(0.0f, 0.0f),
	                                   NULL, count_deserialize, &deserialize_count,
	                                   NULL, 0u, NULL, 0u, NULL) != 1u ||
	    graph.node_count != 2u || deserialize_count != 1)
	{
		fprintf(stderr, "valid bundle payload handling failed\n");
		return 0;
	}

	memset(&graph, 0, sizeof(graph));
	bundle.node_count = RG_GUI_NODE_GRAPH_MAX_NODES + 1u;
	RgGuiId rejected_ids[2] = {0xaaaa, 0xbbbb};
	u32 rejected_link_count = 99u;
	if (rg_gui_node_graph_bundle_apply(&graph, &bundle, rg_vec2(0.0f, 0.0f),
	                                   NULL, NULL, NULL, rejected_ids,
	                                   RG_ARRAY_COUNT(rejected_ids), NULL, 0u,
	                                   &rejected_link_count) != 0u ||
	    graph.node_count != 0u || rejected_ids[0] != 0u || rejected_ids[1] != 0u ||
	    rejected_link_count != 0u)
	{
		fprintf(stderr, "oversized node bundle was accepted or left stale outputs\n");
		return 0;
	}

	bundle.node_count = 1u;
	u8 payload[4] = {1u, 2u, 3u, 4u};
	bundle.payload = payload;
	bundle.payload_capacity = 4u;
	bundle.payload_bytes = 4u;
	bundle.payload_offset[0] = 3u;
	bundle.payload_size[0] = UINT32_MAX;
	if (rg_gui_node_graph_bundle_apply(&graph, &bundle, rg_vec2(0.0f, 0.0f),
	                                   NULL, NULL, NULL, NULL, 0u, NULL, 0u, NULL) != 0u ||
	    graph.node_count != 0u)
	{
		fprintf(stderr, "overflowing bundle payload range was accepted\n");
		return 0;
	}

	bundle.payload = NULL;
	bundle.payload_capacity = 0u;
	bundle.payload_bytes = 0u;
	bundle.payload_offset[0] = 0u;
	bundle.payload_size[0] = 0u;
	bundle.node_count = 2u;
	graph.node_count = RG_GUI_NODE_GRAPH_MAX_NODES - 1u;
	u32 before = graph.node_count;
	if (rg_gui_node_graph_bundle_apply(&graph, &bundle, rg_vec2(0.0f, 0.0f),
	                                   NULL, NULL, NULL, NULL, 0u, NULL, 0u, NULL) != 0u ||
	    graph.node_count != before)
	{
		fprintf(stderr, "near-full graph append was not atomic\n");
		return 0;
	}

	memset(&graph, 0, sizeof(graph));
	memset(&bundle, 0, sizeof(bundle));
	graph.node_count = 1u;
	graph.link_count = RG_GUI_NODE_GRAPH_MAX_LINKS;
	bundle.node_count = 1u;
	bundle.link_count = 1u;
	bundle.nodes[0].id = 0x43u;
	if (rg_gui_node_graph_bundle_apply(&graph, &bundle, rg_vec2(0.0f, 0.0f),
	                                   NULL, NULL, NULL, NULL, 0u, NULL, 0u, NULL) != 0u ||
	    graph.node_count != 1u || graph.link_count != RG_GUI_NODE_GRAPH_MAX_LINKS)
	{
		fprintf(stderr, "near-full graph link append was not atomic\n");
		return 0;
	}
	return 1;
}

static int test_integer_widget_boundaries(RgGuiContext* gui)
{
	int rounded = 77;
	if (!rg_gui_round_f64_to_int_saturated(0.5, &rounded) || rounded != 1 ||
	    !rg_gui_round_f64_to_int_saturated(-0.5, &rounded) || rounded != -1 ||
	    !rg_gui_round_f64_to_int_saturated(HUGE_VAL, &rounded) || rounded != INT_MAX ||
	    !rg_gui_round_f64_to_int_saturated(-HUGE_VAL, &rounded) || rounded != INT_MIN)
	{
		fprintf(stderr, "saturated integer rounding failed\n");
		return 0;
	}
	rounded = 77;
	if (rg_gui_round_f64_to_int_saturated(NAN, &rounded) || rounded != 77 ||
	    rg_gui_u32_count_to_int(UINT32_MAX) != INT_MAX ||
	    rg_gui_nonnegative_f32_to_u32_bounded((f32)HUGE_VAL) != UINT32_MAX ||
	    rg_gui_nonnegative_f32_to_u32_bounded((f32)NAN) != 0u ||
	    rg_gui_trunc_f32_to_int_saturated((f32)HUGE_VAL) != INT_MAX ||
	    rg_gui_trunc_f32_to_int_saturated((f32)-HUGE_VAL) != INT_MIN ||
	    rg_gui_trunc_f32_to_int_saturated((f32)NAN) != 0 ||
	    rg_gui_row_cache_block_count(UINT32_MAX) !=
	        UINT32_MAX / RG_GUI_ROW_CACHE_BLOCK + 1u ||
	    rg_gui_scroll_index_from_ratio(NAN, INT_MAX) != 0 ||
	    rg_gui_scroll_index_from_ratio((f32)HUGE_VAL, INT_MAX) != INT_MAX ||
	    rg_gui_scroll_index_from_ratio(0.5f, INT_MAX) != 1073741824 ||
	    rg_gui_int_step_clamped(INT_MAX, (i64)INT_MAX, INT_MAX, 1,
	                            INT_MIN, INT_MAX) != INT_MAX ||
	    rg_gui_int_step_clamped(INT_MIN, (i64)INT_MIN, INT_MAX, -1,
	                            INT_MIN, INT_MAX) != INT_MIN ||
	    rg_gui_int_step_clamped(0, INT64_MIN, INT_MAX, 1,
	                            INT_MIN, INT_MAX) != INT_MAX ||
	    rg_gui_int_step_clamped(0, INT64_MIN, INT_MAX, -1,
	                            INT_MIN, INT_MAX) != INT_MIN ||
	    rg_gui_int_step_clamped(0, 1, INT_MAX, 1,
	                            INT_MIN, INT_MAX) != INT_MAX ||
	    rg_gui_int_step_clamped(0, 2, INT_MAX, -1,
	                            INT_MIN, INT_MAX) != INT_MIN ||
	    rg_gui_repeat_count_bounded((f32)HUGE_VAL, 0.01f) != RG_GUI_REPEAT_MAX_PER_FRAME ||
	    rg_gui_wheel_steps((f32)HUGE_VAL) != RG_GUI_REPEAT_MAX_PER_FRAME ||
	    rg_gui_wheel_steps((f32)-HUGE_VAL) != -RG_GUI_REPEAT_MAX_PER_FRAME ||
	    rg_gui_wheel_steps((f32)NAN) != 0)
	{
		fprintf(stderr, "bounded integer step or repeat handling failed\n");
		return 0;
	}

	RgInputState input;
	rg_input_init(&input);
	input.mouse_x = -1000;
	input.mouse_y = -1000;
	int range_min = INT_MIN;
	int range_max = INT_MAX;
	int slider_value = INT_MAX;
	int stepper_value = 0;
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	(void)rg_gui_slider_range_int(gui, "Range", &range_min, &range_max,
	                              INT_MIN, INT_MAX,
	                              rg_gui_make_rect(0.0f, 0.0f, 420.0f, 28.0f),
	                              0x7201u);
	(void)rg_gui_slider_int(gui, "Value", &slider_value, INT_MIN, INT_MAX,
	                        rg_gui_make_rect(0.0f, 32.0f, 420.0f, 28.0f),
	                        0x7202u);
	(void)rg_gui_stepper_int(gui, "Step", &stepper_value, INT_MIN, INT_MAX,
	                         INT_MIN, rg_gui_make_rect(0.0f, 64.0f, 420.0f, 28.0f),
	                         0x7203u);
	rg_gui_end_frame(gui);
	if (range_min != INT_MIN || range_max != INT_MAX ||
	    slider_value != INT_MAX || stepper_value != 0)
	{
		fprintf(stderr, "idle full-range integer widgets changed values\n");
		return 0;
	}

	int drag_value = 0;
	rg_input_init(&input);
	input.mouse_x = 4;
	input.mouse_y = 10;
	input.current_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	(void)rg_gui_input_int_drag(gui, "Drag", &drag_value, INT_MIN, INT_MAX,
	                            (f32)HUGE_VAL,
	                            rg_gui_make_rect(0.0f, 0.0f, 420.0f, 28.0f),
	                            0x7204u);
	rg_gui_end_frame(gui);
	input.previous_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	input.mouse_x = 100;
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	(void)rg_gui_input_int_drag(gui, "Drag", &drag_value, INT_MIN, INT_MAX,
	                            (f32)HUGE_VAL,
	                            rg_gui_make_rect(0.0f, 0.0f, 420.0f, 28.0f),
	                            0x7204u);
	rg_gui_end_frame(gui);
	if (drag_value != INT_MAX)
	{
		fprintf(stderr, "non-finite integer drag did not saturate\n");
		return 0;
	}

	LargeListProbe probe = {0};
	int selected = 0;
	int scroll = 0;
	rg_input_init(&input);
	input.mouse_x = 400;
	input.mouse_y = 10000;
	input.current_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	input.previous_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	RgGuiId list_id = 0x7205u;
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	gui->active_id = rg_gui_id_combine(list_id, 1u);
	(void)rg_gui_list_virtual(gui, large_list_item, &probe, UINT32_MAX, &selected, &scroll,
	                          rg_gui_make_rect(0.0f, 0.0f, 320.0f, 96.0f), list_id);
	rg_gui_end_frame(gui);
	if (scroll < INT_MAX - 1024 || selected != 0 || probe.calls == 0u ||
	    probe.max_index >= (u32)INT_MAX)
	{
		fprintf(stderr, "large virtual-list scrollbar bounds failed\n");
		return 0;
	}

	memset(&probe, 0, sizeof(probe));
	selected = INT_MAX - 2;
	scroll = INT_MAX;
	rg_input_init(&input);
	input.mouse_x = 8;
	input.mouse_y = 8;
	input.mouse_scroll_y = -1.0f;
	input.current_keyboard[SDL_SCANCODE_DOWN] = true;
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	gui->focus_id = list_id;
	gui->scroll_owner = list_id;
	(void)rg_gui_list_virtual(gui, large_list_item, &probe, UINT32_MAX, &selected, &scroll,
	                          rg_gui_make_rect(0.0f, 0.0f, 320.0f, 96.0f), list_id);
	rg_gui_end_frame(gui);
	if (scroll < 0 || scroll > INT_MAX || selected != INT_MAX - 1 ||
	    probe.calls == 0u || probe.max_index >= (u32)INT_MAX)
	{
		fprintf(stderr, "large virtual-list wheel/navigation bounds failed\n");
		return 0;
	}

	RgGuiPanelState panel;
	memset(&panel, 0, sizeof(panel));
	panel.scroll_y = FLT_MAX;
	u32 first_row = 0u;
	u32 visible_rows = 0u;
	rg_input_init(&input);
	input.mouse_x = -1000;
	input.mouse_y = -1000;
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	rg_gui_panel_begin_virtual(gui, &panel, rg_gui_make_rect(0.0f, 0.0f, 320.0f, 96.0f),
	                           20.0f, 0.0f, UINT32_MAX, 0x7206u,
	                           &first_row, &visible_rows);
	rg_gui_panel_end(gui, &panel);
	rg_gui_end_frame(gui);
	if (first_row >= UINT32_MAX || visible_rows == 0u ||
	    visible_rows > UINT32_MAX - first_row)
	{
		fprintf(stderr, "large virtual-panel bounds failed\n");
		return 0;
	}

	rg_input_init(&input);
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	gui->mouse_pos = rg_vec2(nextafterf(1.0f, 0.0f), 0.5f);
	int line_index = rg_gui_plot_lines_internal(
	    gui, NULL, NULL, NULL, UINT32_MAX, 0.0f, 1.0f,
	    rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f));
	int histogram_index = rg_gui_plot_histogram_internal(
	    gui, NULL, NULL, NULL, UINT32_MAX, 0.0f, 1.0f,
	    rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f));
	rg_gui_end_frame(gui);
	if (line_index < INT_MAX - 1024 || histogram_index < INT_MAX - 1024)
	{
		fprintf(stderr, "large plot hover-index bounds failed\n");
		return 0;
	}
	return 1;
}

static int test_viewport_and_input_router(RgGuiContext* gui)
{
	const RgGuiId viewport_id = 0x7101u;
	const RgGuiId other_id = 0x7102u;
	RgInputState raw_input;
	rg_input_init(&raw_input);
	raw_input.current_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	raw_input.current_keyboard[SDL_SCANCODE_A] = true;

	RgGuiInputRouter router;
	rg_gui_input_router_init(&router);
	rg_gui_input_router_begin(&router, &raw_input, rg_vec2(125.0f, 235.0f),
	                          viewport_id, viewport_id, 1);
	RgInputState viewport_input;
	RgInputState other_input;
	if (!rg_gui_input_router_route(&router, viewport_id, rg_vec2(100.0f, 200.0f),
	                               80, 80, &viewport_input) ||
	    viewport_input.mouse_x != 25 || viewport_input.mouse_y != 35 ||
	    !viewport_input.current_mouse[RG_MOUSE_BUTTON_LEFT] ||
	    !viewport_input.current_keyboard[SDL_SCANCODE_A] ||
	    rg_gui_input_router_capture(&router) != viewport_id ||
	    rg_gui_input_router_focus(&router) != viewport_id)
	{
		fprintf(stderr, "viewport input routing or capture failed\n");
		return 0;
	}
	(void)rg_gui_input_router_route(&router, other_id, rg_vec2(0.0f, 0.0f),
	                                300, 300, &other_input);
	if (other_input.current_mouse[RG_MOUSE_BUTTON_LEFT] ||
	    other_input.current_keyboard[SDL_SCANCODE_A])
	{
		fprintf(stderr, "captured viewport input leaked to another viewport\n");
		return 0;
	}
	rg_gui_input_router_end(&router);

	rg_gui_input_router_begin(&router, &raw_input, rg_vec2(FLT_MAX, -FLT_MAX),
	                          viewport_id, 0u, 0);
	(void)rg_gui_input_router_route(&router, viewport_id, rg_vec2(-FLT_MAX, FLT_MAX),
	                                80, 80, &viewport_input);
	if (viewport_input.mouse_x != INT_MAX || viewport_input.mouse_y != INT_MIN ||
	    viewport_input.mouse_delta_y != INT_MIN)
	{
		fprintf(stderr, "captured viewport extreme coordinate saturation failed\n");
		return 0;
	}
	rg_gui_input_router_begin(&router, &raw_input, rg_vec2(-FLT_MAX, FLT_MAX),
	                          viewport_id, 0u, 0);
	(void)rg_gui_input_router_route(&router, viewport_id, rg_vec2(FLT_MAX, -FLT_MAX),
	                                80, 80, &viewport_input);
	if (viewport_input.mouse_x != INT_MIN || viewport_input.mouse_y != INT_MAX ||
	    viewport_input.mouse_delta_x != INT_MIN || viewport_input.mouse_delta_y != INT_MAX)
	{
		fprintf(stderr, "captured viewport extreme delta saturation failed\n");
		return 0;
	}

	raw_input.previous_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	raw_input.current_mouse[RG_MOUSE_BUTTON_LEFT] = false;
	rg_gui_input_router_begin(&router, &raw_input, rg_vec2(500.0f, 500.0f),
	                          viewport_id, 0u, 0);
	(void)rg_gui_input_router_route(&router, viewport_id, rg_vec2(100.0f, 200.0f),
	                                80, 80, &viewport_input);
	rg_gui_input_router_end(&router);
	if (rg_gui_input_router_capture(&router) != 0u)
	{
		fprintf(stderr, "viewport input capture did not release\n");
		return 0;
	}

	RgInputEvent event_storage[4];
	char event_text[16];
	RgInputEventQueue events;
	rg_input_event_queue_init(&events, event_storage, RG_ARRAY_COUNT(event_storage),
	                          event_text, sizeof(event_text));
	rg_input_event_queue_reset(&events, SDL_KMOD_NONE);
	rg_gui_begin_frame_ex(gui, &viewport_input, &events, 11u, 1.0f / 60.0f);
	RgGuiViewport* viewport = rg_gui_viewport_begin_ordered_ex(
	    gui, viewport_id, rg_gui_make_rect(0.0f, 0.0f, 320.0f, 240.0f),
	    &viewport_input, &events, 22u, rg_vec2(100.0f, 200.0f));
	if (!viewport)
	{
		fprintf(stderr, "secondary viewport allocation failed\n");
		return 0;
	}
	rg_gui_push_rect(gui, rg_gui_make_rect(1.0f, 2.0f, 3.0f, 4.0f),
	                 rg_gui_color(1.0f, 0.0f, 0.0f, 1.0f));
	rg_gui_viewport_end(gui, viewport);
	const RgGuiDrawList* list = rg_gui_viewport_draw_list(viewport);
	const RgGuiPlatformOutput* output = rg_gui_viewport_platform_output(viewport);
	if (rg_gui_viewport_count(gui) != 1u || rg_gui_viewport_at(gui, 0u) != viewport ||
	    !list || list->count != 1u || rg_gui_viewport_overlay_start(viewport) > list->count ||
	    !output || output->cursor != RG_GUI_CURSOR_DEFAULT || gui->viewport_id != 0u ||
	    !nearly_equal(gui->viewport_origin.x, 0.0f) ||
	    !nearly_equal(gui->viewport_origin.y, 0.0f))
	{
		fprintf(stderr, "secondary viewport lifecycle state was not restored\n");
		return 0;
	}
	rg_gui_end_frame(gui);

	rg_gui_viewport_release(gui, viewport_id);
	rg_gui_begin_frame_ex(gui, &viewport_input, &events, 11u, 1.0f / 60.0f);
	if (rg_gui_viewport_count(gui) != 0u)
	{
		fprintf(stderr, "released viewport remained active in the next frame\n");
		return 0;
	}
	viewport = rg_gui_viewport_begin(gui, viewport_id,
	                                 rg_gui_make_rect(0.0f, 0.0f, 64.0f, 64.0f),
	                                 &viewport_input);
	if (!viewport)
	{
		fprintf(stderr, "released viewport slot was not reusable\n");
		return 0;
	}
	rg_gui_viewport_end(gui, viewport);
	rg_gui_end_frame(gui);
	rg_gui_viewport_release(gui, viewport_id);
	return 1;
}

// Retain the previous prefix-measuring implementation as an independent oracle
// for wrap decisions, including malformed UTF-8 and unusual font metrics.
static size_t reference_wrap_line_end(const RgGuiContext* ctx, const char* buffer,
                                      size_t start, size_t hard_end, f32 wrap_width)
{
	if (!ctx || !buffer || start >= hard_end) return start;
	if (wrap_width <= 0.0f) return rg_gui_utf8_next_boundary(buffer, hard_end, start);
	size_t best = start;
	size_t last_space = start;
	int has_space = 0;
	for (size_t i = start; i < hard_end; i = rg_gui_utf8_next_boundary(buffer, hard_end, i))
	{
		if (buffer[i] == ' ' || buffer[i] == '\t')
		{
			last_space = i;
			has_space = 1;
		}
		size_t candidate = rg_gui_utf8_next_boundary(buffer, hard_end, i);
		f32 width = rg_gui_text_area_measure_range(ctx, buffer + start, candidate - start);
		if (width <= wrap_width)
		{
			best = candidate;
			continue;
		}
		if (has_space && last_space > start) return last_space + 1u;
		if (best > start) return best;
		return candidate;
	}
	return hard_end;
}

static int test_incremental_text_wrapping(void)
{
	RgGuiContext gui;
	memset(&gui, 0, sizeof(gui));
	RgTextFont font;
	RgTextGlyph glyphs[3];
	make_font(&font, glyphs);
	RgTextKerning kernings[] = {
		{'A', 'A', -3}, {'A', '?', 2}, {'?', 'A', -7},
		{'M', 'A', -20}, {'A', 'M', 5}, {'?', '?', 1}
	};
	font.kernings = kernings;
	font.kerning_count = (u32)RG_ARRAY_COUNT(kernings);
	static const char* cases[] = {
		"", "AAAAAAAAAAAAAAAAAAAAAAAAAAAA", "AMAMAMAM", " A  M\tA ",
		"A\rM\nA\r\nM", "A\xc3\xa9" "M", "A\xf0\x9f\x98\x80" "A",
		"A\xff\x80\xbf" "M", "\xc0\xaf" "A\xed\xa0\x80" "M",
		"A\xf4\x90\x80\x80" "M", "A\xe2\x82", "\x80\xbf" "A"
	};
	const f32 heights[] = {-INFINITY, -10.0f, -0.0f, 0.7f, 10.0f, 20.0f, FLT_MAX, INFINITY, NAN};
	const f32 widths[] = {-INFINITY, -1.0f, 0.0f, 0.25f, 4.0f, 8.0f, 17.0f, 31.0f, FLT_MAX, INFINITY, NAN};
	for (u32 variant = 0u; variant < 4u; variant++)
	{
		gui.font = variant == 3u ? NULL : &font;
		font.fallback_codepoint = variant == 1u ? UINT32_MAX : '?';
		glyphs[1].x_advance = variant == 2u ? -8 : 8;
		for (u32 h = 0u; h < RG_ARRAY_COUNT(heights); h++)
		{
			gui.style.text_height = heights[h];
			for (u32 w = 0u; w < RG_ARRAY_COUNT(widths); w++)
			{
				for (u32 c = 0u; c < RG_ARRAY_COUNT(cases); c++)
				{
					const char* text = cases[c];
					// Every byte length exercises truncated multibyte sequences.
					for (size_t length = 0u; length <= strlen(text); length++)
					{
						for (size_t start = 0u; ; start = rg_gui_utf8_next_boundary(text, length, start))
						{
							size_t expected = reference_wrap_line_end(&gui, text, start, length, widths[w]);
							size_t actual = rg_gui_text_area_wrap_line_end(&gui, text, start, length, widths[w]);
							if (actual != expected)
							{
								fprintf(stderr, "incremental wrap differs: font=%u height=%u width=%u case=%u range=%zu..%zu: %zu != %zu\n",
								        variant, h, w, c, start, length, actual, expected);
								return 0;
							}
							if (start == length) break;
						}
					}
				}
			}
		}
	}
	return 1;
}

static void text_area_test_key(RgInputEventQueue* events, SDL_Scancode key, SDL_Keymod modifiers)
{
	RgInputEvent* event = rg_input_event_queue_push(events);
	event->kind = RG_INPUT_EVENT_KEY_DOWN;
	event->data.key.scancode = key;
	event->modifiers = modifiers;
}

static int test_text_area_layout_edits(RgGuiContext* gui)
{
	RgGuiStyle saved_style = gui->style;
	gui->style.padding = 0.0f;
	gui->style.border_thickness = 0.0f;
	gui->style.scroll_bar_width = 0.0f;
	gui->style.text_height = 10.0f;
	RgGuiTextAreaState area = {0};
	const RgGuiId id = 0x7311u;
	RgGuiRect rect = rg_gui_make_rect(0.0f, 0.0f, 16.0f, 200.0f);
	char buffer[64] = "AAAAAAAA";
	RgInputState input;
	rg_input_init(&input);
	input.mouse_x = -100;
	input.mouse_y = -100;
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	gui->focus_id = id;
	gui->text_edit_state->active = 0;
	(void)rg_gui_text_area(gui, &area, buffer, sizeof(buffer), rect, id);
	rg_gui_end_frame(gui);
	rg_gui_text_edit_set_cursor(gui->text_edit_state, 1u, 0);

	RgInputEvent event_storage[8];
	char event_text[32];
	RgInputEventQueue events;
	rg_input_event_queue_init(&events, event_storage, RG_ARRAY_COUNT(event_storage), event_text, sizeof(event_text));
	text_area_test_key(&events, SDL_SCANCODE_DOWN, SDL_KMOD_NONE);
	text_area_test_key(&events, SDL_SCANCODE_DOWN, SDL_KMOD_NONE);
	text_area_test_key(&events, SDL_SCANCODE_LEFT, SDL_KMOD_LSHIFT);
	RgInputEvent* replacement = rg_input_event_queue_push_text(&events, "M");
	replacement->kind = RG_INPUT_EVENT_TEXT_INPUT;
	text_area_test_key(&events, SDL_SCANCODE_UP, SDL_KMOD_NONE);
	text_area_test_key(&events, SDL_SCANCODE_UP, SDL_KMOD_NONE);
	RgInputEvent* preedit = rg_input_event_queue_push_text(&events, "AA");
	preedit->kind = RG_INPUT_EVENT_TEXT_EDITING;
	rg_gui_begin_frame_ex(gui, &input, &events, 0u, 1.0f / 60.0f);
	int changed = rg_gui_text_area(gui, &area, buffer, sizeof(buffer), rect, id);
	rg_gui_end_frame(gui);
	if (!changed || strcmp(buffer, "AAAAMAAA") != 0 || gui->text_edit_state->cursor != 2u ||
	    gui->text_edit_state->preedit_length != 2u || !nearly_equal(area.panel.content_height, 60.0f))
	{
		fprintf(stderr, "text area ordered edits/IME reused stale layout: %s cursor=%zu height=%g\n",
		        buffer, gui->text_edit_state->cursor, (double)area.panel.content_height);
		return 0;
	}

	// An external replacement of equal length must be seen in the next call,
	// even when the edit state's content_version did not change.
	rg_gui_text_edit_clear_preedit(gui->text_edit_state);
	memcpy(buffer, "MMMMMMMM", 9u);
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	(void)rg_gui_text_area(gui, &area, buffer, sizeof(buffer), rect, id);
	rg_gui_end_frame(gui);
	if (!nearly_equal(area.panel.content_height, 80.0f))
	{
		fprintf(stderr, "text area reused layout across external edits\n");
		return 0;
	}

	// One invocation performs click picking, drag picking, and rendering.
	input.mouse_x = 1;
	input.mouse_y = 25;
	input.current_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	rg_gui_begin_frame(gui, &input, 1.0f / 60.0f);
	(void)rg_gui_text_area(gui, &area, buffer, sizeof(buffer), rect, id);
	rg_gui_end_frame(gui);
	if (gui->text_edit_state->cursor != 2u || !nearly_equal(area.panel.content_height, 80.0f))
	{
		fprintf(stderr, "text area click/drag layout disagreed with rendering\n");
		return 0;
	}
	gui->text_edit_state->active = 0;
	gui->focus_id = 0u;
	gui->active_id = 0u;
	gui->style = saved_style;
	return 1;
}

int main(void)
{
	RgTextFont font;
	RgTextGlyph glyphs[3];
	make_font(&font, glyphs);

	void* memory = malloc(MB(16));
	if (!memory)
	{
		return 1;
	}
	RgArena arena = {(char*)memory, MB(16), 0u, MB(16)};
	RgGuiContext gui;
	RgGuiInitDesc desc = {0};
	desc.font = &font;
	desc.max_draw_cmds = 128u;
	desc.text_buffer_size = KB(4);
	if (!test_init_sizing_and_int_parse(&desc))
	{
		free(memory);
		return 1;
	}
	if (!rg_gui_init(&gui, &arena, &desc))
	{
		free(memory);
		return 1;
	}

	if (!nearly_equal(rg_gui_text_measure_prefix(&gui, "A", 1u), 12.8f))
	{
		fprintf(stderr, "font-backed measurement failed\n");
		free(memory);
		return 1;
	}

	(void)rg_gui_text_measure_cached(&gui, "A", 1u);
	gui.style.text_height = 20.0f;
	if (!nearly_equal(rg_gui_text_measure_cached(&gui, "A", 1u), 16.0f))
	{
		fprintf(stderr, "scaled measurement cache failed\n");
		free(memory);
		return 1;
	}

	char multiline_clipboard[] = "A\r\nM\rA\n";
	rg_gui_text_edit_normalize_clipboard_text(multiline_clipboard, 1);
	if (strcmp(multiline_clipboard, "A\nM\nA\n") != 0)
	{
		fprintf(stderr, "multiline clipboard newline normalization failed\n");
		free(memory);
		return 1;
	}
	char single_line_clipboard[] = "A\r\nM\rA\n";
	rg_gui_text_edit_normalize_clipboard_text(single_line_clipboard, 0);
	if (strcmp(single_line_clipboard, "A M A ") != 0)
	{
		fprintf(stderr, "single-line clipboard newline normalization failed\n");
		free(memory);
		return 1;
	}

	static const char static_a[] = "A";
	rg_gui_push_text_static(&gui, static_a, rg_vec2(4.0f, 8.0f), rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f));
	const RgGuiDrawList* draw_list = rg_gui_draw_list(&gui);
	if (!draw_list || draw_list->count != 1u ||
	    draw_list->cmds[0].type != RG_GUI_CMD_TEXT ||
	    !nearly_equal(draw_list->cmds[0].data.text.scale, 2.0f) ||
	    draw_list->cmds[0].data.text.cache_identity != (uintptr_t)static_a)
	{
		fprintf(stderr, "text command scale or identity failed\n");
		free(memory);
		return 1;
	}

	size_t failed_used = 0u;
	char tiny_memory[64];
	RgArena tiny = {tiny_memory, sizeof(tiny_memory), failed_used, sizeof(tiny_memory)};
	RgGuiContext failed;
	if (rg_gui_init(&failed, &tiny, &desc) || tiny.used != failed_used)
	{
		fprintf(stderr, "initialization rollback failed\n");
		free(memory);
		return 1;
	}

	RgInputState input;
	rg_input_init(&input);
	input.mouse_x = 8;
	input.mouse_y = 12;
	input.current_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	input.previous_mouse[RG_MOUSE_BUTTON_LEFT] = false;
	rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
	static const char* menu_labels[] = {"A", "M"};
	static const char* menu_items[] = {"A", "M"};
	int menu_active = -1;
	int menu_open = 0;
	int menu_selected = -1;
	int menu_scroll = 0;
	RgGuiRect menu_rect = rg_gui_make_rect(0.0f, 0.0f, 160.0f, 28.0f);
	RgGuiRect active_rect = {0};
	RgGuiId menu_id = rg_gui_id_str("menu_pairing_regression");
	rg_gui_menu_bar(&gui, menu_labels, (u32)RG_ARRAY_COUNT(menu_labels),
	                &menu_active, &menu_open, menu_rect, menu_id, &active_rect);
	if (menu_open)
	{
		RgGuiRect popup_rect = rg_gui_make_rect(active_rect.x,
		                                        active_rect.y + active_rect.h,
		                                        80.0f, 0.0f);
		rg_gui_menu_popup(&gui, menu_items, (u32)RG_ARRAY_COUNT(menu_items),
		                  &menu_selected, &menu_open, &menu_scroll,
		                  popup_rect, menu_id);
	}
	if (!menu_open || menu_active != 0)
	{
		fprintf(stderr, "paired menu popup closed on its opening click\n");
		free(memory);
		return 1;
	}
	rg_gui_end_frame(&gui);

	input.mouse_x = (int)(active_rect.x + 8.0f);
	input.mouse_y = (int)(active_rect.y + active_rect.h + 8.0f);
	input.previous_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	input.current_mouse[RG_MOUSE_BUTTON_LEFT] = false;
	rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
	rg_gui_menu_bar(&gui, menu_labels, (u32)RG_ARRAY_COUNT(menu_labels),
	                &menu_active, &menu_open, menu_rect, menu_id, &active_rect);
	RgGuiRect popup_rect = rg_gui_make_rect(active_rect.x,
	                                        active_rect.y + active_rect.h,
	                                        80.0f, 0.0f);
	rg_gui_menu_popup(&gui, menu_items, (u32)RG_ARRAY_COUNT(menu_items),
	                  &menu_selected, &menu_open, &menu_scroll,
	                  popup_rect, menu_id);
	rg_gui_end_frame(&gui);

	input.previous_mouse[RG_MOUSE_BUTTON_LEFT] = false;
	input.current_mouse[RG_MOUSE_BUTTON_LEFT] = true;
	rg_gui_begin_frame(&gui, &input, 1.0f / 60.0f);
	rg_gui_menu_bar(&gui, menu_labels, (u32)RG_ARRAY_COUNT(menu_labels),
	                &menu_active, &menu_open, menu_rect, menu_id, &active_rect);
	popup_rect = rg_gui_make_rect(active_rect.x, active_rect.y + active_rect.h,
	                              80.0f, 0.0f);
	int menu_changed = rg_gui_menu_popup(&gui, menu_items,
	                                     (u32)RG_ARRAY_COUNT(menu_items),
	                                     &menu_selected, &menu_open, &menu_scroll,
	                                     popup_rect, menu_id);
	rg_gui_end_frame(&gui);
	if (!menu_changed || menu_open || menu_selected != 0)
	{
		fprintf(stderr, "paired menu popup item click failed\n");
		free(memory);
		return 1;
	}

	if (!test_dock_layout_validation(&gui) || !test_dock_tab_title_cache(&gui) || !test_visual_line_lookup() ||
	    !test_pack_color() ||
	    !test_unpack_color() ||
	    !test_hidden_draw_culling() ||
	    !test_node_graph_bundle_validation() ||
	    !test_integer_widget_boundaries(&gui) ||
	    !test_viewport_and_input_router(&gui) || !test_viewport_storage(&font) ||
	    !test_incremental_text_wrapping() || !test_text_area_layout_edits(&gui) ||
	    !test_text_lookup() || !test_text_lifetimes(&font) || !test_text_mouse_capture(&font) ||
	    !test_text_area_cache())
	{
		free(memory);
		return 1;
	}

	free(memory);
	puts("rg_gui direct text integration tests passed");
	return 0;
}
