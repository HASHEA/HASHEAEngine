# Vulkan Constant Buffer Stage Visibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct Vulkan `ConstBuffer` barriers so uniform reads are visible to the currently supported vertex, fragment, and compute program stages, with deterministic policy coverage and a dual-backend GPU self-test.

**Architecture:** Keep `AshResourceState::ConstBuffer` and all public RHI interfaces unchanged. Move the Vulkan-native stage mask into one internal `inline constexpr` policy consumed by production and doctest, then add an opt-in pre-frame GPU chain that uploads two GPU-only constant buffers, consumes one in compute and one in fragment, and performs exact tile readback on Vulkan and DX12. The GPU chain uses an explicit `UAVCompute -> SRVGraphics` transition for its intermediate buffer and never introduces UAV-to-UAV synchronization.

**Tech Stack:** C++17, doctest, Vulkan 1.x/Volk, DirectX 12, HLSL/DXC, Premake5, MSBuild, PowerShell validation scripts.

---

## File structure and ownership

- Create `project/src/engine/Graphics/Vulkan/VulkanBarrierPolicy.h`: the sole Vulkan-native `ConstBuffer` stage-mask source of truth.
- Modify `project/src/engine/Graphics/Vulkan/VulkanCommandBuffer.cpp`: consume the policy without changing access masks, barrier count, layouts, or tracking.
- Create `project/src/tests/Graphics/vulkan_barrier_policy_tests.cpp`: deterministic native-mask regression test.
- Modify `project/src/tests/premake5.lua`: expose only Vulkan SDK and Volk headers to Tests.
- Create `project/src/engine/Function/Render/RHIConstantBufferSelfTest.h/.cpp`: one-shot dual-backend GPU validation and exact pixel checking.
- Create `project/src/engine/Shaders/SelfTest/RHIConstantBufferSelfTest.hlsl`: compute and graphics stages for the GPU chain.
- Modify `project/src/engine/EntryPoint.h` and `project/src/engine/Function/Application.h/.cpp`: parse, store, and execute `--rhi-selftest-constant-buffer`.
- Modify `project/src/tests/Function/application_automation_tests.cpp`: deterministic CLI parsing coverage.
- Modify `.github/workflows/ci.yml`: run the new self-test beside indirect self-test on WARP and lavapipe.
- Modify `docs/specs/modules/graphics.md`: record the corrected state contract, queue-family assumption, and validation entry point.
- Modify `docs/specs/modules/application.md`: record the new opt-in command-line contract and failure propagation.
- Modify `docs/sdd/SDD-2026-07-13-vulkan-const-buffer-stage-visibility.md`: mark Done only after all gates and review pass.

The worktree is `D:\workspace\AshEngine\HASHEAEngine\.worktrees\remediation-const-buffer` on `codex/remediation-const-buffer`. Never stage another worktree or use directory-wide `git add`. `project/src/tests/Graphics/` is also being used by a separate worktree for `gpu_timing_contract_tests.cpp`; ownership here is limited to `vulkan_barrier_policy_tests.cpp`.

### Task 1: Preflight concurrency and record the paired performance baseline

**Files:**
- Read: all active worktree status/diff metadata
- Read: `product/config/Engine.ini`
- Generated only: `Intermediate/test-reports/perf-gate/**`

- [x] **Step 1: Confirm branch ancestry and exact dirty boundary**

Run:

```powershell
git fetch origin main
git status --short --branch
git rev-parse HEAD
git rev-parse origin/main
git worktree list --porcelain
```

Expected: this worktree is ahead of `origin/main` only by the approved SDD/plan commits; no production files are dirty.

- [x] **Step 2: Recheck active worktree hotspots**

Run `git status --short`, `git diff --name-only`, and `git diff --cached --name-only` in the shared checkout plus the particle, terrain, and GPU-observability worktrees. Stop before editing a file if another active worktree owns the same exact path. Different files under `project/src/tests/Graphics/` are not a path conflict.

- [x] **Step 3: Wait for CPU/GPU exclusivity before baseline sampling**

Run:

```powershell
Get-Process | Where-Object { $_.ProcessName -match 'Sandbox|Editor|Tests|MSBuild|premake' } |
    Select-Object ProcessName,Id,CPU,StartTime,Path
```

Expected before PerfGate: no `Sandbox`, `Editor`, `Tests`, or active build command from another worktree. Do not terminate another session's process; recheck after it exits.

- [x] **Step 4: Record the pre-change Standard profile**

Run:

```bat
RunPerfGate.bat -Profile Standard
```

Expected: exit code 0 and no `FAIL`. Record report directory plus CPU avg/P95/P99, private bytes, engine heap, and draw calls in the task notes. `WARN` is recorded with its metric and does not authorize changing `tools/perf/perf_gate_baselines.json`.

Execution evidence (2026-07-13): report `Intermediate/test-reports/perf-gate/20260713-115306`, overall PASS, no warnings or failures.

