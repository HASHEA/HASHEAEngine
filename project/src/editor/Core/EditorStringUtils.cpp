#include "Core/EditorStringUtils.h"

#include <algorithm>
#include <cctype>

namespace AshEditor
{
	std::string ToLowerCopy(std::string strValue)
	{
		std::transform(
			strValue.begin(),
			strValue.end(),
			strValue.begin(),
			[](unsigned char uChar) { return static_cast<char>(std::tolower(uChar)); });
		return strValue;
	}
}

