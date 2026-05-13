#pragma once

#include "Core/EditorFrameContext.h"
#include "Core/EditorPanel.h"
#include "Core/PanelDeps/AssetPreviewPanelDeps.h"

#include <memory>

namespace AshEditor
{
	struct AssetPreviewPanelState;

	class AssetPreviewPanel final : public EditorPanel
	{
	public:
		explicit AssetPreviewPanel(AssetPreviewPanelDeps deps = {});
		~AssetPreviewPanel() override;

	public:
		void OnAttach() override;
		void OnDetach() override;
		void OnGui(const EditorFrameContext& refFrameContext) override;

	private:
		void ClearDeps();
		AssetPreviewPanelState& GetState();
		const AssetPreviewPanelState& GetState() const;

	private:
		AssetPreviewPanelDeps _deps{};
		std::unique_ptr<AssetPreviewPanelState> _upState{};
	};
}
