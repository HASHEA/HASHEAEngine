# Mini SDD: Windows CI unit-test portability

## Status

Done

## Goal

Make the three unit tests failing in GitHub Actions CI #43 and #44 deterministic
across Windows Git checkout line endings and checkout-root path lengths.

## Non-goals

- No Engine, Editor, RHI, RenderGraph, asset-format, or runtime behavior changes.
- No broad test-helper refactor or repository-wide line-ending rewrite.
- No CI runner or toolchain pinning.

## Files

- `project/src/tests/Terrain/terrain_render_graph_tests.cpp`
- `project/src/tests/Terrain/terrain_render_scene_tests.cpp`
- `project/src/tests/Vegetation/vegetation_storage_tests.cpp`
- `README.md`
- `docs/specs/features/terrain.md`
- `docs/specs/features/vegetation.md`
- This Mini SDD

## Approach

- Read C++ source-contract fixtures in text mode so Windows CRLF checkouts are
  normalized before assertions that intentionally match LF-delimited source
  structure.
- Build the Vegetation long-path fixture to an exact 248-character store-root
  length by using full deterministic components plus a final sized padding
  component. This keeps the store root below 260 while ensuring the generated
  owned stage tree crosses 260, independent of checkout-root length variations
  within the supported test environment.
- Record the portable-test contracts in the Terrain and Vegetation feature
  specifications.

## Verification

- Reproduce the two Terrain cases against CRLF source fixtures.
- Run the three exact formerly failing doctest cases.
- `RunTests.bat Debug`
- `RunTests.bat Release`
- `RunArchGate.bat`
- `git diff --check`

Results:

- Both Terrain cases passed against explicit CRLF source fixtures.
- All three formerly failing cases passed independently.
- Isolated `RunTests.bat Debug` passed; the repeated binary run reported
  `965/965` cases and `70128/70128` assertions with two skips.
- Isolated `RunTests.bat Release` passed with `965/965` cases and
  `70123/70123` assertions with two skips.
- `RunArchGate.bat` passed with 35 unchanged legacy warnings.
- Scoped whitespace validation passed for every file in this SDD.

## Risk / rollback

Risk is limited to test behavior. Text-mode source reads intentionally ignore
the platform representation of line endings. The exact 248-character fixture
still exercises the same below-260 store root and above-260 owned stage tree.
Rollback is a straight revert of the listed test and documentation files.
