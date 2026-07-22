# Editor Camera Movement Speed Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Make Scene Viewport wheel travel constant at the configured camera speed and make Shift double wheel, pan, and dolly movement without mutating the configured speed.

**Architecture:** Extract the three translation calculations from `EditorViewportCameraService.cpp` into a small pure `EditorViewportCameraMath` unit. Doctest exercises that unit directly; the service remains responsible for input routing and state application.

**Tech Stack:** C++17, GLM, doctest, Premake5/MSBuild, Windows x64.

---

### Task 1: Add failing camera-movement regression tests

**Files:**
- Create: `project/src/editor/Services/EditorViewportCameraMath.h`
- Create: `project/src/editor/Services/EditorViewportCameraMath.cpp`
- Create: `project/src/tests/Editor/editor_viewport_camera_tests.cpp`
- Modify: `project/src/tests/premake5.lua`

- [ ] **Step 1: Declare the pure movement calculations**

Create `project/src/editor/Services/EditorViewportCameraMath.h`:

```cpp
#pragma once

#include <glm/vec3.hpp>

namespace AshEditor::EditorViewportCameraMath
{
	struct WheelTranslationResult
	{
		glm::vec3 vecPosition{};
		glm::vec3 vecOrbitTarget{};
	};

	WheelTranslationResult ComputeWheelTranslation(
		const glm::vec3& refPosition,
		const glm::vec3& refOrbitTarget,
		const glm::vec3& refForward,
		float fWheelDelta,
		float fMoveSpeed,
		bool bShiftDown);

	glm::vec3 ComputePanTranslation(
		const glm::vec3& refRight,
		const glm::vec3& refUp,
		float fMouseDeltaX,
		float fMouseDeltaY,
		float fPanUnitsPerPixel,
		float fMoveSpeed,
		bool bShiftDown);

	float ComputeDollyDistanceDelta(
		float fOrbitDistance,
		float fMouseDeltaY,
		float fMoveSpeed,
		bool bShiftDown);
}
```

- [ ] **Step 2: Add a current-behavior implementation so RED is an assertion failure**

Create `project/src/editor/Services/EditorViewportCameraMath.cpp` with the current formulas. The initial version intentionally ignores Shift and keeps the wheel tied to orbit distance:

```cpp
#include "Services/EditorViewportCameraMath.h"

#include <glm/geometric.hpp>

#include <algorithm>

namespace AshEditor::EditorViewportCameraMath
{
	namespace
	{
		constexpr float kReferenceMoveSpeed = 8.0f;
		constexpr float kScrollDollySpeed = 0.12f;
		constexpr float kDragDollySpeed = 0.015f;
		constexpr float kMinOrbitDistance = 0.1f;
		constexpr float kMaxOrbitDistance = 20000.0f;
	}

	WheelTranslationResult ComputeWheelTranslation(
		const glm::vec3& refPosition,
		const glm::vec3& refOrbitTarget,
		const glm::vec3& refForward,
		const float fWheelDelta,
		const float fMoveSpeed,
		const bool /*bShiftDown*/)
	{
		WheelTranslationResult result{ refPosition, refOrbitTarget };
		const float fOrbitDistance = glm::length(refOrbitTarget - refPosition);
		const float fSpeedScale = std::max(0.2f, fMoveSpeed / kReferenceMoveSpeed);
		const float fDistanceDelta =
			std::max(fOrbitDistance * kScrollDollySpeed, kMinOrbitDistance) *
			fWheelDelta *
			fSpeedScale;
		const float fNewOrbitDistance = std::clamp(
			fOrbitDistance - fDistanceDelta,
			kMinOrbitDistance,
			kMaxOrbitDistance);
		result.vecPosition = refOrbitTarget - refForward * fNewOrbitDistance;
		return result;
	}

	glm::vec3 ComputePanTranslation(
		const glm::vec3& refRight,
		const glm::vec3& refUp,
		const float fMouseDeltaX,
		const float fMouseDeltaY,
		const float fPanUnitsPerPixel,
		const float fMoveSpeed,
		const bool /*bShiftDown*/)
	{
		const float fPanScale =
			fPanUnitsPerPixel * std::max(0.25f, fMoveSpeed / kReferenceMoveSpeed);
		return
			(-refRight * fMouseDeltaX + refUp * fMouseDeltaY) *
			fPanScale;
	}

	float ComputeDollyDistanceDelta(
		const float fOrbitDistance,
		const float fMouseDeltaY,
		const float fMoveSpeed,
		const bool /*bShiftDown*/)
	{
		const float fSpeedScale = std::max(0.25f, fMoveSpeed / kReferenceMoveSpeed);
		return
			std::max(fOrbitDistance * kDragDollySpeed, kMinOrbitDistance) *
			(-fMouseDeltaY) *
			fSpeedScale;
	}
}
```

