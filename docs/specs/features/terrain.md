---
owner: huyizhou
last_reviewed: 2026-07-14
status: active
---

# Feature Spec: Terrain Asset Core and Scene Contract

## 当前范围

Phase 1 提供 Terrain 的纯 CPU 资产核心：网格与分块数据、不可变快照、非破坏编辑层、笔刷与 patch 回放、空间查询、`.AshTerrain` 容器、RAW/PNG/EXR 高度图导入导出，以及 `AssetDatabase` 发布与失效接口。Phase 2 已完成四个基础 slice：Scene v6 `TerrainComponent`、独立 revision/extraction 和 world-space CPU query adapter；Function-only 原生 2D texture-array wrapper；`TerrainRenderAsset` 的 content-generation 状态、不可变 Component pointer diff、GPU 资源 ownership 和 `RenderAssetManager` readiness/activity 接入；以及 `RenderTerrainProxy`、`VisibleRenderFrame::terrains` 与 ScenePresentation 的独立 terrain revision 同步。Terrain 尚未生成 LOD batch 或实际 draw，也未进入 Editor 工作流。

生产默认布局为 8193 × 8193 个高度采样、8192 × 8192 个 quad、32 × 32 个 Component，每个 Component 为 256 × 256 quad、257 × 257 个快照采样，默认间距 1 m。纯 CPU API 也接受满足 `sample_count = component_count × component_quad_count + 1` 且采样间距为有限正数的小布局，供测试和工具使用；这不改变生产默认布局。

## 数据与所有权

- `TerrainAssetSnapshot` 是发布给读取方的不可变资产快照。Base 高度、编辑层和 Component 通过 const 共享对象暴露；发布后不得修改。
- `TerrainWorkingSet` 是从快照深验证后创建的受信可变编辑状态。编辑只通过 Terrain brush、patch 和 compose/publication API 完成。
- 全局 sample coordinate 是持久化高度与权重的唯一真源。Component 内部边界样本只有一个 owner；依赖同一边界的全部 Component 都会进入 dirty 集合。
- Base 高度以 R16 配合 `height_offset` / `height_range` 持久化，工作集、合成与查询使用 float32。
- 全地形固定 8 个材质层。最终权重为 8 路 uint8，`quantize_terrain_weights` 保证每个 sample 的总和精确为 255；未绘制区域隐式为 Layer 0 = 255。
- `TerrainEditLayer` 具有稳定 16-byte ID、名称、可见性、强度和 Additive/Alpha 高度混合模式。高度与权重分别保存按 owner Component 划分的 canonical 稀疏 block。

每次非空内容修改只推进一次 `content_generation`。`dirty_components` 按 z/x 排序去重；边界修改按共享 sample 规则扩张。`compose_terrain_components` 只重建请求的 Component，`publish_terrain_working_set` 要求一次提交完整的当前 generation dirty 集合，成功后原子替换对应 const Component 指针并清空 dirty 集合；失败时工作集和旧快照保持不变，未修改 Component 继续共享旧指针。

## 笔刷与 undo/redo 逻辑

当前工具为 Raise、Lower、Smooth、Flatten、Noise、Paint、Erase。公共参数使用世界空间半径、强度、falloff、stroke spacing、选中层、材质层和确定性 seed；`TerrainBrushMetric` 把 terrain-local XZ 映射到世界米，因此正数非均匀缩放下仍保持世界空间圆形影响。

`resample_terrain_stroke` 和 `apply_terrain_brush_stroke` 按世界距离确定性重采样，同一几何路径不依赖输入点密度、帧率或 frame index。一次 stroke 在首次 mutation 前冻结 Base 与选中层及以下可见层，随后按 dab 顺序修改 canonical block。Noise 只依赖完整 64-bit seed 与全局 sample coordinate。

一次 stroke 输出按 owner/domain 排序的 `TerrainEditPatch`。patch 分别记录 before/after 字节，可选择原始或确定性 RLE；`apply_terrain_edit_patches` 在写入前解码并验证整批 patch、当前 source bytes、目标层/owner/domain/rect 与 generation。任一 patch 无效时整批原子失败；成功时 generation 只推进一次，并重建完整 halo dirty 集合。该纯逻辑接口是后续 Editor 命令接入 undo/redo 的基础，但 Phase 1 尚未提供 Editor 命令或 UI。

