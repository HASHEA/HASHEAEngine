#pragma once

#include <vector>

namespace AshEditor
{
	class PropertyEditorRegistry;
	struct PropertyDesc;
	struct PropertyEditorContext;

	// Front door for drawing type-erased properties. Resolves each PropertyDesc through the
	// registry and renders it. Layout is a two-column table (name | value); unknown typeIds
	// render a red placeholder instead of crashing.
	class PropertyEditorWidget final
	{
	public:
		explicit PropertyEditorWidget(PropertyEditorRegistry& refRegistry);

	public:
		// Draws just the control for one property (no surrounding layout). Returns true if changed.
		bool DrawField(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx);
		// Draws a table of properties. Returns true if any field changed this frame.
		bool DrawFields(const std::vector<PropertyDesc>& refDescs, const PropertyEditorContext& refCtx);

	private:
		PropertyEditorRegistry* _pRegistry = nullptr;
	};
}
