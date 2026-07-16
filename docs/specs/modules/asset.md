---
owner: huyizhou
last_reviewed: 2026-07-26
status: active
---

# Module Spec: Asset

## 职责与边界

`AssetDatabase` 提供以资产根目录为范围的资产索引（扫描/查找）与按类型的同步/异步加载，及加载状态与错误查询。管磁盘资产到内存数据结构（Mesh/Model/Material/AshAsset/Terrain/文本/二进制）的读取；Terrain 的纯 CPU 数据、编辑、容器与高度图 IO 也属于本模块。不管 GPU 资源上传（由 render 模块 `TerrainRenderAsset` / `RenderAssetManager` 消费不可变 snapshot）、场景实例化（Scene v6 `TerrainComponent`）或 Editor 交互。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `project/src/engine/Function/Asset/AssetDatabase.h/.cpp` | `AssetDatabase`、`AssetId`、`AssetType`、`AssetLoadState`、`AssetInfo`、全部加载 API |
| `project/src/engine/Function/Asset/AssetData.h/.cpp` | CPU 侧资产数据结构：`Mesh`/`MeshVertex`/`MeshSection`、`MaterialSlot`、`Model`/`ModelNode`、`AshAsset`/`AshAssetNode` |
| `project/src/engine/Function/Asset/AshAssetSerializer.cpp` | `.AshAsset` JSON 序列化/反序列化实现 |
| `project/src/engine/Function/Asset/TerrainData.h/.cpp` | Terrain 布局、全局 sample ownership、不可变 snapshot 与可变 working set |
| `project/src/engine/Function/Asset/TerrainComposition.h/.cpp` | 仿射稀疏层合成、8 路权重量化、dirty Component 发布 |
| `project/src/engine/Function/Asset/TerrainBrush.h/.cpp`、`TerrainEditPatch.cpp` | 7 个 brush、可续接世界距离重采样、仿射 patch 合并与原子回放 |
| `project/src/engine/Function/Asset/TerrainSpatialData.h/.cpp` | Component min/max 层级与 LOD error |
| `project/src/engine/Function/Asset/TerrainContainer*.h/.cpp`、`TerrainBlockCodec.*` | `.AshTerrain` v2 writer / v1-v2 reader、双 descriptor recovery、增量保存、优化与 RLE |
| `project/src/engine/Function/Asset/TerrainImport.*`、`TerrainRawCodec.cpp`、`TerrainPngCodecWin.cpp`、`TerrainExrCodec.cpp` | RAW/PNG/EXR 高度图导入导出 |
| `project/src/engine/Function/Asset/VegetationTypes.h` | 植被稳定 ID、SHA-256、int64 chunk 坐标与显式加载预算/成本 DTO |
| `project/src/engine/Function/Asset/VegetationCodec.h/.cpp` | 植被 SHA-256、CRC32、ties-even u16 舍入与负坐标 floor 分块基础合同 |
| `project/src/engine/Function/Asset/VegetationSurface.h/.cpp` | immutable surface snapshot、批采样 DTO 与 fail-closed validation wrapper |
| `project/src/engine/Function/Asset/VegetationSpecies.h/.cpp` | strict canonical `.AshVegetation` v1 DTO 与 JSON codec |
| `project/src/engine/Function/Asset/VegetationLayer.h/.cpp` | 稀疏 ASVL v1 palette/tile/plane DTO 与顺序二进制 codec |
| `project/src/engine/Function/Asset/VegetationChunk.h/.cpp` | 烘焙 ASVC v1 species/量化 instance DTO 与顺序二进制 codec |

## 公共接口

- 标识与元数据：`AssetId`（uint64）；`AssetInfo`（id/type/name/relative_path/parent_path/is_directory/file_size/last_write_time_ticks）。
- 枚举：
  - `AssetType`：Unknown / Directory / Scene / Shader / Texture / Mesh / Model / Prefab / Material / Text / Binary / Terrain / Species / Layer / Chunk。
  - `AssetLoadState`：Unknown / Unloaded / Loading / Loaded / Missing / Failed。
