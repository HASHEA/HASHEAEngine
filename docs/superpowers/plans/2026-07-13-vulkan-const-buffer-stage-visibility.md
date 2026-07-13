# Vulkan Constant Buffer Stage Visibility Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Correct Vulkan `ConstBuffer` barriers so uniform reads are visible to the currently supported vertex, fragment, and compute program stages, with deterministic policy coverage and a dual-backend GPU self-test.

**Architecture:** Keep `AshResourceState::ConstBuffer` and all public RHI interfaces unchanged. Move the Vulkan-native stage mask into one internal `inline constexpr` policy consumed by production and doctest, then add an opt-in pre-frame GPU chain that uploads two GPU-only constant buffers, consumes one in compute and one in fragment, and performs exact tile readback on Vulkan and DX12. The GPU chain uses an explicit `UAVCompute -> SRVGraphics` transition for its intermediate buffer and never introduces UAV-to-UAV synchronization. Its CB and UAV transitions are separate so Vulkan stage-mask union cannot hide a regression, both self-test render targets explicitly enter `RTV`, both backends release never-submitted pending uploads before their first shutdown deletion-queue drain, and DX12 descriptor-table keys combine CPU slot address with a per-allocation serial so same-frame slot reuse cannot alias an old shader-visible table.

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
- Modify `project/src/engine/Graphics/Vulkan/VulkanContext.h/.cpp` and `project/src/engine/Graphics/DirectX12/DX12Context.cpp`: make pre-frame teardown release pending upload ownership before backend allocators/heaps.
- Modify `project/src/engine/Graphics/DirectX12/DX12Helper.hpp`, `DX12DescriptorHeap.h/.cpp`, `DX12RenderProgramBinder.h/.cpp`, and `DX12RenderProgram.cpp`: propagate CPU descriptor allocation serials into shader-visible table cache identity.
- Create `project/src/tests/Graphics/dx12_descriptor_table_cache_tests.cpp`: deterministic cache-identity coverage for handle reuse and arrays.
- Modify `project/src/engine/Function/Render/RHIIndirectSelfTest.cpp`: make its direct render-pass path explicitly enter `RTV` like the new diagnostic.
- Modify `project/src/tests/Function/application_automation_tests.cpp`: deterministic CLI parsing coverage.
- Modify `.github/workflows/ci.yml`: run the new self-test beside indirect self-test on WARP and lavapipe.
- Modify `docs/specs/modules/graphics.md`: record the corrected state contract, queue-family assumption, and validation entry point.
- Modify `docs/specs/modules/application.md`: record the new opt-in command-line contract and failure propagation.
- Modify `README.md`: expose the repository-level unit/architecture and constant-buffer diagnostic commands required by the documentation contract.
- Modify `docs/sdd/SDD-2026-07-13-vulkan-const-buffer-stage-visibility.md`: mark Done only after all gates and review pass.

