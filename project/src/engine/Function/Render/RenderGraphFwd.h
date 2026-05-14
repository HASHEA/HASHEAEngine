#pragma once

#include "Base/hcore.h"
#include <cstdint>

namespace AshEngine
{
	class RenderGraphBuilder;
	class RenderGraphRasterPassBuilder;
	class RenderGraphComputePassBuilder;
	class RenderGraphRasterContext;
	class RenderGraphComputeContext;

	struct RenderGraphTextureRef
	{
		uint32_t index = UINT32_MAX;

		bool is_valid() const
		{
			return index != UINT32_MAX;
		}

		explicit operator bool() const
		{
			return is_valid();
		}
	};

	inline bool operator==(RenderGraphTextureRef lhs, RenderGraphTextureRef rhs)
	{
		return lhs.index == rhs.index;
	}

	inline bool operator!=(RenderGraphTextureRef lhs, RenderGraphTextureRef rhs)
	{
		return !(lhs == rhs);
	}
}