- [ ] **Step 3: Write tests for the desired behavior**

Create `project/src/tests/Editor/editor_viewport_camera_tests.cpp`:

```cpp
#include "doctest.h"

#include "Services/EditorViewportCameraMath.h"

#include <glm/geometric.hpp>

namespace
{
	void CheckVec3Approx(const glm::vec3& refActual, const glm::vec3& refExpected)
	{
		CHECK(refActual.x == doctest::Approx(refExpected.x));
		CHECK(refActual.y == doctest::Approx(refExpected.y));
		CHECK(refActual.z == doctest::Approx(refExpected.z));
	}
}

TEST_CASE("Editor viewport camera wheel movement stays constant and preserves the orbit offset")
{
	const glm::vec3 vecPosition{ 0.0f, 0.0f, 0.0f };
	const glm::vec3 vecNearTarget{ 0.0f, 0.0f, 10.0f };
	const glm::vec3 vecFarTarget{ 0.0f, 0.0f, 20.0f };
	const glm::vec3 vecForward{ 0.0f, 0.0f, 1.0f };

	const AshEditor::EditorViewportCameraMath::WheelTranslationResult nearResult =
		AshEditor::EditorViewportCameraMath::ComputeWheelTranslation(
			vecPosition,
			vecNearTarget,
			vecForward,
			1.0f,
			8.0f,
			false);
	const AshEditor::EditorViewportCameraMath::WheelTranslationResult farResult =
		AshEditor::EditorViewportCameraMath::ComputeWheelTranslation(
			vecPosition,
			vecFarTarget,
			vecForward,
			1.0f,
			8.0f,
			false);
	const AshEditor::EditorViewportCameraMath::WheelTranslationResult shiftedResult =
		AshEditor::EditorViewportCameraMath::ComputeWheelTranslation(
			vecPosition,
			vecNearTarget,
			vecForward,
			1.0f,
			8.0f,
			true);
	const AshEditor::EditorViewportCameraMath::WheelTranslationResult backwardResult =
		AshEditor::EditorViewportCameraMath::ComputeWheelTranslation(
			vecPosition,
			vecNearTarget,
			vecForward,
			-1.0f,
			8.0f,
			false);

	const glm::vec3 vecNearMovement = nearResult.vecPosition - vecPosition;
	const glm::vec3 vecFarMovement = farResult.vecPosition - vecPosition;
	CheckVec3Approx(vecNearMovement, vecFarMovement);
	CheckVec3Approx(shiftedResult.vecPosition - vecPosition, vecNearMovement * 2.0f);
	CheckVec3Approx(backwardResult.vecPosition - vecPosition, -vecNearMovement);
	CHECK(glm::length(nearResult.vecOrbitTarget - nearResult.vecPosition) ==
		doctest::Approx(glm::length(vecNearTarget - vecPosition)));
}

TEST_CASE("Editor viewport camera Shift doubles pan and dolly movement")
{
	const glm::vec3 vecPan = AshEditor::EditorViewportCameraMath::ComputePanTranslation(
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		4.0f,
		3.0f,
		0.5f,
		8.0f,
		false);
	const glm::vec3 vecShiftPan = AshEditor::EditorViewportCameraMath::ComputePanTranslation(
		glm::vec3(1.0f, 0.0f, 0.0f),
		glm::vec3(0.0f, 1.0f, 0.0f),
		4.0f,
		3.0f,
		0.5f,
		8.0f,
		true);
	CheckVec3Approx(vecShiftPan, vecPan * 2.0f);

	const float fDolly = AshEditor::EditorViewportCameraMath::ComputeDollyDistanceDelta(
		10.0f,
		-4.0f,
		8.0f,
		false);
	const float fShiftDolly = AshEditor::EditorViewportCameraMath::ComputeDollyDistanceDelta(
		10.0f,
		-4.0f,
		8.0f,
		true);
	CHECK(fShiftDolly == doctest::Approx(fDolly * 2.0f));
}
```

