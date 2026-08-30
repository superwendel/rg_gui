// Renderer-neutral validation for the checked-in demo font assets.

#define RGINLINE static inline
#include "rg_text.h"

#include <stdio.h>
#include <stdlib.h>
#include <string.h>

#define TEST_FONT_STEM "examples/assets/inter_medium_16"
#define TEST_GLYPH_CAPACITY 256u
#define TEST_KERNING_CAPACITY 4096u

static int read_file(const char* path, void** out_data, size_t* out_size)
{
	FILE* file = fopen(path, "rb");
	if (!file)
	{
		fprintf(stderr, "Could not open checked-in asset: %s\n", path);
		return 0;
	}
	if (fseek(file, 0, SEEK_END) != 0)
	{
		fclose(file);
		fprintf(stderr, "Could not seek checked-in asset: %s\n", path);
		return 0;
	}
	long length = ftell(file);
	if (length <= 0 || fseek(file, 0, SEEK_SET) != 0)
	{
		fclose(file);
		fprintf(stderr, "Checked-in asset is empty or unreadable: %s\n", path);
		return 0;
	}

	void* data = malloc((size_t)length);
	if (!data)
	{
		fclose(file);
		fprintf(stderr, "Out of memory reading checked-in asset: %s\n", path);
		return 0;
	}
	size_t read_size = fread(data, 1u, (size_t)length, file);
	int close_result = fclose(file);
	if (read_size != (size_t)length || close_result != 0)
	{
		free(data);
		fprintf(stderr, "Could not read complete checked-in asset: %s\n", path);
		return 0;
	}

	*out_data = data;
	*out_size = read_size;
	return 1;
}

static int font_has_codepoint(const RgTextFont* font, u32 codepoint)
{
	for (u32 i = 0u; i < font->glyph_count; i++)
	{
		if (font->glyphs[i].codepoint == codepoint) return 1;
	}
	return 0;
}

static int glyph_has_coverage(const RgTextGlyph* glyph, const u8* pixels, size_t atlas_width)
{
	if (glyph->w == 0 || glyph->h == 0) return 1;
	for (u32 y = 0u; y < (u32)glyph->h; y++)
	{
		for (u32 x = 0u; x < (u32)glyph->w; x++)
		{
			size_t pixel = (((size_t)(u32)glyph->y + (size_t)y) * atlas_width +
			                (size_t)(u32)glyph->x + (size_t)x) *
			               4u;
			if (pixels[pixel + 3u] != 0u) return 1;
		}
	}
	return 0;
}

