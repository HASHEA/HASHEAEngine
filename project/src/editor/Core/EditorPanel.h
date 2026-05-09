#pragma once
#include "Core/EditorFrameContext.h"
#include "Function/Gui/UICommon.h"
#include <string>

namespace AshEditor
{
	class EditorPanel
	{
	public:
		EditorPanel(std::string strId, std::string strTitle);
		virtual ~EditorPanel() = default;

	public:
		const std::string& GetId() const;
		const std::string& GetTitle() const;
		bool IsOpen() const;
		void SetOpen(bool bOpen);

		virtual void OnAttach();
		virtual void OnDetach();
		virtual void OnUpdate();
		virtual void OnGui(const EditorFrameContext& refFrameContext);

	protected:
		bool BeginPanelWindow(const EditorFrameContext& refFrameContext, AshEngine::UIWindowFlags flags = AshEngine::UIWindowFlagBits::None);
		void EndPanelWindow(const EditorFrameContext& refFrameContext);

	private:
		std::string _strId{};
		std::string _strTitle{};
		bool _bOpen = true;
		bool _bWindowActiveThisFrame = false;
	};
}