- [ ] **Step 4: Compile the pure unit into Tests**

Add this entry after `EditorGizmoViewport.cpp` in `project/src/tests/premake5.lua`:

```lua
		"%{wks.location}/project/src/editor/Services/EditorViewportCameraMath.cpp",
```

- [ ] **Step 5: Regenerate and run the focused test to verify RED**

Run:

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="*viewport camera*"
```

Expected: build succeeds, then both camera tests fail because wheel movement depends on orbit distance and Shift has no effect.

### Task 2: Implement constant wheel translation and Shift acceleration

**Files:**
- Modify: `project/src/editor/Services/EditorViewportCameraMath.cpp`
- Modify: `project/src/editor/Services/EditorViewportCameraService.cpp`
- Test: `project/src/tests/Editor/editor_viewport_camera_tests.cpp`

- [ ] **Step 1: Replace the extracted math with the desired calculations**

In `EditorViewportCameraMath.cpp`, replace the anonymous constants and three function bodies with:

```cpp
	namespace
	{
		constexpr float kReferenceMoveSpeed = 8.0f;
		constexpr float kWheelTranslationPerSpeedUnit = 0.12f;
		constexpr float kDragDollySpeed = 0.015f;
		constexpr float kMinOrbitDistance = 0.1f;
		constexpr float kShiftMoveMultiplier = 2.0f;

		float ResolveShiftMultiplier(const bool bShiftDown)
		{
			return bShiftDown ? kShiftMoveMultiplier : 1.0f;
		}
	}

	WheelTranslationResult ComputeWheelTranslation(
		const glm::vec3& refPosition,
		const glm::vec3& refOrbitTarget,
		const glm::vec3& refForward,
		const float fWheelDelta,
		const float fMoveSpeed,
		const bool bShiftDown)
	{
		const glm::vec3 vecTranslation =
			refForward *
			(fWheelDelta * fMoveSpeed * kWheelTranslationPerSpeedUnit * ResolveShiftMultiplier(bShiftDown));
		return { refPosition + vecTranslation, refOrbitTarget + vecTranslation };
	}

	glm::vec3 ComputePanTranslation(
		const glm::vec3& refRight,
		const glm::vec3& refUp,
		const float fMouseDeltaX,
		const float fMouseDeltaY,
		const float fPanUnitsPerPixel,
		const float fMoveSpeed,
		const bool bShiftDown)
	{
		const float fPanScale =
			fPanUnitsPerPixel *
			std::max(0.25f, fMoveSpeed / kReferenceMoveSpeed) *
			ResolveShiftMultiplier(bShiftDown);
		return
			(-refRight * fMouseDeltaX + refUp * fMouseDeltaY) *
			fPanScale;
	}

	float ComputeDollyDistanceDelta(
		const float fOrbitDistance,
		const float fMouseDeltaY,
		const float fMoveSpeed,
		const bool bShiftDown)
	{
		const float fSpeedScale =
			std::max(0.25f, fMoveSpeed / kReferenceMoveSpeed) *
			ResolveShiftMultiplier(bShiftDown);
		return
			std::max(fOrbitDistance * kDragDollySpeed, kMinOrbitDistance) *
			(-fMouseDeltaY) *
			fSpeedScale;
	}
```

Remove the no-longer-used `<glm/geometric.hpp>` include from this file.

- [ ] **Step 2: Route all three movement paths through the tested math**

In `EditorViewportCameraService.cpp`:

1. Add `#include "Services/EditorViewportCameraMath.h"`.
2. Remove `kScrollDollySpeed` and `kDragDollySpeed` from the anonymous namespace.
3. Resolve Shift once after the viewport-interactive guard:

```cpp
		const bool bShiftDown = refInput.IsModifierDown(AshEngine::UIModifierFlagBits::Shift);
```

4. Replace the wheel block with:

```cpp
		if (bMouseInContent && std::abs(refInput.vecMouseWheelDelta.y) > 0.0f)
		{
			const EditorViewportCameraMath::WheelTranslationResult translation =
				EditorViewportCameraMath::ComputeWheelTranslation(
					refState.vecPosition,
					refState.vecOrbitTarget,
					ComputeForwardVector(refState.vecRotationEulerDegrees),
					refInput.vecMouseWheelDelta.y,
					refState.fMoveSpeed,
					bShiftDown);
			refState.vecPosition = translation.vecPosition;
			refState.vecOrbitTarget = translation.vecOrbitTarget;
		}
```

