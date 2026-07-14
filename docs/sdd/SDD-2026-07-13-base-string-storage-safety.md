# Mini SDD: Base string storage safety

## Status

Done on 2026-07-14. Regression coverage now exercises allocator ownership, idempotent shutdown, exact-fit and rejected writes, guard-byte integrity, map allocation release, deduplication, and deterministic packed iteration. The full Debug doctest suite and architecture gate pass, both Debug applications build, and the DX12/Vulkan Editor/Sandbox readiness matrix exits cleanly with the runtime config restored byte-for-byte.

## Goal

Make `StringBuffer` and `StringArray` fail closed at capacity boundaries and pair every allocation with the allocator and object lifetime that created it. Preserve the existing packed-string and interning APIs while removing out-of-bounds writes, foreign-allocator frees, leaked `FlatHashMap` storage, and undefined pointer arithmetic.

## Non-goals

- Replacing the legacy string containers with `std::string` / `std::vector`.
- Changing file enumeration behavior or public call sites.
- Changing Graphics, RenderGraph, RHI, shader, scene, or asset behavior.
- Introducing a new dependency or allocator abstraction.

## Files

- `project/src/engine/Base/hstring.h`
- `project/src/engine/Base/hstring.cpp`
- `project/src/tests/Base/hstring_tests.cpp`
- `docs/specs/modules/base.md`
- `docs/sdd/SDD-2026-07-13-base-string-storage-safety.md`

## Approach

1. Export the existing container methods needed by doctest so tests exercise their real Engine DLL implementation without exporting container internals as a DLL class ABI.
2. Define one capacity rule: `m_uCurrentSize <= m_uBufferSize`; writes that cannot fit, including a consumed string terminator, leave state unchanged and return null where the API permits.
3. Make init/shutdown idempotent and free through the allocator that owns the current storage before adopting a replacement allocator.
4. Construct the `StringArray` map and iterator with correct alignment and placement construction; destroy the map before freeing their shared block so its internal allocations are released.
5. Keep hash lookup as the fast path, compare stored text before accepting a hit, and fall back to packed-string comparison for the collision case so a hash collision cannot alias two different strings.

## Verification

- Focused RED established the original ownership/capacity/leak failures; GREEN: StringBuffer `3/3` cases and `20/20` assertions, StringArray `2/2` cases and `17/17` assertions.
- `RunTests.bat Debug`: `66/66` cases and `876/876` assertions pass.
- `RunArchGate.bat`: PASS with the unchanged 35-entry legacy warning set.
- `build_editor.bat Debug` and `build_sandbox.bat Debug`: PASS.
- `run.bat all Debug --smoke-test-seconds=120`: PASS; child processes exit, fresh logs contain no error/fatal/validation failure, and `product/config/Engine.ini` SHA-256 returns to its pre-run value.
- `git diff --check` and AIDevDoctor plan validation: PASS.

## Risk / rollback

Risk is limited to legacy Base string helpers. The primary compatibility risk is callers that accidentally relied on silent truncation or overflow; the new contract deliberately rejects those writes and logs the failure. Rollback is the isolated branch commit; no serialized data, config, baseline, or rendering contract changes.
