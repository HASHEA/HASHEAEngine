#pragma once

#include "Core/INotificationSink.h"

#include <filesystem>

namespace AshEditor
{
	class EditorSettingsService;
	class EditorViewportService;

	class ViewportLayoutPersistence final
	{
	public:
		static std::filesystem::path GetStatePath(const EditorSettingsService& refSettingsService);
		static void Load(
			const EditorSettingsService& refSettingsService,
			EditorViewportService& refViewportService,
			INotificationSink* pNotificationSink);
		static void Save(
			const EditorSettingsService& refSettingsService,
			const EditorViewportService& refViewportService);
	};
}
