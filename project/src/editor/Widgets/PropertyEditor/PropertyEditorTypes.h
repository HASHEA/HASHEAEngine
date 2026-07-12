#pragma once

#include <string>

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	// Per-frame state handed to every property type editor. Kept as a flat aggregate so
	// callers can construct it inline. Optional host service hooks can be added later.
	struct PropertyEditorContext
	{
		AshEngine::UIContext* pUi = nullptr;
	};

	// UI hints carried alongside a property. These mirror InspectorFieldSpec semantics
	// (tooltip / default / range / behavior) plus widget tuning used by the builtin handlers.
	struct PropertyFieldMeta
	{
		std::string strTooltip{};
		std::string strDefaultValue{};
		std::string strValidRange{};
		std::string strBehavior{};
		bool bReadOnly = false;
		float fSpeed = 0.01f;
		float fMin = 0.0f;
		float fMax = 0.0f;
		std::string strFormat = "%.3f";
	};

	// Type-erased description of a single editable property.
	// strTypeId selects the handler ("float", "color3", "enum:EShadingModel", ...).
	// pValue points at the concrete value whose type the handler agrees on by convention.
	struct PropertyDesc
	{
		std::string strLabel{};
		std::string strTypeId{};
		void* pValue = nullptr;
		PropertyFieldMeta meta{};
	};
}
