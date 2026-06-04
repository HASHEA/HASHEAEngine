#include "Panels/Inspector/InspectorComponentMetadata.h"

#include "Function/Asset/AssetDatabase.h"
#include "Function/Scene/Scene.h"

#include <algorithm>
#include <cstddef>
#include <cstdint>
#include <limits>
#include <sstream>
#include <string_view>
#include <utility>

namespace AshEditor
{
	namespace
	{
		std::string MakeRangeText(const AshEngine::ScenePropertyDesc& refPropertyDesc)
		{
			if (!refPropertyDesc.use_range)
			{
				return {};
			}

			std::ostringstream stream{};
			stream << "[" << refPropertyDesc.range_min << ", " << refPropertyDesc.range_max << "]";
			return stream.str();
		}
	}

	InspectorSceneFieldDesc MakeInspectorSceneFieldDesc(
		const AshEngine::SceneComponentType eComponentType,
		const char* pPropertyName,
		const char* pFallbackTitle,
		const char* pFallbackDescription,
		std::string strDefaultValue,
		std::string strValidRange,
		std::string strBehavior)
	{
		return {
			eComponentType,
			pPropertyName,
			pFallbackTitle,
			pFallbackDescription,
			std::move(strDefaultValue),
			std::move(strValidRange),
			std::move(strBehavior)
		};
	}

	const AshEngine::ScenePropertyDesc* FindScenePropertyDesc(
		const AshEngine::SceneComponentType eComponentType,
		const char* pPropertyName)
	{
		if (!pPropertyName)
		{
			return nullptr;
		}

		const AshEngine::SceneComponentDesc* pComponentDesc =
			AshEngine::get_scene_component_descriptor(eComponentType);
		if (!pComponentDesc || !pComponentDesc->properties)
		{
			return nullptr;
		}

		for (uint32_t uIndex = 0; uIndex < pComponentDesc->property_count; ++uIndex)
		{
			const AshEngine::ScenePropertyDesc& refPropertyDesc = pComponentDesc->properties[uIndex];
			if (refPropertyDesc.name && std::string_view(refPropertyDesc.name) == std::string_view(pPropertyName))
			{
				return &refPropertyDesc;
			}
		}

		return nullptr;
	}

	const char* GetScenePropertyDisplayName(
		const AshEngine::ScenePropertyDesc* pPropertyDesc,
		const char* pFallbackName)
	{
		if (pPropertyDesc && pPropertyDesc->display_name && pPropertyDesc->display_name[0] != '\0')
		{
			return pPropertyDesc->display_name;
		}
		return pFallbackName ? pFallbackName : "Property";
	}

	InspectorFieldSpec MakeInspectorFieldSpec(
		const AshEngine::ScenePropertyDesc* pPropertyDesc,
		const char* pFallbackTitle,
		const char* pFallbackDescription,
		std::string strDefaultValue,
		std::string strValidRange,
		std::string strBehavior)
	{
		if (pPropertyDesc)
		{
			if (strValidRange.empty())
			{
				strValidRange = MakeRangeText(*pPropertyDesc);
			}
			return {
				GetScenePropertyDisplayName(pPropertyDesc, pFallbackTitle),
				pPropertyDesc->tooltip && pPropertyDesc->tooltip[0] != '\0'
					? pPropertyDesc->tooltip
					: pFallbackDescription,
				std::move(strDefaultValue),
				std::move(strValidRange),
				std::move(strBehavior)
			};
		}

		return {
			pFallbackTitle,
			pFallbackDescription,
			std::move(strDefaultValue),
			std::move(strValidRange),
			std::move(strBehavior)
		};
	}

	InspectorFieldSpec MakeInspectorSceneFieldSpec(const InspectorSceneFieldDesc& refDesc)
	{
		return MakeInspectorFieldSpec(
			FindScenePropertyDesc(refDesc.eComponentType, refDesc.pPropertyName),
			refDesc.pFallbackTitle,
			refDesc.pFallbackDescription,
			refDesc.strDefaultValue,
			refDesc.strValidRange,
			refDesc.strBehavior);
	}

