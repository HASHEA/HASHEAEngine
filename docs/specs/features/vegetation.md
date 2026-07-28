---
owner: huyizhou
last_reviewed: 2026-07-28
status: active
---

# Feature Spec: Vegetation Phase 2 authoring and deterministic bake

## Current scope

Phase 2 implements the CPU authoring and bake contract shared by future GPU grass and tree rendering:

```text
.AshVegetation Species
  + .AshVegetationLayer authoring state
  + Scene v7 VegetationComponent(surface + Layer references)
  + immutable surface snapshot
  → deterministic bake
  → content-addressed .AshVegetationChunk objects + ASVM manifest + ASVA active pointer
```

It does not connect vegetation to `VisibleRenderFrame`, `SceneRenderer`, RenderGraph, Graphics, Vulkan, or DX12. Streaming, GPU culling, grass/tree rendering, wind, shadow, impostor, HLOD, and SpeedTree ingestion require a separately approved Phase 3+ SDD.

Phase 2 is `Done` as of 2026-07-28. Tasks 1–11 delivered the CPU authoring/storage/bake implementation, and Task 12 completed durable documentation, fresh generation, Debug/Release focused and full tests, four Editor/Sandbox builds, ArchGate, AIDevDoctor, four-combination readiness, non-bless RenderGate, Standard PerfGate, compared VegetationFullPipeline, and human Vulkan/DX12 disabled-path acceptance. Final review additionally closed mixed-case Create Layer extension handling and scene-local entity-clipboard lifetime with focused regression tests. The completed Phase 2 does not imply runtime vegetation rendering.

## Assets, budgets, and snapshots

| Asset | v1 contract |
| --- | --- |
| `.AshVegetation` | Canonical UTF-8 JSON Species; nonzero 128-bit ID, mesh/material LOD references, integer bounds/filter/density/scale data, render metadata; canonical bytes define the Species SHA-256. |
| `.AshVegetationLayer` | Little-endian `ASVL` authoring container; nonzero Layer ID/generation, seed, Species palette, sparse sorted 32×32 R8 density/weight planes. |
| `.AshVegetationChunk` | Little-endian `ASVC` cooked container; Layer/input/surface identity, sorted Species table, and stable 28-byte quantized instance records. |

The extensions map to `AssetType::Species/Layer/Chunk` and only use typed immutable sync/async loads with an explicit six-field `VegetationLoadBudget`. Generic text/binary/model/material paths fail closed; only Species uses the bounded 1 MiB Editor text preview. Layer/Chunk publication resolves every referenced Species through one Asset-owned immutable `VegetationAssetResolverSnapshot` and checks path, embedded ID, and canonical digest.

`VegetationEditorService::DefaultLoadBudget()` is, by value:

| Field | Default |
| --- | ---: |
| file / payload bytes | 256 MiB each |
| decoded bytes | 1 GiB |
| palette / tile / instance records | 65,534 / 262,144 / 8,388,608 |

`DefaultChunkSetLoadBudget()` reuses that per-file budget and limits manifest entries to 262,144, total inspected bytes to 2 GiB, and retained summary bytes to 64 MiB. These are injectable resident-policy limits, not wire/world caps. The wire palette ceiling is exactly 65,534 Species so density plus every Species plane remains representable by `u16 plane_count`; 65,535 is invalid.

## Exact v1 binary and hash contracts

All integer fields below are little-endian. CRC32 uses reflected IEEE polynomial `0xEDB88320` with initial/final XOR `0xffffffff`.

### ASVL Layer

The `.AshVegetationLayer` header is exactly 80 bytes:

| Offset | Type | Exact value / field |
| ---: | --- | --- |
| 0 / 4 / 6 | `char[4]` / `u16` / `u16` | `ASVL` / schema `1` / header bytes `80` |
| 8 / 12 / 16 | `u32` / `u32` / `u32` | flags `0` / tile resolution `32` / tile size centimeters `3200` |
| 20 / 24 / 32 | `u32` / `u64` / `u64` | palette count / nonzero content generation / Layer seed |
| 40 | `u8[16]` | nonzero Layer ID |
| 56 / 60 | `u32` / `u64` | tile count / payload bytes |
| 68 / 72 / 76 | `u32` / `u32` / `u32` | payload CRC / header CRC / reserved `0` |