- `AssetDatabase`（shared_ptr pimpl 值语义句柄）：
  - 生命周期与索引：`create(root_path)`、`is_valid`、`set_root_path` / `get_root_path`、`refresh`、`get_assets`、`find_asset_by_id` / `find_asset_by_path`。
  - 状态与错误：`get_asset_load_state(id)`、`get_asset_last_error(id)`、`get_last_error()`。
  - 同步加载（`load_*_by_id` / `load_*_by_path`，返回 bool + out 参数）：text、binary、mesh、model、material（`MaterialInterface`）、ashasset。
  - 异步加载（`load_*_by_id_async` / `load_*_by_path_async`，返回 `std::shared_future<std::shared_ptr<const T>>`）：mesh、model、material、ashasset。
  - Terrain 同步/异步加载：`load_terrain_by_{id,path}`、`load_terrain_by_{id,path}_async`，返回共享不可变 `TerrainAssetSnapshot`。Editor reload 另用 `load_terrain_candidate_by_id_async(id)` 直接验证磁盘候选；该接口不改变 cache、in-flight、load state 或 last error，遇到 container `Busy` / `SourceChanged` 时以 `retryable_failure` 标记候选，供上层保留原 publication 后重试。
  - Terrain publication：`publish_terrain_snapshot(id, snapshot)` 按 `(content_generation, residency_revision)` 字典序发布常规更新。Editor 接受隔离磁盘候选前先用 `capture_terrain_snapshot_publication(id)` 捕获绑定资产 ID、catalog generation、每资产 load serial 与当前 snapshot pointer 的 token，再以 `compare_exchange_terrain_snapshot(id, expected, desired, result)` 原子切换；任一血缘字段变化都拒绝 stale candidate，成功返回的新 token 可用于历史提交失败时的精确回滚与重试。`desired == nullptr` 仅用于受同一 token 保护的回滚/失效，`invalidate_terrain_snapshot(id)` 则显式失效一个 Terrain 的 cache/in-flight。
  - Terrain recovery/concurrency metadata：`TerrainContainerLoadReport` 与发布的 `TerrainAssetSnapshot` 同时携带 recovered flag、loaded generation、rejected generation、精确 recovery detail 和稳定的 `TerrainContainerRevision`；调用方可区分“已加载的最后有效旧代”“磁盘上更新但损坏的新代”与可重试的并发写入。
  - Terrain create/import/export：`normalize_terrain_authoring_extent_meters()` 与 `make_terrain_authoring_grid_layout()` 定义每轴 256–8192 m、最近 2 的幂且中点向上的 authoring 合同，默认 2048 × 2048 m；固定 256 quad/Component 与 1 m/sample。`make_default_terrain_grid_layout()` 为兼容既有调用和最大压力测试继续返回 8193² / 32² 的历史 full-pressure 布局。`TerrainHeightImportDesc`、`TerrainHeightExportDesc`、`TerrainImportReport` 和 `TerrainCancellationToken` 是不暴露 codec 类型的值合同。`import_terrain_height` / `import_terrain_height_to_container` 支持 PNG、RAW R16/R32F、EXR；`export_terrain_height` 对最终合成、Base、指定高度层和指定材质权重层都支持这四种格式。材质权重的 PNG/RAW R16 输出使用固定 `[0,1]` normalized 映射，RAW R32F/EXR 直接保留 `[0,1]` 浮点值。`publish_staged_terrain_container_new` 对已经验证的 staged container 执行 named-lease、non-replacing 最终发布。
  - Terrain authoring primitive：每个高度稀疏 sample 保存仿射变换 `a × H + b`；同一编辑层按 stroke 顺序支持全部五种高度工具。`append_resampled_terrain_stroke` 续接上次 segment，只返回新增 dab；`merge_terrain_edit_patches` 把同一次 stroke 的 patch 聚合为首次 before 到最新 after，供一次历史提交或整体回滚。
- 植被基础合同：`VegetationId` / `VegetationSha256`、`VegetationChunkCoord`、显式 `VegetationLoadBudget/VegetationLoadCost`；`vegetation_sha256`、`vegetation_crc32`、`vegetation_round_ties_even_u16` 与 `split_vegetation_world_xz`。
- surface snapshot：`IVegetationSurfaceSnapshot` 提供 immutable identity、coarse chunk bounds 与 resident-only batch sampling；`sample_vegetation_surface_batch` 统一验证请求、identity 前后稳定性、normal/材质权重、aggregate status、取消和绝对 deadline，失败不发布部分结果。
- Phase 2 植被资产 codec：`decode/encode_vegetation_species`、`decode/encode_vegetation_layer`、`decode/encode_vegetation_chunk`。decoder 要求 caller 显式传入 `VegetationLoadBudget`，只在 strict shape、CRC、canonical ordering 与预算全部通过后一次性发布 DTO 和精确 `VegetationLoadCost`；encoder 同样使用临时 byte stream，失败清空输出。

