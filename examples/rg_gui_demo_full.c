// Full rg_gui widget and docking showcase.

#include "rg_gui_demo_common.h"
#include "rg_gui_demo_profile.h"
#include "rg_gui_demo_present.h"

#define FULL_PLOT_SAMPLES 120u
#define FULL_CURVE_CAPACITY 8u
#define FULL_GRADIENT_CAPACITY 8u

typedef enum FullMenuGroup
{
	FULL_MENU_FILE = 0,
	FULL_MENU_VIEW,
	FULL_MENU_HELP,
	FULL_MENU_COUNT
} FullMenuGroup;

typedef enum FullFileMenuItem
{
	FULL_FILE_CLEAR_NOTES = 0,
	FULL_FILE_QUIT
} FullFileMenuItem;

typedef enum FullViewMenuItem
{
	FULL_VIEW_RESET_CONTROLS = 0,
	FULL_VIEW_VSYNC,
	FULL_VIEW_MAILBOX,
	FULL_VIEW_IMMEDIATE,
	FULL_VIEW_TOGGLE_LIVE_STATS,
	FULL_VIEW_NEXT_ACCENT,
	FULL_VIEW_ITEM_COUNT
} FullViewMenuItem;

typedef enum FullHelpMenuItem
{
	FULL_HELP_DESCRIBE_DEMO = 0,
	FULL_HELP_KEYBOARD
} FullHelpMenuItem;

typedef struct FullDemoState
{
	RgGuiDockSpaceState dockspace;
	RgGuiWindowState inspector_window;
	RgGuiWindowState stats_window;
	RgGuiWindowState assets_window;
	RgGuiWindowState graph_window;
	int dock_initialized;

	int menu_active;
	int menu_open;
	int menu_selected[FULL_MENU_COUNT];
	int menu_scroll[FULL_MENU_COUNT];
	char last_action[96];
	int request_quit;
	SDL_GPUPresentMode present_mode;
	SDL_GPUPresentMode requested_present_mode;
	int immediate_present_supported;
	int mailbox_present_supported;
	int present_mode_dirty;

	u32 inspector_tab;
	u32 assets_tab;
	char project_name[64];
	char notes[KB(16)];
	int live_preview;
	int quality;
	int renderer_index;
	int renderer_open;
	int renderer_scroll;
	f32 exposure;
	f32 render_scale;
	rg_vec4 accent;

	rg_vec2 curve_points[FULL_CURVE_CAPACITY];
	u32 curve_count;
	int curve_selected;
	RgGuiGradientStop gradient_stops[FULL_GRADIENT_CAPACITY];
	u32 gradient_count;
	int gradient_selected;

	int asset_selected;
	int asset_scroll;
	int hierarchy_selected;
	int scene_open;
	int environment_open;
	RgGuiTreeState tree;
	RgGuiTextAreaState notes_area;
	RgGuiTextAreaLayoutCache notes_cache;
	char notes_cache_text[KB(16)];
	RgGuiTextAreaVisualLine notes_cache_lines[RG_GUI_TEXT_AREA_VISUAL_LINE_MAX];

	int stats_live;
	f32 activity;
	f32 frame_ms[FULL_PLOT_SAMPLES];
	u32 plot_count;
	u32 previous_draw_command_count;
	DemoProfileFrame previous_profile;
	RgGuiRect profile_notes_rect;
	f32 frame_p95, frame_p99;

	RgGuiNodeEditorState node_editor;
	RgGuiNodeGraphState node_state;
	RgGuiNodeGraph graph;
	f32 node_values[4];
} FullDemoState;

static const char* full_menu_labels[] = {"File", "View", "Help"};
static const char* full_file_items[] = {"Clear notes", "Quit"};
static const char* full_view_items[] = {
	"Reset controls", "VSync", "Fast, no tearing", "Uncapped, may tear",
	"Toggle frame stats", "Next accent"};
static const char* full_help_items[] = {"Describe demo", "Keyboard shortcuts"};
static const char* const* full_menu_groups[] =
    {
        full_file_items,
        full_view_items,
        full_help_items};
static const u32 full_menu_counts[] =
    {
        (u32)RG_ARRAY_COUNT(full_file_items),
        (u32)RG_ARRAY_COUNT(full_view_items),
        (u32)RG_ARRAY_COUNT(full_help_items)};

static const char* full_asset_items[] =
    {
        "characters/player.mesh",
        "characters/robot.mesh",
        "materials/metal.mat",
        "materials/emissive.mat",
        "textures/grid.png",
        "textures/noise.png",
        "audio/ambient.bank",
        "scripts/camera.c",
        "scripts/gameplay.c",
        "scenes/showcase.scene",
        "shaders/lighting.hlsl",
        "shaders/particles.hlsl"};

static const char* full_renderer_items[] =
    {
        "Deferred",
        "Forward+",
        "Path traced"};

static const char* full_node_titles[] =
    {
        "Texture Sample",
        "Color Grade",
        "Output"};

static void full_reset_controls(FullDemoState* state)
{
	SDL_strlcpy(state->project_name, "Reverse Gravity", sizeof(state->project_name));
	state->live_preview = 1;
	state->quality = 2;
	state->renderer_index = 1;
	state->exposure = 1.15f;
	state->render_scale = 0.85f;
	state->accent = rg_gui_color(0.24f, 0.56f, 0.95f, 1.0f);
}

