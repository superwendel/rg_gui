struct FragmentInput
{
	float4 position : SV_Position;
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
};

float4 main(FragmentInput input) : SV_Target0
{
	return input.color;
}
