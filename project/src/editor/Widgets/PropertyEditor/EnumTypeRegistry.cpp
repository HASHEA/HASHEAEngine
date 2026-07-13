#include "Widgets/PropertyEditor/EnumTypeRegistry.h"

#include <utility>

namespace AshEditor
{
	void EnumTypeRegistry::Register(EnumTypeDesc desc)
	{
		if (desc.strTypeName.empty())
		{
			return;
		}
		std::string strKey = desc.strTypeName;
		_mapEnums[std::move(strKey)] = std::move(desc);
	}

	const EnumTypeDesc* EnumTypeRegistry::Find(std::string_view svTypeName) const
	{
		std::unordered_map<std::string, EnumTypeDesc>::const_iterator it =
			_mapEnums.find(std::string(svTypeName));
		if (it == _mapEnums.end())
		{
			return nullptr;
		}
		return &it->second;
	}

	EnumTypeRegistry& EnumTypeRegistry::Instance()
	{
		static EnumTypeRegistry s_registry{};
		return s_registry;
	}
}
