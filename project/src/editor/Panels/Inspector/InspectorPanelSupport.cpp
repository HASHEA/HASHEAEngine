#include "Panels/Inspector/InspectorPanelSupport.h"

#include "Core/EditorComponentComparison.h"
#include "Core/EditorSelection.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Panels/Inspector/InspectorComponentEditorSupport.h"
#include "Widgets/EditorThemeColors.h"
#include "Widgets/EditorTooltipWidgets.h"

#include <algorithm>
#include <cmath>
#include <optional>
#include <string>
#include <string_view>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr AshEngine::UITooltipConfig kInspectorCompactTooltipConfig{
			{ 360.0f, 0.0f },
			{ 220.0f, 0.0f },
			{ 520.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};
		constexpr AshEngine::UITooltipConfig kInspectorDetailTooltipConfig{
			{ 520.0f, 0.0f },
			{ 320.0f, 0.0f },
			{ 720.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};
		constexpr float kInspectorSummaryLabelWidth = 112.0f;
		constexpr AshEngine::UITableFlags kInspectorSummaryTableFlags =
			AshEngine::UITableFlagBits::SizingStretchProp |
			AshEngine::UITableFlagBits::BordersInner;
		constexpr float kInspectorMinimumScale = 0.0001f;
		constexpr float kInspectorMaximumScale = 100000.0f;

		bool BeginSummaryTable(AshEngine::UIContext& refUi, const char* pTableId)
		{
			if (!refUi.begin_table(pTableId, 2, kInspectorSummaryTableFlags))
			{
				return false;
			}

			refUi.table_setup_column("Label", AshEngine::UITableColumnFlagBits::WidthFixed, kInspectorSummaryLabelWidth);
			refUi.table_setup_column("Value", AshEngine::UITableColumnFlagBits::WidthStretch);
			return true;
		}

		void DrawSummaryRow(
			AshEngine::UIContext& refUi,
			const char* pLabel,
			std::string_view svValue)
		{
			refUi.table_next_row();
			refUi.table_next_column();
			refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", pLabel);
			refUi.table_next_column();
			const std::string_view svDisplayValue = svValue.empty() ? std::string_view("-") : svValue;
			const std::string strDisplayValue(svDisplayValue);
			refUi.text_wrapped("%s", strDisplayValue.c_str());
		}

		const char* GetInspectorSelectionKindLabel(EditorSelectionKind eKind)
		{
			switch (eKind)
			{
			case EditorSelectionKind::Entity:
				return "Entity";
			case EditorSelectionKind::Asset:
				return "Asset";
			case EditorSelectionKind::None:
			default:
				return "None";
			}
		}

		std::string MakeInspectorSelectionDisplayName(const EditorSelection& refSelection)
		{
			if (!refSelection.strLabel.empty())
			{
				return refSelection.strLabel;
			}
			if (!refSelection.strPath.empty())
			{
				return refSelection.strPath;
			}
			return refSelection.IsEmpty() ? "None" : "<Unnamed>";
		}

		std::string MakeMultiSelectionKindSummary(
			size_t uEntityCount,
			size_t uAssetCount,
			size_t uSelectionCount)
		{
			if (uSelectionCount == 0)
			{
				return "None";
			}
			if (uEntityCount == uSelectionCount)
			{
				return "Entities";
			}
			if (uAssetCount == uSelectionCount)
			{
				return "Assets";
			}
			return "Mixed";
		}
	}

	bool HasTransformClampWarning(const AshEngine::TransformComponent& refTransform)
	{
		return
			refTransform.scale.x <= kInspectorMinimumScale ||
			refTransform.scale.y <= kInspectorMinimumScale ||
			refTransform.scale.z <= kInspectorMinimumScale ||
			refTransform.scale.x >= kInspectorMaximumScale ||
			refTransform.scale.y >= kInspectorMaximumScale ||
			refTransform.scale.z >= kInspectorMaximumScale;
	}

	bool SanitizeTransformComponent(AshEngine::TransformComponent& refComponent)
	{
		const AshEngine::TransformComponent originalValue = refComponent;
		auto sanitizeFiniteScalar = [](const float fValue, const float fFallbackValue)
		{
			return std::isfinite(fValue) ? fValue : fFallbackValue;
		};
		auto sanitizeFiniteVec3 = [&sanitizeFiniteScalar](const glm::vec3& refValue, const glm::vec3& refFallbackValue)
		{
			return glm::vec3{
				sanitizeFiniteScalar(refValue.x, refFallbackValue.x),
				sanitizeFiniteScalar(refValue.y, refFallbackValue.y),
				sanitizeFiniteScalar(refValue.z, refFallbackValue.z)
			};
		};
		auto sanitizeClampedScalar = [&sanitizeFiniteScalar](
			const float fValue,
			const float fFallbackValue,
			const float fMinValue,
			const float fMaxValue)
		{
			return std::clamp(sanitizeFiniteScalar(fValue, fFallbackValue), fMinValue, fMaxValue);
		};

		refComponent.position = sanitizeFiniteVec3(refComponent.position, { 0.0f, 0.0f, 0.0f });
		refComponent.rotation_euler_degrees = sanitizeFiniteVec3(refComponent.rotation_euler_degrees, { 0.0f, 0.0f, 0.0f });
		refComponent.scale = sanitizeFiniteVec3(refComponent.scale, { 1.0f, 1.0f, 1.0f });
		refComponent.scale.x = sanitizeClampedScalar(refComponent.scale.x, 1.0f, kInspectorMinimumScale, kInspectorMaximumScale);
		refComponent.scale.y = sanitizeClampedScalar(refComponent.scale.y, 1.0f, kInspectorMinimumScale, kInspectorMaximumScale);
		refComponent.scale.z = sanitizeClampedScalar(refComponent.scale.z, 1.0f, kInspectorMinimumScale, kInspectorMaximumScale);
		return !TransformComponentsEqual(refComponent, originalValue);
	}

	std::optional<AshEngine::CameraComponent> GetCameraComponentValue(const AshEngine::Entity& refEntity)
	{
		return refEntity.has_camera_component()
			? std::optional<AshEngine::CameraComponent>{ refEntity.get_camera_component() }
			: std::nullopt;
	}

	std::optional<AshEngine::LightComponent> GetLightComponentValue(const AshEngine::Entity& refEntity)
	{
		return refEntity.has_light_component()
			? std::optional<AshEngine::LightComponent>{ refEntity.get_light_component() }
			: std::nullopt;
	}

	std::optional<AshEngine::MeshComponent> GetMeshComponentValue(const AshEngine::Entity& refEntity)
	{
		return refEntity.has_mesh_component()
			? std::optional<AshEngine::MeshComponent>{ refEntity.get_mesh_component() }
			: std::nullopt;
	}

	std::optional<AshEngine::EnvironmentComponent> GetEnvironmentComponentValue(const AshEngine::Entity& refEntity)
	{
		return refEntity.has_environment_component()
			? std::optional<AshEngine::EnvironmentComponent>{ refEntity.get_environment_component() }
			: std::nullopt;
	}

	std::optional<AshEngine::ParticleComponent> GetParticleComponentValue(const AshEngine::Entity& refEntity)
	{
		return refEntity.has_particle_component()
			? std::optional<AshEngine::ParticleComponent>{ refEntity.get_particle_component() }
			: std::nullopt;
	}

	std::optional<AshEngine::TerrainComponent> GetTerrainComponentValue(const AshEngine::Entity& refEntity)
	{
		return refEntity.has_terrain_component()
			? std::optional<AshEngine::TerrainComponent>{ refEntity.get_terrain_component() }
			: std::nullopt;
	}

	bool ApplyCameraComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::CameraComponent>& optValue)
	{
		if (!entity.is_valid())
		{
			return false;
		}

		if (!optValue.has_value())
		{
			return AshEngine::remove_scene_component(entity, AshEngine::SceneComponentType::Camera);
		}

		if (!entity.has_camera_component() &&
			!AshEngine::add_scene_component(entity, AshEngine::SceneComponentType::Camera))
		{
			return false;
		}

		return entity.set_camera_component(*optValue);
	}

	bool ApplyLightComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::LightComponent>& optValue)
	{
		if (!entity.is_valid())
		{
			return false;
		}

		if (!optValue.has_value())
		{
			return AshEngine::remove_scene_component(entity, AshEngine::SceneComponentType::Light);
		}

		if (!entity.has_light_component() &&
			!AshEngine::add_scene_component(entity, AshEngine::SceneComponentType::Light))
		{
			return false;
		}

		return entity.set_light_component(*optValue);
	}

	bool ApplyMeshComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::MeshComponent>& optValue)
	{
		if (!entity.is_valid())
		{
			return false;
		}

		if (!optValue.has_value())
		{
			return AshEngine::remove_scene_component(entity, AshEngine::SceneComponentType::Mesh);
		}

		if (!entity.has_mesh_component() &&
			!AshEngine::add_scene_component(entity, AshEngine::SceneComponentType::Mesh))
		{
			return false;
		}

		return entity.set_mesh_component(*optValue);
	}

	bool ApplyEnvironmentComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::EnvironmentComponent>& optValue)
	{
		if (!entity.is_valid())
		{
			return false;
		}

		if (!optValue.has_value())
		{
			return AshEngine::remove_scene_component(entity, AshEngine::SceneComponentType::Environment);
		}

		if (!entity.has_environment_component() &&
			!AshEngine::add_scene_component(entity, AshEngine::SceneComponentType::Environment))
		{
			return false;
		}

		return entity.set_environment_component(*optValue);
	}

	bool ApplyParticleComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::ParticleComponent>& optValue)
	{
		if (!entity.is_valid())
		{
			return false;
		}

		if (!optValue.has_value())
		{
			return AshEngine::remove_scene_component(entity, AshEngine::SceneComponentType::Particle);
		}

		if (!entity.has_particle_component() &&
			!AshEngine::add_scene_component(entity, AshEngine::SceneComponentType::Particle))
		{
			return false;
		}

		return entity.set_particle_component(*optValue);
	}

	bool ApplyTerrainComponentValue(
		AshEngine::Entity entity,
		const std::optional<AshEngine::TerrainComponent>& optValue)
	{
		if (!entity.is_valid())
		{
			return false;
		}

		if (!optValue.has_value())
		{
			return AshEngine::remove_scene_component(entity, AshEngine::SceneComponentType::Terrain);
		}

		if (!entity.has_terrain_component() &&
			!AshEngine::add_scene_component(entity, AshEngine::SceneComponentType::Terrain))
		{
			return false;
		}

		return entity.set_terrain_component(*optValue);
	}

	void DrawInspectorPanelIntro(
		AshEngine::UIContext& refUi,
		const char* pTitle,
		const char* pDescription)
	{
		refUi.push_font(AshEngine::UIFontRole::Strong);
		refUi.text_colored(GetEditorHeadingTextColor(refUi), "%s", pTitle);
		refUi.pop_font();
		refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", pDescription);
		refUi.separator();
	}

	void DrawInspectorSelectionSummary(
		AshEngine::UIContext& refUi,
		const EditorSelection& refSelection,
		const std::string_view svTooltipDescription)
	{
		const char* pKindLabel = GetInspectorSelectionKindLabel(refSelection.eKind);
		const std::string strSelectionName = MakeInspectorSelectionDisplayName(refSelection);

		refUi.push_font(AshEngine::UIFontRole::Strong);
		refUi.text_colored(GetEditorHeadingTextColor(refUi), "%s", strSelectionName.c_str());
		refUi.pop_font();
		if (refUi.is_item_hovered())
		{
			const bool bUseDetailTooltip = !refSelection.strPath.empty();
			refUi.begin_tooltip(bUseDetailTooltip ? kInspectorDetailTooltipConfig : kInspectorCompactTooltipConfig);
			if (bUseDetailTooltip)
			{
				DrawEditorTooltipTitle(refUi, strSelectionName, "Selection Details");
				if (BeginEditorTooltipTable(refUi, "InspectorSelectionTooltip", 72.0f))
				{
					DrawEditorTooltipRow(refUi, "Kind", pKindLabel);
					DrawEditorTooltipRow(refUi, "Id", std::to_string(static_cast<unsigned long long>(refSelection.uId)));
					DrawEditorTooltipRow(refUi, "Path", refSelection.strPath);
					refUi.end_table();
				}
			}
			else
			{
				DrawEditorTooltipCompactTitle(refUi, strSelectionName, pKindLabel);
				DrawEditorTooltipCompactRow(
					refUi,
					"Id",
					std::to_string(static_cast<unsigned long long>(refSelection.uId)));
				if (!svTooltipDescription.empty())
				{
					refUi.separator();
					DrawEditorTooltipCaption(refUi, svTooltipDescription);
				}
			}
			if (bUseDetailTooltip && !svTooltipDescription.empty())
			{
				refUi.separator();
				DrawEditorTooltipDescription(refUi, svTooltipDescription);
			}
			refUi.end_tooltip();
		}
		refUi.separator();
	}

	void DrawInspectorMultiSelectionSummary(
		AshEngine::UIContext& refUi,
		const std::vector<EditorSelection>& vecSelections,
		const std::string_view svDescription)
	{
		size_t uEntityCount = 0;
		size_t uAssetCount = 0;
		for (const EditorSelection& refSelection : vecSelections)
		{
			if (refSelection.eKind == EditorSelectionKind::Entity)
			{
				++uEntityCount;
			}
			else if (refSelection.eKind == EditorSelectionKind::Asset)
			{
				++uAssetCount;
			}
		}

		const std::string strTitle = std::to_string(vecSelections.size()) + " selected";
		const std::string strKindSummary = MakeMultiSelectionKindSummary(
			uEntityCount,
			uAssetCount,
			vecSelections.size());
		const EditorSelection* pPrimarySelection = vecSelections.empty() ? nullptr : &vecSelections.back();

		refUi.push_font(AshEngine::UIFontRole::Strong);
		refUi.text_colored(GetEditorHeadingTextColor(refUi), "%s", strTitle.c_str());
		refUi.pop_font();
		if (!svDescription.empty())
		{
			const std::string strDescription(svDescription);
			refUi.text_wrapped("%s", strDescription.c_str());
		}

		if (BeginSummaryTable(refUi, "InspectorMultiSelectionSummary"))
		{
			DrawSummaryRow(refUi, "Kind", strKindSummary);
			DrawSummaryRow(refUi, "Total", std::to_string(vecSelections.size()));
			DrawSummaryRow(refUi, "Entities", std::to_string(uEntityCount));
			DrawSummaryRow(refUi, "Assets", std::to_string(uAssetCount));
			if (pPrimarySelection)
			{
				DrawSummaryRow(refUi, "Primary", MakeInspectorSelectionDisplayName(*pPrimarySelection));
			}
			refUi.end_table();
		}

		constexpr size_t kInspectorMultiSelectionPreviewCount = 12;
		if (!vecSelections.empty() &&
			refUi.collapsing_header("Selected Items", AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			const size_t uPreviewCount = std::min(kInspectorMultiSelectionPreviewCount, vecSelections.size());
			for (size_t uIndex = 0; uIndex < uPreviewCount; ++uIndex)
			{
				const EditorSelection& refSelection = vecSelections[uIndex];
				const std::string strItemLabel =
					MakeInspectorSelectionDisplayName(refSelection) +
					" (" +
					GetInspectorSelectionKindLabel(refSelection.eKind) +
					")";
				refUi.bullet_text("%s", strItemLabel.c_str());
			}
			if (vecSelections.size() > uPreviewCount)
			{
				const std::string strRemaining =
					"... " +
					std::to_string(vecSelections.size() - uPreviewCount) +
					" more";
				refUi.text_colored(GetEditorMutedTextColor(refUi), "%s", strRemaining.c_str());
			}
		}

		refUi.separator();
	}

	void DrawInspectorAssetInspector(
		AshEngine::UIContext& refUi,
		const EditorSelection& refSelection)
	{
		(void)refUi;
		(void)refSelection;
	}

	void DrawInspectorEmptyState(AshEngine::UIContext& refUi)
	{
		DrawInspectorPanelIntro(refUi, "Inspector", "Select an entity or asset to inspect and edit its properties.");
		refUi.bullet_text("Entity selections show editable components and hierarchy data.");
		refUi.bullet_text("Asset selections can be previewed from Asset Browser with the Preview context action.");
	}

	void DrawInspectorHierarchySection(
		AshEngine::UIContext& refUi,
		const AshEngine::Entity& refEntity)
	{
		if (!refUi.collapsing_header("Hierarchy", AshEngine::UITreeNodeFlagBits::DefaultOpen))
		{
			return;
		}

		const AshEngine::Entity parent = refEntity.get_parent();
		if (BeginSummaryTable(refUi, "InspectorHierarchySummary"))
		{
			DrawSummaryRow(
				refUi,
				"Parent",
				parent.is_valid() ? std::to_string(parent.get_id()) : std::string("<Root>"));
			DrawSummaryRow(refUi, "Children", std::to_string(refEntity.get_children().size()));
			refUi.end_table();
		}
	}
}
