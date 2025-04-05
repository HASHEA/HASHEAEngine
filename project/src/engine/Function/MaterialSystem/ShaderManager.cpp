#include "ShaderManager.h"
#include "Base/hfile.h"
#include "Base/hassert.h"
#include "Base/hstring.h"
#include "Graphics/Shader.h"
#include "glslang/Public/ShaderLang.h"
#include "glslang/SPIRV/GlslangToSpv.h"
#include <filesystem>
#include <string>
namespace AshEngine
{
#include <glslang/Public/ShaderLang.h>

	class AshIncluder : public glslang::TShader::Includer {
	public:
		// 包含目录列表
		std::vector<std::string> includeDirs;
		// 添加包含路径
		void addDir(const std::string& dir) {
			includeDirs.push_back(dir);
		}

		// 处理包含请求
		virtual IncludeResult* includeLocal(
			const char* headerName,
			const char* includerName,
			size_t inclusionDepth) override {

			// 在包含目录中搜索文件
			for (const auto& dir : includeDirs) {
				std::filesystem::path fullPath(dir);
				fullPath = fullPath / headerName;

				if (file_exists(fullPath.u8string().c_str())) {
					size_t size = 0;
					char* data = file_read_text(fullPath.u8string().c_str(), nullptr, &size);
					return new IncludeResult(fullPath.u8string(), data, size, nullptr);
				}
			}
			return nullptr;
		} 
		virtual void releaseInclude(glslang::TShader::Includer::IncludeResult* result) override {
			if (result) {
				Ash_Free(nullptr, result->headerData);
				delete result;                
			}
		}
	};


	constexpr const uint32_t GLSLVERSION = 460;
	constexpr const char* k_shader_cache_location = "Caches\\ShaderCaches";
	constexpr const char* k_shader_cache_extension = ".shadercache";
	constexpr const char* k_shader_include_dir = "assets\\Ash-shaders";
	
	ShaderManager* ShaderManager::instance = nullptr;
	bool validate_cache(const std::filesystem::path& sourcePath, const std::filesystem::path& cachePath)
	{
		if (!std::filesystem::exists(sourcePath)) return false;
		if (!std::filesystem::exists(cachePath)) return false;
		auto srcTime = std::filesystem::last_write_time(sourcePath);
		auto cacheTime = std::filesystem::last_write_time(cachePath);
		return srcTime <= cacheTime;
	}

	auto ShaderManager::load_shader(const char* path) -> std::shared_ptr<Shader>
	{
		return get()->_load_shader_internal(path);
	}
	auto ShaderManager::_init() -> void
	{
		if (!directory_exists(k_shader_cache_location))
		{
			directory_create(k_shader_cache_location);
		}
		_init_glslang();
		includer = Ash_New<AshIncluder>();
		includer->addDir(k_shader_include_dir);
	}
	auto ShaderManager::_shutdown() -> void
	{
		Ash_Delete(nullptr,includer);
		_shutdown_glslang();
	}
	auto ShaderManager::_load_shader_internal(const char* path) -> std::shared_ptr<Shader>
	{
		HLogInfo("Loading Shader : {0} !", path);
		bool bNeedCompile = false;
		std::vector<uint32_t> binCode;
		size_t codeSize = 0;
		StringBuffer pathBuffer;
		pathBuffer.init(1024, nullptr);
		char* shaderFileName = pathBuffer.append_get_f("%s", path);
		file_name_from_path(shaderFileName);
		std::filesystem::path internalPath(shaderFileName);
		internalPath += k_shader_cache_extension;
		std::filesystem::path absoluteCachePath(k_shader_cache_location);
		absoluteCachePath = absoluteCachePath / internalPath;
		std::string u8String = absoluteCachePath.u8string();
		const char* cachePath = u8String.c_str();
		if (validate_cache(path, cachePath))
		{
			auto binCodeChar = file_read_binary(cachePath, nullptr, &codeSize);
			H_ASSERT(binCodeChar);
			H_ASSERT(codeSize % sizeof(uint32_t) == 0);
			binCode.resize(codeSize / sizeof(uint32_t));
			memcpy(binCode.data(), binCodeChar, codeSize);
		}
		else
		{
			bNeedCompile = true;
		}
		if (bNeedCompile)
		{
			binCode = _compile_shader(path, shaderFileName);
			if (binCode.size() <= 0)
			{
				HLogError("Compile Shader Failed When Compiling : {}", shaderFileName);
				pathBuffer.shutdown();
				return nullptr;
			}
			size_t byteSize = binCode.size() * sizeof(uint32_t);
			file_write_binary(cachePath, binCode.data(), byteSize);
		}
		pathBuffer.shutdown();
		RHI::ShaderCreation sci{};
		return std::shared_ptr<Shader>();
	}
	auto ShaderManager::_compile_shader(const char* path, const char* name) -> std::vector<uint32_t>
	{
		HLogInfo("Compiling Shader : {0} !", path);
		std::vector<uint32_t> spirv;
		//load file
		size_t fileSize = 0;
		char* src = file_read_text(path,nullptr, &fileSize);
		EShLanguage lang = EShLangVertex;
		glslang::TShader shader(lang);
		shader.setStrings(&src, 1);
		shader.setEntryPoint("main");
		shader.setSourceEntryPoint("main");
		TBuiltInResource resources{};
		//EShMessages messages = static_cast<EShMessages>(EShMsgSpvRules | EShMsgVulkanRules);
		EShMessages messages = static_cast<EShMessages>(EShMsgDefault);
		if (!shader.parse(&resources, GLSLVERSION, false, messages, *includer)) {
			HLogError("Compling Shader : [{0}] Error: {1}", name,shader.getInfoLog());
			Ash_Free(nullptr, src);
			return spirv;
		}

		glslang::TProgram program;
		program.addShader(&shader);
		if (!program.link(messages)) {
			HLogError("Linking program : [{0}] Error: {1}", name, program.getInfoLog());
			Ash_Free(nullptr,src);
			return spirv;
		}
		glslang::GlslangToSpv(*program.getIntermediate(lang), spirv);
		Ash_Free(nullptr, src);
		return spirv;
	}
	auto ShaderManager::_init_glslang() -> void
	{
		H_ASSERTLOG(glslang::InitializeProcess(), "Fatal: failed to init glslang !");
	}
	auto ShaderManager::_shutdown_glslang() -> void
	{
		glslang::FinalizeProcess();
	}
}