The worktree is `D:\workspace\AshEngine\HASHEAEngine\.worktrees\remediation-const-buffer` on `codex/remediation-const-buffer`. Never stage another worktree or use directory-wide `git add`. `project/src/tests/Graphics/` is also being used by a separate worktree for `gpu_timing_contract_tests.cpp`; ownership here is limited to `vulkan_barrier_policy_tests.cpp` and `dx12_descriptor_table_cache_tests.cpp`.

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
- Modify: `project/src/engine/Graphics/Vulkan/VulkanContext.h:165-168`
- Modify: `project/src/engine/Graphics/Vulkan/VulkanContext.cpp:1655-1685`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Context.cpp:390-410`
- Modify: `project/src/engine/Graphics/DirectX12/DX12Helper.hpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12DescriptorHeap.h/.cpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12RenderProgramBinder.h/.cpp`
- Modify: `project/src/engine/Graphics/DirectX12/DX12RenderProgram.cpp`
- Modify: `project/src/engine/Function/Render/RHIIndirectSelfTest.cpp:220-230`
- Create: `project/src/engine/Function/Render/RHIConstantBufferSelfTest.h/.cpp`
- Create: `project/src/engine/Shaders/SelfTest/RHIConstantBufferSelfTest.hlsl`
- Create: `project/src/tests/Graphics/dx12_descriptor_table_cache_tests.cpp`

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
			{ fragment_constants, RHI::AshResourceState::ConstBuffer } }) && recorded;
		recorded = cb->cmd_transition_resource_state(
			{ computed_result, RHI::AshResourceState::UAVCompute }) && recorded;
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
			recorded = cb->cmd_transition_resource_state(
				{ render_target, RHI::AshResourceState::RTV });
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

Pre-fix execution evidence: the mismatch was detected and the wrapper returned 1, but the inner Vulkan process then reproduced `0xC0000005` three times. The stack was `VulkanSampler::~VulkanSampler -> DelayCommandQueue::emplace -> EnvironmentLightingPass::shutdown -> SceneRenderer::shutdown -> Application::_shutdown_runtime`. A separate run with valid pixels/self-test PASS followed only by a forced pre-`_on_startup` failure produced the same stack; therefore the self-test resource chain is excluded. Normal one-frame startup sets `currentFrame` from `UINT32_MAX` to 0 and shuts down cleanly. A sampler-only guard advanced the failure to `VulkanTexture::~VulkanTexture -> DelayCommandQueue::emplace -> SunLightShadowPass::shutdown` with the same invalid deque access. Buffer/View, Framebuffer, RenderPass, Texture/View, Sampler, and StagingBuffer all call the same unsafe accessor, so per-destructor patches cannot satisfy the acceptance criterion. The accessor fallback eliminated both access violations, but VMA then reported seven live allocations and blocked in `vmaDestroyAllocator`; leak records identify two AO textures, four deferred-light mesh buffers, and one volumetric buffer retained by the pending-upload vectors that only normal `begin_frame()` consumes.

- [ ] **Step 8a: Fix the proven pre-first-frame deletion queue root cause**

First restore the uncommitted sampler experiment so `VulkanSampler::~VulkanSampler()` again uses its original `if (immediate_deletion)` condition. Then change only `VulkanContext::get_current_frame_deletion_queue_internal()` to:

```cpp
		inline auto get_current_frame_deletion_queue_internal() -> DelayCommandQueue&
		{
			const uint32_t deletion_queue_index = currentFrame == UINT32_MAX ? 0u : currentFrame;
			return delayed_deletion_queues[deletion_queue_index];
		}
```

Queue 0 is safe for the sentinel path because pre-frame immediate GPU submissions are synchronous (`submit_immediately` waits its fence); the first real `begin_frame()` selects frame 0 before flushing queue 0, while failed startup calls `wait_idle()` and flushes all deletion queues before frame/staging/device teardown. Do not set `currentFrame` early, fabricate a frame, change flush ordering, or patch individual resource destructors.

In `VulkanContext::shutdown()`, after `wait_idle()` / cache unload and before the first `flush_delayed_deletion_queues()`, release pending uploads outside their mutex with the same swap pattern used by the normal flush path:

```cpp
		{
			std::vector<PendingBufferUpload> discarded_buffer_uploads{};
			std::vector<PendingTextureUpload> discarded_texture_uploads{};
			{
				std::scoped_lock<std::mutex> lock(pendingUploadMutex);
				discarded_buffer_uploads.swap(pendingBufferUploads);
				discarded_texture_uploads.swap(pendingTextureUploads);
			}
		}