| Target | Backend | CPU avg / P95 / P99 ms | Private peak MB | Engine heap peak MB | Draw avg |
| --- | --- | --- | ---: | ---: | ---: |
| Sandbox | Vulkan | 9.1156 / 13.4890 / 17.0402 | 3232.38 | 2.1059 | 232 |
| Sandbox | DX12 | 9.9510 / 15.5266 / 19.5370 | 2748.27 | 0.0136 | 232 |
| Editor | Vulkan | 11.8921 / 16.9454 / 20.9764 | 2976.25 | 2.1107 | 234 |
| Editor | DX12 | 11.9115 / 16.0095 / 20.1694 | 2524.22 | 0.0136 | 234 |

### Task 2: Extract the existing vertex-only production policy without changing behavior

**Files:**
- Create: `project/src/engine/Graphics/Vulkan/VulkanBarrierPolicy.h`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanCommandBuffer.cpp:1-15,143-148`

- [ ] **Step 1: Create the behavior-preserving internal policy**

Create `VulkanBarrierPolicy.h` with exactly:

```cpp
#pragma once

#include <volk/volk.h>

namespace RHI::VulkanBarrierPolicy
{
	// VulkanCommandBuffer instances currently come from the graphics queue family,
	// so graphics and compute shader stage bits are legal on every current command buffer.
	// A future dedicated compute-only command buffer must make this policy queue-aware.
	[[nodiscard]] inline constexpr auto const_buffer_stage_mask() noexcept
		-> VkPipelineStageFlags
	{
		return VK_PIPELINE_STAGE_VERTEX_SHADER_BIT;
	}
} // namespace RHI::VulkanBarrierPolicy
```

Use `<volk/volk.h>`, not `<vulkan/vulkan.h>`: including Vulkan prototypes before Volk causes a compile-time conflict. Do not include `VulkanWrapper.h`, because that unnecessarily pulls VMA into Tests.

- [ ] **Step 2: Connect production to the unchanged source of truth**

Add this local include to `VulkanCommandBuffer.cpp`:

```cpp
#include "VulkanBarrierPolicy.h"
```

Replace only the constant-buffer stage line:

```cpp
if (has_any_flags((uint32_t)RHIAccess, (uint32_t)AshResourceState::ConstBuffer))
{
	H_ASSERT(ResourceType != AshBarrier::EType::Texture);
	StageFlags |= VulkanBarrierPolicy::const_buffer_stage_mask();
	AccessFlags |= VK_ACCESS_UNIFORM_READ_BIT;
}
```

- [ ] **Step 3: Verify behavior-preserving GREEN**

Run:

```bat
RunTests.bat Debug
```

Expected: all existing cases and assertions pass. This proves the extraction did not alter behavior before the new regression test is introduced.

- [ ] **Step 4: Commit the extraction only**

Run:

```powershell
git add -- project/src/engine/Graphics/Vulkan/VulkanBarrierPolicy.h project/src/engine/Graphics/Vulkan/VulkanCommandBuffer.cpp
git diff --cached --check
git diff --cached --name-status
git commit -m "refactor(vulkan): centralize constant buffer stage policy"
```

Expected staged paths: exactly the two files above.

### Task 3: Prove RED, then minimally correct the native stage mask

**Files:**
- Create: `project/src/tests/Graphics/vulkan_barrier_policy_tests.cpp`
- Modify: `project/src/tests/premake5.lua:10-22`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanBarrierPolicy.h`

- [ ] **Step 1: Expose Vulkan and Volk headers to Tests**

Add these entries to the existing `includedirs` block in `project/src/tests/premake5.lua`:

```lua
		thirdparty .. "/VulkanSDK/include",
		thirdparty .. "/volk/include",
```

Do not add links, VMA, or `ASH_HAS_VULKAN`; the policy is header-only and uses native flag types only.

- [ ] **Step 2: Write the failing native-policy doctest**

Create `vulkan_barrier_policy_tests.cpp`:

```cpp
#include "Graphics/Vulkan/VulkanBarrierPolicy.h"
#include "doctest.h"

TEST_CASE("Vulkan barrier policy maps ConstBuffer to vertex, fragment, and compute stages")
{
	constexpr VkPipelineStageFlags expected =
		static_cast<VkPipelineStageFlags>(
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);

	constexpr VkPipelineStageFlags actual =
		RHI::VulkanBarrierPolicy::const_buffer_stage_mask();

	CHECK((actual & VK_PIPELINE_STAGE_VERTEX_SHADER_BIT) != 0u);
	CHECK((actual & VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT) != 0u);
	CHECK((actual & VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT) != 0u);
	CHECK(actual == expected);
	CHECK((actual & VK_PIPELINE_STAGE_ALL_GRAPHICS_BIT) == 0u);
	CHECK((actual & VK_PIPELINE_STAGE_ALL_COMMANDS_BIT) == 0u);
}
```

Use doctest assertions rather than `static_assert`, so the RED evidence is an executable test failure.

- [ ] **Step 3: Regenerate the solution and verify RED**

Delete only the generated solution after proving its resolved path belongs to this worktree, then regenerate and run:

