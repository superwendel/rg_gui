Texture2D<float4> image_texture : register(t0, space2);
SamplerState image_sampler : register(s0, space2);

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
	return input.color * (input.uv.z > 0.5 ? texel : float4(1.0, 1.0, 1.0, 1.0));
}