```

The outer scope must end before the existing first deletion-queue drain, so dropping the resource shared_ptrs enqueues native destruction into queue 0 while VMA/staging/device dependencies are still alive. Do not call `_flush_pending_*_uploads()` during shutdown: that would create GPU work after teardown has begun.

Rebuild Sandbox Debug with compute expected red still set to 20 and rerun the injected Vulkan command under the existing 120-second watchdog. GREEN requires all of the following, so a watchdog-forced exit 1 cannot hide a teardown hang:

- the exact constant-buffer mismatch/`FAIL` markers are present and the wrapper returns exit 1;
- Application completes normal teardown well before the watchdog deadline;
- `VMA leak tracking: no live VMA allocations detected before allocator shutdown.` is present;
- `Fatal Error: readiness process deadline expired`, `Detected ... live VMA allocations`, `VMA leak:`, `Assertion failed`, and `0xC0000005` are absent;
- no hang, crash, watchdog timeout, or forced exit occurs.

Restore expected red to 17 with `apply_patch`, rebuild, and confirm the injected expectation diff is gone.

Stage only `project/src/engine/Graphics/Vulkan/VulkanContext.h` and `.cpp`, inspect the cached diff, and commit the atomic P0 separately:

```powershell
git add -- project/src/engine/Graphics/Vulkan/VulkanContext.h project/src/engine/Graphics/Vulkan/VulkanContext.cpp
git diff --cached --check
git diff --cached --name-status
git commit -m "fix(vulkan): make pre-frame teardown safe"
```

- [ ] **Step 8b: Close the independently reproduced DX12 pre-frame teardown failure**

Quality review found that `DX12Context::shutdown()` leaves `m_pendingBufferUploads` and `m_pendingTextureUploads` alive until context member destruction, after descriptor heaps and the D3D12MA allocator have already been shut down. The main agent reproduced this with compute expected red set to 20: the exact mismatch appeared, then D3D12MA asserted at line 8027 with `Some allocations were not freed before destruction of this memory block!`; the process did not complete teardown and was force-exited by the 120-second readiness watchdog at 120.695 seconds.

In `DX12Context::shutdown()`, after `wait_idle()` / profiler uninstall and before the existing first deletion-queue flush, release the two pending vectors outside their mutex:

```cpp
		{
			std::vector<PendingBufferUpload> discardedBufferUploads{};
			std::vector<PendingTextureUpload> discardedTextureUploads{};
			{
				std::scoped_lock<std::mutex> lock(m_pendingUploadMutex);
				discardedBufferUploads.swap(m_pendingBufferUploads);
				discardedTextureUploads.swap(m_pendingTextureUploads);
			}
		}
