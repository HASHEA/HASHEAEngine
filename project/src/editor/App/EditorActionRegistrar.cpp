#include "App/EditorActionRegistrar.h"

#include "Core/EditorIds.h"
#include "Function/Gui/UICommon.h"
#include "Services/CommandService.h"

namespace AshEditor
{
	namespace
	{
		EditorShortcutBinding MakeShortcutBinding(
			const char* pDisplayText,
			uint32_t uChord,
			bool bAllowWhenTextInput = false)
		{
			EditorShortcutBinding binding{};
			binding.strDisplayText = pDisplayText ? pDisplayText : "";
			binding.uChord = uChord;
			binding.bAllowWhenTextInput = bAllowWhenTextInput;
			return binding;
		}
	}

	void EditorActionRegistrar::Register(EditorActionRegistrarContext& refContext)
	{
		IEditorActionHandler* pHandler = refContext.pHandler;

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::FileNewScene,
			"New Scene",
			"Create a new default scene and reset the current editor session state.",
			{},
			{ MakeShortcutBinding("Ctrl+N", AshEngine::make_key_chord(AshEngine::UIKey::N, AshEngine::UIModifierFlagBits::Ctrl)) },
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::FileOpenScene,
			"Open Scene",
			"Open a scene file from disk and activate it in the editor.",
			{},
			{ MakeShortcutBinding("Ctrl+O", AshEngine::make_key_chord(AshEngine::UIKey::O, AshEngine::UIModifierFlagBits::Ctrl)) },
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::FileReloadScene,
			"Reload Scene",
			"Reload the active scene from disk, or fall back to a new untitled scene if reload fails.",
			{},
			{ MakeShortcutBinding("Ctrl+R", AshEngine::make_key_chord(AshEngine::UIKey::R, AshEngine::UIModifierFlagBits::Ctrl)) },
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::FileSaveScene,
			"Save Scene",
			"Save the active scene to its current scene path.",
			{},
			{ MakeShortcutBinding("Ctrl+S", AshEngine::make_key_chord(AshEngine::UIKey::S, AshEngine::UIModifierFlagBits::Ctrl)) },
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::AssetsRefresh,
			"Refresh Assets",
			"Rescan the asset root and rebuild the asset browser index.",
			{},
			{ MakeShortcutBinding("F5", AshEngine::make_key_chord(AshEngine::UIKey::F5)) },
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::AssetsOpenSelected,
			"Open",
			"Open or activate the currently selected asset browser item.",
			{},
			{ MakeShortcutBinding("Enter", AshEngine::make_key_chord(AshEngine::UIKey::Enter)) },
			EditorActionScope::AssetBrowserContent,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::AssetsNavigateUp,
			"Up",
			"Navigate to the parent directory of the current asset browser scope.",
			{},
			{ MakeShortcutBinding("Backspace", AshEngine::make_key_chord(AshEngine::UIKey::Backspace)) },
			EditorActionScope::AssetBrowserContent,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::WindowResetLayout,
			"Reset Layout",
			"Restore the default dock layout and viewport panel arrangement.",
			{},
			{
				MakeShortcutBinding(
					"Ctrl+Shift+R",
					AshEngine::make_key_chord(AshEngine::UIKey::R, AshEngine::UIModifierFlagBits::Ctrl | AshEngine::UIModifierFlagBits::Shift))
			},
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::WindowCommandPalette,
			"Command Palette",
			"Search and execute editor actions.",
			{},
			{ MakeShortcutBinding("Ctrl+P", AshEngine::make_key_chord(AshEngine::UIKey::P, AshEngine::UIModifierFlagBits::Ctrl)) },
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			false
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::EditUndo,
			"Undo",
			"Undo the most recent editor command.",
			{},
			{ MakeShortcutBinding("Ctrl+Z", AshEngine::make_key_chord(AshEngine::UIKey::Z, AshEngine::UIModifierFlagBits::Ctrl)) },
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::EditRedo,
			"Redo",
			"Redo the most recently undone editor command.",
			{},
			{
				MakeShortcutBinding("Ctrl+Y", AshEngine::make_key_chord(AshEngine::UIKey::Y, AshEngine::UIModifierFlagBits::Ctrl)),
				MakeShortcutBinding(
					"Ctrl+Shift+Z",
					AshEngine::make_key_chord(AshEngine::UIKey::Z, AshEngine::UIModifierFlagBits::Ctrl | AshEngine::UIModifierFlagBits::Shift))
			},
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::SceneCreateRoot,
			"Add Root",
			"Create a new root entity in the active scene.",
			{},
			{
				MakeShortcutBinding(
					"Ctrl+Shift+A",
					AshEngine::make_key_chord(AshEngine::UIKey::A, AshEngine::UIModifierFlagBits::Ctrl | AshEngine::UIModifierFlagBits::Shift))
			},
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::SceneCreateChild,
			"Add Child",
			"Create a new child entity under the current selection.",
			{},
			{
				MakeShortcutBinding(
					"Ctrl+Alt+A",
					AshEngine::make_key_chord(AshEngine::UIKey::A, AshEngine::UIModifierFlagBits::Ctrl | AshEngine::UIModifierFlagBits::Alt))
			},
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::SelectionRename,
			"Rename",
			"Rename the currently selected entity.",
			{},
			{ MakeShortcutBinding("F2", AshEngine::make_key_chord(AshEngine::UIKey::F2)) },
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::SelectionReparent,
			"Reparent",
			"Change the parent of the currently selected entity.",
			{},
			{
				MakeShortcutBinding(
					"Ctrl+Shift+P",
					AshEngine::make_key_chord(AshEngine::UIKey::P, AshEngine::UIModifierFlagBits::Ctrl | AshEngine::UIModifierFlagBits::Shift))
			},
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});

		refContext.refCommandService.RegisterAction(EditorAction{
			EditorActionIds::SelectionDelete,
			"Delete",
			"Delete the currently selected entity from the active scene.",
			{},
			{ MakeShortcutBinding("Delete", AshEngine::make_key_chord(AshEngine::UIKey::Delete)) },
			EditorActionScope::Global,
			{},
			{},
			pHandler,
			true
		});
	}
}
