# Reverse-Z Depth Clear Classification Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Preserve valid far-distance scene pixels whose finite reverse-Z depth lies in `(0, 1e-6]` by classifying only the exact depth-clear endpoint as background.

**Architecture:** Add one HLSL-only scene-depth helper under `Shaders/Scene` and route every existing screen-depth background test through it. The change does not alter resource bindings, RenderGraph, RHI, camera clip fitting, shadow splits, or asset data; a doctest source contract plus finite-projection math locks the behavior before dual-backend render validation.

**Tech Stack:** C++17/doctest, HLSL 6, GLM finite reverse-Z projection math, Premake5/MSBuild, Vulkan and DX12.

---

## File map

- Create `project/src/engine/Shaders/Scene/SceneDepthCommon.hlsli`: sole definition of scene-depth clear/background semantics.
- Create `project/src/tests/Function/scene_depth_clear_contract_tests.cpp`: numeric 8 km regression and source-consumer contract.
- Modify eight shader consumers listed in the approved Mini SDD: remove local epsilon checks and call the shared helper.
- Modify render/deferred/shadow/debug specs: record exact clear-endpoint semantics and the large-world regression.
- Modify `docs/sdd/SDD-2026-07-20-reverse-z-depth-clear-classification.md`: mark `Done` only after all required verification succeeds.

### Task 1: Establish the failing large-world contract

**Files:**
- Create: `project/src/tests/Function/scene_depth_clear_contract_tests.cpp`

- [ ] **Step 1: Ensure the visible Editor is safely closed before rebuilding**

Do not terminate the process or discard unsaved scene/terrain state. Ask the human to save or discard explicitly, then verify the terrain Editor process has exited and no effective Tests/MSBuild/Premake/GPU/gate root remains.

- [ ] **Step 2: Add the numeric and source-contract RED tests**

Create the test file with a binary-safe source reader and these contracts:

```cpp
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

#include <array>
#include <fstream>
#include <iterator>
#include <string>
#include <glm/ext/matrix_clip_space.hpp>
#include <glm/glm.hpp>

namespace
{
	std::string ReadSource(const char* path)
	{
		std::ifstream input(path, std::ios::binary);
		REQUIRE_MESSAGE(input.is_open(), "failed to open shader source contract");
		return {
			std::istreambuf_iterator<char>(input),
			std::istreambuf_iterator<char>() };
	}

	float ProjectViewDepth(const glm::mat4& projection, float view_depth)
	{
		const glm::vec4 clip = projection * glm::vec4(0.0f, 0.0f, view_depth, 1.0f);
		return clip.z / clip.w;
	}
}

TEST_CASE("Scene depth clear preserves finite reverse-Z terrain depth below legacy epsilon")
{
	constexpr float near_plane = 0.03f;
	constexpr float far_plane = 8701.6f;
	constexpr float terrain_far_depth = 8292.0f;
	const glm::mat4 reverse_z = glm::perspectiveLH_ZO(
		glm::radians(60.0f), 16.0f / 9.0f, far_plane, near_plane);
	const float device_depth = ProjectViewDepth(reverse_z, terrain_far_depth);
	CHECK(device_depth > 0.0f);
	CHECK(device_depth <= 1.0e-6f);
	CHECK(device_depth != 0.0f);
}

TEST_CASE("Scene depth clear preserves finite normal-Z terrain depth above legacy epsilon")
{
	constexpr float near_plane = 0.03f;
	constexpr float far_plane = 8701.6f;
	constexpr float terrain_far_depth = 8292.0f;
	const glm::mat4 normal_z = glm::perspectiveLH_ZO(
		glm::radians(60.0f), 16.0f / 9.0f, near_plane, far_plane);
	const float device_depth = ProjectViewDepth(normal_z, terrain_far_depth);
	CHECK(device_depth < 1.0f);
	CHECK(device_depth >= 0.999999f);
	CHECK(device_depth != 1.0f);
}

TEST_CASE("Scene depth clear consumers use the shared exact-endpoint contract")
{
	const std::string common = ReadSource(
		"project/src/engine/Shaders/Scene/SceneDepthCommon.hlsli");
	CHECK(common.find("reverse_z ? depth <= 0.0 : depth >= 1.0") != std::string::npos);
	CHECK(common.find("0.000001") == std::string::npos);
	CHECK(common.find("0.999999") == std::string::npos);

	const std::array<const char*, 8> consumers{
		"project/src/engine/Shaders/Deferred/DeferredCommon.hlsli",
		"project/src/engine/Shaders/Deferred/EnvironmentCommon.hlsli",
		"project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli",
		"project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli",
		"project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl",
		"project/src/engine/Shaders/Shadow/DirectionalShadowCascadeDebug.hlsl",
		"project/src/engine/Shaders/Debug/RenderDebugView.hlsl",
		"project/src/engine/Shaders/Particles/ParticleSystem.hlsl",
	};
	for (const char* path : consumers)
	{
		CAPTURE(path);
		const std::string source = ReadSource(path);
		CHECK(source.find("SceneDepthCommon.hlsli") != std::string::npos);
		CHECK(source.find("AshSceneDepthIsBackground(") != std::string::npos);
		CHECK(source.find("0.000001") == std::string::npos);
		CHECK(source.find("0.999999") == std::string::npos);
		CHECK(source.find("1.0e-6") == std::string::npos);
	}
}
```

