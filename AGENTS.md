# AshEngine Agent Rules

This file defines the default project rules for Codex agents working in this repository.

## Scope

- Applies to the whole repository unless a deeper directory provides a more specific `AGENTS.md` or `AGENTS.override.md`.

## Hard Boundaries

- Do not modify Editor code under `project/src/editor` unless the user explicitly overrides this rule for a specific task.
- Respect the Engine / Editor boundary.
- Engine-side work should stay in Engine modules and public Engine-facing abstractions.
- Do not push backend-specific or RHI-internal details directly into Editor-facing code.

## Design And Code Style

- Prefer standard, idiomatic C++.
- Favor clear ownership, small coherent abstractions, and maintainable interfaces over ad-hoc patches.
- When multiple solutions are possible, prefer the more elegant and standards-aligned one as long as it fits the existing codebase.

## Reference Baseline

- For engine architecture, rendering infrastructure, threading, ECS, asset systems, and general low-level design, implementations may heavily reference Unreal Engine 5.7.
- UE 5.7 source under `D:\workspace\UE5.7.1\UnrealEngine` is an approved reference baseline for design direction.

## Local Tool Paths

- Preferred MSBuild path:
  - `C:\Program Files\Microsoft Visual Studio\2022\Enterprise\MSBuild\Current\Bin\MSBuild.exe`
- Alternate MSBuild path also observed on this machine:
  - `C:\Program Files\Microsoft Visual Studio\18\Enterprise\MSBuild\Current\Bin\MSBuild.exe`
- Preferred WinDbg / debugger tool root:
  - `D:\workspace\AKI\Client3.4\Source\Script\Resource\WinDbg\x64`
- Preferred CDB path:
  - `D:\workspace\AKI\Client3.4\Source\Script\Resource\WinDbg\x64\cdb.exe`
- Preferred WinDbg GUI path:
  - `D:\workspace\AKI\Client3.4\Source\Script\Resource\WinDbg\x64\windbg.exe`

## Validation Baseline

- For Engine, runtime, rendering, RHI, startup/shutdown, asset/scene, configuration, or other shared-path changes, validation is not considered complete until both `Sandbox` and `Editor` have been exercised.
- Default validation matrix:
  - `Sandbox` on `Vulkan`
  - `Sandbox` on `DX12`
  - `Editor` on `Vulkan`
  - `Editor` on `DX12`
- Each validation pass should use the normal startup path and graceful shutdown path.
- Only narrow this matrix when the user explicitly asks for a reduced scope, or when the change is clearly private to a single backend or a single executable.

## Error Handling Preference

- Prefer centralized error handling and single-exit style where practical.
- Avoid scattering many early `if (!x) return;` style exits through a function when the code can be expressed more cleanly with the project's existing process-error pattern.
- Prefer the existing error handling helpers such as:
  - `ASH_PROCESS_ERROR`
  - `ASH_LOG_PROCESS_ERROR`
  - `ASH_PROCESS_ERROR_EXIT`
- When touching older code that still uses repeated direct early returns, opportunistically refactor it toward the project's process-error flow if the change remains local, safe, and readable.
- Do not force this mechanically where it would clearly harm readability.
