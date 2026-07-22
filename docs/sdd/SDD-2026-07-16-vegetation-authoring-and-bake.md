# SDD-2026-07-16-vegetation-authoring-and-bake: 植被资产、表面契约、笔刷与确定性烘焙

## Status

Approved（2026-07-16；用户批准 contract-first 方案，并授权后续阶段采用明确标注的推荐方案时无需重复等待；Task 2 codec 执行澄清于同日按该持续授权并入）

## Context

总体设计见 `SDD-2026-07-13-world-scale-gpu-vegetation.md`。Phase 0 的 GPU 性能观测和 Phase 1 的 RenderGraph buffer、indexed indirect 与通用 GPUDriven page/prototype 底座已经完成并通过 PR #9 合入 `main`。当前仓库仍没有生产植被资产、Scene 组件、笔刷、烘焙产物或运行时渲染。

本阶段是总体 S3 的 Phase 2，负责建立 GPU grass/tree 共同依赖的 CPU 作者数据和稳定磁盘契约。Terrain 仍在独立分支开发，且该分支与 `main` 分叉并存在未提交工作；因此本阶段不能直接依赖 Terrain internal、Editor working set 或移动中的 Scene v6 实现。先冻结一个 Function 层不可变 surface snapshot 契约，用确定性 test provider 关闭 paint/erase/undo/reload/cook 闭环；Terrain 稳定合入后只实现 adapter，不改变植被资产格式和烘焙算法。

该变更跨 Function/Asset、Function/Scene 与 Editor，并新增 Scene 数据模型，定级为 S2。本文经批准前不修改生产代码；总体 S3 的后续 GPU grass/tree/HZB/HLOD 仍各自使用独立 SDD。

## Goals

- 定义稳定、严格版本化的 `.AshVegetation`、`.AshVegetationLayer` 与 `.AshVegetationChunk`。
- 定义可由 Terrain 或其他生产表面实现的 worker-safe、批量、不可变 surface snapshot 契约。
- Scene 只引用 Vegetation Layer 与 surface entity，不保存单株 transform 或稀疏 tile payload。
- 提供世界尺度稀疏 density/species-weight tile、确定性笔刷、一次 stroke 一个 patch command 的 Undo/Redo。
- 提供捕获 immutable layer/surface snapshot 的异步增量 baker，以及 generation/revision checked 的原子 chunk 发布。
- 相同 layer、chunk/cell、species、seed 和 surface revision 产生稳定排序与 byte-identical chunk。
- 无 surface provider、provider Pending/Failed、stale generation 或损坏资产时 fail closed，并保留 last-known-good chunk。
- 为后续 Phase 3 把 cooked instances 映射到既有通用 GPUDriven prototype/page 契约，而不污染 GPUDriven 基础类型。

## Non-goals

- 不修改 Graphics、RHI、RenderGraph 或 `Function/Render/GPUDriven` 公共契约。
- 不实现 GPU culling、streaming、grass/tree shader、wind、shadow、impostor、HZB 或 HLOD。
- 不接入 SpeedTree SDK，不在运行时读取 `.st`；Phase 2 species 只引用已有 mesh/material 资产。
- 不在本阶段把活动 Terrain 分支代码复制或合并进本分支。
- 不把 test provider 注册为产品 fallback，也不允许没有真实 provider 时落笔或烘焙。
- 不复制 Terrain 的 reload/conflict/repair/大型文件作业状态机；只实现本阶段所需的 generation-safe save/reload/bake。
- 不修改或 bless render golden、PerfGate baseline。

## Current implementation

- Entry points:
  - `AssetDatabase` 识别并同步/异步加载现有 Mesh/Model/Material/AshAsset；没有植被类型。
  - Scene schema v6 包含 Particle；没有 `VegetationComponent`。
  - Editor 的 Panel → Service → patch Command → `UndoRedoService` 分层已经由 Terrain authoring 证明，但 Terrain 尚未进入 `main`。
  - `Function/Render/GPUDriven/` 已提供通用 prototype/page/view/draw-group 与 generation-safe page retirement。
- Modules: Function/Asset、Function/Scene、Editor；Phase 2 不进入 Graphics/Render。
- Data flow: 当前无生产植被数据流，只有无植被的 `VegetationBaseline.scene.json` 和 Release 2K PerfGate profile。
- Known constraints:
  - `origin/main` 不含 Terrain 数据/query API；活动 Terrain 分支没有 generic/batch surface snapshot。
  - Scene/Entity 不是任意 worker 线程可读对象；baker 不能捕获 `Scene&`。
  - `AssetDatabase` 的 immutable shared result 可复用，但不能假设 catalog 引用与 refresh 任意并发。
  - 世界实例总数不设硬上限；磁盘数据必须用整数分区坐标表达，不能依赖单精度绝对世界坐标。

## Proposal

### 1. Coordinate, identity and surface snapshot contract

Phase 2 固定作者/烘焙分区为 256 m × 256 m 的 `VegetationChunkCoord { int64 x, int64 z }`。位置以 `{chunk_coord, local_xz}` 表达，`local_xz` canonical 范围为 `[0, 256)`；负世界坐标使用 floor division，禁止截断向零。磁盘与 hash 输入使用整数 chunk/cell 坐标，只有 provider adapter 边界允许转换为 double world XZ。

为保持既有 `Scene → Asset` 依赖方向，纯sampling DTO、`IVegetationSurfaceSnapshot` 与batch validation wrapper位于 `Function/Asset/VegetationSurface.*`，不得include Scene。`Function/Scene/VegetationSurfaceProvider.*` 只定义带 `surface_entity_id` 的 `VegetationSurfaceBinding`、capture result/provider与capture validation，并单向include Asset snapshot合同；baker仍在Asset内且只消费snapshot，不知道provider、binding或Scene。

`VegetationSurfaceBinding` 保存 Scene 中的 `surface_entity_id`。provider 在逻辑线程捕获一个 `shared_ptr<const IVegetationSurfaceSnapshot>`；snapshot 必须拥有采样所需的全部不可变数据和值拷贝 transform，不得持有 `Scene&`、Editor service、Terrain working set 或 catalog 内部指针。

```cpp
enum class VegetationSurfaceStatus : uint8_t
{
    Ready,
    Pending,
    Outside,
    Failed
};

struct VegetationSurfaceIdentity
{
    std::array<uint8_t, 16> surface_id;
    uint64_t content_revision;
    uint64_t residency_revision;
    uint64_t transform_revision;
};

struct VegetationSurfaceSampleRequest
{
    VegetationChunkCoord chunk;
    glm::dvec2 local_xz;
};

struct VegetationSurfaceSample
{
    uint32_t request_index;
    VegetationSurfaceStatus status;
    double world_height_meters;
    glm::dvec3 world_normal;
    std::array<uint8_t, 8> material_slot_weights;
};

struct VegetationSurfaceBatchResult
{
    VegetationSurfaceStatus status;
    std::vector<VegetationSurfaceSample> samples;
    std::string detail;
};

struct VegetationSurfaceCaptureResult
{
    VegetationSurfaceStatus status;
    std::shared_ptr<const IVegetationSurfaceSnapshot> snapshot;
    std::string detail;
};
```

`IVegetationSurfaceProvider::capture(binding)` 按值返回 `VegetationSurfaceCaptureResult`。capture 只允许 `Ready/Pending/Failed`：`Ready` 必须携带非空 snapshot，`Pending/Failed` 必须为空，`Outside` 或任意 status/pointer shape 不匹配由统一 wrapper 转为 `Failed`。`IVegetationSurfaceSnapshot` 暴露 `identity()`、`bounds()` 与按值返回的 `sample_batch(requests, control)`；`control` 按值携带共享atomic cancel flag与absolute steady-clock deadline。const batch sampling 必须可在 worker 上调用。统一入口 `sample_vegetation_surface_batch` 捕获 provider 异常、在临时结果中验证完整 batch 后再把结果交给调用方，因此 provider 不能把部分写伪装成成功。单次 batch 最多 4096 个请求，snapshot 只采样已经 resident 的 immutable CPU 数据，禁止在调用内做磁盘 IO；生产provider合同要求单batch在50 ms内返回或观察cancel。

