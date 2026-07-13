---
owner: huyizhou
last_reviewed: 2026-07-11
status: active
---

# Module Spec: Asset

## 职责与边界

`AssetDatabase` 提供以资产根目录为范围的资产索引（扫描/查找）与按类型的同步/异步加载，及加载状态与错误查询。管磁盘资产到内存数据结构（Mesh/Model/Material/AshAsset/文本/二进制）的读取；不管 GPU 资源上传（属 render 模块 `RenderAssetManager`）、场景实例化（属 scene 模块）。

## 目录与关键文件

| 路径 | 内容 |
| --- | --- |
| `project/src/engine/Function/Asset/AssetDatabase.h/.cpp` | `AssetDatabase`、`AssetId`、`AssetType`、`AssetLoadState`、`AssetInfo`、全部加载 API |
| `project/src/engine/Function/Asset/AssetData.h/.cpp` | CPU 侧资产数据结构：`Mesh`/`MeshVertex`/`MeshSection`、`MaterialSlot`、`Model`/`ModelNode`、`AshAsset`/`AshAssetNode` |
| `project/src/engine/Function/Asset/AshAssetSerializer.cpp` | `.AshAsset` JSON 序列化/反序列化实现 |

## 公共接口

- 标识与元数据：`AssetId`（uint64）；`AssetInfo`（id/type/name/relative_path/parent_path/is_directory/file_size/last_write_time_ticks）。
- 枚举：
  - `AssetType`：Unknown / Directory / Scene / Shader / Texture / Mesh / Model / Prefab / Material / Text / Binary。
  - `AssetLoadState`：Unknown / Unloaded / Loading / Loaded / Missing / Failed。
- `AssetDatabase`（shared_ptr pimpl 值语义句柄）：
  - 生命周期与索引：`create(root_path)`、`is_valid`、`set_root_path` / `get_root_path`、`refresh`、`get_assets`、`find_asset_by_id` / `find_asset_by_path`。
  - 状态与错误：`get_asset_load_state(id)`、`get_asset_last_error(id)`、`get_last_error()`。
  - 同步加载（`load_*_by_id` / `load_*_by_path`，返回 bool + out 参数）：text、binary、mesh、model、material（`MaterialInterface`）、ashasset。
  - 异步加载（`load_*_by_id_async` / `load_*_by_path_async`，返回 `std::shared_future<std::shared_ptr<const T>>`）：mesh、model、material、ashasset。

加载结果统一为 `std::shared_ptr<const T>` 共享不可变数据；上层（Scene 实例化、Editor AssetDatabaseService、RenderAssetManager）只应依赖上述接口。

## 约束与不变式

- 所有路径以资产根目录（运行时为 `product/assets`）为相对基准；`AssetId` 在一次索引内唯一。
- 异步 API 返回 `shared_future`，同一资产的并发请求共享同一份加载结果；加载状态经 `get_asset_load_state` 观察（Loading → Loaded/Failed）。
- 加载产物为 const 数据，调用方不得修改；GPU 化由 render 侧另行处理。
- 依赖方向：Asset 依赖 Base 与材质接口类型，不依赖 Scene/Editor。

## 验证

对齐 `docs/VERIFY.md` "Scene / Asset / Application 生命周期"行：

- 构建 + `run.bat all Debug --smoke-test-seconds=120`（全矩阵 readiness smoke；Sandbox ready 要求标准场景引用资产已加载）
- Editor 打开默认场景操作一遍（AssetBrowser 浏览、拖放实例化）

## 历史

- [SDD-2026-07-11-readiness-driven-automation](../../sdd/SDD-2026-07-11-readiness-driven-automation.md)：资产加载结果通过 render asset readiness 间接参与 smoke/capture。
