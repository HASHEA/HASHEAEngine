---
owner: huyizhou
last_reviewed: 2026-07-14
status: active
---

# Module Spec: Asset

## 职责与边界

`AssetDatabase` 提供以资产根目录为范围的资产索引（扫描/查找）与按类型的同步/异步加载，及加载状态与错误查询。管磁盘资产到内存数据结构（Mesh/Model/Material/AshAsset/Terrain/文本/二进制）的读取；Terrain 的纯 CPU 数据、编辑、容器与高度图 IO 也属于本模块。不管 GPU 资源上传（属 render 模块 `RenderAssetManager`）、场景实例化（属 scene 模块）或 Editor 交互。

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
  - Terrain 同步/异步加载：`load_terrain_by_{id,path}`、`load_terrain_by_{id,path}_async`，返回共享不可变 `TerrainAssetSnapshot`。
  - Terrain publication：`publish_terrain_snapshot(id, snapshot)` 按 `(content_generation, residency_revision)` 字典序发布更新；`invalidate_terrain_snapshot(id)` 只失效一个 Terrain 的 cache/in-flight。

加载结果统一为 `std::shared_ptr<const T>` 共享不可变数据；上层（Scene 实例化、Editor AssetDatabaseService、RenderAssetManager）只应依赖上述接口。

## 约束与不变式

- 所有路径以资产根目录（运行时为 `product/assets`）为相对基准；`AssetId` 在一次索引内唯一。
- 异步 API 返回 `shared_future`，同一资产的并发请求共享同一份加载结果；加载状态经 `get_asset_load_state` 观察（Loading → Loaded/Failed）。
- 加载产物为 const 数据，调用方不得修改；GPU 化由 render 侧另行处理。
- `.ashterrain` 扩展名大小写不敏感。实际容器损坏会缓存为 `Failed`，需精确 invalidate 后重试；worker 不可用、关停拒绝或派发异常回到可重试 `Unloaded`，不得永久停在 `Loading`。
- refresh、publish 与 invalidate 都会使旧 Terrain worker 失效；catalog generation 和每资产 load serial 禁止过期磁盘结果覆盖新索引或新发布快照。
- Terrain 生产默认布局、编辑/容器/导入查询契约见 [Terrain Phase 1 Asset Core](../features/terrain.md)。Phase 1 尚未接入 RenderAssetManager、Scene TerrainComponent 或 Editor。
- 依赖方向：Asset 依赖 Base 与材质接口类型，不依赖 Scene/Editor。

## 验证

通用生命周期对齐 `docs/VERIFY.md` "Scene / Asset / Application 生命周期"行；Terrain 纯 CPU 逻辑对齐 "Terrain Asset / CPU logic" 行：

- `RunTests.bat Debug` + `RunTests.bat Release` + `RunArchGate.bat`
- 依赖/工程变化时 fresh generate，并构建 Editor/Sandbox Debug 与 Release
- 构建 + `run.bat all Debug --smoke-test-seconds=120`（全矩阵 readiness smoke；Sandbox ready 要求标准场景引用资产已加载）
- Editor 打开默认场景操作一遍（AssetBrowser 浏览、拖放实例化）

## 历史

- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)：资产加载结果通过 render asset readiness 间接参与 smoke/capture。
- [SDD-2026-07-13-terrain-system](../../sdd/SDD-2026-07-13-terrain-system.md)：Terrain 总体设计；当前仅 Phase 1 Asset Core 已实现。
