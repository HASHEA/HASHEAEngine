#include "Base/hfile.h"
#include "dxcapi.h"
#include <filesystem>
namespace RHI
{
	class DXCIncludeHandler : public IDxcIncludeHandler
	{
	public:
		DXCIncludeHandler();
		virtual ~DXCIncludeHandler();
		HRESULT LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource) override;
	public:
		//clear each time compile
		void set_current_user_shader_path(const std::filesystem::path& path);

		void set_include_root_path(const std::filesystem::path& path);
	private:
		const std::filesystem::path defaultUserShaderName = L"UserShader.hlsli";
		const std::filesystem::path defaultUserShaderPath = L"assets/hlsl/include/UserShader.hlsli";
		IDxcLibrary* m_pLibrary = nullptr;
		std::filesystem::path m_currentUserShaderPath{};
		std::filesystem::path m_currentIncludeRootPath{};
	};
};