加载结果统一为 `std::shared_ptr<const T>` 共享不可变数据；上层（Scene 实例化、Editor AssetDatabaseService、RenderAssetManager）只应依赖上述接口。

## 约束与不变式

- 所有路径以资产根目录（运行时为 `product/assets`）为相对基准；`AssetId` 在一次索引内唯一。
- 常规异步 API 返回 `shared_future`，同一资产的并发请求共享同一份加载结果；加载状态经 `get_asset_load_state` 观察（Loading → Loaded/Failed）。candidate load 有意绕过共享 in-flight/cache/load diagnostics，必须由上层在接受后以 publication token CAS 提交；跨资产 token、目录刷新后的旧 token 或并发发布后的旧 token都必须拒绝。
- 加载产物为 const 数据，调用方不得修改；GPU 化由 render 侧另行处理。
- `make_terrain_working_set` 必须共享 source snapshot 的不可变 Base R16 allocation；`publish_terrain_working_set` 也必须把同一 allocation 传给新 snapshot。可变 authoring 状态只存在于独立 edit-layer stack、dirty set 和新建的 dirty Component；最大 8193² Base 不得因打开 session 或 preview generation 被深拷贝。
- `.ashterrain` 扩展名大小写不敏感。实际容器损坏会缓存为 `Failed`，需精确 invalidate 后重试；worker 不可用、关停拒绝、派发异常以及 container `Busy` / `SourceChanged` 都回到可重试 `Unloaded`，不得永久停在 `Loading`。candidate load 不写这些共享状态，而是在失败 snapshot 上设置 `retryable_failure`；Editor 不得把该结果当作持久损坏或替换当前 cache。
- `.AshTerrain` 当前 writer 固定写 version 2 仿射高度 block；reader 同时接受 version 1 和 2。v1 Additive sample 精确迁移为 `a=1, b=value×coverage`，v1 Alpha sample 迁移为 `a=1-coverage, b=coverage×value`；加载本身不改写磁盘，下一次 Save/Optimize 才写 v2。
- Terrain create/import/export API 是同步纯 CPU/文件 API；Editor 必须在自己的 worker 上调用，并只捕获 descriptor、路径、cancellation token 和不可变 snapshot 的值/共享所有权。Asset API 不持有 Editor service、panel state 或 mutable working set，也不提供 UI 线程内联 fallback。
- Create/Import 的最终 `.AshTerrain` 路径由 Editor 约束在 canonical AssetDatabase root 内，并要求 `.AshTerrain` 扩展名；提交前检查不能替代 `publish_staged_terrain_container_new` 在 commit lease 内的最终 non-replacing 检查。Import source 和高度图 Export destination 是外部工作流路径：绝对路径可以位于 root 外，相对路径以 AssetDatabase root 为基准解析，两者都不经过 `.AshTerrain` containment resolver，也不能被注册成未经 refresh 的 AssetDatabase 身份。Export 固定创建新文件且永不覆盖，不提供 overwrite 开关；调用方必须验证 parent、格式/扩展名并以唯一 stage 做 non-replacing publish，保留既有或竞态 destination。
- Editor Import 必须先让 `import_terrain_height_to_container` 发布到唯一 staged destination，再把该 stage 交给 `publish_staged_terrain_container_new`；禁止直接以最终 asset path 调用 replacing container import。失败或取消清理 stage 与其 `.import.tmp`，PNG 8-bit warning 通过 `TerrainImportReport` 原样上送。
- 双 descriptor 只在另一槽是 generation 更高但无效时报告 `RecoveredPreviousGeneration`；恢复报告先完整构造，再与 snapshot 以无抛出的 move 发布。任一失败结果必须把 snapshot 与 report 输出清空，禁止返回或缓存半成品。
- refresh、publish、candidate CAS 与 invalidate 都会推进相应 Terrain 发布血缘；catalog generation、绑定资产 ID、每资产 load serial 和 snapshot identity 共同禁止过期磁盘结果覆盖新索引或新发布快照。CAS 在所需 map 节点分配完成前不修改 serial/cache，分配失败保持原 publication 不变。
- Terrain authoring 默认、历史 full-pressure 布局、编辑/容器/导入查询及后续 Scene/Render/Editor 消费契约见 [Terrain feature spec](../features/terrain.md)。Asset 模块只发布不可变 snapshot；Scene v6、RenderAssetManager 与 Editor Terrain Mode 已接入。
- 依赖方向：Asset 依赖 Base 与材质接口类型，不依赖 Scene/Editor。
- 植被世界坐标按 256 m chunk 使用 floor division；local XZ 必须在 `[0,256)`。surface batch 请求数固定为 `1..4096`，coarse bounds 不代替逐点 `Outside`，Asset surface 合同不得 include Scene/Terrain/Render/Graphics。
- surface Ready sample 的材质 slot `0..7` 权重和必须为 255；normal 以 double 归一化，slope 使用共享的 ties-even 毫弧度规则。Pending/Failed/异常/identity 变化、取消或超时均 fail closed。
- `.AshVegetation` 是 UTF-8 无 BOM 的 canonical compact JSON v1，拒绝重复/未知/missing key、scalar coercion、非法 ID/path/range，唯一末尾为 LF。ASVL/ASVC 都逐字段 little-endian 解析，禁止 packed struct；header/payload CRC、reserved、排序、shape 与 exact EOF 任一不符均失败。
- Layer plane writer 使用最大合并相邻同值 run；仅当 `3*run_count < 1024` 时选 RLE，否则选 Raw。reader 展开并核对 decoded CRC 后重算 canonical encoding，拒绝非最大 RLE 或可换成更短 codec 的 byte stream。
- `VegetationLoadCost` 收费是 wire-derived 固定合同：Species=`70 + 4*LOD + 全部 canonical UTF-8 bytes`；Layer=`32 + Σ(48+palette path) + 16*tile + Σ(17+1024 per expanded plane)`；Chunk=`112 + Σ(48+species path) + 28*instance`。实际值等于预算上限合法；零预算不是 unlimited。任意 decode 失败将 DTO/cost 归零。
- codec 在建立 DTO ownership 前完成 exact-cost admission：ASVL/ASVC 第一遍只用 bounds-owning byte/string view 与已由 file/payload/count budget 准入的 bounded non-owning view/index scratch 验证完整 wire/CRC/canonical 合同并计算成本，第二遍才 reserve/copy；Species 的 duplicate-key/DOM parser scratch 先受 file/payload budget 限制，再从纯 JSON view 计算 exact decoded cost，准入后才复制到 DTO，发布前还必须与 DTO 重算成本逐字段一致。

