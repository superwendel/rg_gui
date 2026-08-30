ByteAddressBuffer cached_quads : register(t0, space0);
ByteAddressBuffer runs : register(t1, space0);
RWByteAddressBuffer glyphs : register(u0, space1);

[numthreads(64, 1, 1)] void main(uint3 group_id : SV_GroupID, uint3 thread_id : SV_GroupThreadID)
{
	uint run_address = group_id.x * 32u;
	uint first_cached_quad = runs.Load(run_address + 0u);
	uint quad_count = runs.Load(run_address + 4u);
	float2 position = asfloat(runs.Load2(run_address + 8u));
	uint color = runs.Load(run_address + 16u);
	uint first_output_instance = runs.Load(run_address + 20u);
	for (uint index = thread_id.x; index < quad_count; index += 64u)
	{
		uint cached_address = (first_cached_quad + index) * 24u;
		float4 geometry = asfloat(cached_quads.Load4(cached_address));
		uint2 atlas = cached_quads.Load2(cached_address + 16u);
		uint output_address = (first_output_instance + index) * 32u;
		glyphs.Store4(output_address,
		              asuint(float4(position + geometry.xy, geometry.zw)));
		glyphs.Store2(output_address + 16u, atlas);
		glyphs.Store(output_address + 24u, color);
		glyphs.Store(output_address + 28u, 0u);
	}
}
