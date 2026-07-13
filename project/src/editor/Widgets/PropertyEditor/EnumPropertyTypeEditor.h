#pragma once

#include "Widgets/PropertyEditor/IPropertyTypeEditor.h"

#include <memory>

namespace AshEditor
{
	// Shared handler for every "enum:<TypeName>" property. It resolves candidate values
	// from the engine scene enum reflection first, then the editor-side EnumTypeRegistry.
	// The bound value is treated as int32_t*.
	class EnumPropertyTypeEditor final : public IPropertyTypeEditor
	{
	public:
		const char* GetTypeId() const override;
		bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) override;
	};

	std::unique_ptr<IPropertyTypeEditor> CreateEnumPropertyTypeEditor();
}
