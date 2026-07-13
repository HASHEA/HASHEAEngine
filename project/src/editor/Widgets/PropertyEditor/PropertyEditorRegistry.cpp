#include "Widgets/PropertyEditor/PropertyEditorRegistry.h"

#include "Widgets/PropertyEditor/IPropertyTypeEditor.h"

#include <string>

namespace AshEditor
{
	namespace
	{
		constexpr std::string_view kEnumPrefix = "enum:";
		constexpr char kEnumHandlerKey[] = "enum";
	}

	PropertyEditorRegistry::PropertyEditorRegistry() = default;
	PropertyEditorRegistry::~PropertyEditorRegistry() = default;

	void PropertyEditorRegistry::Register(std::unique_ptr<IPropertyTypeEditor> upEditor)
	{
		if (!upEditor)
		{
			return;
		}
		const char* pTypeId = upEditor->GetTypeId();
		if (!pTypeId || pTypeId[0] == '\0')
		{
			return;
		}
		_mapById[std::string(pTypeId)] = upEditor.get();
		_vecEditors.push_back(std::move(upEditor));
	}

	IPropertyTypeEditor* PropertyEditorRegistry::Find(std::string_view svTypeId) const
	{
		// Any "enum:<Name>" routes to the single shared enum handler.
		std::string strKey;
		if (svTypeId.size() >= kEnumPrefix.size() &&
			svTypeId.substr(0, kEnumPrefix.size()) == kEnumPrefix)
		{
			strKey = kEnumHandlerKey;
		}
		else
		{
			strKey.assign(svTypeId);
		}

		std::unordered_map<std::string, IPropertyTypeEditor*>::const_iterator it =
			_mapById.find(strKey);
		if (it == _mapById.end())
		{
			return nullptr;
		}
		return it->second;
	}
}