```powershell
$root = (Resolve-Path .).Path
$solution = Join-Path $root "AshEngine.sln"
if (-not $solution.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved solution path escaped the worktree: $solution"
}
if (Test-Path -LiteralPath $solution) {
    Remove-Item -LiteralPath $solution -Force
}
cmd /c generate_vs2022.bat
cmd /c 'RunTests.bat Debug --test-case="*Vulkan barrier policy*"'
```

Expected: generation succeeds; the filtered test exits non-zero with one failed case. Fragment, compute, and exact-equality assertions fail while vertex and aggregate-bit exclusions pass.

- [ ] **Step 4: Make the minimal production change**

Change only the return expression in `const_buffer_stage_mask()`:

```cpp
		return static_cast<VkPipelineStageFlags>(
			VK_PIPELINE_STAGE_VERTEX_SHADER_BIT |
			VK_PIPELINE_STAGE_FRAGMENT_SHADER_BIT |
			VK_PIPELINE_STAGE_COMPUTE_SHADER_BIT);
```

- [ ] **Step 5: Verify targeted and full GREEN**

Run:

```bat
RunTests.bat Debug --test-case="*Vulkan barrier policy*"
RunTests.bat Debug
```

Expected: the filtered case reports 1 case / 6 assertions passing, then the full suite reports zero failures.

- [ ] **Step 6: Commit the fix and regression test**

Run:

```powershell
git add -- project/src/engine/Graphics/Vulkan/VulkanBarrierPolicy.h project/src/tests/Graphics/vulkan_barrier_policy_tests.cpp project/src/tests/premake5.lua
git diff --cached --check
git diff --cached --name-status
git commit -m "fix(vulkan): expose constant buffers to program shader stages"
```

### Task 4: Add CLI coverage before wiring the dual-backend GPU self-test

**Files:**
- Modify: `project/src/tests/Function/application_automation_tests.cpp`
- Modify: `project/src/engine/EntryPoint.h:291-304,608-611`
- Modify: `project/src/engine/Function/Application.h:124-130,235-237`
- Modify: `project/src/engine/Function/Application.cpp:1-15,391-398`
- Create: `project/src/engine/Function/Render/RHIConstantBufferSelfTest.h/.cpp`
- Create: `project/src/engine/Shaders/SelfTest/RHIConstantBufferSelfTest.hlsl`

- [ ] **Step 1: Write the failing CLI test**

Append this doctest to `application_automation_tests.cpp`:

```cpp
TEST_CASE("entry point recognizes the constant-buffer RHI self-test flag")
{
	char executable[] = "Tests.exe";
	char self_test_option[] = "--rhi-selftest-constant-buffer";
	char* requested_argv[] = { executable, self_test_option };
	CHECK(should_run_rhi_constant_buffer_self_test(2, requested_argv));

	char unrelated_option[] = "--rhi-selftest-indirect";
	char* unrelated_argv[] = { executable, unrelated_option };
	CHECK_FALSE(should_run_rhi_constant_buffer_self_test(2, unrelated_argv));
}
```

- [ ] **Step 2: Verify the CLI test is RED for the missing feature**

Run:

```bat
RunTests.bat Debug --test-case="*constant-buffer RHI self-test flag*"
```

Expected: compilation fails because `should_run_rhi_constant_buffer_self_test` does not exist. This is the intended missing-feature RED, not a syntax or include failure.

- [ ] **Step 3: Add the parser, setter, storage, and execution hook**

Add beside the indirect helper in `EntryPoint.h`:

```cpp
// SDD-2026-07-13-vulkan-const-buffer-stage-visibility: opt-in dual-backend CB visibility self-test.
static bool should_run_rhi_constant_buffer_self_test(int argc, char* argv[])
{
	for (int32_t argumentIndex = 1; argumentIndex < argc; ++argumentIndex)
	{
		const std::string argument = argv[argumentIndex] ? argv[argumentIndex] : "";
		if (argument == "--rhi-selftest-constant-buffer")
		{
			return true;
		}
	}
	return false;
}
```

Beside the indirect setter invocation in `main`, add:

```cpp
	if (should_run_rhi_constant_buffer_self_test(argc, argv))
	{
		application->set_rhi_constant_buffer_self_test_requested(true);
	}
```

Add to `Application.h` public setters:

```cpp
	auto set_rhi_constant_buffer_self_test_requested(bool requested) -> void
	{
		rhiConstantBufferSelfTestRequested = requested;
	}
```

Add beside the indirect request member:

```cpp
		bool rhiConstantBufferSelfTestRequested = false;
```

Include `Function/Render/RHIConstantBufferSelfTest.h` in `Application.cpp`. Replace the pre-frame self-test block with this combined fail-fast block so both requested tests execute independently, but no application startup/frame reuses a command buffer after either test fails:

```cpp
		bool rhiSelfTestFailed = false;
		if (rhiIndirectSelfTestRequested)
		{
			if (!run_rhi_indirect_self_test(graphicsContext))
			{
				rhiSelfTestFailed = true;
			}
		}
		if (rhiConstantBufferSelfTestRequested)
		{
			if (!run_rhi_constant_buffer_self_test(graphicsContext))
			{
				rhiSelfTestFailed = true;
			}
		}
		if (rhiSelfTestFailed)
		{
			runtimeFailureDetected.store(true, std::memory_order_release);
			started = false;
			return false;
		}
```

