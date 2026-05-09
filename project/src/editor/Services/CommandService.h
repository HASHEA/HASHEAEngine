#pragma once

#include <cstdint>
#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace AshEditor
{
	class EditorEventBus;
	class IEditorActionHandler;

	enum class EditorActionScope : uint8_t
	{
		Global = 0,
		AssetBrowserContent
	};

	struct EditorShortcutBinding
	{
		std::string strDisplayText{};
		uint32_t uChord = 0;
		bool bAllowWhenTextInput = false;
	};

	struct EditorAction
	{
		std::string strId{};
		std::string strLabel{};
		std::string strDescription{};
		std::string strShortcut{};
		std::vector<EditorShortcutBinding> vecShortcutBindings{};
		EditorActionScope eScope = EditorActionScope::Global;
		std::function<bool()> fnCanExecute{};
		std::function<void()> fnCallback{};
		IEditorActionHandler* pHandler = nullptr;
		bool bShowInCommandPalette = true;
	};

	class CommandService
	{
	public:
		// Optional event bus used to publish action invocation telemetry/events.
		void SetEventBus(EditorEventBus* pEventBus);

		// Registers or replaces an action by id.
		// - action.strId must be stable and unique (used as lookup key and category prefix).
		// - action.strShortcut is optional; if empty it will be derived from vecShortcutBindings for display.
		void RegisterAction(EditorAction action);

		// Convenience overloads for global-scope actions.
		// - strId: stable unique action id, conventionally "category.action" (category derived via GetActionCategory).
		// - strShortcut: optional display shortcut text for UI (does not automatically bind key chords).
		void RegisterAction(std::string strId, std::string strLabel, std::string strShortcut, std::function<void()> callback);
		void RegisterAction(std::string strId, std::string strLabel, std::function<void()> callback);

		// Invokes an action by id if it exists, is enabled, and has a callback.
		// - svSource: a short string describing the caller path (menu, shortcut scope, panel, etc.) for logging/telemetry.
		// Returns true only if the callback actually executed. An invocation event is published regardless.
		bool Invoke(const std::string& strId, std::string_view svSource = "unknown") const;
		bool CanExecute(const std::string& strId) const;
		bool HasAction(const std::string& strId) const;
		const EditorAction* FindAction(const std::string& strId) const;

		// Collectors return pointers into internal storage (valid until next RegisterAction mutates the vector).
		std::vector<const EditorAction*> CollectActionsWithPrefix(std::string_view svPrefix) const;
		std::vector<const EditorAction*> CollectActionsWithScope(EditorActionScope eScope) const;
		std::vector<const EditorAction*> CollectActionsForCommandPalette() const;
		const std::vector<EditorAction>& GetActions() const;

		// Extracts a category label from an action id using the '.' separator (e.g. "scene.reload" -> "scene").
		static std::string_view GetActionCategory(std::string_view svId);

	private:
		void PublishActionInvoked(
			std::string_view svActionId,
			const EditorAction* pAction,
			std::string_view svSource,
			bool bEnabled,
			bool bExecuted) const;

	private:
		EditorEventBus* _pEventBus = nullptr;
		std::vector<EditorAction> _vecActions{};
	};
}
