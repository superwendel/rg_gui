// rg_gui_tab_strip - closable, reorderable tab strip for rg_gui
//
// Part of the Reverse Gravity (rg_) libraries.
// An opt-in widget beside rg_gui_tabs: tabs as wide as their labels (within
// limits, cut with an ellipsis), a close button on the active and hovered tab,
// middle-click to close, drag to reorder, a "+" button, and horizontal
// scrolling (mouse wheel, and the active tab kept in view) when the tabs do not
// fit. The caller owns the tabs: the widget reports what happened and the
// caller applies it (reorders its array on a move, removes a closed tab).
//
// USAGE:
//   static RgGuiTabStripState strip;            // zero-initialized
//   RgGuiTabStripItem items[n];                  // label, closable
//   RgGuiTabStripResult r;
//   rg_gui_tab_strip(ctx, &strip, items, n, active, 0u, rect, id, &r);
//   if (r.events & RG_GUI_TAB_STRIP_ACTIVATE) active = r.index;
//   if (r.events & RG_GUI_TAB_STRIP_MOVE)     move tab r.from to r.to;
//   if (r.events & RG_GUI_TAB_STRIP_CLOSE)    close tab r.index;
//   if (r.events & RG_GUI_TAB_STRIP_NEW)      open a tab;
//
// NOTES:
//   - A press on a tab activates it at once; a press on its close button, or a
//     middle-button press, closes it when released over the same tab.
//   - While a tab is dragged (and on its release), a move from its place to
//     where it now belongs is reported: past every neighbor whose center its
//     leading edge passed. Apply it before the next frame.
//   - Not focusable: keyboard switching belongs to the application.
//   - All functions have internal linkage and work in unity builds.
//
// Author: Steven Wendel (superwendel)

#ifndef RG_GUI_TAB_STRIP_H
#define RG_GUI_TAB_STRIP_H

#ifndef RG_GUI_INCLUDED
#include "rg_gui.h"
#endif

#include <string.h>

/* RgGuiTabStripResult.events */
enum
{
	RG_GUI_TAB_STRIP_ACTIVATE = 1u << 0, /* tab `index` was pressed */
	RG_GUI_TAB_STRIP_CLOSE = 1u << 1,    /* tab `index` asked to close */
	RG_GUI_TAB_STRIP_MOVE = 1u << 2,     /* the dragged tab moved from `from` to `to` */
	RG_GUI_TAB_STRIP_NEW = 1u << 3       /* the "+" button was clicked */
};

/* rg_gui_tab_strip flags */
enum
{
	RG_GUI_TAB_STRIP_NO_NEW_BUTTON = 1u << 0,
	RG_GUI_TAB_STRIP_DIM_ACTIVE = 1u << 1 /* the active tab without the accent line (an inactive pane) */
};

typedef struct RgGuiTabStripItem
{
	const char* label;
	u32 closable;
} RgGuiTabStripItem;

typedef struct RgGuiTabStripResult
{
	u32 events;
	u32 index;
	u32 from;
	u32 to;
} RgGuiTabStripResult;

/* Zero-initialize. */
typedef struct RgGuiTabStripState
{
	f32 scroll;      /* horizontal scroll of the tabs, px */
	f32 grab;        /* pointer x minus the pressed tab's x at the press */
	f32 press_x;     /* pointer x at the press */
	u32 pressed;     /* left-pressed tab + 1; 0 none */
	u32 middle;      /* middle-pressed tab + 1; 0 none */
	u32 shown_active; /* active tab + 1 last frame (scrolled into view when it changes) */
	f32 shown_width;  /* the tabs' area and total width last frame: a layout change */
	f32 shown_total;  /* (a resize, a renamed tab) also scrolls the active tab into view */
	u8 dragging;
	u8 closing;      /* the left press began on the pressed tab's close button */
	u8 new_pressed;
} RgGuiTabStripState;

/* A tab's width: the label, padding and the close button, within limits that
 * follow the strip's height (so they scale with it). */
RGINLINE f32 rg_gui_tab_strip_tab_width(RgGuiContext* ctx, const RgGuiTabStripItem* item, f32 h)
{
	const f32 pad = ctx->style.padding;
	const f32 close = ctx->style.text_height;
	const char* label = item->label ? item->label : "";
	f32 w = rg_gui_text_measure_cached(ctx, label, strlen(label)) + pad * 3.0f + (item->closable ? close + pad : 0.0f);
	if (w < h * 2.5f) w = h * 2.5f;
	if (w > h * 8.0f) w = h * 8.0f;
	return w;
}