Do not short-circuit one self-test when the other fails. The shared boolean is evaluated only after both requested diagnostics have run; the return occurs before `_on_startup`, `perfGateController.begin()`, and the first frame.

- [ ] **Step 4: Add the shader with independent compute and fragment constant buffers**

Create `RHIConstantBufferSelfTest.hlsl`:

```hlsl
// Constant-buffer visibility self-test. The two cbuffers belong to separate programs,
// so both may use b0 without sharing one descriptor layout.
cbuffer ComputeConstants : register(b0)
{
    uint4 ComputeRgba;
};

RWByteAddressBuffer ComputeResultUAV : register(u0);

[numthreads(1, 1, 1)]
void CSMain()
{
    ComputeResultUAV.Store4(0, ComputeRgba);
}

struct VSOutput
{
    float4 position : SV_Position;
};

VSOutput VSMain(uint vertex_id : SV_VertexID)
{
    float2 uv = float2((vertex_id << 1) & 2, vertex_id & 2);
    VSOutput output;
    output.position = float4(uv * float2(2.0, -2.0) + float2(-1.0, 1.0), 0.0, 1.0);
    return output;
}

cbuffer FragmentConstants : register(b1)
{
    uint4 FragmentRgba;
};

ByteAddressBuffer ComputeResultSRV : register(t0);

float4 PSMain(VSOutput input) : SV_Target0
{
    const uint4 rgba = input.position.x < 32.0
        ? ComputeResultSRV.Load4(0)
        : FragmentRgba;
    return float4(rgba) / 255.0;
}
```

- [ ] **Step 5: Add the self-test declaration**

Create `RHIConstantBufferSelfTest.h`:

```cpp
#pragma once

namespace RHI
{
	class GraphicsContext;
}

namespace AshEngine
{
	// Runs before the first frame when --rhi-selftest-constant-buffer is present.
	// Logs a [RHISelfTest] PASS/FAIL marker and returns false on any setup,
	// recording, submission-result, or exact-readback failure.
	auto run_rhi_constant_buffer_self_test(RHI::GraphicsContext* context) -> bool;
}
```

- [ ] **Step 6: Implement the exact dual-backend GPU chain**

Implement `RHIConstantBufferSelfTest.cpp` with the following required structure and values:

