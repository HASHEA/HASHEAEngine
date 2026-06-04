#pragma once

#include "Core/IEditorViewportBindingResolver.h"
#include "Core/EditorViewportTypes.h"
#include "Function/Render/ScenePresentationHandles.h"

#include <cstdint>
#include <memory>
#include <string>
#include <unordered_map>
#include <vector>

namespace AshEngine
{
	class Scene;
	class ScenePresentationSubsystem;
}

namespace AshEditor
{
	class EditorEventBus;

	enum class EditorViewportKind : uint8_t
	{
		Scene = 0,
		Game,
		Auxiliary
	};

	struct EditorViewportPresentation
	{
		EditorViewportKind eKind = EditorViewportKind::Auxiliary;
		bool bShowToolbar = true;
		bool bPreserveAspect = false;
		bool bAcceptsInput = false;
		bool bShowStats = true;
		bool bShowOverlays = false;
		bool bShowReferenceGrid = true;
		bool bShowReferenceOrigin = true;
		bool bShowSelectionHelpers = true;
		bool bShowCameraHelpers = true;
		bool bShowLightHelpers = true;
		bool bShowSelectionPivot = true;
		bool bPanelOpen = true;
	};

	struct EditorViewportRenderState
	{
		uint32_t uOutputWidth = 0;
		uint32_t uOutputHeight = 0;
		bool bPendingSync = true;
		bool bCameraOverrideApplied = false;
	};

	struct EditorViewportPersistenceState
	{
		std::string strId{};
		bool bPanelOpen = true;
		bool bShowToolbar = true;
		bool bPreserveAspect = false;
		bool bAcceptsInput = false;
		bool bShowStats = true;
		bool bShowOverlays = false;
		bool bShowReferenceGrid = true;
		bool bShowReferenceOrigin = true;
		bool bShowSelectionHelpers = true;
		bool bShowCameraHelpers = true;
		bool bShowLightHelpers = true;
		bool bShowSelectionPivot = true;
	};

	class EditorViewportService
	{
	private:
		struct ViewportRecord
		{
			EditorViewportInstance viewportInstance{};
			EditorViewportPresentation viewportPresentation{};
			EditorViewportRenderState viewportRenderState{};
			uint32_t uPendingRequestedWidth = 0;
			uint32_t uPendingRequestedHeight = 0;
			float fPendingRequestedSinceSeconds = 0.0f;
			AshEngine::SceneOutputHandle sceneOutput{};
			AshEngine::SceneViewBindingHandle sceneViewBinding{};
		};

		using ViewportStorage = std::unordered_map<std::string, std::unique_ptr<ViewportRecord>>;

	public:
		// Optional event bus used to publish viewport presentation/primary changes.
		void SetEventBus(EditorEventBus* pEventBus);

		// Ensures a viewport record exists for the given id and returns its instance.
		// - id: stable identifier used for persistence and lookups (e.g. "scene", "game", "aux.*").
		// - strDisplayName: UI display name only; does not affect persistence keys.
		// The returned reference is owned by the service and remains valid until Clear().
		EditorViewportInstance& EnsureViewport(std::string strViewportId, std::string strDisplayName = {});
		EditorViewportInstance* FindViewport(const std::string& strViewportId);
		const EditorViewportInstance* FindViewport(const std::string& strViewportId) const;

		// Returns viewports in a stable order (Scene, Game, then others) to avoid nondeterminism from unordered_map iteration.
		std::vector<EditorViewportInstance*> GetViewports();
		std::vector<const EditorViewportInstance*> GetViewports() const;

		// Primary viewport is a UI concept used by editor tools that need a default target.
		EditorViewportInstance* GetPrimaryViewport();
		const EditorViewportInstance* GetPrimaryViewport() const;
		const std::string& GetPrimaryViewportId() const;
		bool IsPrimaryViewport(const std::string& strViewportId) const;

		// Sets the primary viewport if the id exists; invalid ids are ignored.
		void SetPrimaryViewport(const std::string& strViewportId);
		EditorViewportPresentation* GetPresentation(const std::string& strViewportId);
		const EditorViewportPresentation* GetPresentation(const std::string& strViewportId) const;
		EditorViewportRenderState* GetRenderState(const std::string& strViewportId);
		const EditorViewportRenderState* GetRenderState(const std::string& strViewportId) const;
		AshEngine::SceneViewBindingHandle GetSceneViewBindingHandle(const std::string& strViewportId) const;
		bool TryGetSceneViewStats(const std::string& strViewportId, AshEngine::SceneViewStats& outStats) const;

		// Updates the viewport requested output size (in pixels). Use 0 to indicate "no valid size this frame".
		// The request is debounced so panel splitter drags do not rebuild offscreen outputs every frame.
		// Returns true only when the debounced size commits and a presentation resync is required.
		bool UpdateRequestedSize(const std::string& strViewportId, uint32_t uWidth, uint32_t uHeight, float fTimeSeconds);
		void SetPanelOpen(const std::string& strId, bool bOpen);

		// Syncs editor viewport outputs/bindings to the engine ScenePresentationSubsystem for the given scene.
		// Should be called after requested sizes/presentation changes and before drawing UI surfaces.
		// Returns true only if all viewports successfully synchronized.
		bool SyncScenePresentations(
			AshEngine::ScenePresentationSubsystem& refScenePresentation,
			AshEngine::Scene& refScene,
			const IEditorViewportBindingResolver* pBindingResolver = nullptr);

		// Destroys any created outputs/bindings and clears UI surfaces.
		// Pass nullptr when the subsystem is already unavailable (handles will still be cleared locally).
		void DestroyScenePresentations(AshEngine::ScenePresentationSubsystem* pScenePresentation);
		void ResetPresentations();
		std::vector<EditorViewportPersistenceState> CapturePersistenceState() const;
		void ApplyPersistenceState(
			const std::vector<EditorViewportPersistenceState>& vecStates,
			const std::string& strPrimaryViewportId);
		void Clear();

	private:
		ViewportRecord* FindRecord(const std::string& strViewportId);
		const ViewportRecord* FindRecord(const std::string& strViewportId) const;
		static EditorViewportPresentation BuildDefaultPresentation(const std::string& strViewportId);
		void NotifyPresentationChanged(const std::string& strViewportId, const EditorViewportPresentation& refPresentation) const;
		void NotifyPrimaryViewportChanged(const std::string& strPreviousViewportId, const std::string& strCurrentViewportId) const;
		bool ApplyPresentationState(ViewportRecord& refRecord, const EditorViewportPersistenceState& refState);

		EditorEventBus* _pEventBus = nullptr;
		ViewportStorage _mapViewports{};
		std::string _strPrimaryViewportId{};
	};
}