```

The outer scope must end before `for (auto& q : m_delayedDeletionQueues) q.flush();`. DX12 starts pre-frame at `m_currentFrame == 0`, so dropping the last resource/view ownership while the context is live enqueues native allocation and descriptor cleanup into queue 0; the existing first drain then runs it before staging buffers, descriptor heaps, and D3D12MA. Do not flush/record the pending uploads during shutdown, move heap/allocator destruction, or patch individual destructors.

Build Sandbox Debug after the shutdown change. Do not run or claim the final failure injection yet: Steps 8c and 8d still modify the diagnostic/descriptor-cache path, so both backend failure paths are deliberately deferred until Step 8e can exercise the final code.

Stage only `project/src/engine/Graphics/DirectX12/DX12Context.cpp`, inspect the cached diff, and commit separately:

```powershell
git add -- project/src/engine/Graphics/DirectX12/DX12Context.cpp
git diff --cached --check
git diff --cached --name-status
git commit -m "fix(dx12): release pending uploads before teardown"
```

- [ ] **Step 8c: Make the GPU diagnostics validation-correct and regression-sensitive**

In `RHIConstantBufferSelfTest.cpp`:

- keep the two ConstBuffer transitions together, but move `computed_result -> UAVCompute` to a separate `cmd_transition_resource_state()` call so its COMPUTE stage cannot be unioned into the CB dependency;
- before `cmd_begin_render_pass`, explicitly transition `render_target -> AshResourceState::RTV` and route failure through the existing recording-failure path;
- remove the `context->wait_idle()` immediately after `submit_immediately()`, because both backend implementations already wait their submission fence.

In `RHIIndirectSelfTest.cpp`, add and check the same explicit `render_target -> RTV` transition before its direct begin-pass call. Do not change RenderGraph or make `cmd_begin_render_pass()` hide attachment transitions.

Build Sandbox Debug, run the policy doctest, and run both diagnostics together on Vulkan and DX12. The pre-fix diagnostic RED is already recorded: validation-enabled Vulkan and DX12 both returned 0/PASS while fresh logs reported the missing RTV state; temporarily removing the production ConstBuffer compute-stage bit still returned Vulkan PASS because the unrelated UAV stage masked it. All temporary source/config changes were restored.

Post-correction Vulkan combined diagnostics passed. DX12 combined diagnostics exposed the independent descriptor-cache issue described in Step 8d, so do not stage/commit these two files or enter final validation until Step 8d is GREEN.

- [ ] **Step 8d: Make DX12 descriptor-table cache identity allocation-aware**

The controller reproduced the same DX12 combined failure both with the Step 8c diff and after restoring both self-test files exactly to `b0ab7f6`: indirect PASS, then constant-buffer compute tile `(8,16)` was `(0,0,0,0)`. Constant-only DX12 passed twice; combined failed repeatedly. Frame-fence tracing proved the constant submit signal/wait reached frame-0 target 3, and restoring the redundant constant `wait_idle()` did not help. A separately approved READBACK create-unmapped/full-range-Map A/B built successfully and still produced the same failure on its first run; it was immediately restored with target diff empty. These results exclude Step 8c, missing completion wait, and readback Map as the root cause.

The static collision is exact. Indirect caches one-descriptor compute UAV tables for CPU slots 1 and 3. Its local resources then return slots to the LIFO free-list in order 2,3,0,1. Constant allocates its compute CBV from slot 1. `DescriptorTableCacheKey` currently contains only heap type, count, and CPU pointer, so the new CBV key hits the old slot-1 UAV GPU table and skips `CopyDescriptorsSimple`; the compute shader reads zero. The bug is production-wide whenever a CPU descriptor slot is reused before the per-frame cache is cleared.

After this amended SDD is independently reviewed and reapproved, perform one disposable root-cause A/B before final implementation. In `DX12DescriptorHeap.cpp`, use `apply_patch` to make only the cache lookup miss (for example, temporarily set `const auto cached = tableCache.end();`), rebuild Sandbox Debug, and run the DX12 combined command once. Both diagnostics must PASS. Restore that exact line with `apply_patch`, assert the descriptor-heap diff is empty, and do not commit the bypass. If it does not turn GREEN, stop and return to root-cause analysis.

Write `project/src/tests/Graphics/dx12_descriptor_table_cache_tests.cpp` before production code. Include `Graphics/DirectX12/DX12DescriptorHeap.h`, `doctest.h`, `<initializer_list>`, and `<vector>`. Tests has no PCH, so `doctest.h` must be explicit in this translation unit. Define the helpers exactly as follows so the test constructs keys through the same internal factory used by production rather than duplicating key logic:

```cpp
namespace
{
    RHI::DX12DescriptorHandle make_handle(SIZE_T cpuAddress, uint64_t allocationSerial)
    {
        RHI::DX12DescriptorHandle handle{};
        handle.cpuHandle.ptr = cpuAddress;
        handle.allocationSerial = allocationSerial;
        return handle;
    }

    RHI::DX12DescriptorHeapManager::DescriptorTableCacheKey make_key(
        std::initializer_list<RHI::DX12DescriptorHandle> handles,
        D3D12_DESCRIPTOR_HEAP_TYPE heapType = D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV)
    {
        const std::vector<RHI::DX12DescriptorHandle> ownedHandles(handles);
        return RHI::DX12DescriptorHeapManager::DescriptorTableCacheKey::from_handles(
            heapType,
            ownedHandles.data(),
            static_cast<uint32_t>(ownedHandles.size()));
    }
}

TEST_CASE("DX12 descriptor table cache identity includes allocation serial")
{
    auto first = make_handle(0x1000u, 7u);
    auto same = make_handle(0x1000u, 7u);
    auto reused = make_handle(0x1000u, 8u);
    CHECK(make_key({ first }) == make_key({ same }));
    CHECK_FALSE(make_key({ first }) == make_key({ reused }));
}

