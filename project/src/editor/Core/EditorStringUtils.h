#pragma once

#include <string>

namespace AshEditor
{
	// Returns a lowercase copy of the input string using the C locale rules.
	// Intended for UI filtering/sorting where case-insensitive matches are desirable.
	std::string ToLowerCopy(std::string strValue);
}