/* Draw `label` from `pos`, cut to `width` with an ellipsis when it is wider. */
RGINLINE void rg_gui_tab_strip_label(RgGuiContext* ctx, const char* label, rg_vec2 pos, f32 width, rg_vec4 color)
{
	static const char ellipsis[] = "\xE2\x80\xA6";
	size_t len = strlen(label);
	char cut[256];
	size_t keep;
	if (width <= 0.0f) return;
	if (rg_gui_text_measure_cached(ctx, label, len) <= width)
	{
		rg_gui_push_text(ctx, label, pos, color);
		return;
	}
	keep = rg_gui_text_fit_stops(ctx, label, len, width - rg_gui_text_measure_cached(ctx, ellipsis, 3u));
	if (keep > sizeof(cut) - 4u) keep = sizeof(cut) - 4u;
	memcpy(cut, label, keep);
	memcpy(cut + keep, ellipsis, 4u); /* with the terminator */
	rg_gui_push_text(ctx, cut, pos, color);
}

/* The close button of a tab at `tab`: a square of the text height at its right. */
RGINLINE RgGuiRect rg_gui_tab_strip_close_rect(const RgGuiContext* ctx, RgGuiRect tab)
{
	const f32 s = ctx->style.text_height;
	return rg_gui_make_rect(tab.x + tab.w - ctx->style.padding - s, tab.y + (tab.h - s) * 0.5f, s, s);
}

RGINLINE void rg_gui_tab_strip_cross(RgGuiContext* ctx, RgGuiRect r, rg_vec4 color)
{
	const f32 inset = r.w * 0.3f;
	const f32 t = r.w * 0.09f > 1.0f ? r.w * 0.09f : 1.0f;
	rg_gui_push_line(ctx, rg_vec2(r.x + inset, r.y + inset), rg_vec2(r.x + r.w - inset, r.y + r.h - inset), t, color);
	rg_gui_push_line(ctx, rg_vec2(r.x + r.w - inset, r.y + inset), rg_vec2(r.x + inset, r.y + r.h - inset), t, color);
}

