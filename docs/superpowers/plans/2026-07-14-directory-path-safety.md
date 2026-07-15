# Directory Path Safety Implementation Plan

> **For Codex:** Execute this plan task-by-task in the current isolated worktree. Apply `superpowers:test-driven-development` for Tasks 1-3 and `superpowers:verification-before-completion` before any completion claim.

**Goal:** Remove fixed-buffer corruption, failed-navigation state loss, and directory-search handle leaks while preserving existing accepted path-fragment semantics and the `Directory` ABI.

**Architecture:** Keep the public `Directory` value type and existing functions. Move all path construction into bounded local buffers, open candidate Win32 search handles before mutating caller state, and commit path/handle together only after the old state can be released. Tests exercise the Engine DLL exports through doctest.

**Tech Stack:** C++17, Win32 `GetFullPathNameA`/`FindFirstFileA`/`FindClose`, doctest, Premake5/MSBuild.

---

### Task 1: Pin the failure and lifetime contracts with doctest

**Files:**
- Modify: `project/src/tests/Base/hfile_tests.cpp`

**Step 1: Add test-only helpers**

- Add `<array>`, `<string>`, and `<system_error>` as needed.
- Create unique temporary root/child directories beneath `Intermediate/test-temp/tests` and remove them with `std::filesystem::remove_all` before/after each test.
- Add a `GuardedDirectory` containing `AshEngine::Directory` followed by a canary array. Use a relative search pattern whose own suffix appends still fit in `Directory::path`, but whose resolved absolute form exceeds `k_max_path`; this exposes legacy state mutation without deliberately executing an out-of-bounds write.
- Keep cleanup explicit: if the current implementation overwrites an owned handle during a failed transition, close the saved handle through a temporary `Directory` so the RED test does not leak a process handle.

**Step 2: Add focused behavioral tests**

- `Directory failed child navigation preserves open state`: open a real temporary root, attempt a missing child, assert false and byte-for-byte path plus handle identity unchanged.
- `Directory oversized resolved child navigation preserves state and canary`: seed a terminated relative pattern ending in `*` so the two-character child plus `\\*` still fit, while the current worktree prefix makes the resolved absolute form exceed `k_max_path`; assert false, unchanged state, and unchanged canary.
- `Directory close is idempotent`: open a real root, close twice, assert both calls succeed and `os_handle == nullptr` after the first close.
- `Directory child and parent navigation commit valid state`: open root, navigate to real child, then parent, assert each successful transition updates to an open search pattern and remains closable.
- `Directory current path writes a terminated bounded result`: wrap a zero-initialized `Directory` in a canary, call `directory_current`, assert a nonempty terminated path and unchanged canary.

**Step 3: Run the first RED build**

Run:

```bat
build_tests.bat Debug x64
```

Expected: fail to link the newly referenced directory functions because they are not currently exported from `Engine.dll`. Record the linker symbols; this is the first RED result, not a product regression.

### Task 2: Make the existing directory API testable without changing its ABI

**Files:**
- Modify: `project/src/engine/Base/hfile.h`

**Step 1: Apply the smallest link-surface change**

- Add value initialization to `Directory::path` and `Directory::os_handle` while preserving field order and sizes.
- Add `ASH_API` to `directory_current`, `file_open_directory`, `file_close_directory`, `file_parent_directory`, and `file_sub_directory`; do not add a new public helper or change a signature.
- Add compile-time assertions in the test only if needed to pin `sizeof(Directory)` and standard-layout status on Windows.

**Step 2: Rebuild and run focused tests to expose behavioral RED**

Run:

```bat
RunTests.bat Debug --test-case="Directory*"
```

Expected: link succeeds; current implementation fails at least failed-state preservation, canary integrity, or repeated-close assertions.

### Task 3: Implement bounded construction and transactional replacement

**Files:**
- Modify: `project/src/engine/Base/hfile.cpp`

**Step 1: Add private bounded helpers**