每个请求有独立状态：

- `Ready`：写入有限 height 和非零有限 normal；八个稳定数值材质 slot 的 R8 权重之和必须精确为 255。wrapper 用 double precision `sqrt(x*x+y*y+z*z)` 单位化 normal并保留normalized double；任一分量或长度非有限、长度≤`1e-20` 都为 `Failed`。slope 只有一个真源：`round_ties_even(acos(clamp(normalized.y, 0, 1)) * 1000)` 得到 milliradian integer，再与 species integer 范围比较；world-up/+X/world-down分别得到0/1571/1571。
- `Outside`：表示该点没有可用 surface；baker 稳定跳过该候选。
- `Pending`：需要重试，整次 chunk 不发布。
- `Failed`：持久或数据错误，整次 chunk 标 Failed，不覆盖 last-known-good。

batch 输入和输出长度必须一致，且第i项必须回传 `request_index==i`；aggregate 状态由 sample 以固定优先级 `Failed > Pending > Ready` 重算，其中只有 `Ready/Outside` samples 才产生 aggregate `Ready`，`Outside` 永不作为 aggregate。provider 声明值与重算值不一致、sample 数量/索引变化或非 `Ready` sample 携带非零 height/normal/weights时整批转 `Failed`；非 `Ready` 输出由 wrapper 规范成零值但保留已验证的request index。非法坐标、NaN/Inf、重复/变化的 identity 或 provider 异常均为 `Failed`。测试必须覆盖 `Ready+Outside`、`Ready+Pending`、`Pending+Failed`、长度/索引不匹配，以及 capture 的四种合法/非法 status-pointer shape。产品没有 provider 时 capture 返回明确的 unavailable 诊断。test provider 仅在单元测试依赖注入，不注册到 Editor 生产 bootstrap。

材质过滤只引用稳定数字 slot `0..7`，不引用显示名或数组顺序之外的隐式身份。Terrain adapter 必须原样映射 `TerrainComponentSnapshot::weights[0..7]`；Terrain 材质层 reorder/替换必须推进 surface content revision并触发重烘焙。没有材质通道的 provider 固定返回 slot 0 = 255、其余为 0；缺失或不能证明映射时返回 Failed。

Terrain adapter 后续以 immutable `TerrainAssetSnapshot`、值拷贝 world transform、asset/content/residency/transform revision 构造 snapshot；不得逐点回查 Scene 或直接索引 Terrain Editor 状态。invisible Terrain 不作为 surface，重叠 surface 必须由显式 `surface_entity_id` 消除歧义。

### 2. Asset formats

三个扩展名大小写不敏感并映射为三个独立 `AssetType`：

| Asset | v1 encoding | Canonical contents |
| --- | --- | --- |
| `.AshVegetation` | strict canonical JSON | 128-bit species ID、name、mesh/material LOD refs、bounds、placement filters、candidate density、scale/yaw/alignment、shadow/deformation/impostor metadata |
| `.AshVegetationLayer` | little-endian binary container | 128-bit layer ID、content generation、64-bit seed、tile layout、按 species ID 排序的 palette、按坐标排序的稀疏 R8 density/weight tiles |
| `.AshVegetationChunk` | little-endian binary container | cooker/schema version、layer ID、chunk input digest、chunk coord、surface identity/revisions、bounds、species table、稳定排序的量化 instance records |

#### `.AshVegetation` schema v1

Species 文件是 UTF-8、无 BOM、单个 JSON object。parser 必须拒绝重复 key、未知 key、尾随非空白、非法 UTF-8、单元素数组伪装 scalar，以及 string/number/bool coercion。v1 所有数值都使用原生 JSON integer，避免跨 CRT 浮点文本差异；范围如下：

```json
{
  "schema_version": 1,
  "species_id": "00112233445566778899aabbccddeeff",
  "name": "Meadow Grass",
  "mesh_lods": [
    {
      "mesh_asset_path": "models/grass_lod0.fbx",
      "material_asset_paths": ["materials/grass.AshMaterial"],
      "screen_error_milli": 250
    }
  ],
  "bounds_mm": { "min": [-500, 0, -500], "max": [500, 1500, 500] },
  "placement": {
    "candidates_per_cell": 8,
    "min_scale_q12": 3277,
    "max_scale_q12": 4915,
    "min_slope_milliradians": 0,
    "max_slope_milliradians": 785,
    "material_slot_min": [0,0,0,0,0,0,0,0],
    "material_slot_max": [255,255,255,255,255,255,255,255],
    "align_to_normal": true
  },
  "render": {
    "casts_shadow": true,
    "two_sided": true,
    "deformation": "Grass",
    "impostor_asset_path": "",
    "chunk_hlod_asset_path": ""
  }
}
```

`species_id` 固定为 32 个 lowercase hex且非全零；name 为 `1..256` UTF-8 bytes。所有非空资产路径为 `1..4096` UTF-8 bytes，使用 `/`、asset-root-relative、禁止 `.`/`..`/绝对路径；mesh path必须非空，每个LOD的material数组为`1..64`个非空path，impostor/HLOD path只有本阶段允许空。LOD 数为 `1..16`，`screen_error_milli`为原生`1..1000000` integer并严格递增，mesh path不重复。bounds 每分量为int32 millimeter且每轴min<max。`candidates_per_cell` 为 `1..256`；Q12 scale 表示 `value / 4096`，满足 `1..65535` 且 min≤max；slope 为 `0..1571` milliradian且 min≤max；slot 数组精确 8 个原生 `0..255` integer且 min≤max。casts/two-sided/align字段必须是原生JSON boolean；deformation 只接受 `None/Grass/Tree`。

canonical writer 固定使用上面的 root/object key 顺序、无可选字段、无额外空白，string 按 JSON UTF-8 escape，integer 用无前导零十进制，文件结尾只有一个 LF。reader 可接受不同 key 顺序和 JSON 空白，但 load→write 必须得到唯一 canonical bytes。canonical bytes 的 SHA-256（FIPS 180-4）是 species content digest；内部 SHA-256 必须通过 empty/`abc` 标准向量，不新增第三方依赖。

#### `.AshVegetationLayer` wire v1

Layer 使用顺序解析的 little-endian binary stream，禁止用 packed C++ struct 直接读写。80-byte header 精确布局：

| Offset | Type | Value |
| ---: | --- | --- |
| 0 | char[4] | `ASVL` |
| 4 | u16 | schema `1` |
| 6 | u16 | header size `80` |
| 8 | u32 | flags `0` |
| 12 | u32 | tile resolution `32` |
| 16 | u32 | tile size centimeters `3200` |
| 20 | u32 | palette count |
| 24 | u64 | content generation, nonzero |
| 32 | u64 | layer seed |
| 40 | u8[16] | nonzero layer ID |
| 56 | u32 | tile count |
| 60 | u64 | payload bytes |
| 68 | u32 | CRC32 of all bytes after header |
| 72 | u32 | header CRC32，计算时本字段视为0 |
| 76 | u32 | reserved `0` |

CRC32 使用 reflected IEEE polynomial `0xEDB88320`、initial/final xor `0xffffffff`。payload 先写 palette，再写 tiles，必须在最后一个 tile 后精确 EOF。

palette count允许`0..65534`，使density加全部species planes仍可由u16 `plane_count`表达。每个 palette record 按 species ID 严格排序：`species_id[16] + species_sha256[32] + path_bytes:u16 + reserved:u16(0) + path_utf8[path_bytes]`；path bytes为`1..4096`。path 是 canonical `.AshVegetation` 相对路径。加载/烘焙时必须以 path读取 species，并同时匹配 embedded ID 与 canonical SHA-256；同一 palette 中 path/ID重复、不同 path 声明同一 ID、或 path/ID/digest 任一不匹配都 fail closed。