The payload CRC covers every byte from offset 80 through exact EOF. The header CRC covers all 80 header bytes with only its offset-72 field set to zero.

| Payload record | Exact relative field order | Canonical constraints |
| --- | --- | --- |
| Palette | `0 species_id[16]`, `16 species_sha256[32]`, `48 path_bytes:u16`, `50 reserved:u16=0`, `52 path_utf8[path_bytes]` | `1..4096` UTF-8 bytes forming a unique canonical asset-root-relative `.AshVegetation` path; records strictly sorted by nonzero Species ID; count `0..65534`. |
| Tile | `0 tile_x:i64`, `8 tile_z:i64`, `16 plane_count:u16`, `18 reserved:u16=0`, `20 record_bytes:u32`, `24 plane_records[record_bytes]` | Tiles strictly sorted by `(tile_z,tile_x)`; first plane is Density, remaining SpeciesWeight planes strictly sorted by Species ID. |
| Plane | `0 kind:u8`, `1 codec:u8`, `2 reserved:u16=0`, `4 species_id[16]`, `20 decoded_bytes:u32=1024`, `24 encoded_bytes:u32`, `28 decoded_crc32:u32`, `32 bytes[encoded_bytes]` | `kind 0=Density, 1=SpeciesWeight`; `codec 0=Raw, 1=Rle`; decoded CRC covers the 1,024 R8 values. Density ID is zero; weight ID is in the palette. |

Payload order is exactly `palette_count` palette records followed by `tile_count` tile records. RLE is canonical `run_length:u16 + value:u8`: each run is `1..1024`, adjacent runs must have different values so every run is maximal, the sum is exactly 1,024, and writer selects RLE only when it is strictly shorter than the 1,024-byte Raw form. Zero planes and tiles without nonzero density are absent.

### ASVC Chunk

The `.AshVegetationChunk` header is exactly 160 bytes:

| Offset | Type | Exact value / field |
| ---: | --- | --- |
| 0 / 4 / 6 | `char[4]` / `u16` / `u16` | `ASVC` / schema `1` / header bytes `160` |
| 8 / 12 | `u32` / `u32` | cooker version `1` / flags `0` |
| 16 / 32 | `u8[16]` / `u8[32]` | Layer ID / ASVI chunk-input SHA-256 |
| 64 / 72 | `i64` / `i64` | chunk x / chunk z |
| 80 | `u8[16]` | surface ID |
| 96 / 104 / 112 | `u64` / `u64` / `u64` | surface content / residency / transform revision |
| 120 / 124 | `u32` / `u32` | Species count / instance count |
| 128 / 132 | `i32` / `i32` | exact min / max world height in millimeters |
| 136 / 144 / 148 | `u64` / `u32` / `u32` | payload bytes / payload CRC / header CRC |
| 152 | `u8[8]` | reserved zero |

The payload CRC covers every byte from offset 160 through exact EOF. The header CRC covers all 160 header bytes with only its offset-148 field set to zero. Payload order is exactly `species_count` ASVL palette-format records followed by `instance_count` fixed records:

| Instance offset | Type / field |
| ---: | --- |
| 0 / 2 / 4 / 6 | `u16 species_index` / `u16 cell_x` / `u16 cell_z` / `u16 candidate_ordinal` |
| 8 / 10 / 12 / 14 | `u16 cell_fraction_x` / `u16 cell_fraction_z` / `u16 yaw_turn` / `u16 scale_q12` |
| 16 / 18 / 20 / 24 | `i16 normal_oct_x` / `i16 normal_oct_y` / `i32 world_height_mm` / `u32 reserved=0` |

Each instance is exactly 28 bytes. The Species table is a strictly ID-sorted, referenced subset of the source palette; instance records use total order `(species_id,cell_z,cell_x,candidate_ordinal)`.

### ASVM manifest and ASVA active pointer

| ASVM offset | Type | Exact value / field |
| ---: | --- | --- |
| 0 / 4 / 6 | `char[4]` / `u16` / `u16` | `ASVM` / schema `1` / header bytes `96` |
| 8 / 24 | `u8[16]` / `u64` | Layer ID / Layer generation |
| 32 | `u8[16]` | surface ID |
| 48 / 56 / 64 | `u64` / `u64` / `u64` | surface content / residency / transform revision |
| 72 / 76 | `u32` / `u32` | entry count / reserved `0` |
| 80 / 88 / 92 | `u64` / `u32` / `u32` | payload bytes / payload CRC / header CRC |