static void full_state_init(FullDemoState* state)
{
	memset(state, 0, sizeof(*state));
	rg_gui_text_area_cache_init(&state->notes_cache, state->notes_cache_text,
	                            sizeof(state->notes_cache_text), state->notes_cache_lines,
	                            RG_GUI_TEXT_AREA_VISUAL_LINE_MAX);
	state->notes_area.layout_cache = &state->notes_cache;
	state->menu_active = 0;
	for (int i = 0; i < FULL_MENU_COUNT; i++) state->menu_selected[i] = -1;
	SDL_strlcpy(state->last_action, "Ready", sizeof(state->last_action));
	SDL_strlcpy(state->notes,
	            "Full showcase notes\n\n- Dock and resize panels\n- Edit controls\n- Pan the node graph\n",
	            sizeof(state->notes));
	full_reset_controls(state);
	state->asset_selected = 0;
	state->hierarchy_selected = 0;
	state->scene_open = 1;
	state->environment_open = 1;
	state->stats_live = 1;
	state->present_mode = SDL_GPU_PRESENTMODE_VSYNC;
	state->requested_present_mode = SDL_GPU_PRESENTMODE_VSYNC;

	state->curve_points[0] = rg_vec2(0.0f, 0.0f);
	state->curve_points[1] = rg_vec2(0.25f, 0.72f);
	state->curve_points[2] = rg_vec2(0.65f, 0.38f);
	state->curve_points[3] = rg_vec2(1.0f, 1.0f);
	state->curve_count = 4u;
	state->curve_selected = -1;

	state->gradient_stops[0].position = 0.0f;
	state->gradient_stops[0].color = rg_gui_color(0.04f, 0.08f, 0.18f, 1.0f);
	state->gradient_stops[1].position = 0.48f;
	state->gradient_stops[1].color = rg_gui_color(0.24f, 0.56f, 0.95f, 1.0f);
	state->gradient_stops[2].position = 1.0f;
	state->gradient_stops[2].color = rg_gui_color(0.96f, 0.48f, 0.14f, 1.0f);
	state->gradient_count = 3u;
	state->gradient_selected = -1;

	rg_gui_node_graph_init(&state->graph);
	rg_gui_node_graph_state_reset(&state->node_state);
	rg_gui_node_graph_add_node(&state->graph, rg_gui_id_str("texture_node"),
	                           rg_vec2(20.0f, 60.0f), rg_vec2(190.0f, 128.0f), 0u, 1u);
	rg_gui_node_graph_add_node(&state->graph, rg_gui_id_str("grade_node"),
	                           rg_vec2(285.0f, 35.0f), rg_vec2(190.0f, 145.0f), 1u, 1u);
	rg_gui_node_graph_add_node(&state->graph, rg_gui_id_str("output_node"),
	                           rg_vec2(550.0f, 80.0f), rg_vec2(170.0f, 115.0f), 1u, 0u);
	rg_gui_node_graph_add_link(&state->graph, 0u, 0u, 1u, 0u);
	rg_gui_node_graph_add_link(&state->graph, 1u, 0u, 2u, 0u);
	state->node_values[0] = 0.72f;
	state->node_values[1] = 0.55f;
	state->node_values[2] = 1.0f;
}

static void full_update_stats(FullDemoState* state, f32 frame_time, f32 delta_time)
{
	if (!state->stats_live) return;
	for (u32 i = 1u; i < FULL_PLOT_SAMPLES; i++)
	{
		state->frame_ms[i - 1u] = state->frame_ms[i];
	}
	f32 frame_ms = frame_time * 1000.0f;
	state->frame_ms[FULL_PLOT_SAMPLES - 1u] = frame_ms;
	if (state->plot_count < FULL_PLOT_SAMPLES) state->plot_count++;
	demo_profile_percentiles(
	    state->frame_ms + FULL_PLOT_SAMPLES - state->plot_count, state->plot_count,
	    &state->frame_p95, &state->frame_p99);
	state->activity += delta_time * 0.22f;
	if (state->activity > 1.0f) state->activity -= 1.0f;
}

static const char* full_node_title(void* user, u32 index)
{
	RG_GUI_UNUSED(user);
	if (index >= (u32)RG_ARRAY_COUNT(full_node_titles)) return "Node";
	return full_node_titles[index];
}

static void full_node_content(RgGuiContext* gui, u32 index, RgGuiId node_id, void* user)
{
	FullDemoState* state = (FullDemoState*)user;
	RG_GUI_UNUSED(node_id);
	if (!state || index >= 3u) return;
	static const char* value_labels[] = {"Source", "Strength", "Display"};
	rg_gui_label_static(gui, value_labels[index], rg_gui_layout_next(gui, 24.0f));
	rg_gui_progress_bar(gui, NULL, state->node_values[index], 0.0f, 1.0f,
	                    rg_gui_layout_next(gui, 18.0f));
}

static void full_apply_menu_action(FullDemoState* state, int group, int item)
{
	if (group < 0 || group >= FULL_MENU_COUNT || item < 0 || item >= (int)full_menu_counts[group]) return;
	rg_snprintf(state->last_action, sizeof(state->last_action), "%s > %s",
	             full_menu_labels[group], full_menu_groups[group][item]);
	if (group == FULL_MENU_FILE && item == FULL_FILE_CLEAR_NOTES)
	{
		state->notes[0] = '\0';
		SDL_strlcpy(state->last_action, "Notes cleared", sizeof(state->last_action));
	}
	else if (group == FULL_MENU_FILE && item == FULL_FILE_QUIT) state->request_quit = 1;
	else if (group == FULL_MENU_VIEW && item == FULL_VIEW_RESET_CONTROLS) full_reset_controls(state);
	else if (group == FULL_MENU_VIEW &&
	         (item == FULL_VIEW_VSYNC || item == FULL_VIEW_MAILBOX || item == FULL_VIEW_IMMEDIATE))
	{
		state->requested_present_mode = item == FULL_VIEW_VSYNC ? SDL_GPU_PRESENTMODE_VSYNC :
		                                item == FULL_VIEW_MAILBOX ? SDL_GPU_PRESENTMODE_MAILBOX :
		                                                           SDL_GPU_PRESENTMODE_IMMEDIATE;
		state->present_mode_dirty = 1;
	}
	else if (group == FULL_MENU_VIEW && item == FULL_VIEW_TOGGLE_LIVE_STATS)
	{
		state->stats_live = !state->stats_live;
	}
	else if (group == FULL_MENU_VIEW && item == FULL_VIEW_NEXT_ACCENT)
	{
		f32 red = state->accent.x;
		state->accent.x = state->accent.y;
		state->accent.y = state->accent.z;
		state->accent.z = red;
	}
	else if (group == FULL_MENU_HELP && item == FULL_HELP_DESCRIBE_DEMO)
	{
		SDL_strlcpy(state->last_action, "Widget, docking, and SDL3 GPU showcase",
		            sizeof(state->last_action));
	}
	else if (group == FULL_MENU_HELP && item == FULL_HELP_KEYBOARD)
	{
		SDL_strlcpy(state->last_action, "Tab moves focus; Escape exits the demo",
		            sizeof(state->last_action));
	}
}