TEST_CASE("DX12 descriptor table cache identity covers arrays and table shape")
{
    auto a = make_handle(0x1000u, 1u);
    auto b = make_handle(0x2000u, 2u);
    auto b_reused = make_handle(0x2000u, 3u);
    CHECK_FALSE(make_key({ a, b }) == make_key({ a, b_reused }));
    CHECK_FALSE(make_key({ a, b }) == make_key({ b, a }));
    CHECK_FALSE(make_key({ a, b }) == make_key({ a }));
    CHECK_FALSE(make_key({ a }, D3D12_DESCRIPTOR_HEAP_TYPE_CBV_SRV_UAV) ==
                make_key({ a }, D3D12_DESCRIPTOR_HEAP_TYPE_SAMPLER));
}

TEST_CASE("DX12 descriptor table cache identity covers overflow elements")
{
    auto a = make_handle(0x1000u, 1u);
    auto b = make_handle(0x2000u, 2u);
    auto c = make_handle(0x3000u, 3u);
    auto d = make_handle(0x4000u, 4u);
    auto e = make_handle(0x5000u, 5u);
    auto f = make_handle(0x6000u, 6u);
    auto g = make_handle(0x7000u, 7u);
    auto h = make_handle(0x8000u, 8u);
    auto i = make_handle(0x9000u, 9u);
    auto i_reused = make_handle(0x9000u, 10u);
    const auto original = make_key({ a, b, c, d, e, f, g, h, i });
    const auto changed = make_key({ a, b, c, d, e, f, g, h, i_reused });
    CHECK_FALSE(original == changed);
    CHECK_FALSE(RHI::DX12DescriptorHeapManager::DescriptorTableCacheKeyHasher{}(original) ==
                RHI::DX12DescriptorHeapManager::DescriptorTableCacheKeyHasher{}(changed));
}
```

Run the focused test and observe the expected compile RED because `allocationSerial` and `from_handles` do not exist yet. Then implement only:

- add `uint64_t allocationSerial = 0` to internal `DX12DescriptorHandle`;
- add `uint64_t m_nextAllocationSerial = 0` to `DX12CPUDescriptorHeap`; reset it in `init()` and, under the existing allocation mutex, fail closed with an invalid handle plus error/assert if it is already `UINT64_MAX`, otherwise pre-increment it and assign the resulting nonzero value on every `allocate()`. Perform this exhaustion check before popping the free-list or advancing `m_currentIndex`, so the failure cannot consume a slot;
- replace `DX12PendingBind::cpuHandle` / `cpuHandles` with `descriptorHandle` / `descriptorHandles` of type `DX12DescriptorHandle` / `std::vector<DX12DescriptorHandle>`, and copy the full handle from every buffer-view, texture-view, and sampler bind path;
- replace `PendingDescriptorTable::sourceHandles` with `std::vector<DX12DescriptorHandle>` and change `find_or_create_shader_visible_table()` to accept `const DX12DescriptorHandle* descriptorHandles`; descriptor validation and `CopyDescriptorsSimple` still use each element's `.cpuHandle`;
- add `DescriptorTableCacheKey::DescriptorIdentity { SIZE_T cpuHandle; uint64_t allocationSerial; }`, replacing the inline/overflow raw-handle containers with `DescriptorIdentity` containers;
- define the public pure factory `static DescriptorTableCacheKey from_handles(D3D12_DESCRIPTOR_HEAP_TYPE heapType, const DX12DescriptorHandle* descriptorHandles, uint32_t descriptorCount)` inline inside `DescriptorTableCacheKey` in `DX12DescriptorHeap.h`, and make both `find_or_create_shader_visible_table()` and the doctest call it. `DX12DescriptorHeapManager` is not DLL-exported, so the factory must not be out-of-line in `DX12DescriptorHeap.cpp`; the header-inline definition keeps this an internal, zero-ABI test seam;
- make the factory map every source handle to `DescriptorIdentity { descriptorHandles[index].cpuHandle.ptr, descriptorHandles[index].allocationSerial }`; make `DescriptorIdentity` equality, `DescriptorTableCacheKey` equality, and `DescriptorTableCacheKeyHasher` include both stored fields for every inline/overflow element while preserving heap type, count, and order. The hasher must combine the address and serial as separate values; it must not narrow either to `uint32_t`.

Preserve the current content-immutability invariant: CBV/SRV/UAV and Sampler descriptors are written once after allocation and are not overwritten in place before their handle is freed. This change does not add an in-place rewrite API. If such an API is introduced later, it must allocate or advance the identity and add a cache regression test in the same change.

Do not clear or scan the cache on free, change descriptor heap capacity/partitioning, add a reverse index, alter view lifetime, or touch `DX12Buffer`, fences, command allocators, Vulkan descriptors, or any UAV barrier policy. Live allocations must retain the existing cache-hit behavior; a reused slot must miss exactly once for its new identity.

Run the focused doctest to GREEN, then `RunTests.bat Debug`, `build_sandbox.bat Debug`, DX12 combined twice, DX12 constant-only once, and Vulkan combined once. Enable DX12 Debug/GPU validation for one further combined run using fresh logfile byte offsets; require exit 0, both PASS markers, debug+GPU-validation markers, and no ERROR/CORRUPTION/assertion/access-violation/readiness-deadline/descriptor-overflow text. Restore `Engine.ini` exactly in `finally`.

Stage and commit the two atomic corrections separately:

```powershell
git add -- project/src/engine/Graphics/DirectX12/DX12Helper.hpp project/src/engine/Graphics/DirectX12/DX12DescriptorHeap.h project/src/engine/Graphics/DirectX12/DX12DescriptorHeap.cpp project/src/engine/Graphics/DirectX12/DX12RenderProgramBinder.h project/src/engine/Graphics/DirectX12/DX12RenderProgramBinder.cpp project/src/engine/Graphics/DirectX12/DX12RenderProgram.cpp project/src/tests/Graphics/dx12_descriptor_table_cache_tests.cpp
git diff --cached --check
git diff --cached --name-status
git commit -m "fix(dx12): version descriptor table cache identity"

