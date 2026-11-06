// End-to-end Direct3D 11 checks for rg_gui_d3d11 on WARP (the software
// rasterizer, so no GPU is needed): the material/clip/alpha/overlay scene of
// test_gui_gpu_device without image materials, the shader clip rule against the
// scissor rule, image texture handles (registered, stale, after a device reset),
// an R8 coverage atlas with snapped text origins, and no live device objects
// after shutdown.

#define RG_SPRINTF_NO_ASM 1
#define RGINLINE static inline
#define WIN32_LEAN_AND_MEAN
#define NOMINMAX
#include <windows.h>
#include <d3d11.h>
#include "../src/rg_gui_d3d11.h"

#include <math.h>
#include <stdio.h>
#include <stdlib.h>

#pragma comment(lib, "d3d11.lib")

#define SIZE 64u

static int g_failures;
static u32 g_checks;

#define CHECK(condition)                                                          \
	do                                                                            \
	{                                                                             \
		g_checks++;                                                               \
		if (!(condition))                                                         \
		{                                                                         \
			fprintf(stderr, "D3D11 check failed at line %d: %s\n", __LINE__, #condition); \
			g_failures++;                                                         \
		}                                                                         \
	} while (0)

typedef struct Target
{
	ID3D11Device* device;
	ID3D11DeviceContext* context;
	ID3D11Texture2D* color;
	ID3D11RenderTargetView* view;
	ID3D11Texture2D* staging;
	u8 pixels[SIZE * SIZE * 4u];
} Target;

static void font_init(RgTextFont* font, RgTextGlyph glyphs[2])
{
	memset(font, 0, sizeof(*font));
	memset(glyphs, 0, sizeof(RgTextGlyph) * 2u);
	font->metrics.atlas_width = 1u;
	font->metrics.atlas_height = 1u;
	font->metrics.line_height = 10;
	font->glyphs = glyphs;
	font->glyph_count = 2u;
	font->glyph_capacity = 2u;
	font->fallback_codepoint = '?';
	glyphs[0].codepoint = '?';
	glyphs[0].w = 1;
	glyphs[0].h = 1;
	glyphs[0].x_advance = 1;
	glyphs[1] = glyphs[0];
	glyphs[1].codepoint = 'A';
}

static int target_init(Target* t)
{
	D3D11_TEXTURE2D_DESC td;
	memset(t, 0, sizeof(*t));
	if (FAILED(D3D11CreateDevice(NULL, D3D_DRIVER_TYPE_WARP, NULL, 0u, NULL, 0u, D3D11_SDK_VERSION, &t->device, NULL,
	                             &t->context)))
		return 0;
	memset(&td, 0, sizeof(td));
	td.Width = td.Height = SIZE;
	td.MipLevels = td.ArraySize = 1u;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1u;
	td.Usage = D3D11_USAGE_DEFAULT;
	td.BindFlags = D3D11_BIND_RENDER_TARGET;
	if (FAILED(t->device->lpVtbl->CreateTexture2D(t->device, &td, NULL, &t->color)) ||
	    FAILED(t->device->lpVtbl->CreateRenderTargetView(t->device, (ID3D11Resource*)t->color, NULL, &t->view)))
		return 0;
	td.Usage = D3D11_USAGE_STAGING;
	td.BindFlags = 0u;
	td.CPUAccessFlags = D3D11_CPU_ACCESS_READ;
	return SUCCEEDED(t->device->lpVtbl->CreateTexture2D(t->device, &td, NULL, &t->staging));
}

/* A 1x1 RGBA texture and its view, for image commands. */
static ID3D11ShaderResourceView* solid_texture(Target* t, u8 r, u8 g, u8 b)
{
	D3D11_TEXTURE2D_DESC td;
	D3D11_SUBRESOURCE_DATA init;
	ID3D11Texture2D* texture = NULL;
	ID3D11ShaderResourceView* view = NULL;
	const u8 texel[4] = {r, g, b, 255u};
	memset(&td, 0, sizeof(td));
	td.Width = td.Height = td.MipLevels = td.ArraySize = 1u;
	td.Format = DXGI_FORMAT_R8G8B8A8_UNORM;
	td.SampleDesc.Count = 1u;
	td.Usage = D3D11_USAGE_IMMUTABLE;
	td.BindFlags = D3D11_BIND_SHADER_RESOURCE;
	memset(&init, 0, sizeof(init));
	init.pSysMem = texel;
	init.SysMemPitch = 4u;
	if (SUCCEEDED(t->device->lpVtbl->CreateTexture2D(t->device, &td, &init, &texture)))
		t->device->lpVtbl->CreateShaderResourceView(t->device, (ID3D11Resource*)texture, NULL, &view);
	RG_GUI_D3D11_RELEASE(texture); /* the view keeps it alive */
	return view;
}

/* Clear to opaque black, render the list, and read the target back. */
static int render(Target* t, RgGuiD3d11* r, RgGuiRenderer* text, const RgGuiDrawList* list, u32 overlay_start,
                  f32 offset_x, f32 offset_y)
{
	const FLOAT black[4] = {0.0f, 0.0f, 0.0f, 1.0f};
	D3D11_VIEWPORT viewport;
	D3D11_MAPPED_SUBRESOURCE mapped;
	RgGuiD3d11DrawDesc draw;
	memset(&viewport, 0, sizeof(viewport));
	viewport.Width = viewport.Height = (FLOAT)SIZE;
	viewport.MaxDepth = 1.0f;
	t->context->lpVtbl->OMSetRenderTargets(t->context, 1u, &t->view, NULL);
	t->context->lpVtbl->RSSetViewports(t->context, 1u, &viewport);
	t->context->lpVtbl->ClearRenderTargetView(t->context, t->view, black);
	rg_gui_renderer_begin_frame(text);
	if (!rg_gui_d3d11_prepare(r, text, list, overlay_start)) return 0;
	memset(&draw, 0, sizeof(draw));
	draw.output_width = draw.output_height = SIZE;
	draw.offset_x = offset_x;
	draw.offset_y = offset_y;
	if (!rg_gui_d3d11_draw(r, text, &draw)) return 0;
	t->context->lpVtbl->CopyResource(t->context, (ID3D11Resource*)t->staging, (ID3D11Resource*)t->color);
	if (FAILED(t->context->lpVtbl->Map(t->context, (ID3D11Resource*)t->staging, 0u, D3D11_MAP_READ, 0u, &mapped)))
		return 0;
	for (u32 y = 0u; y < SIZE; y++)
		memcpy(t->pixels + y * SIZE * 4u, (const u8*)mapped.pData + (size_t)y * mapped.RowPitch, SIZE * 4u);
	t->context->lpVtbl->Unmap(t->context, (ID3D11Resource*)t->staging, 0u);
	return 1;
}

static int pixel_is(const Target* t, u32 x, u32 y, int r, int g, int b)
{
	const u8* p = t->pixels + (y * SIZE + x) * 4u;
	const int ok = abs((int)p[0] - r) <= 1 && abs((int)p[1] - g) <= 1 && abs((int)p[2] - b) <= 1 && p[3] == 255u;
	if (!ok) fprintf(stderr, "  pixel %u,%u: RGBA %u,%u,%u,%u, expected %d,%d,%d\n", x, y, p[0], p[1], p[2], p[3], r, g, b);
	return ok;
}

static RgGuiDrawCmd rect_cmd(f32 x, f32 y, f32 w, f32 h, rg_vec4 color)
{
	RgGuiDrawCmd cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.type = RG_GUI_CMD_RECT;
	cmd.data.rect.rect = rg_gui_make_rect(x, y, w, h);
	cmd.color = rg_gui_pack_color(color);
	return cmd;
}

static RgGuiDrawCmd image_cmd(f32 x, f32 y, f32 w, f32 h, RgGuiTexture texture)
{
	RgGuiDrawCmd cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.type = RG_GUI_CMD_IMAGE;
	cmd.data.image.rect = rg_gui_make_rect(x, y, w, h);
	cmd.data.image.uv = rg_gui_make_rect(0.0f, 0.0f, 1.0f, 1.0f);
	cmd.data.image.texture = texture;
	cmd.color = rg_gui_pack_color(rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f));
	return cmd;
}

