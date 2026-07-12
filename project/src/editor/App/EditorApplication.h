#pragma once

#include <memory>

namespace AshEditor
{
	class EditorApplicationImpl;
	struct EditorViewportInstance;
	class EditorViewportService;

	class EditorApplication
	{
	public:
		EditorApplication();
		~EditorApplication();

		EditorApplication(const EditorApplication&) = delete;
		EditorApplication& operator=(const EditorApplication&) = delete;

		bool Initialize();
		void Shutdown();

		void Update();
		void DrawGui();
		void SyncRuntimeScenePresentations();
		bool IsAutomationReady() const;
		bool HasAutomationFailure() const;
		EditorViewportInstance* GetPrimaryViewport();
		const EditorViewportInstance* GetPrimaryViewport() const;
		EditorViewportService& GetViewportService();
		const EditorViewportService& GetViewportService() const;

	private:
		std::unique_ptr<EditorApplicationImpl> _upImpl{};
	};
}