git add -- project/src/engine/Function/Render/RHIConstantBufferSelfTest.cpp project/src/engine/Function/Render/RHIIndirectSelfTest.cpp
git diff --cached --check
git diff --cached --name-status
git commit -m "fix(rhi): make GPU self-test transitions explicit"
```

- [ ] **Step 8e: Re-run both failure paths against the final self-test code**

With Steps 8b through 8d committed, use `apply_patch` to change only `k_compute_expected_rgba[0]` from 17 to 20, rebuild Sandbox Debug once, and run the Vulkan and DX12 constant-buffer commands serially under the existing 120-second watchdog.

Both backends must produce the exact mismatch/FAIL and wrapper exit 1 through normal Application teardown well before 120 seconds. Vulkan must contain the VMA zero-live marker; DX12 must contain `DX12Context: Shutdown complete.`. Neither run may contain the readiness deadline marker, allocator live-allocation/leak text, D3D12MA `Some allocations were not freed`, `Assertion failed`, `0xC0000005`, crash, hang, or forced exit.

Use a `try/finally` workflow: in `finally`, restore 20 to 17 with `apply_patch`, rebuild Sandbox Debug, assert `git diff -- project/src/engine/Function/Render/RHIConstantBufferSelfTest.cpp` is empty, and confirm no `20u` expected-value residue. A failed acceptance check must not skip restoration.

- [ ] **Step 9: Verify real GPU GREEN on both backends**

Run serially, with no other engine or gate process active:

```bat
run.bat sandbox vulkan Debug --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer
run.bat sandbox dx12 Debug --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer
```

Expected: both exit 0 and log both indirect and constant-buffer PASS markers.

- [ ] **Step 10: Commit the self-test and wiring**

Run:

```powershell
git add -- project/src/tests/Function/application_automation_tests.cpp project/src/engine/EntryPoint.h project/src/engine/Function/Application.h project/src/engine/Function/Application.cpp project/src/engine/Function/Render/RHIConstantBufferSelfTest.h project/src/engine/Function/Render/RHIConstantBufferSelfTest.cpp project/src/engine/Shaders/SelfTest/RHIConstantBufferSelfTest.hlsl
git diff --cached --check
git diff --cached --name-status
git commit -m "test(rhi): validate constant buffer visibility across backends"
```

Rollback checkpoint: keep `fix(dx12): release pending uploads before teardown`, `fix(dx12): version descriptor table cache identity`, and `fix(rhi): make GPU self-test transitions explicit` as separate commits. A repeated allocator/assert/watchdog failure reverts the teardown commit and blocks the new CI flag. A combined-diagnostic/PerfGate regression caused by allocation-serial identity reverts that commit and blocks integration; do not replace it with cache clearing, sleep, or device idle. A validation/self-test/render regression caused by the diagnostic correction reverts that correction and disables the new CLI/CI exposure. In every case, remove the corresponding README/spec guarantees in the same rollback and do not bless baselines.

### Task 5: Wire software-backend CI and update the long-term contract

**Files:**
- Modify: `.github/workflows/ci.yml:94-99,119`
- Modify: `README.md`
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
- backend 原生资源在首帧前失败时必须在 native allocator/descriptor dependencies 有效期间回收。Vulkan current-frame deletion queue accessor 对 `UINT32_MAX` sentinel 返回 queue 0；Vulkan 与 DX12 shutdown 都在首次 drain 前释放未提交的 pending buffer/texture upload ownership，再由既有 queue flush 回收。有效 frame 的 upload/deletion 行为不变。constant-buffer self-test 的双后端故障注入必须证明失败由正常 teardown 返回非零，不发生 access violation、allocator live-allocation assertion 或 watchdog hang。
- DX12 shader-visible descriptor table cache 的内容 identity 必须包含 CPU descriptor slot 地址与本次 allocation serial。相同活跃 allocation 可复用既有 GPU table；slot free 后再次 allocation 即使地址相同也必须 miss 并重新复制。serial 在 CPU heap 既有 allocation mutex 内生成并由 internal handle/binder传递；不得靠 free-time 全 cache clear/scan 修复。
```

