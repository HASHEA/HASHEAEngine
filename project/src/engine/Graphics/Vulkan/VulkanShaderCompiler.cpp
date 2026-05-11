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
#include <array>
#include <algorithm>
#include <cctype>
#include <cstdlib>
#include <string_view>

namespace RHI
{
#if defined(ASH_HAS_DXC)
	namespace
	{
		static size_t find_matching_brace(std::string_view text, size_t open_brace_index)
		{
			if (open_brace_index == std::string_view::npos || open_brace_index >= text.size() || text[open_brace_index] != '{')
			{
				return std::string_view::npos;
			}

			uint32_t brace_depth = 0;
			for (size_t i = open_brace_index; i < text.size(); ++i)
			{
				if (text[i] == '{')
				{
					++brace_depth;
				}
				else if (text[i] == '}')
				{
					H_ASSERTLOG(brace_depth > 0, "Invalid brace depth while rewriting Vulkan root constants.");
					--brace_depth;
					if (brace_depth == 0)
					{
						return i;
					}
				}
			}

			return std::string_view::npos;
		}

		static bool is_hlsl_identifier_char(char c)
		{
			const unsigned char ch = static_cast<unsigned char>(c);
			return std::isalnum(ch) != 0 || c == '_';
		}

		static bool is_root_constant_block_name(std::string_view name)
		{
			return name == "AshRootConstants" || name == "RootConstants";
		}

		static std::string_view trim_string_view(std::string_view text)
		{
			size_t begin = 0;
			while (begin < text.size() && std::isspace(static_cast<unsigned char>(text[begin])) != 0)
			{
				++begin;
			}

			size_t end = text.size();
			while (end > begin && std::isspace(static_cast<unsigned char>(text[end - 1])) != 0)
			{
				--end;
			}

			return text.substr(begin, end - begin);
		}

		static std::string extract_root_constant_member_name(std::string_view statement)
		{
			statement = trim_string_view(statement);
			if (statement.empty())
			{
				return {};
			}

			const size_t colon_index = statement.find(':');
			if (colon_index != std::string_view::npos)
			{
				statement = trim_string_view(statement.substr(0, colon_index));
			}

			if (statement.empty() || statement.find('(') != std::string_view::npos)
			{
				return {};
			}

			size_t end = statement.size();
			while (end > 0)
			{
				end = trim_string_view(statement.substr(0, end)).size();
				if (end == 0)
				{
					return {};
				}

				if (statement[end - 1] != ']')
				{
					break;
				}

				const size_t open_bracket = statement.rfind('[', end - 1);
				if (open_bracket == std::string_view::npos)
				{
					break;
				}
				statement = trim_string_view(statement.substr(0, open_bracket));
				end = statement.size();
			}

			if (statement.empty())
			{
				return {};
			}

			size_t identifier_end = statement.size();
			while (identifier_end > 0 && std::isspace(static_cast<unsigned char>(statement[identifier_end - 1])) != 0)
			{
				--identifier_end;
			}
			size_t identifier_begin = identifier_end;
			while (identifier_begin > 0 && is_hlsl_identifier_char(statement[identifier_begin - 1]))
			{
				--identifier_begin;
			}

			if (identifier_begin == identifier_end)
			{
				return {};
			}

			return std::string(statement.substr(identifier_begin, identifier_end - identifier_begin));
		}

		static std::vector<std::string> extract_root_constant_member_names(std::string_view block_body)
		{
			std::vector<std::string> member_names{};
			std::string current_statement{};
			uint32_t nested_brace_depth = 0;
			bool in_line_comment = false;
			bool in_block_comment = false;

			for (size_t i = 0; i < block_body.size(); ++i)
			{
				const char current_char = block_body[i];
				const char next_char = (i + 1 < block_body.size()) ? block_body[i + 1] : '\0';

				if (in_line_comment)
				{
					if (current_char == '\n')
					{
						in_line_comment = false;
					}
					continue;
				}
				if (in_block_comment)
				{
					if (current_char == '*' && next_char == '/')
					{
						in_block_comment = false;
						++i;
					}
					continue;
				}

				if (current_char == '/' && next_char == '/')
				{
					in_line_comment = true;
					++i;
					continue;
				}
				if (current_char == '/' && next_char == '*')
				{
					in_block_comment = true;
					++i;
					continue;
				}

				if (current_char == '{')
				{
					++nested_brace_depth;
				}
				else if (current_char == '}')
				{
					if (nested_brace_depth > 0)
					{
						--nested_brace_depth;
					}
				}

				if (current_char == ';' && nested_brace_depth == 0)
				{
					const std::string member_name = extract_root_constant_member_name(current_statement);
					if (!member_name.empty())
					{
						member_names.push_back(member_name);
					}
					current_statement.clear();
					continue;
				}

				current_statement.push_back(current_char);
			}

			return member_names;
		}

