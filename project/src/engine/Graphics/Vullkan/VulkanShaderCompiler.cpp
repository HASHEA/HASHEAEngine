#include "VulkanShader.h"
#include "VulkanShaderCompiler.h"
#include "VulkanPipeline.h"
#include "VulkanDescriptorSet.h"
#include "Graphics/ShaderCache.h"
#if defined(ASH_HAS_DXC)
#include "Graphics/DXC/DXCHelper.h"
#include "Graphics/DXC/DXCIncludeHandler.h"
#endif
//
#include "Base/hmemory.h"
#include "Base/hcache.h"
#include "Base/hfile.h"
#include "Base/hstring.h"
#include "Base/hassert.h"
#include <string>
#include <filesystem>
#include <fstream>
#include <vector>
#include <algorithm>

namespace RHI
{
#if defined(ASH_HAS_DXC)
	// Helper function to convert UTF-8 to wide string
	static inline std::wstring utf8_to_wstring(const std::string& str)
	{
		int len = MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, nullptr, 0);
		std::wstring wstr(len, 0);
		MultiByteToWideChar(CP_UTF8, 0, str.c_str(), -1, wstr.data(), len);
		if (!wstr.empty() && wstr.back() == L'\0')
			wstr.pop_back();
		return wstr;
	}

	// Helper function to convert shader stage to DXC profile for SPIR-V
	static std::wstring get_spirv_profile(AshShaderStageFlagBits stage)
	{
		switch (stage)
		{
		case ASH_SHADER_STAGE_VERTEX_BIT:
			return L"vs_6_0";
		case ASH_SHADER_STAGE_FRAGMENT_BIT:
			return L"ps_6_0";
		case ASH_SHADER_STAGE_GEOMETRY_BIT:
			return L"gs_6_0";
		case ASH_SHADER_STAGE_TESSELLATION_CONTROL_BIT:
			return L"hs_6_0";
		case ASH_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
			return L"ds_6_0";
		case ASH_SHADER_STAGE_COMPUTE_BIT:
			return L"cs_6_0";
		case ASH_SHADER_STAGE_RAYGEN_BIT_KHR:
		case ASH_SHADER_STAGE_ANY_HIT_BIT_KHR:
		case ASH_SHADER_STAGE_CLOSEST_HIT_BIT_KHR:
		case ASH_SHADER_STAGE_MISS_BIT_KHR:
		case ASH_SHADER_STAGE_INTERSECTION_BIT_KHR:
		case ASH_SHADER_STAGE_CALLABLE_BIT_KHR:
			return L"lib_6_3";
		default:
			return L"lib_6_0";
		}
	}

	static bool shader_stage_supports_vk_invert_y(AshShaderStageFlagBits stage)
	{
		switch (stage)
		{
		case ASH_SHADER_STAGE_VERTEX_BIT:
		case ASH_SHADER_STAGE_TESSELLATION_EVALUATION_BIT:
		case ASH_SHADER_STAGE_GEOMETRY_BIT:
		case ASH_SHADER_STAGE_MESH_BIT_EXT:
			return true;
		default:
			return false;
		}
	}

	// Helper function to get cache file path
	static std::filesystem::path get_cache_path(const SHA1::Digest& passKey)
	{
		std::filesystem::path cacheDir = CacheDirectoryVulkan;
		std::string hashStr = passKey.to_string();
		return cacheDir / (hashStr + ".spv");
	}

	// Helper function to read from cache
	static bool read_from_cache(const SHA1::Digest& passKey, const SHA1::Digest& textKey, std::vector<uint32_t>& outSpirv)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		std::filesystem::path cachePath = get_cache_path(passKey);
		ASH_PROCESS_ERROR(std::filesystem::exists(cachePath));
		// Read file using standard file I/O
		std::ifstream file(cachePath, std::ios::binary);
		ASH_PROCESS_ERROR(file.is_open());

		ShaderCacheIndexHeader header = {};
		file.read(reinterpret_cast<char*>(&header), sizeof(header));
		ASH_PROCESS_ERROR(file.gcount() == sizeof(header));
		ASH_PROCESS_ERROR(memcmp(header.Magic, kMagic, 4) == 0);
		ASH_PROCESS_ERROR((header.Version == kVersion));
		ASH_PROCESS_ERROR((header.CompilerHash == textKey));
		ASH_PROCESS_ERROR(header.BlobSize > 0);
		ASH_PROCESS_ERROR((header.BlobSize % sizeof(uint32_t)) == 0);

		// Read SPIR-V data
		outSpirv.resize(header.BlobSize / sizeof(uint32_t));
		file.read(reinterpret_cast<char*>(outSpirv.data()), header.BlobSize);
		ASH_PROCESS_ERROR(static_cast<size_t>(file.gcount()) == header.BlobSize);

		// Verify cache hash
		DigestBuilder<SHA1> dataHashBuilder{};
		dataHashBuilder.append(outSpirv.data(), header.BlobSize);
		SHA1::Digest computedHash = dataHashBuilder.finalize();
		ASH_PROCESS_ERROR((computedHash == header.CacheHash));

		file.close();
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}

	// Helper function to write to cache
	static bool write_to_cache(const SHA1::Digest& passKey, const SHA1::Digest& textKey, const std::vector<uint32_t>& spirv)
	{
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		ASH_PROCESS_ERROR(!spirv.empty());
		std::filesystem::path cachePath = get_cache_path(passKey);
		std::filesystem::path cacheDir = cachePath.parent_path();

		// Create cache directory if it doesn't exist
		if (!std::filesystem::exists(cacheDir))
		{
			std::filesystem::create_directories(cacheDir);
		}

		// Compute data hash
		DigestBuilder<SHA1> dataHashBuilder{};
		dataHashBuilder.append(spirv.data(), spirv.size() * sizeof(uint32_t));
		SHA1::Digest dataHash = dataHashBuilder.finalize();

		// Create header
		ShaderCacheIndexHeader header = {};
		memcpy(header.Magic, kMagic, 4);
		header.Version = kVersion;
		header.BlobSize = static_cast<uint32_t>(spirv.size() * sizeof(uint32_t));
		header.CompilerHash = textKey;
		header.CacheHash = dataHash;

		// Write to file
		std::ofstream file(cachePath, std::ios::binary);
		ASH_PROCESS_ERROR(file.is_open());
		file.write(reinterpret_cast<const char*>(&header), sizeof(header));
		file.write(reinterpret_cast<const char*>(spirv.data()), header.BlobSize);
		file.close();

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
	}