	InspectorEnumOptions MakeInspectorEnumOptions(const char* pEnumName)
	{
		InspectorEnumOptions options{};
		const AshEngine::SceneEnumDesc* pEnumDesc = AshEngine::get_scene_enum_descriptor(pEnumName);
		if (!pEnumDesc || !pEnumDesc->values)
		{
			return options;
		}

		options.vecLabels.reserve(pEnumDesc->value_count);
		options.vecValues.reserve(pEnumDesc->value_count);
		for (uint32_t uIndex = 0; uIndex < pEnumDesc->value_count; ++uIndex)
		{
			const AshEngine::SceneEnumValueDesc& refValueDesc = pEnumDesc->values[uIndex];
			options.vecLabels.push_back(refValueDesc.name ? refValueDesc.name : "Unknown");
			options.vecValues.push_back(refValueDesc.value);
		}
		return options;
	}

	std::vector<AshEngine::AssetType> MakeInspectorAssetTypesForProperty(
		const AshEngine::ScenePropertyDesc* pPropertyDesc)
	{
		if (!pPropertyDesc)
		{
			return {};
		}

		switch (pPropertyDesc->asset_ref_kind)
		{
		case AshEngine::ScenePropertyAssetRefKind::Mesh:
			return { AshEngine::AssetType::Mesh, AshEngine::AssetType::Model };
		case AshEngine::ScenePropertyAssetRefKind::Material:
			return { AshEngine::AssetType::Material };
		case AshEngine::ScenePropertyAssetRefKind::Texture:
			return { AshEngine::AssetType::Texture };
		case AshEngine::ScenePropertyAssetRefKind::IBL:
			return { AshEngine::AssetType::Texture, AshEngine::AssetType::Prefab };
		case AshEngine::ScenePropertyAssetRefKind::None:
		default:
			return {};
		}
	}

	InspectorAssetPathFieldDesc MakeInspectorSceneAssetPathFieldDesc(
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		const char* pPopupId,
		const char* pPickerTitle)
	{
		const AshEngine::ScenePropertyDesc* pPropertyDesc =
			FindScenePropertyDesc(refDesc.eComponentType, refDesc.pPropertyName);
		InspectorAssetPathFieldDesc desc{};
		desc.pLabel = pLabel;
		desc.pPopupId = pPopupId;
		desc.pPickerTitle = pPickerTitle;
		desc.fieldSpec = MakeInspectorFieldSpec(
			pPropertyDesc,
			refDesc.pFallbackTitle,
			refDesc.pFallbackDescription,
			refDesc.strDefaultValue,
			refDesc.strValidRange,
			refDesc.strBehavior);
		desc.browseSpec = {
			pPickerTitle,
			"Open the asset picker and choose a resource from the indexed asset database.",
			{},
			{},
			"Immediate action"
		};
		desc.vecAllowedAssetTypes = MakeInspectorAssetTypesForProperty(pPropertyDesc);
		return desc;
	}

	int32_t FindInspectorEnumIndex(const InspectorEnumOptions& refOptions, const int32_t iEnumValue)
	{
		for (size_t uIndex = 0; uIndex < refOptions.vecValues.size(); ++uIndex)
		{
			if (refOptions.vecValues[uIndex] == iEnumValue)
			{
				return static_cast<int32_t>(uIndex);
			}
		}
		return 0;
	}

	int32_t GetInspectorEnumValueAt(
		const InspectorEnumOptions& refOptions,
		const int32_t iIndex,
		const int32_t iFallbackValue)
	{
		if (iIndex < 0 || static_cast<size_t>(iIndex) >= refOptions.vecValues.size())
		{
			return iFallbackValue;
		}
		return refOptions.vecValues[static_cast<size_t>(iIndex)];
	}