- [ ] **Step 3: Regenerate the solution so the globbed test file enters Tests.vcxproj**

Run: `generate_vs2022.bat`

Expected: exit `0`; generated solution contains `scene_depth_clear_contract_tests.cpp`.

- [ ] **Step 4: Run the focused test and verify RED**

Run: `RunTests.bat Debug --test-case="*Scene depth clear*"`

Expected: FAIL because `SceneDepthCommon.hlsli` does not exist and the eight consumers still contain local epsilon classifiers. Do not proceed unless the failure is specifically this missing contract.

### Task 2: Implement one exact-endpoint helper and migrate all consumers

**Files:**
- Create: `project/src/engine/Shaders/Scene/SceneDepthCommon.hlsli`
- Modify: `project/src/engine/Shaders/Deferred/DeferredCommon.hlsli`
- Modify: `project/src/engine/Shaders/Deferred/EnvironmentCommon.hlsli`
- Modify: `project/src/engine/Shaders/Deferred/AmbientOcclusionCommon.hlsli`
- Modify: `project/src/engine/Shaders/Deferred/VolumetricLightingCommon.hlsli`
- Modify: `project/src/engine/Shaders/Shadow/DirectionalShadowMask.hlsl`
- Modify: `project/src/engine/Shaders/Shadow/DirectionalShadowCascadeDebug.hlsl`
- Modify: `project/src/engine/Shaders/Debug/RenderDebugView.hlsl`
- Modify: `project/src/engine/Shaders/Particles/ParticleSystem.hlsl`
- Test: `project/src/tests/Function/scene_depth_clear_contract_tests.cpp`

- [ ] **Step 1: Add the shared helper**

```hlsl
#ifndef ASH_SCENE_DEPTH_COMMON_HLSLI
#define ASH_SCENE_DEPTH_COMMON_HLSLI

bool AshSceneDepthIsBackground(float depth, bool reverse_z)
{
    return reverse_z ? depth <= 0.0 : depth >= 1.0;
}

#endif
```

- [ ] **Step 2: Include the helper and replace each local classifier**

Each consumer includes `../Scene/SceneDepthCommon.hlsli`. Remove duplicate `AshSceneDepthIsBackground`, `AshAOSceneDepthIsBackground`, `AshVolumetricSceneDepthIsBackground`, or `IsBackgroundDepth` definitions and call the shared helper with the consumer's existing reverse-Z flag. Representative conversions:

```hlsl
if (AshSceneDepthIsBackground(surface.depth, AshIsReverseZ()))
```

```hlsl
if (AshSceneDepthIsBackground(scene_depth, AshShadowLightParams.z > 0.5))
```

```hlsl
const bool background = AshSceneDepthIsBackground(depth, reverse_z);
```

```hlsl
const bool background = AshSceneDepthIsBackground(
    scene_device_depth,
    (AshParticleFlags & 1u) != 0u);
```

Update AO/volumetric call sites directly rather than keeping wrappers, so the test can prove all consumers use the same definition.

- [ ] **Step 3: Run the focused test and verify GREEN**

Run: `RunTests.bat Debug --test-case="*Scene depth clear*"`

Expected: PASS; the projected far Terrain depth remains positive and below the legacy epsilon, while all consumers reference the exact-endpoint helper.

- [ ] **Step 4: Verify shader literals and diff scope**

Run:

```powershell
rg -n 'depth\s*<=\s*(0\.000001|1\.0e-6)|depth\s*>=\s*0\.999999' project/src/engine/Shaders
git diff --check
```

Expected: `rg` returns no scene-background classifiers; unrelated reconstruction, material-weight, or temporal tolerances remain unchanged. `git diff --check` passes.

