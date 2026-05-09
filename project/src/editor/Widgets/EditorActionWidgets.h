#pragma once

namespace AshEngine
{
	class UIContext;
}

namespace AshEditor
{
	class IActionInvoker;
	class CommandService;

	bool DrawEditorActionMenuItem(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pSource,
		bool bEnabled = true);

	bool DrawEditorActionMenuItem(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		bool bEnabled = true);

	bool DrawEditorActionMenuItem(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		IActionInvoker* pActionInvoker,
		bool bEnabled = true);

	bool DrawEditorActionButton(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		IActionInvoker* pActionInvoker,
		bool bEnabled = true);

	bool DrawEditorActionButton(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		bool bEnabled = true);

	bool DrawEditorActionSmallButton(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		IActionInvoker* pActionInvoker,
		bool bEnabled = true);

	bool DrawEditorActionSmallButton(
		AshEngine::UIContext& refUi,
		const CommandService& refCommandService,
		const char* pActionId,
		const char* pLabelOverride,
		const char* pSource,
		bool bEnabled = true);
}
