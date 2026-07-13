#pragma once

#include "Widgets/InspectorPropertyWidgets.h"
#include "Widgets/PropertyEditor/PropertyEditorTypes.h"

namespace AshEditor
{
	// Builds an InspectorFieldSpec view over a PropertyDesc. The returned spec borrows the
	// desc's strings, so it must only be used while refDesc stays alive (i.e. within DrawEditor).
	inline InspectorFieldSpec MakePropertyFieldSpec(const PropertyDesc& refDesc)
	{
		InspectorFieldSpec spec{};
		spec.pTitle = refDesc.strLabel.c_str();
		spec.pDescription = refDesc.meta.strTooltip.empty() ? nullptr : refDesc.meta.strTooltip.c_str();
		spec.strDefaultValue = refDesc.meta.strDefaultValue;
		spec.strValidRange = refDesc.meta.strValidRange;
		spec.strBehavior = refDesc.meta.strBehavior;
		return spec;
	}
}