static void full_draw_menu(RgGuiContext* gui, FullDemoState* state, int width)
{
	RgGuiRect menu_rect = rg_gui_make_rect(0.0f, 0.0f, (f32)width, 28.0f);
	rg_gui_push_rect(gui, menu_rect, gui->style.color_panel);
	RgGuiRect active_rect = rg_gui_make_rect(0.0f, 0.0f, 0.0f, 0.0f);
	RgGuiId menu_id = rg_gui_id_str("full_menu");
	rg_gui_menu_bar(gui, full_menu_labels, (u32)RG_ARRAY_COUNT(full_menu_labels),
	                &state->menu_active, &state->menu_open, menu_rect,
	                menu_id, &active_rect);

	if (state->menu_open && state->menu_active >= 0 && state->menu_active < FULL_MENU_COUNT)
	{
		int group = state->menu_active;
		RgGuiMenuItemFlags view_flags[FULL_VIEW_ITEM_COUNT] = {RG_GUI_MENU_ITEM_NONE};
		view_flags[FULL_VIEW_VSYNC] = RG_GUI_MENU_ITEM_CHECKABLE;
		view_flags[FULL_VIEW_MAILBOX] = RG_GUI_MENU_ITEM_CHECKABLE;
		view_flags[FULL_VIEW_IMMEDIATE] = RG_GUI_MENU_ITEM_CHECKABLE;
		if (state->present_mode == SDL_GPU_PRESENTMODE_VSYNC)
			view_flags[FULL_VIEW_VSYNC] |= RG_GUI_MENU_ITEM_CHECKED;
		else if (state->present_mode == SDL_GPU_PRESENTMODE_MAILBOX)
			view_flags[FULL_VIEW_MAILBOX] |= RG_GUI_MENU_ITEM_CHECKED;
		else
			view_flags[FULL_VIEW_IMMEDIATE] |= RG_GUI_MENU_ITEM_CHECKED;
		if (!state->mailbox_present_supported)
			view_flags[FULL_VIEW_MAILBOX] |= RG_GUI_MENU_ITEM_DISABLED;
		if (!state->immediate_present_supported)
			view_flags[FULL_VIEW_IMMEDIATE] |= RG_GUI_MENU_ITEM_DISABLED;
		const RgGuiMenuItemFlags* item_flags =
		    (group == FULL_MENU_VIEW) ? view_flags : NULL;
		RgGuiRect popup = rg_gui_make_rect(active_rect.x, active_rect.y + active_rect.h,
		                                   190.0f, 0.0f);
		if (rg_gui_menu_popup_ex(gui, full_menu_groups[group], NULL, item_flags,
		                         full_menu_counts[group], &state->menu_selected[group],
		                         &state->menu_open, &state->menu_scroll[group], popup, menu_id,
		                         NULL, NULL, NULL, NULL, 1) &&
		    !state->menu_open)
		{
			full_apply_menu_action(state, group, state->menu_selected[group]);
			state->menu_selected[group] = -1;
		}
	}
}

static void full_draw_inspector(RgGuiContext* gui, FullDemoState* state, u32 window_flags)
{
	RgGuiRect initial = rg_gui_make_rect(18.0f, 54.0f, 330.0f, 690.0f);
	if (!rg_gui_window_begin(gui, &state->inspector_window, "Inspector", initial,
	                         270.0f, 300.0f, 8.0f, window_flags,
	                         rg_gui_id_str("inspector_window")))
		return;

	static const char* tabs[] = {"General", "Color", "Curves"};
	rg_gui_tabs(gui, tabs, (u32)RG_ARRAY_COUNT(tabs), &state->inspector_tab,
	            rg_gui_layout_next(gui, 28.0f), rg_gui_id_str("inspector_tabs"));

	if (state->inspector_tab == 0u)
	{
		int submitted = 0;
		rg_gui_text_input(gui, "Project", state->project_name, sizeof(state->project_name),
		                  rg_gui_layout_next(gui, 28.0f), rg_gui_id_str("project_name"), &submitted);
		RG_GUI_UNUSED(submitted);
		rg_gui_checkbox_static(gui, "Live preview", &state->live_preview,
		                       rg_gui_layout_next(gui, 25.0f), rg_gui_id_str("live_preview"));
		rg_gui_slider_float_input(gui, "Exposure", &state->exposure, 0.0f, 4.0f,
		                          rg_gui_layout_next(gui, 30.0f), rg_gui_id_str("exposure"));
		rg_gui_slider_float(gui, "Render scale", &state->render_scale, 0.25f, 1.0f,
		                    rg_gui_layout_next(gui, 28.0f), rg_gui_id_str("render_scale"));
		rg_gui_radio_static(gui, "Low quality", &state->quality, 0,
		                    rg_gui_layout_next(gui, 24.0f), rg_gui_id_str("quality_low"));
		rg_gui_radio_static(gui, "Medium quality", &state->quality, 1,
		                    rg_gui_layout_next(gui, 24.0f), rg_gui_id_str("quality_medium"));
		rg_gui_radio_static(gui, "High quality", &state->quality, 2,
		                    rg_gui_layout_next(gui, 24.0f), rg_gui_id_str("quality_high"));
		rg_gui_dropdown(gui, full_renderer_items, (u32)RG_ARRAY_COUNT(full_renderer_items),
		                &state->renderer_index, &state->renderer_open, &state->renderer_scroll,
		                rg_gui_layout_next(gui, 28.0f), rg_gui_id_str("renderer_dropdown"));
	}
	else if (state->inspector_tab == 1u)
	{
		rg_gui_label_static(gui, "Theme accent", rg_gui_layout_next(gui, 24.0f));
		rg_gui_color_picker_hsv(gui, &state->accent, rg_gui_layout_next(gui, 220.0f),
		                        rg_gui_id_str("accent_picker"), RG_GUI_COLOR_PICKER_NO_ALPHA);
		gui->style.color_accent = state->accent;
	}
	else
	{
		rg_gui_curve_editor(gui, "Response", state->curve_points, &state->curve_count,
		                    FULL_CURVE_CAPACITY, rg_vec2(0.0f, 0.0f), rg_vec2(1.0f, 1.0f),
		                    rg_gui_layout_next(gui, 190.0f), rg_gui_id_str("response_curve"),
		                    RG_GUI_CURVE_NONE, &state->curve_selected);
		rg_gui_gradient_editor(gui, "Palette", state->gradient_stops, &state->gradient_count,
		                       FULL_GRADIENT_CAPACITY, rg_gui_layout_next(gui, 58.0f),
		                       rg_gui_id_str("palette_gradient"), RG_GUI_GRADIENT_NO_ALPHA,
		                       &state->gradient_selected);
	}
	rg_gui_window_end(gui, &state->inspector_window);
}

