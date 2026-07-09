#pragma once
#include <Windows.h>
#include <unknwn.h>
#include "dxcapi.h"
#include "dxctools.h"
#include <filesystem>
#include "Base/hfile.h"
namespace RHI
{
	/// <summary>
 /// Provides a QueryInterface implementation for a class that supports
 /// any number of interfaces in addition to IUnknown.
 /// </summary>
 /// <remarks>
 /// This implementation will also report the instance as not supporting
 /// marshaling. This will help catch marshaling problems early or avoid
 /// them altogether.
 /// </remarks>
	template <typename TObject>
	HRESULT DoBasicQueryInterface_recurse(TObject* self, REFIID iid, void** ppvObject)
	{
		return E_NOINTERFACE;
	}
	template <typename TObject, typename TInterface, typename... Ts>
	HRESULT DoBasicQueryInterface_recurse(TObject* self, REFIID iid, void** ppvObject)
	{
		if (ppvObject == nullptr)
			return E_POINTER;
		if (IsEqualIID(iid, __uuidof(TInterface)))
		{
			*(TInterface**)ppvObject = self;
			self->AddRef();
			return S_OK;
		}
		return DoBasicQueryInterface_recurse<TObject, Ts...>(self, iid, ppvObject);
	}
	template <typename... Ts, typename TObject>
	HRESULT DoBasicQueryInterface(TObject* self, REFIID iid, void** ppvObject)
	{
		if (ppvObject == nullptr)
			return E_POINTER;

		// Support INoMarshal to void GIT shenanigans.
		if (IsEqualIID(iid, __uuidof(IUnknown)) || IsEqualIID(iid, __uuidof(INoMarshal)))
		{
			*ppvObject = reinterpret_cast<IUnknown*>(self);
			reinterpret_cast<IUnknown*>(self)->AddRef();
			return S_OK;
		}

		return DoBasicQueryInterface_recurse<TObject, Ts...>(self, iid, ppvObject);
	}


	class DXCIncludeHandler : public IDxcIncludeHandler
	{
	public:
		DXCIncludeHandler();
		virtual ~DXCIncludeHandler();
		HRESULT LoadSource(LPCWSTR pFilename, IDxcBlob** ppIncludeSource) override;
	public:
		//clear each time compile
		void set_current_user_shader_path(const std::filesystem::path& path);
		void set_current_generated_bindings_path(const std::filesystem::path& path);

		void set_include_root_path(const std::filesystem::path& path);
	public:
		HRESULT __stdcall QueryInterface(REFIID riid, void** ppvObject) override;
		ULONG __stdcall AddRef(void) override;
		ULONG __stdcall Release(void) override;
	private:
		const std::filesystem::path defaultUserShaderName = L"UserShader.hlsli";
		const std::filesystem::path defaultUserShaderPath = L"project/src/engine/Shaders/MaterialV2/Includes/UserShader.hlsli";
		const std::filesystem::path defaultGeneratedBindingsName = L"GeneratedMaterialBindings.hlsli";
		const std::filesystem::path defaultGeneratedBindingsPath = L"project/src/engine/Shaders/MaterialV2/Includes/GeneratedMaterialBindings.hlsli";
		IDxcLibrary* m_pLibrary = nullptr;
		std::filesystem::path m_currentUserShaderPath{};
		std::filesystem::path m_currentGeneratedBindingsPath{};
		std::filesystem::path m_currentIncludeRootPath{};
		volatile std::atomic<int32_t> m_dwRef{ 1 };

		
	};
};
