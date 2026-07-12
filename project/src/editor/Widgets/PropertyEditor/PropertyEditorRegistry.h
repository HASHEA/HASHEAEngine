#pragma once

#include <memory>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AshEditor
{
	class IPropertyTypeEditor;

	// Owns the set of property type editors and resolves a typeId to its handler.
	// Mirrors InspectorComponentEditorRegistry: unique_ptr ownership + free-function defaults.
	// The "enum:" prefix is dispatched to a single shared enum handler that parses the
	// type name after the colon, so any number of enums share one registration.
	class PropertyEditorRegistry final
	{
	public:
		PropertyEditorRegistry();
		~PropertyEditorRegistry();

	public:
		void Register(std::unique_ptr<IPropertyTypeEditor> upEditor);
		IPropertyTypeEditor* Find(std::string_view svTypeId) const;

	private:
		std::vector<std::unique_ptr<IPropertyTypeEditor>> _vecEditors{};
		std::unordered_map<std::string, IPropertyTypeEditor*> _mapById{};
	};

	void RegisterDefaultPropertyTypeEditors(PropertyEditorRegistry& refRegistry);
}
