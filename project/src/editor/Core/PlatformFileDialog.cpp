#include "Core/PlatformFileDialog.h"

#if defined(_WIN32)
#include <windows.h>
#include <commdlg.h>

#pragma comment(lib, "Comdlg32.lib")
#endif

#include <array>

namespace AshEditor
{
	std::filesystem::path OpenSingleFileDialog(const OpenFileDialogOptions& options)
	{
#if defined(_WIN32)
		std::array<wchar_t, 32768> arrFileBuffer{};
		OPENFILENAMEW dialogDesc{};
		dialogDesc.lStructSize = sizeof(dialogDesc);
		dialogDesc.lpstrFile = arrFileBuffer.data();
		dialogDesc.nMaxFile = static_cast<DWORD>(arrFileBuffer.size());
		dialogDesc.lpstrTitle =
			options.strTitle.empty()
			? nullptr
			: options.strTitle.c_str();
		dialogDesc.lpstrFilter =
			options.pFilter
			? options.pFilter
			: L"All Files (*.*)\0*.*\0\0";
		dialogDesc.lpstrDefExt =
			options.strDefaultExtension.empty()
			? nullptr
			: options.strDefaultExtension.c_str();

		const std::wstring strInitialDirectory = options.pathInitialDirectory.wstring();
		dialogDesc.lpstrInitialDir =
			strInitialDirectory.empty()
			? nullptr
			: strInitialDirectory.c_str();
		dialogDesc.Flags =
			OFN_PATHMUSTEXIST |
			OFN_FILEMUSTEXIST |
			OFN_EXPLORER |
			OFN_HIDEREADONLY |
			OFN_NOCHANGEDIR;

		if (GetOpenFileNameW(&dialogDesc))
		{
			return std::filesystem::path(arrFileBuffer.data());
		}
#else
		(void)options;
#endif
		return {};
	}
}