static RgGuiDrawCmd text_cmd(const char* text, f32 x, f32 y, f32 scale)
{
	RgGuiDrawCmd cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.type = RG_GUI_CMD_TEXT;
	cmd.data.text.text = text;
	cmd.data.text.pos = rg_vec2(x, y);
	cmd.data.text.scale = scale;
	cmd.color = rg_gui_pack_color(rg_gui_color(1.0f, 1.0f, 1.0f, 1.0f));
	return cmd;
}

static RgGuiDrawCmd clip_cmd(RgGuiDrawCmdType type, f32 x, f32 y, f32 w, f32 h)
{
	RgGuiDrawCmd cmd;
	memset(&cmd, 0, sizeof(cmd));
	cmd.type = type;
	cmd.data.clip.rect = rg_gui_make_rect(x, y, w, h);
	return cmd;
}

/* The test_gui_gpu_device scene without image materials, plus a registered
 * image and a stale handle. Probes as there. */
static void test_scene(Target* t, RgGuiD3d11* r, RgGuiRenderer* text, RgGuiTexture image, RgGuiTexture stale,
                       int image_live)
{
	const rg_vec4 white = rg_gui_color(1, 1, 1, 1);
	RgGuiDrawCmd cmds[16];
	u32 n = 0u, overlay;
	RgGuiDrawCmd tri;
	cmds[n++] = rect_cmd(0, 0, 8, 8, white);
	cmds[n++] = image_cmd(8, 0, 8, 8, rg_gui_d3d11_atlas_texture(r));
	cmds[n++] = image_cmd(16, 0, 8, 8, stale);
	cmds[n++] = image_cmd(24, 0, 8, 8, image);
	cmds[n++] = image_cmd(48, 0, 8, 8, rg_gui_d3d11_atlas_texture(r));
	cmds[n++] = rect_cmd(56, 0, 8, 8, white);
	cmds[n++] = text_cmd("A", 56, 12, 1.0f);
	/* The body clip stays active at the overlay boundary; the overlay resets it. */
	cmds[n++] = clip_cmd(RG_GUI_CMD_CLIP_PUSH, 0, 20, 8, 8);
	cmds[n++] = rect_cmd(0, 16, 16, 16, rg_gui_color(1, 0, 0, 1));
	overlay = n;
	cmds[n++] = rect_cmd(16, 20, 8, 8, rg_gui_color(0, 1, 0, 1));
	cmds[n++] = rect_cmd(32, 20, 8, 8, rg_gui_color(0, 0, 1, 1));
	cmds[n++] = rect_cmd(32, 20, 8, 8, rg_gui_color(1, 0, 0, 0.5f));
	memset(&tri, 0, sizeof(tri));
	tri.type = RG_GUI_CMD_TRIANGLE;
	tri.data.triangle.a = rg_vec2(48, 20);
	tri.data.triangle.b = rg_vec2(56, 20);
	tri.data.triangle.c = rg_vec2(48, 28);
	tri.color = rg_gui_pack_color(rg_gui_color(1, 1, 0, 1));
	cmds[n++] = tri;
	{
		const RgGuiDrawList list = {cmds, n, n};
		CHECK(render(t, r, text, &list, overlay, 0.0f, 0.0f));
	}
	CHECK(pixel_is(t, 4, 4, 255, 255, 255));
	CHECK(pixel_is(t, 12, 4, 64, 128, 192));                 /* atlas as an image */
	CHECK(pixel_is(t, 20, 4, 0, 0, 0));                      /* stale handle: skipped */
	CHECK(image_live ? pixel_is(t, 28, 4, 10, 200, 30) : pixel_is(t, 28, 4, 0, 0, 0));
	CHECK(pixel_is(t, 36, 4, 0, 0, 0));
	CHECK(pixel_is(t, 52, 4, 64, 128, 192));
	CHECK(pixel_is(t, 60, 4, 255, 255, 255));
	CHECK(pixel_is(t, 56, 12, 64, 128, 192));                /* glyph from the atlas */
	CHECK(pixel_is(t, 4, 18, 0, 0, 0));                      /* clipped away */
	CHECK(pixel_is(t, 4, 24, 255, 0, 0));
	CHECK(pixel_is(t, 12, 24, 0, 0, 0));
	CHECK(pixel_is(t, 20, 24, 0, 255, 0));                   /* overlay, unclipped */
	CHECK(pixel_is(t, 36, 24, 128, 0, 127));                 /* straight alpha */
	CHECK(pixel_is(t, 49, 21, 255, 255, 0));
	CHECK(pixel_is(t, 54, 26, 0, 0, 0));
	CHECK(r->stats.skipped_batches == (image_live ? 1u : 2u));
	CHECK(r->stats.text_instances == 1u && r->stats.draw_calls >= 1u);
}