int main(void)
{
	int result = 1;
	void* metrics_data = NULL;
	void* atlas_data = NULL;
	size_t metrics_size = 0u;
	size_t atlas_size = 0u;
	RgTextGlyph glyphs[TEST_GLYPH_CAPACITY];
	RgTextKerning kernings[TEST_KERNING_CAPACITY];
	RgTextFont font;
	memset(&font, 0, sizeof(font));

	if (!read_file(TEST_FONT_STEM ".font", &metrics_data, &metrics_size)) return 1;

	RgTextFontLoadDesc desc = {0};
	desc.data = metrics_data;
	desc.data_size = metrics_size;
	desc.glyphs = glyphs;
	desc.glyph_capacity = TEST_GLYPH_CAPACITY;
	desc.kernings = kernings;
	desc.kerning_capacity = TEST_KERNING_CAPACITY;
	if (!rg_text_font_load_rgfont(&font, &desc))
	{
		fprintf(stderr, "The checked-in RGFONT metrics are malformed.\n");
		result = 0;
		goto cleanup;
	}

	if (font.glyph_count != 95u)
	{
		fprintf(stderr, "Expected the complete 32-126 ASCII glyph range; found %u glyphs.\n",
		        (unsigned)font.glyph_count);
		result = 0;
		goto cleanup;
	}
	for (u32 codepoint = 32u; codepoint <= 126u; codepoint++)
	{
		if (!font_has_codepoint(&font, codepoint))
		{
			fprintf(stderr, "The checked-in font is missing ASCII codepoint %u.\n",
			        (unsigned)codepoint);
			result = 0;
			goto cleanup;
		}
	}
	if (!font_has_codepoint(&font, font.fallback_codepoint))
	{
		fprintf(stderr, "The checked-in font's fallback codepoint has no glyph.\n");
		result = 0;
		goto cleanup;
	}
	if (font.metrics.ascent <= 0 || font.metrics.descent < 0 ||
	    font.metrics.ascent + font.metrics.descent > font.metrics.line_height)
	{
		fprintf(stderr, "The checked-in font has inconsistent vertical metrics.\n");
		result = 0;
		goto cleanup;
	}

	for (u32 i = 0u; i < font.kerning_count; i++)
	{
		if (!font_has_codepoint(&font, font.kernings[i].left) ||
		    !font_has_codepoint(&font, font.kernings[i].right))
		{
			fprintf(stderr, "Kerning entry %u references a glyph outside the baked range.\n",
			        (unsigned)i);
			result = 0;
			goto cleanup;
		}
	}

	if (!read_file(TEST_FONT_STEM ".rgba", &atlas_data, &atlas_size))
	{
		result = 0;
		goto cleanup;
	}
	u64 width = (u64)font.metrics.atlas_width;
	u64 height = (u64)font.metrics.atlas_height;
	if (width == 0u || height == 0u || width > UINT64_MAX / height ||
	    width * height > UINT64_MAX / 4u || width * height * 4u != (u64)atlas_size)
	{
		fprintf(stderr,
		        "RGBA atlas byte count does not match the RGFONT dimensions (%ux%u).\n",
		        (unsigned)font.metrics.atlas_width, (unsigned)font.metrics.atlas_height);
		result = 0;
		goto cleanup;
	}

	const u8* pixels = (const u8*)atlas_data;
	int saw_coverage = 0;
	int saw_transparency = 0;
	int saw_foreground = 0;
	int saw_shadow = 0;
	for (size_t pixel = 0u; pixel < atlas_size / 4u; pixel++)
	{
		u8 red = pixels[pixel * 4u];
		u8 green = pixels[pixel * 4u + 1u];
		u8 blue = pixels[pixel * 4u + 2u];
		u8 alpha = pixels[pixel * 4u + 3u];
		int transparent = alpha == 0u && red == 0u && green == 0u && blue == 0u;
		int foreground = alpha != 0u && red == 255u && green == 255u && blue == 255u;
		int shadow = alpha != 0u && red == 5u && green == 5u && blue == 8u;
		if (!transparent && !foreground && !shadow)
		{
			fprintf(stderr, "RGBA atlas contains an unsupported transparent, foreground, or shadow pixel.\n");
			result = 0;
			goto cleanup;
		}
		if (alpha != 0u) saw_coverage = 1;
		if (alpha != 255u) saw_transparency = 1;
		if (foreground) saw_foreground = 1;
		if (shadow) saw_shadow = 1;
	}
	if (!saw_coverage || !saw_transparency || !saw_foreground || !saw_shadow)
	{
		fprintf(stderr, "RGBA atlas does not contain the expected foreground, shadow, and antialiased coverage.\n");
		result = 0;
		goto cleanup;
	}

	for (u32 i = 0u; i < font.glyph_count; i++)
	{
		const RgTextGlyph* glyph = &font.glyphs[i];
		if (glyph->x < 0 || glyph->y < 0 || glyph->w < 0 || glyph->h < 0 ||
		    (u64)(u32)glyph->x + (u64)(u32)glyph->w > width ||
		    (u64)(u32)glyph->y + (u64)(u32)glyph->h > height)
		{
			fprintf(stderr, "Glyph %u has an out-of-bounds atlas rectangle.\n",
			        (unsigned)glyph->codepoint);
			result = 0;
			goto cleanup;
		}
		if (!glyph_has_coverage(glyph, pixels, (size_t)font.metrics.atlas_width))
		{
			fprintf(stderr, "Glyph %u has a non-empty rectangle but no alpha coverage.\n",
			        (unsigned)glyph->codepoint);
			result = 0;
			goto cleanup;
		}
	}

	printf("Validated demo font assets: %u glyphs, %u kerning pairs, %ux%u RGBA atlas.\n",
	       (unsigned)font.glyph_count, (unsigned)font.kerning_count,
	       (unsigned)font.metrics.atlas_width, (unsigned)font.metrics.atlas_height);

cleanup:
	free(atlas_data);
	free(metrics_data);
	return result ? 0 : 1;
}
