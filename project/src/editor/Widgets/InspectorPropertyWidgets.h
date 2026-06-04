#pragma once

#include "Function/Gui/UICommon.h"

#include <cstdint>
#include <string>
#include <vector>

#include <glm/vec3.hpp>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	struct InspectorFieldSpec
	{
		const char* pTitle = nullptr;
		const char* pDescription = nullptr;
		std::string strDefaultValue{};
		std::string strValidRange{};
		std::string strBehavior{};
	};

	// Shared Inspector field helpers. They only draw controls/tooltips; business validation and commit stay above.
	void DrawInspectorFieldTooltip(
		AshEngine::UIContext& refUi,
		const InspectorFieldSpec& refFieldSpec);

	bool DrawInspectorTextField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		std::string& strValue,
		const InspectorFieldSpec& refFieldSpec,
		AshEngine::UIInputTextFlags flags = AshEngine::UIInputTextFlagBits::None,
		bool bEnabled = true);

	bool DrawInspectorCheckboxField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		bool& bValue,
		const InspectorFieldSpec& refFieldSpec,
		bool bEnabled = true);

	bool DrawInspectorInputIntField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		int32_t& iValue,
		const InspectorFieldSpec& refFieldSpec,
		int32_t iStep = 1,
		int32_t iStepFast = 100,
		bool bEnabled = true);

	bool DrawInspectorInputFloatField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		float& fValue,
		const InspectorFieldSpec& refFieldSpec,
		float fStep = 0.0f,
		float fStepFast = 0.0f,
		const char* pFormat = "%.3f",
		bool bEnabled = true);

	bool DrawInspectorDragFloatField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		float& fValue,
		float fSpeed,
		float fMinValue,
		float fMaxValue,
		const InspectorFieldSpec& refFieldSpec,
		const char* pFormat = "%.3f",
		bool bEnabled = true);

	bool DrawInspectorDragVec3Field(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		glm::vec3& refValue,
		float fSpeed,
		float fMinValue,
		float fMaxValue,
		const InspectorFieldSpec& refFieldSpec,
		const char* pFormat = "%.3f",
		bool bEnabled = true);

	bool DrawInspectorColor3Field(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		glm::vec3& refValue,
		const InspectorFieldSpec& refFieldSpec,
		bool bEnabled = true);

	bool DrawInspectorComboField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		int32_t& iCurrentIndex,
		const std::vector<const char*>& vecItems,
		const InspectorFieldSpec& refFieldSpec,
		bool bEnabled = true);

	bool DrawInspectorActionButton(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		const InspectorFieldSpec& refFieldSpec,
		const AshEngine::UIVec2& size = {},
		bool bEnabled = true);

	bool DrawInspectorSmallActionButton(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		const InspectorFieldSpec& refFieldSpec,
		bool bEnabled = true);
}
