# Mini SDD: Rotate Gizmo Angle Overlay

## Goal

Improve rotate gizmo drag feedback so users can see the current rotation amount and direction while dragging, similar to Unity/UE style angle wedges.

## Non-goals

- Do not change engine rendering, RHI, RenderGraph, or `UIContext` APIs.
- Do not change rotate transform math, undo/redo transaction semantics, shortcut bindings, or persisted editor settings.
- Do not add polygon drawing support in this step.

## Files

- `project/src/editor/Services/EditorGizmoTypesInternal.h`
- `project/src/editor/Services/RotateGizmoTool.cpp`

## Approach

- Store the current rotate delta in the active `DragSession`.
- During rotate drag updates, keep the session delta synchronized with the snapped rotation delta already used for transforms.
- In `RotateGizmoTool::Draw`, render active rotate feedback before/around the active ring:
  - a semi-transparent radial wedge made from existing `draw_window_line` calls,
  - emphasized start/current direction spokes,
  - an arrow-like current direction marker,
  - a degree label near the arc midpoint.

## Verification

- `git diff --check`
- `RunArchGate.bat`
- Build Editor Debug if no external build process is locking outputs.
- Editor smoke/manual path: rotate a selected entity and verify wedge grows/shrinks, direction is visible, and undo/redo still works.

## Risk / rollback

Risk is limited to Editor UI drawing. If the overlay is noisy or misleading, rollback by removing the drag-session display field and the active rotate overlay drawing helpers; transform behavior remains unchanged.