- [ ] **Step 5: Commit the GREEN code and test only**

Stage the new helper, new test, and eight exact shader files individually. Inspect `git diff --cached` before committing; exclude user scene, Terrain assets, Editor config, verification notes, baselines, and unrelated worktree files.

Commit message: `fix(render): preserve valid far reverse-z depth`

### Task 3: Record the long-term rendering contract

**Files:**
- Modify: `docs/specs/modules/render.md`
- Modify: `docs/specs/features/deferred-lighting.md`
- Modify: `docs/specs/features/shadows.md`
- Modify: `docs/specs/features/render-debug-view.md`
- Modify: `docs/sdd/SDD-2026-07-20-reverse-z-depth-clear-classification.md`

- [ ] **Step 1: Update the module and feature specs**

Record these exact invariants in the relevant existing sections:

- Scene depth background uses the depth target's exact clear endpoint: reverse-Z `0.0`, normal-Z `1.0`.
- Fixed epsilons must not classify screen-space depth coverage because valid finite reverse-Z depth can be arbitrarily close to zero as the near/far ratio grows.
- Deferred, environment, shadow mask/cascade debug, AO, volumetric, particle soft-depth, and depth debug share one HLSL helper.
- `SceneSunLightShadowCascadeIndex` black background must not consume positive far-depth pixels; pixels outside all configured splits remain the debug pass's explicit unmatched color.

- [ ] **Step 2: Mark the Mini SDD Done after verification evidence exists**

Change `Status` from `Approved` to `Done` only after Tasks 4 and 5 pass. Add a concise Result paragraph containing the implementation commit and report paths; do not claim the human A/B before it is signed.

- [ ] **Step 3: Commit documentation only**

Stage the four specs and this SDD individually, inspect the cached diff, then commit:

`docs(render): record exact depth clear semantics`

### Task 4: Run CPU and build verification

**Files:**
- No source edits expected.

- [ ] **Step 1: Run full doctest**

Run: `RunTests.bat Debug`

Expected: all suites/cases PASS with exit `0`.

- [ ] **Step 2: Run architecture gate**

Run: `RunArchGate.bat`

Expected: PASS; legacy warnings do not increase.

- [ ] **Step 3: Build both Debug targets**

Run sequentially:

```bat
build_editor.bat Debug
build_sandbox.bat Debug
```

Expected: both exit `0`; shader compilation accepts the shared include from every consumer.

### Task 5: Run exclusive GPU/render/performance verification

**Files:**
- Runtime config files may be touched by applications but must be restored byte-for-byte after each bounded run.

- [ ] **Step 1: Coordinate an exclusive CPU/GPU window and snapshot configuration**

Wait for all concurrent sessions to confirm release. Fresh-preflight Editor/Sandbox/AshImageDiff, Tests/MSBuild/Premake, validation, RenderGate, and PerfGate roots must be zero. Hash and back up `Engine.ini`, `EditorSettings.json`, `ViewportLayout.json`, `imgui.ini`, and the perf baseline without editing any baseline.

- [ ] **Step 2: Run four-combination readiness**

Run: `run.bat all Debug --smoke-test-seconds=120`

Expected: Editor/Sandbox × Vulkan/DX12 all exit `0` after readiness; fresh logs contain no fatal/error/validation/device-loss findings.

- [ ] **Step 3: Run dual-backend Debug timing validation**

Run:

```bat
RunPerfGate.bat -Profile Standard -Scenario Empty -Configuration Debug -TimingValidation
```

Expected: the built-in Vulkan and DX12 Debug validation matrix reaches readiness, produces complete timing snapshots, exits cleanly, and reports no validation/debug-layer warning or error.

- [ ] **Step 4: Run non-bless render and performance gates**

Run sequentially:

```bat
RunRenderGate.bat
RunPerfGate.bat -Profile Standard
```

Expected: RenderGate PASS without golden changes. PerfGate has no FAIL; any WARN stops completion for explicit review. Do not bless golden or performance baseline.

- [ ] **Step 5: Restore and audit runtime state**

Restore all snapshotted configuration bytes, confirm hashes match, confirm baseline diff is empty, and verify effective process roots are zero. Report all generated gate paths and any invalid diagnostic attempts separately.

- [ ] **Step 6: Perform the human same-camera A/B**

Launch one visible Editor backend at a time only after coordination. The human verifies the same far corner in GBuffer E, `SceneSunLightShadowCascadeIndex`, and final lighting, then repeats on the second backend. The agent must not operate the UI or sign on the human's behalf. Close each Editor safely and restore configuration before changing backend.