		static std::string sanitize_root_constant_block_body_for_vulkan(std::string_view block_body)
		{
			std::string sanitized_body(block_body);
			size_t line_begin = 0;
			while (line_begin < sanitized_body.size())
			{
				size_t line_end = sanitized_body.find('\n', line_begin);
				const size_t line_length = (line_end == std::string::npos) ? (sanitized_body.size() - line_begin) : (line_end - line_begin);
				std::string line = sanitized_body.substr(line_begin, line_length);

				const size_t content_begin = line.find_first_not_of(" \t");
				if (content_begin != std::string::npos &&
					line.compare(content_begin, 5, "const") == 0)
				{
					const size_t qualifier_end = content_begin + 5;
					if (qualifier_end < line.size() &&
						std::isspace(static_cast<unsigned char>(line[qualifier_end])) != 0)
					{
						size_t erase_end = qualifier_end;
						while (erase_end < line.size() &&
							std::isspace(static_cast<unsigned char>(line[erase_end])) != 0)
						{
							++erase_end;
						}
						line.erase(content_begin, erase_end - content_begin);
					}
				}

				sanitized_body.replace(line_begin, line_length, line);
				if (line_end == std::string::npos)
				{
					break;
				}
				line_begin += line.size() + 1;
			}

			return sanitized_body;
		}

		static bool rewrite_next_root_constant_block_for_vulkan(std::string& shader_text, size_t search_begin, size_t& out_next_search_begin)
		{
			static constexpr std::string_view k_cbuffer_keyword = "cbuffer";
			size_t cbuffer_index = shader_text.find(k_cbuffer_keyword, search_begin);
			while (cbuffer_index != std::string::npos)
			{
				const bool has_identifier_before = cbuffer_index > 0 && is_hlsl_identifier_char(shader_text[cbuffer_index - 1]);
				const size_t keyword_end = cbuffer_index + k_cbuffer_keyword.size();
				const bool has_identifier_after = keyword_end < shader_text.size() && is_hlsl_identifier_char(shader_text[keyword_end]);
				if (!has_identifier_before && !has_identifier_after)
				{
					break;
				}
				cbuffer_index = shader_text.find(k_cbuffer_keyword, keyword_end);
			}

			if (cbuffer_index == std::string::npos)
			{
				out_next_search_begin = std::string::npos;
				return false;
			}

			size_t name_begin = cbuffer_index + k_cbuffer_keyword.size();
			while (name_begin < shader_text.size() && std::isspace(static_cast<unsigned char>(shader_text[name_begin])) != 0)
			{
				++name_begin;
			}
			if (name_begin >= shader_text.size())
			{
				out_next_search_begin = std::string::npos;
				return false;
			}

			size_t name_end = name_begin;
			while (name_end < shader_text.size() && is_hlsl_identifier_char(shader_text[name_end]))
			{
				++name_end;
			}

			const std::string_view block_name = std::string_view(shader_text).substr(name_begin, name_end - name_begin);
			if (!is_root_constant_block_name(block_name))
			{
				out_next_search_begin = name_end;
				return false;
			}

			const size_t open_brace_index = shader_text.find('{', name_end);
			if (open_brace_index == std::string::npos)
			{
				out_next_search_begin = std::string::npos;
				return false;
			}

			const size_t close_brace_index = find_matching_brace(shader_text, open_brace_index);
			if (close_brace_index == std::string::npos)
			{
				out_next_search_begin = std::string::npos;
				return false;
			}

			const size_t trailing_semicolon_index = shader_text.find(';', close_brace_index);
			if (trailing_semicolon_index == std::string::npos)
			{
				out_next_search_begin = std::string::npos;
				return false;
			}

			const std::string_view block_body = std::string_view(shader_text).substr(open_brace_index + 1, close_brace_index - open_brace_index - 1);
			const std::string sanitized_block_body = sanitize_root_constant_block_body_for_vulkan(block_body);
			const std::vector<std::string> member_names = extract_root_constant_member_names(block_body);
			const std::string variable_name(block_name);
			const std::string type_name = "AshVulkanPushConstants_" + variable_name;

			std::string replacement{};
			replacement.reserve(block_body.size() + member_names.size() * 32u + 128u);
			replacement += "struct ";
			replacement += type_name;
			replacement += "\n{";
			replacement += sanitized_block_body;
			replacement += "\n};\n[[vk::push_constant]] ";
			replacement += type_name;
			replacement += ' ';
			replacement += variable_name;
			replacement += ";\n";
			for (const std::string& member_name : member_names)
			{
				replacement += "#define ";
				replacement += member_name;
				replacement += ' ';
				replacement += variable_name;
				replacement += '.';
				replacement += member_name;
				replacement += '\n';
			}

			shader_text.replace(cbuffer_index, trailing_semicolon_index - cbuffer_index + 1, replacement);
			out_next_search_begin = cbuffer_index + replacement.size();
			return true;
		}