static void full_draw_stats(RgGuiContext* gui, FullDemoState* state, u32 window_flags)
{
	RgGuiRect initial = rg_gui_make_rect(930.0f, 54.0f, 330.0f, 690.0f);
	if (!rg_gui_window_begin(gui, &state->stats_window, "Frame Stats", initial,
	                         270.0f, 280.0f, 8.0f, window_flags,
	                         rg_gui_id_str("stats_window")))
		return;

	char frame_text[64];
	char present_text[64];
	f32 latest_ms = state->frame_ms[FULL_PLOT_SAMPLES - 1u];
	SDL_snprintf(frame_text, sizeof(frame_text), "Frame %.2f ms / %.0f FPS",
	             latest_ms, latest_ms > 0.0f ? 1000.0f / latest_ms : 0.0f);
	rg_gui_label(gui, frame_text, rg_gui_layout_next(gui, 26.0f));
	rg_snprintf(present_text, sizeof(present_text), "Present: %s",
	             state->present_mode == SDL_GPU_PRESENTMODE_MAILBOX ? "Mailbox (no tearing)" :
	             state->present_mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "Immediate (may tear)" :
	                                                                   "VSync");
	rg_gui_label(gui, present_text, rg_gui_layout_next(gui, 24.0f));
	rg_gui_checkbox_static(gui, "Live capture", &state->stats_live,
	                       rg_gui_layout_next(gui, 25.0f), rg_gui_id_str("stats_live"));
	rg_gui_progress_bar(gui, "Animated sample", state->activity, 0.0f, 1.0f,
	                    rg_gui_layout_next(gui, 24.0f));
	rg_gui_plot_lines(gui, "Frame time history", state->frame_ms + (FULL_PLOT_SAMPLES - state->plot_count),
	                  state->plot_count, 0.0f, 34.0f, rg_gui_layout_next(gui, 105.0f));
	rg_gui_plot_histogram(gui, "Frame time samples", state->frame_ms + (FULL_PLOT_SAMPLES - state->plot_count),
	                      state->plot_count, 0.0f, 34.0f, rg_gui_layout_next(gui, 105.0f));

	char cache_text[96];
	rg_snprintf(cache_text, sizeof(cache_text), "Draw commands (previous frame): %u",
	             state->previous_draw_command_count);
	rg_gui_label(gui, cache_text, rg_gui_layout_next(gui, 24.0f));
	const DemoProfileFrame* p = &state->previous_profile;
	const RgGuiGpuStats* g = &p->main.draw_stats;
	SDL_snprintf(cache_text, sizeof(cache_text), "Frame p95 %.2f / p99 %.2f ms (last %u)",
	             state->frame_p95, state->frame_p99, state->plot_count);
	rg_gui_label(gui, cache_text, rg_gui_layout_next(gui, 22.0f));
	SDL_snprintf(cache_text, sizeof(cache_text), "CPU UI %.3f / prepare %.3f ms", p->ui_ms, p->main.prepare_ms);
	rg_gui_label(gui, cache_text, rg_gui_layout_next(gui, 22.0f));
	SDL_snprintf(cache_text, sizeof(cache_text), "CPU upload %.3f / encode %.3f ms", p->main.stage_upload_ms,
	             p->main.encode_ms + p->main.draw_encode_ms);
	rg_gui_label(gui, cache_text, rg_gui_layout_next(gui, 22.0f));
	SDL_snprintf(cache_text, sizeof(cache_text), "Present wait %.3f / submit %.3f ms",
	             p->main.swapchain_wait_ms, p->main.submit_ms);
	rg_gui_label(gui, cache_text, rg_gui_layout_next(gui, 22.0f));
	rg_snprintf(cache_text, sizeof(cache_text), "Draws %u / dispatches %u / glyphs %u",
	             g->draw_calls, g->dispatches, g->text_instances);
	rg_gui_label(gui, cache_text, rg_gui_layout_next(gui, 22.0f));
	rg_snprintf(cache_text, sizeof(cache_text), "Upload run/cache/geometry: %u / %u / %u B",
	             g->run_upload_bytes, g->cache_upload_bytes, g->geometry_upload_bytes);
	rg_gui_label(gui, cache_text, rg_gui_layout_next(gui, 22.0f));
	rg_snprintf(cache_text, sizeof(cache_text), "Text cache hits %u / misses %u / evictions %u",
	             p->main.text_stats.frame_cache_hits, p->main.text_stats.frame_cache_misses,
	             p->main.text_stats.frame_cache_evictions);
	rg_gui_label(gui, cache_text, rg_gui_layout_next(gui, 22.0f));
	// last_action is rewritten by menu handlers, so it must be copied into this
	// frame instead of using pointer identity as an immutable text-cache key.
	rg_gui_label(gui, state->last_action, rg_gui_layout_next(gui, 28.0f));
	rg_gui_window_end(gui, &state->stats_window);
}

static void full_draw_hierarchy(RgGuiContext* gui, FullDemoState* state)
{
	rg_gui_tree_begin(gui, &state->tree, 18.0f);
	u32 result = rg_gui_tree_node_static(gui, &state->tree, "Scene", &state->scene_open,
	                                     state->hierarchy_selected == 0,
	                                     rg_gui_layout_next(gui, 25.0f), rg_gui_id_str("scene"));
	if (result & RG_GUI_TREE_NODE_RESULT_SELECTION) state->hierarchy_selected = 0;
	if (state->scene_open)
	{
		rg_gui_tree_push(&state->tree);
		result = rg_gui_tree_node_static(gui, &state->tree, "Player", NULL,
		                                 state->hierarchy_selected == 1,
		                                 rg_gui_layout_next(gui, 25.0f), rg_gui_id_str("player"));
		if (result & RG_GUI_TREE_NODE_RESULT_SELECTION) state->hierarchy_selected = 1;
		result = rg_gui_tree_node_static(gui, &state->tree, "Environment", &state->environment_open,
		                                 state->hierarchy_selected == 2,
		                                 rg_gui_layout_next(gui, 25.0f), rg_gui_id_str("environment"));
		if (result & RG_GUI_TREE_NODE_RESULT_SELECTION) state->hierarchy_selected = 2;
		if (state->environment_open)
		{
			rg_gui_tree_push(&state->tree);
			result = rg_gui_tree_node_static(gui, &state->tree, "Key light", NULL,
			                                 state->hierarchy_selected == 3,
			                                 rg_gui_layout_next(gui, 25.0f), rg_gui_id_str("key_light"));
			if (result & RG_GUI_TREE_NODE_RESULT_SELECTION) state->hierarchy_selected = 3;
			result = rg_gui_tree_node_static(gui, &state->tree, "Ground", NULL,
			                                 state->hierarchy_selected == 4,
			                                 rg_gui_layout_next(gui, 25.0f), rg_gui_id_str("ground"));
			if (result & RG_GUI_TREE_NODE_RESULT_SELECTION) state->hierarchy_selected = 4;
			rg_gui_tree_pop(&state->tree);
		}
		rg_gui_tree_pop(&state->tree);
	}
}