## CPU 空间查询

`TerrainQuery.h/.cpp` 直接查询一个 `TerrainAssetSnapshot`，坐标均为 terrain-local：

- `query_height`：双线性采样高度。
- `query_normal`：用相邻高度计算法线。
- `ray_cast_terrain`：先遍历每个 Component 的 min/max 层级，再对叶节点中的真实三角形做精确相交；不使用固定步进。
- `prefetch_query_region`：以非阻塞方式启动/观察 `AssetDatabase` Terrain 加载。

返回状态为 `Ready`、`Pending`、`Outside` 或 `Failed`。缺少相交路径上所需 Component 时返回 `Pending`；非法或损坏数据返回 `Failed`。当前 prefetch 的粒度是整个 Terrain 资产，`sample_region` 只做范围验证，尚未实现 Component 流送。

`SceneQuery.h/.cpp` 在这些 snapshot-local overload 之上提供 Scene world-space adapter。指定 Entity 的高度/法线查询和全 Scene Terrain 射线查询通过 `AssetDatabase` 的共享异步 future 解析资产；Pending/Outside/Failed 时不写输出。adapter 只接受完整 transform chain 上的有限平移、零旋转和有限正缩放；高度按 Y 缩放，法线按 inverse-transpose 缩放并归一化，非均匀缩放下的射线命中距离换算回 world meters。

## Scene v6 contract

- `TerrainComponent` 保存非空 Terrain 资产路径、可见性、投射/接收阴影标志和固定 8 个材质层覆盖位。
- Scene schema version 6 序列化 Terrain；v3-v5 场景继续迁移且默认不含 Terrain。非法 Terrain JSON 或非法 transform 使 load 失败并返回诊断，不产生部分有效场景。
- add/set/reparent 与父级 transform 修改都维护完整 world transform 不变量；非法操作保留既有组件、层级和 revision。
- Terrain 组件内容有独立 `render_terrain_version`，transform 变化只更新 transform revision。`extract_terrain_entities` 发布组件值和 world matrix，后续 Phase 2 rendering slice 消费该接口。
- 参数为空的通用 Add facade 不显示/创建 Terrain，因为默认空资产路径非法；创建工具必须用 typed facade 携带已选资产。通用反射 read/write/remove 完整支持 Terrain。

## Render asset contract

- `TerrainRenderAssetState` 只接受单调递增的 `content_generation`，使用 1024-bit 固定 Component completion mask；同代或旧代结果不能清除 Failed，新 generation 完整成功后才发布 Ready。
- 首次接受 snapshot 时为全部 resident Component 生成私有 GPU payload；后续 snapshot 只处理 `shared_ptr` 发生变化的 Component。`resident -> null` 作为 removal 参与同一 generation completion，并释放帧边界 atlas slot 元数据。任何失败 snapshot 也占用其 generation，禁止旧 generation 倒灌；失败后的新 generation 强制让全部 1024 个坐标参与重建（resident 重传、null retirement），避免复用指针掩盖未完成上传。
- 每个 257 × 257 高度 tile 按偶数 sample 在低 16 bit、奇数 sample 在高 16 bit 打包为 33025 个 `uint32_t`，末尾 padding lane 固定为 0。显式 8 路权重必须逐 sample 精确和为 255并原序拆入两路 RGBA8；空权重物化为 Layer 0 = 255。
- GPU ownership 包含全 Terrain packed-height `StorageBuffer`、dirty-weight staging `StorageBuffer`、两张 4144 × 4144 RGBA8 UAV/SRV atlas、1025 × 1025 coarse target、三张 1024 × 1024 × 8 fallback material arrays 及 256-slot 帧边界 metadata。真实材质层 texture cook/填充留给后续渲染 slice。
- `RenderAssetManager::request_terrain_asset/finalize_pending_terrain_asset` 按规范化资产 key 只登记一次 pending owner；Ready/Failed 只结算一次，并把 Terrain failed key 与 activity epoch 合入通用 readiness。GPU finalize 必须运行在 render thread。
- `RenderTerrainProxy` 固定引用一个不可变 Terrain snapshot generation 和对应 `TerrainRenderAsset`，从完整 Terrain 本地范围与 world transform 计算保守 AABB。transform-only 更新复制 proxy 后原子替换，不修改已发布帧；内容 revision 则重新解析 snapshot，并复用同一路径的 render asset cache 对象。
- `RenderScene::rebuild_terrains_from_scene` 消费 Scene extraction 并原子替换 terrain proxy 集合；`build_visible_render_frame` 对其执行 frustum AABB 裁剪，输出 `VisibleTerrainFrame`。`ScenePresentationSubsystem` 独立跟踪 `render_terrain_version`，且 topology rebuild 必须先于同帧 transform update，避免已删除实体的旧 proxy 造成伪失败。

