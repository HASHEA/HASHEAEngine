#pragma once

#include "Core/EditorSelection.h"

#include <cstddef>
#include <cstdint>
#include <string>
#include <vector>

namespace AshEngine
{
	enum class SceneChangeKind : uint8_t;
	enum class SceneComponentType : uint8_t;
}

namespace AshEditor
{
	// Fired after the editor selection changes.
	// uRevision is a monotonic counter so UI can ignore stale events or detect missed updates.
	struct EditorSelectionChangedEvent
	{
		EditorSelection previousSelection{};
		EditorSelection currentSelection{};
		std::vector<EditorSelection> vecPreviousSelections{};
		std::vector<EditorSelection> vecCurrentSelections{};
		uint64_t uRevision = 0;
	};

	enum class EditorShortcutScope : uint8_t
	{
		Global = 0,
		AssetBrowserContent,
		NodeCanvasContent
	};

	struct EditorShortcutScopeChangedEvent
	{
		// Active shortcut scope for the current frame (global, asset browser content, etc.).
		EditorShortcutScope eScope = EditorShortcutScope::Global;
		// Which panel last requested this scope. Useful for status bars/debugging.
		std::string strOwnerPanelId{};
	};

	struct EditorUndoHistoryChangedEvent
	{
		// CanUndo/CanRedo refer to committed history only. Open transactions block undo/redo until commit/cancel.
		bool bCanUndo = false;
		bool bCanRedo = false;
		bool bHasOpenTransaction = false;
		std::string strOpenTransactionLabel{};
	};

	struct EditorTransactionStateChangedEvent
	{
		// Pending transaction state. When bHasOpenTransaction is false, label/count are unspecified.
		bool bHasOpenTransaction = false;
		std::string strLabel{};
		size_t uPendingCommandCount = 0;
	};

	struct EditorActiveDocumentDirtyStateChangedEvent
	{
		// True when the active document differs from its last saved/marked state.
		bool bDirty = false;
	};

	enum class EditorDocumentOperationKind : uint8_t
	{
		None = 0,
		NewScene,
		LoadScene,
		ReloadScene,
		SaveScene
	};

	enum class EditorDocumentOperationResult : uint8_t
	{
		None = 0,
		Succeeded,
		Failed,
		Skipped,
		FallbackActivated
	};

	struct EditorDocumentOperationEvent
	{
		// Document operations refer to the active editor document (currently scene). The name/path are for UI display.
		EditorDocumentOperationKind eKind = EditorDocumentOperationKind::None;
		EditorDocumentOperationResult eResult = EditorDocumentOperationResult::None;
		std::string strDocumentName{};
		std::string strDocumentPath{};
	};

	struct EditorNotificationEvent
	{
		// Best-effort transient message for UI surfaces (console/toasts).
		std::string strMessage{};
		std::string strSource{ "Editor" };
	};

	enum class EditorLogSeverity : uint8_t
	{
		Trace = 0,
		Info,
		Warning,
		Error
	};

	struct EditorLogEvent
	{
		// Structured log event forwarded to editor surfaces such as Console.
		// Emitted on the editor thread after background/runtime log sinks are drained.
		EditorLogSeverity eSeverity = EditorLogSeverity::Info;
		std::string strSource{ "Editor" };
		std::string strMessage{};
	};

	struct EditorActiveSceneChangedEvent
	{
		std::string strSceneName{};
		std::string strScenePath{};
	};

	struct EditorSceneChangedEvent
	{
		AshEngine::SceneChangeKind eKind;
		AshEngine::SceneComponentType eComponentType;
		uint64_t uEntityId = 0;
		uint64_t uChangeVersion = 0;
		bool bDirty = false;
		std::string strSceneName{};
		std::string strScenePath{};
	};

	struct EditorViewportLayoutResetEvent
	{
	};

	struct EditorViewportPresentationChangedEvent
	{
		// A view-only "presentation" snapshot for a viewport (toolbar/overlays/flags). Per-viewport render state lives elsewhere.
		std::string strViewportId{};
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

	struct EditorPrimaryViewportChangedEvent
	{
		std::string strPreviousViewportId{};
		std::string strCurrentViewportId{};
	};

	struct EditorPanelOpenStateChangedEvent
	{
		std::string strPanelId{};
		bool bOpen = true;
	};

	struct EditorActionInvokedEvent
	{
		std::string strActionId{};
		std::string strActionLabel{};
		std::string strSource{ "unknown" };
		bool bRegistered = false;
		bool bEnabled = false;
		bool bExecuted = false;
	};
}