/* rg_gui_gpu_scissor's rule: floor the near edges and ceil the far edges after
 * the draw offset, clamped to the output. */
static void scissor(RgGuiRect clip, int enabled, f32 ox, f32 oy, int out[4])
{
	int x0 = 0, y0 = 0, x1 = (int)SIZE, y1 = (int)SIZE;
	if (enabled)
	{
		const int cx0 = (int)floorf(clip.x + ox), cy0 = (int)floorf(clip.y + oy);
		const int cx1 = (int)ceilf(clip.x + clip.w + ox), cy1 = (int)ceilf(clip.y + clip.h + oy);
		if (cx0 > x0) x0 = cx0;
		if (cy0 > y0) y0 = cy0;
		if (cx1 < x1) x1 = cx1;
		if (cy1 < y1) y1 = cy1;
		if (x1 < x0) x1 = x0;
		if (y1 < y0) y1 = y0;
	}
	out[0] = x0;
	out[1] = y0;
	out[2] = x1;
	out[3] = y1;
}

/* test_gui_gpu_device's clip-distance cases: each primitive drawn clipped must
 * equal its unclipped pixels inside the scissor rule's rectangle and be
 * background outside it, at whole and fractional draw offsets. */
static void test_clip_distances(Target* t, RgGuiD3d11* r, RgGuiRenderer* text)
{
	typedef struct ClipCase
	{
		u32 type;
		RgGuiRect rect;
		rg_vec4 color;
		u32 clips;
		RgGuiRect clip[2];
	} ClipCase;
	const rg_vec4 red = rg_gui_color(1, 0, 0, 1), green = rg_gui_color(0, 1, 0, 1);
	const rg_vec4 blue = rg_gui_color(0, 0, 1, 1), white = rg_gui_color(1, 1, 1, 1);
	const rg_vec4 yellow = rg_gui_color(1, 1, 0, 1), cyan = rg_gui_color(0, 1, 1, 1);
	const rg_vec4 magenta = rg_gui_color(1, 0, 1, 1);
	const RgGuiRect shared = {22.5f, 40.5f, 20.0f, 12.25f};
	const ClipCase cases[] = {
	    {RG_GUI_CMD_RECT, {2, 2, 14, 12}, red, 1u, {{3.5f, 3.25f, 9.75f, 7.5f}}},
	    {RG_GUI_CMD_IMAGE, {20, 2, 14, 12}, green, 1u, {{21.25f, 2.75f, 10.0f, 8.5f}}},
	    {RG_GUI_CMD_TRIANGLE, {38, 1, 24, 16}, blue, 1u, {{39.5f, 2.5f, 12.25f, 6.75f}}},
	    {RG_GUI_CMD_TEXT, {0, 19, 28, 16}, white, 1u, {{4.5f, 23.5f, 15.25f, 2.75f}}},
	    {RG_GUI_CMD_RECT, {32, 22, 28, 16}, yellow, 2u, {{30, 22, 30, 20}, {35.5f, 25.25f, 40, 7.5f}}},
	    {RG_GUI_CMD_RECT, {21, 42, 8, 12}, cyan, 1u, {shared}},
	    {RG_GUI_CMD_RECT, {2, 42, 12, 8}, magenta, 0u, {{0, 0, 0, 0}}},
	    {RG_GUI_CMD_RECT, {33, 42, 12, 14}, white, 1u, {shared}},
	    {RG_GUI_CMD_RECT, {48, 42, 6, 8}, white, 1u, {{50, 44, 0, 10}}},
	    {RG_GUI_CMD_RECT, {56, 50, 14, 20}, yellow, 1u, {{56.5f, 50.5f, 20, 20}}},
	    {RG_GUI_CMD_RECT, {0, 54, 10, 12}, cyan, 1u, {{-3.25f, 54.5f, 10.5f, 20}}}};
	enum { CASES = sizeof(cases) / sizeof(cases[0]) };
	static const f32 offsets[][2] = {{0.0f, 0.0f}, {0.5f, 0.25f}, {-0.75f, 1.5f}, {0.3f, -0.6f}};
	RgGuiDrawCmd clipped[CASES * 5u], plain[CASES];
	RgGuiRect effective[CASES];
	static u8 coverage[SIZE * SIZE * 4u];
	u32 clipped_count = 0u, matched = 0u;
	for (u32 i = 0u; i < CASES; i++)
	{
		const ClipCase* c = &cases[i];
		RgGuiDrawCmd cmd;
		memset(&cmd, 0, sizeof(cmd));
		if (c->type == RG_GUI_CMD_RECT) cmd = rect_cmd(c->rect.x, c->rect.y, c->rect.w, c->rect.h, c->color);
		else if (c->type == RG_GUI_CMD_IMAGE)
		{
			cmd = image_cmd(c->rect.x, c->rect.y, c->rect.w, c->rect.h, rg_gui_d3d11_atlas_texture(r));
			cmd.color = rg_gui_pack_color(c->color);
		}
		else if (c->type == RG_GUI_CMD_TRIANGLE)
		{
			cmd.type = RG_GUI_CMD_TRIANGLE;
			cmd.data.triangle.a = rg_vec2(c->rect.x, c->rect.y);
			cmd.data.triangle.b = rg_vec2(c->rect.x + c->rect.w, c->rect.y);
			cmd.data.triangle.c = rg_vec2(c->rect.x, c->rect.y + c->rect.h);
			cmd.color = rg_gui_pack_color(c->color);
		}
		else cmd = text_cmd("AAAAAAAA", c->rect.x + 2.0f, c->rect.y + 4.0f, 3.0f);
		plain[i] = cmd;
		for (u32 k = 0u; k < c->clips; k++)
			clipped[clipped_count++] = clip_cmd(RG_GUI_CMD_CLIP_PUSH, c->clip[k].x, c->clip[k].y, c->clip[k].w, c->clip[k].h);
		effective[i] = c->clip[0];
		if (c->clips == 2u) effective[i] = rg_gui_renderer_base_clip_intersect(c->clip[0], c->clip[1]);
		clipped[clipped_count++] = cmd;
		for (u32 k = 0u; k < c->clips; k++) clipped[clipped_count++] = clip_cmd(RG_GUI_CMD_CLIP_POP, 0, 0, 0, 0);
	}
	for (u32 d = 0u; d < sizeof(offsets) / sizeof(offsets[0]); d++)
	{
		const f32 ox = offsets[d][0], oy = offsets[d][1];
		const RgGuiDrawList plain_list = {plain, CASES, CASES};
		const RgGuiDrawList clipped_list = {clipped, clipped_count, clipped_count};
		u32 visible[CASES] = {0};
		int mismatch = 0;
		CHECK(render(t, r, text, &plain_list, CASES, ox, oy));
		memcpy(coverage, t->pixels, sizeof(coverage));
		CHECK(render(t, r, text, &clipped_list, clipped_count, ox, oy));
		/* Nine distinct clips plus the unclipped entry, in one draw call. */
		CHECK(r->stats.draw_calls == 1u);
		for (u32 y = 0u; y < SIZE && !mismatch; y++)
		for (u32 x = 0u; x < SIZE && !mismatch; x++)
		{
			const u8* covered = coverage + (y * SIZE + x) * 4u;
			const u8* got = t->pixels + (y * SIZE + x) * 4u;
			const f32 cx = (f32)x + 0.5f, cy = (f32)y + 0.5f;
			int owner = -1, inside = 0;
			for (u32 i = 0u; i < CASES; i++)
			{
				const RgGuiRect rc = cases[i].rect;
				if (cx >= rc.x - 1.0f + ox && cx < rc.x + rc.w + 1.0f + ox && cy >= rc.y - 1.0f + oy &&
				    cy < rc.y + rc.h + 1.0f + oy)
					owner = (int)i;
			}
			const int background = covered[0] == 0u && covered[1] == 0u && covered[2] == 0u;
			if (owner >= 0)
			{
				int s[4];
				scissor(effective[owner], cases[owner].clips != 0u, ox, oy, s);
				inside = (int)x >= s[0] && (int)x < s[2] && (int)y >= s[1] && (int)y < s[3];
			}
			else if (!background)
			{
				fprintf(stderr, "  coverage escaped its region at %u,%u (offset %u)\n", x, y, d);
				mismatch = 1;
			}
			const u8 expected[4] = {inside ? covered[0] : 0u, inside ? covered[1] : 0u, inside ? covered[2] : 0u, 255u};
			if (memcmp(got, expected, 4u))
			{
				fprintf(stderr, "  clip pixel %u,%u (offset %u): got %u,%u,%u,%u, expected %u,%u,%u,%u\n", x, y, d,
				        got[0], got[1], got[2], got[3], expected[0], expected[1], expected[2], expected[3]);
				mismatch = 1;
			}
			if (owner >= 0 && inside && !background) visible[owner]++;
			matched++;
		}
		CHECK(!mismatch);
		/* Every primitive shows pixels; the zero-width clip (case 8) shows none
		 * at a whole-pixel offset. */
		for (u32 i = 0u; i < CASES; i++) CHECK(i == 8u ? (d != 0u || visible[i] == 0u) : visible[i] != 0u);
	}
	printf("Shader clip distances: %u pixels matched the scissor rule across 4 draw offsets\n", matched);
}