每个 tile 按 `(tile_z, tile_x)` 严格排序：`tile_x:i64 + tile_z:i64 + plane_count:u16 + reserved:u16(0) + record_bytes:u32`，随后精确包含 `plane_count` 个 plane。第一个 plane必须是 density，其后 weight plane按 species ID严格排序。plane record 为 `kind:u8 + codec:u8 + reserved:u16(0) + species_id[16] + decoded_bytes:u32(1024) + encoded_bytes:u32 + decoded_crc32:u32 + bytes[encoded_bytes]`；density 的 species ID全零，weight 的 ID必须存在于 palette。kind `0=Density`、`1=SpeciesWeight`；codec `0=Raw`、`1=Rle`。RLE 是若干 `run_length:u16 + value:u8`，run length必须 `1..1024`且总和精确1024；仅当 RLE bytes严格少于1024时 writer使用RLE，平手使用Raw。全零 plane不写；没有非零 density 的 tile不写。

tile 为 32 m × 32 m、32 × 32 texel，即 1 m cell；一个 256 m chunk 对应 8 × 8 tiles。世界范围由稀疏 int64 tile coord表达，不设 tile总数硬上限；codec接受caller-provided `VegetationLoadBudget`。输入immutable byte snapshot已由caller持有；decoder先按`file_bytes/payload_bytes`准入受限parser/cursor scratch，再以无DTO ownership的preflight完整计算exact logical cost，并在任何variable-size DTO/container/string allocation前按同一byte/count预算checked拒绝。parser/token scratch只受已准入file/payload bytes约束，不计入wire-derived decoded cost；不得借此提前reserve或复制DTO数据。Editor默认预算只是可配置的resident policy而非wire/world上限。测试用极小预算机械证明超限明确失败且不产生部分对象。

#### `.AshVegetationChunk` wire v1

Chunk 的 160-byte little-endian header 精确布局：

| Offset | Type | Value |
| ---: | --- | --- |
| 0 / 4 / 6 | char[4], u16, u16 | `ASVC`, schema `1`, header size `160` |
| 8 / 12 | u32, u32 | cooker version `1`, flags `0` |
| 16 | u8[16] | layer ID |
| 32 | u8[32] | chunk input SHA-256 |
| 64 / 72 | i64, i64 | chunk x/z |
| 80 | u8[16] | surface ID |
| 96 / 104 / 112 | u64 × 3 | surface content/residency/transform revision |
| 120 / 124 | u32, u32 | species count / instance count |
| 128 / 132 | i32, i32 | min/max world height in millimeters |
| 136 | u64 | payload bytes |
| 144 | u32 | payload CRC32 |
| 148 | u32 | header CRC32，计算时本字段视为0 |
| 152 | u8[8] | reserved zero |

payload 先写与 Layer 完全相同的 species path/ID/SHA-256 record table，再写固定 28-byte instance records：`species_index:u16, cell_x:u16, cell_z:u16, candidate_ordinal:u16, cell_fraction_x_u16:u16, cell_fraction_z_u16:u16, yaw_turn_u16:u16, scale_q12:u16, normal_oct_x:i16, normal_oct_y:i16, world_height_mm:i32, reserved:u32(0)`。published Chunk 的species count为`1..min(source_palette_count,65534)`、instance count为`1..u32 max`且仍受load budget；species table必须是source Layer palette的子集、按ID严格排序、无重复且每项至少被一个record引用。每个`species_index < species_count`；cell坐标为chunk内`0..255`；candidate ordinal小于被引用species的candidates-per-cell。零实例chunk不创建object，而是在manifest中删除该coord；独立reader fixture锁定65534 species边界可表达、65535必须失败。

cell fraction 直接使用 jitter 的最高16位，表示 `value / 65536` 的严格 `[0,1)` 范围；surface request 与磁盘 record 都重建为 `cell + fraction/65536` 米，因此最后一个 cell 的任意合法 jitter 都不会溢出或出现“采样点与落盘点不同”。height用 meter×1000 的 ties-to-even并落入int32。yaw不经过浮点弧度：`yaw_turn_u16 = uint16_t(random(3) >> 48)`，其物理角度定义为 `yaw_turn_u16 / 65536` turns。scale令 `r = uint16_t(random(4) >> 48)`，以 checked u32 计算 `min_scale_q12 + round_ties_even((max_scale_q12-min_scale_q12)*r/65535)`；min==max时直接取min。

normal使用已由surface wrapper单位化的 `(x,y,z)`，其中Y为world-up。先计算 `inv_l1=1/(abs(x)+abs(y)+abs(z))` 和 `old=(x*inv_l1,z*inv_l1)`；若 `y<0`，则同时更新为 `((1-abs(old.y))*sign_not_zero(old.x), (1-abs(old.x))*sign_not_zero(old.y))`，且 `sign_not_zero(0)=+1`。每分量 clamp 到 `[-1,1]`，再 `round_ties_even(value*32767)` 到i16并把negative-zero规范为0。golden vectors固定为 world-up→`(0,0)`、+X→`(32767,0)`、+Z→`(0,32767)`、world-down→`(32767,32767)`；yaw random high16为`0x8000`时落盘32768；scale `(min,max,r)=(3277,4915,32768)`时落盘4096。任何超范围/非有限值使整个 chunk失败，禁止 clamp或饱和后伪装成功。records 使用唯一 total key `(species_id, cell_z, cell_x, candidate_ordinal)` 排序；key字段进入record，所以量化位置碰撞仍不依赖生成/线程顺序。payload后必须精确EOF。Phase 3只从 Chunk DTO转换为 `GpuDrivenInstancePageDesc`，Asset层不 include Render类型。

所有 count/size/乘法先做 checked arithmetic；未知 version、reserved非零、尾随 bytes、未排序/重复 record、非法 UTF-8、CRC、SHA或 shape错误均 fail closed。

#### Codec v1 逻辑成本与 canonical reader 澄清

`VegetationLoadCost::decoded_bytes` 是与 C++ 对象布局无关的稳定逻辑成本。v1 固定使用下列 checked 算式；空字符串贡献0，所有字符串长度均为 canonical UTF-8 byte count：

- Species：`70 + 4 * mesh_lod_count + name_bytes + Σ(mesh_path_bytes + Σ(material_path_bytes)) + impostor_path_bytes + chunk_hlod_path_bytes`。固定70 bytes由species ID 16、bounds int32×6、candidate u16、scale u16×2、slope u16×2、两个8-byte slot数组，以及align/shadow/two-sided/deformation各1 byte组成；每个LOD固定4 bytes只计`screen_error_milli`。
- Layer：`32 + Σ(48 + palette_path_bytes) + 16 * tile_count + Σ(17 + 1024)`，最后一项对每个已展开plane计一次。固定32 bytes为layer ID、generation和seed；palette固定48 bytes为ID+SHA；tile固定16 bytes为两轴坐标；plane固定17 bytes为kind+species ID，另计完整1024-byte R8值。
- Chunk：`112 + Σ(48 + species_path_bytes) + 28 * instance_count`。固定112 bytes为layer ID、input SHA、chunk coord、surface ID、三项revision和精确height extrema；instance一律按wire的28-byte逻辑record收费。

Species 的 `palette_records/tile_records/instance_records` 全为0；Layer分别为palette/tile/0；Chunk分别为species/0/instance。`file_bytes`为输入快照长度；Species `payload_bytes=file_bytes`，Layer/Chunk为header声明且经EOF核对的payload长度。所有预算均允许`actual == max`，0就是0预算。失败将DTO、cost与encode输出清空；encode先写临时buffer，全部验证成功后才发布。

Layer plane writer先合并相邻同值texel形成最大RLE runs，仅当`3 * run_count < 1024`时使用RLE，否则使用Raw。合法run count下不存在恰好1024-byte的RLE；可执行边界是341 runs = 1023 bytes必须RLE、342 runs = 1026 bytes必须Raw。reader解码并校验CRC后必须从展开值重算同一canonical编码，要求codec及encoded bytes逐字节相等；因此拒绝可更短的Raw、非最大相邻runs和其他同值不同字节表示。这样合法Layer具有唯一writer byte stream。

