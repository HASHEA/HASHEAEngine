#pragma once

#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/Renderer.h"
#include <filesystem>
#include <memory>
#include <string>
#include <vector>

namespace AshSandbox
{
	struct SandboxTestContext
	{
		AshEngine::Renderer* renderer = nullptr;
		AshEngine::AssetDatabase* asset_database = nullptr;
		std::filesystem::path asset_root{};
		std::filesystem::path report_root{};
		uint64_t frame_index = 0;
	};

	class ISandboxTest
	{
	public:
		virtual ~ISandboxTest() = default;

	public:
		virtual auto get_name() const -> const char* = 0;
		virtual auto wants_render() const -> bool
		{
			return false;
		}
		virtual auto on_startup(SandboxTestContext& context, std::string& out_error) -> bool = 0;
		virtual auto on_render(SandboxTestContext& context, const std::shared_ptr<AshEngine::RenderTarget>& output_target, std::string& out_error) -> bool
		{
			(void)context;
			(void)output_target;
			out_error.clear();
			return true;
		}
		virtual auto on_shutdown(SandboxTestContext& context) -> void
		{
			(void)context;
		}
	};

	auto create_default_sandbox_tests() -> std::vector<std::unique_ptr<ISandboxTest>>;
}
