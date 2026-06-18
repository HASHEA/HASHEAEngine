#include "VolumetricLightingCommon.hlsli"

Texture2D<float4> SceneVolumetricScattering : register(t0);
Texture2D<float4> SceneVolumetricScatteringHistory : register(t1);
RWTexture2D<float4> SceneVolumetricScatteringTemporal : register(u0);
RWTexture2D<float4> SceneVolumetricHistoryValidity : register(u1);
RWTexture2D<float4> SceneVolumetricHistoryWrite : register(u2);
SamplerState SceneLinearClampSampler : register(s0);

bool AshVolumetricPreviousViewDepth01(float3 position_ws, out float previous_depth01)
{
	previous_depth01 = 0.0;
	if (AshScreenLightPositionAndParams.w <= 0.5)
	{
		return false;
	}

	const float3 previous_forward =
		AshVolumetricSafeNormalize(AshVolumetricConfig0.xyz, float3(0.0, 0.0, 1.0));
	const float previous_view_depth = dot(position_ws - AshScreenLightPositionAndParams.xyz, previous_forward);
	const float previous_max_view_depth = max(AshVolumetricConfig0.w, 0.01);
	previous_depth01 = previous_view_depth / previous_max_view_depth;
	return previous_view_depth > 0.0 && previous_depth01 >= 0.0 && previous_depth01 <= 1.0;
}

bool AshVolumetricReprojectHistoryUV(uint2 tile_pixel, uint slice, out float2 history_uv)
{
	history_uv = float2(0.0, 0.0);

	const float2 tile_uv = AshVolumetricTileUV(tile_pixel);
	const float current_view_depth = AshVolumetricSliceViewDepth(slice);
	const float3 position_ws = AshVolumetricReconstructWorldPositionAtViewDepth(tile_uv, current_view_depth);
	const float4 history_clip = mul(AshHistoryViewProjection, float4(position_ws, 1.0));
	if (history_clip.w <= 1e-6)
	{
		return false;
	}

	const float3 history_ndc = history_clip.xyz / history_clip.w;
	if (history_ndc.x < -1.0 || history_ndc.x > 1.0 ||
		history_ndc.y < -1.0 || history_ndc.y > 1.0 ||
		history_ndc.z < 0.0 || history_ndc.z > 1.0)
	{
		return false;
	}

	float previous_depth01 = 0.0;
	if (!AshVolumetricPreviousViewDepth01(position_ws, previous_depth01))
	{
		return false;
	}

	const uint depth_slices = AshVolumetricDepthSliceCount();
	const uint slices_per_row = AshVolumetricSlicesPerRow();
	const uint history_slice = min((uint)floor(AshVolumetricSliceFromDepth01(previous_depth01)), depth_slices - 1u);
	const float2 history_tile_uv = history_ndc.xy * float2(0.5, -0.5) + float2(0.5, 0.5);
	const float2 atlas_inv_size = 1.0 / max(AshVolumetricAtlasSize.zw, float2(1.0, 1.0));
	history_uv = AshVolumetricAtlasUVFromTileUV(history_tile_uv, history_slice, slices_per_row, atlas_inv_size);
	return true;
}

float4 AshVolumetricClampHistory(float4 current_value, float4 history_value)
{
	const float3 rgb_extent = max(abs(current_value.rgb) * 2.0 + 0.02, float3(0.02, 0.02, 0.02));
	const float extinction_extent = max(abs(current_value.a) * 2.0 + 0.02, 0.02);
	history_value.rgb = clamp(history_value.rgb, current_value.rgb - rgb_extent, current_value.rgb + rgb_extent);
	history_value.a = clamp(history_value.a, current_value.a - extinction_extent, current_value.a + extinction_extent);
	return history_value;
}

[numthreads(8, 8, 1)]
void CSMain(uint3 dispatch_id : SV_DispatchThreadID)
{
	uint width = (uint)AshVolumetricAtlasSize.z;
	uint height = (uint)AshVolumetricAtlasSize.w;
	if (dispatch_id.x >= width || dispatch_id.y >= height)
	{
		return;
	}

	uint2 tile_pixel = uint2(0u, 0u);
	uint slice = 0u;
	if (!AshVolumetricDecodeAtlasPixel(dispatch_id.xy, tile_pixel, slice))
	{
		SceneVolumetricScatteringTemporal[dispatch_id.xy] = float4(0.0, 0.0, 0.0, 0.0);
		SceneVolumetricHistoryValidity[dispatch_id.xy] = float4(0.0, 0.0, 0.0, 1.0);
		SceneVolumetricHistoryWrite[dispatch_id.xy] = float4(0.0, 0.0, 0.0, 0.0);
		return;
	}

	float2 uv = (float2(dispatch_id.xy) + 0.5) / max(float2(width, height), float2(1.0, 1.0));
	float4 current_value = SceneVolumetricScattering.SampleLevel(SceneLinearClampSampler, uv, 0);
	float blend = saturate(AshVolumetricConfig1.y);
	float2 history_uv = uv;
	const bool reprojected_history_valid =
		blend > 0.0 && AshVolumetricReprojectHistoryUV(tile_pixel, slice, history_uv);
	float4 history_value = reprojected_history_valid ?
		SceneVolumetricScatteringHistory.SampleLevel(SceneLinearClampSampler, history_uv, 0) :
		current_value;
	history_value = AshVolumetricClampHistory(current_value, history_value);
	const float effective_blend = reprojected_history_valid ? blend : 0.0;
	float4 filtered = lerp(current_value, history_value, effective_blend);
	SceneVolumetricScatteringTemporal[dispatch_id.xy] = filtered;
	SceneVolumetricHistoryValidity[dispatch_id.xy] = float4(reprojected_history_valid ? 1.0 : 0.0, effective_blend, history_uv);
	SceneVolumetricHistoryWrite[dispatch_id.xy] = filtered;
}