Standalone Chunk codec拒绝全零layer ID、input SHA或surface ID；revision允许0。instance的`scale_q12`为`1..65535`，normal oct每轴为`-32767..32767`，cell/candidate为`0..255`，fraction/yaw接受完整u16。Task 2只验证已量化wire字段；随机数到yaw/scale、浮点height/normal到整数record的派生helper与golden属于Task 8，不在Task 2新增单调用点API。

Canonical text fixture由仓库属性强制`*.AshVegetation text eol=lf`；Layer/Chunk二进制扩展强制`-text`，避免Windows checkout改写测试与后续人工资产字节。

`AssetDatabase` 为三类资产提供 immutable `shared_ptr<const T>` 的同步/异步 typed load，并复用现有 load-state/error contract。三类 enum 只追加不重排。成功缓存按 type/AssetId 保存 immutable asset、outer exact cost，并对 Layer/Chunk同时保存同一resolver snapshot内已验证的Species及其exact cost；每次cache hit必须重新独立准入outer file和每个dependency，所以warm/cold、tiny-first/generous-first结果一致。outer与dependency预算不累计。只有成功内容可缓存；Missing/Io/InvalidData和request-local WrongType/BudgetExceeded不做negative cache。dependency path/ID/digest mismatch是outer AssetId的admitted InvalidData并进入global precedence，不是request-local。in-flight key固定为type、AssetId、catalog epoch及六项完整budget；相同key可共享，不同budget隔离。Species/Layer/Chunk三个真实入口统一调用pure in-flight admission reducer，只有exact key identity返回JoinExisting，否则LaunchNew；direct reducer tests加静态map-use审查是共享的机械证据，pointer equality只证明immutable cache identity。同步和异步入口复用同一内部load pipeline，但同步入口禁止以`async().get()`实现，避免单worker自锁。

`set_root_path`、invalid-root reset与成功catalog replacement在同一state mutex下先递增vegetation epoch，再原子清空vegetation cache/in-flight/global state。refresh开始时同锁捕获`(scan_root,captured_epoch)`，锁外只扫描该root，提交时同锁通过SDD批准的single-call pure catalog publication reducer比较current root+epoch与显式scan outcome `Succeeded/InvalidRoot/Failed`。root或epoch不匹配时任何outcome都返回`DiscardStale`且零副作用；完全匹配时Succeeded返回`PublishReplacement`并递增epoch（即使catalog bytes相同），InvalidRoot返回`ResetInvalidRoot`并递增epoch、清catalog/cache/in-flight/global state，Failed返回`KeepLastKnownGood`并保留epoch/catalog/cache/state。refresh的catalog/reset提交不得绕过该reducer。by-id/by-path请求在一次临界区内值拷贝epoch、token、AssetInfo、root和catalog path index。immutable resolver snapshot不持有Impl、mutex、catalog指针或数据库back-pointer，并在单snapshot内memoize Species path load，避免Layer/Chunk验证混入不同磁盘代。旧epoch completion必须兑现私有future，但不能删除新token、发布cache或修改global state；erase要求epoch+token匹配，publish要求epoch匹配。global结果按`Loaded > InvalidData > Io > Missing`归并，同rank保留trim且slash规范为`/`后的字典序最小诊断。vegetation typed请求的admission与in-flight登记永不发布全局Loading；在outer和全部dependency六字段budget及跨资产语义尚未全部准入前，global state/error必须逐字节不变。终态completion才通过epoch/token reducer合并Loaded/InvalidData/Io/Missing，因此request-local WrongType/BudgetExceeded在初始Unloaded及并发tiny+generous场景都不会残留Loading或回滚他人状态，也不修改数据库级`get_last_error()`。Loaded结果必须asset非空/cost精确/error空；其余结果asset空/cost零/error非空，Missing使用Missing state，其他失败使用Failed，future永不throw或停在Loading。

Base worker queue把现有`worker_condition_mutex`作为唯一的condition/lifecycle-admission mutex，而不是另增一把无关锁：worker的wait predicate在该锁下读取stop与queue；enqueue在同一锁内检查shutdown/initialized并push，解锁后notify；shutdown在同一锁内先置shutting_down+stop，解锁后notify/join。固定锁序为condition/lifecycle mutex→CommandQueue内部mutex，禁止反向获取；worker的无锁外层`try_pop`只拿queue mutex。这样producer不可能插入predicate=false与原子wait之间，accepted-before-stop必drain、after-stop必reject，既禁止check/push跨worker exit也禁止lost wake。immediate execution不持condition/lifecycle锁运行用户代码。typed async直接检查`Detail::enqueue_worker_command`返回的command future；enqueue调用异常、即时拒绝、ready command-future异常和task throw全部exactly-once转换为ready Failed/Io、兑现result promise并按epoch/token reducer清理，禁止broken promise。文件不存在映射Missing，open/read或调度拒绝映射Io；worker测试除shutdown两侧外，还以已启动单worker的idle→sole command→bounded future completion证明无需后续notify。Species引用SHA来自strict decode后的canonical re-encode，不接受任意原文本bytes。

generic preview/load不得绕过vegetation reducer。AssetDatabase新增request-local bounded text snapshot API，caller必须传`max_file_bytes`，exact limit合法、zero就是zero，失败/成功均不修改asset/global state或error。bounded reader只通过非写lookup在同一临界区值拷贝root+AssetInfo，禁止复用会写Missing/last_error的legacy resolver；随后只打开一次文件并在该handle上以固定64 KiB scratch增量读入local bytes，每次append前checked，达到max后在同handle探测一个额外byte以区分exact EOF与增长/超限。最终不足64 KiB且同时到达EOF是正常成功；short read却未到EOF、badbit或其他I/O error才失败。out直到成功EOF才move发布。不得用pre-open `file_size`作为admission，也不得check后reopen；atomic path replacement不能改变已打开handle，in-place增长最多读到`max+1`即失败。SDD明确批准一个production bounded-stream helper作为机械测试seam：generic preview与typed Species/Layer/Chunk snapshot读取都是真实consumer，测试以受控stream逐步注入final-partial+EOF、short-without-EOF、exact EOF、max+1/growth和I/O failure，静态审查证明filesystem wrapper从open到EOF只持同一个stream/handle。Editor AssetDatabaseService以唯一named `kAssetTextPreviewMaxFileBytes=1 MiB`调用它；该值只是raw preview cap，不是typed resident/world budget，与Task9 typed resident/world budget无关。Species只通过该bounded path预览；原unbounded text与全部binary generic入口对三种vegetation类型request-local拒绝，Layer/Chunk compact tooltip只使用catalog metadata。三种vegetation类型禁止进入legacy generic state writer。并发typed+preview证明preview对reducer aggregate零副作用。Phase 2 不增加 render readiness；资产/烘焙状态只由 AssetDatabase 与 Editor service 报告。

### 3. Scene contract

以落地时 `main` 的 schema v6 为基数新增 Scene v7：

```cpp
struct VegetationComponent
{
    std::string layer_asset_path;
    EntityId surface_entity_id = 0;
    bool enabled = true;
};
```

Scene JSON 只保存这些引用，不保存 palette、tile、chunk 或 instance transform。`layer_asset_path` 必须非空并规范化为 asset-root-relative `.AshVegetationLayer`；`surface_entity_id` 必须非零且不能引用自己。typed add/set/remove、reflection、read/write、extraction 和独立 vegetation revision 同步实现；无参通用 Add 因无法产生合法引用而 fail closed。v3-v6 scene 继续读取且默认无 Vegetation。

本阶段 extraction 供 Editor/provider binding 和未来 Phase 3 使用，不接入 `VisibleRenderFrame` 或 `SceneRenderer`。Terrain 合入时以当时最新 schema 生成同时包含 Terrain 与 Vegetation 的新版本；禁止让两个不同含义继续共享 v6。

### 4. Authoring data, brush and patches

`VegetationLayerSnapshot` 是 immutable published state；`VegetationLayerWorkingSet` 是 service 唯一可变状态。mutation 只通过 Function/Asset 的 brush/patch API。