Each ASVM entry is exactly 80 bytes: `0 chunk_x:i64`, `8 chunk_z:i64`, `16 chunk_object_sha256[32]`, `48 chunk_input_sha256[32]`. Entries are strictly sorted by signed `(chunk_z,chunk_x)`. Payload CRC covers all entries; header CRC covers all 96 header bytes with only its offset-92 field zero. The manifest SHA-256 covers the final header followed immediately by the payload, including both stored CRCs. Golden locks: empty ASVM is 96 bytes with SHA-256 `5ab5fd715e95768eb84bfd3494f067d4c4dabec1e3bfac60ae21a3404596e702`; one-entry ASVM is 176 bytes with SHA-256 `f76a5a8bfbc4cb7fcae0f481dae76ef4f4c3f4847c4045d49391f28f47a827ad`.

| ASVA offset | Type | Exact value / field |
| ---: | --- | --- |
| 0 / 4 / 6 | `char[4]` / `u16` / `u16` | `ASVA` / schema `1` / header bytes `48` |
| 8 / 40 / 44 | `u8[32]` / `u32` / `u32` | nonzero manifest SHA-256 / reserved `0` / CRC32 |

ASVA CRC covers exactly the first 44 bytes and excludes its own field. The locked fixture CRC is `0x823f6ef3`.

### ASVI chunk-input preimage and counter hash

| ASVI offset | Type | Exact value / field |
| ---: | --- | --- |
| 0 / 4 / 6 | `char[4]` / `u16` / `u16` | `ASVI` / schema `1` / reserved `0` |
| 8 / 12 / 16 / 20 | `u32` / `u32` / `u32` / `u32` | cooker `1` / tile resolution `32` / tile centimeters `3200` / reserved `0` |
| 24 / 40 | `u8[16]` / `u64` | Layer ID / Layer seed |
| 48 / 56 | `i64` / `i64` | chunk x / chunk z |
| 64 | `u8[16]` | surface ID |
| 80 / 88 / 96 | `u64` / `u64` / `u64` | surface content / residency / transform revision |
| 104 | `u32` | logical tile count `64` |

Starting at offset 108, ASVI writes all 64 logical slots with local z outer and local x inner. Each slot is `slot_index:u8=z*8+x`, `presence:u8`, `reserved:u16=0`, `record_bytes:u32`, then exactly that many bytes of the complete canonical ASVL tile record including global tile coordinates. Absence is exactly `presence=0, record_bytes=0`; presence is `1` with a nonempty record. After all slots, `used_species_count:u32` precedes strictly ID-sorted ASVL palette-format records for the union of nonzero SpeciesWeight planes. SHA-256 covers the complete stream and excludes Layer generation.

The locked all-absent fixture uses Layer ID bytes `00..0f`, seed `0x0123456789abcdef`, chunk `(-2,3)`, surface ID bytes `10..1f`, revisions `(4,5,6)`, 64 absent slots, and zero Species. It is exactly 624 bytes and hashes to `8d7e1c07f44858323ffddb12b27daad8ded267169bdf22c7397f366a7cd7d9c3`.

Counter hash v1 folds these ten u64 words in order: Layer ID low/high little-endian halves, chunk x/z two’s-complement bits, cell x/z, Species ID low/high little-endian halves, Layer seed, candidate ordinal. Arithmetic is modulo `2^64`:

```text
splitmix64(x):
  z = x + 0x9E3779B97F4A7C15
  z = (z xor (z >> 30)) * 0xBF58476D1CE4E5B9
  z = (z xor (z >> 27)) * 0x94D049BB133111EB
  return z xor (z >> 31)

state = 0x6A09E667F3BCC909 xor cooker_version
for word in key_words: state = splitmix64(state xor splitmix64(word))
random(stream) = splitmix64(state xor (0xD1B54A32D192ED03 * (stream + 1)))
```

Streams `0..4` are acceptance, X jitter, Z jitter, yaw, and scale respectively. Golden values:

