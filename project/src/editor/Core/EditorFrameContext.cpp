#include "Core/EditorFrameContext.h"

#include "Core/EditorContext.h"

namespace AshEditor
{
	EditorFrameContext MakeEditorFrameContext(const EditorContext& context)
	{
		// Keep this helper narrow so frame state does not quietly become another shared service locator.
		return EditorFrameContext{
			context.pUiContext,
			context.bGuiRendererReady
		};
	}
}