static void full_draw_assets(RgGuiContext* gui, FullDemoState* state, u32 window_flags)
{
	RgGuiRect initial = rg_gui_make_rect(370.0f, 455.0f, 540.0f, 290.0f);
	if (!rg_gui_window_begin(gui, &state->assets_window, "Workspace", initial,
	                         320.0f, 220.0f, 8.0f, window_flags,
	                         rg_gui_id_str("assets_window")))
		return;

	static const char* tabs[] = {"Assets", "Hierarchy", "Notes"};
	rg_gui_tabs(gui, tabs, (u32)RG_ARRAY_COUNT(tabs), &state->assets_tab,
	            rg_gui_layout_next(gui, 28.0f), rg_gui_id_str("workspace_tabs"));
	if (state->assets_tab == 0u)
	{
		rg_gui_list(gui, full_asset_items, (u32)RG_ARRAY_COUNT(full_asset_items),
		            &state->asset_selected, &state->asset_scroll,
		            rg_gui_layout_next(gui, 205.0f), rg_gui_id_str("asset_list"));
	}
	else if (state->assets_tab == 1u)
	{
		full_draw_hierarchy(gui, state);
	}
	else
	{
		state->profile_notes_rect = rg_gui_layout_next(gui, 205.0f);
		rg_gui_text_area(gui, &state->notes_area, state->notes, sizeof(state->notes),
		                 state->profile_notes_rect, rg_gui_id_str("notes"));
	}
	rg_gui_window_end(gui, &state->assets_window);
}

static void full_draw_graph(RgGuiContext* gui, FullDemoState* state, u32 window_flags)
{
	RgGuiRect initial = rg_gui_make_rect(370.0f, 54.0f, 540.0f, 380.0f);
	if (!rg_gui_window_begin(gui, &state->graph_window, "Node Graph", initial,
	                         360.0f, 260.0f, 8.0f, window_flags,
	                         rg_gui_id_str("graph_window")))
		return;

	RgGuiNodeGraphDraw draw = {0};
	draw.title = full_node_title;
	draw.content = full_node_content;
	rg_gui_node_graph_editor(gui, &state->node_editor, &state->node_state,
	                         &state->graph, &draw, NULL, NULL,
	                         rg_gui_layout_next(gui, 330.0f), rg_gui_id_str("node_graph"), state);
	rg_gui_window_end(gui, &state->graph_window);
}

static void full_build_ui(RgGuiContext* gui, FullDemoState* state, int width, int height)
{
	full_draw_menu(gui, state, width);
	RgGuiRect bounds = rg_gui_make_rect(0.0f, 28.0f, (f32)width, (f32)height - 28.0f);
	if (bounds.h < 0.0f) bounds.h = 0.0f;
	rg_gui_window_set_bounds(gui, bounds);

	RgGuiRect dock_rect = rg_gui_make_rect(10.0f, 38.0f, (f32)width - 20.0f,
	                                       (f32)height - 48.0f);
	if (dock_rect.w < 0.0f) dock_rect.w = 0.0f;
	if (dock_rect.h < 0.0f) dock_rect.h = 0.0f;
	rg_gui_push_rect(gui, dock_rect, gui->style.color_panel);
	rg_gui_push_rect_outline(gui, dock_rect, gui->style.color_border,
	                         gui->style.border_thickness);
	rg_gui_dockspace_begin(gui, &state->dockspace, dock_rect, rg_gui_id_str("showcase_dockspace"));
	if (!state->dock_initialized && dock_rect.w > 0.0f && dock_rect.h > 0.0f)
	{
		rg_gui_dockspace_dock(gui, &state->dockspace, &state->inspector_window, "Inspector",
		                      rg_gui_id_str("inspector_window"), RG_GUI_DOCK_SLOT_LEFT);
		rg_gui_dockspace_dock(gui, &state->dockspace, &state->stats_window, "Frame Stats",
		                      rg_gui_id_str("stats_window"), RG_GUI_DOCK_SLOT_RIGHT);
		rg_gui_dockspace_dock(gui, &state->dockspace, &state->assets_window, "Workspace",
		                      rg_gui_id_str("assets_window"), RG_GUI_DOCK_SLOT_CENTER);
		rg_gui_dockspace_dock(gui, &state->dockspace, &state->graph_window, "Node Graph",
		                      rg_gui_id_str("graph_window"), RG_GUI_DOCK_SLOT_CENTER);
		state->dock_initialized = 1;
	}
	rg_gui_dockspace_end(gui, &state->dockspace);

	u32 window_flags = RG_GUI_WINDOW_MOVABLE | RG_GUI_WINDOW_RESIZABLE |
	                   RG_GUI_WINDOW_COLLAPSIBLE | RG_GUI_WINDOW_SNAP |
	                   RG_GUI_WINDOW_DOCKABLE;
	full_draw_inspector(gui, state, window_flags);
	full_draw_stats(gui, state, window_flags);
	full_draw_assets(gui, state, window_flags);
	full_draw_graph(gui, state, window_flags);
}

static int full_apply_present_mode(SDL_GPUDevice* device, SDL_Window* window, FullDemoState* state)
{
	if (!state->present_mode_dirty) return 1;
	state->present_mode_dirty = 0;
	if (state->requested_present_mode == state->present_mode) return 1;

	if (!demo_present_apply(device, window, state->requested_present_mode))
	{
		rg_snprintf(state->last_action, sizeof(state->last_action),
		             "Present mode change failed: %s", SDL_GetError());
		return 0;
	}
	state->present_mode = state->requested_present_mode;
	rg_snprintf(state->last_action, sizeof(state->last_action), "Presentation: %s",
	             state->present_mode == SDL_GPU_PRESENTMODE_MAILBOX ? "Fast, no tearing" :
	             state->present_mode == SDL_GPU_PRESENTMODE_IMMEDIATE ? "Uncapped, may tear" :
	                                                                   "VSync");
	return 1;
}

typedef enum FullProfileScenario
{
	FULL_PROFILE_MANUAL, FULL_PROFILE_IDLE, FULL_PROFILE_TEXT_IDLE,
	FULL_PROFILE_EDIT, FULL_PROFILE_SCROLL, FULL_PROFILE_RESIZE, FULL_PROFILE_IMAGES,
	FULL_PROFILE_IMAGES_GROUPED
} FullProfileScenario;

static int full_profile_scenario(const char* name)
{
	static const char* names[] = {"manual", "idle", "text-idle", "edit", "scroll", "resize", "images", "images-grouped"};
	for (u32 i = 0u; i < RG_ARRAY_COUNT(names); i++)
		if (strcmp(name, names[i]) == 0) return (int)i;
	return -1;
}

static void full_profile_seed_notes(FullDemoState* state)
{
	static const char line[] = "The quick brown fox jumps over the lazy dog. Text layout and selection profiling.\n";
	size_t used = 0u;
	while (used + sizeof(line) < KB(8))
	{
		memcpy(state->notes + used, line, sizeof(line) - 1u);
		used += sizeof(line) - 1u;
	}
	state->notes[used] = '\0';
	state->assets_tab = 2u;
}