| Input | State | Stream 0 | Stream 1 | Stream 2 | Stream 3 | Stream 4 |
| --- | --- | --- | --- | --- | --- | --- |
| Ten zero words, cooker 1 | `936cd7179cecc6f6` | `b69aaf248fe5723e` | `c9d8c945898ec42b` | `8818d088186f267b` | `faeb1d600eaa91b7` | `f432147eb52618d8` |
| Layer ID `00..0f`, chunk `(-2,3)`, cell `(17,29)`, Species ID `10..1f`, seed `0123456789abcdef`, candidate `5`, cooker 1 | `1482fb4898b68eda` | `dbefc5819d9be996` | `da4acc7ef01435b5` | `c48fc8b560bbbbe5` | `3bae788582c73257` | `6ec202003e5df319` |

## Surface and Scene contract

Asset owns the worker-safe `IVegetationSurfaceSnapshot`, batch request/result DTOs, and fail-closed sampling wrapper. Scene owns `VegetationSurfaceBinding` and `IVegetationSurfaceProvider`; capture produces a `shared_ptr<const IVegetationSurfaceSnapshot>` and never exposes `Scene`, Terrain internals, Editor state, or mutable catalog data to a worker.

A snapshot owns immutable resident CPU data, a copied transform, and `{surface_id, content_revision, residency_revision, transform_revision}`. Sampling is resident-only, has at most 4,096 requests per batch, preserves request order, observes cancellation and an absolute deadline, and publishes no partial result. `Ready` normals and eight material-slot weights are validated; `Outside` skips a candidate, while `Pending`, `Failed`, malformed output, exceptions, or identity drift fail the whole chunk.

Scene JSON schema v7 adds reference-only:

```cpp
struct VegetationComponent
{
    std::string layer_asset_path;
    EntityId surface_entity_id = 0;
    bool enabled = true;
};
```

The Layer path is a canonical asset-root-relative `.AshVegetationLayer`; the surface entity is nonzero, existing, and not self. Scene stores no palette, tile, chunk, or instance transform. v3–v6 load with no Vegetation component. Extraction and `get_vegetation_version()` exist for Editor/provider binding and future Phase 3, but Phase 2 does not add a render packet.

## Authoring and history

- World partition is 256 m per signed `VegetationChunkCoord`; local XZ is canonical `[0,256)`. Authoring tiles are 32 m with 1 m cells and use mathematical floor division at negative boundaries.
- Brush inputs are integer millimeters: radius `250..1,024,000`, strength `1..255`, falloff `0..255`, and spacing `1..2,048,000`. Canonicalization removes duplicate points, merges collinear runs, safely segments large deltas, and resamples with checked integer arithmetic and ties-to-even rounding.
- For cell-center distance `d`, `inner=floor(radius*(255-falloff)/255)`. Coverage is `0` for `d>=radius`, `65535` when falloff is zero or `d<=inner`, otherwise `round_ties_even((radius-d)*65535/(radius-inner))`; amount is `round_ties_even(strength*coverage/65535)`.
- Paint saturating-adds density and the selected Species weight. Erase saturating-subtracts density and every Species weight in affected tiles. Zero planes/tiles are removed.
- New Layer requires a new canonical `.AshVegetationLayer` path with a case-insensitive extension, creates a nonzero OS-random 128-bit ID, starts at generation 1, and is Dirty. Palette Add/Replace/Remove use checked before/after patches; Remove with referenced weights requires explicit clear-weights confirmation.
- One valid stroke or palette edit produces one direction-neutral, generation-checked `VegetationLayerPatch`. The already-applied mutation enters history through `RecordExecutedCommand`; it is never executed twice or merged. `Recorded`, `RolledBack`, and `RollbackFailed` have distinct state transitions. History entries carry a Layer document key, and confirmed reload removes only that document’s complete entries.

## Deterministic bake and checked storage

Traversal is fixed as `(chunk_z, chunk_x) → (cell_z, cell_x) → species_id → candidate_ordinal`. The counter hash and streams are locked above. Integer thresholding, ties-to-even quantization, and final key `(species_id, cell_z, cell_x, candidate_ordinal)` make repeat and input-order cooks byte-identical.

`ASVI` is the canonical chunk-input preimage: it includes Layer ID/seed, chunk coordinate, full surface identity, all 64 logical tile slots including absence records, and the sorted union of used Species. The approved zero-content fixture hash is `8d7e1c07f44858323ffddb12b27daad8ded267169bdf22c7397f366a7cd7d9c3`. ASVC header input digest, ASVM entry input digest, and recomputation must match.