```cpp
#include "RHIConstantBufferSelfTest.h"
#include "Base/hlog.h"
#include "Graphics/GraphicsContext.h"
#include "Graphics/CommandBuffer.h"
#include "Graphics/Buffer.h"
#include "Graphics/Texture.h"
#include "Graphics/RenderPass.h"
#include "Graphics/Framebuffer.h"
#include "Graphics/RenderProgram.h"
#include "Graphics/Shader.h"
#include <array>
#include <cmath>
#include <cstddef>
#include <cstdint>
#include <memory>

namespace AshEngine
{
	namespace
	{
		constexpr uint32_t k_rt_size = 64u;
		constexpr uint32_t k_row_pitch = 256u;
		constexpr const char* k_shader_path =
			"project/src/engine/Shaders/SelfTest/RHIConstantBufferSelfTest.hlsl";
		constexpr std::array<uint8_t, 4> k_compute_expected = { 17u, 101u, 203u, 255u };
		constexpr std::array<uint8_t, 4> k_fragment_expected = { 239u, 53u, 7u, 255u };

		struct alignas(16) ConstantPayload
		{
			uint32_t rgba[4]{};
			uint32_t padding[60]{};
		};
		static_assert(sizeof(ConstantPayload) == 256u);

		auto fail(const char* what) -> bool
		{
			HLogError("[RHISelfTest] constant buffer visibility FAIL: {}", what);
			return false;
		}

		auto make_command_buffer_ref(RHI::CommandBuffer* command_buffer)
			-> std::shared_ptr<RHI::CommandBuffer>
		{
			return std::shared_ptr<RHI::CommandBuffer>(command_buffer, [](RHI::CommandBuffer*) {});
		}

		auto create_stage_shader(RHI::GraphicsContext* context,
			RHI::AshShaderStageFlagBits stage, const char* entry_point)
			-> std::shared_ptr<RHI::Shader>
		{
			RHI::ShaderCreation creation{};
			creation.pBaseShaderPath = k_shader_path;
			creation.pEntryPoint = entry_point;
			creation.type = stage;
			return context->create_shader(creation);
		}

		auto tile_matches(const uint8_t* mapped, uint32_t x_begin, uint32_t x_end,
			uint32_t y_begin, uint32_t y_end,
			const std::array<uint8_t, 4>& expected) -> bool
		{
			for (uint32_t y = y_begin; y < y_end; ++y)
			{
				for (uint32_t x = x_begin; x < x_end; ++x)
				{
					const uint8_t* pixel = mapped + static_cast<size_t>(y) * k_row_pitch +
						static_cast<size_t>(x) * 4u;
					for (uint32_t channel = 0; channel < 4u; ++channel)
					{
						if (std::abs(static_cast<int>(pixel[channel]) -
							static_cast<int>(expected[channel])) > 1)
						{
							HLogError(
								"[RHISelfTest] constant buffer mismatch at ({}, {}), channel {}: actual={}, expected={}",
								x, y, channel, pixel[channel], expected[channel]);
							return false;
						}
					}
				}
			}
			return true;
		}
	}

	auto run_rhi_constant_buffer_self_test(RHI::GraphicsContext* context) -> bool
	{
		if (!context)
		{
			return fail("graphics context is null");
		}

		RHI::BufferCreation compute_cb_ci = RHI::BufferCreation::get_ubo_creation(256u);
		compute_cb_ci.name = "RHISelfTestComputeConstants";
		RHI::BufferCreation fragment_cb_ci = RHI::BufferCreation::get_ubo_creation(256u);
		fragment_cb_ci.name = "RHISelfTestFragmentConstants";
		RHI::BufferCreation result_ci = RHI::BufferCreation::get_gpu_rwbuffer_creation(16u);
		result_ci.name = "RHISelfTestComputedResult";
		RHI::BufferCreation readback_ci =
			RHI::BufferCreation::get_cpur_staging_buffer_creation(k_row_pitch * k_rt_size);
		readback_ci.name = "RHISelfTestConstantBufferReadback";

		auto compute_constants = context->create_buffer(compute_cb_ci);
		auto fragment_constants = context->create_buffer(fragment_cb_ci);
		auto computed_result = context->create_buffer(result_ci);
		auto readback = context->create_buffer(readback_ci);
		if (!compute_constants || !fragment_constants || !computed_result || !readback)
		{
			return fail("buffer creation failed");
		}
		compute_constants->immediate_deletion = true;
		fragment_constants->immediate_deletion = true;
		computed_result->immediate_deletion = true;
		readback->immediate_deletion = true;

		auto compute_cbv = compute_constants->get_default_cbv();
		auto fragment_cbv = fragment_constants->get_default_cbv();
		auto result_uav = computed_result->get_default_uav();
		auto result_srv = computed_result->get_default_srv();
		if (!compute_cbv || !fragment_cbv || !result_uav || !result_srv)
		{
			return fail("buffer view creation failed");
		}
		compute_cbv->immediate_deletion = true;
		fragment_cbv->immediate_deletion = true;
		result_uav->immediate_deletion = true;
		result_srv->immediate_deletion = true;

		RHI::TextureCreation rt_ci{};
		rt_ci.width = static_cast<uint16_t>(k_rt_size);
		rt_ci.height = static_cast<uint16_t>(k_rt_size);
		rt_ci.format = RHI::ASH_FORMAT_R8G8B8A8_UNORM;
		rt_ci.type = RHI::Ash_Texture2D;
		rt_ci.uUsageFlags = RHI::ASH_TEXTURE_USAGE_COLOR_ATTACHMENT_BIT |
			RHI::ASH_TEXTURE_USAGE_SAMPLED_BIT;
		rt_ci.name = "RHISelfTestConstantBufferRT";
		auto render_target = context->create_texture(rt_ci);
		if (!render_target)
		{
			return fail("render target creation failed");
		}
		render_target->immediate_deletion = true;

		RHI::RenderPassCreation pass_ci{};
		pass_ci.add_attachment(RHI::ASH_FORMAT_R8G8B8A8_UNORM,
			RHI::AshResourceState::SRVGraphics, RHI::AshLoadOption::ASH_LOAD_CLEAR);
		pass_ci.set_name("RHISelfTestConstantBufferPass");
		auto render_pass = context->create_render_pass(pass_ci);
		if (!render_pass)
		{
			return fail("render pass creation failed");
		}
		render_pass->immediate_deletion = true;

		RHI::FramebufferCreation fb_ci{};
		fb_ci.renderPass = render_pass;
		fb_ci.colorAttachments.push_back(render_target);
		fb_ci.width = static_cast<uint16_t>(k_rt_size);
		fb_ci.height = static_cast<uint16_t>(k_rt_size);
		fb_ci.layers = 1u;
		fb_ci.name = "RHISelfTestConstantBufferFramebuffer";
		auto framebuffer = context->create_framebuffer(fb_ci);
		if (!framebuffer)
		{
			return fail("framebuffer creation failed");
		}
		framebuffer->immediate_deletion = true;

		auto compute_shader =
			create_stage_shader(context, RHI::ASH_SHADER_STAGE_COMPUTE_BIT, "CSMain");
		if (!compute_shader)
		{
			return fail("compute shader creation failed");
		}
		RHI::ComputeProgramCreateDesc compute_desc{};
		compute_desc.pipeline.name = "RHISelfTestConstantBufferCompute";
		compute_desc.pipeline.shaders.add_stage(
			compute_shader, RHI::ASH_SHADER_STAGE_COMPUTE_BIT, "CSMain");
		auto compute_program = context->create_compute_render_program(compute_desc);
		if (!compute_program)
		{
			return fail("compute program creation failed");
		}
		compute_program->begin_bind()
			.add_bind_cbv("ComputeConstants", compute_cbv)
			.add_bind_uav("ComputeResultUAV", result_uav);
		if (!compute_program->end_bind())
		{
			return fail("compute program bind commit failed");
		}

		auto vertex_shader =
			create_stage_shader(context, RHI::ASH_SHADER_STAGE_VERTEX_BIT, "VSMain");
		auto fragment_shader =
			create_stage_shader(context, RHI::ASH_SHADER_STAGE_FRAGMENT_BIT, "PSMain");
		if (!vertex_shader || !fragment_shader)
		{
			return fail("graphics shader creation failed");
		}
		RHI::GraphicProgramCreateDesc graphics_desc{};
		graphics_desc.pipeline.name = "RHISelfTestConstantBufferGraphics";
		graphics_desc.pipeline.render_pass = render_pass;
		graphics_desc.pipeline.shaders.add_stage(
			vertex_shader, RHI::ASH_SHADER_STAGE_VERTEX_BIT, "VSMain");
		graphics_desc.pipeline.shaders.add_stage(
			fragment_shader, RHI::ASH_SHADER_STAGE_FRAGMENT_BIT, "PSMain");
		auto graphics_program = context->create_graphics_render_program(graphics_desc);
		if (!graphics_program)
		{
			return fail("graphics program creation failed");
		}
		graphics_program->begin_bind()
			.add_bind_cbv("FragmentConstants", fragment_cbv)
			.add_bind_srv("ComputeResultSRV", result_srv);
		if (!graphics_program->end_bind())
		{
			return fail("graphics program bind commit failed");
		}

		ConstantPayload compute_data{};
		compute_data.rgba[0] = 17u;
		compute_data.rgba[1] = 101u;
		compute_data.rgba[2] = 203u;
		compute_data.rgba[3] = 255u;
		ConstantPayload fragment_data{};
		fragment_data.rgba[0] = 239u;
		fragment_data.rgba[1] = 53u;
		fragment_data.rgba[2] = 7u;
		fragment_data.rgba[3] = 255u;

		RHI::CommandBuffer* cb = context->get_command_buffer(0u);
		if (!cb)
		{
			return fail("command buffer acquisition failed");
		}
		cb->clear_error();
		cb->begin_record();
		if (cb->get_state() != RHI::ASH_Recording)
		{
			return fail("command buffer did not enter recording state");
		}
		bool recorded = true;
		recorded = cb->cmd_update_sub_resource(compute_constants, 0u, 256u,
			&compute_data) && recorded;
		recorded = cb->cmd_update_sub_resource(fragment_constants, 0u, 256u,
			&fragment_data) && recorded;
		recorded = cb->cmd_transition_resource_state({
			{ compute_constants, RHI::AshResourceState::ConstBuffer },
			{ fragment_constants, RHI::AshResourceState::ConstBuffer },
			{ computed_result, RHI::AshResourceState::UAVCompute } }) && recorded;
		if (recorded)
		{
			auto cb_ref = make_command_buffer_ref(cb);
			recorded = compute_program->apply(cb_ref);
			if (recorded)
			{
				cb->cmd_dispatch(1u, 1u, 1u);
				recorded = cb->cmd_transition_resource_state({ computed_result,
					RHI::AshResourceState::UAVCompute,
					RHI::AshResourceState::SRVGraphics });
			}
		}
		if (recorded)
		{
			cb->cmd_begin_render_pass(framebuffer, "RHISelfTestConstantBuffer");
			const bool graphics_applied =
				graphics_program->apply(make_command_buffer_ref(cb));
			if (graphics_applied)
			{
				RHI::Viewport viewport{};
				viewport.rect.width = static_cast<uint16_t>(k_rt_size);
				viewport.rect.height = static_cast<uint16_t>(k_rt_size);
				viewport.max_depth = 1.0f;
				cb->cmd_set_viewport(viewport);
				cb->cmd_set_scissor(viewport.rect);
				cb->cmd_draw(3u);
			}
			cb->cmd_end_render_pass();
			recorded = graphics_applied;
		}
		if (recorded)
		{
			recorded = cb->cmd_copy_texture_to_buffer(
				render_target, readback, 0u, k_row_pitch);
		}
		cb->end_record();

		if (!recorded || cb->has_error() || cb->get_state() != RHI::ASH_Ended)
		{
			if (cb->has_error())
			{
				HLogError("[RHISelfTest] constant buffer command error: {}",
					cb->get_last_error());
			}
			return fail("command recording failed");
		}

		RHI::SubmitInfo submit{};
		submit.cmds = cb;
		submit.cmdCount = 1u;
		context->submit_immediately(submit);
		context->wait_idle();

		const uint8_t* mapped = readback->get_mapped_data();
		if (!mapped)
		{
			return fail("readback buffer has no mapped data");
		}
		const bool compute_tile_ok =
			tile_matches(mapped, 8u, 24u, 16u, 48u, k_compute_expected);
		const bool fragment_tile_ok =
			tile_matches(mapped, 40u, 56u, 16u, 48u, k_fragment_expected);
		if (!compute_tile_ok || !fragment_tile_ok)
		{
			return fail("exact tile readback did not match expected colors");
		}

		HLogInfo("[RHISelfTest] constant buffer visibility PASS");
		return true;
	}
}
```

