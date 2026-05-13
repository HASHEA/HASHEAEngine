#pragma once

#include <filesystem>
#include <string>

namespace AshEditor
{
	struct OpenFileDialogOptions
	{
		std::wstring strTitle{};
		const wchar_t* pFilter = nullptr;
		std::wstring strDefaultExtension{};
		std::filesystem::path pathInitialDirectory{};
	};

	std::filesystem::path OpenSingleFileDialog(const OpenFileDialogOptions& options);
}
