#pragma once

#include "Core/EditorEventBindings.h"
#include "Core/EditorEvents.h"
#include <cstdint>
#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>

namespace AshEditor
{
	class EditorEventBus;
	class PanelManager;
	class SceneService;
	class UndoRedoService;

	class EditorSessionStateService final
	{
	public:
		// Optional event bus used to keep a cached snapshot of editor session state.
		void BindEventBus(EditorEventBus* pEventBus);

		// Pulls baseline state from services (used on startup to seed UI).
		void SyncFromServices(const SceneService& refSceneService, const UndoRedoService& refUndoRedoService);

		// Pulls baseline panel open states (used on startup to seed UI).
		void SyncFromPanelManager(const PanelManager& refPanelManager);
		void Clear();

		// Accessors return last-known values (updated from events + initial sync).
		const EditorActiveSceneChangedEvent& GetActiveScene() const;
		const EditorSelection& GetSelection() const;
		const std::vector<EditorSelection>& GetSelections() const;
		const EditorUndoHistoryChangedEvent& GetUndoHistory() const;
		const EditorTransactionStateChangedEvent& GetTransactionState() const;
		const EditorActiveDocumentDirtyStateChangedEvent& GetActiveDocumentDirtyState() const;
		const EditorDocumentOperationEvent& GetLastDocumentOperation() const;
		const EditorActionInvokedEvent& GetLastActionInvocation() const;
		const EditorShortcutScopeChangedEvent& GetShortcutScope() const;

		// Panel open state helper used by layout/session UI.
		bool IsPanelOpen(std::string_view svPanelId, bool bDefaultValue = false) const;

		// Monotonic counter incremented when a layout reset event is observed.
		uint64_t GetViewportLayoutResetRevision() const;

	private:
		void UnsubscribeEvents();

	private:
		EditorEventBindings _eventBindings{};
		EditorActiveSceneChangedEvent _eventActiveScene{};
		EditorSelection _selection{};
		std::vector<EditorSelection> _vecSelections{};
		EditorUndoHistoryChangedEvent _eventUndoHistory{};
		EditorTransactionStateChangedEvent _eventTransactionState{};
		EditorActiveDocumentDirtyStateChangedEvent _eventActiveDocumentDirtyState{};
		EditorDocumentOperationEvent _eventLastDocumentOperation{};
		EditorActionInvokedEvent _eventLastActionInvocation{};
		EditorShortcutScopeChangedEvent _eventShortcutScope{};
		std::unordered_map<std::string, bool> _mapPanelOpenStates{};
		uint64_t _uViewportLayoutResetRevision = 0;
	};
}
