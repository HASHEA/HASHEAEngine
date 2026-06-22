#include "TemporalAACommon.hlsli"

Texture2D<float4> SceneCurrentHDR : register(t0);
Texture2D<float4> SceneHistoryHDR : register(t1);
Texture2D<float4> SceneGBufferMotion : register(t2);
Texture2D<float4> SceneDepth : register(t3);
RWTexture2D<float4> SceneTaaResolveOutput : register(u0);
RWTexture2D<float4> SceneTaaHistoryWrite : register(u1);
SamplerState ScenePointClampSampler : register(s0);
SamplerState SceneLinearClampSampler : register(s1);

// GBufferD.rg = current_uv(jittered) - previous_uv(jittered). The jitter delta
// (jitter_curr - jitter_prev) leaks into motion because both UVs come from
// jittered clip matrices. Subtract it to recover pure geometric motion, else a
// static image keeps drifting and TAA injects noise instead of removing it.
float2 AshTaaGeometricMotionUV(float2 motion_sample_uv)
{
	const float2 jitter_delta_uv = AshTaaJitterUV() - AshTaaPreviousJitterUV();
	return motion_sample_uv - jitter_delta_uv;
}

// A depth is "closer" with reverse-Z when it is larger, otherwise smaller.
bool AshTaaDepthCloser(float candidate, float reference)
{
	return AshTaaIsReverseZ() ? (candidate > reference) : (candidate < reference);
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	const float2 extent = AshTaaRenderExtent();
	const uint width = (uint)extent.x;
	const uint height = (uint)extent.y;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}

	const float2 inv_extent = 1.0 / extent;
	const int2 center_pixel = int2(dispatch_id.xy);
	const int2 max_pixel = int2((int)width - 1, (int)height - 1);
	const float2 uv = (float2(dispatch_id.xy) + 0.5) * inv_extent;

	const float3 current_color = SceneCurrentHDR.SampleLevel(ScenePointClampSampler, uv, 0).rgb;

	// 3x3 neighborhood: build current-frame YCoCg statistics (mean/variance) and
	// pick the closest-depth texel for motion-vector dilation (reduces ghosting at
	// silhouette edges, where the foreground motion should win).
	float3 ycocg_mean = 0.0.xxx;
	float3 ycocg_sq_mean = 0.0.xxx;
	int2 closest_pixel = center_pixel;
	float closest_depth = SceneDepth.SampleLevel(ScenePointClampSampler, uv, 0).r;

	[unroll]
	for (int dy = -1; dy <= 1; ++dy)
	{
		[unroll]
		for (int dx = -1; dx <= 1; ++dx)
		{
			const int2 tap_pixel = clamp(center_pixel + int2(dx, dy), int2(0, 0), max_pixel);
			const float2 tap_uv = (float2(tap_pixel) + 0.5) * inv_extent;

			const float3 tap_color = SceneCurrentHDR.SampleLevel(ScenePointClampSampler, tap_uv, 0).rgb;
			const float3 tap_ycocg = AshTaaRgbToYCoCg(tap_color);
			ycocg_mean += tap_ycocg;
			ycocg_sq_mean += tap_ycocg * tap_ycocg;

			const float tap_depth = SceneDepth.SampleLevel(ScenePointClampSampler, tap_uv, 0).r;
			if (AshTaaDepthCloser(tap_depth, closest_depth))
			{
				closest_depth = tap_depth;
				closest_pixel = tap_pixel;
			}
		}
	}

	ycocg_mean *= (1.0 / 9.0);
	ycocg_sq_mean *= (1.0 / 9.0);
	const float3 ycocg_variance = max(ycocg_sq_mean - ycocg_mean * ycocg_mean, 0.0.xxx);
	const float3 ycocg_sigma = sqrt(ycocg_variance);
	const float gamma = AshTaaVarianceGamma();
	const float3 aabb_min = ycocg_mean - gamma * ycocg_sigma;
	const float3 aabb_max = ycocg_mean + gamma * ycocg_sigma;

	// Reproject using the dilated (closest-depth) motion vector.
	const float2 closest_uv = (float2(closest_pixel) + 0.5) * inv_extent;
	const float4 motion_sample = SceneGBufferMotion.SampleLevel(ScenePointClampSampler, closest_uv, 0);
	const bool temporal_valid = motion_sample.a > 0.5;
	const float2 motion_geom_uv = AshTaaGeometricMotionUV(motion_sample.rg);
	const float2 history_uv = uv - motion_geom_uv;

	const bool history_uv_in_bounds =
		history_uv.x >= 0.0 && history_uv.x <= 1.0 &&
		history_uv.y >= 0.0 && history_uv.y <= 1.0;
	const bool use_history = AshTaaHistoryValid() && temporal_valid && history_uv_in_bounds;

	float3 resolved_color = current_color;
	float effective_blend = 0.0;
	if (use_history)
	{
		float3 history_color = SceneHistoryHDR.SampleLevel(SceneLinearClampSampler, history_uv, 0).rgb;

		// Variance clipping: constrain history to the current neighborhood AABB in
		// YCoCg space (line clip toward center, not per-component clamp).
		const float3 history_ycocg = AshTaaRgbToYCoCg(history_color);
		const float3 clipped_ycocg = AshTaaClipToAABB(aabb_min, aabb_max, history_ycocg);
		history_color = AshTaaYCoCgToRgb(clipped_ycocg);

		float blend = AshTaaHistoryBlend();
		if (AshTaaLuminanceWeighting())
		{
			// Anti-flicker: weight current/history by inverse luma so a single
			// bright firefly cannot dominate the accumulated result.
			const float current_weight = (1.0 - blend) / (1.0 + AshTaaLuminance(current_color));
			const float history_weight = blend / (1.0 + AshTaaLuminance(history_color));
			const float weight_sum = max(current_weight + history_weight, 1e-5);
			resolved_color = (current_color * current_weight + history_color * history_weight) / weight_sum;
			effective_blend = history_weight / weight_sum;
		}
		else
		{
			resolved_color = lerp(current_color, history_color, blend);
			effective_blend = blend;
		}
	}

	// History always stores the clean resolved color (never the debug overlay)
	// so debug visualization can't poison next frame's accumulation.
	SceneTaaHistoryWrite[dispatch_id.xy] = float4(resolved_color, 1.0);

	float3 output_color = resolved_color;
	const uint debug_view = AshTaaDebugView();
	if (debug_view == 1u) // MotionVectors
	{
		output_color = float3(abs(motion_geom_uv) * 32.0, temporal_valid ? 0.0 : 1.0);
	}
	else if (debug_view == 2u) // HistoryWeight
	{
		output_color = use_history ? effective_blend.xxx : float3(1.0, 0.0, 0.0);
	}
	else if (debug_view == 3u) // Variance
	{
		output_color = ycocg_sigma * 8.0;
	}

	SceneTaaResolveOutput[dispatch_id.xy] = float4(output_color, 1.0);
}
