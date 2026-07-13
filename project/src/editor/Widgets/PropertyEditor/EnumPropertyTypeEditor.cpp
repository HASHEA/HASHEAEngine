#include "Widgets/PropertyEditor/EnumPropertyTypeEditor.h"

#include "Function/Gui/UICommon.h"
#include "Function/Gui/UIContext.h"
#include "Function/Scene/Scene.h"
#include "Function/Scene/SceneComponents.h"
#include "Widgets/InspectorPropertyWidgets.h"
#include "Widgets/PropertyEditor/EnumTypeRegistry.h"
#include "Widgets/PropertyEditor/PropertyEditorFieldSpec.h"
#include "Widgets/PropertyEditor/PropertyEditorTypes.h"

#include <cstdint>
#include <string>
#include <string_view>
#include <vector>

namespace AshEditor
{
	namespace
	{
		constexpr std::string_view kEnumPrefix = "enum:";

		struct EnumOptionSet
		{
			std::vector<std::string> vecLabelStorage{};
			std::vector<const char*> vecLabels{};
			std::vector<int32_t> vecValues{};
		};

		bool CollectFromEngine(const std::string& strTypeName, EnumOptionSet& refOut)
		{
			const AshEngine::SceneEnumDesc* pEnumDesc =
				AshEngine::get_scene_enum_descriptor(strTypeName.c_str());
			if (!pEnumDesc || !pEnumDesc->values)
			{
				return false;
			}

			for (uint32_t uIndex = 0; uIndex < pEnumDesc->value_count; ++uIndex)
			{
				const AshEngine::SceneEnumValueDesc& refValue = pEnumDesc->values[uIndex];
				refOut.vecLabelStorage.emplace_back(refValue.name ? refValue.name : "Unknown");
				refOut.vecValues.push_back(refValue.value);
			}
			return !refOut.vecValues.empty();
		}

		bool CollectFromRegistry(const std::string& strTypeName, EnumOptionSet& refOut)
		{
			const EnumTypeDesc* pDesc = EnumTypeRegistry::Instance().Find(strTypeName);
			if (!pDesc)
			{
				return false;
			}

			for (const EnumValueDesc& refValue : pDesc->vecValues)
			{
				refOut.vecLabelStorage.push_back(refValue.strName);
				refOut.vecValues.push_back(refValue.iValue);
			}
			return !refOut.vecValues.empty();
		}

		int32_t FindValueIndex(const std::vector<int32_t>& vecValues, const int32_t iValue)
		{
			for (size_t uIndex = 0; uIndex < vecValues.size(); ++uIndex)
			{
				if (vecValues[uIndex] == iValue)
				{
					return static_cast<int32_t>(uIndex);
				}
			}
			return 0;
		}
	}

	const char* EnumPropertyTypeEditor::GetTypeId() const
	{
		return "enum";
	}

	bool EnumPropertyTypeEditor::DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx)
	{
		if (!refCtx.pUi || !refDesc.pValue)
		{
			return false;
		}
		AshEngine::UIContext& refUi = *refCtx.pUi;

		std::string strTypeName{};
		if (refDesc.strTypeId.size() > kEnumPrefix.size() &&
			refDesc.strTypeId.compare(0, kEnumPrefix.size(), kEnumPrefix.data(), kEnumPrefix.size()) == 0)
		{
			strTypeName = refDesc.strTypeId.substr(kEnumPrefix.size());
		}

		EnumOptionSet options{};
		if (strTypeName.empty() ||
			(!CollectFromEngine(strTypeName, options) && !CollectFromRegistry(strTypeName, options)))
		{
			refUi.text_colored(
				AshEngine::UIColor{ 1.0f, 0.35f, 0.35f, 1.0f },
				"<unknown enum: %s>",
				strTypeName.empty() ? refDesc.strTypeId.c_str() : strTypeName.c_str());
			return false;
		}

		options.vecLabels.reserve(options.vecLabelStorage.size());
		for (const std::string& refLabel : options.vecLabelStorage)
		{
			options.vecLabels.push_back(refLabel.c_str());
		}

		int32_t* pValue = static_cast<int32_t*>(refDesc.pValue);
		int32_t iSelectedIndex = FindValueIndex(options.vecValues, *pValue);

		const InspectorFieldSpec spec = MakePropertyFieldSpec(refDesc);
		if (!DrawInspectorComboField(refUi, "##enum", iSelectedIndex, options.vecLabels, spec, !refDesc.meta.bReadOnly))
		{
			return false;
		}

		if (iSelectedIndex >= 0 && static_cast<size_t>(iSelectedIndex) < options.vecValues.size())
		{
			*pValue = options.vecValues[static_cast<size_t>(iSelectedIndex)];
		}
		return true;
	}

	std::unique_ptr<IPropertyTypeEditor> CreateEnumPropertyTypeEditor()
	{
		return std::make_unique<EnumPropertyTypeEditor>();
	}
}