// Feed scripted events through the same ordered input path as SDL events.
// No OS input is injected; normal input remains available in the manual scenario.
static u32 full_profile_input(int scenario, u32 frame, FullDemoState* state,
                               RgGuiContext* gui, RgInputState* input,
                               RgInputEventQueue* events, SDL_WindowID window_id)
{
	if (scenario == FULL_PROFILE_MANUAL) return 0u;
	memset(input->current_keyboard, 0, sizeof(input->current_keyboard));
	memset(input->previous_keyboard, 0, sizeof(input->previous_keyboard));
	memset(input->current_mouse, 0, sizeof(input->current_mouse));
	memset(input->previous_mouse, 0, sizeof(input->previous_mouse));
	input->mouse_x = input->mouse_y = -10000;
	input->mouse_scroll_y = 0.0f;
	input->has_text_input = false;
	rg_input_event_queue_reset(events, SDL_KMOD_NONE);
	if (scenario < FULL_PROFILE_TEXT_IDLE || scenario > FULL_PROFILE_SCROLL) return 0u;
	if (state->assets_window.dock_node > 0u && state->assets_window.dock_node <= gui->dock_node_capacity)
		gui->dock_nodes[state->assets_window.dock_node].active_tab = state->assets_window.dock_tab;
	if (scenario == FULL_PROFILE_TEXT_IDLE) return 0u;
	RgGuiRect rect = state->profile_notes_rect;
	if (frame < 2u || rect.w <= 0.0f || rect.h <= 0.0f) return 0u;
	f32 x = rect.x + 24.0f, y = rect.y + 20.0f;
	input->mouse_x = (int)x; input->mouse_y = (int)y;
	SDL_Event event;
	memset(&event, 0, sizeof(event));
	if (frame == 2u || frame == 3u)
	{
		input->current_mouse[RG_MOUSE_BUTTON_LEFT] = frame == 2u;
		input->previous_mouse[RG_MOUSE_BUTTON_LEFT] = frame == 3u;
		event.type = frame == 2u ? SDL_EVENT_MOUSE_BUTTON_DOWN : SDL_EVENT_MOUSE_BUTTON_UP;
		event.button.windowID = window_id;
		event.button.button = SDL_BUTTON_LEFT;
		event.button.down = frame == 2u;
		event.button.clicks = 1u;
		event.button.x = x; event.button.y = y;
		rg_input_process_event_ex(input, &event, events);
		return 1u;
	}
	if (scenario == FULL_PROFILE_SCROLL)
	{
		// A wheel event every frame, reversing direction every 120 frames.
		event.type = SDL_EVENT_MOUSE_WHEEL;
		event.wheel.windowID = window_id;
		event.wheel.y = (frame / 120u) % 2u ? 1.0f : -1.0f;
		event.wheel.mouse_x = x; event.wheel.mouse_y = y;
		rg_input_process_event_ex(input, &event, events);
		return 1u;
	}
	if (frame % 4u == 0u)
	{
		event.type = SDL_EVENT_TEXT_INPUT;
		event.text.windowID = window_id;
		event.text.text = "x";
		rg_input_process_event_ex(input, &event, events);
		return 1u;
	}
	if (frame % 4u == 2u)
	{
		event.type = SDL_EVENT_KEY_DOWN;
		event.key.windowID = window_id;
		event.key.scancode = SDL_SCANCODE_BACKSPACE;
		event.key.key = SDLK_BACKSPACE;
		event.key.down = true;
		rg_input_process_event_ex(input, &event, events);
		event.type = SDL_EVENT_KEY_UP;
		event.key.down = false;
		rg_input_process_event_ex(input, &event, events);
		return 1u;
	}
	return 0u;
}

static void full_profile_images(RgGuiContext* gui, int width, int height,
                                 SDL_GPUTexture* first, SDL_GPUTexture* second, int grouped)
{
	f32 cell_w = ((f32)width - 32.0f) / 32.0f;
	f32 cell_h = ((f32)height - 90.0f) / 16.0f;
	RgGuiRect clip = rg_gui_make_rect(12.0f, 62.0f, (f32)width - 24.0f, (f32)height - 74.0f);
	rg_gui_push_rect(gui, clip, rg_gui_color(0.06f, 0.07f, 0.09f, 1.0f));
	rg_gui_push_clip(gui, clip);
	for (u32 i = 0u; i < 512u; i++)
	{
		SDL_GPUTexture* texture = (grouped ? i >= 256u : i % 2u != 0u) ? second : first;
		rg_gui_push_image(gui, rg_gui_make_rect(16.0f + (f32)(i % 32u) * cell_w,
		                                      66.0f + (f32)(i / 32u) * cell_h,
		                                      cell_w - 2.0f, cell_h - 2.0f),
		                  rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f),
		                  rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f), (RgGuiTexture)(uintptr_t)texture);
	}
	rg_gui_pop_clip(gui);
}

