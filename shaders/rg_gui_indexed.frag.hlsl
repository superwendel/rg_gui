// See rg_gui_indexed.vert.hlsl: Direct3D 11 builds define RG_D3D11 (no
// register spaces). RG_GUI_ATLAS_COVERAGE selects a glyph atlas that holds
// coverage in its red channel (an R8 texture) instead of RGBA texels.
#ifdef RG_D3D11
#define RG_SPACE(n)
#else
#define RG_SPACE(n) , space##n
#endif
Texture2D<float4> image_texture : register(t0 RG_SPACE(2));
SamplerState image_sampler : register(s0 RG_SPACE(2));

struct FragmentInput
{
	float4 position : SV_Position;
	float4 color : TEXCOORD0;
	float4 uv : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0
{
	// Sample unconditionally and select: solid geometry carries uv (0, 0, 0, 0),
	// so the texel it fetches is discarded. Two input slots, like a plain
	// textured quad; a third slot or a branch here was measurably slower.
	float4 texel = image_texture.Sample(image_sampler, input.uv.xy);
#ifdef RG_GUI_ATLAS_COVERAGE
	// Glyphs (z 2) draw white at the atlas coverage; images (z 1) keep RGBA.
	texel = input.uv.z > 1.5 ? float4(1.0, 1.0, 1.0, texel.r) : texel;
#endif
	return input.color * (input.uv.z > 0.5 ? texel : float4(1.0, 1.0, 1.0, 1.0));
}