## 验证

通用生命周期对齐 `docs/VERIFY.md` "Scene / Asset / Application 生命周期"行；Terrain 纯 CPU 逻辑对齐 "Terrain Asset / CPU logic" 行：

- `RunTests.bat Debug` + `RunTests.bat Release` + `RunArchGate.bat`，包含 PNG/RAW/EXR、256×8192 / 2048×4096 / 2048×2048 / 8192×8192 target matrix、显式 8193² full-pressure fixture、内存上限、取消/临时件清理和 staged non-replacing publication 契约
- 依赖/工程变化时 fresh generate，并构建 Editor/Sandbox Debug 与 Release
- 构建 + `run.bat all Debug --smoke-test-seconds=120`（全矩阵 readiness smoke；Sandbox ready 要求标准场景引用资产已加载）
- Editor 打开默认场景操作一遍（AssetBrowser 浏览、拖放实例化）
- 植被纯合同改动先跑 `Vegetation core*` / `Vegetation surface*` / `Vegetation Species*` / `Vegetation Layer codec*` / `Vegetation Chunk codec*` Debug+Release focused tests、全量 `RunTests.bat` 与 `RunArchGate.bat`。

## 历史

- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)：资产加载结果通过 render asset readiness 间接参与 smoke/capture。
- [SDD-2026-07-13-terrain-system](../../sdd/SDD-2026-07-13-terrain-system.md)：Terrain 总体设计；Phase 1–3 已接入 Asset、Render 与 Editor authoring/recovery 边界。
- [SDD-2026-07-15-terrain-interactive-authoring-workflow](../../sdd/SDD-2026-07-15-terrain-interactive-authoring-workflow.md)：统一仿射编辑层、v1 精确迁移、增量 stroke patch 与默认编辑工作流。
- [SDD-2026-07-16-vegetation-authoring-and-bake](../../sdd/SDD-2026-07-16-vegetation-authoring-and-bake.md)：植被稳定类型、immutable surface snapshot 与 Phase 2 authoring/bake 合同。
