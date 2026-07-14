# Remove hserialization Implementation Plan

> **For agentic workers:** REQUIRED SUB-SKILL: Use superpowers:subagent-driven-development (recommended) or superpowers:executing-plans to implement this plan task-by-task. Steps use checkbox (`- [ ]`) syntax for tracking.

**Goal:** Remove the unused and unsafe `hserialization` module without leaving build references, compatibility shims, or an inaccurate Base contract.

**Architecture:** This is a deletion-only Base cleanup. Remove the implementation and its isolated tests, regenerate Premake outputs, then update the Base spec and SDD so the repository no longer claims a serialization capability. No replacement API or data migration is introduced because there are no internal, external, or persisted-data consumers.

**Tech Stack:** C++17, doctest, Premake5, Visual Studio 2022/MSBuild, PowerShell verification scripts.

---

### Task 1: Prove and remove the isolated module

**Files:**
- Delete: `project/src/engine/Base/hserialization.h`
- Delete: `project/src/engine/Base/hserialization.cpp`
- Delete: `project/src/tests/Base/hserialization_tests.cpp`

- [ ] **Step 1: Run the structural absence check and verify RED**

Run from the repository root:

```powershell
$matches = rg -l 'hserialization|\b(BlobHeader|Serializer|RelativePointer|RelativeArray|RelativeString)\b' project/src premake5.lua
if ($matches) {
    $matches
    exit 1
}
exit 0
```

Expected: exit code `1`. The output must include the two Base files and the Base doctest file; generated `.vcxproj` files may also appear before regeneration.

- [ ] **Step 2: Delete the three owned files**

Use `apply_patch` with three `Delete File` operations:

```text
project/src/engine/Base/hserialization.h
project/src/engine/Base/hserialization.cpp
project/src/tests/Base/hserialization_tests.cpp
```

Do not edit adjacent Base code and do not add a compatibility header.

- [ ] **Step 3: Regenerate Visual Studio projects**

Run:

```powershell
.\generate_vs2022.bat
```

Expected: exit code `0`; Premake regenerates `Engine.vcxproj` and `Tests.vcxproj` without either deleted source file.

- [ ] **Step 4: Re-run the structural absence check and verify GREEN**

Run:

```powershell
$matches = rg -l 'hserialization|\b(BlobHeader|Serializer|RelativePointer|RelativeArray|RelativeString)\b' project/src premake5.lua
if ($matches) {
    $matches
    exit 1
}
exit 0
```

Expected: exit code `0` and no output.

- [ ] **Step 5: Run the post-removal unit-test suite**

Run:

```powershell
.\RunTests.bat
```

Expected: exit code `0`, `77` test cases passed and `928` assertions passed. The baseline was `79` cases and `943` assertions; the exact reduction is the deleted file's two cases and fifteen assertions.

### Task 2: Align the durable Base contract

**Files:**
- Modify: `docs/specs/modules/base.md`
- Modify: `docs/sdd/SDD-2026-07-14-remove-hserialization.md`

- [ ] **Step 1: Remove serialization from the current Base responsibilities**

In `docs/specs/modules/base.md`:

1. Remove `二进制序列化` from the responsibility sentence.
2. Delete the `hserialization.h/.cpp` row from the key-files table.
3. Keep the general Base pure-logic verification rule unchanged.
4. Add this history entry:

```markdown
- [SDD-2026-07-14-remove-hserialization](../../sdd/SDD-2026-07-14-remove-hserialization.md)：删除零调用、无兼容需求且存在边界与所有权缺陷的旧 `hserialization` 模块；Base 不再提供通用二进制序列化能力。
```

- [ ] **Step 2: Close the Mini SDD**

In `docs/sdd/SDD-2026-07-14-remove-hserialization.md`:

1. Change `Status: Review` to `Status: Done`.
2. Append this completion section:

```markdown
## Outcome

已完整删除 `hserialization` 的声明、实现和专属 doctest，重新生成工程后仓内代码不存在残留引用。Base 长期 spec 已同步移除二进制序列化职责；未增加兼容层或替代格式。
```

- [ ] **Step 3: Check documentation and diff hygiene**

Run:

```powershell
git diff --check
git diff --stat
git diff --stat origin/main...HEAD
git status --short
```

Expected: `git diff --check` exits `0`, and status contains only the three deletions, the Base spec/SDD/plan changes, plus the pre-existing LFS worktree noise if still present. Never stage `project/thirdparty/tracy/tracy-profiler.exe`.

