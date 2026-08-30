cbuffer RgGuiTextUniforms : register(b0, space1)
{
	float2 output_size;
	float2 draw_offset;
	float2 atlas_inv_size;
	float2 padding;
};

struct VertexInput
{
	float2 position : TEXCOORD0;
	float2 size : TEXCOORD1;
	uint4 atlas_rect : TEXCOORD2;
	float4 color : TEXCOORD3;
	uint vertex_id : SV_VertexID;
};

struct VertexOutput
{
	float4 position : SV_Position;
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
};

VertexOutput main(VertexInput input)
{
	static const float2 corners[6] =
	    {
	        float2(0.0, 0.0),
	        float2(1.0, 0.0),
	        float2(1.0, 1.0),
	        float2(0.0, 0.0),
	        float2(1.0, 1.0),
	        float2(0.0, 1.0)};

	float2 corner = corners[input.vertex_id];
	float2 pixel = input.position + draw_offset + corner * input.size;
	float2 ndc = float2(pixel.x * (2.0 / output_size.x) - 1.0,
	                    1.0 - pixel.y * (2.0 / output_size.y));

	VertexOutput output;
	output.position = float4(ndc, 0.0, 1.0);
	output.color = input.color;
	output.uv = lerp(float2(input.atlas_rect.xy),
	                 float2(input.atlas_rect.zw), corner) *
	            atlas_inv_size;
	return output;
}