		static bool rewrite_root_constant_blocks_for_vulkan(std::string& shader_text)
		{
			bool rewritten = false;
			size_t search_begin = 0;
			while (search_begin != std::string::npos && search_begin < shader_text.size())
			{
				size_t next_search_begin = std::string::npos;
				const bool block_rewritten = rewrite_next_root_constant_block_for_vulkan(shader_text, search_begin, next_search_begin);
				if (block_rewritten)
				{
					rewritten = true;
				}

				if (next_search_begin == std::string::npos)
				{
					break;
				}
				search_begin = next_search_begin;
			}

			return rewritten;
		}
	}

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

		std::string vulkanShaderText = shaderFullText;
		rewrite_root_constant_blocks_for_vulkan(vulkanShaderText);

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
		shaderTextBuilder.append(vulkanShaderText);
		shaderTextBuilder.append(fileInfo.stage);
		shaderTextBuilder.append(std::string("dxc-spirv-vulkan1.0-dxlayout-bindingshift-inverty-rootconstrewrite-vertexdeclloc-v3"));
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
			bool compileResult = m_pDxcCompiler->compile_shader_from_text(vulkanShaderText, fileInfo, spirvCode, errorMsg);
			if (!compileResult && !errorMsg.empty())
			{
				if (fileInfo.sourceShaderPath != nullptr)
				{
					std::error_code dump_error{};
					const std::filesystem::path dump_directory = "Intermediate/logs/shader-debug";
					std::filesystem::create_directories(dump_directory, dump_error);
					if (!dump_error)
					{
						std::filesystem::path source_path = fileInfo.sourceShaderPath;
						std::string dump_name = source_path.stem().string();
						if (dump_name.empty())
						{
							dump_name = "Shader";
						}
						std::ofstream dump_file(
							dump_directory / (dump_name + ".vulkan.rewritten.hlsl"),
							std::ios::binary);
						if (dump_file.is_open())
						{
							dump_file.write(vulkanShaderText.data(), static_cast<std::streamsize>(vulkanShaderText.size()));
						}
					}
				}
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
		ShaderCreation compiled_shader_creation{};
		compiled_shader_creation.pBaseShaderPath = fileInfo.sourceShaderPath;
		compiled_shader_creation.pUserShaderPath = fileInfo.userShaderPath;
		compiled_shader_creation.pGeneratedBindingsPath = fileInfo.generatedBindingsPath;
		compiled_shader_creation.pShaderMacro = fileInfo.macroDefine;
		compiled_shader_creation.pEntryPoint = fileInfo.entryPoint;
		compiled_shader_creation.type = fileInfo.stage;
		pVulkanShader->set_compiled_binary(
			compiled_shader_creation,
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
		argumentStrings.push_back(L"-fvk-auto-shift-bindings");
		argumentStrings.push_back(L"-fvk-b-shift");
		argumentStrings.push_back(L"0");
		argumentStrings.push_back(L"all");
		argumentStrings.push_back(L"-fvk-t-shift");
		argumentStrings.push_back(L"128");
		argumentStrings.push_back(L"all");
		argumentStrings.push_back(L"-fvk-s-shift");
		argumentStrings.push_back(L"256");
		argumentStrings.push_back(L"all");
		argumentStrings.push_back(L"-fvk-u-shift");
		argumentStrings.push_back(L"384");
		argumentStrings.push_back(L"all");
		if (shader_stage_supports_vk_invert_y(item.stage))
		{
			argumentStrings.push_back(L"-fvk-invert-y");
		}
		argumentStrings.push_back(L"-D");
		argumentStrings.push_back(L"ASH_VULKAN=1");
		argumentStrings.push_back(L"-D");
		argumentStrings.push_back(L"ASH_MESH_VERTEX_POSITION_ATTR=[[vk::location(0)]]");
		argumentStrings.push_back(L"-D");
		argumentStrings.push_back(L"ASH_MESH_VERTEX_NORMAL_ATTR=[[vk::location(1)]]");
		argumentStrings.push_back(L"-D");
		argumentStrings.push_back(L"ASH_MESH_VERTEX_TANGENT_ATTR=[[vk::location(2)]]");
		argumentStrings.push_back(L"-D");
		argumentStrings.push_back(L"ASH_MESH_VERTEX_TEXCOORD0_ATTR=[[vk::location(3)]]");
		argumentStrings.push_back(L"-D");
		argumentStrings.push_back(L"ASH_MESH_VERTEX_TEXCOORD1_ATTR=[[vk::location(4)]]");
		argumentStrings.push_back(L"-D");
		argumentStrings.push_back(L"ASH_MESH_VERTEX_COLOR0_ATTR=[[vk::location(5)]]");

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
