#include "Panels/PropertyEditorDemoPanel.h"

#include "Core/EditorIds.h"
#include "Function/Gui/UIContext.h"
#include "Widgets/PropertyEditor/EnumTypeRegistry.h"
#include "Widgets/PropertyEditor/PropertyEditorTypes.h"

#include <vector>

namespace AshEditor
{
	namespace
	{
		void EnsureDemoEnumRegistered()
		{
			// Registered once; proves a material-editor-only enum needs no engine/core changes.
			if (EnumTypeRegistry::Instance().Find("DemoBlendMode"))
			{
				return;
			}
			EnumTypeDesc desc{};
			desc.strTypeName = "DemoBlendMode";
			desc.vecValues = {
				{ "Opaque", 0 },
				{ "Masked", 1 },
				{ "Transparent", 2 },
			};
			EnumTypeRegistry::Instance().Register(std::move(desc));
		}
	}

	PropertyEditorDemoPanel::PropertyEditorDemoPanel()
		: EditorPanel(EditorPanelIds::PropertyEditorDemo, EditorWindowTitles::PropertyEditorDemo)
		, _widget(_registry)
	{
		RegisterDefaultPropertyTypeEditors(_registry);
		EnsureDemoEnumRegistered();
		SetOpen(false);
	}

	void PropertyEditorDemoPanel::OnGui(const EditorFrameContext& refFrameContext)
	{
		if (!BeginPanelWindow(refFrameContext))
		{
			EndPanelWindow(refFrameContext);
			return;
		}
		if (!refFrameContext.pUiContext)
		{
			EndPanelWindow(refFrameContext);
			return;
		}

		AshEngine::UIContext& refUi = *refFrameContext.pUiContext;
		refUi.text("Generic Property Editor showcase");
		refUi.separator();

		PropertyEditorContext ctx{};
		ctx.pUi = &refUi;

		std::vector<PropertyDesc> vecDescs{};

		PropertyDesc descBool{};
		descBool.strLabel = "Bool";
		descBool.strTypeId = "bool";
		descBool.pValue = &_bSample;
		descBool.meta.strTooltip = "A boolean toggle.";
		vecDescs.push_back(descBool);

		PropertyDesc descInt{};
		descInt.strLabel = "Int";
		descInt.strTypeId = "int";
		descInt.pValue = &_iSample;
		vecDescs.push_back(descInt);

		PropertyDesc descUInt{};
		descUInt.strLabel = "UInt";
		descUInt.strTypeId = "uint";
		descUInt.pValue = &_uSample;
		vecDescs.push_back(descUInt);

		PropertyDesc descFloat{};
		descFloat.strLabel = "Float";
		descFloat.strTypeId = "float";
		descFloat.pValue = &_fSample;
		descFloat.meta.fSpeed = 0.01f;
		descFloat.meta.fMin = 0.0f;
		descFloat.meta.fMax = 1.0f;
		descFloat.meta.strValidRange = "[0, 1]";
		vecDescs.push_back(descFloat);

		PropertyDesc descFloat2{};
		descFloat2.strLabel = "Float2";
		descFloat2.strTypeId = "float2";
		descFloat2.pValue = &_v2Sample;
		vecDescs.push_back(descFloat2);

		PropertyDesc descFloat3{};
		descFloat3.strLabel = "Float3";
		descFloat3.strTypeId = "float3";
		descFloat3.pValue = &_v3Sample;
		vecDescs.push_back(descFloat3);

		PropertyDesc descFloat4{};
		descFloat4.strLabel = "Float4";
		descFloat4.strTypeId = "float4";
		descFloat4.pValue = &_v4Sample;
		vecDescs.push_back(descFloat4);

		PropertyDesc descColor3{};
		descColor3.strLabel = "Color3";
		descColor3.strTypeId = "color3";
		descColor3.pValue = &_color3Sample;
		vecDescs.push_back(descColor3);

		PropertyDesc descColor4{};
		descColor4.strLabel = "Color4";
		descColor4.strTypeId = "color4";
		descColor4.pValue = &_color4Sample;
		vecDescs.push_back(descColor4);

		PropertyDesc descString{};
		descString.strLabel = "String";
		descString.strTypeId = "string";
		descString.pValue = &_strSample;
		vecDescs.push_back(descString);

		PropertyDesc descAsset{};
		descAsset.strLabel = "Asset Path";
		descAsset.strTypeId = "asset_path";
		descAsset.pValue = &_strAssetPath;
		vecDescs.push_back(descAsset);

		PropertyDesc descEngineEnum{};
		descEngineEnum.strLabel = "Engine Enum";
		descEngineEnum.strTypeId = "enum:LightType";
		descEngineEnum.pValue = &_iEngineEnum;
		descEngineEnum.meta.strTooltip = "Resolved from engine scene reflection.";
		vecDescs.push_back(descEngineEnum);

		PropertyDesc descCustomEnum{};
		descCustomEnum.strLabel = "Custom Enum";
		descCustomEnum.strTypeId = "enum:DemoBlendMode";
		descCustomEnum.pValue = &_iCustomEnum;
		descCustomEnum.meta.strTooltip = "Resolved from EnumTypeRegistry (no core change).";
		vecDescs.push_back(descCustomEnum);

		PropertyDesc descUnknown{};
		descUnknown.strLabel = "Unknown";
		descUnknown.strTypeId = "definitely_not_registered";
		descUnknown.pValue = nullptr;
		vecDescs.push_back(descUnknown);

		_widget.DrawFields(vecDescs, ctx);

		EndPanelWindow(refFrameContext);
	}
}
