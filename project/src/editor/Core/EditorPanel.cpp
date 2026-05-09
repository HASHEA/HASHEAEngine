#include "Core/EditorPanel.h"
#include "Function/Gui/UIContext.h"
#include <utility>

namespace AshEditor
{
	EditorPanel::EditorPanel(std::string strId, std::string strTitle)
		: _strId(std::move(strId))
		, _strTitle(std::move(strTitle))
	{
	}

	const std::string& EditorPanel::GetId() const
	{
		return _strId;
	}

	const std::string& EditorPanel::GetTitle() const
	{
		return _strTitle;
	}

	bool EditorPanel::IsOpen() const
	{
		return _bOpen;
	}

	void EditorPanel::SetOpen(bool bOpen)
	{
		_bOpen = bOpen;
	}

	void EditorPanel::OnAttach()
	{
	}

	void EditorPanel::OnDetach()
	{
	}

	void EditorPanel::OnUpdate()
	{
	}

	void EditorPanel::OnGui(const EditorFrameContext& refFrameContext)
	{
		(void)refFrameContext;
	}

	bool EditorPanel::BeginPanelWindow(const EditorFrameContext& refFrameContext, AshEngine::UIWindowFlags flags)
	{
		_bWindowActiveThisFrame = false;
		if (!refFrameContext.pUiContext || !refFrameContext.pUiContext->is_frame_active())
		{
			return false;
		}

		refFrameContext.pUiContext->set_next_window_force_dock_tab_bar(true);

		bool bOpen = _bOpen;
		const bool bVisible = refFrameContext.pUiContext->begin_window(_strTitle.c_str(), &bOpen, flags);
		_bOpen = bOpen;
		_bWindowActiveThisFrame = true;
		return bVisible;
	}

	void EditorPanel::EndPanelWindow(const EditorFrameContext& refFrameContext)
	{
		if (_bWindowActiveThisFrame && refFrameContext.pUiContext)
		{
			refFrameContext.pUiContext->end_window();
		}
		_bWindowActiveThisFrame = false;
	}
}
