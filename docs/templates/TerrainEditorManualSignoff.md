# Terrain Editor Manual Sign-off

Copy this template to `docs/verification/terrain/<YYYY-MM-DD>-<short-sha>-manual-signoff.md` before testing. A named human tester must personally perform every Editor interaction and visual judgment. AI agents and UI automation may prepare fixtures or collect logs, but they cannot operate this checklist, decide PASS, or sign the record.

## Test identity

| Required field | Value |
| --- | --- |
| Human tester | |
| Test date and time zone | |
| Commit SHA | |
| Branch or PR | |
| Build configuration | |
| Windows version | |
| GPU and driver version | |
| Scene and test asset paths | |

## Checklist

Use `PASS`, `FAIL`, or `BLOCKED`. Every non-PASS result requires a note and issue/reference in the final section.

| Human-operated check | Vulkan result | Vulkan evidence | DX12 result | DX12 evidence |
| --- | --- | --- | --- | --- |
| Create production-default 8193² flat Terrain; save, close, and reopen | | | | |
| Import and reimport PNG | | | | |
| Import and reimport RAW R16 | | | | |
| Import and reimport RAW R32F | | | | |
| Import and reimport EXR | | | | |
| Export PNG, RAW R16, RAW R32F, and EXR; verify size, range, and orientation | | | | |
| Raise and Lower; one history entry per drag; Undo/Redo each | | | | |
| Smooth, Flatten, and Noise; Undo/Redo each | | | | |
| Paint and Erase; Undo/Redo each | | | | |
| Add, duplicate, delete, rename, reorder, hide, adjust strength, and lock layers | | | | |
| Locked layer rejects brush input and preserves stable layer ID | | | | |
| Scene viewport pickup and world-space brush overlay follow the surface | | | | |
| Non-uniform positive scale preserves circular world-space brush radius | | | | |
| Camera, gizmo, and selection arbitration has no cross-talk | | | | |
| Alt+LMB, RMB, MMB, wheel, and focus camera controls remain responsive | | | | |
| Game viewport never authors Terrain | | | | |
| Save, Save Copy As, and Optimize | | | | |
| Clean external reload | | | | |
| Dirty conflict: Reload, Keep Local, and Save Copy As | | | | |
| RecoveredReadOnly / Repair with damaged descriptor | | | | |
| Failed save/reload preserves local dirty bytes and history | | | | |
| New/Open/Reload Scene fail closed while Terrain work is unresolved | | | | |
| Successful Scene switch clears Terrain session, Selection, and UndoRedo in order | | | | |
| No Vulkan/DX12 validation, debug-layer, or application error | | | | |

## Evidence and findings

| Item | Details |
| --- | --- |
| Screenshot paths | |
| Log paths | |
| Created test assets and cleanup result | |
| FAIL/BLOCKED items and issue references | |
| Additional observations | |

## Human declaration

I confirm that I personally performed the Editor operations and visual checks above on the recorded commit. Automation did not operate the checklist or determine the result.

| Final field | Value |
| --- | --- |
| Overall result (`PASS` or `FAIL`) | |
| Tester name | |
| Signature or explicit approval reference | |
| Signed date and time zone | |

The record is valid only when every required identity field is filled, every checklist row has a result for both backends, every non-PASS result is explained, and the human declaration is complete. Only an overall `PASS` satisfies the Terrain Editor manual gate.