The intermediate transition must remain exactly `UAVCompute -> SRVGraphics`. Vulkan then emits compute shader write to graphics shader read memory scope; DX12 emits unordered-access to shader-resource state transition. It is not a UAV-to-UAV dependency and must not be replaced by a new barrier API.

- [ ] **Step 7: Regenerate, build, and verify CLI GREEN**

Run:

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="*constant-buffer RHI self-test flag*"
build_sandbox.bat Debug
```

Expected: the CLI case passes and Sandbox Debug builds.

- [ ] **Step 8: Prove the GPU self-test can fail**

Temporarily change only `k_compute_expected[0]` in `RHIConstantBufferSelfTest.cpp` from `17u` to `20u`, rebuild Sandbox Debug, then run:

```bat
run.bat sandbox vulkan Debug --smoke-test-seconds=120 --rhi-selftest-constant-buffer
```

Expected: non-zero exit and a `[RHISelfTest] constant buffer visibility FAIL` log with the exact tile/channel mismatch. Restore `17u` with `apply_patch`, rebuild, and confirm `git diff` contains no injected expectation change.

- [ ] **Step 9: Verify real GPU GREEN on both backends**

Run serially, with no other engine or gate process active:

```bat
run.bat sandbox vulkan Debug --smoke-test-seconds=120 --rhi-selftest-constant-buffer
run.bat sandbox dx12 Debug --smoke-test-seconds=120 --rhi-selftest-constant-buffer
```

Expected: both exit 0 and log `[RHISelfTest] constant buffer visibility PASS`.

- [ ] **Step 10: Commit the self-test and wiring**

Run:

```powershell
git add -- project/src/tests/Function/application_automation_tests.cpp project/src/engine/EntryPoint.h project/src/engine/Function/Application.h project/src/engine/Function/Application.cpp project/src/engine/Function/Render/RHIConstantBufferSelfTest.h project/src/engine/Function/Render/RHIConstantBufferSelfTest.cpp project/src/engine/Shaders/SelfTest/RHIConstantBufferSelfTest.hlsl
git diff --cached --check
git diff --cached --name-status
git commit -m "test(rhi): validate constant buffer visibility across backends"
```

### Task 5: Wire software-backend CI and update the long-term contract

**Files:**
- Modify: `.github/workflows/ci.yml:94-99,119`
- Modify: `docs/specs/modules/graphics.md`
- Modify: `docs/specs/modules/application.md`

- [ ] **Step 1: Extend both software-backend commands**

Change the DX12/WARP and Vulkan/lavapipe commands to:

```yaml
run: call run.bat sandbox dx12 Release --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer
```

```yaml
run: call run.bat sandbox vulkan Release --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer
```

Update the nearby comment to state that indirect and constant-buffer self-tests are independent and either failure propagates to a non-zero smoke exit.

- [ ] **Step 2: Document the current contract and queue assumption**

Add to `docs/specs/modules/graphics.md` under command/resource-state constraints:

```markdown
- `AshResourceState::ConstBuffer` 表示当前 `GraphicsProgram` / `ComputeProgram` 可读取的 uniform buffer。Vulkan 映射精确使用 vertex + fragment + compute shader stage 与 `VK_ACCESS_UNIFORM_READ_BIT`；不使用 `ALL_GRAPHICS` / `ALL_COMMANDS`，DX12 保持 `D3D12_RESOURCE_STATE_VERTEX_AND_CONSTANT_BUFFER`。当前 Vulkan command buffer 均来自 graphics-capable queue family；未来引入 dedicated compute-only command buffer 时，stage policy 必须按 queue capability 收窄。验证入口 `--rhi-selftest-constant-buffer` 在首帧前覆盖 compute/fragment CB 上传、消费与精确读回。
```

- [ ] **Step 3: Document the Application command-line contract**

Add `--rhi-selftest-constant-buffer` to the command-line contract table in `docs/specs/modules/application.md`. Define it as an opt-in pre-frame dual-backend diagnostic; a setup, command, validation, or readback failure sets the existing runtime-failure state and produces a non-zero readiness-smoke exit. State that it is independent from `--rhi-selftest-indirect` and that both execute when both flags are present.

- [ ] **Step 4: Run plan/tooling and documentation integrity checks**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
git diff --check
```

