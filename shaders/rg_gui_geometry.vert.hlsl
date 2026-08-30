cbuffer RgGuiGeometryUniforms : register(b0, space1)
{
	float2 output_size;
	float2 draw_offset;
};

struct VertexInput
{
	float2 position : TEXCOORD0;
	float4 color : TEXCOORD1;
	float2 uv : TEXCOORD2;
};

struct VertexOutput
{
	float4 position : SV_Position;
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
	float2 pixel = input.position + draw_offset;
	float2 ndc = float2(pixel.x * (2.0 / output_size.x) - 1.0,
	                    1.0 - pixel.y * (2.0 / output_size.y));

	VertexOutput output;
	output.position = float4(ndc, 0.0, 1.0);
	output.color = input.color;
	output.uv = input.uv;
	return output;
}
