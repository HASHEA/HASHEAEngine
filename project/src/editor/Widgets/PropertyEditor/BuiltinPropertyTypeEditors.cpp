#include "Function/Gui/UIContext.h"
#include "Widgets/InspectorPropertyWidgets.h"
#include "Widgets/PropertyEditor/EnumPropertyTypeEditor.h"
#include "Widgets/PropertyEditor/IPropertyTypeEditor.h"
#include "Widgets/PropertyEditor/PropertyEditorFieldSpec.h"
#include "Widgets/PropertyEditor/PropertyEditorRegistry.h"
#include "Widgets/PropertyEditor/PropertyEditorTypes.h"

#include <algorithm>
#include <cstdint>
#include <limits>
#include <memory>
#include <string>

#include <glm/vec2.hpp>
#include <glm/vec3.hpp>
#include <glm/vec4.hpp>

namespace AshEditor
{
	namespace
	{
		constexpr char kHiddenLabel[] = "##v";

		// Wrap handlers that have no dedicated InspectorPropertyWidgets helper so they still
		// honour read-only state and show the standard field tooltip.
		template<typename DrawFn>
		bool DrawWrapped(AshEngine::UIContext& refUi, const PropertyDesc& refDesc, DrawFn&& refDrawFn)
		{
			const InspectorFieldSpec spec = MakePropertyFieldSpec(refDesc);
			refUi.begin_disabled(refDesc.meta.bReadOnly);
			const bool bChanged = refDrawFn();
			refUi.end_disabled();
			DrawInspectorFieldTooltip(refUi, spec);
			return bChanged;
		}

		class BoolEditor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "bool"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				return DrawInspectorCheckboxField(
					*refCtx.pUi, kHiddenLabel, *static_cast<bool*>(refDesc.pValue),
					MakePropertyFieldSpec(refDesc), !refDesc.meta.bReadOnly);
			}
		};

		class IntEditor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "int"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				return DrawInspectorInputIntField(
					*refCtx.pUi, kHiddenLabel, *static_cast<int32_t*>(refDesc.pValue),
					MakePropertyFieldSpec(refDesc), 1, 100, !refDesc.meta.bReadOnly);
			}
		};

		class UIntEditor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "uint"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				uint32_t* pValue = static_cast<uint32_t*>(refDesc.pValue);
				int32_t iValue = static_cast<int32_t>(std::min<uint32_t>(
					*pValue, static_cast<uint32_t>(std::numeric_limits<int32_t>::max())));
				if (!DrawInspectorInputIntField(
					*refCtx.pUi, kHiddenLabel, iValue,
					MakePropertyFieldSpec(refDesc), 1, 100, !refDesc.meta.bReadOnly))
				{
					return false;
				}
				*pValue = static_cast<uint32_t>(std::max(0, iValue));
				return true;
			}
		};

		class FloatEditor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "float"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				return DrawInspectorDragFloatField(
					*refCtx.pUi, kHiddenLabel, *static_cast<float*>(refDesc.pValue),
					refDesc.meta.fSpeed, refDesc.meta.fMin, refDesc.meta.fMax,
					MakePropertyFieldSpec(refDesc), refDesc.meta.strFormat.c_str(), !refDesc.meta.bReadOnly);
			}
		};

		class Float2Editor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "float2"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				glm::vec2* pValue = static_cast<glm::vec2*>(refDesc.pValue);
				return DrawWrapped(*refCtx.pUi, refDesc, [&]()
				{
					return refCtx.pUi->drag_float2(
						kHiddenLabel, &pValue->x, refDesc.meta.fSpeed,
						refDesc.meta.fMin, refDesc.meta.fMax, refDesc.meta.strFormat.c_str());
				});
			}
		};

		class Float3Editor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "float3"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				return DrawInspectorDragVec3Field(
					*refCtx.pUi, kHiddenLabel, *static_cast<glm::vec3*>(refDesc.pValue),
					refDesc.meta.fSpeed, refDesc.meta.fMin, refDesc.meta.fMax,
					MakePropertyFieldSpec(refDesc), refDesc.meta.strFormat.c_str(), !refDesc.meta.bReadOnly);
			}
		};

		class Float4Editor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "float4"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				glm::vec4* pValue = static_cast<glm::vec4*>(refDesc.pValue);
				return DrawWrapped(*refCtx.pUi, refDesc, [&]()
				{
					return refCtx.pUi->drag_float4(
						kHiddenLabel, &pValue->x, refDesc.meta.fSpeed,
						refDesc.meta.fMin, refDesc.meta.fMax, refDesc.meta.strFormat.c_str());
				});
			}
		};

		class Color3Editor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "color3"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				return DrawInspectorColor3Field(
					*refCtx.pUi, kHiddenLabel, *static_cast<glm::vec3*>(refDesc.pValue),
					MakePropertyFieldSpec(refDesc), !refDesc.meta.bReadOnly);
			}
		};

		class Color4Editor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "color4"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				glm::vec4* pValue = static_cast<glm::vec4*>(refDesc.pValue);
				return DrawWrapped(*refCtx.pUi, refDesc, [&]()
				{
					return refCtx.pUi->color_edit4(kHiddenLabel, &pValue->x);
				});
			}
		};

		class StringEditor final : public IPropertyTypeEditor
		{
		public:
			const char* GetTypeId() const override { return "string"; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				return DrawInspectorTextField(
					*refCtx.pUi, kHiddenLabel, *static_cast<std::string*>(refDesc.pValue),
					MakePropertyFieldSpec(refDesc), AshEngine::UIInputTextFlagBits::None, !refDesc.meta.bReadOnly);
			}
		};

		// v1 fallback: asset references are edited as an explicit path string until the asset
		// picker service is wired into PropertyEditorContext.
		class AssetPathEditor final : public IPropertyTypeEditor
		{
		public:
			explicit AssetPathEditor(const char* pTypeId) : _pTypeId(pTypeId) {}
			const char* GetTypeId() const override { return _pTypeId; }
			bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override
			{
				return DrawInspectorTextField(
					*refCtx.pUi, kHiddenLabel, *static_cast<std::string*>(refDesc.pValue),
					MakePropertyFieldSpec(refDesc), AshEngine::UIInputTextFlagBits::None, !refDesc.meta.bReadOnly);
			}

		private:
			const char* _pTypeId = nullptr;
		};
	}

	void RegisterDefaultPropertyTypeEditors(PropertyEditorRegistry& refRegistry)
	{
		refRegistry.Register(std::make_unique<BoolEditor>());
		refRegistry.Register(std::make_unique<IntEditor>());
		refRegistry.Register(std::make_unique<UIntEditor>());
		refRegistry.Register(std::make_unique<FloatEditor>());
		refRegistry.Register(std::make_unique<Float2Editor>());
		refRegistry.Register(std::make_unique<Float3Editor>());
		refRegistry.Register(std::make_unique<Float4Editor>());
		refRegistry.Register(std::make_unique<Color3Editor>());
		refRegistry.Register(std::make_unique<Color4Editor>());
		refRegistry.Register(std::make_unique<StringEditor>());
		refRegistry.Register(std::make_unique<AssetPathEditor>("asset_path"));
		refRegistry.Register(std::make_unique<AssetPathEditor>("texture_preview"));
		refRegistry.Register(CreateEnumPropertyTypeEditor());
	}
}