The chunk store beside the Layer contains immutable `objects/<sha>.AshVegetationChunk`, immutable `manifests/<sha>.asvm`, and one fixed `active.asva`. ASVM is the complete sorted mapping; ASVA is a 48-byte pointer to its manifest digest. Workers only read and prepare durable artifacts. The Editor logic thread revalidates operation, Layer/source/native identity, surface revisions, Species digests, target/store identity, cancellation, and deadline before the checked commit can create/replace ASVA.

Dirty sets are exact:

- Palette/Species change: current-manifest coordinates referencing the Species ∪ before-authoring coordinates with a nonzero weight plane ∪ after-authoring coordinates with a nonzero weight plane.
- Seed or surface identity/revision change, and restart generation mismatch without complete same-session before evidence: current-manifest coordinates ∪ authoring coordinates containing nonzero density.

Before evidence accumulates across Undo/Redo, Save, failed/cancelled/stale bake, and palette removal; only a matching-generation successful ASVA commit acknowledges it. Any dirty-chunk failure aborts the complete prepared generation. Pointer failure leaves the prior active manifest and its objects as last-known-good (LKG); no partial manifest becomes active.

Layer Save/Copy As and chunk publication use bounded stable-handle reads, strict canonical readback, move-only prepared capabilities, attempted exact-owned stage cleanup, complete operation controls, named cooperative commit leases, revision/native-identity checks, and replace-only versus create-new separation. Cleanup failure is a failed operation and leaves the precise owned stage file/tree in the registry’s retained set for later retry. This protects cooperating writers using the Vegetation APIs. It does not eliminate namespace races from an equal-permission process that bypasses them.

## Editor, provider, and lifecycle

Without a provider, Create/Open, palette edits, Save, and Reload remain available. Paint/Erase/Bake are disabled and report exactly `No vegetation surface provider is registered.` Product bootstrap must not install a test provider or fabricate/cache a surface binding. Bake accepts a nonzero binding for that exact request; Pending retries reuse only its frozen value.

Providers are `trusted in-process` Engine extensions: they must sample immutable resident data, avoid I/O, return or observe cancel/deadline within 50 ms, and never depend on mutable Scene/Editor/Terrain state. Normal cooperative teardown requests cancellation, waits for completion observation, observes any exception, and exact-joins. C++17 cannot safely terminate a malicious or stuck provider, so a contract-violating call can make normal teardown wait without a bound; there is no provider-specific hard kill or guaranteed diagnostic. A generic readiness process watchdog may terminate the process and fail the gate, while stronger provider isolation requires a future out-of-process design.

`EditorApplicationImpl` creates the task executor before the service, initializes the service after AssetDatabase using nonzero defaults, ticks it before panel update, and treats initialization failure as application initialization failure. Shutdown detaches panels first, then performs the cooperative cancel/observe/exact-join sequence, attempts `RetryAll` cleanup for exact-owned stages, releases the service, and finally releases the executor. Cleanup can retain precisely identified stage files/trees and report failure; it is not unconditional deletion. No successfully completed teardown leaves a worker executing Engine/Editor code.

Scene Hierarchy entity snapshots are scene-local clipboard data. New/load/reload/replace and panel detach clear both snapshot and preferred-parent clipboard arrays before another paste can be issued. Same-scene Duplicate/Paste still restores a complete forest with one ID remap, so a copied Vegetation owner and copied surface bind to the copied surface; stale unmatched numeric IDs cannot cross a scene boundary through the panel clipboard.

## Verification and handoff

Authoritative automated coverage is in `project/src/tests/Vegetation/`, `project/src/tests/Scene/`, and `project/src/tests/Editor/`. After building the corresponding Tests target, focused verification uses the tested direct commands:

```bat
product\bin64\Debug-windows-x86_64\Tests.exe --test-case="*Vegetation*"
product\bin64\Release-windows-x86_64\Tests.exe --test-case="*Vegetation*"
```

