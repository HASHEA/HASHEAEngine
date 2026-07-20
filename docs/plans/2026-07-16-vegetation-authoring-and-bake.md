# Vegetation Authoring and Deterministic Bake Implementation Plan

**Status:** Ready for execution against approved SDD SHA-256 `80D8CDE760EEF9FF8F9EA7EAB6AC66ACBDDDED827A69752519BE12A80777743B` (includes the approved Task 2 codec and Task 3 typed-load execution clarifications).

> **For agentic workers:** REQUIRED SUB-SKILL: use `superpowers:executing-plans` for task execution, `superpowers:test-driven-development` for every behavior change, `superpowers:receiving-code-review` for findings, and `superpowers:verification-before-completion` before any completion claim. Keep one implementation task in progress at a time and request two independent read-only reviews before each focused commit.

**Goal:** Build the Phase 2 CPU authoring pipeline for grass and tree species: strict versioned assets, immutable surface sampling, Scene v7 binding, deterministic sparse brush patches, checked save/reload, deterministic incremental bake, and a provider-safe Editor panel.

**Architecture:** Function/Asset owns canonical DTOs, codecs, the immutable surface snapshot and batch-validation wrapper, sparse mutation, persistence, deterministic bake, and content-addressed chunk publication. Function/Scene owns only surface binding/provider capture plus the asset-backed Scene component and depends one-way on the Asset snapshot contract. Editor owns command history, lifecycle-safe asynchronous orchestration, and UI intent; production bootstrap intentionally injects no surface provider until the later Terrain adapter plan.

**Tech stack:** C++17, GLM, nlohmann JSON already present in the repository, Windows file primitives, Premake5/MSBuild, doctest, AssetDatabase, Scene, Editor `UIContext`, PowerShell verification gates.

---

## Preconditions and hard scope guard

- Execute only in worktree `D:\workspace\AshEngine\HASHEAEngine\.worktrees\gpu-driven-vegetation-phase2` on branch `codex/gpu-driven-vegetation-phase2`.
- Starting integration point is approved-design amendment commit `9b47dbaa0ca8856480db65b444c320767237fb1d` (approved base design `24d88c9bba3195caf0d8c2b8d5cc835612a44faa`); do not rebase a dirty implementation task.
- Design truth is `docs/sdd/SDD-2026-07-16-vegetation-authoring-and-bake.md` at exact SHA-256 `80D8CDE760EEF9FF8F9EA7EAB6AC66ACBDDDED827A69752519BE12A80777743B`. If the hash changes before Task 12, stop and review the new document before continuing. Task 12 alone may update Status/conclusions after all evidence exists and must record the original approved input hash `947CF950782752599F3D6E51918D8E082039237FAC4B3A17F459E8481D4520CF`, the Task 2 execution-clarified hash `B51F296F830C21B5524D5C934262F38AD7A834AFE2F5E9435E8FFC6567D755C9`, this Task 3 execution-clarified input hash, and the final archived hash.
- Existing user-owned dirty files are:
  - `project/thirdparty/tracy/tracy-csvexport.exe`
  - `project/thirdparty/tracy/tracy-profiler.exe`
  Never stage, reset, rewrite, inspect as generated evidence, or include either file in a commit.
- Phase 2 must not modify any path under:
  - `project/src/engine/Graphics/`
  - `project/src/engine/Function/Render/`
  - `project/src/engine/Shaders/`
  - `tools/render/goldens/`
  - `tools/perf/perf_gate_baselines.json`
- Do not include or copy Terrain branch internals. The only production surface provider in this phase is a nullable injected interface; deterministic providers live under `project/src/tests/` only.
- Do not add a third-party dependency, a render-ready state, a GPU upload, a renderer registration, a `VisibleRenderFrame` field, or a GPUDriven conversion.
- New test `.cpp` files require a fresh `generate_vs2022.bat`. `project/src/tests/premake5.lua` changes are limited to explicitly linking the Scene/Editor production `.cpp` files itemized by Tasks 4, 6, 9, and 11; do not add broad source globs.
- Every GPU, RenderGate, or PerfGate command requires a newly coordinated exclusive window. Before Task 12, the only GPU use is Task 3's repository-mandated four-combination Debug readiness smoke; Tasks 4–11 remain CPU/static only.
- Every focused commit must use an explicit `git add -- <paths>` list and pass this staged-path audit before commit:

```powershell
$staged = @(git diff --cached --name-only)
$forbidden = @($staged | Where-Object {
    $_ -like 'project/thirdparty/tracy/*' -or
    $_ -like 'project/src/engine/Graphics/*' -or
    $_ -like 'project/src/engine/Function/Render/*' -or
    $_ -like 'project/src/engine/Shaders/*' -or
    $_ -eq 'tools/perf/perf_gate_baselines.json' -or
    $_ -like 'tools/render/goldens/*'
})
if ($forbidden.Count -ne 0) { throw "Forbidden staged paths: $($forbidden -join ', ')" }
```

## Stable type and naming contract

The following names are fixed by this plan so later tasks do not invent incompatible variants:

```cpp
namespace AshEngine
{
    using VegetationId = std::array<uint8_t, 16>;
    using VegetationSha256 = std::array<uint8_t, 32>;

    struct VegetationChunkCoord
    {
        int64_t x = 0;
        int64_t z = 0;
    };

    struct VegetationLoadBudget
    {
        uint64_t max_file_bytes = 0;
        uint64_t max_payload_bytes = 0;
        uint64_t max_decoded_bytes = 0;
        uint32_t max_palette_records = 0;
        uint32_t max_tile_records = 0;
        uint32_t max_instance_records = 0;
    };

    struct VegetationLoadCost
    {
        uint64_t file_bytes = 0;
        uint64_t payload_bytes = 0;
        uint64_t decoded_bytes = 0;
        uint32_t palette_records = 0;
        uint32_t tile_records = 0;
        uint32_t instance_records = 0;
    };
}
```

- All decode APIs take a caller-provided `VegetationLoadBudget`; zero is a zero budget, not an implicit unlimited value.
- `VegetationLoadCost` is wire-derived, not allocator-derived: `file_bytes` is the exact immutable byte snapshot length; `payload_bytes` is the declared binary payload length (or the exact Species JSON byte length). `decoded_bytes` uses the exact v1 formulas locked in the SDD: Species `70 + 4*LOD count + all canonical DTO string bytes`; Layer `32 + Σ(48+palette path bytes) + 16*tile count + Σ(17+1024 per plane)`; Chunk `112 + Σ(48+species path bytes) + 28*instance count`. Counts are exact logical record counts. Never use `sizeof` on implementation DTOs, vector capacity, allocator overhead, or cache-container cost. The codec computes every term with checked arithmetic before DTO allocation/publication; cached admission reuses this exact value.
- `VegetationComponent::surface_entity_id` is a `uint64_t` in `SceneComponents.h`. `EntityId` remains declared in `Scene.h`, which includes `SceneComponents.h`, so using the alias in the component header would create an ordering dependency.
- All test names begin with `Vegetation` so `RunTests.bat <Config> --test-case="*Vegetation*"` is the common focused gate.

## Task 1: Add core coordinates, hashes, cancellation control, and the immutable surface wrapper

**Files:**

- Create: `project/src/engine/Function/Asset/VegetationTypes.h`
- Create: `project/src/engine/Function/Asset/VegetationCodec.h`
- Create: `project/src/engine/Function/Asset/VegetationCodec.cpp`
- Create: `project/src/engine/Function/Asset/VegetationSurface.h`
- Create: `project/src/engine/Function/Asset/VegetationSurface.cpp`
- Create: `project/src/engine/Function/Scene/VegetationSurfaceProvider.h`
- Create: `project/src/engine/Function/Scene/VegetationSurfaceProvider.cpp`
- Create: `project/src/tests/Vegetation/VegetationTestSupport.h`
- Create: `project/src/tests/Vegetation/vegetation_contract_tests.cpp`

- [ ] **Step 1: Write the focused RED tests**

Create the test directory once before adding files:

```powershell
New-Item -ItemType Directory -Force project/src/tests/Vegetation | Out-Null
```

Add exact SHA-256, CRC32, negative-coordinate, capture-shape, batch-shape, normalization, aggregate-priority, and cancellation/deadline cases. The first test block includes these assertions:

```cpp
#include "Function/Asset/VegetationCodec.h"
#include "Function/Asset/VegetationSurface.h"
#include "Function/Scene/VegetationSurfaceProvider.h"
#include "Vegetation/VegetationTestSupport.h"
#ifdef TYPE_TO_STRING
#undef TYPE_TO_STRING
#endif
#include "doctest.h"

TEST_CASE("Vegetation core hashes and negative chunk coordinates are canonical")
{
    const std::vector<uint8_t> abc{ 'a', 'b', 'c' };
    CHECK(VegetationTest::ToHex(AshEngine::vegetation_sha256(nullptr, 0)) ==
        "e3b0c44298fc1c149afbf4c8996fb92427ae41e4649b934ca495991b7852b855");
    CHECK(VegetationTest::ToHex(AshEngine::vegetation_sha256(abc.data(), abc.size())) ==
        "ba7816bf8f01cfea414140de5dae2223b00361a396177a9cb410ff61f20015ad");
    CHECK(AshEngine::vegetation_crc32(
        reinterpret_cast<const uint8_t*>("123456789"), 9) == 0xcbf43926u);

    AshEngine::VegetationChunkCoord chunk{};
    glm::dvec2 local{};
    REQUIRE(AshEngine::split_vegetation_world_xz(glm::dvec2(-0.001, -256.001), chunk, local));
    CHECK(chunk.x == -1);
    CHECK(chunk.z == -2);
    CHECK(local.x == doctest::Approx(255.999));
    CHECK(local.y == doctest::Approx(255.999));
}

TEST_CASE("Vegetation surface wrapper rejects a partial batch without publishing residue")
{
    VegetationTest::ScriptedSurfaceSnapshot snapshot{};
    snapshot.result.status = AshEngine::VegetationSurfaceStatus::Ready;
    snapshot.result.samples = {
        VegetationTest::ReadySurfaceSample(0, 12.0, glm::dvec3(0.0, 2.0, 0.0))
    };
    const std::vector<AshEngine::VegetationSurfaceSampleRequest> requests{
        VegetationTest::SurfaceRequest(0.5, 0.5),
        VegetationTest::SurfaceRequest(1.5, 1.5)
    };
    const AshEngine::VegetationOperationControl control =
        VegetationTest::ActiveControl(std::chrono::milliseconds(50));

    const AshEngine::VegetationSurfaceBatchResult result =
        AshEngine::sample_vegetation_surface_batch(snapshot, requests, control);
    CHECK(result.status == AshEngine::VegetationSurfaceStatus::Failed);
    CHECK(result.samples.empty());
}
```

`VegetationTestSupport.h` defines `ToHex`, `ScriptedSurfaceSnapshot`, `ReadySurfaceSample`, `SurfaceRequest`, and `ActiveControl`; these helpers remain under tests and are never linked into Editor/Sandbox.

Also add cases for:

- world/local round-trip at `0`, `255.999`, `256`, `-0.001`, `-256`, and int64 overflow;
- the FIPS 180-4 SHA-256 56-byte `abcdbc...nopq` vector in addition to empty/`abc`, so the portable padding path crosses the final-block boundary;
- empty requests, 4097 requests, null cancellation state, default/expired deadline, invalid coarse bounds, and valid requests outside coarse bounds being delegated to the snapshot rather than rejected by the wrapper;
- `Ready+Outside -> Ready`, `Ready+Pending -> Pending`, `Pending+Failed -> Failed`;
- duplicate/out-of-order indices, declared aggregate mismatch, non-ready residue, batch size 4097, NaN/Inf local coordinates;
- normal length `0`, `1e-21`, Inf, normalization of `(0,2,0)`, and slope `0/1571/1571` for up/+X/down;
- material weights totaling 254 or 256;
- capture `Ready+snapshot`, `Ready+null`, `Pending+null`, `Pending+snapshot`, `Failed+null`, `Outside`;
- all-zero `surface_id` rejection and a scripted snapshot whose `identity()` returns one valid identity before `sample_batch` and a different content/residency/transform identity afterward; both must return `Failed` with no published samples;
- a pre-cancelled control and an expired absolute deadline returning `Failed` before provider output is accepted.

- [ ] **Step 2: Run RED**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation core*"
RunTests.bat Debug --test-case="Vegetation surface*"
```

Expected RED: missing `VegetationTypes.h`, `VegetationCodec.h`, and `VegetationSurface.h` symbols. The failure must not come from Tracy files, Premake syntax, or an unrelated test.

- [ ] **Step 3: Implement the minimum public API and validation**

Expose this API shape:

```cpp
ASH_API VegetationSha256 vegetation_sha256(const uint8_t* bytes, size_t byte_count);
ASH_API uint32_t vegetation_crc32(const uint8_t* bytes, size_t byte_count);
ASH_API bool split_vegetation_world_xz(
    const glm::dvec2& world_xz,
    VegetationChunkCoord& out_chunk,
    glm::dvec2& out_local_xz);

struct VegetationOperationControl
{
    std::shared_ptr<const std::atomic_bool> cancel_requested{};
    std::chrono::steady_clock::time_point deadline{};
};

struct VegetationSurfaceBounds
{
    VegetationChunkCoord min_chunk_inclusive{};
    VegetationChunkCoord max_chunk_inclusive{};
};

ASH_API bool evaluate_vegetation_surface_normal(
    const glm::dvec3& world_normal,
    glm::dvec3& out_normalized_world_normal,
    uint16_t& out_slope_milliradians);

class ASH_API IVegetationSurfaceSnapshot
{
public:
    virtual ~IVegetationSurfaceSnapshot() = default;
    virtual VegetationSurfaceIdentity identity() const = 0;
    virtual VegetationSurfaceBounds bounds() const = 0;
    virtual VegetationSurfaceBatchResult sample_batch(
        const std::vector<VegetationSurfaceSampleRequest>& requests,
        VegetationOperationControl control) const = 0;
};

ASH_API VegetationSurfaceBatchResult sample_vegetation_surface_batch(
    const IVegetationSurfaceSnapshot& snapshot,
    const std::vector<VegetationSurfaceSampleRequest>& requests,
    VegetationOperationControl control);

// Function/Scene/VegetationSurfaceProvider.h begins here. It includes the
// Asset snapshot contract; the Asset files never include this Scene header.
struct VegetationSurfaceBinding
{
    uint64_t surface_entity_id = 0;
};

struct VegetationSurfaceCaptureResult
{
    VegetationSurfaceStatus status = VegetationSurfaceStatus::Failed;
    std::shared_ptr<const IVegetationSurfaceSnapshot> snapshot{};
    std::string detail{};
};

class ASH_API IVegetationSurfaceProvider
{
public:
    virtual ~IVegetationSurfaceProvider() = default;
    virtual VegetationSurfaceCaptureResult capture(VegetationSurfaceBinding binding) const = 0;
};

ASH_API VegetationSurfaceCaptureResult capture_vegetation_surface(
    const IVegetationSurfaceProvider* provider,
    VegetationSurfaceBinding binding);
```

Implement floor division, checked conversion, SHA-256 FIPS 180-4, reflected IEEE CRC32, batch validation in a temporary result, exact status priority, non-ready zeroing, double normalization, and slope rounding. Every new public header includes `Base/hcore.h` plus its own required standard/GLM headers and compiles without PCH or transitive includes; every definition in `VegetationTestSupport.h` is `inline`, and Task 1 does not edit tests Premake or add a duplicate test translation unit. `VegetationSurfaceBounds` is a conservative chunk-space coverage only: both axes require `min_chunk_inclusive <= max_chunk_inclusive`, while local holes/out-of-bounds points are still decided by per-sample `Outside`; the wrapper never short-circuits a valid request merely because it is outside the coarse range. `evaluate_vegetation_surface_normal` rejects non-finite/length `<=1e-20`, publishes a double-normalized vector only on success, and computes the SDD ties-to-even milliradian slope; Task 1 wrapper and Task 8 baker are its two production consumers. `sample_vegetation_surface_batch` accepts exactly `1..4096` requests, requires a non-null cancellation state and a non-default unexpired absolute deadline, validates bounds, then reads and validates a nonzero `surface_id` identity immediately before invoking `sample_batch`. It reads identity again after provider return but before accepting any result and requires exact equality of ID plus content/residency/transform revisions; invalid, throwing, or changing identity returns `Failed` with no samples. `sample_batch` receives the shared atomic cancel flag and absolute deadline. The trusted in-process provider contract is resident-only, no IO, and return-or-observe-cancel within 50 ms; the wrapper catches exceptions but does not claim it can forcibly terminate a violating provider.

- [ ] **Step 4: Run GREEN and architecture checks**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation core*"
RunTests.bat Debug --test-case="Vegetation surface*"
RunTests.bat Release --test-case="Vegetation core*"
RunTests.bat Release --test-case="Vegetation surface*"
RunArchGate.bat
git diff --check -- project/src/engine/Function/Asset/VegetationTypes.h project/src/engine/Function/Asset/VegetationCodec.h project/src/engine/Function/Asset/VegetationCodec.cpp project/src/engine/Function/Asset/VegetationSurface.h project/src/engine/Function/Asset/VegetationSurface.cpp project/src/engine/Function/Scene/VegetationSurfaceProvider.h project/src/engine/Function/Scene/VegetationSurfaceProvider.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_contract_tests.cpp
```

Expected GREEN: all focused cases pass in Debug/Release and ArchGate introduces no new violation.

- [ ] **Step 5: Review and selectively commit**

Review 1 checks arithmetic, SHA/CRC vectors, identity validity/stability before and after sampling, aggregate status, cancellation/deadline, and no partial result publication. Review 2 mechanically proves `Function/Asset/VegetationSurface.*` has no Scene include, `Function/Scene/VegetationSurfaceProvider.*` depends one-way on the Asset contract, and there is no Terrain, product test-provider, or Function/Render include.

```bat
git add -- project/src/engine/Function/Asset/VegetationTypes.h project/src/engine/Function/Asset/VegetationCodec.h project/src/engine/Function/Asset/VegetationCodec.cpp project/src/engine/Function/Asset/VegetationSurface.h project/src/engine/Function/Asset/VegetationSurface.cpp project/src/engine/Function/Scene/VegetationSurfaceProvider.h project/src/engine/Function/Scene/VegetationSurfaceProvider.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_contract_tests.cpp
git commit -m "feat(vegetation): add core and surface contracts"
```

## Task 2: Add strict Species, Layer, and Chunk v1 codecs

**Files:**

- Create: `project/src/engine/Function/Asset/VegetationSpecies.h`
- Create: `project/src/engine/Function/Asset/VegetationSpecies.cpp`
- Create: `project/src/engine/Function/Asset/VegetationAssetCodecInternal.h`
- Create: `project/src/engine/Function/Asset/VegetationLayer.h`
- Create: `project/src/engine/Function/Asset/VegetationLayer.cpp`
- Create: `project/src/engine/Function/Asset/VegetationChunk.h`
- Create: `project/src/engine/Function/Asset/VegetationChunk.cpp`
- Create: `project/src/tests/fixtures/vegetation/Phase2ManualSpecies.AshVegetation`
- Create: `project/src/tests/fixtures/vegetation/Phase2ManualSpeciesReplacement.AshVegetation`
- Create: `project/src/tests/Vegetation/vegetation_asset_format_tests.cpp`
- Modify: `project/src/tests/Vegetation/VegetationTestSupport.h`
- Modify: `docs/specs/modules/asset.md`
- Modify: `.gitattributes`

- [ ] **Step 1: Write strict-format RED cases**

Create the reviewed fixture directory before adding files:

```powershell
New-Item -ItemType Directory -Force project/src/tests/fixtures/vegetation | Out-Null
```

Before writing either canonical text fixture, add `*.AshVegetation text eol=lf`, `*.AshVegetationLayer -text`, and `*.AshVegetationChunk -text` to `.gitattributes`; fixture byte identity must not depend on a Windows checkout's `core.autocrlf` setting.

