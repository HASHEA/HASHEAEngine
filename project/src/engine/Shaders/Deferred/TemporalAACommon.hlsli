#ifndef ASH_TEMPORAL_AA_COMMON_HLSLI
#define ASH_TEMPORAL_AA_COMMON_HLSLI

// TAA Resolve root constants (b0). Keep <= 6 vec4 to stay well under the
// 64-DWORD / 256B root signature budget (avoids the volumetric overflow class
// of bug, HRESULT 0x80070057).
cbuffer AshRootConstants : register(b0)
{
	// (render_width, render_height, history_blend, variance_gamma)
	float4 AshTaaConfig0;
	// (jitter_uv.x, jitter_uv.y, prev_jitter_uv.x, prev_jitter_uv.y)
	float4 AshTaaConfig1;
	// (reverse_z, history_valid, luminance_weighting, debug_view)
	float4 AshTaaConfig2;
};

float2 AshTaaRenderExtent()
{
	return max(AshTaaConfig0.xy, float2(1.0, 1.0));
}

float AshTaaHistoryBlend()
{
	return saturate(AshTaaConfig0.z);
}

float AshTaaVarianceGamma()
{
	return max(AshTaaConfig0.w, 0.1);
}

float2 AshTaaJitterUV()
{
	return AshTaaConfig1.xy;
}

float2 AshTaaPreviousJitterUV()
{
	return AshTaaConfig1.zw;
}

bool AshTaaIsReverseZ()
{
	return AshTaaConfig2.x > 0.5;
}

bool AshTaaHistoryValid()
{
	return AshTaaConfig2.y > 0.5;
}

bool AshTaaLuminanceWeighting()
{
	return AshTaaConfig2.z > 0.5;
}

uint AshTaaDebugView()
{
	return (uint)(AshTaaConfig2.w + 0.5);
}

// RGB <-> YCoCg. Variance clipping is more stable in YCoCg because chroma and
// luma decorrelate, reducing color drift on high-contrast edges.
float3 AshTaaRgbToYCoCg(float3 c)
{
	float Y = dot(c, float3(0.25, 0.5, 0.25));
	float Co = dot(c, float3(0.5, 0.0, -0.5));
	float Cg = dot(c, float3(-0.25, 0.5, -0.25));
	return float3(Y, Co, Cg);
}

float3 AshTaaYCoCgToRgb(float3 c)
{
	float Y = c.x;
	float Co = c.y;
	float Cg = c.z;
	return float3(Y + Co - Cg, Y + Cg, Y - Co - Cg);
}

float AshTaaLuminance(float3 c)
{
	return dot(c, float3(0.2126, 0.7152, 0.0722));
}

// Clip history sample q toward the current neighborhood AABB center (Playdead
// INSIDE-style line clip). Unlike per-component clamp, this slides along the
// line from center to q and only intersects the box surface, which preserves
// chroma direction and reduces color drift on high-contrast edges.
float3 AshTaaClipToAABB(float3 aabb_min, float3 aabb_max, float3 q)
{
	const float3 center = 0.5 * (aabb_max + aabb_min);
	const float3 extent = 0.5 * (aabb_max - aabb_min) + 1e-5;
	const float3 v = q - center;
	const float3 unit = v / extent;
	const float3 abs_unit = abs(unit);
	const float max_unit = max(abs_unit.x, max(abs_unit.y, abs_unit.z));
	if (max_unit > 1.0)
	{
		return center + v / max_unit;
	}
	return q;
}

#endif // ASH_TEMPORAL_AA_COMMON_HLSLI