Expected: AIDevDoctor reports the RHI/render verification plan; whitespace check is clean.

### Task 6: Execute the full verification matrix and close the SDD

**Files:**
- Modify after evidence: `docs/sdd/SDD-2026-07-13-vulkan-const-buffer-stage-visibility.md`
- Generated only: build and gate reports

- [ ] **Step 1: Run a fresh generate and required builds**

Delete only the generated solution after proving its path remains inside this worktree, then run:

```powershell
$root = (Resolve-Path .).Path
$solution = Join-Path $root "AshEngine.sln"
if (-not $solution.StartsWith($root, [System.StringComparison]::OrdinalIgnoreCase)) {
    throw "Resolved solution path escaped the worktree: $solution"
}
if (Test-Path -LiteralPath $solution) {
    Remove-Item -LiteralPath $solution -Force
}
cmd /c generate_vs2022.bat
cmd /c build_editor.bat Debug
cmd /c build_sandbox.bat Debug
cmd /c build_sandbox.bat Release
cmd /c RunTests.bat Debug
cmd /c RunArchGate.bat
cmd /c "run.bat all Debug --smoke-test-seconds=120"
```

Expected: every command exits 0; full doctest has zero failed cases/assertions; ArchGate has no new violation.

Then perform the required Application lifecycle check: launch `run.bat editor vulkan Debug`, open the default scene, orbit/pan the viewport camera, select one scene entity, open and close one standard panel, and exit normally. Expected: the default scene renders, interaction remains responsive, and the latest Editor log contains no runtime or validation error. Record the check in the final evidence; an automated smoke is not a substitute for this step.

