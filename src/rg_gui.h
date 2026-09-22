// rg_gui - Immediate-mode UI helpers
//
// Part of the Reverse Gravity (rg_) libraries.
// Single-header C11/GNU11 UI core with draw-list output and SDL3 input integration.
//
// USAGE:
//   #include "rg_gui.h"
//
//   RgGuiContext gui = {0};
//   RgGuiInitDesc desc = {0};
//   desc.font = &font;
//   desc.max_draw_cmds = 4096;
//   desc.text_buffer_size = 64 * 1024;
//   if (!rg_gui_init(&gui, &arena, &desc)) { ... }
//
//   // Per frame:
//   rg_gui_begin_frame(&gui, &input, delta_time);
//   if (rg_gui_button(&gui, "Apply", rect, rg_gui_id_str("apply"))) { ... }
//   rg_gui_end_frame(&gui);
//
// OPTIONS:
//   #define RG_GUI_ASSERT(x)            - Custom assert macro (default: assert)
//   #define RG_GUI_MAX_DRAW_CMDS        - Default draw command capacity (default: 4096)
//   #define RG_GUI_TEXT_BUFFER_SIZE     - Default text buffer size (default: 64 KB)
//   #define RG_GUI_TEXT_MEASURE_CACHE_SIZE - Static text width cache size (default: 256 when RG_GUI_ASSUME_STATIC_LABELS)
//   #define RG_GUI_TEXT_LENGTH_CACHE_SIZE  - Static text length cache size (default: 256 when RG_GUI_ASSUME_STATIC_LABELS)
//   #define RG_GUI_MENU_WIDTH_CACHE_SIZE   - Menu max-width cache size (default: 64 when RG_GUI_ASSUME_STATIC_LABELS)
//   #define RG_GUI_TAB_SCROLL_CACHE_SIZE   - Tab scroll cache size (default: 64)
//   #define RG_GUI_TEXT_PREFIX_CACHE_SIZE  - Text input prefix cache size (default: 16)
//   #define RG_GUI_IME_PREEDIT_SIZE     - Bounded IME composition buffer size (default: 256)
//   #define RG_GUI_TEXT_FILTER_INPUT_SIZE  - Text filter input buffer size (default: 64)
//   #define RG_GUI_TEXT_FILTER_MAX_TOKENS  - Text filter token capacity (default: 8)
//   #define RG_GUI_NUMBER_BUFFER_SIZE   - Slider input buffer size (default: 64)
//   #define RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE - Value-undo entry size (default: 128)
//   #define RG_GUI_TEXT_VALUE_UNDO_CACHE_SIZE - Value-undo cache entries (default: 32)
//   #define RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE - Value-undo history entries (default: 64)
//   #define RG_GUI_VALUE_CACHE_SIZE     - Cached value strings for sliders (default: 64)
//   #define RG_GUI_ITEM_WIDTH_STACK_MAX - Item width stack depth (default: 16)
//   #define RG_GUI_DRAG_THRESHOLD       - Drag start threshold in pixels (default: 6)
//   #define RG_GUI_DRAG_PAYLOAD_SIZE    - Inline drag payload storage (default: 64)
//   #define RG_GUI_REPEAT_MAX_PER_FRAME - Maximum synthesized key/wheel steps per frame (default: 256)
//   #define RG_GUI_WINDOW_SNAP_DISTANCE - Snap radius for windows (default: 8)
//   #define RG_GUI_ENABLE_VIEWPORTS     - Enable multi-viewport draw lists (default: off)
//   #define RG_GUI_MAX_VIEWPORTS        - Max viewports (default: 4)
//   #define RG_GUI_VIEWPORT_STACK_MAX   - Viewport stack depth (default: 8)
//   #define RG_GUI_MAX_DOCKSPACES       - Max dockspaces per frame (default: 16)
//   #define RG_GUI_MAX_DOCK_NODES       - Dock node capacity (default: 256)
//   #define RG_GUI_MAX_DOCK_TABS        - Dock tab capacity (default: 512)
//   #define RG_GUI_DOCK_ZONE_FRACTION   - Drop zone size ratio (default: 0.25)
//   #define RG_GUI_DOCK_ZONE_MIN        - Drop zone min size in pixels (default: 48)
//   #define RG_GUI_DOCK_ZONE_MAX        - Drop zone max size in pixels (default: 200)
//   #define RG_GUI_DOCK_SPLIT_MIN       - Dock split min size in pixels (default: 80)
//   #define RG_GUI_DOCK_SPLITTER_THICKNESS - Dock splitter thickness in pixels (default: 4)
//   #define RG_GUI_DOCK_LAYOUT_MAGIC    - Dock layout file magic (default: 'RGDL')
//   #define RG_GUI_DOCK_LAYOUT_VERSION  - Dock layout file version (default: 1)
//   #define RG_GUI_TABLE_RESIZE_GRIP    - Column resize grip width (default: 6)
//   #define RG_GUI_ROW_CACHE_BLOCK     - Row cache block size for clipped panels (default: 64)
//   #define RG_GUI_NO_STRING_IDS        - Remove string-ID APIs at compile time (use explicit cached IDs)
//   #define RG_GUI_ASSUME_STATIC_LABELS - Treat all labels as static text
//   #define RG_GUI_COPY_DYNAMIC_TEXT    - Copy input/value text into frame buffer (default: 0)
//
// NOTES:
//   - Requires rg_input.h (SDL3) for input state.
//   - Requires rg_text.h for font measurement and renderer cache identity.
//   - Use rg_gui_label_static for constant labels to skip text copies (or RG_GUI_ASSUME_STATIC_LABELS).
//     Strings passed to any *_static API must remain at the same address and their bytes must not
//     change for the lifetime of the GUI context; static-text caches use pointer identity.
//     With RG_GUI_ASSUME_STATIC_LABELS, this contract applies to every label API.
//   - This header intentionally has no include guard and should be included once.
//   - All functions have internal linkage and work in unity builds.
//
// Author: Steven Wendel (superwendel)

#if defined(RG_GUI_INCLUDED)
#error rg_gui.h is include-once; include it once in the unity translation unit
#endif
#define RG_GUI_HAS_TEXT_LOOKUP 1
#define RG_GUI_INCLUDED 1

#define RG_GUI_VERSION_MAJOR 0
#define RG_GUI_VERSION_MINOR 1
#define RG_GUI_VERSION_PATCH 0
#define RG_GUI_VERSION ((RG_GUI_VERSION_MAJOR * 10000) + (RG_GUI_VERSION_MINOR * 100) + RG_GUI_VERSION_PATCH)
#define RG_GUI_VERSION_STRING "0.1.0"

#include "rg_defs.h"
#include "rg_bin.h"
#include "rg_hash.h"
#include "rg_input.h"
#include "rg_math_vec.h"
#include "rg_sprintf_hybrid.h"
#include "rg_text.h"

#include <stdbool.h>
#include <limits.h>
#include <stddef.h>
#include <stdint.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>

// =============================================================================
// CONFIGURATION
// =============================================================================

#ifndef RG_GUI_ASSERT
#include <assert.h>
#define RG_GUI_ASSERT(x) assert(x)
#endif

#ifndef RG_GUI_MAX_DRAW_CMDS
#define RG_GUI_MAX_DRAW_CMDS 4096u
#endif

#ifndef RG_GUI_TEXT_BUFFER_SIZE
#define RG_GUI_TEXT_BUFFER_SIZE (64u * 1024u)
#endif

#ifndef RG_GUI_REPEAT_MAX_PER_FRAME
#define RG_GUI_REPEAT_MAX_PER_FRAME 256
#endif

#if RG_GUI_REPEAT_MAX_PER_FRAME < 1 || RG_GUI_REPEAT_MAX_PER_FRAME > INT_MAX
#error RG_GUI_REPEAT_MAX_PER_FRAME must be between 1 and INT_MAX
#endif

#ifndef RG_GUI_TEXT_MEASURE_CACHE_SIZE
#if defined(RG_GUI_ASSUME_STATIC_LABELS)
#define RG_GUI_TEXT_MEASURE_CACHE_SIZE 256u
#else
#define RG_GUI_TEXT_MEASURE_CACHE_SIZE 0u
#endif
#endif

#ifndef RG_GUI_TEXT_LENGTH_CACHE_SIZE
#if defined(RG_GUI_ASSUME_STATIC_LABELS)
#define RG_GUI_TEXT_LENGTH_CACHE_SIZE 256u
#else
#define RG_GUI_TEXT_LENGTH_CACHE_SIZE 0u
#endif
#endif

#ifndef RG_GUI_MENU_WIDTH_CACHE_SIZE
#if defined(RG_GUI_ASSUME_STATIC_LABELS)
#define RG_GUI_MENU_WIDTH_CACHE_SIZE 64u
#else
#define RG_GUI_MENU_WIDTH_CACHE_SIZE 0u
#endif
#endif

#ifndef RG_GUI_TAB_SCROLL_CACHE_SIZE
#define RG_GUI_TAB_SCROLL_CACHE_SIZE 64u
#endif

#ifndef RG_GUI_TEXT_PREFIX_CACHE_SIZE
#define RG_GUI_TEXT_PREFIX_CACHE_SIZE 16u
#endif

#ifndef RG_GUI_IME_PREEDIT_SIZE
#define RG_GUI_IME_PREEDIT_SIZE 256u
#endif
#if RG_GUI_IME_PREEDIT_SIZE < 1
#undef RG_GUI_IME_PREEDIT_SIZE
#define RG_GUI_IME_PREEDIT_SIZE 1u
#endif

#ifndef RG_GUI_ITEM_WIDTH_STACK_MAX
#define RG_GUI_ITEM_WIDTH_STACK_MAX 16u
#endif

#ifndef RG_GUI_NUMBER_BUFFER_SIZE
#define RG_GUI_NUMBER_BUFFER_SIZE 64u
#endif

#ifndef RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE
#define RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE 128u
#endif
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE < 1
#undef RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE
#define RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE 1u
#endif

#ifndef RG_GUI_TEXT_VALUE_UNDO_CACHE_SIZE
#define RG_GUI_TEXT_VALUE_UNDO_CACHE_SIZE 32u
#endif

#ifndef RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE
#define RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE 64u
#endif

#ifndef RG_GUI_TEXT_UNDO_STACK_SIZE
#define RG_GUI_TEXT_UNDO_STACK_SIZE 64u
#endif

#ifndef RG_GUI_TEXT_UNDO_BUFFER_SIZE
#define RG_GUI_TEXT_UNDO_BUFFER_SIZE 2048u
#endif

#ifndef RG_GUI_TEXT_FILTER_BUFFER_SIZE
#define RG_GUI_TEXT_FILTER_BUFFER_SIZE 256u
#endif

#ifndef RG_GUI_TEXT_FILTER_INPUT_SIZE
#define RG_GUI_TEXT_FILTER_INPUT_SIZE 64u
#endif

#ifndef RG_GUI_TEXT_FILTER_MAX_TOKENS
#define RG_GUI_TEXT_FILTER_MAX_TOKENS 8u
#endif

#ifndef RG_GUI_TEXT_AREA_VISUAL_LINE_MAX
#define RG_GUI_TEXT_AREA_VISUAL_LINE_MAX 1024u
#endif

#ifndef RG_GUI_DOUBLE_CLICK_TIME
#define RG_GUI_DOUBLE_CLICK_TIME 0.35f
#endif

#ifndef RG_GUI_DOUBLE_CLICK_DISTANCE
#define RG_GUI_DOUBLE_CLICK_DISTANCE 4.0f
#endif

#ifndef RG_GUI_VALUE_CACHE_SIZE
#define RG_GUI_VALUE_CACHE_SIZE 64u
#endif

#ifndef RG_GUI_DRAG_THRESHOLD
#define RG_GUI_DRAG_THRESHOLD 6.0f
#endif

#ifndef RG_GUI_DRAG_PAYLOAD_SIZE
#define RG_GUI_DRAG_PAYLOAD_SIZE 64u
#endif

#ifndef RG_GUI_UNUSED
#define RG_GUI_UNUSED(x) (void)(x)
#endif

#ifndef RG_GUI_LABEL_COPY
#if defined(RG_GUI_ASSUME_STATIC_LABELS)
#define RG_GUI_LABEL_COPY 0
#else
#define RG_GUI_LABEL_COPY 1
#endif
#endif

#ifndef RG_GUI_COPY_DYNAMIC_TEXT
#define RG_GUI_COPY_DYNAMIC_TEXT 0
#endif

#ifndef RG_GUI_TEXT_CACHE_IDENTITY
#define RG_GUI_TEXT_CACHE_IDENTITY 1
#endif

#ifndef RG_GUI_DISABLED_STACK_MAX
#define RG_GUI_DISABLED_STACK_MAX 32u
#endif

#ifndef RG_GUI_ID_STACK_MAX
#define RG_GUI_ID_STACK_MAX 32u
#endif

#ifndef RG_GUI_STYLE_VAR_STACK_MAX
#define RG_GUI_STYLE_VAR_STACK_MAX 32u
#endif

#ifndef RG_GUI_STYLE_COLOR_STACK_MAX
#define RG_GUI_STYLE_COLOR_STACK_MAX 32u
#endif

#ifndef RG_GUI_POPUP_STACK_MAX
#define RG_GUI_POPUP_STACK_MAX 16u
#endif

#ifndef RG_GUI_WINDOW_SNAP_DISTANCE
#define RG_GUI_WINDOW_SNAP_DISTANCE 8.0f
#endif

#ifndef RG_GUI_MAX_VIEWPORTS
#define RG_GUI_MAX_VIEWPORTS 4u
#endif

#if defined(RG_GUI_ENABLE_VIEWPORTS) && (RG_GUI_MAX_VIEWPORTS < 1 || RG_GUI_MAX_VIEWPORTS >= UINT32_MAX)
#error RG_GUI_MAX_VIEWPORTS must be between 1 and UINT32_MAX - 1 when viewports are enabled
#endif

#ifndef RG_GUI_INPUT_ROUTE_MAX
#define RG_GUI_INPUT_ROUTE_MAX (RG_GUI_MAX_VIEWPORTS + 1u)
#endif

#ifndef RG_GUI_VIEWPORT_STACK_MAX
#define RG_GUI_VIEWPORT_STACK_MAX 8u
#endif

#ifndef RG_GUI_MAX_DOCKSPACES
#define RG_GUI_MAX_DOCKSPACES 16u
#endif

#ifndef RG_GUI_MAX_DOCK_NODES
#define RG_GUI_MAX_DOCK_NODES 256u
#endif

#if RG_GUI_MAX_DOCK_NODES < 1 || RG_GUI_MAX_DOCK_NODES >= UINT32_MAX
#error RG_GUI_MAX_DOCK_NODES must be between 1 and UINT32_MAX - 1
#endif

#ifndef RG_GUI_MAX_DOCK_TABS
#define RG_GUI_MAX_DOCK_TABS 512u
#endif

#if RG_GUI_MAX_DOCK_TABS < 1 || RG_GUI_MAX_DOCK_TABS >= UINT32_MAX
#error RG_GUI_MAX_DOCK_TABS must be between 1 and UINT32_MAX - 1
#endif

#ifndef RG_GUI_DOCK_ZONE_FRACTION
#define RG_GUI_DOCK_ZONE_FRACTION 0.25f
#endif

#ifndef RG_GUI_DOCK_ZONE_MIN
#define RG_GUI_DOCK_ZONE_MIN 48.0f
#endif

#ifndef RG_GUI_DOCK_ZONE_MAX
#define RG_GUI_DOCK_ZONE_MAX 200.0f
#endif

#ifndef RG_GUI_DOCK_SPLIT_MIN
#define RG_GUI_DOCK_SPLIT_MIN 80.0f
#endif

#ifndef RG_GUI_DOCK_SPLITTER_THICKNESS
#define RG_GUI_DOCK_SPLITTER_THICKNESS 4.0f
#endif

#ifndef RG_GUI_DOCK_LAYOUT_MAGIC
#define RG_GUI_DOCK_LAYOUT_MAGIC 0x4C444752u
#endif
#ifndef RG_GUI_DOCK_LAYOUT_VERSION
#define RG_GUI_DOCK_LAYOUT_VERSION 1u
#endif
#ifndef RG_GUI_DOCK_LAYOUT_HEADER_SIZE
#define RG_GUI_DOCK_LAYOUT_HEADER_SIZE 32u
#endif
#ifndef RG_GUI_DOCK_LAYOUT_NODE_SIZE
#define RG_GUI_DOCK_LAYOUT_NODE_SIZE 32u
#endif
#ifndef RG_GUI_DOCK_LAYOUT_TAB_SIZE
#define RG_GUI_DOCK_LAYOUT_TAB_SIZE 16u
#endif

#ifndef RG_GUI_TABLE_RESIZE_GRIP
#define RG_GUI_TABLE_RESIZE_GRIP 6.0f
#endif
#ifndef RG_GUI_TABLE_MAX_COLUMNS
#define RG_GUI_TABLE_MAX_COLUMNS 64u
#endif
#ifndef RG_GUI_NODE_EDITOR_MAX_NODES
#define RG_GUI_NODE_EDITOR_MAX_NODES 128u
#endif
#ifndef RG_GUI_NODE_GRAPH_MAX_NODES
#define RG_GUI_NODE_GRAPH_MAX_NODES RG_GUI_NODE_EDITOR_MAX_NODES
#endif
#ifndef RG_GUI_NODE_GRAPH_MAX_LINKS
#define RG_GUI_NODE_GRAPH_MAX_LINKS 256u
#endif
#ifndef RG_GUI_NODE_GROUP_MAX_MEMBERS
#define RG_GUI_NODE_GROUP_MAX_MEMBERS RG_GUI_NODE_GRAPH_MAX_NODES
#endif
#ifndef RG_GUI_NODE_GRAPH_HIT_CELL_COUNT
#define RG_GUI_NODE_GRAPH_HIT_CELL_COUNT 128u
#endif
#ifndef RG_GUI_NODE_GRAPH_HIT_CELL_SIZE
#define RG_GUI_NODE_GRAPH_HIT_CELL_SIZE 192.0f
#endif
#ifndef RG_GUI_NODE_GRAPH_HIT_NODE_ENTRIES
#define RG_GUI_NODE_GRAPH_HIT_NODE_ENTRIES (RG_GUI_NODE_GRAPH_MAX_NODES * 8u)
#endif
#if RG_GUI_NODE_GRAPH_HIT_CELL_COUNT < 1
#undef RG_GUI_NODE_GRAPH_HIT_CELL_COUNT
#define RG_GUI_NODE_GRAPH_HIT_CELL_COUNT 1u
#endif
#if RG_GUI_NODE_GRAPH_HIT_NODE_ENTRIES < 1
#undef RG_GUI_NODE_GRAPH_HIT_NODE_ENTRIES
#define RG_GUI_NODE_GRAPH_HIT_NODE_ENTRIES 1u
#endif
#ifndef RG_GUI_ROW_CACHE_BLOCK
#define RG_GUI_ROW_CACHE_BLOCK 64u
#endif

// =============================================================================
// TYPES
// =============================================================================

typedef u64 RgGuiId;
typedef uintptr_t RgGuiTexture;
typedef u64 RgGuiImageMaterial;

/**
 * Optional caller-owned ASCII lookup tables shared by GUI contexts using one font.
 * The font, its glyph/kerning arrays, and these tables must remain alive and unchanged
 * while attached to a context. Rebuild the tables after changing the font.
 * Uses 66,568 bytes on 64-bit targets; no arena allocation is added by default.
 */
typedef struct RgGuiTextLookup
{
	const RgTextFont* font;
	const RgTextGlyph* ascii_glyphs[128];
	i32 ascii_kerning[128 * 128];
} RgGuiTextLookup;

typedef struct RgGuiContext RgGuiContext;
typedef struct RgGuiTableColumn RgGuiTableColumn;

typedef struct RgGuiRect
{
	f32 x;
	f32 y;
	f32 w;
	f32 h;
} RgGuiRect;

typedef struct RgGuiIcon
{
	RgGuiTexture texture;
	RgGuiRect uv;
	rg_vec4 tint;
	RgGuiImageMaterial material;
} RgGuiIcon;

typedef struct RgGuiAnchor
{
	rg_vec2 min;
	rg_vec2 max;
	rg_vec2 offset_min;
	rg_vec2 offset_max;
} RgGuiAnchor;

typedef struct RgGuiTreeItem
{
	const char* label;
	int* open;
	RgGuiId id;
	u32 depth;
	int selected;
} RgGuiTreeItem;

typedef struct RgGuiSelectionState
{
	int anchor;
	int cursor;
	int initialized;
} RgGuiSelectionState;

typedef struct RgGuiSelectionOps
{
	int (*is_selected)(const void* user, u32 index);
	void (*set_selected)(void* user, u32 index, int selected);
	void (*set_range)(void* user, u32 start, u32 end, int selected);
	void (*clear)(void* user);
} RgGuiSelectionOps;

typedef void (*RgGuiUndoFn)(void* user, const void* data);

typedef struct RgGuiUndoCommand
{
	RgGuiUndoFn undo;
	RgGuiUndoFn redo;
	u32 data_offset;
	u32 data_size;
} RgGuiUndoCommand;

typedef struct RgGuiUndoRedo
{
	RgGuiUndoCommand* commands;
	u8* data;
	u32 command_capacity;
	u32 data_capacity;
	u32 command_head;
	u32 command_count;
	u32 command_index;
	u32 data_head;
	u32 data_used;
	void* user;
} RgGuiUndoRedo;

typedef void (*RgGuiImeCallback)(const RgGuiContext* ctx, RgGuiRect caret_rect, void* user_data);
typedef const char* (*RgGuiListItemFn)(const void* user, u32 index);
typedef f32 (*RgGuiListRowHeightFn)(const void* user, u32 index, f32 base_height);
typedef int (*RgGuiTreeItemFn)(const void* user, u32 index, RgGuiTreeItem* out_item);
typedef void (*RgGuiTreeRowFn)(RgGuiContext* ctx, u32 index, const RgGuiTreeItem* item,
                               RgGuiRect row_rect, u32 result, const void* user);
typedef void (*RgGuiTableRowFn)(RgGuiContext* ctx, u32 row_index, const RgGuiTableColumn* columns,
                                const f32* widths, u32 column_count, RgGuiRect row_rect, const void* user);
typedef f32 (*RgGuiPlotSampleFn)(const void* user, u32 index);

typedef struct RgGuiStyle
{
	f32 text_height;
	f32 char_width;
	f32 padding;
	f32 inner_spacing;
	f32 label_width;
	f32 value_width;
	f32 scroll_bar_width;
	f32 slider_handle_width;
	f32 border_thickness;
	f32 focus_border_thickness;
	f32 cursor_blink_interval;
	f32 key_repeat_delay;
	f32 key_repeat_interval;
	f32 key_repeat_fast_delay;
	f32 key_repeat_fast_interval;
	int value_decimals;
	f32 disabled_alpha;

	rg_vec4 color_text;
	rg_vec4 color_text_dim;
	rg_vec4 color_bg;
	rg_vec4 color_bg_hover;
	rg_vec4 color_bg_active;
	rg_vec4 color_border;
	rg_vec4 color_accent;
	rg_vec4 color_panel;
	rg_vec4 color_focus_border;
	rg_vec4 color_selection;
	rg_vec4 color_caret;
	rg_vec4 color_panel_title;
	rg_vec4 color_panel_floating;
	rg_vec4 color_drop_shadow;
} RgGuiStyle;

typedef enum RgGuiStyleVar
{
	RG_GUI_STYLE_VAR_TEXT_HEIGHT = 0,
	RG_GUI_STYLE_VAR_CHAR_WIDTH = 1,
	RG_GUI_STYLE_VAR_PADDING = 2,
	RG_GUI_STYLE_VAR_INNER_SPACING = 3,
	RG_GUI_STYLE_VAR_LABEL_WIDTH = 4,
	RG_GUI_STYLE_VAR_VALUE_WIDTH = 5,
	RG_GUI_STYLE_VAR_SCROLL_BAR_WIDTH = 6,
	RG_GUI_STYLE_VAR_SLIDER_HANDLE_WIDTH = 7,
	RG_GUI_STYLE_VAR_BORDER_THICKNESS = 8,
	RG_GUI_STYLE_VAR_FOCUS_BORDER_THICKNESS = 9,
	RG_GUI_STYLE_VAR_CURSOR_BLINK_INTERVAL = 10,
	RG_GUI_STYLE_VAR_KEY_REPEAT_DELAY = 11,
	RG_GUI_STYLE_VAR_KEY_REPEAT_INTERVAL = 12,
	RG_GUI_STYLE_VAR_KEY_REPEAT_FAST_DELAY = 13,
	RG_GUI_STYLE_VAR_KEY_REPEAT_FAST_INTERVAL = 14,
	RG_GUI_STYLE_VAR_VALUE_DECIMALS = 15,
	RG_GUI_STYLE_VAR_DISABLED_ALPHA = 16
} RgGuiStyleVar;

typedef enum RgGuiStyleColor
{
	RG_GUI_STYLE_COLOR_TEXT = 0,
	RG_GUI_STYLE_COLOR_TEXT_DIM = 1,
	RG_GUI_STYLE_COLOR_BG = 2,
	RG_GUI_STYLE_COLOR_BG_HOVER = 3,
	RG_GUI_STYLE_COLOR_BG_ACTIVE = 4,
	RG_GUI_STYLE_COLOR_BORDER = 5,
	RG_GUI_STYLE_COLOR_ACCENT = 6,
	RG_GUI_STYLE_COLOR_PANEL = 7,
	RG_GUI_STYLE_COLOR_FOCUS_BORDER = 8,
	RG_GUI_STYLE_COLOR_SELECTION = 9,
	RG_GUI_STYLE_COLOR_CARET = 10,
	RG_GUI_STYLE_COLOR_PANEL_TITLE = 11,
	RG_GUI_STYLE_COLOR_PANEL_FLOATING = 12,
	RG_GUI_STYLE_COLOR_DROP_SHADOW = 13
} RgGuiStyleColor;

typedef struct RgGuiStyleVarStackEntry
{
	RgGuiStyleVar var;
	int is_int;
	union
	{
		f32 f;
		int i;
	} value;
} RgGuiStyleVarStackEntry;

typedef struct RgGuiStyleColorStackEntry
{
	RgGuiStyleColor color;
	rg_vec4 value;
} RgGuiStyleColorStackEntry;

typedef enum RgGuiArrowDirection
{
	RG_GUI_ARROW_LEFT = 0,
	RG_GUI_ARROW_RIGHT = 1,
	RG_GUI_ARROW_UP = 2,
	RG_GUI_ARROW_DOWN = 3,
	RG_GUI_ARROW_COUNT = 4
} RgGuiArrowDirection;

typedef enum RgGuiDrawCmdType
{
	RG_GUI_CMD_RECT = 0,
	RG_GUI_CMD_TEXT = 1,
	RG_GUI_CMD_CLIP_PUSH = 2,
	RG_GUI_CMD_CLIP_POP = 3,
	RG_GUI_CMD_IMAGE = 4,
	RG_GUI_CMD_TRIANGLE = 5
} RgGuiDrawCmdType;

typedef struct RgGuiDrawCmd
{
	RgGuiDrawCmdType type;
	union
	{
		struct
		{
			RgGuiRect rect;
			rg_vec4 color;
		} rect;
		struct
		{
			rg_vec2 a;
			rg_vec2 b;
			rg_vec2 c;
			rg_vec4 color;
		} triangle;
		struct
		{
			rg_vec2 pos;
			rg_vec4 color;
			f32 scale;
			const char* text;
#if defined(RG_GUI_TEXT_CACHE_IDENTITY)
			uintptr_t cache_identity;
#endif
		} text;
		struct
		{
			RgGuiRect rect;
			RgGuiRect uv;
			rg_vec4 color;
			RgGuiTexture texture;
			RgGuiImageMaterial material;
		} image;
		struct
		{
			RgGuiRect rect;
		} clip;
	} data;
} RgGuiDrawCmd;

typedef struct RgGuiDrawList
{
	RgGuiDrawCmd* cmds;
	u32 count;
	u32 capacity;
} RgGuiDrawList;

typedef u32 RgGuiDiagnosticFlags;

enum
{
	RG_GUI_DIAGNOSTIC_NONE = 0u,
	RG_GUI_DIAGNOSTIC_DRAW_CAPACITY = 1u << 0,
	RG_GUI_DIAGNOSTIC_TEXT_CAPACITY = 1u << 1,
	RG_GUI_DIAGNOSTIC_OVERLAY_CAPACITY = 1u << 2,
	RG_GUI_DIAGNOSTIC_STACK_OVERFLOW = 1u << 3,
	RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW = 1u << 4,
	RG_GUI_DIAGNOSTIC_STACK_IMBALANCE = 1u << 5,
	RG_GUI_DIAGNOSTIC_INPUT_CAPACITY = 1u << 6,
	RG_GUI_DIAGNOSTIC_ACTIVE_ID_RECOVERED = 1u << 7
};

/** Per-frame capacity, scope, input, and stale-capture diagnostics. */
typedef struct RgGuiDiagnostics
{
	RgGuiDiagnosticFlags flags;
	u32 dropped_draw_commands;
	size_t dropped_text_bytes;
	size_t dropped_input_events;
	size_t dropped_input_text_bytes;
	size_t draw_command_high_water;
	size_t text_buffer_high_water;
	u32 recovered_active_ids;
} RgGuiDiagnostics;

typedef struct RgGuiLayout
{
	f32 x;
	f32 y;
	f32 width;
	f32 spacing;
	f32 cursor_y;
	f32 row_x;
	f32 row_y;
	f32 row_cursor_x;
	f32 row_height;
	f32 row_spacing;
	f32 next_item_width;
	int active;
	int row_active;
} RgGuiLayout;

typedef struct RgGuiPanelState
{
	RgGuiLayout prev_layout;
	RgGuiRect rect;
	RgGuiRect inner_rect;
	f32 scroll_y;
	f32 content_height;
	f32 max_scroll;
	int hovered;
	int enabled;
	int content_override;
} RgGuiPanelState;

typedef enum RgGuiPanelFlags
{
	RG_GUI_PANEL_NONE = 0u,
	RG_GUI_PANEL_FLOATING = 1u << 0
} RgGuiPanelFlags;

typedef struct RgGuiRowCache
{
	f32* row_spans;
	f32* block_spans;
	u32 row_capacity;
	u32 block_capacity;
	u32 row_count;
	u32 block_count;
	f32 default_span;
	f32 spacing;
	f32 total_span;
} RgGuiRowCache;

typedef struct RgGuiTextAreaState
{
	RgGuiPanelState panel;
} RgGuiTextAreaState;

typedef struct RgGuiTextAreaVisualLine
{
	size_t start;
	size_t end;
	int soft_wrap_after;
} RgGuiTextAreaVisualLine;

typedef struct RgGuiTextFilterToken
{
	const char* text;
	u16 length;
	u8 exclude;
	u8 pad;
} RgGuiTextFilterToken;

typedef struct RgGuiTextFilter
{
	char buffer[RG_GUI_TEXT_FILTER_INPUT_SIZE];
	u32 length;
	u32 version;
	u32 parsed_version;
	u32 token_count;
	RgGuiTextFilterToken tokens[RG_GUI_TEXT_FILTER_MAX_TOKENS];
} RgGuiTextFilter;

typedef struct RgGuiPopupState
{
	int open;
	rg_vec2 pos;
} RgGuiPopupState;

typedef struct RgGuiPopupStackEntry
{
	RgGuiLayout prev_layout;
	RgGuiRect rect;
	RgGuiPopupState* popup;
	RgGuiDrawList* prev_draw_target;
	int hovered;
	int child_hovered;
} RgGuiPopupStackEntry;

typedef struct RgGuiColumns
{
	RgGuiRect rect;
	const f32* widths;
	f32 spacing;
	u32 count;
	u32 index;
	f32 cursor_x;
	f32 cursor_y;
	f32 row_height;
} RgGuiColumns;

typedef struct RgGuiTreeState
{
	f32 indent;
	u32 depth;
} RgGuiTreeState;

typedef enum RgGuiNodeLinkStyle
{
	RG_GUI_NODE_LINK_STYLE_ORTHOGONAL = 0,
	RG_GUI_NODE_LINK_STYLE_BEZIER = 1
} RgGuiNodeLinkStyle;

typedef struct RgGuiNodeEditorState
{
	int initialized;
	rg_vec2 pan;
	f32 zoom;
	f32 zoom_min;
	f32 zoom_max;
	int link_style;
	RgGuiRect rect;
	RgGuiLayout prev_layout;
	int node_active;
	rg_vec2 pan_start;
	rg_vec2 pan_start_mouse;
	rg_vec2 drag_start_mouse;
	rg_vec2 drag_start_pos;
	RgGuiId id;
	rg_vec2 content_min;
	rg_vec2 content_max;
	int content_valid;
	int node_hovered;
	rg_vec2 select_start;
	rg_vec2 select_end;
	RgGuiRect node_rects[RG_GUI_NODE_EDITOR_MAX_NODES];
	u32 node_count;
} RgGuiNodeEditorState;

typedef struct RgGuiNodePortRef
{
	u32 node;
	u16 port;
	u8 output;
	u8 pad;
} RgGuiNodePortRef;

typedef struct RgGuiNodeLinkRef
{
	u32 from_node;
	u16 from_port;
	u16 pad0;
	u32 to_node;
	u16 to_port;
	u16 pad1;
} RgGuiNodeLinkRef;

typedef struct RgGuiNodeGraphLink
{
	u32 from_node;
	u16 from_port;
	u16 pad0;
	u32 to_node;
	u16 to_port;
	u16 pad1;
	RgGuiId from_id;
	RgGuiId to_id;
} RgGuiNodeGraphLink;

typedef struct RgGuiNodeGraphNode
{
	RgGuiId id;
	rg_vec2 position;
	rg_vec2 size;
	u8 input_count;
	u8 output_count;
} RgGuiNodeGraphNode;

typedef enum RgGuiNodeGroupFlags
{
	RG_GUI_NODE_GROUP_NONE = 0u,
	RG_GUI_NODE_GROUP_SELECTED = 1u << 0,
	RG_GUI_NODE_GROUP_NO_MOVE = 1u << 1,
	RG_GUI_NODE_GROUP_NO_RESIZE = 1u << 2
} RgGuiNodeGroupFlags;

typedef enum RgGuiNodeGroupMode
{
	RG_GUI_NODE_GROUP_MODE_NONE = 0,
	RG_GUI_NODE_GROUP_MODE_MOVE = 1,
	RG_GUI_NODE_GROUP_MODE_RESIZE = 2
} RgGuiNodeGroupMode;

typedef struct RgGuiNodeGroup
{
	RgGuiId id;
	rg_vec2 position;
	rg_vec2 size;
	u32 flags;
	u32 member_count;
	RgGuiId members[RG_GUI_NODE_GROUP_MAX_MEMBERS];
} RgGuiNodeGroup;

typedef struct RgGuiNodeGroupState
{
	RgGuiId active_id;
	int active_index;
	int active_mode;
	int resize_mask;
	int hovered_index;
	int clicked_index;
	rg_vec2 drag_start_mouse;
	rg_vec2 drag_start_pos;
	rg_vec2 drag_start_size;
	u32 move_count;
	u32 move_indices[RG_GUI_NODE_GRAPH_MAX_NODES];
	RgGuiId move_ids[RG_GUI_NODE_GRAPH_MAX_NODES];
	rg_vec2 move_from[RG_GUI_NODE_GRAPH_MAX_NODES];
	rg_vec2 move_to[RG_GUI_NODE_GRAPH_MAX_NODES];
} RgGuiNodeGroupState;

typedef struct RgGuiNodeGroupResult
{
	int hovered;
	int active;
	int mode;
	int started;
	int ended;
	int moved;
	int resized;
	rg_vec2 move_delta;
	rg_vec2 size_delta;
} RgGuiNodeGroupResult;

typedef struct RgGuiNodeGraph
{
	u32 node_count;
	u32 link_count;
	RgGuiId node_ids[RG_GUI_NODE_GRAPH_MAX_NODES];
	rg_vec2 node_positions[RG_GUI_NODE_GRAPH_MAX_NODES];
	rg_vec2 node_sizes[RG_GUI_NODE_GRAPH_MAX_NODES];
	u8 node_input_count[RG_GUI_NODE_GRAPH_MAX_NODES];
	u8 node_output_count[RG_GUI_NODE_GRAPH_MAX_NODES];
	u32 link_from_node[RG_GUI_NODE_GRAPH_MAX_LINKS];
	u16 link_from_port[RG_GUI_NODE_GRAPH_MAX_LINKS];
	u32 link_to_node[RG_GUI_NODE_GRAPH_MAX_LINKS];
	u16 link_to_port[RG_GUI_NODE_GRAPH_MAX_LINKS];
} RgGuiNodeGraph;

typedef struct RgGuiNodeGraphState
{
	int link_drag_active;
	int link_drag_index;
	int hovered_link_index;
	int clicked_link_index;
	int selected_link_index;
	RgGuiNodePortRef link_drag;
	RgGuiNodeLinkRef link_drag_original;
	int drag_active;
	int drag_anchor;
	rg_vec2 drag_start[RG_GUI_NODE_GRAPH_MAX_NODES];
	int marquee_active;
	int marquee_add;
	int consume_right;
	u32 move_count;
	u32 move_indices[RG_GUI_NODE_GRAPH_MAX_NODES];
	RgGuiId move_ids[RG_GUI_NODE_GRAPH_MAX_NODES];
	rg_vec2 move_from[RG_GUI_NODE_GRAPH_MAX_NODES];
	rg_vec2 move_to[RG_GUI_NODE_GRAPH_MAX_NODES];
	u32 link_scratch_count;
	RgGuiNodeGraphLink link_scratch[RG_GUI_NODE_GRAPH_MAX_LINKS];
} RgGuiNodeGraphState;

typedef u32 (*RgGuiNodeGraphSerializeFn)(void* user, RgGuiId id, u8* out, u32 capacity);
typedef void (*RgGuiNodeGraphDeserializeFn)(void* user, RgGuiId id, const u8* data, u32 size);
typedef RgGuiId (*RgGuiNodeGraphAllocIdFn)(void* user);

typedef struct RgGuiNodeGraphBundle
{
	u32 node_count;
	u32 link_count;
	u32 payload_bytes;
	u32 payload_capacity;
	u8* payload;
	RgGuiNodeGraphNode nodes[RG_GUI_NODE_GRAPH_MAX_NODES];
	RgGuiNodeGraphLink links[RG_GUI_NODE_GRAPH_MAX_LINKS];
	u32 payload_offset[RG_GUI_NODE_GRAPH_MAX_NODES];
	u32 payload_size[RG_GUI_NODE_GRAPH_MAX_NODES];
} RgGuiNodeGraphBundle;

typedef const char* (*RgGuiNodeGraphTitleFn)(void* user, u32 index);
typedef void (*RgGuiNodeGraphNodeFn)(RgGuiContext* ctx, u32 index, RgGuiId node_id, void* user);
typedef void (*RgGuiNodeGraphPortFn)(RgGuiContext* ctx, RgGuiRect rect, int output, int highlighted, void* user);
typedef const char* (*RgGuiNodeGroupTitleFn)(void* user, u32 index);

typedef struct RgGuiNodeGraphNodeStyle
{
	rg_vec4 body;
	rg_vec4 title;
	rg_vec4 title_hover;
	rg_vec4 title_active;
	rg_vec4 border;
	rg_vec4 text;
	f32 border_thickness;
	f32 text_scale;
} RgGuiNodeGraphNodeStyle;

typedef void (*RgGuiNodeGraphStyleFn)(RgGuiContext* ctx, void* user, u32 index,
                                      RgGuiId node_id, RgGuiNodeGraphNodeStyle* style);

typedef struct RgGuiNodeGraphDraw
{
	RgGuiNodeGraphTitleFn title;
	RgGuiNodeGraphNodeFn content;
	RgGuiNodeGraphPortFn port;
	RgGuiNodeGraphStyleFn style;
} RgGuiNodeGraphDraw;

typedef struct RgGuiNodeGraphGroups
{
	RgGuiNodeGroup* groups;
	u32 count;
	RgGuiNodeGroupState* state;
	RgGuiNodeGroupTitleFn title;
} RgGuiNodeGraphGroups;

typedef struct RgGuiNodeGraphSelection
{
	RgGuiSelectionState* state;
	const RgGuiSelectionOps* ops;
	void* user;
	int* primary;
} RgGuiNodeGraphSelection;

typedef struct RgGuiNodeGraphMove
{
	u32 count;
	const u32* indices;
	const RgGuiId* ids;
	const rg_vec2* from;
	const rg_vec2* to;
} RgGuiNodeGraphMove;

typedef struct RgGuiNodeGraphCallbacks
{
	void (*on_node_move)(void* user, const RgGuiNodeGraphMove* move);
	void (*on_link_add)(void* user, const RgGuiNodeGraphLink* links, u32 count);
	void (*on_link_remove)(void* user, const RgGuiNodeGraphLink* links, u32 count);
	void (*on_link_rewire)(void* user, const RgGuiNodeGraphLink* before, const RgGuiNodeGraphLink* after);
} RgGuiNodeGraphCallbacks;

typedef struct RgGuiDragPayload
{
	RgGuiId type;
	const void* data;
	size_t size;
} RgGuiDragPayload;

typedef union RgGuiDragPayloadStorage
{
	u64 align;
	u8 bytes[RG_GUI_DRAG_PAYLOAD_SIZE > 0u ? RG_GUI_DRAG_PAYLOAD_SIZE : 1u];
} RgGuiDragPayloadStorage;

typedef struct RgGuiTableColumn
{
	const char* label;
	u32 id;
	f32 min_width;
	u32 flags;
} RgGuiTableColumn;

typedef struct RgGuiTableState
{
	RgGuiPanelState panel;
	int sort_column;
	int sort_descending;
	int resize_column;
	f32 resize_start_x;
	f32 resize_start_width;
	f32 scroll_x;
	f32 max_scroll_x;
	u32 column_count;
	u32 column_order[RG_GUI_TABLE_MAX_COLUMNS];
	u8 column_visible[RG_GUI_TABLE_MAX_COLUMNS];
	f32 column_offsets[RG_GUI_TABLE_MAX_COLUMNS];
	u32 column_visible_count;
	u8 layout_dirty;
	u32 layout_cache_safe_count;
	u32 layout_cache_display_count;
	u32 layout_cache_display_to_real[RG_GUI_TABLE_MAX_COLUMNS];
	RgGuiTableColumn layout_cache_display_columns[RG_GUI_TABLE_MAX_COLUMNS];
	f32 layout_cache_display_widths[RG_GUI_TABLE_MAX_COLUMNS];
	u32 layout_cache_order[RG_GUI_TABLE_MAX_COLUMNS];
	u8 layout_cache_visible[RG_GUI_TABLE_MAX_COLUMNS];
	f32 layout_cache_widths[RG_GUI_TABLE_MAX_COLUMNS];
	u32 layout_cache_flags[RG_GUI_TABLE_MAX_COLUMNS];
	f32 layout_cache_min_widths[RG_GUI_TABLE_MAX_COLUMNS];
	u32 layout_cache_draw_count;
	u32 layout_cache_frozen_count;
	u32 layout_cache_draw_index_by_display[RG_GUI_TABLE_MAX_COLUMNS];
	RgGuiTableColumn layout_cache_draw_columns[RG_GUI_TABLE_MAX_COLUMNS];
	f32 layout_cache_draw_widths[RG_GUI_TABLE_MAX_COLUMNS];
	int frozen_count;
	RgGuiPopupState header_popup;
	int header_popup_selected;
	int header_popup_scroll;
} RgGuiTableState;

typedef struct RgGuiWindowState
{
	RgGuiLayout prev_layout;
	RgGuiRect rect;
	RgGuiRect start_rect;
	rg_vec2 start_mouse;
	f32 expanded_height;
	u32 resize_flags;
	u32 order;
	u32 dock_flags;
	RgGuiId dockspace_id;
	u32 dock_node;
	u32 dock_tab;
	int docked;
	int dock_drag_pending;
	int open;
	int collapsed;
	int initialized;
	RgGuiIcon icon;
} RgGuiWindowState;

typedef struct RgGuiDockSpaceState
{
	RgGuiRect rect;
	RgGuiId id;
	rg_vec2 origin;
	RgGuiId viewport_id;
	RgGuiDrawList* overlay_target;
	RgGuiPopupState tab_popup;
	int tab_popup_selected;
	int tab_popup_scroll;
	u32 tab_popup_tab;
	u32 tab_popup_node;
	u32 root;
	int initialized;
} RgGuiDockSpaceState;

typedef struct RgGuiDockDragState
{
	int active;
	int mouse_released;
	RgGuiWindowState* window;
	RgGuiId window_id;
	RgGuiId viewport_id;
	const char* title;
	u32 flags;
	rg_vec2 mouse_global;
	rg_vec2 origin;
	RgGuiRect rect;
	RgGuiRect bounds;
	int bounds_set;
} RgGuiDockDragState;

typedef enum RgGuiDockSplit
{
	RG_GUI_DOCK_SPLIT_NONE = 0,
	RG_GUI_DOCK_SPLIT_VERT = 1,
	RG_GUI_DOCK_SPLIT_HORZ = 2
} RgGuiDockSplit;

typedef enum RgGuiDockSlot
{
	RG_GUI_DOCK_SLOT_CENTER = 0,
	RG_GUI_DOCK_SLOT_LEFT = 1,
	RG_GUI_DOCK_SLOT_RIGHT = 2,
	RG_GUI_DOCK_SLOT_TOP = 3,
	RG_GUI_DOCK_SLOT_BOTTOM = 4
} RgGuiDockSlot;

typedef struct RgGuiDockNode
{
	u32 parent;
	u32 child_a;
	u32 child_b;
	f32 split_ratio;
	RgGuiRect rect;
	u32 tab_head;
	u32 tab_count;
	u32 active_tab;
	f32 tab_scroll;
	u32 next_free;
	u8 split;
} RgGuiDockNode;

typedef struct RgGuiDockTab
{
	u32 next;
	u32 next_free;
	RgGuiId window_id;
	u32 flags;
	RgGuiWindowState* window;
	const char* title;
} RgGuiDockTab;

typedef struct RgGuiDockLayoutTab
{
	u32 next;
	RgGuiId window_id;
} RgGuiDockLayoutTab;

typedef struct RgGuiDockLayoutNode
{
	u32 parent;
	u32 child_a;
	u32 child_b;
	u32 tab_head;
	u32 tab_count;
	u32 active_tab;
	f32 split_ratio;
	u8 split;
} RgGuiDockLayoutNode;

typedef struct RgGuiDockLayout
{
	RgGuiId dockspace_id;
	u32 root;
	u32 node_count;
	u32 tab_count;
} RgGuiDockLayout;

typedef struct RgGuiTextEditUndoRecord
{
	size_t pos;
	u32 delete_len;
	u32 insert_len;
	u32 text_offset;
	size_t cursor_before;
	size_t cursor_after;
} RgGuiTextEditUndoRecord;

typedef enum RgGuiTextValueUndoType
{
	RG_GUI_TEXT_VALUE_UNDO_TEXT = 0,
	RG_GUI_TEXT_VALUE_UNDO_INT = 1,
	RG_GUI_TEXT_VALUE_UNDO_FLOAT = 2,
	RG_GUI_TEXT_VALUE_UNDO_DOUBLE = 3
} RgGuiTextValueUndoType;

typedef struct RgGuiTextValueUndoRecord
{
	RgGuiId id;
	RgGuiTextValueUndoType type;
	u8 pad0;
	u16 pad1;
	void* target;
	union
	{
		struct
		{
			u32 prev_length;
			u32 next_length;
			size_t capacity;
			char prev_text[RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE];
			char next_text[RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE];
		} text;
		struct
		{
			f64 prev_value;
			f64 next_value;
		} number;
	} data;
} RgGuiTextValueUndoRecord;

typedef struct RgGuiTextEditState
{
	RgGuiId id;
	char* buffer;
	size_t capacity;
	size_t length;
	size_t cursor;
	size_t view_start;
	size_t selection_anchor;
	size_t selection_start;
	size_t selection_end;
	size_t cached_cursor;
	size_t cached_length;
	f32 cached_cursor_x;
	u32 content_version;
#if RG_GUI_TEXT_UNDO_STACK_SIZE > 0 && RG_GUI_TEXT_UNDO_BUFFER_SIZE > 0
	RgGuiTextEditUndoRecord undo_stack[RG_GUI_TEXT_UNDO_STACK_SIZE];
	u32 undo_count;
	u32 undo_index;
	u32 undo_buffer_used;
	char undo_buffer[RG_GUI_TEXT_UNDO_BUFFER_SIZE];
#endif
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
	RgGuiId value_undo_id;
	u32 value_undo_length;
	char value_undo_buffer[RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE];
	int value_undo_active;
#endif
#if RG_GUI_TEXT_PREFIX_CACHE_SIZE > 0
	const char* prefix_cache_text;
	u32 prefix_cache_version;
	u32 prefix_cache_scale_bits;
	u32 prefix_cache_next;
	u32 prefix_cache_count;
	size_t prefix_cache_index[RG_GUI_TEXT_PREFIX_CACHE_SIZE];
	f32 prefix_cache_width[RG_GUI_TEXT_PREFIX_CACHE_SIZE];
#endif
	RgGuiId last_click_id;
	rg_vec2 last_click_pos;
	f32 last_click_time;
	u32 click_count;
	f32 backspace_hold_time;
	f32 backspace_repeat_time;
	char preedit[RG_GUI_IME_PREEDIT_SIZE];
	size_t preedit_length;
	i32 preedit_selection_start;
	i32 preedit_selection_length;
	int dirty;
	int active;
	f32 ordered_wrap_width;
	u32 ordered_config;
} RgGuiTextEditState;

typedef struct RgGuiNumberEditState
{
	RgGuiId id;
	char buffer[RG_GUI_NUMBER_BUFFER_SIZE];
	RgGuiId cached_id;
	f64 cached_value;
	int cached_decimals;
	RgGuiId drag_id;
	rg_vec2 drag_start;
	f64 drag_start_value;
} RgGuiNumberEditState;

typedef struct RgGuiValueCacheEntry
{
	RgGuiId id;
	f64 value;
	int decimals;
	int valid;
	char text[RG_GUI_NUMBER_BUFFER_SIZE];
} RgGuiValueCacheEntry;

typedef struct RgGuiTextMeasureCacheEntry
{
	const char* text;
	size_t length;
	u32 scale_bits;
	f32 width;
} RgGuiTextMeasureCacheEntry;

typedef struct RgGuiTextLengthCacheEntry
{
	const char* text;
	size_t length;
} RgGuiTextLengthCacheEntry;

typedef enum RgGuiMenuItemFlags
{
	RG_GUI_MENU_ITEM_NONE = 0u,
	RG_GUI_MENU_ITEM_DISABLED = 1u << 0,
	RG_GUI_MENU_ITEM_CHECKABLE = 1u << 1,
	RG_GUI_MENU_ITEM_CHECKED = 1u << 2,
	RG_GUI_MENU_ITEM_RADIO = 1u << 3,
	RG_GUI_MENU_ITEM_SUBMENU = 1u << 4
} RgGuiMenuItemFlags;

typedef enum RgGuiSelectionFlags
{
	RG_GUI_SELECTION_NONE = 0u,
	RG_GUI_SELECTION_TOGGLE = 1u << 0,
	RG_GUI_SELECTION_RANGE = 1u << 1
} RgGuiSelectionFlags;

typedef struct RgGuiMenuWidthCacheEntry
{
	RgGuiId id;
	const char* const* items;
	const char* const* shortcuts;
	const RgGuiMenuItemFlags* flags;
	u32 count;
	f32 max_text_width;
	f32 max_shortcut_width;
	u32 flags_mask;
} RgGuiMenuWidthCacheEntry;

typedef struct RgGuiTabScrollCacheEntry
{
	RgGuiId id;
	f32 scroll;
} RgGuiTabScrollCacheEntry;

typedef enum RgGuiMouseCursor
{
	RG_GUI_CURSOR_DEFAULT = 0,
	RG_GUI_CURSOR_TEXT = 1,
	RG_GUI_CURSOR_MOVE = 2,
	RG_GUI_CURSOR_RESIZE_H = 3,
	RG_GUI_CURSOR_RESIZE_V = 4
} RgGuiMouseCursor;

typedef struct RgGuiPlatformOutput
{
	RgGuiMouseCursor cursor;
	RgGuiRect ime_caret_rect;
	int wants_text_input;
	int ime_caret_valid;
} RgGuiPlatformOutput;

#if defined(RG_GUI_ENABLE_VIEWPORTS)
typedef struct RgGuiViewport
{
	RgGuiId id;
	RgGuiRect bounds;
	rg_vec2 origin;
	RgGuiDrawList draw_list;
	RgGuiDrawList overlay_list;
	u32 overlay_start;
	RgGuiId focus_id;
	RgGuiId active_id;
	RgGuiId scroll_owner;
	RgGuiId key_repeat_owner;
	SDL_Scancode key_repeat_key;
	f32 key_repeat_hold_time;
	f32 key_repeat_repeat_time;
	RgGuiId menu_bar_id;
	int menu_bar_block_left;
	int menu_bar_block_right;
	f32 cursor_blink_timer;
	int cursor_visible;
	RgGuiTextEditState text_edit;
	RgGuiNumberEditState number_edit;
	RgGuiPlatformOutput platform_output;
	int window_focused;
	int active;
} RgGuiViewport;

typedef struct RgGuiViewportStackEntry
{
	RgGuiDrawList* prev_draw_target;
	RgGuiDrawList* prev_overlay_target;
	RgGuiRect prev_window_bounds;
	int prev_window_bounds_set;
	const RgInputState* prev_input;
	rg_vec2 prev_viewport_origin;
	rg_vec2 prev_mouse_pos_global;
	RgGuiId prev_viewport_id;
	rg_vec2 prev_mouse_pos;
	rg_vec2 prev_mouse_pos_raw;
	int prev_mouse_down;
	int prev_mouse_pressed;
	int prev_mouse_released;
	f32 prev_mouse_wheel;
	const RgInputEventQueue* prev_input_events;
	SDL_WindowID prev_input_window_id;
	RgGuiId prev_input_event_focus_id;
	RgGuiId prev_input_events_processed_id;
	RgGuiId prev_input_events_changed_id;
	RgGuiId prev_input_events_submit_id;
	int prev_input_window_focused_at_frame_start;
	int prev_window_focused;
	RgGuiMouseCursor prev_mouse_cursor;
	RgGuiPlatformOutput prev_platform_output;
	f32 prev_cursor_blink_timer;
	int prev_cursor_visible;
	RgGuiId prev_hot_id;
	RgGuiId prev_active_id;
	RgGuiId prev_focus_id;
	RgGuiId prev_scroll_owner;
	RgGuiId prev_scroll_owner_next;
	RgGuiId prev_tab_first_id;
	RgGuiId prev_tab_last_id;
	RgGuiId prev_tab_prev_id;
	RgGuiId prev_tab_focus_id;
	int prev_tab_dir;
	int prev_tab_seek;
	int prev_tab_wrap;
	int prev_tab_found_focus;
	RgGuiId prev_key_repeat_owner;
	SDL_Scancode prev_key_repeat_key;
	f32 prev_key_repeat_hold_time;
	f32 prev_key_repeat_repeat_time;
	RgGuiId prev_menu_bar_id;
	int prev_menu_bar_block_left;
	int prev_menu_bar_block_right;
	RgGuiTextEditState* prev_text_edit_state;
	RgGuiNumberEditState* prev_number_edit_state;
} RgGuiViewportStackEntry;
#endif

typedef struct RgGuiContext
{
	RgGuiStyle style;
	RgGuiIcon arrow_icons[RG_GUI_ARROW_COUNT];
	RgGuiIcon close_icon;
	RgGuiDrawList draw_list;
	RgGuiDrawList overlay_list;
	RgGuiDrawList* draw_target;
	RgGuiDrawList* overlay_target;
	RgGuiDrawList* modal_base_target;
	u32 overlay_start;
#if defined(RG_GUI_ENABLE_VIEWPORTS)
	RgGuiViewport* viewports;
	u32 viewport_capacity;
	u32 viewport_active_count;
	RgGuiViewport* viewport_active[RG_GUI_MAX_VIEWPORTS];
	RgGuiViewportStackEntry viewport_stack[RG_GUI_VIEWPORT_STACK_MAX];
	u32 viewport_stack_top;
	RgGuiId mouse_focus_viewport;
	int mouse_focus_inside;
#endif

	char* text_buffer;
	size_t text_buffer_used;
	size_t text_buffer_capacity;
	const RgTextFont* font;
	RgGuiImeCallback ime_callback;
	void* ime_callback_user;
	RgGuiTextMeasureCacheEntry* text_measure_cache;
	u32 text_measure_cache_capacity;
	u32 text_measure_cache_mask;
	RgGuiTextLengthCacheEntry* text_length_cache;
	u32 text_length_cache_capacity;
	u32 text_length_cache_mask;
	RgGuiMenuWidthCacheEntry* menu_width_cache;
	u32 menu_width_cache_capacity;
	u32 menu_width_cache_mask;
	RgGuiTabScrollCacheEntry* tab_scroll_cache;
	u32 tab_scroll_cache_count;
	u32 tab_scroll_cache_capacity;
	u32 tab_scroll_cache_mask;
	u32 tab_scroll_cache_next;
	RgGuiValueCacheEntry* value_cache;
	u32 value_cache_count;
	u32 value_cache_capacity;
	u32 value_cache_mask;
	u32 value_cache_next;
#if RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE > 0
	RgGuiTextValueUndoRecord value_undo_stack[RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE];
	u32 value_undo_count;
	u32 value_undo_index;
#endif

	const RgInputState* input;
	const RgInputEventQueue* input_events;
	SDL_WindowID input_window_id;
	RgGuiId input_event_focus_id;
	RgGuiId input_events_processed_id;
	int input_window_focused_at_frame_start;
	int window_focused;
	RgGuiPlatformOutput platform_output;
	rg_vec2 mouse_pos;
	rg_vec2 mouse_pos_raw;
	rg_vec2 mouse_pos_global;
	rg_vec2 viewport_origin;
	RgGuiId viewport_id;
	int mouse_down;
	int mouse_down_any;
	int mouse_pressed;
	int mouse_released;
	f32 mouse_wheel;
	RgGuiMouseCursor mouse_cursor;
	int input_capture_active;
	int input_capture_next;
	u32 input_capture_depth;
	int modal_active;
	int modal_opened;
	u32 modal_depth;
	RgGuiRect window_bounds;
	u32 window_order_next;
	int window_bounds_set;
	RgGuiDockSpaceState** dockspaces;
	u32 dockspace_count;
	u32 dockspace_capacity;
	RgGuiDockNode* dock_nodes;
	u32 dock_node_count;
	u32 dock_node_capacity;
	u32 dock_node_free;
	RgGuiDockTab* dock_tabs;
	u32 dock_tab_count;
	u32 dock_tab_capacity;
	u32 dock_tab_free;
	RgGuiDockDragState dock_drag;

	RgGuiId hot_id;
	RgGuiId active_id;
	RgGuiId focus_id;
	RgGuiId scroll_owner;
	RgGuiId scroll_owner_next;
	RgGuiId tab_first_id;
	RgGuiId tab_last_id;
	RgGuiId tab_prev_id;
	RgGuiId tab_focus_id;
	int tab_dir;
	int tab_seek;
	int tab_wrap;
	int tab_found_focus;
	RgGuiId key_repeat_owner;
	SDL_Scancode key_repeat_key;
	f32 key_repeat_hold_time;
	f32 key_repeat_repeat_time;
	RgGuiId menu_bar_id;
	int menu_bar_block_left;
	int menu_bar_block_right;
	u32 disabled_depth;
	u32 disabled_stack_top;
	u8 disabled_stack[RG_GUI_DISABLED_STACK_MAX];

	RgGuiId id_stack[RG_GUI_ID_STACK_MAX];
	u32 id_stack_top;
	RgGuiStyleVarStackEntry style_var_stack[RG_GUI_STYLE_VAR_STACK_MAX];
	u32 style_var_stack_top;
	RgGuiStyleColorStackEntry style_color_stack[RG_GUI_STYLE_COLOR_STACK_MAX];
	u32 style_color_stack_top;
	f32 item_width_stack[RG_GUI_ITEM_WIDTH_STACK_MAX];
	u32 item_width_stack_top;
	RgGuiPopupStackEntry popup_stack[RG_GUI_POPUP_STACK_MAX];
	u32 popup_stack_top;

	RgGuiLayout layout;
	RgGuiTextEditState text_edit;
	RgGuiNumberEditState number_edit;
	RgGuiTextEditState* text_edit_state;
	RgGuiNumberEditState* number_edit_state;
	RgGuiDragPayload drag_payload;
	RgGuiId drag_source_id;
	rg_vec2 drag_start;
	int drag_active;
	RgGuiDragPayloadStorage drag_payload_storage;

	f32 time;
	f32 delta_time;
	f32 cursor_blink_timer;
	int cursor_visible;
	RgGuiId input_events_changed_id;
	RgGuiId input_events_submit_id;
	RgGuiDiagnostics diagnostics;
	const RgGuiTextLookup* text_lookup;
} RgGuiContext;

typedef struct RgGuiInputRouter
{
	RgGuiId focus_viewport;
	RgGuiId capture_viewport;
	RgGuiId mouse_focus_viewport;
	int mouse_focus_inside;
	RgGuiId ids[RG_GUI_INPUT_ROUTE_MAX];
	int last_x[RG_GUI_INPUT_ROUTE_MAX];
	int last_y[RG_GUI_INPUT_ROUTE_MAX];
	u8 last_valid[RG_GUI_INPUT_ROUTE_MAX];
	u32 count;
	const RgInputState* input;
	rg_vec2 mouse_global;
	int left_down;
	int left_pressed;
	f32 mouse_wheel;
	int press_claimed;
	int capture_release_pending;
} RgGuiInputRouter;

typedef struct RgGuiInitDesc
{
	const RgTextFont* font;
	u32 max_draw_cmds;
	size_t text_buffer_size;
	RgGuiImeCallback ime_callback;
	void* ime_callback_user;
	u32 value_cache_size;
	u32 text_measure_cache_size;
	u32 text_length_cache_size;
	u32 menu_width_cache_size;
	u32 tab_scroll_cache_size;
	const RgGuiTextLookup* text_lookup; // Optional; ignored when its font differs from font.
} RgGuiInitDesc;

typedef enum RgGuiTextInputFlags
{
	RG_GUI_TEXT_INPUT_NONE = 0u,
	RG_GUI_TEXT_INPUT_NO_LABEL = 1u << 0,
	RG_GUI_TEXT_INPUT_READONLY = 1u << 1,
	RG_GUI_TEXT_INPUT_FILTER_NUMERIC = 1u << 2,
	RG_GUI_TEXT_INPUT_FILTER_HEX = 1u << 3,
	RG_GUI_TEXT_INPUT_UNDO_VALUE = 1u << 4
} RgGuiTextInputFlags;

typedef enum RgGuiShortcutMods
{
	RG_GUI_SHORTCUT_MOD_NONE = 0u,
	RG_GUI_SHORTCUT_MOD_CTRL = 1u << 0,
	RG_GUI_SHORTCUT_MOD_SHIFT = 1u << 1,
	RG_GUI_SHORTCUT_MOD_ALT = 1u << 2
} RgGuiShortcutMods;

typedef enum RgGuiShortcutFlags
{
	RG_GUI_SHORTCUT_NONE = 0u,
	RG_GUI_SHORTCUT_ALLOW_EXTRA_MODS = 1u << 0,
	RG_GUI_SHORTCUT_ALLOW_WHEN_TEXT_ACTIVE = 1u << 1,
	RG_GUI_SHORTCUT_ALLOW_WHEN_CAPTURED = 1u << 2,
	RG_GUI_SHORTCUT_ALLOW_WHEN_MODAL = 1u << 3
} RgGuiShortcutFlags;

typedef enum RgGuiContextMenuFlags
{
	RG_GUI_CONTEXT_MENU_NONE = 0u,
	RG_GUI_CONTEXT_MENU_ALLOW_WHEN_TEXT_ACTIVE = 1u << 0,
	RG_GUI_CONTEXT_MENU_ALLOW_WHEN_CAPTURED = 1u << 1,
	RG_GUI_CONTEXT_MENU_ALLOW_WHEN_MODAL = 1u << 2
} RgGuiContextMenuFlags;

typedef struct RgGuiShortcut
{
	SDL_Scancode key;
	u32 mods;
} RgGuiShortcut;

typedef enum RgGuiWindowFlags
{
	RG_GUI_WINDOW_NONE = 0u,
	RG_GUI_WINDOW_MOVABLE = 1u << 0,
	RG_GUI_WINDOW_RESIZABLE = 1u << 1,
	RG_GUI_WINDOW_COLLAPSIBLE = 1u << 2,
	RG_GUI_WINDOW_CLOSABLE = 1u << 3,
	RG_GUI_WINDOW_SNAP = 1u << 4,
	RG_GUI_WINDOW_DOCKABLE = 1u << 5
} RgGuiWindowFlags;

typedef enum RgGuiWindowDockFlags
{
	RG_GUI_WINDOW_DOCK_NONE = 0u,
	RG_GUI_WINDOW_DOCK_LEFT = 1u << 0,
	RG_GUI_WINDOW_DOCK_RIGHT = 1u << 1,
	RG_GUI_WINDOW_DOCK_TOP = 1u << 2,
	RG_GUI_WINDOW_DOCK_BOTTOM = 1u << 3
} RgGuiWindowDockFlags;

typedef enum RgGuiTableColumnFlags
{
	RG_GUI_TABLE_COLUMN_NONE = 0u,
	RG_GUI_TABLE_COLUMN_RESIZE = 1u << 0,
	RG_GUI_TABLE_COLUMN_SORT = 1u << 1,
	RG_GUI_TABLE_COLUMN_NO_HIDE = 1u << 2,
	RG_GUI_TABLE_COLUMN_NO_REORDER = 1u << 3
} RgGuiTableColumnFlags;

typedef enum RgGuiTableResult
{
	RG_GUI_TABLE_RESULT_NONE = 0u,
	RG_GUI_TABLE_RESULT_SELECTION = 1u << 0,
	RG_GUI_TABLE_RESULT_SORT = 1u << 1,
	RG_GUI_TABLE_RESULT_RESIZE = 1u << 2
} RgGuiTableResult;

typedef enum RgGuiTreeNodeResult
{
	RG_GUI_TREE_NODE_RESULT_NONE = 0u,
	RG_GUI_TREE_NODE_RESULT_SELECTION = 1u << 0,
	RG_GUI_TREE_NODE_RESULT_TOGGLE = 1u << 1
} RgGuiTreeNodeResult;

typedef enum RgGuiColorPickerFlags
{
	RG_GUI_COLOR_PICKER_NONE = 0u,
	RG_GUI_COLOR_PICKER_NO_ALPHA = 1u << 0
} RgGuiColorPickerFlags;

typedef enum RgGuiCurveFlags
{
	RG_GUI_CURVE_NONE = 0u,
	RG_GUI_CURVE_NO_GRID = 1u << 0,
	RG_GUI_CURVE_NO_ADD = 1u << 1,
	RG_GUI_CURVE_NO_REMOVE = 1u << 2
} RgGuiCurveFlags;

typedef enum RgGuiGradientFlags
{
	RG_GUI_GRADIENT_NONE = 0u,
	RG_GUI_GRADIENT_NO_ALPHA = 1u << 0,
	RG_GUI_GRADIENT_NO_ADD = 1u << 1,
	RG_GUI_GRADIENT_NO_REMOVE = 1u << 2
} RgGuiGradientFlags;

typedef enum RgGuiNodeMinimapFlags
{
	RG_GUI_NODE_MINIMAP_NONE = 0u,
	RG_GUI_NODE_MINIMAP_NO_INTERACT = 1u << 0
} RgGuiNodeMinimapFlags;

typedef struct RgGuiGradientStop
{
	f32 position;
	rg_vec4 color;
} RgGuiGradientStop;

typedef enum RgGuiDragResult
{
	RG_GUI_DRAG_RESULT_NONE = 0u,
	RG_GUI_DRAG_RESULT_HOVER = 1u << 0,
	RG_GUI_DRAG_RESULT_ACCEPT = 1u << 1,
	RG_GUI_DRAG_RESULT_DROP = 1u << 2
} RgGuiDragResult;

typedef enum RgGuiDragTargetFlags
{
	RG_GUI_DRAG_TARGET_NONE = 0u,
	RG_GUI_DRAG_TARGET_NO_ACCEPT = 1u << 0
} RgGuiDragTargetFlags;

// =============================================================================
// PUBLIC API
// =============================================================================

/**
 * @brief Initialize GUI context and buffers
 * @param ctx GUI context
 * @param arena Arena used for persistent buffers
 * @param desc Required init descriptor; desc->font must point to a valid font. Zero-valued
 *             capacity fields select their documented defaults.
 */
RGINLINE int rg_gui_init(RgGuiContext* ctx, RgArena* arena, const RgGuiInitDesc* desc);

/**
 * @brief Build optional caller-owned ASCII tables for an immutable font
 * @return 1 on success, or 0 for an invalid font/lookup; a supplied lookup is cleared on failure
 */
RGINLINE int rg_gui_text_lookup_init(RgGuiTextLookup* lookup, const RgTextFont* font);

/**
 * @brief Return an alignment-safe upper bound for the persistent arena bytes used by rg_gui_init
 * @param desc Required init descriptor, with the same defaults and validation as rg_gui_init
 * @return Required byte capacity for an otherwise empty arena, or 0 for invalid input/overflow
 */
RGINLINE size_t rg_gui_memory_required(const RgGuiInitDesc* desc);

/**
 * @brief Set IME caret callback for text input
 * @param ctx GUI context
 * @param callback Callback for caret rect (NULL to disable)
 * @param user_data User pointer passed to callback
 */
RGINLINE void rg_gui_set_ime_callback(RgGuiContext* ctx, RgGuiImeCallback callback, void* user_data);

/**
 * @brief Set an optional directional arrow icon (NULL or an invalid icon restores the fallback)
 */
RGINLINE void rg_gui_set_arrow_icon(RgGuiContext* ctx, RgGuiArrowDirection direction,
                                    const RgGuiIcon* icon);

/**
 * @brief Set the optional window and dock-tab close icon (NULL or invalid restores the text fallback)
 */
RGINLINE void rg_gui_set_close_icon(RgGuiContext* ctx, const RgGuiIcon* icon);

/**
 * @brief Begin a GUI frame
 * @param ctx GUI context
 * @param input Input state (rg_input)
 * @param delta_time Frame delta time
 */
RGINLINE void rg_gui_begin_frame(RgGuiContext* ctx, const RgInputState* input, f32 delta_time);

/**
 * @brief Begin a GUI frame with an optional ordered event queue
 * @param ctx GUI context
 * @param input Snapshot input state used by pointer and legacy widget paths
 * @param events Optional ordered input queue used by focused text editing
 * @param window_id SDL window id accepted by ordered events (0 accepts all)
 * @param delta_time Frame delta time
 */
RGINLINE void rg_gui_begin_frame_ex(RgGuiContext* ctx, const RgInputState* input,
                                    const RgInputEventQueue* events,
                                    SDL_WindowID window_id, f32 delta_time);

/**
 * @brief Return platform-facing cursor, text-input, and IME caret requests
 */
RGINLINE const RgGuiPlatformOutput* rg_gui_platform_output(const RgGuiContext* ctx);

/**
 * @brief Initialize an input router for global-to-viewport input filtering
 * @param router Input router state
 */
RGINLINE void rg_gui_input_router_init(RgGuiInputRouter* router);

/**
 * @brief Begin input routing for a frame
 * @param router Input router state
 * @param input Global input state (rg_input)
 * @param mouse_global Mouse position in global coordinates
 * @param focus_override Viewport id that should receive keyboard input (0 to keep current)
 * @param mouse_focus_viewport Viewport id that currently has mouse focus (0 for none)
 * @param mouse_focus_inside Non-zero when the mouse focus viewport contains the cursor
 */
RGINLINE void rg_gui_input_router_begin(RgGuiInputRouter* router, const RgInputState* input,
                                        rg_vec2 mouse_global, RgGuiId focus_override, RgGuiId mouse_focus_viewport,
                                        int mouse_focus_inside);

/**
 * @brief End input routing for a frame (clears pending capture release)
 * @param router Input router state
 */
RGINLINE void rg_gui_input_router_end(RgGuiInputRouter* router);

/**
 * @brief Filter input for a viewport (returns non-zero when mouse input is routed to this viewport)
 * @param router Input router state
 * @param viewport_id Non-zero viewport identifier
 * @param origin Viewport origin in global coordinates
 * @param pixel_w Viewport width in pixels
 * @param pixel_h Viewport height in pixels
 * @param out_input Filtered input for this viewport
 */
RGINLINE int rg_gui_input_router_route(RgGuiInputRouter* router, RgGuiId viewport_id,
                                       rg_vec2 origin, int pixel_w, int pixel_h, RgInputState* out_input);

/**
 * @brief Return viewport id that owns keyboard focus
 * @param router Input router state
 */
RGINLINE RgGuiId rg_gui_input_router_focus(const RgGuiInputRouter* router);

/**
 * @brief Return viewport id that owns mouse capture
 * @param router Input router state
 */
RGINLINE RgGuiId rg_gui_input_router_capture(const RgGuiInputRouter* router);

/**
 * @brief End a GUI frame
 * @param ctx GUI context
 */
RGINLINE void rg_gui_end_frame(RgGuiContext* ctx);

/**
 * @brief Push an ID onto the stack to scope subsequent widget IDs
 */
RGINLINE void rg_gui_push_id(RgGuiContext* ctx, RgGuiId id);
RGINLINE void rg_gui_push_id_u64(RgGuiContext* ctx, u64 value);
RGINLINE void rg_gui_push_id_ptr(RgGuiContext* ctx, const void* ptr);
#if !defined(RG_GUI_NO_STRING_IDS)
RGINLINE void rg_gui_push_id_str(RgGuiContext* ctx, const char* str);
RGINLINE void rg_gui_push_id_str_len(RgGuiContext* ctx, const char* str, size_t len);
#endif
RGINLINE void rg_gui_pop_id(RgGuiContext* ctx);
RGINLINE RgGuiId rg_gui_id_scoped(const RgGuiContext* ctx, RgGuiId id);

/**
 * @brief Push/pull style overrides
 */
RGINLINE void rg_gui_push_style_var(RgGuiContext* ctx, RgGuiStyleVar var, f32 value);
RGINLINE void rg_gui_push_style_var_int(RgGuiContext* ctx, RgGuiStyleVar var, int value);
RGINLINE void rg_gui_pop_style_var(RgGuiContext* ctx, u32 count);
RGINLINE void rg_gui_push_style_color(RgGuiContext* ctx, RgGuiStyleColor color, rg_vec4 value);
RGINLINE void rg_gui_pop_style_color(RgGuiContext* ctx, u32 count);

/**
 * @brief Start a vertical layout block
 */
RGINLINE void rg_gui_layout_begin(RgGuiContext* ctx, f32 x, f32 y, f32 width, f32 spacing);

/**
 * @brief Advance layout and return next rectangle
 */
RGINLINE RgGuiRect rg_gui_layout_next(RgGuiContext* ctx, f32 height);

/**
 * @brief Advance layout cursor by custom height
 */
RGINLINE void rg_gui_layout_advance(RgGuiContext* ctx, f32 height);

/**
 * @brief Begin a horizontal row layout
 */
RGINLINE void rg_gui_layout_row_begin(RgGuiContext* ctx, f32 height, f32 spacing);

/**
 * @brief Return next rectangle within the active row
 */
RGINLINE RgGuiRect rg_gui_layout_row_next(RgGuiContext* ctx, f32 width);

/**
 * @brief End a horizontal row layout
 */
RGINLINE void rg_gui_layout_row_end(RgGuiContext* ctx);

/**
 * @brief Set width for the next row item (cleared after use)
 */
RGINLINE void rg_gui_set_next_item_width(RgGuiContext* ctx, f32 width);

/**
 * @brief Push default row item width
 */
RGINLINE void rg_gui_push_item_width(RgGuiContext* ctx, f32 width);

/**
 * @brief Pop row item width
 */
RGINLINE void rg_gui_pop_item_width(RgGuiContext* ctx, u32 count);

/**
 * @brief Begin a multi-column layout helper
 */
RGINLINE void rg_gui_columns_begin(RgGuiColumns* cols, RgGuiRect rect, const f32* widths, u32 count, f32 spacing);

/**
 * @brief Advance to the next column cell
 */
RGINLINE RgGuiRect rg_gui_columns_next(RgGuiColumns* cols, f32 height);

/**
 * @brief Begin a tree view block (resets depth)
 */
RGINLINE void rg_gui_tree_begin(RgGuiContext* ctx, RgGuiTreeState* tree, f32 indent);

/**
 * @brief Increase tree indentation depth
 */
RGINLINE void rg_gui_tree_push(RgGuiTreeState* tree);

/**
 * @brief Decrease tree indentation depth
 */
RGINLINE void rg_gui_tree_pop(RgGuiTreeState* tree);

/**
 * @brief Begin a drag source within a rect
 * @return 1 while dragging is active for this source
 */
RGINLINE int rg_gui_drag_source(RgGuiContext* ctx, RgGuiRect rect, RgGuiId id);

/**
 * @brief Set drag payload data (copied if small enough)
 * @return 1 if payload recorded
 */
RGINLINE int rg_gui_drag_set_payload(RgGuiContext* ctx, RgGuiId type, const void* data, size_t size);

/**
 * @brief Drag target (hover/accept) helper
 * @return Bitmask of RgGuiDragResult flags
 */
RGINLINE u32 rg_gui_drag_target(RgGuiContext* ctx, RgGuiRect rect, RgGuiId accept_type, RgGuiDragPayload* out_payload);

/**
 * @brief Drag target helper with manual accept/reject
 * @return Bitmask of RgGuiDragResult flags
 */
RGINLINE u32 rg_gui_drag_target_ex(RgGuiContext* ctx, RgGuiRect rect, RgGuiId accept_type,
                                   u32 flags, RgGuiDragPayload* out_payload);

/**
 * @brief Current drag payload (NULL if inactive)
 */
RGINLINE const RgGuiDragPayload* rg_gui_drag_payload(const RgGuiContext* ctx);

/**
 * @brief Accept the current drag payload (clears drag)
 * @return 1 on success, 0 if no active payload
 */
RGINLINE int rg_gui_drag_accept(RgGuiContext* ctx, RgGuiDragPayload* out_payload);

/**
 * @brief Reject the current drag payload (clears drag)
 */
RGINLINE void rg_gui_drag_reject(RgGuiContext* ctx);

/**
 * @brief Build a drag preview rectangle near the cursor
 */
RGINLINE RgGuiRect rg_gui_drag_preview_rect(const RgGuiContext* ctx, f32 width, f32 height,
                                            rg_vec2 offset, int clamp_to_window);

/**
 * @brief Begin a context menu trigger (right-click within rect)
 * @return 1 if menu is open
 */
RGINLINE int rg_gui_context_menu_begin(RgGuiContext* ctx, RgGuiPopupState* popup, RgGuiRect rect, RgGuiId id);
/**
 * @brief Begin a context menu trigger with gating flags
 * @return 1 if menu is open
 */
RGINLINE int rg_gui_context_menu_begin_ex(RgGuiContext* ctx, RgGuiPopupState* popup, RgGuiRect rect,
                                          RgGuiId id, u32 flags);

/**
 * @brief Begin a popup container
 * @return 1 if popup is open and content should be drawn
 */
RGINLINE int rg_gui_popup_begin(RgGuiContext* ctx, RgGuiPopupState* popup, RgGuiRect rect, f32 spacing, RgGuiId id);

/**
 * @brief End a popup container
 */
RGINLINE void rg_gui_popup_end(RgGuiContext* ctx);

/**
 * @brief Begin a modal popup container (blocks input outside)
 * @return 1 if modal is open and content should be drawn
 */
RGINLINE int rg_gui_modal_begin(RgGuiContext* ctx, RgGuiPopupState* popup, RgGuiRect rect, f32 spacing, RgGuiId id);

/**
 * @brief End a modal popup container
 */
RGINLINE void rg_gui_modal_end(RgGuiContext* ctx);

/**
 * @brief Menu bar helper (hover-to-switch, click-to-open, keyboard left/right, Alt+letter)
 * @note Pass this same id to the paired rg_gui_menu_popup function.
 * @return 1 if active/open changed
 */
RGINLINE int rg_gui_menu_bar(RgGuiContext* ctx, const char* const* labels, u32 count, int* active, int* open,
                             RgGuiRect rect, RgGuiId id, RgGuiRect* out_active_rect);

/**
 * @brief Popup menu list (overlay)
 * @note When paired with rg_gui_menu_bar, pass the menu bar's id.
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_menu_popup_ex(RgGuiContext* ctx, const char* const* items, const char* const* shortcuts,
                                  const RgGuiMenuItemFlags* flags, u32 count, int* selected,
                                  int* open, int* scroll, RgGuiRect rect, RgGuiId id,
                                  int* out_hovered, RgGuiRect* out_hovered_rect, int* out_list_hovered,
                                  RgGuiRect* out_selected_rect, int allow_keyboard);

/**
 * @brief Popup menu list with an optional icon for each item
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_menu_popup_icons_ex(RgGuiContext* ctx, const char* const* items, const char* const* shortcuts,
                                        const RgGuiIcon* icons, const RgGuiMenuItemFlags* flags,
                                        u32 count, int* selected, int* open, int* scroll,
                                        RgGuiRect rect, RgGuiId id, int* out_hovered,
                                        RgGuiRect* out_hovered_rect, int* out_list_hovered,
                                        RgGuiRect* out_selected_rect, int allow_keyboard);

/**
 * @brief Popup menu list (overlay)
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_menu_popup(RgGuiContext* ctx, const char* const* items, u32 count, int* selected,
                               int* open, int* scroll, RgGuiRect rect, RgGuiId id);

/**
 * @brief Tooltip helper (dynamic text)
 */
RGINLINE void rg_gui_tooltip(RgGuiContext* ctx, RgGuiRect rect, const char* text);

/**
 * @brief Tooltip helper (text address and bytes stay unchanged for the GUI context lifetime)
 */
RGINLINE void rg_gui_tooltip_static(RgGuiContext* ctx, RgGuiRect rect, const char* text);

/**
 * @brief Begin a movable/resizable window
 * @return 1 if open and content should be drawn
 */
RGINLINE int rg_gui_window_begin(RgGuiContext* ctx, RgGuiWindowState* window, const char* title, RgGuiRect rect,
                                 f32 min_w, f32 min_h, f32 spacing, u32 flags, RgGuiId id);

/**
 * @brief End a window started with rg_gui_window_begin
 */
RGINLINE void rg_gui_window_end(RgGuiContext* ctx, RgGuiWindowState* window);
/**
 * @brief Set a window title and dock-tab icon (NULL clears it)
 */
RGINLINE void rg_gui_window_set_icon(RgGuiWindowState* window, const RgGuiIcon* icon);
RGINLINE void rg_gui_window_set_bounds(RgGuiContext* ctx, RgGuiRect bounds);
RGINLINE u32 rg_gui_window_order(const RgGuiWindowState* window);
/**
 * @brief Set the current viewport origin in global coordinates
 */
RGINLINE void rg_gui_set_viewport_origin(RgGuiContext* ctx, rg_vec2 origin);

#if defined(RG_GUI_ENABLE_VIEWPORTS)
/**
 * @brief Set the mouse focus viewport hint for docking hit-tests
 * @param ctx GUI context
 * @param viewport_id Viewport id that currently has mouse focus (0 for none)
 * @param mouse_inside Non-zero when the mouse focus viewport contains the cursor
 */
RGINLINE void rg_gui_set_mouse_focus_viewport(RgGuiContext* ctx, RgGuiId viewport_id, int mouse_inside);
/**
 * @brief Begin a secondary viewport (draws into a separate draw list)
 * @return Viewport handle for rendering
 */
RGINLINE RgGuiViewport* rg_gui_viewport_begin(RgGuiContext* ctx, RgGuiId id, RgGuiRect bounds, const RgInputState* input);
/**
 * @brief Begin a secondary viewport with a global origin
 * @return Viewport handle for rendering
 */
RGINLINE RgGuiViewport* rg_gui_viewport_begin_ex(RgGuiContext* ctx, RgGuiId id, RgGuiRect bounds,
                                                 const RgInputState* input, rg_vec2 origin);

/**
 * @brief Begin a secondary viewport with an ordered event queue and window filter
 * @param events Optional ordered input queue used by focused text editing
 * @param window_id SDL window id accepted by ordered events (0 accepts all)
 * @return Viewport handle for rendering
 */
RGINLINE RgGuiViewport* rg_gui_viewport_begin_ordered_ex(
    RgGuiContext* ctx, RgGuiId id, RgGuiRect bounds,
    const RgInputState* input, const RgInputEventQueue* events,
    SDL_WindowID window_id, rg_vec2 origin);

/**
 * @brief End a viewport begun with rg_gui_viewport_begin
 */
RGINLINE void rg_gui_viewport_end(RgGuiContext* ctx, RgGuiViewport* viewport);

/**
 * @brief Release a viewport by ID so it can be reused
 */
RGINLINE void rg_gui_viewport_release(RgGuiContext* ctx, RgGuiId id);

/**
 * @brief Return active viewport count for the current frame
 */
RGINLINE u32 rg_gui_viewport_count(const RgGuiContext* ctx);

/**
 * @brief Return active viewport by index
 */
RGINLINE const RgGuiViewport* rg_gui_viewport_at(const RgGuiContext* ctx, u32 index);

/**
 * @brief Return the draw list for a viewport
 */
RGINLINE const RgGuiDrawList* rg_gui_viewport_draw_list(const RgGuiViewport* viewport);

/**
 * @brief Return overlay split index for a viewport draw list
 */
RGINLINE u32 rg_gui_viewport_overlay_start(const RgGuiViewport* viewport);

/**
 * @brief Return platform-facing output produced by the viewport's latest build
 */
RGINLINE const RgGuiPlatformOutput* rg_gui_viewport_platform_output(
    const RgGuiViewport* viewport);
#endif
/**
 * @brief Begin a dockspace region for docking windows
 */
RGINLINE void rg_gui_dockspace_begin(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, RgGuiRect rect, RgGuiId id);
/**
 * @brief End a dockspace region
 */
RGINLINE void rg_gui_dockspace_end(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace);
/**
 * @brief Dock a window into a dockspace (programmatic layout)
 */
RGINLINE void rg_gui_dockspace_dock(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace,
                                    RgGuiWindowState* window, const char* title, RgGuiId window_id, RgGuiDockSlot slot);
/**
 * @brief Save a dockspace layout into caller-provided arrays (1-based indices, element 0 unused)
 * @details node_capacity and tab_capacity count usable entries; each backing array must contain
 *          capacity + 1 elements because index 0 is reserved.
 * @return 1 on success, 0 if capacity is insufficient or inputs are invalid
 */
RGINLINE int rg_gui_dock_layout_save(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, RgGuiDockLayout* out_layout,
                                     RgGuiDockLayoutNode* out_nodes, u32 node_capacity,
                                     RgGuiDockLayoutTab* out_tabs, u32 tab_capacity);
/**
 * @brief Load a sole dockspace layout from arrays
 * @details On success, rebuilds the context's shared dock storage and clears every registered
 *          dockspace root before assigning this dockspace. Failure leaves live dock state unchanged.
 *          Arrays are 1-based and must contain node_count + 1 and tab_count + 1 elements.
 * @return 1 on success, 0 if inputs are invalid or capacity is insufficient
 */
RGINLINE int rg_gui_dock_layout_load(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, const RgGuiDockLayout* layout,
                                     const RgGuiDockLayoutNode* nodes, const RgGuiDockLayoutTab* tabs);
/**
 * @brief Write a dock layout blob to a FILE* (binary, little-endian)
 * @details Input arrays are 1-based and must contain node_count + 1 and tab_count + 1 elements.
 * @return 1 on success, 0 on failure
 */
RGINLINE int rg_gui_dock_layout_write(FILE* file, const RgGuiDockLayout* layout,
                                      const RgGuiDockLayoutNode* nodes, const RgGuiDockLayoutTab* tabs);
/**
 * @brief Read a dock layout blob from a FILE* (binary, little-endian)
 * @details Capacities count usable entries; each non-NULL backing array must contain capacity + 1
 *          elements because index 0 is reserved.
 * @return 1 on success, 0 on failure or invalid data
 */
RGINLINE int rg_gui_dock_layout_read(FILE* file, RgGuiDockLayout* layout,
                                     RgGuiDockLayoutNode* nodes, u32 node_capacity,
                                     RgGuiDockLayoutTab* tabs, u32 tab_capacity);
/**
 * @brief Save a dockspace layout directly to a FILE*
 * @details Capacities count usable entries; each backing array must contain capacity + 1 elements.
 * @return 1 on success, 0 on failure
 */
RGINLINE int rg_gui_dock_layout_save_file(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, FILE* file,
                                          RgGuiDockLayout* out_layout,
                                          RgGuiDockLayoutNode* out_nodes, u32 node_capacity,
                                          RgGuiDockLayoutTab* out_tabs, u32 tab_capacity);
/**
 * @brief Load a sole dockspace layout directly from a FILE*
 * @details Has the same global dock reset and atomic-failure contract as rg_gui_dock_layout_load.
 *          Capacities count usable entries; each backing array must contain capacity + 1 elements.
 * @return 1 on success, 0 on failure
 */
RGINLINE int rg_gui_dock_layout_load_file(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, FILE* file,
                                          RgGuiDockLayout* layout,
                                          RgGuiDockLayoutNode* nodes, u32 node_capacity,
                                          RgGuiDockLayoutTab* tabs, u32 tab_capacity);

/**
 * @brief Initialize a row cache for clipped panels
 */
RGINLINE void rg_gui_row_cache_init(RgGuiRowCache* cache, f32* row_spans, f32* block_spans,
                                    u32 row_capacity, u32 block_capacity);

/**
 * @brief Reset row cache for a new row count or spacing
 * @return 1 if cache is ready, 0 if capacity is insufficient
 */
RGINLINE int rg_gui_row_cache_reset(RgGuiRowCache* cache, u32 row_count, f32 row_height, f32 spacing);

/**
 * @brief Update a cached row height (height excludes spacing)
 */
RGINLINE void rg_gui_row_cache_set_height(RgGuiRowCache* cache, u32 index, f32 row_height);

/**
 * @brief Compute the vertical offset to a row index using cached spans
 */
RGINLINE f32 rg_gui_row_cache_offset(const RgGuiRowCache* cache, u32 index);

/**
 * @brief Begin a scrollable panel with optional title and presentation flags
 */
RGINLINE void rg_gui_panel_begin_ex(RgGuiContext* ctx, RgGuiPanelState* panel,
                                    RgGuiRect rect, f32 spacing, RgGuiId id,
                                    const char* title, RgGuiPanelFlags flags);

/**
 * @brief Begin a scrollable panel
 */
RGINLINE void rg_gui_panel_begin(RgGuiContext* ctx, RgGuiPanelState* panel, RgGuiRect rect, f32 spacing, RgGuiId id);

/**
 * @brief Begin a scrollable panel with virtualized rows
 */
RGINLINE void rg_gui_panel_begin_virtual(RgGuiContext* ctx, RgGuiPanelState* panel, RgGuiRect rect, f32 row_height,
                                         f32 spacing, u32 row_count, RgGuiId id, u32* out_start, u32* out_count);

/**
 * @brief Begin a scrollable panel using cached row heights
 */
RGINLINE void rg_gui_panel_begin_clipped(RgGuiContext* ctx, RgGuiPanelState* panel, RgGuiRect rect, f32 row_height,
                                         f32 spacing, u32 row_count, RgGuiId id, RgGuiRowCache* cache,
                                         u32* out_start, u32* out_count);

/**
 * @brief End a scrollable panel
 */
RGINLINE void rg_gui_panel_end(RgGuiContext* ctx, RgGuiPanelState* panel);

/**
 * @brief Split a rect into left/right panels
 */
RGINLINE void rg_gui_split_rect_v(RgGuiRect rect, f32 split, f32 gutter,
                                  RgGuiRect* out_left, RgGuiRect* out_right, RgGuiRect* out_splitter);

/**
 * @brief Split a rect into top/bottom panels
 */
RGINLINE void rg_gui_split_rect_h(RgGuiRect rect, f32 split, f32 gutter,
                                  RgGuiRect* out_top, RgGuiRect* out_bottom, RgGuiRect* out_splitter);

/**
 * @brief Vertical splitter widget (left/right resize)
 * @return 1 if size changed
 */
RGINLINE int rg_gui_splitter_v(RgGuiContext* ctx, RgGuiRect rect, f32* size, f32 min_size, f32 max_size, RgGuiId id);

/**
 * @brief Horizontal splitter widget (top/bottom resize)
 * @return 1 if size changed
 */
RGINLINE int rg_gui_splitter_h(RgGuiContext* ctx, RgGuiRect rect, f32* size, f32 min_size, f32 max_size, RgGuiId id);

/**
 * @brief Begin a disabled widget block
 */
RGINLINE void rg_gui_begin_disabled(RgGuiContext* ctx, int disabled);

/**
 * @brief End a disabled widget block
 */
RGINLINE void rg_gui_end_disabled(RgGuiContext* ctx);

/**
 * @brief Build an anchor description for a parent rect
 */
RGINLINE RgGuiAnchor rg_gui_anchor_make(rg_vec2 min, rg_vec2 max, rg_vec2 offset_min, rg_vec2 offset_max);

/**
 * @brief Resolve an anchored rectangle within a parent rect
 */
RGINLINE RgGuiRect rg_gui_anchor_rect(RgGuiRect parent, RgGuiAnchor anchor);

/**
 * @brief Resolve a fixed-size rect at a normalized anchor point
 */
RGINLINE RgGuiRect rg_gui_anchor_fixed(RgGuiRect parent, rg_vec2 anchor, rg_vec2 size, rg_vec2 pivot, rg_vec2 offset);

/**
 * @brief Button widget
 * @return 1 if clicked, 0 otherwise
 */
RGINLINE int rg_gui_button(RgGuiContext* ctx, const char* label, RgGuiRect rect, RgGuiId id);

/**
 * @brief Button widget with static label text (stable address and immutable bytes for the GUI context lifetime)
 * @return 1 if clicked, 0 otherwise
 */
RGINLINE int rg_gui_button_static(RgGuiContext* ctx, const char* label, RgGuiRect rect, RgGuiId id);

/**
 * @brief Button widget with an optional leading icon
 * @return 1 if clicked, 0 otherwise
 */
RGINLINE int rg_gui_button_icon(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                RgGuiRect rect, RgGuiId id);

/**
 * @brief Button widget with an optional leading icon and static label text
 * @details The label address and bytes must stay unchanged for the GUI context lifetime.
 * @return 1 if clicked, 0 otherwise
 */
RGINLINE int rg_gui_button_icon_static(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                       RgGuiRect rect, RgGuiId id);

/**
 * @brief Icon-only button with a directional arrow
 * @return 1 if clicked, 0 otherwise
 */
RGINLINE int rg_gui_button_arrow(RgGuiContext* ctx, RgGuiArrowDirection direction,
                                 RgGuiRect rect, RgGuiId id);

/**
 * @brief Label widget (non-interactive)
 */
RGINLINE void rg_gui_label(RgGuiContext* ctx, const char* text, RgGuiRect rect);

/**
 * @brief Label widget with static text (stable address and immutable bytes for the GUI context lifetime)
 */
RGINLINE void rg_gui_label_static(RgGuiContext* ctx, const char* text, RgGuiRect rect);

/**
 * @brief Label widget with an optional leading icon
 */
RGINLINE void rg_gui_label_icon(RgGuiContext* ctx, const char* text, const RgGuiIcon* icon, RgGuiRect rect);

/**
 * @brief Label widget with an optional leading icon and static text
 * @details The text address and bytes must stay unchanged for the GUI context lifetime.
 */
RGINLINE void rg_gui_label_icon_static(RgGuiContext* ctx, const char* text, const RgGuiIcon* icon, RgGuiRect rect);

/**
 * @brief Image widget (non-interactive)
 */
RGINLINE void rg_gui_image(RgGuiContext* ctx, RgGuiTexture texture, RgGuiRect rect);

/**
 * @brief Image widget with an application-defined renderer material
 * @details A zero material is identical to rg_gui_image.
 */
RGINLINE void rg_gui_image_material(RgGuiContext* ctx, RgGuiTexture texture,
                                    RgGuiRect rect, RgGuiImageMaterial material);

/**
 * @brief Image widget with custom UV rect and tint
 */
RGINLINE void rg_gui_image_ex(RgGuiContext* ctx, RgGuiTexture texture, RgGuiRect rect, RgGuiRect uv, rg_vec4 tint);

/**
 * @brief Image widget with custom UV rect, tint, and application-defined material
 * @details A zero material is identical to rg_gui_image_ex.
 */
RGINLINE void rg_gui_image_ex_material(RgGuiContext* ctx, RgGuiTexture texture,
                                       RgGuiRect rect, RgGuiRect uv, rg_vec4 tint,
                                       RgGuiImageMaterial material);

/**
 * @brief Image button widget
 * @return 1 if clicked, 0 otherwise
 */
RGINLINE int rg_gui_image_button(RgGuiContext* ctx, RgGuiTexture texture, RgGuiRect rect, RgGuiId id);

/**
 * @brief Image button widget with an application-defined renderer material
 * @return 1 if clicked, 0 otherwise
 */
RGINLINE int rg_gui_image_button_material(RgGuiContext* ctx, RgGuiTexture texture,
                                          RgGuiRect rect, RgGuiId id,
                                          RgGuiImageMaterial material);

/**
 * @brief Image button widget with custom UV rect and tint
 * @return 1 if clicked, 0 otherwise
 */
RGINLINE int rg_gui_image_button_ex(RgGuiContext* ctx, RgGuiTexture texture, RgGuiRect rect, RgGuiRect uv, rg_vec4 tint, RgGuiId id);

/**
 * @brief Image button widget with custom UV rect, tint, and application-defined material
 * @return 1 if clicked, 0 otherwise
 */
RGINLINE int rg_gui_image_button_ex_material(RgGuiContext* ctx, RgGuiTexture texture,
                                             RgGuiRect rect, RgGuiRect uv, rg_vec4 tint,
                                             RgGuiId id, RgGuiImageMaterial material);

/**
 * @brief Selectable row widget
 * @return 1 if activated, 0 otherwise
 */
RGINLINE int rg_gui_selectable(RgGuiContext* ctx, const char* label, int selected, RgGuiRect rect, RgGuiId id);

/**
 * @brief Selectable row widget with static label text (stable address and immutable bytes for the GUI context lifetime)
 * @return 1 if activated, 0 otherwise
 */
RGINLINE int rg_gui_selectable_static(RgGuiContext* ctx, const char* label, int selected, RgGuiRect rect, RgGuiId id);

/**
 * @brief Selectable row widget with an optional leading icon
 * @return 1 if activated, 0 otherwise
 */
RGINLINE int rg_gui_selectable_icon(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                    int selected, RgGuiRect rect, RgGuiId id);

/**
 * @brief Selectable row widget with an optional leading icon and static label text
 * @details The label address and bytes must stay unchanged for the GUI context lifetime.
 * @return 1 if activated, 0 otherwise
 */
RGINLINE int rg_gui_selectable_icon_static(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                           int selected, RgGuiRect rect, RgGuiId id);

/**
 * @brief Tree node widget
 * @return Bitmask of RgGuiTreeNodeResult flags
 */
RGINLINE u32 rg_gui_tree_node(RgGuiContext* ctx, RgGuiTreeState* tree, const char* label, int* open,
                              int selected, RgGuiRect rect, RgGuiId id);

/**
 * @brief Tree node widget with static label text (stable address and immutable bytes for the GUI context lifetime)
 * @return Bitmask of RgGuiTreeNodeResult flags
 */
RGINLINE u32 rg_gui_tree_node_static(RgGuiContext* ctx, RgGuiTreeState* tree, const char* label, int* open,
                                     int selected, RgGuiRect rect, RgGuiId id);

/**
 * @brief Tree node widget with an optional leading icon
 * @return Bitmask of RgGuiTreeNodeResult flags
 */
RGINLINE u32 rg_gui_tree_node_icon(RgGuiContext* ctx, RgGuiTreeState* tree, const char* label,
                                   const RgGuiIcon* icon, int* open, int selected,
                                   RgGuiRect rect, RgGuiId id);

/**
 * @brief Tree node widget with an optional leading icon and static label text
 * @details The label address and bytes must stay unchanged for the GUI context lifetime.
 * @return Bitmask of RgGuiTreeNodeResult flags
 */
RGINLINE u32 rg_gui_tree_node_icon_static(RgGuiContext* ctx, RgGuiTreeState* tree, const char* label,
                                          const RgGuiIcon* icon, int* open, int selected,
                                          RgGuiRect rect, RgGuiId id);

/**
 * @brief Virtualized tree view widget
 * @return Bitmask of RgGuiTreeNodeResult flags across visible rows
 */
RGINLINE u32 rg_gui_tree_virtual(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                 f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                 RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                 RgGuiTreeRowFn row_fn, const void* user);

/**
 * @brief Virtualized tree view widget with static label text
 * @details Every label returned by get_item must keep a stable address and immutable bytes for the GUI context lifetime.
 * @return Bitmask of RgGuiTreeNodeResult flags across visible rows
 */
RGINLINE u32 rg_gui_tree_virtual_static(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                        f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                        RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                        RgGuiTreeRowFn row_fn, const void* user);

/**
 * @brief Multi-select virtualized tree view widget
 * @return Bitmask of RgGuiTreeNodeResult flags across visible rows
 */
RGINLINE u32 rg_gui_tree_virtual_multi(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                       f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                       RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                       RgGuiTreeRowFn row_fn, const void* user,
                                       RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user);

/**
 * @brief Multi-select virtualized tree view widget with static label text
 * @details Every label returned by get_item must keep a stable address and immutable bytes for the GUI context lifetime.
 * @return Bitmask of RgGuiTreeNodeResult flags across visible rows
 */
RGINLINE u32 rg_gui_tree_virtual_multi_static(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                              f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                              RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                              RgGuiTreeRowFn row_fn, const void* user,
                                              RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user);

/**
 * @brief Checkbox widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_checkbox(RgGuiContext* ctx, const char* label, int* value, RgGuiRect rect, RgGuiId id);

/**
 * @brief Checkbox widget with static label text (stable address and immutable bytes for the GUI context lifetime)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_checkbox_static(RgGuiContext* ctx, const char* label, int* value, RgGuiRect rect, RgGuiId id);

/**
 * @brief Radio widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_radio(RgGuiContext* ctx, const char* label, int* value, int option, RgGuiRect rect, RgGuiId id);

/**
 * @brief Radio widget with static label text (stable address and immutable bytes for the GUI context lifetime)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_radio_static(RgGuiContext* ctx, const char* label, int* value, int option, RgGuiRect rect, RgGuiId id);

/**
 * @brief Text input widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_text_input(RgGuiContext* ctx, const char* label, char* buffer, size_t capacity, RgGuiRect rect, RgGuiId id, int* out_submit);

/**
 * @brief Text input widget with flags
 * @return 1 if value changed
 */
RGINLINE int rg_gui_text_input_ex(RgGuiContext* ctx, const char* label, char* buffer, size_t capacity, RgGuiRect rect, RgGuiId id, RgGuiTextInputFlags flags, int* out_submit);

/**
 * @brief Multiline text area widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_text_area(RgGuiContext* ctx, RgGuiTextAreaState* area, char* buffer, size_t capacity, RgGuiRect rect, RgGuiId id);

/**
 * @brief Initialize a text filter helper (clears buffer and tokens)
 */
RGINLINE void rg_gui_text_filter_init(RgGuiTextFilter* filter);

/**
 * @brief Refresh a text filter after external edits (updates length/version)
 */
RGINLINE void rg_gui_text_filter_refresh(RgGuiTextFilter* filter);

/**
 * @brief Text filter input widget
 * @return 1 if filter text changed
 */
RGINLINE int rg_gui_text_filter_input(RgGuiContext* ctx, const char* label, RgGuiTextFilter* filter, RgGuiRect rect, RgGuiId id);

/**
 * @brief Return non-zero when a text filter is active
 */
RGINLINE int rg_gui_text_filter_active(const RgGuiTextFilter* filter);

/**
 * @brief Test if a string matches the current filter (case-insensitive, space-separated tokens)
 */
RGINLINE int rg_gui_text_filter_match(RgGuiTextFilter* filter, const char* text);

/**
 * @brief Build a filtered index list for virtual lists
 * @return Count of indices written
 */
RGINLINE u32 rg_gui_text_filter_list_indices(RgGuiListItemFn get_item, const void* user, u32 count,
                                             RgGuiTextFilter* filter, u32* out_indices, u32 capacity);

/**
 * @brief Build a filtered row list for tree views (keeps parents of matches)
 * @return Count of rows written
 */
RGINLINE u32 rg_gui_text_filter_tree(const u32* rows, const u32* depths, u32 row_count,
                                     RgGuiListItemFn get_label, const void* user, RgGuiTextFilter* filter,
                                     u8* scratch, u32 scratch_count,
                                     u32* out_rows, u32* out_depths, u32 capacity);

/**
 * @brief Keybind capture widget
 * @return 1 if shortcut changed
 */
RGINLINE int rg_gui_keybind(RgGuiContext* ctx, const char* label, RgGuiShortcut* shortcut, RgGuiRect rect, RgGuiId id);

/**
 * @brief Keybind capture widget with static label text (stable address and immutable bytes for the GUI context lifetime)
 * @return 1 if shortcut changed
 */
RGINLINE int rg_gui_keybind_static(RgGuiContext* ctx, const char* label, RgGuiShortcut* shortcut, RgGuiRect rect, RgGuiId id);

/**
 * @brief Format a shortcut into a buffer (returns NULL when unbound)
 */
RGINLINE const char* rg_gui_shortcut_to_text(RgGuiShortcut shortcut, char* buffer, size_t buffer_size);

/**
 * @brief Check if a shortcut was triggered this frame
 */
RGINLINE int rg_gui_shortcut_triggered(RgGuiContext* ctx, RgGuiShortcut shortcut, u32 flags);

/**
 * @brief Return the index of the first triggered shortcut (or -1 if none)
 */
RGINLINE int rg_gui_shortcut_index(RgGuiContext* ctx, const RgGuiShortcut* shortcuts, u32 count, u32 flags);

/**
 * @brief Return non-zero when the global undo shortcut is triggered
 */
RGINLINE int rg_gui_shortcut_undo(RgGuiContext* ctx, u32 flags);

/**
 * @brief Return non-zero when the global redo shortcut is triggered
 */
RGINLINE int rg_gui_shortcut_redo(RgGuiContext* ctx, u32 flags);

/**
 * @brief Initialize an undo/redo helper
 */
RGINLINE void rg_gui_undo_init(RgGuiUndoRedo* undo, RgGuiUndoCommand* commands, u32 command_capacity,
                               void* data, u32 data_capacity, void* user);

/**
 * @brief Clear all undo/redo history
 */
RGINLINE void rg_gui_undo_clear(RgGuiUndoRedo* undo);

/**
 * @brief Push an undo command and payload
 * @return 1 if stored, 0 if the command could not be recorded
 */
RGINLINE int rg_gui_undo_push(RgGuiUndoRedo* undo, RgGuiUndoFn undo_fn, RgGuiUndoFn redo_fn,
                              const void* data, u32 data_size);

/**
 * @brief Undo the most recent command
 * @return 1 if a command was undone
 */
RGINLINE int rg_gui_undo_undo(RgGuiUndoRedo* undo);

/**
 * @brief Redo the next command
 * @return 1 if a command was redone
 */
RGINLINE int rg_gui_undo_redo(RgGuiUndoRedo* undo);

/**
 * @brief Return the number of undoable commands
 */
RGINLINE u32 rg_gui_undo_undo_count(const RgGuiUndoRedo* undo);

/**
 * @brief Return the number of redoable commands
 */
RGINLINE u32 rg_gui_undo_redo_count(const RgGuiUndoRedo* undo);

/**
 * @brief Integer input widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_int(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief Integer input widget with drag-to-change on the label
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_int_drag(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id);

/**
 * @brief f32 input widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_float(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief f32 input widget with drag-to-change on the label
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_float_drag(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id);

/**
 * @brief f64 input widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_double(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief f64 input widget with drag-to-change on the label
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_double_drag(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value, f64 drag_speed, RgGuiRect rect, RgGuiId id);

/**
 * @brief Vec2 input widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_vec2(RgGuiContext* ctx, const char* label, rg_vec2* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief Vec2 input widget with drag-to-change per component
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_vec2_drag(RgGuiContext* ctx, const char* label, rg_vec2* value, f32 min_value, f32 max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id);

/**
 * @brief Vec3 input widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_vec3(RgGuiContext* ctx, const char* label, rg_vec3* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief Vec3 input widget with drag-to-change per component
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_vec3_drag(RgGuiContext* ctx, const char* label, rg_vec3* value, f32 min_value, f32 max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id);

/**
 * @brief Vec4 input widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_vec4(RgGuiContext* ctx, const char* label, rg_vec4* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief Vec4 input widget with drag-to-change per component
 * @return 1 if value changed
 */
RGINLINE int rg_gui_input_vec4_drag(RgGuiContext* ctx, const char* label, rg_vec4* value, f32 min_value, f32 max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id);

/**
 * @brief f32 range slider widget (dual-handle)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_range_float(RgGuiContext* ctx, const char* label, f32* min_value, f32* max_value,
                                       f32 min_limit, f32 max_limit, RgGuiRect rect, RgGuiId id);

/**
 * @brief f32 range slider widget with numeric input fields
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_range_float_input(RgGuiContext* ctx, const char* label, f32* min_value, f32* max_value,
                                             f32 min_limit, f32 max_limit, RgGuiRect rect, RgGuiId id);

/**
 * @brief Int range slider widget (dual-handle)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_range_int(RgGuiContext* ctx, const char* label, int* min_value, int* max_value,
                                     int min_limit, int max_limit, RgGuiRect rect, RgGuiId id);

/**
 * @brief Int range slider widget with numeric input fields
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_range_int_input(RgGuiContext* ctx, const char* label, int* min_value, int* max_value,
                                           int min_limit, int max_limit, RgGuiRect rect, RgGuiId id);

/**
 * @brief f64 range slider widget (dual-handle)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_range_double(RgGuiContext* ctx, const char* label, f64* min_value, f64* max_value,
                                        f64 min_limit, f64 max_limit, RgGuiRect rect, RgGuiId id);

/**
 * @brief f64 range slider widget with numeric input fields
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_range_double_input(RgGuiContext* ctx, const char* label, f64* min_value, f64* max_value,
                                              f64 min_limit, f64 max_limit, RgGuiRect rect, RgGuiId id);

/**
 * @brief Int slider widget (no input field)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_int(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief Int slider widget with numeric input field
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_int_input(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief Slider widget (no input field)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_float(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief Slider widget with numeric input field
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_float_input(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief f64 slider widget (no input field)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_double(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief f64 slider widget with numeric input field
 * @return 1 if value changed
 */
RGINLINE int rg_gui_slider_double_input(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value, RgGuiRect rect, RgGuiId id);

/**
 * @brief f32 stepper widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_stepper_float(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, f32 step, RgGuiRect rect, RgGuiId id);

/**
 * @brief f32 stepper widget with static label text (stable address and immutable bytes for the GUI context lifetime)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_stepper_float_static(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, f32 step, RgGuiRect rect, RgGuiId id);

/**
 * @brief Int stepper widget
 * @return 1 if value changed
 */
RGINLINE int rg_gui_stepper_int(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, int step, RgGuiRect rect, RgGuiId id);

/**
 * @brief Int stepper widget with static label text (stable address and immutable bytes for the GUI context lifetime)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_stepper_int_static(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, int step, RgGuiRect rect, RgGuiId id);

/**
 * @brief Color picker widget (RGBA sliders + preview)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_color_picker(RgGuiContext* ctx, rg_vec4* color, RgGuiRect rect, RgGuiId id);

/**
 * @brief HSV color picker widget (SV square + hue bar + optional alpha)
 * @return 1 if value changed
 */
RGINLINE int rg_gui_color_picker_hsv(RgGuiContext* ctx, rg_vec4* color, RgGuiRect rect, RgGuiId id, u32 flags);

/**
 * @brief Curve editor widget (editable control points)
 * @return 1 if curve changed
 */
RGINLINE int rg_gui_curve_editor(RgGuiContext* ctx, const char* label, rg_vec2* points, u32* point_count,
                                 u32 point_capacity, rg_vec2 min_value, rg_vec2 max_value,
                                 RgGuiRect rect, RgGuiId id, u32 flags, int* selected);

/**
 * @brief Gradient editor widget (editable color stops)
 * @return 1 if gradient changed
 */
RGINLINE int rg_gui_gradient_editor(RgGuiContext* ctx, const char* label, RgGuiGradientStop* stops, u32* stop_count,
                                    u32 stop_capacity, RgGuiRect rect, RgGuiId id, u32 flags, int* selected);

/**
 * @brief Begin a node editor canvas (handles pan/zoom + clipping)
 * @return 1 if hovered
 */
RGINLINE int rg_gui_node_editor_begin(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiRect rect, RgGuiId id);

/**
 * @brief End a node editor canvas
 */
RGINLINE void rg_gui_node_editor_end(RgGuiContext* ctx, RgGuiNodeEditorState* editor);

/**
 * @brief Selection marquee for node editor (draws rect, returns 1 if active)
 */
RGINLINE int rg_gui_node_editor_selection(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiId id,
                                          RgGuiRect* out_screen, RgGuiRect* out_canvas);

/**
 * @brief Draw a node editor minimap
 * @return 1 if the minimap updated pan
 */
RGINLINE int rg_gui_node_editor_minimap(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiRect rect, u32 flags);

/**
 * @brief Convert a canvas position to screen space
 */
RGINLINE rg_vec2 rg_gui_node_editor_to_screen(const RgGuiNodeEditorState* editor, rg_vec2 pos);

/**
 * @brief Convert a screen position to canvas space
 */
RGINLINE rg_vec2 rg_gui_node_editor_to_canvas(const RgGuiNodeEditorState* editor, rg_vec2 pos);

/**
 * @brief Begin a node box (drag to move)
 * @return 1 if clicked
 */
RGINLINE int rg_gui_node_begin(RgGuiContext* ctx, RgGuiNodeEditorState* editor, const char* title,
                               rg_vec2* pos, rg_vec2 size, RgGuiRect* out_rect, RgGuiId id);

/**
 * @brief Begin a node box with optional input gating (drag to move)
 * @return 1 if clicked
 */
RGINLINE int rg_gui_node_begin_ex(RgGuiContext* ctx, RgGuiNodeEditorState* editor, const char* title,
                                  rg_vec2* pos, rg_vec2 size, RgGuiRect* out_rect, RgGuiId id, int allow_input,
                                  const RgGuiNodeGraphNodeStyle* style);

/**
 * @brief End a node box started with rg_gui_node_begin
 */
RGINLINE void rg_gui_node_end(RgGuiContext* ctx, RgGuiNodeEditorState* editor);

/**
 * @brief Draw a link between two canvas points
 */
RGINLINE void rg_gui_node_link(RgGuiContext* ctx, const RgGuiNodeEditorState* editor,
                               rg_vec2 a, rg_vec2 b, rg_vec4 color);

/**
 * @brief Initialize a node graph data container
 */
RGINLINE void rg_gui_node_graph_init(RgGuiNodeGraph* graph);

/**
 * @brief Reset node graph interaction state
 */
RGINLINE void rg_gui_node_graph_state_reset(RgGuiNodeGraphState* state);

/**
 * @brief Reset node group interaction state
 */
RGINLINE void rg_gui_node_group_state_reset(RgGuiNodeGroupState* state);

/**
 * @brief Find a member index inside a node group (returns -1 if not found)
 */
RGINLINE int rg_gui_node_group_find_member(const RgGuiNodeGroup* group, RgGuiId node_id);

/**
 * @brief Add a node id to a node group (returns 1 if added)
 */
RGINLINE int rg_gui_node_group_add_member(RgGuiNodeGroup* group, RgGuiId node_id);

/**
 * @brief Remove a node id from a node group (returns 1 if removed)
 */
RGINLINE int rg_gui_node_group_remove_member(RgGuiNodeGroup* group, RgGuiId node_id);

/**
 * @brief Move group members inside a node graph (returns moved count)
 */
RGINLINE u32 rg_gui_node_group_move_members(RgGuiNodeGraph* graph, const RgGuiNodeGroup* group, rg_vec2 delta);

/**
 * @brief Draw and interact with a node group
 */
RGINLINE RgGuiNodeGroupResult rg_gui_node_group(RgGuiContext* ctx, RgGuiNodeEditorState* editor,
                                                RgGuiNodeGroupState* state, RgGuiNodeGroup* group,
                                                const char* title, u32 index);

/**
 * @brief Find a node index by id (returns -1 if not found)
 */
RGINLINE int rg_gui_node_graph_find_node_index(const RgGuiNodeGraph* graph, RgGuiId id);

/**
 * @brief Add a node to the graph (returns index, or -1 on failure)
 */
RGINLINE int rg_gui_node_graph_add_node(RgGuiNodeGraph* graph, RgGuiId id, rg_vec2 pos, rg_vec2 size,
                                        u8 input_count, u8 output_count);

/**
 * @brief Remove nodes flagged in the mask (returns removed count)
 */
RGINLINE u32 rg_gui_node_graph_remove_nodes(RgGuiNodeGraph* graph, const u8* remove_mask, u32 mask_count);

/**
 * @brief Remove nodes flagged in the mask (returns removed count) and optionally return the remap
 */
RGINLINE u32 rg_gui_node_graph_remove_nodes_ex(RgGuiNodeGraph* graph, const u8* remove_mask, u32 mask_count,
                                               int* out_remap, u32 remap_count);

/**
 * @brief Reset a graph bundle (preserves payload pointers)
 */
RGINLINE void rg_gui_node_graph_bundle_reset(RgGuiNodeGraphBundle* bundle);

/**
 * @brief Capture selected nodes and links into a bundle
 */
RGINLINE int rg_gui_node_graph_bundle_capture(const RgGuiNodeGraph* graph, const u8* selected,
                                              u32 selected_count, RgGuiNodeGraphBundle* out_bundle,
                                              RgGuiNodeGraphSerializeFn serialize, void* user);

/**
 * @brief Apply a bundle to a graph (returns added node count)
 */
RGINLINE u32 rg_gui_node_graph_bundle_apply(RgGuiNodeGraph* graph, const RgGuiNodeGraphBundle* bundle,
                                            rg_vec2 offset, RgGuiNodeGraphAllocIdFn alloc_id,
                                            RgGuiNodeGraphDeserializeFn deserialize, void* user,
                                            RgGuiId* out_new_ids, u32 out_id_capacity,
                                            RgGuiNodeGraphLink* out_new_links, u32 out_link_capacity,
                                            u32* out_link_count);

/**
 * @brief Duplicate selected nodes into the same graph (returns added node count)
 */
RGINLINE u32 rg_gui_node_graph_duplicate_selected(RgGuiNodeGraph* graph, const u8* selected,
                                                  u32 selected_count, rg_vec2 offset,
                                                  RgGuiNodeGraphBundle* scratch, RgGuiNodeGraphAllocIdFn alloc_id,
                                                  RgGuiNodeGraphSerializeFn serialize,
                                                  RgGuiNodeGraphDeserializeFn deserialize, void* user);

/**
 * @brief Add a link to the graph (returns 1 on success)
 */
RGINLINE int rg_gui_node_graph_add_link(RgGuiNodeGraph* graph, u32 from_node, u16 from_port,
                                        u32 to_node, u16 to_port);

/**
 * @brief Remove a link by index (returns 1 on success)
 */
RGINLINE int rg_gui_node_graph_remove_link_index(RgGuiNodeGraph* graph, u32 index);

/**
 * @brief Rewire a link in place (returns 1 on success)
 */
RGINLINE int rg_gui_node_graph_rewire_link(RgGuiNodeGraph* graph, u32 index,
                                           u32 from_node, u16 from_port,
                                           u32 to_node, u16 to_port);

/**
 * @brief Remove invalid links (node or port out of range)
 */
RGINLINE void rg_gui_node_graph_prune_links(RgGuiNodeGraph* graph);

/**
 * @brief Remove all links on a port, optionally returning removed links
 * @return Number of links removed
 */
RGINLINE u32 rg_gui_node_graph_remove_links_for_port(RgGuiNodeGraph* graph, u32 node, u16 port,
                                                     int output, RgGuiNodeGraphLink* out_links, u32 max_links);

/**
 * @brief Default port radius in screen space
 */
RGINLINE f32 rg_gui_node_graph_port_size(f32 zoom);

/**
 * @brief Compute a port center in canvas space for the default layout
 */
RGINLINE rg_vec2 rg_gui_node_graph_port_canvas_pos(const RgGuiContext* ctx, const RgGuiNodeEditorState* editor,
                                                   rg_vec2 node_pos, rg_vec2 node_size,
                                                   u32 port_index, u32 port_count, int output);

/**
 * @brief Compute a port rect in screen space for the default layout
 */
RGINLINE RgGuiRect rg_gui_node_graph_port_screen_rect(const RgGuiNodeEditorState* editor, rg_vec2 canvas_pos);

/**
 * @brief Draw a default port badge
 */
RGINLINE void rg_gui_node_graph_draw_port(RgGuiContext* ctx, RgGuiRect rect, int output, int highlighted);

/**
 * @brief Draw and interact with a node graph editor (with optional groups)
 * @return Non-zero if the editor is hovered
 */
RGINLINE int rg_gui_node_graph_editor_ex(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiNodeGraphState* state,
                                         RgGuiNodeGraph* graph, const RgGuiNodeGraphDraw* draw,
                                         const RgGuiNodeGraphSelection* selection, const RgGuiNodeGraphCallbacks* callbacks,
                                         const RgGuiNodeGraphGroups* groups,
                                         RgGuiRect rect, RgGuiId id, void* user);

/**
 * @brief Draw and interact with a node graph editor
 * @return Non-zero if the editor is hovered
 */
RGINLINE int rg_gui_node_graph_editor(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiNodeGraphState* state,
                                      RgGuiNodeGraph* graph, const RgGuiNodeGraphDraw* draw,
                                      const RgGuiNodeGraphSelection* selection, const RgGuiNodeGraphCallbacks* callbacks,
                                      RgGuiRect rect, RgGuiId id, void* user);

/**
 * @brief Progress bar widget
 */
RGINLINE void rg_gui_progress_bar(RgGuiContext* ctx, const char* label, f32 value, f32 min_value, f32 max_value, RgGuiRect rect);

/**
 * @brief Line plot widget (array samples)
 * @return Hovered sample index, or -1 if none
 */
RGINLINE int rg_gui_plot_lines(RgGuiContext* ctx, const char* label, const f32* values, u32 count,
                               f32 min_value, f32 max_value, RgGuiRect rect);

/**
 * @brief Line plot widget (sample callback)
 * @return Hovered sample index, or -1 if none
 */
RGINLINE int rg_gui_plot_lines_fn(RgGuiContext* ctx, const char* label, RgGuiPlotSampleFn sample_fn, const void* user,
                                  u32 count, f32 min_value, f32 max_value, RgGuiRect rect);

/**
 * @brief Histogram widget (array samples)
 * @return Hovered sample index, or -1 if none
 */
RGINLINE int rg_gui_plot_histogram(RgGuiContext* ctx, const char* label, const f32* values, u32 count,
                                   f32 min_value, f32 max_value, RgGuiRect rect);

/**
 * @brief Histogram widget (sample callback)
 * @return Hovered sample index, or -1 if none
 */
RGINLINE int rg_gui_plot_histogram_fn(RgGuiContext* ctx, const char* label, RgGuiPlotSampleFn sample_fn, const void* user,
                                      u32 count, f32 min_value, f32 max_value, RgGuiRect rect);

/**
 * @brief Tab bar widget
 * @return 1 if active tab changed
 */
RGINLINE int rg_gui_tabs(RgGuiContext* ctx, const char* const* labels, u32 count, u32* active, RgGuiRect rect, RgGuiId id);

/**
 * @brief List widget
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_list(RgGuiContext* ctx, const char* const* items, u32 count, int* selected, int* scroll, RgGuiRect rect, RgGuiId id);

/**
 * @brief Virtual list widget (items resolved via callback)
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_list_virtual(RgGuiContext* ctx, RgGuiListItemFn get_item, const void* user, u32 count, int* selected, int* scroll, RgGuiRect rect, RgGuiId id);

/**
 * @brief Clipped list widget with pixel scrolling (items resolved via callback)
 * @param copy_label Non-zero to copy label text (use for transient buffers)
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_list_clipped(RgGuiContext* ctx, RgGuiPanelState* panel,
                                 RgGuiListItemFn get_item, const void* user, u32 count, int* selected,
                                 f32 row_height, f32 spacing, RgGuiRect rect, RgGuiId id,
                                 RgGuiRowCache* cache, RgGuiListRowHeightFn row_height_fn, int copy_label);

/**
 * @brief Reset selection state (clears anchor/cursor)
 */
RGINLINE void rg_gui_selection_reset(RgGuiSelectionState* state);

/**
 * @brief Apply selection change for multi-select widgets
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_selection_apply(RgGuiSelectionState* state, const RgGuiSelectionOps* ops, void* user,
                                    u32 count, u32 index, u32 flags);

/**
 * @brief Multi-select list widget
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_list_multi(RgGuiContext* ctx, const char* const* items, u32 count,
                               RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                               int* scroll, RgGuiRect rect, RgGuiId id);

/**
 * @brief Multi-select clipped list widget with pixel scrolling (items resolved via callback)
 * @param copy_label Non-zero to copy label text (use for transient buffers)
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_list_multi_clipped(RgGuiContext* ctx, RgGuiPanelState* panel,
                                       RgGuiListItemFn get_item, const void* user, u32 count,
                                       RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                                       f32 row_height, f32 spacing, RgGuiRect rect, RgGuiId id,
                                       RgGuiRowCache* cache, RgGuiListRowHeightFn row_height_fn, int copy_label);

/**
 * @brief Multi-select virtual list widget (items resolved via callback)
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_list_virtual_multi(RgGuiContext* ctx, RgGuiListItemFn get_item, const void* user, u32 count,
                                       RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                                       int* scroll, RgGuiRect rect, RgGuiId id);

/**
 * @brief Table/list view widget with resizable/sortable columns and virtualized rows
 * @return Bitmask of RgGuiTableResult flags
 */
RGINLINE u32 rg_gui_table(RgGuiContext* ctx, RgGuiTableState* table, const RgGuiTableColumn* columns, f32* widths,
                          u32 column_count, u32 row_count, f32 row_height, int* selected,
                          RgGuiRect rect, RgGuiId id, RgGuiTableRowFn row_fn, const void* user);

/**
 * @brief Table/list view widget with cached row heights for variable row sizes
 * @return Bitmask of RgGuiTableResult flags
 */
RGINLINE u32 rg_gui_table_clipped(RgGuiContext* ctx, RgGuiTableState* table, const RgGuiTableColumn* columns, f32* widths,
                                  u32 column_count, u32 row_count, f32 row_height, int* selected,
                                  RgGuiRowCache* cache, RgGuiRect rect, RgGuiId id, RgGuiTableRowFn row_fn, const void* user);

/**
 * @brief Multi-select table/list view widget with resizable/sortable columns and virtualized rows
 * @return Bitmask of RgGuiTableResult flags
 */
RGINLINE u32 rg_gui_table_multi(RgGuiContext* ctx, RgGuiTableState* table, const RgGuiTableColumn* columns, f32* widths,
                                u32 column_count, u32 row_count, f32 row_height,
                                RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                                RgGuiRect rect, RgGuiId id, RgGuiTableRowFn row_fn, const void* user);

/**
 * @brief Multi-select table/list view widget with cached row heights for variable row sizes
 * @return Bitmask of RgGuiTableResult flags
 */
RGINLINE u32 rg_gui_table_multi_clipped(RgGuiContext* ctx, RgGuiTableState* table, const RgGuiTableColumn* columns, f32* widths,
                                        u32 column_count, u32 row_count, f32 row_height,
                                        RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                                        RgGuiRowCache* cache, RgGuiRect rect, RgGuiId id, RgGuiTableRowFn row_fn, const void* user);

/**
 * @brief Resolve a table cell rect using table ordering/frozen columns
 */
RGINLINE RgGuiRect rg_gui_table_column_rect(const RgGuiTableState* table, const f32* widths,
                                            u32 column_index, RgGuiRect row_rect);

/**
 * @brief Dropdown widget
 * @return 1 if selection changed
 */
RGINLINE int rg_gui_dropdown(RgGuiContext* ctx, const char* const* items, u32 count, int* selected, int* open, int* scroll, RgGuiRect rect, RgGuiId id);

/**
 * @brief Access draw list for rendering
 */
RGINLINE const RgGuiDrawList* rg_gui_draw_list(const RgGuiContext* ctx);

/**
 * @brief Access diagnostics for the current or most recently completed frame
 */
RGINLINE const RgGuiDiagnostics* rg_gui_diagnostics(const RgGuiContext* ctx);

/**
 * @brief Offset where overlay commands begin in the draw list
 */
RGINLINE u32 rg_gui_draw_list_overlay_start(const RgGuiContext* ctx);

/**
 * @brief Mouse cursor requested by the UI for the current frame
 */
RGINLINE RgGuiMouseCursor rg_gui_mouse_cursor(const RgGuiContext* ctx);

/**
 * @brief ID helpers
 */
RGINLINE RgGuiId rg_gui_id_ptr(const void* ptr);
#if !defined(RG_GUI_NO_STRING_IDS)
RGINLINE RgGuiId rg_gui_id_str(const char* str);
RGINLINE RgGuiId rg_gui_id_str_len(const char* str, size_t len);
#endif
RGINLINE RgGuiId rg_gui_id_u64(u64 value);
RGINLINE RgGuiId rg_gui_id_combine(RgGuiId a, u64 b);

// =============================================================================
// IMPLEMENTATION
// =============================================================================

RGINLINE rg_vec4 rg_gui_color(f32 r, f32 g, f32 b, f32 a)
{
	rg_vec4 color;
	color.x = r;
	color.y = g;
	color.z = b;
	color.w = a;
	return color;
}

RGINLINE rg_vec3 rg_gui_color_hsv_to_rgb(f32 h, f32 s, f32 v)
{
	f32 r = v;
	f32 g = v;
	f32 b = v;

	if (s > 0.0f)
	{
		h = rg_clampf(h, 0.0f, 1.0f);
		f32 hh = h * 6.0f;
		int sector = hh >= 1.0f ? (int)hh : 0;
		if (sector >= 6)
		{
			sector = 5;
		}
		f32 f = hh - (f32)sector;
		f32 p = v * (1.0f - s);
		f32 q = v * (1.0f - s * f);
		f32 t = v * (1.0f - s * (1.0f - f));

		switch (sector)
		{
			case 0:
				r = v;
				g = t;
				b = p;
				break;
			case 1:
				r = q;
				g = v;
				b = p;
				break;
			case 2:
				r = p;
				g = v;
				b = t;
				break;
			case 3:
				r = p;
				g = q;
				b = v;
				break;
			case 4:
				r = t;
				g = p;
				b = v;
				break;
			default:
				r = v;
				g = p;
				b = q;
				break;
		}
	}

	return rg_vec3(r, g, b);
}

RGINLINE rg_vec3 rg_gui_color_rgb_to_hsv(rg_vec3 rgb)
{
	f32 max_value = rg_maxf(rgb.x, rg_maxf(rgb.y, rgb.z));
	f32 min_value = rg_minf(rgb.x, rg_minf(rgb.y, rgb.z));
	f32 delta = max_value - min_value;
	f32 h = 0.0f;
	f32 s = 0.0f;
	f32 v = max_value;

	if (max_value > 0.0f)
	{
		s = delta / max_value;
	}

	if (delta > 0.0f)
	{
		if (max_value == rgb.x)
		{
			h = (rgb.y - rgb.z) / delta;
		}
		else if (max_value == rgb.y)
		{
			h = 2.0f + (rgb.z - rgb.x) / delta;
		}
		else
		{
			h = 4.0f + (rgb.x - rgb.y) / delta;
		}

		h /= 6.0f;
		if (h < 0.0f)
		{
			h += 1.0f;
		}
	}

	return rg_vec3(h, s, v);
}

RGINLINE rg_vec4 rg_gui_color_hsv(f32 h, f32 s, f32 v, f32 a)
{
	rg_vec3 rgb = rg_gui_color_hsv_to_rgb(h, s, v);
	return rg_gui_color(rgb.x, rgb.y, rgb.z, a);
}

RGINLINE RgGuiRect rg_gui_make_rect(f32 x, f32 y, f32 w, f32 h)
{
	RgGuiRect rect;
	rect.x = x;
	rect.y = y;
	rect.w = w;
	rect.h = h;
	return rect;
}

/**
 * @brief Build an icon description for icon-aware widgets
 */
RGINLINE RgGuiIcon rg_gui_icon_make(RgGuiTexture texture, RgGuiRect uv, rg_vec4 tint)
{
	RgGuiIcon icon;
	icon.texture = texture;
	icon.material = 0u;
	icon.uv = uv;
	icon.tint = tint;
	return icon;
}

/**
 * @brief Build an icon description with an application-defined renderer material
 */
RGINLINE RgGuiIcon rg_gui_icon_make_material(RgGuiTexture texture, RgGuiRect uv,
                                             rg_vec4 tint, RgGuiImageMaterial material)
{
	RgGuiIcon icon;
	icon.texture = texture;
	icon.material = material;
	icon.uv = uv;
	icon.tint = tint;
	return icon;
}

RGINLINE void rg_gui_set_arrow_icon(RgGuiContext* ctx, RgGuiArrowDirection direction,
                                    const RgGuiIcon* icon)
{
	if (!ctx || direction < RG_GUI_ARROW_LEFT || direction >= RG_GUI_ARROW_COUNT)
	{
		return;
	}

	RgGuiIcon* target = &ctx->arrow_icons[direction];
	if (icon && icon->texture != 0)
	{
		*target = *icon;
	}
	else
	{
		memset(target, 0, sizeof(*target));
	}
}

RGINLINE void rg_gui_set_close_icon(RgGuiContext* ctx, const RgGuiIcon* icon)
{
	if (!ctx)
	{
		return;
	}

	if (icon && icon->texture != 0)
	{
		ctx->close_icon = *icon;
	}
	else
	{
		memset(&ctx->close_icon, 0, sizeof(ctx->close_icon));
	}
}

RGINLINE RgGuiAnchor rg_gui_anchor_make(rg_vec2 min, rg_vec2 max, rg_vec2 offset_min, rg_vec2 offset_max)
{
	RgGuiAnchor anchor;
	anchor.min = min;
	anchor.max = max;
	anchor.offset_min = offset_min;
	anchor.offset_max = offset_max;
	return anchor;
}

RGINLINE RgGuiRect rg_gui_anchor_rect(RgGuiRect parent, RgGuiAnchor anchor)
{
	f32 x0 = parent.x + parent.w * anchor.min.x + anchor.offset_min.x;
	f32 y0 = parent.y + parent.h * anchor.min.y + anchor.offset_min.y;
	f32 x1 = parent.x + parent.w * anchor.max.x + anchor.offset_max.x;
	f32 y1 = parent.y + parent.h * anchor.max.y + anchor.offset_max.y;
	return rg_gui_make_rect(x0, y0, x1 - x0, y1 - y0);
}

RGINLINE RgGuiRect rg_gui_anchor_fixed(RgGuiRect parent, rg_vec2 anchor, rg_vec2 size, rg_vec2 pivot, rg_vec2 offset)
{
	f32 x = parent.x + parent.w * anchor.x + offset.x - size.x * pivot.x;
	f32 y = parent.y + parent.h * anchor.y + offset.y - size.y * pivot.y;
	return rg_gui_make_rect(x, y, size.x, size.y);
}

RGINLINE RgGuiStyle rg_gui_style_default(void)
{
	RgGuiStyle style;
	style.text_height = 16.0f;
	style.char_width = 7.0f;
	style.padding = 4.0f;
	style.inner_spacing = 6.0f;
	style.label_width = 80.0f;
	style.value_width = 64.0f;
	style.scroll_bar_width = 8.0f;
	style.slider_handle_width = 4.0f;
	style.border_thickness = 1.0f;
	style.focus_border_thickness = 2.0f;
	style.cursor_blink_interval = 0.5f;
	style.key_repeat_delay = 0.35f;
	style.key_repeat_interval = 0.06f;
	style.key_repeat_fast_delay = 0.80f;
	style.key_repeat_fast_interval = 0.03f;
	style.value_decimals = 3;
	style.disabled_alpha = 0.55f;

	style.color_text = rg_gui_color(0.855f, 0.870f, 0.895f, 1.0f);
	style.color_text_dim = rg_gui_color(0.620f, 0.640f, 0.675f, 1.0f);
	style.color_bg = rg_gui_color(0.145f, 0.152f, 0.165f, 1.0f);
	style.color_bg_hover = rg_gui_color(0.170f, 0.178f, 0.194f, 1.0f);
	style.color_bg_active = rg_gui_color(0.194f, 0.205f, 0.224f, 1.0f);
	style.color_border = rg_gui_color(0.255f, 0.267f, 0.294f, 1.0f);
	style.color_accent = rg_gui_color(0.200f, 0.702f, 1.000f, 1.0f);
	style.color_panel = rg_gui_color(0.122f, 0.129f, 0.141f, 1.0f);
	style.color_focus_border = style.color_accent;
	style.color_selection = style.color_accent;
	style.color_selection.w = 0.35f;
	style.color_caret = style.color_text;
	style.color_panel_title = style.color_bg;
	style.color_panel_floating = style.color_panel;
	style.color_drop_shadow = rg_gui_color(0.0f, 0.0f, 0.0f, 0.35f);

	return style;
}

RGINLINE int rg_gui_text_lookup_init(RgGuiTextLookup* lookup, const RgTextFont* font)
{
	if (!lookup)
	{
		return 0;
	}
	memset(lookup, 0, sizeof(*lookup));
	if (!font || !font->glyphs || font->glyph_count == 0u)
	{
		return 0;
	}

	// Reverse traversal preserves the first matching entry in caller-supplied arrays.
	const RgTextGlyph* fallback = NULL;
	for (u32 i = font->glyph_count; i > 0u; i--)
	{
		const RgTextGlyph* glyph = &font->glyphs[i - 1u];
		if (glyph->codepoint < 128u)
		{
			lookup->ascii_glyphs[glyph->codepoint] = glyph;
		}
		if (glyph->codepoint == font->fallback_codepoint)
		{
			fallback = glyph;
		}
	}
	for (u32 cp = 0u; cp < 128u; cp++)
	{
		if (!lookup->ascii_glyphs[cp]) lookup->ascii_glyphs[cp] = fallback;
	}
	if (font->kernings)
	{
		for (u32 i = font->kerning_count; i > 0u; i--)
		{
			const RgTextKerning* pair = &font->kernings[i - 1u];
			if (pair->left < 128u && pair->right < 128u)
			{
				lookup->ascii_kerning[pair->left * 128u + pair->right] = pair->x_advance;
			}
		}
	}
	lookup->font = font;
	return 1;
}

RGINLINE const RgTextGlyph* rg_gui_text_find_glyph(const RgGuiContext* ctx, u32 codepoint)
{
	const RgTextFont* font = ctx ? ctx->font : NULL;
	const RgGuiTextLookup* lookup = ctx ? ctx->text_lookup : NULL;
	if (lookup && lookup->font == font && codepoint < 128u)
	{
		return lookup->ascii_glyphs[codepoint];
	}
	return rg_text_find_glyph(font, codepoint);
}

RGINLINE i32 rg_gui_text_find_kerning(const RgGuiContext* ctx, u32 left, u32 right)
{
	const RgTextFont* font = ctx ? ctx->font : NULL;
	const RgGuiTextLookup* lookup = ctx ? ctx->text_lookup : NULL;
	if (lookup && lookup->font == font && left < 128u && right < 128u)
	{
		return lookup->ascii_kerning[left * 128u + right];
	}
	return rg_text_find_kerning(font, left, right);
}

RGINLINE f32 rg_gui_text_base_scale(const RgGuiContext* ctx)
{
	if (!ctx || !ctx->font || ctx->font->metrics.line_height <= 0)
	{
		return 0.0f;
	}
	return ctx->style.text_height / (f32)ctx->font->metrics.line_height;
}

RGINLINE u32 rg_gui_text_scale_bits(const RgGuiContext* ctx)
{
	f32 scale = rg_gui_text_base_scale(ctx);
	u32 bits = 0u;
	memcpy(&bits, &scale, sizeof(bits));
	return bits;
}

RGINLINE f32* rg_gui_style_var_ptr(RgGuiStyle* style, RgGuiStyleVar var)
{
	if (!style)
	{
		return NULL;
	}

	switch (var)
	{
		case RG_GUI_STYLE_VAR_TEXT_HEIGHT: return &style->text_height;
		case RG_GUI_STYLE_VAR_CHAR_WIDTH: return &style->char_width;
		case RG_GUI_STYLE_VAR_PADDING: return &style->padding;
		case RG_GUI_STYLE_VAR_INNER_SPACING: return &style->inner_spacing;
		case RG_GUI_STYLE_VAR_LABEL_WIDTH: return &style->label_width;
		case RG_GUI_STYLE_VAR_VALUE_WIDTH: return &style->value_width;
		case RG_GUI_STYLE_VAR_SCROLL_BAR_WIDTH: return &style->scroll_bar_width;
		case RG_GUI_STYLE_VAR_SLIDER_HANDLE_WIDTH: return &style->slider_handle_width;
		case RG_GUI_STYLE_VAR_BORDER_THICKNESS: return &style->border_thickness;
		case RG_GUI_STYLE_VAR_FOCUS_BORDER_THICKNESS: return &style->focus_border_thickness;
		case RG_GUI_STYLE_VAR_CURSOR_BLINK_INTERVAL: return &style->cursor_blink_interval;
		case RG_GUI_STYLE_VAR_KEY_REPEAT_DELAY: return &style->key_repeat_delay;
		case RG_GUI_STYLE_VAR_KEY_REPEAT_INTERVAL: return &style->key_repeat_interval;
		case RG_GUI_STYLE_VAR_KEY_REPEAT_FAST_DELAY: return &style->key_repeat_fast_delay;
		case RG_GUI_STYLE_VAR_KEY_REPEAT_FAST_INTERVAL: return &style->key_repeat_fast_interval;
		case RG_GUI_STYLE_VAR_DISABLED_ALPHA: return &style->disabled_alpha;
		case RG_GUI_STYLE_VAR_VALUE_DECIMALS:
		default:
			break;
	}

	return NULL;
}

RGINLINE rg_vec4* rg_gui_style_color_ptr(RgGuiStyle* style, RgGuiStyleColor color)
{
	if (!style)
	{
		return NULL;
	}

	switch (color)
	{
		case RG_GUI_STYLE_COLOR_TEXT: return &style->color_text;
		case RG_GUI_STYLE_COLOR_TEXT_DIM: return &style->color_text_dim;
		case RG_GUI_STYLE_COLOR_BG: return &style->color_bg;
		case RG_GUI_STYLE_COLOR_BG_HOVER: return &style->color_bg_hover;
		case RG_GUI_STYLE_COLOR_BG_ACTIVE: return &style->color_bg_active;
		case RG_GUI_STYLE_COLOR_BORDER: return &style->color_border;
		case RG_GUI_STYLE_COLOR_ACCENT: return &style->color_accent;
		case RG_GUI_STYLE_COLOR_PANEL: return &style->color_panel;
		case RG_GUI_STYLE_COLOR_FOCUS_BORDER: return &style->color_focus_border;
		case RG_GUI_STYLE_COLOR_SELECTION: return &style->color_selection;
		case RG_GUI_STYLE_COLOR_CARET: return &style->color_caret;
		case RG_GUI_STYLE_COLOR_PANEL_TITLE: return &style->color_panel_title;
		case RG_GUI_STYLE_COLOR_PANEL_FLOATING: return &style->color_panel_floating;
		case RG_GUI_STYLE_COLOR_DROP_SHADOW: return &style->color_drop_shadow;
		default:
			break;
	}

	return NULL;
}

RGINLINE int rg_gui_point_in_rect(f32 x, f32 y, RgGuiRect rect)
{
	return (x >= rect.x && x < rect.x + rect.w && y >= rect.y && y < rect.y + rect.h) ? 1 : 0;
}

RGINLINE rg_vec2 rg_gui_input_blocked_pos(void)
{
	return rg_vec2(-1.0e30f, -1.0e30f);
}

RGINLINE void rg_gui_input_capture_begin(RgGuiContext* ctx)
{
	if (!ctx)
	{
		return;
	}

	ctx->input_capture_depth++;
	if (ctx->input_capture_depth == 1u)
	{
		ctx->mouse_pos = ctx->mouse_pos_raw;
	}
}

RGINLINE void rg_gui_input_capture_end(RgGuiContext* ctx, int keep_active)
{
	if (!ctx)
	{
		return;
	}

	if (ctx->input_capture_depth == 0u)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_input_capture_end without begin");
		return;
	}

	if (keep_active)
	{
		ctx->input_capture_next = 1;
		ctx->input_capture_active = 1;
	}

	ctx->input_capture_depth--;
	if (ctx->input_capture_depth == 0u && ctx->input_capture_active)
	{
		ctx->mouse_pos = rg_gui_input_blocked_pos();
	}
}

RGINLINE int rg_gui_is_disabled(const RgGuiContext* ctx)
{
	if (!ctx)
	{
		return 0;
	}

	if (ctx->disabled_depth > 0u)
	{
		return 1;
	}

	if (ctx->modal_active && ctx->modal_depth == 0u)
	{
		return 1;
	}

	return 0;
}

RGINLINE rg_vec4 rg_gui_apply_disabled_color(const RgGuiContext* ctx, rg_vec4 color)
{
	if (ctx && ctx->disabled_depth > 0u)
	{
		f32 k = ctx->style.disabled_alpha;
		color.x *= k;
		color.y *= k;
		color.z *= k;
	}
	return color;
}

RGINLINE int rg_gui_input_has_ctrl(const RgInputState* input)
{
	if (!input)
	{
		return 0;
	}

	return rg_input_is_key_down(input, SDL_SCANCODE_LCTRL) ||
	       rg_input_is_key_down(input, SDL_SCANCODE_RCTRL) ||
	       rg_input_is_key_down(input, SDL_SCANCODE_LGUI) ||
	       rg_input_is_key_down(input, SDL_SCANCODE_RGUI);
}

RGINLINE int rg_gui_input_has_shift(const RgInputState* input)
{
	if (!input)
	{
		return 0;
	}

	return rg_input_is_key_down(input, SDL_SCANCODE_LSHIFT) ||
	       rg_input_is_key_down(input, SDL_SCANCODE_RSHIFT);
}

RGINLINE int rg_gui_input_has_alt(const RgInputState* input)
{
	if (!input)
	{
		return 0;
	}

	return rg_input_is_key_down(input, SDL_SCANCODE_LALT) ||
	       rg_input_is_key_down(input, SDL_SCANCODE_RALT);
}

RGINLINE int rg_gui_i64_clamp_to_int(i64 value, int min_value, int max_value)
{
	if (value < (i64)min_value) value = (i64)min_value;
	if (value > (i64)max_value) value = (i64)max_value;
	return (int)value;
}

RGINLINE int rg_gui_trunc_f32_to_int_saturated(f32 value)
{
	if (value != value) return 0;
	if (value >= (f32)INT_MAX) return INT_MAX;
	if (value <= (f32)INT_MIN) return INT_MIN;
	return (int)value;
}

RGINLINE void rg_gui_input_router_init(RgGuiInputRouter* router)
{
	if (!router)
	{
		return;
	}

	memset(router, 0, sizeof(*router));
}

RGINLINE void rg_gui_input_router_begin(RgGuiInputRouter* router, const RgInputState* input,
                                        rg_vec2 mouse_global, RgGuiId focus_override, RgGuiId mouse_focus_viewport,
                                        int mouse_focus_inside)
{
	if (!router)
	{
		return;
	}

	router->input = input;
	router->mouse_global = mouse_global;
	router->left_down = input ? rg_input_is_mouse_button_down(input, RG_MOUSE_BUTTON_LEFT) : 0;
	router->left_pressed = input ? rg_input_is_mouse_button_pressed(input, RG_MOUSE_BUTTON_LEFT) : 0;
	router->mouse_wheel = input ? input->mouse_scroll_y : 0.0f;
	router->press_claimed = 0;
	router->capture_release_pending = (!router->left_down && router->capture_viewport != 0u) ? 1 : 0;
	router->mouse_focus_viewport = mouse_focus_viewport;
	router->mouse_focus_inside = mouse_focus_inside ? 1 : 0;

	if (focus_override != 0u)
	{
		router->focus_viewport = focus_override;
	}
}

RGINLINE void rg_gui_input_router_end(RgGuiInputRouter* router)
{
	if (!router)
	{
		return;
	}

	if (router->capture_release_pending)
	{
		router->capture_viewport = 0u;
		router->capture_release_pending = 0;
	}
}

RGINLINE int rg_gui_input_router_slot(RgGuiInputRouter* router, RgGuiId viewport_id)
{
	if (!router || viewport_id == 0u)
	{
		return -1;
	}

	for (u32 i = 0u; i < router->count; i++)
	{
		if (router->ids[i] == viewport_id)
		{
			return (int)i;
		}
	}

	if (router->count >= RG_GUI_INPUT_ROUTE_MAX)
	{
		return -1;
	}

	u32 index = router->count++;
	router->ids[index] = viewport_id;
	router->last_x[index] = 0;
	router->last_y[index] = 0;
	router->last_valid[index] = 0u;
	return (int)index;
}

RGINLINE int rg_gui_input_router_route(RgGuiInputRouter* router, RgGuiId viewport_id,
                                       rg_vec2 origin, int pixel_w, int pixel_h, RgInputState* out_input)
{
	if (!router || !out_input)
	{
		return 0;
	}

	if (router->input)
	{
		*out_input = *router->input;
	}
	else
	{
		memset(out_input, 0, sizeof(*out_input));
	}

	f32 local_xf = router->mouse_global.x - origin.x;
	f32 local_yf = router->mouse_global.y - origin.y;
	int inside = 0;
	if (pixel_w > 0 && pixel_h > 0)
	{
		inside = (local_xf >= 0.0f && local_yf >= 0.0f &&
		          local_xf < (f32)pixel_w && local_yf < (f32)pixel_h)
		             ? 1
		             : 0;
	}

	int allow_mouse = 0;
	if (inside || (router->capture_viewport == viewport_id))
	{
		allow_mouse = 1;
		if (inside && router->left_pressed && !router->press_claimed && viewport_id != 0u)
		{
			if (router->mouse_focus_viewport == 0u || router->mouse_focus_viewport == viewport_id ||
			    !router->mouse_focus_inside)
			{
				router->capture_viewport = viewport_id;
				router->press_claimed = 1;
				router->focus_viewport = viewport_id;
			}
		}
	}

	int slot = rg_gui_input_router_slot(router, viewport_id);
	if (allow_mouse)
	{
		int local_x = inside ? (int)local_xf : rg_gui_trunc_f32_to_int_saturated(local_xf);
		int local_y = inside ? (int)local_yf : rg_gui_trunc_f32_to_int_saturated(local_yf);
		out_input->mouse_x = local_x;
		out_input->mouse_y = local_y;
		if (slot >= 0 && router->last_valid[slot])
		{
			out_input->mouse_delta_x = rg_gui_i64_clamp_to_int(
			    (i64)local_x - (i64)router->last_x[slot], INT_MIN, INT_MAX);
			out_input->mouse_delta_y = rg_gui_i64_clamp_to_int(
			    (i64)local_y - (i64)router->last_y[slot], INT_MIN, INT_MAX);
		}
		else
		{
			out_input->mouse_delta_x = 0;
			out_input->mouse_delta_y = 0;
		}
		if (slot >= 0)
		{
			router->last_x[slot] = local_x;
			router->last_y[slot] = local_y;
			router->last_valid[slot] = 1u;
		}
		out_input->mouse_scroll_y = router->mouse_wheel;
	}
	else
	{
		memset(out_input->current_mouse, 0, sizeof(out_input->current_mouse));
		memset(out_input->previous_mouse, 0, sizeof(out_input->previous_mouse));
		out_input->mouse_x = -100000;
		out_input->mouse_y = -100000;
		out_input->mouse_delta_x = 0;
		out_input->mouse_delta_y = 0;
		out_input->mouse_scroll_y = 0.0f;
		if (slot >= 0)
		{
			router->last_valid[slot] = 0u;
		}
	}

	if (router->capture_viewport != 0u && router->capture_viewport != viewport_id)
	{
		memset(out_input->current_mouse, 0, sizeof(out_input->current_mouse));
		memset(out_input->previous_mouse, 0, sizeof(out_input->previous_mouse));
		out_input->mouse_scroll_y = 0.0f;
	}
	else if (router->capture_viewport == 0u && router->mouse_focus_viewport != 0u &&
	         router->mouse_focus_viewport != viewport_id && router->mouse_focus_inside)
	{
		memset(out_input->current_mouse, 0, sizeof(out_input->current_mouse));
		memset(out_input->previous_mouse, 0, sizeof(out_input->previous_mouse));
		out_input->mouse_scroll_y = 0.0f;
	}

	if (router->focus_viewport != viewport_id)
	{
		memset(out_input->current_keyboard, 0, sizeof(out_input->current_keyboard));
		memset(out_input->previous_keyboard, 0, sizeof(out_input->previous_keyboard));
		out_input->has_text_input = false;
		out_input->text_input_buffer[0] = '\0';
	}

	if (router->capture_release_pending && router->capture_viewport == viewport_id)
	{
		router->capture_viewport = 0u;
		router->capture_release_pending = 0;
	}

	return allow_mouse;
}

RGINLINE RgGuiId rg_gui_input_router_focus(const RgGuiInputRouter* router)
{
	return router ? router->focus_viewport : 0u;
}

RGINLINE RgGuiId rg_gui_input_router_capture(const RgGuiInputRouter* router)
{
	return router ? router->capture_viewport : 0u;
}

RGINLINE char rg_gui_ascii_lower(char c)
{
	return (c >= 'A' && c <= 'Z') ? (char)(c + ('a' - 'A')) : c;
}

RGINLINE int rg_gui_ascii_is_space(char c)
{
	return (c == ' ' || c == '\t' || c == '\n' || c == '\r') ? 1 : 0;
}

RGINLINE SDL_Scancode rg_gui_scancode_from_ascii(char c)
{
	if (c >= 'a' && c <= 'z')
	{
		return (SDL_Scancode)(SDL_SCANCODE_A + (c - 'a'));
	}
	if (c >= '0' && c <= '9')
	{
		if (c == '0')
		{
			return SDL_SCANCODE_0;
		}
		return (SDL_Scancode)(SDL_SCANCODE_1 + (c - '1'));
	}
	return SDL_SCANCODE_UNKNOWN;
}

RGINLINE int rg_gui_scancode_is_modifier(SDL_Scancode scancode)
{
	return scancode == SDL_SCANCODE_LCTRL ||
	       scancode == SDL_SCANCODE_RCTRL ||
	       scancode == SDL_SCANCODE_LSHIFT ||
	       scancode == SDL_SCANCODE_RSHIFT ||
	       scancode == SDL_SCANCODE_LALT ||
	       scancode == SDL_SCANCODE_RALT ||
	       scancode == SDL_SCANCODE_LGUI ||
	       scancode == SDL_SCANCODE_RGUI;
}

RGINLINE SDL_Scancode rg_gui_find_pressed_scancode(const RgInputState* input)
{
	if (!input)
	{
		return SDL_SCANCODE_UNKNOWN;
	}

	int scancode_count = (int)SDL_SCANCODE_COUNT;
	if (scancode_count <= 0)
	{
		scancode_count = 512;
	}

	for (int scancode = 1; scancode < scancode_count; scancode++)
	{
		SDL_Scancode key = (SDL_Scancode)scancode;
		if (rg_input_is_key_pressed(input, key) && !rg_gui_scancode_is_modifier(key))
		{
			return key;
		}
	}

	return SDL_SCANCODE_UNKNOWN;
}

RGINLINE RgGuiShortcut rg_gui_shortcut_make(SDL_Scancode key, u32 mods)
{
	RgGuiShortcut shortcut;
	shortcut.key = key;
	shortcut.mods = mods;
	return shortcut;
}

RGINLINE u32 rg_gui_shortcut_mods_from_input(const RgInputState* input)
{
	u32 mods = 0u;
	if (rg_gui_input_has_ctrl(input))
	{
		mods |= RG_GUI_SHORTCUT_MOD_CTRL;
	}
	if (rg_gui_input_has_shift(input))
	{
		mods |= RG_GUI_SHORTCUT_MOD_SHIFT;
	}
	if (rg_gui_input_has_alt(input))
	{
		mods |= RG_GUI_SHORTCUT_MOD_ALT;
	}
	return mods;
}

RGINLINE const char* rg_gui_scancode_name(SDL_Scancode scancode, char* buffer, size_t buffer_size)
{
	if (!buffer || buffer_size == 0u)
	{
		return "";
	}

	if (scancode >= SDL_SCANCODE_A && scancode <= SDL_SCANCODE_Z)
	{
		buffer[0] = (char)('A' + (scancode - SDL_SCANCODE_A));
		buffer[1] = '\0';
		return buffer;
	}

	if (scancode >= SDL_SCANCODE_1 && scancode <= SDL_SCANCODE_9)
	{
		buffer[0] = (char)('1' + (scancode - SDL_SCANCODE_1));
		buffer[1] = '\0';
		return buffer;
	}

	if (scancode == SDL_SCANCODE_0)
	{
		buffer[0] = '0';
		buffer[1] = '\0';
		return buffer;
	}

	const char* name = SDL_GetScancodeName(scancode);
	if (name && name[0] != '\0')
	{
		return name;
	}

	rg_snprintf(buffer, buffer_size, "Key%d", (int)scancode);
	return buffer;
}

RGINLINE const char* rg_gui_shortcut_to_text(RgGuiShortcut shortcut, char* buffer, size_t buffer_size)
{
	if (!buffer || buffer_size == 0u || shortcut.key == SDL_SCANCODE_UNKNOWN)
	{
		return NULL;
	}

	size_t offset = 0u;
	int has_part = 0;

	if (shortcut.mods & RG_GUI_SHORTCUT_MOD_CTRL)
	{
		int written = rg_snprintf(buffer + offset, buffer_size - offset, "%sCtrl", has_part ? "+" : "");
		if (written > 0)
		{
			offset += (size_t)written;
			has_part = 1;
		}
	}
	if (shortcut.mods & RG_GUI_SHORTCUT_MOD_SHIFT)
	{
		int written = rg_snprintf(buffer + offset, buffer_size - offset, "%sShift", has_part ? "+" : "");
		if (written > 0)
		{
			offset += (size_t)written;
			has_part = 1;
		}
	}
	if (shortcut.mods & RG_GUI_SHORTCUT_MOD_ALT)
	{
		int written = rg_snprintf(buffer + offset, buffer_size - offset, "%sAlt", has_part ? "+" : "");
		if (written > 0)
		{
			offset += (size_t)written;
			has_part = 1;
		}
	}

	char key_name_buf[32];
	const char* key_name = rg_gui_scancode_name(shortcut.key, key_name_buf, sizeof(key_name_buf));
	if (key_name && key_name[0] != '\0')
	{
		int written = rg_snprintf(buffer + offset, buffer_size - offset, "%s%s", has_part ? "+" : "", key_name);
		if (written > 0)
		{
			offset += (size_t)written;
			has_part = 1;
		}
	}

	if (!has_part)
	{
		return NULL;
	}

	return buffer;
}

RGINLINE int rg_gui_shortcut_triggered(RgGuiContext* ctx, RgGuiShortcut shortcut, u32 flags)
{
	if (!ctx || !ctx->input || shortcut.key == SDL_SCANCODE_UNKNOWN)
	{
		return 0;
	}

	if (!(flags & RG_GUI_SHORTCUT_ALLOW_WHEN_TEXT_ACTIVE) && ctx->text_edit_state->active)
	{
		return 0;
	}

	if (!(flags & RG_GUI_SHORTCUT_ALLOW_WHEN_CAPTURED) &&
	    (ctx->input_capture_active || ctx->input_capture_depth > 0u))
	{
		return 0;
	}

	if (!(flags & RG_GUI_SHORTCUT_ALLOW_WHEN_MODAL) && ctx->modal_active)
	{
		return 0;
	}

	if (!rg_input_is_key_pressed(ctx->input, shortcut.key))
	{
		return 0;
	}

	u32 mods = rg_gui_shortcut_mods_from_input(ctx->input);
	if ((mods & shortcut.mods) != shortcut.mods)
	{
		return 0;
	}

	if (!(flags & RG_GUI_SHORTCUT_ALLOW_EXTRA_MODS) && mods != shortcut.mods)
	{
		return 0;
	}

	return 1;
}

RGINLINE int rg_gui_shortcut_index(RgGuiContext* ctx, const RgGuiShortcut* shortcuts, u32 count, u32 flags)
{
	if (!shortcuts || count == 0u)
	{
		return -1;
	}

	for (u32 i = 0u; i < count; i++)
	{
		if (rg_gui_shortcut_triggered(ctx, shortcuts[i], flags))
		{
			return (int)i;
		}
	}

	return -1;
}

RGINLINE int rg_gui_shortcut_undo(RgGuiContext* ctx, u32 flags)
{
	RgGuiShortcut undo_shortcut = rg_gui_shortcut_make(SDL_SCANCODE_Z, RG_GUI_SHORTCUT_MOD_CTRL);
	return rg_gui_shortcut_triggered(ctx, undo_shortcut, flags);
}

RGINLINE int rg_gui_shortcut_redo(RgGuiContext* ctx, u32 flags)
{
	RgGuiShortcut redo_shortcut = rg_gui_shortcut_make(SDL_SCANCODE_Y, RG_GUI_SHORTCUT_MOD_CTRL);
	RgGuiShortcut redo_alt = rg_gui_shortcut_make(SDL_SCANCODE_Z,
	                                              RG_GUI_SHORTCUT_MOD_CTRL | RG_GUI_SHORTCUT_MOD_SHIFT);
	return rg_gui_shortcut_triggered(ctx, redo_shortcut, flags) ||
	       rg_gui_shortcut_triggered(ctx, redo_alt, flags);
}

RGINLINE void rg_gui_undo_init(RgGuiUndoRedo* undo, RgGuiUndoCommand* commands, u32 command_capacity,
                               void* data, u32 data_capacity, void* user)
{
	if (!undo)
	{
		return;
	}

	undo->commands = commands;
	undo->data = (u8*)data;
	undo->command_capacity = command_capacity;
	undo->data_capacity = data_capacity;
	undo->command_head = 0u;
	undo->command_count = 0u;
	undo->command_index = 0u;
	undo->data_head = 0u;
	undo->data_used = 0u;
	undo->user = user;
}

RGINLINE void rg_gui_undo_clear(RgGuiUndoRedo* undo)
{
	if (!undo)
	{
		return;
	}

	undo->command_head = 0u;
	undo->command_count = 0u;
	undo->command_index = 0u;
	undo->data_head = 0u;
	undo->data_used = 0u;
}

RGINLINE void rg_gui_undo_drop_oldest(RgGuiUndoRedo* undo)
{
	if (!undo || undo->command_count == 0u || undo->command_capacity == 0u)
	{
		return;
	}

	u32 head = undo->command_head;
	u32 data_size = undo->commands[head].data_size;
	if (undo->data_used >= data_size)
	{
		undo->data_used -= data_size;
	}
	else
	{
		undo->data_used = 0u;
	}

	undo->command_head = (head + 1u) % undo->command_capacity;
	undo->command_count--;

	if (undo->command_index > 0u)
	{
		undo->command_index--;
	}

	if (undo->command_count == 0u)
	{
		undo->command_head = 0u;
		undo->command_index = 0u;
		undo->data_head = 0u;
		undo->data_used = 0u;
	}
}

RGINLINE void rg_gui_undo_rebuild_data(RgGuiUndoRedo* undo)
{
	if (!undo)
	{
		return;
	}

	if (undo->command_count == 0u || undo->command_capacity == 0u || undo->data_capacity == 0u)
	{
		undo->data_used = 0u;
		undo->data_head = 0u;
		return;
	}

	u32 total = 0u;
	for (u32 i = 0u; i < undo->command_count; i++)
	{
		u32 idx = (undo->command_head + i) % undo->command_capacity;
		total += undo->commands[idx].data_size;
	}

	undo->data_used = total;
	u32 last = (undo->command_head + undo->command_count - 1u) % undo->command_capacity;
	u32 end = undo->commands[last].data_offset + undo->commands[last].data_size;
	undo->data_head = (end >= undo->data_capacity) ? 0u : end;
}

RGINLINE int rg_gui_undo_can_write(const RgGuiUndoRedo* undo, u32 size)
{
	if (!undo || size == 0u)
	{
		return 1;
	}
	if (!undo->data || undo->data_capacity == 0u || size > undo->data_capacity)
	{
		return 0;
	}

	if (undo->command_count == 0u)
	{
		return 1;
	}

	u32 head = undo->data_head;
	u32 capacity = undo->data_capacity;
	u32 write = head;
	if (write + size > capacity)
	{
		write = 0u;
	}

	u32 tail = undo->commands[undo->command_head].data_offset;
	if (write < tail)
	{
		return size <= (tail - write);
	}
	return size <= (capacity - write);
}

RGINLINE int rg_gui_undo_push(RgGuiUndoRedo* undo, RgGuiUndoFn undo_fn, RgGuiUndoFn redo_fn,
                              const void* data, u32 data_size)
{
	if (!undo || !undo->commands || undo->command_capacity == 0u)
	{
		return 0;
	}

	size_t aligned = RG_ALIGN_UP((size_t)data_size, RG_ALIGNOF(void*));
	if (aligned > UINT32_MAX)
	{
		return 0;
	}

	u32 size_u32 = (u32)aligned;
	if (size_u32 > 0u && (!undo->data || undo->data_capacity == 0u || size_u32 > undo->data_capacity))
	{
		return 0;
	}

	if (undo->command_index < undo->command_count)
	{
		undo->command_count = undo->command_index;
		if (undo->command_count == 0u)
		{
			undo->command_head = 0u;
			undo->command_index = 0u;
			undo->data_head = 0u;
			undo->data_used = 0u;
		}
		else
		{
			rg_gui_undo_rebuild_data(undo);
		}
	}

	while (undo->command_count > 0u &&
	       (undo->command_count >= undo->command_capacity ||
	        undo->data_used + size_u32 > undo->data_capacity ||
	        !rg_gui_undo_can_write(undo, size_u32)))
	{
		rg_gui_undo_drop_oldest(undo);
	}

	if (undo->command_count >= undo->command_capacity)
	{
		return 0;
	}
	if (size_u32 > 0u && (undo->data_used + size_u32 > undo->data_capacity || !rg_gui_undo_can_write(undo, size_u32)))
	{
		return 0;
	}

	u32 write_offset = undo->data_head;
	if (size_u32 > 0u && write_offset + size_u32 > undo->data_capacity)
	{
		write_offset = 0u;
	}

	if (size_u32 > 0u && data)
	{
		memcpy(undo->data + write_offset, data, data_size);
	}

	u32 cmd_index = (undo->command_head + undo->command_count) % undo->command_capacity;
	RgGuiUndoCommand* cmd = &undo->commands[cmd_index];
	cmd->undo = undo_fn;
	cmd->redo = redo_fn;
	cmd->data_offset = write_offset;
	cmd->data_size = size_u32;

	undo->command_count++;
	undo->command_index = undo->command_count;

	if (size_u32 > 0u)
	{
		undo->data_head = write_offset + size_u32;
		if (undo->data_head >= undo->data_capacity)
		{
			undo->data_head = 0u;
		}
		undo->data_used += size_u32;
	}

	return 1;
}

RGINLINE int rg_gui_undo_undo(RgGuiUndoRedo* undo)
{
	if (!undo || !undo->commands || undo->command_capacity == 0u || undo->command_index == 0u)
	{
		return 0;
	}

	undo->command_index--;
	u32 index = (undo->command_head + undo->command_index) % undo->command_capacity;
	const RgGuiUndoCommand* cmd = &undo->commands[index];
	const void* payload = (cmd->data_size > 0u && undo->data) ? (const void*)(undo->data + cmd->data_offset) : NULL;
	if (cmd->undo)
	{
		cmd->undo(undo->user, payload);
	}
	return 1;
}

RGINLINE int rg_gui_undo_redo(RgGuiUndoRedo* undo)
{
	if (!undo || !undo->commands || undo->command_capacity == 0u || undo->command_index >= undo->command_count)
	{
		return 0;
	}

	u32 index = (undo->command_head + undo->command_index) % undo->command_capacity;
	const RgGuiUndoCommand* cmd = &undo->commands[index];
	const void* payload = (cmd->data_size > 0u && undo->data) ? (const void*)(undo->data + cmd->data_offset) : NULL;
	if (cmd->redo)
	{
		cmd->redo(undo->user, payload);
	}
	undo->command_index++;
	return 1;
}

RGINLINE u32 rg_gui_undo_undo_count(const RgGuiUndoRedo* undo)
{
	if (!undo)
	{
		return 0u;
	}
	return undo->command_index;
}

RGINLINE u32 rg_gui_undo_redo_count(const RgGuiUndoRedo* undo)
{
	if (!undo)
	{
		return 0u;
	}
	return (undo->command_count > undo->command_index) ? (undo->command_count - undo->command_index) : 0u;
}

RGINLINE char rg_gui_label_hotkey(const char* label)
{
	if (!label)
	{
		return '\0';
	}

	for (const char* it = label; *it; ++it)
	{
		char c = *it;
		if ((c >= 'A' && c <= 'Z') ||
		    (c >= 'a' && c <= 'z') ||
		    (c >= '0' && c <= '9'))
		{
			return rg_gui_ascii_lower(c);
		}
	}

	return '\0';
}

RGINLINE int rg_gui_text_has_prefix(const char* text, const char* prefix)
{
	if (!text || !prefix || !*prefix)
	{
		return 0;
	}

	while (*prefix)
	{
		char a = rg_gui_ascii_lower(*text++);
		char b = rg_gui_ascii_lower(*prefix++);
		if (a == '\0' || a != b)
		{
			return 0;
		}
	}

	return 1;
}

RGINLINE int rg_gui_find_prefix(const char* const* items, u32 count, const char* prefix, int start)
{
	if (count > (u32)INT_MAX) count = (u32)INT_MAX;
	if (!items || !prefix || !*prefix || count == 0u)
	{
		return -1;
	}

	if (start < 0)
	{
		start = 0;
	}

	for (u32 i = (u32)start; i < count; i++)
	{
		const char* item = items[i];
		if (item && rg_gui_text_has_prefix(item, prefix))
		{
			return (int)i;
		}
	}

	return -1;
}

RGINLINE int rg_gui_find_prefix_fn(RgGuiListItemFn get_item, const void* user, u32 count, const char* prefix, int start)
{
	if (count > (u32)INT_MAX) count = (u32)INT_MAX;
	if (!get_item || !prefix || !*prefix || count == 0u)
	{
		return -1;
	}

	if (start < 0)
	{
		start = 0;
	}

	for (u32 i = (u32)start; i < count; i++)
	{
		const char* item = get_item(user, i);
		if (item && rg_gui_text_has_prefix(item, prefix))
		{
			return (int)i;
		}
	}

	return -1;
}

RGINLINE void rg_gui_text_filter_init(RgGuiTextFilter* filter)
{
	if (!filter)
	{
		return;
	}

	filter->buffer[0] = '\0';
	filter->length = 0u;
	filter->version = 1u;
	filter->parsed_version = 0u;
	filter->token_count = 0u;
}

RGINLINE void rg_gui_text_filter_refresh(RgGuiTextFilter* filter)
{
	if (!filter)
	{
		return;
	}

	filter->length = (u32)strlen(filter->buffer);
	filter->version++;
	filter->parsed_version = 0u;
}

RGINLINE void rg_gui_text_filter_parse(RgGuiTextFilter* filter)
{
	if (!filter)
	{
		return;
	}

	if (filter->parsed_version == filter->version)
	{
		return;
	}

	filter->token_count = 0u;
	if (filter->length == 0u)
	{
		filter->parsed_version = filter->version;
		return;
	}

	const char* it = filter->buffer;
	const char* end = filter->buffer + filter->length;
	while (it < end)
	{
		while (it < end && rg_gui_ascii_is_space(*it))
		{
			it++;
		}
		if (it >= end)
		{
			break;
		}

		int exclude = 0;
		if (*it == '-')
		{
			exclude = 1;
			it++;
		}

		const char* start = it;
		while (it < end && !rg_gui_ascii_is_space(*it))
		{
			it++;
		}

		size_t len = (size_t)(it - start);
		if (len > 0u && filter->token_count < RG_GUI_TEXT_FILTER_MAX_TOKENS)
		{
			RgGuiTextFilterToken* token = &filter->tokens[filter->token_count++];
			token->text = start;
			token->length = (u16)len;
			token->exclude = exclude ? 1u : 0u;
			token->pad = 0u;
		}
	}

	filter->parsed_version = filter->version;
}

RGINLINE int rg_gui_text_filter_active(const RgGuiTextFilter* filter)
{
	return (filter && filter->length > 0u) ? 1 : 0;
}

RGINLINE int rg_gui_text_contains(const char* text, const char* needle, size_t needle_len)
{
	if (!text || !needle || needle_len == 0u)
	{
		return 0;
	}

	for (const char* it = text; *it; ++it)
	{
		size_t i = 0u;
		while (i < needle_len)
		{
			char a = rg_gui_ascii_lower(it[i]);
			char b = rg_gui_ascii_lower(needle[i]);
			if (a == '\0' || a != b)
			{
				break;
			}
			i++;
		}
		if (i == needle_len)
		{
			return 1;
		}
	}

	return 0;
}

RGINLINE int rg_gui_text_filter_match(RgGuiTextFilter* filter, const char* text)
{
	if (!filter || filter->length == 0u)
	{
		return 1;
	}
	if (!text)
	{
		return 0;
	}

	rg_gui_text_filter_parse(filter);
	if (filter->token_count == 0u)
	{
		return 1;
	}

	for (u32 i = 0u; i < filter->token_count; i++)
	{
		const RgGuiTextFilterToken* token = &filter->tokens[i];
		int hit = rg_gui_text_contains(text, token->text, token->length);
		if (token->exclude)
		{
			if (hit)
			{
				return 0;
			}
		}
		else if (!hit)
		{
			return 0;
		}
	}

	return 1;
}

RGINLINE int rg_gui_text_filter_input(RgGuiContext* ctx, const char* label, RgGuiTextFilter* filter,
                                      RgGuiRect rect, RgGuiId id)
{
	if (!filter)
	{
		return 0;
	}

	int changed = rg_gui_text_input_ex(ctx, label, filter->buffer, sizeof(filter->buffer),
	                                   rect, id, RG_GUI_TEXT_INPUT_NONE, NULL);
	if (changed)
	{
		filter->length = (u32)strlen(filter->buffer);
		filter->version++;
		filter->parsed_version = 0u;
	}

	return changed;
}

RGINLINE u32 rg_gui_text_filter_list_indices(RgGuiListItemFn get_item, const void* user, u32 count,
                                             RgGuiTextFilter* filter, u32* out_indices, u32 capacity)
{
	if (!out_indices || capacity == 0u)
	{
		return 0u;
	}

	if (!get_item || count == 0u)
	{
		return 0u;
	}

	if (!filter || filter->length == 0u)
	{
		u32 out_count = count;
		if (out_count > capacity)
		{
			out_count = capacity;
		}
		for (u32 i = 0u; i < out_count; i++)
		{
			out_indices[i] = i;
		}
		return out_count;
	}

	rg_gui_text_filter_parse(filter);
	if (filter->token_count == 0u)
	{
		u32 out_count = count;
		if (out_count > capacity)
		{
			out_count = capacity;
		}
		for (u32 i = 0u; i < out_count; i++)
		{
			out_indices[i] = i;
		}
		return out_count;
	}

	u32 out_count = 0u;
	for (u32 i = 0u; i < count; i++)
	{
		const char* item = get_item(user, i);
		if (rg_gui_text_filter_match(filter, item))
		{
			out_indices[out_count++] = i;
			if (out_count >= capacity)
			{
				break;
			}
		}
	}

	return out_count;
}

RGINLINE u32 rg_gui_text_filter_tree(const u32* rows, const u32* depths, u32 row_count,
                                     RgGuiListItemFn get_label, const void* user, RgGuiTextFilter* filter,
                                     u8* scratch, u32 scratch_count,
                                     u32* out_rows, u32* out_depths, u32 capacity)
{
	if (!rows || !depths || !out_rows || !out_depths || !scratch || capacity == 0u)
	{
		return 0u;
	}

	if (!get_label || !filter || filter->length == 0u)
	{
		u32 out_count = row_count;
		if (out_count > capacity)
		{
			out_count = capacity;
		}
		for (u32 i = 0u; i < out_count; i++)
		{
			out_rows[i] = rows[i];
			out_depths[i] = depths[i];
		}
		return out_count;
	}

	if (scratch_count < row_count)
	{
		RG_GUI_ASSERT(0 && "rg_gui_text_filter_tree scratch too small");
		return 0u;
	}
	rg_gui_text_filter_parse(filter);

	for (u32 i = 0u; i < row_count; i++)
	{
		const char* label = get_label(user, rows[i]);
		scratch[i] = rg_gui_text_filter_match(filter, label) ? 1u : 0u;
	}

	u32 stack_count = 0u;
	for (u32 i = 0u; i < row_count; i++)
	{
		u32 depth = depths[i];
		while (stack_count > depth)
		{
			stack_count--;
		}

		u32 parent = UINT32_MAX;
		if (depth > 0u && stack_count > 0u)
		{
			parent = out_depths[stack_count - 1u];
		}
		out_rows[i] = parent;

		out_depths[stack_count++] = i;
	}

	for (u32 i = row_count; i-- > 0u;)
	{
		if (scratch[i])
		{
			u32 parent = out_rows[i];
			if (parent != UINT32_MAX)
			{
				scratch[parent] = 1u;
			}
		}
	}

	u32 out_count = 0u;
	for (u32 i = 0u; i < row_count && out_count < capacity; i++)
	{
		if (scratch[i])
		{
			out_rows[out_count] = rows[i];
			out_depths[out_count] = depths[i];
			out_count++;
		}
	}

	return out_count;
}

RGINLINE int rg_gui_repeat_count_bounded(f32 elapsed, f32 interval)
{
	if (!(elapsed > 0.0f) || !(interval > 0.0f))
	{
		return 0;
	}
	if (elapsed < interval)
	{
		return 0;
	}
	f32 count = elapsed / interval;
	if (!(count > 0.0f))
	{
		return 0;
	}
	if (count >= (f32)RG_GUI_REPEAT_MAX_PER_FRAME)
	{
		return RG_GUI_REPEAT_MAX_PER_FRAME;
	}
	return (int)count;
}

RGINLINE int rg_gui_nonnegative_f32_to_int_bounded(f32 value)
{
	if (!(value > 0.0f)) return 0;
	if (value >= (f32)INT_MAX) return INT_MAX;
	return (int)value;
}

RGINLINE u32 rg_gui_nonnegative_f32_to_u32_bounded(f32 value)
{
	if (!(value > 0.0f)) return 0u;
	if (value >= (f32)UINT32_MAX) return UINT32_MAX;
	return (u32)value;
}

RGINLINE int rg_gui_u32_count_to_int(u32 count)
{
	return count > (u32)INT_MAX ? INT_MAX : (int)count;
}

RGINLINE int rg_gui_scroll_index_from_ratio(f32 ratio, int max_index)
{
	if (max_index <= 0 || !(ratio > 0.0f)) return 0;
	if (ratio >= 1.0f) return max_index;
	f64 rounded = (f64)ratio * (f64)max_index + 0.5;
	if (rounded >= (f64)max_index) return max_index;
	return (int)rounded;
}

RGINLINE int rg_gui_bucket_index_from_ratio(f32 ratio, int count)
{
	if (count <= 1 || !(ratio > 0.0f)) return 0;
	if (ratio >= 1.0f) return count - 1;
	f64 scaled = (f64)ratio * (f64)count;
	if (scaled >= (f64)count) return count - 1;
	return (int)scaled;
}

RGINLINE int rg_gui_round_f64_to_int_saturated(f64 value, int* out_value)
{
	if (!out_value || value != value)
	{
		return 0;
	}
	if (value >= (f64)INT_MAX - 0.5)
	{
		*out_value = INT_MAX;
	}
	else if (value <= (f64)INT_MIN + 0.5)
	{
		*out_value = INT_MIN;
	}
	else
	{
		*out_value = (int)(value >= 0.0 ? value + 0.5 : value - 0.5);
	}
	return 1;
}

RGINLINE int rg_gui_int_step_clamped(int value, i64 step, int repeats,
                                     int direction, int min_value, int max_value)
{
	if (repeats <= 0 || step == 0)
	{
		return rg_gui_i64_clamp_to_int((i64)value, min_value, max_value);
	}

	u64 magnitude = step < 0 ? (u64)(-(step + 1)) + 1u : (u64)step;
	u64 repeat_count = (u64)repeats;
	if (direction < 0)
	{
		if (value <= min_value) return min_value;
		u64 distance = (u64)((i64)value - (i64)min_value);
		if (magnitude > distance)
		{
			return min_value;
		}
		u64 delta = magnitude * repeat_count;
		if (delta > distance)
		{
			return min_value;
		}
		i64 next = (i64)value - (i64)delta;
		return rg_gui_i64_clamp_to_int(next, min_value, max_value);
	}

	if (value >= max_value) return max_value;
	u64 distance = (u64)((i64)max_value - (i64)value);
	if (magnitude > distance)
	{
		return max_value;
	}
	u64 delta = magnitude * repeat_count;
	if (delta > distance)
	{
		return max_value;
	}
	i64 next = (i64)value + (i64)delta;
	return rg_gui_i64_clamp_to_int(next, min_value, max_value);
}

RGINLINE int rg_gui_key_repeat(RgGuiContext* ctx, RgGuiId owner, SDL_Scancode key)
{
	if (!ctx || !ctx->input)
	{
		return 0;
	}

	int pressed = rg_input_is_key_pressed(ctx->input, key);
	int down = rg_input_is_key_down(ctx->input, key);

	if (pressed)
	{
		ctx->key_repeat_owner = owner;
		ctx->key_repeat_key = key;
		ctx->key_repeat_hold_time = 0.0f;
		ctx->key_repeat_repeat_time = 0.0f;
		return 1;
	}

	if (!down || owner != ctx->key_repeat_owner || key != ctx->key_repeat_key)
	{
		if (!down && owner == ctx->key_repeat_owner && key == ctx->key_repeat_key)
		{
			ctx->key_repeat_owner = 0u;
			ctx->key_repeat_key = SDL_SCANCODE_UNKNOWN;
			ctx->key_repeat_hold_time = 0.0f;
			ctx->key_repeat_repeat_time = 0.0f;
		}
		return 0;
	}

	f32 delay = ctx->style.key_repeat_delay;
	f32 interval = ctx->style.key_repeat_interval;
	f32 fast_delay = ctx->style.key_repeat_fast_delay;
	f32 fast_interval = ctx->style.key_repeat_fast_interval;

	if (delay < 0.0f) delay = 0.0f;
	if (!(interval > 0.0f)) interval = 0.01f;
	if (fast_delay < delay) fast_delay = delay;
	if (!(fast_interval > 0.0f)) fast_interval = interval;

	ctx->key_repeat_hold_time += ctx->delta_time;
	if (ctx->key_repeat_hold_time < delay)
	{
		return 0;
	}

	f32 use_interval = (ctx->key_repeat_hold_time >= fast_delay) ? fast_interval : interval;
	if (!(use_interval > 0.0f))
	{
		use_interval = interval;
	}

	ctx->key_repeat_repeat_time += ctx->delta_time;
	int repeats = rg_gui_repeat_count_bounded(ctx->key_repeat_repeat_time, use_interval);
	if (repeats > 0)
	{
		if (repeats == RG_GUI_REPEAT_MAX_PER_FRAME)
			ctx->key_repeat_repeat_time = 0.0f;
		else
			ctx->key_repeat_repeat_time -= (f32)repeats * use_interval;
		return repeats;
	}

	return 0;
}

RGINLINE int rg_gui_wheel_steps(f32 wheel)
{
	if (wheel != wheel)
	{
		return 0;
	}
	if (wheel >= (f32)RG_GUI_REPEAT_MAX_PER_FRAME)
	{
		return RG_GUI_REPEAT_MAX_PER_FRAME;
	}
	if (wheel <= -(f32)RG_GUI_REPEAT_MAX_PER_FRAME)
	{
		return -RG_GUI_REPEAT_MAX_PER_FRAME;
	}
	int steps = (int)wheel;
	if (steps == 0 && wheel != 0.0f)
	{
		steps = (wheel > 0.0f) ? 1 : -1;
	}
	return steps;
}

RGINLINE int rg_gui_nav_list_move(RgGuiContext* ctx, RgGuiId owner, int count, int visible,
                                  int cursor, int* out_target, int* out_dir)
{
	if (out_target)
	{
		*out_target = cursor;
	}
	if (out_dir)
	{
		*out_dir = 0;
	}

	if (!ctx || !ctx->input || count <= 0)
	{
		return 0;
	}

	if (visible < 1)
	{
		visible = 1;
	}

	int target = (cursor >= 0) ? cursor : 0;
	int moved = 0;
	int dir = 0;

	if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP) ||
	    rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
	{
		SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP)
		                       ? SDL_SCANCODE_UP
		                       : SDL_SCANCODE_DOWN;
		int repeats = rg_gui_key_repeat(ctx, owner, key);
		if (repeats > 0)
		{
			target = rg_gui_int_step_clamped(target, 1, repeats,
			                                 key == SDL_SCANCODE_UP ? -1 : 1,
			                                 0, count - 1);
			moved = 1;
			dir = (key == SDL_SCANCODE_UP) ? -1 : 1;
		}
	}

	if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_PAGEUP) ||
	    rg_input_is_key_down(ctx->input, SDL_SCANCODE_PAGEDOWN))
	{
		SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_PAGEUP)
		                       ? SDL_SCANCODE_PAGEUP
		                       : SDL_SCANCODE_PAGEDOWN;
		int repeats = rg_gui_key_repeat(ctx, owner, key);
		if (repeats > 0)
		{
			target = rg_gui_int_step_clamped(target, (i64)visible, repeats,
			                                 key == SDL_SCANCODE_PAGEUP ? -1 : 1,
			                                 0, count - 1);
			moved = 1;
			dir = (key == SDL_SCANCODE_PAGEUP) ? -1 : 1;
		}
	}

	if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_HOME))
	{
		target = 0;
		moved = 1;
		dir = 1;
	}
	if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_END))
	{
		target = count - 1;
		moved = 1;
		dir = -1;
	}

	if (moved)
	{
		if (target < 0)
		{
			target = 0;
		}
		if (target >= count)
		{
			target = count - 1;
		}
	}

	if (out_target)
	{
		*out_target = target;
	}
	if (out_dir)
	{
		*out_dir = dir;
	}
	return moved;
}

RGINLINE void rg_gui_register_focusable(RgGuiContext* ctx, RgGuiId id)
{
	if (!ctx || id == 0u)
	{
		return;
	}

	if (rg_gui_is_disabled(ctx))
	{
		return;
	}

	if (ctx->tab_prev_id == id)
	{
		return;
	}

	if (ctx->tab_first_id == 0u)
	{
		ctx->tab_first_id = id;
	}
	ctx->tab_last_id = id;

	if (ctx->focus_id == id)
	{
		ctx->tab_found_focus = 1;
	}

	if (ctx->tab_dir > 0)
	{
		if (ctx->focus_id == 0u && ctx->tab_focus_id == 0u)
		{
			ctx->tab_focus_id = id;
		}

		if (ctx->focus_id == id && ctx->tab_focus_id == 0u)
		{
			ctx->tab_seek = 1;
		}
		else if (ctx->tab_seek && ctx->tab_focus_id == 0u)
		{
			ctx->tab_focus_id = id;
			ctx->tab_seek = 0;
		}
	}
	else if (ctx->tab_dir < 0)
	{
		if (ctx->focus_id == id && ctx->tab_focus_id == 0u)
		{
			if (ctx->tab_prev_id != 0u)
			{
				ctx->tab_focus_id = ctx->tab_prev_id;
			}
			else
			{
				ctx->tab_wrap = 1;
			}
		}
	}

	ctx->tab_prev_id = id;
}

static RG_NOINLINE void rg_gui_diagnostics_add_draw_drop(RgGuiContext* ctx, u32 count)
{
	ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_DRAW_CAPACITY;
	if (count > UINT32_MAX - ctx->diagnostics.dropped_draw_commands)
	{
		ctx->diagnostics.dropped_draw_commands = UINT32_MAX;
	}
	else
	{
		ctx->diagnostics.dropped_draw_commands += count;
	}
}

static RG_NOINLINE void rg_gui_diagnostics_add_text_drop(RgGuiContext* ctx, size_t count)
{
	size_t maximum = (size_t)-1;
	ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_TEXT_CAPACITY;
	if (count > maximum - ctx->diagnostics.dropped_text_bytes)
	{
		ctx->diagnostics.dropped_text_bytes = maximum;
	}
	else
	{
		ctx->diagnostics.dropped_text_bytes += count;
	}
}

RGINLINE void rg_gui_diagnostics_update_draw_high_water(RgGuiContext* ctx,
                                                        const RgGuiDrawList* draw,
                                                        const RgGuiDrawList* overlay)
{
	size_t count = draw ? (size_t)draw->count : 0u;
	size_t overlay_count = overlay ? (size_t)overlay->count : 0u;
	size_t maximum = (size_t)-1;
	if (overlay_count > maximum - count)
	{
		count = maximum;
	}
	else
	{
		count += overlay_count;
	}
	if (count > ctx->diagnostics.draw_command_high_water)
	{
		ctx->diagnostics.draw_command_high_water = count;
	}
}

RGINLINE const char* rg_gui_copy_text(RgGuiContext* ctx, const char* text)
{
	if (!text)
	{
		return "";
	}

	size_t len = strlen(text);
	size_t available = ctx->text_buffer_used <= ctx->text_buffer_capacity ? ctx->text_buffer_capacity - ctx->text_buffer_used : 0u;
	if (len >= available)
	{
		rg_gui_diagnostics_add_text_drop(ctx, len < (size_t)-1 ? len + 1u : (size_t)-1);
		return "";
	}

	char* dst = ctx->text_buffer + ctx->text_buffer_used;
	if (len > 0u)
	{
		memcpy(dst, text, len);
	}
	dst[len] = '\0';
	ctx->text_buffer_used += len + 1u;
	return dst;
}

RGINLINE const char* rg_gui_copy_text_range(RgGuiContext* ctx, const char* text, size_t start, size_t end)
{
	if (!text || start >= end)
	{
		return "";
	}

	size_t len = end - start;
	size_t available = ctx->text_buffer_used <= ctx->text_buffer_capacity ? ctx->text_buffer_capacity - ctx->text_buffer_used : 0u;
	if (len >= available)
	{
		rg_gui_diagnostics_add_text_drop(ctx, len < (size_t)-1 ? len + 1u : (size_t)-1);
		return "";
	}

	char* dst = ctx->text_buffer + ctx->text_buffer_used;
	memcpy(dst, text + start, len);
	dst[len] = '\0';
	ctx->text_buffer_used += len + 1u;
	return dst;
}

typedef struct RgGuiTextEditPreeditLayout
{
	size_t host_start;
	size_t host_end;
	size_t preedit_start;
	size_t preedit_end;
	size_t caret;
	size_t selection_start;
	size_t selection_end;
} RgGuiTextEditPreeditLayout;

RGINLINE RgGuiTextEditPreeditLayout rg_gui_text_edit_preedit_layout(
    const RgGuiTextEditState* edit, size_t text_length);

RGINLINE const char* rg_gui_text_edit_display_text(RgGuiContext* ctx,
                                                   const RgGuiTextEditState* edit,
                                                   const char* text, size_t text_length,
                                                   size_t* out_length,
                                                   size_t* out_preedit_length)
{
	if (out_length)
	{
		*out_length = text_length;
	}
	if (out_preedit_length)
	{
		*out_preedit_length = 0u;
	}
	if (!ctx || !edit || !text || edit->preedit_length == 0u)
	{
		return text;
	}

	RgGuiTextEditPreeditLayout layout =
	    rg_gui_text_edit_preedit_layout(edit, text_length);
	size_t retained_length = text_length - (layout.host_end - layout.host_start);
	if (edit->preedit_length > SIZE_MAX - retained_length)
	{
		rg_gui_diagnostics_add_text_drop(ctx, (size_t)-1);
		return text;
	}
	size_t combined_length = retained_length + edit->preedit_length;
	size_t available = ctx->text_buffer_used <= ctx->text_buffer_capacity ? ctx->text_buffer_capacity - ctx->text_buffer_used : 0u;
	if (combined_length == (size_t)-1)
	{
		rg_gui_diagnostics_add_text_drop(ctx, (size_t)-1);
		return text;
	}

	size_t required = combined_length + 1u;
	if (!ctx->text_buffer || required > available)
	{
		rg_gui_diagnostics_add_text_drop(ctx, required);
		return text;
	}

	char* dst = ctx->text_buffer + ctx->text_buffer_used;
	memcpy(dst, text, layout.host_start);
	memcpy(dst + layout.preedit_start, edit->preedit, edit->preedit_length);
	memcpy(dst + layout.preedit_end, text + layout.host_end,
	       text_length - layout.host_end);
	dst[combined_length] = '\0';
	ctx->text_buffer_used += required;
	if (out_length)
	{
		*out_length = combined_length;
	}
	if (out_preedit_length)
	{
		*out_preedit_length = edit->preedit_length;
	}
	return dst;
}

RGINLINE void rg_gui_drag_clear(RgGuiContext* ctx)
{
	ctx->drag_active = 0;
	ctx->drag_source_id = 0u;
	ctx->drag_payload.type = 0u;
	ctx->drag_payload.data = NULL;
	ctx->drag_payload.size = 0u;
	ctx->drag_start = rg_vec2(0.0f, 0.0f);
}

RGINLINE RgGuiDrawList* rg_gui_draw_list_target(RgGuiContext* ctx)
{
	if (!ctx)
	{
		return NULL;
	}
	if (ctx->draw_target)
	{
		return ctx->draw_target;
	}
	return &ctx->draw_list;
}

RGINLINE RgGuiDrawList* rg_gui_overlay_target(RgGuiContext* ctx)
{
	if (!ctx)
	{
		return NULL;
	}

	if (ctx->modal_active)
	{
		if (ctx->modal_base_target)
		{
			return ctx->modal_base_target;
		}
		return ctx->draw_target ? ctx->draw_target : &ctx->draw_list;
	}

	if (ctx->overlay_target)
	{
		return ctx->overlay_target;
	}

	return &ctx->overlay_list;
}

RGINLINE rg_vec2 rg_gui_snap_text_pos(rg_vec2 pos)
{
	return rg_vec2(floorf(pos.x + 0.5f), floorf(pos.y + 0.5f));
}

RGINLINE void rg_gui_push_clip(RgGuiContext* ctx, RgGuiRect rect)
{
	RgGuiDrawList* list = rg_gui_draw_list_target(ctx);
	if (!list || list->count >= list->capacity)
	{
		if (list)
		{
			rg_gui_diagnostics_add_draw_drop(ctx, 1u);
		}
		return;
	}

	RgGuiDrawCmd* cmd = &list->cmds[list->count++];
	cmd->type = RG_GUI_CMD_CLIP_PUSH;
	cmd->data.clip.rect = rect;
}

RGINLINE void rg_gui_pop_clip(RgGuiContext* ctx)
{
	RgGuiDrawList* list = rg_gui_draw_list_target(ctx);
	if (!list || list->count >= list->capacity)
	{
		if (list)
		{
			rg_gui_diagnostics_add_draw_drop(ctx, 1u);
		}
		return;
	}

	RgGuiDrawCmd* cmd = &list->cmds[list->count++];
	cmd->type = RG_GUI_CMD_CLIP_POP;
}

RGINLINE void rg_gui_push_rect_to(RgGuiContext* ctx, RgGuiDrawList* list, RgGuiRect rect, rg_vec4 color)
{
	if (!list || list->count >= list->capacity)
	{
		if (list)
		{
			rg_gui_diagnostics_add_draw_drop(ctx, 1u);
		}
		return;
	}

	RgGuiDrawCmd* cmd = &list->cmds[list->count++];
	cmd->type = RG_GUI_CMD_RECT;
	cmd->data.rect.rect = rect;
	cmd->data.rect.color = rg_gui_apply_disabled_color(ctx, color);
}

RGINLINE void rg_gui_push_triangle_to(RgGuiContext* ctx, RgGuiDrawList* list, rg_vec2 a, rg_vec2 b, rg_vec2 c, rg_vec4 color)
{
	if (!list || list->count >= list->capacity)
	{
		if (list)
		{
			rg_gui_diagnostics_add_draw_drop(ctx, 1u);
		}
		return;
	}

	RgGuiDrawCmd* cmd = &list->cmds[list->count++];
	cmd->type = RG_GUI_CMD_TRIANGLE;
	cmd->data.triangle.a = a;
	cmd->data.triangle.b = b;
	cmd->data.triangle.c = c;
	cmd->data.triangle.color = rg_gui_apply_disabled_color(ctx, color);
}

RGINLINE void rg_gui_push_image_to(RgGuiContext* ctx, RgGuiDrawList* list, RgGuiRect rect,
                                   RgGuiRect uv, rg_vec4 color, RgGuiTexture texture);
RGINLINE void rg_gui_push_image_material_to(RgGuiContext* ctx, RgGuiDrawList* list,
                                            RgGuiRect rect, RgGuiRect uv, rg_vec4 color,
                                            RgGuiTexture texture, RgGuiImageMaterial material);

/**
 * @brief Draw a centered filled directional arrow into a specific draw list
 */
RGINLINE void rg_gui_push_arrow_to(RgGuiContext* ctx, RgGuiDrawList* list,
                                   RgGuiRect bounds, RgGuiArrowDirection direction,
                                   rg_vec4 color)
{
	if (!ctx || !list || bounds.w <= 0.0f || bounds.h <= 0.0f ||
	    direction < RG_GUI_ARROW_LEFT || direction > RG_GUI_ARROW_DOWN)
	{
		return;
	}

	f32 size = ctx->style.text_height;
	if (size > bounds.w)
	{
		size = bounds.w;
	}
	if (size > bounds.h)
	{
		size = bounds.h;
	}
	if (size <= 0.0f)
	{
		return;
	}

	const RgGuiIcon* icon = &ctx->arrow_icons[direction];
	if (icon->texture != 0)
	{
		RgGuiRect icon_rect = rg_gui_make_rect(bounds.x + (bounds.w - size) * 0.5f,
		                                       bounds.y + (bounds.h - size) * 0.5f,
		                                       size, size);
		rg_vec4 tint = rg_vec4(icon->tint.x * color.x,
		                       icon->tint.y * color.y,
		                       icon->tint.z * color.z,
		                       icon->tint.w * color.w);
		rg_gui_push_image_material_to(ctx, list, icon_rect, icon->uv, tint,
		                              icon->texture, icon->material);
		return;
	}

	// Match Unity's 14x9 arrow silhouette inside a 24px design square.
	f32 wide_half = size * (7.0f / 24.0f);
	f32 narrow_half = size * (4.5f / 24.0f);
	f32 center_x = floorf((bounds.x + bounds.w * 0.5f) * 2.0f + 0.5f) * 0.5f;
	f32 center_y = floorf((bounds.y + bounds.h * 0.5f) * 2.0f + 0.5f) * 0.5f;
	rg_vec2 a;
	rg_vec2 b;
	rg_vec2 c;

	if (direction == RG_GUI_ARROW_LEFT)
	{
		a = rg_vec2(center_x + narrow_half, center_y - wide_half);
		b = rg_vec2(center_x + narrow_half, center_y + wide_half);
		c = rg_vec2(center_x - narrow_half, center_y);
	}
	else if (direction == RG_GUI_ARROW_RIGHT)
	{
		a = rg_vec2(center_x - narrow_half, center_y - wide_half);
		b = rg_vec2(center_x + narrow_half, center_y);
		c = rg_vec2(center_x - narrow_half, center_y + wide_half);
	}
	else if (direction == RG_GUI_ARROW_UP)
	{
		a = rg_vec2(center_x - wide_half, center_y + narrow_half);
		b = rg_vec2(center_x, center_y - narrow_half);
		c = rg_vec2(center_x + wide_half, center_y + narrow_half);
	}
	else
	{
		a = rg_vec2(center_x - wide_half, center_y - narrow_half);
		b = rg_vec2(center_x + wide_half, center_y - narrow_half);
		c = rg_vec2(center_x, center_y + narrow_half);
	}

	rg_gui_push_triangle_to(ctx, list, a, b, c, color);
}

/**
 * @brief Draw a centered filled directional arrow into the active draw list
 */
RGINLINE void rg_gui_push_arrow(RgGuiContext* ctx, RgGuiRect bounds,
                                RgGuiArrowDirection direction, rg_vec4 color)
{
	rg_gui_push_arrow_to(ctx, rg_gui_draw_list_target(ctx), bounds, direction, color);
}

RGINLINE void rg_gui_push_image_to(RgGuiContext* ctx, RgGuiDrawList* list, RgGuiRect rect, RgGuiRect uv, rg_vec4 color, RgGuiTexture texture)
{
	rg_gui_push_image_material_to(ctx, list, rect, uv, color, texture, 0u);
}

RGINLINE void rg_gui_push_image_material_to(RgGuiContext* ctx, RgGuiDrawList* list,
                                            RgGuiRect rect, RgGuiRect uv, rg_vec4 color,
                                            RgGuiTexture texture, RgGuiImageMaterial material)
{
	if (texture == 0)
	{
		return;
	}

	if (!list || list->count >= list->capacity)
	{
		if (list)
		{
			rg_gui_diagnostics_add_draw_drop(ctx, 1u);
		}
		return;
	}

	RgGuiDrawCmd* cmd = &list->cmds[list->count++];
	cmd->type = RG_GUI_CMD_IMAGE;
	cmd->data.image.rect = rect;
	cmd->data.image.uv = uv;
	cmd->data.image.color = rg_gui_apply_disabled_color(ctx, color);
	cmd->data.image.texture = texture;
	cmd->data.image.material = material;
}

RGINLINE int rg_gui_icon_valid(const RgGuiIcon* icon)
{
	return icon && icon->texture != 0;
}

RGINLINE f32 rg_gui_icon_size(const RgGuiContext* ctx, RgGuiRect bounds)
{
	f32 size = ctx ? ctx->style.text_height : 0.0f;
	if (size > bounds.h)
	{
		size = bounds.h;
	}
	if (size < 0.0f)
	{
		size = 0.0f;
	}
	return size;
}

RGINLINE RgGuiRect rg_gui_icon_rect(const RgGuiContext* ctx, RgGuiRect bounds, f32 x)
{
	f32 size = rg_gui_icon_size(ctx, bounds);
	return rg_gui_make_rect(x, bounds.y + (bounds.h - size) * 0.5f, size, size);
}

RGINLINE void rg_gui_push_icon_to(RgGuiContext* ctx, RgGuiDrawList* list, const RgGuiIcon* icon,
                                  RgGuiRect rect, int dimmed)
{
	if (!rg_gui_icon_valid(icon))
	{
		return;
	}

	rg_vec4 tint = icon->tint;
	if (dimmed && ctx)
	{
		f32 k = ctx->style.disabled_alpha;
		tint.x *= k;
		tint.y *= k;
		tint.z *= k;
	}
	rg_gui_push_image_material_to(ctx, list, rect, icon->uv, tint,
	                              icon->texture, icon->material);
}

RGINLINE void rg_gui_push_icon(RgGuiContext* ctx, const RgGuiIcon* icon, RgGuiRect rect, int dimmed)
{
	rg_gui_push_icon_to(ctx, rg_gui_draw_list_target(ctx), icon, rect, dimmed);
}

RGINLINE void rg_gui_push_text_scaled_ex_to(RgGuiContext* ctx, RgGuiDrawList* list, const char* text,
                                            rg_vec2 pos, rg_vec4 color, f32 scale, int copy)
{
	if (!list || list->count >= list->capacity)
	{
		if (list)
		{
			rg_gui_diagnostics_add_draw_drop(ctx, 1u);
		}
		return;
	}

	RgGuiDrawCmd* cmd = &list->cmds[list->count++];
	cmd->type = RG_GUI_CMD_TEXT;
	cmd->data.text.pos = rg_gui_snap_text_pos(pos);
	cmd->data.text.color = rg_gui_apply_disabled_color(ctx, color);
	cmd->data.text.scale = rg_gui_text_base_scale(ctx) * (scale > 0.0f ? scale : 1.0f);
	if (copy)
	{
		cmd->data.text.text = text ? rg_gui_copy_text(ctx, text) : "";
	}
	else
	{
		cmd->data.text.text = text ? text : "";
	}
#if defined(RG_GUI_TEXT_CACHE_IDENTITY)
	cmd->data.text.cache_identity = !copy && text ? (uintptr_t)text : 0u;
#endif
}

RGINLINE void rg_gui_push_text_ex_to(RgGuiContext* ctx, RgGuiDrawList* list, const char* text,
                                     rg_vec2 pos, rg_vec4 color, int copy)
{
	rg_gui_push_text_scaled_ex_to(ctx, list, text, pos, color, 1.0f, copy);
}

RGINLINE void rg_gui_push_text_scaled_to(RgGuiContext* ctx, RgGuiDrawList* list, const char* text,
                                         rg_vec2 pos, rg_vec4 color, f32 scale)
{
	rg_gui_push_text_scaled_ex_to(ctx, list, text, pos, color, scale, 1);
}

/**
 * @brief Push scaled static text to a draw list
 * @details The text address and bytes must stay unchanged for the GUI context lifetime.
 */
RGINLINE void rg_gui_push_text_scaled_static_to(RgGuiContext* ctx, RgGuiDrawList* list, const char* text,
                                                rg_vec2 pos, rg_vec4 color, f32 scale)
{
	rg_gui_push_text_scaled_ex_to(ctx, list, text, pos, color, scale, 0);
}

RGINLINE void rg_gui_push_text_to(RgGuiContext* ctx, RgGuiDrawList* list, const char* text, rg_vec2 pos, rg_vec4 color)
{
	rg_gui_push_text_ex_to(ctx, list, text, pos, color, 1);
}

/**
 * @brief Push static text to a draw list
 * @details The text address and bytes must stay unchanged for the GUI context lifetime.
 */
RGINLINE void rg_gui_push_text_static_to(RgGuiContext* ctx, RgGuiDrawList* list, const char* text, rg_vec2 pos, rg_vec4 color)
{
	rg_gui_push_text_ex_to(ctx, list, text, pos, color, 0);
}

RGINLINE void rg_gui_push_rect_outline_to(RgGuiContext* ctx, RgGuiDrawList* list, RgGuiRect rect, rg_vec4 color, f32 thickness)
{
	if (thickness <= 0.0f)
	{
		return;
	}

	rg_gui_push_rect_to(ctx, list, rg_gui_make_rect(rect.x, rect.y, rect.w, thickness), color);
	rg_gui_push_rect_to(ctx, list, rg_gui_make_rect(rect.x, rect.y + rect.h - thickness, rect.w, thickness), color);
	rg_gui_push_rect_to(ctx, list, rg_gui_make_rect(rect.x, rect.y, thickness, rect.h), color);
	rg_gui_push_rect_to(ctx, list, rg_gui_make_rect(rect.x + rect.w - thickness, rect.y, thickness, rect.h), color);
}

RGINLINE void rg_gui_push_rect(RgGuiContext* ctx, RgGuiRect rect, rg_vec4 color)
{
	rg_gui_push_rect_to(ctx, rg_gui_draw_list_target(ctx), rect, color);
}

RGINLINE void rg_gui_push_triangle(RgGuiContext* ctx, rg_vec2 a, rg_vec2 b, rg_vec2 c, rg_vec4 color)
{
	rg_gui_push_triangle_to(ctx, rg_gui_draw_list_target(ctx), a, b, c, color);
}

RGINLINE void rg_gui_push_image(RgGuiContext* ctx, RgGuiRect rect, RgGuiRect uv, rg_vec4 color, RgGuiTexture texture)
{
	rg_gui_push_image_to(ctx, rg_gui_draw_list_target(ctx), rect, uv, color, texture);
}

RGINLINE void rg_gui_push_image_material(RgGuiContext* ctx, RgGuiRect rect, RgGuiRect uv,
                                         rg_vec4 color, RgGuiTexture texture,
                                         RgGuiImageMaterial material)
{
	rg_gui_push_image_material_to(ctx, rg_gui_draw_list_target(ctx), rect, uv,
	                              color, texture, material);
}

RGINLINE void rg_gui_push_text_ex(RgGuiContext* ctx, const char* text, rg_vec2 pos, rg_vec4 color, int copy)
{
	rg_gui_push_text_ex_to(ctx, rg_gui_draw_list_target(ctx), text, pos, color, copy);
}

RGINLINE void rg_gui_push_text_scaled_ex(RgGuiContext* ctx, const char* text, rg_vec2 pos,
                                         rg_vec4 color, f32 scale, int copy)
{
	rg_gui_push_text_scaled_ex_to(ctx, rg_gui_draw_list_target(ctx), text, pos, color, scale, copy);
}

RGINLINE void rg_gui_push_text_scaled(RgGuiContext* ctx, const char* text, rg_vec2 pos,
                                      rg_vec4 color, f32 scale)
{
	rg_gui_push_text_scaled_ex_to(ctx, rg_gui_draw_list_target(ctx), text, pos, color, scale, 1);
}

/**
 * @brief Push scaled static text to the current draw list
 * @details The text address and bytes must stay unchanged for the GUI context lifetime.
 */
RGINLINE void rg_gui_push_text_scaled_static(RgGuiContext* ctx, const char* text, rg_vec2 pos,
                                             rg_vec4 color, f32 scale)
{
	rg_gui_push_text_scaled_ex_to(ctx, rg_gui_draw_list_target(ctx), text, pos, color, scale, 0);
}

RGINLINE void rg_gui_push_text(RgGuiContext* ctx, const char* text, rg_vec2 pos, rg_vec4 color)
{
	rg_gui_push_text_ex_to(ctx, rg_gui_draw_list_target(ctx), text, pos, color, 1);
}

/**
 * @brief Push static text to the current draw list
 * @details The text address and bytes must stay unchanged for the GUI context lifetime.
 */
RGINLINE void rg_gui_push_text_static(RgGuiContext* ctx, const char* text, rg_vec2 pos, rg_vec4 color)
{
	rg_gui_push_text_ex_to(ctx, rg_gui_draw_list_target(ctx), text, pos, color, 0);
}

RGINLINE void rg_gui_push_close_glyph_to(RgGuiContext* ctx, RgGuiDrawList* list,
                                         RgGuiRect bounds)
{
	if (!ctx || !list || bounds.w <= 0.0f || bounds.h <= 0.0f)
	{
		return;
	}

	if (rg_gui_icon_valid(&ctx->close_icon))
	{
		f32 size = rg_gui_icon_size(ctx, bounds);
		RgGuiRect icon_rect = rg_gui_make_rect(bounds.x + (bounds.w - size) * 0.5f,
		                                       bounds.y + (bounds.h - size) * 0.5f,
		                                       size, size);
		// The authored tint is authoritative; push_image applies disabled tint once.
		rg_gui_push_icon_to(ctx, list, &ctx->close_icon, icon_rect, 0);
		return;
	}

	rg_vec2 pos = rg_vec2(bounds.x + (bounds.w - ctx->style.text_height * 0.5f) * 0.5f,
	                      bounds.y + (bounds.h - ctx->style.text_height) * 0.5f);
	rg_gui_push_text_static_to(ctx, list, "x", pos, ctx->style.color_text);
}

RGINLINE void rg_gui_push_close_glyph(RgGuiContext* ctx, RgGuiRect bounds)
{
	rg_gui_push_close_glyph_to(ctx, rg_gui_draw_list_target(ctx), bounds);
}

RGINLINE void rg_gui_push_rect_outline(RgGuiContext* ctx, RgGuiRect rect, rg_vec4 color, f32 thickness)
{
	rg_gui_push_rect_outline_to(ctx, rg_gui_draw_list_target(ctx), rect, color, thickness);
}

RGINLINE void rg_gui_push_line_to(RgGuiContext* ctx, RgGuiDrawList* list, rg_vec2 a, rg_vec2 b, f32 thickness, rg_vec4 color)
{
	f32 dx = b.x - a.x;
	f32 dy = b.y - a.y;
	f32 len_sq = dx * dx + dy * dy;
	if (len_sq <= 0.0f || thickness <= 0.0f)
	{
		return;
	}

	if (!list || (u64)list->count + 2u > (u64)list->capacity)
	{
		if (list)
		{
			rg_gui_diagnostics_add_draw_drop(ctx, 2u);
		}
		return;
	}

	f32 inv_len = 1.0f / rg_sqrtf(len_sq);
	f32 nx = -dy * inv_len;
	f32 ny = dx * inv_len;
	f32 half = thickness * 0.5f;
	f32 ox = nx * half;
	f32 oy = ny * half;

	rg_vec2 p0 = rg_vec2(a.x + ox, a.y + oy);
	rg_vec2 p1 = rg_vec2(b.x + ox, b.y + oy);
	rg_vec2 p2 = rg_vec2(b.x - ox, b.y - oy);
	rg_vec2 p3 = rg_vec2(a.x - ox, a.y - oy);

	rg_gui_push_triangle_to(ctx, list, p0, p1, p2, color);
	rg_gui_push_triangle_to(ctx, list, p2, p3, p0, color);
}

RGINLINE void rg_gui_push_line(RgGuiContext* ctx, rg_vec2 a, rg_vec2 b, f32 thickness, rg_vec4 color)
{
	rg_gui_push_line_to(ctx, rg_gui_draw_list_target(ctx), a, b, thickness, color);
}

RGINLINE int rg_gui_text_edit_has_selection(const RgGuiTextEditState* edit)
{
	return (edit->selection_start != edit->selection_end) ? 1 : 0;
}

RGINLINE void rg_gui_text_edit_clear_selection(RgGuiTextEditState* edit)
{
	edit->selection_anchor = edit->cursor;
	edit->selection_start = edit->cursor;
	edit->selection_end = edit->cursor;
}

RGINLINE int rg_gui_utf8_is_continuation(unsigned char byte)
{
	return (byte & 0xc0u) == 0x80u;
}

RGINLINE size_t rg_gui_utf8_boundary_before_or_at(const char* text, size_t length,
                                                  size_t position)
{
	if (!text)
	{
		return 0u;
	}
	if (position > length)
	{
		position = length;
	}
	while (position > 0u && position < length &&
	       rg_gui_utf8_is_continuation((unsigned char)text[position]))
	{
		position--;
	}
	return position;
}

RGINLINE size_t rg_gui_utf8_previous_boundary(const char* text, size_t length,
                                              size_t position)
{
	position = rg_gui_utf8_boundary_before_or_at(text, length, position);
	if (position == 0u)
	{
		return 0u;
	}
	position--;
	while (position > 0u && rg_gui_utf8_is_continuation((unsigned char)text[position]))
	{
		position--;
	}
	return position;
}

RGINLINE size_t rg_gui_utf8_next_boundary(const char* text, size_t length,
                                          size_t position)
{
	if (!text || position >= length)
	{
		return length;
	}
	position = rg_gui_utf8_boundary_before_or_at(text, length, position);
	position++;
	while (position < length && rg_gui_utf8_is_continuation((unsigned char)text[position]))
	{
		position++;
	}
	return position;
}

RGINLINE size_t rg_gui_utf8_codepoint_count(const char* text, size_t length)
{
	size_t count = 0u;
	for (size_t position = 0u; text && position < length;
	     position = rg_gui_utf8_next_boundary(text, length, position))
	{
		count++;
	}
	return count;
}

RGINLINE size_t rg_gui_utf8_advance_codepoints(const char* text, size_t length,
                                               size_t position, size_t count)
{
	position = rg_gui_utf8_boundary_before_or_at(text, length, position);
	while (count > 0u && position < length)
	{
		position = rg_gui_utf8_next_boundary(text, length, position);
		count--;
	}
	return position;
}

RGINLINE size_t rg_gui_utf8_midpoint_boundary(const char* text, size_t length,
                                              size_t low, size_t high)
{
	if (!text || low >= high)
	{
		return high;
	}

	size_t midpoint = low + (high - low) / 2u;
	midpoint = rg_gui_utf8_boundary_before_or_at(text, length, midpoint);
	if (midpoint <= low)
	{
		midpoint = rg_gui_utf8_next_boundary(text, length, low);
	}
	return midpoint < high ? midpoint : high;
}

RGINLINE RgGuiTextEditPreeditLayout rg_gui_text_edit_preedit_layout(
    const RgGuiTextEditState* edit, size_t text_length)
{
	RgGuiTextEditPreeditLayout layout;
	memset(&layout, 0, sizeof(layout));
	if (!edit)
	{
		return layout;
	}

	layout.host_start = edit->cursor < text_length ? edit->cursor : text_length;
	layout.host_end = layout.host_start;
	if (rg_gui_text_edit_has_selection(edit))
	{
		layout.host_start = edit->selection_start < text_length ? edit->selection_start : text_length;
		layout.host_end = edit->selection_end < text_length ? edit->selection_end : text_length;
		if (layout.host_end < layout.host_start)
		{
			size_t swap = layout.host_start;
			layout.host_start = layout.host_end;
			layout.host_end = swap;
		}
	}

	layout.preedit_start = layout.host_start;
	layout.preedit_end = layout.preedit_start + edit->preedit_length;
	size_t caret_relative = edit->preedit_length;
	if (edit->preedit_selection_start >= 0)
	{
		caret_relative = rg_gui_utf8_advance_codepoints(
		    edit->preedit, edit->preedit_length, 0u,
		    (size_t)edit->preedit_selection_start);
	}
	size_t selection_end_relative = caret_relative;
	if (edit->preedit_selection_length > 0)
	{
		selection_end_relative = rg_gui_utf8_advance_codepoints(
		    edit->preedit, edit->preedit_length, caret_relative,
		    (size_t)edit->preedit_selection_length);
	}
	layout.caret = layout.preedit_start + caret_relative;
	layout.selection_start = layout.caret;
	layout.selection_end = layout.preedit_start + selection_end_relative;
	return layout;
}

RGINLINE size_t rg_gui_text_edit_display_offset(RgGuiTextEditPreeditLayout layout,
                                                size_t host_offset)
{
	if (host_offset <= layout.host_start)
	{
		return host_offset;
	}
	if (host_offset < layout.host_end)
	{
		return layout.preedit_start;
	}
	return layout.preedit_end + (host_offset - layout.host_end);
}

RGINLINE void rg_gui_text_edit_clear_preedit(RgGuiTextEditState* edit)
{
	if (!edit)
	{
		return;
	}
	edit->preedit[0] = '\0';
	edit->preedit_length = 0u;
	edit->preedit_selection_start = 0;
	edit->preedit_selection_length = 0;
}

RGINLINE void rg_gui_text_edit_select_range(RgGuiTextEditState* edit, size_t start, size_t end)
{
	if (start > end)
	{
		size_t tmp = start;
		start = end;
		end = tmp;
	}

	if (start > edit->length)
	{
		start = edit->length;
	}
	if (end > edit->length)
	{
		end = edit->length;
	}
	start = rg_gui_utf8_boundary_before_or_at(edit->buffer, edit->length, start);
	end = rg_gui_utf8_boundary_before_or_at(edit->buffer, edit->length, end);

	edit->selection_anchor = start;
	edit->selection_start = start;
	edit->selection_end = end;
	edit->cursor = end;
}

RGINLINE int rg_gui_text_char_class(char ch)
{
	if (ch == ' ' || ch == '\t' || ch == '\n' || ch == '\r' || ch == '\v' || ch == '\f')
	{
		return 0;
	}
	if ((ch >= '0' && ch <= '9') ||
	    (ch >= 'A' && ch <= 'Z') ||
	    (ch >= 'a' && ch <= 'z') ||
	    ch == '_')
	{
		return 1;
	}
	return 2;
}

RGINLINE size_t rg_gui_text_word_left(const char* buffer, size_t length, size_t cursor)
{
	if (!buffer || length == 0u || cursor == 0u)
	{
		return 0u;
	}

	if (cursor > length)
	{
		cursor = length;
	}

	cursor = rg_gui_utf8_boundary_before_or_at(buffer, length, cursor);
	size_t i = rg_gui_utf8_previous_boundary(buffer, length, cursor);
	int cls = rg_gui_text_char_class(buffer[i]);
	while (i > 0u && cls == 0)
	{
		i = rg_gui_utf8_previous_boundary(buffer, length, i);
		cls = rg_gui_text_char_class(buffer[i]);
	}
	while (i > 0u)
	{
		size_t previous = rg_gui_utf8_previous_boundary(buffer, length, i);
		if (rg_gui_text_char_class(buffer[previous]) != cls)
		{
			break;
		}
		i = previous;
	}
	return i;
}

RGINLINE size_t rg_gui_text_word_right(const char* buffer, size_t length, size_t cursor)
{
	if (!buffer || length == 0u)
	{
		return 0u;
	}

	if (cursor > length)
	{
		cursor = length;
	}
	if (cursor >= length)
	{
		return length;
	}

	size_t i = rg_gui_utf8_boundary_before_or_at(buffer, length, cursor);
	int cls = rg_gui_text_char_class(buffer[i]);
	while (i < length && cls == 0)
	{
		i = rg_gui_utf8_next_boundary(buffer, length, i);
		if (i >= length)
		{
			return length;
		}
		cls = rg_gui_text_char_class(buffer[i]);
	}
	while (i < length && rg_gui_text_char_class(buffer[i]) == cls)
	{
		i = rg_gui_utf8_next_boundary(buffer, length, i);
	}
	return i;
}

RGINLINE void rg_gui_text_edit_word_range(const char* buffer, size_t length, size_t cursor, size_t* out_start, size_t* out_end)
{
	if (!out_start || !out_end)
	{
		return;
	}

	if (!buffer || length == 0u)
	{
		*out_start = 0u;
		*out_end = 0u;
		return;
	}

	if (cursor > length)
	{
		cursor = length;
	}

	cursor = rg_gui_utf8_boundary_before_or_at(buffer, length, cursor);
	size_t index = (cursor < length) ? cursor : rg_gui_utf8_previous_boundary(buffer, length, length);
	int cls = rg_gui_text_char_class(buffer[index]);
	if (cls == 0 && index > 0u)
	{
		size_t previous = rg_gui_utf8_previous_boundary(buffer, length, index);
		int prev_cls = rg_gui_text_char_class(buffer[previous]);
		if (prev_cls != 0)
		{
			index = previous;
			cls = prev_cls;
		}
	}

	size_t start = index;
	size_t end = rg_gui_utf8_next_boundary(buffer, length, index);
	while (start > 0u)
	{
		size_t previous = rg_gui_utf8_previous_boundary(buffer, length, start);
		if (rg_gui_text_char_class(buffer[previous]) != cls)
		{
			break;
		}
		start = previous;
	}
	while (end < length && rg_gui_text_char_class(buffer[end]) == cls)
	{
		end = rg_gui_utf8_next_boundary(buffer, length, end);
	}

	*out_start = start;
	*out_end = end;
}

RGINLINE void rg_gui_text_edit_line_range(const char* buffer, size_t length, size_t cursor, int include_newline,
                                          size_t* out_start, size_t* out_end)
{
	if (!out_start || !out_end)
	{
		return;
	}

	if (!buffer || length == 0u)
	{
		*out_start = 0u;
		*out_end = 0u;
		return;
	}

	if (cursor > length)
	{
		cursor = length;
	}

	size_t start = cursor;
	while (start > 0u && buffer[start - 1u] != '\n')
	{
		start--;
	}

	size_t end = cursor;
	while (end < length && buffer[end] != '\n')
	{
		end++;
	}
	if (include_newline && end < length && buffer[end] == '\n')
	{
		end++;
	}

	*out_start = start;
	*out_end = end;
}

RGINLINE int rg_gui_text_edit_update_click(RgGuiContext* ctx, RgGuiTextEditState* edit, RgGuiId id, rg_vec2 pos)
{
	if (!ctx || !edit)
	{
		return 0;
	}

	f32 double_click_time = RG_GUI_DOUBLE_CLICK_TIME;
	f32 double_click_dist = RG_GUI_DOUBLE_CLICK_DISTANCE;
	if (double_click_time <= 0.0f || double_click_dist <= 0.0f)
	{
		edit->click_count = 1u;
		edit->last_click_id = id;
		edit->last_click_time = ctx->time;
		edit->last_click_pos = pos;
		return 1;
	}

	f32 dt = ctx->time - edit->last_click_time;
	f32 dx = pos.x - edit->last_click_pos.x;
	f32 dy = pos.y - edit->last_click_pos.y;
	f32 dist_sq = dx * dx + dy * dy;

	if (edit->last_click_id == id &&
	    dt >= 0.0f && dt <= double_click_time &&
	    dist_sq <= double_click_dist * double_click_dist)
	{
		if (edit->click_count < 3u)
		{
			edit->click_count++;
		}
	}
	else
	{
		edit->click_count = 1u;
	}

	edit->last_click_id = id;
	edit->last_click_time = ctx->time;
	edit->last_click_pos = pos;
	return (int)edit->click_count;
}

RGINLINE void rg_gui_text_edit_set_cursor(RgGuiTextEditState* edit, size_t cursor, int selecting)
{
	if (cursor > edit->length)
	{
		cursor = edit->length;
	}
	cursor = rg_gui_utf8_boundary_before_or_at(edit->buffer, edit->length, cursor);

	edit->cursor = cursor;

	if (selecting)
	{
		size_t anchor = edit->selection_anchor;
		if (cursor < anchor)
		{
			edit->selection_start = cursor;
			edit->selection_end = anchor;
		}
		else
		{
			edit->selection_start = anchor;
			edit->selection_end = cursor;
		}
	}
	else
	{
		rg_gui_text_edit_clear_selection(edit);
	}
}

#if RG_GUI_TEXT_UNDO_STACK_SIZE > 0 && RG_GUI_TEXT_UNDO_BUFFER_SIZE > 0
RGINLINE void rg_gui_text_edit_undo_clear(RgGuiTextEditState* edit)
{
	edit->undo_count = 0u;
	edit->undo_index = 0u;
	edit->undo_buffer_used = 0u;
}

RGINLINE void rg_gui_text_edit_undo_drop_oldest(RgGuiTextEditState* edit)
{
	if (edit->undo_count == 0u)
	{
		return;
	}

	RgGuiTextEditUndoRecord oldest = edit->undo_stack[0];
	u32 drop_len = oldest.delete_len + oldest.insert_len;
	if (drop_len > 0u && edit->undo_buffer_used >= drop_len)
	{
		memmove(edit->undo_buffer,
		        edit->undo_buffer + drop_len,
		        edit->undo_buffer_used - drop_len);
		edit->undo_buffer_used -= drop_len;
	}

	for (u32 i = 1u; i < edit->undo_count; ++i)
	{
		edit->undo_stack[i - 1u] = edit->undo_stack[i];
		if (drop_len > 0u)
		{
			edit->undo_stack[i - 1u].text_offset -= drop_len;
		}
	}

	edit->undo_count--;
	if (edit->undo_index > 0u)
	{
		edit->undo_index--;
	}
}

RGINLINE int rg_gui_text_edit_undo_push(RgGuiTextEditState* edit, size_t pos,
                                        const char* deleted, size_t delete_len,
                                        const char* inserted, size_t insert_len,
                                        size_t cursor_before, size_t cursor_after)
{
	if ((delete_len == 0u && insert_len == 0u) || !edit)
	{
		return 0;
	}

	if (delete_len > UINT32_MAX || insert_len > UINT32_MAX)
	{
		rg_gui_text_edit_undo_clear(edit);
		return 0;
	}

	if (edit->undo_index < edit->undo_count)
	{
		edit->undo_count = edit->undo_index;
		if (edit->undo_count == 0u)
		{
			edit->undo_buffer_used = 0u;
		}
		else
		{
			RgGuiTextEditUndoRecord* last = &edit->undo_stack[edit->undo_count - 1u];
			edit->undo_buffer_used = last->text_offset + last->delete_len + last->insert_len;
		}
	}

	u32 text_len = (u32)(delete_len + insert_len);
	if (text_len > RG_GUI_TEXT_UNDO_BUFFER_SIZE)
	{
		rg_gui_text_edit_undo_clear(edit);
		return 0;
	}

	while (edit->undo_count >= RG_GUI_TEXT_UNDO_STACK_SIZE ||
	       edit->undo_buffer_used + text_len > RG_GUI_TEXT_UNDO_BUFFER_SIZE)
	{
		rg_gui_text_edit_undo_drop_oldest(edit);
		if (edit->undo_count == 0u && edit->undo_buffer_used == 0u)
		{
			break;
		}
	}

	if (edit->undo_count >= RG_GUI_TEXT_UNDO_STACK_SIZE ||
	    edit->undo_buffer_used + text_len > RG_GUI_TEXT_UNDO_BUFFER_SIZE)
	{
		rg_gui_text_edit_undo_clear(edit);
		return 0;
	}

	u32 text_offset = edit->undo_buffer_used;
	if (delete_len > 0u && deleted)
	{
		memcpy(edit->undo_buffer + text_offset, deleted, delete_len);
	}
	if (insert_len > 0u && inserted)
	{
		memcpy(edit->undo_buffer + text_offset + (u32)delete_len, inserted, insert_len);
	}
	edit->undo_buffer_used += text_len;

	RgGuiTextEditUndoRecord* record = &edit->undo_stack[edit->undo_count++];
	record->pos = pos;
	record->delete_len = (u32)delete_len;
	record->insert_len = (u32)insert_len;
	record->text_offset = text_offset;
	record->cursor_before = cursor_before;
	record->cursor_after = cursor_after;

	edit->undo_index = edit->undo_count;
	return 1;
}
#else
RGINLINE void rg_gui_text_edit_undo_clear(RgGuiTextEditState* edit)
{
	RG_GUI_UNUSED(edit);
}

RGINLINE int rg_gui_text_edit_undo_push(RgGuiTextEditState* edit, size_t pos,
                                        const char* deleted, size_t delete_len,
                                        const char* inserted, size_t insert_len,
                                        size_t cursor_before, size_t cursor_after)
{
	RG_GUI_UNUSED(edit);
	RG_GUI_UNUSED(pos);
	RG_GUI_UNUSED(deleted);
	RG_GUI_UNUSED(delete_len);
	RG_GUI_UNUSED(inserted);
	RG_GUI_UNUSED(insert_len);
	RG_GUI_UNUSED(cursor_before);
	RG_GUI_UNUSED(cursor_after);
	return 0;
}
#endif

RGINLINE int rg_gui_text_edit_replace_internal(RgGuiTextEditState* edit, size_t start, size_t end,
                                               const char* insert, size_t insert_len, int record_undo)
{
	if (!edit || !edit->buffer || edit->capacity == 0u)
	{
		return 0;
	}

	if (start > edit->length)
	{
		start = edit->length;
	}
	if (end > edit->length)
	{
		end = edit->length;
	}
	if (end < start)
	{
		end = start;
	}

	size_t delete_len = end - start;
	size_t max_len = (edit->capacity > 0u) ? edit->capacity - 1u : 0u;
	size_t base_len = edit->length - delete_len;
	size_t available = (base_len < max_len) ? (max_len - base_len) : 0u;
	if (insert_len > available)
	{
		insert_len = insert ? rg_gui_utf8_boundary_before_or_at(insert, insert_len, available) : 0u;
	}

	if (delete_len == 0u && insert_len == 0u)
	{
		return 0;
	}

	if (record_undo)
	{
		rg_gui_text_edit_undo_push(edit, start, edit->buffer + start, delete_len, insert, insert_len,
		                           edit->cursor, start + insert_len);
	}

	if (delete_len != insert_len)
	{
		memmove(edit->buffer + start + insert_len,
		        edit->buffer + end,
		        edit->length - end + 1u);
	}

	if (insert_len > 0u && insert)
	{
		memcpy(edit->buffer + start, insert, insert_len);
	}

	edit->length = edit->length - delete_len + insert_len;
	edit->cursor = start + insert_len;
	edit->buffer[edit->length] = '\0';
	rg_gui_text_edit_clear_selection(edit);
	edit->content_version++;
	return 1;
}

RGINLINE void rg_gui_text_prefix_cache_reset(RgGuiTextEditState* edit, const char* buffer);
RGINLINE f32 rg_gui_text_measure_prefix(const RgGuiContext* ctx, const char* text, size_t length);
RGINLINE int rg_gui_format_float(char* buffer, size_t buffer_size, f32 value, int decimals);
RGINLINE int rg_gui_format_double(char* buffer, size_t buffer_size, f64 value, int decimals);

#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
RGINLINE void rg_gui_text_value_undo_clear(RgGuiTextEditState* edit)
{
	if (!edit)
	{
		return;
	}

	edit->value_undo_active = 0;
	edit->value_undo_id = 0u;
	edit->value_undo_length = 0u;
	edit->value_undo_buffer[0] = '\0';
}

RGINLINE int rg_gui_text_value_undo_begin(RgGuiTextEditState* edit, RgGuiId id,
                                          const char* buffer, size_t length)
{
	if (!edit || !buffer || id == 0u)
	{
		rg_gui_text_value_undo_clear(edit);
		return 0;
	}

	if (length >= RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE)
	{
		rg_gui_text_value_undo_clear(edit);
		return 0;
	}

	edit->value_undo_active = 1;
	edit->value_undo_id = id;
	edit->value_undo_length = (u32)length;
	memcpy(edit->value_undo_buffer, buffer, length);
	edit->value_undo_buffer[length] = '\0';
	return 1;
}

#if RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE > 0
RGINLINE void rg_gui_text_value_undo_drop_oldest(RgGuiContext* ctx)
{
	if (!ctx || ctx->value_undo_count == 0u)
	{
		return;
	}

	if (ctx->value_undo_count > 1u)
	{
		memmove(ctx->value_undo_stack,
		        ctx->value_undo_stack + 1u,
		        sizeof(RgGuiTextValueUndoRecord) * (ctx->value_undo_count - 1u));
	}

	ctx->value_undo_count--;
	if (ctx->value_undo_index > 0u)
	{
		ctx->value_undo_index--;
	}
}

RGINLINE int rg_gui_text_value_undo_push_text(RgGuiContext* ctx, RgGuiId id, char* buffer, size_t capacity,
                                              const char* prev_text, size_t prev_len,
                                              const char* next_text, size_t next_len)
{
	if (!ctx || !buffer || !prev_text || !next_text || id == 0u)
	{
		return 0;
	}

	if (capacity == 0u)
	{
		return 0;
	}

	if (prev_len >= RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE ||
	    next_len >= RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE)
	{
		return 0;
	}

	if (prev_len == next_len &&
	    (prev_len == 0u || memcmp(prev_text, next_text, prev_len) == 0))
	{
		return 0;
	}

	if (ctx->value_undo_index < ctx->value_undo_count)
	{
		ctx->value_undo_count = ctx->value_undo_index;
	}

	while (ctx->value_undo_count >= RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE)
	{
		rg_gui_text_value_undo_drop_oldest(ctx);
	}

	u32 slot = ctx->value_undo_count++;
	RgGuiTextValueUndoRecord* record = &ctx->value_undo_stack[slot];
	record->id = id;
	record->type = RG_GUI_TEXT_VALUE_UNDO_TEXT;
	record->pad0 = 0u;
	record->pad1 = 0u;
	record->target = buffer;
	record->data.text.prev_length = (u32)prev_len;
	record->data.text.next_length = (u32)next_len;
	record->data.text.capacity = capacity;
	memcpy(record->data.text.prev_text, prev_text, prev_len);
	record->data.text.prev_text[prev_len] = '\0';
	memcpy(record->data.text.next_text, next_text, next_len);
	record->data.text.next_text[next_len] = '\0';

	ctx->value_undo_index = ctx->value_undo_count;
	return 1;
}

RGINLINE int rg_gui_text_value_undo_push_number(RgGuiContext* ctx, RgGuiId id, void* target,
                                                f64 prev_value, f64 next_value,
                                                RgGuiTextValueUndoType type)
{
	if (!ctx || !target || id == 0u)
	{
		return 0;
	}

	if (prev_value == next_value)
	{
		return 0;
	}

	if (type != RG_GUI_TEXT_VALUE_UNDO_INT &&
	    type != RG_GUI_TEXT_VALUE_UNDO_FLOAT &&
	    type != RG_GUI_TEXT_VALUE_UNDO_DOUBLE)
	{
		return 0;
	}

	if (ctx->value_undo_index < ctx->value_undo_count)
	{
		ctx->value_undo_count = ctx->value_undo_index;
	}

	while (ctx->value_undo_count >= RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE)
	{
		rg_gui_text_value_undo_drop_oldest(ctx);
	}

	u32 slot = ctx->value_undo_count++;
	RgGuiTextValueUndoRecord* record = &ctx->value_undo_stack[slot];
	record->id = id;
	record->type = type;
	record->pad0 = 0u;
	record->pad1 = 0u;
	record->target = target;
	record->data.number.prev_value = prev_value;
	record->data.number.next_value = next_value;

	ctx->value_undo_index = ctx->value_undo_count;
	return 1;
}
#else
RGINLINE int rg_gui_text_value_undo_push_text(RgGuiContext* ctx, RgGuiId id, char* buffer, size_t capacity,
                                              const char* prev_text, size_t prev_len,
                                              const char* next_text, size_t next_len)
{
	RG_GUI_UNUSED(ctx);
	RG_GUI_UNUSED(id);
	RG_GUI_UNUSED(buffer);
	RG_GUI_UNUSED(capacity);
	RG_GUI_UNUSED(prev_text);
	RG_GUI_UNUSED(prev_len);
	RG_GUI_UNUSED(next_text);
	RG_GUI_UNUSED(next_len);
	return 0;
}

RGINLINE int rg_gui_text_value_undo_push_number(RgGuiContext* ctx, RgGuiId id, void* target,
                                                f64 prev_value, f64 next_value,
                                                RgGuiTextValueUndoType type)
{
	RG_GUI_UNUSED(ctx);
	RG_GUI_UNUSED(id);
	RG_GUI_UNUSED(target);
	RG_GUI_UNUSED(prev_value);
	RG_GUI_UNUSED(next_value);
	RG_GUI_UNUSED(type);
	return 0;
}
#endif

RGINLINE int rg_gui_text_value_undo_commit(RgGuiContext* ctx, RgGuiTextEditState* edit, int finalize)
{
	if (!edit || !edit->value_undo_active || !edit->buffer || edit->value_undo_id == 0u)
	{
		if (finalize)
		{
			rg_gui_text_value_undo_clear(edit);
		}
		return 0;
	}

	if (ctx && edit->buffer == ctx->number_edit_state->buffer)
	{
		if (finalize)
		{
			rg_gui_text_value_undo_clear(edit);
		}
		return 0;
	}

	size_t length = edit->length;
	if (length >= RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE)
	{
		rg_gui_text_value_undo_clear(edit);
		return 0;
	}

	int pushed = 0;
	if (edit->value_undo_length != length ||
	    memcmp(edit->value_undo_buffer, edit->buffer, length) != 0)
	{
#if RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE > 0
		pushed = rg_gui_text_value_undo_push_text(ctx, edit->value_undo_id, edit->buffer, edit->capacity,
		                                          edit->value_undo_buffer, edit->value_undo_length,
		                                          edit->buffer, length);
#else
		RG_GUI_UNUSED(ctx);
#endif
		edit->value_undo_length = (u32)length;
		memcpy(edit->value_undo_buffer, edit->buffer, length);
		edit->value_undo_buffer[length] = '\0';
	}

	if (finalize)
	{
		rg_gui_text_value_undo_clear(edit);
	}

	return pushed;
}

RGINLINE void rg_gui_text_value_undo_commit_switch(RgGuiContext* ctx, RgGuiId next_id)
{
	if (!ctx)
	{
		return;
	}

	if (ctx->text_edit_state->value_undo_active &&
	    ctx->text_edit_state->id != 0u &&
	    ctx->text_edit_state->id != next_id &&
	    ctx->text_edit_state->buffer)
	{
		rg_gui_text_value_undo_commit(ctx, ctx->text_edit_state, 1);
	}
}

#if RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE > 0
RGINLINE int rg_gui_text_value_undo_apply_text(RgGuiContext* ctx, const RgGuiTextValueUndoRecord* record,
                                               const char* text, u32 length)
{
	if (!ctx || !record || record->type != RG_GUI_TEXT_VALUE_UNDO_TEXT)
	{
		return 0;
	}

	char* buffer = (char*)record->target;
	if (!buffer)
	{
		return 0;
	}

	size_t capacity = record->data.text.capacity;
	if (capacity == 0u || length >= capacity)
	{
		return 0;
	}

	if (length > 0u)
	{
		memcpy(buffer, text, length);
	}
	buffer[length] = '\0';

	if (ctx->text_edit_state->active && ctx->text_edit_state->buffer == buffer)
	{
		ctx->text_edit_state->length = length;
		if (ctx->text_edit_state->cursor > length)
		{
			ctx->text_edit_state->cursor = length;
		}
		if (ctx->text_edit_state->selection_anchor > length)
		{
			ctx->text_edit_state->selection_anchor = length;
		}
		if (ctx->text_edit_state->selection_start > length)
		{
			ctx->text_edit_state->selection_start = length;
		}
		if (ctx->text_edit_state->selection_end > length)
		{
			ctx->text_edit_state->selection_end = length;
		}
		ctx->text_edit_state->content_version++;
		rg_gui_text_prefix_cache_reset(ctx->text_edit_state, buffer);
		ctx->text_edit_state->dirty = 1;
		if (ctx->text_edit_state->value_undo_active && ctx->text_edit_state->value_undo_id == record->id)
		{
			ctx->text_edit_state->value_undo_length = length;
			if (length > 0u)
			{
				memcpy(ctx->text_edit_state->value_undo_buffer, buffer, length);
			}
			ctx->text_edit_state->value_undo_buffer[length] = '\0';
		}
		return 1;
	}

	return 0;
}

RGINLINE int rg_gui_text_value_undo_apply_number(RgGuiContext* ctx, const RgGuiTextValueUndoRecord* record,
                                                 f64 value)
{
	if (!ctx || !record)
	{
		return 0;
	}

	if (!record->target)
	{
		return 0;
	}

	if (record->type == RG_GUI_TEXT_VALUE_UNDO_INT)
	{
		*(int*)record->target = (int)value;
	}
	else if (record->type == RG_GUI_TEXT_VALUE_UNDO_FLOAT)
	{
		*(f32*)record->target = (f32)value;
	}
	else if (record->type == RG_GUI_TEXT_VALUE_UNDO_DOUBLE)
	{
		*(f64*)record->target = value;
	}
	else
	{
		return 0;
	}

	if (ctx->text_edit_state->active &&
	    ctx->text_edit_state->id == record->id &&
	    ctx->text_edit_state->buffer == ctx->number_edit_state->buffer)
	{
		if (record->type == RG_GUI_TEXT_VALUE_UNDO_INT)
		{
			rg_snprintf(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), "%d", (int)value);
		}
		else if (record->type == RG_GUI_TEXT_VALUE_UNDO_FLOAT)
		{
			rg_gui_format_float(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                    (f32)value, ctx->style.value_decimals);
		}
		else if (record->type == RG_GUI_TEXT_VALUE_UNDO_DOUBLE)
		{
			rg_gui_format_double(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                     value, ctx->style.value_decimals);
		}

		ctx->text_edit_state->length = strlen(ctx->number_edit_state->buffer);
		ctx->text_edit_state->cursor = ctx->text_edit_state->length;
		rg_gui_text_edit_clear_selection(ctx->text_edit_state);
		ctx->text_edit_state->content_version++;
		rg_gui_text_prefix_cache_reset(ctx->text_edit_state, ctx->text_edit_state->buffer);
		ctx->text_edit_state->dirty = 1;
		if (ctx->text_edit_state->value_undo_active && ctx->text_edit_state->value_undo_id == record->id)
		{
			size_t length = ctx->text_edit_state->length;
			if (length < RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE)
			{
				ctx->text_edit_state->value_undo_length = (u32)length;
				if (length > 0u)
				{
					memcpy(ctx->text_edit_state->value_undo_buffer, ctx->text_edit_state->buffer, length);
				}
				ctx->text_edit_state->value_undo_buffer[length] = '\0';
			}
			else
			{
				rg_gui_text_value_undo_clear(ctx->text_edit_state);
			}
		}
		return 1;
	}

	return 0;
}

RGINLINE int rg_gui_text_value_undo_apply(RgGuiContext* ctx, int redo)
{
	if (!ctx || ctx->value_undo_count == 0u)
	{
		return 0;
	}

	RgGuiTextValueUndoRecord* record = NULL;
	if (redo)
	{
		if (ctx->value_undo_index >= ctx->value_undo_count)
		{
			return 0;
		}
		record = &ctx->value_undo_stack[ctx->value_undo_index++];
	}
	else
	{
		if (ctx->value_undo_index == 0u)
		{
			return 0;
		}
		ctx->value_undo_index--;
		record = &ctx->value_undo_stack[ctx->value_undo_index];
	}

	if (!record)
	{
		return 0;
	}

	if (record->type == RG_GUI_TEXT_VALUE_UNDO_TEXT)
	{
		const char* text = redo ? record->data.text.next_text : record->data.text.prev_text;
		u32 length = redo ? record->data.text.next_length : record->data.text.prev_length;
		return rg_gui_text_value_undo_apply_text(ctx, record, text, length);
	}

	f64 value = redo ? record->data.number.next_value : record->data.number.prev_value;
	return rg_gui_text_value_undo_apply_number(ctx, record, value);
}
#else
RGINLINE int rg_gui_text_value_undo_apply(RgGuiContext* ctx, int redo)
{
	RG_GUI_UNUSED(ctx);
	RG_GUI_UNUSED(redo);
	return 0;
}
#endif
#endif

#if RG_GUI_TEXT_UNDO_STACK_SIZE > 0 && RG_GUI_TEXT_UNDO_BUFFER_SIZE > 0
RGINLINE int rg_gui_text_edit_undo(RgGuiTextEditState* edit)
{
	if (!edit || edit->undo_index == 0u)
	{
		return 0;
	}

	edit->undo_index--;
	RgGuiTextEditUndoRecord* record = &edit->undo_stack[edit->undo_index];
	const char* deleted = edit->undo_buffer + record->text_offset;
	int changed = rg_gui_text_edit_replace_internal(edit, record->pos, record->pos + record->insert_len,
	                                                deleted, record->delete_len, 0);
	if (changed)
	{
		edit->cursor = record->cursor_before;
		rg_gui_text_edit_clear_selection(edit);
		edit->dirty = 1;
	}
	return changed;
}

RGINLINE int rg_gui_text_edit_redo(RgGuiTextEditState* edit)
{
	if (!edit || edit->undo_index >= edit->undo_count)
	{
		return 0;
	}

	RgGuiTextEditUndoRecord* record = &edit->undo_stack[edit->undo_index];
	const char* deleted = edit->undo_buffer + record->text_offset;
	const char* inserted = deleted + record->delete_len;
	int changed = rg_gui_text_edit_replace_internal(edit, record->pos, record->pos + record->delete_len,
	                                                inserted, record->insert_len, 0);
	if (changed)
	{
		edit->cursor = record->cursor_after;
		rg_gui_text_edit_clear_selection(edit);
		edit->dirty = 1;
	}
	edit->undo_index++;
	return changed;
}
#else
RGINLINE int rg_gui_text_edit_undo(RgGuiTextEditState* edit)
{
	RG_GUI_UNUSED(edit);
	return 0;
}

RGINLINE int rg_gui_text_edit_redo(RgGuiTextEditState* edit)
{
	RG_GUI_UNUSED(edit);
	return 0;
}
#endif

RGINLINE int rg_gui_text_edit_delete_range(RgGuiTextEditState* edit, size_t start, size_t end)
{
	return rg_gui_text_edit_replace_internal(edit, start, end, NULL, 0u, 1);
}

RGINLINE void rg_gui_text_edit_normalize_clipboard_text(char* text, int allow_newline)
{
	if (!text) return;

	size_t read = 0u;
	size_t write = 0u;
	while (text[read] != '\0')
	{
		char ch = text[read++];
		if (ch == '\r')
		{
			if (text[read] == '\n') read++;
			ch = allow_newline ? '\n' : ' ';
		}
		else if (ch == '\n' && !allow_newline)
		{
			ch = ' ';
		}
		text[write++] = ch;
	}
	text[write] = '\0';
}

RGINLINE int rg_gui_text_edit_copy_selection(RgGuiTextEditState* edit)
{
	if (!rg_gui_text_edit_has_selection(edit))
	{
		return 0;
	}

	size_t start = edit->selection_start;
	size_t end = edit->selection_end;
	if (start >= end || end > edit->length)
	{
		return 0;
	}

	char saved = edit->buffer[end];
	edit->buffer[end] = '\0';
	int copied = SDL_SetClipboardText(edit->buffer + start) ? 1 : 0;
	edit->buffer[end] = saved;
	return copied;
}

RGINLINE void rg_gui_text_edit_select_all(RgGuiTextEditState* edit)
{
	edit->selection_anchor = 0u;
	rg_gui_text_edit_set_cursor(edit, edit->length, 1);
}

RGINLINE size_t rg_gui_text_edit_pick_cursor(const RgGuiContext* ctx, const char* buffer, RgGuiRect field)
{
	if (!ctx || !buffer)
	{
		return 0u;
	}

	size_t length = strlen(buffer);
	f32 local_x = ctx->mouse_pos.x - (field.x + ctx->style.padding);
	if (local_x <= 0.0f)
	{
		return 0u;
	}

	if (ctx->font)
	{
		size_t low = 0u;
		size_t high = length;
		int measure_ok = 1;
		while (low < high)
		{
			size_t mid = rg_gui_utf8_midpoint_boundary(buffer, length, low, high);
			if (mid >= high)
			{
				break;
			}
			f32 width = rg_gui_text_measure_prefix(ctx, buffer, mid);
			if (width < 0.0f)
			{
				measure_ok = 0;
				break;
			}

			if (width < local_x)
			{
				low = mid;
			}
			else
			{
				high = mid;
			}
		}

		if (measure_ok)
		{
			size_t cursor = high;
			if (cursor > length)
			{
				cursor = length;
			}
			cursor = rg_gui_utf8_boundary_before_or_at(buffer, length, cursor);

			if (cursor > 0u)
			{
				size_t previous = rg_gui_utf8_previous_boundary(buffer, length, cursor);
				f32 prev_width = rg_gui_text_measure_prefix(ctx, buffer, previous);
				f32 cur_width = rg_gui_text_measure_prefix(ctx, buffer, cursor);
				if (prev_width >= 0.0f && cur_width >= 0.0f)
				{
					if ((local_x - prev_width) < (cur_width - local_x))
					{
						cursor = previous;
					}
				}
			}

			return cursor;
		}
	}

	if (ctx->style.char_width > 0.0f)
	{
		size_t codepoint = (size_t)(local_x / ctx->style.char_width + 0.5f);
		return rg_gui_utf8_advance_codepoints(buffer, length, 0u, codepoint);
	}

	return 0u;
}

RGINLINE int rg_gui_text_edit_insert(RgGuiTextEditState* edit, const char* text)
{
	if (!text || !text[0])
	{
		return 0;
	}

	size_t start = edit->cursor;
	size_t end = edit->cursor;
	if (rg_gui_text_edit_has_selection(edit))
	{
		start = edit->selection_start;
		end = edit->selection_end;
	}

	size_t insert_len = strlen(text);
	return rg_gui_text_edit_replace_internal(edit, start, end, text, insert_len, 1);
}

RGINLINE int rg_gui_text_edit_insert_filtered(RgGuiContext* ctx, RgGuiTextEditState* edit, const char* text, u32 filter_flags)
{
	if (!text || !text[0])
	{
		return 0;
	}

	u32 filter_mask = RG_GUI_TEXT_INPUT_FILTER_NUMERIC | RG_GUI_TEXT_INPUT_FILTER_HEX;
	if ((filter_flags & filter_mask) == 0u)
	{
		return rg_gui_text_edit_insert(edit, text);
	}

	int use_hex = (filter_flags & RG_GUI_TEXT_INPUT_FILTER_HEX) ? 1 : 0;
	int use_numeric = (!use_hex && (filter_flags & RG_GUI_TEXT_INPUT_FILTER_NUMERIC)) ? 1 : 0;

	size_t sel_start = edit->cursor;
	size_t sel_end = edit->cursor;
	if (rg_gui_text_edit_has_selection(edit))
	{
		sel_start = edit->selection_start;
		sel_end = edit->selection_end;
	}

	int has_sign = 0;
	int has_dot = 0;
	if (use_numeric)
	{
		if (edit->length > 0u)
		{
			char first = edit->buffer[0];
			if ((first == '+' || first == '-') && !(sel_start == 0u && sel_end > 0u))
			{
				has_sign = 1;
			}
		}

		for (size_t i = 0; i < edit->length; ++i)
		{
			if (edit->buffer[i] == '.')
			{
				if (i < sel_start || i >= sel_end)
				{
					has_dot = 1;
					break;
				}
			}
		}
	}

	size_t insert_pos = sel_start;
	size_t text_len = strlen(text);

	if (text_len + 1u <= RG_GUI_TEXT_FILTER_BUFFER_SIZE)
	{
		char filtered[RG_GUI_TEXT_FILTER_BUFFER_SIZE];
		size_t out_len = 0u;
		for (size_t i = 0; i < text_len; ++i)
		{
			char ch = text[i];
			int allow = 0;
			if (use_hex)
			{
				allow = ((ch >= '0' && ch <= '9') ||
				         (ch >= 'a' && ch <= 'f') ||
				         (ch >= 'A' && ch <= 'F'));
			}
			else if (use_numeric)
			{
				if (ch >= '0' && ch <= '9')
				{
					allow = 1;
				}
				else if ((ch == '+' || ch == '-') && insert_pos == 0u && !has_sign)
				{
					allow = 1;
					has_sign = 1;
				}
				else if (ch == '.' && !has_dot)
				{
					allow = 1;
					has_dot = 1;
				}
			}

			if (allow && out_len + 1u < RG_GUI_TEXT_FILTER_BUFFER_SIZE)
			{
				filtered[out_len++] = ch;
				insert_pos++;
			}
		}
		filtered[out_len] = '\0';
		if (out_len == 0u)
		{
			return 0;
		}
		return rg_gui_text_edit_insert(edit, filtered);
	}

	if (ctx && ctx->text_buffer &&
	    text_len + 1u <= ctx->text_buffer_capacity - ctx->text_buffer_used)
	{
		char* scratch = ctx->text_buffer + ctx->text_buffer_used;
		size_t scratch_cap = ctx->text_buffer_capacity - ctx->text_buffer_used;
		size_t out_len = 0u;
		for (size_t i = 0; i < text_len; ++i)
		{
			char ch = text[i];
			int allow = 0;
			if (use_hex)
			{
				allow = ((ch >= '0' && ch <= '9') ||
				         (ch >= 'a' && ch <= 'f') ||
				         (ch >= 'A' && ch <= 'F'));
			}
			else if (use_numeric)
			{
				if (ch >= '0' && ch <= '9')
				{
					allow = 1;
				}
				else if ((ch == '+' || ch == '-') && insert_pos == 0u && !has_sign)
				{
					allow = 1;
					has_sign = 1;
				}
				else if (ch == '.' && !has_dot)
				{
					allow = 1;
					has_dot = 1;
				}
			}

			if (allow && out_len + 1u < scratch_cap)
			{
				scratch[out_len++] = ch;
				insert_pos++;
			}
		}
		scratch[out_len] = '\0';
		if (out_len == 0u)
		{
			return 0;
		}
		ctx->text_buffer_used += out_len + 1u;
		return rg_gui_text_edit_insert(edit, scratch);
	}

	char filtered[RG_GUI_TEXT_FILTER_BUFFER_SIZE];
	size_t out_len = 0u;
	int changed = 0;
	for (size_t i = 0; text[i] != '\0'; ++i)
	{
		char ch = text[i];
		int allow = 0;
		if (use_hex)
		{
			allow = ((ch >= '0' && ch <= '9') ||
			         (ch >= 'a' && ch <= 'f') ||
			         (ch >= 'A' && ch <= 'F'));
		}
		else if (use_numeric)
		{
			if (ch >= '0' && ch <= '9')
			{
				allow = 1;
			}
			else if ((ch == '+' || ch == '-') && insert_pos == 0u && !has_sign)
			{
				allow = 1;
				has_sign = 1;
			}
			else if (ch == '.' && !has_dot)
			{
				allow = 1;
				has_dot = 1;
			}
		}

		if (allow)
		{
			filtered[out_len++] = ch;
			insert_pos++;
			if (out_len + 1u >= RG_GUI_TEXT_FILTER_BUFFER_SIZE)
			{
				filtered[out_len] = '\0';
				changed |= rg_gui_text_edit_insert(edit, filtered);
				out_len = 0u;
				insert_pos = edit->cursor;
			}
		}
	}

	if (out_len > 0u)
	{
		filtered[out_len] = '\0';
		changed |= rg_gui_text_edit_insert(edit, filtered);
	}

	return changed;
}

RGINLINE int rg_gui_text_edit_delete_back(RgGuiTextEditState* edit)
{
	if (rg_gui_text_edit_has_selection(edit))
	{
		return rg_gui_text_edit_delete_range(edit, edit->selection_start, edit->selection_end);
	}

	if (edit->cursor == 0u)
	{
		return 0;
	}

	size_t start = rg_gui_utf8_previous_boundary(edit->buffer, edit->length, edit->cursor);
	return rg_gui_text_edit_replace_internal(edit, start, edit->cursor, NULL, 0u, 1);
}

RGINLINE int rg_gui_text_edit_delete_forward(RgGuiTextEditState* edit)
{
	if (rg_gui_text_edit_has_selection(edit))
	{
		return rg_gui_text_edit_delete_range(edit, edit->selection_start, edit->selection_end);
	}

	if (edit->cursor >= edit->length)
	{
		return 0;
	}

	size_t end = rg_gui_utf8_next_boundary(edit->buffer, edit->length, edit->cursor);
	return rg_gui_text_edit_replace_internal(edit, edit->cursor, end, NULL, 0u, 1);
}

RGINLINE int rg_gui_text_edit_process_snapshot_ex(RgGuiContext* ctx, RgGuiTextEditState* edit,
                                                  int* out_submit, int read_only,
                                                  int allow_newline, u32 filter_flags)
{
	int changed = 0;
	if (!ctx->input)
	{
		return 0;
	}

	size_t prev_cursor = edit->cursor;
	size_t prev_length = edit->length;

	int has_ctrl = rg_gui_input_has_ctrl(ctx->input);
	int has_shift = rg_gui_input_has_shift(ctx->input);

	if (has_ctrl)
	{
		if (!read_only && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_Z))
		{
			if (has_shift)
			{
				if (rg_gui_text_edit_redo(edit))
				{
					changed = 1;
				}
				else
				{
					changed |= rg_gui_text_value_undo_apply(ctx, 1);
				}
			}
			else
			{
				if (rg_gui_text_edit_undo(edit))
				{
					changed = 1;
				}
				else
				{
					changed |= rg_gui_text_value_undo_apply(ctx, 0);
				}
			}
		}

		if (!read_only && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_Y))
		{
			if (rg_gui_text_edit_redo(edit))
			{
				changed = 1;
			}
			else
			{
				changed |= rg_gui_text_value_undo_apply(ctx, 1);
			}
		}

		if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_A))
		{
			rg_gui_text_edit_select_all(edit);
			edit->dirty = 1;
		}

		if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_C))
		{
			rg_gui_text_edit_copy_selection(edit);
		}

		if (!read_only && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_X))
		{
			if (rg_gui_text_edit_copy_selection(edit))
			{
				changed |= rg_gui_text_edit_delete_range(edit, edit->selection_start, edit->selection_end);
			}
		}

		if (!read_only && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_V))
		{
			char* clip = SDL_GetClipboardText();
			if (clip && clip[0])
			{
				rg_gui_text_edit_normalize_clipboard_text(clip, allow_newline);
				changed |= rg_gui_text_edit_insert_filtered(ctx, edit, clip, filter_flags);
			}
			if (clip)
			{
				SDL_free(clip);
			}
		}
	}

	if (!read_only && ctx->input->has_text_input && !has_ctrl)
	{
		changed |= rg_gui_text_edit_insert_filtered(ctx, edit, ctx->input->text_input_buffer, filter_flags);
	}

	if (!read_only && has_ctrl && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_BACKSPACE))
	{
		if (rg_gui_text_edit_has_selection(edit))
		{
			changed |= rg_gui_text_edit_delete_range(edit, edit->selection_start, edit->selection_end);
		}
		else
		{
			size_t start = rg_gui_text_word_left(edit->buffer, edit->length, edit->cursor);
			if (start < edit->cursor)
			{
				changed |= rg_gui_text_edit_delete_range(edit, start, edit->cursor);
			}
		}
		edit->backspace_hold_time = 0.0f;
		edit->backspace_repeat_time = 0.0f;
	}
	else if (!read_only)
	{
		int backspace_pressed = rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_BACKSPACE);
		int backspace_down = rg_input_is_key_down(ctx->input, SDL_SCANCODE_BACKSPACE);
		if (backspace_pressed)
		{
			changed |= rg_gui_text_edit_delete_back(edit);
			edit->backspace_hold_time = 0.0f;
			edit->backspace_repeat_time = 0.0f;
		}

		if (backspace_down)
		{
			f32 delay = ctx->style.key_repeat_delay;
			f32 interval = ctx->style.key_repeat_interval;
			f32 fast_delay = ctx->style.key_repeat_fast_delay;
			f32 fast_interval = ctx->style.key_repeat_fast_interval;

			if (delay < 0.0f) delay = 0.0f;
			if (!(interval > 0.0f)) interval = 0.01f;
			if (fast_delay < delay) fast_delay = delay;
			if (!(fast_interval > 0.0f)) fast_interval = interval;

			edit->backspace_hold_time += ctx->delta_time;
			if (edit->backspace_hold_time >= delay)
			{
				f32 use_interval = (edit->backspace_hold_time >= fast_delay) ? fast_interval : interval;
				if (!(use_interval > 0.0f))
				{
					use_interval = interval;
				}

				edit->backspace_repeat_time += ctx->delta_time;
				int repeats = rg_gui_repeat_count_bounded(edit->backspace_repeat_time,
				                                          use_interval);
				if (repeats > 0)
				{
					if (repeats == RG_GUI_REPEAT_MAX_PER_FRAME)
						edit->backspace_repeat_time = 0.0f;
					else
						edit->backspace_repeat_time -= (f32)repeats * use_interval;
					for (int i = 0; i < repeats; ++i)
					{
						changed |= rg_gui_text_edit_delete_back(edit);
					}
				}
			}
		}
		else
		{
			edit->backspace_hold_time = 0.0f;
			edit->backspace_repeat_time = 0.0f;
		}
	}

	if (!read_only && has_ctrl && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_DELETE))
	{
		if (rg_gui_text_edit_has_selection(edit))
		{
			changed |= rg_gui_text_edit_delete_range(edit, edit->selection_start, edit->selection_end);
		}
		else
		{
			size_t end = rg_gui_text_word_right(edit->buffer, edit->length, edit->cursor);
			if (end > edit->cursor)
			{
				changed |= rg_gui_text_edit_delete_range(edit, edit->cursor, end);
			}
		}
	}
	else if (!read_only && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_DELETE))
	{
		changed |= rg_gui_text_edit_delete_forward(edit);
	}

	if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_LEFT))
	{
		size_t cursor = edit->cursor;
		if (has_ctrl)
		{
			cursor = rg_gui_text_word_left(edit->buffer, edit->length, cursor);
		}
		else if (cursor > 0u)
		{
			cursor = rg_gui_utf8_previous_boundary(edit->buffer, edit->length, cursor);
		}
		rg_gui_text_edit_set_cursor(edit, cursor, has_shift ? 1 : 0);
	}

	if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RIGHT))
	{
		size_t cursor = edit->cursor;
		if (has_ctrl)
		{
			cursor = rg_gui_text_word_right(edit->buffer, edit->length, cursor);
		}
		else if (cursor < edit->length)
		{
			cursor = rg_gui_utf8_next_boundary(edit->buffer, edit->length, cursor);
		}
		rg_gui_text_edit_set_cursor(edit, cursor, has_shift ? 1 : 0);
	}

	if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_HOME))
	{
		size_t cursor = 0u;
		if (allow_newline && !has_ctrl)
		{
			cursor = edit->cursor;
			if (cursor > edit->length)
			{
				cursor = edit->length;
			}
			while (cursor > 0u && edit->buffer[cursor - 1u] != '\n')
			{
				cursor--;
			}
		}
		rg_gui_text_edit_set_cursor(edit, cursor, has_shift ? 1 : 0);
	}

	if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_END))
	{
		size_t cursor = edit->length;
		if (allow_newline && !has_ctrl)
		{
			cursor = edit->cursor;
			if (cursor > edit->length)
			{
				cursor = edit->length;
			}
			while (cursor < edit->length && edit->buffer[cursor] != '\n')
			{
				cursor++;
			}
		}
		rg_gui_text_edit_set_cursor(edit, cursor, has_shift ? 1 : 0);
	}

	if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
	    rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER))
	{
		if (allow_newline && !read_only)
		{
			changed |= rg_gui_text_edit_insert(edit, "\n");
		}
		else if (out_submit)
		{
			*out_submit = 1;
		}
	}

	if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_ESCAPE))
	{
		ctx->focus_id = 0u;
		rg_gui_text_edit_clear_preedit(edit);
		edit->active = 0;
	}

	if (edit->cursor != prev_cursor || edit->length != prev_length || changed)
	{
		edit->dirty = 1;
	}

	return changed;
}

RGINLINE size_t rg_gui_input_event_count(const RgInputEventQueue* events)
{
	if (!events || !events->events)
	{
		return 0u;
	}
	return events->count < events->capacity ? events->count : events->capacity;
}

RGINLINE int rg_gui_input_event_matches_window(const RgGuiContext* ctx,
                                               const RgInputEvent* event)
{
	return ctx && event &&
	       (ctx->input_window_id == 0u || event->window_id == ctx->input_window_id);
}

RGINLINE void rg_gui_input_state_set_modifiers(RgInputState* input, SDL_Keymod modifiers)
{
	if ((modifiers & SDL_KMOD_LSHIFT) != 0) input->current_keyboard[SDL_SCANCODE_LSHIFT] = true;
	if ((modifiers & SDL_KMOD_RSHIFT) != 0) input->current_keyboard[SDL_SCANCODE_RSHIFT] = true;
	if ((modifiers & SDL_KMOD_LCTRL) != 0) input->current_keyboard[SDL_SCANCODE_LCTRL] = true;
	if ((modifiers & SDL_KMOD_RCTRL) != 0) input->current_keyboard[SDL_SCANCODE_RCTRL] = true;
	if ((modifiers & SDL_KMOD_LALT) != 0) input->current_keyboard[SDL_SCANCODE_LALT] = true;
	if ((modifiers & SDL_KMOD_RALT) != 0) input->current_keyboard[SDL_SCANCODE_RALT] = true;
	if ((modifiers & SDL_KMOD_LGUI) != 0) input->current_keyboard[SDL_SCANCODE_LGUI] = true;
	if ((modifiers & SDL_KMOD_RGUI) != 0) input->current_keyboard[SDL_SCANCODE_RGUI] = true;
}

typedef void (*RgGuiTextEditOrderedKeyFn)(RgGuiContext* ctx,
                                          RgGuiTextEditState* edit,
                                          const RgInputEvent* event,
                                          void* user);

RGINLINE int rg_gui_text_edit_process_ordered_with_key_ex(
    RgGuiContext* ctx, RgGuiTextEditState* edit, int* out_submit,
    int read_only, int allow_newline, u32 filter_flags,
    RgGuiTextEditOrderedKeyFn ordered_key, void* ordered_key_user)
{
	if (!ctx || !edit || !ctx->input_events ||
	    ctx->input_event_focus_id != edit->id ||
	    ctx->input_events_processed_id != 0u)
	{
		return 0;
	}

	ctx->input_events_processed_id = edit->id;
	int changed = 0;
	int accepts_keyboard = ctx->input_window_focused_at_frame_start;
	size_t event_count = rg_gui_input_event_count(ctx->input_events);
	for (size_t i = 0u; i < event_count; i++)
	{
		const RgInputEvent* event = &ctx->input_events->events[i];
		if (!rg_gui_input_event_matches_window(ctx, event))
		{
			continue;
		}

		if (event->kind == RG_INPUT_EVENT_WINDOW_FOCUS_LOST)
		{
			accepts_keyboard = 0;
			rg_gui_text_edit_clear_preedit(edit);
			continue;
		}
		if (event->kind == RG_INPUT_EVENT_WINDOW_FOCUS_GAINED)
		{
			accepts_keyboard = 1;
			continue;
		}
		if (!accepts_keyboard || !edit->active)
		{
			continue;
		}

		if (event->kind == RG_INPUT_EVENT_TEXT_INPUT)
		{
			rg_gui_text_edit_clear_preedit(edit);
			if (!read_only && event->data.text.text.data)
			{
				changed |= rg_gui_text_edit_insert_filtered(
				    ctx, edit, event->data.text.text.data, filter_flags);
			}
		}
		else if (event->kind == RG_INPUT_EVENT_TEXT_EDITING)
		{
			if (!read_only && event->data.text.text.data &&
			    event->data.text.text.length < RG_GUI_IME_PREEDIT_SIZE)
			{
				size_t length = event->data.text.text.length;
				memcpy(edit->preedit, event->data.text.text.data, length);
				edit->preedit[length] = '\0';
				edit->preedit_length = length;
				edit->preedit_selection_start = event->data.text.start;
				edit->preedit_selection_length = event->data.text.length;
			}
			else
			{
				rg_gui_text_edit_clear_preedit(edit);
			}
		}
		else if (event->kind == RG_INPUT_EVENT_KEY_DOWN)
		{
			if (ordered_key &&
			    (event->data.key.scancode == SDL_SCANCODE_UP ||
			     event->data.key.scancode == SDL_SCANCODE_DOWN))
			{
				ordered_key(ctx, edit, event, ordered_key_user);
				continue;
			}

			RgInputState event_input;
			memset(&event_input, 0, sizeof(event_input));
			rg_gui_input_state_set_modifiers(&event_input, event->modifiers);
			if ((u32)event->data.key.scancode < SDL_SCANCODE_COUNT)
			{
				event_input.current_keyboard[event->data.key.scancode] = true;
			}

			const RgInputState* saved_input = ctx->input;
			f32 saved_delta_time = ctx->delta_time;
			ctx->input = &event_input;
			ctx->delta_time = 0.0f;
			changed |= rg_gui_text_edit_process_snapshot_ex(
			    ctx, edit, out_submit, read_only, allow_newline, filter_flags);
			ctx->delta_time = saved_delta_time;
			ctx->input = saved_input;
		}
		else if (event->kind == RG_INPUT_EVENT_KEY_UP &&
		         event->data.key.scancode == SDL_SCANCODE_BACKSPACE)
		{
			edit->backspace_hold_time = 0.0f;
			edit->backspace_repeat_time = 0.0f;
		}
	}
	return changed;
}

RGINLINE int rg_gui_text_edit_process_ordered_ex(RgGuiContext* ctx,
                                                 RgGuiTextEditState* edit,
                                                 int* out_submit, int read_only,
                                                 int allow_newline,
                                                 u32 filter_flags)
{
	return rg_gui_text_edit_process_ordered_with_key_ex(
	    ctx, edit, out_submit, read_only, allow_newline, filter_flags,
	    NULL, NULL);
}

RGINLINE int rg_gui_text_edit_process_ex(RgGuiContext* ctx, RgGuiTextEditState* edit,
                                         int* out_submit, int read_only,
                                         int allow_newline, u32 filter_flags)
{
	if (ctx && ctx->input_events)
	{
		return rg_gui_text_edit_process_ordered_ex(
		    ctx, edit, out_submit, read_only, allow_newline, filter_flags);
	}
	return rg_gui_text_edit_process_snapshot_ex(
	    ctx, edit, out_submit, read_only, allow_newline, filter_flags);
}

RGINLINE int rg_gui_text_edit_process(RgGuiContext* ctx, RgGuiTextEditState* edit, int* out_submit, int read_only)
{
	return rg_gui_text_edit_process_ex(ctx, edit, out_submit, read_only, 0, 0u);
}

RGINLINE RgGuiRect rg_gui_text_area_inner_rect(const RgGuiContext* ctx, RgGuiRect rect)
{
	f32 border = ctx->style.border_thickness;
	f32 pad = ctx->style.padding;
	f32 scroll_w = ctx->style.scroll_bar_width;
	f32 inset = border + pad;

	RgGuiRect inner = rect;
	inner.x += inset;
	inner.y += inset;
	inner.w -= inset * 2.0f;
	inner.h -= inset * 2.0f;

	if (scroll_w > 0.0f)
	{
		inner.w -= scroll_w + ctx->style.inner_spacing;
	}

	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;
	return inner;
}

RGINLINE f32 rg_gui_text_area_measure_range(const RgGuiContext* ctx, const char* text, size_t length)
{
	if (!ctx || !text || length == 0u)
	{
		return 0.0f;
	}

	return rg_gui_text_measure_prefix(ctx, text, length);
}

RGINLINE size_t rg_gui_text_area_pick_column(const RgGuiContext* ctx, const char* line, size_t line_len, f32 local_x)
{
	if (!ctx || !line || line_len == 0u || local_x <= 0.0f)
	{
		return 0u;
	}

	if (ctx->font)
	{
		size_t low = 0u;
		size_t high = line_len;
		int measure_ok = 1;
		while (low < high)
		{
			size_t mid = rg_gui_utf8_midpoint_boundary(line, line_len, low, high);
			if (mid >= high)
			{
				break;
			}
			f32 width = rg_gui_text_area_measure_range(ctx, line, mid);
			if (width < 0.0f)
			{
				measure_ok = 0;
				break;
			}

			if (width < local_x)
			{
				low = mid;
			}
			else
			{
				high = mid;
			}
		}

		if (measure_ok)
		{
			size_t column = high;
			if (column > line_len)
			{
				column = line_len;
			}
			column = rg_gui_utf8_boundary_before_or_at(line, line_len, column);

			if (column > 0u)
			{
				size_t previous = rg_gui_utf8_previous_boundary(line, line_len, column);
				f32 prev_width = rg_gui_text_area_measure_range(ctx, line, previous);
				f32 cur_width = rg_gui_text_area_measure_range(ctx, line, column);
				if ((local_x - prev_width) < (cur_width - local_x))
				{
					column = previous;
				}
			}

			return column;
		}
	}

	if (ctx->style.char_width > 0.0f)
	{
		size_t codepoint = (size_t)(local_x / ctx->style.char_width + 0.5f);
		return rg_gui_utf8_advance_codepoints(line, line_len, 0u, codepoint);
	}

	return 0u;
}

RGINLINE size_t rg_gui_text_area_wrap_line_end(const RgGuiContext* ctx, const char* buffer,
                                               size_t start, size_t hard_end, f32 wrap_width)
{
	if (!ctx || !buffer || start >= hard_end)
	{
		return start;
	}

	if (wrap_width <= 0.0f)
	{
		return rg_gui_utf8_next_boundary(buffer, hard_end, start);
	}

	size_t best = start;
	size_t last_space = start;
	int has_space = 0;
	f32 scale = rg_gui_text_base_scale(ctx);
	f32 line_width = 0.0f;
	f32 max_width = 0.0f;
	const RgTextGlyph* previous = NULL;
	size_t offset = start;
	for (size_t i = start; i < hard_end; i = rg_gui_utf8_next_boundary(buffer, hard_end, i))
	{
		if (buffer[i] == ' ' || buffer[i] == '\t')
		{
			last_space = i;
			has_space = 1;
		}

		size_t candidate = rg_gui_utf8_next_boundary(buffer, hard_end, i);
		// Accumulate the same operations as rg_text_measure without remeasuring
		// each growing prefix. Decode up to the GUI boundary, including malformed
		// byte sequences that produce more than one replacement glyph.
		while (ctx->font && scale != 0.0f && offset < candidate)
		{
			u32 cp = rg_text_decode_utf8(buffer, candidate, &offset);
			if (cp == '\r' || cp == '\n')
			{
				if (cp == '\r' && offset < candidate && buffer[offset] == '\n') offset++;
				if (line_width > max_width) max_width = line_width;
				line_width = 0.0f;
				previous = NULL;
				continue;
			}
			const RgTextGlyph* glyph = rg_gui_text_find_glyph(ctx, cp);
			if (!glyph)
			{
				previous = NULL;
				continue;
			}
			if (previous)
			{
				line_width += (f32)rg_gui_text_find_kerning(
				    ctx, previous->codepoint, glyph->codepoint) * scale;
			}
			line_width += (f32)glyph->x_advance * scale;
			previous = glyph;
		}
		f32 width = line_width > max_width ? line_width : max_width;
		if (width <= wrap_width)
		{
			best = candidate;
			continue;
		}

		if (has_space && last_space > start)
		{
			return last_space + 1u;
		}
		if (best > start)
		{
			return best;
		}
		return candidate;
	}

	return hard_end;
}

RGINLINE u32 rg_gui_text_area_build_visual_lines(const RgGuiContext* ctx,
                                                 const char* buffer,
                                                 size_t length,
                                                 f32 wrap_width,
                                                 RgGuiTextAreaVisualLine* lines,
                                                 u32 capacity)
{
	if (!ctx || !buffer || !lines || capacity == 0u)
	{
		return 0u;
	}

	u32 count = 0u;
	if (length == 0u)
	{
		lines[count++] = (RgGuiTextAreaVisualLine){0u, 0u, 0};
		return count;
	}

	size_t index = 0u;
	while (index < length && count < capacity)
	{
		size_t hard_end = index;
		while (hard_end < length && buffer[hard_end] != '\n')
		{
			hard_end++;
		}

		if (hard_end == index)
		{
			lines[count++] = (RgGuiTextAreaVisualLine){index, index, 0};
		}
		else
		{
			size_t line_start = index;
			while (line_start < hard_end && count < capacity)
			{
				size_t line_end =
				    rg_gui_text_area_wrap_line_end(ctx, buffer, line_start, hard_end, wrap_width);
				if (line_end <= line_start)
				{
					line_end = line_start + 1u;
				}
				if (line_end > hard_end)
				{
					line_end = hard_end;
				}

				lines[count++] =
				    (RgGuiTextAreaVisualLine){line_start, line_end, line_end < hard_end ? 1 : 0};
				line_start = line_end;
			}
		}

		if (hard_end < length && buffer[hard_end] == '\n')
		{
			index = hard_end + 1u;
			if (index == length && count < capacity)
			{
				lines[count++] = (RgGuiTextAreaVisualLine){index, index, 0};
			}
		}
		else
		{
			break;
		}
	}

	if (count == 0u)
	{
		lines[count++] = (RgGuiTextAreaVisualLine){0u, 0u, 0};
	}
	return count;
}

// This cache lives only for one widget invocation. Content edits change the
// version, and IME display text uses a different buffer. No text storage is
// retained between frames, so external edits are observed on the next call.
typedef struct RgGuiTextAreaLayout
{
	RgGuiTextAreaVisualLine* lines;
	const char* buffer;
	const RgTextFont* font;
	size_t length;
	u32 content_version;
	u32 scale_bits;
	u32 wrap_width_bits;
	u32 count;
} RgGuiTextAreaLayout;

RGINLINE u32 rg_gui_text_area_ensure_layout(const RgGuiContext* ctx,
                                            RgGuiTextAreaLayout* layout,
                                            const char* buffer, size_t length,
                                            u32 content_version, f32 wrap_width)
{
	u32 scale_bits = rg_gui_text_scale_bits(ctx);
	u32 wrap_width_bits = 0u;
	memcpy(&wrap_width_bits, &wrap_width, sizeof(wrap_width_bits));
	if (layout->count == 0u || layout->buffer != buffer ||
	    layout->length != length || layout->content_version != content_version ||
	    layout->font != ctx->font || layout->scale_bits != scale_bits ||
	    layout->wrap_width_bits != wrap_width_bits)
	{
		layout->count = rg_gui_text_area_build_visual_lines(
		    ctx, buffer, length, wrap_width, layout->lines, RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
		layout->buffer = buffer;
		layout->font = ctx->font;
		layout->length = length;
		layout->content_version = content_version;
		layout->scale_bits = scale_bits;
		layout->wrap_width_bits = wrap_width_bits;
	}
	return layout->count;
}

RGINLINE u32 rg_gui_text_area_find_visual_line(const RgGuiTextAreaVisualLine* lines,
                                               u32 line_count,
                                               size_t cursor)
{
	if (!lines || line_count == 0u)
	{
		return 0u;
	}

	for (u32 i = 0u; i < line_count; i++)
	{
		const RgGuiTextAreaVisualLine* line = &lines[i];
		if (cursor < line->start)
		{
			return i > 0u ? i - 1u : 0u;
		}
		if (cursor >= line->start && cursor < line->end)
		{
			return i;
		}
		if (cursor == line->end)
		{
			if (line->soft_wrap_after && i + 1u < line_count)
			{
				continue;
			}
			return i;
		}
	}

	return line_count - 1u;
}

RGINLINE size_t rg_gui_text_area_pick_cursor_wrapped(const RgGuiContext* ctx,
                                                     const RgGuiTextAreaVisualLine* lines,
                                                     u32 line_count,
                                                     RgGuiRect inner,
                                                     f32 scroll_y,
                                                     f32 line_height)
{
	if (!ctx || !lines || line_count == 0u || line_height <= 0.0f)
	{
		return 0u;
	}

	f32 local_y = ctx->mouse_pos.y - inner.y + scroll_y;
	if (local_y < 0.0f)
	{
		local_y = 0.0f;
	}

	u32 target_line = rg_gui_nonnegative_f32_to_u32_bounded(local_y / line_height);
	if (target_line >= line_count)
	{
		target_line = line_count - 1u;
	}

	const RgGuiTextAreaVisualLine* line = &lines[target_line];
	f32 local_x = ctx->mouse_pos.x - (inner.x + ctx->style.padding);
	size_t line_len = line->end > line->start ? line->end - line->start : 0u;
	size_t column = rg_gui_text_area_pick_column(ctx, ctx->text_edit_state->buffer + line->start,
	                                             line_len, local_x);
	return line->start + column;
}

RGINLINE int rg_gui_text_area_move_cursor_wrapped(const RgGuiContext* ctx,
                                                  RgGuiTextEditState* edit,
                                                  const RgGuiTextAreaVisualLine* lines,
                                                  u32 line_count,
                                                  int direction,
                                                  int selecting)
{
	if (!ctx || !edit || !lines || line_count == 0u || direction == 0)
	{
		return 0;
	}

	if (edit->cursor > edit->length)
	{
		edit->cursor = edit->length;
	}

	u32 line_index = rg_gui_text_area_find_visual_line(lines, line_count, edit->cursor);
	if (direction < 0)
	{
		if (line_index == 0u)
		{
			return 0;
		}
		line_index--;
	}
	else
	{
		if (line_index + 1u >= line_count)
		{
			return 0;
		}
		line_index++;
	}

	u32 current_index = rg_gui_text_area_find_visual_line(lines, line_count, edit->cursor);
	const RgGuiTextAreaVisualLine* current = &lines[current_index];
	size_t current_column = 0u;
	if (edit->cursor > current->start)
	{
		current_column = edit->cursor - current->start;
		size_t max_column = current->end > current->start ? current->end - current->start : 0u;
		if (current_column > max_column)
		{
			current_column = max_column;
		}
	}
	f32 target_x = rg_gui_text_area_measure_range(ctx, edit->buffer + current->start, current_column);

	const RgGuiTextAreaVisualLine* target = &lines[line_index];
	size_t target_len = target->end > target->start ? target->end - target->start : 0u;
	size_t target_column =
	    rg_gui_text_area_pick_column(ctx, edit->buffer + target->start, target_len, target_x);
	rg_gui_text_edit_set_cursor(edit, target->start + target_column, selecting);
	return 1;
}

typedef struct RgGuiTextAreaOrderedKeyContext
{
	f32 wrap_width;
	RgGuiTextAreaLayout* layout;
} RgGuiTextAreaOrderedKeyContext;

static RG_NOINLINE void rg_gui_text_area_process_ordered_key(
    RgGuiContext* ctx, RgGuiTextEditState* edit,
    const RgInputEvent* event, void* user)
{
	RgGuiTextAreaOrderedKeyContext* move = (RgGuiTextAreaOrderedKeyContext*)user;
	if (!ctx || !edit || !event || !move || !edit->buffer)
	{
		return;
	}

	int direction = event->data.key.scancode == SDL_SCANCODE_UP ? -1 : 1;
	int selecting = (event->modifiers & (SDL_KMOD_LSHIFT | SDL_KMOD_RSHIFT)) != 0;
	RgGuiTextAreaVisualLine fallback_lines[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
	RgGuiTextAreaVisualLine* lines = move->layout ? move->layout->lines : fallback_lines;
	u32 line_count = move->layout
	    ? rg_gui_text_area_ensure_layout(ctx, move->layout, edit->buffer,
	                                     edit->length, edit->content_version, move->wrap_width)
	    : rg_gui_text_area_build_visual_lines(ctx, edit->buffer, edit->length,
	                                          move->wrap_width, lines, RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
	if (rg_gui_text_area_move_cursor_wrapped(
	        ctx, edit, lines, line_count, direction, selecting))
	{
		edit->dirty = 1;
		ctx->cursor_visible = 1;
		ctx->cursor_blink_timer = 0.0f;
	}
}

RGINLINE void rg_gui_text_edit_store_ordered_config(RgGuiTextEditState* edit,
                                                    int read_only,
                                                    int allow_newline,
                                                    u32 filter_flags,
                                                    int multiline,
                                                    f32 wrap_width)
{
	if (!edit)
	{
		return;
	}
	edit->ordered_config = (read_only ? 1u : 0u) |
	                       (allow_newline ? 2u : 0u) |
	                       (multiline ? 4u : 0u) |
	                       (filter_flags << 8u);
	edit->ordered_wrap_width = wrap_width;
}

static RG_NOINLINE int rg_gui_text_edit_process_pending_ordered_config_with_layout(
    RgGuiContext* ctx, int read_only, int allow_newline,
    u32 filter_flags, int multiline, f32 wrap_width, RgGuiTextAreaLayout* layout)
{
	if (!ctx || !ctx->input_events || !ctx->text_edit_state ||
	    ctx->input_events_processed_id != 0u ||
	    ctx->input_event_focus_id == 0u ||
	    !ctx->text_edit_state->active ||
	    ctx->text_edit_state->id != ctx->input_event_focus_id)
	{
		return 0;
	}

	RgGuiTextAreaOrderedKeyContext move = {wrap_width, layout};
	int submit = 0;
	RgGuiId id = ctx->text_edit_state->id;
	int changed = rg_gui_text_edit_process_ordered_with_key_ex(
	    ctx, ctx->text_edit_state, &submit, read_only, allow_newline,
	    filter_flags, multiline ? rg_gui_text_area_process_ordered_key : NULL,
	    multiline ? &move : NULL);
	if (changed)
	{
		ctx->input_events_changed_id = id;
	}
	if (submit)
	{
		ctx->input_events_submit_id = id;
	}
	return changed;
}

RGINLINE int rg_gui_text_edit_process_pending_ordered_config(
    RgGuiContext* ctx, int read_only, int allow_newline,
    u32 filter_flags, int multiline, f32 wrap_width)
{
	return rg_gui_text_edit_process_pending_ordered_config_with_layout(
	    ctx, read_only, allow_newline, filter_flags, multiline, wrap_width, NULL);
}

static RG_NOINLINE int rg_gui_text_edit_process_pending_ordered_stored(RgGuiContext* ctx)
{
	if (!ctx || !ctx->text_edit_state)
	{
		return 0;
	}
	RgGuiTextEditState* edit = ctx->text_edit_state;
	return rg_gui_text_edit_process_pending_ordered_config(
	    ctx, (edit->ordered_config & 1u) != 0u,
	    (edit->ordered_config & 2u) != 0u,
	    edit->ordered_config >> 8u,
	    (edit->ordered_config & 4u) != 0u,
	    edit->ordered_wrap_width);
}

RGINLINE int rg_gui_text_edit_consume_ordered_result(RgGuiContext* ctx,
                                                     RgGuiId id,
                                                     int* out_submit)
{
	if (!ctx)
	{
		return 0;
	}

	int changed = ctx->input_events_changed_id == id;
	if (changed)
	{
		ctx->input_events_changed_id = 0u;
	}
	if (ctx->input_events_submit_id == id)
	{
		if (out_submit)
		{
			*out_submit = 1;
		}
		ctx->input_events_submit_id = 0u;
	}
	return changed;
}

RGINLINE size_t rg_gui_text_area_pick_cursor(const RgGuiContext* ctx, const char* buffer, size_t length,
                                             RgGuiRect inner, f32 scroll_y, f32 line_height)
{
	if (!ctx || !buffer)
	{
		return 0u;
	}

	if (line_height <= 0.0f)
	{
		return 0u;
	}

	f32 local_y = ctx->mouse_pos.y - inner.y + scroll_y;
	if (local_y < 0.0f)
	{
		local_y = 0.0f;
	}

	u32 target_line = rg_gui_nonnegative_f32_to_u32_bounded(local_y / line_height);

	size_t index = 0u;
	u32 line = 0u;
	size_t line_start = 0u;
	while (index < length && line < target_line)
	{
		if (buffer[index] == '\n')
		{
			line++;
			line_start = index + 1u;
		}
		index++;
	}

	size_t line_end = length;
	for (size_t i = line_start; i < length; i++)
	{
		if (buffer[i] == '\n')
		{
			line_end = i;
			break;
		}
	}

	f32 local_x = ctx->mouse_pos.x - (inner.x + ctx->style.padding);
	size_t line_len = line_end - line_start;
	size_t column = rg_gui_text_area_pick_column(ctx, buffer + line_start, line_len, local_x);

	return line_start + column;
}

RGINLINE int rg_gui_text_area_move_cursor(const RgGuiContext* ctx, RgGuiTextEditState* edit, const char* buffer, int direction, int selecting)
{
	if (!ctx || !edit || !buffer || direction == 0)
	{
		return 0;
	}

	size_t length = edit->length;
	if (edit->cursor > length)
	{
		edit->cursor = length;
	}

	size_t cursor = edit->cursor;
	size_t line_start = cursor;
	while (line_start > 0u && buffer[line_start - 1u] != '\n')
	{
		line_start--;
	}

	size_t line_end = cursor;
	while (line_end < length && buffer[line_end] != '\n')
	{
		line_end++;
	}

	size_t column = cursor - line_start;

	if (direction < 0)
	{
		if (line_start == 0u)
		{
			return 0;
		}

		size_t prev_end = line_start - 1u;
		size_t prev_start = prev_end;
		while (prev_start > 0u && buffer[prev_start - 1u] != '\n')
		{
			prev_start--;
		}

		size_t prev_len = prev_end - prev_start;
		f32 target_x = rg_gui_text_area_measure_range(ctx, buffer + line_start, column);
		size_t target_column = rg_gui_text_area_pick_column(ctx, buffer + prev_start, prev_len, target_x);
		size_t new_cursor = prev_start + target_column;
		rg_gui_text_edit_set_cursor(edit, new_cursor, selecting);
		return 1;
	}

	if (line_end >= length)
	{
		return 0;
	}

	size_t next_start = line_end + 1u;
	if (next_start > length)
	{
		return 0;
	}

	size_t next_end = next_start;
	while (next_end < length && buffer[next_end] != '\n')
	{
		next_end++;
	}

	size_t next_len = next_end - next_start;
	f32 target_x = rg_gui_text_area_measure_range(ctx, buffer + line_start, column);
	size_t target_column = rg_gui_text_area_pick_column(ctx, buffer + next_start, next_len, target_x);
	size_t new_cursor = next_start + target_column;
	rg_gui_text_edit_set_cursor(edit, new_cursor, selecting);
	return 1;
}

RGINLINE int rg_gui_format_float(char* buffer, size_t buffer_size, f32 value, int decimals)
{
	if (buffer_size == 0u)
	{
		return 0;
	}
	return rg_snprintf(buffer, buffer_size, "%.*f", decimals, value);
}

RGINLINE int rg_gui_format_double(char* buffer, size_t buffer_size, f64 value, int decimals)
{
	if (buffer_size == 0u)
	{
		return 0;
	}
	return rg_snprintf(buffer, buffer_size, "%.*f", decimals, value);
}

RGINLINE f32 rg_gui_text_measure_prefix(const RgGuiContext* ctx, const char* text, size_t length)
{
	if (!ctx || !text || length == 0u)
	{
		return 0.0f;
	}

	f32 scale = rg_gui_text_base_scale(ctx);
	const RgGuiTextLookup* lookup = ctx->text_lookup;
	if (!lookup || lookup->font != ctx->font)
	{
		return rg_text_measure(ctx->font, text, length, scale).width;
	}
	if (!ctx->font || scale == 0.0f)
	{
		return 0.0f;
	}

	f32 max_width = 0.0f;
	f32 line_width = 0.0f;
	size_t offset = 0u;
	const RgTextGlyph* previous = NULL;
	while (offset < length)
	{
		u32 cp = rg_text_decode_utf8(text, length, &offset);
		if (cp == '\r' || cp == '\n')
		{
			if (cp == '\r' && offset < length && text[offset] == '\n') offset++;
			if (line_width > max_width) max_width = line_width;
			line_width = 0.0f;
			previous = NULL;
			continue;
		}
		const RgTextGlyph* glyph = cp < 128u ? lookup->ascii_glyphs[cp] : rg_text_find_glyph(ctx->font, cp);
		if (!glyph)
		{
			previous = NULL;
			continue;
		}
		if (previous)
		{
			u32 left = previous->codepoint;
			u32 right = glyph->codepoint;
			i32 kerning = left < 128u && right < 128u ? lookup->ascii_kerning[left * 128u + right] :
			    rg_text_find_kerning(ctx->font, left, right);
			line_width += (f32)kerning * scale;
		}
		line_width += (f32)glyph->x_advance * scale;
		previous = glyph;
	}
	if (line_width > max_width) max_width = line_width;
	return max_width;
}

RGINLINE f32 rg_gui_text_measure_range(RgGuiContext* ctx, const char* text, size_t start, size_t end)
{
	if (!ctx || !text || end <= start)
	{
		return 0.0f;
	}

	return rg_gui_text_measure_prefix(ctx, text + start, end - start);
}

RGINLINE void rg_gui_text_prefix_cache_reset(RgGuiTextEditState* edit, const char* buffer)
{
#if RG_GUI_TEXT_PREFIX_CACHE_SIZE > 0
	if (!edit)
	{
		return;
	}

	edit->prefix_cache_text = buffer;
	edit->prefix_cache_version = edit->content_version;
	edit->prefix_cache_scale_bits = 0u;
	edit->prefix_cache_next = 0u;
	edit->prefix_cache_count = 0u;
#else
	RG_GUI_UNUSED(edit);
	RG_GUI_UNUSED(buffer);
#endif
}

RGINLINE f32 rg_gui_text_measure_prefix_input(RgGuiContext* ctx, RgGuiTextEditState* edit, const char* text, size_t length)
{
	if (!ctx || !text || length == 0u)
	{
		return 0.0f;
	}

#if RG_GUI_TEXT_PREFIX_CACHE_SIZE == 0
	return rg_gui_text_measure_prefix(ctx, text, length);
#else
	if (!edit)
	{
		return rg_gui_text_measure_prefix(ctx, text, length);
	}

	u32 scale_bits = rg_gui_text_scale_bits(ctx);
	if (edit->prefix_cache_text != text || edit->prefix_cache_version != edit->content_version ||
	    edit->prefix_cache_scale_bits != scale_bits)
	{
		rg_gui_text_prefix_cache_reset(edit, text);
		edit->prefix_cache_scale_bits = scale_bits;
	}

	for (u32 i = 0u; i < edit->prefix_cache_count; i++)
	{
		if (edit->prefix_cache_index[i] == length)
		{
			return edit->prefix_cache_width[i];
		}
	}

	f32 width = rg_gui_text_measure_prefix(ctx, text, length);
	u32 slot = edit->prefix_cache_count;
	if (slot < RG_GUI_TEXT_PREFIX_CACHE_SIZE)
	{
		edit->prefix_cache_count++;
	}
	else
	{
		slot = edit->prefix_cache_next;
		edit->prefix_cache_next = (edit->prefix_cache_next + 1u) % RG_GUI_TEXT_PREFIX_CACHE_SIZE;
	}
	edit->prefix_cache_index[slot] = length;
	edit->prefix_cache_width[slot] = width;
	return width;
#endif
}

RGINLINE size_t rg_gui_text_len_cached(RgGuiContext* ctx, const char* text)
{
	if (!ctx || !text)
	{
		return 0u;
	}

	RgGuiTextLengthCacheEntry* cache = ctx->text_length_cache;
	u32 capacity = ctx->text_length_cache_capacity;
	if (!cache || capacity == 0u)
	{
		return strlen(text);
	}

	u32 hash = (u32)rg_hash_ptr(text);
	u32 mask = ctx->text_length_cache_mask;
	u32 index = mask ? (hash & mask) : (hash % capacity);

	for (u32 probe = 0u; probe < capacity; probe++)
	{
		RgGuiTextLengthCacheEntry* entry = &cache[index];
		if (!entry->text)
		{
			size_t length = strlen(text);
			entry->text = text;
			entry->length = length;
			return length;
		}
		if (entry->text == text)
		{
			return entry->length;
		}
		index = mask ? ((index + 1u) & mask) : ((index + 1u) % capacity);
	}

	size_t length = strlen(text);
	cache[index].text = text;
	cache[index].length = length;
	return length;
}

RGINLINE size_t rg_gui_text_len_label(RgGuiContext* ctx, const char* text, int copy)
{
	if (!text)
	{
		return 0u;
	}

	if (!copy)
	{
		return rg_gui_text_len_cached(ctx, text);
	}

	return strlen(text);
}

RGINLINE f32 rg_gui_text_measure_cached(RgGuiContext* ctx, const char* text, size_t length)
{
	if (!ctx || !text || length == 0u)
	{
		return 0.0f;
	}

	RgGuiTextMeasureCacheEntry* cache = ctx->text_measure_cache;
	u32 capacity = ctx->text_measure_cache_capacity;
	if (!cache || capacity == 0u)
	{
		return rg_gui_text_measure_prefix(ctx, text, length);
	}

	u64 key = (u64)(uintptr_t)text;
	key ^= (u64)length * 0x9e3779b97f4a7c15ull;
	u32 scale_bits = rg_gui_text_scale_bits(ctx);
	key ^= (u64)scale_bits * 0xd6e8feb86659fd93ull;
	u32 hash = (u32)rg_hash_u64(key);
	u32 mask = ctx->text_measure_cache_mask;
	u32 index = mask ? (hash & mask) : (hash % capacity);

	for (u32 probe = 0u; probe < capacity; probe++)
	{
		RgGuiTextMeasureCacheEntry* entry = &cache[index];
		if (!entry->text)
		{
			f32 width = rg_gui_text_measure_prefix(ctx, text, length);
			entry->text = text;
			entry->length = length;
			entry->scale_bits = scale_bits;
			entry->width = width;
			return width;
		}
		if (entry->text == text && entry->length == length && entry->scale_bits == scale_bits)
		{
			return entry->width;
		}
		index = mask ? ((index + 1u) & mask) : ((index + 1u) % capacity);
	}

	f32 width = rg_gui_text_measure_prefix(ctx, text, length);
	cache[index].text = text;
	cache[index].length = length;
	cache[index].scale_bits = scale_bits;
	cache[index].width = width;
	return width;
}

RGINLINE f32 rg_gui_text_measure_label(RgGuiContext* ctx, const char* text, size_t length, int copy)
{
	if (!text || length == 0u)
	{
		return 0.0f;
	}

	if (!copy)
	{
		return rg_gui_text_measure_cached(ctx, text, length);
	}

	return rg_gui_text_measure_prefix(ctx, text, length);
}

RGINLINE void rg_gui_menu_max_widths_scan(RgGuiContext* ctx, const char* const* items,
                                          const char* const* shortcuts, const RgGuiMenuItemFlags* flags,
                                          u32 count, f32* out_label, f32* out_shortcut,
                                          u32* out_flags_mask)
{
	f32 max_label = 0.0f;
	f32 max_shortcut = 0.0f;
	u32 flags_mask = 0u;

	if (count > 0u)
	{
		for (u32 i = 0u; i < count; i++)
		{
			if (items && items[i])
			{
				size_t len = rg_gui_text_len_label(ctx, items[i], RG_GUI_LABEL_COPY);
				if (len > 0u)
				{
					f32 w = rg_gui_text_measure_label(ctx, items[i], len, RG_GUI_LABEL_COPY);
					if (w > max_label)
					{
						max_label = w;
					}
				}
			}

			if (shortcuts && shortcuts[i])
			{
				size_t len = rg_gui_text_len_label(ctx, shortcuts[i], RG_GUI_LABEL_COPY);
				if (len > 0u)
				{
					f32 w = rg_gui_text_measure_label(ctx, shortcuts[i], len, RG_GUI_LABEL_COPY);
					if (w > max_shortcut)
					{
						max_shortcut = w;
					}
				}
			}

			if (flags)
			{
				flags_mask |= (u32)flags[i] &
				              (RG_GUI_MENU_ITEM_CHECKABLE | RG_GUI_MENU_ITEM_RADIO | RG_GUI_MENU_ITEM_SUBMENU);
			}
		}
	}

	if (out_label)
	{
		*out_label = max_label;
	}
	if (out_shortcut)
	{
		*out_shortcut = max_shortcut;
	}
	if (out_flags_mask)
	{
		*out_flags_mask = flags_mask;
	}
}

RGINLINE f32 rg_gui_menu_max_text_width_scan(RgGuiContext* ctx, const char* const* items, u32 count)
{
	f32 max_label = 0.0f;
	rg_gui_menu_max_widths_scan(ctx, items, NULL, NULL, count, &max_label, NULL, NULL);
	return max_label;
}

RGINLINE int rg_gui_menu_find_enabled(const RgGuiMenuItemFlags* flags, u32 count, int start, int dir)
{
	count = (u32)rg_gui_u32_count_to_int(count);
	if (!flags || count == 0u || start < 0 || start >= (int)count)
	{
		return start;
	}

	if (dir == 0)
	{
		dir = 1;
	}

	if ((flags[start] & RG_GUI_MENU_ITEM_DISABLED) == 0u)
	{
		return start;
	}

	int index = start;
	for (u32 step = 0u; step < count; step++)
	{
		index += dir;
		if (index < 0)
		{
			index = (int)count - 1;
		}
		else if (index >= (int)count)
		{
			index = 0;
		}

		if ((flags[index] & RG_GUI_MENU_ITEM_DISABLED) == 0u)
		{
			return index;
		}
	}

	return start;
}

RGINLINE void rg_gui_menu_max_widths(RgGuiContext* ctx, RgGuiId id, const char* const* items,
                                     const char* const* shortcuts, const RgGuiMenuItemFlags* flags,
                                     u32 count, f32* out_label, f32* out_shortcut,
                                     u32* out_flags_mask)
{
	f32 max_label = 0.0f;
	f32 max_shortcut = 0.0f;
	u32 flags_mask = 0u;

	if (!ctx || !items || count == 0u)
	{
		if (out_label)
		{
			*out_label = 0.0f;
		}
		if (out_shortcut)
		{
			*out_shortcut = 0.0f;
		}
		if (out_flags_mask)
		{
			*out_flags_mask = 0u;
		}
		return;
	}

	RgGuiMenuWidthCacheEntry* cache = ctx->menu_width_cache;
	u32 capacity = ctx->menu_width_cache_capacity;
	if (!cache || capacity == 0u)
	{
		rg_gui_menu_max_widths_scan(ctx, items, shortcuts, flags, count, &max_label, &max_shortcut, &flags_mask);
		if (out_label)
		{
			*out_label = max_label;
		}
		if (out_shortcut)
		{
			*out_shortcut = max_shortcut;
		}
		if (out_flags_mask)
		{
			*out_flags_mask = flags_mask;
		}
		return;
	}

	u64 key = (u64)id ^ (u64)(uintptr_t)items ^ ((u64)count << 32u);
	key ^= ((u64)(uintptr_t)shortcuts << 1u);
	key ^= ((u64)(uintptr_t)flags << 2u);
	u32 hash = (u32)rg_hash_u64(key);
	u32 mask = ctx->menu_width_cache_mask;
	u32 index = mask ? (hash & mask) : (hash % capacity);

	int found = 0;
	for (u32 probe = 0u; probe < capacity; probe++)
	{
		RgGuiMenuWidthCacheEntry* entry = &cache[index];
		if (!entry->items)
		{
			rg_gui_menu_max_widths_scan(ctx, items, shortcuts, flags, count, &max_label, &max_shortcut, &flags_mask);
			entry->id = id;
			entry->items = items;
			entry->shortcuts = shortcuts;
			entry->flags = flags;
			entry->count = count;
			entry->max_text_width = max_label;
			entry->max_shortcut_width = max_shortcut;
			entry->flags_mask = flags_mask;
			found = 1;
			break;
		}
		if (entry->id == id && entry->items == items && entry->shortcuts == shortcuts &&
		    entry->flags == flags && entry->count == count)
		{
			max_label = entry->max_text_width;
			max_shortcut = entry->max_shortcut_width;
			flags_mask = entry->flags_mask;
			found = 1;
			break;
		}
		index = mask ? ((index + 1u) & mask) : ((index + 1u) % capacity);
	}

	if (!found)
	{
		rg_gui_menu_max_widths_scan(ctx, items, shortcuts, flags, count, &max_label, &max_shortcut, &flags_mask);
		cache[index].id = id;
		cache[index].items = items;
		cache[index].shortcuts = shortcuts;
		cache[index].flags = flags;
		cache[index].count = count;
		cache[index].max_text_width = max_label;
		cache[index].max_shortcut_width = max_shortcut;
		cache[index].flags_mask = flags_mask;
	}

	if (out_label)
	{
		*out_label = max_label;
	}
	if (out_shortcut)
	{
		*out_shortcut = max_shortcut;
	}
	if (out_flags_mask)
	{
		*out_flags_mask = flags_mask;
	}
}

RGINLINE f32 rg_gui_menu_max_text_width(RgGuiContext* ctx, RgGuiId id, const char* const* items, u32 count)
{
	f32 max_label = 0.0f;
	rg_gui_menu_max_widths(ctx, id, items, NULL, NULL, count, &max_label, NULL, NULL);
	return max_label;
}

RGINLINE RgGuiTabScrollCacheEntry* rg_gui_tab_scroll_cache_find(RgGuiContext* ctx, RgGuiId id)
{
	if (!ctx || id == 0u || !ctx->tab_scroll_cache || ctx->tab_scroll_cache_capacity == 0u)
	{
		return NULL;
	}

	u32 capacity = ctx->tab_scroll_cache_capacity;
	u32 mask = ctx->tab_scroll_cache_mask;
	u32 hash = (u32)rg_hash_u64(id);
	u32 index = mask ? (hash & mask) : (hash % capacity);

	for (u32 probe = 0u; probe < capacity; probe++)
	{
		RgGuiTabScrollCacheEntry* entry = &ctx->tab_scroll_cache[index];
		if (entry->id == id)
		{
			return entry;
		}
		if (entry->id == 0u)
		{
			entry->id = id;
			entry->scroll = 0.0f;
			if (ctx->tab_scroll_cache_count < capacity)
			{
				ctx->tab_scroll_cache_count++;
			}
			return entry;
		}

		index = mask ? ((index + 1u) & mask) : ((index + 1u) % capacity);
	}

	u32 slot = ctx->tab_scroll_cache_next++ % capacity;
	RgGuiTabScrollCacheEntry* entry = &ctx->tab_scroll_cache[slot];
	entry->id = id;
	entry->scroll = 0.0f;
	return entry;
}

RGINLINE size_t rg_gui_text_end_for_width(RgGuiContext* ctx, const char* text, size_t length, size_t start, f32 width)
{
	if (!ctx || !text || start >= length || width <= 0.0f)
	{
		return start;
	}

	if (ctx->font)
	{
		start = rg_gui_utf8_boundary_before_or_at(text, length, start);
		f32 base = rg_gui_text_measure_prefix(ctx, text, start);
		f32 full_width = rg_gui_text_measure_prefix(ctx, text, length) - base;
		if (full_width <= width)
		{
			return length;
		}

		size_t low = start;
		size_t high = length;
		while (low < high)
		{
			size_t mid = rg_gui_utf8_midpoint_boundary(text, length, low, high);
			if (mid >= high)
			{
				break;
			}
			f32 w = rg_gui_text_measure_prefix(ctx, text, mid) - base;
			if (w <= width)
			{
				low = mid;
			}
			else
			{
				high = mid;
			}
		}
		return low;
	}

	if (ctx->style.char_width <= 0.0f)
	{
		return start;
	}

	size_t max_chars = (size_t)(width / ctx->style.char_width);
	return rg_gui_utf8_advance_codepoints(text, length, start, max_chars);
}

RGINLINE size_t rg_gui_text_start_for_cursor_width(RgGuiContext* ctx,
                                                   const char* text, size_t length,
                                                   size_t start, size_t cursor,
                                                   f32 width)
{
	if (!ctx || !text)
	{
		return 0u;
	}

	start = rg_gui_utf8_boundary_before_or_at(text, length, start);
	cursor = rg_gui_utf8_boundary_before_or_at(text, length, cursor);
	if (cursor < start || width <= 0.0f)
	{
		return cursor;
	}

	f32 cursor_width = rg_gui_text_measure_prefix(ctx, text, cursor);
	if (cursor_width - rg_gui_text_measure_prefix(ctx, text, start) <= width)
	{
		return start;
	}

	size_t low = 0u;
	size_t high = cursor;
	while (low < high)
	{
		size_t mid = rg_gui_utf8_midpoint_boundary(text, length, low, high);
		if (mid >= high)
		{
			break;
		}
		f32 visible_width = cursor_width - rg_gui_text_measure_prefix(ctx, text, mid);
		if (visible_width > width)
		{
			low = mid;
		}
		else
		{
			high = mid;
		}
	}
	return high;
}

RGINLINE void rg_gui_text_edit_update_view(RgGuiContext* ctx, RgGuiTextEditState* edit, const char* buffer, RgGuiRect field)
{
	if (!ctx || !edit || !buffer)
	{
		return;
	}

	size_t length = edit->length;
	f32 available = field.w - ctx->style.padding * 2.0f;
	if (available < 0.0f)
	{
		available = 0.0f;
	}

	if (length == 0u)
	{
		edit->view_start = 0u;
		return;
	}

	if (edit->view_start > length)
	{
		edit->view_start = length;
	}

	f32 full_width = rg_gui_text_measure_prefix_input(ctx, edit, buffer, length);
	if (full_width <= available)
	{
		edit->view_start = 0u;
		return;
	}

	size_t cursor = edit->cursor;
	if (cursor < edit->view_start)
	{
		edit->view_start = cursor;
		return;
	}

	f32 start_width = rg_gui_text_measure_prefix_input(ctx, edit, buffer, edit->view_start);
	f32 cursor_width = rg_gui_text_measure_prefix_input(ctx, edit, buffer, cursor);
	f32 rel = cursor_width - start_width;
	if (rel > available)
	{
		if (ctx->font)
		{
			size_t low = 0u;
			size_t high = cursor;
			while (low < high)
			{
				size_t mid = rg_gui_utf8_midpoint_boundary(buffer, length, low, high);
				if (mid >= high)
				{
					break;
				}
				f32 w = cursor_width - rg_gui_text_measure_prefix_input(ctx, edit, buffer, mid);
				if (w > available)
				{
					low = mid;
				}
				else
				{
					high = mid;
				}
			}
			edit->view_start = high;
		}
		else if (ctx->style.char_width > 0.0f)
		{
			size_t max_chars = (size_t)(available / ctx->style.char_width);
			size_t view_start = cursor;
			while (max_chars > 0u && view_start > 0u)
			{
				view_start = rg_gui_utf8_previous_boundary(buffer, length, view_start);
				max_chars--;
			}
			edit->view_start = view_start;
		}
	}
}

RGINLINE void rg_gui_copy_value_buffer(char* dst, size_t dst_size, const char* src)
{
	if (dst_size == 0u)
	{
		return;
	}

	size_t len = strlen(src);
	if (len >= dst_size)
	{
		len = dst_size - 1u;
	}

	memcpy(dst, src, len);
	dst[len] = '\0';
}

RGINLINE const char* rg_gui_cache_value_text(RgGuiContext* ctx, RgGuiId id, f64 value, int decimals)
{
	if (!ctx->value_cache || ctx->value_cache_capacity == 0u)
	{
		return NULL;
	}

	u32 capacity = ctx->value_cache_capacity;
	u32 mask = ctx->value_cache_mask;
	u32 hash = (u32)rg_hash_u64(id);
	u32 index = mask ? (hash & mask) : (hash % capacity);

	for (u32 probe = 0u; probe < capacity; probe++)
	{
		RgGuiValueCacheEntry* entry = &ctx->value_cache[index];
		if (!entry->valid)
		{
			entry->id = id;
			entry->value = value;
			entry->decimals = decimals;
			entry->valid = 1;
			rg_gui_format_double(entry->text, sizeof(entry->text), value, decimals);
			if (ctx->value_cache_count < capacity)
			{
				ctx->value_cache_count++;
			}
			return entry->text;
		}

		if (entry->id == id)
		{
			if (entry->value != value || entry->decimals != decimals)
			{
				rg_gui_format_double(entry->text, sizeof(entry->text), value, decimals);
				entry->value = value;
				entry->decimals = decimals;
			}
			return entry->text;
		}

		index = mask ? ((index + 1u) & mask) : ((index + 1u) % capacity);
	}

	u32 slot = ctx->value_cache_next++ % capacity;
	RgGuiValueCacheEntry* entry = &ctx->value_cache[slot];
	entry->id = id;
	entry->value = value;
	entry->decimals = decimals;
	entry->valid = 1;
	rg_gui_format_double(entry->text, sizeof(entry->text), value, decimals);
	return entry->text;
}

RGINLINE u32 rg_gui_next_pow2_u32(u32 value)
{
	if (value == 0u)
	{
		return 0u;
	}

	value--;
	value |= value >> 1u;
	value |= value >> 2u;
	value |= value >> 4u;
	value |= value >> 8u;
	value |= value >> 16u;
	value++;
	return value;
}

RGINLINE int rg_gui_init_desc_resolve(const RgGuiInitDesc* desc, RgGuiInitDesc* out)
{
	if (!desc || !out || !desc->font || !desc->font->glyphs || desc->font->glyph_count == 0u ||
	    desc->font->metrics.line_height <= 0 || desc->font->metrics.atlas_width == 0u ||
	    desc->font->metrics.atlas_height == 0u)
	{
		return 0;
	}

	memset(out, 0, sizeof(*out));
	out->font = desc->font;
	out->text_lookup = desc->text_lookup;
	out->max_draw_cmds = desc->max_draw_cmds ? desc->max_draw_cmds : RG_GUI_MAX_DRAW_CMDS;
	out->text_buffer_size = desc->text_buffer_size ? desc->text_buffer_size : RG_GUI_TEXT_BUFFER_SIZE;
	out->ime_callback = desc->ime_callback;
	out->ime_callback_user = desc->ime_callback_user;
	out->value_cache_size = desc->value_cache_size ? desc->value_cache_size : RG_GUI_VALUE_CACHE_SIZE;
	out->text_measure_cache_size = desc->text_measure_cache_size ? desc->text_measure_cache_size : RG_GUI_TEXT_MEASURE_CACHE_SIZE;
	out->text_length_cache_size = desc->text_length_cache_size ? desc->text_length_cache_size : RG_GUI_TEXT_LENGTH_CACHE_SIZE;
	out->menu_width_cache_size = desc->menu_width_cache_size ? desc->menu_width_cache_size : RG_GUI_MENU_WIDTH_CACHE_SIZE;
	out->tab_scroll_cache_size = desc->tab_scroll_cache_size ? desc->tab_scroll_cache_size : RG_GUI_TAB_SCROLL_CACHE_SIZE;
	return 1;
}

RGINLINE int rg_gui_memory_add_array(size_t* total, size_t element_size, size_t count, size_t alignment)
{
	if (!total || element_size == 0u || alignment == 0u || (alignment & (alignment - 1u)) != 0u)
	{
		return 0;
	}
	if (count == 0u)
	{
		return 1;
	}
	if (count > SIZE_MAX / element_size)
	{
		return 0;
	}

	size_t bytes = element_size * count;
	size_t padding = alignment - 1u;
	if (*total > SIZE_MAX - padding || *total + padding > SIZE_MAX - bytes)
	{
		return 0;
	}
	*total += padding + bytes;
	return 1;
}

RGINLINE size_t rg_gui_memory_required(const RgGuiInitDesc* desc)
{
	RgGuiInitDesc init;
	if (!rg_gui_init_desc_resolve(desc, &init))
	{
		return 0u;
	}

	u32 value_cache_capacity = rg_gui_next_pow2_u32(init.value_cache_size);
	u32 measure_cache_capacity = rg_gui_next_pow2_u32(init.text_measure_cache_size);
	u32 length_cache_capacity = rg_gui_next_pow2_u32(init.text_length_cache_size);
	u32 menu_cache_capacity = rg_gui_next_pow2_u32(init.menu_width_cache_size);
	u32 tab_scroll_capacity = rg_gui_next_pow2_u32(init.tab_scroll_cache_size);
	if ((init.value_cache_size && !value_cache_capacity) ||
	    (init.text_measure_cache_size && !measure_cache_capacity) ||
	    (init.text_length_cache_size && !length_cache_capacity) ||
	    (init.menu_width_cache_size && !menu_cache_capacity) ||
	    (init.tab_scroll_cache_size && !tab_scroll_capacity))
	{
		return 0u;
	}

	size_t total = 0u;
#define RG_GUI_MEMORY_ADD(type, count)                                                                    \
	do {                                                                                                  \
		if (!rg_gui_memory_add_array(&total, sizeof(type), (size_t)(count), RG_ALIGNOF(type))) return 0u; \
	} while (0)
	RG_GUI_MEMORY_ADD(RgGuiDrawCmd, init.max_draw_cmds);
	RG_GUI_MEMORY_ADD(RgGuiDrawCmd, init.max_draw_cmds);
	RG_GUI_MEMORY_ADD(char, init.text_buffer_size);
	RG_GUI_MEMORY_ADD(RgGuiValueCacheEntry, value_cache_capacity);
	RG_GUI_MEMORY_ADD(RgGuiTextMeasureCacheEntry, measure_cache_capacity);
	RG_GUI_MEMORY_ADD(RgGuiTextLengthCacheEntry, length_cache_capacity);
	RG_GUI_MEMORY_ADD(RgGuiMenuWidthCacheEntry, menu_cache_capacity);
	RG_GUI_MEMORY_ADD(RgGuiTabScrollCacheEntry, tab_scroll_capacity);
	RG_GUI_MEMORY_ADD(RgGuiDockSpaceState*, RG_GUI_MAX_DOCKSPACES);
	RG_GUI_MEMORY_ADD(RgGuiDockNode, (size_t)RG_GUI_MAX_DOCK_NODES + 1u);
	RG_GUI_MEMORY_ADD(RgGuiDockTab, (size_t)RG_GUI_MAX_DOCK_TABS + 1u);
#if defined(RG_GUI_ENABLE_VIEWPORTS)
	RG_GUI_MEMORY_ADD(RgGuiViewport, RG_GUI_MAX_VIEWPORTS);
	for (u32 i = 0u; i < RG_GUI_MAX_VIEWPORTS; i++)
	{
		RG_GUI_MEMORY_ADD(RgGuiDrawCmd, init.max_draw_cmds);
		RG_GUI_MEMORY_ADD(RgGuiDrawCmd, init.max_draw_cmds);
	}
#endif
#undef RG_GUI_MEMORY_ADD
	return total;
}

RGINLINE int rg_gui_parse_int_saturated(const char* text, int* out_value)
{
	if (!text || !out_value)
	{
		return 0;
	}

	char* end_ptr = NULL;
	long parsed = strtol(text, &end_ptr, 10);
	if (end_ptr == text)
	{
		return 0;
	}

	if (parsed > (long)INT_MAX)
	{
		*out_value = INT_MAX;
	}
	else if (parsed < (long)INT_MIN)
	{
		*out_value = INT_MIN;
	}
	else
	{
		*out_value = (int)parsed;
	}
	return 1;
}

RGINLINE int rg_gui_init(RgGuiContext* ctx, RgArena* arena, const RgGuiInitDesc* desc)
{
	RgGuiInitDesc init;
	if (!ctx || !arena || !arena->memory || arena->used > arena->capacity ||
	    !rg_gui_init_desc_resolve(desc, &init))
	{
		if (ctx)
		{
			memset(ctx, 0, sizeof(*ctx));
		}
		return 0;
	}

	size_t arena_used = arena->used;
	memset(ctx, 0, sizeof(*ctx));

	ctx->draw_list.cmds = RG_ARENA_PUSH_ARRAY(arena, RgGuiDrawCmd, init.max_draw_cmds);
	ctx->draw_list.capacity = init.max_draw_cmds;
	ctx->draw_list.count = 0u;

	ctx->overlay_list.cmds = RG_ARENA_PUSH_ARRAY(arena, RgGuiDrawCmd, init.max_draw_cmds);
	ctx->overlay_list.capacity = init.max_draw_cmds;
	ctx->overlay_list.count = 0u;
	ctx->draw_target = &ctx->draw_list;
	ctx->overlay_target = &ctx->overlay_list;
	ctx->modal_base_target = &ctx->draw_list;
	ctx->overlay_start = 0u;
	memset(&ctx->diagnostics, 0, sizeof(ctx->diagnostics));

	ctx->text_buffer = RG_ARENA_PUSH_ARRAY(arena, char, init.text_buffer_size);
	ctx->text_buffer_capacity = init.text_buffer_size;
	ctx->text_buffer_used = 0u;
	ctx->font = init.font;
	ctx->text_lookup = init.text_lookup;
	ctx->ime_callback = init.ime_callback;
	ctx->ime_callback_user = init.ime_callback_user;
	ctx->text_measure_cache = NULL;
	ctx->text_measure_cache_capacity = 0u;
	ctx->text_measure_cache_mask = 0u;
	ctx->text_length_cache = NULL;
	ctx->text_length_cache_capacity = 0u;
	ctx->text_length_cache_mask = 0u;
	ctx->menu_width_cache = NULL;
	ctx->menu_width_cache_capacity = 0u;
	ctx->menu_width_cache_mask = 0u;
	ctx->tab_scroll_cache = NULL;
	ctx->tab_scroll_cache_count = 0u;
	ctx->tab_scroll_cache_capacity = 0u;
	ctx->tab_scroll_cache_mask = 0u;
	ctx->tab_scroll_cache_next = 0u;
	ctx->value_cache = NULL;
	ctx->value_cache_capacity = 0u;
	ctx->value_cache_count = 0u;
	ctx->value_cache_mask = 0u;
	ctx->value_cache_next = 0u;
	ctx->dockspaces = NULL;
	ctx->dockspace_capacity = RG_GUI_MAX_DOCKSPACES;
	ctx->dockspace_count = 0u;
	ctx->dock_nodes = NULL;
	ctx->dock_node_count = 0u;
	ctx->dock_node_capacity = RG_GUI_MAX_DOCK_NODES;
	ctx->dock_node_free = 0u;
	ctx->dock_tabs = NULL;
	ctx->dock_tab_count = 0u;
	ctx->dock_tab_capacity = RG_GUI_MAX_DOCK_TABS;
	ctx->dock_tab_free = 0u;

	if (init.value_cache_size > 0u)
	{
		u32 capacity = rg_gui_next_pow2_u32(init.value_cache_size);
		ctx->value_cache = RG_ARENA_PUSH_ARRAY(arena, RgGuiValueCacheEntry, capacity);
		ctx->value_cache_capacity = capacity;
		if (capacity > 1u && (capacity & (capacity - 1u)) == 0u)
		{
			ctx->value_cache_mask = capacity - 1u;
		}
		if (ctx->value_cache)
		{
			memset(ctx->value_cache, 0, sizeof(RgGuiValueCacheEntry) * capacity);
		}
	}

	if (init.text_measure_cache_size > 0u)
	{
		u32 capacity = rg_gui_next_pow2_u32(init.text_measure_cache_size);
		ctx->text_measure_cache = RG_ARENA_PUSH_ARRAY(arena, RgGuiTextMeasureCacheEntry, capacity);
		ctx->text_measure_cache_capacity = capacity;
		if (capacity > 1u && (capacity & (capacity - 1u)) == 0u)
		{
			ctx->text_measure_cache_mask = capacity - 1u;
		}
		if (ctx->text_measure_cache)
		{
			memset(ctx->text_measure_cache, 0, sizeof(RgGuiTextMeasureCacheEntry) * capacity);
		}
	}

	if (init.text_length_cache_size > 0u)
	{
		u32 capacity = rg_gui_next_pow2_u32(init.text_length_cache_size);
		ctx->text_length_cache = RG_ARENA_PUSH_ARRAY(arena, RgGuiTextLengthCacheEntry, capacity);
		ctx->text_length_cache_capacity = capacity;
		if (capacity > 1u && (capacity & (capacity - 1u)) == 0u)
		{
			ctx->text_length_cache_mask = capacity - 1u;
		}
		if (ctx->text_length_cache)
		{
			memset(ctx->text_length_cache, 0, sizeof(RgGuiTextLengthCacheEntry) * capacity);
		}
	}

	if (init.menu_width_cache_size > 0u)
	{
		u32 capacity = rg_gui_next_pow2_u32(init.menu_width_cache_size);
		ctx->menu_width_cache = RG_ARENA_PUSH_ARRAY(arena, RgGuiMenuWidthCacheEntry, capacity);
		ctx->menu_width_cache_capacity = capacity;
		if (capacity > 1u && (capacity & (capacity - 1u)) == 0u)
		{
			ctx->menu_width_cache_mask = capacity - 1u;
		}
		if (ctx->menu_width_cache)
		{
			memset(ctx->menu_width_cache, 0, sizeof(RgGuiMenuWidthCacheEntry) * capacity);
		}
	}

	if (init.tab_scroll_cache_size > 0u)
	{
		u32 capacity = rg_gui_next_pow2_u32(init.tab_scroll_cache_size);
		ctx->tab_scroll_cache = RG_ARENA_PUSH_ARRAY(arena, RgGuiTabScrollCacheEntry, capacity);
		ctx->tab_scroll_cache_capacity = capacity;
		if (capacity > 1u && (capacity & (capacity - 1u)) == 0u)
		{
			ctx->tab_scroll_cache_mask = capacity - 1u;
		}
		if (ctx->tab_scroll_cache)
		{
			memset(ctx->tab_scroll_cache, 0, sizeof(RgGuiTabScrollCacheEntry) * capacity);
		}
	}

	if (ctx->dockspace_capacity > 0u)
	{
		ctx->dockspaces = RG_ARENA_PUSH_ARRAY(arena, RgGuiDockSpaceState*, ctx->dockspace_capacity);
		if (ctx->dockspaces)
		{
			memset(ctx->dockspaces, 0, sizeof(RgGuiDockSpaceState*) * ctx->dockspace_capacity);
		}
	}

	if (ctx->dock_node_capacity > 0u)
	{
		ctx->dock_nodes = RG_ARENA_PUSH_ARRAY(arena, RgGuiDockNode, ctx->dock_node_capacity + 1u);
		if (ctx->dock_nodes)
		{
			memset(ctx->dock_nodes, 0, sizeof(RgGuiDockNode) * (ctx->dock_node_capacity + 1u));
		}
	}

	if (ctx->dock_tab_capacity > 0u)
	{
		ctx->dock_tabs = RG_ARENA_PUSH_ARRAY(arena, RgGuiDockTab, ctx->dock_tab_capacity + 1u);
		if (ctx->dock_tabs)
		{
			memset(ctx->dock_tabs, 0, sizeof(RgGuiDockTab) * (ctx->dock_tab_capacity + 1u));
		}
	}

#if defined(RG_GUI_ENABLE_VIEWPORTS)
	ctx->viewports = RG_ARENA_PUSH_ARRAY(arena, RgGuiViewport, RG_GUI_MAX_VIEWPORTS);
	ctx->viewport_capacity = RG_GUI_MAX_VIEWPORTS;
	ctx->viewport_active_count = 0u;
	ctx->viewport_stack_top = 0u;
	if (ctx->viewports)
	{
		memset(ctx->viewports, 0, sizeof(RgGuiViewport) * ctx->viewport_capacity);
		for (u32 i = 0u; i < ctx->viewport_capacity; i++)
		{
			ctx->viewports[i].draw_list.cmds = RG_ARENA_PUSH_ARRAY(arena, RgGuiDrawCmd, init.max_draw_cmds);
			ctx->viewports[i].draw_list.capacity = init.max_draw_cmds;
			ctx->viewports[i].draw_list.count = 0u;

			ctx->viewports[i].overlay_list.cmds = RG_ARENA_PUSH_ARRAY(arena, RgGuiDrawCmd, init.max_draw_cmds);
			ctx->viewports[i].overlay_list.capacity = init.max_draw_cmds;
			ctx->viewports[i].overlay_list.count = 0u;
			ctx->viewports[i].overlay_start = 0u;
			memset(&ctx->viewports[i].platform_output, 0,
			       sizeof(ctx->viewports[i].platform_output));
			ctx->viewports[i].platform_output.cursor = RG_GUI_CURSOR_DEFAULT;
			ctx->viewports[i].window_focused = 1;
			ctx->viewports[i].active = 0;
			ctx->viewports[i].origin = rg_vec2(0.0f, 0.0f);
		}
	}
#endif

	ctx->style = rg_gui_style_default();
	RgTextSize reference_glyph = rg_text_measure(ctx->font, "M", 1u, rg_gui_text_base_scale(ctx));
	if (reference_glyph.width > 0.0f)
	{
		ctx->style.char_width = reference_glyph.width;
	}
	memset(ctx->arrow_icons, 0, sizeof(ctx->arrow_icons));
	memset(&ctx->close_icon, 0, sizeof(ctx->close_icon));
	ctx->input = NULL;
	ctx->input_events = NULL;
	ctx->input_window_id = 0u;
	ctx->input_event_focus_id = 0u;
	ctx->input_events_processed_id = 0u;
	ctx->input_events_changed_id = 0u;
	ctx->input_events_submit_id = 0u;
	ctx->input_window_focused_at_frame_start = 1;
	ctx->window_focused = 1;
	memset(&ctx->platform_output, 0, sizeof(ctx->platform_output));
	ctx->platform_output.cursor = RG_GUI_CURSOR_DEFAULT;
	ctx->mouse_pos = rg_vec2(0.0f, 0.0f);
	ctx->mouse_pos_raw = rg_vec2(0.0f, 0.0f);
	ctx->mouse_pos_global = rg_vec2(0.0f, 0.0f);
	ctx->viewport_origin = rg_vec2(0.0f, 0.0f);
	ctx->viewport_id = 0u;
	ctx->mouse_down = 0;
	ctx->mouse_pressed = 0;
	ctx->mouse_released = 0;
	ctx->mouse_wheel = 0.0f;
	ctx->mouse_cursor = RG_GUI_CURSOR_DEFAULT;
	ctx->input_capture_active = 0;
	ctx->input_capture_next = 0;
	ctx->input_capture_depth = 0u;
	ctx->modal_active = 0;
	ctx->modal_opened = 0;
	ctx->modal_depth = 0u;
	ctx->window_bounds = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	ctx->window_order_next = 0u;
	ctx->window_bounds_set = 0;

	ctx->hot_id = 0u;
	ctx->active_id = 0u;
	ctx->focus_id = 0u;
	ctx->scroll_owner = 0u;
	ctx->scroll_owner_next = 0u;
	ctx->tab_first_id = 0u;
	ctx->tab_last_id = 0u;
	ctx->tab_prev_id = 0u;
	ctx->tab_focus_id = 0u;
	ctx->tab_dir = 0;
	ctx->tab_seek = 0;
	ctx->tab_wrap = 0;
	ctx->tab_found_focus = 0;
	ctx->key_repeat_owner = 0u;
	ctx->key_repeat_key = SDL_SCANCODE_UNKNOWN;
	ctx->key_repeat_hold_time = 0.0f;
	ctx->key_repeat_repeat_time = 0.0f;
	ctx->menu_bar_id = 0u;
	ctx->menu_bar_block_left = 0;
	ctx->menu_bar_block_right = 0;
	ctx->disabled_depth = 0u;
	ctx->disabled_stack_top = 0u;
	ctx->id_stack_top = 0u;
	ctx->id_stack[0] = 0u;
	ctx->style_var_stack_top = 0u;
	ctx->style_color_stack_top = 0u;
	ctx->item_width_stack_top = 0u;
	ctx->popup_stack_top = 0u;

	memset(&ctx->layout, 0, sizeof(ctx->layout));
	memset(&ctx->text_edit, 0, sizeof(ctx->text_edit));
	memset(&ctx->number_edit, 0, sizeof(ctx->number_edit));
	ctx->text_edit_state = &ctx->text_edit;
	ctx->number_edit_state = &ctx->number_edit;
	rg_gui_drag_clear(ctx);

	ctx->time = 0.0f;
	ctx->delta_time = 0.0f;
	ctx->cursor_blink_timer = 0.0f;
	ctx->cursor_visible = 1;

	int allocation_failed = !ctx->draw_list.cmds || !ctx->overlay_list.cmds || !ctx->text_buffer ||
	                        (init.value_cache_size > 0u && !ctx->value_cache) ||
	                        (init.text_measure_cache_size > 0u && !ctx->text_measure_cache) ||
	                        (init.text_length_cache_size > 0u && !ctx->text_length_cache) ||
	                        (init.menu_width_cache_size > 0u && !ctx->menu_width_cache) ||
	                        (init.tab_scroll_cache_size > 0u && !ctx->tab_scroll_cache) ||
	                        (ctx->dockspace_capacity > 0u && !ctx->dockspaces) ||
	                        (ctx->dock_node_capacity > 0u && !ctx->dock_nodes) ||
	                        (ctx->dock_tab_capacity > 0u && !ctx->dock_tabs);
#if defined(RG_GUI_ENABLE_VIEWPORTS)
	allocation_failed = allocation_failed || !ctx->viewports;
	if (!allocation_failed)
	{
		for (u32 i = 0u; i < ctx->viewport_capacity; i++)
		{
			allocation_failed = allocation_failed || !ctx->viewports[i].draw_list.cmds ||
			                    !ctx->viewports[i].overlay_list.cmds;
		}
	}
#endif
	if (allocation_failed)
	{
		arena->used = arena_used;
		memset(ctx, 0, sizeof(*ctx));
		return 0;
	}

	return 1;
}

RGINLINE void rg_gui_set_ime_callback(RgGuiContext* ctx, RgGuiImeCallback callback, void* user_data)
{
	RG_GUI_ASSERT(ctx != NULL);
	ctx->ime_callback = callback;
	ctx->ime_callback_user = user_data;
}

RGINLINE void rg_gui_text_edit_update_blink(RgGuiContext* ctx)
{
	if (!ctx)
	{
		return;
	}

	if (ctx->text_edit_state->active)
	{
		ctx->cursor_blink_timer += ctx->delta_time;
		if (ctx->cursor_blink_timer >= ctx->style.cursor_blink_interval)
		{
			ctx->cursor_visible = !ctx->cursor_visible;
			ctx->cursor_blink_timer = 0.0f;
		}
	}
	else
	{
		ctx->cursor_blink_timer = 0.0f;
		ctx->cursor_visible = 1;
		ctx->text_edit_state->backspace_hold_time = 0.0f;
		ctx->text_edit_state->backspace_repeat_time = 0.0f;
	}
}

RGINLINE void rg_gui_begin_frame_ex(RgGuiContext* ctx, const RgInputState* input,
                                    const RgInputEventQueue* events,
                                    SDL_WindowID window_id, f32 delta_time)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(input != NULL);

	memset(&ctx->diagnostics, 0, sizeof(ctx->diagnostics));
	ctx->text_edit_state = &ctx->text_edit;
	ctx->number_edit_state = &ctx->number_edit;
	ctx->input = input;
	ctx->input_events = events;
	ctx->input_window_id = window_id;
	ctx->input_event_focus_id = ctx->focus_id;
	ctx->input_events_processed_id = 0u;
	ctx->input_window_focused_at_frame_start = events ? ctx->window_focused : 1;
	if (!events)
	{
		ctx->window_focused = 1;
	}
	else
	{
		ctx->input_events_changed_id = 0u;
		ctx->input_events_submit_id = 0u;
		if (events->dropped_event_count > 0u ||
		    events->dropped_text_byte_count > 0u)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_INPUT_CAPACITY;
			ctx->diagnostics.dropped_input_events = events->dropped_event_count;
			ctx->diagnostics.dropped_input_text_bytes = events->dropped_text_byte_count;
		}

		size_t event_count = rg_gui_input_event_count(events);
		for (size_t i = 0u; i < event_count; i++)
		{
			const RgInputEvent* event = &events->events[i];
			if (!rg_gui_input_event_matches_window(ctx, event))
			{
				continue;
			}
			if (event->kind == RG_INPUT_EVENT_WINDOW_FOCUS_LOST)
			{
				ctx->window_focused = 0;
				ctx->active_id = 0u;
				ctx->input_capture_active = 0;
				ctx->input_capture_next = 0;
				rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
			}
			else if (event->kind == RG_INPUT_EVENT_WINDOW_FOCUS_GAINED)
			{
				ctx->window_focused = 1;
			}
		}
	}
	ctx->delta_time = delta_time;
	ctx->time += delta_time;
	memset(&ctx->platform_output, 0, sizeof(ctx->platform_output));
	ctx->platform_output.cursor = RG_GUI_CURSOR_DEFAULT;

	ctx->mouse_pos_raw = rg_vec2((f32)input->mouse_x, (f32)input->mouse_y);
	ctx->mouse_pos = ctx->mouse_pos_raw;
	ctx->mouse_down = rg_input_is_mouse_button_down(input, RG_MOUSE_BUTTON_LEFT);
	ctx->mouse_down_any = ctx->mouse_down;
	ctx->mouse_pressed = rg_input_is_mouse_button_pressed(input, RG_MOUSE_BUTTON_LEFT);
	ctx->mouse_released = rg_input_is_mouse_button_released(input, RG_MOUSE_BUTTON_LEFT);
	ctx->mouse_wheel = input->mouse_scroll_y;
	if (!ctx->window_focused)
	{
		ctx->mouse_down = 0;
		ctx->mouse_down_any = 0;
		ctx->mouse_pressed = 0;
		ctx->mouse_released = 0;
		ctx->mouse_wheel = 0.0f;
	}
	ctx->mouse_cursor = RG_GUI_CURSOR_DEFAULT;
	ctx->viewport_id = 0u;
	ctx->viewport_origin = rg_vec2(0.0f, 0.0f);
	ctx->dockspace_count = 0u;
	ctx->dock_drag.active = 0;
	ctx->dock_drag.mouse_released = 0;
	ctx->dock_drag.window = NULL;
	ctx->dock_drag.window_id = 0u;
	ctx->dock_drag.viewport_id = 0u;
	ctx->dock_drag.title = NULL;
	ctx->dock_drag.flags = 0u;
	ctx->dock_drag.mouse_global = rg_vec2(0.0f, 0.0f);
	ctx->dock_drag.origin = rg_vec2(0.0f, 0.0f);
	ctx->dock_drag.rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	ctx->dock_drag.bounds = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	ctx->dock_drag.bounds_set = 0;
	ctx->input_capture_active = ctx->input_capture_next;
	ctx->input_capture_next = 0;
	ctx->input_capture_depth = 0u;
	ctx->modal_opened = 0;
	ctx->modal_depth = 0u;
	if (ctx->input_capture_active)
	{
		ctx->mouse_pos = rg_gui_input_blocked_pos();
	}
	ctx->mouse_pos_global = rg_vec2(ctx->mouse_pos.x + ctx->viewport_origin.x,
	                                ctx->mouse_pos.y + ctx->viewport_origin.y);

	ctx->draw_list.count = 0u;
	ctx->overlay_list.count = 0u;
	ctx->draw_target = &ctx->draw_list;
	ctx->overlay_target = &ctx->overlay_list;
	ctx->modal_base_target = &ctx->draw_list;
	ctx->overlay_start = 0u;
	ctx->text_buffer_used = 0u;
	ctx->hot_id = 0u;
	ctx->scroll_owner_next = 0u;
	ctx->tab_first_id = 0u;
	ctx->tab_last_id = 0u;
	ctx->tab_prev_id = 0u;
	ctx->tab_focus_id = 0u;
	ctx->tab_dir = 0;
	ctx->tab_seek = 0;
	ctx->tab_wrap = 0;
	ctx->tab_found_focus = 0;
	ctx->disabled_depth = 0u;
	ctx->disabled_stack_top = 0u;
	ctx->id_stack_top = 0u;
	ctx->id_stack[0] = 0u;
	ctx->style_var_stack_top = 0u;
	ctx->style_color_stack_top = 0u;
	ctx->item_width_stack_top = 0u;
	ctx->popup_stack_top = 0u;

#if defined(RG_GUI_ENABLE_VIEWPORTS)
	ctx->viewport_active_count = 0u;
	ctx->viewport_stack_top = 0u;
	ctx->mouse_focus_viewport = 0u;
	ctx->mouse_focus_inside = 0;
	if (ctx->viewports)
	{
		for (u32 i = 0u; i < ctx->viewport_capacity; i++)
		{
			ctx->viewports[i].active = 0;
			ctx->viewports[i].draw_list.count = 0u;
			ctx->viewports[i].overlay_list.count = 0u;
			ctx->viewports[i].overlay_start = 0u;
			memset(&ctx->viewports[i].platform_output, 0,
			       sizeof(ctx->viewports[i].platform_output));
			ctx->viewports[i].platform_output.cursor = RG_GUI_CURSOR_DEFAULT;
		}
	}
#endif

	if (rg_input_is_key_pressed(input, SDL_SCANCODE_TAB))
	{
		ctx->tab_dir = rg_gui_input_has_shift(input) ? -1 : 1;
	}

	if (ctx->mouse_pressed && !ctx->input_events)
	{
		rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		ctx->focus_id = 0u;
		ctx->text_edit_state->active = 0;
	}

	rg_gui_text_edit_update_blink(ctx);
}

RGINLINE void rg_gui_begin_frame(RgGuiContext* ctx, const RgInputState* input,
                                 f32 delta_time)
{
	rg_gui_begin_frame_ex(ctx, input, NULL, 0u, delta_time);
}

RGINLINE void rg_gui_end_frame_state(RgGuiContext* ctx)
{
	if (!ctx)
	{
		return;
	}

	if (ctx->mouse_pressed && ctx->input_events &&
	    ctx->input_event_focus_id != 0u &&
	    ctx->focus_id == ctx->input_event_focus_id &&
	    ctx->active_id != ctx->input_event_focus_id)
	{
		ctx->focus_id = 0u;
		rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		ctx->text_edit_state->active = 0;
	}

	if (ctx->mouse_pressed && ctx->hot_id == 0u)
	{
		ctx->focus_id = 0u;
		ctx->active_id = 0u;
		rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		ctx->text_edit_state->active = 0;
	}

	if (ctx->tab_dir != 0 && ctx->tab_focus_id == 0u)
	{
		if (ctx->tab_dir > 0)
		{
			if ((ctx->tab_seek || !ctx->tab_found_focus) && ctx->tab_first_id != 0u)
			{
				ctx->tab_focus_id = ctx->tab_first_id;
			}
		}
		else
		{
			if ((ctx->tab_wrap || !ctx->tab_found_focus) && ctx->tab_last_id != 0u)
			{
				ctx->tab_focus_id = ctx->tab_last_id;
			}
		}
	}

	if (ctx->tab_focus_id != 0u)
	{
		ctx->focus_id = ctx->tab_focus_id;
	}

	if (ctx->text_edit_state->active && ctx->focus_id != ctx->text_edit_state->id)
	{
		rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		ctx->text_edit_state->active = 0;
	}

#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
	if (ctx->text_edit_state->value_undo_active && !ctx->text_edit_state->active)
	{
		rg_gui_text_value_undo_commit(ctx, ctx->text_edit_state, 1);
	}
#endif

#if RG_GUI_TEXT_VALUE_UNDO_STACK_SIZE > 0
	if (!ctx->text_edit_state->active)
	{
		if (rg_gui_shortcut_undo(ctx, 0u))
		{
			rg_gui_text_value_undo_apply(ctx, 0);
		}
		else if (rg_gui_shortcut_redo(ctx, 0u))
		{
			rg_gui_text_value_undo_apply(ctx, 1);
		}
	}
#endif

	if (!ctx->mouse_down_any && (ctx->drag_active || ctx->drag_source_id != 0u))
	{
		rg_gui_drag_clear(ctx);
	}

	ctx->scroll_owner = ctx->scroll_owner_next;
}

static RG_NOINLINE void rg_gui_finalize_platform_output(RgGuiContext* ctx)
{
	if (!ctx)
	{
		return;
	}
	if (!ctx->window_focused || !ctx->text_edit_state ||
	    !ctx->text_edit_state->active ||
	    ctx->focus_id != ctx->text_edit_state->id)
	{
		ctx->platform_output.wants_text_input = 0;
		ctx->platform_output.ime_caret_valid = 0;
		ctx->platform_output.ime_caret_rect =
		    rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	}
}

RGINLINE void rg_gui_dock_drag_handle(RgGuiContext* ctx);

static RG_NOINLINE void rg_gui_diagnostics_recover_stacks(RgGuiContext* ctx)
{
	ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_IMBALANCE;

	if (ctx->style_var_stack_top > 0u)
	{
		rg_gui_pop_style_var(ctx, ctx->style_var_stack_top);
	}
	if (ctx->style_color_stack_top > 0u)
	{
		rg_gui_pop_style_color(ctx, ctx->style_color_stack_top);
	}

	ctx->id_stack_top = 0u;
	ctx->id_stack[0] = 0u;
	ctx->item_width_stack_top = 0u;
	ctx->popup_stack_top = 0u;
	ctx->disabled_stack_top = 0u;
	ctx->disabled_depth = 0u;
	ctx->input_capture_depth = 0u;
	ctx->modal_depth = 0u;
	RG_GUI_ASSERT(0 && "rg_gui_end_frame with unbalanced stacks");
}

RGINLINE void rg_gui_diagnostics_finish_stacks(RgGuiContext* ctx)
{
	u32 unbalanced = ctx->id_stack_top |
	                 ctx->style_var_stack_top |
	                 ctx->style_color_stack_top |
	                 ctx->item_width_stack_top |
	                 ctx->popup_stack_top |
	                 ctx->disabled_stack_top |
	                 ctx->disabled_depth |
	                 ctx->input_capture_depth |
	                 ctx->modal_depth;
#if defined(RG_GUI_ENABLE_VIEWPORTS)
	unbalanced |= ctx->viewport_stack_top;
#endif
	if (unbalanced != 0u)
	{
		rg_gui_diagnostics_recover_stacks(ctx);
	}
}

RGINLINE void rg_gui_end_frame(RgGuiContext* ctx)
{
	rg_gui_end_frame_state(ctx);

	rg_gui_dock_drag_handle(ctx);

	rg_gui_diagnostics_update_draw_high_water(ctx, &ctx->draw_list, &ctx->overlay_list);
	ctx->overlay_start = ctx->draw_list.count;
	if (ctx->overlay_list.count > 0u)
	{
		u32 available = ctx->draw_list.count <= ctx->draw_list.capacity ? ctx->draw_list.capacity - ctx->draw_list.count : 0u;
		u32 copy_count = ctx->overlay_list.count;
		if (copy_count > available)
		{
			copy_count = available;
		}

		if (copy_count < ctx->overlay_list.count)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_OVERLAY_CAPACITY;
			rg_gui_diagnostics_add_draw_drop(ctx, ctx->overlay_list.count - copy_count);
		}

		if (copy_count > 0u)
		{
			memcpy(ctx->draw_list.cmds + ctx->draw_list.count,
			       ctx->overlay_list.cmds,
			       sizeof(RgGuiDrawCmd) * copy_count);
			ctx->draw_list.count += copy_count;
		}
	}

	if (ctx->text_buffer_used > ctx->diagnostics.text_buffer_high_water)
	{
		ctx->diagnostics.text_buffer_high_water = ctx->text_buffer_used;
	}

	if (!ctx->mouse_down_any)
	{
		if (ctx->active_id != 0u)
		{
			ctx->active_id = 0u;
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_ACTIVE_ID_RECOVERED;
			ctx->diagnostics.recovered_active_ids++;
		}
#if defined(RG_GUI_ENABLE_VIEWPORTS)
		if (ctx->viewports)
		{
			for (u32 i = 0u; i < ctx->viewport_capacity; i++)
			{
				if (ctx->viewports[i].active_id != 0u)
				{
					ctx->viewports[i].active_id = 0u;
					ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_ACTIVE_ID_RECOVERED;
					if (ctx->diagnostics.recovered_active_ids < UINT32_MAX)
					{
						ctx->diagnostics.recovered_active_ids++;
					}
				}
			}
		}
#endif
	}

	rg_gui_diagnostics_finish_stacks(ctx);
	ctx->modal_active = ctx->modal_opened;
	if (ctx->platform_output.wants_text_input ||
	    ctx->platform_output.ime_caret_valid)
	{
		rg_gui_finalize_platform_output(ctx);
	}
	ctx->platform_output.cursor = ctx->mouse_cursor;
}

RGINLINE void rg_gui_push_id(RgGuiContext* ctx, RgGuiId id)
{
	if (!ctx)
	{
		return;
	}

	if (ctx->id_stack_top + 1u >= RG_GUI_ID_STACK_MAX)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_OVERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_push_id stack overflow");
		return;
	}

	RgGuiId base = ctx->id_stack[ctx->id_stack_top];
	if (id == 0u)
	{
		ctx->id_stack[++ctx->id_stack_top] = base;
		return;
	}

	ctx->id_stack[++ctx->id_stack_top] = rg_gui_id_combine(base, id);
}

RGINLINE void rg_gui_push_id_u64(RgGuiContext* ctx, u64 value)
{
	rg_gui_push_id(ctx, rg_gui_id_u64(value));
}

RGINLINE void rg_gui_push_id_ptr(RgGuiContext* ctx, const void* ptr)
{
	rg_gui_push_id(ctx, rg_gui_id_ptr(ptr));
}

#if !defined(RG_GUI_NO_STRING_IDS)
RGINLINE void rg_gui_push_id_str(RgGuiContext* ctx, const char* str)
{
	rg_gui_push_id(ctx, rg_gui_id_str(str));
}

RGINLINE void rg_gui_push_id_str_len(RgGuiContext* ctx, const char* str, size_t len)
{
	rg_gui_push_id(ctx, rg_gui_id_str_len(str, len));
}
#endif

RGINLINE void rg_gui_pop_id(RgGuiContext* ctx)
{
	if (!ctx || ctx->id_stack_top == 0u)
	{
		if (ctx)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW;
		}
		RG_GUI_ASSERT(0 && "rg_gui_pop_id without push");
		return;
	}

	ctx->id_stack_top--;
}

RGINLINE void rg_gui_push_style_var(RgGuiContext* ctx, RgGuiStyleVar var, f32 value)
{
	if (!ctx)
	{
		return;
	}

	if (var == RG_GUI_STYLE_VAR_VALUE_DECIMALS)
	{
		RG_GUI_ASSERT(0 && "rg_gui_push_style_var: use rg_gui_push_style_var_int");
		return;
	}

	if (ctx->style_var_stack_top >= RG_GUI_STYLE_VAR_STACK_MAX)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_OVERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_push_style_var stack overflow");
		return;
	}

	f32* target = rg_gui_style_var_ptr(&ctx->style, var);
	if (!target)
	{
		RG_GUI_ASSERT(0 && "rg_gui_push_style_var invalid var");
		return;
	}

	RgGuiStyleVarStackEntry* entry = &ctx->style_var_stack[ctx->style_var_stack_top++];
	entry->var = var;
	entry->is_int = 0;
	entry->value.f = *target;
	*target = value;
}

RGINLINE void rg_gui_push_style_var_int(RgGuiContext* ctx, RgGuiStyleVar var, int value)
{
	if (!ctx)
	{
		return;
	}

	if (var != RG_GUI_STYLE_VAR_VALUE_DECIMALS)
	{
		RG_GUI_ASSERT(0 && "rg_gui_push_style_var_int expects RG_GUI_STYLE_VAR_VALUE_DECIMALS");
		return;
	}

	if (ctx->style_var_stack_top >= RG_GUI_STYLE_VAR_STACK_MAX)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_OVERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_push_style_var stack overflow");
		return;
	}

	RgGuiStyleVarStackEntry* entry = &ctx->style_var_stack[ctx->style_var_stack_top++];
	entry->var = var;
	entry->is_int = 1;
	entry->value.i = ctx->style.value_decimals;
	ctx->style.value_decimals = value;
}

RGINLINE void rg_gui_pop_style_var(RgGuiContext* ctx, u32 count)
{
	if (!ctx)
	{
		return;
	}

	while (count-- > 0u)
	{
		if (ctx->style_var_stack_top == 0u)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW;
			RG_GUI_ASSERT(0 && "rg_gui_pop_style_var stack underflow");
			return;
		}

		RgGuiStyleVarStackEntry* entry = &ctx->style_var_stack[--ctx->style_var_stack_top];
		if (entry->is_int)
		{
			ctx->style.value_decimals = entry->value.i;
		}
		else
		{
			f32* target = rg_gui_style_var_ptr(&ctx->style, entry->var);
			if (target)
			{
				*target = entry->value.f;
			}
		}
	}
}

RGINLINE void rg_gui_push_style_color(RgGuiContext* ctx, RgGuiStyleColor color, rg_vec4 value)
{
	if (!ctx)
	{
		return;
	}

	if (ctx->style_color_stack_top >= RG_GUI_STYLE_COLOR_STACK_MAX)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_OVERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_push_style_color stack overflow");
		return;
	}

	rg_vec4* target = rg_gui_style_color_ptr(&ctx->style, color);
	if (!target)
	{
		RG_GUI_ASSERT(0 && "rg_gui_push_style_color invalid color");
		return;
	}

	RgGuiStyleColorStackEntry* entry = &ctx->style_color_stack[ctx->style_color_stack_top++];
	entry->color = color;
	entry->value = *target;
	*target = value;
}

RGINLINE void rg_gui_pop_style_color(RgGuiContext* ctx, u32 count)
{
	if (!ctx)
	{
		return;
	}

	while (count-- > 0u)
	{
		if (ctx->style_color_stack_top == 0u)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW;
			RG_GUI_ASSERT(0 && "rg_gui_pop_style_color stack underflow");
			return;
		}

		RgGuiStyleColorStackEntry* entry = &ctx->style_color_stack[--ctx->style_color_stack_top];
		rg_vec4* target = rg_gui_style_color_ptr(&ctx->style, entry->color);
		if (target)
		{
			*target = entry->value;
		}
	}
}

RGINLINE void rg_gui_layout_reset_row(RgGuiLayout* layout)
{
	if (!layout)
	{
		return;
	}

	layout->row_active = 0;
	layout->row_x = layout->x;
	layout->row_y = layout->cursor_y;
	layout->row_cursor_x = layout->x;
	layout->row_height = 0.0f;
	layout->row_spacing = 0.0f;
	layout->next_item_width = 0.0f;
}

RGINLINE void rg_gui_layout_begin(RgGuiContext* ctx, f32 x, f32 y, f32 width, f32 spacing)
{
	ctx->layout.x = x;
	ctx->layout.y = y;
	ctx->layout.width = width;
	ctx->layout.spacing = spacing;
	ctx->layout.cursor_y = y;
	ctx->layout.active = 1;
	rg_gui_layout_reset_row(&ctx->layout);
}

RGINLINE RgGuiRect rg_gui_layout_next(RgGuiContext* ctx, f32 height)
{
	if (ctx->layout.row_active)
	{
		rg_gui_layout_row_end(ctx);
	}

	ctx->layout.next_item_width = 0.0f;
	RgGuiRect rect = rg_gui_make_rect(ctx->layout.x, ctx->layout.cursor_y, ctx->layout.width, height);
	ctx->layout.cursor_y += height + ctx->layout.spacing;
	return rect;
}

RGINLINE void rg_gui_layout_advance(RgGuiContext* ctx, f32 height)
{
	if (ctx->layout.row_active)
	{
		rg_gui_layout_row_end(ctx);
	}

	ctx->layout.cursor_y += height;
}

RGINLINE void rg_gui_set_next_item_width(RgGuiContext* ctx, f32 width)
{
	if (!ctx)
	{
		return;
	}

	if (width < 0.0f)
	{
		width = 0.0f;
	}

	ctx->layout.next_item_width = width;
}

RGINLINE void rg_gui_push_item_width(RgGuiContext* ctx, f32 width)
{
	if (!ctx)
	{
		return;
	}

	if (ctx->item_width_stack_top >= RG_GUI_ITEM_WIDTH_STACK_MAX)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_OVERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_push_item_width stack overflow");
		return;
	}

	if (width < 0.0f)
	{
		width = 0.0f;
	}

	ctx->item_width_stack[ctx->item_width_stack_top++] = width;
}

RGINLINE void rg_gui_pop_item_width(RgGuiContext* ctx, u32 count)
{
	if (!ctx)
	{
		return;
	}

	while (count-- > 0u)
	{
		if (ctx->item_width_stack_top == 0u)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW;
			RG_GUI_ASSERT(0 && "rg_gui_pop_item_width stack underflow");
			return;
		}

		ctx->item_width_stack_top--;
	}
}

RGINLINE void rg_gui_layout_row_begin(RgGuiContext* ctx, f32 height, f32 spacing)
{
	if (!ctx || !ctx->layout.active)
	{
		return;
	}

	if (ctx->layout.row_active)
	{
		rg_gui_layout_row_end(ctx);
	}

	if (spacing < 0.0f)
	{
		spacing = ctx->style.inner_spacing;
	}
	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	ctx->layout.row_active = 1;
	ctx->layout.row_x = ctx->layout.x;
	ctx->layout.row_y = ctx->layout.cursor_y;
	ctx->layout.row_cursor_x = ctx->layout.x;
	ctx->layout.row_height = height;
	ctx->layout.row_spacing = spacing;
}

RGINLINE RgGuiRect rg_gui_layout_row_next(RgGuiContext* ctx, f32 width)
{
	RgGuiRect rect = {0};
	if (!ctx || !ctx->layout.active || !ctx->layout.row_active)
	{
		return rect;
	}

	f32 item_width = width;
	if (item_width <= 0.0f)
	{
		if (ctx->layout.next_item_width > 0.0f)
		{
			item_width = ctx->layout.next_item_width;
			ctx->layout.next_item_width = 0.0f;
		}
		else if (ctx->item_width_stack_top > 0u)
		{
			item_width = ctx->item_width_stack[ctx->item_width_stack_top - 1u];
		}
	}

	f32 max_x = ctx->layout.x + ctx->layout.width;
	f32 remaining = max_x - ctx->layout.row_cursor_x;
	if (item_width <= 0.0f || item_width > remaining)
	{
		item_width = remaining;
	}
	if (item_width < 0.0f)
	{
		item_width = 0.0f;
	}

	rect = rg_gui_make_rect(ctx->layout.row_cursor_x, ctx->layout.row_y, item_width, ctx->layout.row_height);
	ctx->layout.row_cursor_x += item_width + ctx->layout.row_spacing;
	return rect;
}

RGINLINE void rg_gui_layout_row_end(RgGuiContext* ctx)
{
	if (!ctx || !ctx->layout.row_active)
	{
		return;
	}

	ctx->layout.cursor_y = ctx->layout.row_y + ctx->layout.row_height + ctx->layout.spacing;
	ctx->layout.row_active = 0;
}

RGINLINE void rg_gui_columns_begin(RgGuiColumns* cols, RgGuiRect rect, const f32* widths, u32 count, f32 spacing)
{
	RG_GUI_ASSERT(cols != NULL);

	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	cols->rect = rect;
	cols->widths = widths;
	cols->spacing = spacing;
	cols->count = count;
	cols->index = 0u;
	cols->cursor_x = rect.x;
	cols->cursor_y = rect.y;
	cols->row_height = 0.0f;
}

RGINLINE RgGuiRect rg_gui_columns_next(RgGuiColumns* cols, f32 height)
{
	RgGuiRect rect = {0};
	if (!cols || cols->count == 0u)
	{
		return rect;
	}

	f32 spacing = cols->spacing;
	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	f32 width = 0.0f;
	if (cols->widths)
	{
		width = cols->widths[cols->index];
	}
	else
	{
		f32 available = cols->rect.w - spacing * (f32)(cols->count - 1u);
		if (available < 0.0f)
		{
			available = 0.0f;
		}
		width = available / (f32)cols->count;
	}

	if (width < 0.0f)
	{
		width = 0.0f;
	}

	rect = rg_gui_make_rect(cols->cursor_x, cols->cursor_y, width, height);

	if (height > cols->row_height)
	{
		cols->row_height = height;
	}

	cols->index++;
	if (cols->index >= cols->count)
	{
		cols->index = 0u;
		cols->cursor_x = cols->rect.x;
		cols->cursor_y += cols->row_height + spacing;
		cols->row_height = 0.0f;
	}
	else
	{
		cols->cursor_x += width + spacing;
	}

	return rect;
}

RGINLINE void rg_gui_tree_begin(RgGuiContext* ctx, RgGuiTreeState* tree, f32 indent)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(tree != NULL);

	if (indent <= 0.0f)
	{
		indent = ctx->style.char_width + ctx->style.padding;
		if (indent < 1.0f)
		{
			indent = 1.0f;
		}
	}

	tree->indent = indent;
	tree->depth = 0u;
}

RGINLINE void rg_gui_tree_push(RgGuiTreeState* tree)
{
	RG_GUI_ASSERT(tree != NULL);
	tree->depth++;
}

RGINLINE void rg_gui_tree_pop(RgGuiTreeState* tree)
{
	RG_GUI_ASSERT(tree != NULL);
	if (tree->depth > 0u)
	{
		tree->depth--;
	}
}

RGINLINE int rg_gui_context_menu_begin(RgGuiContext* ctx, RgGuiPopupState* popup, RgGuiRect rect, RgGuiId id)
{
	return rg_gui_context_menu_begin_ex(ctx, popup, rect, id, RG_GUI_CONTEXT_MENU_NONE);
}

RGINLINE int rg_gui_context_menu_begin_ex(RgGuiContext* ctx, RgGuiPopupState* popup, RgGuiRect rect,
                                          RgGuiId id, u32 flags)
{
	if (!ctx || !popup || !ctx->input)
	{
		return 0;
	}

	id = rg_gui_id_scoped(ctx, id);
	RgGuiId list_id = rg_gui_id_combine(id, 0x4D454E55u);
	RG_GUI_UNUSED(list_id);

	if (!popup->open)
	{
		if (!(flags & RG_GUI_CONTEXT_MENU_ALLOW_WHEN_TEXT_ACTIVE) && ctx->text_edit_state->active)
		{
			return 0;
		}
		if (!(flags & RG_GUI_CONTEXT_MENU_ALLOW_WHEN_CAPTURED) &&
		    (ctx->input_capture_active || ctx->input_capture_depth > 0u))
		{
			return 0;
		}
		if (!(flags & RG_GUI_CONTEXT_MENU_ALLOW_WHEN_MODAL) && ctx->modal_active)
		{
			return 0;
		}
	}

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	int right_pressed = rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_RIGHT);

	if (enabled && right_pressed)
	{
		if (hovered)
		{
			popup->open = 1;
			popup->pos = ctx->mouse_pos;
			ctx->focus_id = id;
		}
		else if (popup->open)
		{
			popup->open = 0;
		}
	}

	return popup->open;
}

RGINLINE int rg_gui_popup_begin(RgGuiContext* ctx, RgGuiPopupState* popup, RgGuiRect rect, f32 spacing, RgGuiId id)
{
	if (!ctx || !popup || !popup->open)
	{
		return 0;
	}

	id = rg_gui_id_scoped(ctx, id);

	if (ctx->popup_stack_top >= RG_GUI_POPUP_STACK_MAX)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_OVERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_popup_begin stack overflow");
		return 0;
	}

	rg_gui_input_capture_begin(ctx);

	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	RgGuiRect popup_rect = rect;
	popup_rect.x += popup->pos.x;
	popup_rect.y += popup->pos.y;

	if (popup_rect.w < 0.0f) popup_rect.w = 0.0f;
	if (popup_rect.h < 0.0f) popup_rect.h = 0.0f;

	if (ctx->window_bounds_set)
	{
		RgGuiRect bounds = ctx->window_bounds;
		if (popup_rect.w > bounds.w) popup_rect.w = bounds.w;
		if (popup_rect.h > bounds.h) popup_rect.h = bounds.h;

		f32 max_x = bounds.x + bounds.w - popup_rect.w;
		f32 max_y = bounds.y + bounds.h - popup_rect.h;
		if (popup_rect.x > max_x) popup_rect.x = max_x;
		if (popup_rect.y > max_y) popup_rect.y = max_y;
		if (popup_rect.x < bounds.x) popup_rect.x = bounds.x;
		if (popup_rect.y < bounds.y) popup_rect.y = bounds.y;
	}

	RgGuiPopupStackEntry* entry = &ctx->popup_stack[ctx->popup_stack_top++];
	entry->prev_layout = ctx->layout;
	entry->rect = popup_rect;
	entry->popup = popup;
	entry->prev_draw_target = ctx->draw_target;
	entry->hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, popup_rect);
	entry->child_hovered = 0;

	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && entry->hovered)
	{
		ctx->hot_id = id;
		if (ctx->mouse_pressed)
		{
			ctx->focus_id = id;
		}
	}

	f32 border = ctx->style.border_thickness;
	f32 pad = ctx->style.padding;
	f32 inset = border + pad;

	RgGuiRect inner = popup_rect;
	inner.x += inset;
	inner.y += inset;
	inner.w -= inset * 2.0f;
	inner.h -= inset * 2.0f;

	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;

	rg_gui_push_rect(ctx, popup_rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, popup_rect, ctx->style.color_border, ctx->style.border_thickness);

	ctx->layout.x = inner.x;
	ctx->layout.y = inner.y;
	ctx->layout.width = inner.w;
	ctx->layout.spacing = spacing;
	ctx->layout.cursor_y = inner.y;
	ctx->layout.active = 1;
	rg_gui_layout_reset_row(&ctx->layout);

	rg_gui_push_clip(ctx, inner);
	return 1;
}

RGINLINE void rg_gui_popup_end(RgGuiContext* ctx)
{
	if (!ctx || ctx->popup_stack_top == 0u)
	{
		if (ctx)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW;
		}
		RG_GUI_ASSERT(0 && "rg_gui_popup_end without begin");
		return;
	}

	RgGuiPopupStackEntry* entry = &ctx->popup_stack[ctx->popup_stack_top - 1u];
	int any_pressed = ctx->mouse_pressed;
	if (ctx->input && rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_RIGHT))
	{
		any_pressed = 1;
	}

	int any_hovered = entry->hovered || entry->child_hovered;
	if (ctx->popup_stack_top > 1u)
	{
		RgGuiPopupStackEntry* parent = &ctx->popup_stack[ctx->popup_stack_top - 2u];
		parent->child_hovered = parent->child_hovered || any_hovered;
	}

	if (any_pressed && !any_hovered && entry->popup)
	{
		entry->popup->open = 0;
	}

	ctx->layout = entry->prev_layout;
	ctx->popup_stack_top--;
	rg_gui_pop_clip(ctx);
	ctx->draw_target = entry->prev_draw_target ? entry->prev_draw_target : &ctx->draw_list;
	rg_gui_input_capture_end(ctx, entry->popup && entry->popup->open);
}

RGINLINE int rg_gui_modal_begin(RgGuiContext* ctx, RgGuiPopupState* popup, RgGuiRect rect, f32 spacing, RgGuiId id)
{
	if (!ctx || !popup || !popup->open)
	{
		return 0;
	}

	id = rg_gui_id_scoped(ctx, id);

	if (ctx->popup_stack_top >= RG_GUI_POPUP_STACK_MAX)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_OVERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_modal_begin stack overflow");
		return 0;
	}

	if (ctx->modal_depth == 0u)
	{
		ctx->modal_base_target = ctx->draw_target ? ctx->draw_target : &ctx->draw_list;
	}
	ctx->modal_active = 1;
	ctx->modal_depth++;

	rg_gui_input_capture_begin(ctx);

	RgGuiDrawList* prev_draw_target = ctx->draw_target ? ctx->draw_target : &ctx->draw_list;
	RgGuiDrawList* overlay_target = ctx->overlay_target ? ctx->overlay_target : &ctx->overlay_list;
	ctx->draw_target = overlay_target;

	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	RgGuiRect modal_rect = rect;
	modal_rect.x += popup->pos.x;
	modal_rect.y += popup->pos.y;

	if (modal_rect.w < 0.0f) modal_rect.w = 0.0f;
	if (modal_rect.h < 0.0f) modal_rect.h = 0.0f;

	if (ctx->window_bounds_set)
	{
		RgGuiRect bounds = ctx->window_bounds;
		if (modal_rect.w > bounds.w) modal_rect.w = bounds.w;
		if (modal_rect.h > bounds.h) modal_rect.h = bounds.h;

		f32 max_x = bounds.x + bounds.w - modal_rect.w;
		f32 max_y = bounds.y + bounds.h - modal_rect.h;
		if (modal_rect.x > max_x) modal_rect.x = max_x;
		if (modal_rect.y > max_y) modal_rect.y = max_y;
		if (modal_rect.x < bounds.x) modal_rect.x = bounds.x;
		if (modal_rect.y < bounds.y) modal_rect.y = bounds.y;
	}

	RgGuiRect backdrop = ctx->window_bounds_set ? ctx->window_bounds : modal_rect;
	if (backdrop.w > 0.0f && backdrop.h > 0.0f)
	{
		rg_vec4 overlay = ctx->style.color_bg;
		overlay.w = 0.35f;
		rg_gui_push_rect(ctx, backdrop, overlay);
	}

	RgGuiPopupStackEntry* entry = &ctx->popup_stack[ctx->popup_stack_top++];
	entry->prev_layout = ctx->layout;
	entry->rect = modal_rect;
	entry->popup = popup;
	entry->prev_draw_target = prev_draw_target;
	entry->hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, modal_rect);
	entry->child_hovered = 0;

	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && entry->hovered)
	{
		ctx->hot_id = id;
		if (ctx->mouse_pressed)
		{
			ctx->focus_id = id;
		}
	}

	f32 border = ctx->style.border_thickness;
	f32 pad = ctx->style.padding;
	f32 inset = border + pad;

	RgGuiRect inner = modal_rect;
	inner.x += inset;
	inner.y += inset;
	inner.w -= inset * 2.0f;
	inner.h -= inset * 2.0f;

	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;

	rg_gui_push_rect(ctx, modal_rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, modal_rect, ctx->style.color_border, ctx->style.border_thickness);

	ctx->layout.x = inner.x;
	ctx->layout.y = inner.y;
	ctx->layout.width = inner.w;
	ctx->layout.spacing = spacing;
	ctx->layout.cursor_y = inner.y;
	ctx->layout.active = 1;
	rg_gui_layout_reset_row(&ctx->layout);

	rg_gui_push_clip(ctx, inner);
	return 1;
}

RGINLINE void rg_gui_modal_end(RgGuiContext* ctx)
{
	if (!ctx || ctx->popup_stack_top == 0u)
	{
		if (ctx)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW;
		}
		RG_GUI_ASSERT(0 && "rg_gui_modal_end without begin");
		return;
	}

	RgGuiPopupStackEntry* entry = &ctx->popup_stack[ctx->popup_stack_top - 1u];
	ctx->layout = entry->prev_layout;
	ctx->popup_stack_top--;
	rg_gui_pop_clip(ctx);
	ctx->draw_target = entry->prev_draw_target ? entry->prev_draw_target : &ctx->draw_list;
	rg_gui_input_capture_end(ctx, entry->popup && entry->popup->open);

	if (ctx->modal_depth > 0u)
	{
		ctx->modal_depth--;
	}

	if (entry->popup && entry->popup->open)
	{
		ctx->modal_opened = 1;
	}
	else if (ctx->modal_depth == 0u)
	{
		ctx->modal_active = 0;
		ctx->modal_base_target = ctx->draw_target ? ctx->draw_target : &ctx->draw_list;
	}
}

RGINLINE int rg_gui_menu_bar(RgGuiContext* ctx, const char* const* labels, u32 count, int* active, int* open,
                             RgGuiRect rect, RgGuiId id, RgGuiRect* out_active_rect)
{
	RG_GUI_ASSERT(active != NULL);
	RG_GUI_ASSERT(open != NULL);
	count = (u32)rg_gui_u32_count_to_int(count);

	if (!ctx)
	{
		if (out_active_rect)
		{
			*out_active_rect = rg_gui_make_rect(rect.x, rect.y, 0.0f, rect.h);
		}
		return 0;
	}

	rg_gui_input_capture_begin(ctx);

	id = rg_gui_id_scoped(ctx, id);
	RgGuiId list_id = rg_gui_id_combine(id, 0x4D454E55u);
	ctx->menu_bar_id = id;
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && ctx->tab_focus_id == id && ctx->focus_id != id)
	{
		ctx->focus_id = id;
	}
	int open_state = *open ? 1 : 0;
	int active_index = *active;
	int changed = 0;
	int mouse_moved = 0;
	if (enabled && ctx->input)
	{
		mouse_moved = (ctx->input->mouse_delta_x != 0 || ctx->input->mouse_delta_y != 0);
	}

	if (count == 0u)
	{
		active_index = -1;
		open_state = 0;
	}
	else if (active_index < 0 || active_index >= (int)count)
	{
		active_index = open_state ? 0 : -1;
	}

	if (enabled && count > 0u && ctx->input && rg_gui_input_has_alt(ctx->input))
	{
		for (u32 i = 0u; i < count; i++)
		{
			const char* label = labels ? labels[i] : NULL;
			char hot = rg_gui_label_hotkey(label);
			SDL_Scancode sc = rg_gui_scancode_from_ascii(hot);
			if (sc != SDL_SCANCODE_UNKNOWN && rg_input_is_key_pressed(ctx->input, sc))
			{
				active_index = (int)i;
				open_state = 1;
				ctx->focus_id = id;
				changed = 1;
				break;
			}
		}
	}

	if (enabled && open_state && ctx->focus_id == 0u)
	{
		ctx->focus_id = id;
	}

	int menu_list_focused = (ctx->focus_id == list_id);
	int focused = enabled && (open_state || ctx->focus_id == id || menu_list_focused);
	int block_left = (open_state && ctx->menu_bar_id == id && ctx->menu_bar_block_left);
	int block_right = (open_state && ctx->menu_bar_id == id && ctx->menu_bar_block_right);
	int nav_enabled = (ctx->focus_id == id || menu_list_focused);

	if (focused && count > 0u)
	{
		int left_pressed = rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_LEFT);
		int right_pressed = rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RIGHT);
		int left_down = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT);
		int right_down = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT);
		SDL_Scancode key = SDL_SCANCODE_UNKNOWN;
		if (nav_enabled)
		{
			if ((left_pressed || left_down) && !block_left)
			{
				key = SDL_SCANCODE_LEFT;
			}
			else if ((right_pressed || right_down) && !block_right)
			{
				key = SDL_SCANCODE_RIGHT;
			}
		}

		if (key != SDL_SCANCODE_UNKNOWN)
		{
			RgGuiId repeat_owner = menu_list_focused ? list_id : id;
			int repeats = rg_gui_key_repeat(ctx, repeat_owner, key);
			if (repeats > 0)
			{
				u32 move = (u32)repeats;
				if (move >= count) move %= count;
				u32 next = active_index < 0 ? 0u : (u32)active_index;
				if (key == SDL_SCANCODE_LEFT)
				{
					next = next >= move ? next - move : count - (move - next);
				}
				else
				{
					next = next >= count - move ? next - (count - move) : next + move;
				}
				if ((int)next != active_index)
				{
					active_index = (int)next;
					changed = 1;
				}
			}
		}

		if (!open_state &&
		    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
		     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
		     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE) ||
		     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_DOWN)))
		{
			open_state = 1;
			if (active_index < 0)
			{
				active_index = 0;
			}
			changed = 1;
		}

		if (open_state && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_ESCAPE))
		{
			open_state = 0;
			changed = 1;
		}
	}

	f32 pad = ctx->style.padding;
	f32 spacing = ctx->style.inner_spacing;
	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	f32 item_h = rect.h;
	if (item_h <= 0.0f)
	{
		item_h = ctx->style.text_height + pad * 2.0f;
	}

	f32 cursor_x = rect.x + pad;
	f32 cursor_y = rect.y;
	RgGuiRect active_rect = rg_gui_make_rect(rect.x, rect.y, 0.0f, item_h);

	for (u32 i = 0u; i < count; i++)
	{
		const char* label = labels ? labels[i] : NULL;
		size_t len = rg_gui_text_len_label(ctx, label, RG_GUI_LABEL_COPY);
		f32 text_w = (label && len > 0u) ? rg_gui_text_measure_label(ctx, label, len, RG_GUI_LABEL_COPY) : 0.0f;
		f32 item_w = text_w + pad * 2.0f;
		if (item_w < 1.0f)
		{
			item_w = 1.0f;
		}

		RgGuiRect item_rect = rg_gui_make_rect(cursor_x, cursor_y, item_w, item_h);
		int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, item_rect);
		if (hovered)
		{
			ctx->hot_id = id;
			if (open_state && (int)i != active_index && mouse_moved)
			{
				active_index = (int)i;
				changed = 1;
			}
		}

		if (enabled && hovered && ctx->mouse_pressed)
		{
			if (!open_state || active_index != (int)i)
			{
				active_index = (int)i;
				open_state = 1;
			}
			else
			{
				open_state = 0;
			}
			ctx->focus_id = id;
			changed = 1;
		}

		if (label && *label)
		{
			rg_vec2 pos = rg_vec2(item_rect.x + pad,
			                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
			if (RG_GUI_LABEL_COPY)
			{
				rg_gui_push_text(ctx, label, pos, ctx->style.color_text);
			}
			else
			{
				rg_gui_push_text_static(ctx, label, pos, ctx->style.color_text);
			}
		}

		if ((int)i == active_index)
		{
			active_rect = item_rect;
		}

		cursor_x += item_w + spacing;
	}

	*active = active_index;
	*open = open_state;
	if (!open_state)
	{
		ctx->menu_bar_block_left = 0;
		ctx->menu_bar_block_right = 0;
	}
	if (out_active_rect)
	{
		*out_active_rect = active_rect;
	}
	rg_gui_input_capture_end(ctx, 0);
	return changed;
}

RGINLINE void rg_gui_tooltip_ex(RgGuiContext* ctx, RgGuiRect rect, const char* text, int copy)
{
	if (!ctx || !text || !*text)
	{
		return;
	}

	if (rg_gui_is_disabled(ctx))
	{
		return;
	}

	if (!rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect))
	{
		return;
	}

	size_t len = rg_gui_text_len_label(ctx, text, copy);
	f32 text_w = rg_gui_text_measure_label(ctx, text, len, copy);
	f32 pad = ctx->style.padding;
	f32 w = text_w + pad * 2.0f;
	f32 h = ctx->style.text_height + pad * 2.0f;

	f32 offset_x = 12.0f;
	f32 offset_y = 16.0f;
	RgGuiRect tip = rg_gui_make_rect(ctx->mouse_pos.x + offset_x,
	                                 ctx->mouse_pos.y + offset_y,
	                                 w,
	                                 h);

	RgGuiDrawList* overlay = rg_gui_overlay_target(ctx);
	rg_gui_push_rect_to(ctx, overlay, tip, ctx->style.color_panel);
	rg_gui_push_rect_outline_to(ctx, overlay, tip, ctx->style.color_border, ctx->style.border_thickness);

	rg_vec2 pos = rg_vec2(tip.x + pad,
	                      tip.y + (tip.h - ctx->style.text_height) * 0.5f);
	rg_gui_push_text_ex_to(ctx, overlay, text, pos, ctx->style.color_text, copy);
}

RGINLINE void rg_gui_tooltip(RgGuiContext* ctx, RgGuiRect rect, const char* text)
{
	rg_gui_tooltip_ex(ctx, rect, text, 1);
}

RGINLINE void rg_gui_tooltip_static(RgGuiContext* ctx, RgGuiRect rect, const char* text)
{
	rg_gui_tooltip_ex(ctx, rect, text, 0);
}

// Docking internals (forward declarations)
RGINLINE RgGuiDockSpaceState* rg_gui_dockspace_find(RgGuiContext* ctx, RgGuiId id);
RGINLINE u32 rg_gui_dock_find_tab_by_id(RgGuiContext* ctx, u32 node_index, RgGuiId window_id, u32* out_node);
RGINLINE void rg_gui_dock_remove_tab(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace,
                                     u32 node_index, u32 tab_index, int close_window);
RGINLINE void rg_gui_dock_detach_window(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, RgGuiWindowState* window);
#if defined(RG_GUI_ENABLE_VIEWPORTS)
RGINLINE RgGuiViewport* rg_gui_viewport_find(RgGuiContext* ctx, RgGuiId id);
#endif

typedef struct RgGuiDockTabDrag
{
	u32 node;
	u32 tab;
} RgGuiDockTabDrag;

RGINLINE int rg_gui_dock_tab_move(RgGuiContext* ctx, u32 node_index, u32 tab_index, u32 target_index, int after);
RGINLINE void rg_gui_dock_layout_collect(RgGuiContext* ctx, u32 node_index,
                                         u32* node_map, u32* node_count,
                                         u32* tab_map, u32* tab_count);
RGINLINE void rg_gui_dock_reset(RgGuiContext* ctx);

RGINLINE int rg_gui_dock_tab_move(RgGuiContext* ctx, u32 node_index, u32 tab_index, u32 target_index, int after)
{
	if (!ctx || tab_index == 0u || target_index == 0u || tab_index == target_index)
	{
		return 0;
	}
	if (node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return 0;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	u32 prev = 0u;
	u32 iter = node->tab_head;
	while (iter != 0u && iter != tab_index)
	{
		prev = iter;
		iter = ctx->dock_tabs[iter].next;
	}
	if (iter == 0u)
	{
		return 0;
	}

	u32 next = ctx->dock_tabs[iter].next;
	if (prev == 0u)
	{
		node->tab_head = next;
	}
	else
	{
		ctx->dock_tabs[prev].next = next;
	}

	if (after)
	{
		u32 target_next = ctx->dock_tabs[target_index].next;
		ctx->dock_tabs[target_index].next = tab_index;
		ctx->dock_tabs[tab_index].next = target_next;
	}
	else
	{
		if (node->tab_head == target_index)
		{
			ctx->dock_tabs[tab_index].next = target_index;
			node->tab_head = tab_index;
		}
		else
		{
			u32 target_prev = node->tab_head;
			while (target_prev != 0u && ctx->dock_tabs[target_prev].next != target_index)
			{
				target_prev = ctx->dock_tabs[target_prev].next;
			}
			if (target_prev != 0u)
			{
				ctx->dock_tabs[tab_index].next = target_index;
				ctx->dock_tabs[target_prev].next = tab_index;
			}
			else
			{
				ctx->dock_tabs[tab_index].next = node->tab_head;
				node->tab_head = tab_index;
			}
		}
	}

	return 1;
}

RGINLINE void rg_gui_dock_layout_collect(RgGuiContext* ctx, u32 node_index,
                                         u32* node_map, u32* node_count,
                                         u32* tab_map, u32* tab_count)
{
	if (!ctx || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return;
	}
	if (node_map[node_index] != 0u)
	{
		return;
	}

	*node_count += 1u;
	node_map[node_index] = *node_count;

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	u32 tab = node->tab_head;
	while (tab != 0u && tab <= ctx->dock_tab_capacity)
	{
		if (tab_map[tab] == 0u)
		{
			*tab_count += 1u;
			tab_map[tab] = *tab_count;
		}
		tab = ctx->dock_tabs[tab].next;
	}

	if (node->split != RG_GUI_DOCK_SPLIT_NONE)
	{
		rg_gui_dock_layout_collect(ctx, node->child_a, node_map, node_count, tab_map, tab_count);
		rg_gui_dock_layout_collect(ctx, node->child_b, node_map, node_count, tab_map, tab_count);
	}
}

RGINLINE void rg_gui_dock_reset(RgGuiContext* ctx)
{
	if (!ctx)
	{
		return;
	}

	ctx->dock_node_count = 0u;
	ctx->dock_node_free = 0u;
	if (ctx->dock_nodes)
	{
		memset(ctx->dock_nodes, 0, sizeof(RgGuiDockNode) * (ctx->dock_node_capacity + 1u));
	}

	ctx->dock_tab_count = 0u;
	ctx->dock_tab_free = 0u;
	if (ctx->dock_tabs)
	{
		memset(ctx->dock_tabs, 0, sizeof(RgGuiDockTab) * (ctx->dock_tab_capacity + 1u));
	}

	if (ctx->dockspaces)
	{
		for (u32 i = 0u; i < ctx->dockspace_count; i++)
		{
			if (ctx->dockspaces[i])
			{
				ctx->dockspaces[i]->root = 0u;
				ctx->dockspaces[i]->tab_popup.open = 0;
				ctx->dockspaces[i]->tab_popup.pos = rg_vec2(0.0f, 0.0f);
				ctx->dockspaces[i]->tab_popup_selected = -1;
				ctx->dockspaces[i]->tab_popup_scroll = 0;
				ctx->dockspaces[i]->tab_popup_tab = 0u;
				ctx->dockspaces[i]->tab_popup_node = 0u;
				ctx->dockspaces[i]->initialized = 1;
			}
		}
	}
}

RGINLINE int rg_gui_window_begin_docked(RgGuiContext* ctx, RgGuiWindowState* window, const char* title,
                                        f32 spacing, u32 flags, RgGuiId id, int* out_fallback)
{
	if (out_fallback)
	{
		*out_fallback = 0;
	}

	if (!ctx || !window)
	{
		return 0;
	}

	if ((flags & RG_GUI_WINDOW_CLOSABLE) == 0u)
	{
		window->open = 1;
	}

	if (!window->open)
	{
		RgGuiDockSpaceState* current = rg_gui_dockspace_find(ctx, window->dockspace_id);
		if (current && window->dock_node != 0u && window->dock_tab != 0u)
		{
			rg_gui_dock_remove_tab(ctx, current, window->dock_node, window->dock_tab, 0);
		}
		return 0;
	}

	RgGuiDockSpaceState* dockspace = rg_gui_dockspace_find(ctx, window->dockspace_id);
	if (!dockspace || dockspace->root == 0u)
	{
		window->docked = 0;
		window->dockspace_id = 0u;
		window->dock_node = 0u;
		window->dock_tab = 0u;
		window->dock_drag_pending = 0;
		if (out_fallback)
		{
			*out_fallback = 1;
		}
		return 0;
	}

	u32 node_index = window->dock_node;
	u32 tab_index = window->dock_tab;
	if (node_index == 0u || tab_index == 0u ||
	    node_index > ctx->dock_node_capacity || tab_index > ctx->dock_tab_capacity)
	{
		tab_index = rg_gui_dock_find_tab_by_id(ctx, dockspace->root, id, &node_index);
		if (tab_index == 0u)
		{
			window->docked = 0;
			window->dockspace_id = 0u;
			window->dock_node = 0u;
			window->dock_tab = 0u;
			window->dock_drag_pending = 0;
			if (out_fallback)
			{
				*out_fallback = 1;
			}
			return 0;
		}
		window->dock_node = node_index;
		window->dock_tab = tab_index;
	}

	if (node_index == 0u || tab_index == 0u ||
	    node_index > ctx->dock_node_capacity || tab_index > ctx->dock_tab_capacity)
	{
		if (out_fallback)
		{
			*out_fallback = 1;
		}
		return 0;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	RgGuiDockTab* tab = &ctx->dock_tabs[tab_index];
	tab->window_id = id;
	tab->flags = flags;
	tab->window = window;
	tab->title = title;

	if (node->active_tab == 0u && node->tab_head != 0u)
	{
		node->active_tab = node->tab_head;
	}

	f32 pad = ctx->style.padding;
	f32 inner = ctx->style.inner_spacing;
	if (inner < 0.0f)
	{
		inner = 0.0f;
	}

	f32 tab_h = ctx->style.text_height + pad * 2.0f;
	if (tab_h < 1.0f)
	{
		tab_h = 1.0f;
	}

	RgGuiRect win_rect = node->rect;
	RgGuiRect tab_bar = rg_gui_make_rect(win_rect.x, win_rect.y, win_rect.w, tab_h);
	RgGuiRect tear_rect = tab_bar;

	if (window->dock_drag_pending)
	{
		if (!ctx->mouse_down)
		{
			window->dock_drag_pending = 0;
		}
		else
		{
			rg_vec2 delta = rg_vec2(ctx->mouse_pos.x - window->start_mouse.x,
			                        ctx->mouse_pos.y - window->start_mouse.y);
			f32 dist2 = delta.x * delta.x + delta.y * delta.y;
			if (dist2 > 36.0f)
			{
				if (!rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, tear_rect))
				{
					window->rect = node->rect;
					window->start_rect = window->rect;
					window->start_mouse = ctx->mouse_pos;
					rg_gui_dock_detach_window(ctx, dockspace, window);
					ctx->active_id = rg_gui_id_combine(id, 1u);
					if (out_fallback)
					{
						*out_fallback = 1;
					}
					return 0;
				}
			}
		}
	}

	if (node->active_tab != tab_index)
	{
		return 0;
	}

	window->prev_layout = ctx->layout;
	window->rect = node->rect;

	rg_gui_push_rect(ctx, win_rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, win_rect, ctx->style.color_border, ctx->style.border_thickness);
	rg_gui_push_rect(ctx, tab_bar, ctx->style.color_bg);

	int enabled = !rg_gui_is_disabled(ctx);
	f32 close_size = tab_h - pad;
	if (close_size < ctx->style.text_height)
	{
		close_size = ctx->style.text_height;
	}
	if (close_size < 4.0f)
	{
		close_size = 4.0f;
	}

	int right_pressed = enabled && ctx->input &&
	                    rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_RIGHT);
	int middle_pressed = enabled && ctx->input &&
	                     rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_MIDDLE);

	int switched = 0;
	int reorder_requested = 0;
	u32 reorder_tab = 0u;
	u32 reorder_target = 0u;
	int reorder_after = 0;
	RgGuiId drag_type = rg_gui_id_combine(dockspace->id, 0x544142u);
	int tab_menu_opened = 0;

	f32 tab_total = 0.0f;
	f32 tab_widths[RG_GUI_MAX_DOCK_TABS + 1u];
	f32 active_start = -1.0f;
	f32 active_end = -1.0f;
	f32 content_x = 0.0f;
	u32 iter = node->tab_head;
	while (iter != 0u)
	{
		RgGuiDockTab* cur = &ctx->dock_tabs[iter];
		const char* tab_title = cur->title ? cur->title : "";
		size_t title_len = rg_gui_text_len_label(ctx, tab_title, RG_GUI_LABEL_COPY);
		f32 text_w = rg_gui_text_measure_label(ctx, tab_title, title_len, RG_GUI_LABEL_COPY);
		f32 tab_w = text_w + pad * 2.0f;
		const RgGuiIcon* tab_icon = cur->window ? &cur->window->icon : NULL;
		if (rg_gui_icon_valid(tab_icon))
		{
			tab_w += ctx->style.text_height;
			if (title_len > 0u)
			{
				tab_w += inner;
			}
		}
		if ((cur->flags & RG_GUI_WINDOW_CLOSABLE) != 0u)
		{
			tab_w += close_size + inner;
		}
		if (iter <= RG_GUI_MAX_DOCK_TABS)
		{
			tab_widths[iter] = tab_w;
		}

		if (iter == node->active_tab)
		{
			active_start = content_x;
			active_end = content_x + tab_w;
		}

		content_x += tab_w + inner;
		iter = cur->next;
	}

	if (content_x > 0.0f)
	{
		tab_total = content_x - inner;
	}

	int show_scroll = (tab_total > tab_bar.w && tab_bar.w > 0.0f);
	f32 arrow_w = 0.0f;
	if (show_scroll)
	{
		arrow_w = tab_h;
		if (arrow_w * 2.0f > tab_bar.w)
		{
			arrow_w = tab_bar.w * 0.5f;
		}
	}

	f32 strip_x = tab_bar.x + arrow_w;
	f32 strip_w = tab_bar.w - arrow_w * 2.0f;
	if (strip_w < 0.0f)
	{
		strip_w = 0.0f;
	}
	RgGuiRect strip_rect = rg_gui_make_rect(strip_x, tab_bar.y, strip_w, tab_bar.h);
	int strip_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, strip_rect);

	f32 scroll_x = node->tab_scroll;
	f32 max_scroll = tab_total - strip_w;
	if (max_scroll < 0.0f)
	{
		max_scroll = 0.0f;
	}
	if (max_scroll <= 0.0f)
	{
		scroll_x = 0.0f;
	}
	if (scroll_x < 0.0f)
	{
		scroll_x = 0.0f;
	}
	if (scroll_x > max_scroll)
	{
		scroll_x = max_scroll;
	}

	if (max_scroll > 0.0f && active_start >= 0.0f && strip_w > 0.0f)
	{
		f32 active_w = active_end - active_start;
		if (active_w > strip_w)
		{
			f32 min_scroll = active_start;
			f32 max_active_scroll = active_end - strip_w;
			if (scroll_x < min_scroll)
			{
				scroll_x = min_scroll;
			}
			if (scroll_x > max_active_scroll)
			{
				scroll_x = max_active_scroll;
			}
		}
		else
		{
			if (active_start < scroll_x)
			{
				scroll_x = active_start;
			}
			else if (active_end > scroll_x + strip_w)
			{
				scroll_x = active_end - strip_w;
			}
		}
		if (scroll_x < 0.0f)
		{
			scroll_x = 0.0f;
		}
		if (scroll_x > max_scroll)
		{
			scroll_x = max_scroll;
		}
	}

	if (show_scroll)
	{
		f32 scroll_step = strip_w * 0.5f;
		if (scroll_step < tab_h)
		{
			scroll_step = tab_h;
		}
		f32 wheel_step = tab_h * 2.0f;
		if (wheel_step < 8.0f)
		{
			wheel_step = 8.0f;
		}

		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, tab_bar);
		if (bar_hovered && ctx->mouse_wheel != 0.0f && max_scroll > 0.0f)
		{
			int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
			if (steps != 0)
			{
				scroll_x -= (f32)steps * wheel_step;
			}
		}

		RgGuiRect left_rect = rg_gui_make_rect(tab_bar.x, tab_bar.y, arrow_w, tab_bar.h);
		RgGuiRect right_rect = rg_gui_make_rect(tab_bar.x + tab_bar.w - arrow_w, tab_bar.y, arrow_w, tab_bar.h);

		int can_left = (scroll_x > 0.0f);
		int can_right = (scroll_x + 0.5f < max_scroll);

		RgGuiId left_id = rg_gui_id_combine(id, 0x5441424Cu);
		RgGuiId right_id = rg_gui_id_combine(id, 0x54414252u);

		int left_hovered = enabled && can_left && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, left_rect);
		int right_hovered = enabled && can_right && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, right_rect);

		if (left_hovered)
		{
			ctx->hot_id = left_id;
		}
		if (right_hovered)
		{
			ctx->hot_id = right_id;
		}

		if (enabled && left_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = left_id;
		}
		if (enabled && right_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = right_id;
		}

		if (!enabled && (ctx->active_id == left_id || ctx->active_id == right_id))
		{
			ctx->active_id = 0u;
		}

		if (enabled && ctx->active_id == left_id && ctx->mouse_released)
		{
			if (left_hovered)
			{
				scroll_x -= scroll_step;
			}
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == right_id && ctx->mouse_released)
		{
			if (right_hovered)
			{
				scroll_x += scroll_step;
			}
			ctx->active_id = 0u;
		}

		rg_vec4 left_bg = ctx->style.color_bg;
		if (enabled && ctx->active_id == left_id)
		{
			left_bg = ctx->style.color_bg_active;
		}
		else if (left_hovered)
		{
			left_bg = ctx->style.color_bg_hover;
		}
		rg_vec4 right_bg = ctx->style.color_bg;
		if (enabled && ctx->active_id == right_id)
		{
			right_bg = ctx->style.color_bg_active;
		}
		else if (right_hovered)
		{
			right_bg = ctx->style.color_bg_hover;
		}

		rg_gui_push_rect(ctx, left_rect, left_bg);
		rg_gui_push_rect_outline(ctx, left_rect, ctx->style.color_border, ctx->style.border_thickness);
		rg_gui_push_rect(ctx, right_rect, right_bg);
		rg_gui_push_rect_outline(ctx, right_rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec4 arrow_color = ctx->style.color_text;
		if (!can_left || !enabled)
		{
			arrow_color = ctx->style.color_text_dim;
		}
		rg_vec4 arrow_color_right = ctx->style.color_text;
		if (!can_right || !enabled)
		{
			arrow_color_right = ctx->style.color_text_dim;
		}

		rg_gui_push_arrow(ctx, left_rect, RG_GUI_ARROW_LEFT, arrow_color);
		rg_gui_push_arrow(ctx, right_rect, RG_GUI_ARROW_RIGHT, arrow_color_right);

		if (scroll_x < 0.0f)
		{
			scroll_x = 0.0f;
		}
		if (scroll_x > max_scroll)
		{
			scroll_x = max_scroll;
		}
	}

	node->tab_scroll = scroll_x;

	if (strip_rect.w > 0.0f && strip_rect.h > 0.0f)
	{
		f32 cursor_x = strip_rect.x - scroll_x;
		iter = node->tab_head;
		rg_gui_push_clip(ctx, strip_rect);
		while (iter != 0u)
		{
			RgGuiDockTab* cur = &ctx->dock_tabs[iter];
			const char* tab_title = cur->title ? cur->title : "";
			f32 tab_w = (iter <= RG_GUI_MAX_DOCK_TABS) ? tab_widths[iter] : 0.0f;

			RgGuiRect tab_rect = rg_gui_make_rect(cursor_x, tab_bar.y, tab_w, tab_bar.h);
			int tab_visible = (tab_rect.x + tab_rect.w > strip_rect.x && tab_rect.x < strip_rect.x + strip_rect.w);
			int tab_hovered = enabled && strip_hovered && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, tab_rect);

			if (tab_hovered)
			{
				ctx->hot_id = rg_gui_id_combine(id, (u64)iter + 10u);
			}

			u32 tab_flags = cur->flags;
			int tab_closable = (tab_flags & RG_GUI_WINDOW_CLOSABLE) != 0u;
			int tab_dockable = (tab_flags & RG_GUI_WINDOW_DOCKABLE) != 0u;

			if (right_pressed && tab_hovered)
			{
				dockspace->tab_popup.open = 1;
				dockspace->tab_popup.pos = ctx->mouse_pos;
				dockspace->tab_popup_selected = -1;
				dockspace->tab_popup_scroll = 0;
				dockspace->tab_popup_tab = iter;
				dockspace->tab_popup_node = node_index;
				tab_menu_opened = 1;
			}

			RgGuiRect close_rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
			int close_hovered = 0;
			if (tab_closable)
			{
				f32 close_x = tab_rect.x + tab_rect.w - close_size - inner;
				if (close_x < tab_rect.x + pad)
				{
					close_x = tab_rect.x + tab_rect.w - close_size;
				}
				close_rect = rg_gui_make_rect(close_x,
				                              tab_rect.y + (tab_rect.h - close_size) * 0.5f,
				                              close_size,
				                              close_size);
				close_hovered = enabled && strip_hovered &&
				                rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, close_rect);
				if (close_hovered)
				{
					ctx->hot_id = rg_gui_id_combine(id, (u64)iter + 100u);
				}
			}

			if (enabled && tab_hovered && middle_pressed && tab_closable)
			{
				rg_gui_pop_clip(ctx);
				rg_gui_dock_remove_tab(ctx, dockspace, node_index, iter, 1);
				return 0;
			}

			if (enabled && close_hovered && ctx->mouse_pressed && tab_closable)
			{
				rg_gui_pop_clip(ctx);
				rg_gui_dock_remove_tab(ctx, dockspace, node_index, iter, 1);
				return 0;
			}

			if (enabled && tab_hovered && ctx->mouse_pressed && !close_hovered)
			{
				node->active_tab = iter;
				ctx->focus_id = cur->window_id;
				if (iter != tab_index)
				{
					switched = 1;
				}
			}

			if (enabled && tab_hovered && ctx->mouse_pressed && !close_hovered && tab_dockable)
			{
				if (cur->window)
				{
					cur->window->dock_drag_pending = 1;
					cur->window->start_mouse = ctx->mouse_pos;
				}
			}

			if (enabled && tab_hovered && !close_hovered)
			{
				RgGuiId drag_id = rg_gui_id_combine(id, (u64)iter + 1000u);
				if (rg_gui_drag_source(ctx, tab_rect, drag_id))
				{
					RgGuiDockTabDrag drag_payload = {node_index, iter};
					rg_gui_drag_set_payload(ctx, drag_type, &drag_payload, sizeof(drag_payload));
				}
			}

			if (tab_visible)
			{
				rg_vec4 tab_color = ctx->style.color_bg;
				if (iter == node->active_tab)
				{
					tab_color = ctx->style.color_bg_active;
				}
				else if (tab_hovered)
				{
					tab_color = ctx->style.color_bg_hover;
				}
				rg_gui_push_rect(ctx, tab_rect, tab_color);
				rg_gui_push_rect_outline(ctx, tab_rect, ctx->style.color_border, ctx->style.border_thickness);

				if (close_hovered)
				{
					rg_gui_push_rect(ctx, close_rect, ctx->style.color_bg_hover);
				}

				f32 text_left = tab_rect.x + pad;
				f32 text_right = tab_rect.x + tab_rect.w - pad;
				if (tab_closable)
				{
					text_right = close_rect.x - inner;
				}
				f32 text_w = text_right - text_left;
				if (text_w > 0.0f && tab_rect.h > 0.0f)
				{
					RgGuiRect text_clip = rg_gui_make_rect(text_left, tab_rect.y, text_w, tab_rect.h);
					rg_gui_push_clip(ctx, text_clip);
					const RgGuiIcon* tab_icon = cur->window ? &cur->window->icon : NULL;
					if (rg_gui_icon_valid(tab_icon))
					{
						RgGuiRect icon_rect = rg_gui_icon_rect(ctx, tab_rect, text_left);
						rg_gui_push_icon(ctx, tab_icon, icon_rect, 0);
						text_left = icon_rect.x + icon_rect.w;
						if (tab_title && *tab_title)
						{
							text_left += inner;
						}
					}
					rg_vec2 text_pos = rg_vec2(text_left,
					                           tab_rect.y + (tab_rect.h - ctx->style.text_height) * 0.5f);
					if (tab_title && RG_GUI_LABEL_COPY)
					{
						rg_gui_push_text(ctx, tab_title, text_pos, ctx->style.color_text);
					}
					else if (tab_title)
					{
						rg_gui_push_text_static(ctx, tab_title, text_pos, ctx->style.color_text);
					}
					rg_gui_pop_clip(ctx);
				}

				if (tab_closable)
				{
					rg_gui_push_close_glyph(ctx, close_rect);
				}
			}

			if (tab_visible && strip_hovered)
			{
				const RgGuiDragPayload* active_drag = rg_gui_drag_payload(ctx);
				if (active_drag &&
				    active_drag->type == drag_type &&
				    active_drag->data != NULL &&
				    active_drag->size == sizeof(RgGuiDockTabDrag))
				{
					const RgGuiDockTabDrag* drag_payload = (const RgGuiDockTabDrag*)active_drag->data;
					if (drag_payload->node == node_index && drag_payload->tab != iter)
					{
						RgGuiDragPayload drop_payload = {0};
						u32 drag_result = rg_gui_drag_target(ctx, tab_rect, drag_type, &drop_payload);
						if (drag_result & RG_GUI_DRAG_RESULT_HOVER)
						{
							int drop_after = (ctx->mouse_pos.x > tab_rect.x + tab_rect.w * 0.5f);
							f32 line_x = drop_after ? (tab_rect.x + tab_rect.w - 2.0f) : tab_rect.x;
							RgGuiRect line = rg_gui_make_rect(line_x, tab_rect.y, 2.0f, tab_rect.h);
							rg_gui_push_rect(ctx, line, ctx->style.color_accent);
						}
						if (drag_result & RG_GUI_DRAG_RESULT_ACCEPT)
						{
							reorder_requested = 1;
							reorder_tab = drag_payload->tab;
							reorder_target = iter;
							reorder_after = (ctx->mouse_pos.x > tab_rect.x + tab_rect.w * 0.5f);
						}
					}
				}
			}

			cursor_x += tab_w + inner;
			iter = cur->next;
		}
		rg_gui_pop_clip(ctx);
	}

	if (reorder_requested)
	{
		rg_gui_dock_tab_move(ctx, node_index, reorder_tab, reorder_target, reorder_after);
	}

	if (right_pressed && !tab_menu_opened && dockspace->tab_popup.open)
	{
		dockspace->tab_popup.open = 0;
	}

	if (dockspace->tab_popup.open)
	{
		u32 menu_node = dockspace->tab_popup_node;
		u32 menu_tab = dockspace->tab_popup_tab;
		int menu_valid = 0;
		if (menu_node != 0u && menu_node <= ctx->dock_node_capacity &&
		    menu_tab != 0u && menu_tab <= ctx->dock_tab_capacity)
		{
			u32 check = ctx->dock_nodes[menu_node].tab_head;
			while (check != 0u)
			{
				if (check == menu_tab)
				{
					menu_valid = 1;
					break;
				}
				check = ctx->dock_tabs[check].next;
			}
		}

		if (!menu_valid)
		{
			dockspace->tab_popup.open = 0;
		}
		else
		{
			enum
			{
				TAB_MENU_CLOSE = 0,
				TAB_MENU_CLOSE_OTHERS,
				TAB_MENU_CLOSE_ALL,
				TAB_MENU_UNDOCK,
				TAB_MENU_COUNT
			};

			RgGuiDockTab* menu_target = &ctx->dock_tabs[menu_tab];
			u32 menu_flags = menu_target->flags;
			int menu_closable = (menu_flags & RG_GUI_WINDOW_CLOSABLE) != 0u;
			int menu_dockable = (menu_flags & RG_GUI_WINDOW_DOCKABLE) != 0u;

			int closable_count = 0;
			int closable_others = 0;
			u32 count_iter = ctx->dock_nodes[menu_node].tab_head;
			while (count_iter != 0u)
			{
				if ((ctx->dock_tabs[count_iter].flags & RG_GUI_WINDOW_CLOSABLE) != 0u)
				{
					closable_count++;
					if (count_iter != menu_tab)
					{
						closable_others = 1;
					}
				}
				count_iter = ctx->dock_tabs[count_iter].next;
			}

			const char* menu_items[TAB_MENU_COUNT] =
			    {
			        "Close",
			        "Close others",
			        "Close all",
			        "Undock"};
			RgGuiMenuItemFlags menu_item_flags[TAB_MENU_COUNT] =
			    {
			        menu_closable ? RG_GUI_MENU_ITEM_NONE : RG_GUI_MENU_ITEM_DISABLED,
			        closable_others ? RG_GUI_MENU_ITEM_NONE : RG_GUI_MENU_ITEM_DISABLED,
			        (closable_count > 0) ? RG_GUI_MENU_ITEM_NONE : RG_GUI_MENU_ITEM_DISABLED,
			        menu_dockable ? RG_GUI_MENU_ITEM_NONE : RG_GUI_MENU_ITEM_DISABLED};

			if (dockspace->tab_popup_selected < 0)
			{
				int first = rg_gui_menu_find_enabled(menu_item_flags, TAB_MENU_COUNT, 0, 1);
				dockspace->tab_popup_selected = (first >= 0) ? first : -1;
			}

			RgGuiRect menu_rect = rg_gui_make_rect(dockspace->tab_popup.pos.x,
			                                       dockspace->tab_popup.pos.y,
			                                       160.0f,
			                                       0.0f);
			int menu_changed = rg_gui_menu_popup_ex(ctx, menu_items, NULL, menu_item_flags, TAB_MENU_COUNT,
			                                        &dockspace->tab_popup_selected, &dockspace->tab_popup.open,
			                                        &dockspace->tab_popup_scroll, menu_rect,
			                                        rg_gui_id_combine(dockspace->id, 0x5441424Du),
			                                        NULL, NULL, NULL, NULL, 1);
			if (menu_changed && !dockspace->tab_popup.open &&
			    dockspace->tab_popup_selected >= 0 &&
			    dockspace->tab_popup_selected < TAB_MENU_COUNT)
			{
				int close_current = 0;
				int action = dockspace->tab_popup_selected;
				if (action == TAB_MENU_CLOSE)
				{
					if (menu_closable)
					{
						if (menu_tab == tab_index)
						{
							close_current = 1;
						}
						rg_gui_dock_remove_tab(ctx, dockspace, menu_node, menu_tab, 1);
					}
				}
				else if (action == TAB_MENU_CLOSE_OTHERS)
				{
					u32 close_iter = ctx->dock_nodes[menu_node].tab_head;
					while (close_iter != 0u)
					{
						u32 next = ctx->dock_tabs[close_iter].next;
						if (close_iter != menu_tab &&
						    (ctx->dock_tabs[close_iter].flags & RG_GUI_WINDOW_CLOSABLE) != 0u)
						{
							if (close_iter == tab_index)
							{
								close_current = 1;
							}
							rg_gui_dock_remove_tab(ctx, dockspace, menu_node, close_iter, 1);
						}
						close_iter = next;
					}
				}
				else if (action == TAB_MENU_CLOSE_ALL)
				{
					u32 close_iter = ctx->dock_nodes[menu_node].tab_head;
					while (close_iter != 0u)
					{
						u32 next = ctx->dock_tabs[close_iter].next;
						if ((ctx->dock_tabs[close_iter].flags & RG_GUI_WINDOW_CLOSABLE) != 0u)
						{
							if (close_iter == tab_index)
							{
								close_current = 1;
							}
							rg_gui_dock_remove_tab(ctx, dockspace, menu_node, close_iter, 1);
						}
						close_iter = next;
					}
				}
				else if (action == TAB_MENU_UNDOCK)
				{
					if (menu_dockable && menu_target->window)
					{
						menu_target->window->rect = ctx->dock_nodes[menu_node].rect;
						menu_target->window->start_rect = menu_target->window->rect;
						menu_target->window->start_mouse = ctx->mouse_pos;
						rg_gui_dock_detach_window(ctx, dockspace, menu_target->window);
						if (menu_tab == tab_index)
						{
							if (out_fallback)
							{
								*out_fallback = 1;
							}
							return 0;
						}
					}
				}

				if (close_current)
				{
					return 0;
				}
			}
		}
	}

	if (switched || node->active_tab != tab_index)
	{
		return 0;
	}

	f32 border = ctx->style.border_thickness;
	f32 inset = border + pad;
	f32 layout_spacing = spacing;
	if (layout_spacing < 0.0f)
	{
		layout_spacing = 0.0f;
	}

	RgGuiRect inner_rect = win_rect;
	inner_rect.x += inset;
	inner_rect.y += tab_h + inset;
	inner_rect.w -= inset * 2.0f;
	inner_rect.h -= tab_h + inset * 2.0f;
	if (inner_rect.w < 0.0f) inner_rect.w = 0.0f;
	if (inner_rect.h < 0.0f) inner_rect.h = 0.0f;

	ctx->layout.x = inner_rect.x;
	ctx->layout.y = inner_rect.y;
	ctx->layout.width = inner_rect.w;
	ctx->layout.spacing = layout_spacing;
	ctx->layout.cursor_y = inner_rect.y;
	ctx->layout.active = 1;
	rg_gui_layout_reset_row(&ctx->layout);

	rg_gui_push_clip(ctx, inner_rect);
	return 1;
}

RGINLINE u32 rg_gui_dock_node_alloc(RgGuiContext* ctx)
{
	if (!ctx || !ctx->dock_nodes || ctx->dock_node_capacity == 0u)
	{
		return 0u;
	}

	u32 index = ctx->dock_node_free;
	if (index != 0u)
	{
		ctx->dock_node_free = ctx->dock_nodes[index].next_free;
	}
	else
	{
		if (ctx->dock_node_count >= ctx->dock_node_capacity)
		{
			return 0u;
		}
		index = ++ctx->dock_node_count;
	}

	memset(&ctx->dock_nodes[index], 0, sizeof(RgGuiDockNode));
	return index;
}

RGINLINE void rg_gui_dock_node_free(RgGuiContext* ctx, u32 index)
{
	if (!ctx || index == 0u || index > ctx->dock_node_capacity)
	{
		return;
	}

	ctx->dock_nodes[index].next_free = ctx->dock_node_free;
	ctx->dock_node_free = index;
}

RGINLINE u32 rg_gui_dock_tab_alloc(RgGuiContext* ctx)
{
	if (!ctx || !ctx->dock_tabs || ctx->dock_tab_capacity == 0u)
	{
		return 0u;
	}

	u32 index = ctx->dock_tab_free;
	if (index != 0u)
	{
		ctx->dock_tab_free = ctx->dock_tabs[index].next_free;
	}
	else
	{
		if (ctx->dock_tab_count >= ctx->dock_tab_capacity)
		{
			return 0u;
		}
		index = ++ctx->dock_tab_count;
	}

	memset(&ctx->dock_tabs[index], 0, sizeof(RgGuiDockTab));
	return index;
}

RGINLINE void rg_gui_dock_tab_free(RgGuiContext* ctx, u32 index)
{
	if (!ctx || index == 0u || index > ctx->dock_tab_capacity)
	{
		return;
	}

	ctx->dock_tabs[index].next_free = ctx->dock_tab_free;
	ctx->dock_tab_free = index;
}

RGINLINE RgGuiDockSpaceState* rg_gui_dockspace_find(RgGuiContext* ctx, RgGuiId id)
{
	if (!ctx || !ctx->dockspaces)
	{
		return NULL;
	}

	for (u32 i = 0u; i < ctx->dockspace_count; i++)
	{
		RgGuiDockSpaceState* dockspace = ctx->dockspaces[i];
		if (dockspace && dockspace->id == id)
		{
			return dockspace;
		}
	}

	return NULL;
}

RGINLINE f32 rg_gui_dock_zone_size(RgGuiRect rect)
{
	f32 min_side = rect.w < rect.h ? rect.w : rect.h;
	f32 size = min_side * (f32)RG_GUI_DOCK_ZONE_FRACTION;
	if (size < (f32)RG_GUI_DOCK_ZONE_MIN)
	{
		size = (f32)RG_GUI_DOCK_ZONE_MIN;
	}
	if (size > (f32)RG_GUI_DOCK_ZONE_MAX)
	{
		size = (f32)RG_GUI_DOCK_ZONE_MAX;
	}

	f32 max_size = min_side * 0.5f;
	if (size > max_size)
	{
		size = max_size;
	}
	if (size < 0.0f)
	{
		size = 0.0f;
	}
	return size;
}

RGINLINE f32 rg_gui_dock_clamp_ratio(f32 ratio, f32 total)
{
	if (ratio <= 0.0f || ratio >= 1.0f)
	{
		ratio = 0.5f;
	}

	f32 min_size = (f32)RG_GUI_DOCK_SPLIT_MIN;
	if (min_size < 0.0f)
	{
		min_size = 0.0f;
	}

	if (total > 0.0f)
	{
		f32 min_ratio = min_size / total;
		if (min_ratio > 0.5f)
		{
			min_ratio = 0.5f;
		}
		f32 max_ratio = 1.0f - min_ratio;
		if (ratio < min_ratio)
		{
			ratio = min_ratio;
		}
		if (ratio > max_ratio)
		{
			ratio = max_ratio;
		}
	}

	return ratio;
}

RGINLINE RgGuiRect rg_gui_dock_preview_rect(RgGuiRect rect, RgGuiDockSlot slot)
{
	RgGuiRect preview = rect;
	if (slot == RG_GUI_DOCK_SLOT_LEFT || slot == RG_GUI_DOCK_SLOT_RIGHT)
	{
		f32 ratio = rg_gui_dock_clamp_ratio(0.5f, rect.w);
		f32 width = rect.w * ratio;
		if (width < 0.0f)
		{
			width = 0.0f;
		}
		preview.w = width;
		if (slot == RG_GUI_DOCK_SLOT_RIGHT)
		{
			preview.x = rect.x + rect.w - width;
		}
	}
	else if (slot == RG_GUI_DOCK_SLOT_TOP || slot == RG_GUI_DOCK_SLOT_BOTTOM)
	{
		f32 ratio = rg_gui_dock_clamp_ratio(0.5f, rect.h);
		f32 height = rect.h * ratio;
		if (height < 0.0f)
		{
			height = 0.0f;
		}
		preview.h = height;
		if (slot == RG_GUI_DOCK_SLOT_BOTTOM)
		{
			preview.y = rect.y + rect.h - height;
		}
	}

	return preview;
}

RGINLINE int rg_gui_rects_overlap(RgGuiRect a, RgGuiRect b)
{
	f32 a_right = a.x + a.w;
	f32 a_bottom = a.y + a.h;
	f32 b_right = b.x + b.w;
	f32 b_bottom = b.y + b.h;
	if (a_right <= b.x || b_right <= a.x)
	{
		return 0;
	}
	if (a_bottom <= b.y || b_bottom <= a.y)
	{
		return 0;
	}
	return 1;
}

RGINLINE void rg_gui_dock_draw_ghost(RgGuiContext* ctx, RgGuiDrawList* list, RgGuiRect rect, const char* title)
{
	if (!ctx || !list)
	{
		return;
	}

	rg_vec4 ghost_fill = ctx->style.color_bg_active;
	ghost_fill.w *= 0.28f;
	rg_vec4 ghost_border = ctx->style.color_accent;
	ghost_border.w *= 0.7f;
	rg_gui_push_rect_to(ctx, list, rect, ghost_fill);
	rg_gui_push_rect_outline_to(ctx, list, rect, ghost_border, ctx->style.border_thickness);

	if (title && *title && rect.w > 0.0f && rect.h > 0.0f)
	{
		f32 pad = ctx->style.padding;
		f32 title_h = ctx->style.text_height + pad * 2.0f;
		if (title_h < 1.0f)
		{
			title_h = 1.0f;
		}
		if (title_h > rect.h)
		{
			title_h = rect.h;
		}

		if (title_h > 0.0f)
		{
			RgGuiRect title_rect = rg_gui_make_rect(rect.x, rect.y, rect.w, title_h);
			rg_vec4 title_bg = ctx->style.color_bg;
			title_bg.w *= 0.45f;
			rg_gui_push_rect_to(ctx, list, title_rect, title_bg);

			rg_vec2 pos = rg_vec2(rect.x + pad,
			                      rect.y + (title_h - ctx->style.text_height) * 0.5f);
			rg_vec4 text_color = ctx->style.color_text;
			text_color.w *= 0.9f;
			if (RG_GUI_LABEL_COPY)
			{
				rg_gui_push_text_to(ctx, list, title, pos, text_color);
			}
			else
			{
				rg_gui_push_text_static_to(ctx, list, title, pos, text_color);
			}
		}
	}
}

RGINLINE RgGuiDrawList* rg_gui_dock_overlay_list(RgGuiContext* ctx, const RgGuiDockSpaceState* dockspace)
{
	if (!ctx || !dockspace)
	{
		return NULL;
	}

#if defined(RG_GUI_ENABLE_VIEWPORTS)
	if (dockspace->viewport_id != 0u)
	{
		RgGuiViewport* viewport = rg_gui_viewport_find(ctx, dockspace->viewport_id);
		if (viewport)
		{
			return &viewport->draw_list;
		}
		return NULL;
	}
#endif

	return rg_gui_overlay_target(ctx);
}

RGINLINE int rg_gui_dock_draw_target_for_point(RgGuiContext* ctx, rg_vec2 point_global,
                                               RgGuiDrawList** out_list, rg_vec2* out_origin)
{
	if (!ctx || !out_list || !out_origin)
	{
		return 0;
	}

#if defined(RG_GUI_ENABLE_VIEWPORTS)
	for (u32 i = 0u; i < ctx->viewport_active_count; i++)
	{
		RgGuiViewport* viewport = ctx->viewport_active[i];
		if (!viewport)
		{
			continue;
		}

		RgGuiRect bounds = viewport->bounds;
		bounds.x += viewport->origin.x;
		bounds.y += viewport->origin.y;
		if (rg_gui_point_in_rect(point_global.x, point_global.y, bounds))
		{
			*out_list = &viewport->draw_list;
			*out_origin = viewport->origin;
			return 1;
		}
	}
#endif

	if (ctx->window_bounds_set)
	{
		RgGuiRect bounds = ctx->window_bounds;
		bounds.x += ctx->viewport_origin.x;
		bounds.y += ctx->viewport_origin.y;
		if (rg_gui_point_in_rect(point_global.x, point_global.y, bounds))
		{
			*out_list = rg_gui_overlay_target(ctx);
			*out_origin = ctx->viewport_origin;
			return 1;
		}
	}

	return 0;
}

RGINLINE void rg_gui_dock_layout(RgGuiContext* ctx, u32 node_index, RgGuiRect rect)
{
	if (!ctx || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	node->rect = rect;

	if (node->split == RG_GUI_DOCK_SPLIT_NONE || node->child_a == 0u || node->child_b == 0u)
	{
		return;
	}

	if (node->split == RG_GUI_DOCK_SPLIT_VERT)
	{
		f32 ratio = rg_gui_dock_clamp_ratio(node->split_ratio, rect.w);
		node->split_ratio = ratio;
		f32 split = rect.w * ratio;
		if (split < 0.0f)
		{
			split = 0.0f;
		}
		if (split > rect.w)
		{
			split = rect.w;
		}

		RgGuiRect left = rg_gui_make_rect(rect.x, rect.y, split, rect.h);
		RgGuiRect right = rg_gui_make_rect(rect.x + split, rect.y, rect.w - split, rect.h);
		rg_gui_dock_layout(ctx, node->child_a, left);
		rg_gui_dock_layout(ctx, node->child_b, right);
	}
	else if (node->split == RG_GUI_DOCK_SPLIT_HORZ)
	{
		f32 ratio = rg_gui_dock_clamp_ratio(node->split_ratio, rect.h);
		node->split_ratio = ratio;
		f32 split = rect.h * ratio;
		if (split < 0.0f)
		{
			split = 0.0f;
		}
		if (split > rect.h)
		{
			split = rect.h;
		}

		RgGuiRect top = rg_gui_make_rect(rect.x, rect.y, rect.w, split);
		RgGuiRect bottom = rg_gui_make_rect(rect.x, rect.y + split, rect.w, rect.h - split);
		rg_gui_dock_layout(ctx, node->child_a, top);
		rg_gui_dock_layout(ctx, node->child_b, bottom);
	}
}

RGINLINE int rg_gui_dock_handle_splitter(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, u32 node_index)
{
	if (!ctx || !dockspace || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return 0;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	if (node->split == RG_GUI_DOCK_SPLIT_NONE || node->child_a == 0u || node->child_b == 0u)
	{
		return 0;
	}

	RgGuiRect rect = node->rect;
	if (rect.w <= 0.0f || rect.h <= 0.0f)
	{
		return 0;
	}

	f32 thickness = (f32)RG_GUI_DOCK_SPLITTER_THICKNESS;
	if (thickness < 1.0f)
	{
		thickness = 1.0f;
	}

	f32 min_size = (f32)RG_GUI_DOCK_SPLIT_MIN;
	if (min_size < 0.0f)
	{
		min_size = 0.0f;
	}

	int enabled = !rg_gui_is_disabled(ctx);
	int changed = 0;
	RgGuiDrawList* overlay = rg_gui_overlay_target(ctx);

	if (node->split == RG_GUI_DOCK_SPLIT_VERT)
	{
		f32 size = rect.w * node->split_ratio;
		f32 max_size = rect.w - min_size;
		if (max_size < min_size)
		{
			max_size = min_size;
		}

		RgGuiId split_id = rg_gui_id_combine(dockspace->id, ((u64)node_index << 1u) | 1u);

		f32 split_x = rect.x + size;
		f32 half = thickness * 0.5f;
		RgGuiRect splitter = rg_gui_make_rect(split_x - half, rect.y, thickness, rect.h);
		if (splitter.w > rect.w)
		{
			splitter.w = rect.w;
			splitter.x = rect.x;
		}
		if (splitter.x < rect.x)
		{
			splitter.x = rect.x;
		}
		f32 max_x = rect.x + rect.w - splitter.w;
		if (splitter.x > max_x)
		{
			splitter.x = max_x;
		}

		int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, splitter);
		if (hovered)
		{
			ctx->hot_id = split_id;
		}
		if (enabled && hovered && ctx->mouse_pressed)
		{
			ctx->active_id = split_id;
		}

		if (!enabled && ctx->active_id == split_id)
		{
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == split_id)
		{
			if (ctx->mouse_down && ctx->input)
			{
				f32 delta = (f32)ctx->input->mouse_delta_x;
				if (delta != 0.0f)
				{
					f32 next = size + delta;
					if (max_size < min_size)
					{
						max_size = min_size;
					}
					if (next < min_size)
					{
						next = min_size;
					}
					if (next > max_size)
					{
						next = max_size;
					}
					if (next != size)
					{
						size = next;
						changed = 1;
					}
				}
			}
			if (ctx->mouse_released)
			{
				ctx->active_id = 0u;
			}
		}

		if (rect.w > 0.0f)
		{
			node->split_ratio = rg_gui_dock_clamp_ratio(size / rect.w, rect.w);
			size = rect.w * node->split_ratio;
		}

		split_x = rect.x + size;
		splitter.x = split_x - half;
		if (splitter.w > rect.w)
		{
			splitter.w = rect.w;
			splitter.x = rect.x;
		}
		if (splitter.x < rect.x)
		{
			splitter.x = rect.x;
		}
		max_x = rect.x + rect.w - splitter.w;
		if (splitter.x > max_x)
		{
			splitter.x = max_x;
		}

		rg_vec4 color = ctx->style.color_border;
		if (enabled && ctx->active_id == split_id)
		{
			color = ctx->style.color_accent;
		}
		else if (enabled && hovered)
		{
			color = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect_to(ctx, overlay, splitter, color);

		if (ctx->active_id == split_id || ctx->hot_id == split_id)
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
		}
	}
	else if (node->split == RG_GUI_DOCK_SPLIT_HORZ)
	{
		f32 size = rect.h * node->split_ratio;
		f32 max_size = rect.h - min_size;
		if (max_size < min_size)
		{
			max_size = min_size;
		}

		RgGuiId split_id = rg_gui_id_combine(dockspace->id, ((u64)node_index << 1u) | 2u);

		f32 split_y = rect.y + size;
		f32 half = thickness * 0.5f;
		RgGuiRect splitter = rg_gui_make_rect(rect.x, split_y - half, rect.w, thickness);
		if (splitter.h > rect.h)
		{
			splitter.h = rect.h;
			splitter.y = rect.y;
		}
		if (splitter.y < rect.y)
		{
			splitter.y = rect.y;
		}
		f32 max_y = rect.y + rect.h - splitter.h;
		if (splitter.y > max_y)
		{
			splitter.y = max_y;
		}

		int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, splitter);
		if (hovered)
		{
			ctx->hot_id = split_id;
		}
		if (enabled && hovered && ctx->mouse_pressed)
		{
			ctx->active_id = split_id;
		}

		if (!enabled && ctx->active_id == split_id)
		{
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == split_id)
		{
			if (ctx->mouse_down && ctx->input)
			{
				f32 delta = (f32)ctx->input->mouse_delta_y;
				if (delta != 0.0f)
				{
					f32 next = size + delta;
					if (max_size < min_size)
					{
						max_size = min_size;
					}
					if (next < min_size)
					{
						next = min_size;
					}
					if (next > max_size)
					{
						next = max_size;
					}
					if (next != size)
					{
						size = next;
						changed = 1;
					}
				}
			}
			if (ctx->mouse_released)
			{
				ctx->active_id = 0u;
			}
		}

		if (rect.h > 0.0f)
		{
			node->split_ratio = rg_gui_dock_clamp_ratio(size / rect.h, rect.h);
			size = rect.h * node->split_ratio;
		}

		split_y = rect.y + size;
		splitter.y = split_y - half;
		if (splitter.h > rect.h)
		{
			splitter.h = rect.h;
			splitter.y = rect.y;
		}
		if (splitter.y < rect.y)
		{
			splitter.y = rect.y;
		}
		max_y = rect.y + rect.h - splitter.h;
		if (splitter.y > max_y)
		{
			splitter.y = max_y;
		}

		rg_vec4 color = ctx->style.color_border;
		if (enabled && ctx->active_id == split_id)
		{
			color = ctx->style.color_accent;
		}
		else if (enabled && hovered)
		{
			color = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect_to(ctx, overlay, splitter, color);

		if (ctx->active_id == split_id || ctx->hot_id == split_id)
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_V;
		}
	}

	return changed;
}

RGINLINE int rg_gui_dock_handle_splitters(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, u32 node_index)
{
	if (!ctx || !dockspace || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return 0;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	if (node->split == RG_GUI_DOCK_SPLIT_NONE || node->child_a == 0u || node->child_b == 0u)
	{
		return 0;
	}

	int changed = rg_gui_dock_handle_splitter(ctx, dockspace, node_index);
	changed |= rg_gui_dock_handle_splitters(ctx, dockspace, node->child_a);
	changed |= rg_gui_dock_handle_splitters(ctx, dockspace, node->child_b);
	return changed;
}

RGINLINE u32 rg_gui_dock_find_leaf_at(RgGuiContext* ctx, u32 node_index, rg_vec2 pos)
{
	if (!ctx || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return 0u;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	if (node->split == RG_GUI_DOCK_SPLIT_NONE)
	{
		return rg_gui_point_in_rect(pos.x, pos.y, node->rect) ? node_index : 0u;
	}

	u32 hit = rg_gui_dock_find_leaf_at(ctx, node->child_a, pos);
	if (hit != 0u)
	{
		return hit;
	}
	return rg_gui_dock_find_leaf_at(ctx, node->child_b, pos);
}

RGINLINE u32 rg_gui_dock_find_leaf_first(RgGuiContext* ctx, u32 node_index)
{
	if (!ctx || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return 0u;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	if (node->split == RG_GUI_DOCK_SPLIT_NONE)
	{
		return node_index;
	}

	u32 leaf = rg_gui_dock_find_leaf_first(ctx, node->child_a);
	if (leaf != 0u)
	{
		return leaf;
	}
	return rg_gui_dock_find_leaf_first(ctx, node->child_b);
}

RGINLINE u32 rg_gui_dock_find_tab_by_id(RgGuiContext* ctx, u32 node_index, RgGuiId window_id, u32* out_node)
{
	if (!ctx || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return 0u;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	if (node->split == RG_GUI_DOCK_SPLIT_NONE)
	{
		u32 tab = node->tab_head;
		while (tab != 0u)
		{
			if (ctx->dock_tabs[tab].window_id == window_id)
			{
				if (out_node)
				{
					*out_node = node_index;
				}
				return tab;
			}
			tab = ctx->dock_tabs[tab].next;
		}
		return 0u;
	}

	u32 found = rg_gui_dock_find_tab_by_id(ctx, node->child_a, window_id, out_node);
	if (found != 0u)
	{
		return found;
	}
	return rg_gui_dock_find_tab_by_id(ctx, node->child_b, window_id, out_node);
}

RGINLINE void rg_gui_dock_prune_empty(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, u32 node_index)
{
	if (!ctx || !dockspace || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	if (node->split != RG_GUI_DOCK_SPLIT_NONE || node->tab_count != 0u)
	{
		return;
	}

	u32 parent_index = node->parent;
	if (parent_index == 0u)
	{
		return;
	}

	RgGuiDockNode* parent = &ctx->dock_nodes[parent_index];
	u32 sibling = (parent->child_a == node_index) ? parent->child_b : parent->child_a;

	u32 grand = parent->parent;
	if (grand == 0u)
	{
		dockspace->root = sibling;
		if (sibling != 0u)
		{
			ctx->dock_nodes[sibling].parent = 0u;
		}
	}
	else
	{
		RgGuiDockNode* grand_node = &ctx->dock_nodes[grand];
		if (grand_node->child_a == parent_index)
		{
			grand_node->child_a = sibling;
		}
		else if (grand_node->child_b == parent_index)
		{
			grand_node->child_b = sibling;
		}
		if (sibling != 0u)
		{
			ctx->dock_nodes[sibling].parent = grand;
		}
	}

	rg_gui_dock_node_free(ctx, parent_index);
	rg_gui_dock_node_free(ctx, node_index);
}

RGINLINE u32 rg_gui_dock_add_tab(RgGuiContext* ctx, u32 node_index, RgGuiWindowState* window,
                                 const char* title, RgGuiId window_id)
{
	if (!ctx || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return 0u;
	}

	u32 tab_index = rg_gui_dock_tab_alloc(ctx);
	if (tab_index == 0u)
	{
		return 0u;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	RgGuiDockTab* tab = &ctx->dock_tabs[tab_index];
	tab->window_id = window_id;
	tab->window = window;
	tab->title = title;
	tab->next = 0u;

	if (node->tab_head == 0u)
	{
		node->tab_head = tab_index;
	}
	else
	{
		u32 iter = node->tab_head;
		while (ctx->dock_tabs[iter].next != 0u)
		{
			iter = ctx->dock_tabs[iter].next;
		}
		ctx->dock_tabs[iter].next = tab_index;
	}

	node->tab_count++;
	node->active_tab = tab_index;
	return tab_index;
}

RGINLINE void rg_gui_dock_remove_tab(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace,
                                     u32 node_index, u32 tab_index, int close_window)
{
	if (!ctx || !dockspace || node_index == 0u || tab_index == 0u ||
	    node_index > ctx->dock_node_capacity || tab_index > ctx->dock_tab_capacity)
	{
		return;
	}

	RgGuiDockNode* node = &ctx->dock_nodes[node_index];
	u32 prev = 0u;
	u32 iter = node->tab_head;
	while (iter != 0u)
	{
		if (iter == tab_index)
		{
			break;
		}
		prev = iter;
		iter = ctx->dock_tabs[iter].next;
	}

	if (iter == 0u)
	{
		return;
	}

	if (prev == 0u)
	{
		node->tab_head = ctx->dock_tabs[iter].next;
	}
	else
	{
		ctx->dock_tabs[prev].next = ctx->dock_tabs[iter].next;
	}

	if (node->active_tab == tab_index)
	{
		node->active_tab = node->tab_head;
	}

	if (node->tab_count > 0u)
	{
		node->tab_count--;
	}

	if (ctx->dock_tabs[iter].window)
	{
		RgGuiWindowState* removed = ctx->dock_tabs[iter].window;
		int preserve_host = (!close_window && removed->open);
		removed->docked = 0;
		if (!preserve_host)
		{
			removed->dockspace_id = 0u;
		}
		removed->dock_node = 0u;
		removed->dock_tab = 0u;
		removed->dock_drag_pending = 0;
		if (close_window)
		{
			removed->open = 0;
		}
	}

	rg_gui_dock_tab_free(ctx, iter);
	rg_gui_dock_prune_empty(ctx, dockspace, node_index);
}

RGINLINE u32 rg_gui_dock_attach_window(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, u32 node_index,
                                       RgGuiWindowState* window, const char* title, RgGuiId window_id)
{
	if (!ctx || !dockspace || !window || node_index == 0u || node_index > ctx->dock_node_capacity)
	{
		return 0u;
	}

	u32 tab_index = rg_gui_dock_add_tab(ctx, node_index, window, title, window_id);
	if (tab_index == 0u)
	{
		return 0u;
	}

	window->docked = 1;
	window->dockspace_id = dockspace->id;
	window->dock_node = node_index;
	window->dock_tab = tab_index;
	window->dock_drag_pending = 0;
	window->open = 1;
	return tab_index;
}

RGINLINE void rg_gui_dock_detach_window(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, RgGuiWindowState* window)
{
	if (!ctx || !dockspace || !window || !window->docked)
	{
		return;
	}

	if (window->dock_node != 0u && window->dock_tab != 0u)
	{
		rg_gui_dock_remove_tab(ctx, dockspace, window->dock_node, window->dock_tab, 0);
	}
	else
	{
		window->docked = 0;
		window->dockspace_id = 0u;
		window->dock_node = 0u;
		window->dock_tab = 0u;
		window->dock_drag_pending = 0;
	}
}

RGINLINE int rg_gui_dock_attach_slot(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, u32 target_node,
                                     RgGuiWindowState* window, const char* title, RgGuiId window_id, RgGuiDockSlot slot)
{
	if (!ctx || !dockspace || !window || target_node == 0u || target_node > ctx->dock_node_capacity)
	{
		return 0;
	}

	RgGuiDockNode* target = &ctx->dock_nodes[target_node];
	if (target->split != RG_GUI_DOCK_SPLIT_NONE)
	{
		target_node = rg_gui_dock_find_leaf_first(ctx, target_node);
		if (target_node == 0u)
		{
			return 0;
		}
		target = &ctx->dock_nodes[target_node];
	}

	if (slot == RG_GUI_DOCK_SLOT_CENTER)
	{
		return rg_gui_dock_attach_window(ctx, dockspace, target_node, window, title, window_id) != 0u;
	}

	u32 new_leaf = rg_gui_dock_node_alloc(ctx);
	u32 split_node = rg_gui_dock_node_alloc(ctx);
	if (new_leaf == 0u || split_node == 0u)
	{
		if (new_leaf != 0u)
		{
			rg_gui_dock_node_free(ctx, new_leaf);
		}
		if (split_node != 0u)
		{
			rg_gui_dock_node_free(ctx, split_node);
		}
		return 0;
	}

	RgGuiDockNode* leaf = &ctx->dock_nodes[new_leaf];
	leaf->split = RG_GUI_DOCK_SPLIT_NONE;
	leaf->tab_head = 0u;
	leaf->tab_count = 0u;
	leaf->active_tab = 0u;

	RgGuiDockNode* split = &ctx->dock_nodes[split_node];
	split->split_ratio = 0.5f;
	split->tab_head = 0u;
	split->tab_count = 0u;
	split->active_tab = 0u;

	if (slot == RG_GUI_DOCK_SLOT_LEFT || slot == RG_GUI_DOCK_SLOT_RIGHT)
	{
		split->split = RG_GUI_DOCK_SPLIT_VERT;
	}
	else
	{
		split->split = RG_GUI_DOCK_SPLIT_HORZ;
	}

	if (split->split == RG_GUI_DOCK_SPLIT_VERT && target->rect.w > 0.0f && window->rect.w > 0.0f)
	{
		f32 left_size = window->rect.w;
		if (slot == RG_GUI_DOCK_SLOT_RIGHT)
		{
			left_size = target->rect.w - window->rect.w;
		}
		if (left_size < 0.0f)
		{
			left_size = 0.0f;
		}
		if (left_size > target->rect.w)
		{
			left_size = target->rect.w;
		}
		split->split_ratio = rg_gui_dock_clamp_ratio(left_size / target->rect.w, target->rect.w);
	}
	else if (split->split == RG_GUI_DOCK_SPLIT_HORZ && target->rect.h > 0.0f && window->rect.h > 0.0f)
	{
		f32 top_size = window->rect.h;
		if (slot == RG_GUI_DOCK_SLOT_BOTTOM)
		{
			top_size = target->rect.h - window->rect.h;
		}
		if (top_size < 0.0f)
		{
			top_size = 0.0f;
		}
		if (top_size > target->rect.h)
		{
			top_size = target->rect.h;
		}
		split->split_ratio = rg_gui_dock_clamp_ratio(top_size / target->rect.h, target->rect.h);
	}

	if (slot == RG_GUI_DOCK_SLOT_LEFT || slot == RG_GUI_DOCK_SLOT_TOP)
	{
		split->child_a = new_leaf;
		split->child_b = target_node;
	}
	else
	{
		split->child_a = target_node;
		split->child_b = new_leaf;
	}

	split->parent = target->parent;
	target->parent = split_node;
	leaf->parent = split_node;

	if (split->parent == 0u)
	{
		dockspace->root = split_node;
	}
	else
	{
		RgGuiDockNode* parent = &ctx->dock_nodes[split->parent];
		if (parent->child_a == target_node)
		{
			parent->child_a = split_node;
		}
		else if (parent->child_b == target_node)
		{
			parent->child_b = split_node;
		}
	}

	return rg_gui_dock_attach_window(ctx, dockspace, new_leaf, window, title, window_id) != 0u;
}

RGINLINE void rg_gui_dock_drag_handle(RgGuiContext* ctx)
{
	if (!ctx || !ctx->dock_drag.active || !ctx->dock_drag.window)
	{
		return;
	}

	RgGuiDockDragState* drag = &ctx->dock_drag;
	int dockable = (drag->flags & RG_GUI_WINDOW_DOCKABLE) != 0u;

	RgGuiDockSpaceState* drop_space = NULL;
	u32 drop_node = 0u;
	RgGuiDockSlot drop_slot = RG_GUI_DOCK_SLOT_CENTER;
	RgGuiRect drop_rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	RgGuiRect left = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	RgGuiRect right = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	RgGuiRect top = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	RgGuiRect bottom = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	RgGuiRect center = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);

#if defined(RG_GUI_ENABLE_VIEWPORTS)
	RgGuiId mouse_focus_viewport = 0u;
	int mouse_focus_valid = 0;

	if (ctx->mouse_focus_inside && ctx->mouse_focus_viewport != 0u)
	{
		mouse_focus_viewport = ctx->mouse_focus_viewport;
		mouse_focus_valid = 1;
	}
#endif

	if (dockable && ctx->dockspace_count > 0u)
	{
		for (u32 i = 0u; i < ctx->dockspace_count; i++)
		{
			RgGuiDockSpaceState* dockspace = ctx->dockspaces[i];
			if (!dockspace || dockspace->root == 0u)
			{
				continue;
			}

			rg_vec2 mouse_local = rg_vec2(drag->mouse_global.x - dockspace->origin.x,
			                              drag->mouse_global.y - dockspace->origin.y);
			if (!rg_gui_point_in_rect(mouse_local.x, mouse_local.y, dockspace->rect))
			{
				continue;
			}

			u32 leaf = rg_gui_dock_find_leaf_at(ctx, dockspace->root, mouse_local);
			if (leaf == 0u)
			{
				leaf = dockspace->root;
			}
			if (leaf == 0u)
			{
				continue;
			}

			RgGuiRect candidate_rect = ctx->dock_nodes[leaf].rect;
			f32 zone = rg_gui_dock_zone_size(candidate_rect);
			if (zone <= 0.0f)
			{
				continue;
			}

			f32 center_w = candidate_rect.w - zone * 2.0f;
			f32 center_h = candidate_rect.h - zone * 2.0f;
			if (center_w < 0.0f) center_w = 0.0f;
			if (center_h < 0.0f) center_h = 0.0f;

			RgGuiRect candidate_left = rg_gui_make_rect(candidate_rect.x, candidate_rect.y,
			                                            zone, candidate_rect.h);
			RgGuiRect candidate_right = rg_gui_make_rect(candidate_rect.x + candidate_rect.w - zone,
			                                             candidate_rect.y, zone, candidate_rect.h);
			RgGuiRect candidate_top = rg_gui_make_rect(candidate_rect.x, candidate_rect.y,
			                                           candidate_rect.w, zone);
			RgGuiRect candidate_bottom = rg_gui_make_rect(candidate_rect.x,
			                                              candidate_rect.y + candidate_rect.h - zone,
			                                              candidate_rect.w, zone);
			RgGuiRect candidate_center = rg_gui_make_rect(candidate_rect.x + zone, candidate_rect.y + zone,
			                                              center_w, center_h);

			RgGuiDockSlot candidate_slot = RG_GUI_DOCK_SLOT_CENTER;

			if (candidate_center.w > 0.0f && candidate_center.h > 0.0f &&
			    rg_gui_point_in_rect(mouse_local.x, mouse_local.y, candidate_center))
			{
				candidate_slot = RG_GUI_DOCK_SLOT_CENTER;
			}
			else
			{
				f32 dist_left = mouse_local.x - candidate_rect.x;
				f32 dist_right = (candidate_rect.x + candidate_rect.w) - mouse_local.x;
				f32 dist_top = mouse_local.y - candidate_rect.y;
				f32 dist_bottom = (candidate_rect.y + candidate_rect.h) - mouse_local.y;

				f32 min_dist = dist_left;
				candidate_slot = RG_GUI_DOCK_SLOT_LEFT;
				if (dist_right < min_dist)
				{
					min_dist = dist_right;
					candidate_slot = RG_GUI_DOCK_SLOT_RIGHT;
				}
				if (dist_top < min_dist)
				{
					min_dist = dist_top;
					candidate_slot = RG_GUI_DOCK_SLOT_TOP;
				}
				if (dist_bottom < min_dist)
				{
					min_dist = dist_bottom;
					candidate_slot = RG_GUI_DOCK_SLOT_BOTTOM;
				}

				if (min_dist > zone)
				{
					candidate_slot = RG_GUI_DOCK_SLOT_CENTER;
				}
			}

			int prefer = 0;
#if defined(RG_GUI_ENABLE_VIEWPORTS)
			if (mouse_focus_valid && dockspace->viewport_id == mouse_focus_viewport)
			{
				prefer = 1;
			}
#endif

			if (!drop_space || prefer)
			{
				drop_space = dockspace;
				drop_node = leaf;
				drop_slot = candidate_slot;
				drop_rect = candidate_rect;
				left = candidate_left;
				right = candidate_right;
				top = candidate_top;
				bottom = candidate_bottom;
				center = candidate_center;
				if (prefer)
				{
					break;
				}
			}
		}
	}

	if (drop_space)
	{
		RgGuiDrawList* overlay = rg_gui_dock_overlay_list(ctx, drop_space);
		if (overlay)
		{
			rg_vec4 base = ctx->style.color_accent;
			rg_vec4 hover = ctx->style.color_accent;
			base.w *= 0.18f;
			hover.w *= 0.35f;

			rg_gui_push_rect_to(ctx, overlay, left,
			                    drop_slot == RG_GUI_DOCK_SLOT_LEFT ? hover : base);
			rg_gui_push_rect_to(ctx, overlay, right,
			                    drop_slot == RG_GUI_DOCK_SLOT_RIGHT ? hover : base);
			rg_gui_push_rect_to(ctx, overlay, top,
			                    drop_slot == RG_GUI_DOCK_SLOT_TOP ? hover : base);
			rg_gui_push_rect_to(ctx, overlay, bottom,
			                    drop_slot == RG_GUI_DOCK_SLOT_BOTTOM ? hover : base);
			rg_gui_push_rect_to(ctx, overlay, center,
			                    drop_slot == RG_GUI_DOCK_SLOT_CENTER ? hover : base);

			RgGuiRect preview = rg_gui_dock_preview_rect(drop_rect, drop_slot);
			rg_vec4 preview_fill = ctx->style.color_accent;
			preview_fill.w *= 0.22f;
			rg_vec4 preview_border = ctx->style.color_accent;
			preview_border.w *= 0.65f;
			rg_gui_push_rect_to(ctx, overlay, preview, preview_fill);
			rg_gui_push_rect_outline_to(ctx, overlay, preview, preview_border,
			                            ctx->style.border_thickness);
		}
	}

	{
		RgGuiRect ghost_global = drag->rect;
		ghost_global.x += drag->origin.x;
		ghost_global.y += drag->origin.y;

		int drawn = 0;
#if defined(RG_GUI_ENABLE_VIEWPORTS)
		for (u32 i = 0u; i < ctx->viewport_active_count; i++)
		{
			RgGuiViewport* viewport = ctx->viewport_active[i];
			if (!viewport)
			{
				continue;
			}

			RgGuiRect bounds = viewport->bounds;
			bounds.x += viewport->origin.x;
			bounds.y += viewport->origin.y;
			if (!rg_gui_rects_overlap(ghost_global, bounds))
			{
				continue;
			}

			RgGuiRect ghost = ghost_global;
			ghost.x -= viewport->origin.x;
			ghost.y -= viewport->origin.y;
			rg_gui_dock_draw_ghost(ctx, &viewport->draw_list, ghost, drag->title);
			drawn = 1;
		}
#endif
		if (!drawn)
		{
			RgGuiDrawList* ghost_list = rg_gui_overlay_target(ctx);
			if (ghost_list)
			{
				RgGuiRect ghost = ghost_global;
				ghost.x -= ctx->viewport_origin.x;
				ghost.y -= ctx->viewport_origin.y;
				rg_gui_dock_draw_ghost(ctx, ghost_list, ghost, drag->title);
			}
		}
	}

	if (drag->mouse_released)
	{
		int docked = 0;
		if (dockable && drop_space)
		{
			if (drag->window->docked)
			{
				RgGuiDockSpaceState* current = rg_gui_dockspace_find(ctx, drag->window->dockspace_id);
				if (current)
				{
					rg_gui_dock_detach_window(ctx, current, drag->window);
				}
			}

			if (rg_gui_dock_attach_slot(ctx, drop_space, drop_node, drag->window, drag->title,
			                            drag->window_id, drop_slot))
			{
				docked = 1;
				drag->window->dock_flags = 0u;
			}
		}

		if (!docked && dockable && drag->bounds_set)
		{
			f32 snap = (f32)RG_GUI_WINDOW_SNAP_DISTANCE;
			if (snap < 0.0f)
			{
				snap = 0.0f;
			}

			RgGuiRect bounds = drag->bounds;
			f32 bounds_right = bounds.x + bounds.w;
			f32 bounds_bottom = bounds.y + bounds.h;
			f32 max_x = bounds_right - drag->window->rect.w;
			f32 max_y = bounds_bottom - drag->window->rect.h;
			if (max_x < bounds.x)
			{
				max_x = bounds.x;
			}
			if (max_y < bounds.y)
			{
				max_y = bounds.y;
			}

			u32 dock_flags = 0u;
			f32 dist = drag->window->rect.x - bounds.x;
			if (dist < 0.0f)
			{
				dist = -dist;
			}
			if (dist <= snap)
			{
				dock_flags |= RG_GUI_WINDOW_DOCK_LEFT;
			}
			else
			{
				dist = (drag->window->rect.x + drag->window->rect.w) - bounds_right;
				if (dist < 0.0f)
				{
					dist = -dist;
				}
				if (dist <= snap)
				{
					dock_flags |= RG_GUI_WINDOW_DOCK_RIGHT;
				}
			}

			dist = drag->window->rect.y - bounds.y;
			if (dist < 0.0f)
			{
				dist = -dist;
			}
			if (dist <= snap)
			{
				dock_flags |= RG_GUI_WINDOW_DOCK_TOP;
			}
			else
			{
				dist = (drag->window->rect.y + drag->window->rect.h) - bounds_bottom;
				if (dist < 0.0f)
				{
					dist = -dist;
				}
				if (dist <= snap)
				{
					dock_flags |= RG_GUI_WINDOW_DOCK_BOTTOM;
				}
			}

			drag->window->dock_flags = dock_flags;
			if (dock_flags != 0u)
			{
				if (dock_flags & RG_GUI_WINDOW_DOCK_LEFT)
				{
					drag->window->rect.x = bounds.x;
				}
				else if (dock_flags & RG_GUI_WINDOW_DOCK_RIGHT)
				{
					drag->window->rect.x = max_x;
				}

				if (dock_flags & RG_GUI_WINDOW_DOCK_TOP)
				{
					drag->window->rect.y = bounds.y;
				}
				else if (dock_flags & RG_GUI_WINDOW_DOCK_BOTTOM)
				{
					drag->window->rect.y = max_y;
				}
			}
		}

#if defined(RG_GUI_ENABLE_VIEWPORTS)
		if (drag->viewport_id != 0u)
		{
			RgGuiViewport* viewport = rg_gui_viewport_find(ctx, drag->viewport_id);
			if (viewport)
			{
				viewport->active_id = 0u;
			}
		}
#endif
		ctx->active_id = 0u;
		drag->active = 0;
		drag->window = NULL;
	}
}

RGINLINE int rg_gui_window_begin(RgGuiContext* ctx, RgGuiWindowState* window, const char* title, RgGuiRect rect,
                                 f32 min_w, f32 min_h, f32 spacing, u32 flags, RgGuiId id)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(window != NULL);

	id = rg_gui_id_scoped(ctx, id);

	if (!window->initialized)
	{
		window->rect = rect;
		window->open = 1;
		window->collapsed = 0;
		window->expanded_height = rect.h;
		window->resize_flags = 0u;
		window->order = 0u;
		window->dock_flags = 0u;
		if (!window->docked)
		{
			window->dockspace_id = 0u;
			window->dock_node = 0u;
			window->dock_tab = 0u;
			window->docked = 0;
		}
		window->dock_drag_pending = 0;
		window->initialized = 1;
	}

	if (window->docked)
	{
		if (window->dockspace_id != 0u)
		{
			int dock_fallback = 0;
			int dock_draw = rg_gui_window_begin_docked(ctx, window, title, spacing, flags, id, &dock_fallback);
			if (!dock_fallback)
			{
				return dock_draw;
			}
		}
		window->docked = 0;
		window->dock_node = 0u;
		window->dock_tab = 0u;
		window->dock_drag_pending = 0;
	}

	if ((flags & RG_GUI_WINDOW_CLOSABLE) == 0u)
	{
		window->open = 1;
	}

	if (!window->open)
	{
		return 0;
	}

	if (window->order == 0u)
	{
		window->order = ++ctx->window_order_next;
	}
	else if (window->order > ctx->window_order_next)
	{
		ctx->window_order_next = window->order;
	}

	RgGuiId drag_id = rg_gui_id_combine(id, 1u);
	RgGuiId resize_id = rg_gui_id_combine(id, 2u);

	window->prev_layout = ctx->layout;

	f32 pad = ctx->style.padding;
	f32 inner = ctx->style.inner_spacing;
	if (inner < 0.0f)
	{
		inner = 0.0f;
	}

	f32 title_h = ctx->style.text_height + pad * 2.0f;
	if (title_h < 1.0f)
	{
		title_h = 1.0f;
	}

	f32 min_width = min_w;
	f32 min_height = min_h;
	f32 layout_spacing = spacing;
	if (layout_spacing < 0.0f)
	{
		layout_spacing = 0.0f;
	}
	if (min_width <= 0.0f)
	{
		min_width = title_h * 4.0f;
	}
	if (min_height <= 0.0f)
	{
		min_height = title_h + ctx->style.text_height * 2.0f;
	}
	if (min_height < title_h)
	{
		min_height = title_h;
	}

	if (window->rect.w < min_width)
	{
		window->rect.w = min_width;
	}
	if (window->rect.h < min_height)
	{
		window->rect.h = min_height;
	}
	if (window->rect.w < 1.0f)
	{
		window->rect.w = 1.0f;
	}
	if (window->rect.h < title_h)
	{
		window->rect.h = title_h;
	}

	if (window->collapsed)
	{
		if (window->rect.h != title_h)
		{
			window->rect.h = title_h;
		}
	}

	if ((flags & RG_GUI_WINDOW_DOCKABLE) != 0u && window->dock_flags != 0u && ctx->window_bounds_set)
	{
		if (ctx->active_id != drag_id && ctx->active_id != resize_id)
		{
			RgGuiRect bounds = ctx->window_bounds;
			f32 max_x = bounds.x + bounds.w - window->rect.w;
			f32 max_y = bounds.y + bounds.h - window->rect.h;
			if (max_x < bounds.x)
			{
				max_x = bounds.x;
			}
			if (max_y < bounds.y)
			{
				max_y = bounds.y;
			}

			if (window->dock_flags & RG_GUI_WINDOW_DOCK_LEFT)
			{
				window->rect.x = bounds.x;
			}
			else if (window->dock_flags & RG_GUI_WINDOW_DOCK_RIGHT)
			{
				window->rect.x = max_x;
			}

			if (window->dock_flags & RG_GUI_WINDOW_DOCK_TOP)
			{
				window->rect.y = bounds.y;
			}
			else if (window->dock_flags & RG_GUI_WINDOW_DOCK_BOTTOM)
			{
				window->rect.y = max_y;
			}
		}
	}

	RgGuiRect win_rect = window->rect;
	RgGuiRect title_rect = rg_gui_make_rect(win_rect.x, win_rect.y, win_rect.w, title_h);

	f32 button_size = title_h - pad;
	if (button_size < ctx->style.text_height)
	{
		button_size = ctx->style.text_height;
	}
	if (button_size < 4.0f)
	{
		button_size = 4.0f;
	}

	f32 button_y = win_rect.y + (title_h - button_size) * 0.5f;
	f32 right = win_rect.x + win_rect.w - pad;

	RgGuiRect close_rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	RgGuiRect collapse_rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	int has_close = (flags & RG_GUI_WINDOW_CLOSABLE) != 0u;
	int has_collapse = (flags & RG_GUI_WINDOW_COLLAPSIBLE) != 0u;
	if (has_close)
	{
		close_rect = rg_gui_make_rect(right - button_size, button_y, button_size, button_size);
		right -= button_size + inner;
	}
	if (has_collapse)
	{
		collapse_rect = rg_gui_make_rect(right - button_size, button_y, button_size, button_size);
		right -= button_size + inner;
	}

	int enabled = !rg_gui_is_disabled(ctx);
	int close_hovered = has_close && enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, close_rect);
	int collapse_hovered = has_collapse && enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, collapse_rect);

	if (close_hovered)
	{
		ctx->hot_id = rg_gui_id_combine(id, 3u);
	}
	if (collapse_hovered)
	{
		ctx->hot_id = rg_gui_id_combine(id, 4u);
	}

	if (enabled && close_hovered && ctx->mouse_pressed)
	{
		window->open = 0;
		ctx->focus_id = id;
		return 0;
	}

	if (enabled && collapse_hovered && ctx->mouse_pressed)
	{
		window->collapsed = !window->collapsed;
		if (window->collapsed)
		{
			window->expanded_height = win_rect.h;
			window->rect.h = title_h;
		}
		else
		{
			f32 restored = window->expanded_height;
			if (restored < min_height)
			{
				restored = min_height;
			}
			if (restored < title_h)
			{
				restored = title_h;
			}
			window->rect.h = restored;
		}
		ctx->focus_id = id;
		win_rect = window->rect;
	}

	if (enabled && ctx->mouse_pressed &&
	    rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, win_rect))
	{
		window->order = ++ctx->window_order_next;
	}

	int title_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, title_rect);
	int drag_hovered = title_hovered && !close_hovered && !collapse_hovered;

	f32 edge = pad;
	if (edge < 6.0f)
	{
		edge = 6.0f;
	}

	u32 resize_flags = 0u;
	if (enabled && (flags & RG_GUI_WINDOW_RESIZABLE) != 0u && !window->collapsed)
	{
		f32 content_y = win_rect.y + title_h;
		f32 content_h = win_rect.h - title_h;
		if (content_h < 0.0f)
		{
			content_h = 0.0f;
		}

		RgGuiRect left_edge = rg_gui_make_rect(win_rect.x, content_y, edge, content_h);
		RgGuiRect right_edge = rg_gui_make_rect(win_rect.x + win_rect.w - edge, content_y, edge, content_h);
		RgGuiRect bottom_edge = rg_gui_make_rect(win_rect.x, win_rect.y + win_rect.h - edge, win_rect.w, edge);

		if (rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, left_edge))
		{
			resize_flags |= 1u;
		}
		if (rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, right_edge))
		{
			resize_flags |= 2u;
		}
		if (rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, bottom_edge))
		{
			resize_flags |= 4u;
		}

		if (resize_flags != 0u)
		{
			ctx->hot_id = resize_id;
		}
	}

	if ((flags & RG_GUI_WINDOW_MOVABLE) != 0u && drag_hovered)
	{
		ctx->hot_id = drag_id;
	}

	if (enabled && drag_hovered && ctx->mouse_pressed && (flags & RG_GUI_WINDOW_MOVABLE) != 0u)
	{
		ctx->active_id = drag_id;
		ctx->focus_id = id;
		window->dock_flags = 0u;
		window->start_mouse = ctx->mouse_pos;
		window->start_rect = window->rect;
	}

	if (enabled && resize_flags != 0u && ctx->mouse_pressed)
	{
		ctx->active_id = resize_id;
		ctx->focus_id = id;
		window->dock_flags = 0u;
		window->resize_flags = resize_flags;
		window->start_mouse = ctx->mouse_pos;
		window->start_rect = window->rect;
	}

	if (ctx->active_id == drag_id)
	{
		int drag_released = !ctx->mouse_down;
		if (!drag_released)
		{
			rg_vec2 delta = rg_vec2(ctx->mouse_pos.x - window->start_mouse.x,
			                        ctx->mouse_pos.y - window->start_mouse.y);
			RgGuiRect next = window->start_rect;
			next.x = window->start_rect.x + delta.x;
			next.y = window->start_rect.y + delta.y;

			if ((flags & RG_GUI_WINDOW_SNAP) != 0u && ctx->window_bounds_set)
			{
				f32 snap = (f32)RG_GUI_WINDOW_SNAP_DISTANCE;
				if (snap < 0.0f)
				{
					snap = 0.0f;
				}

				RgGuiRect bounds = ctx->window_bounds;
				f32 bounds_right = bounds.x + bounds.w;
				f32 bounds_bottom = bounds.y + bounds.h;
				f32 max_x = bounds_right - next.w;
				f32 max_y = bounds_bottom - next.h;
				if (max_x < bounds.x)
				{
					max_x = bounds.x;
				}
				if (max_y < bounds.y)
				{
					max_y = bounds.y;
				}

				f32 dist = next.x - bounds.x;
				if (dist < 0.0f)
				{
					dist = -dist;
				}
				if (dist <= snap)
				{
					next.x = bounds.x;
				}

				dist = (next.x + next.w) - bounds_right;
				if (dist < 0.0f)
				{
					dist = -dist;
				}
				if (dist <= snap)
				{
					next.x = max_x;
				}

				dist = next.y - bounds.y;
				if (dist < 0.0f)
				{
					dist = -dist;
				}
				if (dist <= snap)
				{
					next.y = bounds.y;
				}

				dist = (next.y + next.h) - bounds_bottom;
				if (dist < 0.0f)
				{
					dist = -dist;
				}
				if (dist <= snap)
				{
					next.y = max_y;
				}
			}

			window->rect = next;
			win_rect = next;
		}
		ctx->dock_drag.active = 1;
		ctx->dock_drag.mouse_released = ctx->mouse_released || drag_released;
		ctx->dock_drag.window = window;
		ctx->dock_drag.window_id = id;
		ctx->dock_drag.viewport_id = ctx->viewport_id;
		ctx->dock_drag.title = title;
		ctx->dock_drag.flags = flags;
		ctx->dock_drag.mouse_global = ctx->mouse_pos_global;
		ctx->dock_drag.origin = ctx->viewport_origin;
		ctx->dock_drag.rect = window->rect;
		ctx->dock_drag.bounds = ctx->window_bounds;
		ctx->dock_drag.bounds_set = ctx->window_bounds_set;
		if (drag_released)
		{
			ctx->active_id = 0u;
		}
	}

	if (ctx->active_id == resize_id)
	{
		if (ctx->mouse_down)
		{
			rg_vec2 delta = rg_vec2(ctx->mouse_pos.x - window->start_mouse.x,
			                        ctx->mouse_pos.y - window->start_mouse.y);
			RgGuiRect next = window->start_rect;
			if (window->resize_flags & 1u)
			{
				next.x = window->start_rect.x + delta.x;
				next.w = window->start_rect.w - delta.x;
			}
			if (window->resize_flags & 2u)
			{
				next.w = window->start_rect.w + delta.x;
			}
			if (window->resize_flags & 4u)
			{
				next.h = window->start_rect.h + delta.y;
			}

			if (next.w < min_width)
			{
				if (window->resize_flags & 1u)
				{
					next.x = window->start_rect.x + (window->start_rect.w - min_width);
				}
				next.w = min_width;
			}
			if (next.h < min_height)
			{
				next.h = min_height;
			}
			if (next.h < title_h)
			{
				next.h = title_h;
			}

			window->rect = next;
			win_rect = next;
		}
		if (ctx->mouse_released)
		{
			ctx->active_id = 0u;
			window->resize_flags = 0u;
		}
	}

	if ((flags & RG_GUI_WINDOW_RESIZABLE) != 0u && !window->collapsed)
	{
		u32 cursor_flags = resize_flags;
		if (ctx->active_id == resize_id)
		{
			cursor_flags = window->resize_flags;
		}
		if (cursor_flags & (1u | 2u))
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
		}
		else if (cursor_flags & 4u)
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_V;
		}
	}

	if ((flags & RG_GUI_WINDOW_MOVABLE) != 0u)
	{
		if (ctx->active_id == drag_id || drag_hovered)
		{
			if (ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
			{
				ctx->mouse_cursor = RG_GUI_CURSOR_MOVE;
			}
		}
	}

	rg_gui_push_rect(ctx, win_rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, win_rect, ctx->style.color_border, ctx->style.border_thickness);
	rg_gui_push_rect(ctx, title_rect, ctx->style.color_bg);

	if ((title && *title) || rg_gui_icon_valid(&window->icon))
	{
		f32 text_left = win_rect.x + pad;
		f32 text_right = right;
		f32 text_w = text_right - text_left;
		if (text_w > 0.0f && title_rect.h > 0.0f)
		{
			RgGuiRect text_clip = rg_gui_make_rect(text_left, title_rect.y, text_w, title_rect.h);
			rg_gui_push_clip(ctx, text_clip);
			if (rg_gui_icon_valid(&window->icon))
			{
				RgGuiRect icon_rect = rg_gui_icon_rect(ctx, title_rect, text_left);
				rg_gui_push_icon(ctx, &window->icon, icon_rect, 0);
				text_left = icon_rect.x + icon_rect.w;
				if (title && *title)
				{
					text_left += inner;
				}
			}
			rg_vec2 title_pos = rg_vec2(text_left,
			                            win_rect.y + (title_h - ctx->style.text_height) * 0.5f);
			if (title && *title && RG_GUI_LABEL_COPY)
			{
				rg_gui_push_text(ctx, title, title_pos, ctx->style.color_text);
			}
			else if (title && *title)
			{
				rg_gui_push_text_static(ctx, title, title_pos, ctx->style.color_text);
			}
			rg_gui_pop_clip(ctx);
		}
	}

	if (has_collapse)
	{
		rg_vec4 btn_color = collapse_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, collapse_rect, btn_color);
		rg_gui_push_rect_outline(ctx, collapse_rect, ctx->style.color_border, ctx->style.border_thickness);
		const char* glyph = window->collapsed ? "+" : "-";
		rg_vec2 pos = rg_vec2(collapse_rect.x + (collapse_rect.w - ctx->style.text_height * 0.5f) * 0.5f,
		                      collapse_rect.y + (collapse_rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_static(ctx, glyph, pos, ctx->style.color_text);
	}

	if (has_close)
	{
		rg_vec4 btn_color = close_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, close_rect, btn_color);
		rg_gui_push_rect_outline(ctx, close_rect, ctx->style.color_border, ctx->style.border_thickness);
		rg_gui_push_close_glyph(ctx, close_rect);
	}

	if (window->collapsed)
	{
		ctx->layout = window->prev_layout;
		return 0;
	}

	f32 border = ctx->style.border_thickness;
	f32 inset = border + pad;
	RgGuiRect inner_rect = win_rect;
	inner_rect.x += inset;
	inner_rect.y += title_h + inset;
	inner_rect.w -= inset * 2.0f;
	inner_rect.h -= title_h + inset * 2.0f;
	if (inner_rect.w < 0.0f) inner_rect.w = 0.0f;
	if (inner_rect.h < 0.0f) inner_rect.h = 0.0f;

	ctx->layout.x = inner_rect.x;
	ctx->layout.y = inner_rect.y;
	ctx->layout.width = inner_rect.w;
	ctx->layout.spacing = layout_spacing;
	ctx->layout.cursor_y = inner_rect.y;
	ctx->layout.active = 1;
	rg_gui_layout_reset_row(&ctx->layout);

	rg_gui_push_clip(ctx, inner_rect);
	return 1;
}

RGINLINE void rg_gui_window_end(RgGuiContext* ctx, RgGuiWindowState* window)
{
	if (!ctx || !window)
	{
		return;
	}

	rg_gui_pop_clip(ctx);
	ctx->layout = window->prev_layout;
}

RGINLINE void rg_gui_window_set_icon(RgGuiWindowState* window, const RgGuiIcon* icon)
{
	if (!window)
	{
		return;
	}

	if (icon)
	{
		window->icon = *icon;
	}
	else
	{
		memset(&window->icon, 0, sizeof(window->icon));
	}
}

RGINLINE void rg_gui_window_set_bounds(RgGuiContext* ctx, RgGuiRect bounds)
{
	if (!ctx)
	{
		return;
	}

	ctx->window_bounds = bounds;
	ctx->window_bounds_set = 1;
}

RGINLINE void rg_gui_set_viewport_origin(RgGuiContext* ctx, rg_vec2 origin)
{
	if (!ctx)
	{
		return;
	}

	ctx->viewport_origin = origin;
	ctx->mouse_pos_global = rg_vec2(ctx->mouse_pos.x + origin.x,
	                                ctx->mouse_pos.y + origin.y);
}

#if defined(RG_GUI_ENABLE_VIEWPORTS)
RGINLINE void rg_gui_set_mouse_focus_viewport(RgGuiContext* ctx, RgGuiId viewport_id, int mouse_inside)
{
	if (!ctx)
	{
		return;
	}

	ctx->mouse_focus_viewport = viewport_id;
	ctx->mouse_focus_inside = mouse_inside ? 1 : 0;
}

RGINLINE void rg_gui_viewport_state_reset(RgGuiViewport* viewport)
{
	if (!viewport)
	{
		return;
	}

	viewport->focus_id = 0u;
	viewport->active_id = 0u;
	viewport->scroll_owner = 0u;
	viewport->key_repeat_owner = 0u;
	viewport->key_repeat_key = SDL_SCANCODE_UNKNOWN;
	viewport->key_repeat_hold_time = 0.0f;
	viewport->key_repeat_repeat_time = 0.0f;
	viewport->menu_bar_id = 0u;
	viewport->menu_bar_block_left = 0;
	viewport->menu_bar_block_right = 0;
	viewport->cursor_blink_timer = 0.0f;
	viewport->cursor_visible = 1;
	memset(&viewport->text_edit, 0, sizeof(viewport->text_edit));
	memset(&viewport->number_edit, 0, sizeof(viewport->number_edit));
	memset(&viewport->platform_output, 0, sizeof(viewport->platform_output));
	viewport->platform_output.cursor = RG_GUI_CURSOR_DEFAULT;
	viewport->window_focused = 1;
}

RGINLINE RgGuiViewport* rg_gui_viewport_get(RgGuiContext* ctx, RgGuiId id)
{
	if (!ctx || !ctx->viewports || id == 0u)
	{
		return NULL;
	}

	RgGuiViewport* free_slot = NULL;
	for (u32 i = 0u; i < ctx->viewport_capacity; i++)
	{
		if (ctx->viewports[i].id == id)
		{
			return &ctx->viewports[i];
		}
		if (ctx->viewports[i].id == 0u && !free_slot)
		{
			free_slot = &ctx->viewports[i];
		}
	}

	if (free_slot)
	{
		free_slot->id = id;
		rg_gui_viewport_state_reset(free_slot);
		return free_slot;
	}

	return NULL;
}

RGINLINE RgGuiViewport* rg_gui_viewport_find(RgGuiContext* ctx, RgGuiId id)
{
	if (!ctx || !ctx->viewports || id == 0u)
	{
		return NULL;
	}

	for (u32 i = 0u; i < ctx->viewport_active_count; i++)
	{
		RgGuiViewport* viewport = ctx->viewport_active[i];
		if (viewport && viewport->id == id)
		{
			return viewport;
		}
	}

	for (u32 i = 0u; i < ctx->viewport_capacity; i++)
	{
		if (ctx->viewports[i].id == id)
		{
			return &ctx->viewports[i];
		}
	}

	return NULL;
}

RGINLINE RgGuiViewport* rg_gui_viewport_begin_ordered_ex(
    RgGuiContext* ctx, RgGuiId id, RgGuiRect bounds,
    const RgInputState* input, const RgInputEventQueue* events,
    SDL_WindowID window_id, rg_vec2 origin)
{
	if (!ctx || id == 0u || !ctx->viewports)
	{
		return NULL;
	}

	if (ctx->viewport_stack_top >= RG_GUI_VIEWPORT_STACK_MAX)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_OVERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_viewport_begin stack overflow");
		return NULL;
	}

	RgGuiViewport* viewport = rg_gui_viewport_get(ctx, id);
	if (!viewport || !viewport->draw_list.cmds || !viewport->overlay_list.cmds)
	{
		return NULL;
	}

	if (!viewport->active)
	{
		viewport->active = 1;
		viewport->draw_list.count = 0u;
		viewport->overlay_list.count = 0u;
		viewport->overlay_start = 0u;
		if (ctx->viewport_active_count < ctx->viewport_capacity)
		{
			ctx->viewport_active[ctx->viewport_active_count++] = viewport;
		}
	}

	viewport->bounds = bounds;
	viewport->origin = origin;

	RgGuiViewportStackEntry* entry = &ctx->viewport_stack[ctx->viewport_stack_top++];
	entry->prev_draw_target = ctx->draw_target;
	entry->prev_overlay_target = ctx->overlay_target;
	entry->prev_window_bounds = ctx->window_bounds;
	entry->prev_window_bounds_set = ctx->window_bounds_set;
	entry->prev_input = ctx->input;
	entry->prev_viewport_origin = ctx->viewport_origin;
	entry->prev_mouse_pos_global = ctx->mouse_pos_global;
	entry->prev_viewport_id = ctx->viewport_id;
	entry->prev_mouse_pos = ctx->mouse_pos;
	entry->prev_mouse_pos_raw = ctx->mouse_pos_raw;
	entry->prev_mouse_down = ctx->mouse_down;
	entry->prev_mouse_pressed = ctx->mouse_pressed;
	entry->prev_mouse_released = ctx->mouse_released;
	entry->prev_mouse_wheel = ctx->mouse_wheel;
	entry->prev_input_events = ctx->input_events;
	entry->prev_input_window_id = ctx->input_window_id;
	entry->prev_input_event_focus_id = ctx->input_event_focus_id;
	entry->prev_input_events_processed_id = ctx->input_events_processed_id;
	entry->prev_input_events_changed_id = ctx->input_events_changed_id;
	entry->prev_input_events_submit_id = ctx->input_events_submit_id;
	entry->prev_input_window_focused_at_frame_start =
	    ctx->input_window_focused_at_frame_start;
	entry->prev_window_focused = ctx->window_focused;
	entry->prev_mouse_cursor = ctx->mouse_cursor;
	entry->prev_platform_output = ctx->platform_output;
	entry->prev_cursor_blink_timer = ctx->cursor_blink_timer;
	entry->prev_cursor_visible = ctx->cursor_visible;
	entry->prev_hot_id = ctx->hot_id;
	entry->prev_active_id = ctx->active_id;
	entry->prev_focus_id = ctx->focus_id;
	entry->prev_scroll_owner = ctx->scroll_owner;
	entry->prev_scroll_owner_next = ctx->scroll_owner_next;
	entry->prev_tab_first_id = ctx->tab_first_id;
	entry->prev_tab_last_id = ctx->tab_last_id;
	entry->prev_tab_prev_id = ctx->tab_prev_id;
	entry->prev_tab_focus_id = ctx->tab_focus_id;
	entry->prev_tab_dir = ctx->tab_dir;
	entry->prev_tab_seek = ctx->tab_seek;
	entry->prev_tab_wrap = ctx->tab_wrap;
	entry->prev_tab_found_focus = ctx->tab_found_focus;
	entry->prev_key_repeat_owner = ctx->key_repeat_owner;
	entry->prev_key_repeat_key = ctx->key_repeat_key;
	entry->prev_key_repeat_hold_time = ctx->key_repeat_hold_time;
	entry->prev_key_repeat_repeat_time = ctx->key_repeat_repeat_time;
	entry->prev_menu_bar_id = ctx->menu_bar_id;
	entry->prev_menu_bar_block_left = ctx->menu_bar_block_left;
	entry->prev_menu_bar_block_right = ctx->menu_bar_block_right;
	entry->prev_text_edit_state = ctx->text_edit_state;
	entry->prev_number_edit_state = ctx->number_edit_state;

	ctx->draw_target = &viewport->draw_list;
	ctx->overlay_target = &viewport->overlay_list;
	ctx->window_bounds = bounds;
	ctx->window_bounds_set = 1;
	ctx->viewport_origin = origin;
	ctx->viewport_id = id;

	ctx->hot_id = 0u;
	ctx->active_id = viewport->active_id;
	ctx->focus_id = viewport->focus_id;
	ctx->scroll_owner = viewport->scroll_owner;
	ctx->scroll_owner_next = 0u;
	ctx->tab_first_id = 0u;
	ctx->tab_last_id = 0u;
	ctx->tab_prev_id = 0u;
	ctx->tab_focus_id = 0u;
	ctx->tab_dir = 0;
	ctx->tab_seek = 0;
	ctx->tab_wrap = 0;
	ctx->tab_found_focus = 0;
	ctx->key_repeat_owner = viewport->key_repeat_owner;
	ctx->key_repeat_key = viewport->key_repeat_key;
	ctx->key_repeat_hold_time = viewport->key_repeat_hold_time;
	ctx->key_repeat_repeat_time = viewport->key_repeat_repeat_time;
	ctx->menu_bar_id = viewport->menu_bar_id;
	ctx->menu_bar_block_left = viewport->menu_bar_block_left;
	ctx->menu_bar_block_right = viewport->menu_bar_block_right;
	ctx->cursor_blink_timer = viewport->cursor_blink_timer;
	ctx->cursor_visible = viewport->cursor_visible;
	ctx->text_edit_state = &viewport->text_edit;
	ctx->number_edit_state = &viewport->number_edit;

	ctx->input_events = events;
	ctx->input_window_id = window_id;
	ctx->input_event_focus_id = ctx->focus_id;
	ctx->input_events_processed_id = 0u;
	ctx->input_window_focused_at_frame_start = events ? viewport->window_focused : 1;
	ctx->window_focused = events ? viewport->window_focused : 1;
	if (events)
	{
		ctx->input_events_changed_id = 0u;
		ctx->input_events_submit_id = 0u;
		size_t event_count = rg_gui_input_event_count(events);
		for (size_t i = 0u; i < event_count; i++)
		{
			const RgInputEvent* event = &events->events[i];
			if (!rg_gui_input_event_matches_window(ctx, event))
			{
				continue;
			}
			if (event->kind == RG_INPUT_EVENT_WINDOW_FOCUS_LOST)
			{
				ctx->window_focused = 0;
				ctx->active_id = 0u;
				rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
			}
			else if (event->kind == RG_INPUT_EVENT_WINDOW_FOCUS_GAINED)
			{
				ctx->window_focused = 1;
			}
		}
	}
	ctx->platform_output = viewport->platform_output;
	ctx->mouse_cursor = ctx->platform_output.cursor;

	if (input)
	{
		ctx->input = input;
		ctx->mouse_pos_raw = rg_vec2((f32)input->mouse_x, (f32)input->mouse_y);
		ctx->mouse_pos = ctx->mouse_pos_raw;
		ctx->mouse_down = rg_input_is_mouse_button_down(input, RG_MOUSE_BUTTON_LEFT);
		ctx->mouse_pressed = rg_input_is_mouse_button_pressed(input, RG_MOUSE_BUTTON_LEFT);
		ctx->mouse_released = rg_input_is_mouse_button_released(input, RG_MOUSE_BUTTON_LEFT);
		ctx->mouse_wheel = input->mouse_scroll_y;
		if (ctx->input_capture_active)
		{
			ctx->mouse_pos = rg_gui_input_blocked_pos();
		}
		if (ctx->mouse_down && ctx->window_focused)
		{
			ctx->mouse_down_any = 1;
		}
	}
	if (!ctx->window_focused)
	{
		ctx->mouse_down = 0;
		ctx->mouse_pressed = 0;
		ctx->mouse_released = 0;
		ctx->mouse_wheel = 0.0f;
	}
	if (ctx->input && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_TAB))
	{
		ctx->tab_dir = rg_gui_input_has_shift(ctx->input) ? -1 : 1;
	}

	if (ctx->mouse_pressed && !ctx->input_events)
	{
		rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		ctx->focus_id = 0u;
		ctx->text_edit_state->active = 0;
	}

	rg_gui_text_edit_update_blink(ctx);

	ctx->mouse_pos_global = rg_vec2(ctx->mouse_pos.x + origin.x,
	                                ctx->mouse_pos.y + origin.y);
	return viewport;
}

RGINLINE RgGuiViewport* rg_gui_viewport_begin_ex(RgGuiContext* ctx, RgGuiId id,
                                                 RgGuiRect bounds,
                                                 const RgInputState* input,
                                                 rg_vec2 origin)
{
	return rg_gui_viewport_begin_ordered_ex(
	    ctx, id, bounds, input, NULL, 0u, origin);
}

RGINLINE RgGuiViewport* rg_gui_viewport_begin(RgGuiContext* ctx, RgGuiId id, RgGuiRect bounds, const RgInputState* input)
{
	return rg_gui_viewport_begin_ex(ctx, id, bounds, input, rg_vec2(0.0f, 0.0f));
}

RGINLINE void rg_gui_viewport_end(RgGuiContext* ctx, RgGuiViewport* viewport)
{
	if (!ctx || !viewport || ctx->viewport_stack_top == 0u)
	{
		if (ctx && ctx->viewport_stack_top == 0u)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW;
		}
		RG_GUI_ASSERT(0 && "rg_gui_viewport_end without begin");
		return;
	}

	rg_gui_end_frame_state(ctx);

	viewport->focus_id = ctx->focus_id;
	viewport->active_id = ctx->active_id;
	viewport->scroll_owner = ctx->scroll_owner;
	viewport->key_repeat_owner = ctx->key_repeat_owner;
	viewport->key_repeat_key = ctx->key_repeat_key;
	viewport->key_repeat_hold_time = ctx->key_repeat_hold_time;
	viewport->key_repeat_repeat_time = ctx->key_repeat_repeat_time;
	viewport->menu_bar_id = ctx->menu_bar_id;
	viewport->menu_bar_block_left = ctx->menu_bar_block_left;
	viewport->menu_bar_block_right = ctx->menu_bar_block_right;
	viewport->cursor_blink_timer = ctx->cursor_blink_timer;
	viewport->cursor_visible = ctx->cursor_visible;
	viewport->window_focused = ctx->window_focused;
	if (ctx->platform_output.wants_text_input ||
	    ctx->platform_output.ime_caret_valid)
	{
		rg_gui_finalize_platform_output(ctx);
	}
	ctx->platform_output.cursor = ctx->mouse_cursor;
	viewport->platform_output = ctx->platform_output;

	rg_gui_diagnostics_update_draw_high_water(ctx, &viewport->draw_list,
	                                          &viewport->overlay_list);
	viewport->overlay_start = viewport->draw_list.count;
	if (viewport->overlay_list.count > 0u)
	{
		u32 available = viewport->draw_list.count <= viewport->draw_list.capacity ? viewport->draw_list.capacity - viewport->draw_list.count : 0u;
		u32 copy_count = viewport->overlay_list.count;
		if (copy_count > available)
		{
			copy_count = available;
		}

		if (copy_count < viewport->overlay_list.count)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_OVERLAY_CAPACITY;
			rg_gui_diagnostics_add_draw_drop(ctx, viewport->overlay_list.count - copy_count);
		}

		if (copy_count > 0u)
		{
			memcpy(viewport->draw_list.cmds + viewport->draw_list.count,
			       viewport->overlay_list.cmds,
			       sizeof(RgGuiDrawCmd) * copy_count);
			viewport->draw_list.count += copy_count;
		}
	}

	RgGuiViewportStackEntry* entry = &ctx->viewport_stack[--ctx->viewport_stack_top];
	ctx->draw_target = entry->prev_draw_target ? entry->prev_draw_target : &ctx->draw_list;
	ctx->overlay_target = entry->prev_overlay_target ? entry->prev_overlay_target : &ctx->overlay_list;
	ctx->window_bounds = entry->prev_window_bounds;
	ctx->window_bounds_set = entry->prev_window_bounds_set;
	ctx->input = entry->prev_input;
	ctx->mouse_pos = entry->prev_mouse_pos;
	ctx->mouse_pos_raw = entry->prev_mouse_pos_raw;
	ctx->mouse_pos_global = entry->prev_mouse_pos_global;
	ctx->viewport_origin = entry->prev_viewport_origin;
	ctx->viewport_id = entry->prev_viewport_id;
	ctx->mouse_down = entry->prev_mouse_down;
	ctx->mouse_pressed = entry->prev_mouse_pressed;
	ctx->mouse_released = entry->prev_mouse_released;
	ctx->mouse_wheel = entry->prev_mouse_wheel;
	ctx->input_events = entry->prev_input_events;
	ctx->input_window_id = entry->prev_input_window_id;
	ctx->input_event_focus_id = entry->prev_input_event_focus_id;
	ctx->input_events_processed_id = entry->prev_input_events_processed_id;
	ctx->input_events_changed_id = entry->prev_input_events_changed_id;
	ctx->input_events_submit_id = entry->prev_input_events_submit_id;
	ctx->input_window_focused_at_frame_start =
	    entry->prev_input_window_focused_at_frame_start;
	ctx->window_focused = entry->prev_window_focused;
	ctx->mouse_cursor = entry->prev_mouse_cursor;
	ctx->platform_output = entry->prev_platform_output;
	ctx->cursor_blink_timer = entry->prev_cursor_blink_timer;
	ctx->cursor_visible = entry->prev_cursor_visible;
	ctx->hot_id = entry->prev_hot_id;
	ctx->active_id = entry->prev_active_id;
	ctx->focus_id = entry->prev_focus_id;
	ctx->scroll_owner = entry->prev_scroll_owner;
	ctx->scroll_owner_next = entry->prev_scroll_owner_next;
	ctx->tab_first_id = entry->prev_tab_first_id;
	ctx->tab_last_id = entry->prev_tab_last_id;
	ctx->tab_prev_id = entry->prev_tab_prev_id;
	ctx->tab_focus_id = entry->prev_tab_focus_id;
	ctx->tab_dir = entry->prev_tab_dir;
	ctx->tab_seek = entry->prev_tab_seek;
	ctx->tab_wrap = entry->prev_tab_wrap;
	ctx->tab_found_focus = entry->prev_tab_found_focus;
	ctx->key_repeat_owner = entry->prev_key_repeat_owner;
	ctx->key_repeat_key = entry->prev_key_repeat_key;
	ctx->key_repeat_hold_time = entry->prev_key_repeat_hold_time;
	ctx->key_repeat_repeat_time = entry->prev_key_repeat_repeat_time;
	ctx->menu_bar_id = entry->prev_menu_bar_id;
	ctx->menu_bar_block_left = entry->prev_menu_bar_block_left;
	ctx->menu_bar_block_right = entry->prev_menu_bar_block_right;
	ctx->text_edit_state = entry->prev_text_edit_state ? entry->prev_text_edit_state : &ctx->text_edit;
	ctx->number_edit_state = entry->prev_number_edit_state ? entry->prev_number_edit_state : &ctx->number_edit;
}

RGINLINE void rg_gui_viewport_release(RgGuiContext* ctx, RgGuiId id)
{
	if (!ctx || !ctx->viewports || id == 0u)
	{
		return;
	}

	for (u32 i = 0u; i < ctx->viewport_capacity; i++)
	{
		RgGuiViewport* viewport = &ctx->viewports[i];
		if (viewport->id == id)
		{
			viewport->id = 0u;
			viewport->active = 0;
			viewport->bounds = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
			viewport->origin = rg_vec2(0.0f, 0.0f);
			viewport->draw_list.count = 0u;
			viewport->overlay_list.count = 0u;
			viewport->overlay_start = 0u;
			viewport->focus_id = 0u;
			viewport->active_id = 0u;
			viewport->scroll_owner = 0u;
			viewport->key_repeat_owner = 0u;
			viewport->key_repeat_key = SDL_SCANCODE_UNKNOWN;
			viewport->key_repeat_hold_time = 0.0f;
			viewport->key_repeat_repeat_time = 0.0f;
			viewport->menu_bar_id = 0u;
			viewport->menu_bar_block_left = 0;
			viewport->menu_bar_block_right = 0;
			viewport->cursor_blink_timer = 0.0f;
			viewport->cursor_visible = 1;
			memset(&viewport->text_edit, 0, sizeof(viewport->text_edit));
			memset(&viewport->number_edit, 0, sizeof(viewport->number_edit));
			memset(&viewport->platform_output, 0, sizeof(viewport->platform_output));
			viewport->platform_output.cursor = RG_GUI_CURSOR_DEFAULT;
			viewport->window_focused = 1;
			return;
		}
	}
}
#endif

RGINLINE u32 rg_gui_window_order(const RgGuiWindowState* window)
{
	return window ? window->order : 0u;
}

RGINLINE void rg_gui_dockspace_begin(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, RgGuiRect rect, RgGuiId id)
{
	if (!ctx || !dockspace)
	{
		return;
	}

	id = rg_gui_id_scoped(ctx, id);

	if (!dockspace->initialized)
	{
		dockspace->rect = rect;
		dockspace->root = 0u;
		dockspace->tab_popup.open = 0;
		dockspace->tab_popup.pos = rg_vec2(0.0f, 0.0f);
		dockspace->tab_popup_selected = -1;
		dockspace->tab_popup_scroll = 0;
		dockspace->tab_popup_tab = 0u;
		dockspace->tab_popup_node = 0u;
		dockspace->initialized = 1;
	}

	dockspace->rect = rect;
	dockspace->id = id;
	dockspace->origin = ctx->viewport_origin;
	dockspace->viewport_id = ctx->viewport_id;
	dockspace->overlay_target = ctx->overlay_target;
	if (dockspace->root == 0u)
	{
		dockspace->root = rg_gui_dock_node_alloc(ctx);
	}

	if (dockspace->root != 0u)
	{
		rg_gui_dock_layout(ctx, dockspace->root, rect);
		if (rg_gui_dock_handle_splitters(ctx, dockspace, dockspace->root))
		{
			rg_gui_dock_layout(ctx, dockspace->root, rect);
		}
	}

	if (ctx->dockspaces && ctx->dockspace_count < ctx->dockspace_capacity)
	{
		ctx->dockspaces[ctx->dockspace_count++] = dockspace;
	}
}

RGINLINE void rg_gui_dockspace_end(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace)
{
	RG_GUI_UNUSED(ctx);
	RG_GUI_UNUSED(dockspace);
}

RGINLINE void rg_gui_dockspace_dock(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace,
                                    RgGuiWindowState* window, const char* title, RgGuiId window_id, RgGuiDockSlot slot)
{
	if (!ctx || !dockspace || !window)
	{
		return;
	}

	window_id = rg_gui_id_scoped(ctx, window_id);

	if (dockspace->root == 0u)
	{
		dockspace->root = rg_gui_dock_node_alloc(ctx);
		if (dockspace->root == 0u)
		{
			return;
		}
	}

	if (window->docked)
	{
		RgGuiDockSpaceState* current = rg_gui_dockspace_find(ctx, window->dockspace_id);
		if (current)
		{
			rg_gui_dock_detach_window(ctx, current, window);
		}
		else
		{
			window->docked = 0;
			window->dockspace_id = 0u;
			window->dock_node = 0u;
			window->dock_tab = 0u;
			window->dock_drag_pending = 0;
		}
	}

	u32 target = rg_gui_dock_find_leaf_first(ctx, dockspace->root);
	if (target == 0u)
	{
		return;
	}

	rg_gui_dock_attach_slot(ctx, dockspace, target, window, title, window_id, slot);
}

RGINLINE void rg_gui_dock_layout_node_to_bytes(const RgGuiDockLayoutNode* node, u8* out)
{
	rg_bin_store_u32_le(out + 0, node->parent);
	rg_bin_store_u32_le(out + 4, node->child_a);
	rg_bin_store_u32_le(out + 8, node->child_b);
	rg_bin_store_u32_le(out + 12, node->tab_head);
	rg_bin_store_u32_le(out + 16, node->tab_count);
	rg_bin_store_u32_le(out + 20, node->active_tab);
	rg_bin_store_f32_le(out + 24, node->split_ratio);
	rg_bin_store_u32_le(out + 28, (u32)node->split);
}

RGINLINE int rg_gui_dock_layout_node_from_bytes(RgGuiDockLayoutNode* node, const u8* data)
{
	u32 split = rg_bin_load_u32_le(data + 28);
	if (split > (u32)RG_GUI_DOCK_SPLIT_HORZ)
	{
		return 0;
	}
	node->parent = rg_bin_load_u32_le(data + 0);
	node->child_a = rg_bin_load_u32_le(data + 4);
	node->child_b = rg_bin_load_u32_le(data + 8);
	node->tab_head = rg_bin_load_u32_le(data + 12);
	node->tab_count = rg_bin_load_u32_le(data + 16);
	node->active_tab = rg_bin_load_u32_le(data + 20);
	node->split_ratio = rg_bin_load_f32_le(data + 24);
	node->split = (u8)split;
	return 1;
}

RGINLINE void rg_gui_dock_layout_tab_to_bytes(const RgGuiDockLayoutTab* tab, u8* out)
{
	rg_bin_store_u32_le(out + 0, tab->next);
	rg_bin_store_u64_le(out + 4, (u64)tab->window_id);
	rg_bin_store_u32_le(out + 12, 0u);
}

RGINLINE void rg_gui_dock_layout_tab_from_bytes(RgGuiDockLayoutTab* tab, const u8* data)
{
	tab->next = rg_bin_load_u32_le(data + 0);
	tab->window_id = (RgGuiId)rg_bin_load_u64_le(data + 4);
}

static int rg_gui_dock_layout_validate_internal(const RgGuiDockLayout* layout,
                                                const RgGuiDockLayoutNode* nodes,
                                                const RgGuiDockLayoutTab* tabs)
{
	if (!layout || layout->node_count > RG_GUI_MAX_DOCK_NODES ||
	    layout->tab_count > RG_GUI_MAX_DOCK_TABS)
	{
		return 0;
	}

	if (layout->node_count == 0u)
	{
		return layout->root == 0u && layout->tab_count == 0u;
	}
	if (!nodes || layout->root == 0u || layout->root > layout->node_count ||
	    (layout->tab_count > 0u && !tabs))
	{
		return 0;
	}

	u8 incoming[RG_GUI_MAX_DOCK_NODES + 1u] = {0};
	for (u32 i = 1u; i <= layout->node_count; i++)
	{
		const RgGuiDockLayoutNode* node = &nodes[i];
		if (node->parent > layout->node_count ||
		    node->split > RG_GUI_DOCK_SPLIT_HORZ ||
		    !(node->split_ratio >= 0.0f && node->split_ratio <= 1.0f))
		{
			return 0;
		}

		if (node->split == RG_GUI_DOCK_SPLIT_NONE)
		{
			if (node->child_a != 0u || node->child_b != 0u)
			{
				return 0;
			}
			continue;
		}

		if (node->child_a == 0u || node->child_a > layout->node_count ||
		    node->child_b == 0u || node->child_b > layout->node_count ||
		    node->child_a == node->child_b ||
		    !(node->split_ratio > 0.0f && node->split_ratio < 1.0f) ||
		    node->tab_head != 0u || node->tab_count != 0u || node->active_tab != 0u)
		{
			return 0;
		}

		if (++incoming[node->child_a] != 1u || ++incoming[node->child_b] != 1u ||
		    nodes[node->child_a].parent != i || nodes[node->child_b].parent != i)
		{
			return 0;
		}
	}

	if (nodes[layout->root].parent != 0u || incoming[layout->root] != 0u)
	{
		return 0;
	}
	for (u32 i = 1u; i <= layout->node_count; i++)
	{
		if (i != layout->root && (nodes[i].parent == 0u || incoming[i] != 1u))
		{
			return 0;
		}
	}

	u8 node_seen[RG_GUI_MAX_DOCK_NODES + 1u] = {0};
	u32 node_stack[RG_GUI_MAX_DOCK_NODES + 1u];
	u32 stack_count = 1u;
	u32 reached = 0u;
	node_stack[0] = layout->root;
	while (stack_count > 0u)
	{
		u32 index = node_stack[--stack_count];
		if (node_seen[index])
		{
			return 0;
		}
		node_seen[index] = 1u;
		reached++;
		if (nodes[index].split != RG_GUI_DOCK_SPLIT_NONE)
		{
			node_stack[stack_count++] = nodes[index].child_a;
			node_stack[stack_count++] = nodes[index].child_b;
		}
	}
	if (reached != layout->node_count)
	{
		return 0;
	}

	u8 tab_seen[RG_GUI_MAX_DOCK_TABS + 1u] = {0};
	u32 tabs_reached = 0u;
	for (u32 i = 1u; i <= layout->node_count; i++)
	{
		const RgGuiDockLayoutNode* node = &nodes[i];
		if (node->split != RG_GUI_DOCK_SPLIT_NONE)
		{
			continue;
		}
		if (node->tab_count == 0u)
		{
			if (node->tab_head != 0u || node->active_tab != 0u)
			{
				return 0;
			}
			continue;
		}
		if (!tabs || node->tab_head == 0u || node->tab_head > layout->tab_count ||
		    node->active_tab == 0u || node->active_tab > layout->tab_count)
		{
			return 0;
		}

		u32 tab = node->tab_head;
		int active_seen = 0;
		for (u32 n = 0u; n < node->tab_count; n++)
		{
			if (tab == 0u || tab > layout->tab_count || tab_seen[tab])
			{
				return 0;
			}
			tab_seen[tab] = 1u;
			tabs_reached++;
			active_seen |= (tab == node->active_tab);
			tab = tabs[tab].next;
		}
		if (tab != 0u || !active_seen)
		{
			return 0;
		}
	}

	return tabs_reached == layout->tab_count;
}

RGINLINE int rg_gui_dock_layout_validate(const RgGuiDockLayout* layout,
                                         const RgGuiDockLayoutNode* nodes,
                                         const RgGuiDockLayoutTab* tabs)
{
	return rg_gui_dock_layout_validate_internal(layout, nodes, tabs);
}

RGINLINE int rg_gui_dock_layout_save(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, RgGuiDockLayout* out_layout,
                                     RgGuiDockLayoutNode* out_nodes, u32 node_capacity,
                                     RgGuiDockLayoutTab* out_tabs, u32 tab_capacity)
{
	if (!ctx || !dockspace || !out_layout)
	{
		return 0;
	}

	out_layout->dockspace_id = dockspace->id;
	out_layout->root = 0u;
	out_layout->node_count = 0u;
	out_layout->tab_count = 0u;

	if (dockspace->root == 0u)
	{
		return 1;
	}

	u32 node_map[RG_GUI_MAX_DOCK_NODES + 1u] = {0};
	u32 tab_map[RG_GUI_MAX_DOCK_TABS + 1u] = {0};
	u32 node_count = 0u;
	u32 tab_count = 0u;

	rg_gui_dock_layout_collect(ctx, dockspace->root, node_map, &node_count, tab_map, &tab_count);

	out_layout->node_count = node_count;
	out_layout->tab_count = tab_count;
	out_layout->root = node_map[dockspace->root];

	if (out_layout->root == 0u)
	{
		return 0;
	}

	if (!out_nodes || !out_tabs || node_count > node_capacity || tab_count > tab_capacity)
	{
		return 0;
	}

	memset(out_nodes, 0, sizeof(RgGuiDockLayoutNode) * (node_count + 1u));
	memset(out_tabs, 0, sizeof(RgGuiDockLayoutTab) * (tab_count + 1u));

	for (u32 i = 1u; i <= ctx->dock_node_capacity; i++)
	{
		u32 mapped = node_map[i];
		if (mapped == 0u)
		{
			continue;
		}

		const RgGuiDockNode* src = &ctx->dock_nodes[i];
		RgGuiDockLayoutNode* dst = &out_nodes[mapped];
		dst->parent = (src->parent > 0u) ? node_map[src->parent] : 0u;
		dst->child_a = (src->child_a > 0u) ? node_map[src->child_a] : 0u;
		dst->child_b = (src->child_b > 0u) ? node_map[src->child_b] : 0u;
		dst->tab_head = (src->tab_head > 0u) ? tab_map[src->tab_head] : 0u;
		dst->tab_count = src->tab_count;
		dst->active_tab = (src->active_tab > 0u) ? tab_map[src->active_tab] : 0u;
		dst->split_ratio = src->split_ratio;
		dst->split = src->split;
	}

	for (u32 i = 1u; i <= ctx->dock_tab_capacity; i++)
	{
		u32 mapped = tab_map[i];
		if (mapped == 0u)
		{
			continue;
		}

		const RgGuiDockTab* src = &ctx->dock_tabs[i];
		RgGuiDockLayoutTab* dst = &out_tabs[mapped];
		dst->next = (src->next > 0u) ? tab_map[src->next] : 0u;
		dst->window_id = src->window_id;
	}

	return 1;
}

static int rg_gui_dock_layout_load_validated(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace,
                                             const RgGuiDockLayout* layout,
                                             const RgGuiDockLayoutNode* nodes,
                                             const RgGuiDockLayoutTab* tabs)
{
	if (!ctx || !dockspace || !layout || !nodes || !tabs || !ctx->dock_nodes || !ctx->dock_tabs)
	{
		return 0;
	}

	if (layout->dockspace_id != 0u && dockspace->id != 0u && layout->dockspace_id != dockspace->id)
	{
		return 0;
	}

	if (layout->node_count > ctx->dock_node_capacity || layout->tab_count > ctx->dock_tab_capacity)
	{
		return 0;
	}
	rg_gui_dock_reset(ctx);

	if (layout->node_count == 0u)
	{
		dockspace->root = 0u;
		dockspace->initialized = 1;
		return 1;
	}

	ctx->dock_node_count = layout->node_count;
	ctx->dock_tab_count = layout->tab_count;

	for (u32 i = 1u; i <= layout->node_count; i++)
	{
		const RgGuiDockLayoutNode* src = &nodes[i];
		RgGuiDockNode* dst = &ctx->dock_nodes[i];
		dst->parent = src->parent;
		dst->child_a = src->child_a;
		dst->child_b = src->child_b;
		dst->tab_head = src->tab_head;
		dst->tab_count = src->tab_count;
		dst->active_tab = src->active_tab;
		dst->split_ratio = src->split_ratio;
		dst->split = src->split;
	}

	for (u32 i = 1u; i <= layout->tab_count; i++)
	{
		const RgGuiDockLayoutTab* src = &tabs[i];
		RgGuiDockTab* dst = &ctx->dock_tabs[i];
		dst->next = src->next;
		dst->window_id = src->window_id;
		dst->window = NULL;
		dst->title = NULL;
	}

	dockspace->root = layout->root;
	dockspace->initialized = 1;

	return 1;
}

RGINLINE int rg_gui_dock_layout_load(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace,
                                     const RgGuiDockLayout* layout,
                                     const RgGuiDockLayoutNode* nodes,
                                     const RgGuiDockLayoutTab* tabs)
{
	if (!ctx || !dockspace || !layout || !nodes || !tabs ||
	    !ctx->dock_nodes || !ctx->dock_tabs ||
	    !rg_gui_dock_layout_validate(layout, nodes, tabs))
	{
		return 0;
	}
	return rg_gui_dock_layout_load_validated(ctx, dockspace, layout, nodes, tabs);
}

RGINLINE int rg_gui_dock_layout_write(FILE* file, const RgGuiDockLayout* layout,
                                      const RgGuiDockLayoutNode* nodes, const RgGuiDockLayoutTab* tabs)
{
	if (!file || !layout || !nodes || !tabs)
	{
		return 0;
	}
	if (!rg_gui_dock_layout_validate(layout, nodes, tabs))
	{
		return 0;
	}

	u8 header[RG_GUI_DOCK_LAYOUT_HEADER_SIZE];
	rg_bin_store_u32_le(header + 0, RG_GUI_DOCK_LAYOUT_MAGIC);
	rg_bin_store_u32_le(header + 4, RG_GUI_DOCK_LAYOUT_VERSION);
	rg_bin_store_u32_le(header + 8, RG_GUI_DOCK_LAYOUT_HEADER_SIZE);
	rg_bin_store_u32_le(header + 12, layout->node_count);
	rg_bin_store_u32_le(header + 16, layout->tab_count);
	rg_bin_store_u32_le(header + 20, layout->root);
	rg_bin_store_u64_le(header + 24, (u64)layout->dockspace_id);

	if (fwrite(header, 1, sizeof(header), file) != sizeof(header))
	{
		return 0;
	}

	u8 buffer[RG_GUI_DOCK_LAYOUT_NODE_SIZE];
	for (u32 i = 1u; i <= layout->node_count; i++)
	{
		rg_gui_dock_layout_node_to_bytes(&nodes[i], buffer);
		if (fwrite(buffer, 1, sizeof(buffer), file) != sizeof(buffer))
		{
			return 0;
		}
	}

	u8 tab_buffer[RG_GUI_DOCK_LAYOUT_TAB_SIZE];
	for (u32 i = 1u; i <= layout->tab_count; i++)
	{
		rg_gui_dock_layout_tab_to_bytes(&tabs[i], tab_buffer);
		if (fwrite(tab_buffer, 1, sizeof(tab_buffer), file) != sizeof(tab_buffer))
		{
			return 0;
		}
	}

	return 1;
}

RGINLINE int rg_gui_dock_layout_read(FILE* file, RgGuiDockLayout* layout,
                                     RgGuiDockLayoutNode* nodes, u32 node_capacity,
                                     RgGuiDockLayoutTab* tabs, u32 tab_capacity)
{
	if (!file || !layout)
	{
		return 0;
	}

	u8 header[RG_GUI_DOCK_LAYOUT_HEADER_SIZE];
	if (fread(header, 1, sizeof(header), file) != sizeof(header))
	{
		return 0;
	}

	u32 magic = rg_bin_load_u32_le(header + 0);
	u32 version = rg_bin_load_u32_le(header + 4);
	u32 header_size = rg_bin_load_u32_le(header + 8);
	if (magic != RG_GUI_DOCK_LAYOUT_MAGIC || version != RG_GUI_DOCK_LAYOUT_VERSION)
	{
		return 0;
	}
	if (header_size < RG_GUI_DOCK_LAYOUT_HEADER_SIZE)
	{
		return 0;
	}

	layout->node_count = rg_bin_load_u32_le(header + 12);
	layout->tab_count = rg_bin_load_u32_le(header + 16);
	layout->root = rg_bin_load_u32_le(header + 20);
	layout->dockspace_id = (RgGuiId)rg_bin_load_u64_le(header + 24);

	if ((layout->node_count > 0u || layout->tab_count > 0u) && (!nodes || !tabs))
	{
		return 0;
	}

	if (layout->node_count > node_capacity || layout->tab_count > tab_capacity ||
	    layout->node_count > RG_GUI_MAX_DOCK_NODES || layout->tab_count > RG_GUI_MAX_DOCK_TABS)
	{
		return 0;
	}

	if (header_size > RG_GUI_DOCK_LAYOUT_HEADER_SIZE)
	{
		u32 skip = header_size - RG_GUI_DOCK_LAYOUT_HEADER_SIZE;
		if ((u64)skip > (u64)LONG_MAX)
		{
			return 0;
		}
		if (fseek(file, (long)skip, SEEK_CUR) != 0)
		{
			return 0;
		}
	}

	if (nodes)
	{
		memset(nodes, 0, sizeof(RgGuiDockLayoutNode) * (layout->node_count + 1u));
	}
	if (tabs)
	{
		memset(tabs, 0, sizeof(RgGuiDockLayoutTab) * (layout->tab_count + 1u));
	}

	u8 buffer[RG_GUI_DOCK_LAYOUT_NODE_SIZE];
	for (u32 i = 1u; i <= layout->node_count; i++)
	{
		if (fread(buffer, 1, sizeof(buffer), file) != sizeof(buffer))
		{
			return 0;
		}
		if (!rg_gui_dock_layout_node_from_bytes(&nodes[i], buffer))
		{
			return 0;
		}
	}

	u8 tab_buffer[RG_GUI_DOCK_LAYOUT_TAB_SIZE];
	for (u32 i = 1u; i <= layout->tab_count; i++)
	{
		if (fread(tab_buffer, 1, sizeof(tab_buffer), file) != sizeof(tab_buffer))
		{
			return 0;
		}
		rg_gui_dock_layout_tab_from_bytes(&tabs[i], tab_buffer);
	}

	return rg_gui_dock_layout_validate(layout, nodes, tabs);
}

RGINLINE int rg_gui_dock_layout_save_file(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, FILE* file,
                                          RgGuiDockLayout* out_layout,
                                          RgGuiDockLayoutNode* out_nodes, u32 node_capacity,
                                          RgGuiDockLayoutTab* out_tabs, u32 tab_capacity)
{
	if (!file)
	{
		return 0;
	}

	if (!rg_gui_dock_layout_save(ctx, dockspace, out_layout, out_nodes, node_capacity, out_tabs, tab_capacity))
	{
		return 0;
	}

	return rg_gui_dock_layout_write(file, out_layout, out_nodes, out_tabs);
}

RGINLINE int rg_gui_dock_layout_load_file(RgGuiContext* ctx, RgGuiDockSpaceState* dockspace, FILE* file,
                                          RgGuiDockLayout* layout,
                                          RgGuiDockLayoutNode* nodes, u32 node_capacity,
                                          RgGuiDockLayoutTab* tabs, u32 tab_capacity)
{
	if (!file)
	{
		return 0;
	}

	if (!rg_gui_dock_layout_read(file, layout, nodes, node_capacity, tabs, tab_capacity))
	{
		return 0;
	}

	return rg_gui_dock_layout_load_validated(ctx, dockspace, layout, nodes, tabs);
}

RGINLINE u32 rg_gui_row_cache_block_count(u32 row_count)
{
	return row_count / RG_GUI_ROW_CACHE_BLOCK +
	       (row_count % RG_GUI_ROW_CACHE_BLOCK != 0u);
}

RGINLINE void rg_gui_row_cache_init(RgGuiRowCache* cache, f32* row_spans, f32* block_spans,
                                    u32 row_capacity, u32 block_capacity)
{
	if (!cache)
	{
		return;
	}

	cache->row_spans = row_spans;
	cache->block_spans = block_spans;
	cache->row_capacity = row_capacity;
	cache->block_capacity = block_capacity;
	cache->row_count = 0u;
	cache->block_count = 0u;
	cache->default_span = 0.0f;
	cache->spacing = 0.0f;
	cache->total_span = 0.0f;
}

RGINLINE int rg_gui_row_cache_reset(RgGuiRowCache* cache, u32 row_count, f32 row_height, f32 spacing)
{
	if (!cache || !cache->row_spans || !cache->block_spans)
	{
		return 0;
	}

	f32 span = row_height + spacing;
	if (span <= 0.0f)
	{
		cache->row_count = 0u;
		cache->block_count = 0u;
		cache->default_span = 0.0f;
		cache->spacing = spacing;
		cache->total_span = 0.0f;
		return 0;
	}

	u32 block_count = rg_gui_row_cache_block_count(row_count);
	if (row_count > cache->row_capacity || block_count > cache->block_capacity)
	{
		cache->row_count = 0u;
		cache->block_count = 0u;
		cache->default_span = span;
		cache->spacing = spacing;
		cache->total_span = 0.0f;
		return 0;
	}

	cache->row_count = row_count;
	cache->block_count = block_count;
	cache->default_span = span;
	cache->spacing = spacing;
	cache->total_span = span * (f32)row_count;

	if (row_count > 0u)
	{
		memset(cache->row_spans, 0, sizeof(f32) * row_count);
	}

	for (u32 block = 0u; block < block_count; block++)
	{
		u32 base = block * RG_GUI_ROW_CACHE_BLOCK;
		u32 rows = row_count - base;
		if (rows > RG_GUI_ROW_CACHE_BLOCK)
		{
			rows = RG_GUI_ROW_CACHE_BLOCK;
		}
		cache->block_spans[block] = span * (f32)rows;
	}

	return 1;
}

RGINLINE int rg_gui_row_cache_prepare(RgGuiRowCache* cache, u32 row_count, f32 row_height, f32 spacing)
{
	if (!cache || !cache->row_spans || !cache->block_spans)
	{
		return 0;
	}

	f32 span = row_height + spacing;
	if (span <= 0.0f)
	{
		cache->total_span = 0.0f;
		return 0;
	}

	u32 block_count = rg_gui_row_cache_block_count(row_count);
	if (row_count > cache->row_capacity || block_count > cache->block_capacity)
	{
		return 0;
	}

	if (cache->row_count != row_count || cache->default_span != span || cache->spacing != spacing)
	{
		return rg_gui_row_cache_reset(cache, row_count, row_height, spacing);
	}

	return 1;
}

RGINLINE void rg_gui_row_cache_set_height(RgGuiRowCache* cache, u32 index, f32 row_height)
{
	if (!cache || !cache->row_spans || !cache->block_spans || index >= cache->row_count)
	{
		return;
	}

	f32 span = row_height + cache->spacing;
	if (span <= 0.0f)
	{
		span = cache->default_span;
	}

	f32 prev = cache->row_spans[index];
	if (prev <= 0.0f)
	{
		prev = cache->default_span;
	}

	if (span == prev)
	{
		return;
	}

	cache->row_spans[index] = span;

	u32 block = index / RG_GUI_ROW_CACHE_BLOCK;
	if (block < cache->block_count)
	{
		f32 delta = span - prev;
		cache->block_spans[block] += delta;
		cache->total_span += delta;
		if (cache->total_span < 0.0f)
		{
			cache->total_span = 0.0f;
		}
	}
}

RGINLINE f32 rg_gui_row_cache_row_span(const RgGuiRowCache* cache, u32 index)
{
	f32 span = cache->row_spans[index];
	if (span <= 0.0f)
	{
		span = cache->default_span;
	}
	return span;
}

RGINLINE f32 rg_gui_row_cache_block_span(const RgGuiRowCache* cache, u32 row_count, u32 block)
{
	if (!cache || !cache->block_spans)
	{
		return 0.0f;
	}

	u32 base = block * RG_GUI_ROW_CACHE_BLOCK;
	if (base >= row_count)
	{
		return 0.0f;
	}

	u32 rows = row_count - base;
	if (rows > RG_GUI_ROW_CACHE_BLOCK)
	{
		rows = RG_GUI_ROW_CACHE_BLOCK;
	}

	f32 span = cache->block_spans[block];
	if (span <= 0.0f)
	{
		span = cache->default_span * (f32)rows;
	}

	return span;
}

RGINLINE f32 rg_gui_row_cache_total(const RgGuiRowCache* cache)
{
	if (!cache)
	{
		return 0.0f;
	}

	return cache->total_span;
}

RGINLINE void rg_gui_row_cache_range(const RgGuiRowCache* cache, u32 row_count,
                                     f32 view_top, f32 view_bottom, f32 total_height,
                                     u32* out_start, u32* out_count, f32* out_offset)
{
	u32 start = 0u;
	u32 count = 0u;

	if (!cache || row_count == 0u || cache->default_span <= 0.0f)
	{
		if (out_start)
		{
			*out_start = 0u;
		}
		if (out_count)
		{
			*out_count = 0u;
		}
		if (out_offset)
		{
			*out_offset = 0.0f;
		}
		return;
	}

	u32 block_count = cache->block_count;
	f32 target_top = view_top;
	if (target_top < 0.0f)
	{
		target_top = 0.0f;
	}
	if (target_top > total_height)
	{
		target_top = total_height;
	}

	f32 target_bottom = view_bottom;
	if (target_bottom < target_top)
	{
		target_bottom = target_top;
	}
	if (target_bottom > total_height)
	{
		target_bottom = total_height;
	}

	f32 cursor = 0.0f;
	start = 0u;

	if (block_count > 0u && target_top > total_height * 0.5f)
	{
		u32 block = block_count;
		cursor = total_height;
		while (block > 0u)
		{
			u32 prev_block = block - 1u;
			f32 block_span = rg_gui_row_cache_block_span(cache, row_count, prev_block);
			if (cursor - block_span <= target_top)
			{
				block = prev_block;
				cursor -= block_span;
				break;
			}

			cursor -= block_span;
			block = prev_block;
		}

		start = block * RG_GUI_ROW_CACHE_BLOCK;
		if (start > row_count)
		{
			start = row_count;
		}
	}
	else
	{
		for (u32 block = 0u; block < block_count; block++)
		{
			f32 block_span = rg_gui_row_cache_block_span(cache, row_count, block);
			if (cursor + block_span > target_top)
			{
				start = block * RG_GUI_ROW_CACHE_BLOCK;
				break;
			}

			cursor += block_span;
			start = (block + 1u) * RG_GUI_ROW_CACHE_BLOCK;
		}

		if (start > row_count)
		{
			start = row_count;
		}
	}

	if (row_count > 0u && target_top >= total_height)
	{
		start = row_count - 1u;
		cursor = total_height - rg_gui_row_cache_row_span(cache, start);
	}

	while (start < row_count)
	{
		f32 span = rg_gui_row_cache_row_span(cache, start);
		if (cursor + span > target_top)
		{
			break;
		}
		cursor += span;
		start++;
	}

	if (start >= row_count)
	{
		start = row_count > 0u ? row_count - 1u : 0u;
		cursor = (row_count > 0u) ? total_height - rg_gui_row_cache_row_span(cache, start) : 0.0f;
	}

	f32 end_cursor = cursor;
	u32 end = start;
	while (end < row_count && end_cursor < target_bottom)
	{
		end_cursor += rg_gui_row_cache_row_span(cache, end);
		end++;
	}

	if (end > start)
	{
		count = end - start;
	}

	if (row_count > 0u && count == 0u)
	{
		count = 1u;
	}

	if (start + count > row_count)
	{
		count = row_count - start;
	}

	if (out_start)
	{
		*out_start = start;
	}
	if (out_count)
	{
		*out_count = count;
	}
	if (out_offset)
	{
		*out_offset = cursor;
	}
}

RGINLINE f32 rg_gui_row_cache_offset(const RgGuiRowCache* cache, u32 index)
{
	if (!cache || !cache->row_spans || !cache->block_spans || cache->default_span <= 0.0f)
	{
		return 0.0f;
	}

	if (index == 0u)
	{
		return 0.0f;
	}

	u32 row_count = cache->row_count;
	if (index >= row_count)
	{
		return rg_gui_row_cache_total(cache);
	}

	u32 block = index / RG_GUI_ROW_CACHE_BLOCK;
	f32 offset = 0.0f;

	if (block <= cache->block_count / 2u)
	{
		for (u32 b = 0u; b < block && b < cache->block_count; b++)
		{
			offset += rg_gui_row_cache_block_span(cache, row_count, b);
		}

		u32 base = block * RG_GUI_ROW_CACHE_BLOCK;
		for (u32 i = base; i < index; i++)
		{
			offset += rg_gui_row_cache_row_span(cache, i);
		}
	}
	else
	{
		offset = rg_gui_row_cache_total(cache);

		for (u32 b = block + 1u; b < cache->block_count; b++)
		{
			offset -= rg_gui_row_cache_block_span(cache, row_count, b);
		}

		u32 block_end = (block + 1u) * RG_GUI_ROW_CACHE_BLOCK;
		if (block_end > row_count)
		{
			block_end = row_count;
		}

		for (u32 i = index; i < block_end; i++)
		{
			offset -= rg_gui_row_cache_row_span(cache, i);
		}
	}

	return offset;
}

RGINLINE void rg_gui_panel_begin_ex(RgGuiContext* ctx, RgGuiPanelState* panel,
                                    RgGuiRect rect, f32 spacing, RgGuiId id,
                                    const char* title, RgGuiPanelFlags flags)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(panel != NULL);

	id = rg_gui_id_scoped(ctx, id);

	panel->prev_layout = ctx->layout;
	panel->rect = rect;
	panel->enabled = !rg_gui_is_disabled(ctx);

	f32 border = ctx->style.border_thickness;
	f32 pad = ctx->style.padding;
	f32 scroll_w = ctx->style.scroll_bar_width;
	f32 inset = border + pad;
	f32 title_height = (title && title[0]) ? ctx->style.text_height + pad * 2.0f : 0.0f;

	RgGuiRect inner = rect;
	inner.x += inset;
	inner.y += inset + title_height;
	inner.w -= inset * 2.0f;
	inner.h -= inset * 2.0f + title_height;

	if (scroll_w > 0.0f)
	{
		inner.w -= scroll_w + ctx->style.inner_spacing;
	}

	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;

	panel->inner_rect = inner;

	f32 max_scroll = panel->content_height - inner.h;
	if (max_scroll < 0.0f)
	{
		max_scroll = 0.0f;
	}
	panel->max_scroll = max_scroll;

	if (panel->scroll_y < 0.0f) panel->scroll_y = 0.0f;
	if (panel->scroll_y > max_scroll) panel->scroll_y = max_scroll;

	int hovered = panel->enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	panel->hovered = hovered;

	if (panel->enabled && hovered && max_scroll > 0.0f)
	{
		ctx->scroll_owner_next = id;
	}

	if (panel->enabled && hovered && ctx->mouse_wheel != 0.0f && max_scroll > 0.0f && ctx->scroll_owner == id)
	{
		int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
		if (steps != 0)
		{
			f32 step = ctx->style.text_height + ctx->style.padding * 2.0f;
			panel->scroll_y -= (f32)steps * step;
			if (panel->scroll_y < 0.0f) panel->scroll_y = 0.0f;
			if (panel->scroll_y > max_scroll) panel->scroll_y = max_scroll;
		}
	}

	if ((flags & RG_GUI_PANEL_FLOATING) != 0u)
	{
		f32 shadow_offset = pad;
		if (shadow_offset < 1.0f)
		{
			shadow_offset = 1.0f;
		}
		RgGuiRect shadow = rect;
		shadow.x += shadow_offset;
		shadow.y += shadow_offset;
		rg_gui_push_rect(ctx, shadow, ctx->style.color_drop_shadow);
	}

	rg_vec4 body_color = (flags & RG_GUI_PANEL_FLOATING) != 0u ? ctx->style.color_panel_floating : ctx->style.color_panel;
	rg_gui_push_rect(ctx, rect, body_color);

	if (title_height > 0.0f)
	{
		RgGuiRect title_rect = rg_gui_make_rect(rect.x, rect.y, rect.w, title_height);
		rg_vec2 title_pos = rg_vec2(rect.x + inset,
		                            rect.y + (title_height - ctx->style.text_height) * 0.5f);
		rg_gui_push_rect(ctx, title_rect, ctx->style.color_panel_title);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, title, title_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, title, title_pos, ctx->style.color_text);
		}
	}

	rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

	if (scroll_w > 0.0f && max_scroll > 0.0f)
	{
		f32 track_x = rect.x + rect.w - inset - scroll_w;
		f32 track_y = inner.y;
		f32 track_h = inner.h;
		if (track_h < 0.0f) track_h = 0.0f;

		RgGuiRect track = rg_gui_make_rect(track_x, track_y, scroll_w, track_h);

		f32 handle_h = track_h;
		if (panel->content_height > 0.0f)
		{
			handle_h = track_h * (inner.h / panel->content_height);
		}
		f32 min_handle = ctx->style.text_height * 0.5f;
		if (handle_h < min_handle) handle_h = min_handle;
		if (handle_h > track_h) handle_h = track_h;

		f32 scroll_t = 0.0f;
		if (max_scroll > 0.0f && track_h > handle_h)
		{
			scroll_t = panel->scroll_y / max_scroll;
		}
		f32 handle_y = track_y + (track_h - handle_h) * scroll_t;
		RgGuiRect handle = rg_gui_make_rect(track_x, handle_y, scroll_w, handle_h);

		RgGuiId bar_id = rg_gui_id_combine(id, 1u);
		int bar_hovered = panel->enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, track);
		if (bar_hovered)
		{
			ctx->hot_id = bar_id;
		}

		if (panel->enabled && ctx->active_id == bar_id)
		{
			ctx->scroll_owner_next = id;
		}

		if (panel->enabled && bar_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = bar_id;
			ctx->focus_id = id;
		}

		if (!panel->enabled && ctx->active_id == bar_id)
		{
			ctx->active_id = 0u;
		}

		if (panel->enabled && ctx->active_id == bar_id)
		{
			if (ctx->mouse_down)
			{
				f32 track_span = track_h - handle_h;
				f32 t = 0.0f;
				if (track_span > 0.0f)
				{
					t = (ctx->mouse_pos.y - track_y - handle_h * 0.5f) / track_span;
				}
				if (t < 0.0f) t = 0.0f;
				if (t > 1.0f) t = 1.0f;
				panel->scroll_y = t * max_scroll;
			}
			if (ctx->mouse_released)
			{
				ctx->active_id = 0u;
			}
		}

		rg_vec4 track_color = bar_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, track, track_color);
		rg_gui_push_rect_outline(ctx, track, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec4 handle_color = (ctx->active_id == bar_id) ? ctx->style.color_accent : ctx->style.color_bg_active;
		rg_gui_push_rect(ctx, handle, handle_color);
	}

	ctx->layout.x = inner.x;
	ctx->layout.y = inner.y - panel->scroll_y;
	ctx->layout.width = inner.w;
	ctx->layout.spacing = spacing;
	ctx->layout.cursor_y = inner.y - panel->scroll_y;
	ctx->layout.active = 1;
	rg_gui_layout_reset_row(&ctx->layout);

	rg_gui_push_clip(ctx, inner);
}

RGINLINE void rg_gui_panel_begin(RgGuiContext* ctx, RgGuiPanelState* panel,
                                 RgGuiRect rect, f32 spacing, RgGuiId id)
{
	rg_gui_panel_begin_ex(ctx, panel, rect, spacing, id, NULL, RG_GUI_PANEL_NONE);
}

RGINLINE void rg_gui_panel_begin_virtual(RgGuiContext* ctx, RgGuiPanelState* panel, RgGuiRect rect, f32 row_height,
                                         f32 spacing, u32 row_count, RgGuiId id, u32* out_start, u32* out_count)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(panel != NULL);

	f32 row_stride = row_height + spacing;
	f32 total_height = (row_count > 0u && row_stride > 0.0f) ? row_stride * (f32)row_count : 0.0f;
	panel->content_height = total_height;
	panel->content_override = 1;

	rg_gui_panel_begin(ctx, panel, rect, spacing, id);

	u32 first = 0u;
	u32 count = 0u;
	if (row_count > 0u && row_stride > 0.0f)
	{
		f32 view_top = panel->scroll_y;
		f32 view_bottom = panel->scroll_y + panel->inner_rect.h;

		u32 first_index = rg_gui_nonnegative_f32_to_u32_bounded(view_top / row_stride);
		if (first_index >= row_count) first_index = row_count - 1u;

		f32 first_end = (f32)first_index * row_stride + row_height;
		if (first_end < view_top && first_index < row_count - 1u)
		{
			first_index++;
		}

		u32 last_index = rg_gui_nonnegative_f32_to_u32_bounded(view_bottom / row_stride);
		if (last_index >= row_count)
		{
			last_index = row_count - 1u;
		}

		if (first_index > last_index)
		{
			first_index = last_index;
		}

		first = first_index;
		count = last_index - first + 1u;

		f32 offset = (f32)first * row_stride;
		ctx->layout.y += offset;
		ctx->layout.cursor_y += offset;
	}

	if (out_start)
	{
		*out_start = first;
	}
	if (out_count)
	{
		*out_count = count;
	}
}

RGINLINE void rg_gui_panel_begin_clipped(RgGuiContext* ctx, RgGuiPanelState* panel, RgGuiRect rect, f32 row_height,
                                         f32 spacing, u32 row_count, RgGuiId id, RgGuiRowCache* cache,
                                         u32* out_start, u32* out_count)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(panel != NULL);

	if (!rg_gui_row_cache_prepare(cache, row_count, row_height, spacing))
	{
		rg_gui_panel_begin_virtual(ctx, panel, rect, row_height, spacing, row_count, id, out_start, out_count);
		return;
	}

	f32 total_height = rg_gui_row_cache_total(cache);
	panel->content_height = total_height;
	panel->content_override = 1;

	rg_gui_panel_begin(ctx, panel, rect, spacing, id);

	u32 start = 0u;
	u32 count = 0u;
	f32 offset = 0.0f;

	if (row_count > 0u)
	{
		f32 view_top = panel->scroll_y;
		f32 view_bottom = panel->scroll_y + panel->inner_rect.h;
		rg_gui_row_cache_range(cache, row_count, view_top, view_bottom, total_height, &start, &count, &offset);
		ctx->layout.y += offset;
		ctx->layout.cursor_y += offset;
	}

	if (out_start)
	{
		*out_start = start;
	}
	if (out_count)
	{
		*out_count = count;
	}
}

RGINLINE void rg_gui_panel_end(RgGuiContext* ctx, RgGuiPanelState* panel)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(panel != NULL);

	f32 content_height = panel->content_height;
	if (!panel->content_override)
	{
		f32 start_y = panel->inner_rect.y - panel->scroll_y;
		content_height = ctx->layout.cursor_y - start_y;
		if (content_height < 0.0f)
		{
			content_height = 0.0f;
		}
		panel->content_height = content_height;
	}
	panel->content_override = 0;

	f32 max_scroll = panel->content_height - panel->inner_rect.h;
	if (max_scroll < 0.0f)
	{
		max_scroll = 0.0f;
	}
	panel->max_scroll = max_scroll;
	if (panel->scroll_y > max_scroll)
	{
		panel->scroll_y = max_scroll;
	}

	ctx->layout = panel->prev_layout;
	rg_gui_pop_clip(ctx);
}

RGINLINE void rg_gui_split_rect_v(RgGuiRect rect, f32 split, f32 gutter,
                                  RgGuiRect* out_left, RgGuiRect* out_right, RgGuiRect* out_splitter)
{
	if (gutter < 0.0f)
	{
		gutter = 0.0f;
	}

	f32 max_split = rect.w - gutter;
	if (max_split < 0.0f)
	{
		max_split = 0.0f;
	}
	if (split < 0.0f)
	{
		split = 0.0f;
	}
	if (split > max_split)
	{
		split = max_split;
	}

	f32 right_w = rect.w - split - gutter;
	if (right_w < 0.0f)
	{
		right_w = 0.0f;
	}

	if (out_left)
	{
		*out_left = rg_gui_make_rect(rect.x, rect.y, split, rect.h);
	}
	if (out_splitter)
	{
		*out_splitter = rg_gui_make_rect(rect.x + split, rect.y, gutter, rect.h);
	}
	if (out_right)
	{
		*out_right = rg_gui_make_rect(rect.x + split + gutter, rect.y, right_w, rect.h);
	}
}

RGINLINE void rg_gui_split_rect_h(RgGuiRect rect, f32 split, f32 gutter,
                                  RgGuiRect* out_top, RgGuiRect* out_bottom, RgGuiRect* out_splitter)
{
	if (gutter < 0.0f)
	{
		gutter = 0.0f;
	}

	f32 max_split = rect.h - gutter;
	if (max_split < 0.0f)
	{
		max_split = 0.0f;
	}
	if (split < 0.0f)
	{
		split = 0.0f;
	}
	if (split > max_split)
	{
		split = max_split;
	}

	f32 bottom_h = rect.h - split - gutter;
	if (bottom_h < 0.0f)
	{
		bottom_h = 0.0f;
	}

	if (out_top)
	{
		*out_top = rg_gui_make_rect(rect.x, rect.y, rect.w, split);
	}
	if (out_splitter)
	{
		*out_splitter = rg_gui_make_rect(rect.x, rect.y + split, rect.w, gutter);
	}
	if (out_bottom)
	{
		*out_bottom = rg_gui_make_rect(rect.x, rect.y + split + gutter, rect.w, bottom_h);
	}
}

RGINLINE int rg_gui_splitter_v(RgGuiContext* ctx, RgGuiRect rect, f32* size, f32 min_size, f32 max_size, RgGuiId id)
{
	if (!ctx || !size)
	{
		return 0;
	}

	id = rg_gui_id_scoped(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
	}

	int changed = 0;
	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}
	if (enabled && ctx->active_id == id)
	{
		if (ctx->mouse_down && ctx->input)
		{
			f32 delta = (f32)ctx->input->mouse_delta_x;
			if (delta != 0.0f)
			{
				f32 next = *size + delta;
				if (max_size < min_size)
				{
					max_size = min_size;
				}
				if (next < min_size)
				{
					next = min_size;
				}
				if (next > max_size)
				{
					next = max_size;
				}
				if (next != *size)
				{
					*size = next;
					changed = 1;
				}
			}
		}
		if (ctx->mouse_released)
		{
			ctx->active_id = 0u;
		}
	}

	rg_vec4 color = ctx->style.color_border;
	if (enabled && ctx->active_id == id)
	{
		color = ctx->style.color_accent;
	}
	else if (enabled && hovered)
	{
		color = ctx->style.color_bg_hover;
	}

	rg_gui_push_rect(ctx, rect, color);
	return changed;
}

RGINLINE int rg_gui_splitter_h(RgGuiContext* ctx, RgGuiRect rect, f32* size, f32 min_size, f32 max_size, RgGuiId id)
{
	if (!ctx || !size)
	{
		return 0;
	}

	id = rg_gui_id_scoped(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
	}

	int changed = 0;
	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}
	if (enabled && ctx->active_id == id)
	{
		if (ctx->mouse_down && ctx->input)
		{
			f32 delta = (f32)ctx->input->mouse_delta_y;
			if (delta != 0.0f)
			{
				f32 next = *size + delta;
				if (max_size < min_size)
				{
					max_size = min_size;
				}
				if (next < min_size)
				{
					next = min_size;
				}
				if (next > max_size)
				{
					next = max_size;
				}
				if (next != *size)
				{
					*size = next;
					changed = 1;
				}
			}
		}
		if (ctx->mouse_released)
		{
			ctx->active_id = 0u;
		}
	}

	rg_vec4 color = ctx->style.color_border;
	if (enabled && ctx->active_id == id)
	{
		color = ctx->style.color_accent;
	}
	else if (enabled && hovered)
	{
		color = ctx->style.color_bg_hover;
	}

	rg_gui_push_rect(ctx, rect, color);
	return changed;
}

RGINLINE void rg_gui_begin_disabled(RgGuiContext* ctx, int disabled)
{
	if (!ctx)
	{
		return;
	}

	if (ctx->disabled_stack_top >= RG_GUI_DISABLED_STACK_MAX)
	{
		ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_OVERFLOW;
		RG_GUI_ASSERT(0 && "rg_gui_begin_disabled stack overflow");
		return;
	}

	ctx->disabled_stack[ctx->disabled_stack_top++] = (u8)(disabled != 0);
	if (disabled)
	{
		ctx->disabled_depth++;
	}
}

RGINLINE void rg_gui_end_disabled(RgGuiContext* ctx)
{
	if (!ctx || ctx->disabled_stack_top == 0u)
	{
		if (ctx)
		{
			ctx->diagnostics.flags |= RG_GUI_DIAGNOSTIC_STACK_UNDERFLOW;
		}
		RG_GUI_ASSERT(0 && "rg_gui_end_disabled without begin");
		return;
	}

	u8 was_disabled = ctx->disabled_stack[--ctx->disabled_stack_top];
	if (was_disabled && ctx->disabled_depth > 0u)
	{
		ctx->disabled_depth--;
	}
}

RGINLINE int rg_gui_button_internal(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                    RgGuiRect rect, RgGuiId id, int copy_label)
{
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
		ctx->focus_id = id;
	}

	int pressed = 0;
	int focused = enabled && (ctx->focus_id == id);
	if (focused &&
	    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE)))
	{
		pressed = 1;
	}

	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}
	if (enabled && ctx->active_id == id)
	{
		if (ctx->mouse_released)
		{
			if (hovered)
			{
				pressed = 1;
			}
			ctx->active_id = 0u;
		}
	}

	rg_vec4 bg = ctx->style.color_bg;
	if (enabled && ctx->active_id == id)
	{
		bg = ctx->style.color_bg_active;
	}
	else if (enabled && hovered)
	{
		bg = ctx->style.color_bg_hover;
	}

	rg_gui_push_rect(ctx, rect, bg);
	rg_gui_push_rect_outline(ctx, rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	f32 text_x = rect.x + ctx->style.padding;
	if (rg_gui_icon_valid(icon))
	{
		f32 icon_x = text_x;
		if (!label || !*label)
		{
			f32 icon_size = rg_gui_icon_size(ctx, rect);
			icon_x = rect.x + (rect.w - icon_size) * 0.5f;
		}
		RgGuiRect icon_rect = rg_gui_icon_rect(ctx, rect, icon_x);
		rg_gui_push_icon(ctx, icon, icon_rect, 0);
		if (label && *label)
		{
			text_x = icon_rect.x + icon_rect.w + ctx->style.inner_spacing;
		}
	}

	if (label)
	{
		rg_vec2 pos = rg_vec2(text_x,
		                      rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (copy_label)
		{
			rg_gui_push_text(ctx, label, pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, pos, ctx->style.color_text);
		}
	}

	return pressed;
}

RGINLINE int rg_gui_button(RgGuiContext* ctx, const char* label, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_button_internal(ctx, label, NULL, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE int rg_gui_button_static(RgGuiContext* ctx, const char* label, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_button_internal(ctx, label, NULL, rect, id, 0);
}

RGINLINE int rg_gui_button_icon(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_button_internal(ctx, label, icon, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE int rg_gui_button_icon_static(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                       RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_button_internal(ctx, label, icon, rect, id, 0);
}

RGINLINE int rg_gui_button_arrow(RgGuiContext* ctx, RgGuiArrowDirection direction,
                                 RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	int pressed = rg_gui_button_internal(ctx, NULL, NULL, rect, id, 0);
	rg_gui_push_arrow(ctx, rect, direction, ctx->style.color_text);
	return pressed;
}

RGINLINE void rg_gui_label_internal(RgGuiContext* ctx, const char* text, const RgGuiIcon* icon,
                                    RgGuiRect rect, int copy_text)
{
	if (!text && !rg_gui_icon_valid(icon))
	{
		return;
	}

	f32 text_x = rect.x + ctx->style.padding;
	if (rg_gui_icon_valid(icon))
	{
		f32 icon_x = text_x;
		if (!text || !*text)
		{
			f32 icon_size = rg_gui_icon_size(ctx, rect);
			icon_x = rect.x + (rect.w - icon_size) * 0.5f;
		}
		RgGuiRect icon_rect = rg_gui_icon_rect(ctx, rect, icon_x);
		rg_gui_push_icon(ctx, icon, icon_rect, 0);
		if (text && *text)
		{
			text_x = icon_rect.x + icon_rect.w + ctx->style.inner_spacing;
		}
	}

	if (!text)
	{
		return;
	}

	rg_vec2 pos = rg_vec2(text_x, rect.y + (rect.h - ctx->style.text_height) * 0.5f);
	if (copy_text)
	{
		rg_gui_push_text(ctx, text, pos, ctx->style.color_text);
	}
	else
	{
		rg_gui_push_text_static(ctx, text, pos, ctx->style.color_text);
	}
}

RGINLINE void rg_gui_label(RgGuiContext* ctx, const char* text, RgGuiRect rect)
{
	rg_gui_label_internal(ctx, text, NULL, rect, RG_GUI_LABEL_COPY);
}

RGINLINE void rg_gui_label_static(RgGuiContext* ctx, const char* text, RgGuiRect rect)
{
	rg_gui_label_internal(ctx, text, NULL, rect, 0);
}

RGINLINE void rg_gui_label_icon(RgGuiContext* ctx, const char* text, const RgGuiIcon* icon, RgGuiRect rect)
{
	rg_gui_label_internal(ctx, text, icon, rect, RG_GUI_LABEL_COPY);
}

RGINLINE void rg_gui_label_icon_static(RgGuiContext* ctx, const char* text, const RgGuiIcon* icon, RgGuiRect rect)
{
	rg_gui_label_internal(ctx, text, icon, rect, 0);
}

RGINLINE void rg_gui_image(RgGuiContext* ctx, RgGuiTexture texture, RgGuiRect rect)
{
	rg_gui_image_material(ctx, texture, rect, 0u);
}

RGINLINE void rg_gui_image_material(RgGuiContext* ctx, RgGuiTexture texture,
                                    RgGuiRect rect, RgGuiImageMaterial material)
{
	if (!ctx)
	{
		return;
	}

	RgGuiRect uv = rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f);
	rg_gui_push_image_material(ctx, rect, uv,
	                           rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f),
	                           texture, material);
}

RGINLINE void rg_gui_image_ex(RgGuiContext* ctx, RgGuiTexture texture, RgGuiRect rect, RgGuiRect uv, rg_vec4 tint)
{
	rg_gui_image_ex_material(ctx, texture, rect, uv, tint, 0u);
}

RGINLINE void rg_gui_image_ex_material(RgGuiContext* ctx, RgGuiTexture texture,
                                       RgGuiRect rect, RgGuiRect uv, rg_vec4 tint,
                                       RgGuiImageMaterial material)
{
	if (!ctx)
	{
		return;
	}

	rg_gui_push_image_material(ctx, rect, uv, tint, texture, material);
}

RGINLINE int rg_gui_image_button_ex(RgGuiContext* ctx, RgGuiTexture texture, RgGuiRect rect, RgGuiRect uv, rg_vec4 tint, RgGuiId id)
{
	return rg_gui_image_button_ex_material(ctx, texture, rect, uv, tint, id, 0u);
}

RGINLINE int rg_gui_image_button_ex_material(RgGuiContext* ctx, RgGuiTexture texture,
                                             RgGuiRect rect, RgGuiRect uv, rg_vec4 tint,
                                             RgGuiId id, RgGuiImageMaterial material)
{
	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && ctx->tab_focus_id == id && ctx->focus_id != id)
	{
		ctx->focus_id = id;
	}
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
		ctx->focus_id = id;
	}

	int pressed = 0;
	int focused = enabled && (ctx->focus_id == id);
	if (focused &&
	    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE)))
	{
		pressed = 1;
	}

	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}
	if (enabled && ctx->active_id == id)
	{
		if (ctx->mouse_released)
		{
			if (hovered)
			{
				pressed = 1;
			}
			ctx->active_id = 0u;
		}
	}

	rg_vec4 bg = ctx->style.color_bg;
	if (enabled && ctx->active_id == id)
	{
		bg = ctx->style.color_bg_active;
	}
	else if (enabled && hovered)
	{
		bg = ctx->style.color_bg_hover;
	}

	rg_gui_push_rect(ctx, rect, bg);
	rg_gui_push_rect_outline(ctx, rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	f32 inset = ctx->style.border_thickness + ctx->style.padding;
	RgGuiRect inner = rg_gui_make_rect(rect.x + inset, rect.y + inset,
	                                   rect.w - inset * 2.0f, rect.h - inset * 2.0f);
	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;
	rg_gui_push_image_material(ctx, inner, uv, tint, texture, material);

	return pressed;
}

RGINLINE int rg_gui_image_button(RgGuiContext* ctx, RgGuiTexture texture, RgGuiRect rect, RgGuiId id)
{
	return rg_gui_image_button_material(ctx, texture, rect, id, 0u);
}

RGINLINE int rg_gui_image_button_material(RgGuiContext* ctx, RgGuiTexture texture,
                                          RgGuiRect rect, RgGuiId id,
                                          RgGuiImageMaterial material)
{
	RgGuiRect uv = rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f);
	return rg_gui_image_button_ex_material(
	    ctx, texture, rect, uv, rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f), id, material);
}

RGINLINE int rg_gui_selectable_internal(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                        int selected, RgGuiRect rect, RgGuiId id, int copy_label)
{
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	int activated = 0;
	int focused = enabled && (ctx->focus_id == id);
	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
		ctx->focus_id = id;
	}

	if (focused &&
	    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE)))
	{
		activated = 1;
	}

	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}
	if (enabled && ctx->active_id == id)
	{
		if (ctx->mouse_released)
		{
			if (hovered)
			{
				activated = 1;
			}
			ctx->active_id = 0u;
		}
	}

	rg_vec4 bg = ctx->style.color_bg;
	int draw_bg = 0;
	if (enabled && ctx->active_id == id)
	{
		bg = ctx->style.color_bg_active;
		draw_bg = 1;
	}
	else if (selected)
	{
		bg = ctx->style.color_bg_active;
		draw_bg = 1;
	}
	else if (enabled && (hovered || focused))
	{
		bg = ctx->style.color_bg_hover;
		draw_bg = 1;
	}

	if (draw_bg)
	{
		rg_gui_push_rect(ctx, rect, bg);
	}

	f32 text_x = rect.x + ctx->style.padding;
	if (rg_gui_icon_valid(icon))
	{
		f32 icon_x = text_x;
		if (!label || !*label)
		{
			f32 icon_size = rg_gui_icon_size(ctx, rect);
			icon_x = rect.x + (rect.w - icon_size) * 0.5f;
		}
		RgGuiRect icon_rect = rg_gui_icon_rect(ctx, rect, icon_x);
		rg_gui_push_icon(ctx, icon, icon_rect, 0);
		if (label && *label)
		{
			text_x = icon_rect.x + icon_rect.w + ctx->style.inner_spacing;
		}
	}

	if (label)
	{
		rg_vec2 pos = rg_vec2(text_x,
		                      rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (copy_label)
		{
			rg_gui_push_text(ctx, label, pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, pos, ctx->style.color_text);
		}
	}

	return activated;
}

RGINLINE int rg_gui_selectable(RgGuiContext* ctx, const char* label, int selected, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_selectable_internal(ctx, label, NULL, selected, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE int rg_gui_selectable_static(RgGuiContext* ctx, const char* label, int selected, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_selectable_internal(ctx, label, NULL, selected, rect, id, 0);
}

RGINLINE int rg_gui_selectable_icon(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                    int selected, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_selectable_internal(ctx, label, icon, selected, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE int rg_gui_selectable_icon_static(RgGuiContext* ctx, const char* label, const RgGuiIcon* icon,
                                           int selected, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_selectable_internal(ctx, label, icon, selected, rect, id, 0);
}

RGINLINE int rg_gui_drag_source(RgGuiContext* ctx, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (!ctx->drag_active && enabled && ctx->mouse_pressed && hovered)
	{
		ctx->drag_source_id = id;
		ctx->drag_start = ctx->mouse_pos;
		ctx->drag_payload.type = 0u;
		ctx->drag_payload.data = NULL;
		ctx->drag_payload.size = 0u;
	}

	if (!ctx->drag_active && ctx->drag_source_id == id && ctx->mouse_down)
	{
		f32 dx = ctx->mouse_pos.x - ctx->drag_start.x;
		f32 dy = ctx->mouse_pos.y - ctx->drag_start.y;
		f32 threshold = RG_GUI_DRAG_THRESHOLD;
		if (dx * dx + dy * dy >= threshold * threshold)
		{
			ctx->drag_active = 1;
		}
	}

	if (!ctx->mouse_down && ctx->drag_source_id == id && !ctx->drag_active)
	{
		ctx->drag_source_id = 0u;
	}

	return (ctx->drag_active && ctx->drag_source_id == id);
}

RGINLINE int rg_gui_drag_set_payload(RgGuiContext* ctx, RgGuiId type, const void* data, size_t size)
{
	if (!ctx->drag_active || ctx->drag_source_id == 0u || type == 0u)
	{
		return 0;
	}

	if (size == 0u)
	{
		ctx->drag_payload.type = type;
		ctx->drag_payload.size = 0u;
		ctx->drag_payload.data = NULL;
		return 1;
	}

	if (!data)
	{
		ctx->drag_payload.type = 0u;
		ctx->drag_payload.size = 0u;
		ctx->drag_payload.data = NULL;
		return 0;
	}

	ctx->drag_payload.type = type;
	ctx->drag_payload.size = size;

	if (size <= RG_GUI_DRAG_PAYLOAD_SIZE)
	{
		memcpy(ctx->drag_payload_storage.bytes, data, size);
		ctx->drag_payload.data = ctx->drag_payload_storage.bytes;
	}
	else
	{
		ctx->drag_payload.data = data;
	}

	return 1;
}

RGINLINE const RgGuiDragPayload* rg_gui_drag_payload(const RgGuiContext* ctx)
{
	if (!ctx || !ctx->drag_active || ctx->drag_payload.type == 0u)
	{
		return NULL;
	}

	return &ctx->drag_payload;
}

RGINLINE int rg_gui_drag_accept(RgGuiContext* ctx, RgGuiDragPayload* out_payload)
{
	if (!ctx || !ctx->drag_active || ctx->drag_payload.type == 0u)
	{
		return 0;
	}

	if (out_payload)
	{
		*out_payload = ctx->drag_payload;
	}

	rg_gui_drag_clear(ctx);
	ctx->mouse_pressed = 0;
	ctx->mouse_released = 0;
	return 1;
}

RGINLINE void rg_gui_drag_reject(RgGuiContext* ctx)
{
	if (!ctx || !ctx->drag_active)
	{
		return;
	}

	rg_gui_drag_clear(ctx);
	ctx->mouse_pressed = 0;
	ctx->mouse_released = 0;
}

RGINLINE RgGuiRect rg_gui_drag_preview_rect(const RgGuiContext* ctx, f32 width, f32 height,
                                            rg_vec2 offset, int clamp_to_window)
{
	RgGuiRect rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	if (!ctx)
	{
		return rect;
	}

	rect.x = ctx->mouse_pos_raw.x + offset.x;
	rect.y = ctx->mouse_pos_raw.y + offset.y;
	rect.w = width;
	rect.h = height;
	if (rect.w < 0.0f)
	{
		rect.w = 0.0f;
	}
	if (rect.h < 0.0f)
	{
		rect.h = 0.0f;
	}

	if (clamp_to_window && ctx->window_bounds_set)
	{
		RgGuiRect bounds = ctx->window_bounds;
		if (rect.w > bounds.w)
		{
			rect.w = bounds.w;
		}
		if (rect.h > bounds.h)
		{
			rect.h = bounds.h;
		}

		if (rect.x < bounds.x)
		{
			rect.x = bounds.x;
		}
		if (rect.y < bounds.y)
		{
			rect.y = bounds.y;
		}

		f32 max_x = bounds.x + bounds.w - rect.w;
		f32 max_y = bounds.y + bounds.h - rect.h;
		if (rect.x > max_x)
		{
			rect.x = max_x;
		}
		if (rect.y > max_y)
		{
			rect.y = max_y;
		}
	}

	return rect;
}

RGINLINE u32 rg_gui_drag_target_ex(RgGuiContext* ctx, RgGuiRect rect, RgGuiId accept_type,
                                   u32 flags, RgGuiDragPayload* out_payload)
{
	if (!ctx || !ctx->drag_active || ctx->drag_payload.type == 0u)
	{
		return RG_GUI_DRAG_RESULT_NONE;
	}

	if (accept_type != 0u && ctx->drag_payload.type != accept_type)
	{
		return RG_GUI_DRAG_RESULT_NONE;
	}

	if (!rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect))
	{
		return RG_GUI_DRAG_RESULT_NONE;
	}

	u32 result = RG_GUI_DRAG_RESULT_HOVER;
	if (ctx->mouse_released)
	{
		if ((flags & RG_GUI_DRAG_TARGET_NO_ACCEPT) != 0u)
		{
			result |= RG_GUI_DRAG_RESULT_DROP;
			if (out_payload)
			{
				*out_payload = ctx->drag_payload;
			}
		}
		else
		{
			result |= RG_GUI_DRAG_RESULT_ACCEPT;
			rg_gui_drag_accept(ctx, out_payload);
		}
	}

	return result;
}

RGINLINE u32 rg_gui_drag_target(RgGuiContext* ctx, RgGuiRect rect, RgGuiId accept_type, RgGuiDragPayload* out_payload)
{
	return rg_gui_drag_target_ex(ctx, rect, accept_type, RG_GUI_DRAG_TARGET_NONE, out_payload);
}

RGINLINE void rg_gui_selection_state_init(RgGuiSelectionState* state)
{
	if (!state)
	{
		return;
	}

	if (!state->initialized)
	{
		state->anchor = -1;
		state->cursor = -1;
		state->initialized = 1;
	}
}

RGINLINE void rg_gui_selection_reset(RgGuiSelectionState* state)
{
	if (!state)
	{
		return;
	}

	state->anchor = -1;
	state->cursor = -1;
	state->initialized = 1;
}

RGINLINE int rg_gui_selection_clear_internal(const RgGuiSelectionOps* ops, void* user, u32 count)
{
	if (!ops)
	{
		return 0;
	}

	if (ops->clear)
	{
		ops->clear(user);
		return count > 0u;
	}

	if (count == 0u)
	{
		return 0;
	}

	if (ops->set_range)
	{
		ops->set_range(user, 0u, count - 1u, 0);
		return 1;
	}

	if (ops->set_selected)
	{
		for (u32 i = 0u; i < count; i++)
		{
			ops->set_selected(user, i, 0);
		}
		return 1;
	}

	return 0;
}

RGINLINE int rg_gui_selection_set_range_internal(const RgGuiSelectionOps* ops, void* user,
                                                 u32 start, u32 end, int selected)
{
	if (!ops)
	{
		return 0;
	}

	if (start > end)
	{
		u32 tmp = start;
		start = end;
		end = tmp;
	}

	if (ops->set_range)
	{
		ops->set_range(user, start, end, selected);
		return start <= end;
	}

	if (ops->set_selected)
	{
		for (u32 i = start; i <= end; i++)
		{
			ops->set_selected(user, i, selected);
		}
		return start <= end;
	}

	return 0;
}

RGINLINE int rg_gui_selection_apply(RgGuiSelectionState* state, const RgGuiSelectionOps* ops, void* user,
                                    u32 count, u32 index, u32 flags)
{
	count = (u32)rg_gui_u32_count_to_int(count);
	if (!state || !ops || !ops->is_selected || !ops->set_selected)
	{
		return 0;
	}

	if (count == 0u || index >= count)
	{
		return 0;
	}

	rg_gui_selection_state_init(state);

	int toggle = (flags & RG_GUI_SELECTION_TOGGLE) != 0u;
	int range = (flags & RG_GUI_SELECTION_RANGE) != 0u;

	int anchor = state->anchor;
	if (anchor < 0 || anchor >= (int)count)
	{
		if (state->cursor >= 0 && state->cursor < (int)count)
		{
			anchor = state->cursor;
		}
		else
		{
			anchor = (int)index;
		}
	}

	int changed = 0;
	if (range)
	{
		if (!toggle)
		{
			changed |= rg_gui_selection_clear_internal(ops, user, count);
		}
		changed |= rg_gui_selection_set_range_internal(ops, user, (u32)anchor, index, 1);
	}
	else if (toggle)
	{
		int is_selected = ops->is_selected(user, index) ? 1 : 0;
		ops->set_selected(user, index, !is_selected);
		changed = 1;
	}
	else
	{
		changed |= rg_gui_selection_clear_internal(ops, user, count);
		ops->set_selected(user, index, 1);
		changed = 1;
	}

	state->cursor = (int)index;
	state->anchor = range ? anchor : (int)index;

	return changed;
}

RGINLINE u32 rg_gui_tree_node_internal(RgGuiContext* ctx, RgGuiTreeState* tree, const char* label,
                                       const RgGuiIcon* icon, int* open, int selected,
                                       RgGuiRect rect, RgGuiId id, int copy_label)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(tree != NULL);

	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);

	f32 indent = tree->indent;
	if (indent <= 0.0f)
	{
		indent = ctx->style.char_width + ctx->style.padding;
		if (indent < 1.0f)
		{
			indent = 1.0f;
		}
	}

	f32 offset = indent * (f32)tree->depth;
	f32 content_x = rect.x + offset;
	f32 content_w = rect.w - offset;
	if (content_w < 0.0f)
	{
		content_w = 0.0f;
	}

	int has_children = (open != NULL);
	f32 toggle_w = ctx->style.char_width + ctx->style.padding;
	if (toggle_w < 0.0f)
	{
		toggle_w = 0.0f;
	}

	RgGuiRect toggle_rect = rg_gui_make_rect(content_x, rect.y, toggle_w, rect.h);
	int toggle_hovered = 0;
	if (has_children && enabled && toggle_w > 0.0f)
	{
		toggle_hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, toggle_rect);
	}

	u32 result = RG_GUI_TREE_NODE_RESULT_NONE;
	if (enabled && hovered && ctx->mouse_pressed)
	{
		if (has_children && toggle_hovered)
		{
			*open = !(*open);
			result |= RG_GUI_TREE_NODE_RESULT_TOGGLE;
		}
		result |= RG_GUI_TREE_NODE_RESULT_SELECTION;
	}

	if (focused)
	{
		if (has_children && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RIGHT))
		{
			if (!(*open))
			{
				*open = 1;
				result |= RG_GUI_TREE_NODE_RESULT_TOGGLE;
			}
		}
		if (has_children && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_LEFT))
		{
			if (*open)
			{
				*open = 0;
				result |= RG_GUI_TREE_NODE_RESULT_TOGGLE;
			}
		}

		if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
		    rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
		    rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE))
		{
			result |= RG_GUI_TREE_NODE_RESULT_SELECTION;
		}
	}

	rg_vec4 bg = ctx->style.color_bg;
	int draw_bg = 0;
	if (selected)
	{
		bg = ctx->style.color_bg_active;
		draw_bg = 1;
	}
	else if (enabled && (hovered || focused))
	{
		bg = ctx->style.color_bg_hover;
		draw_bg = 1;
	}

	if (draw_bg)
	{
		rg_gui_push_rect(ctx, rect, bg);
	}

	if (focused)
	{
		rg_gui_push_rect_outline(ctx, rect,
		                         ctx->style.color_focus_border,
		                         ctx->style.focus_border_thickness);
	}

	if (has_children)
	{
		RgGuiArrowDirection direction = *open ? RG_GUI_ARROW_DOWN : RG_GUI_ARROW_RIGHT;
		rg_gui_push_arrow(ctx, toggle_rect, direction, ctx->style.color_text_dim);
	}

	f32 label_x = content_x + toggle_w + ctx->style.padding;
	if (label_x < content_x)
	{
		label_x = content_x;
	}

	if (rg_gui_icon_valid(icon))
	{
		RgGuiRect icon_rect = rg_gui_icon_rect(ctx, rect, label_x);
		rg_gui_push_icon(ctx, icon, icon_rect, 0);
		label_x = icon_rect.x + icon_rect.w + ctx->style.inner_spacing;
	}

	if (label)
	{
		rg_vec2 pos = rg_vec2(label_x,
		                      rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (copy_label)
		{
			rg_gui_push_text(ctx, label, pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, pos, ctx->style.color_text);
		}
	}

	return result;
}

RGINLINE u32 rg_gui_tree_node(RgGuiContext* ctx, RgGuiTreeState* tree, const char* label, int* open,
                              int selected, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_tree_node_internal(ctx, tree, label, NULL, open, selected, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE u32 rg_gui_tree_node_static(RgGuiContext* ctx, RgGuiTreeState* tree, const char* label, int* open,
                                     int selected, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_tree_node_internal(ctx, tree, label, NULL, open, selected, rect, id, 0);
}

RGINLINE u32 rg_gui_tree_node_icon(RgGuiContext* ctx, RgGuiTreeState* tree, const char* label,
                                   const RgGuiIcon* icon, int* open, int selected,
                                   RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_tree_node_internal(ctx, tree, label, icon, open, selected, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE u32 rg_gui_tree_node_icon_static(RgGuiContext* ctx, RgGuiTreeState* tree, const char* label,
                                          const RgGuiIcon* icon, int* open, int selected,
                                          RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_tree_node_internal(ctx, tree, label, icon, open, selected, rect, id, 0);
}

RGINLINE u32 rg_gui_tree_virtual_internal(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                          f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                          RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                          RgGuiTreeRowFn row_fn, const void* user, int copy_label)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(tree != NULL);
	RG_GUI_ASSERT(panel != NULL);
	RG_GUI_ASSERT(get_item != NULL);

	if (row_height < 1.0f)
	{
		row_height = 1.0f;
	}
	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	rg_gui_tree_begin(ctx, tree, indent);

	u32 start = 0u;
	u32 visible = 0u;
	rg_gui_panel_begin_virtual(ctx, panel, rect, row_height, spacing, row_count, id, &start, &visible);

	u32 result = RG_GUI_TREE_NODE_RESULT_NONE;
	u32 end = start + visible;
	for (u32 i = start; i < end; i++)
	{
		RgGuiTreeItem item = {0};
		if (!get_item(user, i, &item))
		{
			continue;
		}

		RgGuiId node_id = item.id;
		if (node_id == 0u)
		{
			node_id = rg_gui_id_combine(id, (u64)(0x10000u + i));
		}
		else
		{
			node_id = rg_gui_id_scoped(ctx, node_id);
		}
		item.id = node_id;

		tree->depth = item.depth;

		RgGuiRect row_rect = rg_gui_layout_next(ctx, row_height);
		u32 row_result = rg_gui_tree_node_internal(ctx, tree, item.label, NULL, item.open,
		                                           item.selected, row_rect, node_id, copy_label);
		result |= row_result;

		if (row_fn)
		{
			row_fn(ctx, i, &item, row_rect, row_result, user);
		}
	}

	tree->depth = 0u;
	rg_gui_panel_end(ctx, panel);
	return result;
}

RGINLINE u32 rg_gui_tree_virtual_multi_internal(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                                f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                                RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                                RgGuiTreeRowFn row_fn, const void* user,
                                                RgGuiSelectionState* selection, const RgGuiSelectionOps* ops,
                                                void* selection_user, int copy_label)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(tree != NULL);
	RG_GUI_ASSERT(panel != NULL);
	RG_GUI_ASSERT(get_item != NULL);
	RG_GUI_ASSERT(selection != NULL);
	RG_GUI_ASSERT(ops != NULL);
	RG_GUI_ASSERT(ops->is_selected != NULL);
	RG_GUI_ASSERT(ops->set_selected != NULL);
	row_count = (u32)rg_gui_u32_count_to_int(row_count);

	rg_gui_selection_state_init(selection);

	if (row_height < 1.0f)
	{
		row_height = 1.0f;
	}
	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && ctx->tab_focus_id == id && ctx->focus_id != id)
	{
		ctx->focus_id = id;
	}
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}
	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);
	int row_count_int = (int)row_count;
	int cursor = selection->cursor;
	if (cursor >= row_count_int)
	{
		cursor = row_count_int > 0 ? row_count_int - 1 : -1;
	}
	if (cursor < -1)
	{
		cursor = -1;
	}
	selection->cursor = cursor;

	u32 result = RG_GUI_TREE_NODE_RESULT_NONE;

	if (focused && row_count > 0u)
	{
		f32 row_stride = row_height + spacing;
		if (row_stride < 1.0f)
		{
			row_stride = 1.0f;
		}

		f32 border = ctx->style.border_thickness;
		f32 pad = ctx->style.padding;
		f32 inset = border + pad;
		f32 view_h = rect.h - inset * 2.0f;
		if (view_h < 0.0f)
		{
			view_h = 0.0f;
		}

		int visible_rows = rg_gui_nonnegative_f32_to_int_bounded(view_h / row_stride);
		if (visible_rows < 1)
		{
			visible_rows = 1;
		}
		if (visible_rows > row_count_int)
		{
			visible_rows = row_count_int;
		}

		int target = (cursor >= 0) ? cursor : 0;
		int moved = 0;

		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP)
			                       ? SDL_SCANCODE_UP
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, id, key);
			if (repeats > 0)
			{
				target = rg_gui_int_step_clamped(target, 1, repeats,
				                                 key == SDL_SCANCODE_UP ? -1 : 1,
				                                 0, row_count_int - 1);
				moved = 1;
			}
		}

		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_PAGEUP) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_PAGEDOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_PAGEUP)
			                       ? SDL_SCANCODE_PAGEUP
			                       : SDL_SCANCODE_PAGEDOWN;
			int repeats = rg_gui_key_repeat(ctx, id, key);
			if (repeats > 0)
			{
				target = rg_gui_int_step_clamped(target, (i64)visible_rows, repeats,
				                                 key == SDL_SCANCODE_PAGEUP ? -1 : 1,
				                                 0, row_count_int - 1);
				moved = 1;
			}
		}

		if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_HOME))
		{
			target = 0;
			moved = 1;
		}
		if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_END))
		{
			target = row_count_int - 1;
			moved = 1;
		}

		if (moved)
		{
			if (target < 0) target = 0;
			if (target >= row_count_int) target = row_count_int - 1;

			u32 flags = RG_GUI_SELECTION_NONE;
			if (ctx->input && rg_gui_input_has_shift(ctx->input))
			{
				flags |= RG_GUI_SELECTION_RANGE;
			}
			if (rg_gui_selection_apply(selection, ops, selection_user, row_count, (u32)target, flags))
			{
				cursor = selection->cursor;
				ctx->focus_id = id;
				result |= RG_GUI_TREE_NODE_RESULT_SELECTION;
			}

			f32 max_scroll = row_stride * (f32)row_count - view_h;
			if (max_scroll < 0.0f)
			{
				max_scroll = 0.0f;
			}

			f32 row_top = row_stride * (f32)cursor;
			f32 row_bottom = row_top + row_height;
			if (row_top < panel->scroll_y)
			{
				panel->scroll_y = row_top;
			}
			else if (row_bottom > panel->scroll_y + view_h)
			{
				panel->scroll_y = row_bottom - view_h;
			}

			if (panel->scroll_y < 0.0f) panel->scroll_y = 0.0f;
			if (panel->scroll_y > max_scroll) panel->scroll_y = max_scroll;
		}

		if (cursor >= 0 &&
		    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_LEFT) ||
		     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RIGHT)))
		{
			RgGuiTreeItem focus_item = {0};
			if (get_item(user, (u32)cursor, &focus_item) && focus_item.open)
			{
				if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RIGHT))
				{
					if (!(*focus_item.open))
					{
						*focus_item.open = 1;
						result |= RG_GUI_TREE_NODE_RESULT_TOGGLE;
					}
				}
				else if (*focus_item.open)
				{
					*focus_item.open = 0;
					result |= RG_GUI_TREE_NODE_RESULT_TOGGLE;
				}
			}
		}

		if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
		    rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
		    rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE))
		{
			if (cursor < 0 && row_count > 0u)
			{
				cursor = 0;
			}
			if (cursor >= 0)
			{
				u32 flags = RG_GUI_SELECTION_NONE;
				if (ctx->input && rg_gui_input_has_ctrl(ctx->input))
				{
					flags |= RG_GUI_SELECTION_TOGGLE;
				}
				if (ctx->input && rg_gui_input_has_shift(ctx->input))
				{
					flags |= RG_GUI_SELECTION_RANGE;
				}
				if (rg_gui_selection_apply(selection, ops, selection_user, row_count, (u32)cursor, flags))
				{
					ctx->focus_id = id;
					result |= RG_GUI_TREE_NODE_RESULT_SELECTION;
				}
			}
		}
	}

	rg_gui_tree_begin(ctx, tree, indent);

	u32 start = 0u;
	u32 visible = 0u;
	rg_gui_panel_begin_virtual(ctx, panel, rect, row_height, spacing, row_count, id, &start, &visible);

	u32 end = start + visible;
	for (u32 i = start; i < end; i++)
	{
		RgGuiTreeItem item = {0};
		if (!get_item(user, i, &item))
		{
			continue;
		}

		RgGuiId node_id = item.id;
		if (node_id == 0u)
		{
			node_id = rg_gui_id_combine(id, (u64)(0x10000u + i));
		}
		else
		{
			node_id = rg_gui_id_scoped(ctx, node_id);
		}
		item.id = node_id;

		tree->depth = item.depth;
		item.selected = ops->is_selected(selection_user, i) ? 1 : 0;

		RgGuiRect row_rect = rg_gui_layout_next(ctx, row_height);
		u32 row_result = rg_gui_tree_node_internal(ctx, tree, item.label, NULL, item.open,
		                                           item.selected, row_rect, node_id, copy_label);
		result |= row_result;

		if (row_result & RG_GUI_TREE_NODE_RESULT_SELECTION)
		{
			u32 flags = RG_GUI_SELECTION_NONE;
			if (ctx->input && rg_gui_input_has_ctrl(ctx->input))
			{
				flags |= RG_GUI_SELECTION_TOGGLE;
			}
			if (ctx->input && rg_gui_input_has_shift(ctx->input))
			{
				flags |= RG_GUI_SELECTION_RANGE;
			}
			if (flags == RG_GUI_SELECTION_NONE && item.selected)
			{
				selection->cursor = (int)i;
				selection->anchor = (int)i;
			}
			else if (rg_gui_selection_apply(selection, ops, selection_user, row_count, i, flags))
			{
				result |= RG_GUI_TREE_NODE_RESULT_SELECTION;
			}
			ctx->focus_id = id;
		}

		if (row_fn)
		{
			row_fn(ctx, i, &item, row_rect, row_result, user);
		}
	}

	tree->depth = 0u;
	rg_gui_panel_end(ctx, panel);
	return result;
}

RGINLINE u32 rg_gui_tree_virtual(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                 f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                 RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                 RgGuiTreeRowFn row_fn, const void* user)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_tree_virtual_internal(ctx, tree, panel, indent, row_height, spacing, row_count,
	                                    rect, id, get_item, row_fn, user, RG_GUI_LABEL_COPY);
}

RGINLINE u32 rg_gui_tree_virtual_static(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                        f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                        RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                        RgGuiTreeRowFn row_fn, const void* user)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_tree_virtual_internal(ctx, tree, panel, indent, row_height, spacing, row_count,
	                                    rect, id, get_item, row_fn, user, 0);
}

RGINLINE u32 rg_gui_tree_virtual_multi(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                       f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                       RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                       RgGuiTreeRowFn row_fn, const void* user,
                                       RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_tree_virtual_multi_internal(ctx, tree, panel, indent, row_height, spacing, row_count,
	                                          rect, id, get_item, row_fn, user,
	                                          selection, ops, selection_user, RG_GUI_LABEL_COPY);
}

RGINLINE u32 rg_gui_tree_virtual_multi_static(RgGuiContext* ctx, RgGuiTreeState* tree, RgGuiPanelState* panel,
                                              f32 indent, f32 row_height, f32 spacing, u32 row_count,
                                              RgGuiRect rect, RgGuiId id, RgGuiTreeItemFn get_item,
                                              RgGuiTreeRowFn row_fn, const void* user,
                                              RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_tree_virtual_multi_internal(ctx, tree, panel, indent, row_height, spacing, row_count,
	                                          rect, id, get_item, row_fn, user,
	                                          selection, ops, selection_user, 0);
}

RGINLINE int rg_gui_checkbox_internal(RgGuiContext* ctx, const char* label, int* value, RgGuiRect rect, RgGuiId id, int copy_label)
{
	RG_GUI_ASSERT(value != NULL);

	rg_gui_register_focusable(ctx, id);

	f32 box_size = ctx->style.text_height;
	RgGuiRect box = rg_gui_make_rect(rect.x, rect.y + (rect.h - box_size) * 0.5f, box_size, box_size);
	RgGuiRect label_rect = rg_gui_make_rect(rect.x + box_size + ctx->style.inner_spacing,
	                                        rect.y,
	                                        rect.w - box_size - ctx->style.inner_spacing,
	                                        rect.h);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, box);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	int changed = 0;
	int focused = enabled && (ctx->focus_id == id);
	if (enabled && hovered && ctx->mouse_pressed)
	{
		*value = !(*value);
		changed = 1;
		ctx->focus_id = id;
	}

	if (focused &&
	    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE)))
	{
		*value = !(*value);
		changed = 1;
	}

	rg_vec4 bg = ctx->style.color_bg;
	if (enabled && hovered)
	{
		bg = ctx->style.color_bg_hover;
	}
	rg_gui_push_rect(ctx, box, bg);
	rg_gui_push_rect_outline(ctx, box,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	if (*value)
	{
		f32 inset = 3.0f;
		rg_gui_push_rect(ctx, rg_gui_make_rect(box.x + inset, box.y + inset, box.w - inset * 2.0f, box.h - inset * 2.0f),
		                 ctx->style.color_accent);
	}

	if (label)
	{
		rg_vec2 pos = rg_vec2(label_rect.x,
		                      label_rect.y + (label_rect.h - ctx->style.text_height) * 0.5f);
		if (copy_label)
		{
			rg_gui_push_text(ctx, label, pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, pos, ctx->style.color_text);
		}
	}

	return changed;
}

RGINLINE int rg_gui_checkbox(RgGuiContext* ctx, const char* label, int* value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_checkbox_internal(ctx, label, value, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE int rg_gui_checkbox_static(RgGuiContext* ctx, const char* label, int* value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_checkbox_internal(ctx, label, value, rect, id, 0);
}

RGINLINE int rg_gui_radio_internal(RgGuiContext* ctx, const char* label, int* value, int option, RgGuiRect rect, RgGuiId id, int copy_label)
{
	RG_GUI_ASSERT(value != NULL);

	rg_gui_register_focusable(ctx, id);

	f32 box_size = ctx->style.text_height;
	RgGuiRect box = rg_gui_make_rect(rect.x, rect.y + (rect.h - box_size) * 0.5f, box_size, box_size);
	RgGuiRect label_rect = rg_gui_make_rect(rect.x + box_size + ctx->style.inner_spacing,
	                                        rect.y,
	                                        rect.w - box_size - ctx->style.inner_spacing,
	                                        rect.h);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, box);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	int changed = 0;
	int selected = (*value == option);
	int focused = enabled && (ctx->focus_id == id);
	if (enabled && hovered && ctx->mouse_pressed)
	{
		if (!selected)
		{
			*value = option;
			changed = 1;
			selected = 1;
		}
		ctx->focus_id = id;
	}

	if (focused &&
	    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE)))
	{
		if (!selected)
		{
			*value = option;
			changed = 1;
			selected = 1;
		}
	}

	rg_vec4 bg = ctx->style.color_bg;
	if (enabled && hovered)
	{
		bg = ctx->style.color_bg_hover;
	}
	rg_gui_push_rect(ctx, box, bg);
	rg_gui_push_rect_outline(ctx, box,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	if (selected)
	{
		f32 inset = 4.0f;
		rg_gui_push_rect(ctx, rg_gui_make_rect(box.x + inset, box.y + inset, box.w - inset * 2.0f, box.h - inset * 2.0f),
		                 ctx->style.color_accent);
	}

	if (label)
	{
		rg_vec2 pos = rg_vec2(label_rect.x,
		                      label_rect.y + (label_rect.h - ctx->style.text_height) * 0.5f);
		if (copy_label)
		{
			rg_gui_push_text(ctx, label, pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, pos, ctx->style.color_text);
		}
	}

	return changed;
}

RGINLINE int rg_gui_radio(RgGuiContext* ctx, const char* label, int* value, int option, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_radio_internal(ctx, label, value, option, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE int rg_gui_radio_static(RgGuiContext* ctx, const char* label, int* value, int option, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_radio_internal(ctx, label, value, option, rect, id, 0);
}

RGINLINE int rg_gui_text_input_ex_internal(RgGuiContext* ctx, const char* label, char* buffer, size_t capacity, RgGuiRect rect, RgGuiId id, RgGuiTextInputFlags flags, int* out_submit)
{
	if (out_submit)
	{
		*out_submit = 0;
	}

	RG_GUI_ASSERT(buffer != NULL);
	RG_GUI_ASSERT(capacity > 0u);

	int read_only = (flags & RG_GUI_TEXT_INPUT_READONLY) ? 1 : 0;
	u32 filter_flags = (u32)flags &
	                   (RG_GUI_TEXT_INPUT_FILTER_NUMERIC | RG_GUI_TEXT_INPUT_FILTER_HEX);
	int value_undo = (flags & RG_GUI_TEXT_INPUT_UNDO_VALUE) ? 1 : 0;
	if (!read_only || filter_flags != 0u)
	{
		value_undo = 1;
	}
	if (buffer == ctx->number_edit_state->buffer)
	{
		value_undo = 0;
	}
	int value_undo_supported = 0;
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
	if (value_undo)
	{
		value_undo_supported = 1;
	}
	if (!value_undo && ctx->text_edit_state->value_undo_active && ctx->text_edit_state->value_undo_id == id)
	{
		rg_gui_text_value_undo_clear(ctx->text_edit_state);
	}
#else
	RG_GUI_UNUSED(value_undo);
	RG_GUI_UNUSED(value_undo_supported);
#endif

	RgGuiRect field = rect;
	if (!(flags & RG_GUI_TEXT_INPUT_NO_LABEL) && label)
	{
		f32 label_width = ctx->style.label_width;
		RgGuiRect label_rect = rg_gui_make_rect(rect.x, rect.y, label_width, rect.h);
		rg_vec2 label_pos = rg_vec2(label_rect.x,
		                            label_rect.y + (label_rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}

		field.x += label_width + ctx->style.inner_spacing;
		field.w -= label_width + ctx->style.inner_spacing;
	}

	if (field.w <= 0.0f)
	{
		field.w = 0.0f;
	}

	int enabled = !rg_gui_is_disabled(ctx);

	rg_gui_register_focusable(ctx, id);

	if (!enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		ctx->text_edit_state->active = 0;
	}

	int changed = 0;
	if (enabled && ctx->input_events && ctx->input_event_focus_id == id)
	{
		rg_gui_text_edit_process_pending_ordered_config(
		    ctx, read_only, 0, filter_flags, 0, 0.0f);
		changed |= rg_gui_text_edit_consume_ordered_result(ctx, id, out_submit);
	}

	int activate_from_focus = enabled &&
	                          ((ctx->tab_focus_id == id && ctx->focus_id != id) ||
	                           (ctx->tab_dir == 0 && ctx->focus_id == id &&
	                            (!ctx->text_edit_state->active || ctx->text_edit_state->id != id)));
	if (activate_from_focus)
	{
		rg_gui_text_edit_process_pending_ordered_stored(ctx);
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
		rg_gui_text_value_undo_commit_switch(ctx, id);
#endif
		if (ctx->text_edit_state->id != id)
		{
			rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		}
		ctx->focus_id = id;
		ctx->text_edit_state->id = id;
		ctx->text_edit_state->buffer = buffer;
		ctx->text_edit_state->capacity = capacity;
		ctx->text_edit_state->length = strlen(buffer);
		ctx->text_edit_state->cursor = ctx->text_edit_state->length;
		ctx->text_edit_state->view_start = 0u;
		rg_gui_text_edit_clear_selection(ctx->text_edit_state);
		rg_gui_text_edit_undo_clear(ctx->text_edit_state);
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
		rg_gui_text_value_undo_begin(ctx->text_edit_state, id, buffer, ctx->text_edit_state->length);
#endif
		ctx->text_edit_state->content_version++;
		rg_gui_text_prefix_cache_reset(ctx->text_edit_state, buffer);
		ctx->text_edit_state->active = 1;
		ctx->text_edit_state->dirty = 1;
		ctx->text_edit_state->backspace_hold_time = 0.0f;
		ctx->text_edit_state->backspace_repeat_time = 0.0f;
		ctx->cursor_visible = 1;
		ctx->cursor_blink_timer = 0.0f;
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
		if (value_undo_supported)
		{
			rg_gui_text_value_undo_begin(ctx->text_edit_state, id, buffer, ctx->text_edit_state->length);
		}
		else if (value_undo)
		{
			rg_gui_text_value_undo_clear(ctx->text_edit_state);
		}
#endif
	}

	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, field);
	if (hovered)
	{
		ctx->hot_id = id;
	}
	if (hovered && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_TEXT;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		if (ctx->text_edit_state->id != id)
		{
			rg_gui_text_edit_process_pending_ordered_stored(ctx);
		}
		int was_active = (ctx->text_edit_state->active && ctx->text_edit_state->id == id) ? 1 : 0;
		if (!was_active)
		{
			rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		}
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
		if (!was_active)
		{
			rg_gui_text_value_undo_commit_switch(ctx, id);
		}
#endif
		ctx->active_id = id;
		ctx->focus_id = id;
		if (!(flags & RG_GUI_TEXT_INPUT_READONLY))
		{
			ctx->text_edit_state->id = id;
			ctx->text_edit_state->buffer = buffer;
			ctx->text_edit_state->capacity = capacity;
			ctx->text_edit_state->length = strlen(buffer);
			ctx->text_edit_state->active = 1;
			if (!was_active)
			{
				ctx->text_edit_state->view_start = 0u;
				ctx->text_edit_state->backspace_hold_time = 0.0f;
				ctx->text_edit_state->backspace_repeat_time = 0.0f;
				rg_gui_text_edit_undo_clear(ctx->text_edit_state);
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
				if (value_undo_supported)
				{
					rg_gui_text_value_undo_begin(ctx->text_edit_state, id, buffer, ctx->text_edit_state->length);
				}
				else if (value_undo)
				{
					rg_gui_text_value_undo_clear(ctx->text_edit_state);
				}
#endif
			}
			ctx->text_edit_state->content_version++;
			rg_gui_text_prefix_cache_reset(ctx->text_edit_state, buffer);

			size_t pick_start = ctx->text_edit_state->view_start;
			size_t cursor = rg_gui_text_edit_pick_cursor(ctx, buffer + pick_start, field) + pick_start;
			int click_count = rg_gui_text_edit_update_click(ctx, ctx->text_edit_state, id, ctx->mouse_pos);
			if (click_count == 1)
			{
				int extend = (was_active && rg_gui_input_has_shift(ctx->input)) ? 1 : 0;
				rg_gui_text_edit_set_cursor(ctx->text_edit_state, cursor, extend);
			}
			else if (click_count == 2)
			{
				size_t start = 0u;
				size_t end = 0u;
				rg_gui_text_edit_word_range(buffer, ctx->text_edit_state->length, cursor, &start, &end);
				rg_gui_text_edit_select_range(ctx->text_edit_state, start, end);
			}
			else
			{
				size_t start = 0u;
				size_t end = 0u;
				rg_gui_text_edit_line_range(buffer, ctx->text_edit_state->length, cursor, 0, &start, &end);
				rg_gui_text_edit_select_range(ctx->text_edit_state, start, end);
			}

			ctx->text_edit_state->dirty = 1;
			ctx->cursor_visible = 1;
			ctx->cursor_blink_timer = 0.0f;
		}
	}

	if (ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		rg_gui_text_edit_store_ordered_config(
		    ctx->text_edit_state, read_only, 0, filter_flags, 0, 0.0f);
	}

	int logical_focus = enabled && (ctx->focus_id == id);
	int focused = logical_focus && ctx->window_focused;

	if (logical_focus && ctx->text_edit_state->active && ctx->text_edit_state->id == id &&
	    (ctx->window_focused || ctx->input_events))
	{
		changed |= rg_gui_text_edit_process_ex(ctx, ctx->text_edit_state, out_submit, read_only, 0, filter_flags);
	}

#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
	if (out_submit && *out_submit &&
	    ctx->text_edit_state->value_undo_active && ctx->text_edit_state->id == id)
	{
		rg_gui_text_value_undo_commit(ctx, ctx->text_edit_state, 0);
	}
#endif

	if (focused && ctx->active_id == id && ctx->text_edit_state->active &&
	    ctx->text_edit_state->id == id && ctx->mouse_down)
	{
		size_t pick_start = ctx->text_edit_state->view_start;
		size_t cursor = rg_gui_text_edit_pick_cursor(ctx, buffer + pick_start, field) + pick_start;
		rg_gui_text_edit_set_cursor(ctx->text_edit_state, cursor, 1);
		ctx->text_edit_state->dirty = 1;
		ctx->cursor_visible = 1;
		ctx->cursor_blink_timer = 0.0f;
	}

	size_t text_length = buffer ? strlen(buffer) : 0u;
	size_t preedit_length = 0u;
	const char* render_buffer = buffer;
	size_t display_start = 0u;
	RgGuiTextEditPreeditLayout preedit_layout;
	memset(&preedit_layout, 0, sizeof(preedit_layout));
	f32 available_width = field.w - ctx->style.padding * 2.0f;
	if (available_width < 0.0f)
	{
		available_width = 0.0f;
	}

	if (focused && ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		text_length = ctx->text_edit_state->length;
		rg_gui_text_edit_update_view(ctx, ctx->text_edit_state, buffer, field);
		display_start = ctx->text_edit_state->view_start;
		preedit_layout = rg_gui_text_edit_preedit_layout(
		    ctx->text_edit_state, ctx->text_edit_state->length);
		render_buffer = rg_gui_text_edit_display_text(
		    ctx, ctx->text_edit_state, buffer, ctx->text_edit_state->length,
		    &text_length, &preedit_length);
		if (preedit_length > 0u)
		{
			display_start = rg_gui_text_edit_display_offset(
			    preedit_layout, display_start);
			display_start = rg_gui_text_start_for_cursor_width(
			    ctx, render_buffer, text_length, display_start,
			    preedit_layout.caret, available_width);
		}
	}

	size_t display_end = rg_gui_text_end_for_width(
	    ctx, render_buffer, text_length, display_start, available_width);
	const char* display_text = render_buffer;
	int display_copy = RG_GUI_COPY_DYNAMIC_TEXT;
	if (display_start != 0u || display_end != text_length)
	{
		display_text = rg_gui_copy_text_range(ctx, render_buffer, display_start, display_end);
		display_copy = 0;
	}

	rg_vec4 bg = ctx->style.color_bg;
	if (focused)
	{
		bg = ctx->style.color_bg_active;
	}
	else if (hovered)
	{
		bg = ctx->style.color_bg_hover;
	}

	rg_gui_push_rect(ctx, field, bg);

	if (focused && ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		size_t sel_start = 0u;
		size_t sel_end = 0u;
		if (preedit_length > 0u)
		{
			sel_start = preedit_layout.selection_start;
			sel_end = preedit_layout.selection_end;
		}
		else if (rg_gui_text_edit_has_selection(ctx->text_edit_state))
		{
			sel_start = ctx->text_edit_state->selection_start;
			sel_end = ctx->text_edit_state->selection_end;
			size_t text_len = ctx->text_edit_state->length;
			if (sel_start > text_len) sel_start = text_len;
			if (sel_end > text_len) sel_end = text_len;
		}

		if (sel_start != sel_end)
		{
			rg_vec2 text_pos = rg_vec2(field.x + ctx->style.padding,
			                           field.y + (field.h - ctx->style.text_height) * 0.5f);
			f32 base = rg_gui_text_measure_prefix(ctx, render_buffer, display_start);
			f32 x0 = text_pos.x + rg_gui_text_measure_prefix(ctx, render_buffer, sel_start) - base;
			f32 x1 = text_pos.x + rg_gui_text_measure_prefix(ctx, render_buffer, sel_end) - base;
			if (x1 < x0)
			{
				f32 tmp = x0;
				x0 = x1;
				x1 = tmp;
			}

			f32 min_x = field.x + ctx->style.padding;
			f32 max_x = field.x + field.w - ctx->style.padding;
			if (x0 < min_x) x0 = min_x;
			if (x1 > max_x) x1 = max_x;

			if (x1 > x0)
			{
				f32 sel_y = field.y + ctx->style.padding * 0.5f;
				f32 sel_h = field.h - ctx->style.padding;
				rg_gui_push_rect(ctx, rg_gui_make_rect(x0, sel_y, x1 - x0, sel_h),
				                 ctx->style.color_selection);
			}
		}
	}

	if (focused && preedit_length > 0u &&
	    ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		size_t preedit_start = preedit_layout.preedit_start;
		size_t preedit_end = preedit_layout.preedit_end;
		f32 base = rg_gui_text_measure_prefix(ctx, render_buffer, display_start);
		f32 x0 = field.x + ctx->style.padding +
		         rg_gui_text_measure_prefix(ctx, render_buffer, preedit_start) - base;
		f32 x1 = field.x + ctx->style.padding +
		         rg_gui_text_measure_prefix(ctx, render_buffer, preedit_end) - base;
		f32 min_x = field.x + ctx->style.padding;
		f32 max_x = field.x + field.w - ctx->style.padding;
		if (x0 < min_x) x0 = min_x;
		if (x1 > max_x) x1 = max_x;
		if (x1 > x0)
		{
			rg_gui_push_rect(ctx, rg_gui_make_rect(x0, field.y + field.h - ctx->style.padding, x1 - x0, 1.0f),
			                 ctx->style.color_accent);
		}
	}

	rg_gui_push_rect_outline(ctx, field,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	if (buffer)
	{
		rg_vec2 text_pos = rg_vec2(field.x + ctx->style.padding,
		                           field.y + (field.h - ctx->style.text_height) * 0.5f);
		if (display_text)
		{
			rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, display_copy);
		}

		if (focused && ctx->text_edit_state->active && ctx->text_edit_state->id == id)
		{
			size_t cursor = ctx->text_edit_state->cursor;
			if (cursor > ctx->text_edit_state->length)
			{
				cursor = ctx->text_edit_state->length;
			}
			size_t render_cursor = preedit_length > 0u ? preedit_layout.caret : cursor;

			f32 base = rg_gui_text_measure_prefix(ctx, render_buffer, display_start);
			f32 cursor_x = text_pos.x +
			               rg_gui_text_measure_prefix(ctx, render_buffer, render_cursor) - base;
			if (ctx->font)
			{
				if (preedit_length == 0u)
				{
					if (ctx->text_edit_state->dirty ||
					    ctx->text_edit_state->cached_cursor != cursor ||
					    ctx->text_edit_state->cached_length != ctx->text_edit_state->length)
					{
						ctx->text_edit_state->cached_cursor_x =
						    rg_gui_text_measure_prefix(ctx, buffer, cursor);

						ctx->text_edit_state->cached_cursor = cursor;
						ctx->text_edit_state->cached_length = ctx->text_edit_state->length;
						ctx->text_edit_state->dirty = 0;
					}
					cursor_x = text_pos.x + ctx->text_edit_state->cached_cursor_x - base;
				}
			}
			f32 cursor_y = field.y + ctx->style.padding * 0.5f;
			f32 cursor_h = field.h - ctx->style.padding;
			RgGuiRect caret = rg_gui_make_rect(cursor_x, cursor_y, 1.0f, cursor_h);
			if (!read_only)
			{
				ctx->platform_output.wants_text_input = 1;
				ctx->platform_output.ime_caret_valid = 1;
				ctx->platform_output.ime_caret_rect = caret;
			}
			if (ctx->ime_callback)
			{
				ctx->ime_callback(ctx, caret, ctx->ime_callback_user);
			}
			if (ctx->cursor_visible)
			{
				rg_gui_push_rect(ctx, caret, ctx->style.color_caret);
			}
		}
	}

	return changed;
}

RGINLINE int rg_gui_text_input_ex(RgGuiContext* ctx, const char* label, char* buffer, size_t capacity, RgGuiRect rect, RgGuiId id, RgGuiTextInputFlags flags, int* out_submit)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_text_input_ex_internal(ctx, label, buffer, capacity, rect, id, flags, out_submit);
}

RGINLINE int rg_gui_text_input(RgGuiContext* ctx, const char* label, char* buffer, size_t capacity, RgGuiRect rect, RgGuiId id, int* out_submit)
{
	return rg_gui_text_input_ex(ctx, label, buffer, capacity, rect, id, RG_GUI_TEXT_INPUT_NONE, out_submit);
}

RGINLINE int rg_gui_text_area(RgGuiContext* ctx, RgGuiTextAreaState* area, char* buffer, size_t capacity, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(area != NULL);
	RG_GUI_ASSERT(buffer != NULL);
	RG_GUI_ASSERT(capacity > 0u);

	id = rg_gui_id_scoped(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	rg_gui_register_focusable(ctx, id);

	if (!enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		ctx->text_edit_state->active = 0;
	}

	int activate_from_focus = enabled &&
	                          ((ctx->tab_focus_id == id && ctx->focus_id != id) ||
	                           (ctx->tab_dir == 0 && ctx->focus_id == id &&
	                            (!ctx->text_edit_state->active || ctx->text_edit_state->id != id)));
	if (activate_from_focus)
	{
		rg_gui_text_edit_process_pending_ordered_stored(ctx);
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
		rg_gui_text_value_undo_commit_switch(ctx, id);
#endif
		if (ctx->text_edit_state->id != id)
		{
			rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		}
		ctx->focus_id = id;
		ctx->text_edit_state->id = id;
		ctx->text_edit_state->buffer = buffer;
		ctx->text_edit_state->capacity = capacity;
		ctx->text_edit_state->length = strlen(buffer);
		ctx->text_edit_state->cursor = ctx->text_edit_state->length;
		ctx->text_edit_state->view_start = 0u;
		rg_gui_text_edit_clear_selection(ctx->text_edit_state);
		rg_gui_text_edit_undo_clear(ctx->text_edit_state);
		ctx->text_edit_state->content_version++;
		rg_gui_text_prefix_cache_reset(ctx->text_edit_state, buffer);
		ctx->text_edit_state->active = 1;
		ctx->text_edit_state->dirty = 1;
		ctx->text_edit_state->backspace_hold_time = 0.0f;
		ctx->text_edit_state->backspace_repeat_time = 0.0f;
		ctx->cursor_visible = 1;
		ctx->cursor_blink_timer = 0.0f;
	}

	f32 line_height = ctx->style.text_height + ctx->style.padding * 2.0f;
	if (line_height < 1.0f)
	{
		line_height = 1.0f;
	}

	RgGuiRect inner = rg_gui_text_area_inner_rect(ctx, rect);
	f32 wrap_width = inner.w - ctx->style.padding * 2.0f;
	if (wrap_width < 1.0f)
	{
		wrap_width = 1.0f;
	}
	RgGuiTextAreaVisualLine visual_lines[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];
	RgGuiTextAreaLayout layout = {0};
	layout.lines = visual_lines;

	int changed = 0;
	if (enabled && ctx->input_events && ctx->input_event_focus_id == id)
	{
		rg_gui_text_edit_process_pending_ordered_config_with_layout(
		    ctx, 0, 1, 0u, 1, wrap_width, &layout);
		changed |= rg_gui_text_edit_consume_ordered_result(ctx, id, NULL);
	}

	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}
	if (hovered && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_TEXT;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		if (ctx->text_edit_state->id != id)
		{
			rg_gui_text_edit_process_pending_ordered_stored(ctx);
		}
		int was_active = (ctx->text_edit_state->active && ctx->text_edit_state->id == id) ? 1 : 0;
		if (!was_active)
		{
			rg_gui_text_edit_clear_preedit(ctx->text_edit_state);
		}
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
		if (!was_active)
		{
			rg_gui_text_value_undo_commit_switch(ctx, id);
		}
#endif
		ctx->active_id = id;
		ctx->focus_id = id;
		ctx->text_edit_state->id = id;
		ctx->text_edit_state->buffer = buffer;
		ctx->text_edit_state->capacity = capacity;
		ctx->text_edit_state->length = strlen(buffer);
		ctx->text_edit_state->active = 1;
		if (!was_active)
		{
			ctx->text_edit_state->view_start = 0u;
			ctx->text_edit_state->backspace_hold_time = 0.0f;
			ctx->text_edit_state->backspace_repeat_time = 0.0f;
			rg_gui_text_edit_undo_clear(ctx->text_edit_state);
#if RG_GUI_TEXT_VALUE_UNDO_ENTRY_SIZE > 0
			rg_gui_text_value_undo_begin(ctx->text_edit_state, id, buffer, ctx->text_edit_state->length);
#endif
		}
		ctx->text_edit_state->content_version++;
		rg_gui_text_prefix_cache_reset(ctx->text_edit_state, buffer);

		u32 pick_line_count =
		    rg_gui_text_area_ensure_layout(ctx, &layout, buffer, ctx->text_edit_state->length,
		                                   ctx->text_edit_state->content_version, wrap_width);
		size_t pick = rg_gui_text_area_pick_cursor_wrapped(ctx, visual_lines, pick_line_count,
		                                                   inner, area->panel.scroll_y, line_height);
		int click_count = rg_gui_text_edit_update_click(ctx, ctx->text_edit_state, id, ctx->mouse_pos);
		if (click_count == 1)
		{
			int extend = (was_active && rg_gui_input_has_shift(ctx->input)) ? 1 : 0;
			rg_gui_text_edit_set_cursor(ctx->text_edit_state, pick, extend);
		}
		else if (click_count == 2)
		{
			size_t start = 0u;
			size_t end = 0u;
			rg_gui_text_edit_word_range(buffer, ctx->text_edit_state->length, pick, &start, &end);
			rg_gui_text_edit_select_range(ctx->text_edit_state, start, end);
		}
		else
		{
			size_t start = 0u;
			size_t end = 0u;
			rg_gui_text_edit_line_range(buffer, ctx->text_edit_state->length, pick, 1, &start, &end);
			rg_gui_text_edit_select_range(ctx->text_edit_state, start, end);
		}
		ctx->text_edit_state->dirty = 1;
		ctx->cursor_visible = 1;
		ctx->cursor_blink_timer = 0.0f;
	}

	if (ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		rg_gui_text_edit_store_ordered_config(
		    ctx->text_edit_state, 0, 1, 0u, 1, wrap_width);
	}

	int logical_focus = enabled && (ctx->focus_id == id);
	int focused = logical_focus && ctx->window_focused;

	if (logical_focus && ctx->text_edit_state->active && ctx->text_edit_state->id == id &&
	    (ctx->window_focused || ctx->input_events))
	{
		changed |= rg_gui_text_edit_process_ex(ctx, ctx->text_edit_state, NULL, 0, 1, 0u);

		if (!ctx->input_events &&
		    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_UP) ||
		     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_DOWN)))
		{
			int selecting = rg_gui_input_has_shift(ctx->input) ? 1 : 0;
			int direction = rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_UP) ? -1 : 1;
			u32 move_line_count =
			    rg_gui_text_area_ensure_layout(ctx, &layout, buffer, ctx->text_edit_state->length,
			                                   ctx->text_edit_state->content_version, wrap_width);
			if (rg_gui_text_area_move_cursor_wrapped(ctx, ctx->text_edit_state,
			                                         visual_lines, move_line_count,
			                                         direction, selecting))
			{
				ctx->text_edit_state->dirty = 1;
				ctx->cursor_visible = 1;
				ctx->cursor_blink_timer = 0.0f;
			}
		}
	}

	if (focused && ctx->active_id == id && ctx->text_edit_state->active &&
	    ctx->text_edit_state->id == id && ctx->mouse_down)
	{
		u32 pick_line_count =
		    rg_gui_text_area_ensure_layout(ctx, &layout, buffer, ctx->text_edit_state->length,
		                                   ctx->text_edit_state->content_version, wrap_width);
		size_t pick = rg_gui_text_area_pick_cursor_wrapped(ctx, visual_lines, pick_line_count,
		                                                   inner, area->panel.scroll_y, line_height);
		rg_gui_text_edit_set_cursor(ctx->text_edit_state, pick, 1);
		ctx->text_edit_state->dirty = 1;
		ctx->cursor_visible = 1;
		ctx->cursor_blink_timer = 0.0f;
	}

	size_t length = strlen(buffer);
	size_t cursor = length;
	size_t preedit_length = 0u;
	const char* render_buffer = buffer;
	RgGuiTextEditPreeditLayout preedit_layout;
	memset(&preedit_layout, 0, sizeof(preedit_layout));
	if (ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		length = ctx->text_edit_state->length;
		cursor = ctx->text_edit_state->cursor;
		if (cursor > length)
		{
			cursor = length;
		}
		if (focused)
		{
			preedit_layout = rg_gui_text_edit_preedit_layout(
			    ctx->text_edit_state, length);
			render_buffer = rg_gui_text_edit_display_text(
			    ctx, ctx->text_edit_state, buffer, length, &length, &preedit_length);
			if (preedit_length > 0u)
			{
				cursor = preedit_layout.caret;
			}
		}
	}

	u32 line_count =
	    rg_gui_text_area_ensure_layout(ctx, &layout, render_buffer, length,
	                                   ctx->text_edit_state->content_version, wrap_width);
	u32 cursor_line =
	    rg_gui_text_area_find_visual_line(visual_lines, line_count, cursor);
	RgGuiTextAreaVisualLine cursor_visual_line = visual_lines[cursor_line];
	size_t cursor_line_start = cursor_visual_line.start;
	size_t cursor_line_end = cursor_visual_line.end;

	f32 content_height = line_height * (f32)line_count;
	area->panel.content_height = content_height;
	area->panel.content_override = 1;

	if (focused && ctx->text_edit_state->active && ctx->text_edit_state->id == id && ctx->mouse_wheel == 0.0f)
	{
		f32 max_scroll = content_height - inner.h;
		if (max_scroll < 0.0f)
		{
			max_scroll = 0.0f;
		}

		f32 cursor_y = (f32)cursor_line * line_height;
		if (cursor_y < area->panel.scroll_y)
		{
			area->panel.scroll_y = cursor_y;
		}
		else if (cursor_y + line_height > area->panel.scroll_y + inner.h)
		{
			area->panel.scroll_y = cursor_y + line_height - inner.h;
		}

		if (area->panel.scroll_y < 0.0f) area->panel.scroll_y = 0.0f;
		if (area->panel.scroll_y > max_scroll) area->panel.scroll_y = max_scroll;
	}

	rg_gui_panel_begin(ctx, &area->panel, rect, 0.0f, id);

	if (focused)
	{
		rg_gui_push_rect_outline(ctx, rect, ctx->style.color_focus_border,
		                         ctx->style.focus_border_thickness);
	}

	RgGuiRect clip = area->panel.inner_rect;
	f32 base_y = clip.y - area->panel.scroll_y;
	f32 text_x = clip.x + ctx->style.padding;

	u32 start_line = 0u;
	if (line_height > 0.0f)
	{
		start_line = rg_gui_nonnegative_f32_to_u32_bounded(area->panel.scroll_y / line_height);
	}
	if (start_line > line_count) start_line = line_count;
	u32 visible_lines = 0u;
	if (line_height > 0.0f)
	{
		visible_lines = rg_gui_nonnegative_f32_to_u32_bounded(clip.h / line_height);
		if (visible_lines < UINT32_MAX) visible_lines++;
	}
	if (visible_lines > line_count - start_line) visible_lines = line_count - start_line;
	u32 end_line = start_line + visible_lines;
	if (end_line > line_count)
	{
		end_line = line_count;
	}

	size_t sel_start = 0u;
	size_t sel_end = 0u;
	int has_selection = 0;
	if (focused && ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		if (preedit_length > 0u)
		{
			sel_start = preedit_layout.selection_start;
			sel_end = preedit_layout.selection_end;
		}
		else if (rg_gui_text_edit_has_selection(ctx->text_edit_state))
		{
			sel_start = ctx->text_edit_state->selection_start;
			sel_end = ctx->text_edit_state->selection_end;
			size_t logical_length = ctx->text_edit_state->length;
			if (sel_start > logical_length) sel_start = logical_length;
			if (sel_end > logical_length) sel_end = logical_length;
		}
		if (sel_start != sel_end)
		{
			has_selection = 1;
		}
	}

	for (u32 line = start_line; line < end_line; line++)
	{
		RgGuiTextAreaVisualLine visual_line = visual_lines[line];
		size_t line_start_index = visual_line.start;
		size_t line_end_index = visual_line.end;
		size_t draw_end_index = line_end_index;
		while (draw_end_index > line_start_index &&
		       (render_buffer[draw_end_index - 1u] == ' ' ||
		        render_buffer[draw_end_index - 1u] == '\t'))
		{
			draw_end_index--;
		}

		f32 line_y = base_y + (f32)line * line_height;
		rg_vec2 text_pos = rg_vec2(text_x,
		                           line_y + (line_height - ctx->style.text_height) * 0.5f);

		if (has_selection)
		{
			size_t sel_line_start = (sel_start > line_start_index) ? sel_start : line_start_index;
			size_t sel_line_end = (sel_end < line_end_index) ? sel_end : line_end_index;
			if (sel_line_start < sel_line_end)
			{
				f32 x0 = text_x + rg_gui_text_area_measure_range(ctx, render_buffer + line_start_index,
				                                                 sel_line_start - line_start_index);
				f32 x1 = text_x + rg_gui_text_area_measure_range(ctx, render_buffer + line_start_index,
				                                                 sel_line_end - line_start_index);
				f32 min_x = clip.x + ctx->style.padding;
				f32 max_x = clip.x + clip.w - ctx->style.padding;
				if (x0 < min_x) x0 = min_x;
				if (x1 > max_x) x1 = max_x;

				if (x1 > x0)
				{
					f32 sel_y = line_y + ctx->style.padding * 0.5f;
					f32 sel_h = line_height - ctx->style.padding;
					rg_gui_push_rect(ctx, rg_gui_make_rect(x0, sel_y, x1 - x0, sel_h),
					                 ctx->style.color_selection);
				}
			}
		}

		if (preedit_length > 0u)
		{
			size_t preedit_start = preedit_layout.preedit_start;
			size_t preedit_end = preedit_layout.preedit_end;
			size_t underline_start = preedit_start > line_start_index ? preedit_start : line_start_index;
			size_t underline_end = preedit_end < line_end_index ? preedit_end : line_end_index;
			if (underline_start < underline_end)
			{
				f32 x0 = text_x + rg_gui_text_area_measure_range(
				                      ctx, render_buffer + line_start_index, underline_start - line_start_index);
				f32 x1 = text_x + rg_gui_text_area_measure_range(
				                      ctx, render_buffer + line_start_index, underline_end - line_start_index);
				if (x1 > x0)
				{
					rg_gui_push_rect(ctx, rg_gui_make_rect(x0, line_y + line_height - ctx->style.padding, x1 - x0, 1.0f),
					                 ctx->style.color_accent);
				}
			}
		}

		if (draw_end_index > line_start_index)
		{
			const char* line_text =
			    rg_gui_copy_text_range(ctx, render_buffer, line_start_index, draw_end_index);
			rg_gui_push_text_ex(ctx, line_text, text_pos, ctx->style.color_text, 0);
		}
	}

	if (focused && ctx->text_edit_state->active && ctx->text_edit_state->id == id)
	{
		size_t column = cursor > cursor_line_start ? cursor - cursor_line_start : 0u;
		size_t max_column =
		    cursor_line_end > cursor_line_start ? cursor_line_end - cursor_line_start : 0u;
		if (column > max_column)
		{
			column = max_column;
		}
		f32 cursor_x = text_x + rg_gui_text_area_measure_range(
		                            ctx, render_buffer + cursor_line_start, column);
		f32 cursor_y = base_y + (f32)cursor_line * line_height + ctx->style.padding * 0.5f;
		f32 cursor_h = line_height - ctx->style.padding;
		RgGuiRect caret = rg_gui_make_rect(cursor_x, cursor_y, 1.0f, cursor_h);
		ctx->platform_output.wants_text_input = 1;
		ctx->platform_output.ime_caret_valid = 1;
		ctx->platform_output.ime_caret_rect = caret;
		if (ctx->ime_callback)
		{
			ctx->ime_callback(ctx, caret, ctx->ime_callback_user);
		}
		if (ctx->cursor_visible && cursor_line >= start_line && cursor_line < end_line)
		{
			rg_gui_push_rect(ctx, caret, ctx->style.color_caret);
		}
	}

	rg_gui_panel_end(ctx, &area->panel);

	return changed;
}

RGINLINE int rg_gui_keybind_internal(RgGuiContext* ctx, const char* label, RgGuiShortcut* shortcut,
                                     RgGuiRect rect, RgGuiId id, int copy_label)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(shortcut != NULL);

	rg_gui_register_focusable(ctx, id);

	f32 inner = ctx->style.inner_spacing;
	if (inner < 0.0f)
	{
		inner = 0.0f;
	}

	RgGuiRect field = rect;
	if (label)
	{
		f32 label_width = ctx->style.label_width;
		if (label_width < 0.0f)
		{
			label_width = 0.0f;
		}
		RgGuiRect label_rect = rg_gui_make_rect(rect.x, rect.y, label_width, rect.h);
		rg_vec2 label_pos = rg_vec2(label_rect.x,
		                            label_rect.y + (label_rect.h - ctx->style.text_height) * 0.5f);
		if (copy_label)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}

		field.x += label_width + inner;
		field.w -= label_width + inner;
	}

	if (field.w < 0.0f)
	{
		field.w = 0.0f;
	}

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, field);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);
	int capturing = enabled && (ctx->active_id == id);
	int changed = 0;

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
		ctx->focus_id = id;
		capturing = 1;
	}

	if (focused &&
	    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE)))
	{
		ctx->active_id = id;
		capturing = 1;
	}

	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}

	if (capturing && ctx->input)
	{
		if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_ESCAPE))
		{
			ctx->active_id = 0u;
			capturing = 0;
		}
		else if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_BACKSPACE) ||
		         rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_DELETE))
		{
			if (shortcut->key != SDL_SCANCODE_UNKNOWN || shortcut->mods != 0u)
			{
				shortcut->key = SDL_SCANCODE_UNKNOWN;
				shortcut->mods = 0u;
				changed = 1;
			}
			ctx->active_id = 0u;
			capturing = 0;
		}
		else
		{
			SDL_Scancode pressed = rg_gui_find_pressed_scancode(ctx->input);
			if (pressed != SDL_SCANCODE_UNKNOWN)
			{
				u32 mods = rg_gui_shortcut_mods_from_input(ctx->input);
				if (shortcut->key != pressed || shortcut->mods != mods)
				{
					shortcut->key = pressed;
					shortcut->mods = mods;
					changed = 1;
				}
				ctx->active_id = 0u;
				capturing = 0;
			}
		}
	}

	rg_vec4 bg = ctx->style.color_bg;
	if (capturing)
	{
		bg = ctx->style.color_bg_active;
	}
	else if (hovered)
	{
		bg = ctx->style.color_bg_hover;
	}
	rg_gui_push_rect(ctx, field, bg);
	rg_gui_push_rect_outline(ctx, field,
	                         (focused || capturing) ? ctx->style.color_focus_border : ctx->style.color_border,
	                         (focused || capturing) ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	const char* display_text = NULL;
	int copy_display = 0;
	rg_vec4 text_color = ctx->style.color_text;
	if (capturing)
	{
		display_text = "Press key";
		copy_display = 0;
	}
	else
	{
		char display_buffer[64];
		display_text = rg_gui_shortcut_to_text(*shortcut, display_buffer, sizeof(display_buffer));
		if (!display_text)
		{
			display_text = "Unbound";
			text_color = ctx->style.color_text_dim;
			copy_display = 0;
		}
		else
		{
			copy_display = 1;
		}
	}

	if (display_text)
	{
		rg_vec2 text_pos = rg_vec2(field.x + ctx->style.padding,
		                           field.y + (field.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, display_text, text_pos, text_color, copy_display);
	}

	return changed;
}

RGINLINE int rg_gui_keybind(RgGuiContext* ctx, const char* label, RgGuiShortcut* shortcut, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_keybind_internal(ctx, label, shortcut, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE int rg_gui_keybind_static(RgGuiContext* ctx, const char* label, RgGuiShortcut* shortcut, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_keybind_internal(ctx, label, shortcut, rect, id, 0);
}

RGINLINE int rg_gui_input_float_internal(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value,
                                         f32 drag_speed, int allow_drag, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(value != NULL);

	f32 label_width = label ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;
	f32 value_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 value_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f);
	if (value_w < 0.0f)
	{
		value_w = 0.0f;
	}

	RgGuiRect label_rect = rg_gui_make_rect(rect.x, rect.y, label_width, rect.h);
	RgGuiRect value_rect = rg_gui_make_rect(value_x, rect.y, value_w, rect.h);

	int enabled = !rg_gui_is_disabled(ctx);
	int clamp = (min_value <= max_value);

	RgGuiId drag_id = id;
	RgGuiId input_id = rg_gui_id_combine(id, 1u);

	rg_gui_register_focusable(ctx, input_id);

	if (!enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	int input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int other_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id != input_id);
	int changed = 0;
	int drag_hovered = 0;
	int dragging = 0;

	if (allow_drag && enabled && !input_active && label_width > 0.0f)
	{
		f32 use_speed = drag_speed;
		if (use_speed == 0.0f)
		{
			use_speed = 1.0f;
		}

		drag_hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, label_rect);
		if (drag_hovered)
		{
			ctx->hot_id = drag_id;
		}

		if (drag_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = drag_id;
			ctx->number_edit_state->drag_id = drag_id;
			ctx->number_edit_state->drag_start = ctx->mouse_pos;
			ctx->number_edit_state->drag_start_value = (f64)*value;
		}

		if (ctx->active_id == drag_id)
		{
			if (!ctx->mouse_down)
			{
				ctx->active_id = 0u;
				if (ctx->number_edit_state->drag_id == drag_id)
				{
					ctx->number_edit_state->drag_id = 0u;
				}
			}
			else if (ctx->number_edit_state->drag_id == drag_id)
			{
				f32 dx = ctx->mouse_pos.x - ctx->number_edit_state->drag_start.x;
				f32 abs_dx = dx < 0.0f ? -dx : dx;
				if (abs_dx >= RG_GUI_DRAG_THRESHOLD)
				{
					f64 new_value = ctx->number_edit_state->drag_start_value + (f64)dx * (f64)use_speed;
					if (clamp)
					{
						if (new_value < (f64)min_value) new_value = (f64)min_value;
						if (new_value > (f64)max_value) new_value = (f64)max_value;
					}
					if ((f32)new_value != *value)
					{
						*value = (f32)new_value;
						changed = 1;
					}
				}
			}
		}

		dragging = (ctx->active_id == drag_id && ctx->number_edit_state->drag_id == drag_id) ? 1 : 0;
		if ((drag_hovered || dragging) && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
		}
	}

	if (label)
	{
		if (allow_drag && label_width > 0.0f && (drag_hovered || dragging))
		{
			rg_vec4 label_bg = dragging ? ctx->style.color_bg_active : ctx->style.color_bg_hover;
			rg_gui_push_rect(ctx, label_rect, label_bg);
			rg_gui_push_rect_outline(ctx, label_rect, ctx->style.color_border, ctx->style.border_thickness);
		}

		rg_vec2 label_pos = rg_vec2(label_rect.x,
		                            label_rect.y + (label_rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	int submit = 0;
	int input_enabled = enabled;
	if (!input_enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int value_hovered = input_enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, value_rect);
	int want_activate = input_enabled && (!input_active && value_hovered && ctx->mouse_pressed);
	int want_tab_focus = input_enabled && (ctx->tab_focus_id == input_id);
	if (want_tab_focus)
	{
		want_activate = 1;
	}
	int use_number_buffer = input_enabled && (input_active || want_activate || !other_active);
	const char* cached_value_text = NULL;
	if (!input_active)
	{
		cached_value_text = rg_gui_cache_value_text(ctx, input_id, *value, ctx->style.value_decimals);
	}

	if (use_number_buffer)
	{
		if (!input_active && !want_activate)
		{
			const char* display_text = cached_value_text;
			char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
			int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
			if (!display_text)
			{
				rg_gui_format_float(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
				display_text = display_buffer;
				copy_display = 1;
			}

			if (value_hovered)
			{
				ctx->hot_id = input_id;
			}

			rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
			rg_gui_push_rect(ctx, value_rect, bg);
			rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

			rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
			                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
			rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
		}
		else
		{
			if (!input_active)
			{
				const char* cached_text = cached_value_text;
				if (!cached_text)
				{
					rg_gui_format_float(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), *value, ctx->style.value_decimals);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
				else if (ctx->number_edit_state->cached_id != input_id ||
				         ctx->number_edit_state->cached_value != *value ||
				         ctx->number_edit_state->cached_decimals != ctx->style.value_decimals)
				{
					rg_gui_copy_value_buffer(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), cached_text);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
			}
			else
			{
				ctx->number_edit_state->cached_id = 0u;
			}

			rg_gui_text_input_ex_internal(ctx, NULL, ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                              value_rect, input_id,
			                              RG_GUI_TEXT_INPUT_NO_LABEL | RG_GUI_TEXT_INPUT_UNDO_VALUE, &submit);
		}
	}
	else
	{
		const char* display_text = cached_value_text;
		char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
		int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
		if (!display_text)
		{
			rg_gui_format_float(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
			display_text = display_buffer;
			copy_display = 1;
		}

		if (value_hovered)
		{
			ctx->hot_id = input_id;
		}

		rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, value_rect, bg);
		rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
		                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	if (use_number_buffer)
	{
		if (input_active && ctx->number_edit_state->id != input_id)
		{
			ctx->number_edit_state->id = input_id;
		}

		if (ctx->number_edit_state->id == input_id)
		{
			if (submit || (!input_active && ctx->focus_id != input_id))
			{
				char* end_ptr = NULL;
				f32 parsed = strtof(ctx->number_edit_state->buffer, &end_ptr);
				if (end_ptr != ctx->number_edit_state->buffer)
				{
					if (clamp)
					{
						if (parsed < min_value) parsed = min_value;
						if (parsed > max_value) parsed = max_value;
					}
					if (parsed != *value)
					{
						f64 prev_value = (f64)*value;
						f64 next_value = (f64)parsed;
						rg_gui_text_value_undo_push_number(ctx, input_id, value,
						                                   prev_value, next_value,
						                                   RG_GUI_TEXT_VALUE_UNDO_FLOAT);
						*value = parsed;
						changed = 1;
					}
				}
				ctx->number_edit_state->id = 0u;
				ctx->number_edit_state->cached_id = 0u;
			}
		}
	}

	return changed;
}

RGINLINE int rg_gui_input_double_internal(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value,
                                          f64 drag_speed, int allow_drag, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(value != NULL);

	f32 label_width = label ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;
	f32 value_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 value_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f);
	if (value_w < 0.0f)
	{
		value_w = 0.0f;
	}

	RgGuiRect label_rect = rg_gui_make_rect(rect.x, rect.y, label_width, rect.h);
	RgGuiRect value_rect = rg_gui_make_rect(value_x, rect.y, value_w, rect.h);

	int enabled = !rg_gui_is_disabled(ctx);
	int clamp = (min_value <= max_value);

	RgGuiId drag_id = id;
	RgGuiId input_id = rg_gui_id_combine(id, 1u);

	rg_gui_register_focusable(ctx, input_id);

	if (!enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	int input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int other_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id != input_id);
	int changed = 0;
	int drag_hovered = 0;
	int dragging = 0;

	if (allow_drag && enabled && !input_active && label_width > 0.0f)
	{
		f64 use_speed = drag_speed;
		if (use_speed == 0.0)
		{
			use_speed = 1.0;
		}

		drag_hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, label_rect);
		if (drag_hovered)
		{
			ctx->hot_id = drag_id;
		}

		if (drag_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = drag_id;
			ctx->number_edit_state->drag_id = drag_id;
			ctx->number_edit_state->drag_start = ctx->mouse_pos;
			ctx->number_edit_state->drag_start_value = *value;
		}

		if (ctx->active_id == drag_id)
		{
			if (!ctx->mouse_down)
			{
				ctx->active_id = 0u;
				if (ctx->number_edit_state->drag_id == drag_id)
				{
					ctx->number_edit_state->drag_id = 0u;
				}
			}
			else if (ctx->number_edit_state->drag_id == drag_id)
			{
				f32 dx = ctx->mouse_pos.x - ctx->number_edit_state->drag_start.x;
				f32 abs_dx = dx < 0.0f ? -dx : dx;
				if (abs_dx >= RG_GUI_DRAG_THRESHOLD)
				{
					f64 new_value = ctx->number_edit_state->drag_start_value + (f64)dx * use_speed;
					if (clamp)
					{
						if (new_value < min_value) new_value = min_value;
						if (new_value > max_value) new_value = max_value;
					}
					if (new_value != *value)
					{
						f64 prev_value = (f64)*value;
						f64 next_value = (f64)new_value;
						rg_gui_text_value_undo_push_number(ctx, input_id, value,
						                                   prev_value, next_value,
						                                   RG_GUI_TEXT_VALUE_UNDO_INT);
						*value = new_value;
						changed = 1;
					}
				}
			}
		}

		dragging = (ctx->active_id == drag_id && ctx->number_edit_state->drag_id == drag_id) ? 1 : 0;
		if ((drag_hovered || dragging) && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
		}
	}

	if (label)
	{
		if (allow_drag && label_width > 0.0f && (drag_hovered || dragging))
		{
			rg_vec4 label_bg = dragging ? ctx->style.color_bg_active : ctx->style.color_bg_hover;
			rg_gui_push_rect(ctx, label_rect, label_bg);
			rg_gui_push_rect_outline(ctx, label_rect, ctx->style.color_border, ctx->style.border_thickness);
		}

		rg_vec2 label_pos = rg_vec2(label_rect.x,
		                            label_rect.y + (label_rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	int submit = 0;
	int input_enabled = enabled;
	if (!input_enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int value_hovered = input_enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, value_rect);
	int want_activate = input_enabled && (!input_active && value_hovered && ctx->mouse_pressed);
	int want_tab_focus = input_enabled && (ctx->tab_focus_id == input_id);
	if (want_tab_focus)
	{
		want_activate = 1;
	}
	int use_number_buffer = input_enabled && (input_active || want_activate || !other_active);
	const char* cached_value_text = NULL;
	if (!input_active)
	{
		cached_value_text = rg_gui_cache_value_text(ctx, input_id, *value, ctx->style.value_decimals);
	}

	if (use_number_buffer)
	{
		if (!input_active && !want_activate)
		{
			const char* display_text = cached_value_text;
			char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
			int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
			if (!display_text)
			{
				rg_gui_format_double(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
				display_text = display_buffer;
				copy_display = 1;
			}

			if (value_hovered)
			{
				ctx->hot_id = input_id;
			}

			rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
			rg_gui_push_rect(ctx, value_rect, bg);
			rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

			rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
			                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
			rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
		}
		else
		{
			if (!input_active)
			{
				const char* cached_text = cached_value_text;
				if (!cached_text)
				{
					rg_gui_format_double(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), *value, ctx->style.value_decimals);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
				else if (ctx->number_edit_state->cached_id != input_id ||
				         ctx->number_edit_state->cached_value != *value ||
				         ctx->number_edit_state->cached_decimals != ctx->style.value_decimals)
				{
					rg_gui_copy_value_buffer(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), cached_text);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
			}
			else
			{
				ctx->number_edit_state->cached_id = 0u;
			}

			rg_gui_text_input_ex_internal(ctx, NULL, ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                              value_rect, input_id,
			                              RG_GUI_TEXT_INPUT_NO_LABEL | RG_GUI_TEXT_INPUT_UNDO_VALUE, &submit);
		}
	}
	else
	{
		const char* display_text = cached_value_text;
		char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
		int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
		if (!display_text)
		{
			rg_gui_format_double(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
			display_text = display_buffer;
			copy_display = 1;
		}

		if (value_hovered)
		{
			ctx->hot_id = input_id;
		}

		rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, value_rect, bg);
		rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
		                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	if (use_number_buffer)
	{
		if (input_active && ctx->number_edit_state->id != input_id)
		{
			ctx->number_edit_state->id = input_id;
		}

		if (ctx->number_edit_state->id == input_id)
		{
			if (submit || (!input_active && ctx->focus_id != input_id))
			{
				char* end_ptr = NULL;
				f64 parsed = strtod(ctx->number_edit_state->buffer, &end_ptr);
				if (end_ptr != ctx->number_edit_state->buffer)
				{
					if (clamp)
					{
						if (parsed < min_value) parsed = min_value;
						if (parsed > max_value) parsed = max_value;
					}
					if (parsed != *value)
					{
						f64 prev_value = *value;
						f64 next_value = parsed;
						rg_gui_text_value_undo_push_number(ctx, input_id, value,
						                                   prev_value, next_value,
						                                   RG_GUI_TEXT_VALUE_UNDO_DOUBLE);
						*value = parsed;
						changed = 1;
					}
				}
				ctx->number_edit_state->id = 0u;
				ctx->number_edit_state->cached_id = 0u;
			}
		}
	}

	return changed;
}

RGINLINE int rg_gui_input_int_internal(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value,
                                       f32 drag_speed, int allow_drag, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(value != NULL);

	f32 label_width = label ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;
	f32 value_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 value_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f);
	if (value_w < 0.0f)
	{
		value_w = 0.0f;
	}

	RgGuiRect label_rect = rg_gui_make_rect(rect.x, rect.y, label_width, rect.h);
	RgGuiRect value_rect = rg_gui_make_rect(value_x, rect.y, value_w, rect.h);

	int enabled = !rg_gui_is_disabled(ctx);
	int clamp = (min_value <= max_value);

	RgGuiId drag_id = id;
	RgGuiId input_id = rg_gui_id_combine(id, 1u);

	rg_gui_register_focusable(ctx, input_id);

	if (!enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	int input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int other_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id != input_id);
	int changed = 0;
	int drag_hovered = 0;
	int dragging = 0;

	if (allow_drag && enabled && !input_active && label_width > 0.0f)
	{
		f32 use_speed = drag_speed;
		if (use_speed == 0.0f)
		{
			use_speed = 1.0f;
		}

		drag_hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, label_rect);
		if (drag_hovered)
		{
			ctx->hot_id = drag_id;
		}

		if (drag_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = drag_id;
			ctx->number_edit_state->drag_id = drag_id;
			ctx->number_edit_state->drag_start = ctx->mouse_pos;
			ctx->number_edit_state->drag_start_value = (f64)*value;
		}

		if (ctx->active_id == drag_id)
		{
			if (!ctx->mouse_down)
			{
				ctx->active_id = 0u;
				if (ctx->number_edit_state->drag_id == drag_id)
				{
					ctx->number_edit_state->drag_id = 0u;
				}
			}
			else if (ctx->number_edit_state->drag_id == drag_id)
			{
				f32 dx = ctx->mouse_pos.x - ctx->number_edit_state->drag_start.x;
				f32 abs_dx = dx < 0.0f ? -dx : dx;
				if (abs_dx >= RG_GUI_DRAG_THRESHOLD)
				{
					f64 new_value = ctx->number_edit_state->drag_start_value + (f64)dx * (f64)use_speed;
					int rounded = *value;
					if (rg_gui_round_f64_to_int_saturated(new_value, &rounded) && clamp)
					{
						if (rounded < min_value) rounded = min_value;
						if (rounded > max_value) rounded = max_value;
					}
					if (rounded != *value)
					{
						*value = rounded;
						changed = 1;
					}
				}
			}
		}

		dragging = (ctx->active_id == drag_id && ctx->number_edit_state->drag_id == drag_id) ? 1 : 0;
		if ((drag_hovered || dragging) && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
		}
	}

	if (label)
	{
		if (allow_drag && label_width > 0.0f && (drag_hovered || dragging))
		{
			rg_vec4 label_bg = dragging ? ctx->style.color_bg_active : ctx->style.color_bg_hover;
			rg_gui_push_rect(ctx, label_rect, label_bg);
			rg_gui_push_rect_outline(ctx, label_rect, ctx->style.color_border, ctx->style.border_thickness);
		}

		rg_vec2 label_pos = rg_vec2(label_rect.x,
		                            label_rect.y + (label_rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	int submit = 0;
	int input_enabled = enabled;
	if (!input_enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int value_hovered = input_enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, value_rect);
	int want_activate = input_enabled && (!input_active && value_hovered && ctx->mouse_pressed);
	int want_tab_focus = input_enabled && (ctx->tab_focus_id == input_id);
	if (want_tab_focus)
	{
		want_activate = 1;
	}
	int use_number_buffer = input_enabled && (input_active || want_activate || !other_active);
	const char* cached_value_text = NULL;
	if (!input_active)
	{
		cached_value_text = rg_gui_cache_value_text(ctx, input_id, (f64)*value, 0);
	}

	if (use_number_buffer)
	{
		if (!input_active && !want_activate)
		{
			const char* display_text = cached_value_text;
			char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
			int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
			if (!display_text)
			{
				rg_snprintf(display_buffer, sizeof(display_buffer), "%d", *value);
				display_text = display_buffer;
				copy_display = 1;
			}

			if (value_hovered)
			{
				ctx->hot_id = input_id;
			}

			rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
			rg_gui_push_rect(ctx, value_rect, bg);
			rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

			rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
			                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
			rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
		}
		else
		{
			if (!input_active)
			{
				const char* cached_text = cached_value_text;
				if (!cached_text)
				{
					rg_snprintf(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), "%d", *value);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = (f64)*value;
					ctx->number_edit_state->cached_decimals = 0;
				}
				else if (ctx->number_edit_state->cached_id != input_id ||
				         ctx->number_edit_state->cached_value != (f64)*value ||
				         ctx->number_edit_state->cached_decimals != 0)
				{
					rg_gui_copy_value_buffer(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), cached_text);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = (f64)*value;
					ctx->number_edit_state->cached_decimals = 0;
				}
			}
			else
			{
				ctx->number_edit_state->cached_id = 0u;
			}

			rg_gui_text_input_ex_internal(ctx, NULL, ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                              value_rect, input_id,
			                              RG_GUI_TEXT_INPUT_NO_LABEL | RG_GUI_TEXT_INPUT_UNDO_VALUE, &submit);
		}
	}
	else
	{
		const char* display_text = cached_value_text;
		char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
		int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
		if (!display_text)
		{
			rg_snprintf(display_buffer, sizeof(display_buffer), "%d", *value);
			display_text = display_buffer;
			copy_display = 1;
		}

		if (value_hovered)
		{
			ctx->hot_id = input_id;
		}

		rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, value_rect, bg);
		rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
		                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	if (use_number_buffer)
	{
		if (input_active && ctx->number_edit_state->id != input_id)
		{
			ctx->number_edit_state->id = input_id;
		}

		if (ctx->number_edit_state->id == input_id)
		{
			if (submit || (!input_active && ctx->focus_id != input_id))
			{
				int new_value = 0;
				if (rg_gui_parse_int_saturated(ctx->number_edit_state->buffer, &new_value))
				{
					if (clamp)
					{
						if (new_value < min_value) new_value = min_value;
						if (new_value > max_value) new_value = max_value;
					}
					if (new_value != *value)
					{
						*value = new_value;
						changed = 1;
					}
				}
				ctx->number_edit_state->id = 0u;
				ctx->number_edit_state->cached_id = 0u;
			}
		}
	}

	return changed;
}

RGINLINE int rg_gui_input_vec_internal(RgGuiContext* ctx, const char* label, f32* values, u32 count,
                                       f32 min_value, f32 max_value, f32 drag_speed, int allow_drag,
                                       RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(values != NULL);

	if (count == 0u)
	{
		return 0;
	}

	if (count > 4u)
	{
		count = 4u;
	}

	f32 label_width = label ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;
	f32 fields_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 fields_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f);
	if (fields_w < 0.0f)
	{
		fields_w = 0.0f;
	}

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	f32 gaps = inner * (f32)(count - 1u);
	f32 field_w = (fields_w - gaps) / (f32)count;
	if (field_w < 0.0f)
	{
		field_w = 0.0f;
	}

	f32 saved_label_width = ctx->style.label_width;
	ctx->style.label_width = ctx->style.text_height;

	static const char* const kAxisLabels[4] = {"X", "Y", "Z", "W"};

	int changed = 0;
	f32 x = fields_x;
	for (u32 i = 0u; i < count; i++)
	{
		RgGuiRect field = rg_gui_make_rect(x, rect.y, field_w, rect.h);
		RgGuiId field_id = rg_gui_id_combine(id, (u64)(i + 1u));
		changed |= rg_gui_input_float_internal(ctx, kAxisLabels[i], &values[i],
		                                       min_value, max_value, drag_speed, allow_drag,
		                                       field, field_id);
		x += field_w + inner;
	}

	ctx->style.label_width = saved_label_width;
	return changed;
}

RGINLINE int rg_gui_input_int(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_int_internal(ctx, label, value, min_value, max_value, 0.0f, 0, rect, id);
}

RGINLINE int rg_gui_input_int_drag(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_int_internal(ctx, label, value, min_value, max_value, drag_speed, 1, rect, id);
}

RGINLINE int rg_gui_input_float(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_float_internal(ctx, label, value, min_value, max_value, 0.0f, 0, rect, id);
}

RGINLINE int rg_gui_input_float_drag(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_float_internal(ctx, label, value, min_value, max_value, drag_speed, 1, rect, id);
}

RGINLINE int rg_gui_input_double(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_double_internal(ctx, label, value, min_value, max_value, 0.0, 0, rect, id);
}

RGINLINE int rg_gui_input_double_drag(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value, f64 drag_speed, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_double_internal(ctx, label, value, min_value, max_value, drag_speed, 1, rect, id);
}

RGINLINE int rg_gui_input_vec2(RgGuiContext* ctx, const char* label, rg_vec2* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_vec_internal(ctx, label, value->data, 2u, min_value, max_value, 0.0f, 0, rect, id);
}

RGINLINE int rg_gui_input_vec2_drag(RgGuiContext* ctx, const char* label, rg_vec2* value, f32 min_value, f32 max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_vec_internal(ctx, label, value->data, 2u, min_value, max_value, drag_speed, 1, rect, id);
}

RGINLINE int rg_gui_input_vec3(RgGuiContext* ctx, const char* label, rg_vec3* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_vec_internal(ctx, label, value->data, 3u, min_value, max_value, 0.0f, 0, rect, id);
}

RGINLINE int rg_gui_input_vec3_drag(RgGuiContext* ctx, const char* label, rg_vec3* value, f32 min_value, f32 max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_vec_internal(ctx, label, value->data, 3u, min_value, max_value, drag_speed, 1, rect, id);
}

RGINLINE int rg_gui_input_vec4(RgGuiContext* ctx, const char* label, rg_vec4* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_vec_internal(ctx, label, value->data, 4u, min_value, max_value, 0.0f, 0, rect, id);
}

RGINLINE int rg_gui_input_vec4_drag(RgGuiContext* ctx, const char* label, rg_vec4* value, f32 min_value, f32 max_value, f32 drag_speed, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_input_vec_internal(ctx, label, value->data, 4u, min_value, max_value, drag_speed, 1, rect, id);
}

RGINLINE void rg_gui_draw_value_text_float(RgGuiContext* ctx, RgGuiRect rect, RgGuiId id, f32 value, int dim)
{
	const char* value_text = rg_gui_cache_value_text(ctx, id, value, ctx->style.value_decimals);
	char value_fallback[RG_GUI_NUMBER_BUFFER_SIZE];
	int copy_value = RG_GUI_COPY_DYNAMIC_TEXT;
	if (!value_text)
	{
		rg_gui_format_float(value_fallback, sizeof(value_fallback), value, ctx->style.value_decimals);
		value_text = value_fallback;
		copy_value = 1;
	}
	rg_vec2 value_pos = rg_vec2(rect.x + ctx->style.padding,
	                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
	rg_gui_push_text_ex(ctx, value_text, value_pos,
	                    dim ? ctx->style.color_text_dim : ctx->style.color_text,
	                    copy_value);
}

RGINLINE void rg_gui_draw_value_text_int(RgGuiContext* ctx, RgGuiRect rect, RgGuiId id, int value, int dim)
{
	const char* value_text = rg_gui_cache_value_text(ctx, id, (f64)value, 0);
	char value_fallback[RG_GUI_NUMBER_BUFFER_SIZE];
	int copy_value = RG_GUI_COPY_DYNAMIC_TEXT;
	if (!value_text)
	{
		rg_snprintf(value_fallback, sizeof(value_fallback), "%d", value);
		value_text = value_fallback;
		copy_value = 1;
	}
	rg_vec2 value_pos = rg_vec2(rect.x + ctx->style.padding,
	                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
	rg_gui_push_text_ex(ctx, value_text, value_pos,
	                    dim ? ctx->style.color_text_dim : ctx->style.color_text,
	                    copy_value);
}

RGINLINE void rg_gui_draw_value_text_double(RgGuiContext* ctx, RgGuiRect rect, RgGuiId id, f64 value, int dim)
{
	const char* value_text = rg_gui_cache_value_text(ctx, id, value, ctx->style.value_decimals);
	char value_fallback[RG_GUI_NUMBER_BUFFER_SIZE];
	int copy_value = RG_GUI_COPY_DYNAMIC_TEXT;
	if (!value_text)
	{
		rg_gui_format_double(value_fallback, sizeof(value_fallback), value, ctx->style.value_decimals);
		value_text = value_fallback;
		copy_value = 1;
	}
	rg_vec2 value_pos = rg_vec2(rect.x + ctx->style.padding,
	                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
	rg_gui_push_text_ex(ctx, value_text, value_pos,
	                    dim ? ctx->style.color_text_dim : ctx->style.color_text,
	                    copy_value);
}

RGINLINE int rg_gui_value_input_float(RgGuiContext* ctx, RgGuiRect rect, RgGuiId input_id,
                                      f32* value, f32 min_value, f32 max_value)
{
	rg_gui_register_focusable(ctx, input_id);

	int enabled = !rg_gui_is_disabled(ctx);

	if (!enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	int submit = 0;
	int input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int other_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id != input_id);
	int value_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (value_hovered && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_TEXT;
	}
	int want_activate = enabled && (!input_active && value_hovered && ctx->mouse_pressed);
	int want_tab_focus = enabled && (ctx->tab_focus_id == input_id);
	if (want_tab_focus)
	{
		want_activate = 1;
	}
	int use_number_buffer = enabled && (input_active || want_activate || !other_active);
	const char* cached_value_text = NULL;
	if (!input_active)
	{
		cached_value_text = rg_gui_cache_value_text(ctx, input_id, *value, ctx->style.value_decimals);
	}

	if (use_number_buffer)
	{
		if (!input_active && !want_activate)
		{
			const char* display_text = cached_value_text;
			char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
			int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
			if (!display_text)
			{
				rg_gui_format_float(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
				display_text = display_buffer;
				copy_display = 1;
			}

			if (value_hovered)
			{
				ctx->hot_id = input_id;
			}

			rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
			rg_gui_push_rect(ctx, rect, bg);
			rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

			rg_vec2 text_pos = rg_vec2(rect.x + ctx->style.padding,
			                           rect.y + (rect.h - ctx->style.text_height) * 0.5f);
			rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
		}
		else
		{
			if (!input_active)
			{
				const char* cached_text = cached_value_text;
				if (!cached_text)
				{
					rg_gui_format_float(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), *value, ctx->style.value_decimals);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
				else if (ctx->number_edit_state->cached_id != input_id ||
				         ctx->number_edit_state->cached_value != *value ||
				         ctx->number_edit_state->cached_decimals != ctx->style.value_decimals)
				{
					rg_gui_copy_value_buffer(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), cached_text);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
			}
			else
			{
				ctx->number_edit_state->cached_id = 0u;
			}

			rg_gui_text_input_ex_internal(ctx, NULL, ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                              rect, input_id,
			                              RG_GUI_TEXT_INPUT_NO_LABEL | RG_GUI_TEXT_INPUT_UNDO_VALUE, &submit);
		}
	}
	else
	{
		const char* display_text = cached_value_text;
		char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
		int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
		if (!display_text)
		{
			rg_gui_format_float(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
			display_text = display_buffer;
			copy_display = 1;
		}

		if (value_hovered)
		{
			ctx->hot_id = input_id;
		}

		rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, rect, bg);
		rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec2 text_pos = rg_vec2(rect.x + ctx->style.padding,
		                           rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
	}

	input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int changed = 0;
	if (use_number_buffer)
	{
		if (input_active && ctx->number_edit_state->id != input_id)
		{
			ctx->number_edit_state->id = input_id;
		}

		if (ctx->number_edit_state->id == input_id)
		{
			if (submit || (!input_active && ctx->focus_id != input_id))
			{
				char* end_ptr = NULL;
				f32 parsed = strtof(ctx->number_edit_state->buffer, &end_ptr);
				if (end_ptr != ctx->number_edit_state->buffer)
				{
					if (parsed < min_value) parsed = min_value;
					if (parsed > max_value) parsed = max_value;
					if (parsed != *value)
					{
						f64 prev_value = (f64)*value;
						f64 next_value = (f64)parsed;
						rg_gui_text_value_undo_push_number(ctx, input_id, value,
						                                   prev_value, next_value,
						                                   RG_GUI_TEXT_VALUE_UNDO_FLOAT);
						*value = parsed;
						changed = 1;
					}
				}
				ctx->number_edit_state->id = 0u;
				ctx->number_edit_state->cached_id = 0u;
			}
		}
	}

	return changed;
}

RGINLINE int rg_gui_value_input_int(RgGuiContext* ctx, RgGuiRect rect, RgGuiId input_id,
                                    int* value, int min_value, int max_value)
{
	rg_gui_register_focusable(ctx, input_id);

	int enabled = !rg_gui_is_disabled(ctx);

	if (!enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	int submit = 0;
	int input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int other_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id != input_id);
	int value_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (value_hovered && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_TEXT;
	}
	int want_activate = enabled && (!input_active && value_hovered && ctx->mouse_pressed);
	int want_tab_focus = enabled && (ctx->tab_focus_id == input_id);
	if (want_tab_focus)
	{
		want_activate = 1;
	}
	int use_number_buffer = enabled && (input_active || want_activate || !other_active);
	const char* cached_value_text = NULL;
	if (!input_active)
	{
		cached_value_text = rg_gui_cache_value_text(ctx, input_id, (f64)*value, 0);
	}

	if (use_number_buffer)
	{
		if (!input_active && !want_activate)
		{
			const char* display_text = cached_value_text;
			char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
			int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
			if (!display_text)
			{
				rg_snprintf(display_buffer, sizeof(display_buffer), "%d", *value);
				display_text = display_buffer;
				copy_display = 1;
			}

			if (value_hovered)
			{
				ctx->hot_id = input_id;
			}

			rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
			rg_gui_push_rect(ctx, rect, bg);
			rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

			rg_vec2 text_pos = rg_vec2(rect.x + ctx->style.padding,
			                           rect.y + (rect.h - ctx->style.text_height) * 0.5f);
			rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
		}
		else
		{
			if (!input_active)
			{
				const char* cached_text = cached_value_text;
				if (!cached_text)
				{
					rg_snprintf(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), "%d", *value);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = (f64)*value;
					ctx->number_edit_state->cached_decimals = 0;
				}
				else if (ctx->number_edit_state->cached_id != input_id ||
				         ctx->number_edit_state->cached_value != (f64)*value ||
				         ctx->number_edit_state->cached_decimals != 0)
				{
					rg_gui_copy_value_buffer(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), cached_text);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = (f64)*value;
					ctx->number_edit_state->cached_decimals = 0;
				}
			}
			else
			{
				ctx->number_edit_state->cached_id = 0u;
			}

			rg_gui_text_input_ex_internal(ctx, NULL, ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                              rect, input_id,
			                              RG_GUI_TEXT_INPUT_NO_LABEL | RG_GUI_TEXT_INPUT_UNDO_VALUE, &submit);
		}
	}
	else
	{
		const char* display_text = cached_value_text;
		char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
		int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
		if (!display_text)
		{
			rg_snprintf(display_buffer, sizeof(display_buffer), "%d", *value);
			display_text = display_buffer;
			copy_display = 1;
		}

		if (value_hovered)
		{
			ctx->hot_id = input_id;
		}

		rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, rect, bg);
		rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec2 text_pos = rg_vec2(rect.x + ctx->style.padding,
		                           rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
	}

	input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int changed = 0;
	if (use_number_buffer)
	{
		if (input_active && ctx->number_edit_state->id != input_id)
		{
			ctx->number_edit_state->id = input_id;
		}

		if (ctx->number_edit_state->id == input_id)
		{
			if (submit || (!input_active && ctx->focus_id != input_id))
			{
				int new_value = 0;
				if (rg_gui_parse_int_saturated(ctx->number_edit_state->buffer, &new_value))
				{
					if (new_value < min_value) new_value = min_value;
					if (new_value > max_value) new_value = max_value;
					if (new_value != *value)
					{
						f64 prev_value = (f64)*value;
						f64 next_value = (f64)new_value;
						rg_gui_text_value_undo_push_number(ctx, input_id, value,
						                                   prev_value, next_value,
						                                   RG_GUI_TEXT_VALUE_UNDO_INT);
						*value = new_value;
						changed = 1;
					}
				}
				ctx->number_edit_state->id = 0u;
				ctx->number_edit_state->cached_id = 0u;
			}
		}
	}

	return changed;
}

RGINLINE int rg_gui_value_input_double(RgGuiContext* ctx, RgGuiRect rect, RgGuiId input_id,
                                       f64* value, f64 min_value, f64 max_value)
{
	rg_gui_register_focusable(ctx, input_id);

	int enabled = !rg_gui_is_disabled(ctx);

	if (!enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	int submit = 0;
	int input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int other_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id != input_id);
	int value_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (value_hovered && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_TEXT;
	}
	int want_activate = enabled && (!input_active && value_hovered && ctx->mouse_pressed);
	int want_tab_focus = enabled && (ctx->tab_focus_id == input_id);
	if (want_tab_focus)
	{
		want_activate = 1;
	}
	int use_number_buffer = enabled && (input_active || want_activate || !other_active);
	const char* cached_value_text = NULL;
	if (!input_active)
	{
		cached_value_text = rg_gui_cache_value_text(ctx, input_id, *value, ctx->style.value_decimals);
	}

	if (use_number_buffer)
	{
		if (!input_active && !want_activate)
		{
			const char* display_text = cached_value_text;
			char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
			int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
			if (!display_text)
			{
				rg_gui_format_double(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
				display_text = display_buffer;
				copy_display = 1;
			}

			if (value_hovered)
			{
				ctx->hot_id = input_id;
			}

			rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
			rg_gui_push_rect(ctx, rect, bg);
			rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

			rg_vec2 text_pos = rg_vec2(rect.x + ctx->style.padding,
			                           rect.y + (rect.h - ctx->style.text_height) * 0.5f);
			rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
		}
		else
		{
			if (!input_active)
			{
				const char* cached_text = cached_value_text;
				if (!cached_text)
				{
					rg_gui_format_double(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), *value, ctx->style.value_decimals);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
				else if (ctx->number_edit_state->cached_id != input_id ||
				         ctx->number_edit_state->cached_value != *value ||
				         ctx->number_edit_state->cached_decimals != ctx->style.value_decimals)
				{
					rg_gui_copy_value_buffer(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), cached_text);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
			}
			else
			{
				ctx->number_edit_state->cached_id = 0u;
			}

			rg_gui_text_input_ex_internal(ctx, NULL, ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                              rect, input_id,
			                              RG_GUI_TEXT_INPUT_NO_LABEL | RG_GUI_TEXT_INPUT_UNDO_VALUE, &submit);
		}
	}
	else
	{
		const char* display_text = cached_value_text;
		char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
		int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
		if (!display_text)
		{
			rg_gui_format_double(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
			display_text = display_buffer;
			copy_display = 1;
		}

		if (value_hovered)
		{
			ctx->hot_id = input_id;
		}

		rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, rect, bg);
		rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec2 text_pos = rg_vec2(rect.x + ctx->style.padding,
		                           rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
	}

	input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int changed = 0;
	if (use_number_buffer)
	{
		if (input_active && ctx->number_edit_state->id != input_id)
		{
			ctx->number_edit_state->id = input_id;
		}

		if (ctx->number_edit_state->id == input_id)
		{
			if (submit || (!input_active && ctx->focus_id != input_id))
			{
				char* end_ptr = NULL;
				f64 parsed = strtod(ctx->number_edit_state->buffer, &end_ptr);
				if (end_ptr != ctx->number_edit_state->buffer)
				{
					if (parsed < min_value) parsed = min_value;
					if (parsed > max_value) parsed = max_value;
					if (parsed != *value)
					{
						f64 prev_value = *value;
						f64 next_value = parsed;
						rg_gui_text_value_undo_push_number(ctx, input_id, value,
						                                   prev_value, next_value,
						                                   RG_GUI_TEXT_VALUE_UNDO_DOUBLE);
						*value = parsed;
						changed = 1;
					}
				}
				ctx->number_edit_state->id = 0u;
				ctx->number_edit_state->cached_id = 0u;
			}
		}
	}

	return changed;
}

RGINLINE int rg_gui_slider_range_float_internal(RgGuiContext* ctx, const char* label, f32* min_value, f32* max_value,
                                                f32 min_limit, f32 max_limit, RgGuiRect rect, RgGuiId id, int with_input)
{
	RG_GUI_ASSERT(min_value != NULL);
	RG_GUI_ASSERT(max_value != NULL);

	int enabled = !rg_gui_is_disabled(ctx);

	f32 min = *min_value;
	f32 max = *max_value;
	f32 limit_min = min_limit;
	f32 limit_max = max_limit;
	if (limit_max < limit_min)
	{
		f32 tmp = limit_min;
		limit_min = limit_max;
		limit_max = tmp;
	}

	int changed = 0;
	if (min > max)
	{
		f32 tmp = min;
		min = max;
		max = tmp;
		changed = 1;
	}

	if (min < limit_min)
	{
		min = limit_min;
		changed = 1;
	}
	if (min > limit_max)
	{
		min = limit_max;
		changed = 1;
	}
	if (max < limit_min)
	{
		max = limit_min;
		changed = 1;
	}
	if (max > limit_max)
	{
		max = limit_max;
		changed = 1;
	}
	if (min > max)
	{
		min = max;
		changed = 1;
	}

	f32 label_width = label ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;
	f32 value_w = ctx->style.value_width;

	f32 content_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 content_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f);
	if (content_w < 0.0f)
	{
		content_w = 0.0f;
	}

	f32 total_inner = inner * 2.0f;
	if (value_w * 2.0f + total_inner > content_w)
	{
		value_w = (content_w - total_inner) * 0.5f;
		if (value_w < 0.0f)
		{
			value_w = 0.0f;
		}
	}

	f32 slider_w = content_w - value_w * 2.0f - total_inner;
	if (slider_w < 0.0f)
	{
		slider_w = 0.0f;
	}

	RgGuiRect slider_rect = rg_gui_make_rect(content_x, rect.y, slider_w, rect.h);
	RgGuiRect min_value_rect = rg_gui_make_rect(slider_rect.x + slider_rect.w + inner, rect.y, value_w, rect.h);
	RgGuiRect max_value_rect = rg_gui_make_rect(min_value_rect.x + value_w + inner, rect.y, value_w, rect.h);

	RgGuiId min_id = rg_gui_id_combine(id, 1u);
	RgGuiId max_id = rg_gui_id_combine(id, 2u);
	RgGuiId min_input_id = rg_gui_id_combine(id, 3u);
	RgGuiId max_input_id = rg_gui_id_combine(id, 4u);

	rg_gui_register_focusable(ctx, min_id);
	rg_gui_register_focusable(ctx, max_id);

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	f32 range = limit_max - limit_min;
	f32 t_min = 0.0f;
	f32 t_max = 0.0f;
	if (range > 0.0f)
	{
		t_min = (min - limit_min) / range;
		t_max = (max - limit_min) / range;
	}
	if (t_min < 0.0f) t_min = 0.0f;
	if (t_min > 1.0f) t_min = 1.0f;
	if (t_max < 0.0f) t_max = 0.0f;
	if (t_max > 1.0f) t_max = 1.0f;
	f32 handle_w = ctx->style.slider_handle_width;
	f32 min_handle_x = slider_rect.x + slider_rect.w * t_min - handle_w * 0.5f;
	f32 max_handle_x = slider_rect.x + slider_rect.w * t_max - handle_w * 0.5f;
	if (slider_rect.w <= handle_w)
	{
		min_handle_x = slider_rect.x;
		max_handle_x = slider_rect.x;
	}
	else
	{
		if (min_handle_x < slider_rect.x)
		{
			min_handle_x = slider_rect.x;
		}
		if (min_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			min_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
		if (max_handle_x < slider_rect.x)
		{
			max_handle_x = slider_rect.x;
		}
		if (max_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			max_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
	}

	RgGuiRect min_handle_rect = rg_gui_make_rect(min_handle_x, slider_rect.y, handle_w, slider_rect.h);
	RgGuiRect max_handle_rect = rg_gui_make_rect(max_handle_x, slider_rect.y, handle_w, slider_rect.h);

	int hovered_slider = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, slider_rect);
	int hovered_min = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, min_handle_rect);
	int hovered_max = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, max_handle_rect);

	if (hovered_min)
	{
		ctx->hot_id = min_id;
	}
	else if (hovered_max)
	{
		ctx->hot_id = max_id;
	}
	else if (hovered_slider)
	{
		ctx->hot_id = id;
	}
	if (enabled &&
	    (hovered_slider || hovered_min || hovered_max || ctx->active_id == min_id || ctx->active_id == max_id) &&
	    ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
	}

	if (!enabled && (ctx->active_id == min_id || ctx->active_id == max_id))
	{
		ctx->active_id = 0u;
	}

	if (enabled && (hovered_slider || hovered_min || hovered_max) && ctx->mouse_pressed)
	{
		RgGuiId pick_id = min_id;
		if (hovered_min && !hovered_max)
		{
			pick_id = min_id;
		}
		else if (hovered_max && !hovered_min)
		{
			pick_id = max_id;
		}
		else if (range > 0.0f)
		{
			f32 t = (slider_rect.w > 0.0f) ? (ctx->mouse_pos.x - slider_rect.x) / slider_rect.w : 0.0f;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			f32 target = limit_min + range * t;
			f32 dist_min = target - min;
			f32 dist_max = target - max;
			if (dist_min < 0.0f) dist_min = -dist_min;
			if (dist_max < 0.0f) dist_max = -dist_max;
			if (dist_max < dist_min)
			{
				pick_id = max_id;
			}
			else if (dist_min < dist_max)
			{
				pick_id = min_id;
			}
			else
			{
				f32 center = slider_rect.x + slider_rect.w * t_min;
				pick_id = (ctx->mouse_pos.x >= center) ? max_id : min_id;
			}
		}

		ctx->active_id = pick_id;
		ctx->focus_id = pick_id;
	}

	if (enabled && (ctx->active_id == min_id || ctx->active_id == max_id))
	{
		if (ctx->mouse_down)
		{
			f32 t = 0.0f;
			if (slider_rect.w > 0.0f)
			{
				t = (ctx->mouse_pos.x - slider_rect.x) / slider_rect.w;
			}
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			f32 new_value = limit_min + range * t;
			if (ctx->active_id == min_id)
			{
				if (new_value < limit_min) new_value = limit_min;
				if (new_value > max) new_value = max;
				if (new_value != min)
				{
					min = new_value;
					changed = 1;
				}
			}
			else
			{
				if (new_value > limit_max) new_value = limit_max;
				if (new_value < min) new_value = min;
				if (new_value != max)
				{
					max = new_value;
					changed = 1;
				}
			}
		}

		if (ctx->mouse_released)
		{
			ctx->active_id = 0u;
		}
	}

	if (enabled && ctx->active_id != min_id && ctx->focus_id == min_id && range > 0.0f)
	{
		f32 step = range * 0.01f;
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
			                       ? SDL_SCANCODE_LEFT
			                       : SDL_SCANCODE_UP;
			int repeats = rg_gui_key_repeat(ctx, min_id, key);
			if (repeats > 0)
			{
				f32 new_value = min - step * (f32)repeats;
				if (new_value < limit_min) new_value = limit_min;
				if (new_value > max) new_value = max;
				if (new_value != min)
				{
					min = new_value;
					changed = 1;
				}
			}
		}
		else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
		         rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
			                       ? SDL_SCANCODE_RIGHT
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, min_id, key);
			if (repeats > 0)
			{
				f32 new_value = min + step * (f32)repeats;
				if (new_value < limit_min) new_value = limit_min;
				if (new_value > max) new_value = max;
				if (new_value != min)
				{
					min = new_value;
					changed = 1;
				}
			}
		}
	}

	if (enabled && ctx->active_id != max_id && ctx->focus_id == max_id && range > 0.0f)
	{
		f32 step = range * 0.01f;
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
			                       ? SDL_SCANCODE_LEFT
			                       : SDL_SCANCODE_UP;
			int repeats = rg_gui_key_repeat(ctx, max_id, key);
			if (repeats > 0)
			{
				f32 new_value = max - step * (f32)repeats;
				if (new_value < min) new_value = min;
				if (new_value > limit_max) new_value = limit_max;
				if (new_value != max)
				{
					max = new_value;
					changed = 1;
				}
			}
		}
		else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
		         rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
			                       ? SDL_SCANCODE_RIGHT
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, max_id, key);
			if (repeats > 0)
			{
				f32 new_value = max + step * (f32)repeats;
				if (new_value < min) new_value = min;
				if (new_value > limit_max) new_value = limit_max;
				if (new_value != max)
				{
					max = new_value;
					changed = 1;
				}
			}
		}
	}

	rg_vec4 track_color = hovered_slider ? ctx->style.color_bg_hover : ctx->style.color_bg;
	int focused = enabled && (ctx->focus_id == min_id || ctx->focus_id == max_id);
	rg_gui_push_rect(ctx, slider_rect, track_color);
	rg_gui_push_rect_outline(ctx, slider_rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	t_min = 0.0f;
	t_max = 0.0f;
	if (range > 0.0f)
	{
		t_min = (min - limit_min) / range;
		t_max = (max - limit_min) / range;
	}
	if (t_min < 0.0f) t_min = 0.0f;
	if (t_min > 1.0f) t_min = 1.0f;
	if (t_max < 0.0f) t_max = 0.0f;
	if (t_max > 1.0f) t_max = 1.0f;

	f32 fill_start = slider_rect.x + slider_rect.w * t_min;
	f32 fill_end = slider_rect.x + slider_rect.w * t_max;
	if (fill_end < fill_start)
	{
		f32 tmp = fill_start;
		fill_start = fill_end;
		fill_end = tmp;
	}
	rg_gui_push_rect(ctx, rg_gui_make_rect(fill_start, slider_rect.y, fill_end - fill_start, slider_rect.h),
	                 ctx->style.color_accent);

	min_handle_x = slider_rect.x + slider_rect.w * t_min - handle_w * 0.5f;
	max_handle_x = slider_rect.x + slider_rect.w * t_max - handle_w * 0.5f;
	if (slider_rect.w <= handle_w)
	{
		min_handle_x = slider_rect.x;
		max_handle_x = slider_rect.x;
	}
	else
	{
		if (min_handle_x < slider_rect.x)
		{
			min_handle_x = slider_rect.x;
		}
		if (min_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			min_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
		if (max_handle_x < slider_rect.x)
		{
			max_handle_x = slider_rect.x;
		}
		if (max_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			max_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
	}

	min_handle_rect = rg_gui_make_rect(min_handle_x, slider_rect.y, handle_w, slider_rect.h);
	max_handle_rect = rg_gui_make_rect(max_handle_x, slider_rect.y, handle_w, slider_rect.h);

	if (ctx->active_id == min_id)
	{
		rg_gui_push_rect(ctx, max_handle_rect, ctx->style.color_text);
		rg_gui_push_rect(ctx, min_handle_rect, ctx->style.color_text);
	}
	else
	{
		rg_gui_push_rect(ctx, min_handle_rect, ctx->style.color_text);
		rg_gui_push_rect(ctx, max_handle_rect, ctx->style.color_text);
	}

	if (with_input)
	{
		changed |= rg_gui_value_input_float(ctx, min_value_rect, min_input_id, &min, limit_min, max);
		changed |= rg_gui_value_input_float(ctx, max_value_rect, max_input_id, &max, min, limit_max);
	}
	else
	{
		rg_gui_draw_value_text_float(ctx, min_value_rect, min_input_id, min, 1);
		rg_gui_draw_value_text_float(ctx, max_value_rect, max_input_id, max, 1);
	}

	if (min < limit_min)
	{
		min = limit_min;
		changed = 1;
	}
	if (max > limit_max)
	{
		max = limit_max;
		changed = 1;
	}
	if (min > max)
	{
		min = max;
		changed = 1;
	}

	if (min != *min_value || max != *max_value)
	{
		*min_value = min;
		*max_value = max;
	}

	return changed;
}

RGINLINE int rg_gui_slider_range_double_internal(RgGuiContext* ctx, const char* label, f64* min_value, f64* max_value,
                                                 f64 min_limit, f64 max_limit, RgGuiRect rect, RgGuiId id, int with_input)
{
	RG_GUI_ASSERT(min_value != NULL);
	RG_GUI_ASSERT(max_value != NULL);

	int enabled = !rg_gui_is_disabled(ctx);

	f64 min = *min_value;
	f64 max = *max_value;
	f64 limit_min = min_limit;
	f64 limit_max = max_limit;
	if (limit_max < limit_min)
	{
		f64 tmp = limit_min;
		limit_min = limit_max;
		limit_max = tmp;
	}

	int changed = 0;
	if (min > max)
	{
		f64 tmp = min;
		min = max;
		max = tmp;
		changed = 1;
	}

	if (min < limit_min)
	{
		min = limit_min;
		changed = 1;
	}
	if (min > limit_max)
	{
		min = limit_max;
		changed = 1;
	}
	if (max < limit_min)
	{
		max = limit_min;
		changed = 1;
	}
	if (max > limit_max)
	{
		max = limit_max;
		changed = 1;
	}
	if (min > max)
	{
		min = max;
		changed = 1;
	}

	f32 label_width = label ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;
	f32 value_w = ctx->style.value_width;

	f32 content_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 content_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f);
	if (content_w < 0.0f)
	{
		content_w = 0.0f;
	}

	f32 total_inner = inner * 2.0f;
	if (value_w * 2.0f + total_inner > content_w)
	{
		value_w = (content_w - total_inner) * 0.5f;
		if (value_w < 0.0f)
		{
			value_w = 0.0f;
		}
	}

	f32 slider_w = content_w - value_w * 2.0f - total_inner;
	if (slider_w < 0.0f)
	{
		slider_w = 0.0f;
	}

	RgGuiRect slider_rect = rg_gui_make_rect(content_x, rect.y, slider_w, rect.h);
	RgGuiRect min_value_rect = rg_gui_make_rect(slider_rect.x + slider_rect.w + inner, rect.y, value_w, rect.h);
	RgGuiRect max_value_rect = rg_gui_make_rect(min_value_rect.x + value_w + inner, rect.y, value_w, rect.h);

	RgGuiId min_id = rg_gui_id_combine(id, 1u);
	RgGuiId max_id = rg_gui_id_combine(id, 2u);
	RgGuiId min_input_id = rg_gui_id_combine(id, 3u);
	RgGuiId max_input_id = rg_gui_id_combine(id, 4u);

	rg_gui_register_focusable(ctx, min_id);
	rg_gui_register_focusable(ctx, max_id);

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	f64 range = limit_max - limit_min;
	f32 t_min = 0.0f;
	f32 t_max = 0.0f;
	if (range > 0.0)
	{
		t_min = (f32)((min - limit_min) / range);
		t_max = (f32)((max - limit_min) / range);
	}
	if (t_min < 0.0f) t_min = 0.0f;
	if (t_min > 1.0f) t_min = 1.0f;
	if (t_max < 0.0f) t_max = 0.0f;
	if (t_max > 1.0f) t_max = 1.0f;
	f32 handle_w = ctx->style.slider_handle_width;
	f32 min_handle_x = slider_rect.x + slider_rect.w * t_min - handle_w * 0.5f;
	f32 max_handle_x = slider_rect.x + slider_rect.w * t_max - handle_w * 0.5f;
	if (slider_rect.w <= handle_w)
	{
		min_handle_x = slider_rect.x;
		max_handle_x = slider_rect.x;
	}
	else
	{
		if (min_handle_x < slider_rect.x)
		{
			min_handle_x = slider_rect.x;
		}
		if (min_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			min_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
		if (max_handle_x < slider_rect.x)
		{
			max_handle_x = slider_rect.x;
		}
		if (max_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			max_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
	}

	RgGuiRect min_handle_rect = rg_gui_make_rect(min_handle_x, slider_rect.y, handle_w, slider_rect.h);
	RgGuiRect max_handle_rect = rg_gui_make_rect(max_handle_x, slider_rect.y, handle_w, slider_rect.h);

	int hovered_slider = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, slider_rect);
	int hovered_min = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, min_handle_rect);
	int hovered_max = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, max_handle_rect);

	if (hovered_min)
	{
		ctx->hot_id = min_id;
	}
	else if (hovered_max)
	{
		ctx->hot_id = max_id;
	}
	else if (hovered_slider)
	{
		ctx->hot_id = id;
	}
	if (enabled &&
	    (hovered_slider || hovered_min || hovered_max || ctx->active_id == min_id || ctx->active_id == max_id) &&
	    ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
	}

	if (!enabled && (ctx->active_id == min_id || ctx->active_id == max_id))
	{
		ctx->active_id = 0u;
	}

	if (enabled && (hovered_slider || hovered_min || hovered_max) && ctx->mouse_pressed)
	{
		RgGuiId pick_id = min_id;
		if (hovered_min && !hovered_max)
		{
			pick_id = min_id;
		}
		else if (hovered_max && !hovered_min)
		{
			pick_id = max_id;
		}
		else if (range > 0.0)
		{
			f64 t = (slider_rect.w > 0.0f) ? (f64)((ctx->mouse_pos.x - slider_rect.x) / slider_rect.w) : 0.0;
			if (t < 0.0) t = 0.0;
			if (t > 1.0) t = 1.0;
			f64 target = limit_min + range * t;
			f64 dist_min = target - min;
			f64 dist_max = target - max;
			if (dist_min < 0.0) dist_min = -dist_min;
			if (dist_max < 0.0) dist_max = -dist_max;
			if (dist_max < dist_min)
			{
				pick_id = max_id;
			}
			else if (dist_min < dist_max)
			{
				pick_id = min_id;
			}
			else
			{
				f32 center = slider_rect.x + slider_rect.w * t_min;
				pick_id = (ctx->mouse_pos.x >= center) ? max_id : min_id;
			}
		}

		ctx->active_id = pick_id;
		ctx->focus_id = pick_id;
	}

	if (enabled && (ctx->active_id == min_id || ctx->active_id == max_id))
	{
		if (ctx->mouse_down)
		{
			f32 t = 0.0f;
			if (slider_rect.w > 0.0f)
			{
				t = (ctx->mouse_pos.x - slider_rect.x) / slider_rect.w;
			}
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			f64 new_value = limit_min + range * (f64)t;
			if (ctx->active_id == min_id)
			{
				if (new_value < limit_min) new_value = limit_min;
				if (new_value > max) new_value = max;
				if (new_value != min)
				{
					min = new_value;
					changed = 1;
				}
			}
			else
			{
				if (new_value > limit_max) new_value = limit_max;
				if (new_value < min) new_value = min;
				if (new_value != max)
				{
					max = new_value;
					changed = 1;
				}
			}
		}

		if (ctx->mouse_released)
		{
			ctx->active_id = 0u;
		}
	}

	if (enabled && ctx->active_id != min_id && ctx->focus_id == min_id && range > 0.0)
	{
		f64 step = range * 0.01;
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
			                       ? SDL_SCANCODE_LEFT
			                       : SDL_SCANCODE_UP;
			int repeats = rg_gui_key_repeat(ctx, min_id, key);
			if (repeats > 0)
			{
				f64 new_value = min - step * (f64)repeats;
				if (new_value < limit_min) new_value = limit_min;
				if (new_value > max) new_value = max;
				if (new_value != min)
				{
					min = new_value;
					changed = 1;
				}
			}
		}
		else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
		         rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
			                       ? SDL_SCANCODE_RIGHT
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, min_id, key);
			if (repeats > 0)
			{
				f64 new_value = min + step * (f64)repeats;
				if (new_value < limit_min) new_value = limit_min;
				if (new_value > max) new_value = max;
				if (new_value != min)
				{
					min = new_value;
					changed = 1;
				}
			}
		}
	}

	if (enabled && ctx->active_id != max_id && ctx->focus_id == max_id && range > 0.0)
	{
		f64 step = range * 0.01;
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
			                       ? SDL_SCANCODE_LEFT
			                       : SDL_SCANCODE_UP;
			int repeats = rg_gui_key_repeat(ctx, max_id, key);
			if (repeats > 0)
			{
				f64 new_value = max - step * (f64)repeats;
				if (new_value < min) new_value = min;
				if (new_value > limit_max) new_value = limit_max;
				if (new_value != max)
				{
					max = new_value;
					changed = 1;
				}
			}
		}
		else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
		         rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
			                       ? SDL_SCANCODE_RIGHT
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, max_id, key);
			if (repeats > 0)
			{
				f64 new_value = max + step * (f64)repeats;
				if (new_value < min) new_value = min;
				if (new_value > limit_max) new_value = limit_max;
				if (new_value != max)
				{
					max = new_value;
					changed = 1;
				}
			}
		}
	}

	rg_vec4 track_color = hovered_slider ? ctx->style.color_bg_hover : ctx->style.color_bg;
	int focused = enabled && (ctx->focus_id == min_id || ctx->focus_id == max_id);
	rg_gui_push_rect(ctx, slider_rect, track_color);
	rg_gui_push_rect_outline(ctx, slider_rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	f32 fill_start = slider_rect.x + slider_rect.w * t_min;
	f32 fill_end = slider_rect.x + slider_rect.w * t_max;
	if (fill_end < fill_start)
	{
		f32 tmp = fill_start;
		fill_start = fill_end;
		fill_end = tmp;
	}
	rg_gui_push_rect(ctx, rg_gui_make_rect(fill_start, slider_rect.y, fill_end - fill_start, slider_rect.h),
	                 ctx->style.color_accent);

	min_handle_x = slider_rect.x + slider_rect.w * t_min - handle_w * 0.5f;
	max_handle_x = slider_rect.x + slider_rect.w * t_max - handle_w * 0.5f;
	if (slider_rect.w <= handle_w)
	{
		min_handle_x = slider_rect.x;
		max_handle_x = slider_rect.x;
	}
	else
	{
		if (min_handle_x < slider_rect.x)
		{
			min_handle_x = slider_rect.x;
		}
		if (min_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			min_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
		if (max_handle_x < slider_rect.x)
		{
			max_handle_x = slider_rect.x;
		}
		if (max_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			max_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
	}

	min_handle_rect = rg_gui_make_rect(min_handle_x, slider_rect.y, handle_w, slider_rect.h);
	max_handle_rect = rg_gui_make_rect(max_handle_x, slider_rect.y, handle_w, slider_rect.h);

	if (ctx->active_id == min_id)
	{
		rg_gui_push_rect(ctx, max_handle_rect, ctx->style.color_text);
		rg_gui_push_rect(ctx, min_handle_rect, ctx->style.color_text);
	}
	else
	{
		rg_gui_push_rect(ctx, min_handle_rect, ctx->style.color_text);
		rg_gui_push_rect(ctx, max_handle_rect, ctx->style.color_text);
	}

	if (with_input)
	{
		changed |= rg_gui_value_input_double(ctx, min_value_rect, min_input_id, &min, limit_min, max);
		changed |= rg_gui_value_input_double(ctx, max_value_rect, max_input_id, &max, min, limit_max);
	}
	else
	{
		rg_gui_draw_value_text_double(ctx, min_value_rect, min_input_id, min, 1);
		rg_gui_draw_value_text_double(ctx, max_value_rect, max_input_id, max, 1);
	}

	if (min < limit_min)
	{
		min = limit_min;
		changed = 1;
	}
	if (max > limit_max)
	{
		max = limit_max;
		changed = 1;
	}
	if (min > max)
	{
		min = max;
		changed = 1;
	}

	if (min != *min_value || max != *max_value)
	{
		*min_value = min;
		*max_value = max;
	}

	return changed;
}

RGINLINE int rg_gui_slider_range_int_internal(RgGuiContext* ctx, const char* label, int* min_value, int* max_value,
                                              int min_limit, int max_limit, RgGuiRect rect, RgGuiId id, int with_input)
{
	RG_GUI_ASSERT(min_value != NULL);
	RG_GUI_ASSERT(max_value != NULL);

	int enabled = !rg_gui_is_disabled(ctx);

	int min = *min_value;
	int max = *max_value;
	int limit_min = min_limit;
	int limit_max = max_limit;
	if (limit_max < limit_min)
	{
		int tmp = limit_min;
		limit_min = limit_max;
		limit_max = tmp;
	}

	int changed = 0;
	if (min > max)
	{
		int tmp = min;
		min = max;
		max = tmp;
		changed = 1;
	}

	if (min < limit_min)
	{
		min = limit_min;
		changed = 1;
	}
	if (min > limit_max)
	{
		min = limit_max;
		changed = 1;
	}
	if (max < limit_min)
	{
		max = limit_min;
		changed = 1;
	}
	if (max > limit_max)
	{
		max = limit_max;
		changed = 1;
	}
	if (min > max)
	{
		min = max;
		changed = 1;
	}

	f32 label_width = label ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;
	f32 value_w = ctx->style.value_width;

	f32 content_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 content_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f);
	if (content_w < 0.0f)
	{
		content_w = 0.0f;
	}

	f32 total_inner = inner * 2.0f;
	if (value_w * 2.0f + total_inner > content_w)
	{
		value_w = (content_w - total_inner) * 0.5f;
		if (value_w < 0.0f)
		{
			value_w = 0.0f;
		}
	}

	f32 slider_w = content_w - value_w * 2.0f - total_inner;
	if (slider_w < 0.0f)
	{
		slider_w = 0.0f;
	}

	RgGuiRect slider_rect = rg_gui_make_rect(content_x, rect.y, slider_w, rect.h);
	RgGuiRect min_value_rect = rg_gui_make_rect(slider_rect.x + slider_rect.w + inner, rect.y, value_w, rect.h);
	RgGuiRect max_value_rect = rg_gui_make_rect(min_value_rect.x + value_w + inner, rect.y, value_w, rect.h);

	RgGuiId min_id = rg_gui_id_combine(id, 1u);
	RgGuiId max_id = rg_gui_id_combine(id, 2u);
	RgGuiId min_input_id = rg_gui_id_combine(id, 3u);
	RgGuiId max_input_id = rg_gui_id_combine(id, 4u);

	rg_gui_register_focusable(ctx, min_id);
	rg_gui_register_focusable(ctx, max_id);

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	i64 range = (i64)limit_max - (i64)limit_min;
	int range_narrow = range <= (i64)INT_MAX;
	f32 range_f = range_narrow ? (f32)(int)range : (f32)range;
	f32 t_min = 0.0f;
	f32 t_max = 0.0f;
	if (range_f > 0.0f)
	{
		if (range_narrow)
		{
			t_min = (f32)(min - limit_min) / range_f;
			t_max = (f32)(max - limit_min) / range_f;
		}
		else
		{
			t_min = (f32)((i64)min - (i64)limit_min) / range_f;
			t_max = (f32)((i64)max - (i64)limit_min) / range_f;
		}
	}
	if (t_min < 0.0f) t_min = 0.0f;
	if (t_min > 1.0f) t_min = 1.0f;
	if (t_max < 0.0f) t_max = 0.0f;
	if (t_max > 1.0f) t_max = 1.0f;
	int ratio_min = min;
	int ratio_max = max;

	f32 handle_w = ctx->style.slider_handle_width;
	f32 min_handle_x = slider_rect.x + slider_rect.w * t_min - handle_w * 0.5f;
	f32 max_handle_x = slider_rect.x + slider_rect.w * t_max - handle_w * 0.5f;
	if (slider_rect.w <= handle_w)
	{
		min_handle_x = slider_rect.x;
		max_handle_x = slider_rect.x;
	}
	else
	{
		if (min_handle_x < slider_rect.x)
		{
			min_handle_x = slider_rect.x;
		}
		if (min_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			min_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
		if (max_handle_x < slider_rect.x)
		{
			max_handle_x = slider_rect.x;
		}
		if (max_handle_x + handle_w > slider_rect.x + slider_rect.w)
		{
			max_handle_x = slider_rect.x + slider_rect.w - handle_w;
		}
	}

	RgGuiRect min_handle_rect = rg_gui_make_rect(min_handle_x, slider_rect.y, handle_w, slider_rect.h);
	RgGuiRect max_handle_rect = rg_gui_make_rect(max_handle_x, slider_rect.y, handle_w, slider_rect.h);

	int hovered_slider = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, slider_rect);
	int hovered_min = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, min_handle_rect);
	int hovered_max = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, max_handle_rect);

	if (hovered_min)
	{
		ctx->hot_id = min_id;
	}
	else if (hovered_max)
	{
		ctx->hot_id = max_id;
	}
	else if (hovered_slider)
	{
		ctx->hot_id = id;
	}
	if (enabled &&
	    (hovered_slider || hovered_min || hovered_max || ctx->active_id == min_id || ctx->active_id == max_id) &&
	    ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
	}

	if (!enabled && (ctx->active_id == min_id || ctx->active_id == max_id))
	{
		ctx->active_id = 0u;
	}

	if (enabled && (hovered_slider || hovered_min || hovered_max) && ctx->mouse_pressed)
	{
		RgGuiId pick_id = min_id;
		if (hovered_min && !hovered_max)
		{
			pick_id = min_id;
		}
		else if (hovered_max && !hovered_min)
		{
			pick_id = max_id;
		}
		else if (range_f > 0.0f)
		{
			f32 t = (slider_rect.w > 0.0f) ? (ctx->mouse_pos.x - slider_rect.x) / slider_rect.w : 0.0f;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			f64 target = (f64)limit_min + (f64)range * (f64)t;
			f64 dist_min = target - (f64)min;
			f64 dist_max = target - (f64)max;
			if (dist_min < 0.0f) dist_min = -dist_min;
			if (dist_max < 0.0f) dist_max = -dist_max;
			if (dist_max < dist_min)
			{
				pick_id = max_id;
			}
			else if (dist_min < dist_max)
			{
				pick_id = min_id;
			}
			else
			{
				f32 center = slider_rect.x + slider_rect.w * t_min;
				pick_id = (ctx->mouse_pos.x >= center) ? max_id : min_id;
			}
		}

		ctx->active_id = pick_id;
		ctx->focus_id = pick_id;
	}

	if (enabled && (ctx->active_id == min_id || ctx->active_id == max_id))
	{
		if (ctx->mouse_down)
		{
			f32 t = 0.0f;
			if (slider_rect.w > 0.0f)
			{
				t = (ctx->mouse_pos.x - slider_rect.x) / slider_rect.w;
			}
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			f64 new_value_f = (f64)limit_min + (f64)range * (f64)t;
			int new_value = ctx->active_id == min_id ? min : max;
			(void)rg_gui_round_f64_to_int_saturated(new_value_f, &new_value);
			if (ctx->active_id == min_id)
			{
				if (new_value < limit_min) new_value = limit_min;
				if (new_value > max) new_value = max;
				if (new_value != min)
				{
					min = new_value;
					changed = 1;
				}
			}
			else
			{
				if (new_value > limit_max) new_value = limit_max;
				if (new_value < min) new_value = min;
				if (new_value != max)
				{
					max = new_value;
					changed = 1;
				}
			}
		}

		if (ctx->mouse_released)
		{
			ctx->active_id = 0u;
		}
	}

	if (enabled && ctx->active_id != min_id && ctx->focus_id == min_id && range != 0)
	{
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
			                       ? SDL_SCANCODE_LEFT
			                       : SDL_SCANCODE_UP;
			int repeats = rg_gui_key_repeat(ctx, min_id, key);
			if (repeats > 0)
			{
				int step = (int)(range / 100);
				if (step < 1) step = 1;
				int new_value = rg_gui_int_step_clamped(min, (i64)step, repeats, -1,
				                                        limit_min, max);
				if (new_value != min)
				{
					min = new_value;
					changed = 1;
				}
			}
		}
		else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
		         rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
			                       ? SDL_SCANCODE_RIGHT
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, min_id, key);
			if (repeats > 0)
			{
				int step = (int)(range / 100);
				if (step < 1) step = 1;
				int new_value = rg_gui_int_step_clamped(min, (i64)step, repeats, 1,
				                                        limit_min, max);
				if (new_value != min)
				{
					min = new_value;
					changed = 1;
				}
			}
		}
	}

	if (enabled && ctx->active_id != max_id && ctx->focus_id == max_id && range != 0)
	{
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
			                       ? SDL_SCANCODE_LEFT
			                       : SDL_SCANCODE_UP;
			int repeats = rg_gui_key_repeat(ctx, max_id, key);
			if (repeats > 0)
			{
				int step = (int)(range / 100);
				if (step < 1) step = 1;
				int new_value = rg_gui_int_step_clamped(max, (i64)step, repeats, -1,
				                                        min, limit_max);
				if (new_value != max)
				{
					max = new_value;
					changed = 1;
				}
			}
		}
		else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
		         rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
			                       ? SDL_SCANCODE_RIGHT
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, max_id, key);
			if (repeats > 0)
			{
				int step = (int)(range / 100);
				if (step < 1) step = 1;
				int new_value = rg_gui_int_step_clamped(max, (i64)step, repeats, 1,
				                                        min, limit_max);
				if (new_value != max)
				{
					max = new_value;
					changed = 1;
				}
			}
		}
	}

	rg_vec4 track_color = hovered_slider ? ctx->style.color_bg_hover : ctx->style.color_bg;
	int focused = enabled && (ctx->focus_id == min_id || ctx->focus_id == max_id);
	rg_gui_push_rect(ctx, slider_rect, track_color);
	rg_gui_push_rect_outline(ctx, slider_rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	if (min != ratio_min || max != ratio_max)
	{
		t_min = 0.0f;
		t_max = 0.0f;
		if (range_f > 0.0f)
		{
			if (range_narrow)
			{
				t_min = (f32)(min - limit_min) / range_f;
				t_max = (f32)(max - limit_min) / range_f;
			}
			else
			{
				t_min = (f32)((i64)min - (i64)limit_min) / range_f;
				t_max = (f32)((i64)max - (i64)limit_min) / range_f;
			}
		}
		if (t_min < 0.0f) t_min = 0.0f;
		if (t_min > 1.0f) t_min = 1.0f;
		if (t_max < 0.0f) t_max = 0.0f;
		if (t_max > 1.0f) t_max = 1.0f;

		min_handle_x = slider_rect.x + slider_rect.w * t_min - handle_w * 0.5f;
		max_handle_x = slider_rect.x + slider_rect.w * t_max - handle_w * 0.5f;
		if (slider_rect.w <= handle_w)
		{
			min_handle_x = slider_rect.x;
			max_handle_x = slider_rect.x;
		}
		else
		{
			if (min_handle_x < slider_rect.x)
			{
				min_handle_x = slider_rect.x;
			}
			if (min_handle_x + handle_w > slider_rect.x + slider_rect.w)
			{
				min_handle_x = slider_rect.x + slider_rect.w - handle_w;
			}
			if (max_handle_x < slider_rect.x)
			{
				max_handle_x = slider_rect.x;
			}
			if (max_handle_x + handle_w > slider_rect.x + slider_rect.w)
			{
				max_handle_x = slider_rect.x + slider_rect.w - handle_w;
			}
		}

		min_handle_rect = rg_gui_make_rect(min_handle_x, slider_rect.y, handle_w, slider_rect.h);
		max_handle_rect = rg_gui_make_rect(max_handle_x, slider_rect.y, handle_w, slider_rect.h);
	}

	f32 fill_start = slider_rect.x + slider_rect.w * t_min;
	f32 fill_end = slider_rect.x + slider_rect.w * t_max;
	if (fill_end < fill_start)
	{
		f32 tmp = fill_start;
		fill_start = fill_end;
		fill_end = tmp;
	}
	rg_gui_push_rect(ctx, rg_gui_make_rect(fill_start, slider_rect.y, fill_end - fill_start, slider_rect.h),
	                 ctx->style.color_accent);

	if (ctx->active_id == min_id)
	{
		rg_gui_push_rect(ctx, max_handle_rect, ctx->style.color_text);
		rg_gui_push_rect(ctx, min_handle_rect, ctx->style.color_text);
	}
	else
	{
		rg_gui_push_rect(ctx, min_handle_rect, ctx->style.color_text);
		rg_gui_push_rect(ctx, max_handle_rect, ctx->style.color_text);
	}

	if (with_input)
	{
		changed |= rg_gui_value_input_int(ctx, min_value_rect, min_input_id, &min, limit_min, max);
		changed |= rg_gui_value_input_int(ctx, max_value_rect, max_input_id, &max, min, limit_max);
	}
	else
	{
		rg_gui_draw_value_text_int(ctx, min_value_rect, min_input_id, min, 1);
		rg_gui_draw_value_text_int(ctx, max_value_rect, max_input_id, max, 1);
	}

	if (min < limit_min)
	{
		min = limit_min;
		changed = 1;
	}
	if (max > limit_max)
	{
		max = limit_max;
		changed = 1;
	}
	if (min > max)
	{
		min = max;
		changed = 1;
	}

	if (min != *min_value || max != *max_value)
	{
		*min_value = min;
		*max_value = max;
	}

	return changed;
}

RGINLINE int rg_gui_slider_range_float(RgGuiContext* ctx, const char* label, f32* min_value, f32* max_value,
                                       f32 min_limit, f32 max_limit, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_range_float_internal(ctx, label, min_value, max_value, min_limit, max_limit, rect, id, 0);
}

RGINLINE int rg_gui_slider_range_float_input(RgGuiContext* ctx, const char* label, f32* min_value, f32* max_value,
                                             f32 min_limit, f32 max_limit, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_range_float_internal(ctx, label, min_value, max_value, min_limit, max_limit, rect, id, 1);
}

RGINLINE int rg_gui_slider_range_int(RgGuiContext* ctx, const char* label, int* min_value, int* max_value,
                                     int min_limit, int max_limit, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_range_int_internal(ctx, label, min_value, max_value, min_limit, max_limit, rect, id, 0);
}

RGINLINE int rg_gui_slider_range_int_input(RgGuiContext* ctx, const char* label, int* min_value, int* max_value,
                                           int min_limit, int max_limit, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_range_int_internal(ctx, label, min_value, max_value, min_limit, max_limit, rect, id, 1);
}

RGINLINE int rg_gui_slider_range_double(RgGuiContext* ctx, const char* label, f64* min_value, f64* max_value,
                                        f64 min_limit, f64 max_limit, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_range_double_internal(ctx, label, min_value, max_value, min_limit, max_limit, rect, id, 0);
}

RGINLINE int rg_gui_slider_range_double_input(RgGuiContext* ctx, const char* label, f64* min_value, f64* max_value,
                                              f64 min_limit, f64 max_limit, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_range_double_internal(ctx, label, min_value, max_value, min_limit, max_limit, rect, id, 1);
}

RGINLINE int rg_gui_slider_int_internal(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value,
                                        RgGuiRect rect, RgGuiId id, int with_input)
{
	RG_GUI_ASSERT(value != NULL);

	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);

	f32 label_width = (label != NULL) ? ctx->style.label_width : 0.0f;
	f32 value_width = ctx->style.value_width;
	f32 inner = ctx->style.inner_spacing;

	f32 slider_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 slider_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f) - value_width - (value_width > 0.0f ? inner : 0.0f);

	if (slider_w < 0.0f)
	{
		slider_w = 0.0f;
	}

	RgGuiRect slider_rect = rg_gui_make_rect(slider_x, rect.y, slider_w, rect.h);
	RgGuiRect value_rect = rg_gui_make_rect(rect.x + rect.w - value_width, rect.y, value_width, rect.h);

	RgGuiId input_id = 0u;
	if (with_input)
	{
		input_id = rg_gui_id_combine(id, 1u);
	}

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, slider_rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}
	if (enabled && (hovered || ctx->active_id == id) && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);
	int changed = 0;
	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}
	if (enabled && ctx->active_id == id)
	{
		if (ctx->mouse_down)
		{
			f32 t = 0.0f;
			if (slider_rect.w > 0.0f)
			{
				t = (ctx->mouse_pos.x - slider_rect.x) / slider_rect.w;
			}
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			f64 range = (f64)max_value - (f64)min_value;
			f64 new_value_f = (f64)min_value + range * (f64)t;
			int new_value = *value;
			(void)rg_gui_round_f64_to_int_saturated(new_value_f, &new_value);
			if (new_value != *value)
			{
				*value = new_value;
				changed = 1;
			}
		}
		if (ctx->mouse_released)
		{
			ctx->active_id = 0u;
		}
	}

	if (focused && ctx->active_id != id)
	{
		i64 range = (i64)max_value - (i64)min_value;
		if (range < 0)
		{
			range = -range;
		}
		if (range > 0)
		{
			int step = (int)(range / 100);
			if (step < 1)
			{
				step = 1;
			}
			if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
			    rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
			{
				SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
				                       ? SDL_SCANCODE_LEFT
				                       : SDL_SCANCODE_UP;
				int repeats = rg_gui_key_repeat(ctx, id, key);
				if (repeats > 0)
				{
					int new_value = rg_gui_int_step_clamped(*value, (i64)step, repeats,
					                                        -1, min_value, max_value);
					if (new_value != *value)
					{
						*value = new_value;
						changed = 1;
					}
				}
			}
			else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
			         rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
			{
				SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
				                       ? SDL_SCANCODE_RIGHT
				                       : SDL_SCANCODE_DOWN;
				int repeats = rg_gui_key_repeat(ctx, id, key);
				if (repeats > 0)
				{
					int new_value = rg_gui_int_step_clamped(*value, (i64)step, repeats,
					                                        1, min_value, max_value);
					if (new_value != *value)
					{
						*value = new_value;
						changed = 1;
					}
				}
			}
		}
	}

	rg_vec4 track_color = hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
	rg_gui_push_rect(ctx, slider_rect, track_color);
	rg_gui_push_rect_outline(ctx, slider_rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	f32 t = 0.0f;
	if (max_value > min_value)
	{
		i64 denominator = (i64)max_value - (i64)min_value;
		if (denominator <= (i64)INT_MAX && *value >= min_value && *value <= max_value)
		{
			int numerator = *value - min_value;
			int denominator_int = max_value - min_value;
			t = (f32)((f64)numerator / (f64)denominator_int);
		}
		else
		{
			i64 numerator = (i64)*value - (i64)min_value;
			t = (f32)((f64)numerator / (f64)denominator);
		}
	}
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	f32 fill_w = slider_rect.w * t;
	rg_gui_push_rect(ctx, rg_gui_make_rect(slider_rect.x, slider_rect.y, fill_w, slider_rect.h), ctx->style.color_accent);

	f32 handle_x = slider_rect.x + fill_w - ctx->style.slider_handle_width * 0.5f;
	if (slider_rect.w <= ctx->style.slider_handle_width)
	{
		handle_x = slider_rect.x;
	}
	else
	{
		if (handle_x < slider_rect.x)
		{
			handle_x = slider_rect.x;
		}
		if (handle_x + ctx->style.slider_handle_width > slider_rect.x + slider_rect.w)
		{
			handle_x = slider_rect.x + slider_rect.w - ctx->style.slider_handle_width;
		}
	}
	rg_gui_push_rect(ctx, rg_gui_make_rect(handle_x, slider_rect.y, ctx->style.slider_handle_width, slider_rect.h), ctx->style.color_text);

	if (with_input)
	{
		changed |= rg_gui_value_input_int(ctx, value_rect, input_id, value, min_value, max_value);
	}
	else
	{
		rg_gui_draw_value_text_int(ctx, value_rect, id, *value, 1);
	}

	return changed;
}

RGINLINE int rg_gui_slider_int(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_int_internal(ctx, label, value, min_value, max_value, rect, id, 0);
}

RGINLINE int rg_gui_slider_int_input(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_int_internal(ctx, label, value, min_value, max_value, rect, id, 1);
}

RGINLINE int rg_gui_slider_float_internal(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value,
                                          RgGuiRect rect, RgGuiId id, int with_input)
{
	RG_GUI_ASSERT(value != NULL);

	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);

	f32 label_width = (label != NULL) ? ctx->style.label_width : 0.0f;
	f32 value_width = ctx->style.value_width;
	f32 inner = ctx->style.inner_spacing;

	f32 slider_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 slider_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f) - value_width - (value_width > 0.0f ? inner : 0.0f);

	if (slider_w < 0.0f)
	{
		slider_w = 0.0f;
	}

	RgGuiRect slider_rect = rg_gui_make_rect(slider_x, rect.y, slider_w, rect.h);
	RgGuiRect value_rect = rg_gui_make_rect(rect.x + rect.w - value_width, rect.y, value_width, rect.h);

	RgGuiId input_id = 0u;
	if (with_input)
	{
		input_id = rg_gui_id_combine(id, 1u);
	}

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, slider_rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}
	if (enabled && (hovered || ctx->active_id == id) && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);
	int changed = 0;
	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}
	if (enabled && ctx->active_id == id)
	{
		if (ctx->mouse_down)
		{
			f32 t = 0.0f;
			if (slider_rect.w > 0.0f)
			{
				t = (ctx->mouse_pos.x - slider_rect.x) / slider_rect.w;
			}
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			f32 new_value = min_value + (max_value - min_value) * t;
			if (new_value != *value)
			{
				*value = new_value;
				changed = 1;
			}
		}
		if (ctx->mouse_released)
		{
			ctx->active_id = 0u;
		}
	}

	if (focused && ctx->active_id != id)
	{
		f32 range = max_value - min_value;
		if (range < 0.0f)
		{
			range = -range;
		}
		if (range > 0.0f)
		{
			f32 step = range * 0.01f;
			if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
			    rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
			{
				SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
				                       ? SDL_SCANCODE_LEFT
				                       : SDL_SCANCODE_UP;
				int repeats = rg_gui_key_repeat(ctx, id, key);
				if (repeats > 0)
				{
					f32 new_value = *value - step * (f32)repeats;
					if (new_value < min_value) new_value = min_value;
					if (new_value > max_value) new_value = max_value;
					if (new_value != *value)
					{
						*value = new_value;
						changed = 1;
					}
				}
			}
			else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
			         rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
			{
				SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
				                       ? SDL_SCANCODE_RIGHT
				                       : SDL_SCANCODE_DOWN;
				int repeats = rg_gui_key_repeat(ctx, id, key);
				if (repeats > 0)
				{
					f32 new_value = *value + step * (f32)repeats;
					if (new_value < min_value) new_value = min_value;
					if (new_value > max_value) new_value = max_value;
					if (new_value != *value)
					{
						*value = new_value;
						changed = 1;
					}
				}
			}
		}
	}

	rg_vec4 track_color = hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
	rg_gui_push_rect(ctx, slider_rect, track_color);
	rg_gui_push_rect_outline(ctx, slider_rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	f32 t = 0.0f;
	if (max_value > min_value)
	{
		t = (*value - min_value) / (max_value - min_value);
	}
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	f32 fill_w = slider_rect.w * t;
	rg_gui_push_rect(ctx, rg_gui_make_rect(slider_rect.x, slider_rect.y, fill_w, slider_rect.h), ctx->style.color_accent);

	f32 handle_x = slider_rect.x + fill_w - ctx->style.slider_handle_width * 0.5f;
	if (slider_rect.w <= ctx->style.slider_handle_width)
	{
		handle_x = slider_rect.x;
	}
	else
	{
		if (handle_x < slider_rect.x)
		{
			handle_x = slider_rect.x;
		}
		if (handle_x + ctx->style.slider_handle_width > slider_rect.x + slider_rect.w)
		{
			handle_x = slider_rect.x + slider_rect.w - ctx->style.slider_handle_width;
		}
	}
	rg_gui_push_rect(ctx, rg_gui_make_rect(handle_x, slider_rect.y, ctx->style.slider_handle_width, slider_rect.h), ctx->style.color_text);

	if (with_input)
	{
		rg_gui_register_focusable(ctx, input_id);

		int input_enabled = enabled;
		if (!input_enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
		{
			ctx->text_edit_state->active = 0;
		}

		int submit = 0;
		int input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
		int other_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id != input_id);
		int value_hovered = input_enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, value_rect);
		int want_activate = input_enabled && (!input_active && value_hovered && ctx->mouse_pressed);
		int want_tab_focus = input_enabled && (ctx->tab_focus_id == input_id);
		if (want_tab_focus)
		{
			want_activate = 1;
		}
		int use_number_buffer = input_enabled && (input_active || want_activate || !other_active);
		const char* cached_value_text = NULL;
		if (!input_active)
		{
			cached_value_text = rg_gui_cache_value_text(ctx, input_id, *value, ctx->style.value_decimals);
		}

		if (use_number_buffer)
		{
			if (!input_active && !want_activate)
			{
				const char* display_text = cached_value_text;
				char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
				int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
				if (!display_text)
				{
					rg_gui_format_float(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
					display_text = display_buffer;
					copy_display = 1;
				}

				if (value_hovered)
				{
					ctx->hot_id = input_id;
				}

				rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
				rg_gui_push_rect(ctx, value_rect, bg);
				rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

				rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
				                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
				rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
			}
			else
			{
				if (!input_active)
				{
					const char* cached_text = cached_value_text;
					if (!cached_text)
					{
						rg_gui_format_float(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), *value, ctx->style.value_decimals);
						ctx->number_edit_state->cached_id = input_id;
						ctx->number_edit_state->cached_value = *value;
						ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
					}
					else if (ctx->number_edit_state->cached_id != input_id ||
					         ctx->number_edit_state->cached_value != *value ||
					         ctx->number_edit_state->cached_decimals != ctx->style.value_decimals)
					{
						rg_gui_copy_value_buffer(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), cached_text);
						ctx->number_edit_state->cached_id = input_id;
						ctx->number_edit_state->cached_value = *value;
						ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
					}
				}
				else
				{
					ctx->number_edit_state->cached_id = 0u;
				}

				rg_gui_text_input_ex_internal(ctx, NULL, ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
				                              value_rect, input_id,
				                              RG_GUI_TEXT_INPUT_NO_LABEL | RG_GUI_TEXT_INPUT_UNDO_VALUE, &submit);
			}
		}
		else
		{
			const char* display_text = cached_value_text;
			char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
			int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
			if (!display_text)
			{
				rg_gui_format_float(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
				display_text = display_buffer;
				copy_display = 1;
			}

			if (value_hovered)
			{
				ctx->hot_id = input_id;
			}

			rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
			rg_gui_push_rect(ctx, value_rect, bg);
			rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

			rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
			                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
			rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
		}

		input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
		if (use_number_buffer)
		{
			if (input_active && ctx->number_edit_state->id != input_id)
			{
				ctx->number_edit_state->id = input_id;
			}

			if (ctx->number_edit_state->id == input_id)
			{
				if (submit || (!input_active && ctx->focus_id != input_id))
				{
					char* end_ptr = NULL;
					f32 parsed = strtof(ctx->number_edit_state->buffer, &end_ptr);
					if (end_ptr != ctx->number_edit_state->buffer)
					{
						if (parsed < min_value) parsed = min_value;
						if (parsed > max_value) parsed = max_value;
						if (parsed != *value)
						{
							*value = parsed;
							changed = 1;
						}
					}
					ctx->number_edit_state->id = 0u;
					ctx->number_edit_state->cached_id = 0u;
				}
			}
		}
	}
	else
	{
		const char* value_text = rg_gui_cache_value_text(ctx, id, *value, ctx->style.value_decimals);
		char value_fallback[RG_GUI_NUMBER_BUFFER_SIZE];
		int copy_value = RG_GUI_COPY_DYNAMIC_TEXT;
		if (!value_text)
		{
			rg_gui_format_float(value_fallback, sizeof(value_fallback), *value, ctx->style.value_decimals);
			value_text = value_fallback;
			copy_value = 1;
		}
		rg_vec2 value_pos = rg_vec2(value_rect.x + ctx->style.padding,
		                            value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, value_text, value_pos, ctx->style.color_text_dim, copy_value);
	}

	return changed;
}

RGINLINE int rg_gui_slider_float(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_float_internal(ctx, label, value, min_value, max_value, rect, id, 0);
}

RGINLINE int rg_gui_slider_float_input(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_float_internal(ctx, label, value, min_value, max_value, rect, id, 1);
}

RGINLINE int rg_gui_slider_double_internal(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value,
                                           RgGuiRect rect, RgGuiId id, int with_input)
{
	RG_GUI_ASSERT(value != NULL);

	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);

	f32 label_width = (label != NULL) ? ctx->style.label_width : 0.0f;
	f32 value_width = ctx->style.value_width;
	f32 inner = ctx->style.inner_spacing;

	f32 slider_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 slider_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f) - value_width - (value_width > 0.0f ? inner : 0.0f);

	if (slider_w < 0.0f)
	{
		slider_w = 0.0f;
	}

	RgGuiRect slider_rect = rg_gui_make_rect(slider_x, rect.y, slider_w, rect.h);
	RgGuiRect value_rect = rg_gui_make_rect(rect.x + rect.w - value_width, rect.y, value_width, rect.h);

	RgGuiId input_id = 0u;
	if (with_input)
	{
		input_id = rg_gui_id_combine(id, 1u);
	}

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, slider_rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}
	if (enabled && (hovered || ctx->active_id == id) && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);
	int changed = 0;
	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}
	if (enabled && ctx->active_id == id)
	{
		if (ctx->mouse_down)
		{
			f32 t = 0.0f;
			if (slider_rect.w > 0.0f)
			{
				t = (ctx->mouse_pos.x - slider_rect.x) / slider_rect.w;
			}
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			f64 new_value = min_value + (max_value - min_value) * (f64)t;
			if (new_value != *value)
			{
				*value = new_value;
				changed = 1;
			}
		}
		if (ctx->mouse_released)
		{
			ctx->active_id = 0u;
		}
	}

	if (focused && ctx->active_id != id)
	{
		f64 range = max_value - min_value;
		if (range < 0.0)
		{
			range = -range;
		}
		if (range > 0.0)
		{
			f64 step = range * 0.01;
			if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
			    rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
			{
				SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
				                       ? SDL_SCANCODE_LEFT
				                       : SDL_SCANCODE_UP;
				int repeats = rg_gui_key_repeat(ctx, id, key);
				if (repeats > 0)
				{
					f64 new_value = *value - step * (f64)repeats;
					if (new_value < min_value) new_value = min_value;
					if (new_value > max_value) new_value = max_value;
					if (new_value != *value)
					{
						*value = new_value;
						changed = 1;
					}
				}
			}
			else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
			         rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
			{
				SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
				                       ? SDL_SCANCODE_RIGHT
				                       : SDL_SCANCODE_DOWN;
				int repeats = rg_gui_key_repeat(ctx, id, key);
				if (repeats > 0)
				{
					f64 new_value = *value + step * (f64)repeats;
					if (new_value < min_value) new_value = min_value;
					if (new_value > max_value) new_value = max_value;
					if (new_value != *value)
					{
						*value = new_value;
						changed = 1;
					}
				}
			}
		}
	}

	rg_vec4 track_color = hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
	rg_gui_push_rect(ctx, slider_rect, track_color);
	rg_gui_push_rect_outline(ctx, slider_rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	f32 t = 0.0f;
	if (max_value > min_value)
	{
		t = (f32)((*value - min_value) / (max_value - min_value));
	}
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	f32 fill_w = slider_rect.w * t;
	rg_gui_push_rect(ctx, rg_gui_make_rect(slider_rect.x, slider_rect.y, fill_w, slider_rect.h), ctx->style.color_accent);

	f32 handle_x = slider_rect.x + fill_w - ctx->style.slider_handle_width * 0.5f;
	if (slider_rect.w <= ctx->style.slider_handle_width)
	{
		handle_x = slider_rect.x;
	}
	else
	{
		if (handle_x < slider_rect.x)
		{
			handle_x = slider_rect.x;
		}
		if (handle_x + ctx->style.slider_handle_width > slider_rect.x + slider_rect.w)
		{
			handle_x = slider_rect.x + slider_rect.w - ctx->style.slider_handle_width;
		}
	}
	rg_gui_push_rect(ctx, rg_gui_make_rect(handle_x, slider_rect.y, ctx->style.slider_handle_width, slider_rect.h), ctx->style.color_text);

	if (with_input)
	{
		changed |= rg_gui_value_input_double(ctx, value_rect, input_id, value, min_value, max_value);
	}
	else
	{
		rg_gui_draw_value_text_double(ctx, value_rect, id, *value, 1);
	}

	return changed;
}

RGINLINE int rg_gui_slider_double(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_double_internal(ctx, label, value, min_value, max_value, rect, id, 0);
}

RGINLINE int rg_gui_slider_double_input(RgGuiContext* ctx, const char* label, f64* value, f64 min_value, f64 max_value, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_slider_double_internal(ctx, label, value, min_value, max_value, rect, id, 1);
}

RGINLINE int rg_gui_stepper_button(RgGuiContext* ctx, RgGuiRect rect, const char* label, RgGuiId id, RgGuiId focus_id)
{
	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->active_id = id;
		ctx->focus_id = focus_id;
	}

	int pressed = 0;
	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}
	if (enabled && ctx->active_id == id)
	{
		if (ctx->mouse_released)
		{
			if (hovered)
			{
				pressed = 1;
			}
			ctx->active_id = 0u;
		}
	}

	rg_vec4 bg = ctx->style.color_bg;
	if (enabled && ctx->active_id == id)
	{
		bg = ctx->style.color_bg_active;
	}
	else if (enabled && hovered)
	{
		bg = ctx->style.color_bg_hover;
	}

	rg_gui_push_rect(ctx, rect, bg);
	rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

	if (label)
	{
		f32 text_w = ctx->style.char_width;
		rg_vec2 pos = rg_vec2(rect.x + (rect.w - text_w) * 0.5f,
		                      rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_static(ctx, label, pos, ctx->style.color_text);
	}

	return pressed;
}

RGINLINE int rg_gui_stepper_float_internal(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value,
                                           f32 step, RgGuiRect rect, RgGuiId id, int copy_label)
{
	RG_GUI_ASSERT(value != NULL);

	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);

	f32 min = min_value;
	f32 max = max_value;
	if (max < min)
	{
		f32 tmp = min;
		min = max;
		max = tmp;
	}

	f32 use_step = step;
	if (use_step < 0.0f)
	{
		use_step = -use_step;
	}
	if (use_step == 0.0f)
	{
		f32 range = max - min;
		if (range < 0.0f)
		{
			range = -range;
		}
		use_step = range * 0.01f;
		if (use_step == 0.0f)
		{
			use_step = 1.0f;
		}
	}

	f32 label_width = label ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;
	f32 button_w = rect.h;
	f32 value_w = ctx->style.value_width;

	f32 content_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 content_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f);
	f32 button_span = button_w * 2.0f + inner;

	if (value_w + button_span > content_w)
	{
		value_w = content_w - button_span;
	}
	if (value_w < 0.0f)
	{
		value_w = 0.0f;
	}

	RgGuiRect value_rect = rg_gui_make_rect(content_x, rect.y, value_w, rect.h);
	RgGuiRect minus_rect = rg_gui_make_rect(value_rect.x + value_rect.w + inner, rect.y, button_w, rect.h);
	RgGuiRect plus_rect = rg_gui_make_rect(minus_rect.x + button_w + inner, rect.y, button_w, rect.h);

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (copy_label)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	RgGuiId input_id = rg_gui_id_combine(id, 1u);
	RgGuiId minus_id = rg_gui_id_combine(id, 2u);
	RgGuiId plus_id = rg_gui_id_combine(id, 3u);

	rg_gui_register_focusable(ctx, input_id);

	int input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int changed = 0;
	int stepped = 0;

	if (enabled)
	{
		if (rg_gui_stepper_button(ctx, minus_rect, "-", minus_id, id))
		{
			f32 new_value = *value - use_step;
			if (new_value < min) new_value = min;
			if (new_value > max) new_value = max;
			if (new_value != *value)
			{
				*value = new_value;
				changed = 1;
			}
			stepped = 1;
		}

		if (rg_gui_stepper_button(ctx, plus_rect, "+", plus_id, id))
		{
			f32 new_value = *value + use_step;
			if (new_value < min) new_value = min;
			if (new_value > max) new_value = max;
			if (new_value != *value)
			{
				*value = new_value;
				changed = 1;
			}
			stepped = 1;
		}
	}

	if (stepped && input_active)
	{
		ctx->text_edit_state->active = 0;
		ctx->number_edit_state->id = 0u;
		ctx->number_edit_state->cached_id = 0u;
		input_active = 0;
	}

	if (enabled && !input_active && (ctx->focus_id == id || ctx->focus_id == input_id))
	{
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
			                       ? SDL_SCANCODE_LEFT
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, id, key);
			if (repeats > 0)
			{
				f32 new_value = *value - use_step * (f32)repeats;
				if (new_value < min) new_value = min;
				if (new_value > max) new_value = max;
				if (new_value != *value)
				{
					*value = new_value;
					changed = 1;
				}
			}
		}
		else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
		         rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
			                       ? SDL_SCANCODE_RIGHT
			                       : SDL_SCANCODE_UP;
			int repeats = rg_gui_key_repeat(ctx, id, key);
			if (repeats > 0)
			{
				f32 new_value = *value + use_step * (f32)repeats;
				if (new_value < min) new_value = min;
				if (new_value > max) new_value = max;
				if (new_value != *value)
				{
					*value = new_value;
					changed = 1;
				}
			}
		}
	}

	int submit = 0;
	int input_enabled = enabled;
	if (!input_enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int other_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id != input_id);
	int value_hovered = input_enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, value_rect);
	int want_activate = input_enabled && (!input_active && value_hovered && ctx->mouse_pressed);
	int want_tab_focus = input_enabled && (ctx->tab_focus_id == input_id);
	if (want_tab_focus)
	{
		want_activate = 1;
	}
	int use_number_buffer = input_enabled && (input_active || want_activate || !other_active);
	const char* cached_value_text = NULL;
	if (!input_active)
	{
		cached_value_text = rg_gui_cache_value_text(ctx, input_id, *value, ctx->style.value_decimals);
	}

	if (use_number_buffer)
	{
		if (!input_active && !want_activate)
		{
			const char* display_text = cached_value_text;
			char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
			int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
			if (!display_text)
			{
				rg_gui_format_float(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
				display_text = display_buffer;
				copy_display = 1;
			}

			if (value_hovered)
			{
				ctx->hot_id = input_id;
			}

			rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
			rg_gui_push_rect(ctx, value_rect, bg);
			rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

			rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
			                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
			rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
		}
		else
		{
			if (!input_active)
			{
				const char* cached_text = cached_value_text;
				if (!cached_text)
				{
					rg_gui_format_float(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), *value, ctx->style.value_decimals);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
				else if (ctx->number_edit_state->cached_id != input_id ||
				         ctx->number_edit_state->cached_value != *value ||
				         ctx->number_edit_state->cached_decimals != ctx->style.value_decimals)
				{
					rg_gui_copy_value_buffer(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), cached_text);
					ctx->number_edit_state->cached_id = input_id;
					ctx->number_edit_state->cached_value = *value;
					ctx->number_edit_state->cached_decimals = ctx->style.value_decimals;
				}
			}
			else
			{
				ctx->number_edit_state->cached_id = 0u;
			}

			rg_gui_text_input_ex_internal(ctx, NULL, ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                              value_rect, input_id,
			                              RG_GUI_TEXT_INPUT_NO_LABEL | RG_GUI_TEXT_INPUT_UNDO_VALUE, &submit);
		}
	}
	else
	{
		const char* display_text = cached_value_text;
		char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
		int copy_display = RG_GUI_COPY_DYNAMIC_TEXT;
		if (!display_text)
		{
			rg_gui_format_float(display_buffer, sizeof(display_buffer), *value, ctx->style.value_decimals);
			display_text = display_buffer;
			copy_display = 1;
		}

		if (value_hovered)
		{
			ctx->hot_id = input_id;
		}

		rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, value_rect, bg);
		rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
		                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, display_text, text_pos, ctx->style.color_text, copy_display);
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	if (use_number_buffer)
	{
		if (input_active && ctx->number_edit_state->id != input_id)
		{
			ctx->number_edit_state->id = input_id;
		}

		if (ctx->number_edit_state->id == input_id)
		{
			if (submit || (!input_active && ctx->focus_id != input_id))
			{
				char* end_ptr = NULL;
				f32 parsed = strtof(ctx->number_edit_state->buffer, &end_ptr);
				if (end_ptr != ctx->number_edit_state->buffer)
				{
					if (parsed < min) parsed = min;
					if (parsed > max) parsed = max;
					if (parsed != *value)
					{
						*value = parsed;
						changed = 1;
					}
				}
				ctx->number_edit_state->id = 0u;
				ctx->number_edit_state->cached_id = 0u;
			}
		}
	}

	return changed;
}

RGINLINE int rg_gui_stepper_int_internal(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value,
                                         int step, RgGuiRect rect, RgGuiId id, int copy_label)
{
	RG_GUI_ASSERT(value != NULL);

	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);

	int min = min_value;
	int max = max_value;
	if (max < min)
	{
		int tmp = min;
		min = max;
		max = tmp;
	}

	i64 use_step = step == 0 ? 1 : (i64)step;

	f32 label_width = label ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;
	f32 button_w = rect.h;
	f32 value_w = ctx->style.value_width;

	f32 content_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 content_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f);
	f32 button_span = button_w * 2.0f + inner;

	if (value_w + button_span > content_w)
	{
		value_w = content_w - button_span;
	}
	if (value_w < 0.0f)
	{
		value_w = 0.0f;
	}

	RgGuiRect value_rect = rg_gui_make_rect(content_x, rect.y, value_w, rect.h);
	RgGuiRect minus_rect = rg_gui_make_rect(value_rect.x + value_rect.w + inner, rect.y, button_w, rect.h);
	RgGuiRect plus_rect = rg_gui_make_rect(minus_rect.x + button_w + inner, rect.y, button_w, rect.h);

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (copy_label)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	RgGuiId input_id = rg_gui_id_combine(id, 1u);
	RgGuiId minus_id = rg_gui_id_combine(id, 2u);
	RgGuiId plus_id = rg_gui_id_combine(id, 3u);

	rg_gui_register_focusable(ctx, input_id);

	int input_active = enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int changed = 0;
	int stepped = 0;

	if (enabled)
	{
		if (rg_gui_stepper_button(ctx, minus_rect, "-", minus_id, id))
		{
			int new_value = rg_gui_int_step_clamped(*value, use_step, 1, -1, min, max);
			if (new_value != *value)
			{
				*value = new_value;
				changed = 1;
			}
			stepped = 1;
		}

		if (rg_gui_stepper_button(ctx, plus_rect, "+", plus_id, id))
		{
			int new_value = rg_gui_int_step_clamped(*value, use_step, 1, 1, min, max);
			if (new_value != *value)
			{
				*value = new_value;
				changed = 1;
			}
			stepped = 1;
		}
	}

	if (stepped && input_active)
	{
		ctx->text_edit_state->active = 0;
		ctx->number_edit_state->id = 0u;
		ctx->number_edit_state->cached_id = 0u;
		input_active = 0;
	}

	if (enabled && !input_active && (ctx->focus_id == id || ctx->focus_id == input_id))
	{
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
			                       ? SDL_SCANCODE_LEFT
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, id, key);
			if (repeats > 0)
			{
				int new_value = rg_gui_int_step_clamped(*value, use_step, repeats,
				                                        -1, min, max);
				if (new_value != *value)
				{
					*value = new_value;
					changed = 1;
				}
			}
		}
		else if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT) ||
		         rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT)
			                       ? SDL_SCANCODE_RIGHT
			                       : SDL_SCANCODE_UP;
			int repeats = rg_gui_key_repeat(ctx, id, key);
			if (repeats > 0)
			{
				int new_value = rg_gui_int_step_clamped(*value, use_step, repeats,
				                                        1, min, max);
				if (new_value != *value)
				{
					*value = new_value;
					changed = 1;
				}
			}
		}
	}

	int submit = 0;
	int input_enabled = enabled;
	if (!input_enabled && ctx->text_edit_state->active && ctx->text_edit_state->id == input_id)
	{
		ctx->text_edit_state->active = 0;
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	int other_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id != input_id);
	int value_hovered = input_enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, value_rect);
	int want_activate = input_enabled && (!input_active && value_hovered && ctx->mouse_pressed);
	int want_tab_focus = input_enabled && (ctx->tab_focus_id == input_id);
	if (want_tab_focus)
	{
		want_activate = 1;
	}
	int use_number_buffer = input_enabled && (input_active || want_activate || !other_active);

	if (use_number_buffer)
	{
		if (!input_active && !want_activate)
		{
			char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
			rg_snprintf(display_buffer, sizeof(display_buffer), "%d", *value);
			if (value_hovered)
			{
				ctx->hot_id = input_id;
			}

			rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
			rg_gui_push_rect(ctx, value_rect, bg);
			rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

			rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
			                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
			rg_gui_push_text_ex(ctx, display_buffer, text_pos, ctx->style.color_text, 1);
		}
		else
		{
			if (!input_active)
			{
				rg_snprintf(ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer), "%d", *value);
			}

			rg_gui_text_input_ex_internal(ctx, NULL, ctx->number_edit_state->buffer, sizeof(ctx->number_edit_state->buffer),
			                              value_rect, input_id,
			                              RG_GUI_TEXT_INPUT_NO_LABEL | RG_GUI_TEXT_INPUT_UNDO_VALUE, &submit);
		}
	}
	else
	{
		char display_buffer[RG_GUI_NUMBER_BUFFER_SIZE];
		rg_snprintf(display_buffer, sizeof(display_buffer), "%d", *value);
		if (value_hovered)
		{
			ctx->hot_id = input_id;
		}

		rg_vec4 bg = value_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, value_rect, bg);
		rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
		                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text_ex(ctx, display_buffer, text_pos, ctx->style.color_text, 1);
	}

	input_active = input_enabled && (ctx->text_edit_state->active && ctx->text_edit_state->id == input_id);
	if (use_number_buffer)
	{
		if (input_active && ctx->number_edit_state->id != input_id)
		{
			ctx->number_edit_state->id = input_id;
		}

		if (ctx->number_edit_state->id == input_id)
		{
			if (submit || (!input_active && ctx->focus_id != input_id))
			{
				int new_value = 0;
				if (rg_gui_parse_int_saturated(ctx->number_edit_state->buffer, &new_value))
				{
					if (new_value < min) new_value = min;
					if (new_value > max) new_value = max;
					if (new_value != *value)
					{
						*value = new_value;
						changed = 1;
					}
				}
				ctx->number_edit_state->id = 0u;
				ctx->number_edit_state->cached_id = 0u;
			}
		}
	}

	return changed;
}

RGINLINE int rg_gui_stepper_float(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, f32 step, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_stepper_float_internal(ctx, label, value, min_value, max_value, step, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE int rg_gui_stepper_float_static(RgGuiContext* ctx, const char* label, f32* value, f32 min_value, f32 max_value, f32 step, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_stepper_float_internal(ctx, label, value, min_value, max_value, step, rect, id, 0);
}

RGINLINE int rg_gui_stepper_int(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, int step, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_stepper_int_internal(ctx, label, value, min_value, max_value, step, rect, id, RG_GUI_LABEL_COPY);
}

RGINLINE int rg_gui_stepper_int_static(RgGuiContext* ctx, const char* label, int* value, int min_value, int max_value, int step, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	return rg_gui_stepper_int_internal(ctx, label, value, min_value, max_value, step, rect, id, 0);
}

RGINLINE int rg_gui_color_picker(RgGuiContext* ctx, rg_vec4* color, RgGuiRect rect, RgGuiId id)
{
	id = rg_gui_id_scoped(ctx, id);
	RG_GUI_ASSERT(color != NULL);

	f32 spacing = ctx->style.inner_spacing;
	f32 available_h = rect.h - spacing * 3.0f;
	if (available_h < 0.0f)
	{
		available_h = 0.0f;
	}

	f32 row_h = available_h * 0.25f;
	if (row_h < 1.0f)
	{
		row_h = 1.0f;
	}

	f32 preview_w = rect.h;
	f32 slider_w = rect.w - preview_w - spacing;
	if (slider_w < 0.0f)
	{
		preview_w = 0.0f;
		slider_w = rect.w;
	}

	f32 slider_x = rect.x;
	f32 preview_x = rect.x + slider_w + spacing;

	f32 old_label_width = ctx->style.label_width;
	ctx->style.label_width = ctx->style.text_height;

	int changed = 0;
	RgGuiRect row = rg_gui_make_rect(slider_x, rect.y, slider_w, row_h);
	changed |= rg_gui_slider_float_input(ctx, "R", &color->x, 0.0f, 1.0f, row, rg_gui_id_combine(id, 1u));

	row.y += row_h + spacing;
	changed |= rg_gui_slider_float_input(ctx, "G", &color->y, 0.0f, 1.0f, row, rg_gui_id_combine(id, 2u));

	row.y += row_h + spacing;
	changed |= rg_gui_slider_float_input(ctx, "B", &color->z, 0.0f, 1.0f, row, rg_gui_id_combine(id, 3u));

	row.y += row_h + spacing;
	changed |= rg_gui_slider_float_input(ctx, "A", &color->w, 0.0f, 1.0f, row, rg_gui_id_combine(id, 4u));

	ctx->style.label_width = old_label_width;

	if (preview_w > 0.0f)
	{
		RgGuiRect preview = rg_gui_make_rect(preview_x, rect.y, preview_w, rect.h);
		rg_gui_push_rect(ctx, preview, *color);
		rg_gui_push_rect_outline(ctx, preview, ctx->style.color_border, ctx->style.border_thickness);
	}

	return changed;
}

RGINLINE int rg_gui_color_picker_hsv(RgGuiContext* ctx, rg_vec4* color, RgGuiRect rect, RgGuiId id, u32 flags)
{
	id = rg_gui_id_scoped(ctx, id);
	RG_GUI_ASSERT(color != NULL);

	f32 spacing = ctx->style.inner_spacing;
	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	if (rect.w < 0.0f) rect.w = 0.0f;
	if (rect.h < 0.0f) rect.h = 0.0f;

	f32 bar_w = rg_maxf(8.0f, ctx->style.text_height * 0.6f);
	f32 alpha_h = (flags & RG_GUI_COLOR_PICKER_NO_ALPHA) ? 0.0f
	                                                     : (ctx->style.text_height + ctx->style.padding * 2.0f);
	f32 sv_h = rect.h - alpha_h - ((alpha_h > 0.0f) ? spacing : 0.0f);
	if (sv_h < 0.0f)
	{
		sv_h = 0.0f;
	}

	f32 sv_w = rect.w - bar_w - spacing;
	if (sv_w < 0.0f)
	{
		bar_w = 0.0f;
		spacing = 0.0f;
		sv_w = rect.w;
	}

	f32 sv_size = rg_minf(sv_w, sv_h);
	if (sv_size < 0.0f)
	{
		sv_size = 0.0f;
	}

	f32 hue_gap = (bar_w > 0.0f) ? spacing : 0.0f;
	f32 hue_x = rect.x + sv_size + hue_gap;
	f32 preview_w = rect.w - sv_size - ((bar_w > 0.0f) ? (bar_w + spacing) : 0.0f);
	if (preview_w < 0.0f)
	{
		preview_w = 0.0f;
	}

	RgGuiRect sv_rect = rg_gui_make_rect(rect.x, rect.y, sv_size, sv_size);
	RgGuiRect hue_rect = rg_gui_make_rect(hue_x, rect.y, bar_w, sv_size);
	RgGuiRect preview_rect = rg_gui_make_rect(hue_x + bar_w + spacing, rect.y, preview_w, sv_size);
	f32 alpha_y = rect.y + sv_size + ((alpha_h > 0.0f) ? spacing : 0.0f);
	RgGuiRect alpha_rect = rg_gui_make_rect(rect.x, alpha_y, rect.w, alpha_h);

	rg_vec3 hsv = rg_gui_color_rgb_to_hsv(rg_vec3(color->x, color->y, color->z));
	f32 h = hsv.x;
	f32 s = hsv.y;
	f32 v = hsv.z;
	f32 a = color->w;

	int enabled = !rg_gui_is_disabled(ctx);
	int changed = 0;

	RgGuiId sv_id = rg_gui_id_combine(id, 1u);
	RgGuiId hue_id = rg_gui_id_combine(id, 2u);
	RgGuiId alpha_id = rg_gui_id_combine(id, 3u);

	if (enabled && sv_rect.w > 0.0f && sv_rect.h > 0.0f)
	{
		int sv_hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, sv_rect);
		if (sv_hovered)
		{
			ctx->hot_id = sv_id;
		}
		if (sv_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = sv_id;
			ctx->focus_id = id;
		}
		if (ctx->active_id == sv_id)
		{
			if (!ctx->mouse_down)
			{
				ctx->active_id = 0u;
			}
			else
			{
				f32 tx = (ctx->mouse_pos.x - sv_rect.x) / sv_rect.w;
				f32 ty = (ctx->mouse_pos.y - sv_rect.y) / sv_rect.h;
				s = rg_clampf(tx, 0.0f, 1.0f);
				v = 1.0f - rg_clampf(ty, 0.0f, 1.0f);
				changed = 1;
			}
		}
	}
	else if (ctx->active_id == sv_id)
	{
		ctx->active_id = 0u;
	}

	if (enabled && bar_w > 0.0f && hue_rect.h > 0.0f)
	{
		int hue_hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, hue_rect);
		if (hue_hovered)
		{
			ctx->hot_id = hue_id;
		}
		if (hue_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = hue_id;
			ctx->focus_id = id;
		}
		if (ctx->active_id == hue_id)
		{
			if (!ctx->mouse_down)
			{
				ctx->active_id = 0u;
			}
			else
			{
				f32 ty = (ctx->mouse_pos.y - hue_rect.y) / hue_rect.h;
				h = 1.0f - rg_clampf(ty, 0.0f, 1.0f);
				changed = 1;
			}
		}
	}
	else if (ctx->active_id == hue_id)
	{
		ctx->active_id = 0u;
	}

	if (!(flags & RG_GUI_COLOR_PICKER_NO_ALPHA) && enabled &&
	    alpha_rect.w > 0.0f && alpha_rect.h > 0.0f)
	{
		int alpha_hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, alpha_rect);
		if (alpha_hovered)
		{
			ctx->hot_id = alpha_id;
		}
		if (alpha_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = alpha_id;
			ctx->focus_id = id;
		}
		if (ctx->active_id == alpha_id)
		{
			if (!ctx->mouse_down)
			{
				ctx->active_id = 0u;
			}
			else
			{
				f32 tx = (ctx->mouse_pos.x - alpha_rect.x) / alpha_rect.w;
				a = rg_clampf(tx, 0.0f, 1.0f);
				changed = 1;
			}
		}
	}
	else if (ctx->active_id == alpha_id)
	{
		ctx->active_id = 0u;
	}

	if (changed)
	{
		rg_vec4 new_color = rg_gui_color_hsv(h, s, v, a);
		color->x = new_color.x;
		color->y = new_color.y;
		color->z = new_color.z;
		color->w = new_color.w;
	}

	if (sv_rect.w > 0.0f && sv_rect.h > 0.0f)
	{
		u32 steps = rg_gui_nonnegative_f32_to_u32_bounded(sv_rect.w / 10.0f);
		if (steps < 4u) steps = 4u;
		if (steps > 16u) steps = 16u;
		f32 step_w = sv_rect.w / (f32)steps;
		f32 step_h = sv_rect.h / (f32)steps;

		for (u32 y = 0u; y < steps; y++)
		{
			f32 v0 = 1.0f - ((f32)y + 0.5f) / (f32)steps;
			f32 y0 = sv_rect.y + step_h * (f32)y;

			for (u32 x = 0u; x < steps; x++)
			{
				f32 s0 = ((f32)x + 0.5f) / (f32)steps;
				f32 x0 = sv_rect.x + step_w * (f32)x;
				rg_vec4 cell = rg_gui_color_hsv(h, s0, v0, 1.0f);
				rg_gui_push_rect(ctx, rg_gui_make_rect(x0, y0, step_w, step_h), cell);
			}
		}

		rg_gui_push_rect_outline(ctx, sv_rect, ctx->style.color_border, ctx->style.border_thickness);
	}

	if (bar_w > 0.0f && hue_rect.h > 0.0f)
	{
		u32 steps = 12u;
		f32 step_h = hue_rect.h / (f32)steps;
		for (u32 i = 0u; i < steps; i++)
		{
			f32 t = ((f32)i + 0.5f) / (f32)steps;
			f32 hh = 1.0f - t;
			rg_vec4 col = rg_gui_color_hsv(hh, 1.0f, 1.0f, 1.0f);
			f32 y0 = hue_rect.y + step_h * (f32)i;
			rg_gui_push_rect(ctx, rg_gui_make_rect(hue_rect.x, y0, hue_rect.w, step_h), col);
		}
		rg_gui_push_rect_outline(ctx, hue_rect, ctx->style.color_border, ctx->style.border_thickness);
	}

	if (!(flags & RG_GUI_COLOR_PICKER_NO_ALPHA) && alpha_rect.w > 0.0f && alpha_rect.h > 0.0f)
	{
		u32 check_cols = 8u;
		f32 check_w = alpha_rect.w / (f32)check_cols;
		f32 check_h = alpha_rect.h * 0.5f;
		if (check_h > 0.0f)
		{
			for (u32 row = 0u; row < 2u; row++)
			{
				f32 y0 = alpha_rect.y + check_h * (f32)row;
				for (u32 col = 0u; col < check_cols; col++)
				{
					rg_vec4 check = ((col + row) & 1u) ? ctx->style.color_bg : ctx->style.color_bg_hover;
					f32 x0 = alpha_rect.x + check_w * (f32)col;
					rg_gui_push_rect(ctx, rg_gui_make_rect(x0, y0, check_w, check_h), check);
				}
			}
		}

		u32 steps = 12u;
		f32 step_w = alpha_rect.w / (f32)steps;
		for (u32 i = 0u; i < steps; i++)
		{
			f32 t = ((f32)i + 0.5f) / (f32)steps;
			rg_vec4 col = rg_gui_color_hsv(h, s, v, t);
			f32 x0 = alpha_rect.x + step_w * (f32)i;
			rg_gui_push_rect(ctx, rg_gui_make_rect(x0, alpha_rect.y, step_w, alpha_rect.h), col);
		}

		rg_gui_push_rect_outline(ctx, alpha_rect, ctx->style.color_border, ctx->style.border_thickness);
	}

	if (preview_rect.w > 0.0f && preview_rect.h > 0.0f)
	{
		rg_gui_push_rect(ctx, preview_rect, *color);
		rg_gui_push_rect_outline(ctx, preview_rect, ctx->style.color_border, ctx->style.border_thickness);
	}

	if (sv_rect.w > 0.0f && sv_rect.h > 0.0f)
	{
		f32 marker_size = rg_maxf(4.0f, ctx->style.text_height * 0.4f);
		f32 half = marker_size * 0.5f;
		f32 mx = sv_rect.x + sv_rect.w * s;
		f32 my = sv_rect.y + sv_rect.h * (1.0f - v);
		RgGuiRect mark = rg_gui_make_rect(mx - half, my - half, marker_size, marker_size);
		rg_gui_push_rect(ctx, mark, ctx->style.color_text);
		rg_gui_push_rect_outline(ctx, mark, ctx->style.color_border, ctx->style.border_thickness);
	}

	if (bar_w > 0.0f && hue_rect.h > 0.0f)
	{
		f32 y = hue_rect.y + hue_rect.h * (1.0f - h);
		f32 thickness = rg_maxf(1.0f, ctx->style.border_thickness);
		RgGuiRect line = rg_gui_make_rect(hue_rect.x - thickness, y - thickness * 0.5f,
		                                  hue_rect.w + thickness * 2.0f, thickness);
		rg_gui_push_rect(ctx, line, ctx->style.color_text);
	}

	if (!(flags & RG_GUI_COLOR_PICKER_NO_ALPHA) && alpha_rect.w > 0.0f && alpha_rect.h > 0.0f)
	{
		f32 x = alpha_rect.x + alpha_rect.w * a;
		f32 thickness = rg_maxf(1.0f, ctx->style.border_thickness);
		RgGuiRect line = rg_gui_make_rect(x - thickness * 0.5f, alpha_rect.y - thickness,
		                                  thickness, alpha_rect.h + thickness * 2.0f);
		rg_gui_push_rect(ctx, line, ctx->style.color_text);
	}

	return changed;
}

RGINLINE void rg_gui_progress_bar(RgGuiContext* ctx, const char* label, f32 value, f32 min_value, f32 max_value, RgGuiRect rect)
{
	f32 label_width = (label != NULL) ? ctx->style.label_width : 0.0f;
	f32 value_width = ctx->style.value_width;
	f32 inner = ctx->style.inner_spacing;

	f32 bar_x = rect.x + label_width + (label_width > 0.0f ? inner : 0.0f);
	f32 bar_w = rect.w - label_width - (label_width > 0.0f ? inner : 0.0f) -
	            value_width - (value_width > 0.0f ? inner : 0.0f);
	if (bar_w < 0.0f)
	{
		bar_w = 0.0f;
	}

	RgGuiRect bar_rect = rg_gui_make_rect(bar_x, rect.y, bar_w, rect.h);
	RgGuiRect value_rect = rg_gui_make_rect(rect.x + rect.w - value_width, rect.y, value_width, rect.h);
	if (value_rect.w < 0.0f)
	{
		value_rect.w = 0.0f;
	}

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	if (max_value < min_value)
	{
		f32 tmp = min_value;
		min_value = max_value;
		max_value = tmp;
	}

	f32 t = 0.0f;
	if (max_value > min_value)
	{
		t = (value - min_value) / (max_value - min_value);
	}
	else
	{
		t = (value >= max_value) ? 1.0f : 0.0f;
	}
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, bar_rect);
	rg_vec4 track_color = hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
	rg_gui_push_rect(ctx, bar_rect, track_color);

	f32 fill_w = bar_rect.w * t;
	if (fill_w > 0.0f)
	{
		rg_gui_push_rect(ctx, rg_gui_make_rect(bar_rect.x, bar_rect.y, fill_w, bar_rect.h), ctx->style.color_accent);
	}

	rg_gui_push_rect_outline(ctx, bar_rect, ctx->style.color_border, ctx->style.border_thickness);

	if (value_rect.w > 0.0f)
	{
		char buffer[RG_GUI_NUMBER_BUFFER_SIZE];
		rg_gui_format_float(buffer, sizeof(buffer), value, ctx->style.value_decimals);

		rg_gui_push_rect(ctx, value_rect, ctx->style.color_bg);
		rg_gui_push_rect_outline(ctx, value_rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec2 text_pos = rg_vec2(value_rect.x + ctx->style.padding,
		                           value_rect.y + (value_rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text(ctx, buffer, text_pos, ctx->style.color_text);
	}
}

RGINLINE f32 rg_gui_plot_sample_array(const void* user, u32 index)
{
	const f32* values = (const f32*)user;
	return values ? values[index] : 0.0f;
}

RGINLINE void rg_gui_plot_resolve_range(RgGuiPlotSampleFn sample_fn, const void* user, u32 count,
                                        f32* out_min, f32* out_max)
{
	f32 min_value = 0.0f;
	f32 max_value = 1.0f;

	if (count > 0u && sample_fn)
	{
		min_value = sample_fn(user, 0u);
		max_value = min_value;
		for (u32 i = 1u; i < count; i++)
		{
			f32 value = sample_fn(user, i);
			if (value < min_value)
			{
				min_value = value;
			}
			if (value > max_value)
			{
				max_value = value;
			}
		}

		if (min_value == max_value)
		{
			min_value -= 1.0f;
			max_value += 1.0f;
		}
	}

	*out_min = min_value;
	*out_max = max_value;
}

RGINLINE f32 rg_gui_plot_value_to_y(f32 value, f32 min_value, f32 max_value, RgGuiRect rect)
{
	if (max_value <= min_value || rect.h <= 0.0f)
	{
		return rect.y + rect.h * 0.5f;
	}

	f32 t = (value - min_value) / (max_value - min_value);
	if (t < 0.0f) t = 0.0f;
	if (t > 1.0f) t = 1.0f;

	return rect.y + rect.h - t * rect.h;
}

RGINLINE int rg_gui_plot_lines_internal(RgGuiContext* ctx, const char* label, RgGuiPlotSampleFn sample_fn,
                                        const void* user, u32 count, f32 min_value, f32 max_value, RgGuiRect rect)
{
	count = (u32)rg_gui_u32_count_to_int(count);
	f32 label_width = (label != NULL) ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	if (label_width > 0.0f)
	{
		rect.x += label_width + inner;
		rect.w -= label_width + inner;
		if (rect.w < 0.0f)
		{
			rect.w = 0.0f;
		}
	}

	if (rect.h < 0.0f)
	{
		rect.h = 0.0f;
	}

	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);

	int hovered_index = -1;
	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && count > 0u && rect.w > 0.0f &&
	    rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect))
	{
		if (count == 1u)
		{
			hovered_index = 0;
		}
		else
		{
			f32 t = (ctx->mouse_pos.x - rect.x) / rect.w;
			if (t < 0.0f) t = 0.0f;
			if (t > 1.0f) t = 1.0f;
			hovered_index = rg_gui_scroll_index_from_ratio(t, (int)count - 1);
		}
	}

	if (sample_fn && count > 0u && rect.w > 0.0f && rect.h > 0.0f)
	{
		if (min_value >= max_value)
		{
			rg_gui_plot_resolve_range(sample_fn, user, count, &min_value, &max_value);
		}

		f32 thickness = rg_maxf(1.0f, ctx->style.border_thickness);
		f32 half = thickness * 0.5f;

		if (count == 1u)
		{
			f32 value = sample_fn(user, 0u);
			f32 y = rg_gui_plot_value_to_y(value, min_value, max_value, rect);
			rg_gui_push_rect(ctx, rg_gui_make_rect(rect.x, y - half, rect.w, thickness), ctx->style.color_accent);
		}
		else
		{
			f32 step = rect.w / (f32)(count - 1u);
			f32 prev_value = sample_fn(user, 0u);
			f32 prev_y = rg_gui_plot_value_to_y(prev_value, min_value, max_value, rect);

			for (u32 i = 0u; i < count - 1u; i++)
			{
				f32 x0 = rect.x + step * (f32)i;
				f32 x1 = rect.x + step * (f32)(i + 1u);
				f32 y0 = prev_y;

				f32 value = sample_fn(user, i + 1u);
				f32 y1 = rg_gui_plot_value_to_y(value, min_value, max_value, rect);

				f32 width = x1 - x0;
				if (width < 0.0f)
				{
					width = 0.0f;
				}
				rg_gui_push_rect(ctx, rg_gui_make_rect(x0, y0 - half, width, thickness), ctx->style.color_accent);

				if (y1 != y0)
				{
					f32 top = (y0 < y1) ? y0 : y1;
					f32 height = (y0 < y1) ? (y1 - y0) : (y0 - y1);
					if (height > 0.0f)
					{
						rg_gui_push_rect(ctx, rg_gui_make_rect(x1 - half, top, thickness, height), ctx->style.color_accent);
					}
				}

				prev_y = y1;
			}
		}
	}

	rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

	return hovered_index;
}

RGINLINE int rg_gui_plot_histogram_internal(RgGuiContext* ctx, const char* label, RgGuiPlotSampleFn sample_fn,
                                            const void* user, u32 count, f32 min_value, f32 max_value, RgGuiRect rect)
{
	count = (u32)rg_gui_u32_count_to_int(count);
	f32 label_width = (label != NULL) ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	if (label_width > 0.0f)
	{
		rect.x += label_width + inner;
		rect.w -= label_width + inner;
		if (rect.w < 0.0f)
		{
			rect.w = 0.0f;
		}
	}

	if (rect.h < 0.0f)
	{
		rect.h = 0.0f;
	}

	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);

	int hovered_index = -1;
	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && count > 0u && rect.w > 0.0f &&
	    rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect))
	{
		f32 t = (ctx->mouse_pos.x - rect.x) / rect.w;
		if (t < 0.0f) t = 0.0f;
		if (t > 1.0f) t = 1.0f;
		hovered_index = rg_gui_bucket_index_from_ratio(t, (int)count);
	}

	if (sample_fn && count > 0u && rect.w > 0.0f && rect.h > 0.0f)
	{
		if (min_value >= max_value)
		{
			rg_gui_plot_resolve_range(sample_fn, user, count, &min_value, &max_value);
		}

		f32 bar_w = rect.w / (f32)count;
		f32 gap = (bar_w > 2.0f) ? 1.0f : 0.0f;
		f32 draw_w = bar_w - gap;
		if (draw_w < 0.0f)
		{
			draw_w = 0.0f;
		}

		f32 base_y = rect.y + rect.h;
		f32 x = rect.x + gap * 0.5f;

		for (u32 i = 0u; i < count; i++)
		{
			f32 value = sample_fn(user, i);
			f32 y = rg_gui_plot_value_to_y(value, min_value, max_value, rect);
			f32 height = base_y - y;
			if (height < 0.0f)
			{
				height = 0.0f;
			}

			if (draw_w > 0.0f)
			{
				rg_gui_push_rect(ctx, rg_gui_make_rect(x, y, draw_w, height), ctx->style.color_accent);
			}

			x += bar_w;
		}
	}

	rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

	return hovered_index;
}

RGINLINE int rg_gui_plot_lines(RgGuiContext* ctx, const char* label, const f32* values, u32 count,
                               f32 min_value, f32 max_value, RgGuiRect rect)
{
	RG_GUI_ASSERT(values != NULL || count == 0u);
	return rg_gui_plot_lines_internal(ctx, label, rg_gui_plot_sample_array, values, count, min_value, max_value, rect);
}

RGINLINE int rg_gui_plot_lines_fn(RgGuiContext* ctx, const char* label, RgGuiPlotSampleFn sample_fn, const void* user,
                                  u32 count, f32 min_value, f32 max_value, RgGuiRect rect)
{
	RG_GUI_ASSERT(sample_fn != NULL || count == 0u);
	return rg_gui_plot_lines_internal(ctx, label, sample_fn, user, count, min_value, max_value, rect);
}

RGINLINE int rg_gui_plot_histogram(RgGuiContext* ctx, const char* label, const f32* values, u32 count,
                                   f32 min_value, f32 max_value, RgGuiRect rect)
{
	RG_GUI_ASSERT(values != NULL || count == 0u);
	return rg_gui_plot_histogram_internal(ctx, label, rg_gui_plot_sample_array, values, count, min_value, max_value, rect);
}

RGINLINE int rg_gui_plot_histogram_fn(RgGuiContext* ctx, const char* label, RgGuiPlotSampleFn sample_fn, const void* user,
                                      u32 count, f32 min_value, f32 max_value, RgGuiRect rect)
{
	RG_GUI_ASSERT(sample_fn != NULL || count == 0u);
	return rg_gui_plot_histogram_internal(ctx, label, sample_fn, user, count, min_value, max_value, rect);
}

RGINLINE f32 rg_gui_curve_normalize(f32 value, f32 min_value, f32 max_value)
{
	f32 range = max_value - min_value;
	if (range <= 0.0f)
	{
		return 0.0f;
	}

	return (value - min_value) / range;
}

RGINLINE f32 rg_gui_curve_denormalize(f32 t, f32 min_value, f32 max_value)
{
	return min_value + (max_value - min_value) * t;
}

RGINLINE rg_vec2 rg_gui_curve_point_to_screen(RgGuiRect rect, rg_vec2 value, rg_vec2 min_value, rg_vec2 max_value)
{
	f32 tx = rg_gui_curve_normalize(value.x, min_value.x, max_value.x);
	f32 ty = rg_gui_curve_normalize(value.y, min_value.y, max_value.y);
	tx = rg_clampf(tx, 0.0f, 1.0f);
	ty = rg_clampf(ty, 0.0f, 1.0f);

	f32 x = rect.x + rect.w * tx;
	f32 y = rect.y + rect.h * (1.0f - ty);
	return rg_vec2(x, y);
}

RGINLINE rg_vec2 rg_gui_curve_screen_to_point(RgGuiRect rect, rg_vec2 pos, rg_vec2 min_value, rg_vec2 max_value)
{
	f32 tx = (rect.w > 0.0f) ? (pos.x - rect.x) / rect.w : 0.0f;
	f32 ty = (rect.h > 0.0f) ? 1.0f - ((pos.y - rect.y) / rect.h) : 0.0f;
	tx = rg_clampf(tx, 0.0f, 1.0f);
	ty = rg_clampf(ty, 0.0f, 1.0f);

	return rg_vec2(rg_gui_curve_denormalize(tx, min_value.x, max_value.x),
	               rg_gui_curve_denormalize(ty, min_value.y, max_value.y));
}

RGINLINE void rg_gui_curve_remove_point(rg_vec2* points, u32* count, u32 index)
{
	if (!points || !count || *count == 0u || index >= *count)
	{
		return;
	}

	for (u32 i = index + 1u; i < *count; i++)
	{
		points[i - 1u] = points[i];
	}
	*count -= 1u;
}

RGINLINE int rg_gui_curve_editor(RgGuiContext* ctx, const char* label, rg_vec2* points, u32* point_count,
                                 u32 point_capacity, rg_vec2 min_value, rg_vec2 max_value,
                                 RgGuiRect rect, RgGuiId id, u32 flags, int* selected)
{
	id = rg_gui_id_scoped(ctx, id);
	RG_GUI_ASSERT(points != NULL);
	RG_GUI_ASSERT(point_count != NULL);
	rg_gui_register_focusable(ctx, id);

	u32 count = *point_count;
	point_capacity = (u32)rg_gui_u32_count_to_int(point_capacity);
	if (count > point_capacity)
	{
		count = point_capacity;
		*point_count = count;
	}

	f32 label_width = (label != NULL) ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	if (label_width > 0.0f)
	{
		rect.x += label_width + inner;
		rect.w -= label_width + inner;
		if (rect.w < 0.0f)
		{
			rect.w = 0.0f;
		}
	}

	if (rect.h < 0.0f)
	{
		rect.h = 0.0f;
	}

	if (max_value.x < min_value.x)
	{
		f32 tmp = min_value.x;
		min_value.x = max_value.x;
		max_value.x = tmp;
	}
	if (max_value.y < min_value.y)
	{
		f32 tmp = min_value.y;
		min_value.y = max_value.y;
		max_value.y = tmp;
	}

	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}
	if (enabled && hovered && (ctx->mouse_pressed || (ctx->input && rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_RIGHT))))
	{
		ctx->focus_id = id;
	}
	int focused = enabled && (ctx->focus_id == id);

	if (selected)
	{
		if (*selected >= (int)count)
		{
			*selected = (count > 0u) ? (int)count - 1 : -1;
		}
		if (*selected < -1)
		{
			*selected = -1;
		}
	}

	if (!(flags & RG_GUI_CURVE_NO_GRID) && rect.w > 0.0f && rect.h > 0.0f)
	{
		rg_vec4 grid = ctx->style.color_border;
		grid.w *= 0.5f;
		for (u32 i = 1u; i < 4u; i++)
		{
			f32 t = (f32)i / 4.0f;
			f32 x = rect.x + rect.w * t;
			f32 y = rect.y + rect.h * t;
			rg_gui_push_line(ctx, rg_vec2(x, rect.y), rg_vec2(x, rect.y + rect.h), 1.0f, grid);
			rg_gui_push_line(ctx, rg_vec2(rect.x, y), rg_vec2(rect.x + rect.w, y), 1.0f, grid);
		}
	}

	f32 handle_size = rg_maxf(4.0f, ctx->style.text_height * 0.5f);
	f32 half = handle_size * 0.5f;
	int hovered_index = -1;
	int active_index = -1;
	int changed = 0;

	for (u32 i = 0u; i < count; i++)
	{
		rg_vec2 pos = rg_gui_curve_point_to_screen(rect, points[i], min_value, max_value);
		RgGuiRect handle = rg_gui_make_rect(pos.x - half, pos.y - half, handle_size, handle_size);
		RgGuiId handle_id = rg_gui_id_combine(id, (u64)(i + 1u));
		int handle_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, handle);

		if (handle_hovered)
		{
			hovered_index = (int)i;
			ctx->hot_id = handle_id;
		}

		if (enabled && handle_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = handle_id;
			if (selected)
			{
				*selected = (int)i;
			}
		}

		if (ctx->active_id == handle_id)
		{
			active_index = (int)i;
		}

		if ((handle_hovered || ctx->active_id == handle_id) && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_MOVE;
		}
	}

	if (enabled && hovered_index >= 0 && !(flags & RG_GUI_CURVE_NO_REMOVE) &&
	    ctx->input && rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_RIGHT))
	{
		rg_gui_curve_remove_point(points, &count, (u32)hovered_index);
		*point_count = count;
		if (ctx->active_id == rg_gui_id_combine(id, (u64)(hovered_index + 1u)))
		{
			ctx->active_id = 0u;
		}
		if (selected)
		{
			if (*selected == hovered_index)
			{
				if (count > 0u)
				{
					int next = hovered_index - 1;
					if (next < 0)
					{
						next = 0;
					}
					if (next >= (int)count)
					{
						next = (int)count - 1;
					}
					*selected = next;
				}
				else
				{
					*selected = -1;
				}
			}
			else if (*selected > hovered_index)
			{
				*selected -= 1;
			}
		}
		active_index = -1;
		changed = 1;
	}

	if (enabled && active_index >= 0)
	{
		if (!ctx->mouse_down)
		{
			ctx->active_id = 0u;
		}
		else if (rect.w > 0.0f && rect.h > 0.0f)
		{
			rg_vec2 value = rg_gui_curve_screen_to_point(rect, ctx->mouse_pos, min_value, max_value);
			if (active_index > 0)
			{
				value.x = rg_maxf(value.x, points[active_index - 1].x);
			}
			if ((u32)active_index + 1u < count)
			{
				value.x = rg_minf(value.x, points[active_index + 1].x);
			}
			points[active_index] = value;
			changed = 1;
		}
	}

	if (enabled && hovered && ctx->mouse_pressed && hovered_index < 0 && !(flags & RG_GUI_CURVE_NO_ADD))
	{
		if (count < point_capacity && rect.w > 0.0f && rect.h > 0.0f)
		{
			rg_vec2 value = rg_gui_curve_screen_to_point(rect, ctx->mouse_pos, min_value, max_value);
			u32 insert = count;
			for (u32 i = 0u; i < count; i++)
			{
				if (value.x < points[i].x)
				{
					insert = i;
					break;
				}
			}
			for (u32 i = count; i > insert; i--)
			{
				points[i] = points[i - 1u];
			}
			points[insert] = value;
			count++;
			*point_count = count;
			if (selected)
			{
				*selected = (int)insert;
			}
			ctx->active_id = rg_gui_id_combine(id, (u64)(insert + 1u));
			active_index = (int)insert;
			changed = 1;
		}
	}

	if (focused && selected && *selected >= 0 && !(flags & RG_GUI_CURVE_NO_REMOVE) &&
	    ctx->input &&
	    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_DELETE) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_BACKSPACE)))
	{
		int remove_index = *selected;
		if (remove_index >= 0 && (u32)remove_index < count)
		{
			rg_gui_curve_remove_point(points, &count, (u32)remove_index);
			*point_count = count;
			if (ctx->active_id == rg_gui_id_combine(id, (u64)(remove_index + 1u)))
			{
				ctx->active_id = 0u;
			}
			if (*selected >= (int)count)
			{
				*selected = (count > 0u) ? (int)count - 1 : -1;
			}
			changed = 1;
		}
	}

	if (count > 1u && rect.w > 0.0f && rect.h > 0.0f)
	{
		f32 thickness = rg_maxf(1.0f, ctx->style.border_thickness);
		for (u32 i = 0u; i + 1u < count; i++)
		{
			rg_vec2 a = rg_gui_curve_point_to_screen(rect, points[i], min_value, max_value);
			rg_vec2 b = rg_gui_curve_point_to_screen(rect, points[i + 1u], min_value, max_value);
			rg_gui_push_line(ctx, a, b, thickness, ctx->style.color_accent);
		}
	}

	for (u32 i = 0u; i < count; i++)
	{
		rg_vec2 pos = rg_gui_curve_point_to_screen(rect, points[i], min_value, max_value);
		RgGuiRect handle = rg_gui_make_rect(pos.x - half, pos.y - half, handle_size, handle_size);
		rg_vec4 fill = ctx->style.color_text;
		if ((selected && *selected == (int)i) || (int)i == active_index)
		{
			fill = ctx->style.color_accent;
		}
		else if ((int)i == hovered_index)
		{
			fill = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect(ctx, handle, fill);
		rg_gui_push_rect_outline(ctx, handle, ctx->style.color_border, ctx->style.border_thickness);
	}

	rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

	return changed;
}

RGINLINE rg_vec4 rg_gui_gradient_lerp(rg_vec4 a, rg_vec4 b, f32 t)
{
	rg_vec4 out;
	out.x = rg_lerpf(a.x, b.x, t);
	out.y = rg_lerpf(a.y, b.y, t);
	out.z = rg_lerpf(a.z, b.z, t);
	out.w = rg_lerpf(a.w, b.w, t);
	return out;
}

RGINLINE rg_vec4 rg_gui_gradient_sample(const RgGuiGradientStop* stops, u32 count, f32 t)
{
	if (!stops || count == 0u)
	{
		return rg_gui_color(0.0f, 0.0f, 0.0f, 0.0f);
	}

	if (count == 1u)
	{
		return stops[0].color;
	}

	t = rg_clampf(t, 0.0f, 1.0f);
	if (t <= stops[0].position)
	{
		return stops[0].color;
	}
	if (t >= stops[count - 1u].position)
	{
		return stops[count - 1u].color;
	}

	for (u32 i = 0u; i + 1u < count; i++)
	{
		f32 p0 = stops[i].position;
		f32 p1 = stops[i + 1u].position;
		if (t <= p1)
		{
			f32 denom = p1 - p0;
			f32 u = (denom > 0.0f) ? ((t - p0) / denom) : 0.0f;
			return rg_gui_gradient_lerp(stops[i].color, stops[i + 1u].color, u);
		}
	}

	return stops[count - 1u].color;
}

RGINLINE void rg_gui_gradient_remove_stop(RgGuiGradientStop* stops, u32* count, u32 index)
{
	if (!stops || !count || *count == 0u || index >= *count)
	{
		return;
	}

	for (u32 i = index + 1u; i < *count; i++)
	{
		stops[i - 1u] = stops[i];
	}
	*count -= 1u;
}

RGINLINE int rg_gui_gradient_editor(RgGuiContext* ctx, const char* label, RgGuiGradientStop* stops, u32* stop_count,
                                    u32 stop_capacity, RgGuiRect rect, RgGuiId id, u32 flags, int* selected)
{
	id = rg_gui_id_scoped(ctx, id);
	RG_GUI_ASSERT(stops != NULL);
	RG_GUI_ASSERT(stop_count != NULL);
	rg_gui_register_focusable(ctx, id);

	u32 count = *stop_count;
	stop_capacity = (u32)rg_gui_u32_count_to_int(stop_capacity);
	if (count > stop_capacity)
	{
		count = stop_capacity;
		*stop_count = count;
	}

	f32 label_width = (label != NULL) ? ctx->style.label_width : 0.0f;
	f32 inner = ctx->style.inner_spacing;

	if (label)
	{
		rg_vec2 label_pos = rg_vec2(rect.x,
		                            rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		if (RG_GUI_LABEL_COPY)
		{
			rg_gui_push_text(ctx, label, label_pos, ctx->style.color_text);
		}
		else
		{
			rg_gui_push_text_static(ctx, label, label_pos, ctx->style.color_text);
		}
	}

	if (label_width > 0.0f)
	{
		rect.x += label_width + inner;
		rect.w -= label_width + inner;
		if (rect.w < 0.0f)
		{
			rect.w = 0.0f;
		}
	}

	if (rect.w < 0.0f) rect.w = 0.0f;
	if (rect.h < 0.0f) rect.h = 0.0f;

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}
	if (enabled && hovered &&
	    (ctx->mouse_pressed ||
	     (ctx->input && rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_RIGHT))))
	{
		ctx->focus_id = id;
	}
	int focused = enabled && (ctx->focus_id == id);

	if (selected)
	{
		if (*selected >= (int)count)
		{
			*selected = (count > 0u) ? (int)count - 1 : -1;
		}
		if (*selected < -1)
		{
			*selected = -1;
		}
	}

	f32 handle_h = rg_maxf(6.0f, ctx->style.text_height * 0.6f);
	if (handle_h > rect.h)
	{
		handle_h = rect.h;
	}
	f32 handle_w = handle_h;
	RgGuiRect bar_rect = rect;
	RgGuiRect handle_strip = rg_gui_make_rect(rect.x, rect.y + rect.h - handle_h, rect.w, handle_h);

	if (bar_rect.w > 0.0f && bar_rect.h > 0.0f)
	{
		if (!(flags & RG_GUI_GRADIENT_NO_ALPHA))
		{
			u32 check_cols = 8u;
			f32 check_w = bar_rect.w / (f32)check_cols;
			f32 check_h = bar_rect.h * 0.5f;
			if (check_h < 1.0f)
			{
				check_h = bar_rect.h;
			}
			for (u32 row = 0u; row < 2u; row++)
			{
				f32 y0 = bar_rect.y + check_h * (f32)row;
				for (u32 col = 0u; col < check_cols; col++)
				{
					rg_vec4 check = ((col + row) & 1u) ? ctx->style.color_bg : ctx->style.color_bg_hover;
					f32 x0 = bar_rect.x + check_w * (f32)col;
					rg_gui_push_rect(ctx, rg_gui_make_rect(x0, y0, check_w, check_h), check);
				}
			}
		}
		else
		{
			rg_gui_push_rect(ctx, bar_rect, ctx->style.color_panel);
		}

		if (count > 0u)
		{
			u32 steps = rg_gui_nonnegative_f32_to_u32_bounded(bar_rect.w / 8.0f);
			if (steps < 8u) steps = 8u;
			if (steps > 64u) steps = 64u;
			f32 step_w = bar_rect.w / (f32)steps;
			for (u32 i = 0u; i < steps; i++)
			{
				f32 t = ((f32)i + 0.5f) / (f32)steps;
				rg_vec4 col = rg_gui_gradient_sample(stops, count, t);
				if (flags & RG_GUI_GRADIENT_NO_ALPHA)
				{
					col.w = 1.0f;
				}
				f32 x0 = bar_rect.x + step_w * (f32)i;
				rg_gui_push_rect(ctx, rg_gui_make_rect(x0, bar_rect.y, step_w, bar_rect.h), col);
			}
		}
	}

	int hovered_index = -1;
	int active_index = -1;
	int changed = 0;

	for (u32 i = 0u; i < count; i++)
	{
		f32 pos = rg_clampf(stops[i].position, 0.0f, 1.0f);
		f32 x = bar_rect.x + bar_rect.w * pos;
		RgGuiRect handle = rg_gui_make_rect(x - handle_w * 0.5f, handle_strip.y, handle_w, handle_strip.h);
		if (handle.x < bar_rect.x)
		{
			handle.x = bar_rect.x;
		}
		if (handle.x + handle.w > bar_rect.x + bar_rect.w)
		{
			handle.x = bar_rect.x + bar_rect.w - handle.w;
		}

		RgGuiId handle_id = rg_gui_id_combine(id, (u64)(i + 1u));
		int handle_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, handle);
		if (handle_hovered)
		{
			hovered_index = (int)i;
			ctx->hot_id = handle_id;
		}

		if (enabled && handle_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = handle_id;
			if (selected)
			{
				*selected = (int)i;
			}
		}

		if (ctx->active_id == handle_id)
		{
			active_index = (int)i;
		}

		if ((handle_hovered || ctx->active_id == handle_id) && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_MOVE;
		}
	}

	if (enabled && hovered_index >= 0 && !(flags & RG_GUI_GRADIENT_NO_REMOVE) &&
	    ctx->input && rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_RIGHT) &&
	    count > 2u)
	{
		rg_gui_gradient_remove_stop(stops, &count, (u32)hovered_index);
		*stop_count = count;
		if (ctx->active_id == rg_gui_id_combine(id, (u64)(hovered_index + 1u)))
		{
			ctx->active_id = 0u;
		}
		if (selected)
		{
			if (*selected == hovered_index)
			{
				if (count > 0u)
				{
					int next = hovered_index - 1;
					if (next < 0)
					{
						next = 0;
					}
					if (next >= (int)count)
					{
						next = (int)count - 1;
					}
					*selected = next;
				}
				else
				{
					*selected = -1;
				}
			}
			else if (*selected > hovered_index)
			{
				*selected -= 1;
			}
		}
		active_index = -1;
		changed = 1;
	}

	if (enabled && active_index >= 0)
	{
		if (!ctx->mouse_down)
		{
			ctx->active_id = 0u;
		}
		else if (bar_rect.w > 0.0f)
		{
			f32 t = (ctx->mouse_pos.x - bar_rect.x) / bar_rect.w;
			t = rg_clampf(t, 0.0f, 1.0f);
			if (active_index > 0)
			{
				t = rg_maxf(t, stops[active_index - 1].position);
			}
			if ((u32)active_index + 1u < count)
			{
				t = rg_minf(t, stops[active_index + 1u].position);
			}
			if (stops[active_index].position != t)
			{
				stops[active_index].position = t;
				changed = 1;
			}
		}
	}

	if (enabled && hovered && ctx->mouse_pressed && hovered_index < 0 && !(flags & RG_GUI_GRADIENT_NO_ADD))
	{
		if (count < stop_capacity && bar_rect.w > 0.0f)
		{
			f32 t = (ctx->mouse_pos.x - bar_rect.x) / bar_rect.w;
			t = rg_clampf(t, 0.0f, 1.0f);
			rg_vec4 col = (count > 0u) ? rg_gui_gradient_sample(stops, count, t)
			                           : rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f);
			if (flags & RG_GUI_GRADIENT_NO_ALPHA)
			{
				col.w = 1.0f;
			}

			u32 insert = count;
			for (u32 i = 0u; i < count; i++)
			{
				if (t < stops[i].position)
				{
					insert = i;
					break;
				}
			}
			for (u32 i = count; i > insert; i--)
			{
				stops[i] = stops[i - 1u];
			}
			stops[insert].position = t;
			stops[insert].color = col;
			count++;
			*stop_count = count;
			if (selected)
			{
				*selected = (int)insert;
			}
			ctx->active_id = rg_gui_id_combine(id, (u64)(insert + 1u));
			active_index = (int)insert;
			changed = 1;
		}
	}

	if (focused && selected && *selected >= 0 && !(flags & RG_GUI_GRADIENT_NO_REMOVE) &&
	    ctx->input && (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_DELETE) || rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_BACKSPACE)) &&
	    count > 2u)
	{
		int remove_index = *selected;
		if (remove_index >= 0 && (u32)remove_index < count)
		{
			rg_gui_gradient_remove_stop(stops, &count, (u32)remove_index);
			*stop_count = count;
			if (ctx->active_id == rg_gui_id_combine(id, (u64)(remove_index + 1u)))
			{
				ctx->active_id = 0u;
			}
			if (*selected >= (int)count)
			{
				*selected = (count > 0u) ? (int)count - 1 : -1;
			}
			changed = 1;
		}
	}

	if (selected && *selected >= 0 && *selected < (int)count)
	{
		f32 pos = rg_clampf(stops[*selected].position, 0.0f, 1.0f);
		f32 x = bar_rect.x + bar_rect.w * pos;
		f32 thickness = rg_maxf(1.0f, ctx->style.border_thickness);
		rg_gui_push_rect(ctx, rg_gui_make_rect(x - thickness * 0.5f, bar_rect.y, thickness, bar_rect.h), ctx->style.color_accent);
	}

	for (u32 i = 0u; i < count; i++)
	{
		f32 pos = rg_clampf(stops[i].position, 0.0f, 1.0f);
		f32 x = bar_rect.x + bar_rect.w * pos;
		RgGuiRect handle = rg_gui_make_rect(x - handle_w * 0.5f, handle_strip.y, handle_w, handle_strip.h);
		if (handle.x < bar_rect.x)
		{
			handle.x = bar_rect.x;
		}
		if (handle.x + handle.w > bar_rect.x + bar_rect.w)
		{
			handle.x = bar_rect.x + bar_rect.w - handle.w;
		}

		rg_vec4 fill = stops[i].color;
		fill.w = 1.0f;
		if ((selected && *selected == (int)i) || (int)i == active_index)
		{
			fill = ctx->style.color_accent;
		}
		else if ((int)i == hovered_index)
		{
			fill = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect(ctx, handle, fill);
		rg_gui_push_rect_outline(ctx, handle, ctx->style.color_border, ctx->style.border_thickness);
	}

	rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);

	return changed;
}

RGINLINE int rg_gui_node_editor_begin(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(editor != NULL);

	id = rg_gui_id_scoped(ctx, id);

	if (!editor->initialized)
	{
		if (editor->zoom <= 0.0f)
		{
			editor->zoom = 1.0f;
		}
		if (editor->zoom_min <= 0.0f)
		{
			editor->zoom_min = 0.5f;
		}
		if (editor->zoom_max <= 0.0f)
		{
			editor->zoom_max = 2.5f;
		}
		editor->pan = rg_vec2(0.0f, 0.0f);
		editor->initialized = 1;
	}

	if (editor->zoom < editor->zoom_min)
	{
		editor->zoom = editor->zoom_min;
	}
	if (editor->zoom > editor->zoom_max)
	{
		editor->zoom = editor->zoom_max;
	}

	if (rect.w < 0.0f) rect.w = 0.0f;
	if (rect.h < 0.0f) rect.h = 0.0f;

	editor->rect = rect;
	editor->node_active = 0;
	editor->id = id;
	editor->content_min = rg_vec2(0.0f, 0.0f);
	editor->content_max = rg_vec2(0.0f, 0.0f);
	editor->content_valid = 0;
	editor->node_hovered = 0;
	editor->node_count = 0u;

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
		ctx->scroll_owner_next = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	if (enabled && hovered && ctx->mouse_wheel != 0.0f)
	{
		f32 wheel = ctx->mouse_wheel;
		f32 zoom_factor = 1.0f + wheel * 0.1f;
		if (zoom_factor < 0.1f)
		{
			zoom_factor = 0.1f;
		}

		f32 prev_zoom = editor->zoom;
		f32 next_zoom = prev_zoom * zoom_factor;
		if (next_zoom < editor->zoom_min) next_zoom = editor->zoom_min;
		if (next_zoom > editor->zoom_max) next_zoom = editor->zoom_max;

		if (next_zoom != prev_zoom && prev_zoom > 0.0f)
		{
			rg_vec2 mouse = ctx->mouse_pos;
			f32 inv_zoom = 1.0f / prev_zoom;
			rg_vec2 canvas;
			canvas.x = (mouse.x - rect.x - editor->pan.x) * inv_zoom;
			canvas.y = (mouse.y - rect.y - editor->pan.y) * inv_zoom;
			editor->zoom = next_zoom;
			editor->pan.x = mouse.x - rect.x - canvas.x * editor->zoom;
			editor->pan.y = mouse.y - rect.y - canvas.y * editor->zoom;
		}
	}

	RgGuiId pan_id = rg_gui_id_combine(id, 0x50414Eu);
	int middle_pressed = ctx->input &&
	                     rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_MIDDLE);
	int middle_down = ctx->input &&
	                  rg_input_is_mouse_button_down(ctx->input, RG_MOUSE_BUTTON_MIDDLE);

	if (enabled && hovered && middle_pressed)
	{
		ctx->active_id = pan_id;
		editor->pan_start = editor->pan;
		editor->pan_start_mouse = ctx->mouse_pos;
	}

	if (ctx->active_id == pan_id)
	{
		if (!middle_down)
		{
			ctx->active_id = 0u;
		}
		else
		{
			editor->pan.x = editor->pan_start.x + (ctx->mouse_pos.x - editor->pan_start_mouse.x);
			editor->pan.y = editor->pan_start.y + (ctx->mouse_pos.y - editor->pan_start_mouse.y);
			if (ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
			{
				ctx->mouse_cursor = RG_GUI_CURSOR_MOVE;
			}
		}
	}

	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);
	rg_gui_push_clip(ctx, rect);

	return hovered;
}

RGINLINE void rg_gui_node_editor_end(RgGuiContext* ctx, RgGuiNodeEditorState* editor)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(editor != NULL);
	RG_GUI_UNUSED(editor);
	rg_gui_pop_clip(ctx);
}

RGINLINE rg_vec2 rg_gui_node_editor_to_screen(const RgGuiNodeEditorState* editor, rg_vec2 pos)
{
	rg_vec2 out;
	out.x = editor->rect.x + editor->pan.x + pos.x * editor->zoom;
	out.y = editor->rect.y + editor->pan.y + pos.y * editor->zoom;
	return out;
}

RGINLINE rg_vec2 rg_gui_node_editor_to_canvas(const RgGuiNodeEditorState* editor, rg_vec2 pos)
{
	f32 inv_zoom = (editor->zoom > 0.0f) ? (1.0f / editor->zoom) : 0.0f;
	rg_vec2 out;
	out.x = (pos.x - editor->rect.x - editor->pan.x) * inv_zoom;
	out.y = (pos.y - editor->rect.y - editor->pan.y) * inv_zoom;
	return out;
}

RGINLINE int rg_gui_node_editor_selection(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiId id,
                                          RgGuiRect* out_screen, RgGuiRect* out_canvas)
{
	if (!ctx || !editor || !ctx->input)
	{
		return 0;
	}

	id = rg_gui_id_scoped(ctx, id);
	RgGuiId select_id = rg_gui_id_combine(id, 0x53454C45u);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, editor->rect);
	int left_pressed = rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_LEFT);
	int left_down = rg_input_is_mouse_button_down(ctx->input, RG_MOUSE_BUTTON_LEFT);

	if (enabled && hovered && left_pressed && !editor->node_hovered &&
	    (ctx->active_id == 0u || ctx->active_id == select_id))
	{
		ctx->active_id = select_id;
		ctx->focus_id = id;
		editor->select_start = rg_gui_node_editor_to_canvas(editor, ctx->mouse_pos);
		editor->select_end = editor->select_start;
	}

	if (ctx->active_id == select_id)
	{
		if (!left_down)
		{
			ctx->active_id = 0u;
		}
		else
		{
			editor->select_end = rg_gui_node_editor_to_canvas(editor, ctx->mouse_pos);
		}
	}

	if (ctx->active_id == select_id)
	{
		f32 min_x = rg_minf(editor->select_start.x, editor->select_end.x);
		f32 min_y = rg_minf(editor->select_start.y, editor->select_end.y);
		f32 max_x = rg_maxf(editor->select_start.x, editor->select_end.x);
		f32 max_y = rg_maxf(editor->select_start.y, editor->select_end.y);

		RgGuiRect canvas_rect = rg_gui_make_rect(min_x, min_y, max_x - min_x, max_y - min_y);
		rg_vec2 screen_min = rg_gui_node_editor_to_screen(editor, rg_vec2(min_x, min_y));
		rg_vec2 screen_max = rg_gui_node_editor_to_screen(editor, rg_vec2(max_x, max_y));
		RgGuiRect screen_rect = rg_gui_make_rect(screen_min.x, screen_min.y,
		                                         screen_max.x - screen_min.x,
		                                         screen_max.y - screen_min.y);
		if (screen_rect.w < 0.0f)
		{
			screen_rect.x += screen_rect.w;
			screen_rect.w = -screen_rect.w;
		}
		if (screen_rect.h < 0.0f)
		{
			screen_rect.y += screen_rect.h;
			screen_rect.h = -screen_rect.h;
		}

		rg_vec4 fill = ctx->style.color_accent;
		fill.w *= 0.2f;
		rg_gui_push_rect(ctx, screen_rect, fill);
		rg_gui_push_rect_outline(ctx, screen_rect, ctx->style.color_accent,
		                         rg_maxf(1.0f, ctx->style.border_thickness));

		if (out_screen)
		{
			*out_screen = screen_rect;
		}
		if (out_canvas)
		{
			*out_canvas = canvas_rect;
		}

		return 1;
	}

	return 0;
}

RGINLINE int rg_gui_node_editor_minimap(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiRect rect, u32 flags)
{
	if (!ctx || !editor)
	{
		return 0;
	}

	if (rect.w < 0.0f) rect.w = 0.0f;
	if (rect.h < 0.0f) rect.h = 0.0f;

	rg_vec2 view_min = rg_gui_node_editor_to_canvas(editor, rg_vec2(editor->rect.x, editor->rect.y));
	rg_vec2 view_max = rg_gui_node_editor_to_canvas(editor, rg_vec2(editor->rect.x + editor->rect.w,
	                                                                editor->rect.y + editor->rect.h));
	f32 min_x = rg_minf(view_min.x, view_max.x);
	f32 min_y = rg_minf(view_min.y, view_max.y);
	f32 max_x = rg_maxf(view_min.x, view_max.x);
	f32 max_y = rg_maxf(view_min.y, view_max.y);

	if (editor->content_valid)
	{
		if (editor->content_min.x < min_x) min_x = editor->content_min.x;
		if (editor->content_min.y < min_y) min_y = editor->content_min.y;
		if (editor->content_max.x > max_x) max_x = editor->content_max.x;
		if (editor->content_max.y > max_y) max_y = editor->content_max.y;
	}

	f32 span_x = max_x - min_x;
	f32 span_y = max_y - min_y;
	if (span_x < 1.0f) span_x = 1.0f;
	if (span_y < 1.0f) span_y = 1.0f;

	f32 scale_x = rect.w / span_x;
	f32 scale_y = rect.h / span_y;
	f32 scale = rg_minf(scale_x, scale_y);
	if (scale <= 0.0f)
	{
		scale = 1.0f;
	}

	f32 map_w = span_x * scale;
	f32 map_h = span_y * scale;
	f32 map_x = rect.x + (rect.w - map_w) * 0.5f;
	f32 map_y = rect.y + (rect.h - map_h) * 0.5f;
	RgGuiRect map_rect = rg_gui_make_rect(map_x, map_y, map_w, map_h);

	rg_vec4 map_color = ctx->style.color_bg;
	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, rect, ctx->style.color_border, ctx->style.border_thickness);
	rg_gui_push_rect(ctx, map_rect, map_color);
	rg_gui_push_rect_outline(ctx, map_rect, ctx->style.color_border, ctx->style.border_thickness);

	if (editor->node_count > 0u)
	{
		rg_vec4 node_fill = ctx->style.color_accent;
		node_fill.w *= 0.35f;
		rg_vec4 node_outline = ctx->style.color_accent;
		for (u32 i = 0u; i < editor->node_count; i++)
		{
			const RgGuiRect node = editor->node_rects[i];
			f32 node_x = map_x + (node.x - min_x) * scale;
			f32 node_y = map_y + (node.y - min_y) * scale;
			f32 node_w = node.w * scale;
			f32 node_h = node.h * scale;
			if (node_w < 1.0f) node_w = 1.0f;
			if (node_h < 1.0f) node_h = 1.0f;
			rg_gui_push_rect(ctx, rg_gui_make_rect(node_x, node_y, node_w, node_h), node_fill);
			rg_gui_push_rect_outline(ctx, rg_gui_make_rect(node_x, node_y, node_w, node_h),
			                         node_outline, ctx->style.border_thickness);
		}
	}

	f32 view_min_x = rg_minf(view_min.x, view_max.x);
	f32 view_min_y = rg_minf(view_min.y, view_max.y);
	f32 view_max_x = rg_maxf(view_min.x, view_max.x);
	f32 view_max_y = rg_maxf(view_min.y, view_max.y);
	f32 view_x = map_x + (view_min_x - min_x) * scale;
	f32 view_y = map_y + (view_min_y - min_y) * scale;
	f32 view_w = (view_max_x - view_min_x) * scale;
	f32 view_h = (view_max_y - view_min_y) * scale;
	RgGuiRect view_rect = rg_gui_make_rect(view_x, view_y, view_w, view_h);

	rg_vec4 view_fill = ctx->style.color_accent;
	view_fill.w *= 0.15f;
	rg_gui_push_rect(ctx, view_rect, view_fill);
	rg_gui_push_rect_outline(ctx, view_rect, ctx->style.color_accent,
	                         rg_maxf(1.0f, ctx->style.border_thickness));

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = rg_gui_id_combine(editor->id, 0x4D4D4150u);
		editor->node_hovered = 1;
	}

	if ((flags & RG_GUI_NODE_MINIMAP_NO_INTERACT) == 0u && ctx->input)
	{
		RgGuiId minimap_id = rg_gui_id_combine(editor->id, 0x4D4D4150u);
		int left_pressed = rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_LEFT);
		int left_down = rg_input_is_mouse_button_down(ctx->input, RG_MOUSE_BUTTON_LEFT);

		if (enabled && hovered && left_pressed && (ctx->active_id == 0u || ctx->active_id == minimap_id))
		{
			ctx->active_id = minimap_id;
		}

		if (ctx->active_id == minimap_id)
		{
			if (!left_down)
			{
				ctx->active_id = 0u;
			}
			else
			{
				f32 local_x = ctx->mouse_pos.x;
				f32 local_y = ctx->mouse_pos.y;
				if (local_x < map_rect.x) local_x = map_rect.x;
				if (local_x > map_rect.x + map_rect.w) local_x = map_rect.x + map_rect.w;
				if (local_y < map_rect.y) local_y = map_rect.y;
				if (local_y > map_rect.y + map_rect.h) local_y = map_rect.y + map_rect.h;

				rg_vec2 target;
				target.x = min_x + (local_x - map_rect.x) / scale;
				target.y = min_y + (local_y - map_rect.y) / scale;

				f32 center_x = editor->rect.x + editor->rect.w * 0.5f;
				f32 center_y = editor->rect.y + editor->rect.h * 0.5f;
				editor->pan.x = center_x - editor->rect.x - target.x * editor->zoom;
				editor->pan.y = center_y - editor->rect.y - target.y * editor->zoom;
				return 1;
			}
		}
	}

	return 0;
}

RGINLINE RgGuiRect rg_gui_node_graph_node_screen_rect(const RgGuiContext* ctx, const RgGuiNodeEditorState* editor,
                                                      rg_vec2 pos, rg_vec2 size, f32* out_title_h)
{
	RgGuiRect node_rect = {0};
	if (!ctx || !editor)
	{
		return node_rect;
	}

	rg_vec2 screen = rg_gui_node_editor_to_screen(editor, pos);
	f32 zoom = editor->zoom;
	f32 node_w = size.x * zoom;
	f32 node_h = size.y * zoom;
	if (node_w < 0.0f) node_w = 0.0f;
	if (node_h < 0.0f) node_h = 0.0f;

	f32 pad = ctx->style.padding;
	f32 base_title = ctx->style.text_height + pad * 2.0f;
	f32 title_h = base_title * zoom;
	if (title_h > node_h)
	{
		title_h = node_h;
	}

	if (node_h < title_h)
	{
		node_h = title_h;
	}

	if (out_title_h)
	{
		*out_title_h = title_h;
	}

	node_rect = rg_gui_make_rect(screen.x, screen.y, node_w, node_h);
	return node_rect;
}

RGINLINE int rg_gui_node_begin_ex(RgGuiContext* ctx, RgGuiNodeEditorState* editor, const char* title,
                                  rg_vec2* pos, rg_vec2 size, RgGuiRect* out_rect, RgGuiId id, int allow_input,
                                  const RgGuiNodeGraphNodeStyle* style)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(editor != NULL);
	RG_GUI_ASSERT(pos != NULL);

	id = rg_gui_id_scoped(ctx, id);

	f32 title_h = 0.0f;
	RgGuiRect node_rect = rg_gui_node_graph_node_screen_rect(ctx, editor, *pos, size, &title_h);

	int enabled = !rg_gui_is_disabled(ctx);
	int node_hovered = allow_input && enabled &&
	                   rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, node_rect);
	if (node_hovered)
	{
		editor->node_hovered = 1;
	}

	RgGuiId drag_id = rg_gui_id_combine(id, 1u);
	RgGuiRect title_rect = rg_gui_make_rect(node_rect.x, node_rect.y, node_rect.w, title_h);
	int header_hovered = allow_input && enabled &&
	                     rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, title_rect);
	int clicked = 0;
	if (header_hovered)
	{
		ctx->hot_id = drag_id;
	}
	if (enabled && header_hovered && ctx->mouse_pressed &&
	    (ctx->active_id == 0u || ctx->active_id == drag_id))
	{
		ctx->active_id = drag_id;
		ctx->focus_id = id;
		editor->drag_start_mouse = rg_gui_node_editor_to_canvas(editor, ctx->mouse_pos);
		editor->drag_start_pos = *pos;
		clicked = 1;
	}

	if (ctx->active_id == drag_id)
	{
		if (!ctx->mouse_down)
		{
			ctx->active_id = 0u;
		}
		else
		{
			rg_vec2 mouse_canvas = rg_gui_node_editor_to_canvas(editor, ctx->mouse_pos);
			f32 next_x = editor->drag_start_pos.x + (mouse_canvas.x - editor->drag_start_mouse.x);
			f32 next_y = editor->drag_start_pos.y + (mouse_canvas.y - editor->drag_start_mouse.y);
			if (next_x != pos->x || next_y != pos->y)
			{
				pos->x = next_x;
				pos->y = next_y;
				node_rect = rg_gui_node_graph_node_screen_rect(ctx, editor, *pos, size, &title_h);
				title_rect = rg_gui_make_rect(node_rect.x, node_rect.y, node_rect.w, title_h);
			}
		}
		if (ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
		{
			ctx->mouse_cursor = RG_GUI_CURSOR_MOVE;
		}
	}

	if (out_rect)
	{
		*out_rect = node_rect;
	}

	f32 node_min_x = rg_minf(pos->x, pos->x + size.x);
	f32 node_min_y = rg_minf(pos->y, pos->y + size.y);
	f32 node_max_x = rg_maxf(pos->x, pos->x + size.x);
	f32 node_max_y = rg_maxf(pos->y, pos->y + size.y);
	if (!editor->content_valid)
	{
		editor->content_min = rg_vec2(node_min_x, node_min_y);
		editor->content_max = rg_vec2(node_max_x, node_max_y);
		editor->content_valid = 1;
	}
	else
	{
		if (node_min_x < editor->content_min.x) editor->content_min.x = node_min_x;
		if (node_min_y < editor->content_min.y) editor->content_min.y = node_min_y;
		if (node_max_x > editor->content_max.x) editor->content_max.x = node_max_x;
		if (node_max_y > editor->content_max.y) editor->content_max.y = node_max_y;
	}

	if (editor->node_count < RG_GUI_NODE_EDITOR_MAX_NODES)
	{
		editor->node_rects[editor->node_count++] =
		    rg_gui_make_rect(node_min_x, node_min_y, node_max_x - node_min_x, node_max_y - node_min_y);
	}

	if (!rg_gui_rects_overlap(node_rect, editor->rect) && ctx->active_id != drag_id)
	{
		return 0;
	}

	RgGuiRect body_rect = rg_gui_make_rect(node_rect.x,
	                                       node_rect.y + title_h,
	                                       node_rect.w,
	                                       node_rect.h - title_h);
	if (body_rect.h < 0.0f)
	{
		body_rect.h = 0.0f;
	}

	f32 pad = ctx->style.padding;
	RgGuiNodeGraphNodeStyle local_style;
	local_style.body = ctx->style.color_panel;
	local_style.title = ctx->style.color_bg;
	local_style.title_hover = ctx->style.color_bg_hover;
	local_style.title_active = ctx->style.color_bg_active;
	local_style.border = ctx->style.color_border;
	local_style.text = ctx->style.color_text;
	local_style.border_thickness = ctx->style.border_thickness;
	local_style.text_scale = 1.0f;
	if (style)
	{
		local_style = *style;
	}

	rg_vec4 body_color = local_style.body;
	rg_vec4 title_color = local_style.title;
	if (ctx->active_id == drag_id)
	{
		title_color = local_style.title_active;
	}
	else if (header_hovered)
	{
		title_color = local_style.title_hover;
	}
	f32 node_text_scale = local_style.text_scale > 0.0f ? local_style.text_scale : 1.0f;
	f32 node_pad = pad * node_text_scale;
	if (node_pad < 1.0f && pad > 0.0f)
	{
		node_pad = 1.0f;
	}

	rg_gui_push_rect(ctx, node_rect, body_color);
	rg_gui_push_rect(ctx, title_rect, title_color);
	rg_gui_push_rect_outline(ctx, node_rect, local_style.border, local_style.border_thickness);

	if (title && title_rect.h > 0.0f)
	{
		f32 text_scale = node_text_scale;
		f32 scaled_text_h = ctx->style.text_height * text_scale;
		rg_vec2 text_pos = rg_vec2(title_rect.x + node_pad,
		                           title_rect.y + (title_rect.h - scaled_text_h) * 0.5f);
		const char* title_draw = title;
		int copy_title = RG_GUI_LABEL_COPY;
		f32 available_w = title_rect.w - node_pad * 2.0f;
		if (available_w > 0.0f && text_scale > 0.0f)
		{
			size_t title_len = strlen(title);
			size_t title_end = rg_gui_text_end_for_width(ctx, title, title_len, 0u, available_w / text_scale);
			if (title_end < title_len)
			{
				title_draw = rg_gui_copy_text_range(ctx, title, 0u, title_end);
				copy_title = 0;
			}
		}
		if (copy_title)
		{
			rg_gui_push_text_scaled(ctx, title_draw, text_pos, local_style.text, text_scale);
		}
		else
		{
			rg_gui_push_text_scaled_static(ctx, title_draw, text_pos, local_style.text, text_scale);
		}
	}

	editor->prev_layout = ctx->layout;
	editor->node_active = 1;
	rg_gui_push_clip(ctx, body_rect);

	f32 content_pad = node_pad;
	f32 content_w = body_rect.w - content_pad * 2.0f;
	if (content_w < 0.0f)
	{
		content_w = 0.0f;
	}
	f32 content_x = body_rect.x + content_pad;
	f32 content_y = body_rect.y + content_pad;

	rg_gui_layout_begin(ctx, content_x, content_y, content_w, ctx->style.inner_spacing);

	return clicked;
}

RGINLINE int rg_gui_node_begin(RgGuiContext* ctx, RgGuiNodeEditorState* editor, const char* title,
                               rg_vec2* pos, rg_vec2 size, RgGuiRect* out_rect, RgGuiId id)
{
	return rg_gui_node_begin_ex(ctx, editor, title, pos, size, out_rect, id, 1, NULL);
}

RGINLINE void rg_gui_node_end(RgGuiContext* ctx, RgGuiNodeEditorState* editor)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(editor != NULL);

	if (editor->node_active)
	{
		rg_gui_pop_clip(ctx);
		ctx->layout = editor->prev_layout;
		editor->node_active = 0;
	}
}

RGINLINE rg_vec2 rg_gui_node_bezier_point(rg_vec2 a, rg_vec2 b, rg_vec2 c, rg_vec2 d, f32 t)
{
	f32 u = 1.0f - t;
	f32 tt = t * t;
	f32 uu = u * u;
	f32 uuu = uu * u;
	f32 ttt = tt * t;

	rg_vec2 p;
	p.x = uuu * a.x;
	p.y = uuu * a.y;

	p.x += 3.0f * uu * t * b.x;
	p.y += 3.0f * uu * t * b.y;

	p.x += 3.0f * u * tt * c.x;
	p.y += 3.0f * u * tt * c.y;

	p.x += ttt * d.x;
	p.y += ttt * d.y;
	return p;
}

RGINLINE void rg_gui_node_link(RgGuiContext* ctx, const RgGuiNodeEditorState* editor,
                               rg_vec2 a, rg_vec2 b, rg_vec4 color)
{
	if (!ctx || !editor)
	{
		return;
	}

	rg_vec2 start = rg_gui_node_editor_to_screen(editor, a);
	rg_vec2 end = rg_gui_node_editor_to_screen(editor, b);
	f32 thickness = rg_maxf(1.0f, ctx->style.border_thickness);
	if (editor->link_style == RG_GUI_NODE_LINK_STYLE_BEZIER)
	{
		f32 dx = rg_maxf(40.0f, rg_absf(end.x - start.x) * 0.5f);
		rg_vec2 c1 = rg_vec2(start.x + dx, start.y);
		rg_vec2 c2 = rg_vec2(end.x - dx, end.y);
		const u32 segments = 12u;
		rg_vec2 prev = start;
		for (u32 i = 1u; i <= segments; i++)
		{
			f32 t = (f32)i / (f32)segments;
			rg_vec2 p = rg_gui_node_bezier_point(start, c1, c2, end, t);
			rg_gui_push_line(ctx, prev, p, thickness, color);
			prev = p;
		}
		return;
	}

	f32 mid_x = start.x + (end.x - start.x) * 0.5f;
	rg_vec2 elbow_a = rg_vec2(mid_x, start.y);
	rg_vec2 elbow_b = rg_vec2(mid_x, end.y);

	rg_gui_push_line(ctx, start, elbow_a, thickness, color);
	rg_gui_push_line(ctx, elbow_a, elbow_b, thickness, color);
	rg_gui_push_line(ctx, elbow_b, end, thickness, color);
}

RGINLINE void rg_gui_node_graph_init(RgGuiNodeGraph* graph)
{
	if (!graph)
	{
		return;
	}

	graph->node_count = 0u;
	graph->link_count = 0u;
}

RGINLINE void rg_gui_node_graph_state_reset(RgGuiNodeGraphState* state)
{
	if (!state)
	{
		return;
	}

	state->link_drag_active = 0;
	state->link_drag_index = -1;
	state->hovered_link_index = -1;
	state->clicked_link_index = -1;
	state->selected_link_index = -1;
	state->link_drag.node = 0u;
	state->link_drag.port = 0u;
	state->link_drag.output = 0u;
	state->link_drag_original.from_node = 0u;
	state->link_drag_original.from_port = 0u;
	state->link_drag_original.to_node = 0u;
	state->link_drag_original.to_port = 0u;
	state->drag_active = 0;
	state->drag_anchor = -1;
	state->marquee_active = 0;
	state->marquee_add = 0;
	state->consume_right = 0;
	state->move_count = 0u;
	state->link_scratch_count = 0u;
}

RGINLINE void rg_gui_node_group_state_reset(RgGuiNodeGroupState* state)
{
	if (!state)
	{
		return;
	}

	state->active_id = 0u;
	state->active_index = -1;
	state->active_mode = RG_GUI_NODE_GROUP_MODE_NONE;
	state->resize_mask = 0;
	state->hovered_index = -1;
	state->clicked_index = -1;
	state->drag_start_mouse = rg_vec2(0.0f, 0.0f);
	state->drag_start_pos = rg_vec2(0.0f, 0.0f);
	state->drag_start_size = rg_vec2(0.0f, 0.0f);
	state->move_count = 0u;
}

RGINLINE int rg_gui_node_group_find_member(const RgGuiNodeGroup* group, RgGuiId node_id)
{
	if (!group)
	{
		return -1;
	}

	for (u32 i = 0u; i < group->member_count; i++)
	{
		if (group->members[i] == node_id)
		{
			return (int)i;
		}
	}

	return -1;
}

RGINLINE int rg_gui_node_group_add_member(RgGuiNodeGroup* group, RgGuiId node_id)
{
	if (!group || group->member_count >= RG_GUI_NODE_GROUP_MAX_MEMBERS)
	{
		return 0;
	}

	if (rg_gui_node_group_find_member(group, node_id) >= 0)
	{
		return 0;
	}

	group->members[group->member_count++] = node_id;
	return 1;
}

RGINLINE int rg_gui_node_group_remove_member(RgGuiNodeGroup* group, RgGuiId node_id)
{
	if (!group)
	{
		return 0;
	}

	int index = rg_gui_node_group_find_member(group, node_id);
	if (index < 0)
	{
		return 0;
	}

	u32 last = group->member_count - 1u;
	if ((u32)index != last)
	{
		group->members[index] = group->members[last];
	}
	group->member_count--;
	return 1;
}

RGINLINE u32 rg_gui_node_group_move_members(RgGuiNodeGraph* graph, const RgGuiNodeGroup* group, rg_vec2 delta)
{
	if (!graph || !group)
	{
		return 0u;
	}

	u32 moved = 0u;
	if (delta.x == 0.0f && delta.y == 0.0f)
	{
		return 0u;
	}

	for (u32 i = 0u; i < group->member_count; i++)
	{
		int index = rg_gui_node_graph_find_node_index(graph, group->members[i]);
		if (index < 0)
		{
			continue;
		}
		graph->node_positions[index].x += delta.x;
		graph->node_positions[index].y += delta.y;
		moved++;
	}

	return moved;
}

RGINLINE RgGuiNodeGroupResult rg_gui_node_group(RgGuiContext* ctx, RgGuiNodeEditorState* editor,
                                                RgGuiNodeGroupState* state, RgGuiNodeGroup* group,
                                                const char* title, u32 index)
{
	RgGuiNodeGroupResult result = {0};
	result.mode = RG_GUI_NODE_GROUP_MODE_NONE;

	if (!ctx || !editor || !state || !group)
	{
		return result;
	}

	if (group->id == 0u)
	{
		return result;
	}

	RgGuiId id = rg_gui_id_scoped(ctx, group->id);

	f32 zoom = editor->zoom;
	if (zoom <= 0.0f)
	{
		zoom = 1.0f;
	}

	f32 min_x = rg_minf(group->position.x, group->position.x + group->size.x);
	f32 min_y = rg_minf(group->position.y, group->position.y + group->size.y);
	f32 max_x = rg_maxf(group->position.x, group->position.x + group->size.x);
	f32 max_y = rg_maxf(group->position.y, group->position.y + group->size.y);

	rg_vec2 screen = rg_gui_node_editor_to_screen(editor, rg_vec2(min_x, min_y));
	f32 rect_w = (max_x - min_x) * zoom;
	f32 rect_h = (max_y - min_y) * zoom;
	if (rect_w < 0.0f) rect_w = 0.0f;
	if (rect_h < 0.0f) rect_h = 0.0f;
	RgGuiRect rect = rg_gui_make_rect(screen.x, screen.y, rect_w, rect_h);

	f32 pad = ctx->style.padding;
	f32 base_title = ctx->style.text_height + pad * 2.0f;
	f32 title_h = base_title * zoom;
	if (title_h < base_title)
	{
		title_h = base_title;
	}
	if (title_h > rect.h)
	{
		title_h = rect.h;
	}

	RgGuiRect title_rect = rg_gui_make_rect(rect.x, rect.y, rect.w, title_h);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	int header_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, title_rect);
	int left_pressed = ctx->input && rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_LEFT);
	int left_down = ctx->input && rg_input_is_mouse_button_down(ctx->input, RG_MOUSE_BUTTON_LEFT);

	result.hovered = hovered ? 1 : 0;
	if (hovered)
	{
		state->hovered_index = (int)index;
	}

	int can_move = (group->flags & RG_GUI_NODE_GROUP_NO_MOVE) == 0u;
	int can_resize = (group->flags & RG_GUI_NODE_GROUP_NO_RESIZE) == 0u;

	enum
	{
		RG_GUI_NODE_GROUP_RESIZE_LEFT = 1,
		RG_GUI_NODE_GROUP_RESIZE_RIGHT = 2,
		RG_GUI_NODE_GROUP_RESIZE_TOP = 4,
		RG_GUI_NODE_GROUP_RESIZE_BOTTOM = 8
	};

	int resize_mask = 0;
	if (enabled && can_resize && rect.w > 0.0f && rect.h > 0.0f)
	{
		f32 resize_pad = rg_maxf(4.0f, pad);
		f32 mx = ctx->mouse_pos.x;
		f32 my = ctx->mouse_pos.y;

		if (mx >= rect.x - resize_pad && mx <= rect.x + resize_pad)
		{
			resize_mask |= RG_GUI_NODE_GROUP_RESIZE_LEFT;
		}
		if (mx >= rect.x + rect.w - resize_pad && mx <= rect.x + rect.w + resize_pad)
		{
			resize_mask |= RG_GUI_NODE_GROUP_RESIZE_RIGHT;
		}
		if (my >= rect.y - resize_pad && my <= rect.y + resize_pad)
		{
			resize_mask |= RG_GUI_NODE_GROUP_RESIZE_TOP;
		}
		if (my >= rect.y + rect.h - resize_pad && my <= rect.y + rect.h + resize_pad)
		{
			resize_mask |= RG_GUI_NODE_GROUP_RESIZE_BOTTOM;
		}

		if (!hovered)
		{
			resize_mask = 0;
		}
	}

	if ((header_hovered && can_move) || resize_mask || ctx->active_id == id)
	{
		editor->node_hovered = 1;
	}

	if (enabled && left_pressed && (ctx->active_id == 0u || ctx->active_id == id) &&
	    (state->active_id == 0u || state->active_id == id))
	{
		if (resize_mask != 0 && can_resize)
		{
			ctx->active_id = id;
			ctx->focus_id = id;
			state->active_id = id;
			state->active_index = (int)index;
			state->active_mode = RG_GUI_NODE_GROUP_MODE_RESIZE;
			state->resize_mask = resize_mask;
			state->drag_start_mouse = rg_gui_node_editor_to_canvas(editor, ctx->mouse_pos);
			state->drag_start_pos = rg_vec2(min_x, min_y);
			state->drag_start_size = rg_vec2(max_x - min_x, max_y - min_y);
			result.started = 1;
			result.active = 1;
			result.mode = RG_GUI_NODE_GROUP_MODE_RESIZE;
			state->clicked_index = (int)index;
		}
		else if (header_hovered && can_move)
		{
			ctx->active_id = id;
			ctx->focus_id = id;
			state->active_id = id;
			state->active_index = (int)index;
			state->active_mode = RG_GUI_NODE_GROUP_MODE_MOVE;
			state->resize_mask = 0;
			state->drag_start_mouse = rg_gui_node_editor_to_canvas(editor, ctx->mouse_pos);
			state->drag_start_pos = rg_vec2(min_x, min_y);
			state->drag_start_size = rg_vec2(max_x - min_x, max_y - min_y);
			result.started = 1;
			result.active = 1;
			result.mode = RG_GUI_NODE_GROUP_MODE_MOVE;
			state->clicked_index = (int)index;
		}
	}

	int active = (ctx->active_id == id && state->active_id == id);
	if (active)
	{
		result.active = 1;
		result.mode = state->active_mode;

		if (!left_down)
		{
			result.ended = 1;
			ctx->active_id = 0u;
			state->active_id = 0u;
			state->active_index = -1;
			state->active_mode = RG_GUI_NODE_GROUP_MODE_NONE;
			state->resize_mask = 0;
			result.active = 0;
			active = 0;
		}
		else if (state->active_mode == RG_GUI_NODE_GROUP_MODE_MOVE)
		{
			rg_vec2 mouse_canvas = rg_gui_node_editor_to_canvas(editor, ctx->mouse_pos);
			rg_vec2 delta =
			    {
			        .x = mouse_canvas.x - state->drag_start_mouse.x,
			        .y = mouse_canvas.y - state->drag_start_mouse.y};
			result.moved = (delta.x != 0.0f || delta.y != 0.0f) ? 1 : 0;
			result.move_delta = delta;
			group->position.x = state->drag_start_pos.x + delta.x;
			group->position.y = state->drag_start_pos.y + delta.y;
			group->size = state->drag_start_size;
			if (ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
			{
				ctx->mouse_cursor = RG_GUI_CURSOR_MOVE;
			}
		}
		else if (state->active_mode == RG_GUI_NODE_GROUP_MODE_RESIZE)
		{
			rg_vec2 mouse_canvas = rg_gui_node_editor_to_canvas(editor, ctx->mouse_pos);
			rg_vec2 delta =
			    {
			        .x = mouse_canvas.x - state->drag_start_mouse.x,
			        .y = mouse_canvas.y - state->drag_start_mouse.y};
			rg_vec2 pos = state->drag_start_pos;
			rg_vec2 size = state->drag_start_size;
			f32 right = state->drag_start_pos.x + state->drag_start_size.x;
			f32 bottom = state->drag_start_pos.y + state->drag_start_size.y;

			if (state->resize_mask & RG_GUI_NODE_GROUP_RESIZE_LEFT)
			{
				pos.x = state->drag_start_pos.x + delta.x;
				size.x = right - pos.x;
			}
			if (state->resize_mask & RG_GUI_NODE_GROUP_RESIZE_RIGHT)
			{
				size.x = state->drag_start_size.x + delta.x;
			}
			if (state->resize_mask & RG_GUI_NODE_GROUP_RESIZE_TOP)
			{
				pos.y = state->drag_start_pos.y + delta.y;
				size.y = bottom - pos.y;
			}
			if (state->resize_mask & RG_GUI_NODE_GROUP_RESIZE_BOTTOM)
			{
				size.y = state->drag_start_size.y + delta.y;
			}

			f32 min_w = rg_maxf(base_title * 2.0f, base_title) / zoom;
			f32 min_h = base_title / zoom;
			if (size.x < min_w)
			{
				if (state->resize_mask & RG_GUI_NODE_GROUP_RESIZE_LEFT)
				{
					pos.x = right - min_w;
				}
				size.x = min_w;
			}
			if (size.y < min_h)
			{
				if (state->resize_mask & RG_GUI_NODE_GROUP_RESIZE_TOP)
				{
					pos.y = bottom - min_h;
				}
				size.y = min_h;
			}

			group->position = pos;
			group->size = size;
			result.resized = (size.x != state->drag_start_size.x || size.y != state->drag_start_size.y) ? 1 : 0;
			result.size_delta.x = size.x - state->drag_start_size.x;
			result.size_delta.y = size.y - state->drag_start_size.y;

			if (ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
			{
				int use_h = (state->resize_mask & (RG_GUI_NODE_GROUP_RESIZE_LEFT | RG_GUI_NODE_GROUP_RESIZE_RIGHT)) != 0;
				int use_v = (state->resize_mask & (RG_GUI_NODE_GROUP_RESIZE_TOP | RG_GUI_NODE_GROUP_RESIZE_BOTTOM)) != 0;
				ctx->mouse_cursor = use_h && !use_v ? RG_GUI_CURSOR_RESIZE_H : RG_GUI_CURSOR_RESIZE_V;
			}
		}
	}
	else if (resize_mask != 0 && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		int use_h = (resize_mask & (RG_GUI_NODE_GROUP_RESIZE_LEFT | RG_GUI_NODE_GROUP_RESIZE_RIGHT)) != 0;
		int use_v = (resize_mask & (RG_GUI_NODE_GROUP_RESIZE_TOP | RG_GUI_NODE_GROUP_RESIZE_BOTTOM)) != 0;
		ctx->mouse_cursor = use_h && !use_v ? RG_GUI_CURSOR_RESIZE_H : RG_GUI_CURSOR_RESIZE_V;
	}
	else if (header_hovered && can_move && ctx->mouse_cursor == RG_GUI_CURSOR_DEFAULT)
	{
		ctx->mouse_cursor = RG_GUI_CURSOR_MOVE;
	}

	min_x = rg_minf(group->position.x, group->position.x + group->size.x);
	min_y = rg_minf(group->position.y, group->position.y + group->size.y);
	max_x = rg_maxf(group->position.x, group->position.x + group->size.x);
	max_y = rg_maxf(group->position.y, group->position.y + group->size.y);

	if (!editor->content_valid)
	{
		editor->content_min = rg_vec2(min_x, min_y);
		editor->content_max = rg_vec2(max_x, max_y);
		editor->content_valid = 1;
	}
	else
	{
		if (min_x < editor->content_min.x) editor->content_min.x = min_x;
		if (min_y < editor->content_min.y) editor->content_min.y = min_y;
		if (max_x > editor->content_max.x) editor->content_max.x = max_x;
		if (max_y > editor->content_max.y) editor->content_max.y = max_y;
	}

	screen = rg_gui_node_editor_to_screen(editor, rg_vec2(min_x, min_y));
	rect_w = (max_x - min_x) * zoom;
	rect_h = (max_y - min_y) * zoom;
	if (rect_w < 0.0f) rect_w = 0.0f;
	if (rect_h < 0.0f) rect_h = 0.0f;
	rect = rg_gui_make_rect(screen.x, screen.y, rect_w, rect_h);
	if (!rg_gui_rects_overlap(rect, editor->rect))
	{
		return result;
	}

	title_h = base_title * zoom;
	if (title_h < base_title)
	{
		title_h = base_title;
	}
	if (title_h > rect.h)
	{
		title_h = rect.h;
	}
	title_rect = rg_gui_make_rect(rect.x, rect.y, rect.w, title_h);

	rg_vec4 body = ctx->style.color_panel;
	body.x *= 0.85f;
	body.y *= 0.85f;
	body.z *= 0.85f;
	rg_vec4 title_color = ctx->style.color_bg;
	if (active)
	{
		title_color = ctx->style.color_bg_active;
	}
	else if (header_hovered)
	{
		title_color = ctx->style.color_bg_hover;
	}

	rg_vec4 border_color = ctx->style.color_border;
	f32 border_thickness = ctx->style.border_thickness;
	if (group->flags & RG_GUI_NODE_GROUP_SELECTED)
	{
		border_color = ctx->style.color_accent;
		border_thickness = rg_maxf(1.0f, border_thickness * 2.0f);
	}

	rg_gui_push_rect(ctx, rect, body);
	if (title_h > 0.0f)
	{
		rg_gui_push_rect(ctx, title_rect, title_color);
		if (title)
		{
			rg_vec2 text_pos = rg_vec2(title_rect.x + pad,
			                           title_rect.y + (title_rect.h - ctx->style.text_height) * 0.5f);
			if (RG_GUI_LABEL_COPY)
			{
				rg_gui_push_text(ctx, title, text_pos, ctx->style.color_text);
			}
			else
			{
				rg_gui_push_text_static(ctx, title, text_pos, ctx->style.color_text);
			}
		}
	}
	rg_gui_push_rect_outline(ctx, rect, border_color, border_thickness);

	return result;
}

RGINLINE int rg_gui_node_graph_find_node_index(const RgGuiNodeGraph* graph, RgGuiId id)
{
	if (!graph)
	{
		return -1;
	}

	for (u32 i = 0u; i < graph->node_count; i++)
	{
		if (graph->node_ids[i] == id)
		{
			return (int)i;
		}
	}

	return -1;
}

RGINLINE int rg_gui_node_graph_add_node(RgGuiNodeGraph* graph, RgGuiId id, rg_vec2 pos, rg_vec2 size,
                                        u8 input_count, u8 output_count)
{
	if (!graph || graph->node_count >= RG_GUI_NODE_GRAPH_MAX_NODES)
	{
		return -1;
	}

	u32 index = graph->node_count++;
	graph->node_ids[index] = id;
	graph->node_positions[index] = pos;
	graph->node_sizes[index] = size;
	graph->node_input_count[index] = input_count;
	graph->node_output_count[index] = output_count;
	return (int)index;
}

RGINLINE u32 rg_gui_node_graph_remove_nodes(RgGuiNodeGraph* graph, const u8* remove_mask, u32 mask_count)
{
	return rg_gui_node_graph_remove_nodes_ex(graph, remove_mask, mask_count, NULL, 0u);
}

RGINLINE u32 rg_gui_node_graph_remove_nodes_ex(RgGuiNodeGraph* graph, const u8* remove_mask, u32 mask_count,
                                               int* out_remap, u32 remap_count)
{
	if (!graph || !remove_mask)
	{
		if (out_remap)
		{
			for (u32 i = 0u; i < remap_count; i++)
			{
				out_remap[i] = -1;
			}
		}
		return 0u;
	}

	u32 old_count = graph->node_count;
	if (old_count == 0u)
	{
		if (out_remap)
		{
			for (u32 i = 0u; i < remap_count; i++)
			{
				out_remap[i] = -1;
			}
		}
		return 0u;
	}

	int remap[RG_GUI_NODE_GRAPH_MAX_NODES];
	for (u32 i = 0u; i < old_count; i++)
	{
		remap[i] = -1;
	}

	u32 write = 0u;
	for (u32 i = 0u; i < old_count; i++)
	{
		int remove = (i < mask_count) ? (remove_mask[i] != 0u) : 0;
		if (remove)
		{
			continue;
		}

		remap[i] = (int)write;
		if (write != i)
		{
			graph->node_ids[write] = graph->node_ids[i];
			graph->node_positions[write] = graph->node_positions[i];
			graph->node_sizes[write] = graph->node_sizes[i];
			graph->node_input_count[write] = graph->node_input_count[i];
			graph->node_output_count[write] = graph->node_output_count[i];
		}
		write++;
	}

	graph->node_count = write;

	u32 link_write = 0u;
	for (u32 i = 0u; i < graph->link_count; i++)
	{
		u32 from_node = graph->link_from_node[i];
		u32 to_node = graph->link_to_node[i];
		if (from_node >= old_count || to_node >= old_count)
		{
			continue;
		}

		int mapped_from = remap[from_node];
		int mapped_to = remap[to_node];
		if (mapped_from < 0 || mapped_to < 0)
		{
			continue;
		}

		graph->link_from_node[link_write] = (u32)mapped_from;
		graph->link_from_port[link_write] = graph->link_from_port[i];
		graph->link_to_node[link_write] = (u32)mapped_to;
		graph->link_to_port[link_write] = graph->link_to_port[i];
		link_write++;
	}
	graph->link_count = link_write;

	if (out_remap)
	{
		u32 count = remap_count < old_count ? remap_count : old_count;
		for (u32 i = 0u; i < count; i++)
		{
			out_remap[i] = remap[i];
		}
		for (u32 i = count; i < remap_count; i++)
		{
			out_remap[i] = -1;
		}
	}

	return old_count - write;
}

RGINLINE void rg_gui_node_graph_bundle_reset(RgGuiNodeGraphBundle* bundle)
{
	if (!bundle)
	{
		return;
	}

	bundle->node_count = 0u;
	bundle->link_count = 0u;
	bundle->payload_bytes = 0u;
}

RGINLINE int rg_gui_node_graph_bundle_capture(const RgGuiNodeGraph* graph, const u8* selected,
                                              u32 selected_count, RgGuiNodeGraphBundle* out_bundle,
                                              RgGuiNodeGraphSerializeFn serialize, void* user)
{
	if (!graph || !out_bundle)
	{
		return 0;
	}

	rg_gui_node_graph_bundle_reset(out_bundle);

	if (graph->node_count == 0u)
	{
		return 0;
	}

	u8 captured[RG_GUI_NODE_GRAPH_MAX_NODES] = {0};
	int select_all = (selected == NULL);

	for (u32 i = 0u; i < graph->node_count; i++)
	{
		int is_selected = select_all ? 1 : (i < selected_count && selected[i] != 0u);
		if (!is_selected)
		{
			continue;
		}

		if (out_bundle->node_count >= RG_GUI_NODE_GRAPH_MAX_NODES)
		{
			break;
		}

		u32 write = out_bundle->node_count++;
		captured[i] = 1u;

		RgGuiNodeGraphNode* node = &out_bundle->nodes[write];
		node->id = graph->node_ids[i];
		node->position = graph->node_positions[i];
		node->size = graph->node_sizes[i];
		node->input_count = graph->node_input_count[i];
		node->output_count = graph->node_output_count[i];

		out_bundle->payload_offset[write] = 0u;
		out_bundle->payload_size[write] = 0u;

		if (serialize && out_bundle->payload && out_bundle->payload_capacity > out_bundle->payload_bytes)
		{
			u32 capacity = out_bundle->payload_capacity - out_bundle->payload_bytes;
			u32 written = serialize(user, node->id, out_bundle->payload + out_bundle->payload_bytes, capacity);
			if (written > capacity)
			{
				rg_gui_node_graph_bundle_reset(out_bundle);
				return 0;
			}

			out_bundle->payload_offset[write] = out_bundle->payload_bytes;
			out_bundle->payload_size[write] = written;
			out_bundle->payload_bytes += written;
		}
	}

	if (out_bundle->node_count == 0u)
	{
		return 0;
	}

	for (u32 i = 0u; i < graph->link_count; i++)
	{
		u32 from_node = graph->link_from_node[i];
		u32 to_node = graph->link_to_node[i];
		if (from_node >= graph->node_count || to_node >= graph->node_count)
		{
			continue;
		}
		if (!captured[from_node] || !captured[to_node])
		{
			continue;
		}
		if (out_bundle->link_count >= RG_GUI_NODE_GRAPH_MAX_LINKS)
		{
			break;
		}

		RgGuiNodeGraphLink link = {0};
		link.from_id = graph->node_ids[from_node];
		link.to_id = graph->node_ids[to_node];
		link.from_port = graph->link_from_port[i];
		link.to_port = graph->link_to_port[i];
		out_bundle->links[out_bundle->link_count++] = link;
	}

	return 1;
}

RGINLINE u32 rg_gui_node_graph_bundle_apply(RgGuiNodeGraph* graph, const RgGuiNodeGraphBundle* bundle,
                                            rg_vec2 offset, RgGuiNodeGraphAllocIdFn alloc_id,
                                            RgGuiNodeGraphDeserializeFn deserialize, void* user,
                                            RgGuiId* out_new_ids, u32 out_id_capacity,
                                            RgGuiNodeGraphLink* out_new_links, u32 out_link_capacity,
                                            u32* out_link_count)
{
	if (!graph || !bundle)
	{
		return 0;
	}
	if (out_new_ids && out_id_capacity > 0u)
	{
		memset(out_new_ids, 0, sizeof(RgGuiId) * out_id_capacity);
	}
	if (out_link_count)
	{
		*out_link_count = 0u;
	}
	if (graph->node_count > RG_GUI_NODE_GRAPH_MAX_NODES ||
	    graph->link_count > RG_GUI_NODE_GRAPH_MAX_LINKS ||
	    bundle->node_count == 0u || bundle->node_count > RG_GUI_NODE_GRAPH_MAX_NODES ||
	    bundle->link_count > RG_GUI_NODE_GRAPH_MAX_LINKS ||
	    bundle->node_count > RG_GUI_NODE_GRAPH_MAX_NODES - graph->node_count ||
	    bundle->link_count > RG_GUI_NODE_GRAPH_MAX_LINKS - graph->link_count ||
	    bundle->payload_bytes > bundle->payload_capacity)
	{
		return 0;
	}

	for (u32 i = 0u; i < bundle->node_count; i++)
	{
		u32 data_offset = bundle->payload_offset[i];
		u32 data_size = bundle->payload_size[i];
		if (data_offset > bundle->payload_bytes ||
		    data_size > bundle->payload_bytes - data_offset ||
		    (data_size > 0u && !bundle->payload))
		{
			return 0;
		}
	}

	RgGuiId old_ids[RG_GUI_NODE_GRAPH_MAX_NODES] = {0};
	RgGuiId new_ids[RG_GUI_NODE_GRAPH_MAX_NODES] = {0};
	int new_indices[RG_GUI_NODE_GRAPH_MAX_NODES];
	u8 mapped[RG_GUI_NODE_GRAPH_MAX_NODES] = {0};

	for (u32 i = 0u; i < bundle->node_count; i++)
	{
		old_ids[i] = bundle->nodes[i].id;
		new_indices[i] = -1;
	}

	u32 added = 0u;
	for (u32 i = 0u; i < bundle->node_count; i++)
	{
		if (graph->node_count >= RG_GUI_NODE_GRAPH_MAX_NODES)
		{
			break;
		}

		RgGuiId new_id = alloc_id ? alloc_id(user) : bundle->nodes[i].id;
		int conflict = (rg_gui_node_graph_find_node_index(graph, new_id) >= 0);
		if (!conflict)
		{
			for (u32 j = 0u; j < i; j++)
			{
				if (mapped[j] && new_ids[j] == new_id)
				{
					conflict = 1;
					break;
				}
			}
		}

		if (conflict)
		{
			continue;
		}

		rg_vec2 pos = rg_vec2(bundle->nodes[i].position.x + offset.x,
		                      bundle->nodes[i].position.y + offset.y);
		rg_vec2 size = bundle->nodes[i].size;

		int index = rg_gui_node_graph_add_node(graph, new_id, pos, size,
		                                       bundle->nodes[i].input_count,
		                                       bundle->nodes[i].output_count);
		if (index < 0)
		{
			continue;
		}

		new_ids[i] = new_id;
		new_indices[i] = index;
		mapped[i] = 1u;
		added++;

		if (out_new_ids && i < out_id_capacity)
		{
			out_new_ids[i] = new_id;
		}

		if (deserialize && bundle->payload && bundle->payload_size[i] > 0u)
		{
			u32 data_offset = bundle->payload_offset[i];
			u32 data_size = bundle->payload_size[i];
			deserialize(user, new_id, bundle->payload + data_offset, data_size);
		}
	}

	u32 link_added = 0u;
	for (u32 i = 0u; i < bundle->link_count; i++)
	{
		const RgGuiNodeGraphLink* link = &bundle->links[i];
		int from_index = -1;
		int to_index = -1;
		for (u32 n = 0u; n < bundle->node_count; n++)
		{
			if (!mapped[n])
			{
				continue;
			}
			if (old_ids[n] == link->from_id)
			{
				from_index = new_indices[n];
			}
			if (old_ids[n] == link->to_id)
			{
				to_index = new_indices[n];
			}
		}

		if (from_index < 0 || to_index < 0)
		{
			continue;
		}

		if (rg_gui_node_graph_add_link(graph,
		                               (u32)from_index, link->from_port,
		                               (u32)to_index, link->to_port))
		{
			if (out_new_links && link_added < out_link_capacity)
			{
				RgGuiNodeGraphLink new_link = {0};
				new_link.from_node = (u32)from_index;
				new_link.to_node = (u32)to_index;
				new_link.from_port = link->from_port;
				new_link.to_port = link->to_port;
				new_link.from_id = graph->node_ids[from_index];
				new_link.to_id = graph->node_ids[to_index];
				out_new_links[link_added] = new_link;
			}
			link_added++;
		}
	}

	if (out_link_count)
	{
		*out_link_count = link_added;
	}

	return added;
}

RGINLINE u32 rg_gui_node_graph_duplicate_selected(RgGuiNodeGraph* graph, const u8* selected,
                                                  u32 selected_count, rg_vec2 offset,
                                                  RgGuiNodeGraphBundle* scratch, RgGuiNodeGraphAllocIdFn alloc_id,
                                                  RgGuiNodeGraphSerializeFn serialize,
                                                  RgGuiNodeGraphDeserializeFn deserialize, void* user)
{
	if (!graph || !scratch)
	{
		return 0;
	}

	if (!rg_gui_node_graph_bundle_capture(graph, selected, selected_count, scratch, serialize, user))
	{
		return 0;
	}

	return rg_gui_node_graph_bundle_apply(graph, scratch, offset, alloc_id, deserialize, user,
	                                      NULL, 0u, NULL, 0u, NULL);
}

RGINLINE int rg_gui_node_graph_add_link(RgGuiNodeGraph* graph, u32 from_node, u16 from_port,
                                        u32 to_node, u16 to_port)
{
	if (!graph || graph->link_count >= RG_GUI_NODE_GRAPH_MAX_LINKS)
	{
		return 0;
	}
	if (from_node >= graph->node_count || to_node >= graph->node_count)
	{
		return 0;
	}
	if (from_node == to_node)
	{
		return 0;
	}
	if (from_port >= graph->node_output_count[from_node])
	{
		return 0;
	}
	if (to_port >= graph->node_input_count[to_node])
	{
		return 0;
	}

	for (u32 i = 0u; i < graph->link_count; i++)
	{
		if (graph->link_from_node[i] == from_node &&
		    graph->link_from_port[i] == from_port &&
		    graph->link_to_node[i] == to_node &&
		    graph->link_to_port[i] == to_port)
		{
			return 0;
		}
	}

	graph->link_from_node[graph->link_count] = from_node;
	graph->link_from_port[graph->link_count] = from_port;
	graph->link_to_node[graph->link_count] = to_node;
	graph->link_to_port[graph->link_count] = to_port;
	graph->link_count++;
	return 1;
}

RGINLINE int rg_gui_node_graph_remove_link_index(RgGuiNodeGraph* graph, u32 index)
{
	if (!graph || index >= graph->link_count)
	{
		return 0;
	}

	u32 last = graph->link_count - 1u;
	if (index != last)
	{
		graph->link_from_node[index] = graph->link_from_node[last];
		graph->link_from_port[index] = graph->link_from_port[last];
		graph->link_to_node[index] = graph->link_to_node[last];
		graph->link_to_port[index] = graph->link_to_port[last];
	}
	graph->link_count--;
	return 1;
}

RGINLINE int rg_gui_node_graph_rewire_link(RgGuiNodeGraph* graph, u32 index,
                                           u32 from_node, u16 from_port,
                                           u32 to_node, u16 to_port)
{
	if (!graph || index >= graph->link_count)
	{
		return 0;
	}
	if (from_node >= graph->node_count || to_node >= graph->node_count)
	{
		return 0;
	}
	if (from_node == to_node)
	{
		return 0;
	}
	if (from_port >= graph->node_output_count[from_node])
	{
		return 0;
	}
	if (to_port >= graph->node_input_count[to_node])
	{
		return 0;
	}

	for (u32 i = 0u; i < graph->link_count; i++)
	{
		if (i == index)
		{
			continue;
		}
		if (graph->link_from_node[i] == from_node &&
		    graph->link_from_port[i] == from_port &&
		    graph->link_to_node[i] == to_node &&
		    graph->link_to_port[i] == to_port)
		{
			return 0;
		}
	}

	graph->link_from_node[index] = from_node;
	graph->link_from_port[index] = from_port;
	graph->link_to_node[index] = to_node;
	graph->link_to_port[index] = to_port;
	return 1;
}

RGINLINE void rg_gui_node_graph_prune_links(RgGuiNodeGraph* graph)
{
	if (!graph)
	{
		return;
	}

	u32 write = 0u;
	for (u32 i = 0u; i < graph->link_count; i++)
	{
		u32 from_node = graph->link_from_node[i];
		u32 to_node = graph->link_to_node[i];
		if (from_node >= graph->node_count || to_node >= graph->node_count)
		{
			continue;
		}
		if (graph->link_from_port[i] >= graph->node_output_count[from_node])
		{
			continue;
		}
		if (graph->link_to_port[i] >= graph->node_input_count[to_node])
		{
			continue;
		}

		graph->link_from_node[write] = graph->link_from_node[i];
		graph->link_from_port[write] = graph->link_from_port[i];
		graph->link_to_node[write] = graph->link_to_node[i];
		graph->link_to_port[write] = graph->link_to_port[i];
		write++;
	}
	graph->link_count = write;
}

RGINLINE f32 rg_gui_node_graph_title_canvas_height(const RgGuiContext* ctx, const RgGuiNodeEditorState* editor,
                                                   f32 node_canvas_h)
{
	if (!ctx || !editor)
	{
		return 0.0f;
	}

	f32 zoom = editor->zoom;
	if (zoom <= 0.0f)
	{
		return 0.0f;
	}

	f32 base_title = ctx->style.text_height + ctx->style.padding * 2.0f;
	f32 title_h = base_title * zoom;
	f32 node_h = node_canvas_h * zoom;
	if (title_h > node_h)
	{
		title_h = node_h;
	}

	return title_h / zoom;
}

RGINLINE RgGuiNodeGraphLink rg_gui_node_graph_link_make(const RgGuiNodeGraph* graph,
                                                        u32 from_node, u16 from_port,
                                                        u32 to_node, u16 to_port)
{
	RgGuiNodeGraphLink link;
	link.from_node = from_node;
	link.from_port = from_port;
	link.pad0 = 0u;
	link.to_node = to_node;
	link.to_port = to_port;
	link.pad1 = 0u;
	link.from_id = (graph && from_node < graph->node_count) ? graph->node_ids[from_node] : 0u;
	link.to_id = (graph && to_node < graph->node_count) ? graph->node_ids[to_node] : 0u;
	return link;
}

RGINLINE RgGuiNodeGraphLink rg_gui_node_graph_link_to_event(const RgGuiNodeGraph* graph, u32 index)
{
	if (!graph || index >= graph->link_count)
	{
		RgGuiNodeGraphLink empty = {0};
		return empty;
	}

	return rg_gui_node_graph_link_make(graph,
	                                   graph->link_from_node[index],
	                                   graph->link_from_port[index],
	                                   graph->link_to_node[index],
	                                   graph->link_to_port[index]);
}

RGINLINE u32 rg_gui_node_graph_remove_links_for_port(RgGuiNodeGraph* graph, u32 node, u16 port,
                                                     int output, RgGuiNodeGraphLink* out_links, u32 max_links)
{
	if (!graph || node >= graph->node_count)
	{
		return 0u;
	}

	u32 removed = 0u;
	u32 write = 0u;
	for (u32 i = 0u; i < graph->link_count; i++)
	{
		int match = output ? (graph->link_from_node[i] == node && graph->link_from_port[i] == port) : (graph->link_to_node[i] == node && graph->link_to_port[i] == port);
		if (match)
		{
			if (out_links && removed < max_links)
			{
				out_links[removed] = rg_gui_node_graph_link_to_event(graph, i);
			}
			removed++;
			continue;
		}

		if (write != i)
		{
			graph->link_from_node[write] = graph->link_from_node[i];
			graph->link_from_port[write] = graph->link_from_port[i];
			graph->link_to_node[write] = graph->link_to_node[i];
			graph->link_to_port[write] = graph->link_to_port[i];
		}
		write++;
	}
	graph->link_count = write;
	return removed;
}

RGINLINE f32 rg_gui_node_graph_port_size(f32 zoom)
{
	f32 size = 6.0f * zoom;
	if (size < 6.0f)
	{
		size = 6.0f;
	}
	return size;
}

RGINLINE rg_vec2 rg_gui_node_graph_port_canvas_pos(const RgGuiContext* ctx, const RgGuiNodeEditorState* editor,
                                                   rg_vec2 node_pos, rg_vec2 node_size,
                                                   u32 port_index, u32 port_count, int output)
{
	f32 y = node_size.y * 0.5f;
	if (port_count > 0u)
	{
		f32 title_h = rg_gui_node_graph_title_canvas_height(ctx, editor, node_size.y);
		f32 body_h = node_size.y - title_h;
		if (body_h > 0.0f)
		{
			f32 step = body_h / (f32)(port_count + 1u);
			y = title_h + step * (f32)(port_index + 1u);
		}
	}

	rg_vec2 pos = node_pos;
	pos.x += output ? node_size.x : 0.0f;
	pos.y += y;
	return pos;
}

RGINLINE RgGuiRect rg_gui_node_graph_port_screen_rect(const RgGuiNodeEditorState* editor, rg_vec2 canvas_pos)
{
	f32 size = rg_gui_node_graph_port_size(editor ? editor->zoom : 1.0f);
	rg_vec2 screen = rg_gui_node_editor_to_screen(editor, canvas_pos);
	return rg_gui_make_rect(screen.x - size * 0.5f,
	                        screen.y - size * 0.5f,
	                        size, size);
}

RGINLINE void rg_gui_node_graph_draw_port(RgGuiContext* ctx, RgGuiRect rect, int output, int highlighted)
{
	if (!ctx)
	{
		return;
	}

	rg_vec4 fill = output ? ctx->style.color_accent : ctx->style.color_bg_hover;
	if (highlighted && !output)
	{
		fill = ctx->style.color_bg_active;
	}
	rg_vec4 outline = highlighted ? ctx->style.color_accent : ctx->style.color_border;
	rg_gui_push_rect(ctx, rect, fill);
	rg_gui_push_rect_outline(ctx, rect, outline, ctx->style.border_thickness);
}

RGINLINE f32 rg_gui_node_graph_segment_distance_sq(rg_vec2 p, rg_vec2 a, rg_vec2 b)
{
	f32 abx = b.x - a.x;
	f32 aby = b.y - a.y;
	f32 apx = p.x - a.x;
	f32 apy = p.y - a.y;
	f32 ab_len = abx * abx + aby * aby;
	f32 t = 0.0f;
	if (ab_len > 0.0f)
	{
		t = (apx * abx + apy * aby) / ab_len;
		if (t < 0.0f)
		{
			t = 0.0f;
		}
		else if (t > 1.0f)
		{
			t = 1.0f;
		}
	}
	f32 cx = a.x + abx * t;
	f32 cy = a.y + aby * t;
	f32 dx = p.x - cx;
	f32 dy = p.y - cy;
	return dx * dx + dy * dy;
}

RGINLINE f32 rg_gui_node_graph_link_distance_sq(rg_vec2 p, rg_vec2 a, rg_vec2 b)
{
	f32 mid_x = a.x + (b.x - a.x) * 0.5f;
	rg_vec2 elbow_a = rg_vec2(mid_x, a.y);
	rg_vec2 elbow_b = rg_vec2(mid_x, b.y);
	f32 d0 = rg_gui_node_graph_segment_distance_sq(p, a, elbow_a);
	f32 d1 = rg_gui_node_graph_segment_distance_sq(p, elbow_a, elbow_b);
	f32 d2 = rg_gui_node_graph_segment_distance_sq(p, elbow_b, b);
	f32 best = d0 < d1 ? d0 : d1;
	return best < d2 ? best : d2;
}

RGINLINE f32 rg_gui_node_graph_bezier_distance_sq(rg_vec2 p, rg_vec2 a, rg_vec2 b)
{
	f32 dx = rg_maxf(40.0f, rg_absf(b.x - a.x) * 0.5f);
	rg_vec2 c1 = rg_vec2(a.x + dx, a.y);
	rg_vec2 c2 = rg_vec2(b.x - dx, b.y);
	const u32 segments = 12u;
	rg_vec2 prev = a;
	f32 best = -1.0f;
	for (u32 i = 1u; i <= segments; i++)
	{
		f32 t = (f32)i / (f32)segments;
		rg_vec2 point = rg_gui_node_bezier_point(a, c1, c2, b, t);
		f32 dist = rg_gui_node_graph_segment_distance_sq(p, prev, point);
		if (best < 0.0f || dist < best)
		{
			best = dist;
		}
		prev = point;
	}
	return best < 0.0f ? 0.0f : best;
}

RGINLINE f32 rg_gui_node_graph_routed_link_distance_sq(const RgGuiNodeEditorState* editor,
                                                       rg_vec2 p, rg_vec2 a, rg_vec2 b)
{
	if (editor && editor->link_style == RG_GUI_NODE_LINK_STYLE_BEZIER)
	{
		return rg_gui_node_graph_bezier_distance_sq(p, a, b);
	}
	return rg_gui_node_graph_link_distance_sq(p, a, b);
}

RGINLINE int rg_gui_node_graph_find_link_for_port_ex(const RgGuiNodeGraph* graph, const RgGuiContext* ctx,
                                                     const RgGuiNodeEditorState* editor, RgGuiNodePortRef port,
                                                     rg_vec2 mouse_screen, const u32* candidates,
                                                     u32 candidate_count)
{
	if (!graph || !ctx || !editor)
	{
		return -1;
	}

	if (port.node >= graph->node_count)
	{
		return -1;
	}

	u32 port_count = port.output ? graph->node_output_count[port.node] : graph->node_input_count[port.node];
	if (port.port >= port_count || port_count == 0u)
	{
		return -1;
	}

	rg_vec2 port_canvas = rg_gui_node_graph_port_canvas_pos(ctx, editor,
	                                                        graph->node_positions[port.node],
	                                                        graph->node_sizes[port.node],
	                                                        port.port, port_count, port.output);
	rg_vec2 port_screen = rg_gui_node_editor_to_screen(editor, port_canvas);
	rg_vec2 mouse_vec = rg_vec2(mouse_screen.x - port_screen.x, mouse_screen.y - port_screen.y);
	f32 mouse_len_sq = mouse_vec.x * mouse_vec.x + mouse_vec.y * mouse_vec.y;

	f32 best_align = -2.0f;
	f32 best_dist = -1.0f;
	int best_index = -1;

	u32 loop_count = candidates ? candidate_count : graph->link_count;
	for (u32 c = 0u; c < loop_count; c++)
	{
		u32 i = candidates ? candidates[c] : c;
		if (i >= graph->link_count)
		{
			continue;
		}

		int match = port.output ? (graph->link_from_node[i] == port.node && graph->link_from_port[i] == port.port) : (graph->link_to_node[i] == port.node && graph->link_to_port[i] == port.port);
		if (!match)
		{
			continue;
		}

		u32 from_node = graph->link_from_node[i];
		u32 to_node = graph->link_to_node[i];
		if (from_node >= graph->node_count || to_node >= graph->node_count)
		{
			continue;
		}
		if (graph->link_from_port[i] >= graph->node_output_count[from_node])
		{
			continue;
		}
		if (graph->link_to_port[i] >= graph->node_input_count[to_node])
		{
			continue;
		}

		rg_vec2 from_canvas = rg_gui_node_graph_port_canvas_pos(ctx, editor,
		                                                        graph->node_positions[from_node],
		                                                        graph->node_sizes[from_node],
		                                                        graph->link_from_port[i],
		                                                        graph->node_output_count[from_node], 1);
		rg_vec2 to_canvas = rg_gui_node_graph_port_canvas_pos(ctx, editor,
		                                                      graph->node_positions[to_node],
		                                                      graph->node_sizes[to_node],
		                                                      graph->link_to_port[i],
		                                                      graph->node_input_count[to_node], 0);
		rg_vec2 from_screen = rg_gui_node_editor_to_screen(editor, from_canvas);
		rg_vec2 to_screen = rg_gui_node_editor_to_screen(editor, to_canvas);
		rg_vec2 other = port.output ? to_screen : from_screen;
		f32 dist = rg_gui_node_graph_routed_link_distance_sq(editor, mouse_screen, from_screen, to_screen);

		f32 align = 0.0f;
		if (mouse_len_sq > 4.0f)
		{
			rg_vec2 dir = rg_vec2(other.x - port_screen.x, other.y - port_screen.y);
			f32 dir_len_sq = dir.x * dir.x + dir.y * dir.y;
			if (dir_len_sq > 0.0f)
			{
				f32 denom = sqrtf(mouse_len_sq * dir_len_sq);
				if (denom > 0.0f)
				{
					align = (mouse_vec.x * dir.x + mouse_vec.y * dir.y) / denom;
				}
			}
		}

		if (mouse_len_sq > 4.0f)
		{
			if (best_index < 0 || align > best_align || (align == best_align && dist < best_dist))
			{
				best_align = align;
				best_dist = dist;
				best_index = (int)i;
			}
		}
		else
		{
			if (best_index < 0 || dist < best_dist)
			{
				best_dist = dist;
				best_index = (int)i;
			}
		}
	}

	return best_index;
}

RGINLINE int rg_gui_node_graph_find_link_for_port(const RgGuiNodeGraph* graph, const RgGuiContext* ctx,
                                                  const RgGuiNodeEditorState* editor, RgGuiNodePortRef port,
                                                  rg_vec2 mouse_screen)
{
	return rg_gui_node_graph_find_link_for_port_ex(graph, ctx, editor, port, mouse_screen, NULL, 0u);
}

RGINLINE u32 rg_gui_node_graph_hit_cell_hash(int x, int y)
{
	u32 ux = (u32)x;
	u32 uy = (u32)y;
	u32 h = (ux * 73856093u) ^ (uy * 19349663u);
	return h % RG_GUI_NODE_GRAPH_HIT_CELL_COUNT;
}

RGINLINE int rg_gui_node_graph_editor_ex(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiNodeGraphState* state,
                                         RgGuiNodeGraph* graph, const RgGuiNodeGraphDraw* draw,
                                         const RgGuiNodeGraphSelection* selection, const RgGuiNodeGraphCallbacks* callbacks,
                                         const RgGuiNodeGraphGroups* groups,
                                         RgGuiRect rect, RgGuiId id, void* user)
{
	if (!ctx || !editor || !state || !graph)
	{
		return 0;
	}

	int hovered = rg_gui_node_editor_begin(ctx, editor, rect, id);

	state->consume_right = 0;
	state->link_scratch_count = 0u;

	{
		const f32 minor_spacing = 32.0f;
		const f32 major_spacing = 128.0f;
		f32 minor_px = minor_spacing * editor->zoom;
		f32 major_px = major_spacing * editor->zoom;
		f32 min_px = 8.0f;

		if (minor_px >= min_px || major_px >= min_px)
		{
			RgGuiRect grid_rect = editor->rect;
			rg_vec4 minor_color = ctx->style.color_border;
			rg_vec4 major_color = ctx->style.color_border;
			f32 grid_gray = (minor_color.x + minor_color.y + minor_color.z) * 0.33333334f;
			grid_gray = rg_clampf(grid_gray * 0.62f, 0.105f, 0.180f);
			minor_color.x = grid_gray;
			minor_color.y = grid_gray;
			minor_color.z = grid_gray;
			major_color.x = rg_minf(grid_gray + 0.045f, 1.0f);
			major_color.y = major_color.x;
			major_color.z = major_color.x;
			minor_color.w = rg_clampf(minor_color.w * 0.115f, 0.075f, 0.140f);
			major_color.w = rg_clampf(major_color.w * 0.180f, 0.120f, 0.220f);

			if (minor_px >= min_px)
			{
				f32 offset_x = fmodf(editor->pan.x, minor_px);
				f32 offset_y = fmodf(editor->pan.y, minor_px);
				if (offset_x < 0.0f) offset_x += minor_px;
				if (offset_y < 0.0f) offset_y += minor_px;

				f32 x = grid_rect.x + offset_x;
				f32 y = grid_rect.y + offset_y;
				f32 end_x = grid_rect.x + grid_rect.w;
				f32 end_y = grid_rect.y + grid_rect.h;

				if (x <= grid_rect.x + 0.5f) x += minor_px;
				if (y <= grid_rect.y + 0.5f) y += minor_px;

				for (; x < end_x; x += minor_px)
				{
					rg_gui_push_line(ctx, rg_vec2(x, grid_rect.y),
					                 rg_vec2(x, grid_rect.y + grid_rect.h), 1.0f, minor_color);
				}
				for (; y < end_y; y += minor_px)
				{
					rg_gui_push_line(ctx, rg_vec2(grid_rect.x, y),
					                 rg_vec2(grid_rect.x + grid_rect.w, y), 1.0f, minor_color);
				}
			}

			if (major_px >= min_px)
			{
				f32 offset_x = fmodf(editor->pan.x, major_px);
				f32 offset_y = fmodf(editor->pan.y, major_px);
				if (offset_x < 0.0f) offset_x += major_px;
				if (offset_y < 0.0f) offset_y += major_px;

				f32 x = grid_rect.x + offset_x;
				f32 y = grid_rect.y + offset_y;
				f32 end_x = grid_rect.x + grid_rect.w;
				f32 end_y = grid_rect.y + grid_rect.h;

				if (x <= grid_rect.x + 0.5f) x += major_px;
				if (y <= grid_rect.y + 0.5f) y += major_px;

				for (; x < end_x; x += major_px)
				{
					rg_gui_push_line(ctx, rg_vec2(x, grid_rect.y),
					                 rg_vec2(x, grid_rect.y + grid_rect.h), 1.0f, major_color);
				}
				for (; y < end_y; y += major_px)
				{
					rg_gui_push_line(ctx, rg_vec2(grid_rect.x, y),
					                 rg_vec2(grid_rect.x + grid_rect.w, y), 1.0f, major_color);
				}
			}
		}
	}

	if (groups && groups->state)
	{
		RgGuiNodeGroupState* group_state = groups->state;
		group_state->hovered_index = -1;
		group_state->clicked_index = -1;

		if (groups->groups && groups->count > 0u)
		{
			for (u32 i = 0u; i < groups->count; i++)
			{
				RgGuiNodeGroup* group = &groups->groups[i];
				const char* title = groups->title ? groups->title(user, i) : NULL;
				RgGuiNodeGroupResult group_result = rg_gui_node_group(ctx, editor, group_state, group, title, i);

				if (group_result.started && group_result.mode == RG_GUI_NODE_GROUP_MODE_MOVE)
				{
					group_state->move_count = 0u;
					if (group->member_count > 0u)
					{
						for (u32 m = 0u; m < group->member_count; m++)
						{
							int index = rg_gui_node_graph_find_node_index(graph, group->members[m]);
							if (index < 0)
							{
								continue;
							}
							if (group_state->move_count >= RG_GUI_NODE_GRAPH_MAX_NODES)
							{
								break;
							}
							u32 write = group_state->move_count++;
							group_state->move_indices[write] = (u32)index;
							group_state->move_ids[write] = graph->node_ids[index];
							group_state->move_from[write] = graph->node_positions[index];
						}
					}
				}

				if (group_result.active && group_result.mode == RG_GUI_NODE_GROUP_MODE_MOVE &&
				    group_state->active_index == (int)i && ctx->active_id == group_state->active_id)
				{
					rg_vec2 delta =
					    {
					        .x = group->position.x - group_state->drag_start_pos.x,
					        .y = group->position.y - group_state->drag_start_pos.y};

					if (delta.x != 0.0f || delta.y != 0.0f)
					{
						for (u32 m = 0u; m < group_state->move_count; m++)
						{
							u32 idx = group_state->move_indices[m];
							if (idx >= graph->node_count)
							{
								continue;
							}
							graph->node_positions[idx].x = group_state->move_from[m].x + delta.x;
							graph->node_positions[idx].y = group_state->move_from[m].y + delta.y;
						}
					}
				}

				if (group_result.ended && group_result.mode == RG_GUI_NODE_GROUP_MODE_MOVE)
				{
					u32 moved_count = 0u;
					for (u32 m = 0u; m < group_state->move_count; m++)
					{
						u32 idx = group_state->move_indices[m];
						if (idx >= graph->node_count)
						{
							continue;
						}
						rg_vec2 after = graph->node_positions[idx];
						rg_vec2 before = group_state->move_from[m];
						if (after.x != before.x || after.y != before.y)
						{
							group_state->move_indices[moved_count] = idx;
							group_state->move_ids[moved_count] = group_state->move_ids[m];
							group_state->move_from[moved_count] = before;
							group_state->move_to[moved_count] = after;
							moved_count++;
						}
					}

					if (moved_count > 0u && callbacks && callbacks->on_node_move)
					{
						RgGuiNodeGraphMove move =
						    {
						        moved_count,
						        group_state->move_indices,
						        group_state->move_ids,
						        group_state->move_from,
						        group_state->move_to};
						callbacks->on_node_move(user, &move);
					}

					group_state->move_count = 0u;
				}
			}
		}
	}

	int enabled = !rg_gui_is_disabled(ctx);
	int left_pressed = ctx->input && rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_LEFT);
	int left_down = ctx->input && rg_input_is_mouse_button_down(ctx->input, RG_MOUSE_BUTTON_LEFT);
	int right_pressed = ctx->input && rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_RIGHT);
	state->hovered_link_index = -1;
	state->clicked_link_index = -1;
	if (state->selected_link_index >= (int)graph->link_count)
	{
		state->selected_link_index = -1;
	}

	RgGuiRect view_rect = editor->rect;
	f32 port_size = rg_gui_node_graph_port_size(editor->zoom);
	f32 hit_pad = rg_maxf(2.0f, port_size);
	f32 cell_size = RG_GUI_NODE_GRAPH_HIT_CELL_SIZE;
	if (cell_size < 4.0f)
	{
		cell_size = 4.0f;
	}
	f32 inv_cell = (cell_size > 0.0f) ? (1.0f / cell_size) : 0.0f;
	int max_cell_x = 0;
	int max_cell_y = 0;
	if (view_rect.w > 0.0f && view_rect.h > 0.0f)
	{
		max_cell_x = (int)floorf((view_rect.w - 1.0f) * inv_cell);
		max_cell_y = (int)floorf((view_rect.h - 1.0f) * inv_cell);
		if (max_cell_x < 0) max_cell_x = 0;
		if (max_cell_y < 0) max_cell_y = 0;
	}

	RgGuiRect node_rects[RG_GUI_NODE_GRAPH_MAX_NODES] = {0};
	RgGuiRect node_hit_rects[RG_GUI_NODE_GRAPH_MAX_NODES] = {0};
	u8 node_hot[RG_GUI_NODE_GRAPH_MAX_NODES] = {0};

	int node_cell_head[RG_GUI_NODE_GRAPH_HIT_CELL_COUNT];
	int node_entry_next[RG_GUI_NODE_GRAPH_HIT_NODE_ENTRIES];
	u32 node_entry_index[RG_GUI_NODE_GRAPH_HIT_NODE_ENTRIES];
	u32 node_entry_count = 0u;
	u32 node_overflow[RG_GUI_NODE_GRAPH_MAX_NODES];
	u32 node_overflow_count = 0u;

	int link_cell_head[RG_GUI_NODE_GRAPH_HIT_CELL_COUNT];
	int link_entry_next[RG_GUI_NODE_GRAPH_MAX_LINKS * 2u];
	u32 link_entry_index[RG_GUI_NODE_GRAPH_MAX_LINKS * 2u];
	u32 link_entry_count = 0u;
	u32 link_overflow[RG_GUI_NODE_GRAPH_MAX_LINKS];
	u32 link_overflow_count = 0u;

	u32 link_candidates[RG_GUI_NODE_GRAPH_MAX_LINKS];
	u8 link_mark[RG_GUI_NODE_GRAPH_MAX_LINKS] = {0};

	for (u32 i = 0u; i < RG_GUI_NODE_GRAPH_HIT_CELL_COUNT; i++)
	{
		node_cell_head[i] = -1;
	}

	int mouse_in_view = enabled &&
	                    rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, view_rect);
	int use_node_grid = (RG_GUI_NODE_GRAPH_HIT_CELL_COUNT > 0u &&
	                     view_rect.w > 0.0f && view_rect.h > 0.0f);

	const RgGuiSelectionOps* selection_ops = selection ? selection->ops : NULL;
	RgGuiSelectionState* selection_state = selection ? selection->state : NULL;
	void* selection_user = selection ? selection->user : NULL;
	int* primary = selection ? selection->primary : NULL;

	if (graph->node_count > 0u)
	{
		for (u32 i = 0u; i < graph->node_count; i++)
		{
			RgGuiRect node_rect = rg_gui_node_graph_node_screen_rect(ctx, editor,
			                                                         graph->node_positions[i],
			                                                         graph->node_sizes[i], NULL);
			node_rects[i] = node_rect;

			RgGuiRect hit_rect = node_rect;
			hit_rect.x -= hit_pad;
			hit_rect.y -= hit_pad;
			hit_rect.w += hit_pad * 2.0f;
			hit_rect.h += hit_pad * 2.0f;
			node_hit_rects[i] = hit_rect;

			if (use_node_grid &&
			    rg_gui_rects_overlap(hit_rect, view_rect))
			{
				int min_cx = (int)floorf((hit_rect.x - view_rect.x) * inv_cell);
				int max_cx = (int)floorf((hit_rect.x + hit_rect.w - view_rect.x) * inv_cell);
				int min_cy = (int)floorf((hit_rect.y - view_rect.y) * inv_cell);
				int max_cy = (int)floorf((hit_rect.y + hit_rect.h - view_rect.y) * inv_cell);

				if (min_cx < 0) min_cx = 0;
				if (min_cy < 0) min_cy = 0;
				if (max_cx > max_cell_x) max_cx = max_cell_x;
				if (max_cy > max_cell_y) max_cy = max_cell_y;

				if (max_cx >= min_cx && max_cy >= min_cy)
				{
					u32 cells_needed =
					    (u32)(max_cx - min_cx + 1) * (u32)(max_cy - min_cy + 1);
					if (node_entry_count + cells_needed <= RG_GUI_NODE_GRAPH_HIT_NODE_ENTRIES)
					{
						for (int cy = min_cy; cy <= max_cy; cy++)
						{
							for (int cx = min_cx; cx <= max_cx; cx++)
							{
								u32 cell = rg_gui_node_graph_hit_cell_hash(cx, cy);
								node_entry_index[node_entry_count] = i;
								node_entry_next[node_entry_count] = node_cell_head[cell];
								node_cell_head[cell] = (int)node_entry_count;
								node_entry_count++;
							}
						}
					}
					else if (node_overflow_count < RG_GUI_NODE_GRAPH_MAX_NODES)
					{
						node_overflow[node_overflow_count++] = i;
					}
				}
			}
		}
	}

	int mouse_cell_valid = 0;
	u32 mouse_cell = 0u;
	if (mouse_in_view && use_node_grid)
	{
		int mouse_cx = (int)floorf((ctx->mouse_pos.x - view_rect.x) * inv_cell);
		int mouse_cy = (int)floorf((ctx->mouse_pos.y - view_rect.y) * inv_cell);
		if (mouse_cx >= 0 && mouse_cx <= max_cell_x &&
		    mouse_cy >= 0 && mouse_cy <= max_cell_y)
		{
			mouse_cell = rg_gui_node_graph_hit_cell_hash(mouse_cx, mouse_cy);
			mouse_cell_valid = 1;
			for (int entry = node_cell_head[mouse_cell]; entry >= 0; entry = node_entry_next[entry])
			{
				u32 idx = node_entry_index[entry];
				if (node_hot[idx])
				{
					continue;
				}
				if (rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, node_hit_rects[idx]))
				{
					node_hot[idx] = 1u;
				}
			}
		}

		for (u32 i = 0u; i < node_overflow_count; i++)
		{
			u32 idx = node_overflow[i];
			if (node_hot[idx])
			{
				continue;
			}
			if (rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, node_hit_rects[idx]))
			{
				node_hot[idx] = 1u;
			}
		}
	}

	int mouse_over_node = 0;
	for (u32 i = 0u; i < graph->node_count; i++)
	{
		if (node_hot[i])
		{
			mouse_over_node = 1;
			break;
		}
	}

	RgGuiId port_drag_id = rg_gui_id_combine(editor->id, 0x504F5254u);
	RgGuiId link_select_id = rg_gui_id_combine(editor->id, 0x4C494E4Bu);
	RgGuiNodePortRef hovered_port = {0};
	int port_hovered = 0;
	int was_dragging = state->drag_active;
	int drag_index = -1;

	int node_drag_in_progress = state->drag_active && !state->link_drag_active;
	int use_link_grid = use_node_grid && !node_drag_in_progress;
	f32 link_thickness = rg_maxf(1.0f, ctx->style.border_thickness);
	f32 link_hit_radius = rg_maxf(7.0f, 8.0f * editor->zoom);
	f32 link_hit_radius_sq = link_hit_radius * link_hit_radius;
	f32 best_link_dist_sq = -1.0f;
	int best_link_index = -1;
	if (use_link_grid)
	{
		for (u32 i = 0u; i < RG_GUI_NODE_GRAPH_HIT_CELL_COUNT; i++)
		{
			link_cell_head[i] = -1;
		}
	}

	for (u32 i = 0u; i < graph->link_count; i++)
	{
		if (state->link_drag_active && state->link_drag_index == (int)i)
		{
			continue;
		}

		u32 from_node = graph->link_from_node[i];
		u32 to_node = graph->link_to_node[i];
		if (from_node >= graph->node_count || to_node >= graph->node_count)
		{
			continue;
		}
		if (graph->link_from_port[i] >= graph->node_output_count[from_node])
		{
			continue;
		}
		if (graph->link_to_port[i] >= graph->node_input_count[to_node])
		{
			continue;
		}

		rg_vec2 link_a = rg_gui_node_graph_port_canvas_pos(ctx, editor,
		                                                   graph->node_positions[from_node],
		                                                   graph->node_sizes[from_node],
		                                                   graph->link_from_port[i],
		                                                   graph->node_output_count[from_node], 1);
		rg_vec2 link_b = rg_gui_node_graph_port_canvas_pos(ctx, editor,
		                                                   graph->node_positions[to_node],
		                                                   graph->node_sizes[to_node],
		                                                   graph->link_to_port[i],
		                                                   graph->node_input_count[to_node], 0);

		rg_vec2 from_screen = rg_gui_node_editor_to_screen(editor, link_a);
		rg_vec2 to_screen = rg_gui_node_editor_to_screen(editor, link_b);
		f32 mid_x = from_screen.x + (to_screen.x - from_screen.x) * 0.5f;
		rg_vec2 elbow_a = rg_vec2(mid_x, from_screen.y);
		rg_vec2 elbow_b = rg_vec2(mid_x, to_screen.y);
		int use_bezier_link = editor->link_style == RG_GUI_NODE_LINK_STYLE_BEZIER;

		f32 min_x = from_screen.x;
		f32 max_x = from_screen.x;
		f32 min_y = from_screen.y;
		f32 max_y = from_screen.y;
		if (to_screen.x < min_x) min_x = to_screen.x;
		if (to_screen.x > max_x) max_x = to_screen.x;
		if (to_screen.y < min_y) min_y = to_screen.y;
		if (to_screen.y > max_y) max_y = to_screen.y;
		if (use_bezier_link)
		{
			f32 dx = rg_maxf(40.0f, rg_absf(to_screen.x - from_screen.x) * 0.5f);
			rg_vec2 c1 = rg_vec2(from_screen.x + dx, from_screen.y);
			rg_vec2 c2 = rg_vec2(to_screen.x - dx, to_screen.y);
			if (c1.x < min_x) min_x = c1.x;
			if (c1.x > max_x) max_x = c1.x;
			if (c1.y < min_y) min_y = c1.y;
			if (c1.y > max_y) max_y = c1.y;
			if (c2.x < min_x) min_x = c2.x;
			if (c2.x > max_x) max_x = c2.x;
			if (c2.y < min_y) min_y = c2.y;
			if (c2.y > max_y) max_y = c2.y;
		}
		else
		{
			if (elbow_a.x < min_x) min_x = elbow_a.x;
			if (elbow_a.x > max_x) max_x = elbow_a.x;
			if (elbow_a.y < min_y) min_y = elbow_a.y;
			if (elbow_a.y > max_y) max_y = elbow_a.y;
			if (elbow_b.x < min_x) min_x = elbow_b.x;
			if (elbow_b.x > max_x) max_x = elbow_b.x;
			if (elbow_b.y < min_y) min_y = elbow_b.y;
			if (elbow_b.y > max_y) max_y = elbow_b.y;
		}

		RgGuiRect link_rect = rg_gui_make_rect(min_x - link_thickness,
		                                       min_y - link_thickness,
		                                       (max_x - min_x) + link_thickness * 2.0f,
		                                       (max_y - min_y) + link_thickness * 2.0f);
		if (rg_gui_rects_overlap(link_rect, view_rect))
		{
			rg_vec4 wire = ctx->style.color_accent;
			wire.w *= 0.40f;
			rg_gui_node_link(ctx, editor, link_a, link_b, wire);
		}

		if (enabled && mouse_in_view && !mouse_over_node &&
		    !node_drag_in_progress && !state->link_drag_active)
		{
			f32 dist_sq = rg_gui_node_graph_routed_link_distance_sq(
			    editor, ctx->mouse_pos, from_screen, to_screen);
			if (dist_sq <= link_hit_radius_sq &&
			    (best_link_index < 0 || dist_sq < best_link_dist_sq))
			{
				best_link_dist_sq = dist_sq;
				best_link_index = (int)i;
			}
		}

		if (use_link_grid)
		{
			int from_in = (from_screen.x >= view_rect.x - hit_pad &&
			               from_screen.x <= view_rect.x + view_rect.w + hit_pad &&
			               from_screen.y >= view_rect.y - hit_pad &&
			               from_screen.y <= view_rect.y + view_rect.h + hit_pad);
			int to_in = (to_screen.x >= view_rect.x - hit_pad &&
			             to_screen.x <= view_rect.x + view_rect.w + hit_pad &&
			             to_screen.y >= view_rect.y - hit_pad &&
			             to_screen.y <= view_rect.y + view_rect.h + hit_pad);
			u32 needed = (from_in ? 1u : 0u) + (to_in ? 1u : 0u);
			if (needed > 0u)
			{
				if (link_entry_count + needed > (RG_GUI_NODE_GRAPH_MAX_LINKS * 2u))
				{
					if (link_overflow_count < RG_GUI_NODE_GRAPH_MAX_LINKS)
					{
						link_overflow[link_overflow_count++] = i;
					}
				}
				else
				{
					if (from_in)
					{
						int cx = (int)floorf((from_screen.x - view_rect.x) * inv_cell);
						int cy = (int)floorf((from_screen.y - view_rect.y) * inv_cell);
						if (cx >= 0 && cx <= max_cell_x && cy >= 0 && cy <= max_cell_y)
						{
							u32 cell = rg_gui_node_graph_hit_cell_hash(cx, cy);
							link_entry_index[link_entry_count] = i;
							link_entry_next[link_entry_count] = link_cell_head[cell];
							link_cell_head[cell] = (int)link_entry_count;
							link_entry_count++;
						}
					}
					if (to_in)
					{
						int cx = (int)floorf((to_screen.x - view_rect.x) * inv_cell);
						int cy = (int)floorf((to_screen.y - view_rect.y) * inv_cell);
						if (cx >= 0 && cx <= max_cell_x && cy >= 0 && cy <= max_cell_y)
						{
							u32 cell = rg_gui_node_graph_hit_cell_hash(cx, cy);
							link_entry_index[link_entry_count] = i;
							link_entry_next[link_entry_count] = link_cell_head[cell];
							link_cell_head[cell] = (int)link_entry_count;
							link_entry_count++;
						}
					}
				}
			}
		}
	}

	if (best_link_index >= 0)
	{
		state->hovered_link_index = best_link_index;
		editor->node_hovered = 1;
		ctx->hot_id = link_select_id;
		if (left_pressed && (ctx->active_id == 0u || ctx->active_id == link_select_id))
		{
			ctx->active_id = link_select_id;
			ctx->focus_id = editor->id;
			state->clicked_link_index = best_link_index;
			state->selected_link_index = best_link_index;
		}
	}

	if (ctx->active_id == link_select_id && !left_down)
	{
		ctx->active_id = 0u;
	}

	if (state->selected_link_index >= 0 || state->hovered_link_index >= 0)
	{
		for (u32 i = 0u; i < graph->link_count; i++)
		{
			if ((int)i != state->selected_link_index &&
			    (int)i != state->hovered_link_index)
			{
				continue;
			}
			if (state->link_drag_active && state->link_drag_index == (int)i)
			{
				continue;
			}

			u32 from_node = graph->link_from_node[i];
			u32 to_node = graph->link_to_node[i];
			if (from_node >= graph->node_count || to_node >= graph->node_count)
			{
				continue;
			}
			if (graph->link_from_port[i] >= graph->node_output_count[from_node])
			{
				continue;
			}
			if (graph->link_to_port[i] >= graph->node_input_count[to_node])
			{
				continue;
			}

			rg_vec2 link_a = rg_gui_node_graph_port_canvas_pos(ctx, editor,
			                                                   graph->node_positions[from_node],
			                                                   graph->node_sizes[from_node],
			                                                   graph->link_from_port[i],
			                                                   graph->node_output_count[from_node], 1);
			rg_vec2 link_b = rg_gui_node_graph_port_canvas_pos(ctx, editor,
			                                                   graph->node_positions[to_node],
			                                                   graph->node_sizes[to_node],
			                                                   graph->link_to_port[i],
			                                                   graph->node_input_count[to_node], 0);
			rg_vec4 wire = ctx->style.color_accent;
			wire.w = ((int)i == state->selected_link_index) ? 1.0f : 0.78f;
			rg_gui_node_link(ctx, editor, link_a, link_b, wire);
		}
	}

	for (u32 i = 0u; i < graph->node_count; i++)
	{
		RgGuiId stack_id = graph->node_ids[i];
		if (stack_id == 0u)
		{
			stack_id = (RgGuiId)(i + 1u);
		}
		rg_gui_push_id_u64(ctx, stack_id);

		const char* title = (draw && draw->title) ? draw->title(user, i) : "Node";
		RgGuiId scoped_node_id = rg_gui_id_scoped(ctx, id);
		RgGuiId drag_id = rg_gui_id_combine(scoped_node_id, 1u);
		int allow_input = !use_node_grid || node_hot[i] || (ctx->active_id == drag_id);
		int node_visible = rg_gui_rects_overlap(node_rects[i], view_rect);
		if (!node_visible && ctx->active_id != drag_id)
		{
			rg_gui_pop_id(ctx);
			continue;
		}

		RgGuiNodeGraphNodeStyle node_style;
		node_style.body = ctx->style.color_panel;
		node_style.title = ctx->style.color_bg;
		node_style.title_hover = ctx->style.color_bg_hover;
		node_style.title_active = ctx->style.color_bg_active;
		node_style.border = ctx->style.color_border;
		node_style.text = ctx->style.color_text;
		node_style.border_thickness = ctx->style.border_thickness;
		node_style.text_scale = 1.0f;
		if (draw && draw->style)
		{
			draw->style(ctx, user, i, graph->node_ids[i], &node_style);
		}

		RgGuiRect node_rect = {0};
		int clicked = rg_gui_node_begin_ex(ctx, editor, title,
		                                   &graph->node_positions[i], graph->node_sizes[i],
		                                   &node_rect, id, allow_input, &node_style);
		node_rects[i] = node_rect;
		if (!editor->node_active)
		{
			rg_gui_pop_id(ctx);
			continue;
		}

		if (ctx->active_id == drag_id)
		{
			drag_index = (int)i;
			if (!state->drag_active)
			{
				state->drag_active = 1;
				state->drag_anchor = (int)i;
				for (u32 j = 0u; j < graph->node_count; j++)
				{
					state->drag_start[j] = graph->node_positions[j];
				}
			}
		}

		if (clicked && selection_ops && selection_ops->is_selected && selection_ops->set_selected && selection_state)
		{
			int use_ctrl = rg_gui_input_has_ctrl(ctx->input);
			int use_shift = rg_gui_input_has_shift(ctx->input);
			int is_selected = selection_ops->is_selected(selection_user, i) ? 1 : 0;

			if (use_ctrl || use_shift)
			{
				u32 select_flags = 0u;
				if (use_ctrl)
				{
					select_flags |= RG_GUI_SELECTION_TOGGLE;
				}
				if (use_shift)
				{
					select_flags |= RG_GUI_SELECTION_RANGE;
				}
				if (rg_gui_selection_apply(selection_state, selection_ops, selection_user,
				                           graph->node_count, i, select_flags))
				{
					if (primary)
					{
						if (selection_ops->is_selected(selection_user, i))
						{
							*primary = (int)i;
						}
						else if (*primary == (int)i)
						{
							*primary = -1;
						}
					}
				}
			}
			else
			{
				if (!is_selected)
				{
					rg_gui_selection_clear_internal(selection_ops, selection_user, graph->node_count);
					selection_ops->set_selected(selection_user, i, 1);
				}
				if (selection_state)
				{
					selection_state->anchor = (int)i;
					selection_state->cursor = (int)i;
					selection_state->initialized = 1;
				}
				if (primary)
				{
					*primary = (int)i;
				}
			}
		}

		if (draw && draw->content)
		{
			draw->content(ctx, i, graph->node_ids[i], user);
		}

		rg_gui_node_end(ctx, editor);

		u32 input_count = graph->node_input_count[i];
		u32 output_count = graph->node_output_count[i];

		for (u32 p = 0u; p < input_count; p++)
		{
			RgGuiNodePortRef port = {0};
			port.node = i;
			port.port = (u16)p;
			port.output = 0u;

			rg_vec2 canvas_pos = rg_gui_node_graph_port_canvas_pos(ctx, editor,
			                                                       graph->node_positions[i],
			                                                       graph->node_sizes[i],
			                                                       p, input_count, 0);
			RgGuiRect port_rect = rg_gui_node_graph_port_screen_rect(editor, canvas_pos);
			int port_hovered_local = !node_drag_in_progress && allow_input && enabled &&
			                         rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, port_rect);
			if (port_hovered_local)
			{
				ctx->hot_id = rg_gui_id_combine(scoped_node_id, 0x4000u + p);
				editor->node_hovered = 1;
				hovered_port = port;
				port_hovered = 1;
				if (right_pressed && !state->link_drag_active)
				{
					u32 removed = rg_gui_node_graph_remove_links_for_port(graph, i, (u16)p, 0,
					                                                      state->link_scratch,
					                                                      RG_ARRAY_COUNT(state->link_scratch));
					if (removed > 0u)
					{
						u32 send = removed;
						if (send > RG_ARRAY_COUNT(state->link_scratch))
						{
							send = RG_ARRAY_COUNT(state->link_scratch);
						}
						state->link_scratch_count = send;
						if (callbacks && callbacks->on_link_remove)
						{
							callbacks->on_link_remove(user, state->link_scratch, send);
						}
						state->consume_right = 1;
					}
				}
			}

			int drag_start = state->link_drag_active &&
			                 state->link_drag.node == port.node &&
			                 state->link_drag.port == port.port &&
			                 state->link_drag.output == port.output;

			if (draw && draw->port)
			{
				draw->port(ctx, port_rect, 0, port_hovered_local || drag_start, user);
			}
			else
			{
				rg_gui_node_graph_draw_port(ctx, port_rect, 0, port_hovered_local || drag_start);
			}

			if (enabled && port_hovered_local && left_pressed &&
			    (ctx->active_id == 0u || ctx->active_id == port_drag_id))
			{
				RgGuiNodePortRef drag_port = port;
				int lift_index = -1;
				if (use_link_grid && mouse_cell_valid && link_entry_count > 0u)
				{
					u32 link_candidate_count = 0u;
					for (int entry = link_cell_head[mouse_cell]; entry >= 0; entry = link_entry_next[entry])
					{
						u32 link_index = link_entry_index[entry];
						if (link_index >= graph->link_count || link_mark[link_index])
						{
							continue;
						}
						if (link_candidate_count < RG_GUI_NODE_GRAPH_MAX_LINKS)
						{
							link_mark[link_index] = 1u;
							link_candidates[link_candidate_count++] = link_index;
						}
					}

					for (u32 entry = 0u; entry < link_overflow_count; entry++)
					{
						u32 link_index = link_overflow[entry];
						if (link_index >= graph->link_count || link_mark[link_index])
						{
							continue;
						}
						if (link_candidate_count < RG_GUI_NODE_GRAPH_MAX_LINKS)
						{
							link_mark[link_index] = 1u;
							link_candidates[link_candidate_count++] = link_index;
						}
					}

					if (link_candidate_count > 0u)
					{
						lift_index = rg_gui_node_graph_find_link_for_port_ex(graph, ctx, editor, port,
						                                                     ctx->mouse_pos, link_candidates,
						                                                     link_candidate_count);
						for (u32 entry = 0u; entry < link_candidate_count; entry++)
						{
							link_mark[link_candidates[entry]] = 0u;
						}
					}
				}

				if (lift_index < 0)
				{
					lift_index = rg_gui_node_graph_find_link_for_port(graph, ctx, editor, port, ctx->mouse_pos);
				}
				if (lift_index >= 0)
				{
					drag_port.node = graph->link_from_node[lift_index];
					drag_port.port = graph->link_from_port[lift_index];
					drag_port.output = 1u;

					state->link_drag_index = lift_index;
					state->link_drag_original.from_node = graph->link_from_node[lift_index];
					state->link_drag_original.from_port = graph->link_from_port[lift_index];
					state->link_drag_original.to_node = graph->link_to_node[lift_index];
					state->link_drag_original.to_port = graph->link_to_port[lift_index];
				}
				else
				{
					state->link_drag_index = -1;
					memset(&state->link_drag_original, 0, sizeof(state->link_drag_original));
				}

				ctx->active_id = port_drag_id;
				ctx->focus_id = id;
				state->link_drag_active = 1;
				state->link_drag = drag_port;
			}
		}

		for (u32 p = 0u; p < output_count; p++)
		{
			RgGuiNodePortRef port = {0};
			port.node = i;
			port.port = (u16)p;
			port.output = 1u;

			rg_vec2 canvas_pos = rg_gui_node_graph_port_canvas_pos(ctx, editor,
			                                                       graph->node_positions[i],
			                                                       graph->node_sizes[i],
			                                                       p, output_count, 1);
			RgGuiRect port_rect = rg_gui_node_graph_port_screen_rect(editor, canvas_pos);
			int port_hovered_local = !node_drag_in_progress && allow_input && enabled &&
			                         rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, port_rect);
			if (port_hovered_local)
			{
				ctx->hot_id = rg_gui_id_combine(scoped_node_id, 0x8000u + p);
				editor->node_hovered = 1;
				hovered_port = port;
				port_hovered = 1;
				if (right_pressed && !state->link_drag_active)
				{
					u32 removed = rg_gui_node_graph_remove_links_for_port(graph, i, (u16)p, 1,
					                                                      state->link_scratch,
					                                                      RG_ARRAY_COUNT(state->link_scratch));
					if (removed > 0u)
					{
						u32 send = removed;
						if (send > RG_ARRAY_COUNT(state->link_scratch))
						{
							send = RG_ARRAY_COUNT(state->link_scratch);
						}
						state->link_scratch_count = send;
						if (callbacks && callbacks->on_link_remove)
						{
							callbacks->on_link_remove(user, state->link_scratch, send);
						}
						state->consume_right = 1;
					}
				}
			}

			int drag_start = state->link_drag_active &&
			                 state->link_drag.node == port.node &&
			                 state->link_drag.port == port.port &&
			                 state->link_drag.output == port.output;

			if (draw && draw->port)
			{
				draw->port(ctx, port_rect, 1, port_hovered_local || drag_start, user);
			}
			else
			{
				rg_gui_node_graph_draw_port(ctx, port_rect, 1, port_hovered_local || drag_start);
			}

			if (enabled && port_hovered_local && left_pressed &&
			    (ctx->active_id == 0u || ctx->active_id == port_drag_id))
			{
				ctx->active_id = port_drag_id;
				ctx->focus_id = id;
				state->link_drag_active = 1;
				state->link_drag = port;
				state->link_drag_index = -1;
				memset(&state->link_drag_original, 0, sizeof(state->link_drag_original));
			}
		}

		if (selection_ops && selection_ops->is_selected &&
		    selection_ops->is_selected(selection_user, i))
		{
			f32 thickness = 1.0f;
			if (primary && *primary == (int)i)
			{
				thickness = 2.0f;
			}
			rg_gui_push_rect_outline(ctx, node_rect, ctx->style.color_accent, thickness);
		}

		rg_gui_pop_id(ctx);
	}

	if (state->drag_active)
	{
		if (ctx->active_id == 0u || drag_index < 0)
		{
			state->drag_active = 0;
			state->drag_anchor = -1;
		}
		else if (state->drag_anchor >= 0 &&
		         state->drag_anchor < (int)graph->node_count &&
		         selection_ops && selection_ops->is_selected &&
		         selection_ops->is_selected(selection_user, (u32)state->drag_anchor))
		{
			rg_vec2 delta =
			    {
			        .x = graph->node_positions[state->drag_anchor].x - state->drag_start[state->drag_anchor].x,
			        .y = graph->node_positions[state->drag_anchor].y - state->drag_start[state->drag_anchor].y};
			if (delta.x != 0.0f || delta.y != 0.0f)
			{
				for (u32 i = 0u; i < graph->node_count; i++)
				{
					if ((int)i == state->drag_anchor)
					{
						continue;
					}
					if (selection_ops->is_selected(selection_user, i))
					{
						graph->node_positions[i].x = state->drag_start[i].x + delta.x;
						graph->node_positions[i].y = state->drag_start[i].y + delta.y;
					}
				}
			}
		}
	}

	if (was_dragging && !state->drag_active)
	{
		state->move_count = 0u;
		for (u32 i = 0u; i < graph->node_count; i++)
		{
			rg_vec2 before = state->drag_start[i];
			rg_vec2 after = graph->node_positions[i];
			if (before.x != after.x || before.y != after.y)
			{
				u32 index = state->move_count;
				state->move_indices[index] = i;
				state->move_ids[index] = graph->node_ids[i];
				state->move_from[index] = before;
				state->move_to[index] = after;
				state->move_count++;
			}
		}

		if (groups && groups->groups && groups->count > 0u && state->move_count > 0u)
		{
			for (u32 m = 0u; m < state->move_count; m++)
			{
				u32 node_index = state->move_indices[m];
				if (node_index >= graph->node_count)
				{
					continue;
				}

				RgGuiId node_id = graph->node_ids[node_index];
				if (node_id == 0u)
				{
					continue;
				}

				rg_vec2 node_pos = graph->node_positions[node_index];
				rg_vec2 node_size = graph->node_sizes[node_index];
				f32 node_min_x = rg_minf(node_pos.x, node_pos.x + node_size.x);
				f32 node_min_y = rg_minf(node_pos.y, node_pos.y + node_size.y);
				f32 node_max_x = rg_maxf(node_pos.x, node_pos.x + node_size.x);
				f32 node_max_y = rg_maxf(node_pos.y, node_pos.y + node_size.y);
				f32 node_cx = (node_min_x + node_max_x) * 0.5f;
				f32 node_cy = (node_min_y + node_max_y) * 0.5f;

				int target_group = -1;
				int current_group = -1;
				for (int g = (int)groups->count - 1; g >= 0; g--)
				{
					RgGuiNodeGroup* group = &groups->groups[g];
					f32 group_min_x = rg_minf(group->position.x, group->position.x + group->size.x);
					f32 group_min_y = rg_minf(group->position.y, group->position.y + group->size.y);
					f32 group_max_x = rg_maxf(group->position.x, group->position.x + group->size.x);
					f32 group_max_y = rg_maxf(group->position.y, group->position.y + group->size.y);

					if (node_cx >= group_min_x && node_cx <= group_max_x &&
					    node_cy >= group_min_y && node_cy <= group_max_y)
					{
						target_group = g;
						break;
					}
				}

				for (u32 g = 0u; g < groups->count; g++)
				{
					RgGuiNodeGroup* group = &groups->groups[g];
					int member_index = rg_gui_node_group_find_member(group, node_id);
					if (member_index >= 0 && current_group < 0)
					{
						current_group = (int)g;
					}
				}

				if (target_group >= 0)
				{
					RgGuiNodeGroup* group = &groups->groups[target_group];
					int in_target = rg_gui_node_group_find_member(group, node_id) >= 0;
					if (!in_target && group->member_count >= RG_GUI_NODE_GROUP_MAX_MEMBERS)
					{
						target_group = current_group;
					}
				}

				for (u32 g = 0u; g < groups->count; g++)
				{
					RgGuiNodeGroup* group = &groups->groups[g];
					int member_index = rg_gui_node_group_find_member(group, node_id);
					if ((int)g == target_group)
					{
						if (member_index < 0)
						{
							rg_gui_node_group_add_member(group, node_id);
						}
					}
					else if (member_index >= 0)
					{
						rg_gui_node_group_remove_member(group, node_id);
					}
				}
			}
		}

		if (state->move_count > 0u && callbacks && callbacks->on_node_move)
		{
			RgGuiNodeGraphMove move =
			    {
			        state->move_count,
			        state->move_indices,
			        state->move_ids,
			        state->move_from,
			        state->move_to};
			callbacks->on_node_move(user, &move);
		}
	}

	if (state->link_drag_active && ctx->active_id != port_drag_id)
	{
		state->link_drag_active = 0;
		state->link_drag_index = -1;
		memset(&state->link_drag_original, 0, sizeof(state->link_drag_original));
	}

	if (state->link_drag_active && ctx->active_id == port_drag_id && !left_down)
	{
		int committed = 0;
		int did_add = 0;
		int did_rewire = 0;
		RgGuiNodeGraphLink added_link = {0};
		RgGuiNodeGraphLink rewire_before = {0};
		RgGuiNodeGraphLink rewire_after = {0};

		if (port_hovered)
		{
			RgGuiNodePortRef from = state->link_drag;
			RgGuiNodePortRef to = hovered_port;
			if (from.output != to.output)
			{
				if (!from.output)
				{
					RgGuiNodePortRef tmp = from;
					from = to;
					to = tmp;
				}

				if (from.node != to.node &&
				    from.node < graph->node_count &&
				    to.node < graph->node_count &&
				    from.port < graph->node_output_count[from.node] &&
				    to.port < graph->node_input_count[to.node])
				{
					if (state->link_drag_index >= 0)
					{
						int same = (state->link_drag_original.from_node == from.node &&
						            state->link_drag_original.from_port == from.port &&
						            state->link_drag_original.to_node == to.node &&
						            state->link_drag_original.to_port == to.port);
						if (same)
						{
							committed = 1;
						}
						else if (rg_gui_node_graph_rewire_link(graph, (u32)state->link_drag_index,
						                                       from.node, from.port, to.node, to.port))
						{
							committed = 1;
							did_rewire = 1;
							rewire_before = rg_gui_node_graph_link_make(graph,
							                                            state->link_drag_original.from_node,
							                                            state->link_drag_original.from_port,
							                                            state->link_drag_original.to_node,
							                                            state->link_drag_original.to_port);
							rewire_after = rg_gui_node_graph_link_make(graph,
							                                           from.node, from.port, to.node, to.port);
						}
					}
					else
					{
						if (rg_gui_node_graph_add_link(graph, from.node, from.port, to.node, to.port))
						{
							committed = 1;
							did_add = 1;
							added_link = rg_gui_node_graph_link_make(graph, from.node, from.port, to.node, to.port);
						}
					}
				}
			}
		}

		if (!committed && state->link_drag_index >= 0)
		{
			RgGuiNodeGraphLink removed =
			    rg_gui_node_graph_link_make(graph,
			                                state->link_drag_original.from_node,
			                                state->link_drag_original.from_port,
			                                state->link_drag_original.to_node,
			                                state->link_drag_original.to_port);
			if (rg_gui_node_graph_remove_link_index(graph, (u32)state->link_drag_index))
			{
				if (callbacks && callbacks->on_link_remove)
				{
					callbacks->on_link_remove(user, &removed, 1u);
				}
			}
		}
		else if (did_rewire)
		{
			if (callbacks && callbacks->on_link_rewire)
			{
				callbacks->on_link_rewire(user, &rewire_before, &rewire_after);
			}
		}
		else if (did_add)
		{
			if (callbacks && callbacks->on_link_add)
			{
				callbacks->on_link_add(user, &added_link, 1u);
			}
		}

		state->link_drag_active = 0;
		state->link_drag_index = -1;
		memset(&state->link_drag_original, 0, sizeof(state->link_drag_original));
		ctx->active_id = 0u;
	}

	if (state->link_drag_active && ctx->active_id == port_drag_id)
	{
		if (state->link_drag.node < graph->node_count)
		{
			u32 port_count = state->link_drag.output ? graph->node_output_count[state->link_drag.node] : graph->node_input_count[state->link_drag.node];
			if (state->link_drag.port < port_count)
			{
				rg_vec2 start = rg_gui_node_graph_port_canvas_pos(ctx, editor,
				                                                  graph->node_positions[state->link_drag.node],
				                                                  graph->node_sizes[state->link_drag.node],
				                                                  state->link_drag.port, port_count,
				                                                  state->link_drag.output);
				rg_vec2 end = rg_gui_node_editor_to_canvas(editor, ctx->mouse_pos);
				rg_vec4 ghost = ctx->style.color_accent;
				ghost.w *= 0.62f;
				rg_gui_node_link(ctx, editor, start, end, ghost);
			}
		}
	}

	if (selection_ops && selection_ops->set_selected && selection_state)
	{
		RgGuiRect select_screen = {0};
		int selection_active = rg_gui_node_editor_selection(ctx, editor, id, &select_screen, NULL);
		if (selection_active && !state->marquee_active)
		{
			state->marquee_active = 1;
			state->marquee_add = rg_gui_input_has_ctrl(ctx->input) || rg_gui_input_has_shift(ctx->input);
		}
		if (selection_active)
		{
			if (!state->marquee_add)
			{
				rg_gui_selection_clear_internal(selection_ops, selection_user, graph->node_count);
			}
			for (u32 i = 0u; i < graph->node_count; i++)
			{
				if (rg_gui_rects_overlap(select_screen, node_rects[i]))
				{
					selection_ops->set_selected(selection_user, i, 1);
				}
				else if (!state->marquee_add)
				{
					selection_ops->set_selected(selection_user, i, 0);
				}
			}
		}
		else if (state->marquee_active)
		{
			state->marquee_active = 0;
		}
	}

	if (primary && selection_ops && selection_ops->is_selected)
	{
		if (*primary >= 0 && *primary < (int)graph->node_count)
		{
			if (!selection_ops->is_selected(selection_user, (u32)*primary))
			{
				*primary = -1;
			}
		}
		if (*primary < 0)
		{
			for (u32 i = 0u; i < graph->node_count; i++)
			{
				if (selection_ops->is_selected(selection_user, i))
				{
					*primary = (int)i;
					break;
				}
			}
		}
	}

	rg_gui_node_editor_end(ctx, editor);
	return hovered;
}

RGINLINE int rg_gui_node_graph_editor(RgGuiContext* ctx, RgGuiNodeEditorState* editor, RgGuiNodeGraphState* state,
                                      RgGuiNodeGraph* graph, const RgGuiNodeGraphDraw* draw,
                                      const RgGuiNodeGraphSelection* selection, const RgGuiNodeGraphCallbacks* callbacks,
                                      RgGuiRect rect, RgGuiId id, void* user)
{
	return rg_gui_node_graph_editor_ex(ctx, editor, state, graph, draw, selection, callbacks,
	                                   NULL, rect, id, user);
}

RGINLINE int rg_gui_tabs(RgGuiContext* ctx, const char* const* labels, u32 count, u32* active, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(active != NULL);

	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && ctx->tab_focus_id == id && ctx->focus_id != id)
	{
		ctx->focus_id = id;
	}
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	u32 active_index = *active;
	if (count == 0u)
	{
		active_index = 0u;
	}
	else if (active_index >= count)
	{
		active_index = count - 1u;
	}

	int focused = enabled && (ctx->focus_id == id);
	int changed = 0;

	if (focused && count > 0u)
	{
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_RIGHT))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_LEFT)
			                       ? SDL_SCANCODE_LEFT
			                       : SDL_SCANCODE_RIGHT;
			int repeats = rg_gui_key_repeat(ctx, id, key);
			if (repeats > 0)
			{
				u32 move = (u32)repeats;
				if (move >= count) move %= count;
				u32 next = active_index;
				if (key == SDL_SCANCODE_LEFT)
				{
					next = next >= move ? next - move : count - (move - next);
				}
				else
				{
					next = next >= count - move ? next - (count - move) : next + move;
				}
				if (next != active_index)
				{
					active_index = next;
					changed = 1;
				}
			}
		}
	}

	f32 pad = ctx->style.padding;
	f32 tab_h = rect.h;
	if (tab_h < 1.0f)
	{
		tab_h = 1.0f;
	}
	f32 min_tab_w = ctx->style.char_width * 6.0f + pad * 2.0f;
	if (min_tab_w < tab_h)
	{
		min_tab_w = tab_h;
	}

	f32 tab_w = 0.0f;
	if (count > 0u && rect.w > 0.0f)
	{
		tab_w = rect.w / (f32)count;
	}

	int show_scroll = (count > 0u && rect.w > 0.0f && rect.h > 0.0f && tab_w < min_tab_w);
	if (show_scroll)
	{
		tab_w = min_tab_w;
	}

	f32 total_w = tab_w * (f32)count;
	f32 active_start = (count > 0u) ? (tab_w * (f32)active_index) : -1.0f;
	f32 active_end = (active_start >= 0.0f) ? (active_start + tab_w) : -1.0f;
	f32 arrow_w = 0.0f;
	if (show_scroll)
	{
		arrow_w = tab_h;
		if (arrow_w * 2.0f > rect.w)
		{
			arrow_w = rect.w * 0.5f;
		}
	}

	f32 strip_x = rect.x + arrow_w;
	f32 strip_w = rect.w - arrow_w * 2.0f;
	if (strip_w < 0.0f)
	{
		strip_w = 0.0f;
	}
	RgGuiRect strip_rect = rg_gui_make_rect(strip_x, rect.y, strip_w, rect.h);
	int strip_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, strip_rect);

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
		if (strip_hovered)
		{
			ctx->active_id = id;
		}
	}

	RgGuiTabScrollCacheEntry* scroll_entry = rg_gui_tab_scroll_cache_find(ctx, id);
	f32 scroll_x = scroll_entry ? scroll_entry->scroll : 0.0f;
	f32 max_scroll = total_w - strip_w;
	if (max_scroll < 0.0f)
	{
		max_scroll = 0.0f;
	}
	if (max_scroll <= 0.0f)
	{
		scroll_x = 0.0f;
	}
	if (scroll_x < 0.0f)
	{
		scroll_x = 0.0f;
	}
	if (scroll_x > max_scroll)
	{
		scroll_x = max_scroll;
	}

	if (max_scroll > 0.0f && active_start >= 0.0f && strip_w > 0.0f)
	{
		f32 active_w = active_end - active_start;
		if (active_w > strip_w)
		{
			f32 min_scroll = active_start;
			f32 max_active_scroll = active_end - strip_w;
			if (scroll_x < min_scroll)
			{
				scroll_x = min_scroll;
			}
			if (scroll_x > max_active_scroll)
			{
				scroll_x = max_active_scroll;
			}
		}
		else
		{
			if (active_start < scroll_x)
			{
				scroll_x = active_start;
			}
			else if (active_end > scroll_x + strip_w)
			{
				scroll_x = active_end - strip_w;
			}
		}
		if (scroll_x < 0.0f)
		{
			scroll_x = 0.0f;
		}
		if (scroll_x > max_scroll)
		{
			scroll_x = max_scroll;
		}
	}

	if (show_scroll)
	{
		f32 scroll_step = strip_w * 0.5f;
		if (scroll_step < tab_h)
		{
			scroll_step = tab_h;
		}
		f32 wheel_step = tab_h * 2.0f;
		if (wheel_step < 8.0f)
		{
			wheel_step = 8.0f;
		}

		if (enabled && hovered && ctx->mouse_wheel != 0.0f && max_scroll > 0.0f)
		{
			int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
			if (steps != 0)
			{
				scroll_x -= (f32)steps * wheel_step;
			}
		}

		RgGuiRect left_rect = rg_gui_make_rect(rect.x, rect.y, arrow_w, rect.h);
		RgGuiRect right_rect = rg_gui_make_rect(rect.x + rect.w - arrow_w, rect.y, arrow_w, rect.h);

		int can_left = (scroll_x > 0.0f);
		int can_right = (scroll_x + 0.5f < max_scroll);

		RgGuiId left_id = rg_gui_id_combine(id, 0x5441424Cu);
		RgGuiId right_id = rg_gui_id_combine(id, 0x54414252u);

		int left_hovered = enabled && can_left && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, left_rect);
		int right_hovered = enabled && can_right && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, right_rect);

		if (left_hovered)
		{
			ctx->hot_id = left_id;
		}
		if (right_hovered)
		{
			ctx->hot_id = right_id;
		}

		if (enabled && left_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = left_id;
		}
		if (enabled && right_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = right_id;
		}

		if (!enabled && (ctx->active_id == left_id || ctx->active_id == right_id))
		{
			ctx->active_id = 0u;
		}

		if (enabled && ctx->active_id == left_id && ctx->mouse_released)
		{
			if (left_hovered)
			{
				scroll_x -= scroll_step;
			}
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == right_id && ctx->mouse_released)
		{
			if (right_hovered)
			{
				scroll_x += scroll_step;
			}
			ctx->active_id = 0u;
		}

		rg_vec4 left_bg = ctx->style.color_bg;
		if (enabled && ctx->active_id == left_id)
		{
			left_bg = ctx->style.color_bg_active;
		}
		else if (left_hovered)
		{
			left_bg = ctx->style.color_bg_hover;
		}
		rg_vec4 right_bg = ctx->style.color_bg;
		if (enabled && ctx->active_id == right_id)
		{
			right_bg = ctx->style.color_bg_active;
		}
		else if (right_hovered)
		{
			right_bg = ctx->style.color_bg_hover;
		}

		rg_gui_push_rect(ctx, left_rect, left_bg);
		rg_gui_push_rect_outline(ctx, left_rect, ctx->style.color_border, ctx->style.border_thickness);
		rg_gui_push_rect(ctx, right_rect, right_bg);
		rg_gui_push_rect_outline(ctx, right_rect, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec4 arrow_color = ctx->style.color_text;
		if (!can_left || !enabled)
		{
			arrow_color = ctx->style.color_text_dim;
		}
		rg_vec4 arrow_color_right = ctx->style.color_text;
		if (!can_right || !enabled)
		{
			arrow_color_right = ctx->style.color_text_dim;
		}

		rg_gui_push_arrow(ctx, left_rect, RG_GUI_ARROW_LEFT, arrow_color);
		rg_gui_push_arrow(ctx, right_rect, RG_GUI_ARROW_RIGHT, arrow_color_right);

		if (scroll_x < 0.0f)
		{
			scroll_x = 0.0f;
		}
		if (scroll_x > max_scroll)
		{
			scroll_x = max_scroll;
		}
	}

	if (!enabled && ctx->active_id == id)
	{
		ctx->active_id = 0u;
	}

	int pending_click = enabled && ctx->active_id == id && ctx->mouse_released;
	f32 clicked_start = -1.0f;
	f32 clicked_end = -1.0f;

	if (count > 0u && rect.w > 0.0f && rect.h > 0.0f && strip_rect.w > 0.0f)
	{
		rg_gui_push_clip(ctx, strip_rect);
		f32 cursor_x = strip_rect.x - scroll_x;

		for (u32 i = 0u; i < count; i++)
		{
			RgGuiRect tab_rect = rg_gui_make_rect(cursor_x, rect.y, tab_w, rect.h);
			int tab_visible = (tab_rect.x + tab_rect.w > strip_rect.x && tab_rect.x < strip_rect.x + strip_rect.w);

			int tab_hovered = enabled && strip_hovered && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, tab_rect);
			if (tab_hovered)
			{
				ctx->hot_id = id;
			}

			if (pending_click && tab_hovered)
			{
				if (i != active_index)
				{
					active_index = i;
					changed = 1;
				}
				clicked_start = tab_w * (f32)i;
				clicked_end = clicked_start + tab_w;
			}

			if (tab_visible)
			{
				rg_vec4 bg = ctx->style.color_bg;
				if (i == active_index)
				{
					bg = ctx->style.color_bg_active;
				}
				else if (enabled && tab_hovered)
				{
					bg = ctx->style.color_bg_hover;
				}

				rg_gui_push_rect(ctx, tab_rect, bg);
				rg_gui_push_rect_outline(ctx, tab_rect, ctx->style.color_border, ctx->style.border_thickness);

				if (labels && labels[i])
				{
					rg_vec2 pos = rg_vec2(tab_rect.x + pad,
					                      tab_rect.y + (tab_rect.h - ctx->style.text_height) * 0.5f);
					if (RG_GUI_LABEL_COPY)
					{
						rg_gui_push_text(ctx, labels[i], pos, ctx->style.color_text);
					}
					else
					{
						rg_gui_push_text_static(ctx, labels[i], pos, ctx->style.color_text);
					}
				}
			}

			cursor_x += tab_w;
		}

		rg_gui_pop_clip(ctx);
	}

	if (pending_click)
	{
		ctx->active_id = 0u;
	}

	if (clicked_start >= 0.0f && max_scroll > 0.0f && strip_w > 0.0f)
	{
		if (clicked_start < scroll_x)
		{
			scroll_x = clicked_start;
		}
		else if (clicked_end > scroll_x + strip_w)
		{
			scroll_x = clicked_end - strip_w;
		}
		if (scroll_x < 0.0f)
		{
			scroll_x = 0.0f;
		}
		if (scroll_x > max_scroll)
		{
			scroll_x = max_scroll;
		}
	}

	if (scroll_entry)
	{
		scroll_entry->scroll = scroll_x;
	}

	if (rect.w > 0.0f && rect.h > 0.0f)
	{
		rg_gui_push_rect_outline(ctx, rect,
		                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
		                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);
	}

	*active = active_index;
	return changed;
}

RGINLINE int rg_gui_list(RgGuiContext* ctx, const char* const* items, u32 count, int* selected, int* scroll, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(selected != NULL);
	count = (u32)rg_gui_u32_count_to_int(count);

	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);

	f32 item_h = ctx->style.text_height + ctx->style.padding * 2.0f;
	if (item_h < 1.0f)
	{
		item_h = 1.0f;
	}

	f32 border = ctx->style.border_thickness;
	RgGuiRect inner = rg_gui_make_rect(rect.x + border,
	                                   rect.y + border,
	                                   rect.w - border * 2.0f,
	                                   rect.h - border * 2.0f);
	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;

	int visible = rg_gui_nonnegative_f32_to_int_bounded(inner.h / item_h);
	if (visible < 1)
	{
		visible = 1;
	}

	f32 scroll_w = ctx->style.scroll_bar_width;
	f32 scroll_gap = ctx->style.inner_spacing;
	if (scroll_gap < 0.0f)
	{
		scroll_gap = 0.0f;
	}
	int use_scrollbar = (scroll_w > 0.0f && count > (u32)visible);
	if (use_scrollbar)
	{
		inner.w -= scroll_w + scroll_gap;
		if (inner.w < 0.0f)
		{
			inner.w = 0.0f;
		}
	}

	int count_int = (int)count;
	int top = scroll ? *scroll : 0;
	int max_top = count_int - visible;
	if (max_top < 0)
	{
		max_top = 0;
	}
	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	if (enabled && hovered && max_top > 0)
	{
		ctx->scroll_owner_next = id;
	}

	int sel = *selected;
	if (sel >= count_int)
	{
		sel = count_int > 0 ? count_int - 1 : -1;
	}
	if (sel < -1)
	{
		sel = -1;
	}

	if (enabled && hovered && ctx->mouse_wheel != 0.0f && count > 0u && ctx->scroll_owner == id)
	{
		int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
		if (steps != 0)
		{
			top = rg_gui_i64_clamp_to_int((i64)top - (i64)steps, 0, max_top);
		}
	}

	int changed = 0;
	if (focused && count > 0u)
	{
		int target = sel;
		if (rg_gui_nav_list_move(ctx, id, count_int, visible, sel, &target, NULL))
		{
			sel = target;
			changed = 1;
		}

		if (ctx->input && ctx->input->has_text_input && !rg_gui_input_has_ctrl(ctx->input))
		{
			const char* query = ctx->input->text_input_buffer;
			int start_index = (sel >= 0) ? sel + 1 : 0;
			int found = rg_gui_find_prefix(items, count, query, start_index);
			if (found < 0 && start_index > 0)
			{
				found = rg_gui_find_prefix(items, count, query, 0);
			}
			if (found >= 0 && found != sel)
			{
				sel = found;
				changed = 1;
			}
		}
	}

	if (use_scrollbar && max_top > 0 && inner.h > 0.0f)
	{
		f32 track_x = inner.x + inner.w + scroll_gap;
		f32 track_y = inner.y;
		f32 track_h = inner.h;
		RgGuiRect track = rg_gui_make_rect(track_x, track_y, scroll_w, track_h);

		f32 handle_h = track_h;
		f32 content_h = item_h * (f32)count;
		if (content_h > 0.0f)
		{
			handle_h = track_h * (inner.h / content_h);
		}
		f32 min_handle = ctx->style.text_height * 0.5f;
		if (handle_h < min_handle) handle_h = min_handle;
		if (handle_h > track_h) handle_h = track_h;

		f32 scroll_t = 0.0f;
		if (max_top > 0 && track_h > handle_h)
		{
			scroll_t = (f32)top / (f32)max_top;
		}
		f32 handle_y = track_y + (track_h - handle_h) * scroll_t;
		RgGuiRect handle = rg_gui_make_rect(track_x, handle_y, scroll_w, handle_h);

		RgGuiId bar_id = rg_gui_id_combine(id, 1u);
		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, track);
		if (bar_hovered)
		{
			ctx->hot_id = bar_id;
		}

		if (enabled && ctx->active_id == bar_id)
		{
			ctx->scroll_owner_next = id;
		}

		if (enabled && bar_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = bar_id;
			ctx->focus_id = id;
		}

		if (!enabled && ctx->active_id == bar_id)
		{
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == bar_id)
		{
			if (ctx->mouse_down)
			{
				f32 track_span = track_h - handle_h;
				f32 t = 0.0f;
				if (track_span > 0.0f)
				{
					t = (ctx->mouse_pos.y - track_y - handle_h * 0.5f) / track_span;
				}
				if (t < 0.0f) t = 0.0f;
				if (t > 1.0f) t = 1.0f;
				top = rg_gui_scroll_index_from_ratio(t, max_top);
			}
			if (ctx->mouse_released)
			{
				ctx->active_id = 0u;
			}
		}

		rg_vec4 track_color = bar_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, track, track_color);
		rg_gui_push_rect_outline(ctx, track, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec4 handle_color = (ctx->active_id == bar_id) ? ctx->style.color_accent : ctx->style.color_bg_active;
		rg_gui_push_rect(ctx, handle, handle_color);
	}

	int keep_visible = (scroll == NULL) ? 1 : changed;
	if (keep_visible && sel >= 0 && count > 0u)
	{
		if (sel < top)
		{
			top = sel;
		}
		else if (sel >= top + visible)
		{
			top = sel - visible + 1;
		}
	}

	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	u32 start = (u32)top;
	u32 end = start + (u32)visible;
	if (end > count)
	{
		end = count;
	}

	for (u32 i = start; i < end; i++)
	{
		f32 y = inner.y + item_h * (f32)(i - start);
		RgGuiRect item_rect = rg_gui_make_rect(inner.x, y, inner.w, item_h);
		int item_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, item_rect);
		if (item_hovered)
		{
			ctx->hot_id = id;
		}

		if (enabled && item_hovered && ctx->mouse_pressed)
		{
			sel = (int)i;
			changed = 1;
			ctx->focus_id = id;
		}

		rg_vec4 bg = ctx->style.color_bg;
		if ((int)i == sel)
		{
			bg = ctx->style.color_bg_active;
		}
		else if (enabled && item_hovered)
		{
			bg = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect(ctx, item_rect, bg);

		if (items && items[i])
		{
			rg_vec2 pos = rg_vec2(item_rect.x + ctx->style.padding,
			                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
			if (RG_GUI_LABEL_COPY)
			{
				rg_gui_push_text(ctx, items[i], pos, ctx->style.color_text);
			}
			else
			{
				rg_gui_push_text_static(ctx, items[i], pos, ctx->style.color_text);
			}
		}
	}

	*selected = sel;
	if (scroll)
	{
		*scroll = top;
	}
	return changed;
}

RGINLINE int rg_gui_list_virtual_multi(RgGuiContext* ctx, RgGuiListItemFn get_item, const void* user, u32 count,
                                       RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                                       int* scroll, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(selection != NULL);
	RG_GUI_ASSERT(ops != NULL);
	RG_GUI_ASSERT(ops->is_selected != NULL);
	RG_GUI_ASSERT(ops->set_selected != NULL);
	count = (u32)rg_gui_u32_count_to_int(count);

	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && ctx->tab_focus_id == id && ctx->focus_id != id)
	{
		ctx->focus_id = id;
	}
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);

	rg_gui_selection_state_init(selection);

	f32 item_h = ctx->style.text_height + ctx->style.padding * 2.0f;
	if (item_h < 1.0f)
	{
		item_h = 1.0f;
	}

	f32 border = ctx->style.border_thickness;
	RgGuiRect inner = rg_gui_make_rect(rect.x + border,
	                                   rect.y + border,
	                                   rect.w - border * 2.0f,
	                                   rect.h - border * 2.0f);
	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;

	int visible = rg_gui_nonnegative_f32_to_int_bounded(inner.h / item_h);
	if (visible < 1)
	{
		visible = 1;
	}

	f32 scroll_w = ctx->style.scroll_bar_width;
	f32 scroll_gap = ctx->style.inner_spacing;
	if (scroll_gap < 0.0f)
	{
		scroll_gap = 0.0f;
	}
	int use_scrollbar = (scroll_w > 0.0f && count > (u32)visible);
	if (use_scrollbar)
	{
		inner.w -= scroll_w + scroll_gap;
		if (inner.w < 0.0f)
		{
			inner.w = 0.0f;
		}
	}

	int top = scroll ? *scroll : 0;
	int max_top = (int)count - visible;
	if (max_top < 0)
	{
		max_top = 0;
	}
	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	if (enabled && hovered && max_top > 0)
	{
		ctx->scroll_owner_next = id;
	}

	int cursor = selection->cursor;
	if (cursor >= (int)count)
	{
		cursor = count > 0u ? (int)count - 1 : -1;
	}
	if (cursor < -1)
	{
		cursor = -1;
	}
	selection->cursor = cursor;

	if (enabled && hovered && ctx->mouse_wheel != 0.0f && count > 0u && ctx->scroll_owner == id)
	{
		int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
		if (steps != 0)
		{
			top = rg_gui_i64_clamp_to_int((i64)top - (i64)steps, 0, max_top);
		}
	}

	int changed = 0;
	if (focused && count > 0u)
	{
		int target = cursor;
		if (rg_gui_nav_list_move(ctx, id, (int)count, visible, cursor, &target, NULL))
		{
			u32 flags = RG_GUI_SELECTION_NONE;
			if (ctx->input && rg_gui_input_has_shift(ctx->input))
			{
				flags |= RG_GUI_SELECTION_RANGE;
			}
			changed |= rg_gui_selection_apply(selection, ops, selection_user, count, (u32)target, flags);
			cursor = selection->cursor;
		}

		if (ctx->input && ctx->input->has_text_input && !rg_gui_input_has_ctrl(ctx->input))
		{
			const char* query = ctx->input->text_input_buffer;
			int start_index = (cursor >= 0) ? cursor + 1 : 0;
			int found = rg_gui_find_prefix_fn(get_item, user, count, query, start_index);
			if (found < 0 && start_index > 0)
			{
				found = rg_gui_find_prefix_fn(get_item, user, count, query, 0);
			}
			if (found >= 0 && found != cursor)
			{
				changed |= rg_gui_selection_apply(selection, ops, selection_user, count, (u32)found, RG_GUI_SELECTION_NONE);
				cursor = selection->cursor;
			}
		}
	}

	if (use_scrollbar && max_top > 0 && inner.h > 0.0f)
	{
		f32 track_x = inner.x + inner.w + scroll_gap;
		f32 track_y = inner.y;
		f32 track_h = inner.h;
		RgGuiRect track = rg_gui_make_rect(track_x, track_y, scroll_w, track_h);

		f32 handle_h = track_h;
		f32 content_h = item_h * (f32)count;
		if (content_h > 0.0f)
		{
			handle_h = track_h * (inner.h / content_h);
		}
		f32 min_handle = ctx->style.text_height * 0.5f;
		if (handle_h < min_handle) handle_h = min_handle;
		if (handle_h > track_h) handle_h = track_h;

		f32 scroll_t = 0.0f;
		if (max_top > 0 && track_h > handle_h)
		{
			scroll_t = (f32)top / (f32)max_top;
		}
		f32 handle_y = track_y + (track_h - handle_h) * scroll_t;
		RgGuiRect handle = rg_gui_make_rect(track_x, handle_y, scroll_w, handle_h);

		RgGuiId bar_id = rg_gui_id_combine(id, 1u);
		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, track);
		if (bar_hovered)
		{
			ctx->hot_id = bar_id;
		}

		if (enabled && ctx->active_id == bar_id)
		{
			ctx->scroll_owner_next = id;
		}

		if (enabled && bar_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = bar_id;
			ctx->focus_id = id;
		}

		if (!enabled && ctx->active_id == bar_id)
		{
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == bar_id)
		{
			if (ctx->mouse_down)
			{
				f32 track_span = track_h - handle_h;
				f32 t = 0.0f;
				if (track_span > 0.0f)
				{
					t = (ctx->mouse_pos.y - track_y - handle_h * 0.5f) / track_span;
				}
				if (t < 0.0f) t = 0.0f;
				if (t > 1.0f) t = 1.0f;
				top = rg_gui_scroll_index_from_ratio(t, max_top);
			}
			if (ctx->mouse_released)
			{
				ctx->active_id = 0u;
			}
		}

		rg_vec4 track_color = bar_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, track, track_color);
		rg_gui_push_rect_outline(ctx, track, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec4 handle_color = (ctx->active_id == bar_id) ? ctx->style.color_accent : ctx->style.color_bg_active;
		rg_gui_push_rect(ctx, handle, handle_color);
	}

	int keep_visible = (scroll == NULL) ? 1 : changed;
	if (keep_visible && cursor >= 0 && count > 0u)
	{
		if (cursor < top)
		{
			top = cursor;
		}
		else if (cursor >= top + visible)
		{
			top = cursor - visible + 1;
		}
	}

	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	u32 start = (u32)top;
	u32 end = start + (u32)visible;
	if (end > count)
	{
		end = count;
	}

	for (u32 i = start; i < end; i++)
	{
		f32 y = inner.y + item_h * (f32)(i - start);
		RgGuiRect item_rect = rg_gui_make_rect(inner.x, y, inner.w, item_h);
		int item_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, item_rect);
		if (item_hovered)
		{
			ctx->hot_id = id;
		}

		if (enabled && item_hovered && ctx->mouse_pressed)
		{
			u32 flags = RG_GUI_SELECTION_NONE;
			if (ctx->input && rg_gui_input_has_ctrl(ctx->input))
			{
				flags |= RG_GUI_SELECTION_TOGGLE;
			}
			if (ctx->input && rg_gui_input_has_shift(ctx->input))
			{
				flags |= RG_GUI_SELECTION_RANGE;
			}
			changed |= rg_gui_selection_apply(selection, ops, selection_user, count, i, flags);
			cursor = selection->cursor;
			ctx->focus_id = id;
		}

		int item_selected = ops->is_selected(selection_user, i) ? 1 : 0;
		rg_vec4 bg = ctx->style.color_bg;
		if (item_selected)
		{
			bg = ctx->style.color_bg_active;
		}
		else if (enabled && item_hovered)
		{
			bg = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect(ctx, item_rect, bg);

		if (get_item)
		{
			const char* item = get_item(user, i);
			if (item)
			{
				rg_vec2 pos = rg_vec2(item_rect.x + ctx->style.padding,
				                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
				if (RG_GUI_LABEL_COPY)
				{
					rg_gui_push_text(ctx, item, pos, ctx->style.color_text);
				}
				else
				{
					rg_gui_push_text_static(ctx, item, pos, ctx->style.color_text);
				}
			}
		}
	}

	if (use_scrollbar && max_top > 0 && inner.h > 0.0f)
	{
		f32 track_x = inner.x + inner.w + scroll_gap;
		f32 track_y = inner.y;
		f32 track_h = inner.h;
		RgGuiRect track = rg_gui_make_rect(track_x, track_y, scroll_w, track_h);

		f32 handle_h = track_h;
		f32 content_h = item_h * (f32)count;
		if (content_h > 0.0f)
		{
			handle_h = track_h * (inner.h / content_h);
		}
		f32 min_handle = ctx->style.text_height * 0.5f;
		if (handle_h < min_handle) handle_h = min_handle;
		if (handle_h > track_h) handle_h = track_h;

		f32 scroll_t = 0.0f;
		if (max_top > 0 && track_h > handle_h)
		{
			scroll_t = (f32)top / (f32)max_top;
		}
		f32 handle_y = track_y + (track_h - handle_h) * scroll_t;
		RgGuiRect handle = rg_gui_make_rect(track_x, handle_y, scroll_w, handle_h);

		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, track);
		rg_vec4 track_color = bar_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, track, track_color);
		rg_gui_push_rect_outline(ctx, track, ctx->style.color_border, ctx->style.border_thickness);

		RgGuiId bar_id = rg_gui_id_combine(id, 1u);
		rg_vec4 handle_color = (ctx->active_id == bar_id) ? ctx->style.color_accent : ctx->style.color_bg_active;
		rg_gui_push_rect(ctx, handle, handle_color);
	}

	if (scroll)
	{
		*scroll = top;
	}
	return changed;
}

RGINLINE int rg_gui_list_multi(RgGuiContext* ctx, const char* const* items, u32 count,
                               RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                               int* scroll, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(selection != NULL);
	RG_GUI_ASSERT(ops != NULL);
	RG_GUI_ASSERT(ops->is_selected != NULL);
	RG_GUI_ASSERT(ops->set_selected != NULL);
	count = (u32)rg_gui_u32_count_to_int(count);

	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && ctx->tab_focus_id == id && ctx->focus_id != id)
	{
		ctx->focus_id = id;
	}
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);

	rg_gui_selection_state_init(selection);

	f32 item_h = ctx->style.text_height + ctx->style.padding * 2.0f;
	if (item_h < 1.0f)
	{
		item_h = 1.0f;
	}

	f32 border = ctx->style.border_thickness;
	RgGuiRect inner = rg_gui_make_rect(rect.x + border,
	                                   rect.y + border,
	                                   rect.w - border * 2.0f,
	                                   rect.h - border * 2.0f);
	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;

	int visible = rg_gui_nonnegative_f32_to_int_bounded(inner.h / item_h);
	if (visible < 1)
	{
		visible = 1;
	}

	f32 scroll_w = ctx->style.scroll_bar_width;
	f32 scroll_gap = ctx->style.inner_spacing;
	if (scroll_gap < 0.0f)
	{
		scroll_gap = 0.0f;
	}
	int use_scrollbar = (scroll_w > 0.0f && count > (u32)visible);
	if (use_scrollbar)
	{
		inner.w -= scroll_w + scroll_gap;
		if (inner.w < 0.0f)
		{
			inner.w = 0.0f;
		}
	}

	int top = scroll ? *scroll : 0;
	int max_top = (int)count - visible;
	if (max_top < 0)
	{
		max_top = 0;
	}
	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	if (enabled && hovered && max_top > 0)
	{
		ctx->scroll_owner_next = id;
	}

	int cursor = selection->cursor;
	if (cursor >= (int)count)
	{
		cursor = count > 0u ? (int)count - 1 : -1;
	}
	if (cursor < -1)
	{
		cursor = -1;
	}
	selection->cursor = cursor;

	if (enabled && hovered && ctx->mouse_wheel != 0.0f && count > 0u && ctx->scroll_owner == id)
	{
		int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
		if (steps != 0)
		{
			top = rg_gui_i64_clamp_to_int((i64)top - (i64)steps, 0, max_top);
		}
	}

	int changed = 0;
	if (focused && count > 0u)
	{
		int target = cursor;
		if (rg_gui_nav_list_move(ctx, id, (int)count, visible, cursor, &target, NULL))
		{
			u32 flags = RG_GUI_SELECTION_NONE;
			if (ctx->input && rg_gui_input_has_shift(ctx->input))
			{
				flags |= RG_GUI_SELECTION_RANGE;
			}
			changed |= rg_gui_selection_apply(selection, ops, selection_user, count, (u32)target, flags);
			cursor = selection->cursor;
		}

		if (ctx->input && ctx->input->has_text_input && !rg_gui_input_has_ctrl(ctx->input))
		{
			const char* query = ctx->input->text_input_buffer;
			int start_index = (cursor >= 0) ? cursor + 1 : 0;
			int found = rg_gui_find_prefix(items, count, query, start_index);
			if (found < 0 && start_index > 0)
			{
				found = rg_gui_find_prefix(items, count, query, 0);
			}
			if (found >= 0 && found != cursor)
			{
				changed |= rg_gui_selection_apply(selection, ops, selection_user, count, (u32)found, RG_GUI_SELECTION_NONE);
				cursor = selection->cursor;
			}
		}
	}

	if (use_scrollbar && max_top > 0 && inner.h > 0.0f)
	{
		f32 track_x = inner.x + inner.w + scroll_gap;
		f32 track_y = inner.y;
		f32 track_h = inner.h;
		RgGuiRect track = rg_gui_make_rect(track_x, track_y, scroll_w, track_h);

		f32 handle_h = track_h;
		f32 content_h = item_h * (f32)count;
		if (content_h > 0.0f)
		{
			handle_h = track_h * (inner.h / content_h);
		}
		f32 min_handle = ctx->style.text_height * 0.5f;
		if (handle_h < min_handle) handle_h = min_handle;
		if (handle_h > track_h) handle_h = track_h;

		f32 scroll_t = 0.0f;
		if (max_top > 0 && track_h > handle_h)
		{
			scroll_t = (f32)top / (f32)max_top;
		}
		f32 handle_y = track_y + (track_h - handle_h) * scroll_t;
		RgGuiRect handle = rg_gui_make_rect(track_x, handle_y, scroll_w, handle_h);

		RgGuiId bar_id = rg_gui_id_combine(id, 1u);
		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, track);
		if (bar_hovered)
		{
			ctx->hot_id = bar_id;
		}

		if (enabled && ctx->active_id == bar_id)
		{
			ctx->scroll_owner_next = id;
		}

		if (enabled && bar_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = bar_id;
			ctx->focus_id = id;
		}

		if (!enabled && ctx->active_id == bar_id)
		{
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == bar_id)
		{
			if (ctx->mouse_down)
			{
				f32 track_span = track_h - handle_h;
				f32 t = 0.0f;
				if (track_span > 0.0f)
				{
					t = (ctx->mouse_pos.y - track_y - handle_h * 0.5f) / track_span;
				}
				if (t < 0.0f) t = 0.0f;
				if (t > 1.0f) t = 1.0f;
				top = rg_gui_scroll_index_from_ratio(t, max_top);
			}
			if (ctx->mouse_released)
			{
				ctx->active_id = 0u;
			}
		}

		rg_vec4 track_color = bar_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, track, track_color);
		rg_gui_push_rect_outline(ctx, track, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec4 handle_color = (ctx->active_id == bar_id) ? ctx->style.color_accent : ctx->style.color_bg_active;
		rg_gui_push_rect(ctx, handle, handle_color);
	}

	int keep_visible = (scroll == NULL) ? 1 : changed;
	if (keep_visible && cursor >= 0 && count > 0u)
	{
		if (cursor < top)
		{
			top = cursor;
		}
		else if (cursor >= top + visible)
		{
			top = cursor - visible + 1;
		}
	}

	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	u32 start = (u32)top;
	u32 end = start + (u32)visible;
	if (end > count)
	{
		end = count;
	}

	for (u32 i = start; i < end; i++)
	{
		f32 y = inner.y + item_h * (f32)(i - start);
		RgGuiRect item_rect = rg_gui_make_rect(inner.x, y, inner.w, item_h);
		int item_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, item_rect);
		if (item_hovered)
		{
			ctx->hot_id = id;
		}

		if (enabled && item_hovered && ctx->mouse_pressed)
		{
			u32 flags = RG_GUI_SELECTION_NONE;
			if (ctx->input && rg_gui_input_has_ctrl(ctx->input))
			{
				flags |= RG_GUI_SELECTION_TOGGLE;
			}
			if (ctx->input && rg_gui_input_has_shift(ctx->input))
			{
				flags |= RG_GUI_SELECTION_RANGE;
			}
			changed |= rg_gui_selection_apply(selection, ops, selection_user, count, i, flags);
			cursor = selection->cursor;
			ctx->focus_id = id;
		}

		int item_selected = ops->is_selected(selection_user, i) ? 1 : 0;
		rg_vec4 bg = ctx->style.color_bg;
		if (item_selected)
		{
			bg = ctx->style.color_bg_active;
		}
		else if (enabled && item_hovered)
		{
			bg = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect(ctx, item_rect, bg);

		if (items && items[i])
		{
			rg_vec2 pos = rg_vec2(item_rect.x + ctx->style.padding,
			                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
			if (RG_GUI_LABEL_COPY)
			{
				rg_gui_push_text(ctx, items[i], pos, ctx->style.color_text);
			}
			else
			{
				rg_gui_push_text_static(ctx, items[i], pos, ctx->style.color_text);
			}
		}
	}

	if (scroll)
	{
		*scroll = top;
	}
	return changed;
}

RGINLINE int rg_gui_list_clipped(RgGuiContext* ctx, RgGuiPanelState* panel,
                                 RgGuiListItemFn get_item, const void* user, u32 count, int* selected,
                                 f32 row_height, f32 spacing, RgGuiRect rect, RgGuiId id,
                                 RgGuiRowCache* cache, RgGuiListRowHeightFn row_height_fn, int copy_label)
{
	RG_GUI_ASSERT(panel != NULL);
	RG_GUI_ASSERT(selected != NULL);
	count = (u32)rg_gui_u32_count_to_int(count);

	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);

	f32 item_h = row_height;
	if (item_h <= 0.0f)
	{
		item_h = ctx->style.text_height + ctx->style.padding * 2.0f;
	}
	if (item_h < 1.0f)
	{
		item_h = 1.0f;
	}

	f32 row_spacing = spacing;
	if (row_spacing < 0.0f)
	{
		row_spacing = 0.0f;
	}

	int use_cache = 0;
	if (cache)
	{
		use_cache = rg_gui_row_cache_prepare(cache, count, item_h, row_spacing);
		if (!use_cache)
		{
			row_height_fn = NULL;
		}
	}
	else
	{
		row_height_fn = NULL;
	}

	int sel = *selected;
	if (sel >= (int)count)
	{
		sel = count > 0u ? (int)count - 1 : -1;
	}
	if (sel < -1)
	{
		sel = -1;
	}

	f32 border = ctx->style.border_thickness;
	f32 pad = ctx->style.padding;
	f32 inset = border + pad;
	f32 view_h = rect.h - inset * 2.0f;
	if (view_h < 0.0f)
	{
		view_h = 0.0f;
	}

	f32 row_stride = item_h + row_spacing;
	if (row_stride < 1.0f)
	{
		row_stride = 1.0f;
	}

	int changed = 0;
	int keep_visible = 0;

	if (focused && count > 0u)
	{
		int visible = rg_gui_nonnegative_f32_to_int_bounded(view_h / row_stride);
		if (visible < 1)
		{
			visible = 1;
		}
		if (visible > (int)count)
		{
			visible = (int)count;
		}

		int target = sel;
		if (rg_gui_nav_list_move(ctx, id, (int)count, visible, sel, &target, NULL))
		{
			sel = target;
			changed = 1;
			keep_visible = 1;
		}

		if (ctx->input && ctx->input->has_text_input && !rg_gui_input_has_ctrl(ctx->input))
		{
			const char* query = ctx->input->text_input_buffer;
			int start_index = (sel >= 0) ? sel + 1 : 0;
			int found = rg_gui_find_prefix_fn(get_item, user, count, query, start_index);
			if (found < 0 && start_index > 0)
			{
				found = rg_gui_find_prefix_fn(get_item, user, count, query, 0);
			}
			if (found >= 0 && found != sel)
			{
				sel = found;
				changed = 1;
				keep_visible = 1;
			}
		}
	}

	f32 total_height = 0.0f;
	if (count > 0u)
	{
		if (use_cache)
		{
			total_height = rg_gui_row_cache_total(cache);
		}
		else
		{
			total_height = row_stride * (f32)count;
		}
	}
	f32 max_scroll = total_height - view_h;
	if (max_scroll < 0.0f)
	{
		max_scroll = 0.0f;
	}

	if (panel->scroll_y < 0.0f) panel->scroll_y = 0.0f;
	if (panel->scroll_y > max_scroll) panel->scroll_y = max_scroll;

	if (keep_visible && sel >= 0 && count > 0u)
	{
		f32 row_top = use_cache ? rg_gui_row_cache_offset(cache, (u32)sel) : row_stride * (f32)sel;
		f32 row_span = use_cache ? rg_gui_row_cache_row_span(cache, (u32)sel) : row_stride;
		f32 row_h = row_span - row_spacing;
		if (row_h <= 0.0f)
		{
			row_h = item_h;
		}
		f32 row_bottom = row_top + row_h;

		if (row_top < panel->scroll_y)
		{
			panel->scroll_y = row_top;
		}
		else if (row_bottom > panel->scroll_y + view_h)
		{
			panel->scroll_y = row_bottom - view_h;
		}

		if (panel->scroll_y < 0.0f) panel->scroll_y = 0.0f;
		if (panel->scroll_y > max_scroll) panel->scroll_y = max_scroll;
	}

	u32 start = 0u;
	u32 visible_count = 0u;
	rg_gui_panel_begin_clipped(ctx, panel, rect, item_h, row_spacing, count, id, cache, &start, &visible_count);

	u32 end = start + visible_count;
	for (u32 i = start; i < end; i++)
	{
		f32 row_h = item_h;
		if (row_height_fn)
		{
			row_h = row_height_fn(user, i, item_h);
			if (row_h <= 0.0f)
			{
				row_h = item_h;
			}
			if (cache)
			{
				rg_gui_row_cache_set_height(cache, i, row_h);
			}
		}

		RgGuiRect item_rect = rg_gui_layout_next(ctx, row_h);
		int item_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, item_rect);
		if (item_hovered)
		{
			ctx->hot_id = id;
		}

		if (enabled && item_hovered && ctx->mouse_pressed)
		{
			sel = (int)i;
			changed = 1;
			ctx->focus_id = id;
		}

		rg_vec4 bg = ctx->style.color_bg;
		if ((int)i == sel)
		{
			bg = ctx->style.color_bg_active;
		}
		else if (enabled && item_hovered)
		{
			bg = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect(ctx, item_rect, bg);

		if (get_item)
		{
			const char* item = get_item(user, i);
			if (item)
			{
				rg_vec2 pos = rg_vec2(item_rect.x + ctx->style.padding,
				                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
				if (copy_label)
				{
					rg_gui_push_text(ctx, item, pos, ctx->style.color_text);
				}
				else
				{
					rg_gui_push_text_static(ctx, item, pos, ctx->style.color_text);
				}
			}
		}
	}

	rg_gui_panel_end(ctx, panel);

	*selected = sel;
	return changed;
}

RGINLINE int rg_gui_list_multi_clipped(RgGuiContext* ctx, RgGuiPanelState* panel,
                                       RgGuiListItemFn get_item, const void* user, u32 count,
                                       RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                                       f32 row_height, f32 spacing, RgGuiRect rect, RgGuiId id,
                                       RgGuiRowCache* cache, RgGuiListRowHeightFn row_height_fn, int copy_label)
{
	RG_GUI_ASSERT(panel != NULL);
	RG_GUI_ASSERT(selection != NULL);
	RG_GUI_ASSERT(ops != NULL);
	RG_GUI_ASSERT(ops->is_selected != NULL);
	RG_GUI_ASSERT(ops->set_selected != NULL);
	count = (u32)rg_gui_u32_count_to_int(count);

	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	if (enabled && ctx->tab_focus_id == id && ctx->focus_id != id)
	{
		ctx->focus_id = id;
	}
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);

	rg_gui_selection_state_init(selection);

	f32 item_h = row_height;
	if (item_h <= 0.0f)
	{
		item_h = ctx->style.text_height + ctx->style.padding * 2.0f;
	}
	if (item_h < 1.0f)
	{
		item_h = 1.0f;
	}

	f32 row_spacing = spacing;
	if (row_spacing < 0.0f)
	{
		row_spacing = 0.0f;
	}

	int use_cache = 0;
	if (cache)
	{
		use_cache = rg_gui_row_cache_prepare(cache, count, item_h, row_spacing);
		if (!use_cache)
		{
			row_height_fn = NULL;
		}
	}
	else
	{
		row_height_fn = NULL;
	}

	int cursor = selection->cursor;
	if (cursor >= (int)count)
	{
		cursor = count > 0u ? (int)count - 1 : -1;
	}
	if (cursor < -1)
	{
		cursor = -1;
	}
	selection->cursor = cursor;

	f32 border = ctx->style.border_thickness;
	f32 pad = ctx->style.padding;
	f32 inset = border + pad;
	f32 view_h = rect.h - inset * 2.0f;
	if (view_h < 0.0f)
	{
		view_h = 0.0f;
	}

	f32 row_stride = item_h + row_spacing;
	if (row_stride < 1.0f)
	{
		row_stride = 1.0f;
	}

	int changed = 0;
	int keep_visible = 0;

	if (focused && count > 0u)
	{
		int visible = rg_gui_nonnegative_f32_to_int_bounded(view_h / row_stride);
		if (visible < 1)
		{
			visible = 1;
		}
		if (visible > (int)count)
		{
			visible = (int)count;
		}

		int target = cursor;
		if (rg_gui_nav_list_move(ctx, id, (int)count, visible, cursor, &target, NULL))
		{
			u32 flags = RG_GUI_SELECTION_NONE;
			if (ctx->input && rg_gui_input_has_shift(ctx->input))
			{
				flags |= RG_GUI_SELECTION_RANGE;
			}
			changed |= rg_gui_selection_apply(selection, ops, selection_user, count, (u32)target, flags);
			cursor = selection->cursor;
			keep_visible = 1;
		}

		if (ctx->input && ctx->input->has_text_input && !rg_gui_input_has_ctrl(ctx->input))
		{
			const char* query = ctx->input->text_input_buffer;
			int start_index = (cursor >= 0) ? cursor + 1 : 0;
			int found = rg_gui_find_prefix_fn(get_item, user, count, query, start_index);
			if (found < 0 && start_index > 0)
			{
				found = rg_gui_find_prefix_fn(get_item, user, count, query, 0);
			}
			if (found >= 0 && found != cursor)
			{
				changed |= rg_gui_selection_apply(selection, ops, selection_user, count, (u32)found, RG_GUI_SELECTION_NONE);
				cursor = selection->cursor;
				keep_visible = 1;
			}
		}
	}

	f32 total_height = 0.0f;
	if (count > 0u)
	{
		if (use_cache)
		{
			total_height = rg_gui_row_cache_total(cache);
		}
		else
		{
			total_height = row_stride * (f32)count;
		}
	}
	f32 max_scroll = total_height - view_h;
	if (max_scroll < 0.0f)
	{
		max_scroll = 0.0f;
	}

	if (panel->scroll_y < 0.0f) panel->scroll_y = 0.0f;
	if (panel->scroll_y > max_scroll) panel->scroll_y = max_scroll;

	if (keep_visible && cursor >= 0 && count > 0u)
	{
		f32 row_top = use_cache ? rg_gui_row_cache_offset(cache, (u32)cursor) : row_stride * (f32)cursor;
		f32 row_span = use_cache ? rg_gui_row_cache_row_span(cache, (u32)cursor) : row_stride;
		f32 row_h = row_span - row_spacing;
		if (row_h <= 0.0f)
		{
			row_h = item_h;
		}
		f32 row_bottom = row_top + row_h;

		if (row_top < panel->scroll_y)
		{
			panel->scroll_y = row_top;
		}
		else if (row_bottom > panel->scroll_y + view_h)
		{
			panel->scroll_y = row_bottom - view_h;
		}

		if (panel->scroll_y < 0.0f) panel->scroll_y = 0.0f;
		if (panel->scroll_y > max_scroll) panel->scroll_y = max_scroll;
	}

	u32 start = 0u;
	u32 visible_count = 0u;
	rg_gui_panel_begin_clipped(ctx, panel, rect, item_h, row_spacing, count, id, cache, &start, &visible_count);

	u32 end = start + visible_count;
	for (u32 i = start; i < end; i++)
	{
		f32 row_h = item_h;
		if (row_height_fn)
		{
			row_h = row_height_fn(user, i, item_h);
			if (row_h <= 0.0f)
			{
				row_h = item_h;
			}
			if (cache)
			{
				rg_gui_row_cache_set_height(cache, i, row_h);
			}
		}

		RgGuiRect item_rect = rg_gui_layout_next(ctx, row_h);
		int item_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, item_rect);
		if (item_hovered)
		{
			ctx->hot_id = id;
		}

		if (enabled && item_hovered && ctx->mouse_pressed)
		{
			u32 flags = RG_GUI_SELECTION_NONE;
			if (ctx->input && rg_gui_input_has_ctrl(ctx->input))
			{
				flags |= RG_GUI_SELECTION_TOGGLE;
			}
			if (ctx->input && rg_gui_input_has_shift(ctx->input))
			{
				flags |= RG_GUI_SELECTION_RANGE;
			}
			changed |= rg_gui_selection_apply(selection, ops, selection_user, count, i, flags);
			cursor = selection->cursor;
			ctx->focus_id = id;
		}

		int item_selected = ops->is_selected(selection_user, i) ? 1 : 0;
		rg_vec4 bg = ctx->style.color_bg;
		if (item_selected)
		{
			bg = ctx->style.color_bg_active;
		}
		else if (enabled && item_hovered)
		{
			bg = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect(ctx, item_rect, bg);

		if (get_item)
		{
			const char* item = get_item(user, i);
			if (item)
			{
				rg_vec2 pos = rg_vec2(item_rect.x + ctx->style.padding,
				                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
				if (copy_label)
				{
					rg_gui_push_text(ctx, item, pos, ctx->style.color_text);
				}
				else
				{
					rg_gui_push_text_static(ctx, item, pos, ctx->style.color_text);
				}
			}
		}
	}

	rg_gui_panel_end(ctx, panel);

	selection->cursor = cursor;
	return changed;
}

RGINLINE int rg_gui_menu_popup_icons_ex(RgGuiContext* ctx, const char* const* items, const char* const* shortcuts,
                                        const RgGuiIcon* icons, const RgGuiMenuItemFlags* flags,
                                        u32 count, int* selected, int* open, int* scroll,
                                        RgGuiRect rect, RgGuiId id, int* out_hovered,
                                        RgGuiRect* out_hovered_rect, int* out_list_hovered,
                                        RgGuiRect* out_selected_rect, int allow_keyboard)
{
	RG_GUI_ASSERT(selected != NULL);
	RG_GUI_ASSERT(open != NULL);
	count = (u32)rg_gui_u32_count_to_int(count);

	if (out_hovered)
	{
		*out_hovered = -1;
	}
	if (out_hovered_rect)
	{
		*out_hovered_rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	}
	if (out_list_hovered)
	{
		*out_list_hovered = 0;
	}
	if (out_selected_rect)
	{
		*out_selected_rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	}

	if (!ctx || !open || !*open)
	{
		return 0;
	}

	RgGuiId menu_id = rg_gui_id_scoped(ctx, id);
	RgGuiId list_id = rg_gui_id_combine(menu_id, 0x4D454E55u);

	int enabled = !rg_gui_is_disabled(ctx);

	int open_state = *open ? 1 : 0;
	if (enabled && ctx->input && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_ESCAPE))
	{
		open_state = 0;
	}
	if (enabled && ctx->tab_dir != 0)
	{
		open_state = 0;
	}
	if (!open_state)
	{
		*open = 0;
		return 0;
	}

	if (enabled)
	{
		ctx->focus_id = list_id;
	}
	int focused = enabled && (ctx->focus_id == list_id);

	rg_gui_input_capture_begin(ctx);

	f32 pad = ctx->style.padding;
	f32 spacing = ctx->style.inner_spacing;
	if (spacing < 0.0f)
	{
		spacing = 0.0f;
	}

	f32 max_label = 0.0f;
	f32 max_shortcut = 0.0f;
	u32 flags_mask = 0u;
	rg_gui_menu_max_widths(ctx, menu_id, items, shortcuts, flags, count, &max_label, &max_shortcut, &flags_mask);

	int has_indicator = (flags_mask & (RG_GUI_MENU_ITEM_CHECKABLE | RG_GUI_MENU_ITEM_RADIO)) != 0u;
	int has_submenu = (flags_mask & RG_GUI_MENU_ITEM_SUBMENU) != 0u;
	f32 indicator_w = has_indicator ? ctx->style.text_height : 0.0f;
	int has_icons = 0;
	if (icons)
	{
		for (u32 i = 0u; i < count; i++)
		{
			if (rg_gui_icon_valid(&icons[i]))
			{
				has_icons = 1;
				break;
			}
		}
	}
	f32 icon_w = has_icons ? ctx->style.text_height : 0.0f;
	f32 submenu_w = has_submenu ? ctx->style.text_height : 0.0f;

	f32 right_w = 0.0f;
	if (max_shortcut > 0.0f)
	{
		right_w += max_shortcut;
	}
	if (submenu_w > 0.0f)
	{
		right_w += (right_w > 0.0f ? spacing : 0.0f) + submenu_w;
	}

	f32 menu_w = rect.w;
	if (menu_w <= 0.0f)
	{
		f32 content_w = max_label;
		if (indicator_w > 0.0f)
		{
			content_w += indicator_w + spacing;
		}
		if (icon_w > 0.0f)
		{
			content_w += icon_w + spacing;
		}
		if (right_w > 0.0f)
		{
			content_w += right_w + spacing;
		}
		menu_w = content_w + pad * 2.0f;
		if (menu_w < 1.0f)
		{
			menu_w = 1.0f;
		}
	}

	f32 item_h = ctx->style.text_height + pad * 2.0f;
	if (item_h < 1.0f)
	{
		item_h = 1.0f;
	}

	f32 border = ctx->style.border_thickness;

	u32 max_visible = count;
	if (rect.h > 0.0f)
	{
		max_visible = rg_gui_nonnegative_f32_to_u32_bounded(rect.h / item_h);
	}
	if (max_visible < 1u)
	{
		max_visible = 1u;
	}
	if (count > 0u && max_visible > count)
	{
		max_visible = count;
	}

	f32 list_h = (rect.h > 0.0f) ? rect.h : (item_h * (f32)max_visible + border * 2.0f);
	RgGuiRect list_rect = rg_gui_make_rect(rect.x, rect.y, menu_w, list_h);
	int list_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, list_rect);
	if (out_list_hovered)
	{
		*out_list_hovered = list_hovered;
	}
	int consume_click = enabled && list_hovered && ctx->mouse_pressed;

	RgGuiRect inner = rg_gui_make_rect(list_rect.x + border,
	                                   list_rect.y + border,
	                                   list_rect.w - border * 2.0f,
	                                   list_rect.h - border * 2.0f);
	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;

	int visible_rows = rg_gui_nonnegative_f32_to_int_bounded(inner.h / item_h);
	if (visible_rows < 1)
	{
		visible_rows = 1;
	}
	if (visible_rows > (int)count && count > 0u)
	{
		visible_rows = (int)count;
	}

	f32 scroll_w = ctx->style.scroll_bar_width;
	f32 scroll_gap = spacing;
	int use_scrollbar = (scroll_w > 0.0f && count > (u32)visible_rows);
	if (use_scrollbar)
	{
		inner.w -= scroll_w + scroll_gap;
		if (inner.w < 0.0f)
		{
			inner.w = 0.0f;
		}
	}

	int top = scroll ? *scroll : 0;
	int max_top = (int)count - visible_rows;
	if (max_top < 0)
	{
		max_top = 0;
	}
	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	if (enabled && list_hovered && max_top > 0)
	{
		ctx->scroll_owner_next = list_id;
	}

	int sel = *selected;
	if (sel >= (int)count)
	{
		sel = count > 0u ? (int)count - 1 : -1;
	}
	if (sel < -1)
	{
		sel = -1;
	}

	if (enabled && list_hovered && ctx->mouse_wheel != 0.0f && ctx->scroll_owner == list_id)
	{
		int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
		if (steps != 0)
		{
			top = rg_gui_i64_clamp_to_int((i64)top - (i64)steps, 0, max_top);
		}
	}

	int changed = 0;
	if (allow_keyboard && focused && count > 0u)
	{
		int nav_dir = 0;
		int target = sel;
		if (rg_gui_nav_list_move(ctx, list_id, (int)count, visible_rows, sel, &target, &nav_dir))
		{
			sel = target;
			if (flags)
			{
				int dir = (nav_dir != 0) ? nav_dir : 1;
				int next = rg_gui_menu_find_enabled(flags, count, sel, dir);
				if (next >= 0 && next < (int)count)
				{
					sel = next;
				}
			}
			changed = 1;
		}

		if (ctx->input && ctx->input->has_text_input && !rg_gui_input_has_ctrl(ctx->input))
		{
			const char* query = ctx->input->text_input_buffer;
			int start_index = (sel >= 0) ? sel + 1 : 0;
			int found = rg_gui_find_prefix(items, count, query, start_index);
			if (found < 0 && start_index > 0)
			{
				found = rg_gui_find_prefix(items, count, query, 0);
			}
			if (found >= 0 && flags)
			{
				int resolved = rg_gui_menu_find_enabled(flags, count, found, 1);
				if (resolved >= 0 && resolved < (int)count &&
				    (flags[resolved] & RG_GUI_MENU_ITEM_DISABLED) == 0u)
				{
					found = resolved;
				}
				else
				{
					found = -1;
				}
			}
			if (found >= 0 && found != sel)
			{
				sel = found;
				changed = 1;
			}
		}

		if (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
		    rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER))
		{
			if (sel >= 0 && count > 0u)
			{
				RgGuiMenuItemFlags item_flags = flags ? flags[sel] : RG_GUI_MENU_ITEM_NONE;
				if ((item_flags & RG_GUI_MENU_ITEM_DISABLED) == 0u)
				{
					changed = 1;
					if ((item_flags & RG_GUI_MENU_ITEM_SUBMENU) == 0u)
					{
						open_state = 0;
					}
				}
			}
		}
	}

	if (use_scrollbar && max_top > 0 && inner.h > 0.0f)
	{
		f32 track_x = inner.x + inner.w + scroll_gap;
		f32 track_y = inner.y;
		f32 track_h = inner.h;
		f32 handle_h = track_h;
		f32 content_h = item_h * (f32)count;
		if (content_h > 0.0f)
		{
			handle_h = track_h * (inner.h / content_h);
		}
		f32 min_handle = ctx->style.text_height * 0.5f;
		if (handle_h < min_handle) handle_h = min_handle;
		if (handle_h > track_h) handle_h = track_h;

		RgGuiId bar_id = rg_gui_id_combine(menu_id, 0x5343524Cu);
		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y,
		                                                  rg_gui_make_rect(track_x, track_y, scroll_w, track_h));
		if (bar_hovered)
		{
			ctx->hot_id = bar_id;
		}

		if (enabled && ctx->active_id == bar_id)
		{
			ctx->scroll_owner_next = id;
		}

		if (enabled && bar_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = bar_id;
			ctx->focus_id = id;
		}

		if (!enabled && ctx->active_id == bar_id)
		{
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == bar_id)
		{
			if (ctx->mouse_down)
			{
				f32 track_span = track_h - handle_h;
				f32 t = 0.0f;
				if (track_span > 0.0f)
				{
					t = (ctx->mouse_pos.y - track_y - handle_h * 0.5f) / track_span;
				}
				if (t < 0.0f) t = 0.0f;
				if (t > 1.0f) t = 1.0f;
				top = rg_gui_scroll_index_from_ratio(t, max_top);
			}
			if (ctx->mouse_released)
			{
				ctx->active_id = 0u;
			}
		}
	}

	int keep_visible = (scroll == NULL) ? 1 : changed;
	if (keep_visible && sel >= 0 && count > 0u)
	{
		if (sel < top)
		{
			top = sel;
		}
		else if (sel >= top + visible_rows)
		{
			top = sel - visible_rows + 1;
		}
	}

	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	RgGuiDrawList* overlay = rg_gui_overlay_target(ctx);
	rg_gui_push_rect_to(ctx, overlay, list_rect, ctx->style.color_panel);
	rg_gui_push_rect_outline_to(ctx, overlay, list_rect,
	                            ctx->style.color_border,
	                            ctx->style.border_thickness);

	u32 start = (u32)top;
	u32 end = start + (u32)visible_rows;
	if (end > count)
	{
		end = count;
	}

	int hovered_index = -1;
	RgGuiRect hovered_rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	int selected_visible = 0;
	RgGuiRect selected_rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);

	for (u32 i = start; i < end; i++)
	{
		f32 y = inner.y + item_h * (f32)(i - start);
		RgGuiRect item_rect = rg_gui_make_rect(inner.x, y, inner.w, item_h);
		RgGuiMenuItemFlags item_flags = flags ? flags[i] : RG_GUI_MENU_ITEM_NONE;
		int item_disabled = (item_flags & RG_GUI_MENU_ITEM_DISABLED) != 0u;
		int item_enabled = enabled && !item_disabled;
		int item_hovered = item_enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, item_rect);
		if (item_hovered)
		{
			ctx->hot_id = list_id;
			hovered_index = (int)i;
			hovered_rect = item_rect;
		}

		if ((int)i == sel)
		{
			selected_visible = 1;
			selected_rect = item_rect;
		}

		if (item_enabled && item_hovered && ctx->mouse_pressed)
		{
			sel = (int)i;
			changed = 1;
			ctx->focus_id = list_id;
			if ((item_flags & RG_GUI_MENU_ITEM_SUBMENU) == 0u)
			{
				open_state = 0;
			}
		}

		rg_vec4 bg = ctx->style.color_bg;
		if (!item_disabled)
		{
			if ((int)i == sel)
			{
				bg = ctx->style.color_bg_active;
			}
			else if (item_hovered)
			{
				bg = ctx->style.color_bg_hover;
			}
		}
		rg_gui_push_rect_to(ctx, overlay, item_rect, bg);

		f32 content_x = item_rect.x + pad;

		if (indicator_w > 0.0f)
		{
			f32 indicator_x = content_x;
			f32 indicator_y = item_rect.y + (item_rect.h - indicator_w) * 0.5f;
			if ((item_flags & (RG_GUI_MENU_ITEM_CHECKABLE | RG_GUI_MENU_ITEM_RADIO)) != 0u &&
			    (item_flags & RG_GUI_MENU_ITEM_CHECKED) != 0u)
			{
				f32 inset = (item_flags & RG_GUI_MENU_ITEM_RADIO) ? 4.0f : 3.0f;
				rg_vec4 indicator_color = item_disabled ? ctx->style.color_text_dim : ctx->style.color_accent;
				RgGuiRect fill = rg_gui_make_rect(indicator_x + inset, indicator_y + inset,
				                                  indicator_w - inset * 2.0f,
				                                  indicator_w - inset * 2.0f);
				rg_gui_push_rect_to(ctx, overlay, fill, indicator_color);
			}
		}

		f32 text_x = content_x;
		if (indicator_w > 0.0f)
		{
			text_x += indicator_w + spacing;
		}

		if (icon_w > 0.0f)
		{
			if (icons && rg_gui_icon_valid(&icons[i]))
			{
				RgGuiRect icon_bounds = rg_gui_make_rect(text_x, item_rect.y, icon_w, item_rect.h);
				RgGuiRect icon_rect = rg_gui_icon_rect(ctx, icon_bounds, text_x);
				rg_gui_push_icon_to(ctx, overlay, &icons[i], icon_rect, item_disabled);
			}
			text_x += icon_w + spacing;
		}

		rg_vec4 text_color = item_disabled ? ctx->style.color_text_dim : ctx->style.color_text;
		if (items && items[i])
		{
			rg_vec2 pos = rg_vec2(text_x,
			                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
			if (RG_GUI_LABEL_COPY)
			{
				rg_gui_push_text_to(ctx, overlay, items[i], pos, text_color);
			}
			else
			{
				rg_gui_push_text_static_to(ctx, overlay, items[i], pos, text_color);
			}
		}

		if (shortcuts && shortcuts[i])
		{
			size_t len = rg_gui_text_len_label(ctx, shortcuts[i], RG_GUI_LABEL_COPY);
			if (len > 0u)
			{
				f32 shortcut_w = rg_gui_text_measure_label(ctx, shortcuts[i], len, RG_GUI_LABEL_COPY);
				f32 right_edge = item_rect.x + item_rect.w - pad;
				if (submenu_w > 0.0f)
				{
					right_edge -= submenu_w + spacing;
				}
				f32 shortcut_x = right_edge - shortcut_w;
				rg_vec2 pos = rg_vec2(shortcut_x,
				                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
				rg_vec4 shortcut_color = ctx->style.color_text_dim;
				if (RG_GUI_LABEL_COPY)
				{
					rg_gui_push_text_to(ctx, overlay, shortcuts[i], pos, shortcut_color);
				}
				else
				{
					rg_gui_push_text_static_to(ctx, overlay, shortcuts[i], pos, shortcut_color);
				}
			}
		}

		if (submenu_w > 0.0f && (item_flags & RG_GUI_MENU_ITEM_SUBMENU) != 0u)
		{
			f32 arrow_x = item_rect.x + item_rect.w - pad - submenu_w;
			RgGuiRect arrow_rect = rg_gui_make_rect(arrow_x, item_rect.y,
			                                        submenu_w, item_rect.h);
			rg_vec4 arrow_color = ctx->style.color_text_dim;
			rg_gui_push_arrow_to(ctx, overlay, arrow_rect, RG_GUI_ARROW_RIGHT, arrow_color);
		}
	}

	if (use_scrollbar && max_top > 0 && inner.h > 0.0f)
	{
		f32 track_x = inner.x + inner.w + scroll_gap;
		f32 track_y = inner.y;
		f32 track_h = inner.h;
		RgGuiRect track = rg_gui_make_rect(track_x, track_y, scroll_w, track_h);

		f32 handle_h = track_h;
		f32 content_h = item_h * (f32)count;
		if (content_h > 0.0f)
		{
			handle_h = track_h * (inner.h / content_h);
		}
		f32 min_handle = ctx->style.text_height * 0.5f;
		if (handle_h < min_handle) handle_h = min_handle;
		if (handle_h > track_h) handle_h = track_h;

		f32 scroll_t = 0.0f;
		if (max_top > 0 && track_h > handle_h)
		{
			scroll_t = (f32)top / (f32)max_top;
		}
		f32 handle_y = track_y + (track_h - handle_h) * scroll_t;
		RgGuiRect handle = rg_gui_make_rect(track_x, handle_y, scroll_w, handle_h);

		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, track);
		rg_vec4 track_color = bar_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect_to(ctx, overlay, track, track_color);
		rg_gui_push_rect_outline_to(ctx, overlay, track, ctx->style.color_border, ctx->style.border_thickness);

		RgGuiId bar_id = rg_gui_id_combine(menu_id, 0x5343524Cu);
		rg_vec4 handle_color = (ctx->active_id == bar_id) ? ctx->style.color_accent : ctx->style.color_bg_active;
		rg_gui_push_rect_to(ctx, overlay, handle, handle_color);
	}

	if (scroll)
	{
		*scroll = top;
	}

	if (enabled && ctx->input)
	{
		int right_pressed = rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_RIGHT);
		if ((ctx->mouse_pressed || right_pressed) && !list_hovered &&
		    ctx->hot_id != list_id && ctx->hot_id != menu_id)
		{
			open_state = 0;
		}
	}

	if (consume_click)
	{
		ctx->mouse_pressed = 0;
	}

	if (out_hovered)
	{
		if (hovered_index >= 0)
		{
			*out_hovered = hovered_index;
			if (out_hovered_rect)
			{
				*out_hovered_rect = hovered_rect;
			}
		}
		else if (focused && selected_visible && sel >= 0 && sel < (int)count)
		{
			*out_hovered = sel;
			if (out_hovered_rect)
			{
				*out_hovered_rect = selected_rect;
			}
		}
	}
	if (out_selected_rect)
	{
		*out_selected_rect = selected_rect;
	}

	if (menu_id == ctx->menu_bar_id)
	{
		int block_right = 0;
		int index = -1;
		if (focused && selected_visible && sel >= 0 && sel < (int)count)
		{
			index = sel;
		}
		else if (hovered_index >= 0)
		{
			index = hovered_index;
		}

		if (index >= 0 && flags)
		{
			RgGuiMenuItemFlags item_flags = flags[index];
			if ((item_flags & RG_GUI_MENU_ITEM_SUBMENU) != 0u)
			{
				block_right = 1;
			}
		}

		ctx->menu_bar_block_left = 0;
		ctx->menu_bar_block_right = block_right;
	}

	*selected = sel;
	*open = open_state;
	rg_gui_input_capture_end(ctx, open_state);
	return changed;
}

RGINLINE int rg_gui_menu_popup_ex(RgGuiContext* ctx, const char* const* items, const char* const* shortcuts,
                                  const RgGuiMenuItemFlags* flags, u32 count, int* selected,
                                  int* open, int* scroll, RgGuiRect rect, RgGuiId id,
                                  int* out_hovered, RgGuiRect* out_hovered_rect, int* out_list_hovered,
                                  RgGuiRect* out_selected_rect, int allow_keyboard)
{
	return rg_gui_menu_popup_icons_ex(ctx, items, shortcuts, NULL, flags, count, selected, open, scroll,
	                                  rect, id, out_hovered, out_hovered_rect, out_list_hovered,
	                                  out_selected_rect, allow_keyboard);
}

RGINLINE int rg_gui_menu_popup(RgGuiContext* ctx, const char* const* items, u32 count, int* selected,
                               int* open, int* scroll, RgGuiRect rect, RgGuiId id)
{
	return rg_gui_menu_popup_ex(ctx, items, NULL, NULL, count, selected, open, scroll, rect, id,
	                            NULL, NULL, NULL, NULL, 1);
}

RGINLINE int rg_gui_list_virtual(RgGuiContext* ctx, RgGuiListItemFn get_item, const void* user, u32 count, int* selected, int* scroll, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(selected != NULL);
	count = (u32)rg_gui_u32_count_to_int(count);

	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);

	f32 item_h = ctx->style.text_height + ctx->style.padding * 2.0f;
	if (item_h < 1.0f)
	{
		item_h = 1.0f;
	}

	f32 border = ctx->style.border_thickness;
	RgGuiRect inner = rg_gui_make_rect(rect.x + border,
	                                   rect.y + border,
	                                   rect.w - border * 2.0f,
	                                   rect.h - border * 2.0f);
	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;

	int visible = rg_gui_nonnegative_f32_to_int_bounded(inner.h / item_h);
	if (visible < 1)
	{
		visible = 1;
	}

	f32 scroll_w = ctx->style.scroll_bar_width;
	f32 scroll_gap = ctx->style.inner_spacing;
	if (scroll_gap < 0.0f)
	{
		scroll_gap = 0.0f;
	}
	int use_scrollbar = (scroll_w > 0.0f && count > (u32)visible);
	if (use_scrollbar)
	{
		inner.w -= scroll_w + scroll_gap;
		if (inner.w < 0.0f)
		{
			inner.w = 0.0f;
		}
	}

	int top = scroll ? *scroll : 0;
	int max_top = (int)count - visible;
	if (max_top < 0)
	{
		max_top = 0;
	}
	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	if (enabled && hovered && max_top > 0)
	{
		ctx->scroll_owner_next = id;
	}

	int sel = *selected;
	if (sel >= (int)count)
	{
		sel = count > 0u ? (int)count - 1 : -1;
	}
	if (sel < -1)
	{
		sel = -1;
	}

	if (enabled && hovered && ctx->mouse_wheel != 0.0f && count > 0u && ctx->scroll_owner == id)
	{
		int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
		if (steps != 0)
		{
			top = rg_gui_i64_clamp_to_int((i64)top - (i64)steps, 0, max_top);
		}
	}

	int changed = 0;
	if (focused && count > 0u)
	{
		int target = sel;
		if (rg_gui_nav_list_move(ctx, id, (int)count, visible, sel, &target, NULL))
		{
			sel = target;
			changed = 1;
		}

		if (ctx->input && ctx->input->has_text_input && !rg_gui_input_has_ctrl(ctx->input))
		{
			const char* query = ctx->input->text_input_buffer;
			int start_index = (sel >= 0) ? sel + 1 : 0;
			int found = rg_gui_find_prefix_fn(get_item, user, count, query, start_index);
			if (found < 0 && start_index > 0)
			{
				found = rg_gui_find_prefix_fn(get_item, user, count, query, 0);
			}
			if (found >= 0 && found != sel)
			{
				sel = found;
				changed = 1;
			}
		}
	}

	if (use_scrollbar && max_top > 0 && inner.h > 0.0f)
	{
		f32 track_x = inner.x + inner.w + scroll_gap;
		f32 track_y = inner.y;
		f32 track_h = inner.h;
		RgGuiRect track = rg_gui_make_rect(track_x, track_y, scroll_w, track_h);

		f32 handle_h = track_h;
		f32 content_h = item_h * (f32)count;
		if (content_h > 0.0f)
		{
			handle_h = track_h * (inner.h / content_h);
		}
		f32 min_handle = ctx->style.text_height * 0.5f;
		if (handle_h < min_handle) handle_h = min_handle;
		if (handle_h > track_h) handle_h = track_h;

		f32 scroll_t = 0.0f;
		if (max_top > 0 && track_h > handle_h)
		{
			scroll_t = (f32)top / (f32)max_top;
		}
		f32 handle_y = track_y + (track_h - handle_h) * scroll_t;
		RgGuiRect handle = rg_gui_make_rect(track_x, handle_y, scroll_w, handle_h);

		RgGuiId bar_id = rg_gui_id_combine(id, 1u);
		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, track);
		if (bar_hovered)
		{
			ctx->hot_id = bar_id;
		}

		if (enabled && ctx->active_id == bar_id)
		{
			ctx->scroll_owner_next = id;
		}

		if (enabled && bar_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = bar_id;
			ctx->focus_id = id;
		}

		if (!enabled && ctx->active_id == bar_id)
		{
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == bar_id)
		{
			if (ctx->mouse_down)
			{
				f32 track_span = track_h - handle_h;
				f32 t = 0.0f;
				if (track_span > 0.0f)
				{
					t = (ctx->mouse_pos.y - track_y - handle_h * 0.5f) / track_span;
				}
				if (t < 0.0f) t = 0.0f;
				if (t > 1.0f) t = 1.0f;
				top = rg_gui_scroll_index_from_ratio(t, max_top);
			}
			if (ctx->mouse_released)
			{
				ctx->active_id = 0u;
			}
		}

		rg_vec4 track_color = bar_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, track, track_color);
		rg_gui_push_rect_outline(ctx, track, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec4 handle_color = (ctx->active_id == bar_id) ? ctx->style.color_accent : ctx->style.color_bg_active;
		rg_gui_push_rect(ctx, handle, handle_color);
	}

	int keep_visible = (scroll == NULL) ? 1 : changed;
	if (keep_visible && sel >= 0 && count > 0u)
	{
		if (sel < top)
		{
			top = sel;
		}
		else if (sel >= top + visible)
		{
			top = sel - visible + 1;
		}
	}

	if (top < 0)
	{
		top = 0;
	}
	if (top > max_top)
	{
		top = max_top;
	}

	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	u32 start = (u32)top;
	u32 end = start + (u32)visible;
	if (end > count)
	{
		end = count;
	}

	for (u32 i = start; i < end; i++)
	{
		f32 y = inner.y + item_h * (f32)(i - start);
		RgGuiRect item_rect = rg_gui_make_rect(inner.x, y, inner.w, item_h);
		int item_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, item_rect);
		if (item_hovered)
		{
			ctx->hot_id = id;
		}

		if (enabled && item_hovered && ctx->mouse_pressed)
		{
			sel = (int)i;
			changed = 1;
			ctx->focus_id = id;
		}

		rg_vec4 bg = ctx->style.color_bg;
		if ((int)i == sel)
		{
			bg = ctx->style.color_bg_active;
		}
		else if (enabled && item_hovered)
		{
			bg = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect(ctx, item_rect, bg);

		if (get_item)
		{
			const char* item = get_item(user, i);
			if (item)
			{
				rg_vec2 pos = rg_vec2(item_rect.x + ctx->style.padding,
				                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
				if (RG_GUI_LABEL_COPY)
				{
					rg_gui_push_text(ctx, item, pos, ctx->style.color_text);
				}
				else
				{
					rg_gui_push_text_static(ctx, item, pos, ctx->style.color_text);
				}
			}
		}
	}

	*selected = sel;
	if (scroll)
	{
		*scroll = top;
	}
	return changed;
}

RGINLINE u32 rg_gui_table_internal(RgGuiContext* ctx, RgGuiTableState* table, const RgGuiTableColumn* columns, f32* widths,
                                   u32 column_count, u32 row_count, f32 row_height, int* selected,
                                   RgGuiSelectionState* selection, const RgGuiSelectionOps* selection_ops, void* selection_user,
                                   RgGuiRowCache* row_cache, RgGuiRect rect, RgGuiId id, RgGuiTableRowFn row_fn, const void* user)
{
	RG_GUI_ASSERT(ctx != NULL);
	RG_GUI_ASSERT(table != NULL);
	RG_GUI_ASSERT(columns != NULL);
	RG_GUI_ASSERT(widths != NULL);
	row_count = (u32)rg_gui_u32_count_to_int(row_count);

	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	if (enabled && hovered && ctx->mouse_pressed)
	{
		ctx->focus_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);

	f32 item_h = row_height;
	if (item_h <= 0.0f)
	{
		item_h = ctx->style.text_height + ctx->style.padding * 2.0f;
	}
	if (item_h < 1.0f)
	{
		item_h = 1.0f;
	}

	f32 header_h = item_h;
	f32 row_spacing = 0.0f;
	f32 row_stride = item_h + row_spacing;
	int use_row_cache = 0;
	if (row_cache)
	{
		use_row_cache = rg_gui_row_cache_prepare(row_cache, row_count, item_h, row_spacing);
	}

	f32 border = ctx->style.border_thickness;
	RgGuiRect inner = rg_gui_make_rect(rect.x + border,
	                                   rect.y + border,
	                                   rect.w - border * 2.0f,
	                                   rect.h - border * 2.0f);
	if (inner.w < 0.0f) inner.w = 0.0f;
	if (inner.h < 0.0f) inner.h = 0.0f;

	RgGuiRect header_rect = rg_gui_make_rect(inner.x, inner.y, inner.w, header_h);
	RgGuiRect body_rect = rg_gui_make_rect(inner.x, inner.y + header_h, inner.w, inner.h - header_h);
	if (body_rect.h < 0.0f) body_rect.h = 0.0f;

	f32 scroll_w = ctx->style.scroll_bar_width;
	f32 scroll_gap = ctx->style.inner_spacing;
	if (scroll_gap < 0.0f)
	{
		scroll_gap = 0.0f;
	}

	f32 inset = border + ctx->style.padding;
	f32 content_x = header_rect.x + inset;
	f32 content_w = header_rect.w - inset * 2.0f;
	if (scroll_w > 0.0f)
	{
		content_w -= scroll_w + scroll_gap;
	}
	if (content_w < 0.0f)
	{
		content_w = 0.0f;
	}

	u32 safe_count = column_count;
	if (safe_count > RG_GUI_TABLE_MAX_COLUMNS)
	{
		safe_count = RG_GUI_TABLE_MAX_COLUMNS;
	}

	if (table->column_count != safe_count)
	{
		table->column_count = safe_count;
		for (u32 i = 0u; i < safe_count; i++)
		{
			table->column_order[i] = i;
			table->column_visible[i] = 1u;
		}
		table->column_visible_count = safe_count;
		table->header_popup.open = 0;
		table->header_popup.pos = rg_vec2(0.0f, 0.0f);
		table->header_popup_selected = -1;
		table->header_popup_scroll = 0;
		table->layout_dirty = 1u;
	}

	RgGuiTableColumn display_columns[RG_GUI_TABLE_MAX_COLUMNS];
	f32 display_widths[RG_GUI_TABLE_MAX_COLUMNS];
	u32 display_to_real[RG_GUI_TABLE_MAX_COLUMNS];
	f32 display_offsets[RG_GUI_TABLE_MAX_COLUMNS];
	u32 display_count = 0u;

	int layout_dirty = table->layout_dirty ? 1 : 0;
	if (table->layout_cache_safe_count != safe_count)
	{
		layout_dirty = 1;
	}

	if (!layout_dirty)
	{
		for (u32 i = 0u; i < safe_count; i++)
		{
			if (table->layout_cache_order[i] != table->column_order[i] ||
			    table->layout_cache_visible[i] != table->column_visible[i] ||
			    table->layout_cache_widths[i] != widths[i] ||
			    table->layout_cache_flags[i] != columns[i].flags ||
			    table->layout_cache_min_widths[i] != columns[i].min_width)
			{
				layout_dirty = 1;
				break;
			}
		}
	}

	if (!layout_dirty)
	{
		display_count = table->layout_cache_display_count;
		if (display_count > RG_GUI_TABLE_MAX_COLUMNS)
		{
			display_count = 0u;
			layout_dirty = 1;
		}
		else if (display_count > 0u)
		{
			memcpy(display_to_real, table->layout_cache_display_to_real, sizeof(u32) * display_count);
			memcpy(display_columns, table->layout_cache_display_columns, sizeof(RgGuiTableColumn) * display_count);
			memcpy(display_widths, table->layout_cache_display_widths, sizeof(f32) * display_count);
		}
	}

	if (layout_dirty)
	{
		display_count = 0u;

		for (u32 i = 0u; i < safe_count; i++)
		{
			u32 col_index = table->column_order[i];
			if (col_index >= safe_count)
			{
				continue;
			}
			if ((columns[col_index].flags & RG_GUI_TABLE_COLUMN_NO_HIDE) != 0u)
			{
				table->column_visible[col_index] = 1u;
			}
			if (!table->column_visible[col_index])
			{
				continue;
			}

			f32 min_width = columns[col_index].min_width;
			f32 width = widths[col_index];
			if (width < min_width)
			{
				width = min_width;
				widths[col_index] = width;
			}
			if (width < 0.0f)
			{
				width = 0.0f;
				widths[col_index] = width;
			}

			display_to_real[display_count] = col_index;
			display_columns[display_count] = columns[col_index];
			display_widths[display_count] = width;
			display_count++;
		}

		if (display_count == 0u && safe_count > 0u)
		{
			u32 col_index = table->column_order[0];
			if (col_index >= safe_count)
			{
				col_index = 0u;
			}
			table->column_visible[col_index] = 1u;

			f32 min_width = columns[col_index].min_width;
			f32 width = widths[col_index];
			if (width < min_width)
			{
				width = min_width;
				widths[col_index] = width;
			}
			if (width < 0.0f)
			{
				width = 0.0f;
				widths[col_index] = width;
			}

			display_to_real[0] = col_index;
			display_columns[0] = columns[col_index];
			display_widths[0] = width;
			display_count = 1u;
		}

		table->layout_cache_safe_count = safe_count;
		table->layout_cache_display_count = display_count;
		if (display_count > 0u)
		{
			memcpy(table->layout_cache_display_to_real, display_to_real, sizeof(u32) * display_count);
			memcpy(table->layout_cache_display_columns, display_columns, sizeof(RgGuiTableColumn) * display_count);
			memcpy(table->layout_cache_display_widths, display_widths, sizeof(f32) * display_count);
		}
		for (u32 i = 0u; i < safe_count; i++)
		{
			table->layout_cache_order[i] = table->column_order[i];
			table->layout_cache_visible[i] = table->column_visible[i];
			table->layout_cache_widths[i] = widths[i];
			table->layout_cache_flags[i] = columns[i].flags;
			table->layout_cache_min_widths[i] = columns[i].min_width;
		}
		table->layout_dirty = 0u;
	}

	f32 col_spacing = ctx->style.inner_spacing;
	if (col_spacing < 0.0f)
	{
		col_spacing = 0.0f;
	}

	u32 frozen_count = 0u;
	if (table->frozen_count > 0)
	{
		frozen_count = (u32)table->frozen_count;
	}
	if (frozen_count > display_count)
	{
		frozen_count = display_count;
	}
	table->frozen_count = (int)frozen_count;

	f32 frozen_w = 0.0f;
	if (frozen_count > 0u)
	{
		for (u32 i = 0u; i < frozen_count; i++)
		{
			frozen_w += display_widths[i];
		}
		if (frozen_count > 1u)
		{
			frozen_w += col_spacing * (f32)(frozen_count - 1u);
		}
		if (frozen_count < display_count)
		{
			frozen_w += col_spacing;
		}
	}

	u32 scrollable_count = (display_count > frozen_count) ? (display_count - frozen_count) : 0u;
	f32 scrollable_w = 0.0f;
	if (scrollable_count > 0u)
	{
		for (u32 i = frozen_count; i < display_count; i++)
		{
			scrollable_w += display_widths[i];
		}
		if (scrollable_count > 1u)
		{
			scrollable_w += col_spacing * (f32)(scrollable_count - 1u);
		}
	}

	f32 scroll_view_w = content_w - frozen_w;
	if (scroll_view_w < 0.0f)
	{
		scroll_view_w = 0.0f;
	}

	int use_hscroll = (scroll_w > 0.0f && scrollable_count > 0u &&
	                   scrollable_w > scroll_view_w && scroll_view_w > 0.0f);
	if (use_hscroll)
	{
		body_rect.h -= scroll_w + scroll_gap;
		if (body_rect.h < 0.0f) body_rect.h = 0.0f;
	}

	f32 max_scroll_x = use_hscroll ? (scrollable_w - scroll_view_w) : 0.0f;
	if (max_scroll_x < 0.0f)
	{
		max_scroll_x = 0.0f;
	}
	table->max_scroll_x = max_scroll_x;
	if (table->scroll_x < 0.0f) table->scroll_x = 0.0f;
	if (table->scroll_x > max_scroll_x) table->scroll_x = max_scroll_x;

	if (enabled && hovered && use_hscroll && ctx->input && ctx->mouse_wheel != 0.0f &&
	    rg_gui_input_has_shift(ctx->input))
	{
		int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
		if (steps != 0)
		{
			f32 step = ctx->style.text_height + ctx->style.padding * 2.0f;
			table->scroll_x -= (f32)steps * step;
			if (table->scroll_x < 0.0f) table->scroll_x = 0.0f;
			if (table->scroll_x > max_scroll_x) table->scroll_x = max_scroll_x;
			ctx->mouse_wheel = 0.0f;
		}
	}

	for (u32 i = 0u; i < display_count; i++)
	{
		display_offsets[i] = 0.0f;
	}

	if (display_count > 0u)
	{
		f32 cursor_x = content_x;
		for (u32 i = 0u; i < frozen_count; i++)
		{
			display_offsets[i] = cursor_x;
			cursor_x += display_widths[i];
			if (i + 1u < display_count)
			{
				cursor_x += col_spacing;
			}
		}

		f32 scroll_cursor_x = content_x + frozen_w - table->scroll_x;
		for (u32 i = frozen_count; i < display_count; i++)
		{
			display_offsets[i] = scroll_cursor_x;
			scroll_cursor_x += display_widths[i];
			if (i + 1u < display_count)
			{
				scroll_cursor_x += col_spacing;
			}
		}
	}

	RgGuiTableColumn draw_columns[RG_GUI_TABLE_MAX_COLUMNS];
	f32 draw_widths[RG_GUI_TABLE_MAX_COLUMNS];
	u32 draw_count = display_count;
	u32 draw_index_by_display[RG_GUI_TABLE_MAX_COLUMNS];

	int draw_layout_dirty = layout_dirty;
	if (table->layout_cache_draw_count != display_count ||
	    table->layout_cache_frozen_count != frozen_count)
	{
		draw_layout_dirty = 1;
	}

	if (draw_layout_dirty)
	{
		u32 draw_index = 0u;
		for (u32 i = frozen_count; i < display_count; i++)
		{
			table->layout_cache_draw_index_by_display[i] = draw_index;
			draw_index++;
		}
		for (u32 i = 0u; i < frozen_count; i++)
		{
			table->layout_cache_draw_index_by_display[i] = draw_index;
			draw_index++;
		}

		for (u32 i = 0u; i < display_count; i++)
		{
			u32 out_index = table->layout_cache_draw_index_by_display[i];
			table->layout_cache_draw_columns[out_index] = display_columns[i];
			table->layout_cache_draw_widths[out_index] = display_widths[i];
		}

		table->layout_cache_draw_count = display_count;
		table->layout_cache_frozen_count = frozen_count;
	}

	if (display_count > 0u)
	{
		memcpy(draw_index_by_display, table->layout_cache_draw_index_by_display, sizeof(u32) * display_count);
		memcpy(draw_columns, table->layout_cache_draw_columns, sizeof(RgGuiTableColumn) * display_count);
		memcpy(draw_widths, table->layout_cache_draw_widths, sizeof(f32) * display_count);
	}

	for (u32 i = 0u; i < display_count; i++)
	{
		u32 out_index = draw_index_by_display[i];
		table->column_offsets[out_index] = display_offsets[i];
	}
	table->column_visible_count = draw_count;

	RgGuiRect hscroll_track = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	RgGuiRect hscroll_handle = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	int hscroll_visible = 0;
	if (use_hscroll && scroll_view_w > 0.0f && scroll_w > 0.0f)
	{
		f32 track_x = content_x + frozen_w;
		f32 track_y = body_rect.y + body_rect.h + scroll_gap;
		f32 track_w = scroll_view_w;
		f32 track_h = scroll_w;
		if (track_w < 0.0f) track_w = 0.0f;
		if (track_h < 0.0f) track_h = 0.0f;

		hscroll_track = rg_gui_make_rect(track_x, track_y, track_w, track_h);

		f32 handle_w = track_w;
		if (scrollable_w > 0.0f)
		{
			handle_w = track_w * (scroll_view_w / scrollable_w);
		}
		f32 min_handle = ctx->style.text_height * 0.5f;
		if (handle_w < min_handle) handle_w = min_handle;
		if (handle_w > track_w) handle_w = track_w;

		RgGuiId bar_id = rg_gui_id_combine(id, 0x2000u);
		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, hscroll_track);
		if (bar_hovered)
		{
			ctx->hot_id = bar_id;
		}

		if (enabled && bar_hovered && ctx->mouse_pressed)
		{
			ctx->active_id = bar_id;
			ctx->focus_id = id;
		}

		if (!enabled && ctx->active_id == bar_id)
		{
			ctx->active_id = 0u;
		}
		if (enabled && ctx->active_id == bar_id)
		{
			if (ctx->mouse_down)
			{
				f32 track_span = track_w - handle_w;
				f32 t = 0.0f;
				if (track_span > 0.0f)
				{
					t = (ctx->mouse_pos.x - track_x - handle_w * 0.5f) / track_span;
				}
				if (t < 0.0f) t = 0.0f;
				if (t > 1.0f) t = 1.0f;
				table->scroll_x = t * max_scroll_x;
			}
			if (ctx->mouse_released)
			{
				ctx->active_id = 0u;
			}
		}

		f32 scroll_t = 0.0f;
		if (max_scroll_x > 0.0f && track_w > handle_w)
		{
			scroll_t = table->scroll_x / max_scroll_x;
			if (scroll_t < 0.0f) scroll_t = 0.0f;
			if (scroll_t > 1.0f) scroll_t = 1.0f;
		}
		f32 handle_x = track_x + (track_w - handle_w) * scroll_t;
		hscroll_handle = rg_gui_make_rect(handle_x, track_y, handle_w, track_h);
		hscroll_visible = 1;
	}

	int multi = (selection != NULL && selection_ops != NULL &&
	             selection_ops->is_selected != NULL && selection_ops->set_selected != NULL);
	if (multi)
	{
		rg_gui_selection_state_init(selection);
	}

	int row_count_int = (int)row_count;
	int sel = selected ? *selected : -1;
	int cursor = multi ? selection->cursor : sel;
	if (cursor >= row_count_int)
	{
		cursor = row_count_int > 0 ? row_count_int - 1 : -1;
	}
	if (cursor < -1)
	{
		cursor = -1;
	}
	if (multi)
	{
		selection->cursor = cursor;
	}
	else
	{
		sel = cursor;
	}

	if (table->sort_column >= (int)safe_count)
	{
		table->sort_column = -1;
	}
	if (table->sort_column < -1)
	{
		table->sort_column = -1;
	}
	table->sort_descending = table->sort_descending ? 1 : 0;

	u32 result = RG_GUI_TABLE_RESULT_NONE;

	if (focused && row_count > 0u && (selected || multi))
	{
		int visible = rg_gui_nonnegative_f32_to_int_bounded(body_rect.h / row_stride);
		if (visible < 1)
		{
			visible = 1;
		}
		if (visible > row_count_int)
		{
			visible = row_count_int;
		}

		int target = cursor;
		int moved = rg_gui_nav_list_move(ctx, id, row_count_int, visible, cursor, &target, NULL);

		if (moved)
		{
			if (multi)
			{
				u32 flags = RG_GUI_SELECTION_NONE;
				if (ctx->input && rg_gui_input_has_shift(ctx->input))
				{
					flags |= RG_GUI_SELECTION_RANGE;
				}
				if (rg_gui_selection_apply(selection, selection_ops, selection_user, row_count, (u32)target, flags))
				{
					result |= RG_GUI_TABLE_RESULT_SELECTION;
				}
				cursor = selection->cursor;
			}
			else
			{
				if (target != sel)
				{
					sel = target;
					result |= RG_GUI_TABLE_RESULT_SELECTION;
				}
				cursor = sel;
			}

			f32 row_top = 0.0f;
			f32 row_bottom = 0.0f;
			if (use_row_cache)
			{
				u32 row_index = (cursor < 0) ? 0u : (u32)cursor;
				f32 row_span = rg_gui_row_cache_row_span(row_cache, row_index);
				f32 row_h = row_span - row_spacing;
				if (row_h < 1.0f)
				{
					row_h = 1.0f;
				}
				row_top = rg_gui_row_cache_offset(row_cache, row_index);
				row_bottom = row_top + row_h;
			}
			else
			{
				row_top = row_stride * (f32)cursor;
				row_bottom = row_top + item_h;
			}
			f32 view_top = table->panel.scroll_y;
			f32 view_bottom = table->panel.scroll_y + body_rect.h;
			if (row_top < view_top)
			{
				table->panel.scroll_y = row_top;
			}
			else if (row_bottom > view_bottom)
			{
				table->panel.scroll_y = row_bottom - body_rect.h;
			}

			f32 total_height = row_stride * (f32)row_count;
			if (use_row_cache)
			{
				total_height = rg_gui_row_cache_total(row_cache);
			}
			f32 max_scroll = total_height - body_rect.h;
			if (max_scroll < 0.0f)
			{
				max_scroll = 0.0f;
			}
			if (table->panel.scroll_y < 0.0f) table->panel.scroll_y = 0.0f;
			if (table->panel.scroll_y > max_scroll) table->panel.scroll_y = max_scroll;
		}
	}

	rg_gui_push_rect(ctx, rect, ctx->style.color_panel);
	rg_gui_push_rect_outline(ctx, rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	if (header_rect.h > 0.0f && header_rect.w > 0.0f)
	{
		rg_gui_push_rect(ctx, header_rect, ctx->style.color_bg);
		rg_gui_push_rect_outline(ctx, header_rect, ctx->style.color_border, ctx->style.border_thickness);
	}

	int drag_consumed = 0;
	RgGuiId drag_type = rg_gui_id_combine(id, 0x54424Cu);

	f32 clip_left = content_x;
	f32 clip_right = content_x + content_w;
	f32 frozen_right = content_x + frozen_w;

	RgGuiRect header_clip = rg_gui_make_rect(content_x, header_rect.y, content_w, header_rect.h);
	int header_clip_pushed = 0;
	if (header_clip.w > 0.0f && header_clip.h > 0.0f)
	{
		rg_gui_push_clip(ctx, header_clip);
		header_clip_pushed = 1;
	}

	for (u32 di = 0u; di < frozen_count; di++)
	{
		u32 real_index = display_to_real[di];
		RgGuiTableColumn col = display_columns[di];
		f32 min_width = col.min_width;
		f32 width = display_widths[di];
		if (width < min_width)
		{
			width = min_width;
			widths[real_index] = width;
			display_widths[di] = width;
			draw_widths[draw_index_by_display[di]] = width;
		}
		if (width < 0.0f)
		{
			width = 0.0f;
			widths[real_index] = width;
			display_widths[di] = width;
			draw_widths[draw_index_by_display[di]] = width;
		}

		RgGuiRect cell = rg_gui_make_rect(display_offsets[di], header_rect.y, width, header_rect.h);
		int cell_visible = (cell.x + cell.w) > clip_left && cell.x < frozen_right;
		int handle_hovered = 0;
		if (enabled && (col.flags & RG_GUI_TABLE_COLUMN_RESIZE) && di + 1u < display_count)
		{
			f32 handle_x = cell.x + width + col_spacing * 0.5f - RG_GUI_TABLE_RESIZE_GRIP * 0.5f;
			RgGuiRect handle = rg_gui_make_rect(handle_x, header_rect.y, RG_GUI_TABLE_RESIZE_GRIP, header_rect.h);
			int handle_visible = (handle.x + handle.w) > clip_left && handle.x < clip_right;
			if (handle_visible)
			{
				handle_hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, handle);

				RgGuiId handle_id = rg_gui_id_combine(id, (u64)(0x1000u + real_index));
				if (handle_hovered)
				{
					ctx->hot_id = handle_id;
					ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
				}

				if (enabled && handle_hovered && ctx->mouse_pressed)
				{
					ctx->active_id = handle_id;
					ctx->focus_id = id;
					table->resize_column = (int)real_index;
					table->resize_start_x = ctx->mouse_pos.x;
					table->resize_start_width = width;
				}

				if (!enabled && ctx->active_id == handle_id)
				{
					ctx->active_id = 0u;
				}
				if (enabled && ctx->active_id == handle_id)
				{
					if (ctx->mouse_down)
					{
						f32 delta = ctx->mouse_pos.x - table->resize_start_x;
						f32 next = table->resize_start_width + delta;
						if (next < min_width)
						{
							next = min_width;
						}
						if (next < 0.0f)
						{
							next = 0.0f;
						}
						if (next != width)
						{
							f32 delta_w = next - width;
							widths[real_index] = next;
							display_widths[di] = next;
							draw_widths[draw_index_by_display[di]] = next;
							width = next;
							if (delta_w != 0.0f)
							{
								for (u32 j = di + 1u; j < display_count; j++)
								{
									display_offsets[j] += delta_w;
									table->column_offsets[draw_index_by_display[j]] = display_offsets[j];
								}
								frozen_w += delta_w;
								frozen_right += delta_w;
							}
							result |= RG_GUI_TABLE_RESULT_RESIZE;
							table->layout_dirty = 1u;
						}
					}
					if (ctx->mouse_released)
					{
						ctx->active_id = 0u;
					}
				}
			}
		}

		int drag_hovered = 0;
		int dragging = 0;
		if (enabled && (col.flags & RG_GUI_TABLE_COLUMN_NO_REORDER) == 0u && display_count > 1u && cell_visible)
		{
			RgGuiId drag_id = rg_gui_id_combine(id, (u64)(0x3000u + real_index));
			if (rg_gui_drag_source(ctx, cell, drag_id))
			{
				u32 payload = real_index;
				rg_gui_drag_set_payload(ctx, drag_type, &payload, sizeof(payload));
				dragging = 1;
			}

			RgGuiDragPayload drop_payload;
			u32 drag_result = rg_gui_drag_target(ctx, cell, drag_type, &drop_payload);
			if (drag_result & RG_GUI_DRAG_RESULT_HOVER)
			{
				drag_hovered = 1;
			}
			if (drag_result & RG_GUI_DRAG_RESULT_ACCEPT)
			{
				if (drop_payload.data && drop_payload.size == sizeof(u32))
				{
					u32 source = *(const u32*)drop_payload.data;
					if (source < safe_count && source != real_index)
					{
						if ((columns[source].flags & RG_GUI_TABLE_COLUMN_NO_REORDER) == 0u)
						{
							int source_pos = -1;
							int target_pos = -1;
							for (u32 j = 0u; j < safe_count; j++)
							{
								if (table->column_order[j] == source)
								{
									source_pos = (int)j;
								}
								if (table->column_order[j] == real_index)
								{
									target_pos = (int)j;
								}
							}
							if (source_pos >= 0 && target_pos >= 0 && source_pos != target_pos)
							{
								u32 moved = table->column_order[source_pos];
								if (source_pos < target_pos)
								{
									for (int j = source_pos; j < target_pos; j++)
									{
										table->column_order[j] = table->column_order[j + 1];
									}
								}
								else
								{
									for (int j = source_pos; j > target_pos; j--)
									{
										table->column_order[j] = table->column_order[j - 1];
									}
								}
								table->column_order[target_pos] = moved;
								table->layout_dirty = 1u;
							}
						}
					}
				}
				drag_consumed = 1;
			}
		}

		if (header_rect.h > 0.0f && header_rect.w > 0.0f && cell_visible)
		{
			int cell_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, cell) && !handle_hovered;
			RgGuiId cell_id = rg_gui_id_combine(id, (u64)(real_index + 1u));

			if (cell_hovered)
			{
				ctx->hot_id = cell_id;
			}

			if (enabled && cell_hovered && ctx->mouse_pressed)
			{
				ctx->active_id = cell_id;
				ctx->focus_id = id;
			}

			if (!enabled && ctx->active_id == cell_id)
			{
				ctx->active_id = 0u;
			}
			if (enabled && ctx->active_id == cell_id && ctx->mouse_released)
			{
				if (!drag_consumed && !ctx->drag_active && cell_hovered && (col.flags & RG_GUI_TABLE_COLUMN_SORT))
				{
					if (table->sort_column == (int)real_index)
					{
						table->sort_descending = !table->sort_descending;
					}
					else
					{
						table->sort_column = (int)real_index;
						table->sort_descending = 0;
					}
					result |= RG_GUI_TABLE_RESULT_SORT;
				}
				ctx->active_id = 0u;
			}

			rg_vec4 cell_bg = ctx->style.color_bg;
			if (table->sort_column == (int)real_index)
			{
				cell_bg = ctx->style.color_bg_active;
			}
			else if (enabled && cell_hovered)
			{
				cell_bg = ctx->style.color_bg_hover;
			}
			if (drag_hovered || dragging)
			{
				cell_bg = ctx->style.color_bg_active;
			}
			rg_gui_push_rect(ctx, cell, cell_bg);

			if (col.label)
			{
				rg_vec2 pos = rg_vec2(cell.x + ctx->style.padding,
				                      cell.y + (cell.h - ctx->style.text_height) * 0.5f);
				if (RG_GUI_LABEL_COPY)
				{
					rg_gui_push_text(ctx, col.label, pos, ctx->style.color_text);
				}
				else
				{
					rg_gui_push_text_static(ctx, col.label, pos, ctx->style.color_text);
				}
			}

			if (table->sort_column == (int)real_index && cell.w > 0.0f)
			{
				f32 arrow_w = ctx->style.text_height;
				f32 arrow_x = cell.x + cell.w - ctx->style.padding - arrow_w;
				if (arrow_x > cell.x + ctx->style.padding)
				{
					RgGuiRect arrow_rect = rg_gui_make_rect(arrow_x, cell.y, arrow_w, cell.h);
					RgGuiArrowDirection direction = table->sort_descending
					                                    ? RG_GUI_ARROW_DOWN
					                                    : RG_GUI_ARROW_UP;
					rg_gui_push_arrow(ctx, arrow_rect, direction, ctx->style.color_text_dim);
				}
			}
		}
	}

	f32 scroll_clip_left = content_x + frozen_w;
	f32 scroll_clip_w = content_w - frozen_w;
	if (scroll_clip_w < 0.0f)
	{
		scroll_clip_w = 0.0f;
	}
	RgGuiRect scroll_clip = rg_gui_make_rect(scroll_clip_left, header_rect.y, scroll_clip_w, header_rect.h);
	int scroll_clip_pushed = 0;
	if (scroll_clip.w > 0.0f && scroll_clip.h > 0.0f)
	{
		rg_gui_push_clip(ctx, scroll_clip);
		scroll_clip_pushed = 1;
	}

	for (u32 di = frozen_count; di < display_count; di++)
	{
		u32 real_index = display_to_real[di];
		RgGuiTableColumn col = display_columns[di];
		f32 min_width = col.min_width;
		f32 width = display_widths[di];
		if (width < min_width)
		{
			width = min_width;
			widths[real_index] = width;
			display_widths[di] = width;
			draw_widths[draw_index_by_display[di]] = width;
		}
		if (width < 0.0f)
		{
			width = 0.0f;
			widths[real_index] = width;
			display_widths[di] = width;
			draw_widths[draw_index_by_display[di]] = width;
		}

		RgGuiRect cell = rg_gui_make_rect(display_offsets[di], header_rect.y, width, header_rect.h);
		int cell_visible = (cell.x + cell.w) > scroll_clip_left && cell.x < clip_right;
		int handle_hovered = 0;
		if (enabled && (col.flags & RG_GUI_TABLE_COLUMN_RESIZE) && di + 1u < display_count)
		{
			f32 handle_x = cell.x + width + col_spacing * 0.5f - RG_GUI_TABLE_RESIZE_GRIP * 0.5f;
			RgGuiRect handle = rg_gui_make_rect(handle_x, header_rect.y, RG_GUI_TABLE_RESIZE_GRIP, header_rect.h);
			int handle_visible = (handle.x + handle.w) > scroll_clip_left && handle.x < clip_right;
			if (handle_visible)
			{
				handle_hovered = rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, handle);

				RgGuiId handle_id = rg_gui_id_combine(id, (u64)(0x1000u + real_index));
				if (handle_hovered)
				{
					ctx->hot_id = handle_id;
					ctx->mouse_cursor = RG_GUI_CURSOR_RESIZE_H;
				}

				if (enabled && handle_hovered && ctx->mouse_pressed)
				{
					ctx->active_id = handle_id;
					ctx->focus_id = id;
					table->resize_column = (int)real_index;
					table->resize_start_x = ctx->mouse_pos.x;
					table->resize_start_width = width;
				}

				if (!enabled && ctx->active_id == handle_id)
				{
					ctx->active_id = 0u;
				}
				if (enabled && ctx->active_id == handle_id)
				{
					if (ctx->mouse_down)
					{
						f32 delta = ctx->mouse_pos.x - table->resize_start_x;
						f32 next = table->resize_start_width + delta;
						if (next < min_width)
						{
							next = min_width;
						}
						if (next < 0.0f)
						{
							next = 0.0f;
						}
						if (next != width)
						{
							f32 delta_w = next - width;
							widths[real_index] = next;
							display_widths[di] = next;
							draw_widths[draw_index_by_display[di]] = next;
							width = next;
							if (delta_w != 0.0f)
							{
								for (u32 j = di + 1u; j < display_count; j++)
								{
									display_offsets[j] += delta_w;
									table->column_offsets[draw_index_by_display[j]] = display_offsets[j];
								}
							}
							result |= RG_GUI_TABLE_RESULT_RESIZE;
							table->layout_dirty = 1u;
						}
					}
					if (ctx->mouse_released)
					{
						ctx->active_id = 0u;
					}
				}
			}
		}

		int drag_hovered = 0;
		int dragging = 0;
		if (enabled && (col.flags & RG_GUI_TABLE_COLUMN_NO_REORDER) == 0u && display_count > 1u && cell_visible)
		{
			RgGuiId drag_id = rg_gui_id_combine(id, (u64)(0x3000u + real_index));
			if (rg_gui_drag_source(ctx, cell, drag_id))
			{
				u32 payload = real_index;
				rg_gui_drag_set_payload(ctx, drag_type, &payload, sizeof(payload));
				dragging = 1;
			}

			RgGuiDragPayload drop_payload;
			u32 drag_result = rg_gui_drag_target(ctx, cell, drag_type, &drop_payload);
			if (drag_result & RG_GUI_DRAG_RESULT_HOVER)
			{
				drag_hovered = 1;
			}
			if (drag_result & RG_GUI_DRAG_RESULT_ACCEPT)
			{
				if (drop_payload.data && drop_payload.size == sizeof(u32))
				{
					u32 source = *(const u32*)drop_payload.data;
					if (source < safe_count && source != real_index)
					{
						if ((columns[source].flags & RG_GUI_TABLE_COLUMN_NO_REORDER) == 0u)
						{
							int source_pos = -1;
							int target_pos = -1;
							for (u32 j = 0u; j < safe_count; j++)
							{
								if (table->column_order[j] == source)
								{
									source_pos = (int)j;
								}
								if (table->column_order[j] == real_index)
								{
									target_pos = (int)j;
								}
							}
							if (source_pos >= 0 && target_pos >= 0 && source_pos != target_pos)
							{
								u32 moved = table->column_order[source_pos];
								if (source_pos < target_pos)
								{
									for (int j = source_pos; j < target_pos; j++)
									{
										table->column_order[j] = table->column_order[j + 1];
									}
								}
								else
								{
									for (int j = source_pos; j > target_pos; j--)
									{
										table->column_order[j] = table->column_order[j - 1];
									}
								}
								table->column_order[target_pos] = moved;
								table->layout_dirty = 1u;
							}
						}
					}
				}
				drag_consumed = 1;
			}
		}

		if (header_rect.h > 0.0f && header_rect.w > 0.0f && cell_visible)
		{
			int cell_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, cell) && !handle_hovered;
			RgGuiId cell_id = rg_gui_id_combine(id, (u64)(real_index + 1u));

			if (cell_hovered)
			{
				ctx->hot_id = cell_id;
			}

			if (enabled && cell_hovered && ctx->mouse_pressed)
			{
				ctx->active_id = cell_id;
				ctx->focus_id = id;
			}

			if (!enabled && ctx->active_id == cell_id)
			{
				ctx->active_id = 0u;
			}
			if (enabled && ctx->active_id == cell_id && ctx->mouse_released)
			{
				if (!drag_consumed && !ctx->drag_active && cell_hovered && (col.flags & RG_GUI_TABLE_COLUMN_SORT))
				{
					if (table->sort_column == (int)real_index)
					{
						table->sort_descending = !table->sort_descending;
					}
					else
					{
						table->sort_column = (int)real_index;
						table->sort_descending = 0;
					}
					result |= RG_GUI_TABLE_RESULT_SORT;
				}
				ctx->active_id = 0u;
			}

			rg_vec4 cell_bg = ctx->style.color_bg;
			if (table->sort_column == (int)real_index)
			{
				cell_bg = ctx->style.color_bg_active;
			}
			else if (enabled && cell_hovered)
			{
				cell_bg = ctx->style.color_bg_hover;
			}
			if (drag_hovered || dragging)
			{
				cell_bg = ctx->style.color_bg_active;
			}
			rg_gui_push_rect(ctx, cell, cell_bg);

			if (col.label)
			{
				rg_vec2 pos = rg_vec2(cell.x + ctx->style.padding,
				                      cell.y + (cell.h - ctx->style.text_height) * 0.5f);
				if (RG_GUI_LABEL_COPY)
				{
					rg_gui_push_text(ctx, col.label, pos, ctx->style.color_text);
				}
				else
				{
					rg_gui_push_text_static(ctx, col.label, pos, ctx->style.color_text);
				}
			}

			if (table->sort_column == (int)real_index && cell.w > 0.0f)
			{
				f32 arrow_w = ctx->style.text_height;
				f32 arrow_x = cell.x + cell.w - ctx->style.padding - arrow_w;
				if (arrow_x > cell.x + ctx->style.padding)
				{
					RgGuiRect arrow_rect = rg_gui_make_rect(arrow_x, cell.y, arrow_w, cell.h);
					RgGuiArrowDirection direction = table->sort_descending
					                                    ? RG_GUI_ARROW_DOWN
					                                    : RG_GUI_ARROW_UP;
					rg_gui_push_arrow(ctx, arrow_rect, direction, ctx->style.color_text_dim);
				}
			}
		}
	}

	if (scroll_clip_pushed)
	{
		rg_gui_pop_clip(ctx);
	}
	if (header_clip_pushed)
	{
		rg_gui_pop_clip(ctx);
	}

	if (rg_gui_context_menu_begin(ctx, &table->header_popup, header_rect, id))
	{
		const char* menu_items[RG_GUI_TABLE_MAX_COLUMNS];
		RgGuiMenuItemFlags menu_flags[RG_GUI_TABLE_MAX_COLUMNS];
		u32 menu_to_real[RG_GUI_TABLE_MAX_COLUMNS];
		u32 menu_count = 0u;
		for (u32 i = 0u; i < safe_count; i++)
		{
			u32 col_index = table->column_order[i];
			if (col_index >= safe_count)
			{
				continue;
			}
			menu_items[menu_count] = columns[col_index].label ? columns[col_index].label : "";
			menu_to_real[menu_count] = col_index;

			RgGuiMenuItemFlags flags = RG_GUI_MENU_ITEM_CHECKABLE;
			if (table->column_visible[col_index])
			{
				flags |= RG_GUI_MENU_ITEM_CHECKED;
			}
			if ((columns[col_index].flags & RG_GUI_TABLE_COLUMN_NO_HIDE) != 0u ||
			    (table->column_visible[col_index] && display_count <= 1u))
			{
				flags |= RG_GUI_MENU_ITEM_DISABLED;
			}
			menu_flags[menu_count] = flags;
			menu_count++;
		}

		RgGuiRect menu_rect = rg_gui_make_rect(table->header_popup.pos.x, table->header_popup.pos.y, 160.0f, 0.0f);
		int menu_changed = rg_gui_menu_popup_ex(ctx, menu_items, NULL, menu_flags, menu_count,
		                                        &table->header_popup_selected, &table->header_popup.open,
		                                        &table->header_popup_scroll, menu_rect,
		                                        rg_gui_id_combine(id, 0x4844524Du),
		                                        NULL, NULL, NULL, NULL, 1);
		if (menu_changed && !table->header_popup.open && table->header_popup_selected >= 0 &&
		    (u32)table->header_popup_selected < menu_count)
		{
			u32 col_index = menu_to_real[table->header_popup_selected];
			if (col_index < safe_count)
			{
				int visible = table->column_visible[col_index] != 0u;
				int no_hide = (columns[col_index].flags & RG_GUI_TABLE_COLUMN_NO_HIDE) != 0u;
				if (visible)
				{
					if (!no_hide && display_count > 1u)
					{
						table->column_visible[col_index] = 0u;
						table->layout_dirty = 1u;
					}
				}
				else
				{
					table->column_visible[col_index] = 1u;
					table->layout_dirty = 1u;
				}
			}
		}
	}

	u32 start = 0u;
	u32 visible = 0u;
	if (use_row_cache)
	{
		rg_gui_panel_begin_clipped(ctx, &table->panel, body_rect, item_h, row_spacing, row_count, id, row_cache,
		                           &start, &visible);
	}
	else
	{
		rg_gui_panel_begin_virtual(ctx, &table->panel, body_rect, item_h, row_spacing, row_count, id, &start, &visible);
	}

	f32 row_y = ctx->layout.cursor_y;
	for (u32 i = start; i < start + visible; i++)
	{
		f32 row_span = row_stride;
		f32 row_h = item_h;
		if (use_row_cache)
		{
			row_span = rg_gui_row_cache_row_span(row_cache, i);
			row_h = row_span - row_spacing;
			if (row_h < 1.0f)
			{
				row_h = 1.0f;
			}
		}

		RgGuiRect row_hit_rect = rg_gui_make_rect(content_x, row_y, content_w, row_h);
		RgGuiRect row_rect = rg_gui_make_rect(content_x, row_y, content_w, row_h);
		int row_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, row_hit_rect);
		if (row_hovered)
		{
			ctx->hot_id = id;
		}

		if (enabled && row_hovered && ctx->mouse_pressed && (selected || multi))
		{
			if (multi)
			{
				u32 flags = RG_GUI_SELECTION_NONE;
				if (ctx->input && rg_gui_input_has_ctrl(ctx->input))
				{
					flags |= RG_GUI_SELECTION_TOGGLE;
				}
				if (ctx->input && rg_gui_input_has_shift(ctx->input))
				{
					flags |= RG_GUI_SELECTION_RANGE;
				}
				if (rg_gui_selection_apply(selection, selection_ops, selection_user, row_count, i, flags))
				{
					result |= RG_GUI_TABLE_RESULT_SELECTION;
				}
				cursor = selection->cursor;
			}
			else
			{
				if (sel != (int)i)
				{
					sel = (int)i;
					result |= RG_GUI_TABLE_RESULT_SELECTION;
				}
			}
			ctx->focus_id = id;
		}

		int row_selected = multi ? (selection_ops->is_selected(selection_user, i) ? 1 : 0) : ((int)i == sel);
		rg_vec4 row_bg = ctx->style.color_bg;
		if (row_selected)
		{
			row_bg = ctx->style.color_bg_active;
		}
		else if (enabled && row_hovered)
		{
			row_bg = ctx->style.color_bg_hover;
		}
		rg_gui_push_rect(ctx, row_hit_rect, row_bg);

		if (row_fn)
		{
			row_fn(ctx, i, draw_columns, draw_widths, draw_count, row_rect, user);
		}

		row_y += row_span;
	}

	rg_gui_panel_end(ctx, &table->panel);

	if (hscroll_visible && hscroll_track.w > 0.0f && hscroll_track.h > 0.0f)
	{
		RgGuiId bar_id = rg_gui_id_combine(id, 0x2000u);
		int bar_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, hscroll_track);
		rg_vec4 track_color = bar_hovered ? ctx->style.color_bg_hover : ctx->style.color_bg;
		rg_gui_push_rect(ctx, hscroll_track, track_color);
		rg_gui_push_rect_outline(ctx, hscroll_track, ctx->style.color_border, ctx->style.border_thickness);

		rg_vec4 handle_color = (ctx->active_id == bar_id) ? ctx->style.color_accent : ctx->style.color_bg_active;
		rg_gui_push_rect(ctx, hscroll_handle, handle_color);
	}

	if (selected)
	{
		*selected = sel;
	}

	return result;
}

RGINLINE u32 rg_gui_table(RgGuiContext* ctx, RgGuiTableState* table, const RgGuiTableColumn* columns, f32* widths,
                          u32 column_count, u32 row_count, f32 row_height, int* selected,
                          RgGuiRect rect, RgGuiId id, RgGuiTableRowFn row_fn, const void* user)
{
	return rg_gui_table_internal(ctx, table, columns, widths, column_count, row_count, row_height, selected,
	                             NULL, NULL, NULL, NULL, rect, id, row_fn, user);
}

RGINLINE u32 rg_gui_table_clipped(RgGuiContext* ctx, RgGuiTableState* table, const RgGuiTableColumn* columns, f32* widths,
                                  u32 column_count, u32 row_count, f32 row_height, int* selected,
                                  RgGuiRowCache* cache, RgGuiRect rect, RgGuiId id, RgGuiTableRowFn row_fn,
                                  const void* user)
{
	return rg_gui_table_internal(ctx, table, columns, widths, column_count, row_count, row_height, selected,
	                             NULL, NULL, NULL, cache, rect, id, row_fn, user);
}

RGINLINE u32 rg_gui_table_multi(RgGuiContext* ctx, RgGuiTableState* table, const RgGuiTableColumn* columns, f32* widths,
                                u32 column_count, u32 row_count, f32 row_height,
                                RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                                RgGuiRect rect, RgGuiId id, RgGuiTableRowFn row_fn, const void* user)
{
	return rg_gui_table_internal(ctx, table, columns, widths, column_count, row_count, row_height, NULL,
	                             selection, ops, selection_user, NULL, rect, id, row_fn, user);
}

RGINLINE u32 rg_gui_table_multi_clipped(RgGuiContext* ctx, RgGuiTableState* table, const RgGuiTableColumn* columns,
                                        f32* widths, u32 column_count, u32 row_count, f32 row_height,
                                        RgGuiSelectionState* selection, const RgGuiSelectionOps* ops, void* selection_user,
                                        RgGuiRowCache* cache, RgGuiRect rect, RgGuiId id,
                                        RgGuiTableRowFn row_fn, const void* user)
{
	return rg_gui_table_internal(ctx, table, columns, widths, column_count, row_count, row_height, NULL,
	                             selection, ops, selection_user, cache, rect, id, row_fn, user);
}

RGINLINE RgGuiRect rg_gui_table_column_rect(const RgGuiTableState* table, const f32* widths,
                                            u32 column_index, RgGuiRect row_rect)
{
	RgGuiRect rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	if (!table || !widths)
	{
		return rect;
	}

	if (column_index >= table->column_visible_count)
	{
		return rect;
	}

	rect.x = table->column_offsets[column_index];
	rect.y = row_rect.y;
	rect.w = widths[column_index];
	rect.h = row_rect.h;
	return rect;
}

RGINLINE int rg_gui_dropdown(RgGuiContext* ctx, const char* const* items, u32 count, int* selected, int* open, int* scroll, RgGuiRect rect, RgGuiId id)
{
	RG_GUI_ASSERT(selected != NULL);
	RG_GUI_ASSERT(open != NULL);

	id = rg_gui_id_scoped(ctx, id);
	rg_gui_register_focusable(ctx, id);

	int open_state = *open ? 1 : 0;
	int was_open = open_state;
	int capture_scope = 0;
	if (open_state)
	{
		rg_gui_input_capture_begin(ctx);
		capture_scope = 1;
	}

	int enabled = !rg_gui_is_disabled(ctx);
	int hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
	}

	int focused = enabled && (ctx->focus_id == id);
	if (enabled && hovered && ctx->mouse_pressed)
	{
		open_state = !open_state;
		ctx->focus_id = id;
	}

	if (focused &&
	    (rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_RETURN) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_KP_ENTER) ||
	     rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_SPACE)))
	{
		open_state = !open_state;
	}

	if (focused && rg_input_is_key_pressed(ctx->input, SDL_SCANCODE_ESCAPE))
	{
		open_state = 0;
	}

	int count_int = rg_gui_u32_count_to_int(count);
	count = (u32)count_int;
	int sel = *selected;
	if (sel >= count_int)
	{
		sel = count_int > 0 ? count_int - 1 : -1;
	}
	if (sel < -1)
	{
		sel = -1;
	}

	u32 max_visible = 6u;
	int page_step = (int)max_visible;
	if (page_step < 1)
	{
		page_step = 1;
	}
	if (page_step > count_int && count_int > 0)
	{
		page_step = count_int;
	}

	int changed = 0;
	if (focused && count > 0u)
	{
		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_DOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_UP)
			                       ? SDL_SCANCODE_UP
			                       : SDL_SCANCODE_DOWN;
			int repeats = rg_gui_key_repeat(ctx, id, key);
			if (repeats > 0)
			{
				if (sel < 0)
				{
					sel = 0;
				}
				sel = rg_gui_int_step_clamped(sel, 1, repeats,
				                              key == SDL_SCANCODE_UP ? -1 : 1,
				                              0, count_int - 1);
				changed = 1;
			}
		}

		if (rg_input_is_key_down(ctx->input, SDL_SCANCODE_PAGEUP) ||
		    rg_input_is_key_down(ctx->input, SDL_SCANCODE_PAGEDOWN))
		{
			SDL_Scancode key = rg_input_is_key_down(ctx->input, SDL_SCANCODE_PAGEUP)
			                       ? SDL_SCANCODE_PAGEUP
			                       : SDL_SCANCODE_PAGEDOWN;
			int repeats = rg_gui_key_repeat(ctx, id, key);
			if (repeats > 0)
			{
				if (sel < 0)
				{
					sel = 0;
				}
				sel = rg_gui_int_step_clamped(sel, (i64)page_step, repeats,
				                              key == SDL_SCANCODE_PAGEUP ? -1 : 1,
				                              0, count_int - 1);
				changed = 1;
			}
		}

		if (ctx->input && ctx->input->has_text_input && !rg_gui_input_has_ctrl(ctx->input))
		{
			const char* query = ctx->input->text_input_buffer;
			int start_index = (sel >= 0) ? sel + 1 : 0;
			int found = rg_gui_find_prefix(items, count, query, start_index);
			if (found < 0 && start_index > 0)
			{
				found = rg_gui_find_prefix(items, count, query, 0);
			}
			if (found >= 0 && found != sel)
			{
				sel = found;
				changed = 1;
			}
		}
	}

	rg_vec4 bg = ctx->style.color_bg;
	if (open_state)
	{
		bg = ctx->style.color_bg_active;
	}
	else if (hovered)
	{
		bg = ctx->style.color_bg_hover;
	}

	rg_gui_push_rect(ctx, rect, bg);
	rg_gui_push_rect_outline(ctx, rect,
	                         focused ? ctx->style.color_focus_border : ctx->style.color_border,
	                         focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

	const char* selection = "";
	if (items && sel >= 0 && sel < (int)count && items[sel])
	{
		selection = items[sel];
	}

	if (selection)
	{
		rg_vec2 pos = rg_vec2(rect.x + ctx->style.padding,
		                      rect.y + (rect.h - ctx->style.text_height) * 0.5f);
		rg_gui_push_text(ctx, selection, pos, ctx->style.color_text);
	}

	f32 arrow_w = ctx->style.text_height;
	RgGuiRect arrow_rect = rg_gui_make_rect(
	    rect.x + rect.w - ctx->style.padding - arrow_w,
	    rect.y, arrow_w, rect.h);
	RgGuiArrowDirection arrow_direction = open_state ? RG_GUI_ARROW_UP : RG_GUI_ARROW_DOWN;
	rg_gui_push_arrow(ctx, arrow_rect, arrow_direction, ctx->style.color_text_dim);

	if (open_state)
	{
		if (!capture_scope)
		{
			rg_gui_input_capture_begin(ctx);
			capture_scope = 1;
		}
	}

	if (open_state && count > 0u)
	{
		f32 item_h = ctx->style.text_height + ctx->style.padding * 2.0f;
		if (item_h < 1.0f)
		{
			item_h = 1.0f;
		}

		u32 visible = count < max_visible ? count : max_visible;
		f32 border = ctx->style.border_thickness;
		f32 list_h = item_h * (f32)visible + border * 2.0f;
		RgGuiRect list_rect = rg_gui_make_rect(rect.x,
		                                       rect.y + rect.h + ctx->style.inner_spacing,
		                                       rect.w,
		                                       list_h);

		RgGuiRect inner = rg_gui_make_rect(list_rect.x + border,
		                                   list_rect.y + border,
		                                   list_rect.w - border * 2.0f,
		                                   item_h * (f32)visible);
		if (inner.w < 0.0f) inner.w = 0.0f;
		if (inner.h < 0.0f) inner.h = 0.0f;

		int list_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, list_rect);
		if (list_hovered)
		{
			ctx->hot_id = id;
		}
		int consume_click = enabled && list_hovered && ctx->mouse_pressed;

		int visible_rows = visible > (u32)INT_MAX ? INT_MAX : (int)visible;
		if (visible_rows < 1)
		{
			visible_rows = 1;
		}

		int top = scroll ? *scroll : 0;
		int max_top = (int)count - visible_rows;
		if (max_top < 0)
		{
			max_top = 0;
		}
		if (top < 0)
		{
			top = 0;
		}
		if (top > max_top)
		{
			top = max_top;
		}

		if (enabled && list_hovered && max_top > 0)
		{
			ctx->scroll_owner_next = id;
		}

		if (list_hovered && ctx->mouse_wheel != 0.0f && ctx->scroll_owner == id)
		{
			int steps = rg_gui_wheel_steps(ctx->mouse_wheel);
			if (steps != 0)
			{
				top = rg_gui_i64_clamp_to_int((i64)top - (i64)steps, 0, max_top);
			}
		}

		int keep_visible = (scroll == NULL) ? 1 : (changed || (open_state && !was_open));
		if (keep_visible && sel >= 0 && count > 0u)
		{
			if (sel < top)
			{
				top = sel;
			}
			else if (sel >= top + visible_rows)
			{
				top = sel - visible_rows + 1;
			}
		}

		if (top < 0)
		{
			top = 0;
		}
		if (top > max_top)
		{
			top = max_top;
		}

		RgGuiDrawList* overlay = rg_gui_overlay_target(ctx);
		rg_gui_push_rect_to(ctx, overlay, list_rect, ctx->style.color_panel);
		rg_gui_push_rect_outline_to(ctx, overlay, list_rect,
		                            focused ? ctx->style.color_focus_border : ctx->style.color_border,
		                            focused ? ctx->style.focus_border_thickness : ctx->style.border_thickness);

		u32 start = (u32)top;
		u32 end = start + (u32)visible_rows;
		if (end > count)
		{
			end = count;
		}

		for (u32 i = start; i < end; i++)
		{
			f32 y = inner.y + item_h * (f32)(i - start);
			RgGuiRect item_rect = rg_gui_make_rect(inner.x, y, inner.w, item_h);
			int item_hovered = enabled && rg_gui_point_in_rect(ctx->mouse_pos.x, ctx->mouse_pos.y, item_rect);
			if (item_hovered)
			{
				ctx->hot_id = id;
			}

			if (enabled && item_hovered && ctx->mouse_pressed)
			{
				sel = (int)i;
				changed = 1;
				open_state = 0;
				ctx->focus_id = id;
			}

			rg_vec4 item_bg = ctx->style.color_bg;
			if ((int)i == sel)
			{
				item_bg = ctx->style.color_bg_active;
			}
			else if (enabled && item_hovered)
			{
				item_bg = ctx->style.color_bg_hover;
			}
			rg_gui_push_rect_to(ctx, overlay, item_rect, item_bg);

			if (items && items[i])
			{
				rg_vec2 pos = rg_vec2(item_rect.x + ctx->style.padding,
				                      item_rect.y + (item_rect.h - ctx->style.text_height) * 0.5f);
				rg_gui_push_text_to(ctx, overlay, items[i], pos, ctx->style.color_text);
			}
		}

		if (scroll)
		{
			*scroll = top;
		}

		if (enabled && ctx->mouse_pressed && !hovered && !list_hovered)
		{
			open_state = 0;
		}

		if (consume_click)
		{
			ctx->mouse_pressed = 0;
		}
	}

	*selected = sel;
	*open = open_state;
	if (capture_scope)
	{
		rg_gui_input_capture_end(ctx, open_state);
	}
	return changed;
}

RGINLINE const RgGuiDrawList* rg_gui_draw_list(const RgGuiContext* ctx)
{
	return &ctx->draw_list;
}

RGINLINE const RgGuiDiagnostics* rg_gui_diagnostics(const RgGuiContext* ctx)
{
	return ctx ? &ctx->diagnostics : NULL;
}

RGINLINE u32 rg_gui_draw_list_overlay_start(const RgGuiContext* ctx)
{
	return ctx ? ctx->overlay_start : 0u;
}

#if defined(RG_GUI_ENABLE_VIEWPORTS)
RGINLINE u32 rg_gui_viewport_count(const RgGuiContext* ctx)
{
	return ctx ? ctx->viewport_active_count : 0u;
}

RGINLINE const RgGuiViewport* rg_gui_viewport_at(const RgGuiContext* ctx, u32 index)
{
	if (!ctx || index >= ctx->viewport_active_count)
	{
		return NULL;
	}

	return ctx->viewport_active[index];
}

RGINLINE const RgGuiDrawList* rg_gui_viewport_draw_list(const RgGuiViewport* viewport)
{
	return viewport ? &viewport->draw_list : NULL;
}

RGINLINE u32 rg_gui_viewport_overlay_start(const RgGuiViewport* viewport)
{
	return viewport ? viewport->overlay_start : 0u;
}

RGINLINE const RgGuiPlatformOutput* rg_gui_viewport_platform_output(
    const RgGuiViewport* viewport)
{
	return viewport ? &viewport->platform_output : NULL;
}
#endif

RGINLINE RgGuiMouseCursor rg_gui_mouse_cursor(const RgGuiContext* ctx)
{
	if (!ctx)
	{
		return RG_GUI_CURSOR_DEFAULT;
	}
	return ctx->mouse_cursor;
}

RGINLINE const RgGuiPlatformOutput* rg_gui_platform_output(const RgGuiContext* ctx)
{
	return ctx ? &ctx->platform_output : NULL;
}

RGINLINE RgGuiId rg_gui_id_ptr(const void* ptr)
{
	return rg_hash_ptr(ptr);
}

#if !defined(RG_GUI_NO_STRING_IDS)
RGINLINE RgGuiId rg_gui_id_str(const char* str)
{
	return rg_hash_str(str);
}

RGINLINE RgGuiId rg_gui_id_str_len(const char* str, size_t len)
{
	return rg_hash_bytes(str, len, RG_HASH_SEED);
}

#ifndef RG_GUI_ID_LIT
#define RG_GUI_ID_LIT(str_lit) rg_gui_id_str_len((str_lit), sizeof(str_lit) - 1u)
#endif
#else
// These macros intentionally produce a compile error only when a disabled API is used.
#define rg_gui_id_str(...) RG_GUI_STRING_IDS_ARE_DISABLED__USE_rg_gui_id_u64_or_rg_gui_id_ptr
#define rg_gui_id_str_len(...) RG_GUI_STRING_IDS_ARE_DISABLED__USE_rg_gui_id_u64_or_rg_gui_id_ptr
#define rg_gui_push_id_str(...) RG_GUI_STRING_IDS_ARE_DISABLED__USE_rg_gui_push_id_u64_or_rg_gui_push_id_ptr
#define rg_gui_push_id_str_len(...) RG_GUI_STRING_IDS_ARE_DISABLED__USE_rg_gui_push_id_u64_or_rg_gui_push_id_ptr
#ifndef RG_GUI_ID_LIT
#define RG_GUI_ID_LIT(...) RG_GUI_STRING_IDS_ARE_DISABLED__USE_rg_gui_id_u64_or_rg_gui_id_ptr
#endif
#endif

RGINLINE RgGuiId rg_gui_id_u64(u64 value)
{
	return rg_hash_u64(value);
}

RGINLINE RgGuiId rg_gui_id_combine(RgGuiId a, u64 b)
{
	return rg_hash_u64(a ^ b);
}

RGINLINE RgGuiId rg_gui_id_scoped(const RgGuiContext* ctx, RgGuiId id)
{
	if (!ctx || id == 0u)
	{
		return id;
	}

	if (ctx->id_stack_top == 0u)
	{
		return id;
	}

	return rg_gui_id_combine(ctx->id_stack[ctx->id_stack_top], id);
}