Use canonical in-memory fixtures and mutate one byte/field at a time. The first cases include:

```cpp
TEST_CASE("Vegetation Species rejects scalar coercion and writes one canonical byte stream")
{
    const std::vector<uint8_t> canonical = VegetationTest::CanonicalGrassSpeciesJson();
    AshEngine::VegetationSpecies species{};
    std::string error{};
    REQUIRE(AshEngine::decode_vegetation_species(
        canonical, VegetationTest::GenerousLoadBudget(), species, &error));

    std::vector<uint8_t> rewritten{};
    REQUIRE(AshEngine::encode_vegetation_species(species, rewritten, &error));
    CHECK(rewritten == canonical);

    const std::vector<uint8_t> array_scalar = VegetationTest::ReplaceJsonToken(
        canonical, "\"candidates_per_cell\":8", "\"candidates_per_cell\":[8]");
    CHECK_FALSE(AshEngine::decode_vegetation_species(
        array_scalar, VegetationTest::GenerousLoadBudget(), species, &error));
}

TEST_CASE("Vegetation Layer codec load budget rejects before publishing a partial object")
{
    const std::vector<uint8_t> bytes = VegetationTest::MinimalLayerBytes();
    AshEngine::VegetationLayerSnapshot layer{};
    std::string error{};
    AshEngine::VegetationLoadBudget budget = VegetationTest::GenerousLoadBudget();
    budget.max_decoded_bytes = 1023;
    CHECK_FALSE(AshEngine::decode_vegetation_layer(bytes, budget, layer, &error));
    CHECK(layer.tiles.empty());
    CHECK(layer.palette.empty());
}

TEST_CASE("Vegetation Chunk codec rejects zero instances and unreferenced species")
{
    std::vector<uint8_t> bytes = VegetationTest::MinimalChunkBytes();
    VegetationTest::WriteU32LE(bytes, 124, 0);
    VegetationTest::RepairChunkHeaderCrc(bytes);
    AshEngine::VegetationChunk chunk{};
    std::string error{};
    CHECK_FALSE(AshEngine::decode_vegetation_chunk(
        bytes, VegetationTest::GenerousLoadBudget(), chunk, &error));
    CHECK(chunk.instances.empty());
}
```

Add exact cases for:

- Species UTF-8 without BOM, duplicate/unknown keys, trailing bytes, native integer/boolean types, one-element arrays, and canonical key order/LF;
- the reviewed `Phase2ManualSpecies.AshVegetation` and `Phase2ManualSpeciesReplacement.AshVegetation` fixtures each decoding successfully and encoding to byte-identical canonical JSON; they carry the same embedded nonzero species ID but different canonical paths/content digests and valid render metadata, so the later human Replace gate never depends on test-helper-only byte generation;
- name `1..256` bytes; asset path `1..4096` bytes; mesh path non-empty; material count `1..64`; LOD count `1..16`; `screen_error_milli 1..1000000` strictly increasing; ordered int32 bounds with `min < max`; candidates `1..256`; scale `1..65535`; slope `0..1571`; exact eight slot values; deformation enum;
- nonzero lowercase 128-bit IDs and canonical asset-root-relative `/` paths without absolute, `.`, or `..` components;
- ASVL exact 80-byte header, `tile_resolution=32`, `tile_size_cm=3200`, palette `0..65534`, nonzero generation/ID, CRC coverage, strict EOF, sort/duplicate/reserved rejection;
- maximal RLE with 341 runs (1023 bytes) chooses RLE while 342 runs (1026 bytes) chooses Raw; reader rejects the opposite codec and non-maximal same-value run splits; also cover RLE sum 1024, decoded CRC, density-first, sorted weight planes, no all-zero plane or density-zero tile;
- ASVC exact 160-byte header and 28-byte records; standalone codec species count `1..65534`; instances `1..u32 max` within budget; every species referenced; every species index and cell valid; candidate ordinal must be within the schema-wide `0..255` bound. A nonempty Chunk requires header `min_height_mm <= max_height_mm`, every record height inside that range, and canonical writer extrema equal the exact min/max record values; CRC-repaired loose or false extrema are rejected. A fixture accepts 65534 species and rejects 65535; zero-instance chunk is rejected because absence is represented by deleting its manifest coordinate. Actual `candidate_ordinal < referenced_species.candidates_per_cell` is a cross-asset semantic check in Task 3 typed Chunk load; source-Layer palette subset/count remains Task 8 where the Layer is available;
- cell fraction/yaw full-u16 wire values, scale `1..65535`, normal oct `[-32767,32767]`, exact int32 height/extrema, total record ordering, tail/CRC/shape corruption; Task 8 owns random/float-to-record quantization helpers and their scale/normal/height golden vectors;
- caller budgets for file bytes, payload bytes, decoded bytes, palette, tiles, and instances, each proving the output object remains empty after rejection.

- [ ] **Step 2: Run RED**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation Species*"
RunTests.bat Debug --test-case="Vegetation Layer codec*"
RunTests.bat Debug --test-case="Vegetation Chunk codec*"
```

Expected RED: the three DTOs and encode/decode APIs do not exist.

- [ ] **Step 3: Implement strict sequential codecs**

Expose value-oriented APIs; immutable sharing is added by AssetDatabase in Task 3:

```cpp
ASH_API bool decode_vegetation_species(
    const std::vector<uint8_t>& bytes,
    const VegetationLoadBudget& budget,
    VegetationSpecies& out_species,
    std::string* out_error,
    VegetationLoadCost* out_cost = nullptr);
ASH_API bool encode_vegetation_species(
    const VegetationSpecies& species,
    std::vector<uint8_t>& out_bytes,
    std::string* out_error);

ASH_API bool decode_vegetation_layer(
    const std::vector<uint8_t>& bytes,
    const VegetationLoadBudget& budget,
    VegetationLayerSnapshot& out_layer,
    std::string* out_error,
    VegetationLoadCost* out_cost = nullptr);
ASH_API bool encode_vegetation_layer(
    const VegetationLayerSnapshot& layer,
    std::vector<uint8_t>& out_bytes,
    std::string* out_error);

ASH_API bool decode_vegetation_chunk(
    const std::vector<uint8_t>& bytes,
    const VegetationLoadBudget& budget,
    VegetationChunk& out_chunk,
    std::string* out_error,
    VegetationLoadCost* out_cost = nullptr);
ASH_API bool encode_vegetation_chunk(
    const VegetationChunk& chunk,
    std::vector<uint8_t>& out_bytes,
    std::string* out_error);
```

The public value DTOs use only wire-semantic fields: Species stores a 16-byte ID, UTF-8 name, ordered LODs (mesh path, material paths, `uint32_t screen_error_milli`), two `std::array<int32_t,3>` bounds, placement scalar/slot fields, native booleans, deformation enum and optional render paths. Layer stores ID/generation/seed, sorted `VegetationSpeciesReference { id, sha256, asset_path }`, and sorted tiles whose planes own expanded `std::array<uint8_t,1024>` values. Chunk stores layer/input/chunk/surface identity, exact height extrema, the same sorted Species references, and 28-byte-semantic instance fields. It does not expose encoded RLE buffers or packed headers.

Parse binary streams field-by-field; never read a packed struct. `VegetationAssetCodecInternal.h` is a non-exported three-codec implementation seam for checked add/multiply/narrowing, bounds-owning little-endian cursor/writer, strict JSON scalar/path validation, budget admission, and shared palette records; it must not expose DTO behavior or be included outside these codecs. Check every count, addition, multiplication, variable-size DTO/container allocation, and string length before that allocation. The already-owned immutable input snapshot and parser/token scratch are first bounded by admitted file/payload bytes and are not charged as wire-derived decoded ownership; no DTO reserve/copy may occur during that preflight. Decode into local temporaries and move into outputs only after header, payload, CRC, ordering, identity, shape, exact logical-cost computation and exact EOF validation all succeed. On success return the exact `VegetationLoadCost`; on failure leave DTO, cost, and encode output empty. Species JSON uses a duplicate-key-aware parse path and native JSON type inspection; it must not use permissive numeric/string conversion. Layer writer merges adjacent equal texels into maximal RLE runs and selects RLE only when strictly shorter than 1024 bytes; reader decodes, validates CRC, re-encodes, and requires exact codec/encoded-byte equality. Standalone Chunk rejects zero layer/input/surface identity, zero scale and oct `-32768`; revision zero remains legal.

- [ ] **Step 4: Run GREEN and corruption regression**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation Species*"
RunTests.bat Debug --test-case="Vegetation Layer codec*"
RunTests.bat Debug --test-case="Vegetation Chunk codec*"
RunTests.bat Release --test-case="Vegetation Species*"
RunTests.bat Release --test-case="Vegetation Layer codec*"
RunTests.bat Release --test-case="Vegetation Chunk codec*"
RunTests.bat Debug --test-case="Vegetation core*"
RunArchGate.bat
git diff --check -- .gitattributes docs/specs/modules/asset.md project/src/engine/Function/Asset/VegetationAssetCodecInternal.h project/src/engine/Function/Asset/VegetationSpecies.h project/src/engine/Function/Asset/VegetationSpecies.cpp project/src/engine/Function/Asset/VegetationLayer.h project/src/engine/Function/Asset/VegetationLayer.cpp project/src/engine/Function/Asset/VegetationChunk.h project/src/engine/Function/Asset/VegetationChunk.cpp project/src/tests/fixtures/vegetation/Phase2ManualSpecies.AshVegetation project/src/tests/fixtures/vegetation/Phase2ManualSpeciesReplacement.AshVegetation project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_asset_format_tests.cpp
```

- [ ] **Step 5: Review and selectively commit**

Review 1 compares every byte offset, CRC domain, range, canonical ordering, and shape against the locked SDD. Review 2 attempts scalar-array coercion, overflow, tail, duplicate, and tiny-budget bypasses and confirms no partial output.

```bat
git add -- .gitattributes docs/specs/modules/asset.md project/src/engine/Function/Asset/VegetationAssetCodecInternal.h project/src/engine/Function/Asset/VegetationSpecies.h project/src/engine/Function/Asset/VegetationSpecies.cpp project/src/engine/Function/Asset/VegetationLayer.h project/src/engine/Function/Asset/VegetationLayer.cpp project/src/engine/Function/Asset/VegetationChunk.h project/src/engine/Function/Asset/VegetationChunk.cpp project/src/tests/fixtures/vegetation/Phase2ManualSpecies.AshVegetation project/src/tests/fixtures/vegetation/Phase2ManualSpeciesReplacement.AshVegetation project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_asset_format_tests.cpp
git commit -m "feat(vegetation): add strict asset codecs"
```

## Task 3: Register three AssetDatabase types and immutable typed loads

**Files:**

- Modify: `project/src/engine/Base/hthreading.cpp`
- Modify: `project/src/engine/Function/Asset/AssetDatabase.h`
- Modify: `project/src/engine/Function/Asset/AssetDatabase.cpp`
- Modify: `project/src/editor/Services/AssetDatabaseService.h`
- Modify: `project/src/editor/Services/AssetDatabaseService.cpp`
- Modify: `project/src/editor/Panels/AssetBrowser/AssetBrowserSupport.h`
- Modify: `project/src/editor/Panels/AssetBrowser/AssetBrowserSupport.cpp`
- Modify: `project/src/editor/Panels/AssetBrowser/AssetBrowserToolbarView.cpp`
- Modify: `project/src/editor/Core/AssetPresentationUtils.h`
- Modify: `project/src/editor/Core/AssetPresentationUtils.cpp`
- Modify: `docs/specs/modules/base.md`
- Modify: `docs/specs/modules/asset.md`
- Modify: `docs/plans/2026-07-16-vegetation-authoring-and-bake.md`
- Create: `project/src/tests/Vegetation/vegetation_asset_database_tests.cpp`
- Create: `project/src/tests/Vegetation/vegetation_asset_presentation_tests.cpp`
- Modify: `project/src/tests/Vegetation/VegetationTestSupport.h`

- [ ] **Step 1: Write typed-load RED cases**

```cpp
TEST_CASE("Vegetation AssetDatabase detects case-insensitive types and deduplicates async loads")
{
    VegetationTest::ScopedAssetRoot root("asset-database");
    root.Write("flora/grass.ASHVEGETATION", VegetationTest::CanonicalGrassSpeciesJson());
    root.Write("flora/meadow.AshVegetationLayer", VegetationTest::MinimalLayerBytes());
    root.Write("flora/0_0.ashvegetationchunk", VegetationTest::MinimalChunkBytes());

    AshEngine::AssetDatabase database = AshEngine::AssetDatabase::create(root.Path());
    REQUIRE(database.is_valid());
    REQUIRE(database.refresh());
    CHECK(database.find_asset_by_path("flora/grass.ASHVEGETATION")->type == AshEngine::AssetType::Species);
    CHECK(database.find_asset_by_path("flora/meadow.AshVegetationLayer")->type == AshEngine::AssetType::Layer);
    CHECK(database.find_asset_by_path("flora/0_0.ashvegetationchunk")->type == AshEngine::AssetType::Chunk);

    const AshEngine::VegetationLoadBudget budget = VegetationTest::GenerousLoadBudget();
    auto first = database.load_vegetation_species_by_path_async("flora/grass.ASHVEGETATION", budget);
    auto second = database.load_vegetation_species_by_path_async("flora/grass.ASHVEGETATION", budget);
    const auto first_result = first.get();
    const auto second_result = second.get();
    REQUIRE(first_result.asset);
    REQUIRE(second_result.asset);
    CHECK(first_result.state == AshEngine::AssetLoadState::Loaded);
    CHECK(first_result.asset == second_result.asset);
}
```

Add sync/async by-id/by-path cases for all three types, wrong-type requests, missing/corrupt files, tiny caller budgets, a generous successful load followed by a tiny-budget request that must still fail, and tiny-first followed by generous success. Concurrent same-budget requests share one in-flight result; different budgets have request-isolated results and cannot poison each other. For Layer typed loads, resolve every palette Species path through the same AssetDatabase catalog revision and require embedded ID plus canonical file SHA-256 equality; path/ID/digest mismatch is admitted outer-asset `InvalidData`, returns no Layer, caches no partial asset and participates in the outer AssetId global precedence reducer. For Chunk typed loads, perform the same embedded Species path/ID/digest resolution and enforce `candidate_ordinal < resolved_species.candidates_per_cell`; mutate an ordinal from 7 to 8 against an 8-candidate species for the RED. Deterministic epoch tests call the production completion-publication reducer below with explicit old/new epoch and request tokens: an old completion after refresh may fulfill its private result but cannot erase the new in-flight token, refill cache, or change global state/error; same-epoch admitted failure and success in both orders produce the same final `Loaded` state. No test uses sleeps, large-file timing, or a test-only executor hook. Successful catalog replacement and invalid-root reset invalidate vegetation caches/in-flight maps; a scan failure without replacement preserves epoch, catalog, cache and state. Confirm Species is text-previewable, Layer/Chunk are binary-style, and none is scene-instantiable.

Budget admission is independently applied to the outer Layer/Chunk file and to every referenced Species file; dependency costs are not summed into the outer file cost. Cold and warm outcomes must be identical: a cached Layer/Chunk retains the exact outer cost plus immutable resolved Species assets and their exact costs, and every cache hit re-admits both the outer result and each dependency against the caller's six-field budget. Add Layer and Chunk cold/warm tests where the outer file fits but a referenced Species exceeds the caller budget. Successful decoded content is the only cacheable result; Missing/Io/InvalidData and request-local WrongType/BudgetExceeded are never negative-cached.

The async test owns a single-worker RAII executor and queues a promise-controlled blocker before issuing requests. A production pure in-flight admission reducer, called by Species/Layer/Chunk, returns JoinExisting only for exact type/AssetId/epoch/six-field-budget identity and otherwise LaunchNew; direct reducer tests plus static map-use review prove same-budget sharing and different-budget isolation. Result pointer equality is retained only as immutable cache-identity evidence, not as proof of in-flight sharing. Worker admission tests cover both sides of shutdown without sleeps: a command accepted before the shutdown lifecycle lock flips must drain after the blocker and complete normally; once `is_threading_shutting_down()` is true, a second command must be rejected immediately and never execute. The typed wrapper converts that rejection to a ready non-throwing Failed/Io result and cleans its matching in-flight token before releasing/joining the blocker. A separate started-single-worker idle handshake followed by one sole command must reach bounded future completion with no later enqueue/notify, mechanically guarding the condition-variable lost-wake edge. Catalog publication reducer cases distinguish matching Success→PublishReplacement, matching InvalidRoot→ResetInvalidRoot, matching Failed→KeepLastKnownGood, and every stale root/epoch outcome→DiscardStale; stale InvalidRoot must not clear a newer root. Initial-Unloaded outer decoded-budget and dependency-budget failures assert global state/error and database error remain byte-identical, and a tiny+generous blocker case proves the tiny failure neither leaves Loading nor rolls back the generous request. Every temporary asset root is unique to the current worktree/process/test instance and is cleaned only by its owner.

- [ ] **Step 2: Run RED**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation AssetDatabase*"
```

Expected RED: `AssetType::Species/Layer/Chunk` and typed load methods are missing.

- [ ] **Step 3: Add types, caches, and exact typed API**

Add enum values without renumbering existing values. Freeze a request-scoped result so budget failures have a stable diagnostic without racing the legacy AssetId-wide state:

```cpp
enum class VegetationAssetLoadFailure : uint8_t
{
    None,
    BudgetExceeded,
    WrongType,
    Missing,
    Io,
    InvalidData
};

template<typename T>
struct VegetationAssetLoadResult
{
    AssetLoadState state = AssetLoadState::Unloaded;
    VegetationAssetLoadFailure failure = VegetationAssetLoadFailure::None;
    std::shared_ptr<const T> asset{};
    VegetationLoadCost cost{};
    std::string error{};
};

struct VegetationAssetCompletionPublicationInput
{
    uint64_t captured_epoch = 0;
    uint64_t current_epoch = 0;
    uint64_t captured_request_token = 0;
    uint64_t current_in_flight_token = 0;
    AssetLoadState current_global_state = AssetLoadState::Unloaded;
    VegetationAssetLoadFailure current_global_failure = VegetationAssetLoadFailure::None;
    std::string current_global_error{};
    VegetationAssetLoadFailure completion_failure = VegetationAssetLoadFailure::None;
    std::string completion_error{};
    bool completion_has_asset = false;
};

struct VegetationAssetCompletionPublicationDecision
{
    bool erase_matching_in_flight = false;
    bool publish_cache = false;
    bool publish_global_state = false;
    AssetLoadState global_state = AssetLoadState::Unloaded;
    VegetationAssetLoadFailure global_failure = VegetationAssetLoadFailure::None;
    std::string global_error{};
};

struct VegetationAssetInFlightAdmissionInput
{
    bool has_existing = false;
    AssetType requested_type = AssetType::Unknown;
    AssetType existing_type = AssetType::Unknown;
    AssetId requested_id = 0;
    AssetId existing_id = 0;
    uint64_t requested_epoch = 0;
    uint64_t existing_epoch = 0;
    VegetationLoadBudget requested_budget{};
    VegetationLoadBudget existing_budget{};
};

enum class VegetationAssetInFlightAdmissionDecision : uint8_t
{
    LaunchNew,
    JoinExisting
};

ASH_API VegetationAssetInFlightAdmissionDecision
decide_vegetation_asset_in_flight_admission(
    const VegetationAssetInFlightAdmissionInput& input);

enum class VegetationCatalogScanOutcome : uint8_t
{
    Succeeded,
    InvalidRoot,
    Failed
};

struct VegetationCatalogPublicationInput
{
    uint64_t captured_epoch = 0;
    uint64_t current_epoch = 0;
    std::filesystem::path captured_root{};
    std::filesystem::path current_root{};
    VegetationCatalogScanOutcome scan_outcome = VegetationCatalogScanOutcome::Failed;
};