/** Draw the strip and report this frame's events in *out. Returns out->events. */
RGINLINE u32 rg_gui_tab_strip(RgGuiContext* ctx, RgGuiTabStripState* state, const RgGuiTabStripItem* items, u32 count,
                              u32 active, u32 flags, RgGuiRect rect, RgGuiId id, RgGuiTabStripResult* out)
{
	enum { MAX_TABS = 64 };
	f32 x_of[MAX_TABS + 1];
	const f32 h = rect.h;
	const f32 gap = 1.0f;
	const f32 new_w = (flags & RG_GUI_TAB_STRIP_NO_NEW_BUTTON) ? 0.0f : h;
	const rg_vec2 mouse = ctx->mouse_pos;
	const int enabled = !rg_gui_is_disabled(ctx);
	const int middle_pressed = ctx->input && rg_input_is_mouse_button_pressed(ctx->input, RG_MOUSE_BUTTON_MIDDLE);
	const int middle_released = ctx->input && rg_input_is_mouse_button_released(ctx->input, RG_MOUSE_BUTTON_MIDDLE);
	RgGuiRect tabs_rect, new_rect;
	f32 total, max_scroll, drag_x = 0.0f;
	int hovered;
	u32 i, hover_tab = 0u; /* + 1 */
	RgGuiTabStripResult result;
	memset(&result, 0, sizeof(result));
	id = rg_gui_id_scoped(ctx, id);
	if (count > MAX_TABS) count = MAX_TABS;
	if (active >= count) active = count ? count - 1u : 0u;
	if (state->pressed > count) state->pressed = 0u, state->dragging = 0u, state->closing = 0u;
	if (state->middle > count) state->middle = 0u;

	/* Layout: tab offsets from the strip's start; "+" after the last tab, or
	 * pinned at the right when the tabs overflow. */
	x_of[0] = 0.0f;
	for (i = 0; i < count; i++) x_of[i + 1] = x_of[i] + rg_gui_tab_strip_tab_width(ctx, &items[i], h) + gap;
	total = x_of[count];
	if (total + new_w <= rect.w)
	{
		tabs_rect = rg_gui_make_rect(rect.x, rect.y, total, h);
		new_rect = rg_gui_make_rect(rect.x + total, rect.y, new_w, h);
	}
	else
	{
		tabs_rect = rg_gui_make_rect(rect.x, rect.y, rect.w - new_w > 0.0f ? rect.w - new_w : 0.0f, h);
		new_rect = rg_gui_make_rect(rect.x + tabs_rect.w, rect.y, new_w, h);
	}
	max_scroll = total - tabs_rect.w > 0.0f ? total - tabs_rect.w : 0.0f;
	hovered = enabled && rg_gui_point_in_rect(mouse.x, mouse.y, rect);
	if (hovered)
	{
		ctx->hot_id = id;
		if (ctx->mouse_wheel != 0.0f && max_scroll > 0.0f) state->scroll -= ctx->mouse_wheel * h;
	}
	if (count && (state->shown_active != active + 1u || state->shown_width != tabs_rect.w || state->shown_total != total))
	{
		/* A new active tab (a click, a key, a new tab) or a new layout scrolls the
		 * active tab into view; wheel scrolling is left alone otherwise. */
		state->shown_width = tabs_rect.w;
		state->shown_total = total;
		if (x_of[active] < state->scroll) state->scroll = x_of[active];
		else if (x_of[active + 1u] > state->scroll + tabs_rect.w) state->scroll = x_of[active + 1u] - tabs_rect.w;
		state->shown_active = active + 1u;
	}
	if (state->scroll > max_scroll) state->scroll = max_scroll;
	if (state->scroll < 0.0f) state->scroll = 0.0f;

	if (enabled && rg_gui_point_in_rect(mouse.x, mouse.y, tabs_rect))
		for (i = 0; i < count; i++)
		{
			const f32 x0 = tabs_rect.x + x_of[i] - state->scroll, x1 = tabs_rect.x + x_of[i + 1u] - state->scroll - gap;
			if (mouse.x >= x0 && mouse.x < x1)
			{
				hover_tab = i + 1u;
				break;
			}
		}

	/* Presses. */
	if (enabled && ctx->mouse_pressed && hover_tab)
	{
		const u32 t = hover_tab - 1u;
		const RgGuiRect tab = rg_gui_make_rect(tabs_rect.x + x_of[t] - state->scroll, rect.y, x_of[t + 1u] - x_of[t] - gap, h);
		state->pressed = hover_tab;
		state->closing = (u8)(items[t].closable && rg_gui_point_in_rect(mouse.x, mouse.y, rg_gui_tab_strip_close_rect(ctx, tab)));
		state->dragging = 0u;
		state->grab = mouse.x - tab.x;
		state->press_x = mouse.x;
		ctx->active_id = id;
		if (!state->closing)
		{
			result.events |= RG_GUI_TAB_STRIP_ACTIVATE;
			result.index = t;
		}
	}
	if (enabled && middle_pressed && hover_tab) state->middle = hover_tab;
	if (enabled && ctx->mouse_pressed && new_w > 0.0f && rg_gui_point_in_rect(mouse.x, mouse.y, new_rect))
	{
		state->new_pressed = 1u;
		ctx->active_id = id;
	}

	/* Dragging: past a few pixels the pressed tab follows the pointer. It belongs
	 * past every neighbor whose center its leading edge has passed (its center
	 * could not pass a narrower last tab's: the tab stays inside the strip), with
	 * the neighbors already passed shifted out of its way; the gap between tabs
	 * keeps a moved tab from flipping back. The whole move is reported at once,
	 * on the release frame too: a fast drag can arrive in one frame, release
	 * included, and no further frame comes while the pointer rests. */
	if (state->pressed && !state->closing && (ctx->mouse_down || ctx->mouse_released))
	{
		const u32 t = state->pressed - 1u;
		const f32 w = x_of[t + 1u] - x_of[t];
		u32 to = t;
		if (!state->dragging && fabsf(mouse.x - state->press_x) > h * 0.2f) state->dragging = 1u;
		if (state->dragging)
		{
			drag_x = mouse.x - state->grab - tabs_rect.x + state->scroll; /* strip coordinates */
			if (drag_x < 0.0f) drag_x = 0.0f;
			if (drag_x > total - w) drag_x = total - w;
			while (to + 1u < count && drag_x + w > (x_of[to + 1u] + x_of[to + 2u]) * 0.5f) to++;
			if (to == t)
				while (to > 0u && drag_x < (x_of[to - 1u] + x_of[to]) * 0.5f) to--;
			if (to != t)
			{
				result.events |= RG_GUI_TAB_STRIP_MOVE;
				result.from = t;
				result.to = to;
				state->pressed = to + 1u;
			}
		}
	}

	/* Releases. */
	if (ctx->mouse_released)
	{
		if (state->pressed && state->closing && hover_tab == state->pressed)
		{
			const u32 t = state->pressed - 1u;
			const RgGuiRect tab = rg_gui_make_rect(tabs_rect.x + x_of[t] - state->scroll, rect.y, x_of[t + 1u] - x_of[t] - gap, h);
			if (rg_gui_point_in_rect(mouse.x, mouse.y, rg_gui_tab_strip_close_rect(ctx, tab)))
			{
				result.events |= RG_GUI_TAB_STRIP_CLOSE;
				result.index = t;
			}
		}
		if (state->new_pressed && new_w > 0.0f && rg_gui_point_in_rect(mouse.x, mouse.y, new_rect))
			result.events |= RG_GUI_TAB_STRIP_NEW;
		if (ctx->active_id == id) ctx->active_id = 0u;
		state->pressed = 0u;
		state->dragging = 0u;
		state->closing = 0u;
		state->new_pressed = 0u;
	}
	if (middle_released)
	{
		if (state->middle && state->middle == hover_tab && items[state->middle - 1u].closable &&
		    !(result.events & RG_GUI_TAB_STRIP_CLOSE))
		{
			result.events |= RG_GUI_TAB_STRIP_CLOSE;
			result.index = state->middle - 1u;
		}
		state->middle = 0u;
	}

	/* Drawing: the tabs in place (the dragged one last, at the pointer), then "+". */
	rg_gui_push_clip(ctx, tabs_rect);
	for (u32 pass = 0u; pass < 2u; pass++)
		for (i = 0; i < count; i++)
		{
			const int dragged = state->dragging && state->pressed == i + 1u;
			const int is_active = i == active;
			const f32 w = x_of[i + 1u] - x_of[i] - gap;
			RgGuiRect tab;
			rg_vec4 text_color = is_active ? ctx->style.color_text : ctx->style.color_text_dim;
			f32 label_w;
			if ((pass == 0u) == (dragged != 0)) continue;
			tab = rg_gui_make_rect(tabs_rect.x + (dragged ? drag_x : x_of[i]) - state->scroll, rect.y, w, h);
			if (tab.x + tab.w < tabs_rect.x || tab.x > tabs_rect.x + tabs_rect.w) continue;
			if (is_active || dragged) rg_gui_push_rect(ctx, tab, ctx->style.color_bg);
			else if (hover_tab == i + 1u) rg_gui_push_rect(ctx, tab, ctx->style.color_bg_hover);
			if (is_active)
				rg_gui_push_rect(ctx, rg_gui_make_rect(tab.x, tab.y, tab.w, h > 20.0f ? 2.0f : 1.0f),
				                 (flags & RG_GUI_TAB_STRIP_DIM_ACTIVE) ? ctx->style.color_border : ctx->style.color_accent);
			label_w = tab.w - ctx->style.padding * 2.0f - (items[i].closable ? ctx->style.text_height + ctx->style.padding : 0.0f);
			rg_gui_tab_strip_label(ctx, items[i].label ? items[i].label : "",
			                       rg_vec2(tab.x + ctx->style.padding, tab.y + (h - ctx->style.text_height) * 0.5f), label_w,
			                       text_color);
			if (items[i].closable && (is_active || hover_tab == i + 1u || dragged))
			{
				const RgGuiRect close = rg_gui_tab_strip_close_rect(ctx, tab);
				const int over = rg_gui_point_in_rect(mouse.x, mouse.y, close) && !state->dragging;
				if (over) rg_gui_push_rect(ctx, close, ctx->style.color_bg_hover);
				rg_gui_tab_strip_cross(ctx, close, over ? ctx->style.color_text : ctx->style.color_text_dim);
			}
		}
	rg_gui_pop_clip(ctx);
	if (new_w > 0.0f)
	{
		const int over = enabled && rg_gui_point_in_rect(mouse.x, mouse.y, new_rect);
		const f32 arm = h * 0.18f, t = h * 0.05f > 1.0f ? h * 0.05f : 1.0f;
		const rg_vec2 c = rg_vec2(new_rect.x + new_rect.w * 0.5f, new_rect.y + new_rect.h * 0.5f);
		const rg_vec4 color = over ? ctx->style.color_text : ctx->style.color_text_dim;
		if (over) rg_gui_push_rect(ctx, new_rect, ctx->style.color_bg_hover);
		rg_gui_push_line(ctx, rg_vec2(c.x - arm, c.y), rg_vec2(c.x + arm, c.y), t, color);
		rg_gui_push_line(ctx, rg_vec2(c.x, c.y - arm), rg_vec2(c.x, c.y + arm), t, color);
	}
	*out = result;
	return result.events;
}

#endif // RG_GUI_TAB_STRIP_H
