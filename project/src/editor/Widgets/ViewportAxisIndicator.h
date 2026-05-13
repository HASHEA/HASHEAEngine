#pragma once

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	// Draws a small 3-axis orientation indicator (XYZ) in the top-right corner
	// of the current viewport content area.
	// viewRight/viewUp/viewForward are the camera's basis vectors in world space
	// (columns of the inverse view matrix, or equivalently rows of the view matrix).
	struct ViewportAxisIndicatorParams
	{
		float viewRightX = 1.0f, viewRightY = 0.0f, viewRightZ = 0.0f;
		float viewUpX = 0.0f, viewUpY = 1.0f, viewUpZ = 0.0f;
		float viewForwardX = 0.0f, viewForwardY = 0.0f, viewForwardZ = 1.0f;
	};

	void DrawViewportAxisIndicator(
		AshEngine::UIContext& refUi,
		float fContentOriginX,
		float fContentOriginY,
		float fContentWidth,
		float fContentHeight,
		const ViewportAxisIndicatorParams& params = {});
}