enum class VegetationCatalogPublicationDecision : uint8_t
{
    KeepLastKnownGood,
    PublishReplacement,
    ResetInvalidRoot,
    DiscardStale
};

ASH_API VegetationCatalogPublicationDecision decide_vegetation_catalog_publication(
    const VegetationCatalogPublicationInput& input);

namespace Detail
{
    // SDD-approved production seam used by every vegetation filesystem snapshot caller.
    ASH_API bool read_vegetation_bounded_stream_snapshot(
        std::istream& input,
        uint64_t max_file_bytes,
        std::vector<uint8_t>& out_bytes,
        std::string* out_error);
}

ASH_API VegetationAssetCompletionPublicationDecision
decide_vegetation_asset_completion_publication(
    const VegetationAssetCompletionPublicationInput& input);

class ASH_API VegetationAssetResolverSnapshot
{
public:
    VegetationAssetLoadResult<VegetationSpecies> load_species_by_path(
        const std::filesystem::path& path,
        const VegetationLoadBudget& budget) const;
};

VegetationAssetLoadResult<VegetationSpecies> load_vegetation_species_by_id(
    AssetId id,
    const VegetationLoadBudget& budget);
VegetationAssetLoadResult<VegetationSpecies> load_vegetation_species_by_path(
    const std::filesystem::path& path,
    const VegetationLoadBudget& budget);
std::shared_future<VegetationAssetLoadResult<VegetationSpecies>> load_vegetation_species_by_id_async(
    AssetId id,
    VegetationLoadBudget budget);
std::shared_future<VegetationAssetLoadResult<VegetationSpecies>> load_vegetation_species_by_path_async(
    const std::filesystem::path& path,
    VegetationLoadBudget budget);
std::shared_ptr<const VegetationAssetResolverSnapshot> capture_vegetation_resolver_snapshot() const;
```

Mirror the four signatures for `VegetationLayerSnapshot` and `VegetationChunk`. `VegetationAssetResolverSnapshot` is an immutable, worker-safe catalog/root/path view with no back-pointer to mutable AssetDatabase state; typed Layer, typed Chunk, and Task 8 active-store validation are its three production consumers. Its Species load reads one byte snapshot, runs the strict codec and canonical SHA, and returns only a value/shared immutable asset. Typed Layer and Chunk success is published only after all referenced Species resolve against the same resolver snapshot and pass path/ID/canonical-digest checks. Refresh invalidates shared cross-asset cache/publication/in-flight membership, while an old request still completes its private future from its captured immutable snapshot and is barred from shared publication. A successful immutable cache stores the exact wire-derived `VegetationLoadCost`; every later caller must pass its own budget against that cost before receiving the cached pointer. Same-budget requests may share one in-flight future. Different-budget requests do not share an in-flight future; `VegetationAssetLoadFailure::BudgetExceeded` is request-local, is not cached as a content failure, and never changes `get_asset_load_state`, `get_asset_last_error`, or database-level `get_last_error()`.

`AssetDatabase::Impl` owns a mutex-protected `uint64_t vegetation_catalog_epoch` and monotonic request token. Every successful catalog replacement by `refresh()` and every reset increments the epoch before clearing vegetation caches/in-flight entries. A typed request captures the current epoch, token, and one immutable resolver snapshot. Completion always fulfills its request-local promise, but all Species/Layer/Chunk completion paths must call the exported pure `decide_vegetation_asset_completion_publication` reducer before touching shared state; these are three real production call sites, not a test-only abstraction. The reducer permits cache/global publication only when epochs match, permits erase only when both epoch and in-flight token match, and otherwise returns all side-effect flags false. Production applies the decision while holding the same epoch/state mutex and rechecks the decision inputs under that lock. Within one epoch global outcome precedence is fixed and completion-order independent: `Loaded` dominates all failures; otherwise admitted content failures rank `InvalidData > Io > Missing`, and equal-ranked failures retain the lexicographically smallest normalized diagnostic. Vegetation typed request admission and in-flight registration never write global `Loading`; progress ownership is private to the exact in-flight key/token. Until the outer asset and every dependency have passed all six budget fields and cross-asset validation, global state/error and database `get_last_error()` remain byte-identical. Only terminal completion is reduced into Loaded/InvalidData/Io/Missing. Therefore `WrongType` and `BudgetExceeded` are fully request-local even from initial Unloaded and during a different-budget generous request, with no Loading residue or rollback. A later success upgrades Failed to Loaded and clears the global error; no failure can downgrade Loaded. Add tiny-first/generous, concurrent tiny+generous, both reducer completion orders, explicit old/new epoch/token tests, an initial-Unloaded decoded/dependency budget failure test, and an `InvalidData -> retry admitted -> Io/Missing` test that asserts the InvalidData aggregate persists during and after the lower-ranked completion while success still upgrades to Loaded. Assert request-local failures leave typed state/error and database `get_last_error()` byte-identical. Extend `Impl` caches/in-flight maps and clear them only under the same epoch/state mutex. Review proves every typed async completion uses the reducer and no parallel write path exists. Extend all exhaustive Editor presentation switches; do not add the types to `IsAssetTypeSceneInstantiable`.

`set_root_path()` changes root, increments the vegetation epoch and clears the vegetation catalog/cache/in-flight/global state atomically under that same mutex; no request may observe old `AssetInfo` with a new root. `refresh()` begins by capturing `(scan_root, captured_epoch)` in that mutex, scans only the copied root outside the lock, then reacquires the mutex and calls the exported pure catalog-publication reducer against current root/epoch plus `VegetationCatalogScanOutcome::{Succeeded,InvalidRoot,Failed}` before any swap. Any root/epoch mismatch returns DiscardStale for all outcomes. On an exact match, Succeeded→PublishReplacement and bumps epoch even when catalog bytes are unchanged; InvalidRoot→ResetInvalidRoot and atomically bumps epoch/clears catalog, vegetation cache/in-flight/global state; Failed→KeepLastKnownGood without bumping or discarding. The reducer is an SDD-approved single-call mechanical race seam; static review proves refresh has no catalog/reset bypass. Tests include stale InvalidRoot after a new root and require zero clearing. By-id and by-path typed requests capture epoch, token, copied `AssetInfo`, root and immutable catalog index in one critical section. The resolver value-copies these data and never holds an `Impl`, mutex, catalog pointer or database back-pointer. Within one resolver snapshot, Species path resolutions are memoized so Layer/Chunk validation cannot mix disk generations. Missing means path absent; open/read failures are Io. Species reference digests are computed from strict decode followed by canonical re-encode, never from noncanonical source text bytes.

Generic raw loads cannot be a parallel vegetation state writer. Add `load_text_by_id_bounded/load_text_by_path_bounded(..., uint64_t max_file_bytes, std::string&)`: it first performs a non-writing catalog lookup that copies root+`AssetInfo` together and never calls the legacy resolver that writes Missing/last_error. The filesystem wrapper opens exactly one binary stream/handle, performs no trusted pre-open `file_size` admission and never reopens, then uses an SDD-approved production bounded-stream helper to read fixed 64 KiB chunks into local bytes. Every append is checked; after exactly max bytes it probes one extra byte on the same handle, so exact EOF succeeds, max+1 or in-place growth fails, and zero admits only an empty file. A final partial chunk accompanied by EOF is normal success; a short read without EOF, badbit or another I/O error fails. Output is moved only after successful EOF. Atomic path replacement cannot change the already-open handle. The same wrapper/helper serves generic preview and typed Species/Layer/Chunk resolver reads, giving multiple real production consumers rather than a test-only abstraction. Direct helper tests feed controlled exact/final-partial+EOF/short-without-EOF/max+1/growing/error streams; public tests cover below/exact/zero, and static review locks one-open-to-EOF. The API never touches asset/global load state or errors. `AssetDatabaseService::LoadTextById` uses this bounded API for every Editor text preview with an Editor-owned named constant `kAssetTextPreviewMaxFileBytes = 1 MiB`; this is solely a raw preview/read cap and is unrelated to Task 9 typed resident/world budgets. Species is allowed through this bounded preview path. Existing unbounded `load_text_by_id/path` and all generic `load_binary_by_id/path` reject Species/Layer/Chunk request-locally with no typed state/error or database `get_last_error()` change; Layer/Chunk compact tooltips use catalog metadata and do not read payload bytes. No generic path calls legacy `set_load_loading/success/failed_locked` for the three vegetation types. Existing non-vegetation generic behavior remains unchanged. Add concurrent typed+bounded-preview assertions proving preview has zero shared side effects, plus static review of every generic entry point.

Task 3 closes the Base worker enqueue/shutdown and condition-variable lost-wake races rather than assuming them away. `hthreading.cpp` reuses the existing `worker_condition_mutex` as the sole condition/lifecycle-admission mutex: the worker wait predicate reads stop/queue while holding it; enqueue holds it across `shutting_down`/initialized inspection and queue push, then unlocks before notify; shutdown holds it while atomically flipping `shutting_down` plus `worker_stop_requested`, then unlocks before notify/join. Lock order is always condition mutex before `CommandQueue`'s internal mutex, never the reverse; the outer worker `try_pop` only takes the queue mutex. Therefore producer cannot notify between a false predicate and atomic wait, and a command is either admitted-before-stop and drained or rejected-after-stop; push-after-worker-exit is impossible. Immediate/no-worker execution never holds this mutex while running user code. Add a started-one-worker idle→sole-command bounded-completion case with no subsequent notification, plus the blocker shutdown cases. Typed async submission uses `Detail::enqueue_worker_command` through one AssetDatabase-local wrapper rather than ignoring `dispatch_background_task`'s command completion. It retains the request result promise and inspects the returned `ThreadCommandFuture`; immediate rejection is synchronously reduced as Failed/Io, fulfills the result promise, and erases only the matching epoch/token. The wrapper catches enqueue invocation exceptions, ready command-future exceptions and all task exceptions, and converts each to the same non-throwing result shape exactly once. Thus no typed future can become `broken_promise` or remain Loading. Update the durable Base threading spec in the same commit.

Every result shape is exact: Loaded has `failure=None`, a non-null asset, exact outer cost and empty error; Missing has `state=Missing`, `failure=Missing`, null asset, zero cost and a normalized non-empty error; BudgetExceeded/WrongType/Io/InvalidData have `state=Failed`, the matching failure, null asset, zero cost and a normalized non-empty error. Futures never throw and never complete in Loading. Normalized diagnostics are trimmed stable messages with path separators canonicalized to `/`; equal-rank reduction compares those normalized bytes. Append the three enum values without renumbering existing values. The Asset Browser filter array grows from 11 to 14 in the header, support implementation and toolbar consumer; Species is text-previewable, Layer/Chunk use compact binary tooltips, and all three remain non-instantiable. Update the durable Asset module spec in this task; the consolidated Editor authoring/usage contract remains Task 12's Editor-spec responsibility.

- [ ] **Step 4: Run GREEN and existing asset regression**

Run all focused/full CPU tests and both Debug builds first. Then obtain a fresh exclusive GPU window from every active worktree for the single `run.bat all Debug` readiness matrix. Immediately before that command, snapshot `product/config/Engine.ini`, `product/config/editor/EditorSettings.json`, `product/config/editor/ViewportLayout.json`, and `product/config/editor/imgui.ini` byte-for-byte and record their SHA-256 values. Do not run validation, RenderGate, PerfGate, or bless/import anything. Require Editor/Sandbox × Vulkan/DX12 exit zero, Sandbox clean exit, and every fresh Engine/Application log to have zero generic error/critical, validation/debug-layer, device-lost, access-violation, fatal, assertion, and bad-leak findings. In a finally-style cleanup, restore all four files to the exact pre-run bytes, verify their SHA-256 values, verify effective Editor/Sandbox/AshImageDiff/gate roots are zero, and explicitly release the GPU window even on failure.

```bat
RunTests.bat Debug --test-case="Vegetation AssetDatabase*"
RunTests.bat Release --test-case="Vegetation AssetDatabase*"
RunTests.bat Debug --test-case="Vegetation editor*"
RunTests.bat Release --test-case="Vegetation editor*"
RunTests.bat Debug --test-case="Vegetation * codec*"
RunTests.bat Debug
RunTests.bat Release
build_editor.bat Debug
build_sandbox.bat Debug
run.bat all Debug --smoke-test-seconds=120
RunArchGate.bat
git diff --check -- project/src/engine/Base/hthreading.cpp project/src/engine/Function/Asset/AssetDatabase.h project/src/engine/Function/Asset/AssetDatabase.cpp project/src/editor/Services/AssetDatabaseService.h project/src/editor/Services/AssetDatabaseService.cpp project/src/editor/Panels/AssetBrowser/AssetBrowserSupport.h project/src/editor/Panels/AssetBrowser/AssetBrowserSupport.cpp project/src/editor/Panels/AssetBrowser/AssetBrowserToolbarView.cpp project/src/editor/Core/AssetPresentationUtils.h project/src/editor/Core/AssetPresentationUtils.cpp docs/specs/modules/base.md docs/specs/modules/asset.md docs/plans/2026-07-16-vegetation-authoring-and-bake.md project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_asset_database_tests.cpp project/src/tests/Vegetation/vegetation_asset_presentation_tests.cpp
```

- [ ] **Step 5: Review and selectively commit**

Review 1 checks load-cost admission, same-budget in-flight identity, request-local budget failure, global state precedence, and typed Chunk species/candidate validation. Review 2 checks every exhaustive `AssetType` switch, case-insensitive extension detection, immutable sharing, and refresh invalidation.

```bat
git add -- project/src/engine/Base/hthreading.cpp project/src/engine/Function/Asset/AssetDatabase.h project/src/engine/Function/Asset/AssetDatabase.cpp project/src/editor/Services/AssetDatabaseService.h project/src/editor/Services/AssetDatabaseService.cpp project/src/editor/Panels/AssetBrowser/AssetBrowserSupport.h project/src/editor/Panels/AssetBrowser/AssetBrowserSupport.cpp project/src/editor/Panels/AssetBrowser/AssetBrowserToolbarView.cpp project/src/editor/Core/AssetPresentationUtils.h project/src/editor/Core/AssetPresentationUtils.cpp docs/specs/modules/base.md docs/specs/modules/asset.md docs/plans/2026-07-16-vegetation-authoring-and-bake.md project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_asset_database_tests.cpp project/src/tests/Vegetation/vegetation_asset_presentation_tests.cpp
git commit -m "feat(asset): load vegetation asset types"
```

## Task 4: Add Scene v7 VegetationComponent and Editor snapshot preservation

**Files:**

- Modify: `project/src/engine/Function/Scene/SceneComponents.h`
- Modify: `project/src/engine/Function/Scene/Scene.h`
- Modify: `project/src/engine/Function/Scene/Scene.cpp`
- Modify: `project/src/editor/Core/SceneComponentSerialization.cpp`
- Modify: `project/src/editor/Core/SceneSnapshotComponentUtils.cpp`
- Modify: `project/src/editor/Panels/Inspector/InspectorComponentMetadata.cpp`
- Modify: `project/src/tests/Scene/particle_component_tests.cpp`
- Modify: `project/src/tests/premake5.lua`
- Create: `project/src/tests/Scene/vegetation_component_tests.cpp`

- [ ] **Step 1: Write Scene v7 RED cases**

```cpp
TEST_CASE("VegetationComponent is typed, versioned, extracted, and never default-added")
{
    AshEngine::Scene scene = AshEngine::Scene::create("Vegetation Scene");
    AshEngine::Entity surface = scene.create_entity("Surface");
    AshEngine::Entity vegetation = scene.create_entity("Vegetation");
    REQUIRE(surface.is_valid());
    REQUIRE(vegetation.is_valid());

    CHECK_FALSE(AshEngine::can_add_scene_component(
        vegetation, AshEngine::SceneComponentType::Vegetation));
    AshEngine::VegetationComponent component{};
    component.layer_asset_path = "vegetation/meadow.AshVegetationLayer";
    component.surface_entity_id = surface.get_id();
    component.enabled = true;
    REQUIRE(vegetation.add_vegetation_component(component));
    CHECK(scene.get_vegetation_version() != 0);

    const auto extracted = scene.extract_vegetation_entities();
    REQUIRE(extracted.size() == 1);
    CHECK(extracted[0].entity_id == vegetation.get_id());
    CHECK(extracted[0].vegetation.surface_entity_id == surface.get_id());
}

TEST_CASE("VegetationComponent rejects self binding and legacy v6 defaults absent")
{
    AshEngine::Scene scene = AshEngine::Scene::create("Invalid Vegetation");
    AshEngine::Entity entity = scene.create_entity("Vegetation");
    AshEngine::VegetationComponent component{};
    component.layer_asset_path = "vegetation/meadow.AshVegetationLayer";
    component.surface_entity_id = entity.get_id();
    CHECK_FALSE(entity.add_vegetation_component(component));

    const AshEngine::Scene legacy = VegetationTest::LoadSceneJsonWithVersion(6);
    CHECK(legacy.is_valid());
    CHECK(legacy.extract_vegetation_entities().empty());
}
```

Add descriptor assertions for `UInt64` and `VegetationLayer`, typed add/set/remove, independent vegetation version only, v7 round-trip, invalid/absolute/dot-segment/non-layer paths, zero/missing/self surface IDs, v3-v6 default absence even when a vegetation-shaped unknown field is injected, generic add false, generic remove true, duplicate/copy/paste snapshot preservation, and component read/write serialization without uint64 truncation. Update only the current writer-version assertion in `particle_component_tests.cpp` from 6 to 7; preserve `vegetation_baseline_scene_tests.cpp` as explicit legacy-v6 evidence.

- [ ] **Step 2: Wire the two real Editor snapshot sources and run RED**

Add both direct implementation units together; `SceneSnapshotComponentUtils.cpp` calls `SceneComponentSerialization.cpp` and is not linkable alone:

```lua
"%{wks.location}/project/src/editor/Core/SceneComponentSerialization.cpp",
"%{wks.location}/project/src/editor/Core/SceneSnapshotComponentUtils.cpp",
```

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="VegetationComponent*"
```

Expected RED: missing component enum/type/facade/extraction/version and missing `UInt64` serialization.

- [ ] **Step 3: Implement the minimum Scene contract**

Add this component and extraction API without touching render presentation:

```cpp
struct VegetationComponent
{
    std::string layer_asset_path{};
    uint64_t surface_entity_id = 0;
    bool enabled = true;
};

struct ASH_API SceneVegetationExtractionDesc
{
    EntityId entity_id = 0;
    VegetationComponent vegetation{};
};

bool Entity::has_vegetation_component() const;
VegetationComponent Entity::get_vegetation_component() const;
bool Entity::add_vegetation_component(const VegetationComponent& component);
bool Entity::set_vegetation_component(const VegetationComponent& component);
bool Entity::remove_vegetation_component();
std::vector<SceneVegetationExtractionDesc> Scene::extract_vegetation_entities() const;
uint64_t Scene::get_vegetation_version() const;
```

Append `SceneComponentType::Vegetation`, `ScenePropertyType::UInt64`, and `ScenePropertyAssetRefKind::VegetationLayer`. Bump writer schema to 7, load Vegetation only for schema 7, and validate all references before mutating storage. Add independent `vegetation_version`; do not add a `RenderScene`, `VisibleRenderFrame`, `ScenePresentationSubsystem`, or `SceneRenderer` path.

- [ ] **Step 4: Run GREEN and legacy regression**

```bat
RunTests.bat Debug --test-case="VegetationComponent*"
RunTests.bat Release --test-case="VegetationComponent*"
RunTests.bat Debug --test-case="ParticleComponent*"
RunTests.bat Debug --test-case="Vegetation baseline*"
build_editor.bat Debug
RunArchGate.bat
git diff --check -- project/src/engine/Function/Scene/SceneComponents.h project/src/engine/Function/Scene/Scene.h project/src/engine/Function/Scene/Scene.cpp project/src/editor/Core/SceneComponentSerialization.cpp project/src/editor/Core/SceneSnapshotComponentUtils.cpp project/src/editor/Panels/Inspector/InspectorComponentMetadata.cpp project/src/tests/Scene/particle_component_tests.cpp project/src/tests/Scene/vegetation_component_tests.cpp project/src/tests/premake5.lua
```

