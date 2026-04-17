#pragma once

#include "Base/hcore.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneView.h"
#include <cstdint>
#include <vector>

namespace AshEngine
{
	struct ASH_API VisibilityQuery
	{
		const RenderScene* render_scene = nullptr;
		const SceneView* view = nullptr;
	};

	struct ASH_API VisiblePrimitiveSet
	{
		std::vector<uint64_t> primitive_ids{};
	};

	struct ASH_API VisibilityResult
	{
		VisiblePrimitiveSet visible_primitives{};
	};

	ASH_API bool run_visibility_query(const VisibilityQuery& query, VisibilityResult& out_result);
}