### Task 3: Run the required non-GPU verification

**Files:**
- No source changes expected.

- [ ] **Step 1: Run the architecture gate**

```powershell
.\RunArchGate.bat
```

Expected: exit code `0`; no new dependency violation.

- [ ] **Step 2: Build both Debug executable targets**

```powershell
.\build_editor.bat Debug
.\build_sandbox.bat Debug
```

Expected: both commands exit `0` and no compile or link step refers to `hserialization`.

- [ ] **Step 3: Validate the AI-development plan and repository rules**

```powershell
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
```

Expected: exit code `0` with no plan or rule violation.

### Task 4: Run the coordinated readiness smoke

**Files:**
- No source changes expected.

- [ ] **Step 1: Obtain the shared GPU window**

Notify the Terrain and GPU-observability tasks that this worktree will run one Base readiness matrix. Wait until both confirm they have no Editor, Sandbox, validation, RenderGate, PerfGate, or GPU smoke process active.

- [ ] **Step 2: Record configuration state and start time**

```powershell
$engineIniBefore = (Get-FileHash product/config/Engine.ini -Algorithm SHA256).Hash
$smokeStart = Get-Date
```

- [ ] **Step 3: Run all Debug backend/target combinations**

```powershell
.\run.bat all Debug --smoke-test-seconds=120
```

Expected: exit code `0`; Editor and Sandbox pass readiness and clean-exit checks on Vulkan and DX12.

- [ ] **Step 4: Verify restoration and fresh logs**

```powershell
$engineIniAfter = (Get-FileHash product/config/Engine.ini -Algorithm SHA256).Hash
if ($engineIniAfter -ne $engineIniBefore) {
    throw 'Engine.ini was not restored after smoke'
}

Get-ChildItem product/logs -File |
    Where-Object LastWriteTime -ge $smokeStart |
    Select-String -Pattern 'validation error|device lost|access violation|fatal error' -CaseSensitive:$false
```

Expected: hashes match and the fresh-log scan returns no unexpected validation, device-loss, access-violation, or fatal-error record. Release the GPU window immediately after the audit.

### Task 5: Commit and publish the focused package

**Files:**
- Delete: `project/src/engine/Base/hserialization.h`
- Delete: `project/src/engine/Base/hserialization.cpp`
- Delete: `project/src/tests/Base/hserialization_tests.cpp`
- Modify: `docs/specs/modules/base.md`
- Modify: `docs/sdd/SDD-2026-07-14-remove-hserialization.md`
- Create: `docs/plans/2026-07-14-remove-hserialization.md`

- [ ] **Step 1: Review the final tracked diff**

```powershell
git diff --check
git diff --name-status
git diff --name-status origin/main...HEAD
git status --short
```

Expected: only the six owned paths are included in the package. The existing Tracy LFS file is excluded.

- [ ] **Step 2: Stage only the owned implementation package**

```powershell
git add -- project/src/engine/Base/hserialization.h project/src/engine/Base/hserialization.cpp project/src/tests/Base/hserialization_tests.cpp docs/specs/modules/base.md docs/sdd/SDD-2026-07-14-remove-hserialization.md docs/plans/2026-07-14-remove-hserialization.md
git diff --cached --check
git diff --cached --name-status
```

Expected: the staged set contains exactly the three deletions and three documentation paths; no binary or unrelated session file is staged.

- [ ] **Step 3: Commit the verified implementation**

```powershell
git commit -m "refactor(base): remove unused hserialization"
```

Expected: commit succeeds and the worktree has no owned dirty files.

- [ ] **Step 4: Push and create a ready pull request**

```powershell
git push -u origin codex/remediation-remove-hserialization
$body = @"
## Summary
- remove the unused and unsafe Base hserialization implementation
- remove its isolated doctest coverage and regenerate project membership
- update the Base contract and close the approved Mini SDD

## Compatibility
- no repository or external consumers
- no persisted blob compatibility requirement
- no replacement API or compatibility shim

## Verification
- RunTests.bat
- RunArchGate.bat
- build_editor.bat Debug
- build_sandbox.bat Debug
- run.bat all Debug --smoke-test-seconds=120
- AIDevDoctor ValidatePlan
"@
gh pr create --base main --head codex/remediation-remove-hserialization --title "refactor(base): remove unused hserialization" --body $body
```

The PR body must summarize the removed unsafe surface, state that there are no consumers or compatibility requirements, and list every verification result. Create a ready PR, not a draft.