- [ ] **Step 5: Review and selectively commit**

Review 1 checks strict v7 serialization, v3-v6 compatibility, self-reference/path rejection, and independent versioning. Review 2 checks exhaustive component switches, snapshot duplication, uint64 handling, and proves there is no Function/Render diff.

```bat
git add -- project/src/engine/Function/Scene/SceneComponents.h project/src/engine/Function/Scene/Scene.h project/src/engine/Function/Scene/Scene.cpp project/src/editor/Core/SceneComponentSerialization.cpp project/src/editor/Core/SceneSnapshotComponentUtils.cpp project/src/editor/Panels/Inspector/InspectorComponentMetadata.cpp project/src/tests/Scene/particle_component_tests.cpp project/src/tests/Scene/vegetation_component_tests.cpp project/src/tests/premake5.lua
git commit -m "feat(scene): add vegetation layer bindings"
```

## Task 5: Add sparse working state, canonical strokes, and atomic patches

**Files:**

- Modify: `project/src/engine/Function/Asset/VegetationLayer.h`
- Modify: `project/src/engine/Function/Asset/VegetationLayer.cpp`
- Create: `project/src/engine/Function/Asset/VegetationBrush.h`
- Create: `project/src/engine/Function/Asset/VegetationBrush.cpp`
- Create: `project/src/tests/Vegetation/vegetation_brush_tests.cpp`
- Modify: `project/src/tests/Vegetation/VegetationTestSupport.h`

- [ ] **Step 1: Write deterministic brush RED cases**

```cpp
TEST_CASE("Vegetation brush canonical stroke ignores collinear event density across ten billion millimeters")
{
    std::vector<AshEngine::VegetationWorldMillimeterPoint> one_billion_steps{};
    std::vector<AshEngine::VegetationWorldMillimeterPoint> half_billion_steps{};
    for (int64_t index = 0; index <= 10; ++index)
    {
        one_billion_steps.push_back({ index * 1000000000LL, 0 });
    }
    for (int64_t index = 0; index <= 20; ++index)
    {
        half_billion_steps.push_back({ index * 500000000LL, 0 });
    }

    const auto first = AshEngine::canonicalize_vegetation_stroke(one_billion_steps);
    const auto second = AshEngine::canonicalize_vegetation_stroke(half_billion_steps);
    REQUIRE(first.succeeded);
    REQUIRE(second.succeeded);
    CHECK(first.safe_segments == second.safe_segments);
    CHECK(first.safe_segments.size() == 10);
    for (const auto& segment : first.safe_segments)
    {
        CHECK(segment.end.x - segment.begin.x == 1000000000LL);
        CHECK(segment.end.z == segment.begin.z);
    }
}

TEST_CASE("Vegetation brush falloff and resampling match v1 golden values")
{
    CHECK(AshEngine::vegetation_brush_amount(0, 1000, 128, 255) == 128);
    CHECK(AshEngine::vegetation_brush_amount(500, 1000, 128, 255) == 64);
    CHECK(AshEngine::vegetation_brush_amount(1000, 1000, 128, 255) == 0);

    const std::vector<AshEngine::VegetationWorldMillimeterPoint> path{ { 0, 0 }, { 2000, 0 } };
    const auto resampled = AshEngine::resample_vegetation_stroke(path, 500);
    REQUIRE(resampled.succeeded);
    CHECK(resampled.dabs == std::vector<AshEngine::VegetationWorldMillimeterPoint>{
        { 0, 0 }, { 500, 0 }, { 1000, 0 }, { 1500, 0 }, { 2000, 0 }
    });
}

TEST_CASE("Vegetation patch validates all source bytes before changing the working set")
{
    AshEngine::VegetationLayerWorkingSet working(VegetationTest::MinimalLayerSnapshot());
    const auto before = working.publish_snapshot();
    AshEngine::VegetationLayerPatch patch = VegetationTest::SingleTexelPaintPatch(working);
    patch.entries.back().before_bytes[0] ^= 0xffu;
    CHECK(AshEngine::apply_vegetation_layer_patch(
        working, patch, working.content_generation()) ==
        AshEngine::VegetationPatchApplyStatus::SourceMismatch);
    CHECK(working.publish_snapshot()->content_generation == before->content_generation);
    CHECK(working.publish_snapshot()->tiles == before->tiles);
}
```

Add cases for:

- chunk/local to signed world-mm conversion and int64 overflow;
- radius `250..1024000`, strength `1..255`, falloff `0..255`, spacing `1..2048000`, raw delta per axis `<=1e9`;
- GCD primitive direction without cross-product overflow, checked step count, canonical splitting, and identical dabs for collinear event insertion/removal;
- duplicate raw points, diagonal segments, endpoint append, carried spacing remainder, rational ties-to-even interpolation, and square sums bounded by `2e18`;
- Paint requiring a palette species, saturating density plus selected weight, global Erase subtracting density and every weight plane;
- palette Add rejecting duplicate path/ID, Replace requiring the same embedded species ID while changing path/digest, Remove rejecting nonzero weight planes unless `clear_weights=true`, and confirmed Remove atomically deleting those planes; each successful edit returns one reversible patch, advances generation once, and works without a surface provider;
- locked/read-only, no selected species, empty/no-op stroke, and invalid coordinate paths leaving generation and patch output unchanged; provider availability is an Editor-service precondition tested in Task 9, not a dependency of this pure Asset mutation API;
- patch order `(tile_z,tile_x,plane_kind,species_id)`, compressed before/after bytes, caller-supplied expected-current-generation plus direction-specific shape/species/bytes preflight, apply/revert each incrementing generation exactly once, zero weight-plane/tile removal, and canonical authoring payload equality across repeated apply/revert cycles. `VegetationTest::CanonicalAuthoringPayloadBytes(snapshot)` serializes only the ASVL palette+tiles payload; it deliberately excludes the monotonic `content_generation` and all header/CRC bytes derived from that generation.
- working-set bake dirty evidence accumulating touched density coords and per-species before/after nonzero-weight coords across apply, revert, Undo, Redo and Save; snapshotting evidence is non-destructive, an acknowledgement with the exact captured generation clears only after a matching successful bake commit, while stale/failed acknowledgement preserves it.

- [ ] **Step 2: Run RED**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation brush*"
RunTests.bat Debug --test-case="Vegetation patch*"
```

Expected RED: working set, brush, canonicalization, resampling, and patch APIs are missing.

- [ ] **Step 3: Implement integer-only mutation APIs**

Expose this minimum shape:

```cpp
struct VegetationWorldMillimeterPoint
{
    int64_t x = 0;
    int64_t z = 0;

