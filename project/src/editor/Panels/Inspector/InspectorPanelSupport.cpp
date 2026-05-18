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

	bool CanAddCameraComponent(const AshEngine::Entity& refEntity)
	{
		return !refEntity.has_camera_component();
	}

	bool CanAddLightComponent(const AshEngine::Entity& refEntity)
	{
		return !refEntity.has_light_component();
	}

	bool CanAddMeshComponent(const AshEngine::Entity& refEntity)
	{
		return !refEntity.has_mesh_component();
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
			return entity.has_camera_component() && entity.remove_camera_component();
		}

		return entity.has_camera_component()
			? entity.set_camera_component(*optValue)
			: entity.add_camera_component(*optValue);
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
			return entity.has_light_component() && entity.remove_light_component();
		}

		return entity.has_light_component()
			? entity.set_light_component(*optValue)
			: entity.add_light_component(*optValue);
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
			return entity.has_mesh_component() && entity.remove_mesh_component();
		}

		return entity.has_mesh_component()
			? entity.set_mesh_component(*optValue)
			: entity.add_mesh_component(*optValue);
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
		const char* pKindLabel = "Selection";
		switch (refSelection.eKind)
		{
		case EditorSelectionKind::Entity:
			pKindLabel = "Entity";
			break;
		case EditorSelectionKind::Asset:
			pKindLabel = "Asset";
			break;
		default:
			break;
		}

		refUi.push_font(AshEngine::UIFontRole::Strong);
		refUi.text_colored(GetEditorHeadingTextColor(refUi), "%s", refSelection.strLabel.c_str());
		refUi.pop_font();
		if (refUi.is_item_hovered())
		{
			const bool bUseDetailTooltip = !refSelection.strPath.empty();
			refUi.begin_tooltip(bUseDetailTooltip ? kInspectorDetailTooltipConfig : kInspectorCompactTooltipConfig);
			if (bUseDetailTooltip)
			{
				DrawEditorTooltipTitle(refUi, refSelection.strLabel, "Selection Details");
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
				DrawEditorTooltipCompactTitle(refUi, refSelection.strLabel, pKindLabel);
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
