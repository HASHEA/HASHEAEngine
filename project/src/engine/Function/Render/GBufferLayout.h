#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderDevice.h"
#include <cstdint>
#include <initializer_list>
#include <string_view>
#include <vector>

namespace AshEngine
{
	enum class SceneRenderFeature : uint64_t
	{
		None = 0,
		DeferredLighting = 1ull << 0,
		TemporalMotionVector3D = 1ull << 1,
		ExtendedMaterialData = 1ull << 2,
		HDRMaterialEmission = 1ull << 3,
		MaterialId = 1ull << 4,
		DebugGBuffer = 1ull << 5
	};

	enum class GBufferSemantic : uint8_t
	{
		BaseColor = 0,
		ShadingModelAndFlags,
		MetallicRoughnessAOAndSpecular,
		CustomData,
		MotionVector3D,
		TemporalFlags,
		NormalOct,
		EmissiveOrCustom
	};

	struct GBufferLayoutKey
	{
		uint64_t feature_flags = 0;
		uint32_t quality_tier = 0;
		uint32_t platform_tier = 0;

		bool operator==(const GBufferLayoutKey& rhs) const;
	};

	struct GBufferAttachmentDesc
	{
		std::string_view name{};
		RenderTextureFormat format = RenderTextureFormat::Unknown;
		bool clear_to_zero = true;
	};

	struct GBufferSemanticMapping
	{
		GBufferSemantic semantic = GBufferSemantic::BaseColor;
		uint8_t attachment_index = 0;
		uint8_t component_mask = 0;
	};

	struct GBufferLayoutDesc
	{
		GBufferLayoutKey key{};
		uint64_t layout_hash = 0;
		std::vector<GBufferAttachmentDesc> attachments{};
		std::vector<GBufferSemanticMapping> semantic_mappings{};
	};

	ASH_API uint64_t make_scene_render_feature_flags(std::initializer_list<SceneRenderFeature> features);
	ASH_API GBufferLayoutKey make_deferred_hq_gbuffer_layout_key();
	ASH_API const GBufferLayoutDesc& get_deferred_hq_gbuffer_layout();
	ASH_API const GBufferSemanticMapping* find_gbuffer_semantic_mapping(
		const GBufferLayoutDesc& layout,
		GBufferSemantic semantic);
}