- [ ] **Step 3: Document the Application command-line contract**

Add `--rhi-selftest-constant-buffer` to the command-line contract table in `docs/specs/modules/application.md`. Define it as an opt-in pre-frame dual-backend diagnostic; a setup, command, validation, or readback failure sets the existing runtime-failure state and produces a non-zero readiness-smoke exit. State that it is independent from `--rhi-selftest-indirect` and that both execute when both flags are present.

Add the repository-level `RunTests.bat`, `RunArchGate.bat`, and constant-buffer diagnostic command to the root README validation block, keeping module details in the long-term specs.

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

Do not scan the file with the latest timestamp: logs use minute-granularity names and append within the same minute. Immediately before each run, snapshot the byte length of every `product/logs/*.logfile`; after the process exits, read only bytes appended after those offsets (and all bytes of newly created files). Use a PowerShell helper equivalent to:

```powershell
function Get-LogOffsets {
    $offsets = @{}
    Get-ChildItem product/logs -Filter *.logfile -File -ErrorAction SilentlyContinue | ForEach-Object {
        $offsets[$_.FullName] = $_.Length
    }
    return $offsets
}

function Read-NewLogBytes([hashtable]$offsets) {
    $text = [System.Text.StringBuilder]::new()
    Get-ChildItem product/logs -Filter *.logfile -File -ErrorAction Stop | ForEach-Object {
        $offset = if ($offsets.ContainsKey($_.FullName)) { [int64]$offsets[$_.FullName] } else { 0L }
        if ($_.Length -lt $offset) { throw "Log was truncated during validation: $($_.FullName)" }
        $stream = [System.IO.File]::Open($_.FullName, 'Open', 'Read', 'ReadWrite')
        try {
            [void]$stream.Seek($offset, 'Begin')
            $reader = [System.IO.StreamReader]::new($stream, [System.Text.Encoding]::UTF8, $true, 4096, $true)
            try { [void]$text.AppendLine($reader.ReadToEnd()) } finally { $reader.Dispose() }
        }
        finally { $stream.Dispose() }
    }
    return $text.ToString()
}
```

For each temporarily enabled backend, take `$before = Get-LogOffsets`, run the matching command below, save `$LASTEXITCODE`, then set `$fresh = Read-NewLogBytes $before`:

```bat
run.bat sandbox vulkan Debug --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer
run.bat sandbox dx12 Debug --smoke-test-seconds=120 --rhi-selftest-indirect --rhi-selftest-constant-buffer
```

