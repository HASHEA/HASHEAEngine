#pragma once

namespace AshEditor
{
	struct PropertyDesc;
	struct PropertyEditorContext;

	// Draws the control for one property type. One instance is shared across all fields
	// of the same typeId, so handlers must stay stateless and read everything from refDesc.
	class IPropertyTypeEditor
	{
	public:
		virtual ~IPropertyTypeEditor() = default;

	public:
		virtual const char* GetTypeId() const = 0;
		// Returns true when the underlying value changed this frame.
		virtual bool DrawEditor(const PropertyDesc& refDesc, const PropertyEditorContext& refCtx) = 0;
	};
}