The `RunTests.bat ... --test-case="*Vegetation*"` wrapper invocation is not focused evidence because its leading-star filter was consumed and it ran the full suite. Against source HEAD `10dd9d1433dac84d38b4aa6bacf25a50956d36a1`, direct Debug/Release focused runs each passed `338/338` cases and `39276/39276` assertions (`177` skipped), while the Debug/Release full suites each passed `515/515` cases and `41756/41756` assertions. After final-review remediation, fresh Debug/Release focused suites each passed `339/339` cases and `39279/39279` assertions (`177` skipped), and full suites each passed `516/516` cases and `41759/41759` assertions; the mixed-case Create Layer section and exact clipboard-lifetime test also passed independently. The production panel delegates its active-scene/reload/replace subscriptions to a shared lifecycle binding helper; the clipboard test binds that helper to a real `EditorEventBus`, publishes all three events and checks both arrays after each event. Its supporting tree-widget drag reset is an unchanged three-field inline state operation. Remediation adds ten assertions, while the current Windows token cannot create the optional directory symlink (`CreateSymbolicLinkW` error `1314`) and therefore executes seven fewer conditional storage assertions; `+10-7=+3` explains the final totals relative to the original evidence. Fresh generation, four builds, ArchGate with `35` unchanged legacy warnings, AIDevDoctor, Editor/Sandbox × Vulkan/DX12 readiness, non-bless RenderGate, Standard PerfGate, compared VegetationFullPipeline, and config/log restoration all passed.

The post-remediation candidate first reran Standard PerfGate across Sandbox/Editor × Vulkan/DX12 at `Intermediate/test-reports/perf-gate/20260728-111016-083-78468-8835755f/`; all four runs passed without warnings/failures. A later frozen rerun at `Intermediate/test-reports/perf-gate/20260728-111537-301-83988-122cc676/` was retained as `FAIL` because Sandbox/DX12 had one `1.785159 s` sampling gap. With no source/config/baseline change, the complete retry at `Intermediate/test-reports/perf-gate/20260728-112001-249-78452-35b6d97b/` passed all four runs with maximum gap `0.089162 s`, no warnings/failures, expected `MISSING` Standard baseline status and no baseline change. AIDevDoctor package `Intermediate/test-reports/ai-dev/20260728-032406/` indexed the retained FAIL and replacement PASS, reported `4/4` fresh coverage and no inferred validation gap.

After event-bus lifecycle-test strengthening, Debug/Release exact tests each passed `1/1` case and `6/6` assertions, focused suites retained `339/339` cases and `39279/39279` assertions, and full suites retained `516/516` cases and `41759/41759` assertions. Editor Debug/Release and ArchGate passed again. Fresh Editor Vulkan/DX12 readiness (PIDs `84004` / `34516`) reached frame `3`, produced four logs with zero rejected diagnostics and complete shutdown/all-memory-free markers, exited both roots and restored all four configs byte-for-byte. Frozen Standard report `Intermediate/test-reports/perf-gate/20260728-113221-924-78488-26510d98/` passed all four runs with no warning/failure and maximum gap `0.046864 s`; the final documentation-locked rerun is indexed by stable AIDevDoctor package `Intermediate/test-reports/ai-dev/phase2-final-freeze-20260728-v2/`, which must show `4/4` fresh coverage, fresh logs/PerfGate and no inferred validation gap.

The 2026-07-28 human gate used a unique session-owned asset directory and the two reviewed same-ID/different-digest Species fixtures. The human completed New Layer, Add, Save, Reload, same-ID Replace, Remove, Undo/Redo, responsiveness, and exact no-provider disabled-reason checks in Vulkan and DX12. The first DX12 UI report was not accepted as complete because the distinct DX12 Layer had not reached disk; a corrective DX12 run saved `manual-dx12.AshVegetationLayer`, AI verified its original ID/path/digest read-only, and the human repeated the remaining flow and reported PASS. All three Editor sessions exited normally with zero rejected Engine/Application diagnostics. The four config files were restored byte-for-byte, the verified session directory was deleted, and runtime/gate roots returned to zero.

Phase 3 may adapt a production Terrain surface snapshot and convert validated ASVC DTOs to the existing GPUDriven prototype/page contract. It must not change the Phase 2 asset bytes, brush/hash ordering, provider boundary, checked publication, or LKG semantics without a new approved SDD.

## History

- [Phase 2 SDD](../../sdd/SDD-2026-07-16-vegetation-authoring-and-bake.md), original approved input SHA-256 `947CF950782752599F3D6E51918D8E082039237FAC4B3A17F459E8481D4520CF`; the final archived `Done` hash is recorded in the executable Phase 2 plan and final review evidence.
- [Overall S3 vegetation SDD](../../sdd/SDD-2026-07-13-world-scale-gpu-vegetation.md).
- [Executable Phase 2 plan](../../plans/2026-07-16-vegetation-authoring-and-bake.md).