## `.AshTerrain` 容器

`.AshTerrain` 是 little-endian version 1 分块容器：

- 固定 96-byte header、两个 index descriptor 槽以及 CRC32。
- block kind 覆盖 metadata、Base height、edit height/weight、composed Component、min/max 和 LOD error。
- block codec 为 `None` 或确定性 `Rle`。
- 增量保存追加 dirty block 和新索引，flush 后才覆盖较旧 descriptor；任一步失败都保留上一有效 generation。
- 加载选择 generation 最大的有效 descriptor。最新 descriptor 或 index 无效时可恢复上一 generation；两个 descriptor 都无效时返回 `Corrupt`。选中 index 后若 payload CRC、解码或逻辑 block 校验失败则直接返回错误，不再回退另一 descriptor，也不得发布快照。
- `optimize_terrain_container` 把 live block 写入临时容器，验证后原子替换原文件；失败时原容器仍可读。

`TerrainContainerLoadReport` 记录实际 generation、是否恢复上一代和解码 block 数；`TerrainContainerSaveReport` 记录前后 generation、追加字节和写入 block 数。

## 高度图导入与导出

`TerrainImport.h` 支持：

- RAW R16 / R32F：大小端与 X/Z 翻转。
- PNG：Windows Imaging Component 导入 8-bit 或 16-bit grayscale；8-bit 输入写入精度警告。导出固定要求 16-bit grayscale，并拒绝编码器静默降精度。
- EXR：TinyEXR 单通道 half/float，保持线性数值。

尺寸不匹配时必须显式选择 Reject、Crop 或确定性 Catmull-Rom；不会静默缩放。导入/导出支持取消，所有大小计算执行溢出检查。导入的可配置峰值内存上限默认 1 GiB；EXR 导出使用固定 1 GiB 峰值上限，RAW/PNG 导出按行流式处理并执行尺寸/分配检查，不声明统一 1 GiB 硬上限。导出源可选最终合成高度、Base 高度、指定高度编辑层或指定材质权重层。`import_terrain_height_to_container` 先完成并验证临时结果，再发布目标容器；失败或取消不替换已有有效数据。

## AssetDatabase 契约

`AssetType::Terrain` 识别大小写不敏感的 `.ashterrain` 扩展名。同步和异步 API 都支持按 `TerrainAssetId` 或相对路径加载 `std::shared_ptr<const TerrainAssetSnapshot>`。

- 同一 Terrain 的并发异步请求共享一个 `shared_future` 和一次磁盘加载；成功快照与实际容器损坏失败都会缓存，后者需显式 invalidate 后重试。
- 没有 worker、关停拒绝或派发异常属于可重试的 `Unloaded`，不会遗留永久 `Loading`，也不要求先 invalidate。
- refresh、publish 和 invalidate 通过 catalog generation 与每资产 load serial 阻止旧 worker 覆盖新索引或新发布结果。
- `publish_terrain_snapshot` 只接受匹配 ID 的有效快照，并按 `(content_generation, residency_revision)` 字典序拒绝 stale publication。
- `invalidate_terrain_snapshot(id)` 只清除该 Terrain 的 cache/in-flight，不做全局 refresh。

## 实现位置

