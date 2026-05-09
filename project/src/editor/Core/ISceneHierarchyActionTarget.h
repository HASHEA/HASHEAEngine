#pragma once

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class ISceneHierarchyActionTarget
	{
	public:
		virtual ~ISceneHierarchyActionTarget() = default;

		virtual void ExecuteCreateRoot() = 0;
		virtual void ExecuteCreateChildFromSelection() = 0;
		virtual void RequestRenameSelected(AshEngine::UIContext* pUiContext) = 0;
		virtual void RequestReparentSelected(AshEngine::UIContext* pUiContext) = 0;
		virtual void RequestDeleteSelected(AshEngine::UIContext* pUiContext) = 0;
	};
}
