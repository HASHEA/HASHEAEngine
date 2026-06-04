#pragma once

#include "Function/Scene/SceneComponents.h"
#include "Widgets/InspectorAssetPathWidgets.h"
#include "Widgets/InspectorPropertyWidgets.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace AshEngine
{
	enum class AssetType : uint8_t;
	class UIContext;
}

namespace AshEditor
{
	struct InspectorEnumOptions
	{
		std::vector<const char*> vecLabels{};
		std::vector<int32_t> vecValues{};
	};

	struct InspectorSceneFieldDesc
	{
		AshEngine::SceneComponentType eComponentType = AshEngine::SceneComponentType::Name;
		const char* pPropertyName = nullptr;
		const char* pFallbackTitle = nullptr;
		const char* pFallbackDescription = nullptr;
		std::string strDefaultValue{};
		std::string strValidRange{};
		std::string strBehavior{};
	};

	InspectorSceneFieldDesc MakeInspectorSceneFieldDesc(
		AshEngine::SceneComponentType eComponentType,
		const char* pPropertyName,
		const char* pFallbackTitle,
		const char* pFallbackDescription,
		std::string strDefaultValue = {},
		std::string strValidRange = {},
		std::string strBehavior = {});
	const AshEngine::ScenePropertyDesc* FindScenePropertyDesc(
		AshEngine::SceneComponentType eComponentType,
		const char* pPropertyName);

	const char* GetScenePropertyDisplayName(
		const AshEngine::ScenePropertyDesc* pPropertyDesc,
		const char* pFallbackName);

	InspectorFieldSpec MakeInspectorFieldSpec(
		const AshEngine::ScenePropertyDesc* pPropertyDesc,
		const char* pFallbackTitle,
		const char* pFallbackDescription,
		std::string strDefaultValue = {},
		std::string strValidRange = {},
		std::string strBehavior = {});
	InspectorFieldSpec MakeInspectorSceneFieldSpec(const InspectorSceneFieldDesc& refDesc);
	std::vector<AshEngine::AssetType> MakeInspectorAssetTypesForProperty(
		const AshEngine::ScenePropertyDesc* pPropertyDesc);
	InspectorAssetPathFieldDesc MakeInspectorSceneAssetPathFieldDesc(
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		const char* pPopupId,
		const char* pPickerTitle);

	InspectorEnumOptions MakeInspectorEnumOptions(const char* pEnumName);
	int32_t FindInspectorEnumIndex(const InspectorEnumOptions& refOptions, int32_t iEnumValue);
	int32_t GetInspectorEnumValueAt(const InspectorEnumOptions& refOptions, int32_t iIndex, int32_t iFallbackValue);
	bool DrawInspectorSceneEnumComboField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		const char* pEnumName,
		int32_t& iEnumValue,
		const InspectorFieldSpec& refFieldSpec,
		bool bEnabled = true);
	bool DrawInspectorSceneBoolField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		bool& bValue,
		bool bEnabled = true);
	bool DrawInspectorSceneDragFloatField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		float& fValue,
		float fSpeed,
		float fMinValue,
		float fMaxValue,
		const char* pFormat = "%.3f",
		bool bEnabled = true);
	bool DrawInspectorSceneIntField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		int32_t& iValue,
		int32_t iStep = 1,
		int32_t iStepFast = 100,
		bool bEnabled = true);
	bool DrawInspectorSceneUIntField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		uint32_t& uValue,
		int32_t iStep = 1,
		int32_t iStepFast = 100,
		bool bEnabled = true);
	bool DrawInspectorSceneEnumField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		int32_t& iEnumValue,
		bool bEnabled = true);
	bool DrawInspectorSceneEnumField(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		const char* pEnumName,
		int32_t& iEnumValue,
		bool bEnabled = true);
	bool DrawInspectorSceneColor3Field(
		AshEngine::UIContext& refUi,
		const InspectorSceneFieldDesc& refDesc,
		const char* pLabel,
		glm::vec3& refValue,
		bool bEnabled = true);
}
