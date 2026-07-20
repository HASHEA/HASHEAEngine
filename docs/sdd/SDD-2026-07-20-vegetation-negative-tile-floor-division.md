# Mini SDD: Vegetation negative-tile floor division

## Status

Done

## Goal

Make brush texel-to-tile mapping deterministic in optimized Windows builds when a negative
cell coordinate is an exact multiple of the 32-texel tile width. World-scale authoring must
address the same owning tile in Debug and Release for positive and negative coordinates.

## Non-goals

- No brush format, patch, history, RHI, renderer, or terrain-provider changes.
- No compiler flags or toolchain pin changes.

## Files

- `project/src/engine/Function/Asset/VegetationLayer.cpp`
- `project/src/tests/Vegetation/vegetation_brush_tests.cpp`
- `project/src/tests/Editor/vegetation_undo_redo_tests.cpp`
- `docs/specs/modules/asset.md`
- `docs/sdd/SDD-2026-07-20-vegetation-negative-tile-floor-division.md`

## Approach

Add Debug/Release regression coverage at cells `-32` and `-64`. Replace the current
quotient/remainder correction with an equivalent positive-divisor floor formula that avoids
the optimized `% 32` code shape while remaining safe for `INT64_MIN`:
`value >= 0 ? value / divisor : -1 - (-(value + 1) / divisor)`.

## Verification

- `RunTests.bat Debug --test-case="Vegetation brush*"`
- `RunTests.bat Release --test-case="Vegetation brush*"`
- `RunTests.bat Debug`
- `RunTests.bat Release`
- `RunArchGate.bat`
- `powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan`
- `git diff --check`

## Risk / rollback

Risk is limited to internal signed floor division used by vegetation tile/chunk mapping and
brush bounds. Boundary tests and the sparse history regression cover negative exact multiples;
the existing suites cover positive, negative non-multiples, patch dirtiness, and serialization.
Rollback is the isolated commit if either configuration changes canonical results.

## Result

The Release-only RED reproduced at cell `-32` while the same Debug binary passed. After the
floor formula change, Debug and Release brush suites both pass 12/12 cases and 241/241
assertions; the restored negative-tile command suite passes 3/3 cases and 72/72 assertions,
and the history suite passes 3/3 cases and 102/102 assertions in both configurations. Final
Debug and Release full suites each pass 280/280 cases and 6898/6898 assertions.
