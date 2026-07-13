#pragma once

#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AshEditor
{
	struct EnumValueDesc
	{
		std::string strName{};
		int32_t iValue = 0;
	};

	struct EnumTypeDesc
	{
		std::string strTypeName{};
		std::vector<EnumValueDesc> vecValues{};
	};

	// Registry for enums that are not described by the engine scene reflection.
	// The enum property handler queries the engine descriptor first, then this registry,
	// so material-editor-only enums can be added without touching engine code.
	class EnumTypeRegistry final
	{
	public:
		void Register(EnumTypeDesc desc);
		const EnumTypeDesc* Find(std::string_view svTypeName) const;

		static EnumTypeRegistry& Instance();

	private:
		std::unordered_map<std::string, EnumTypeDesc> _mapEnums{};
	};
}
