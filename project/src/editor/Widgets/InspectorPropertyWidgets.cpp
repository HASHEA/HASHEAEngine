#include "Widgets/InspectorPropertyWidgets.h"

#include "Function/Gui/UIContext.h"
#include "Widgets/EditorTooltipWidgets.h"

#include <string>

namespace AshEditor
{
	namespace
	{
		constexpr AshEngine::UITooltipConfig kInspectorFieldTooltipConfig{
			{ 360.0f, 0.0f },
			{ 260.0f, 0.0f },
			{ 520.0f, 0.0f },
			AshEngine::UIConditionFlagBits::Always,
			0.0f,
			AshEngine::UIWindowFlagBits::None
		};

		template<typename DrawFn>
		bool DrawInspectorFieldControl(
			AshEngine::UIContext& refUi,
			const InspectorFieldSpec& refFieldSpec,
			const bool bEnabled,
			DrawFn&& refDrawFn)
		{
			// Keep the reusable field layer UI-only so panels can decide when a changed value should commit.
			refUi.begin_disabled(!bEnabled);
			const bool bChanged = refDrawFn();
			refUi.end_disabled();
			DrawInspectorFieldTooltip(refUi, refFieldSpec);
			return bChanged;
		}
	}

	void DrawInspectorFieldTooltip(
		AshEngine::UIContext& refUi,
		const InspectorFieldSpec& refFieldSpec)
	{
		if (!refUi.is_item_hovered())
		{
			return;
		}

		refUi.begin_tooltip(kInspectorFieldTooltipConfig);
		DrawEditorTooltipCompactTitle(
			refUi,
			refFieldSpec.pTitle ? refFieldSpec.pTitle : "Field",
			"Inspector Field");
		if (refFieldSpec.pDescription && refFieldSpec.pDescription[0] != '\0')
		{
			DrawEditorTooltipDescription(refUi, refFieldSpec.pDescription);
		}

		const std::string strTableId = std::string("InspectorFieldTooltip##") +
			(refFieldSpec.pTitle ? refFieldSpec.pTitle : "Field");
		if (BeginEditorTooltipTable(refUi, strTableId.c_str(), 92.0f))
		{
			if (!refFieldSpec.strDefaultValue.empty())
			{
				DrawEditorTooltipRow(refUi, "Default", refFieldSpec.strDefaultValue);
			}
			if (!refFieldSpec.strValidRange.empty())
			{
				DrawEditorTooltipRow(refUi, "Range", refFieldSpec.strValidRange);
			}
			if (!refFieldSpec.strBehavior.empty())
			{
				DrawEditorTooltipRow(refUi, "Behavior", refFieldSpec.strBehavior);
			}
			refUi.end_table();
		}

		refUi.end_tooltip();
	}

	bool DrawInspectorTextField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		std::string& strValue,
		const InspectorFieldSpec& refFieldSpec,
		const AshEngine::UIInputTextFlags flags,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &strValue, flags]()
			{
				return refUi.input_text(pLabel, strValue, flags);
			});
	}

	bool DrawInspectorCheckboxField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		bool& bValue,
		const InspectorFieldSpec& refFieldSpec,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &bValue]()
			{
				return refUi.checkbox(pLabel, bValue);
			});
	}

	bool DrawInspectorInputIntField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		int32_t& iValue,
		const InspectorFieldSpec& refFieldSpec,
		const int32_t iStep,
		const int32_t iStepFast,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &iValue, iStep, iStepFast]()
			{
				return refUi.input_int(pLabel, iValue, iStep, iStepFast);
			});
	}

	bool DrawInspectorInputUIntField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		uint32_t& uValue,
		const InspectorFieldSpec& refFieldSpec,
		const uint32_t uStep,
		const uint32_t uStepFast,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &uValue, uStep, uStepFast]()
			{
				return refUi.input_uint(pLabel, uValue, uStep, uStepFast);
			});
	}

	bool DrawInspectorInputFloatField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		float& fValue,
		const InspectorFieldSpec& refFieldSpec,
		const float fStep,
		const float fStepFast,
		const char* pFormat,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &fValue, fStep, fStepFast, pFormat]()
			{
				return refUi.input_float(pLabel, fValue, fStep, fStepFast, pFormat);
			});
	}

	bool DrawInspectorDragFloatField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		float& fValue,
		const float fSpeed,
		const float fMinValue,
		const float fMaxValue,
		const InspectorFieldSpec& refFieldSpec,
		const char* pFormat,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &fValue, fSpeed, fMinValue, fMaxValue, pFormat]()
			{
				return refUi.drag_float(pLabel, fValue, fSpeed, fMinValue, fMaxValue, pFormat);
			});
	}

	bool DrawInspectorDragVec3Field(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		glm::vec3& refValue,
		const float fSpeed,
		const float fMinValue,
		const float fMaxValue,
		const InspectorFieldSpec& refFieldSpec,
		const char* pFormat,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &refValue, fSpeed, fMinValue, fMaxValue, pFormat]()
			{
				return refUi.drag_float3(pLabel, &refValue.x, fSpeed, fMinValue, fMaxValue, pFormat);
			});
	}

	bool DrawInspectorColor3Field(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		glm::vec3& refValue,
		const InspectorFieldSpec& refFieldSpec,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &refValue]()
			{
				return refUi.color_edit3(pLabel, &refValue.x);
			});
	}

	bool DrawInspectorColor4Field(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		glm::vec4& refValue,
		const InspectorFieldSpec& refFieldSpec,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &refValue]()
			{
				return refUi.color_edit4(pLabel, &refValue.x);
			});
	}

	bool DrawInspectorComboField(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		int32_t& iCurrentIndex,
		const std::vector<const char*>& vecItems,
		const InspectorFieldSpec& refFieldSpec,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, &iCurrentIndex, &vecItems]()
			{
				return refUi.combo(pLabel, iCurrentIndex, vecItems);
			});
	}

	bool DrawInspectorActionButton(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		const InspectorFieldSpec& refFieldSpec,
		const AshEngine::UIVec2& size,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel, size]()
			{
				return refUi.button(pLabel, size);
			});
	}

	bool DrawInspectorSmallActionButton(
		AshEngine::UIContext& refUi,
		const char* pLabel,
		const InspectorFieldSpec& refFieldSpec,
		const bool bEnabled)
	{
		return DrawInspectorFieldControl(
			refUi,
			refFieldSpec,
			bEnabled,
			[&refUi, pLabel]()
			{
				return refUi.small_button(pLabel);
			});
	}
}
