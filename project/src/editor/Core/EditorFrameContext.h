#pragma once

namespace AshEditor
{
	struct EditorContext;
}

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	struct EditorFrameContext
	{
		// Frame-only UI state. This struct intentionally does not carry any shared viewport snapshot.
		// Multi-viewport callers must query EditorViewportService per panel instead of reading stale shared state.
		AshEngine::UIContext* pUiContext = nullptr;
		bool bGuiRendererReady = false;
	};

	EditorFrameContext MakeEditorFrameContext(const EditorContext& context);
}