#endif

	VulkanShaderCompiler::~VulkanShaderCompiler()
	{
		uninit();
	}

	bool VulkanShaderCompiler::check_and_compile_shader(ShaderItem const& fileInfo, const std::string& shaderFullText, std::shared_ptr<Shader> pTargetShader)
	{
#if defined(ASH_HAS_DXC)
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		
		ASH_PROCESS_ERROR(m_pDxcCompiler);
		ASH_PROCESS_ERROR(pTargetShader);
		auto pVulkanShader = std::dynamic_pointer_cast<VulkanShader>(pTargetShader);
		ASH_PROCESS_ERROR(pVulkanShader);

		// Build pass key hash (from shader file info)
		DigestBuilder<SHA1> passKeyBuilder{};
		passKeyBuilder.append(fileInfo.sourceShaderPath ? fileInfo.sourceShaderPath : "");
		passKeyBuilder.append(fileInfo.userShaderPath ? fileInfo.userShaderPath : "");
		passKeyBuilder.append(fileInfo.macroDefine ? fileInfo.macroDefine : "");
		passKeyBuilder.append(fileInfo.entryPoint ? fileInfo.entryPoint : "");
		passKeyBuilder.append(fileInfo.stage);
		SHA1::Digest passKey = passKeyBuilder.finalize();

		// Build shader text key hash (from full shader text + compiler version)
		DigestBuilder<SHA1> shaderTextBuilder{};
		shaderTextBuilder.append(shaderFullText);
		shaderTextBuilder.append(fileInfo.stage);
		shaderTextBuilder.append(std::string("dxc-spirv-vulkan1.0-dxlayout-inverty"));
		SHA1::Digest textKey = shaderTextBuilder.finalize();

		// Try to read from cache
		std::vector<uint32_t> spirvCode;
		bool cacheHit = read_from_cache(passKey, textKey, spirvCode);
		if (cacheHit && spirvCode.empty())
		{
			HLogWarning("Ignoring invalid empty shader cache for {}", fileInfo.sourceShaderPath ? fileInfo.sourceShaderPath : "<null>");
			std::error_code remove_error{};
			std::filesystem::remove(get_cache_path(passKey), remove_error);
			cacheHit = false;
		}

		if (!cacheHit)
		{
			// Cache miss - compile shader
			std::string errorMsg;
			bool compileResult = m_pDxcCompiler->compile_shader_from_text(shaderFullText, fileInfo, spirvCode, errorMsg);
			if (!compileResult && !errorMsg.empty())
			{
				HLogError("Failed to compile shader to SPIR-V: {}", errorMsg);
			}
			ASH_PROCESS_ERROR(compileResult);
			ASH_PROCESS_ERROR(!spirvCode.empty());

			// Write to cache
			write_to_cache(passKey, textKey, spirvCode);
		}
		if (spirvCode.empty())
		{
			HLogError("Shader compile produced empty SPIR-V for {}", fileInfo.sourceShaderPath ? fileInfo.sourceShaderPath : "<null>");
		}
		ASH_PROCESS_ERROR(!spirvCode.empty());

		ParseResult reflection_data{};
		bResult = parse_binary_spv(spirvCode.data(), spirvCode.size(), fileInfo.stage, &reflection_data);
		ASH_PROCESS_ERROR(bResult);
		pVulkanShader->set_compiled_binary(
			{
				fileInfo.sourceShaderPath,
				fileInfo.userShaderPath,
				nullptr,
				fileInfo.macroDefine,
				fileInfo.entryPoint,
				fileInfo.stage
			},
			std::move(spirvCode),
			reflection_data);

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
#else
		(void)fileInfo;
		(void)shaderFullText;
		(void)pTargetShader;
		HLogError("Vulkan runtime shader compilation requires DXC, but this build was compiled without ASH_HAS_DXC.");
		return false;
#endif
	}

	bool VulkanShaderCompiler::init()
	{
#if defined(ASH_HAS_DXC)
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		m_pDxcCompiler = std::make_unique<DxcCompiler_VK>();
		ASH_PROCESS_ERROR(m_pDxcCompiler);
		bResult = m_pDxcCompiler->init();
		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
#else
		HLogError("Vulkan shader compiler initialization failed because DXC is unavailable on this platform/build.");
		return false;
#endif
	}

	void VulkanShaderCompiler::uninit()
	{
		if (m_pDxcCompiler)
		{
			m_pDxcCompiler->uninit();
			m_pDxcCompiler.reset();
		}
	}

	/************* dxc compiler for vulkan ****************/
	bool DxcCompiler_VK::init()
	{
#if defined(ASH_HAS_DXC)
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		
		// Initialize DXC context
		m_pDxcContext = std::make_unique<AshDXCContext>();
		ASH_PROCESS_ERROR(m_pDxcContext);
		bResult = m_pDxcContext->init();
		ASH_PROCESS_ERROR(bResult);

		// Get utils from context (we need to access m_pUtils)
		// Since AshDXCContext doesn't expose utils directly, we'll create our own
		HRESULT hrRes = DxcCreateInstance(CLSID_DxcUtils, IID_PPV_ARGS(&m_pUtils));
		ASH_PROCESS_ERROR(hrRes == S_OK);

		// Create compiler
		hrRes = DxcCreateInstance(CLSID_DxcCompiler, IID_PPV_ARGS(&m_pCompiler));
		ASH_PROCESS_ERROR(hrRes == S_OK);

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
#else
		return false;
#endif
	}

	void DxcCompiler_VK::uninit()
	{
		if (m_pCompiler)
		{
			m_pCompiler.Release();
		}
		if (m_pUtils)
		{
			m_pUtils.Release();
		}
		if (m_pDxcContext)
		{
			m_pDxcContext->uninit();
			m_pDxcContext.reset();
		}
	}

	bool DxcCompiler_VK::compile_shader_from_text(std::string const& pFullText, const ShaderItem& item, std::vector<uint32_t>& outSpirv, std::string& outErrorMsg)
	{
#if defined(ASH_HAS_DXC)
		ASH_SAFE_EXECUTE_BEGIN(bResult);
		
		outSpirv.clear();
		outErrorMsg.clear();

		ASH_PROCESS_ERROR(m_pCompiler);
		ASH_PROCESS_ERROR(m_pUtils);
		ASH_PROCESS_ERROR(!pFullText.empty());
		ASH_PROCESS_ERROR(item.entryPoint);

		// Create source blob
		CComPtr<IDxcBlobEncoding> pSourceBlob = nullptr;
		HRESULT hrRes = m_pUtils->CreateBlobFromPinned(pFullText.data(), static_cast<uint32_t>(pFullText.size()), CP_UTF8, &pSourceBlob);
		ASH_PROCESS_ERROR(hrRes == S_OK);

		// Prepare arguments for SPIR-V compilation
		std::vector<std::wstring> argumentStrings;
		argumentStrings.reserve(16);

		std::vector<LPCWSTR> arguments;
		arguments.reserve(16);

		// Source file name for diagnostics.
		std::filesystem::path shaderPath = item.sourceShaderPath ? item.sourceShaderPath : "shader.hlsl";
		std::wstring sourceName = shaderPath.filename().wstring();
		argumentStrings.push_back(sourceName);

		// Entry point
		std::wstring entryPointW = utf8_to_wstring(item.entryPoint);
		argumentStrings.push_back(L"-E");
		argumentStrings.push_back(entryPointW);

		std::wstring profile = get_spirv_profile(item.stage);
		
		argumentStrings.push_back(L"-T");
		argumentStrings.push_back(profile);

		// SPIR-V target
		argumentStrings.push_back(L"-spirv");

		// Additional options for Vulkan
		argumentStrings.push_back(L"-fspv-target-env=vulkan1.0");
		argumentStrings.push_back(L"-fvk-use-dx-layout");
		if (shader_stage_supports_vk_invert_y(item.stage))
		{
			argumentStrings.push_back(L"-fvk-invert-y");
		}

		// Prepare defines as command-line -D arguments for IDxcCompiler3.
		if (item.macroDefine && strlen(item.macroDefine) > 0)
		{
			// Parse macro defines (format: "MACRO1=value1;MACRO2=value2")
			std::string macroStr = item.macroDefine;
			size_t start = 0;
			while (start < macroStr.length())
			{
				size_t semicolon = macroStr.find(';', start);
				std::string macro = (semicolon == std::string::npos) ? macroStr.substr(start) : macroStr.substr(start, semicolon - start);
				
				// Trim whitespace
				while (!macro.empty() && (macro[0] == ' ' || macro[0] == '\t'))
					macro.erase(0, 1);
				
				size_t equals = macro.find('=');
				if (equals != std::string::npos)
				{
					std::string name = macro.substr(0, equals);
					std::string value = macro.substr(equals + 1);
					// Trim whitespace from name and value
					while (!name.empty() && (name.back() == ' ' || name.back() == '\t'))
						name.pop_back();
					while (!value.empty() && (value[0] == ' ' || value[0] == '\t'))
						value.erase(0, 1);
					
					argumentStrings.push_back(L"-D");
					argumentStrings.push_back(utf8_to_wstring(name + "=" + value));
				}
				else if (!macro.empty())
				{
					argumentStrings.push_back(L"-D");
					argumentStrings.push_back(utf8_to_wstring(macro));
				}

				if (semicolon == std::string::npos)
					break;
				start = semicolon + 1;
			}
		}
		for (const std::wstring& argument : argumentStrings)
		{
			arguments.push_back(argument.c_str());
		}

		// Get include handler
		CComPtr<DXCIncludeHandler> pIncludeHandler = m_pDxcContext->get_default_includer();
		ASH_PROCESS_ERROR(pIncludeHandler);

		// Compile
		DxcBuffer sourceBuffer = {};
		sourceBuffer.Ptr = pSourceBlob->GetBufferPointer();
		sourceBuffer.Size = pSourceBlob->GetBufferSize();
		sourceBuffer.Encoding = CP_UTF8; 

		CComPtr<IDxcResult> pResults = nullptr;
		hrRes = m_pCompiler->Compile(&sourceBuffer, arguments.data(), static_cast<uint32_t>(arguments.size()),
			pIncludeHandler.p, IID_PPV_ARGS(&pResults));
		ASH_PROCESS_ERROR(hrRes == S_OK);
		ASH_PROCESS_ERROR(pResults);

		// Get error buffer
		CComPtr<IDxcBlobUtf8> pErrors = nullptr;
		hrRes = pResults->GetOutput(DXC_OUT_ERRORS, IID_PPV_ARGS(&pErrors), nullptr);
		if (pErrors && pErrors->GetStringLength() > 0)
		{
			outErrorMsg = std::string(pErrors->GetStringPointer(), pErrors->GetStringLength());
		}

		// Check compilation status
		HRESULT compileStatus = S_OK;
		hrRes = pResults->GetStatus(&compileStatus);
		ASH_PROCESS_ERROR(hrRes == S_OK);
		if (FAILED(compileStatus) && outErrorMsg.empty())
		{
			outErrorMsg = "DXC compile failed without diagnostics.";
		}
		ASH_PROCESS_ERROR(compileStatus == S_OK);

		// Get compiled SPIR-V
		CComPtr<IDxcBlob> pSpirvBlob = nullptr;
		hrRes = pResults->GetOutput(DXC_OUT_OBJECT, IID_PPV_ARGS(&pSpirvBlob), nullptr);
		ASH_PROCESS_ERROR(hrRes == S_OK);
		ASH_PROCESS_ERROR(pSpirvBlob);
		if (pSpirvBlob && pSpirvBlob->GetBufferSize() == 0 && outErrorMsg.empty())
		{
			outErrorMsg = "DXC returned an empty shader object.";
		}
		ASH_PROCESS_ERROR(pSpirvBlob->GetBufferSize() > 0);
		ASH_PROCESS_ERROR((pSpirvBlob->GetBufferSize() % sizeof(uint32_t)) == 0);

		// Copy SPIR-V to output
		uint32_t* spirvData = reinterpret_cast<uint32_t*>(pSpirvBlob->GetBufferPointer());
		size_t spirvSize = pSpirvBlob->GetBufferSize() / sizeof(uint32_t);
		outSpirv.assign(spirvData, spirvData + spirvSize);

		ASH_SAFE_EXECUTE_END(bResult);
		return bResult;
#else
		(void)pFullText;
		(void)item;
		outSpirv.clear();
		outErrorMsg = "DXC is unavailable on this platform/build.";
		return false;
#endif
	}
}