Brush v1 支持 Paint 与全局 Erase。持久化/算法参数全部是整数：center/path 由 `{chunk,local}` checked转换为signed world millimeter；radius `250..1,024,000 mm`；strength `1..255`；falloff `0..255`；spacing `1..2,048,000 mm`；stroke seed为u64。Paint要求非零selected species ID且必须在palette；Erase不读取selected species。相邻raw event的每轴delta不得超过`1,000,000,000 mm`且坐标转换必须落入int64，超限stroke整体失败。

End Stroke 时先把每个point以ties-to-even量化到world millimeter并移除连续重复点。每个raw delta用`gcd(abs(dx),abs(dz))`约成primitive integer direction；连续primitive direction相同的段属于一个同向共线run，不使用可能溢出的cross product。run的primitive step count与坐标累加全部checked；再以 `k_max=min(floor(1e9/abs(primitive_axis)))`（零轴忽略）从run起点按固定step重新切成canonical safe segments并追加精确终点，使每段每轴仍≤1e9。该切分只依赖run两端和primitive direction，不依赖raw event位置，因此插入/删除同向共线events得到相同canonical polyline；`(0,0),(1e9,0),...,(10e9,0)`固定切为十个1e9 segments且所有square≤2e18。

resampler立即输出首点，对每个canonical safe segment用floor integer-sqrt计算millimeter长度，沿checked u64累计弧长每隔spacing发一个dab；segment插值使用checked int64有理数ties-to-even，余量跨segment保存；若最终raw endpoint尚未输出则追加一次。golden vector `(0,0)→(2000,0), spacing=500` 固定输出`0,500,1000,1500,2000`，加入中间`(1000,0)`结果相同；上述每1e9一个event的10e9长链，与每0.5e9一个event的同路径合法输入必须得到相同canonical segments/dabs且不得溢出。

每个1 m texel的采样位置是其world-millimeter中心。令`d=floor_isqrt(dx²+dz²)`；`d>=radius`时coverage为0。`inner=floor(radius*(255-falloff)/255)`；falloff为0或`d<=inner`时coverage Q16为65535，否则为`round_ties_even((radius-d)*65535/(radius-inner))`。amount为`round_ties_even(strength*coverage/65535)`。radius=1000、strength=128、falloff=255时，distance 0/500/1000 的amount固定为128/64/0。

Paint 对density与selected species weight分别做`saturating_add(amount)`；Erase对density和当前tile全部species weight分别做`saturating_sub(amount)`，因此是明确的全局清除而不是隐式“只删选中物种”。全零weight plane和全零tile在提交时移除。所有运算只使用上述整数公式；未选物种、锁定/只读layer、provider非Ready或no-op不推进generation、不创建patch、不进入history。Phase 2 service在End时一次应用完整canonical stroke；实时增量content preview留给Terrain adapter集成后的独立设计，不能改变Brush v1最终bytes。

一次有效 stroke 产生按 `(tile_z, tile_x, plane_kind, species_id)` 排序的 `VegetationTilePatch`，保存压缩 before/after bytes。整批 patch 在 mutation 前验证 source generation、tile shape、species membership 和全部 source bytes；任一项不符则保持 working set 不变。apply/revert 各只推进一次 generation；全零 plane/tile 在提交后移除。Editor 的 `VegetationStrokeCommand` 只保存聚合 patch并通过 `RecordExecutedCommand` 接入现有 Undo/Redo；不复制整个 layer 或 cooked chunk。

Editor Panel 只绘制 palette、brush/bake/status 并提交 intent；`VegetationEditorService` 持有 working set、immutable publication、active stroke、bake future 与 last-known-good 状态。New Layer、palette Add/Replace/Remove、load/save/reload不依赖surface provider；palette edit使用与stroke相同的原子before/after patch和already-executed document command，Remove含非零weight时必须显式确认清除。provider 通过构造/装配注入，不用全局 singleton。没有 provider 时 palette/load/save可用，Paint/Erase/Bake显示精确 unavailable 原因并禁用。

#### Layer save/reload lifecycle

service 分开跟踪 `content_generation`、`persisted_generation` 与 `VegetationFileRevision { file_size, sha256 }`，状态为 `Clean / Dirty / Saving / SourceChanged / Failed`。active stroke、bake 或 Saving 期间不允许替换 session；Saving 期间暂停新的 layer mutation，避免 worker 捕获与当前身份分离。

Save 只写当前 source path，并携带打开时的 expected revision。worker只在目标同目录创建唯一stage writer，使用一个稳定native handle按 `1..1 MiB` 且offset严格连续的块写入；一次成功flush/close后token永久关闭，禁止二次写/flush，零字节artifact非法。随后有界回读同一stage byte snapshot，strict parse并核对canonical bytes/SHA，不持有目标replace能力。所有读取都接受显式byte ceiling；超限必须返回独立的 `LimitExceeded` 且bytes为空，任何非Succeeded结果都不得泄露部分snapshot。`VegetationPreparedLayerWrite` 是storage-only构造、外部只读的prepared capability：公开默认构造只能得到empty Failed，只有storage内部friend access可填充private payload；调用方只能读取，不能重绑kind、root/path/identity、stage、revision或operation serial。Editor主线程收到completion后才取得按 lowercase canonical path 的SHA-256派生的Windows named-mutex commit lease；每次prepare/commit都必须携带完整operation control（nonnull shared cancel flag + non-default absolute deadline），不完整control在lease或target mutation前fail closed。lease在等待中轮询cancel flag且不越过绝对deadline，把Cancelled/TimedOut/Failed作为null lease受控结果，禁止无限阻塞Editor。commit在lease前、acquired后、目标有界重读/revision校验后、紧邻publish前各检查一次control；任一检查失败只定向清理自己的stage。commit还必须在首次target mutation前证明prepared stage属于传入registry；伪造prepared或错误registry在publish前fail closed。lease内从目标同一byte snapshot计算revision；只有与expected相等、operation仍为当前serial且目标仍是existing regular file才允许replace-only `AtomicReplace`，目标不存在时禁止退化成create。shutdown先取消/回收worker且不消费completion，因此不存在service销毁后的迟到replace。所有目标必须经canonical/reparse-component检查证明仍在asset root内。FileOps结果有唯一legal shape：inspection/bytes/stage file/stage tree/lease的status与payload必须匹配；stage成功时path非空且writer非空、失败时二者都空，stage tree成功时root非空、失败时为空，lease Acquired时lease非空、其他状态必须为空。每个storage consumer都必须在读取或move payload前独立验证legal shape，不能信任production或scripted provider。成功replace/create-new已经消费自己的stage；`ForgetConsumedStageFile/Tree` 只是幂等registry bookkeeping，有效exact path即使entry已被并发定向清理也视为成功，不执行文件删除，且不得把已经成功的publish降级为失败。publish前失败只定向清理自己的stage；删除失败必须保留exact path供shutdown重试且操作最终为Failed，禁止RetryAll误碰同registry其他operation。成功后persisted generation和observed revision同时推进；任一步失败保留原文件、working set、history和dirty。New Layer只创建内存Dirty session并以“expected revision absent”表示目标必须不存在；首次Save在lease内用create-new/non-replacing publish，若目标在期间出现则保留对方文件并返回AlreadyExists。Save Copy As同样只接受asset root内不存在的`.AshVegetationLayer`并用create-new提交，但不重绑当前session。Chunk stage tree本身是cleanup registry的ownership unit；每个child必须是root内canonical relative path、由FileOps atomic create-new并返回同类稳定writer，禁止absolute/dot/reparse/existing child与路径隐式创建。publish消费child后无需逐文件forget，最终只定向清理剩余owned tree。

