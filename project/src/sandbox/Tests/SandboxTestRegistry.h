#pragma once

#include "Function/Asset/AssetDatabase.h"
#include "Function/Render/RenderScene.h"
#include "Function/Render/SceneView.h"
#include "Function/Render/Renderer.h"
#include "Function/Scene/Scene.h"
#include <filesystem>
#include <functional>
#include <memory>
#include <mutex>
#include <string>
#include <vector>

namespace AshSandbox
{
	enum class SandboxSceneRenderFlowStatus : uint8_t
	{
		Idle = 0,
		ScenePrepared,
		AwaitingLogicIntegration,
		VisibleFrameReady,
		AwaitingRenderIntegration,
		RenderSubmitted,
		Failed
	};

	struct SandboxSceneRenderFlowState
	{
		mutable std::mutex mutex{};
		AshEngine::Scene scene{};
		std::filesystem::path sample_asset_path{};
		std::filesystem::path generated_scene_path{};
		AshEngine::RenderScene render_scene{};
		AshEngine::SceneView scene_view{};
		std::shared_ptr<AshEngine::VisibleRenderFrame> visible_frame = nullptr;
		uint64_t startup_frame_index = 0;
		uint64_t logic_tick_count = 0;
		uint64_t render_tick_count = 0;
		uint64_t visible_frame_build_count = 0;
		uint64_t render_submission_count = 0;
		SandboxSceneRenderFlowStatus status = SandboxSceneRenderFlowStatus::Idle;
		std::string status_detail{};
		bool scene_prepared = false;
		bool visible_frame_ready = false;
		bool render_submission_exercised = false;
		bool logic_hook_wait_logged = false;
		bool render_hook_wait_logged = false;
	};

	struct SandboxTestContext;

	using SandboxSceneRenderBuildVisibleFrameHook = std::function<bool(
		SandboxTestContext& context,
		SandboxSceneRenderFlowState& state,
		std::string& out_error)>;

	using SandboxSceneRenderSubmitFrameHook = std::function<bool(
		SandboxTestContext& context,
		SandboxSceneRenderFlowState& state,
		const std::shared_ptr<AshEngine::RenderTarget>& output_target,
		std::string& out_error)>;

	struct SandboxSceneRenderFlowHooks
	{
		SandboxSceneRenderBuildVisibleFrameHook build_visible_frame{};
		SandboxSceneRenderSubmitFrameHook submit_frame{};
	};

	struct SandboxTestContext
	{
		AshEngine::Renderer* renderer = nullptr;
		AshEngine::AssetDatabase* asset_database = nullptr;
		std::filesystem::path asset_root{};
		std::filesystem::path report_root{};
		uint64_t frame_index = 0;
		bool logic_thread_enabled = false;
		std::function<void()> request_exit{};
		SandboxSceneRenderFlowState* scene_render_flow = nullptr;
		SandboxSceneRenderFlowHooks* scene_render_hooks = nullptr;
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
		virtual auto wants_logic_update() const -> bool
		{
			return false;
		}
		virtual auto on_startup(SandboxTestContext& context, std::string& out_error) -> bool = 0;
		virtual auto on_logic_update(SandboxTestContext& context, std::string& out_error) -> bool
		{
			(void)context;
			out_error.clear();
			return true;
		}
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

	auto get_sandbox_scene_render_flow_status_name(SandboxSceneRenderFlowStatus status) -> const char*;
	auto create_default_sandbox_tests() -> std::vector<std::unique_ptr<ISandboxTest>>;
}