| 路径 | 当前职责 |
| --- | --- |
| `project/src/engine/Function/Asset/TerrainData.h/.cpp` | 布局、所有权、R16 映射、flat snapshot 与核心数据结构 |
| `project/src/engine/Function/Asset/TerrainComposition.h/.cpp` | 8 路权重量化、dirty 收集、Component 合成与不可变发布 |
| `project/src/engine/Function/Asset/TerrainBrush.h/.cpp`、`TerrainEditPatch.cpp` | 笔刷、路径重采样、patch 生成与原子 undo/redo 回放 |
| `project/src/engine/Function/Asset/TerrainSpatialData.h/.cpp` | Component min/max 层级与 LOD error 数据 |
| `project/src/engine/Function/Asset/TerrainContainer*.h/.cpp`、`TerrainBlockCodec.*` | version 1 容器、CRC、generation recovery、增量保存、优化与 RLE |
| `project/src/engine/Function/Asset/TerrainImport.*`、`TerrainRawCodec.cpp`、`TerrainPngCodecWin.cpp`、`TerrainExrCodec.cpp` | RAW/PNG/EXR 导入导出与取消/内存合同 |
| `project/src/engine/Function/Asset/AssetDatabase.h/.cpp` | Terrain 索引、同步/异步 cache、发布与精确失效 |
| `project/src/engine/Function/Scene/TerrainQuery.h/.cpp` | snapshot-local 高度、法线、射线与非阻塞预取查询 |
| `project/src/engine/Function/Scene/SceneComponents.h`、`Scene.h/.cpp` | Scene v6 Terrain 组件、验证、序列化、提取与独立 revision |
| `project/src/engine/Function/Scene/SceneQuery.h/.cpp` | world-space Scene Terrain 高度、法线和射线 adapter |
| `project/src/engine/Function/Render/TerrainRenderAsset.h/.cpp` | content-generation 状态、R16/权重 GPU 打包、不可变 pointer diff、资源 ownership 与 removal metadata |
| `project/src/engine/Function/Render/RenderAssetManager.h/.cpp` | Terrain request/finalize cache、pending/failed owner 与 activity epoch 接入 |
| `project/src/engine/Function/Render/TerrainRenderProxy.h/.cpp`、`RenderScene.*` | Terrain proxy、world bounds、frustum 裁剪与不可变 visible-frame snapshot |
| `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp` | 独立 terrain revision、topology/transform 同步顺序与帧快照发布 |
| `project/src/tests/Terrain/` | Phase 1 单元、故障注入和源码边界契约 |
| `project/src/tests/Scene/terrain_component_tests.cpp` | Scene v6、层级不变量、revision 与 world-query 契约 |

## 尚未实现（not implemented）

- Scene Terrain 尚未并入通用 mesh `ray_cast_scene`、Editor GPU pick 或完整角色控制器；当前 world adapter 是这些调用方可直接使用的 CPU 契约。
- Component 实际流送策略/LRU、LOD 绘制、shadow/GBuffer shader、weight-atlas compute 写入、真实材质层 texture-array 填充或双后端可见渲染。
- Editor Terrain Mode、创建/导入面板、图层栈 UI、笔刷 overlay、CommandService/UndoRedoService 接入、保存/重载工作流。
- Terrain RenderGate 场景、golden、300 FPS 最终性能验收和 Phase 2–4 的内存/流送压力验证。

## 验证

- `RunTests.bat Debug` 与 `RunTests.bat Release`：Terrain layout/data/composition/brush/patch/spatial/query/container/import/AssetDatabase 全部契约。
- 依赖或工程生成发生变化时 fresh `generate_vs2022.bat`，并构建 Editor/Sandbox Debug 与 Release。
- `RunArchGate.bat`：Asset/Scene 公共边界无新增越界。
- `run.bat all Debug --smoke-test-seconds=120`：Editor/Sandbox × Vulkan/DX12 readiness 与正常退出。
- `scripts/AIDevDoctor.ps1 -Mode ValidatePlan`：核对 dirty path 对应的验证计划。

当前 slice 已改变 Scene → `VisibleRenderFrame` 数据流但仍未被 draw pass 消费，不改变 shader 或 RHI 公共接口；因此必须跑双后端 readiness 与 `RunRenderGate.bat` 证明既有画面无回归，尚不单独要求 PerfGate。进入 atlas compute、Terrain shader/pass 或可见绘制后必须执行完整双后端 RenderGate 与 PerfGate。

## 历史

- [SDD-2026-07-13-terrain-system](../../sdd/SDD-2026-07-13-terrain-system.md)
- [Phase 1 implementation plan](../../superpowers/plans/2026-07-13-terrain-phase-1-asset-core.md)
- [Phase 2 rendering implementation plan](../../superpowers/plans/2026-07-13-terrain-phase-2-rendering.md)
