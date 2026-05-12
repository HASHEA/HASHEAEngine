#include "Function/Render/GBufferLayout.h"

namespace AshEngine
{
	namespace
	{
		static constexpr uint8_t k_component_rgb = 0x7u;
		static constexpr uint8_t k_component_a = 0x8u;
		static constexpr uint8_t k_component_rgba = 0xFu;
		static constexpr uint8_t k_component_rg = 0x3u;
		static constexpr uint8_t k_component_ba = 0xCu;

		auto hash_layout(const GBufferLayoutDesc& layout) -> uint64_t
		{
			std::size_t hash_value = 0;
			ASH_HASH::hash_combine(hash_value, layout.key.feature_flags);
			ASH_HASH::hash_combine(hash_value, layout.key.quality_tier);
			ASH_HASH::hash_combine(hash_value, layout.key.platform_tier);
			for (const GBufferAttachmentDesc& attachment : layout.attachments)
			{
				ASH_HASH::hash_combine(hash_value, attachment.name, std::hash<std::string_view>{});
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(attachment.format));
				ASH_HASH::hash_combine(hash_value, attachment.clear_to_zero);
			}
			for (const GBufferSemanticMapping& mapping : layout.semantic_mappings)
			{
				ASH_HASH::hash_combine(hash_value, static_cast<uint8_t>(mapping.semantic));
				ASH_HASH::hash_combine(hash_value, mapping.attachment_index);
				ASH_HASH::hash_combine(hash_value, mapping.component_mask);
			}
			return static_cast<uint64_t>(hash_value);
		}

		auto build_deferred_hq_layout() -> GBufferLayoutDesc
		{
			GBufferLayoutDesc layout{};
			layout.key = make_deferred_hq_gbuffer_layout_key();
			layout.attachments = {
				{ "GBufferA", RenderTextureFormat::RGBA8_UNORM, true },
				{ "GBufferB", RenderTextureFormat::RGBA8_UNORM, true },
				{ "GBufferC", RenderTextureFormat::RGBA8_UNORM, true },
				{ "GBufferD", RenderTextureFormat::RGBA16_SFLOAT, true },
				{ "GBufferE", RenderTextureFormat::RGBA16_SFLOAT, true }
			};
			layout.semantic_mappings = {
				{ GBufferSemantic::BaseColor, 0, k_component_rgb },
				{ GBufferSemantic::ShadingModelAndFlags, 0, k_component_a },
				{ GBufferSemantic::MetallicRoughnessAOAndSpecular, 1, k_component_rgba },
				{ GBufferSemantic::CustomData, 2, k_component_rgba },
				{ GBufferSemantic::MotionVector3D, 3, k_component_rgb },
				{ GBufferSemantic::TemporalFlags, 3, k_component_a },
				{ GBufferSemantic::NormalOct, 4, k_component_rg },
				{ GBufferSemantic::EmissiveOrCustom, 4, k_component_ba }
			};
			layout.layout_hash = hash_layout(layout);
			return layout;
		}
	}

	bool GBufferLayoutKey::operator==(const GBufferLayoutKey& rhs) const
	{
		return feature_flags == rhs.feature_flags &&
			quality_tier == rhs.quality_tier &&
			platform_tier == rhs.platform_tier;
	}

	uint64_t make_scene_render_feature_flags(std::initializer_list<SceneRenderFeature> features)
	{
		uint64_t flags = 0;
		for (SceneRenderFeature feature : features)
		{
			flags |= static_cast<uint64_t>(feature);
		}
		return flags;
	}

	GBufferLayoutKey make_deferred_hq_gbuffer_layout_key()
	{
		return {
			make_scene_render_feature_flags({
				SceneRenderFeature::DeferredLighting,
				SceneRenderFeature::TemporalMotionVector3D,
				SceneRenderFeature::ExtendedMaterialData,
				SceneRenderFeature::HDRMaterialEmission
			}),
			1u,
			0u
		};
	}

	const GBufferLayoutDesc& get_deferred_hq_gbuffer_layout()
	{
		static const GBufferLayoutDesc layout = build_deferred_hq_layout();
		return layout;
	}

	const GBufferSemanticMapping* find_gbuffer_semantic_mapping(
		const GBufferLayoutDesc& layout,
		GBufferSemantic semantic)
	{
		for (const GBufferSemanticMapping& mapping : layout.semantic_mappings)
		{
			if (mapping.semantic == semantic)
			{
				return &mapping;
			}
		}
		return nullptr;
	}
}
