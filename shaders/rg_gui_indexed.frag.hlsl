Texture2D<float4> image_texture : register(t0, space2);
SamplerState image_sampler : register(s0, space2);

struct FragmentInput
{
	float4 position : SV_Position;
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
	nointerpolation uint sampled : TEXCOORD2;
};

float4 main(FragmentInput input) : SV_Target0
{
	return input.sampled ? input.color * image_texture.Sample(image_sampler, input.uv) : input.color;
}
