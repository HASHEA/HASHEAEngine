---
owner: huyizhou
last_reviewed: 2026-07-15
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
| `project/src/engine/Function/Asset/TerrainComposition.h/.cpp` | 稀疏层合成、8 路权重量化、dirty Component 发布 |
| `project/src/engine/Function/Asset/TerrainBrush.h/.cpp`、`TerrainEditPatch.cpp` | 7 个 brush、世界距离重采样、确定性 patch 与原子回放 |
| `project/src/engine/Function/Asset/TerrainSpatialData.h/.cpp` | Component min/max 层级与 LOD error |
| `project/src/engine/Function/Asset/TerrainContainer*.h/.cpp`、`TerrainBlockCodec.*` | `.AshTerrain` v1 容器、双 descriptor recovery、增量保存、优化与 RLE |
| `project/src/engine/Function/Asset/TerrainImport.*`、`TerrainRawCodec.cpp`、`TerrainPngCodecWin.cpp`、`TerrainExrCodec.cpp` | RAW/PNG/EXR 高度图导入导出 |

## 公共接口

- 标识与元数据：`AssetId`（uint64）；`AssetInfo`（id/type/name/relative_path/parent_path/is_directory/file_size/last_write_time_ticks）。
- 枚举：
  - `AssetType`：Unknown / Directory / Scene / Shader / Texture / Mesh / Model / Prefab / Material / Text / Binary / Terrain。
  - `AssetLoadState`：Unknown / Unloaded / Loading / Loaded / Missing / Failed。
- `AssetDatabase`（shared_ptr pimpl 值语义句柄）：
  - 生命周期与索引：`create(root_path)`、`is_valid`、`set_root_path` / `get_root_path`、`refresh`、`get_assets`、`find_asset_by_id` / `find_asset_by_path`。
  - 状态与错误：`get_asset_load_state(id)`、`get_asset_last_error(id)`、`get_last_error()`。
  - 同步加载（`load_*_by_id` / `load_*_by_path`，返回 bool + out 参数）：text、binary、mesh、model、material（`MaterialInterface`）、ashasset。
  - 异步加载（`load_*_by_id_async` / `load_*_by_path_async`，返回 `std::shared_future<std::shared_ptr<const T>>`）：mesh、model、material、ashasset。
  - Terrain 同步/异步加载：`load_terrain_by_{id,path}`、`load_terrain_by_{id,path}_async`，返回共享不可变 `TerrainAssetSnapshot`。Editor reload 另用 `load_terrain_candidate_by_id_async(id)` 直接验证磁盘候选；该接口不改变 cache、in-flight、load state 或 last error，遇到 container `Busy` / `SourceChanged` 时以 `retryable_failure` 标记候选，供上层保留原 publication 后重试。
  - Terrain publication：`publish_terrain_snapshot(id, snapshot)` 按 `(content_generation, residency_revision)` 字典序发布常规更新。Editor 接受隔离磁盘候选前先用 `capture_terrain_snapshot_publication(id)` 捕获绑定资产 ID、catalog generation、每资产 load serial 与当前 snapshot pointer 的 token，再以 `compare_exchange_terrain_snapshot(id, expected, desired, result)` 原子切换；任一血缘字段变化都拒绝 stale candidate，成功返回的新 token 可用于历史提交失败时的精确回滚与重试。`desired == nullptr` 仅用于受同一 token 保护的回滚/失效，`invalidate_terrain_snapshot(id)` 则显式失效一个 Terrain 的 cache/in-flight。
  - Terrain recovery/concurrency metadata：`TerrainContainerLoadReport` 与发布的 `TerrainAssetSnapshot` 同时携带 recovered flag、loaded generation、rejected generation、精确 recovery detail 和稳定的 `TerrainContainerRevision`；调用方可区分“已加载的最后有效旧代”“磁盘上更新但损坏的新代”与可重试的并发写入。
  - Terrain create/import/export：`make_default_terrain_grid_layout()` 与 `create_flat_terrain_snapshot` 提供 8193² / 32² / 256 quad / 1 m 的生产 flat 数据；`TerrainHeightImportDesc`、`TerrainHeightExportDesc`、`TerrainImportReport` 和 `TerrainCancellationToken` 是不暴露 codec 类型的值合同。`import_terrain_height` / `import_terrain_height_to_container` 支持 PNG、RAW R16/R32F、EXR；`export_terrain_height` 对最终合成、Base、指定高度层和指定材质权重层都支持这四种格式。材质权重的 PNG/RAW R16 输出使用固定 `[0,1]` normalized 映射，RAW R32F/EXR 直接保留 `[0,1]` 浮点值。`publish_staged_terrain_container_new` 对已经验证的 staged container 执行 named-lease、non-replacing 最终发布。