Task 7 的精确事务补充如下：prepared capability 为 move-only，copy 与 copy/move assignment 均删除，并私有绑定接受该 stage 的 cleanup registry；move构造必须在转移后显式把source重置为empty `Failed`，不能留下第二个可清理同一stage的capability。storage 在首次 `WriteBlock` 前独立验证 stage 是 normalized absolute、与 target 不同且为同目录 sibling，并比较从真实稳定handle取得的volume serial+file index；大小写别名与hard-link同一identity都在写入前fail closed，不能仅靠lowercase path。成功stage与存在的inspection必须携带identity，其他result shape必须清空。`AtomicReplace` 返回 `Replaced / TargetPreserved / RecoveryRequired`，并在同目录预留 registry-owned backup 传给 `ReplaceFileW`。紧邻调用前，registry必须把已存在且未在cleanup的exact owned stage原子pin为`Publishing`；调用后再原子解析为`TargetPreserved / Consumed / RecoveryRequired`，禁止用瞬时`OwnsStageFile`检查留下cleanup竞态。未解析的`Publishing` pin也属于fail-closed受保护恢复态：若终态转换不能确认，`AtomicReplace`可以上报该exact stage，`IsRecoveryStageFile`必须承认它，caller只能在同步调用返回后通过`ReleaseRecoveryStageFile`显式释放；普通cleanup与`RetryAll`始终排除它，且`RetryAll`必须把它列入`retained_recovery_stage_files`。成功publication若已经消费stage但留下未解析pin，Storage只在同一FileOps对exact stage返回合法`NotFound`后调用`ReconcileConsumedStageFileAfterPublish`擦除`Owned/Publishing` entry；仍存在的stage与显式`Recovery`永不由该路径删除，reconciliation失败也不得降级已完成的target mutation。若 Windows error 1177 已把原文件移到 backup，必须先恢复 target 才可返回普通失败；只有backup明确`Missing`才允许改保replacement stage，probe `Failed / Invalid`与present但恢复失败都必须继续保护backup。非1177且target不可确认时不得报告未证实存在的backup，而应保护经确认仍存在的replacement stage。post-call路径使用明确结果状态，只能报告仍受registry保护的恢复artifact，不能由异常兜底重新报告已消费或未生成的backup。consumer收到非法shape或无效recovery ownership时，也只有在exact registry仍持有prepared stage、同一有界回读成功且revision等于staged revision后才可把它转为fallback recovery；已消费/缺失stage返回受控失败，不重新制造entry。恢复失败时 exact backup（或最后可用的 replacement stage）进入 recovery-protected 集合，`RetryAll` 不得删除，并由 storage 以 `RecoveryRequired + recovery_path` 上报。上段“任一步失败保留原文件”的普通失败含义仅指 `TargetPreserved`；若 OS 已发生部分变更且自动恢复也失败，则不谎报普通失败，而是保留唯一恢复 artifact 并要求显式恢复。raw `RemoveOwnedStageFile/Tree` 只校验 operation-owned naming shape；正常删除权由 registry exact membership 控制，ownership registration 失败时不得猜测删除未认领 path。

named lease只协调遵守Vegetation container API的writer；Windows没有“比较文件hash后条件rename”的通用原子原语，拥有同等文件权限且绕过API的进程仍可在revision检查与replace之间竞争。该同权限恶意/非协作writer不在Phase 2事务保证内，Editor必须在usage/spec明确此限制；正常外部工具修改在下一次checked Save/Reload被检测。测试覆盖协作writer race、stale completion与shutdown late completion，不宣称消除任意本地攻击者TOCTOU。

Reload 先从一个 byte snapshot计算 revision并 strict parse，提交前再次核对目标 revision。当前 Dirty、active stroke/bake/save 时普通 Reload返回 `SourceChanged/DirtyConflict`且零副作用；显式 `ReloadDiscard` 必须来自确认过的 Editor intent，不能被后台自动调用。接受 clean candidate时原子替换working set/publication，并只移除同一 layer document的 Vegetation commands，不清无关Scene/Editor history。为此 `EditorCommand` 增加可选 `EditorCommandDocumentKey`，`UndoRedoService` 提供保留state-id映射的选择性删除；现有命令默认无document key。该 seam由Vegetation当前调用，且总体Terrain集成明确是第二个生产调用方，属于本文批准的跨资产history合同。

Phase 2 不做500 ms外部轮询、repair或自动冲突合并；外部变化在Save/Reload的checked revision处fail closed。自动exit中的“reload”使用Clean session，另以故障测试证明Dirty不会被静默覆盖。

Task 7 的stage identity检查同时覆盖“初检target已存在”和“初检target缺失、stage创建期间才形成alias”两种时序：已存在末级路径用一个带`OPEN_REPARSE_POINT`的稳定handle同时取得attributes、reparse判定与volume/file-index，且unavailable identity的两个数值必须同时清零。cleanup registry 对file/tree exact identity及ancestor关系在Windows上逐component做不区分大小写比较，因此case-only spelling不能绕过duplicate ownership或Recovery保护。创建stage后、首次写入前必须再次inspection同一target，并对前后target shape/identity及stage identity做fail-closed比较。若协作creator在该窗口创建了不同identity的target，则只定向清理已证明不同identity的stage，并返回`AlreadyExists`或`SourceChanged`；若stage与target identity相同，绝不把该路径登记为可删除stage。若二次target inspection失败、因而无法证明stage与target不同，则关闭writer但不猜测删除；storage仅在另一次exact-stage inspection证明该regular文件仍存在且identity等于创建handle快照，并且new-only registry插入确认该Windows path（含case-only spelling）尚无owner时，才直接登记为显式Recovery并返回`RecoveryRequired`。缺失/漂移stage或重复owner一律`Failed`，既不接管旧entry也不制造ghost。

### 5. Deterministic incremental bake

纯 Function baker 接受 immutable layer snapshot、按 species ID 排序的 species snapshots、immutable surface snapshot 和脏 chunk 集合。Editor worker 只按值捕获这些对象、cancellation token与共享 operation result，不捕获 service、Scene、selection或UI state。worker只生成/校验 immutable objects和manifest，永不切换active pointer；只有Editor主线程在身份复核后提交。

固定遍历顺序为 `(chunk_z, chunk_x) → (cell_z, cell_x) → species_id → candidate_ordinal`。counter hash v1 对以下十个 little-endian u64 words依次 fold：layer ID低/高64位、chunk x/z的two's-complement bits、cell x/z、species ID低/高64位、layer seed、candidate ordinal。算法精确定义为：

```text
splitmix64(x):
  z = x + 0x9E3779B97F4A7C15 (mod 2^64)
  z = (z xor (z >> 30)) * 0xBF58476D1CE4E5B9 (mod 2^64)
  z = (z xor (z >> 27)) * 0x94D049BB133111EB (mod 2^64)
  return z xor (z >> 31)

state = 0x6A09E667F3BCC909 xor cooker_version
for word in key_words:
  state = splitmix64(state xor splitmix64(word))
random(stream) = splitmix64(state xor
  (0xD1B54A32D192ED03 * (stream + 1) mod 2^64))
```

全零十词、cooker v1 的 state/stream0..3 test vector 为 `936cd7179cecc6f6 / b69aaf248fe5723e / c9d8c945898ec42b / 8818d088186f267b / faeb1d600eaa91b7`。第二向量使用 layer ID bytes `00..0f`、chunk `(-2,3)`、cell `(17,29)`、species ID bytes `10..1f`、seed `0123456789abcdef`、candidate `5`，结果为 `1482fb4898b68eda / dbefc5819d9be996 / da4acc7ef01435b5 / c48fc8b560bbbbe5 / 3bae788582c73257`。禁止使用 `std::random_device`、标准库distribution、帧时间或容器迭代顺序。

每个 species/cell/candidate 的 effective R8阈值为 `t=(density * species_weight + 127) / 255`。再计算u32 `accept_limit=round_ties_even(t*65536/255)`，当`uint16_t(random(0)>>48) < accept_limit`时接受；t为0/1/254/255时limit固定为0/257/65279/65536，因此R8满值必定接受、零值必定拒绝。`random(1/2)`最高16位分别除以 `65536` 生成严格 `[0,1)` cell jitter，并原样写入 instance fraction；`random(3)`最高16位直接映射turn，`random(4)`最高16位按上一节公式在species Q12 scale闭区间内插值。候选按最多4096点的batch调用surface snapshot；Ready sample按规范化normal派生integer slope并逐slot检查species min/max，Outside跳过，Pending/Failed使整个chunk不发布。records最终按唯一total key `(species_id, cell_z, cell_x, candidate_ordinal)` 排序。

#### Chunk-set transaction

Chunk发布使用 layer旁的 `<layer>.AshVegetationChunks/` content-addressed store，而不是逐目标replace：

