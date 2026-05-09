#pragma once

#include "Function/Gui/UICommon.h"

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class DockLayoutController final
	{
	public:
		void RequestLayoutReset();
		void ClearRuntimeState();
		void DrawWorkspaceHost(AshEngine::UIContext& refUi, float fBottomInset = 0.0f);

	private:
		void BuildDefaultDockLayout(
			AshEngine::UIContext& refUi,
			AshEngine::UIDockNodeId uDockspaceId,
			const AshEngine::UIVec2& refSize) const;

	private:
		bool _bResetLayoutRequested = false;
		bool _bDefaultDockLayoutBuilt = false;
	};
}