/* An R8 coverage atlas: glyphs draw white at the coverage, images keep RGBA,
 * and text origins snap to whole pixels (10.4, 20.5 draws as 10, 21). */
static void test_coverage_atlas(Target* t, RgGuiRenderer* text, RgArena* arena)
{
	RgGuiD3d11 r;
	RgGuiD3d11Desc desc;
	ID3D11ShaderResourceView* view = solid_texture(t, 10u, 200u, 30u);
	const u8 coverage = 128u;
	static u8 snapped[SIZE * SIZE * 4u];
	RgGuiTexture image;
	memset(&desc, 0, sizeof(desc));
	desc.device = t->device;
	desc.context = t->context;
	desc.arena = arena;
	desc.atlas_width = desc.atlas_height = 1u;
	desc.atlas_coverage = 1u;
	desc.snap_text_origins = 1u;
	desc.max_cached_quads = 64u;
	desc.max_runs = 16u;
	desc.max_text_instances = 64u;
	desc.max_geometry_vertices = 96u;
	desc.max_items = 16u;
	CHECK(view && rg_gui_d3d11_init(&r, &desc));
	rg_gui_d3d11_update_atlas(&r, &coverage, 1u, 0u, 0u, 1u, 1u);
	image = rg_gui_d3d11_texture_register(&r, view);
	CHECK(image != 0u);
	{
		RgGuiDrawCmd cmds[2];
		RgGuiDrawList list = {cmds, 2u, 2u};
		cmds[0] = text_cmd("AAAA", 10.0f, 21.0f, 2.0f);
		cmds[1] = image_cmd(40, 40, 8, 8, image);
		CHECK(render(t, &r, text, &list, 2u, 0.0f, 0.0f));
		memcpy(snapped, t->pixels, sizeof(snapped));
		cmds[0] = text_cmd("AAAA", 10.4f, 20.5f, 2.0f);
		CHECK(render(t, &r, text, &list, 2u, 0.0f, 0.0f));
		CHECK(memcmp(snapped, t->pixels, sizeof(snapped)) == 0);
	}
	{
		u32 lit = 0u;
		for (u32 y = 0u; y < 32u; y++)
		for (u32 x = 0u; x < 32u; x++)
		{
			const u8* p = t->pixels + (y * SIZE + x) * 4u;
			if (p[0] || p[1] || p[2])
			{
				lit++;
				CHECK(pixel_is(t, x, y, 128, 128, 128));
			}
		}
		CHECK(lit == 4u * 4u); /* four 2x2 glyphs */
	}
	CHECK(pixel_is(t, 44, 44, 10, 200, 30));
	rg_gui_d3d11_shutdown(&r);
	RG_GUI_D3D11_RELEASE(view);
}

