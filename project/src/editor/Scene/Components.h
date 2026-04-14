#pragma once
#include <array>
#include <string>

namespace AshEditor
{
	struct NameComponent
	{
		std::string name = "Entity";
	};

	struct TransformComponent
	{
		std::array<float, 3> position{ 0.0f, 0.0f, 0.0f };
		std::array<float, 3> rotation{ 0.0f, 0.0f, 0.0f };
		std::array<float, 3> scale{ 1.0f, 1.0f, 1.0f };
	};
}
