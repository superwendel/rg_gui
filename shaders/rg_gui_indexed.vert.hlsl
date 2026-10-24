ByteAddressBuffer cached_quads : register(t0, space0);
ByteAddressBuffer frame : register(t1, space0);
cbuffer Projection : register(b0, space1)
{
	float2 output_size;
	float2 draw_offset;
	float2 atlas_inv_size;
	uint geometry_offset;
	uint refs_offset;
	uint clips_offset;
};
struct Output
{
	float4 position : SV_Position;
	float4 color : TEXCOORD0;
	float2 uv : TEXCOORD1;
	nointerpolation uint sampled : TEXCOORD2;
	// Distances to the four edges of the primitive's clip rectangle. The
	// rasterizer drops pixels where any is negative, so clipping needs no
	// scissor change and one draw call can span many clips.
	float4 clip : SV_ClipDistance0;
};
Output main(uint vertex : SV_VertexID)
{
	static const float2 corners[4] = {
	    float2(0, 0), float2(1, 0), float2(1, 1), float2(0, 1)};
	uint2 ref = frame.Load2(refs_offset + (vertex >> 2) * 8);
	uint corner_id = vertex & 3;
	uint kind = ref.y >> 30;
	float2 pixel;
	uint color;
	uint clip;
	Output result;
	if (kind == 0)
	{
		// A glyph: ref.x is the cached quad, ref.y the frame run (position,
		// color, clip-table index).
		uint4 run = frame.Load4((ref.y & 0x3fffffff) * 16);
		uint quad = ref.x * 24;
		float2 corner = corners[corner_id];
		pixel = asfloat(cached_quads.Load2(quad)) + asfloat(run.xy) +
		        draw_offset + corner * asfloat(cached_quads.Load2(quad + 8));
		color = run.z;
		clip = run.w;
		uint2 uv = cached_quads.Load2(quad + 16);
		result.uv = lerp(float2(uv.x & 65535, uv.x >> 16),
		                 float2(uv.y & 65535, uv.y >> 16), corner) *
		            atlas_inv_size;
		result.sampled = 1;
	}
	else
	{
		// A quad holds four raw vertices in corner order. A lone triangle maps
		// corner 3 to 2, so its second triangle degenerates. ref.y carries the
		// clip-table index in bits 1..29 and the quad flag in bit 0.
		uint raw_corner = (ref.y & 1) != 0 ? corner_id : min(corner_id, 2);
		uint raw_vertex = ref.x + raw_corner;
		uint raw = geometry_offset + raw_vertex * 20;
		pixel = asfloat(frame.Load2(raw)) + draw_offset;
		color = frame.Load(raw + 8);
		clip = (ref.y >> 1) & 0x1fffffff;
		result.uv = asfloat(frame.Load2(raw + 12));
		result.sampled = kind == 2;
	}
	// Round the clip like the CPU scissor did: floor the near edges and ceil
	// the far edges after adding the draw offset.
	float4 edges = asfloat(frame.Load4(clips_offset + clip * 16));
	float2 near_edge = floor(edges.xy + draw_offset);
	float2 far_edge = ceil(edges.zw + draw_offset);
	result.clip = float4(pixel - near_edge, far_edge - pixel);
	result.position = float4(pixel.x * (2.0 / output_size.x) - 1.0,
	                         1.0 - pixel.y * (2.0 / output_size.y), 0, 1);
	result.color = float4(color & 255, (color >> 8) & 255,
	                      (color >> 16) & 255, (color >> 24) & 255) /
	               255.0;
	return result;
}