- `objects/<chunk-sha256>.AshVegetationChunk` 是只创建不覆盖的immutable object；
- `manifests/<manifest-sha256>.asvm` 是immutable完整映射；
- 固定 `active.asva` 只包含当前manifest SHA，最终只原子替换这一个pointer。

`ASVM` v1 使用96-byte header：`magic[4]=ASVM, version:u16=1, header_size:u16=96, layer_id[16], layer_generation:u64, surface_id[16], surface_content/residency/transform:u64×3, entry_count:u32, reserved:u32(0), payload_bytes:u64, payload_crc32:u32, header_crc32:u32`。payload entries按 `(chunk_z,chunk_x)`严格排序，每项固定80 bytes：`chunk_x:i64, chunk_z:i64, chunk_object_sha256[32], chunk_input_sha256[32]`。payload CRC只覆盖全部payload bytes；header CRC覆盖完整96-byte header且计算时只把header CRC字段置零；manifest SHA-256覆盖最终header紧接payload的全部bytes。`ASVA` v1固定48 bytes：`magic[4]=ASVA, version:u16=1, header_size:u16=48, manifest_sha256[32], reserved:u32(0), crc32:u32`；CRC覆盖前44 bytes，不包含末尾CRC字段。empty manifest与单entry manifest各有固定golden byte fixture。

`chunk_input_sha256` 的preimage是唯一little-endian byte stream，顺序固定为：`magic[4]="ASVI", version:u16=1, reserved:u16=0, cooker_version:u32, tile_resolution:u32=32, tile_size_cm:u32=3200, reserved:u32=0, layer_id[16], layer_seed:u64, chunk_x:i64, chunk_z:i64, surface_id[16], surface_content:u64, surface_residency:u64, surface_transform:u64, logical_tile_count:u32=64`。随后按 `local_tile_z=0..7` 外层、`local_tile_x=0..7` 内层写64个slot：`slot_index:u8=(z*8+x), presence:u8, reserved:u16=0, record_bytes:u32, canonical_tile_record[record_bytes]`；absence精确写`presence=0, record_bytes=0`且无record bytes，present精确写`presence=1`及Layer payload中包含global tile coord的完整canonical tile record。

最后写`used_species_count:u32`，再按species ID严格排序写 `species_id[16], species_sha256[32], path_bytes:u16, reserved:u16=0, path_utf8[path_bytes]`；used集合是64个present tile全部非零species-weight planes的并集。SHA-256覆盖上述preimage全部bytes，不含全局layer generation。golden fixture使用layer ID bytes`00..0f`、seed`0123456789abcdef`、chunk`(-2,3)`、surface ID bytes`10..1f`、revisions`(4,5,6)`、64个absent slots、0 species，preimage为624 bytes，SHA-256固定为`8d7e1c07f44858323ffddb12b27daad8ded267169bdf22c7397f366a7cd7d9c3`。

Chunk header、manifest entry和重新计算的input digest必须三者相等。普通brush只重烘焙受patch影响的authoring chunk并可复用input digest未变的旧object；palette/species变更的dirty set是“当前manifest中引用该species的coords ∪ palette patch 的 before authoring 中含该species非零weight plane的coords ∪ after snapshot 中含该species非零weight plane的coords”。外部Species内容变更没有palette patch时，before与after都取当前snapshot。before证据必须在palette patch应用前按值捕获，并由working set跨Undo/Redo/Save与失败、取消、stale bake累积保留；只有匹配generation的active pointer成功提交后才可清除，防止Remove+clear把唯一的dirty证据同时删除。layer seed变更、surface ID或任一revision变化没有区域级dirty证据，full dirty universe必须精确取“当前manifest全部coords ∪ layer中所有含非零density tile、因而可能产生候选的authoring chunk coords”，禁止只遍历manifest而漏掉旧bake为空的chunk。重启后active manifest的layer generation与当前持久化Layer不同且没有完整same-session before证据时也必须使用该full dirty universe。每个dirty coord都重新计算input/candidates：新结果为空则删除entry，非空则发布新object。seed与surface测试必须各覆盖absent→present和present→absent；seed-only case还保持tiles/species/surface不变并证明非空object的input digest与object SHA都改变。

增量 bake 从当前manifest复制完整映射，只替换dirty coord的object/input digest；删除空chunk则删除entry。active读取先验证ASVA/ASVM，再对每个引用object运行完整strict ASVC codec与hash/identity检查，并通过AssetDatabase捕获的immutable resolver snapshot核对embedded Species path/ID/digest和candidate ordinal；返回的immutable entry summary保留referenced Species IDs。读取同时执行per-file和whole-set累计resident预算，任一失败不发布partial snapshot。任一dirty chunk出现Pending、Failed、cancel或非法batch会使整个prepared结果失败，禁止用已成功的前缀构造部分manifest；已经durable发布但未被active引用的content-addressed object允许成为待后续GC的orphan。worker在唯一stage目录写object/manifest并逐个同byte SHA+strict回读。所有新object和manifest先 durable publish，随后主线程取得 chunk-store canonical path 派生的同类 named commit lease，在 lease 内从磁盘重读 active ASVA及其manifest identity并与prepared所捕获的source-active identity比较，禁止用调用者缓存的current identity代替；匹配后才写/验证唯一staged ASVA并atomic replace `active.asva`。crash/故障发生在pointer切换前只暴露旧manifest，切换后所有新引用已经存在；旧objects/manifests保留到无active/reader引用后的独立GC。故障注入必须证明单chunk编辑后旧object仍以原input digest合法复用；第N个object/manifest/pointer失败和重启后只能读到完整旧代或完整新代，不能读到混合generation。

主线程只有在以下身份仍全部匹配时才允许切换 pointer：

- 当前 layer ID/content generation；
- surface ID/content/residency/transform revision；
- species ID 与 canonical asset digest；
- cooker/schema version；
- 目标 chunk coord 与 operation serial。

stale completion 只删除自己的stage，不覆盖新状态。失败保留active manifest指向的last-known-good set并标记Stale/Failed；成功切换ASVA后才推进published bake generation。相同输入连续两次、palette/dirty input乱序和刻意量化位置碰撞后必须产生byte-identical Chunk及manifest。

### 6. Failure, lifecycle and limits

- 无 provider：authoring service 可加载 layer/palette，但 brush/bake 零副作用。
- provider Pending：Editor按 `50,100,200,400,800,1000,1000,1000 ms` 最多8次重试，并受30秒operation deadline约束；超限为TimedOut，不创建history、不发布chunk。
- provider Failed/invalid：结束 operation 为 Failed，保留 working set 与 last-known-good chunk。
- provider identity/revision 在 worker 期间变化：completion 视为 stale，丢弃 stage。
- Asset load/corruption：输出保持空，`AssetLoadState::Failed` 带精确诊断；不得发布部分 snapshot。
- count/size/instance budgets 是单文件与 resident 工作预算，不是世界硬上限；达到预算时明确失败，不截断成“成功”。
- cancellation在每个batch、chunk和每个最多1 MiB的stage写块前后检查。bake task由生命周期长于`VegetationEditorService`的受控Editor executor拥有；service shutdown先置cancel并等待task acknowledgement，禁止detach或丢弃仍执行Engine/Editor代码的thread。EditorApplication shutdown在释放service、Engine DLL或worker infrastructure前join该executor全部task。provider实现必须只读resident immutable data且单batch不做IO，并按control deadline/cancel合同在50 ms内返回；throw/partial/malformed result由wrapper转Failed。fault test用cooperative slow provider在采样循环和stage写入中触发shutdown，必须证明cancel被观察、executor完成join、stage清理且无pointer切换。provider是受信任的in-process Engine扩展，C++17没有安全强杀任意卡死调用的机制；若实现违反50 ms/cancel合同，watchdog记录错误但teardown会继续等待，Phase 2不宣称对buggy/malicious provider有bounded shutdown。需要强隔离的第三方provider必须在后续设计中移到进程外。

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Function/Asset | species/layer/chunk DTO、codec、surface snapshot/batch wrapper、brush patch、deterministic baker、typed load | `Function/Asset/Vegetation*.h/.cpp`、`AssetDatabase.*` |
| Function/Scene | surface binding/provider capture、VegetationComponent、schema v7、extraction/version | `Function/Scene/VegetationSurfaceProvider.*`、`SceneComponents.h`、`Scene.h/.cpp` |
| Editor/Core | patch command、document identity与选择性history删除 | `editor/Core/VegetationCommands.*`、`EditorCommand.*`、`UndoRedoService.*`、现有 component utility |
| Editor/Services | provider-injected authoring/bake service | `editor/Services/VegetationEditorService.*` |
| Editor/Panels | Vegetation panel；只经 UIContext/Service | `editor/Panels/Vegetation/*`、最小 bootstrap 接线 |
| Tests | codec、schema、provider、brush、patch、bake、scene、service | `project/src/tests/Vegetation/*`、`tests/Scene/*`、`tests/Editor/*` |
| Docs | 当前合同与 Phase 2 验证回写 | `docs/specs/modules/{asset,scene,editor}.md`、新 vegetation feature spec、总体 SDD |

