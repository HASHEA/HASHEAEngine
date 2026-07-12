#include "Widgets/ViewportAxisIndicator.h"

#include "Function/Gui/UIContext.h"
#include "Widgets/EditorThemeColors.h"

#include <cmath>

namespace AshEditor
{
	void DrawViewportAxisIndicator(
		AshEngine::UIContext& refUi,
		float fContentOriginX,
		float fContentOriginY,
		float fContentWidth,
		float fContentHeight,
		const ViewportAxisIndicatorParams& params)
	{
		const float fAxisLength = 28.0f;
		const float fMargin = 40.0f;
		const float fLineThickness = 2.0f;

		const float fCenterX = fContentOriginX + fContentWidth - fMargin;
		const float fCenterY = fContentOriginY + fMargin;

		if (fContentWidth < fMargin * 2.0f || fContentHeight < fMargin * 2.0f)
		{
			return;
		}

		// Project each world axis onto screen-space 2D using camera basis vectors.
		// Screen X = dot(worldAxis, viewRight), Screen Y = -dot(worldAxis, viewUp) (Y is flipped in screen coords).
		struct AxisInfo
		{
			float fScreenX;
			float fScreenY;
			AshEngine::UIColor color;
			const char* pLabel;
		};

		AxisInfo axes[3] = {
			{ // X axis (1,0,0)
				params.viewRightX * fAxisLength,
				-params.viewUpX * fAxisLength,
				{ 0.90f, 0.25f, 0.25f, 1.0f },
				"X"
			},
			{ // Y axis (0,1,0)
				params.viewRightY * fAxisLength,
				-params.viewUpY * fAxisLength,
				{ 0.25f, 0.85f, 0.25f, 1.0f },
				"Y"
			},
			{ // Z axis (0,0,1)
				params.viewRightZ * fAxisLength,
				-params.viewUpZ * fAxisLength,
				{ 0.30f, 0.50f, 0.95f, 1.0f },
				"Z"
			}
		};

		// Draw background circle.
		const float fBgRadius = fAxisLength + 10.0f;
		const AshEngine::UIRect rectBg{
			fCenterX - fBgRadius,
			fCenterY - fBgRadius,
			fBgRadius * 2.0f,
			fBgRadius * 2.0f
		};
		refUi.draw_window_rect_filled(rectBg, GetEditorOverlayBackgroundColor(refUi), fBgRadius);

		// Draw axes — back-to-front by depth (viewForward dot).
		// Sort by forward component so axes pointing away draw first.
		int order[3] = { 0, 1, 2 };
		float depths[3] = {
			params.viewForwardX,  // depth of X axis
			params.viewForwardY,  // depth of Y axis
			params.viewForwardZ   // depth of Z axis
		};
		// Positive camera-forward depth is farther from the viewer, so draw in descending order.
		for (int i = 0; i < 2; ++i)
		{
			for (int j = i + 1; j < 3; ++j)
			{
				if (depths[order[j]] > depths[order[i]])
				{
					int tmp = order[i];
					order[i] = order[j];
					order[j] = tmp;
				}
			}
		}

		for (int i = 0; i < 3; ++i)
		{
			const AxisInfo& refAxis = axes[order[i]];
			const AshEngine::UIVec2 vecStart{ fCenterX, fCenterY };
			const AshEngine::UIVec2 vecEnd{ fCenterX + refAxis.fScreenX, fCenterY + refAxis.fScreenY };
			refUi.draw_window_line(vecStart, vecEnd, refAxis.color, fLineThickness);

			const float fLabelOffset = 4.0f;
			float fLabelX = vecEnd.x + fLabelOffset;
			float fLabelY = vecEnd.y - 5.0f;
			refUi.draw_window_text({ fLabelX, fLabelY }, refAxis.color, refAxis.pLabel);
		}
	}
}