加载结果统一为 `std::shared_ptr<const T>` 共享不可变数据；上层（Scene 实例化、Editor AssetDatabaseService、RenderAssetManager）只应依赖上述接口。

## 约束与不变式

- 所有路径以资产根目录（运行时为 `product/assets`）为相对基准；`AssetId` 在一次索引内唯一。
- 常规异步 API 返回 `shared_future`，同一资产的并发请求共享同一份加载结果；加载状态经 `get_asset_load_state` 观察（Loading → Loaded/Failed）。candidate load 有意绕过共享 in-flight/cache/load diagnostics，必须由上层在接受后以 publication token CAS 提交；跨资产 token、目录刷新后的旧 token 或并发发布后的旧 token都必须拒绝。
- 加载产物为 const 数据，调用方不得修改；GPU 化由 render 侧另行处理。
- `.ashterrain` 扩展名大小写不敏感。实际容器损坏会缓存为 `Failed`，需精确 invalidate 后重试；worker 不可用、关停拒绝、派发异常以及 container `Busy` / `SourceChanged` 都回到可重试 `Unloaded`，不得永久停在 `Loading`。candidate load 不写这些共享状态，而是在失败 snapshot 上设置 `retryable_failure`；Editor 不得把该结果当作持久损坏或替换当前 cache。
- Terrain create/import/export API 是同步纯 CPU/文件 API；Editor 必须在自己的 worker 上调用，并只捕获 descriptor、路径、cancellation token 和不可变 snapshot 的值/共享所有权。Asset API 不持有 Editor service、panel state 或 mutable working set，也不提供 UI 线程内联 fallback。
- Create/Import 的最终 `.AshTerrain` 路径由 Editor 约束在 canonical AssetDatabase root 内，并要求 `.AshTerrain` 扩展名；提交前检查不能替代 `publish_staged_terrain_container_new` 在 commit lease 内的最终 non-replacing 检查。Import source 和高度图 Export destination 是外部工作流路径：绝对路径可以位于 root 外，相对路径以 AssetDatabase root 为基准解析，两者都不经过 `.AshTerrain` containment resolver，也不能被注册成未经 refresh 的 AssetDatabase 身份。Export 固定创建新文件且永不覆盖，不提供 overwrite 开关；调用方必须验证 parent、格式/扩展名并以唯一 stage 做 non-replacing publish，保留既有或竞态 destination。
- Editor Import 必须先让 `import_terrain_height_to_container` 发布到唯一 staged destination，再把该 stage 交给 `publish_staged_terrain_container_new`；禁止直接以最终 asset path 调用 replacing container import。失败或取消清理 stage 与其 `.import.tmp`，PNG 8-bit warning 通过 `TerrainImportReport` 原样上送。
- 双 descriptor 只在另一槽是 generation 更高但无效时报告 `RecoveredPreviousGeneration`；恢复报告先完整构造，再与 snapshot 以无抛出的 move 发布。任一失败结果必须把 snapshot 与 report 输出清空，禁止返回或缓存半成品。
- refresh、publish、candidate CAS 与 invalidate 都会推进相应 Terrain 发布血缘；catalog generation、绑定资产 ID、每资产 load serial 和 snapshot identity 共同禁止过期磁盘结果覆盖新索引或新发布快照。CAS 在所需 map 节点分配完成前不修改 serial/cache，分配失败保持原 publication 不变。
- Terrain 生产默认布局、编辑/容器/导入查询及后续 Scene/Render/Editor 消费契约见 [Terrain feature spec](../features/terrain.md)。Asset 模块只发布不可变 snapshot；Scene v6、RenderAssetManager 与 Editor Terrain Mode 已接入。
- 依赖方向：Asset 依赖 Base 与材质接口类型，不依赖 Scene/Editor。

## 验证

通用生命周期对齐 `docs/VERIFY.md` "Scene / Asset / Application 生命周期"行；Terrain 纯 CPU 逻辑对齐 "Terrain Asset / CPU logic" 行：

- `RunTests.bat Debug` + `RunTests.bat Release` + `RunArchGate.bat`，包含 PNG/RAW/EXR、8193 默认布局、内存上限、取消/临时件清理和 staged non-replacing publication 契约
- 依赖/工程变化时 fresh generate，并构建 Editor/Sandbox Debug 与 Release
- 构建 + `run.bat all Debug --smoke-test-seconds=120`（全矩阵 readiness smoke；Sandbox ready 要求标准场景引用资产已加载）
- Editor 打开默认场景操作一遍（AssetBrowser 浏览、拖放实例化）

## 历史

- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)：资产加载结果通过 render asset readiness 间接参与 smoke/capture。
- [SDD-2026-07-13-terrain-system](../../sdd/SDD-2026-07-13-terrain-system.md)：Terrain 总体设计；Phase 1–3 已接入 Asset、Render 与 Editor authoring/recovery 边界。