    friend constexpr bool operator==(
        const VegetationWorldMillimeterPoint& lhs,
        const VegetationWorldMillimeterPoint& rhs) noexcept
    {
        return lhs.x == rhs.x && lhs.z == rhs.z;
    }
    friend constexpr bool operator!=(
        const VegetationWorldMillimeterPoint& lhs,
        const VegetationWorldMillimeterPoint& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

struct VegetationSafeStrokeSegment
{
    VegetationWorldMillimeterPoint begin{};
    VegetationWorldMillimeterPoint end{};

    friend constexpr bool operator==(
        const VegetationSafeStrokeSegment& lhs,
        const VegetationSafeStrokeSegment& rhs) noexcept
    {
        return lhs.begin == rhs.begin && lhs.end == rhs.end;
    }
    friend constexpr bool operator!=(
        const VegetationSafeStrokeSegment& lhs,
        const VegetationSafeStrokeSegment& rhs) noexcept
    {
        return !(lhs == rhs);
    }
};

struct VegetationStrokeCanonicalizationResult
{
    bool succeeded = false;
    std::vector<VegetationSafeStrokeSegment> safe_segments{};
    std::string error{};
};

struct VegetationStrokeResampleResult
{
    bool succeeded = false;
    std::vector<VegetationWorldMillimeterPoint> dabs{};
    std::string error{};
};

enum class VegetationBrushMode : uint8_t { Paint, Erase };

struct VegetationBrushStroke
{
    VegetationBrushMode mode = VegetationBrushMode::Paint;
    VegetationId selected_species{};
    uint32_t radius_mm = 250;
    uint8_t strength = 1;
    uint8_t falloff = 0;
    uint32_t spacing_mm = 1;
    uint64_t stroke_seed = 0;
    std::vector<VegetationSurfaceSampleRequest> path{};
};

enum class VegetationPaletteEditMode : uint8_t { Add, Replace, Remove };

struct VegetationPaletteEdit
{
    VegetationPaletteEditMode mode = VegetationPaletteEditMode::Add;
    VegetationId target_species_id{};
    VegetationPaletteRecord replacement{};
    bool clear_weights = false;
};

class ASH_API VegetationLayerWorkingSet
{
public:
    explicit VegetationLayerWorkingSet(std::shared_ptr<const VegetationLayerSnapshot> snapshot);
    std::shared_ptr<const VegetationLayerSnapshot> publish_snapshot() const;
    VegetationAuthoringDirtyEvidence snapshot_bake_dirty_evidence() const;
    bool acknowledge_bake_dirty_evidence(uint64_t captured_generation);
    uint64_t content_generation() const;
};

ASH_API bool vegetation_surface_request_to_world_millimeter(
    const VegetationSurfaceSampleRequest& request,
    VegetationWorldMillimeterPoint& out_point);
ASH_API VegetationStrokeCanonicalizationResult canonicalize_vegetation_stroke(
    const std::vector<VegetationWorldMillimeterPoint>& raw_points);
ASH_API VegetationStrokeResampleResult resample_vegetation_stroke(
    const std::vector<VegetationWorldMillimeterPoint>& raw_points,
    uint32_t spacing_mm);
ASH_API uint8_t vegetation_brush_amount(
    uint64_t distance_mm,
    uint32_t radius_mm,
    uint8_t strength,
    uint8_t falloff);
ASH_API VegetationBrushApplyResult apply_vegetation_brush_stroke(
    VegetationLayerWorkingSet& working_set,
    const VegetationBrushStroke& stroke);
ASH_API VegetationPaletteApplyResult apply_vegetation_palette_edit(
    VegetationLayerWorkingSet& working_set,
    const VegetationPaletteEdit& edit);
ASH_API VegetationPatchApplyStatus apply_vegetation_layer_patch(
    VegetationLayerWorkingSet& working_set,
    const VegetationLayerPatch& patch,
    uint64_t expected_current_generation);
ASH_API VegetationPatchApplyStatus revert_vegetation_layer_patch(
    VegetationLayerWorkingSet& working_set,
    const VegetationLayerPatch& patch,
    uint64_t expected_current_generation);
```

`VegetationLayerPatch` is direction-neutral: it carries sorted tile before/after entries plus an optional complete palette before/after value, but no immutable “only valid at creation generation” gate. Each apply/revert call supplies `expected_current_generation`; the working set first requires exact equality, then validates every direction-specific source byte/shape/species entry, and only then commits atomically. Success advances generation once and returns the new generation through the brush/palette apply result; failure leaves generation/output unchanged. This lets one command reuse the same immutable patch while still rejecting intervening edits. Every successful apply/revert atomically merges the affected density coords and palette before/after nonzero-weight coords into working-set bake dirty evidence before publishing the new generation. Snapshotting that evidence never clears it; only an exact-generation acknowledgement after active-pointer commit may clear it, so Undo/Redo, Save and failed/stale bake cannot lose the pre-edit side of a Remove+clear. Use only the SDD integer formulas. Build the complete patch in temporary storage, validate every source entry, then commit the whole patch. Do not add real-time incremental preview semantics; End Stroke applies one canonical result.

- [ ] **Step 4: Run GREEN and codec round-trip**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation brush*"
RunTests.bat Debug --test-case="Vegetation patch*"
RunTests.bat Release --test-case="Vegetation brush*"
RunTests.bat Release --test-case="Vegetation patch*"
RunTests.bat Debug --test-case="Vegetation Layer codec*"
RunArchGate.bat
git diff --check -- project/src/engine/Function/Asset/VegetationLayer.h project/src/engine/Function/Asset/VegetationLayer.cpp project/src/engine/Function/Asset/VegetationBrush.h project/src/engine/Function/Asset/VegetationBrush.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_brush_tests.cpp
```

- [ ] **Step 5: Review and selectively commit**

Review 1 mechanically recomputes the long-stroke split, resampling, falloff, saturation, ordering, and generation rules. Review 2 probes overflow/no-op/atomicity and confirms no world-size or instance-count hard cap was introduced.

```bat
git add -- project/src/engine/Function/Asset/VegetationLayer.h project/src/engine/Function/Asset/VegetationLayer.cpp project/src/engine/Function/Asset/VegetationBrush.h project/src/engine/Function/Asset/VegetationBrush.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_brush_tests.cpp
git commit -m "feat(vegetation): add deterministic brush patches"
```

## Task 6: Add already-executed commands and document-scoped history removal

**Files:**

- Modify: `project/src/editor/Core/EditorCommand.h`
- Modify: `project/src/editor/Core/EditorCommand.cpp`
- Modify: `project/src/editor/Core/IEditorCommandExecutor.h`
- Create: `project/src/editor/Core/VegetationCommands.h`
- Create: `project/src/editor/Core/VegetationCommands.cpp`
- Modify: `project/src/editor/Services/UndoRedoService.h`
- Modify: `project/src/editor/Services/UndoRedoService.cpp`
- Modify: `project/src/editor/App/EditorApplicationImpl.h`
- Modify: `project/src/editor/App/EditorApplicationImpl.cpp`
- Modify: `project/src/tests/premake5.lua`
- Create: `project/src/tests/Editor/vegetation_undo_redo_tests.cpp`

- [ ] **Step 1: Write history RED cases**

The test file defines this complete command helper:

```cpp
class CountingDocumentCommand final : public AshEditor::EditorCommand
{
public:
    CountingDocumentCommand(
        int& value,
        AshEditor::EditorCommandDocumentKey key,
        bool undo_succeeds = true)
        : _value(value), _key(std::move(key)), _undo_succeeds(undo_succeeds) {}

    const char* GetLabel() const override { return "Counting document command"; }
    bool Execute(AshEditor::EditorContext&) override { ++_value; return true; }
    bool Undo(AshEditor::EditorContext&) override
    {
        if (!_undo_succeeds) return false;
        --_value;
        return true;
    }
    std::optional<AshEditor::EditorCommandDocumentKey> GetDocumentKey() const override { return _key; }

private:
    int& _value;
    AshEditor::EditorCommandDocumentKey _key{};
    bool _undo_succeeds = true;
};

TEST_CASE("Vegetation already-executed command records without executing twice")
{
    AshEditor::UndoRedoService history{};
    AshEditor::EditorContext context{};
    int value = 1;
    const AshEditor::EditorCommandDocumentKey key{ "vegetation-layer", "vegetation/meadow.ashvegetationlayer" };
    const auto result = history.RecordExecutedCommand(
        std::make_unique<CountingDocumentCommand>(value, key), context);
    CHECK(result == AshEditor::EditorCommandRecordResult::Recorded);
    CHECK(value == 1);
    REQUIRE(history.Undo(context));
    CHECK(value == 0);
}

TEST_CASE("Vegetation already-executed command rejection rolls back synchronously")
{
    AshEditor::UndoRedoService history{};
    AshEditor::EditorContext context{};
    REQUIRE(history.BeginTransaction("Existing transaction"));
    int value = 1;
    const AshEditor::EditorCommandDocumentKey key{ "vegetation-layer", "vegetation/meadow.ashvegetationlayer" };
    CHECK(history.RecordExecutedCommand(
        std::make_unique<CountingDocumentCommand>(value, key), context) ==
        AshEditor::EditorCommandRecordResult::RolledBack);
    CHECK(value == 0);
}
```

Add cases for null commands, rollback failure returning `RollbackFailed`, redo clearing, selection application, no merge across saved checkpoint, homogeneous/mixed Composite keys, removal from undo and redo, removal of only one document, current/saved/next state-ID remapping, dirty-state preservation, and `VegetationStrokeCommand` apply/revert without copying a whole layer. The Vegetation command test starts from an already-applied patch, runs `Undo -> Redo -> Undo -> Redo`, proves generations increase monotonically on all four successful directions while `CanonicalAuthoringPayloadBytes` alternates exactly between before/after payloads, and checks the command's expected-generation cursor updates after each success. A second command applies an intervening working-set mutation and proves stale Undo/Redo returns false without moving history or changing payload/generation.

- [ ] **Step 2: Run RED**

Keep the two snapshot/serialization sources added by Task 4 and add these additional Editor sources to `project/src/tests/premake5.lua`:

```lua
"%{wks.location}/project/src/editor/Core/EditorCommand.cpp",
"%{wks.location}/project/src/editor/Core/EditorEventBus.cpp",
"%{wks.location}/project/src/editor/Core/VegetationCommands.cpp",
"%{wks.location}/project/src/editor/Services/SceneService.cpp",
"%{wks.location}/project/src/editor/Services/SelectionService.cpp",
"%{wks.location}/project/src/editor/Services/UndoRedoService.cpp",
```

Then run:

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation *command*"
RunTests.bat Debug --test-case="Vegetation *history*"
```

Expected RED: missing document key, tri-state record API, selective removal, and Vegetation command.

- [ ] **Step 3: Implement the generic history contract**

```cpp
struct EditorCommandDocumentKey
{
    std::string strDomain{};
    std::string strIdentity{};
    bool operator==(const EditorCommandDocumentKey& other) const;
};

enum class EditorCommandRecordResult : uint8_t
{
    Recorded,
    RolledBack,
    RollbackFailed
};

// EditorCommand.h, inside class EditorCommand:
virtual std::optional<EditorCommandDocumentKey> GetDocumentKey() const;

// EditorCommand.cpp:
std::optional<EditorCommandDocumentKey> EditorCommand::GetDocumentKey() const
{
    return std::nullopt;
}

EditorCommandRecordResult UndoRedoService::RecordExecutedCommand(
    std::unique_ptr<EditorCommand> command,
    EditorContext& context);
size_t UndoRedoService::RemoveCommandsForDocument(
    const EditorCommandDocumentKey& key);
```

Add matching `IEditorCommandExecutor` methods and forward them through `EditorApplicationImpl`. `RecordExecutedCommand` never calls Execute, rejects an open transaction, and attempts Undo before returning either rollback state. `RemoveCommandsForDocument` removes matching undo/redo entries and remaps state IDs without changing surviving semantic order. `CompositeCommand::GetDocumentKey` returns a key only when every child has the same key. `VegetationStrokeCommand` owns one immutable `VegetationLayerPatch`, a weak working-set target, and a mutable `expected_current_generation` initialized from the generation produced by the already-applied edit. Execute applies for redo and Undo reverts using that cursor; after success it replaces the cursor with `working_set.content_generation()`, while failure leaves the cursor and history entry unchanged. Thus repeated cycles work and any intervening mutation fails closed.

- [ ] **Step 4: Run GREEN and Editor command regression**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation *command*"
RunTests.bat Debug --test-case="Vegetation *history*"
RunTests.bat Release --test-case="Vegetation *history*"
RunTests.bat Release --test-case="Vegetation *command*"
RunTests.bat Debug --test-case="*Gizmo*"
build_editor.bat Debug
RunArchGate.bat
git diff --check -- project/src/editor/Core/EditorCommand.h project/src/editor/Core/EditorCommand.cpp project/src/editor/Core/IEditorCommandExecutor.h project/src/editor/Core/VegetationCommands.h project/src/editor/Core/VegetationCommands.cpp project/src/editor/Services/UndoRedoService.h project/src/editor/Services/UndoRedoService.cpp project/src/editor/App/EditorApplicationImpl.h project/src/editor/App/EditorApplicationImpl.cpp project/src/tests/premake5.lua project/src/tests/Editor/vegetation_undo_redo_tests.cpp
```

- [ ] **Step 5: Review and selectively commit**

Review 1 checks exactly-once execution, synchronous rollback, composite key semantics, and state-ID remapping. Review 2 checks old commands default to no key, selection behavior remains stable, and test-premake additions are minimal.

```bat
git add -- project/src/editor/Core/EditorCommand.h project/src/editor/Core/EditorCommand.cpp project/src/editor/Core/IEditorCommandExecutor.h project/src/editor/Core/VegetationCommands.h project/src/editor/Core/VegetationCommands.cpp project/src/editor/Services/UndoRedoService.h project/src/editor/Services/UndoRedoService.cpp project/src/editor/App/EditorApplicationImpl.h project/src/editor/App/EditorApplicationImpl.cpp project/src/tests/premake5.lua project/src/tests/Editor/vegetation_undo_redo_tests.cpp
git commit -m "feat(editor): add document-scoped command history"
```

## Task 7: Add checked Layer stage, commit, copy, and reload storage

**Files:**

- Create: `project/src/engine/Function/Asset/VegetationFileOps.h`
- Create: `project/src/engine/Function/Asset/VegetationFileOps.cpp`
- Create: `project/src/engine/Function/Asset/VegetationStorage.h`
- Create: `project/src/engine/Function/Asset/VegetationStorage.cpp`
- Create: `project/src/tests/Vegetation/vegetation_storage_tests.cpp`
- Modify: `project/src/tests/Vegetation/VegetationTestSupport.h`

- [ ] **Step 1: Write transactional storage RED cases**

```cpp
TEST_CASE("Vegetation storage checked save never replaces an externally changed Layer")
{
    VegetationTest::ScopedAssetRoot root("storage-source-changed");
    const std::filesystem::path target_relative = "vegetation/meadow.AshVegetationLayer";
    const std::filesystem::path target_absolute = root.Path() / target_relative;
    root.Write(target_relative, VegetationTest::MinimalLayerBytes());
    const AshEngine::VegetationLoadBudget budget = VegetationTest::GenerousLoadBudget();
    const AshEngine::VegetationLayerReadResult opened =
        AshEngine::read_vegetation_layer_snapshot(root.Path(), target_relative, budget);
    REQUIRE(opened.status == AshEngine::VegetationStorageStatus::Succeeded);
    AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};

    const AshEngine::VegetationPreparedLayerWrite prepared =
        AshEngine::prepare_vegetation_layer_write(
            root.Path(), target_relative, opened.revision, *opened.snapshot, 7,
            VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
    REQUIRE(prepared.status == AshEngine::VegetationStorageStatus::Prepared);
    const std::vector<uint8_t> external = VegetationTest::DifferentValidLayerBytes();
    VegetationTest::WriteAllBytes(target_absolute, external);

    const AshEngine::VegetationStorageResult committed =
        AshEngine::commit_vegetation_layer_write(
            prepared, 7, cleanup_registry);
    CHECK(committed.status == AshEngine::VegetationStorageStatus::SourceChanged);
    CHECK(VegetationTest::ReadAllBytes(target_absolute) == external);
    CHECK_FALSE(std::filesystem::exists(prepared.stage_path));
}

TEST_CASE("Vegetation storage Save Copy As is create-new and never rebinds the source")
{
    VegetationTest::ScopedAssetRoot root("storage-copy");
    const std::filesystem::path source_relative = "vegetation/source.AshVegetationLayer";
    const std::filesystem::path source_absolute = root.Path() / source_relative;
    const std::filesystem::path destination_relative = "vegetation/copy.AshVegetationLayer";
    const std::filesystem::path destination_absolute = root.Path() / destination_relative;
    root.Write(source_relative, VegetationTest::MinimalLayerBytes());
    AshEngine::VegetationOwnedStageCleanupRegistry cleanup_registry{};
    const auto prepared = AshEngine::prepare_vegetation_layer_copy_as(
        root.Path(), destination_relative, *VegetationTest::MinimalLayerSnapshot(), 11,
        VegetationTest::ActiveControl(std::chrono::seconds(1)), cleanup_registry);
    REQUIRE(prepared.status == AshEngine::VegetationStorageStatus::Prepared);
    VegetationTest::WriteAllBytes(destination_absolute, VegetationTest::DifferentValidLayerBytes());
    const auto result = AshEngine::commit_vegetation_layer_copy_as(
        prepared, 11, cleanup_registry);
    CHECK(result.status == AshEngine::VegetationStorageStatus::AlreadyExists);
    CHECK(VegetationTest::ReadAllBytes(destination_absolute) == VegetationTest::DifferentValidLayerBytes());
    CHECK(std::filesystem::exists(source_absolute));
}
```

Add same-byte revision, same-buffer SHA/parse, lowercase canonical-path mutex identity, asset-root and extension checks, absolute/dot-segment/reparse rejection, idempotent checked parent creation, unique same-directory sibling stage file, strict readback, flush, source revision recheck, serial mismatch, create-new copy, cooperative two-writer race, stale completion, cancel before/after every <=1 MiB write block, directory/stage/readback/flush/replace failure injection, and shutdown cleanup. Add a FileOps legal-shape table test: valid absent `InspectPath` is `Succeeded` with canonical relative/absolute/identity populated and `exists=false,is_regular_file=false`; an existing file is `Succeeded/true/true`; an existing directory is `Succeeded/true/false`; invalid input is `InvalidPath` with every resolved identity/path cleared and both booleans false; absent `ReadAllBytes` is `NotFound` with empty bytes. A first-save test creates a previously absent nested parent through `EnsureDirectoryTree`; an nth-component failure leaves the target absent, while two cooperative creators converge on the same safe parent but receive distinct sibling stage files. `ScriptedVegetationFileOps` fails a selected `(VegetationFileOpKind, occurrence)` and records every path/offset/byte span, so each directory component, sibling-stage create, write-block, readback, flush, lease, replace, create-new, and owned-stage-file cleanup path is an independently reproducible RED. If `RemoveOwnedStageFile` fails, the operation remains Failed and retains only that exact owned stage file in its cleanup registry; shutdown retries it, never broadens deletion to the parent, and reports failure if it still cannot remove it. A RED fails the first cleanup, succeeds on shutdown retry, and proves no unrelated path is touched. Tests do not claim protection against a same-permission process that bypasses this API between revision check and replace.

- [ ] **Step 2: Run RED**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation storage*"
```

Expected RED: file revision, prepared write, named lease, checked commit, and copy APIs are missing.

- [ ] **Step 3: Implement split worker/main-thread storage operations**

```cpp
struct VegetationFileRevision
{
    uint64_t file_size = 0;
    VegetationSha256 sha256{};
};

enum class VegetationFileOpKind : uint8_t
{
    InspectPath,
    ReadAllBytes,
    EnsureDirectoryTree,
    CreateUniqueSiblingStageFile,
    CreateUniqueStageTree,
    WriteBlock,
    Flush,
    PublishImmutableFromStage,
    AcquireNamedLease,
    AtomicReplace,
    CreateNewFromStage,
    RemoveOwnedStageFile,
    RemoveOwnedStageTree
};

enum class VegetationCreateNewStatus : uint8_t
{
    Created,
    AlreadyExists,
    Failed
};

enum class VegetationFileResultStatus : uint8_t
{
    Succeeded,
    NotFound,
    InvalidPath,
    Failed
};

struct VegetationFileInspection
{
    VegetationFileResultStatus status = VegetationFileResultStatus::Failed;
    std::filesystem::path canonical_relative_path{};
    std::filesystem::path resolved_absolute_path{};
    std::string canonical_identity{};
    bool exists = false;
    bool is_regular_file = false;
    std::string error{};
};

struct VegetationFileBytesResult
{
    VegetationFileResultStatus status = VegetationFileResultStatus::Failed;
    std::vector<uint8_t> bytes{};
    std::string error{};
};

struct VegetationStageFileResult
{
    VegetationFileResultStatus status = VegetationFileResultStatus::Failed;
    std::filesystem::path owned_stage_file{};
    std::string error{};
};

struct VegetationStageTreeResult
{
    VegetationFileResultStatus status = VegetationFileResultStatus::Failed;
    std::filesystem::path owned_stage_root{};
    std::string error{};
};

// Legal result shapes are part of the interface contract. A valid absent
// InspectPath target is Succeeded with canonical paths/identity populated and
// exists=false/is_regular_file=false. InvalidPath/Failed clear those fields.
// ReadAllBytes uses NotFound only for an absent target and returns no bytes for
// every non-Succeeded status.

class ASH_API IVegetationFileLease
{
public:
    virtual ~IVegetationFileLease() = default;
};

struct VegetationByteSpan
{
    const uint8_t* data = nullptr;
    size_t size = 0;
};

class ASH_API IVegetationStageFileOps
{
public:
    virtual ~IVegetationStageFileOps() = default;
    virtual VegetationFileInspection InspectPath(
        const std::filesystem::path& asset_root,
        const std::filesystem::path& path) = 0;
    virtual VegetationFileBytesResult ReadAllBytes(
        const std::filesystem::path& path) = 0;
    virtual VegetationFileResultStatus EnsureDirectoryTree(
        const std::filesystem::path& asset_root,
        const std::filesystem::path& relative_directory) = 0;
    virtual VegetationStageFileResult CreateUniqueSiblingStageFile(
        const std::filesystem::path& target,
        uint64_t operation_serial) = 0;
    virtual VegetationStageTreeResult CreateUniqueStageTree(
        const std::filesystem::path& store_root,
        uint64_t operation_serial) = 0;
    virtual bool WriteBlock(const std::filesystem::path& stage,
                            uint64_t offset,
                            VegetationByteSpan bytes) = 0;
    virtual bool Flush(const std::filesystem::path& path) = 0;
    virtual bool RemoveOwnedStageFile(const std::filesystem::path& stage_file) = 0;
    virtual bool RemoveOwnedStageTree(const std::filesystem::path& stage_root) = 0;
};

class ASH_API IVegetationImmutablePublishFileOps :
    public virtual IVegetationStageFileOps
{
public:
    virtual VegetationCreateNewStatus PublishImmutableFromStage(
        const std::filesystem::path& stage,
        const std::filesystem::path& content_addressed_target) = 0;
};

class ASH_API IVegetationCommitFileOps :
    public virtual IVegetationStageFileOps
{
public:
    virtual std::unique_ptr<IVegetationFileLease> AcquireNamedLease(
        std::string_view canonical_identity) = 0;
    virtual bool AtomicReplace(const std::filesystem::path& stage,
                               const std::filesystem::path& target) = 0;
    virtual VegetationCreateNewStatus CreateNewFromStage(
        const std::filesystem::path& stage,
        const std::filesystem::path& target) = 0;
};

class ASH_API IVegetationFileOps :
    public IVegetationImmutablePublishFileOps,
    public IVegetationCommitFileOps
{
public:
    ~IVegetationFileOps() override = default;
};

ASH_API IVegetationFileOps& get_default_vegetation_file_ops();

struct VegetationOwnedStageCleanupStatus
{
    bool all_removed = true;
    std::vector<std::filesystem::path> retained_stage_files{};
    std::vector<std::filesystem::path> retained_stage_trees{};
};

class ASH_API VegetationOwnedStageCleanupRegistry
{
public:
    void TrackStageFile(std::filesystem::path owned_stage_file);
    void TrackStageTree(std::filesystem::path owned_stage_root);
    VegetationOwnedStageCleanupStatus RetryAll(IVegetationStageFileOps& file_ops);
    bool empty() const noexcept;
};

enum class VegetationStorageStatus : uint8_t
{
    Succeeded,
    Prepared,
    SourceChanged,
    AlreadyExists,
    InvalidPath,
    Cancelled,
    Failed
};

ASH_API VegetationLayerReadResult read_vegetation_layer_snapshot(
    const std::filesystem::path& asset_root,
    const std::filesystem::path& layer_path,
    const VegetationLoadBudget& budget,
    IVegetationStageFileOps& file_ops = get_default_vegetation_file_ops());
ASH_API VegetationPreparedLayerWrite prepare_vegetation_layer_write(
    const std::filesystem::path& asset_root,
    const std::filesystem::path& target,
    std::optional<VegetationFileRevision> expected_revision,
    const VegetationLayerSnapshot& snapshot,
    uint64_t operation_serial,
    VegetationOperationControl control,
    VegetationOwnedStageCleanupRegistry& cleanup_registry,
    IVegetationStageFileOps& file_ops = get_default_vegetation_file_ops());
ASH_API VegetationStorageResult commit_vegetation_layer_write(
    const VegetationPreparedLayerWrite& prepared,
    uint64_t current_operation_serial,
    VegetationOwnedStageCleanupRegistry& cleanup_registry,
    IVegetationCommitFileOps& file_ops = get_default_vegetation_file_ops());
ASH_API VegetationPreparedLayerWrite prepare_vegetation_layer_copy_as(
    const std::filesystem::path& asset_root,
    const std::filesystem::path& destination,
    const VegetationLayerSnapshot& snapshot,
    uint64_t operation_serial,
    VegetationOperationControl control,
    VegetationOwnedStageCleanupRegistry& cleanup_registry,
    IVegetationStageFileOps& file_ops = get_default_vegetation_file_ops());
ASH_API VegetationStorageResult commit_vegetation_layer_copy_as(
    const VegetationPreparedLayerWrite& prepared,
    uint64_t current_operation_serial,
    VegetationOwnedStageCleanupRegistry& cleanup_registry,
    IVegetationCommitFileOps& file_ops = get_default_vegetation_file_ops());
```

The split FileOps interfaces are a real production boundary shared by Layer storage and Chunk-set publication, not test-only free functions. A Layer worker receives only `IVegetationStageFileOps`, so target create/replace is mechanically unavailable. A Chunk publisher receives `IVegetationImmutablePublishFileOps`, which adds only collision-safe create-new for hash-named immutable objects/manifests. Main-thread commits alone receive `IVegetationCommitFileOps`, which owns the named lease, target create-new and active-pointer atomic replace. The production and scripted implementations satisfy the combined `IVegetationFileOps`; service tasks pass only the least-capability base-interface view required. Tri-state create-new distinguishes `Created`, atomic `AlreadyExists`, and I/O `Failed`; callers never collapse a collision into success.

`EnsureDirectoryTree` is the only persistent directory-creation primitive. It accepts an asset root plus canonical relative directory, rejects absolute/dot-segment/reparse escapes, creates missing components one at a time, revalidates every component, and is idempotently `Succeeded` when a cooperative creator already made the same real directory. Every component attempt is a distinct scripted fault occurrence. Layer storage uses `CreateUniqueSiblingStageFile`: after the target parent is ensured, it atomically creates one collision-resistant operation-owned file directly in `target.parent_path()`, satisfying same-directory replace semantics. `RemoveOwnedStageFile` accepts only that exact registered file and never removes its parent. Chunk publication separately uses `CreateUniqueStageTree`: after the store root is ensured, it creates one unique operation-owned child directory that may contain object/manifest/pointer candidate files; `RemoveOwnedStageTree` accepts only that exact registered root. Two cooperative creators must receive distinct sibling files/trees. Empty persistent asset/store/objects/manifests directories created before a later failure may remain.

`VegetationFileInspection` uses the commented legal shapes exactly. `Succeeded/true/true` is an existing regular file and `Succeeded/true/false` is an existing non-regular path. `InvalidPath` and `Failed` clear `canonical_relative_path`, `resolved_absolute_path`, `canonical_identity`, `exists`, and `is_regular_file`. `VegetationFileBytesResult::Succeeded` means one immutable byte snapshot was read; `NotFound`, `InvalidPath`, and `Failed` always carry empty bytes. Production and scripted implementations share these invariants.

`VegetationOwnedStageCleanupRegistry` is a production value owner, not a global. Layer storage and Chunk publication register the exact file/tree immediately after successful creation and remove it only after a confirmed cleanup. `RetryAll` attempts each retained exact path through the matching FileOps removal method, erases only successes, and returns the remaining paths without broadening ownership. Task 7 tests call `RetryAll` directly to prove first-failure/second-success and repeated-failure reporting; Task 9 `VegetationEditorService` owns one registry and calls `RetryAll` during every operation completion and again during `Shutdown`, exposing a failed cleanup in its status instead of reporting clean.

Worker Layer preparation first calls `EnsureDirectoryTree` for the validated target parent, then may create, flush, reread, parse, and hash only its unique same-directory sibling stage file. `VegetationPreparedLayerWrite` captures the `std::optional<VegetationFileRevision>` passed to prepare; existing same-byte revision is distinct from the explicit absent identity (`nullopt`) used by a newly created unsaved Layer. Commit has no second caller-supplied revision that could rebase this optimistic check: it acquires the canonical-path named mutex, reads one target byte snapshot when the captured revision is present, derives revision from those same bytes, verifies serial and the prepared captured revision, then atomically replaces. With captured `nullopt` it uses atomic create-new; a target created after the unsaved session began is preserved and returns `AlreadyExists`. Copy commit acquires the same lease and always uses create-new/non-replacing publication. All failures preserve target bytes and delete only the exact operation-owned stage file; a failed file deletion is tracked for bounded shutdown retry rather than falsely reported clean.

- [ ] **Step 4: Run GREEN and codec regression**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation storage*"
RunTests.bat Release --test-case="Vegetation storage*"
RunTests.bat Debug --test-case="Vegetation Layer codec*"
RunArchGate.bat
git diff --check -- project/src/engine/Function/Asset/VegetationFileOps.h project/src/engine/Function/Asset/VegetationFileOps.cpp project/src/engine/Function/Asset/VegetationStorage.h project/src/engine/Function/Asset/VegetationStorage.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_storage_tests.cpp
```

- [ ] **Step 5: Review and selectively commit**

Review 1 follows every prepare/commit/cancel/fault path and verifies target preservation and same-byte identity. Review 2 checks root/reparse/mutex semantics, stage ownership, and the documented non-cooperative-writer limitation.

```bat
git add -- project/src/engine/Function/Asset/VegetationFileOps.h project/src/engine/Function/Asset/VegetationFileOps.cpp project/src/engine/Function/Asset/VegetationStorage.h project/src/engine/Function/Asset/VegetationStorage.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_storage_tests.cpp
git commit -m "feat(vegetation): add checked layer storage"
```

## Task 8: Add deterministic bake, ASVI input identity, and content-addressed Chunk publication

**Files:**

- Create: `project/src/engine/Function/Asset/VegetationBaker.h`
- Create: `project/src/engine/Function/Asset/VegetationBaker.cpp`
- Create: `project/src/engine/Function/Asset/VegetationChunkSet.h`
- Create: `project/src/engine/Function/Asset/VegetationChunkSet.cpp`
- Modify: `project/src/engine/Function/Asset/VegetationChunk.h`
- Modify: `project/src/engine/Function/Asset/VegetationChunk.cpp`
- Modify: `project/src/engine/Function/Asset/VegetationStorage.h`
- Modify: `project/src/engine/Function/Asset/VegetationStorage.cpp`
- Create: `project/src/tests/Vegetation/vegetation_baker_tests.cpp`
- Modify: `project/src/tests/Vegetation/VegetationTestSupport.h`

- [ ] **Step 1: Write deterministic bake and transaction RED cases**

```cpp
TEST_CASE("Vegetation baker counter hash and R8 accept limits match v1 vectors")
{
    const auto zero = AshEngine::make_vegetation_counter_hash(VegetationTest::ZeroCounterKey(), 1);
    CHECK(zero.state == 0x936cd7179cecc6f6ULL);
    CHECK(zero.random[0] == 0xb69aaf248fe5723eULL);
    CHECK(zero.random[1] == 0xc9d8c945898ec42bULL);
    CHECK(zero.random[2] == 0x8818d088186f267bULL);
    CHECK(zero.random[3] == 0xfaeb1d600eaa91b7ULL);
    CHECK(zero.random[4] == 0xf432147eb52618d8ULL);
    CHECK(AshEngine::vegetation_candidate_accept_limit(0) == 0u);
    CHECK(AshEngine::vegetation_candidate_accept_limit(1) == 257u);
    CHECK(AshEngine::vegetation_candidate_accept_limit(254) == 65279u);
    CHECK(AshEngine::vegetation_candidate_accept_limit(255) == 65536u);
}

TEST_CASE("Vegetation baker ASVI all-absent preimage has exact bytes and digest")
{
    const AshEngine::VegetationChunkInputIdentity input = VegetationTest::AllAbsentAsviGoldenInput();
    std::vector<uint8_t> preimage{};
    const AshEngine::VegetationSha256 digest =
        AshEngine::build_vegetation_chunk_input_digest(input, &preimage);
    CHECK(preimage.size() == 624);
    CHECK(VegetationTest::ToHex(digest) ==
        "8d7e1c07f44858323ffddb12b27daad8ded267169bdf22c7397f366a7cd7d9c3");
}

TEST_CASE("Vegetation baker seed-only mutation invalidates every existing chunk object")
{
    const auto first = VegetationTest::BakeSingleChunk(0x1234u);
    const auto second = VegetationTest::BakeSingleChunk(0x1235u);
    REQUIRE(first.status == AshEngine::VegetationBakeStatus::Succeeded);
    REQUIRE(second.status == AshEngine::VegetationBakeStatus::Succeeded);
    CHECK(first.chunks[0].input_digest != second.chunks[0].input_digest);
    CHECK(first.chunks[0].object_sha256 != second.chunks[0].object_sha256);
    CHECK(second.full_rebake_required);
}
```

Add the second counter-hash vector, including stream 4 `0x6ec202003e5df319`; require `VegetationCounterHashResult::random` to contain exactly five streams so scale never reads an implicit or regenerated value. At record level, scale range Q12 `[3277,4915]` must produce `4839` for the zero key and `3986` for the second key, proving the baker consumes stream 4. Add threshold rejection at random high16 equal to limit and acceptance one below; full-value guaranteed acceptance; jitter/yaw/scale/normal/height quantization; Ready/Outside/Pending/Failed batches; material/slope filters; unique total sort under quantized collisions; dirty-order and palette-order independence; exact 64 ASVI slot order/presence/record bytes; used-species union and sorting; ASVM empty/single-entry goldens; ASVA CRC; Chunk/header/manifest/recomputed digest equality; published Chunk species table being a sorted source-Layer palette subset with count `1..min(source_palette_count,65534)`; zero-instance coordinate deletion; input-digest reuse of untouched objects; palette/species dirty expansion; seed/surface revision full expansion; operation-identity stale rejection; and object/manifest/pointer failure at each numbered stage with restart seeing only a complete old or complete new generation. The baker must call Task 1 `evaluate_vegetation_surface_normal` for every Ready sample and use its normalized normal plus milliradian slope for both filtering and record quantization; it must not duplicate or approximate that policy locally.

Add a palette Remove+clear RED where the old authoring snapshot has a nonzero S weight, the retained manifest coordinate contains another accepted species but does not reference S, and the after snapshot no longer contains S; dirty evidence must still include that coordinate from patch-before. The full-dirty RED must construct one coordinate present only in the current manifest and another present only in authoring data through a nonzero density tile, then prove the dirty universe is their union. Palette/species dirty is exactly `manifest references ∪ patch-before nonzero weight coords ∪ after-snapshot nonzero weight coords`; an external Species digest change uses the current snapshot for both before/after. Add a two-dirty-chunk RED where the first chunk succeeds and a later batch is Pending/Failed/cancelled: the complete operation fails, no prepared manifest/pointer is committable, and active/LKG remain unchanged (unreferenced immutable objects may remain for later GC). A first-store RED starts with no chunk-store directory and creates store/objects/manifests/staging through `EnsureDirectoryTree`; every nth directory-component/stage failure stops before pointer switch, and two cooperative creators may leave safe empty persistent directories but never share or remove each other's owned stage root. First-pointer commit has three REDs: `NoActive` create-new succeeds, a cooperative creator wins after prepare so `AlreadyExists` returns source-changed/stale while preserving its exact bytes, and create-new I/O failure leaves no active pointer. All numbered publisher failures use `ScriptedVegetationFileOps`, including the nth directory component, object write/flush/create-new, manifest write/flush/create-new, active-pointer stage/write/flush/create-new-or-replace, and cleanup calls.

Add negative-coordinate dirty mapping goldens: tile x `-1,-8,-9` maps to chunk x `-1,-1,-2` (and the same rule independently for z), using checked floor division rather than C++ truncation. Add a restart RED where a newer saved Layer generation is opened against an older active ASVM and no in-memory patch-before evidence exists: the safe fallback is full dirty `all_manifest_coords ∪ current authoring nonzero-density coords`, including one authoring-only coord, while the old active remains LKG until a successful commit. Matching generations may use the exact localized evidence path.

Add active-store read REDs for no `active.asva`, corrupt pointer CRC, missing/corrupt manifest, missing/corrupt referenced object, full ASVC codec failures (payload/header CRC, exact EOF, ordering, instance/species/height-extrema invariants), embedded Species path/ID/digest mismatch, `candidate_ordinal == candidates_per_cell`, budget failure without partial snapshot, and a cooperative writer switching active between prepare and commit. Summary-budget REDs use the exact logical formula below and test `required-1` rejection, exact-limit success, zero budget, checked multiplication/addition overflow, and multiple entries with repeated referenced Species IDs (each retained per-entry ID is charged). A scripted multi-object reader cancels after object N and proves cancellation is checked before/after ASVA, ASVM, every ASVC read/decode/resolver boundary, returns promptly with no partial snapshot, and leaves no publication or stage. The old active Chunk need not be a subset of the current after-Layer during palette removal, but every embedded Species must resolve through the captured catalog snapshot. The writer race must be detected by a lease-protected on-disk reread and return stale/source-changed without pointer replacement.

- [ ] **Step 2: Run RED**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation baker*"
RunTests.bat Debug --test-case="Vegetation chunk set*"
```

Expected RED: counter hash, accept limit, ASVI, baker, ASVM/ASVA, and content-addressed publisher APIs are missing.

- [ ] **Step 3: Implement pure bake and checked pointer publication**

```cpp
struct VegetationCounterHashResult
{
    uint64_t state = 0;
    std::array<uint64_t, 5> random{};
};

struct VegetationChunkSetLoadBudget
{
    VegetationLoadBudget per_file{};
    uint32_t max_manifest_entries = 0;
    uint64_t max_total_inspected_bytes = 0;
    uint64_t max_summary_bytes = 0;
};

ASH_API VegetationCounterHashResult make_vegetation_counter_hash(
    const VegetationCounterHashKey& key,
    uint32_t cooker_version);
ASH_API uint32_t vegetation_candidate_accept_limit(uint8_t effective_threshold);
ASH_API VegetationSha256 build_vegetation_chunk_input_digest(
    const VegetationChunkInputIdentity& input,
    std::vector<uint8_t>* out_preimage);
ASH_API VegetationBakeResult bake_vegetation_chunks(
    const VegetationBakeInput& input,
    VegetationOperationControl control);
ASH_API VegetationActiveChunkSetReadResult read_active_vegetation_chunk_set(
    const std::filesystem::path& asset_root,
    const std::filesystem::path& layer_path,
    const VegetationAssetResolverSnapshot& resolver,
    const VegetationChunkSetLoadBudget& budget,
    VegetationOperationControl control,
    IVegetationStageFileOps& file_ops = get_default_vegetation_file_ops());
ASH_API VegetationPreparedChunkSet prepare_vegetation_chunk_set(
    const std::filesystem::path& asset_root,
    const std::filesystem::path& layer_path,
    const VegetationBakeResult& bake,
    VegetationOperationControl control,
    VegetationOwnedStageCleanupRegistry& cleanup_registry,
    IVegetationImmutablePublishFileOps& file_ops = get_default_vegetation_file_ops());
ASH_API VegetationChunkSetCommitResult commit_vegetation_chunk_set(
    const VegetationPreparedChunkSet& prepared,
    const VegetationChunkSetExpectedIdentity& current_operation_identity,
    VegetationOwnedStageCleanupRegistry& cleanup_registry,
    IVegetationCommitFileOps& file_ops = get_default_vegetation_file_ops());
```

Bake consumes immutable layer/species/surface/resolver snapshots plus an immutable active chunk-set snapshot only. `read_active_vegetation_chunk_set` returns a clean `NoActive` state for an absent store; otherwise it strictly validates the exact ASVA byte snapshot/CRC, manifest SHA and ASVM shape, runs the complete strict ASVC codec for every referenced object (all CRC/EOF/order/record/extrema invariants), checks size/SHA/coord/layer/surface/input identity, and resolves every embedded Species path/ID/digest plus candidate ordinal through the immutable resolver before publishing a snapshot. It observes `VegetationOperationControl` before and after ASVA/ASVM, every referenced ASVC read and strict decode, and every potentially expensive resolver boundary; cancellation/deadline returns a non-success result with no partial snapshot. Each immutable entry summary contains the referenced Species IDs required to reconstruct `manifest_coords_referencing_species` after restart.

`max_summary_bytes` uses a canonical logical charge, never `sizeof`, vector capacity, allocator overhead, or implementation padding: `112` bytes for the snapshot header (`state/reserved:8 + layer_id:16 + layer_generation:8 + surface_id:16 + three revisions:24 + manifest_sha256:32 + entry_count:8`), plus `88` bytes per retained entry (`chunk_coord:16 + object_sha256:32 + input_sha256:32 + referenced_species_count:8`), plus `16` bytes for every retained referenced Species ID in every entry. Thus `required = 112 + entry_count*88 + total_entry_species_references*16`, with every multiply/add checked before allocation; `NoActive` still requires 112, zero is a zero budget, and duplicate IDs across different entries are charged independently because they remain independently stored. The reader enforces `per_file` independently for ASVA/ASVM/each ASVC and checked cumulative `max_manifest_entries`, `max_total_inspected_bytes`, and this exact `max_summary_bytes` across the whole read; it may stream object validation but never publishes a partial snapshot or resets the aggregate budget per object. `VegetationBakeInput` carries that snapshot, current after-Layer, and palette-patch before dirty evidence so untouched entries/input digests can be reused without consulting mutable files.

Use fixed integer traversal, SplitMix64 words and explicit streams `0..4`, the 16-bit R8 acceptance limit, batch size <=4096, exact quantization, and final total sorting. ASVI writes all 64 logical tile slots including explicit absence markers and layer seed. Palette/species changes use the exact three-way dirty union above; seed or surface identity/revision changes dirty `all_manifest_coords ∪ authoring_coords_with_nonzero_density`. If the active manifest's persisted Layer generation differs from the opened Layer and no same-session before-evidence journal proves a complete localized delta, use that same full-dirty union; never infer “no changes” from ASVI omitting global generation. Any dirty-chunk Pending/Failed/cancel/invalid result aborts the entire prepared generation. `prepare_vegetation_chunk_set` validates the canonical relative `layer_path`, derives exactly `<layer>.AshVegetationChunks/`, and binds that path/store identity into the prepared result; bake data alone never chooses a filesystem target. The worker publishes immutable objects/manifests durably through `IVegetationImmutablePublishFileOps`, which has no active-pointer replace capability. If a content-addressed object or manifest create-new reports `AlreadyExists`, the worker must read the existing bytes once, require exact size/SHA/strict decode equality with the candidate, and fail without overwrite on mismatch.

`VegetationPreparedChunkSet` captures the validated layer/store target, operation identity and exact source-active identity from the read snapshot. Main-thread commit accepts only `IVegetationCommitFileOps`, revalidates the bound target, compares `current_operation_identity`, then acquires the chunk-store lease, reads one exact on-disk `active.asva` byte snapshot and its manifest identity, and requires equality with the captured source-active identity before staging the pointer. If both captured and on-disk identity are `NoActive`, commit must use `CreateNewFromStage`; `AlreadyExists` means a cooperative writer won and returns source-changed/stale without touching that pointer, while I/O `Failed` remains a controlled failure. Only an existing on-disk active identity that exactly matches the captured existing identity may use `AtomicReplace`. `AtomicReplace` is never create-or-replace. A caller-cached current pointer is not accepted as disk evidence. Cancellation is checked around each batch, chunk, and <=1 MiB write block.

- [ ] **Step 4: Run GREEN and byte-determinism regression**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation baker*"
RunTests.bat Debug --test-case="Vegetation chunk set*"
RunTests.bat Release --test-case="Vegetation baker*"
RunTests.bat Release --test-case="Vegetation chunk set*"
RunTests.bat Debug --test-case="Vegetation Chunk codec*"
RunTests.bat Debug --test-case="Vegetation storage*"
RunArchGate.bat
git diff --check -- project/src/engine/Function/Asset/VegetationBaker.h project/src/engine/Function/Asset/VegetationBaker.cpp project/src/engine/Function/Asset/VegetationChunkSet.h project/src/engine/Function/Asset/VegetationChunkSet.cpp project/src/engine/Function/Asset/VegetationChunk.h project/src/engine/Function/Asset/VegetationChunk.cpp project/src/engine/Function/Asset/VegetationStorage.h project/src/engine/Function/Asset/VegetationStorage.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_baker_tests.cpp
```

- [ ] **Step 5: Review and selectively commit**

Review 1 independently recalculates both hash vectors, all four R8 limits, ASVI 624 bytes/digest, quantization, sort, the 65534 species ceiling, and both exact manifest/authoring dirty unions. Review 2 follows crash/fault/stale/cancel publication paths and proves no mixed generation, no zero-instance object, no global layer-generation pollution of ASVI, no Asset-to-Scene dependency, and no Render/GPUDriven include.

```bat
git add -- project/src/engine/Function/Asset/VegetationBaker.h project/src/engine/Function/Asset/VegetationBaker.cpp project/src/engine/Function/Asset/VegetationChunkSet.h project/src/engine/Function/Asset/VegetationChunkSet.cpp project/src/engine/Function/Asset/VegetationChunk.h project/src/engine/Function/Asset/VegetationChunk.cpp project/src/engine/Function/Asset/VegetationStorage.h project/src/engine/Function/Asset/VegetationStorage.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_baker_tests.cpp
git commit -m "feat(vegetation): bake deterministic chunk sets"
```

## Task 9: Orchestrate provider-safe authoring, retry, save, reload, and bake

**Depends on:** Tasks 1–8.

**Files:**

- Create: `project/src/editor/Services/VegetationEditorTaskExecutor.h`
- Create: `project/src/editor/Services/VegetationEditorTaskExecutor.cpp`
- Create: `project/src/editor/Services/VegetationEditorService.h`
- Create: `project/src/editor/Services/VegetationEditorService.cpp`
- Create: `project/src/tests/Editor/vegetation_editor_service_tests.cpp`
- Modify: `project/src/tests/Vegetation/VegetationTestSupport.h`
- Modify: `project/src/tests/premake5.lua`

- [ ] **Step 1: Write deterministic service RED cases**

Add the service and executor `.cpp` files to the Tests project, then add this test shape. Time is explicit; no test sleeps:

```cpp
TEST_CASE("Vegetation service retries Pending with the exact bounded schedule")
{
    VegetationTest::ScopedAssetRoot root("service-pending");
    const auto layer_path = root.WriteMinimalLayer();
    VegetationTest::ScriptedSurfaceProvider provider{};
    provider.PushPending(9); // initial attempt plus all eight retries
    VegetationTest::ManualVegetationEditorTaskExecutor executor{};
    VegetationTest::RecordingCommandExecutor commands{};
    AshEditor::VegetationEditorService service(VegetationTest::MakeEditorServiceDeps(
        root, commands, &provider, executor, VegetationTest::GenerousLoadBudget(),
        VegetationTest::GenerousChunkSetLoadBudget()));
    REQUIRE(service.Initialize());
    REQUIRE(service.OpenLayer(layer_path));

    const auto t0 = std::chrono::steady_clock::now();
    REQUIRE(service.RequestBake(t0));
    CHECK(provider.AttemptCount() == 1);
    CHECK(executor.IsIdle());
    const std::array<std::chrono::milliseconds, 8> schedule{
        50ms, 100ms, 200ms, 400ms, 800ms, 1000ms, 1000ms, 1000ms
    };
    std::chrono::milliseconds elapsed{0};
    for (size_t i = 0; i < schedule.size(); ++i)
    {
        service.Tick(t0 + elapsed + schedule[i] - 1ms);
        CHECK(provider.AttemptCount() == i + 1);
        elapsed += schedule[i];
        service.Tick(t0 + elapsed);
        CHECK(provider.AttemptCount() == i + 2);
        CHECK(executor.IsIdle());
    }
    CHECK(service.GetOperationState() == AshEditor::VegetationOperationState::TimedOut);
    CHECK(commands.RecordedCount() == 0);
    CHECK_FALSE(root.ActiveManifestChanged());
}

TEST_CASE("Vegetation service without a provider keeps palette and persistence but disables mutation")
{
    VegetationTest::ScopedAssetRoot root("service-no-provider");
    const auto species = root.WriteSpecies(
        "grass.AshVegetation", VegetationTest::SpeciesId(7));
    VegetationTest::ManualVegetationEditorTaskExecutor executor{};
    VegetationTest::RecordingCommandExecutor commands{};
    auto deps = VegetationTest::MakeEditorServiceDeps(
        root, commands, nullptr, executor, VegetationTest::GenerousLoadBudget(),
        VegetationTest::GenerousChunkSetLoadBudget());
    deps.create_layer_id = [] { return VegetationTest::LayerId(41); };
    AshEditor::VegetationEditorService service(std::move(deps));
    REQUIRE(service.Initialize());
    REQUIRE(service.CreateLayer("vegetation/new.AshVegetationLayer", 0x1234u));
    REQUIRE(service.AddPaletteSpecies(species));
    REQUIRE(commands.RecordedCount() == 1);
    const auto palette = service.GetPaletteView();
    REQUIRE(palette != nullptr);
    REQUIRE(palette->size() == 1);
    CHECK((*palette)[0].species_id == VegetationTest::SpeciesId(7));
    CHECK((*palette)[0].species_path == species);
    const auto capabilities = service.GetCapabilities();
    const auto t0 = std::chrono::steady_clock::now();
    const uint64_t before_generation = service.GetContentGeneration();
    CHECK(capabilities.can_load);
    CHECK(capabilities.can_save);
    CHECK_FALSE(capabilities.can_paint);
    CHECK_FALSE(capabilities.can_bake);
    CHECK(capabilities.surface_unavailable_reason ==
          "No vegetation surface provider is registered.");
    CHECK_FALSE(service.BeginStroke(VegetationTest::PaintBrush(), {}));
    CHECK_FALSE(service.RequestBake(t0));
    CHECK(service.GetContentGeneration() == before_generation);
    CHECK(commands.RecordedCount() == 1);
    REQUIRE(service.RequestSave(t0));
    executor.RunAll();
    service.Tick(t0);
    REQUIRE(service.RequestReload(t0 + 1ms));
    executor.RunAll();
    service.Tick(t0 + 1ms);
    REQUIRE(service.GetPaletteView()->size() == 1);
    CHECK(service.GetPaletteView()->front().species_digest == (*palette)[0].species_digest);
}
```

All `ScopedAssetRoot::Write*` helpers used by service tests create their complete on-disk fixture set before `MakeEditorServiceDeps` constructs the scanned `AssetDatabase`, and return canonical asset-root-relative paths. Tests that intentionally add files after `Initialize` must call an explicit catalog refresh helper before resolving them; no helper performs an implicit refresh.
`VegetationTest::MinimalLayerSnapshot()` consistently returns `std::shared_ptr<const AshEngine::VegetationLayerSnapshot>`; call sites taking a snapshot value/reference explicitly dereference it.

Add RED cases for `Ready` after retry; permanent `Pending`; `Failed`, throw, malformed capture and malformed sample; active-stroke/save/bake/reload conflict with zero side effects; `Dirty` ordinary Reload rejection; explicit confirmed `ReloadDiscard`; save source revision conflict; stale operation serial, layer generation, surface identity/revisions, and species digest; Create Layer rejecting existing/escaping paths and injected zero/colliding IDs; the target absent at Create Layer but created externally before the first Save commit, which must preserve external bytes, return `AlreadyExists`, keep the session Dirty with no observed revision, and remove/track only its owned stage; palette Add/Replace/Remove each producing one reversible command, updating the immutable view and generation once, undo/redo restoring exact canonical authoring payload bytes while generation remains monotonic, Remove confirmation clearing weights, and Save -> Reload retaining the edit without any provider; save completion advancing persisted generation only after checked main-thread commit; bake completion switching `active.asva` only after checked main-thread commit; working-set before/after dirty evidence surviving Undo/Redo/Save and every failed/stale/cancelled bake, then clearing only after the matching active-pointer commit; session replacement conservatively choosing full dirty when active Layer generation mismatches and no complete same-session evidence remains; cooperative cancellation during sampling and stage writes; shutdown acknowledgement, stage cleanup and no pointer switch; and executor destruction after service shutdown. The executor RED suite separately covers cancel-before-run, cancel-while-running, completion observation, exact join, and shutdown with a barrier-controlled production task. Tests assert exact state/reason strings and use explicit `Tick(now)` rather than wall-clock waits.

The stroke-specific RED matrix is separate and exact: `BeginStroke` accepts only a wrapper-validated `Ready+snapshot` capture made synchronously on the calling Editor/logic thread and stores that immutable snapshot plus its full identity; `Pending`, `Failed`, `Outside`, null/malformed, or throwing capture rejects the begin with no active stroke. `EndStroke` samples every accumulated request through that one snapshot in stable input order and batches of at most 4096. Every returned sample must be `Ready`; one `Outside`, `Pending`, `Failed`, throw, partial/malformed batch, cancellation, or deadline failure aborts the whole stroke with unchanged generation/patch/history. A completion-time wrapper capture on the Editor/logic thread must again be `Ready` with the exact stored surface ID and content/residency/transform revisions before mutation. Tests cover 4097 points as two batches, a failure in the second batch, surface revision change after sampling, and the success path applying exactly one patch then recording exactly one already-executed command. Brush strokes do not use the bake retry schedule: `Pending` is a fail-closed no-mutation result and the user may start a later stroke after residency becomes Ready.

- [ ] **Step 2: Run RED**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation service*"
RunTests.bat Debug --test-case="Vegetation executor*"
```

Expected RED: executor interface/production implementation/manual seam, service state/capability/palette snapshots, create/edit APIs, explicit-time retry, lifecycle-safe save/reload/bake, and cancellation APIs are absent. A RED caused by an Editor source omitted from `project/src/tests/premake5.lua` is not accepted as behavioral evidence.

- [ ] **Step 3: Implement the minimum orchestration API**

```cpp
struct VegetationEditorServiceDeps
{
    AshEngine::AssetDatabase* pAssetDatabase = nullptr;
    std::filesystem::path asset_root{};
    IEditorCommandExecutor* pCommandExecutor = nullptr;
    const AshEngine::IVegetationSurfaceProvider* pSurfaceProvider = nullptr;
    IVegetationEditorTaskExecutor* pTaskExecutor = nullptr;
    AshEngine::VegetationLoadBudget load_budget{};
    AshEngine::VegetationChunkSetLoadBudget chunk_set_load_budget{};
    std::function<AshEngine::VegetationId()> create_layer_id{};
    AshEngine::IVegetationFileOps* pFileOps = nullptr;
};

struct VegetationEditorTaskSubmission
{
    std::chrono::steady_clock::time_point deadline{};
    std::function<void(AshEngine::VegetationOperationControl)> work{};
};

class IVegetationEditorTaskExecutor
{
public:
    virtual ~IVegetationEditorTaskExecutor() = default;
    virtual uint64_t Submit(VegetationEditorTaskSubmission submission) = 0;
    virtual void RequestCancel(uint64_t task_id) = 0;
    virtual bool IsComplete(uint64_t task_id) const = 0;
    virtual std::exception_ptr GetException(uint64_t task_id) const = 0;
    virtual void Join(uint64_t task_id) = 0;
    virtual void CancelAndJoinAll() = 0;
    virtual bool IsIdle() const = 0;
};

class VegetationEditorTaskExecutor final : public IVegetationEditorTaskExecutor
{
public:
    uint64_t Submit(VegetationEditorTaskSubmission submission) override;
    void RequestCancel(uint64_t task_id) override;
    bool IsComplete(uint64_t task_id) const override;
    std::exception_ptr GetException(uint64_t task_id) const override;
    void Join(uint64_t task_id) override;
    void CancelAndJoinAll() override;
    bool IsIdle() const override;
};

struct VegetationPaletteViewEntry
{
    std::filesystem::path species_path{};
    AshEngine::VegetationId species_id{};
    AshEngine::VegetationSha256 species_digest{};
    std::string display_name{};
    AshEngine::AssetLoadState load_state = AshEngine::AssetLoadState::Unloaded;
    std::string error{};
};

using VegetationPaletteView = std::vector<VegetationPaletteViewEntry>;

class VegetationEditorService final
{
public:
    explicit VegetationEditorService(VegetationEditorServiceDeps deps);
    ~VegetationEditorService();
    static AshEngine::VegetationLoadBudget DefaultLoadBudget();
    static AshEngine::VegetationChunkSetLoadBudget DefaultChunkSetLoadBudget();
    bool Initialize();
    void Shutdown();
    void Tick(std::chrono::steady_clock::time_point now);
    bool CreateLayer(const std::filesystem::path& layer_path, uint64_t seed);
    bool OpenLayer(const std::filesystem::path& layer_path);
    bool AddPaletteSpecies(const std::filesystem::path& species_path);
    bool ReplacePaletteSpecies(AshEngine::VegetationId target_species_id,
                               const std::filesystem::path& species_path);
    bool RemovePaletteSpecies(AshEngine::VegetationId target_species_id,
                              bool confirmed_clear_weights);
    bool BeginStroke(const AshEngine::VegetationBrushStroke& stroke_without_path,
                     AshEngine::VegetationSurfaceBinding binding);
    bool AppendStrokePoint(const AshEngine::VegetationSurfaceSampleRequest& point);
    bool EndStroke(std::chrono::steady_clock::time_point now);
    bool RequestSave(std::chrono::steady_clock::time_point now);
    bool RequestSaveCopyAs(const std::filesystem::path& destination,
                           std::chrono::steady_clock::time_point now);
    bool RequestReload(std::chrono::steady_clock::time_point now);
    bool RequestReloadDiscard(bool confirmed,
                              std::chrono::steady_clock::time_point now);
    bool RequestBake(std::chrono::steady_clock::time_point now);
    VegetationEditorCapabilities GetCapabilities() const;
    std::shared_ptr<const VegetationPaletteView> GetPaletteView() const;
    VegetationOperationState GetOperationState() const;
    uint64_t GetContentGeneration() const;
};
```

The executor owns every worker thread and outlives the service. `Submit` creates the shared atomic cancellation state and passes the resulting `VegetationOperationControl` plus the absolute deadline into the work item; cancellation is therefore observable inside provider sampling and every file-write block rather than only by the queue. Every production thread entry catches all exceptions, stores `std::exception_ptr`, marks complete, and remains joinable; the service observes that exception as a controlled Failed result before joining, with no commit or stage leak. `ManualVegetationEditorTaskExecutor` lives only in the test file/support layer and deterministically exposes `RunNext`/`RunAll`; it implements the same exception state and is injected through the production interface so `Tick(now)` completion tests never sleep. A separate production-executor test uses condition-variable barriers, not elapsed sleeps, to prove queued cancellation, running cancellation acknowledgement, exact join, `CancelAndJoinAll`, and a throwing work item becoming complete/failed/idle without `std::terminate`.

`RequestBake` and every due retry in `Tick(now)` call `capture_vegetation_surface` synchronously on the Editor/logic thread, matching the provider boundary. `Pending` records only the next retry time and queues no executor work; `Failed`/malformed/throw terminates without a worker. Only a wrapper-validated `Ready+snapshot` is handed to the executor. REDs assert attempt count immediately after `RequestBake`/due `Tick`, assert `executor.IsIdle()` for every Pending attempt, and only call `RunNext` after a Ready capture has made the executor non-idle.

Workers capture immutable Layer/Species/surface/active-store/resolver snapshots, operation control, paths, serials, value results, and an independently lifetime-owned cleanup registry only; they never call provider `capture`, and never capture `this`, Scene, selection, UI, or mutable AssetDatabase catalog internals. A null `pFileOps` resolves once to `get_default_vegetation_file_ops()`; tests inject `ScriptedVegetationFileOps`, and every Layer/Chunk operation receives that same reference. The service creates one `std::shared_ptr<VegetationOwnedStageCleanupRegistry>` and workers capture that shared pointer by value rather than a service subobject; `Track*` is internally synchronized, while `RetryAll` runs on the Editor thread only after the exact worker has joined. The Editor thread retries retained cleanup after each joined completion, rechecks every identity, and alone invokes checked commit. Pending retry times are exactly `50,100,200,400,800,1000,1000,1000 ms`, at most eight retries and never after the absolute 30-second operation deadline. Shutdown first requests cancellation, then waits for acknowledgement/join, invokes `cleanup_registry->RetryAll(*pFileOps)`, and discards completion without switching a pointer. A non-empty registry keeps shutdown/status Failed and lists only the exact retained paths; it is never silently cleared. A trusted provider that violates its 50 ms cooperative contract is logged, but C++17 teardown still waits and does not detach or forcibly kill it.

At RequestBake the service captures the working set's immutable dirty-evidence snapshot together with its generation and passes it into `VegetationBakeInput`. Failed, cancelled, timed-out, stale, or publication-fault paths retain the evidence. Only after the main-thread pointer commit succeeds and all identities still match may the service call `acknowledge_bake_dirty_evidence(captured_generation)`; any later edits make that acknowledgement fail without clearing newer evidence. Open/reload/session replacement discards the old working set, and the active-manifest versus opened-Layer generation rule then supplies the restart full-dirty fallback.

`BeginStroke` requires `stroke_without_path.path.empty()`, captures through `capture_vegetation_surface`, accepts only `Ready+snapshot`, and stores that immutable snapshot and full identity for the stroke. The service appends only canonical surface requests supplied through `AppendStrokePoint`. `EndStroke` schedules one controlled worker operation that validates the complete path through `sample_vegetation_surface_batch` in stable batches of at most 4096; it accepts only per-sample `Ready` and treats `Outside` as whole-stroke failure because Editor hit requests are required to be on the explicitly bound surface. It publishes no partial samples. On completion the Editor thread re-captures through the wrapper and requires the exact stored surface identity/revisions before it calls the pure Task 5 brush API; only a successful non-no-op patch is applied and passed once to `RecordExecutedCommand`. `Pending` is not retried for strokes. All other sampling/capture/cancel/deadline/stale paths discard the pending stroke with generation and history unchanged. `CreateLayer` validates an asset-root-relative path, confirms absence through the injected FileOps, and opens an unsaved Dirty session with `expected_revision=nullopt`; it performs no target write. The first `RequestSave` stages on a worker and commits with atomic create-new, so a racing creator is preserved and the session remains dirty with `AlreadyExists`. The Layer obtains a nonzero 128-bit identity from the injected generator in tests or two OS-backed `std::random_device` 64-bit words in production, retries zero/current-ID collisions, and never overwrites an existing file. This entropy creates asset identity only and is never read by the deterministic baker. Palette Add/Replace/Remove first resolve the Species through the typed AssetDatabase path, then call the Task 5 palette patch API and record exactly one already-executed document command. Successful edits update the immutable palette view, advance generation once, mark the session dirty, and remain usable without a surface provider; Remove requires the explicit clear-weights confirmation when referenced. Save/reload round-trip palette order, IDs, digests, paths, and cleared planes.

`DefaultLoadBudget()` is the explicit product resident policy, not a wire/world cap: `max_file_bytes=256 MiB`, `max_payload_bytes=256 MiB`, `max_decoded_bytes=1 GiB`, `max_palette_records=65534`, `max_tile_records=262144`, and `max_instance_records=8388608`. `DefaultChunkSetLoadBudget()` uses that per-file budget plus `max_manifest_entries=262144`, `max_total_inspected_bytes=2 GiB`, and `max_summary_bytes=64 MiB`. `EditorApplicationImpl` injects both values rather than `{}`; tests assert every field and the actual bootstrap dependency are nonzero. Callers/tests may inject smaller or larger resident policies without changing wire/world limits.

- [ ] **Step 4: Run GREEN and lifecycle regression**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation service*"
RunTests.bat Debug --test-case="Vegetation executor*"
RunTests.bat Release --test-case="Vegetation service*"
RunTests.bat Debug --test-case="Vegetation storage*"
RunTests.bat Debug --test-case="Vegetation baker*"
build_editor.bat Debug
RunArchGate.bat
git diff --check -- project/src/editor/Services/VegetationEditorTaskExecutor.h project/src/editor/Services/VegetationEditorTaskExecutor.cpp project/src/editor/Services/VegetationEditorService.h project/src/editor/Services/VegetationEditorService.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Editor/vegetation_editor_service_tests.cpp project/src/tests/premake5.lua
```

- [ ] **Step 5: Review and selectively commit**

Review 1 follows every state transition, retry boundary, palette mutation, conflict and stale completion, and proves failed operations leave content/history/manifest unchanged. Review 2 follows ownership and teardown, proving cancellation reaches running work, no detached thread, service pointer, Scene pointer, catalog pointer, late commit, test provider in product bootstrap, or Graphics/Render dependency.

```bat
git add -- project/src/editor/Services/VegetationEditorTaskExecutor.h project/src/editor/Services/VegetationEditorTaskExecutor.cpp project/src/editor/Services/VegetationEditorService.h project/src/editor/Services/VegetationEditorService.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Editor/vegetation_editor_service_tests.cpp project/src/tests/premake5.lua
git commit -m "feat(editor): orchestrate vegetation authoring"
```

## Task 10: Close the Phase 2 exit contract with one end-to-end integration test

**Depends on:** Task 9.

**Files:**

- Create: `project/src/tests/Vegetation/vegetation_phase2_exit_tests.cpp`
- Modify: `project/src/tests/Vegetation/VegetationTestSupport.h`
- Modify: `project/src/editor/Services/VegetationEditorService.h`
- Modify: `project/src/editor/Services/VegetationEditorService.cpp`

- [ ] **Step 1: Write the full-chain RED test**

```cpp
TEST_CASE("Vegetation Phase 2 authoring and deterministic bake exit contract")
{
    VegetationTest::ScopedAssetRoot root("phase2-exit");
    const auto layer_path = root.WriteLayerWithTwoSpecies();
    const auto replacement_path = root.WriteReplacementSpeciesWithSameId();
    VegetationTest::DeterministicSurfaceProvider provider{};
    VegetationTest::ManualVegetationEditorTaskExecutor executor{};
    VegetationTest::RecordingCommandExecutor commands{};
    AshEditor::VegetationEditorService service(VegetationTest::MakeEditorServiceDeps(
        root, commands, &provider, executor, VegetationTest::GenerousLoadBudget(),
        VegetationTest::GenerousChunkSetLoadBudget()));
    REQUIRE(service.Initialize());
    REQUIRE(service.OpenLayer(layer_path));
    REQUIRE(service.ReplacePaletteSpecies(
        VegetationTest::SpeciesId(1), replacement_path));
    REQUIRE(service.RemovePaletteSpecies(VegetationTest::SpeciesId(2), true));
    REQUIRE(commands.Undo());
    REQUIRE(commands.Redo());

    REQUIRE(VegetationTest::ExecutePaintStroke(service, executor));
    REQUIRE(VegetationTest::ExecuteEraseStroke(service, executor));
    CHECK(commands.RecordedCount() == 4);
    REQUIRE(commands.Undo());
    REQUIRE(commands.Redo());
    REQUIRE(VegetationTest::CompleteSave(service, executor));
    const auto saved = service.GetStatusSnapshot();
    CHECK(saved.session == AshEditor::VegetationSessionState::Clean);
    REQUIRE(VegetationTest::CompleteCleanReload(service, executor));
    REQUIRE(service.GetStatusSnapshot().palette != nullptr);
    CHECK(service.GetStatusSnapshot().palette->size() == 1);

    const auto first = VegetationTest::CompleteBake(service, executor, provider, false);
    const auto reordered = VegetationTest::CompleteBake(service, executor, provider, true);
    CHECK(first.chunk_bytes == reordered.chunk_bytes);
    CHECK(first.manifest_bytes == reordered.manifest_bytes);
    CHECK(first.active_pointer_bytes == reordered.active_pointer_bytes);

    provider.SetMode(VegetationTest::SurfaceMode::Failed);
    CHECK_FALSE(VegetationTest::CompleteBake(
        service, executor, provider, false).published);
    CHECK(service.GetStatusSnapshot().last_known_good_manifest_digest ==
          first.manifest_digest);
    service.Shutdown();
    CHECK(executor.IsIdle());
    CHECK_FALSE(root.HasOperationStages());
}
```

The same file adds provider `Pending`, malformed batch, source-changed reload, dirty ordinary reload, stale serial, object/manifest/pointer fault through a service-injected `ScriptedVegetationFileOps`, manifest-only versus authoring-density-only full-dirty coordinates, seed absent-to-present/present-to-absent, surface revision absent-to-present/present-to-absent, and shutdown during sample/write subcases. It also constructs the exact Remove+clear regression: active manifest coord C contains another species but not S, old authoring at C has nonzero S weight, Remove clears it, Save and a failed bake occur, then the next successful service bake must still include C from retained before-evidence and clear the evidence only after pointer commit. Every fixture uses deterministic explicit time and in-memory scripted provider/file state.

All async test helpers take the injected `ManualVegetationEditorTaskExecutor&` explicitly and drive `RunNext/RunAll` plus `service.Tick(explicit_now)`; none discovers an executor through globals. `CompleteBake` also takes `DeterministicSurfaceProvider&` and sets its deterministic input-order mode before requesting the bake, then restores the mode before return. The helpers assert a task was queued, drive only that executor, and fail if the expected completion is not observed. This is the only mechanism by which the exit test advances save/reload/stroke/bake work.

- [ ] **Step 2: Run RED**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation Phase 2 authoring and deterministic bake exit contract"
```

Expected RED: the production service has no coherent `VegetationEditorStatusSnapshot`/`GetStatusSnapshot()` containing published generation, active and last-known-good manifest identities, immutable palette, operation state and capabilities. The RED must not be manufactured with a test-only free function.

- [ ] **Step 3: Add the production status snapshot and close sequencing gaps**

```cpp
struct VegetationEditorStatusSnapshot
{
    VegetationSessionState session = VegetationSessionState::Failed;
    VegetationOperationState operation = VegetationOperationState::Idle;
    std::filesystem::path source_path{};
    uint64_t content_generation = 0;
    uint64_t persisted_generation = 0;
    std::optional<AshEngine::VegetationFileRevision> observed_revision{};
    AshEngine::VegetationSha256 active_manifest_digest{};
    AshEngine::VegetationSha256 last_known_good_manifest_digest{};
    std::shared_ptr<const VegetationPaletteView> palette{};
    VegetationEditorCapabilities capabilities{};
    std::string detail{};
};

VegetationEditorStatusSnapshot VegetationEditorService::GetStatusSnapshot() const;
```

Return one Editor-thread value snapshot; its immutable palette pointer is replaced only after a complete successful load/edit/revert and the panel in Task 11 is its second production consumer. Fix only integration sequencing exposed by the test: successful checked commit advances publication/persisted state, while any failed, cancelled, stale, dirty-conflict, or malformed path preserves the prior coherent snapshot, palette and last-known-good pointer.

- [ ] **Step 4: Run GREEN in both configurations**

```bat
RunTests.bat Debug --test-case="Vegetation Phase 2 authoring and deterministic bake exit contract"
RunTests.bat Release --test-case="Vegetation Phase 2 authoring and deterministic bake exit contract"
RunTests.bat Debug --test-case="Vegetation service*"
RunTests.bat Debug --test-case="Vegetation baker*"
build_editor.bat Debug
RunArchGate.bat
git diff --check -- project/src/editor/Services/VegetationEditorService.h project/src/editor/Services/VegetationEditorService.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_phase2_exit_tests.cpp
```

- [ ] **Step 5: Review and selectively commit**

Review 1 traces the full chain and proves byte identity across input order and repeat cook. Review 2 proves every negative path retains last-known-good and that `GetStatusSnapshot()` is a real panel-facing value boundary, not a test-only abstraction.

```bat
git add -- project/src/editor/Services/VegetationEditorService.h project/src/editor/Services/VegetationEditorService.cpp project/src/tests/Vegetation/VegetationTestSupport.h project/src/tests/Vegetation/vegetation_phase2_exit_tests.cpp
git commit -m "feat(vegetation): close phase two exit contract"
```

## Task 11: Add the disabled-safe Vegetation panel and application lifecycle wiring

**Depends on:** Tasks 9–10.

**Files:**

- Create: `project/src/editor/Core/PanelDeps/VegetationPanelDeps.h`
- Create: `project/src/editor/Panels/Vegetation/VegetationPanel.h`
- Create: `project/src/editor/Panels/Vegetation/VegetationPanel.cpp`
- Create: `project/src/tests/Editor/vegetation_panel_contract_tests.cpp`
- Modify: `project/src/editor/Core/EditorIds.h`
- Modify: `project/src/editor/App/PanelBootstrapper.h`
- Modify: `project/src/editor/App/PanelBootstrapper.cpp`
- Modify: `project/src/editor/App/EditorApplicationImpl.h`
- Modify: `project/src/editor/App/EditorApplicationImpl.cpp`
- Modify: `project/src/tests/premake5.lua`

- [ ] **Step 1: Write the panel and lifecycle RED contract**

Create the panel directory before adding its files:

```powershell
New-Item -ItemType Directory -Force project/src/editor/Panels/Vegetation | Out-Null
```

```cpp
TEST_CASE("Vegetation panel exposes a disabled-safe no-provider contract")
{
    VegetationTest::ScopedAssetRoot root("panel-no-provider");
    VegetationTest::ManualVegetationEditorTaskExecutor executor{};
    VegetationTest::RecordingCommandExecutor commands{};
    AshEditor::VegetationEditorService service(VegetationTest::MakeEditorServiceDeps(
        root, commands, nullptr, executor, VegetationTest::GenerousLoadBudget(),
        VegetationTest::GenerousChunkSetLoadBudget()));
    REQUIRE(service.Initialize());
    AshEditor::VegetationPanel panel({ &service });
    CHECK(panel.GetId() == AshEditor::EditorPanelIds::Vegetation);
    CHECK(panel.GetTitle() == AshEditor::EditorWindowTitles::Vegetation);
    const auto default_budget = AshEditor::VegetationEditorService::DefaultLoadBudget();
    const auto default_chunk_budget = AshEditor::VegetationEditorService::DefaultChunkSetLoadBudget();
    CHECK(default_budget.max_file_bytes > 0);
    CHECK(default_budget.max_decoded_bytes > 0);
    CHECK(default_chunk_budget.per_file.max_file_bytes > 0);
    CHECK(default_chunk_budget.max_total_inspected_bytes > 0);
    const auto status = service.GetStatusSnapshot();
    CHECK(status.capabilities.can_load);
    REQUIRE(status.palette != nullptr);
    CHECK_FALSE(status.capabilities.can_paint);
    CHECK_FALSE(status.capabilities.can_bake);
    CHECK(status.capabilities.surface_unavailable_reason ==
          "No vegetation surface provider is registered.");
}
```

Add public-behavior cases for attach/detach safety and no-provider `OnUpdate` using the real service plus the existing recording command executor. The CPU Tests target does not construct or fake a `UIContext`: repository `UIContext` initialization requires a real Window/GraphicsContext/RenderDevice and there is no approved headless-input seam. New Layer, Add/Replace/Remove Species, Save, Reload, palette mutation, exact Undo/Redo, and Save -> Reload intent effects are therefore mechanically covered at the real service boundary in Task 9; Task 11 checks the panel ID/title, lifecycle-safe cached status/capabilities and disabled reason without invoking `OnGui`. A code review maps every `OnGui` control to exactly one existing service intent and confirms Paint/Erase/Bake use disabled UI scopes when the provider is absent. Task 12's visible Vulkan/DX12 human gate is the executable evidence for actual control clicks. The test does not subclass the final service, inspect source text, invent a panel presenter abstraction, or call GUI without a real `UIContext`. Application ownership order is verified by the Editor build and the two independent lifecycle reviews rather than a test-only application abstraction.

- [ ] **Step 2: Run RED**

Add `Core/EditorPanel.cpp`, the new panel `.cpp`, and only their already-required Editor dependencies to `project/src/tests/premake5.lua`, then run:

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation panel*"
```

Expected RED: panel IDs/deps/class do not exist. Bootstrap/application wiring is compiled only after the panel GREEN, so an unrelated unresolved symbol is not accepted as this RED.

- [ ] **Step 3: Implement the smallest production panel and bootstrap**

```cpp
struct VegetationPanelDeps
{
    VegetationEditorService* pVegetationService = nullptr;
};

class VegetationPanel final : public EditorPanel
{
public:
    explicit VegetationPanel(VegetationPanelDeps deps = {});
    void OnAttach() override;
    void OnDetach() override;
    void OnUpdate() override;
    void OnGui(const EditorFrameContext& frame) override;
private:
    VegetationPanelDeps _deps{};
    VegetationEditorStatusSnapshot _status{};
};
```

Add `EditorPanelIds::Vegetation = "vegetation"` and `EditorWindowTitles::Vegetation = "Vegetation"`. Add a service pointer to `PanelBootstrapContext`, construct one panel through `PanelManager`, and pass only `VegetationPanelDeps`. The panel uses `UIContext` through `EditorFrameContext`; it never includes Graphics, backend, Scene internals, or Terrain internals. It displays the immutable palette view and exposes New Layer, Load, Add/Replace/Remove Species, Save and Reload intents even when no provider exists. Removing a referenced Species requires an explicit clear-weights confirmation. Paint/Erase/Bake controls are disabled with the exact capability reason when no provider is injected.

`EditorApplicationImpl` owns `VegetationEditorTaskExecutor` before `VegetationEditorService`, initializes the service after AssetDatabase, passes `DefaultLoadBudget()`, `DefaultChunkSetLoadBudget()`, the default FileOps and a null provider, ticks it before panel update, shuts panels down first, then invokes service shutdown/cancel/join, then releases the service and executor. A lifecycle review and the panel contract verify the bootstrap never passes zero budgets. Do not register a deterministic provider in product code.

- [ ] **Step 4: Run GREEN and Editor regression**

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="Vegetation panel*"
RunTests.bat Release --test-case="Vegetation panel*"
RunTests.bat Debug --test-case="*Inspector*"
build_editor.bat Debug
RunArchGate.bat
git diff --check -- project/src/editor/Core/PanelDeps/VegetationPanelDeps.h project/src/editor/Panels/Vegetation/VegetationPanel.h project/src/editor/Panels/Vegetation/VegetationPanel.cpp project/src/editor/Core/EditorIds.h project/src/editor/App/PanelBootstrapper.h project/src/editor/App/PanelBootstrapper.cpp project/src/editor/App/EditorApplicationImpl.h project/src/editor/App/EditorApplicationImpl.cpp project/src/tests/Editor/vegetation_panel_contract_tests.cpp project/src/tests/premake5.lua
```

- [ ] **Step 5: Review and selectively commit**

Review 1 checks panel intent-only behavior, palette edit/save/reload and exact disabled reason; only surface-dependent mutation is blocked without a provider. Review 2 checks construction/destruction order, cancellation/join, no product test provider, no Scene/Graphics/Terrain coupling, and exact test-premake additions.

```bat
git add -- project/src/editor/Core/PanelDeps/VegetationPanelDeps.h project/src/editor/Panels/Vegetation/VegetationPanel.h project/src/editor/Panels/Vegetation/VegetationPanel.cpp project/src/editor/Core/EditorIds.h project/src/editor/App/PanelBootstrapper.h project/src/editor/App/PanelBootstrapper.cpp project/src/editor/App/EditorApplicationImpl.h project/src/editor/App/EditorApplicationImpl.cpp project/src/tests/Editor/vegetation_panel_contract_tests.cpp project/src/tests/premake5.lua
git commit -m "feat(editor): add vegetation authoring panel"
```

## Task 12: Update durable contracts and run the complete Phase 2 exit matrix

**Depends on:** Tasks 1–11 and all focused reviews.

**Files:**

- Modify: `README.md`
- Create: `docs/specs/features/vegetation.md`
- Modify: `docs/specs/README.md`
- Modify: `docs/specs/modules/asset.md`
- Modify: `docs/specs/modules/scene.md`
- Modify: `docs/specs/modules/editor.md`
- Modify: `docs/CODEBASE_MAP.md`
- Modify: `docs/sdd/SDD-2026-07-16-vegetation-authoring-and-bake.md`
- Modify: `docs/sdd/SDD-2026-07-13-world-scale-gpu-vegetation.md`
- Modify: `docs/plans/2026-07-16-vegetation-authoring-and-bake.md`
- Modify: `docs/plans/README.md`

- [ ] **Step 1: Run the documentation RED contract before editing docs**

```powershell
$feature = 'docs/specs/features/vegetation.md'
if (-not (Test-Path $feature)) { throw 'vegetation feature spec missing' }
$text = Get-Content -Raw $feature
@('ASVI',
  '947CF950782752599F3D6E51918D8E082039237FAC4B3A17F459E8481D4520CF',
  'RecordExecutedCommand',
  'trusted in-process',
  'No vegetation surface provider is registered.') | ForEach-Object {
    if ($text -notmatch [regex]::Escape($_)) { throw "vegetation feature contract missing: $_" }
}
```

Expected RED: the feature spec does not yet exist. This is the only documentation-only RED; do not change production code in Task 12.

- [ ] **Step 2: Write the long-lived contract**

Document exact asset shapes and budgets, Asset-owned snapshot versus Scene-owned provider capture, Scene v7 reference-only component, brush integer formulas, palette/Create Layer behavior, document history, checked storage, deterministic hash/ASVI/ASVM/ASVA, the two exact manifest/authoring dirty unions, 65534 species ceiling, cooperative-provider limitation, no-provider behavior, lifecycle order, last-known-good semantics, and Phase 3 handoff. Update the root `README.md` verification/feature index with the public vegetation spec and the new focused verification entry; AIDevDoctor treats that root pointer as part of the durable documentation contract. Mark the Phase 2 SDD Done only after automated gates and human disabled-path checks have actually passed. Update `docs/CODEBASE_MAP.md` because new public Asset/Scene/Editor entry points exist. Mark the overall S3 Phase 2 complete without claiming GPU rendering exists.

- [ ] **Step 3: Run fresh CPU generation, tests, builds, architecture and plan gates**

Run strictly from the Phase 2 worktree and stop on the first failure:

```bat
generate_vs2022.bat
RunTests.bat Debug --test-case="*Vegetation*"
RunTests.bat Release --test-case="*Vegetation*"
RunTests.bat Debug
RunTests.bat Release
build_editor.bat Debug
build_editor.bat Release
build_sandbox.bat Debug
build_sandbox.bat Release
RunArchGate.bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
git diff --check
```

Record exact case/assertion counts, exit codes, ArchGate legacy warning count, build target/configuration, and the source HEAD. Request two read-only **code-freeze pre-reviews** before GPU/manual execution. One review follows all wire/hash/transaction/state contracts; the other audits dependency direction, lifetimes, no-provider product behavior, and forbidden paths. These pre-reviews do not certify the still-pending final evidence/docs diff. Any P0/P1/P2 finding reopens the relevant task and invalidates later evidence.

- [ ] **Step 4: Run the coordinated GPU/readiness regression matrix**

Obtain a fresh exclusive CPU/GPU window from every active worktree. Snapshot `product/config/Engine.ini`, `product/config/editor/EditorSettings.json`, `product/config/editor/ViewportLayout.json`, and `product/config/editor/imgui.ini` byte-for-byte before running. Do not bless or import anything.

```bat
run.bat all Debug --smoke-test-seconds=120
RunRenderGate.bat
RunPerfGate.bat -Profile Standard
RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release
```

Requirements: Editor/Sandbox × Vulkan/DX12 readiness all exit zero; Sandbox reports clean exit; every newly created Engine/Application log has zero generic error/critical, validation, debug-layer, device-lost, access-violation, fatal, assertion, and bad-leak findings; RenderGate is PASS non-bless; both PerfGate profiles are PASS/COMPARED with no warning/failure and no baseline change. Phase 2 does not run GPU validation because it makes no Graphics/backend/render integration; that starts with the Phase 3 rendering SDD. Restore all four config files to the exact pre-run bytes, verify their SHA-256 values, verify effective Editor/Sandbox/AshImageDiff/build/gate roots are zero, and explicitly release the window.

- [ ] **Step 5: Obtain the human disabled-path evidence**

Before launch, create a unique session-owned directory under `product/assets/vegetation/manual-phase2/<session-id>/` and verify it did not preexist. Copy the exact reviewed and codec-round-tripped bytes of both `project/src/tests/fixtures/vegetation/Phase2ManualSpecies.AshVegetation` and `project/src/tests/fixtures/vegetation/Phase2ManualSpeciesReplacement.AshVegetation` into that directory; do not generate ad-hoc JSON or touch user assets. Assert before launch that both decode to the same embedded species ID and different canonical SHA-256 values. Snapshot `Engine.ini`, `EditorSettings.json`, `ViewportLayout.json`, and `imgui.ini` byte-for-byte immediately before the manual sequence. Coordinate a visible Editor window separately for Vulkan, then DX12. For each backend the human uses a distinct new filename (`manual-vulkan.AshVegetationLayer` then `manual-dx12.AshVegetationLayer`), adds the original Species, saves, reloads, verifies the palette ID/path/digest remains present, replaces it with the reviewed same-ID replacement fixture, removes it, undoes/redoes the palette edits, and confirms the Vegetation panel remains responsive. Paint/Erase/Bake must remain disabled with `No vegetation surface provider is registered.`, and no test provider or fake Terrain surface may appear.

AI may launch and collect logs, but must not operate the UI, overwrite a preexisting path, or declare the human result. After each Editor closes, require normal exit and fresh Engine/Application logs with zero generic error/critical, validation/debug, device-lost, access-violation, fatal, assertion and bad-leak findings. After both backends, restore all four config files from the manual pre-snapshot, verify exact SHA-256, delete only the verified session-owned directory, confirm effective Editor/Sandbox/AshImageDiff/gate roots are zero, and explicitly release the GPU window. If the human is unavailable, record `automated gates passed; manual disabled-path pending`, preserve no temporary fixture/config mutation, and do not mark the SDD Done.

- [ ] **Step 6: Write final evidence, lock the final diff, re-review, and selectively commit**

After automated and human evidence is complete, update every Task 12 spec/SDD/plan file with exact report paths, case/assertion counts, source HEAD, manual Vulkan/DX12 outcome, config/baseline restoration, remaining limitations, and Phase 3 handoff. Only now may the Phase 2 SDD status become Done; if human evidence is unavailable or failed, leave it non-Done and record the pending/failed gate. Run AIDevDoctor and `git diff --check`, then record SHA-256 for the complete plan, SDD, and final diff path list.

Request two new independent read-only reviews against those exact final hashes. One rechecks code/wire/hash/transaction/state plus evidence; the other rechecks dependency/lifetime/no-provider behavior, all durable docs, forbidden paths, and that claims match reports. Both must return P0=0/P1=0/P2=0 CLEAN. After the hashes are sent, no file may change before staging/commit; any change invalidates both verdicts and requires fresh hashes/reviews (and reruns affected execution gates if behavior changed).

```powershell
$sddHash = (Get-FileHash -Algorithm SHA256 docs/sdd/SDD-2026-07-16-vegetation-authoring-and-bake.md).Hash
$planHash = (Get-FileHash -Algorithm SHA256 docs/plans/2026-07-16-vegetation-authoring-and-bake.md).Hash
$staged = @(git diff --cached --name-only)
if ($staged | Where-Object { $_ -like 'project/thirdparty/tracy/*' }) {
    throw 'Tracy executable noise must never be staged'
}
Write-Host "Final archived SDD hash: $sddHash"
Write-Host "Final plan hash: $planHash"
```

```bat
powershell -NoProfile -ExecutionPolicy Bypass -File scripts/AIDevDoctor.ps1 -Mode ValidatePlan
git diff --check
git add -- README.md docs/specs/features/vegetation.md docs/specs/README.md docs/specs/modules/asset.md docs/specs/modules/scene.md docs/specs/modules/editor.md docs/CODEBASE_MAP.md docs/sdd/SDD-2026-07-16-vegetation-authoring-and-bake.md docs/sdd/SDD-2026-07-13-world-scale-gpu-vegetation.md docs/plans/2026-07-16-vegetation-authoring-and-bake.md docs/plans/README.md
git commit -m "docs(vegetation): close phase two authoring"
```

## Dependency and contract coverage

| Task | Requires | Primary contract closed |
| ---: | --- | --- |
| 1 | approved SDD | coordinates, SHA/CRC, cancellation, Asset snapshot validation, Scene provider capture |
| 2 | 1 | strict Species/Layer/Chunk v1 codecs and caller budgets |
| 3 | 2 | AssetDatabase registration and immutable typed loads |
| 4 | 1–3 | Scene v7 reference-only component and Editor snapshots |
| 5 | 1–2 | sparse working set, canonical stroke, integer brush, atomic patch |
| 6 | 5 | already-executed command and document-scoped history |
| 7 | 1–2 | checked Layer stage/commit/copy/reload |
| 8 | 1–3, 5, 7 | deterministic bake, ASVI, dirty unions, immutable object/manifest publication |
| 9 | 1–8 | explicit-time retry, service state, save/reload/bake orchestration, bounded cooperative lifecycle |
| 10 | 9 | single Phase 2 paint-to-LKG exit contract |
| 11 | 9–10 | disabled-safe panel and application ownership order |
| 12 | 1–11 | durable specs, full CPU/build/readiness/render/performance/manual exit evidence |

## Stop rules and completion definition

- Stop immediately if the approved SDD hash changes, an expected RED fails for an unrelated reason, a focused/full test fails, ArchGate adds a violation, a backend log contains a rejected diagnostic, RenderGate/PerfGate fails or warns, config restoration differs, or a review reports P0/P1/P2.
- Never resolve a failure by disabling validation, lowering an SSIM/performance threshold, blessing a golden/baseline, truncating a world/file into a successful result, registering the test provider in product, or entering Graphics/Render/GPUDriven.
- Never stage/reset the two Tracy executables. Before every commit compare `git diff --cached --name-only` against its exact task list.
- Phase 2 is complete only when all twelve tasks are selectively committed, Debug/Release focused and full tests pass, Editor/Sandbox Debug/Release builds pass, ArchGate/AIDevDoctor pass, two stable-diff reviews are clean, four-combination readiness/RenderGate/Standard/VFP pass non-bless, configuration and roots are clean, the human disabled-path result is signed, and long-lived specs/SDDs are updated. This does not claim grass/tree rendering, Terrain integration, GPU culling, streaming, HLOD, wind, shadow, or SpeedTree ingestion; those remain later approved phases.