	bool DrawInspectorSceneEnumComboField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		const char* pEnumName,
		int32_t& iEnumValue,
		const InspectorFieldSpec& refFieldSpec,
		const bool bEnabled)
	{
		const InspectorEnumOptions options = MakeInspectorEnumOptions(pEnumName);
		if (options.vecLabels.empty())
		{
			return false;
		}

		int32_t iSelectedIndex = FindInspectorEnumIndex(options, iEnumValue);
		if (!DrawInspectorComboField(refUi, pLabel, iSelectedIndex, options.vecLabels, refFieldSpec, bEnabled))
		{
			return false;
		}

		iEnumValue = GetInspectorEnumValueAt(options, iSelectedIndex, iEnumValue);
		return true;
	}

	bool DrawInspectorSceneBoolField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		bool& bValue,
		const bool bEnabled)
	{
		return DrawInspectorCheckboxField(
			refUi,
			pLabel,
			bValue,
			MakeInspectorSceneFieldSpec(refDesc),
			bEnabled);
	}

	bool DrawInspectorSceneDragFloatField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		float& fValue,
		const float fSpeed,
		const float fMinValue,
		const float fMaxValue,
		const char* pFormat,
		const bool bEnabled)
	{
		return DrawInspectorDragFloatField(
			refUi,
			pLabel,
			fValue,
			fSpeed,
			fMinValue,
			fMaxValue,
			MakeInspectorSceneFieldSpec(refDesc),
			pFormat,
			bEnabled);
	}

	bool DrawInspectorSceneIntField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		int32_t& iValue,
		const int32_t iStep,
		const int32_t iStepFast,
		const bool bEnabled)
	{
		return DrawInspectorInputIntField(
			refUi,
			pLabel,
			iValue,
			MakeInspectorSceneFieldSpec(refDesc),
			iStep,
			iStepFast,
			bEnabled);
	}

	bool DrawInspectorSceneUIntField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		uint32_t& uValue,
		const int32_t iStep,
		const int32_t iStepFast,
		const bool bEnabled)
	{
		int32_t iValue = static_cast<int32_t>(std::min<uint32_t>(
			uValue,
			static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
		if (!DrawInspectorSceneIntField(refUi, refDesc, pLabel, iValue, iStep, iStepFast, bEnabled))
		{
			return false;
		}

		uValue = static_cast<uint32_t>(std::max(0, iValue));
		return true;
	}

	bool DrawInspectorSceneEnumField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		int32_t& iEnumValue,
		const bool bEnabled)
	{
		const AshEngine::ScenePropertyDesc* pPropertyDesc =
			FindScenePropertyDesc(refDesc.eComponentType, refDesc.pPropertyName);
		if (!pPropertyDesc || !pPropertyDesc->enum_name)
		{
			return false;
		}

		return DrawInspectorSceneEnumComboField(
			refUi,
			pLabel,
			pPropertyDesc->enum_name,
			iEnumValue,
			MakeInspectorFieldSpec(
				pPropertyDesc,
				refDesc.pFallbackTitle,
				refDesc.pFallbackDescription,
				refDesc.strDefaultValue,
				refDesc.strValidRange,
				refDesc.strBehavior),
			bEnabled);
	}

	bool DrawInspectorSceneEnumField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		const char* pEnumName,
		int32_t& iEnumValue,
		const bool bEnabled)
	{
		return DrawInspectorSceneEnumComboField(
			refUi,
			pLabel,
			pEnumName,
			iEnumValue,
			MakeInspectorSceneFieldSpec(refDesc),
			bEnabled);
	}

	bool DrawInspectorSceneColor3Field(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		glm::vec3& refValue,
		const bool bEnabled)
	{
		return DrawInspectorColor3Field(
			refUi,
			pLabel,
			refValue,
			MakeInspectorSceneFieldSpec(refDesc),
			bEnabled);
	}
}