### API / contract changes

- `AssetType` 新增 Species/Layer/Chunk 三类及 typed immutable load。
- Scene schema v6 → v7，新增 asset-backed `VegetationComponent`；旧 scene 默认无该组件。
- 新增 Scene侧 `IVegetationSurfaceProvider` capture 与 Asset侧 immutable snapshot batch API；Scene只依赖Asset，Asset codec/baker不include Scene。该稳定边界同时被brush hit/capture与baker sampling两个真实调用方消费，不是test-only seam。
- 新增 patch-based authoring API和 versioned cook API；所有持久化 identity、sort、rounding 与 failure 语义写入格式测试。
- 不改变 GPUDriven、RenderGraph、Graphics 或 backend API。

### Backend impact

Phase 2 纯 CPU，不包含 Vulkan/DX12 特化。Editor/Sandbox 四组合 smoke 用于证明新增资产扫描、Scene schema 与 Editor bootstrap 不破坏两后端启动和退出；任何 backend 日志错误仍按 bug 处理。GPU validation 留到 Phase 3 首个真实渲染接入。

### Performance

- 稀疏 tile 按非零 plane 分配；零 tile 不驻留，世界总量不设硬上限。
- brush 只触及半径覆盖 tile，patch 只保存变化 bytes。
- baker 使用 batch surface sampling和脏 chunk增量提交；不允许逐 candidate 访问 Scene/AssetDatabase。
- 本阶段不设 3.33 ms runtime 目标，也不修改性能阈值。最终运行 Standard 和 VegetationFullPipeline non-bless，确认无内容基线无回归；结果只作为 Phase 2 归因证据。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| Focused unit | wire golden、SHA/CRC/hash vectors、strict shape、坐标、brush/patch/provider/bake | `RunTests.bat Debug --test-case=*Vegetation*` 与 Release 对应用例 |
| Phase 2 exit integration | test provider下 paint/erase→undo/redo→save/reload→重复/乱序cook byte-identical；无provider与失败保持LKG | Debug/Release `Vegetation Phase 2 authoring and deterministic bake exit contract` |
| Fault injection | dirty reload、source changed、provider partial/throw/permanent Pending、sort collision、object/manifest/pointer第N步失败与重启 | focused doctest cases，禁止依赖sleep |
| Full unit | 跨模块回归 | `RunTests.bat Debug`、`RunTests.bat Release` |
| Architecture | Base ← Graphics ← Function ← Editor 边界 | `RunArchGate.bat` |
| Generate/build | 新文件与四目标 | fresh `generate_vs2022.bat`；Editor/Sandbox Debug + Release |
| Readiness | 资产扫描、Scene v7、Editor bootstrap、无 provider 安全禁用 | `run.bat all Debug --smoke-test-seconds=120` |
| Render regression | 新 Scene component 不改变可见帧 | `RunRenderGate.bat`，non-bless |
| Performance | Phase 2 无内容回归 | `RunPerfGate.bat -Profile Standard`、`RunPerfGate.bat -Profile VegetationFullPipeline -Configuration Release`，non-bless |
| Plan/doc | dirty path 和验证矩阵 | `AIDevDoctor.ps1 -Mode ValidatePlan`、`git diff --check` |
| Manual Editor | 无 provider 时 palette 可编辑、brush/bake disabled 且原因明确 | Editor Vulkan/DX12 各一次；不伪装为真实 Terrain brush 验收 |

GPU/Perf/RenderGate 与其他 worktree 串行协调；CPU build/tests 仅在输出隔离时并行。任何 FAIL 按 stop rule 处理，不 bless baseline/golden。

## Task breakdown

1. 建立 Phase 2 schema/coordinate/surface contracts 的 RED tests；实现 strict DTO/codec 与 test provider。
2. 增加 AssetDatabase 三类 typed load及损坏/并发 load测试。
3. 增加 Scene v7 `VegetationComponent`、typed facade、extraction/version与 v3-v6迁移测试。
4. 实现稀疏 layer working set、确定性 stroke resampling、Paint/Erase patch及 apply/revert原子性。
5. 实现 checked layer Save/Save Copy As/Reload、document-key history失效和dirty/source-changed零副作用。
6. 实现 counter-hash/quantization、deterministic baker、Chunk codec、content-addressed manifest transaction和 stale generation/revision拒绝。
7. 实现 Editor command/service/panel；验证无provider零副作用、一次stroke一个history command和bounded cancel/shutdown。
8. 增加单一Phase 2 exit integration test，机械证明paint/erase/undo/reload/cook byte-stable与last-known-good恢复。
9. 完成 focused/full CPU门禁、双重只读审查及选择性提交。
10. 完成四组合readiness、RenderGate、Standard/VFP non-bless、人工disabled-path检查；回写长期spec并提交Phase 2。
11. Terrain稳定合入后，另起小型integration SDD/plan实现production adapter和真实地形paint/erase/undo/reload/cook exit gate。

## Risks

| Risk | Mitigation |
| --- | --- |
| Terrain 与 Vegetation 同时占用 Scene v6 | Phase 2 基于 main 使用 v7；Terrain 集成时以最新真实 schema再递增并保留两类迁移 |
| surface abstraction 变成 Terrain internal 泄漏 | 只暴露 immutable identity/bounds/batch samples；adapter拥有 Terrain snapshot而非反向 include |
| 单元素/尾随/溢出数据绕过 strict loader | 原生类型与 shape检查、checked arithmetic、CRC、canonical round-trip、fuzz-like corrupt fixtures |
| 不同遍历/线程顺序造成 cook 漂移 | 固定整数 key、counter hash、排序、量化；乱序输入/重复 cook byte comparison |
| 异步 stale worker覆盖新 layer/surface | operation serial + layer generation +完整 surface identity checked commit |
| 多文件 chunk replace 暴露混合 generation | immutable content-addressed objects/manifest；只原子切换单一ASVA pointer；fault/restart tests |
| species ID 无法定位或外部文件静默变化 | palette冻结canonical path + embedded ID + canonical SHA-256并三重校验 |
| provider永久Pending或违反cancel合同 | Pending由8次/30s约束；trusted provider接收deadline/cancel且正常50 ms内返回，worker由受控executor join且禁止detach；违反合同会被watchdog诊断但in-process teardown不宣称有界 |
| brush history复制大世界数据 | 稀疏变化 plane before/after patch；零 plane回收；一次 stroke一个 command |
| Phase 2 误接通 GPU 渲染形成半成品 | 明确不接 RenderScene/VisibleRenderFrame；Phase 3 独立 SDD 才消费 Chunk |
| 世界范围或实例数被静态容器上限截断 | int64 sparse coords；预算失败显式报告，不作为世界硬上限或成功截断 |

## Open questions

- 无 Phase 2 设计阻塞项。Terrain adapter 的最终 revision 数值组合与 Scene schema 序号以 Terrain 稳定合入时的生产合同为准；材质必须继续原样映射稳定 slot `0..7`，不能改变本 SDD 的 provider、layer 或 chunk 持久化语义。