Fail the validation step unless all of these hold:

- process exit is 0 and `$fresh` contains both the indirect and constant-buffer PASS markers;
- Vulkan `$fresh` contains `VulkanContext: Added bundled Vulkan validation layer path:`; before launch the temporary config is asserted to contain `Enabled=true` and `SynchronizationValidation=true`;
- DX12 `$fresh` contains both `DX12Context: Debug layer enabled.` and `DX12Context: GPU-based validation enabled.`;
- `$fresh` does not match `\[Vulkan Validation\]\s*-\s*ERROR|\[DX12 Validation\].*(ERROR|CORRUPTION)|Assertion failed|0xC0000005|Fatal Error: readiness process deadline expired`.

Make those conditions executable rather than manual inspection, for example after each run:

```powershell
if ($runExit -ne 0) { throw "Validation self-test exited $runExit" }
if (-not $fresh.Contains('[RHISelfTest] indirect draw substrate PASS') -or
    -not $fresh.Contains('[RHISelfTest] constant buffer visibility PASS')) {
    throw 'Fresh log delta is missing one or both self-test PASS markers.'
}
if ($backend -eq 'vulkan' -and
    -not $fresh.Contains('VulkanContext: Added bundled Vulkan validation layer path:')) {
    throw 'Fresh log delta does not prove Vulkan validation was enabled.'
}
if ($backend -eq 'dx12' -and
    (-not $fresh.Contains('DX12Context: Debug layer enabled.') -or
     -not $fresh.Contains('DX12Context: GPU-based validation enabled.'))) {
    throw 'Fresh log delta does not prove DX12 GPU validation was enabled.'
}
$forbidden = '\[Vulkan Validation\]\s*-\s*ERROR|\[DX12 Validation\].*(ERROR|CORRUPTION)|Assertion failed|0xC0000005|Fatal Error: readiness process deadline expired'
if ($fresh -match $forbidden) { throw "Fresh validation log matched a forbidden pattern: $($matches[0])" }
```

Restore `Engine.ini` in `finally` after each backend, then require `git diff -- product/config/Engine.ini` to be empty. Print or retain the fresh delta as validation evidence; old bytes are never part of the decision.

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

Change the SDD status from its post-reapproval implementation state to `Done`. Add a concise implementation outcome recording the exact stage union, explicit diagnostic transitions, dual-backend pre-frame teardown, self-test/gate results, and any accepted PerfGate warning. Do not alter the approved UAV non-goals.

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
gh pr create --base main --head codex/remediation-const-buffer --title "fix(rhi): correct constant buffer visibility and pre-frame teardown" --body-file Intermediate/pr-body-const-buffer.md
```

Before the command, create ignored `Intermediate/pr-body-const-buffer.md` with `apply_patch` and this exact structure after every listed gate has passed:

```markdown
## Summary
- fix Vulkan `ConstBuffer` visibility from vertex-only to the exact vertex + fragment + compute stage union
- keep the public RHI ABI and normal-frame barrier count unchanged; add a production-shared native policy regression test
- add an opt-in dual-backend constant-buffer GPU self-test and enable it beside indirect self-test in software-backend CI
- release never-submitted pending uploads before the first Vulkan/DX12 shutdown drain so pre-frame failures complete normal teardown
- make direct GPU diagnostics explicitly enter RTV state and isolate CB barriers from unrelated UAV stage masks
- version DX12 descriptor-table cache keys so recycled CPU slots cannot alias stale GPU tables

## Synchronization boundary
- no UAV-to-UAV barrier or implicit dependency was added
- the self-test intermediate uses the existing explicit `UAVCompute -> SRVGraphics` write-to-read transition

## Validation
- deterministic policy doctest: observed RED on fragment/compute/exact scope, then GREEN
- Vulkan and DX12 injected-mismatch paths: exact FAIL + normal teardown + exit 1, with no allocator leak/assert or watchdog forced exit
- combined indirect + constant-buffer diagnostics on Vulkan and DX12, including deterministic same-frame descriptor-slot reuse
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