int main(int argc, char** argv)
{
	int result = 1;
	int hidden = 0;
	int frame_limit = 0;
	SDL_GPUPresentMode start_present_mode = SDL_GPU_PRESENTMODE_VSYNC;
	int smoke_test = 0;
	int capture_wait_ms = 0;
	DemoProfileOptions profile_options;
	demo_profile_options_init(&profile_options);
	DemoProfileCapture profile_capture = {0};
	for (int i = 1; i < argc; i++)
	{
		int profile_arg = demo_profile_parse_arg(&profile_options, argc, argv, &i);
		if (profile_arg < 0) return 1;
		if (profile_arg > 0) continue;
		int present_arg = demo_present_parse_arg(argc, argv, &i, &start_present_mode);
		if (present_arg < 0)
		{
			fprintf(stderr, "%s\n", SDL_GetError());
			return 1;
		}
		if (present_arg > 0) continue;
		if (strcmp(argv[i], "--hidden") == 0) hidden = 1;
		else if (strcmp(argv[i], "--smoke-test") == 0) smoke_test = 1;
		else if (strcmp(argv[i], "--capture-wait-ms") == 0 && i + 1 < argc)
		{
			capture_wait_ms = atoi(argv[++i]);
			if (capture_wait_ms < 0 || capture_wait_ms > 10000) return 1;
		}
		else if (strcmp(argv[i], "--frames") == 0 && i + 1 < argc)
		{
			frame_limit = atoi(argv[++i]);
			if (frame_limit < 1) frame_limit = 1;
		}
		else
		{
			fprintf(stderr, "Unknown or incomplete option: %s\n", argv[i]);
			return 1;
		}
	}
	int profile_scenario = full_profile_scenario(profile_options.scenario);
	if (profile_scenario < 0)
	{
		fprintf(stderr, "Unknown full-demo scenario: %s\n", profile_options.scenario);
		return 1;
	}
	if (profile_options.path && frame_limit == 0) frame_limit = (int)profile_options.warmup + 1200;
	if (smoke_test)
	{
		hidden = 0;
		frame_limit = 3;
	}

	SDL_Window* window = NULL;
	SDL_GPUDevice* device = NULL;
	SDL_GPUTexture* atlas = NULL;
	SDL_GPUTexture* profile_atlas = NULL;
	RgGuiGpuRenderer gpu = {0};
	RgGpuUploadRing upload_ring = {0};
	DemoFontAssets demo_font = {0};
	DemoPlatformState platform_state = {0};
	RgInputState input = {0};
	void* gui_memory = NULL;
	void* text_memory = NULL;
	int window_claimed = 0;

	if (!SDL_Init(SDL_INIT_VIDEO)) goto cleanup;
	if (!demo_profile_capture_begin(&profile_capture, &profile_options, (u32)frame_limit)) goto cleanup;
	demo_platform_init(&platform_state);
	SDL_WindowFlags window_flags = SDL_WINDOW_RESIZABLE;
	if (hidden) window_flags |= SDL_WINDOW_HIDDEN;
	window = SDL_CreateWindow("rg_gui full showcase", 1280, 800, window_flags);
	if (!window) goto cleanup;

	RgGpuDeviceDesc device_desc = {0};
	device_desc.shader_formats = RG_GPU_DEFAULT_SHADER_FORMATS;
	device_desc.enable_debug = RG_GUI_DEMO_GPU_DEBUG;
	device = rg_gpu_device_create(&device_desc);
	if (!device || !rg_gpu_claim_window(device, window)) goto cleanup;
	window_claimed = 1;

	if (!demo_font_load(&demo_font)) goto cleanup;
	atlas = demo_atlas_create(device, demo_font.pixels,
	                          demo_font.atlas_width, demo_font.atlas_height);
	if (!atlas) goto cleanup;
	if (profile_scenario == FULL_PROFILE_IMAGES || profile_scenario == FULL_PROFILE_IMAGES_GROUPED)
	{
		profile_atlas = demo_atlas_create(device, demo_font.pixels, demo_font.atlas_width, demo_font.atlas_height);
		if (!profile_atlas) goto cleanup;
	}

	RgGuiContext gui;
	RgGuiInitDesc gui_desc = {0};
	gui_desc.font = &demo_font.font;
	gui_desc.text_lookup = &demo_font.text_lookup;
	gui_desc.max_draw_cmds = 16384u;
	gui_desc.text_buffer_size = KB(256);
	size_t gui_memory_size = rg_gui_memory_required(&gui_desc);
	if (!gui_memory_size)
	{
		SDL_SetError("Invalid rg_gui initialization limits");
		goto cleanup;
	}
	gui_memory = malloc(gui_memory_size);
	if (!gui_memory)
	{
		SDL_SetError("Out of memory while allocating the rg_gui arena");
		goto cleanup;
	}
	RgArena gui_arena = {(char*)gui_memory, gui_memory_size, 0u, gui_memory_size};
	if (!rg_gui_init(&gui, &gui_arena, &gui_desc))
	{
		SDL_SetError("rg_gui initialization rejected the demo arena or limits");
		goto cleanup;
	}

	RgGuiRendererLimits limits = rg_gui_renderer_limits_default();
	limits.max_frame_instances = 32768u;
	limits.max_frame_runs = 2048u;
	limits.max_batches = 4096u;
	RgGuiRendererInitDesc text_desc = {0};
	text_desc.font = &demo_font.font;
	text_desc.limits = limits;
	text_desc.text_lookup = &demo_font.text_lookup;
	size_t text_memory_size = rg_gui_renderer_memory_required_ex(&text_desc);
	if (text_memory_size == SIZE_MAX)
	{
		SDL_SetError("Invalid text-renderer limits");
		goto cleanup;
	}
	text_memory = malloc(text_memory_size);
	if (!text_memory)
	{
		SDL_SetError("Out of memory while allocating the text-renderer arena");
		goto cleanup;
	}
	RgArena text_arena = {(char*)text_memory, text_memory_size, 0u, text_memory_size};
	RgGuiRenderer text_renderer;
	if (!rg_gui_renderer_init(&text_renderer, &text_arena, &text_desc))
	{
		SDL_SetError("Text-renderer initialization rejected the demo arena or limits");
		goto cleanup;
	}

	char shader_root[1024];
	if (!demo_shader_root(shader_root, sizeof(shader_root))) goto cleanup;
	RgGuiGpuDesc gpu_desc = {0};
	gpu_desc.device = device;
	gpu_desc.target_format = rg_gpu_swapchain_format(device, window);
	gpu_desc.shader_root = shader_root;
	gpu_desc.atlas_texture = atlas;
	gpu_desc.atlas_width = demo_font.atlas_width;
	gpu_desc.atlas_height = demo_font.atlas_height;
	gpu_desc.max_cached_quads = limits.max_cached_quads;
	gpu_desc.max_runs = limits.max_frame_runs;
	gpu_desc.max_text_instances = limits.max_frame_instances;
	gpu_desc.max_geometry_vertices = 65536u;
	gpu_desc.max_items = 8192u;
	gpu_desc.frame_buffer_count = 2u;
	gpu_desc.min_filter = SDL_GPU_FILTER_LINEAR;
	gpu_desc.mag_filter = SDL_GPU_FILTER_LINEAR;
	if (!rg_gui_gpu_create(&gpu, &gpu_desc)) goto cleanup;
	if (!rg_gpu_upload_ring_init(&upload_ring, device,
	                            rg_gui_gpu_upload_ring_size_required(&gpu))) goto cleanup;

	rg_input_init(&input);
	RgInputEvent input_event_storage[256];
	char input_event_text[KB(8)];
	RgInputEventQueue input_events;
	rg_input_event_queue_init(&input_events, input_event_storage,
	                          RG_ARRAY_COUNT(input_event_storage),
	                          input_event_text, sizeof(input_event_text));
	SDL_WindowID window_id = SDL_GetWindowID(window);
	FullDemoState state;
	full_state_init(&state);
	if (profile_scenario >= FULL_PROFILE_TEXT_IDLE && profile_scenario <= FULL_PROFILE_SCROLL)
		full_profile_seed_notes(&state);
	state.immediate_present_supported = SDL_WindowSupportsGPUPresentMode(
	                                        device, window, SDL_GPU_PRESENTMODE_IMMEDIATE)
	                                        ? 1
	                                        : 0;
	state.mailbox_present_supported = SDL_WindowSupportsGPUPresentMode(
	                                      device, window, SDL_GPU_PRESENTMODE_MAILBOX) ? 1 : 0;
	state.requested_present_mode = start_present_mode;
	state.present_mode_dirty = start_present_mode != state.present_mode;
	if (!full_apply_present_mode(device, window, &state)) goto cleanup;
	printf("Profile configuration: backend=%s scenario=%s present=%s\n", SDL_GetGPUDeviceDriver(device),
	       profile_options.scenario, demo_present_name(state.present_mode));
	if (capture_wait_ms) SDL_Delay((u32)capture_wait_ms);
	u64 previous_ticks = SDL_GetTicksNS();
	u64 previous_frame_start = previous_ticks;
	u32 observed_edits = 0u, observed_scrolls = 0u;
	int running = 1;
	int submitted_frames = 0;
	int presented_frames = 0;

	while (running)
	{
		u64 frame_start = SDL_GetTicksNS();
		DemoProfileFrame sample = {0};
		sample.frame = (u32)submitted_frames;
		sample.frame_interval_ms = (double)(frame_start - previous_frame_start) / 1000000.0;
		previous_frame_start = frame_start;
		rg_input_begin_frame(&input);
		rg_input_event_queue_reset(&input_events, SDL_GetModState());
		SDL_Event event;
		while (SDL_PollEvent(&event))
		{
			if (event.type == SDL_EVENT_QUIT || event.type == SDL_EVENT_WINDOW_CLOSE_REQUESTED)
				running = 0;
			demo_platform_process_event(&platform_state, &event);
			rg_input_process_event_ex(&input, &event, &input_events);
		}
		rg_input_sample(&input);
		if (rg_input_is_key_pressed(&input, SDL_SCANCODE_ESCAPE)) running = 0;
		if (!running) break;
		if (profile_scenario == FULL_PROFILE_RESIZE && sample.frame % 30u == 0u)
		{
			int alternate = (sample.frame / 30u) % 2u != 0u;
			if (!SDL_SetWindowSize(window, alternate ? 1024 : 1280, alternate ? 720 : 800)) goto cleanup;
			sample.actions++;
		}
		sample.actions += full_profile_input(profile_scenario, sample.frame, &state, &gui,
		                                     &input, &input_events, window_id);

		u64 ticks = SDL_GetTicksNS();
		f32 frame_time = (f32)((f64)(ticks - previous_ticks) / 1000000000.0);
		previous_ticks = ticks;
		f32 delta_time = frame_time;
		if (delta_time > 0.1f) delta_time = 0.1f;
		full_update_stats(&state, frame_time, delta_time);

		int width = 0;
		int height = 0;
		if (!SDL_GetWindowSizeInPixels(window, &width, &height)) goto cleanup;
		sample.width = (u32)width; sample.height = (u32)height;
		sample.event_ms = demo_profile_elapsed_ms(frame_start);
		u64 ui_start = SDL_GetTicksNS();
		size_t previous_note_length = strlen(state.notes);
		f32 previous_scroll = state.notes_area.panel.scroll_y;
		rg_gui_begin_frame_ex(&gui, &input, &input_events, window_id, delta_time);
		gui.style.color_accent = state.accent;
		full_build_ui(&gui, &state, width, height);
		if (profile_atlas) full_profile_images(&gui, width, height, atlas, profile_atlas,
		                                         profile_scenario == FULL_PROFILE_IMAGES_GROUPED);
		rg_gui_end_frame(&gui);
		sample.ui_ms = demo_profile_elapsed_ms(ui_start);
		sample.diagnostic_flags = (u32)gui.diagnostics.flags;
		observed_edits += previous_note_length != strlen(state.notes);
		observed_scrolls += previous_scroll != state.notes_area.panel.scroll_y;
		u64 platform_start = SDL_GetTicksNS();
		full_apply_present_mode(device, window, &state);

		const RgGuiPlatformOutput* platform = rg_gui_platform_output(&gui);
		if (!demo_platform_apply_cursor(&platform_state, platform) ||
		    !demo_platform_update_text_input(&platform_state, &input, window, platform,
		                                     width, height))
			goto cleanup;
		sample.platform_ms = demo_profile_elapsed_ms(platform_start);
		int presented = 0;
		if (!demo_render_frame_profiled(device, window, &gpu, &text_renderer, &upload_ring, &gui,
		                                &presented, &sample.main))
			goto cleanup;
		const RgGuiDrawList* completed_draw_list = rg_gui_draw_list(&gui);
		state.previous_draw_command_count = completed_draw_list ? completed_draw_list->count : 0u;
		sample.frame_work_ms = demo_profile_elapsed_ms(frame_start);
		if (!demo_profile_capture_append(&profile_capture, &sample)) goto cleanup;
		// Refresh numeric telemetry at 15-frame intervals to limit self-induced text churn.
		if (sample.frame % 15u == 0u) state.previous_profile = sample;

		submitted_frames++;
		presented_frames += presented;
		if (state.request_quit || (smoke_test && presented_frames >= frame_limit) ||
		    (!smoke_test && frame_limit > 0 && submitted_frames >= frame_limit))
			running = 0;
		else if (smoke_test && submitted_frames >= 120)
		{
			SDL_SetError("Full-demo smoke test could not present three swapchain frames in 120 attempts");
			goto cleanup;
		}
	}

	if ((profile_scenario == FULL_PROFILE_EDIT && observed_edits < 2u) ||
	    (profile_scenario == FULL_PROFILE_SCROLL && observed_scrolls < 2u))
	{
		SDL_SetError("Scripted profile did not exercise the requested text interaction");
		goto cleanup;
	}
	printf("Profile interactions: text_edits=%u scroll_changes=%u\n", observed_edits, observed_scrolls);
	if (!demo_profile_capture_write(&profile_capture)) goto cleanup;
	if (smoke_test && presented_frames < frame_limit)
	{
		SDL_SetError("Full-demo smoke test ended before presenting three frames");
		goto cleanup;
	}
	printf("rg_gui full showcase completed %d submitted frame%s (%d presented)\n",
	       submitted_frames, submitted_frames == 1 ? "" : "s", presented_frames);
	result = 0;

cleanup:
	if (result != 0) fprintf(stderr, "rg_gui full showcase failed: %s\n", SDL_GetError());
	if (device) rg_gpu_wait_idle(device);
	rg_gpu_upload_ring_destroy(&upload_ring);
	rg_gui_gpu_destroy(&gpu);
	free(text_memory);
	free(gui_memory);
	if (atlas) SDL_ReleaseGPUTexture(device, atlas);
	if (profile_atlas) SDL_ReleaseGPUTexture(device, profile_atlas);
	demo_profile_capture_destroy(&profile_capture);
	demo_font_destroy(&demo_font);
	demo_platform_destroy(&platform_state, &input);
	if (window_claimed) SDL_ReleaseWindowFromGPUDevice(device, window);
	if (device) rg_gpu_device_destroy(device);
	if (window) SDL_DestroyWindow(window);
	SDL_Quit();
	return result;
}
