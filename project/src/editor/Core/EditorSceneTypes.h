#pragma once

#include <cstdint>
#include <limits>

namespace AshEditor
{
	using SceneEntityId = uint64_t;
	inline constexpr uint32_t kSceneAppendSiblingIndex = std::numeric_limits<uint32_t>::max();
}