- Include `<array>` and `<string_view>` only if the concrete implementation needs them.
- Add a private helper that validates non-null/nonempty input, calls `GetFullPathNameA` with `k_max_path`, rejects return values `>= k_max_path`, and falls back to the raw input only when the Win32 call returns zero and the raw input fully fits.
- Normalize only the search suffix: preserve a trailing `*`; append `*` after a trailing slash; otherwise append `\\*`. Check capacity before every append and copy exactly once into a local `std::array<char, k_max_path>`.
- Add a private raw-handle close helper so candidate cleanup and old-state release use the same Win32 rule.

**Step 2: Make `file_open_directory` transactional**

- Reject null arguments before dereference.
- Build the candidate search pattern locally and open it with `FindFirstFileA`.
- If candidate open fails, return false without touching `outDirectory`.
- If `outDirectory` owns a handle, close it before commit. If old close fails, close the candidate, return false, and leave the stored path/handle unchanged.
- On success, copy the complete local pattern and assign the candidate handle; never expose partial state.

**Step 3: Make close and current-directory updates safe**

- Treat null `Directory*` as failure for close; treat a valid object with a null handle as already closed/success.
- After a successful `FindClose`, assign `nullptr` immediately.
- Have `directory_current` write into a local array and commit only when the Win32 result is in `1..k_max_path-1`; null, zero, and oversized results leave the caller unchanged.

**Step 4: Make child/parent navigation reuse the transaction**

- In `file_sub_directory`, validate pointers, copy the current pattern to a local bounded buffer/string, remove only the terminal `*`, append `subDirectoryName` verbatim after a capacity check, then call `file_open_directory` with the local candidate.
- Do not reject or canonicalize `..`, nested separators, `/`, or absolute-looking fragments.
- In `file_parent_directory`, retain the temporary-open approach and assign only after success. Remove any redundant close/assignment sequence that could double-close after `file_open_directory` becomes transactional.

**Step 5: Run focused GREEN**

Run:

```bat
RunTests.bat Debug --test-case="Directory*"
```

Expected: all focused directory test cases and assertions pass, with process exit code 0.

### Task 4: Record the durable Base contract

**Files:**
- Modify: `docs/specs/modules/base.md`
- Modify: `docs/sdd/SDD-2026-07-14-directory-path-safety.md`

**Step 1: Update the Base module spec**

- Add the directory navigation contract: `k_max_path` is a hard non-truncating limit; open/sub/current use local construction and failure leaves state unchanged; close is idempotent; accepted path fragments retain Win32-compatible semantics.
- Add the completed SDD to History.

**Step 2: Close the SDD**

- Set Status to `Done` only after focused tests are GREEN.
- Keep the implementation outcome consistent with the approved compatibility scope; document any deviation before continuing.

### Task 5: Full verification and focused commit

**Files:**
- Verify only; no new scope.

**Step 1: Run CPU/build gates**

Run serially in this worktree:

```bat
RunTests.bat Debug
RunArchGate.bat
build_editor.bat Debug
build_sandbox.bat Debug
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
```

Expected: every command exits 0; full doctest counts increase by the new tests and report no failures.

**Step 2: Coordinate and run Base readiness smoke**

- Before launching, confirm no other session owns the GPU/validation window.
- Run `run.bat all Debug --smoke-test-seconds=120`.
- Confirm all Vulkan/DX12 Editor/Sandbox combinations pass, child processes exit, fresh logs contain no validation/debug-layer errors, and `product/config/Engine.ini` is unchanged/restored.

**Step 3: Audit the diff**

Run:

```bat
git diff --check
git status --short
git diff -- project/src/engine/Base/hfile.h project/src/engine/Base/hfile.cpp project/src/tests/Base/hfile_tests.cpp docs/specs/modules/base.md docs/sdd/SDD-2026-07-14-directory-path-safety.md
```

- Verify the only unrelated dirty files remain the pre-existing Tracy LFS artifacts and are neither edited nor staged.
- Review the failure paths for candidate-handle cleanup and exactly-once ownership transfer.

**Step 4: Commit exact files**

Stage only the five SDD-approved implementation/spec files and commit with a root-cause-oriented message, for example:

```bat
git commit -m "fix(base): make directory navigation transactional"
```

Do not stage the plan file again if it was already committed, and do not stage any third-party artifact.