5. Replace the Pan delta calculation with:

```cpp
					const glm::vec3 vecPanDelta = EditorViewportCameraMath::ComputePanTranslation(
						ComputeRightVector(refState.vecRotationEulerDegrees),
						ComputeUpVector(refState.vecRotationEulerDegrees),
						static_cast<float>(dMouseDeltaX),
						static_cast<float>(dMouseDeltaY),
						fPanUnitsPerPixel,
						refState.fMoveSpeed,
						bShiftDown);
```

6. Replace the Dolly delta calculation with:

```cpp
					const float fDistanceDelta = EditorViewportCameraMath::ComputeDollyDistanceDelta(
						refState.fOrbitDistance,
						static_cast<float>(dMouseDeltaY),
						refState.fMoveSpeed,
						bShiftDown);
```

Keep the existing application of `vecPanDelta`, `fDistanceDelta`, and `UpdatePositionFromOrbit` unchanged.

- [ ] **Step 3: Run the focused test to verify GREEN**

Run:

```bat
RunTests.bat Debug --test-case="*viewport camera*"
```

Expected: 2 test cases pass with no failed assertions.

- [ ] **Step 4: Commit the behavior and tests**

```bat
git add project/src/editor/Services/EditorViewportCameraMath.h project/src/editor/Services/EditorViewportCameraMath.cpp project/src/editor/Services/EditorViewportCameraService.cpp project/src/tests/Editor/editor_viewport_camera_tests.cpp project/src/tests/premake5.lua
git commit -m "fix(editor): stabilize viewport camera movement speed"
```

### Task 3: Update the long-term contract and close the Mini SDD

**Files:**
- Modify: `docs/specs/modules/editor.md`
- Modify: `docs/sdd/SDD-2026-07-20-editor-wheel-camera-translation.md`

- [ ] **Step 1: Document the camera movement invariant**

Add this bullet under `docs/specs/modules/editor.md` → `约束与不变式`:

```markdown
- Scene Viewport 相机移动：滚轮沿视线方向等步长平移 camera position 与 orbit target，不得随 orbit distance 缩短而减速；Shift 仅将滚轮、MMB Pan、Alt+RMB Dolly 的当次移动速度乘以 2，不得写回 Camera Speed 设置。Orbit 旋转灵敏度与 F Focus 不受 Shift 影响。
```

- [ ] **Step 2: Mark the SDD Done and record the result**

Change `## Status` from `Review` to `Done`, then append:

```markdown
## Result

实现将滚轮改为 camera position 与 orbit target 的同步固定步长平移，并统一对滚轮、Pan、Dolly 应用非持久化的 Shift ×2 倍率。长期行为契约已回写 `docs/specs/modules/editor.md`。
```

- [ ] **Step 3: Commit the documentation update**

```bat
git add docs/specs/modules/editor.md docs/sdd/SDD-2026-07-20-editor-wheel-camera-translation.md
git commit -m "docs(editor): specify viewport camera movement speed"
```

### Task 4: Run the required verification matrix

**Files:**
- Verify only; do not edit baselines or golden images.

- [ ] **Step 1: Run the complete unit suite**

Run:

```bat
RunTests.bat Debug
```

Expected: all doctest cases and assertions pass.

- [ ] **Step 2: Build Editor and check architecture boundaries**

Run:

```bat
build_editor.bat Debug
RunArchGate.bat
```

Expected: Editor builds successfully and ArchGate reports no new dependency violations.

- [ ] **Step 3: Run both Editor backend smokes**

Run:

```bat
run.bat editor vulkan Debug --smoke-test-seconds=120
run.bat editor dx12 Debug --smoke-test-seconds=120
```

Expected: both readiness smokes exit successfully; their logs contain no new validation or debug-layer errors.

- [ ] **Step 4: Validate the change plan and diff hygiene**

Run:

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
git diff --check HEAD~2..HEAD
```

Expected: AIDevDoctor reports a verification plan consistent with Editor/test/doc changes; scoped diff check reports no whitespace errors.

- [ ] **Step 5: Record the manual interaction check**

In a visible Editor session, verify continuous wheel-forward movement does not slow or stop, wheel-backward reverses direction, Shift doubles wheel/Pan/Dolly translation, and Orbit/F Focus remain unchanged. Record any environment limitation if this interaction cannot be performed automatically.