int main(void)
{
	static Target target;
	Target* t = &target;
	RgTextFont font;
	RgTextGlyph glyphs[2];
	RgGuiRenderer text;
	RgGuiD3d11 renderer;
	RgGuiD3d11Desc desc;
	ID3D11ShaderResourceView* image_view = NULL;
	RgGuiTexture image, stale;
	void* text_memory = NULL;
	void* renderer_memory = NULL;
	static const u8 atlas[4] = {64u, 128u, 192u, 255u};
	ULONG remaining = 1u;

	if (!target_init(t))
	{
		fprintf(stderr, "WARP device unavailable\n");
		return 1;
	}
	font_init(&font, glyphs);
	{
		RgGuiRendererLimits limits = rg_gui_renderer_limits_default();
		RgGuiRendererInitDesc text_desc;
		size_t size;
		RgArena arena;
		limits.max_cached_runs = 16u;
		limits.hash_slot_count = 32u;
		limits.text_capacity = KB(1);
		limits.max_cached_quads = 64u;
		limits.max_frame_instances = 64u;
		limits.max_batches = 16u;
		size = rg_gui_renderer_memory_required(&limits, 32u);
		text_memory = malloc(size);
		CHECK(text_memory != NULL);
		if (!text_memory) return 1;
		arena = (RgArena){(char*)text_memory, size, 0u, size};
		memset(&text_desc, 0, sizeof(text_desc));
		text_desc.font = &font;
		text_desc.limits = limits;
		text_desc.page_quads = 32u;
		CHECK(rg_gui_renderer_init(&text, &arena, &text_desc));
	}
	renderer_memory = malloc(MB(1));
	if (!renderer_memory) return 1;
	{
		RgArena arena = {(char*)renderer_memory, MB(1), 0u, MB(1)};
		memset(&desc, 0, sizeof(desc));
		desc.device = t->device;
		desc.context = t->context;
		desc.arena = &arena;
		desc.atlas_width = desc.atlas_height = 1u;
		desc.nearest_filter = 1u;
		desc.max_cached_quads = 64u;
		desc.max_runs = 16u;
		desc.max_text_instances = 64u;
		desc.max_geometry_vertices = 96u;
		desc.max_items = 16u;
		CHECK(rg_gui_d3d11_init(&renderer, &desc));
		rg_gui_d3d11_update_atlas(&renderer, atlas, 4u, 0u, 0u, 1u, 1u);

		/* A registered image draws; an unregistered handle is stale. */
		image_view = solid_texture(t, 10u, 200u, 30u);
		stale = rg_gui_d3d11_texture_register(&renderer, image_view);
		rg_gui_d3d11_texture_unregister(&renderer, stale);
		image = rg_gui_d3d11_texture_register(&renderer, image_view);
		CHECK(image != 0u && stale != 0u && image != stale && (image & 0xffffffffu) == (stale & 0xffffffffu));
		test_scene(t, &renderer, &text, image, stale, 1);
		test_clip_distances(t, &renderer, &text);

		/* Device reset: the same frames draw again (glyph cache in full, atlas
		 * uploaded again); handles registered before it are stale. */
		CHECK(rg_gui_d3d11_reset_device(&renderer, t->device, t->context));
		rg_gui_d3d11_update_atlas(&renderer, atlas, 4u, 0u, 0u, 1u, 1u);
		test_scene(t, &renderer, &text, image, stale, 0);
		CHECK(renderer.stats.full_cache_upload == 1u);

		test_coverage_atlas(t, &text, &arena);
		rg_gui_d3d11_shutdown(&renderer);
	}
	RG_GUI_D3D11_RELEASE(image_view);
	RG_GUI_D3D11_RELEASE(t->staging);
	RG_GUI_D3D11_RELEASE(t->view);
	RG_GUI_D3D11_RELEASE(t->color);
	t->context->lpVtbl->ClearState(t->context);
	t->context->lpVtbl->Flush(t->context);
	RG_GUI_D3D11_RELEASE(t->context);
	remaining = t->device->lpVtbl->Release(t->device);
	CHECK(remaining == 0u); /* every renderer object was released */
	free(renderer_memory);
	free(text_memory);
	printf("rg_gui_d3d11 WARP checks: %u checks, %d failed\n", g_checks, g_failures);
	return g_failures ? 1 : 0;
}