- [ ] **Step 2: Run Debug validation on both backends**

Use `apply_patch` to enable `[VulkanValidation] Enabled=true` plus synchronization validation in this worktree's `product/config/Engine.ini`, run the Vulkan constant-buffer self-test, and scan the newly generated log for validation errors. Restore the exact original lines with `apply_patch`. Repeat with `[DX12Validation] Enabled=true` and `GpuValidation=true` for DX12. Always restore configuration in the same turn, even when the run fails.

Commands after each temporary configuration change:

```bat
run.bat sandbox vulkan Debug --smoke-test-seconds=120 --rhi-selftest-constant-buffer
run.bat sandbox dx12 Debug --smoke-test-seconds=120 --rhi-selftest-constant-buffer
```

Expected: exit 0, self-test PASS, and no validation error/corruption message. `git diff -- product/config/Engine.ini` must be empty after restoration.

- [ ] **Step 3: Run visual and final performance gates serially**

After confirming no other Sandbox/Editor/build/gate process is active, run:

```bat
RunRenderGate.bat
RunPerfGate.bat -Profile Standard
```

Expected: RenderGate passes every golden and cross-backend comparison without blessing. PerfGate exits 0 with no `FAIL`. Compare the final report with Task 1 on CPU avg/P95/P99, private bytes, engine heap, and draw calls; an over-threshold regression blocks completion, while any `WARN` is recorded with an evidence-based explanation.

- [ ] **Step 4: Request independent code review**

Provide the reviewer with the approved SDD, this plan, base SHA `18d5f5108712c6d6c9792b5e7cf47ed583099820`, current HEAD, and the exact diff. Fix all Critical and Important findings, rerun the narrowest affected verification, then rerun `RunTests.bat Debug` and `git diff --check`.

- [ ] **Step 5: Mark the design Done only after review and gates**

Change the SDD status from `Approved` to `Done`. Add a concise implementation outcome recording the exact stage union, dual-backend self-test, gate results, and any accepted PerfGate warning. Do not alter the approved UAV non-goals.

- [ ] **Step 6: Final integrity review and commit**

Run:

```powershell
git status --short
git diff --check
git diff --name-status origin/main...HEAD
git diff --stat origin/main...HEAD
```

Stage only remaining task files explicitly, inspect `git diff --cached`, and commit:

```powershell
git commit -m "docs(graphics): record constant buffer visibility contract"
```

- [ ] **Step 7: Push and create a ready PR**

Run:

```powershell
git push -u origin codex/remediation-const-buffer
gh pr create --base main --head codex/remediation-const-buffer --title "fix(vulkan): correct constant buffer shader visibility" --body-file Intermediate/pr-body-const-buffer.md
```

Before the command, create ignored `Intermediate/pr-body-const-buffer.md` with `apply_patch` and this exact structure after every listed gate has passed:

```markdown
## Summary
- fix Vulkan `ConstBuffer` visibility from vertex-only to the exact vertex + fragment + compute stage union
- keep the public RHI ABI and barrier count unchanged; add a production-shared native policy regression test
- add an opt-in dual-backend constant-buffer GPU self-test and enable it beside indirect self-test in software-backend CI

## Synchronization boundary
- no UAV-to-UAV barrier or implicit dependency was added
- the self-test intermediate uses the existing explicit `UAVCompute -> SRVGraphics` write-to-read transition

## Validation
- deterministic policy doctest: observed RED on fragment/compute/exact scope, then GREEN
- full `RunTests.bat Debug`
- fresh Editor Debug, Sandbox Debug, and Sandbox Release builds
- `RunArchGate.bat`
- Vulkan and DX12 Debug validation self-tests
- `run.bat all Debug --smoke-test-seconds=120`
- `RunRenderGate.bat`
- paired pre/post `RunPerfGate.bat -Profile Standard`
- `AIDevDoctor.ps1 -Mode ValidatePlan`
```

Remove the temporary PR body file after successful creation without touching any tracked file.
