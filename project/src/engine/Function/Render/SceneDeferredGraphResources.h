#pragma once

#include "Function/Render/RenderGraphFwd.h"
#include <vector>

namespace AshEngine
{
	struct SceneDeferredGraphResources
	{
		std::vector<RenderGraphTextureRef> gbuffer_targets{};
		RenderGraphTextureRef depth{};
		RenderGraphTextureRef lighting_accum{};
	};
}
