#pragma once

#include "Function/Gui/UICommon.h"
#include <cstdint>

namespace AshEngine
{
	struct SceneOutputHandle
	{
		uint32_t value = 0;

		bool is_valid() const
		{
			return value != 0;
		}
	};

	struct SceneViewBindingHandle
	{
		uint32_t value = 0;

		bool is_valid() const
		{
			return value != 0;
		}
	};
}
