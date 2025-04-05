#pragma once
#include <memory>
#include "Base/hmemory.h"
#include "Graphics/RHICommon.h"
#include <unordered_map>
namespace RHI
{
	class Shader;
};
namespace AshEngine
{
	
	class AshIncluder;
	class ShaderManager
	{
	public:
		ShaderManager() = default;
		virtual ~ShaderManager() {};
	public:
		/*static interfaces*/
		static auto load_shader(const char* path) -> std::shared_ptr<RHI::Shader>;
		
		static auto init() -> void
		{
			ShaderManager::get()->_init();
		};
		static auto shutdown() -> void
		{
			if (instance)
			{
				instance->_shutdown();
			}
			Ash_Delete(nullptr, instance);
		};
	
	
	private:
		static auto get() -> ShaderManager*
		{
			if (!instance)
			{
				instance = Ash_New<ShaderManager>();
			
			}
			return instance;
		};
	
	private:
		auto _load_shader_internal(const char* path) -> std::shared_ptr<RHI::Shader>;
		auto _init() -> void;
		auto _shutdown() -> void;
		auto _compile_shader(const char* path,const char* name, RHI::AshShaderStageFlagBits)-> std::vector<uint32_t>;
		auto _init_glslang() -> void;
		auto _shutdown_glslang() -> void;
	private:
		static ShaderManager* instance;
		AshIncluder* includer = nullptr;
		std::unordered_map<uint64_t, std::shared_ptr<RHI::Shader>> m_mapShader;
	};
};