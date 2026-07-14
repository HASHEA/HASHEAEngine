---
owner: huyizhou
last_reviewed: 2026-07-15
status: active
---

# Feature Spec: Terrain Asset Core and Scene Contract

## 当前范围

Phase 1 提供 Terrain 的纯 CPU 资产核心：网格与分块数据、不可变快照、非破坏编辑层、笔刷与 patch 回放、空间查询、`.AshTerrain` 容器、RAW/PNG/EXR 高度图导入导出，以及 `AssetDatabase` 发布与失效接口。Phase 2 已完成九个 rendering slice：Scene v6 `TerrainComponent`、独立 revision/extraction 和 world-space CPU query adapter；Function-only 原生 2D texture-array wrapper；`TerrainRenderAsset` 的 content-generation 状态、不可变 Component pointer diff、GPU 资源 ownership 和 `RenderAssetManager` readiness/activity 接入；`RenderTerrainProxy`、`VisibleRenderFrame::terrains` 与 ScenePresentation 的独立 terrain revision 同步；纯 CPU Component quadtree culling、投影误差 LOD、邻接修复与稳定 draw batch 生成；dirty weight-atlas 的 raw upload、RenderGraph UAV→SRV 契约与 compute shader；9 级共享 index grid、packed-height/morph surface shader 和固定 top-four 材质混合；`SceneRenderer` 的既有 GBuffer/方向光阴影接入、LOD debug、按 view 的 TAA history 失效和 Terrain capture readiness；以及 production-size 确定性 fixture 与 generation 驱动的 load/compose/upload/atlas/scene-submit readiness evaluator。Phase 3 当前实现包括 UI-free authoring session、stroke 与图层栈命令/undo-redo，以及 UIContext-only Terrain Mode：Asset Browser 的 Terrain 选择、service-owned Manage/Sculpt/Paint/Layers 配置、兼容笔刷控件、稳定 ID 图层操作、primary Scene viewport 笔刷输入仲裁、world-space brush overlay、异步 Create Flat / PNG、RAW R16、RAW R32F、EXR Import / Export、Save / Save Copy As / Optimize，以及 generation-aware Reload、外部修改冲突与只读恢复流程。

生产默认布局为 8193 × 8193 个高度采样、8192 × 8192 个 quad、32 × 32 个 Component，每个 Component 为 256 × 256 quad、257 × 257 个快照采样，默认间距 1 m。纯 CPU API 也接受满足 `sample_count = component_count × component_quad_count + 1` 且采样间距为有限正数的小布局，供测试和工具使用；这不改变生产默认布局。

## 数据与所有权

- `TerrainAssetSnapshot` 是发布给读取方的不可变资产快照。Base 高度、编辑层和 Component 通过 const 共享对象暴露；发布后不得修改。
- `TerrainWorkingSet` 是从快照深验证后创建的受信可变编辑状态。编辑只通过 Terrain brush、patch 和 compose/publication API 完成。
- 全局 sample coordinate 是持久化高度与权重的唯一真源。Component 内部边界样本只有一个 owner；依赖同一边界的全部 Component 都会进入 dirty 集合。
- Base 高度以 R16 配合 `height_offset` / `height_range` 持久化，工作集、合成与查询使用 float32。
- 全地形固定 8 个材质层。最终权重为 8 路 uint8，`quantize_terrain_weights` 保证每个 sample 的总和精确为 255；未绘制区域隐式为 Layer 0 = 255。
- `TerrainEditLayer` 具有稳定 16-byte ID、名称、可见性、锁定状态、强度和 Additive/Alpha 高度混合模式。高度与权重分别保存按 owner Component 划分的 canonical 稀疏 block。

每次非空内容修改只推进一次 `content_generation`。`dirty_components` 按 z/x 排序去重；边界修改按共享 sample 规则扩张。`compose_terrain_components` 只重建请求的 Component，`publish_terrain_working_set` 要求一次提交完整的当前 generation dirty 集合，成功后原子替换对应 const Component 指针并清空 dirty 集合；失败时工作集和旧快照保持不变，未修改 Component 继续共享旧指针。

## 笔刷与 undo/redo 逻辑

当前工具为 Raise、Lower、Smooth、Flatten、Noise、Paint、Erase。公共参数使用世界空间半径、强度、falloff、stroke spacing、选中层、材质层和确定性 seed；`TerrainBrushMetric` 把 terrain-local XZ 映射到世界米，因此正数非均匀缩放下仍保持世界空间圆形影响。

`resample_terrain_stroke` 和 `apply_terrain_brush_stroke` 按世界距离确定性重采样，同一几何路径不依赖输入点密度、帧率或 frame index。一次 stroke 在首次 mutation 前冻结 Base 与选中层及以下可见层，随后按 dab 顺序修改 canonical block。Noise 只依赖完整 64-bit seed 与全局 sample coordinate。

Editor viewport 只允许 canonical primary Scene 在 Sculpt/Paint 模式提交笔刷。world ray 命中的 Terrain component 必须解析为当前选中 asset；`TerrainRayHit.local_sample` 是 sample index，进入 brush 前必须乘 `sample_spacing_meters` 转为 terrain-local 米，`TerrainBrushMetric` 则取命中 entity 世界矩阵 X/Z 轴长度以保留正非均匀缩放。一次 LMB press 最多 Begin 一次；Ready press 同帧加入首采样，Ready release 加入末采样后 End，同帧完成的短点击也必须走 Begin + Add + End。活动 stroke 进入 Outside 会 End 已完成段并锁存到 release，Pending/Failed、相机接管或 viewport 生命周期失效会 Cancel，禁止跳过无效区后重新连接 raw path。Authoring session readiness 与 cursor hit status 是两个独立状态：后者只由当前选中 asset 的 canonical primary Scene 更新，并携带 world anchor、normal、service-owned radius 和 Terrain entity identity；Outside/owner lifecycle 清空，foreign hit 不得替换合法 anchor。

Brush preview 只在 authoring session Ready、canonical primary Scene 可接收输入且当前 Terrain entity 仍绑定选中 snapshot 时提交。`TerrainBrushOverlayRenderer` 在 world-XZ 平面生成固定 64 段圆环，把每个候选点按有限平移与正非均匀缩放映射到 service 发布的 immutable snapshot，逐点调用 `query_height` 后还原 world Y；因此坡地上的 XZ 半径与实际 brush world metric 一致，不使用会在斜坡上收缩的切平面圆。只有相邻两个原始端点都 Ready 才生成该边，Pending/Outside/Failed Component 不会被跨越连接。Ready 为绿色、cursor Pending 为琥珀色、cursor Failed 或锁定层为红色；Outside、无 anchor、session 非 Ready、非法 transform/binding 或整圈无有效边均不提交。线段使用 `SceneOverlayDepthMode::DepthTestNoWrite` 并通过 Function `submit_scene_overlay` 深拷贝，Editor 不访问 Graphics 或持有 GPU 资源。

一次 stroke 输出按 owner/domain 排序的 `TerrainEditPatch`。patch 分别记录 before/after 字节，可选择原始或确定性 RLE；`apply_terrain_edit_patches` 在写入前解码并验证整批 patch、当前 source bytes、目标层/owner/domain/rect 与 generation。任一 patch 无效时整批原子失败；成功时 generation 只推进一次，并重建完整 halo dirty 集合。Editor 的 `TerrainStrokeCommand` 只保存该 Engine patch，并经 `RecordExecutedCommand` 接入 undo/redo；Editor 不复制 patch codec 或 brush kernel。

## 图层栈与 undo/redo

`apply_terrain_layer_stack_edit` 支持 Add、Delete、Duplicate、Rename、Move、SetVisible、SetOpacity 和 SetLocked。一次有效变更原子推进一次 `content_generation`，并返回以稳定 layer ID 和完整 before/after 顺序描述的 `TerrainLayerStackPatch`；`apply_terrain_layer_stack_patch` 对 source order、metadata 和 retained layer 做完整校验后执行 undo/redo，stale 或损坏 patch 不得部分修改 working set。

拓扑 patch 只保留被添加、删除或复制的单层稀疏 block；metadata 与顺序 patch 不复制稀疏内容，禁止用整栈 snapshot 承载 8193² authoring history。删除、复制、可见性、强度和移动会把受影响层的 canonical occupancy 与既有 dirty 集合合并；移动还包含跨越层。空 Add、Rename、锁定以及幂等编辑不会制造额外 dirty，其中幂等编辑返回 `has_change()==false`、不推进 generation，也不得进入 Editor 历史。

`TerrainEditorService` 先通过 Function API 完成原子 mutation，再把同一 patch 包装为 `TerrainLayerCommand` 交给 `RecordExecutedCommand`。历史记录失败沿同一 patch 回滚；回滚结果无法证明时沿用 stroke 的 generation/pending-publication 隔离和 session quarantine 契约。选择只保存稳定 layer ID；锁定层会同步到 preview，并拒绝开始 stroke。

## 异步保存与 Scene 顺序

`TerrainEditorSessionCore` 分开跟踪当前 `content_generation` 和已持久化 generation。Save 请求若遇到待合成内容，先进入 `AwaitingPublication`；该短暂阶段暂停新 authoring mutation，直到所请求 generation 完整合成并发布。随后 worker 按操作只捕获所需的 immutable snapshot、累积 dirty payload 和解析后路径，不捕获 service、working set 或 UI 状态；进入 `Running` 后的后续编辑可继续生成新 generation，但任何 session replacement（包括 history quarantine 的逃生选择）都必须等该文件操作完成，不能让 worker 的捕获身份与当前 session 分离。Editor 主线程由 `Update()` 轮询 `Succeeded` / `Failed`，普通 `Ctrl+S` 不在 UI 线程同步等待磁盘。worker 不可用、派发被拒绝或异常时必须快速失败，不回退为 UI 线程内联写文件；service shutdown 必须等待已经 Running 的 worker 收敛。

普通 Save 对已有容器构造“自磁盘已持久化 generation 以来”的累积 dirty 集合：Component 保留自己最后变化时的 generation，必须且只能提交 `component.content_generation > persisted_generation` 的坐标，payload generation 必须与对应 Component generation 相等。因此多次发布后一次保存可同时包含不同 Component generation，不会丢掉较早未落盘的 Component。新容器则以空 dirty 列表写完整 snapshot，同样允许 Component generation 混合，但任何 Component generation 都不得超过 snapshot generation。

Save 成功只把启动时捕获的 generation 推进为 persisted；worker 运行期间产生的更新 generation 仍为 dirty。`TerrainContainerRevision` 是 Save/Optimize 的来源身份与外部变化权威，write time 只作为辅助 metadata 随结果返回，不能代替 revision 比较。Save/Optimize 启动时必须持有有效的 observed revision，并调用 checked overload；writer 在共享的 destination commit lease 内比较实际 revision，变化时返回可重试的 `SourceChanged`，成功时通过 `TerrainContainerSaveReport::committed_revision` 返回真正提交的 revision。Editor 只有同时取得 committed revision 与稳定 write time 才推进观测基线；成功写盘却无法取得 write time 仍按 operation failure 处理，后续由 revision poll 重新协调。失败不推进 persisted generation，不清理 working data 或 undo/redo 历史。

Save 和 Optimize 始终绑定当前 working set 的 `source_path`，且拒绝显式目标路径，避免普通保存或压实意外写到其他资产。只有 Save Copy As 接受显式新路径：它先在目标同目录把完整容器写入唯一 staged file 并回读校验，再通过 `publish_staged_terrain_container_new` 取得与 Save/Optimize 相同的 destination commit lease，在 lease 内重新确认目标不存在并 durable、non-replacing 地发布；目标在检查后出现、lease 为 `Busy` 或任一步失败时都保留既有目标并清理临时件。Save Copy As 不重绑当前会话，也不清 dirty。Optimize 只接受无 dirty、已保存的源资产。三种操作都强制要求有效 AssetDatabase root，最终解析路径必须位于该根内且扩展名为 `.AshTerrain`；根包含检查以 canonical path 为准，目录联接或符号链接不能把输出引到 root 外。仅提供绝对路径而未配置资产根不能绕过此约束。

`Ctrl+S` 保持 Terrain asset dirty 与 Scene dirty 两条独立轨迹。当唯一 mutable authoring session 中选中的 dirty Terrain 被当前 Scene 引用时，`EditorActionCoordinator` 先发起 Save（或附着到同一资产已在进行的 Save），记录精确 operation serial，并把 Scene 写盘延后到 Terrain `Succeeded`。Scene 引用暂时无法从 catalog 解析时，coordinator 只允许在资产根内规范化后与 working-set source path 精确匹配；路径身份无法安全证明则 fail closed，不能跳过 dirty Terrain 继续写 Scene。Terrain 失败会把精确资产路径和错误传递给 Scene save failure，且不写 Scene；等待期间 Scene content epoch 变化也会取消该次写入，避免把新 Scene 写到旧请求的路径。Terrain 成功后再保存同一 epoch 的 active Scene；只有当 undo/redo history state 仍等于 `Ctrl+S` 时的 checkpoint 才调用 `MarkSaved()`，否则文件虽已写出，更新命令仍保持 unsaved。

## Reload、外部冲突与恢复

`TerrainEditorService` 以 `None / Reloading / Conflict / RecoveredReadOnly / Failed` 表示磁盘同步状态。它每 500 ms 按 wall clock 通过 `inspect_terrain_container_revision` 检查物理 revision；revision 是是否变化的权威，write time 只随 revision 保存为辅助 metadata。连续两次真实 metadata/容器查询失败才进入只读 `Failed`，从而容忍一次原子替换窗口且不依赖固定帧数；`Busy` 表示遵守 API 的 writer 正持有 commit lease，不消耗失败 debounce 预算。reload candidate 在解码完成后再次核对 revision；`Busy` / `SourceChanged` 都是可重试结果，不发布半成品，也不把 session 冻结为损坏。候选期间发生的第二次提交会因 revision 不同再次排入 reload。同一资产在 load/reload 期间被再次选择是保持 future、状态与诊断的 no-op。

reload 使用不写共享 cache 的 AssetDatabase candidate load；在 clean 接受或用户明确解决 conflict 前，普通资产消费者继续看到原 published snapshot。clean session 的有效新代只在 Editor `Update()` 边界替换 working set、immutable published snapshot 与 AssetDatabase cache，并从 undo/redo 两栈删除该 asset 的 Terrain command；无关 entity/Terrain command 与 saved/current history 映射保留。选择性历史删除是无抛出、可失败的提交前门槛；失败时 cache 回滚，本地 working set、候选、Conflict/恢复状态和全部历史保持不变，可安全重试。dirty session 只进入 `Conflict`，在用户选择前保留本地 bytes、generation、published snapshot、共享 cache 和历史：Reload 应用已经验证的磁盘候选；Save Copy As 先把本地状态发布到新路径，成功后再应用候选；Keep Local 把 persisted/observed generation 对齐到磁盘有效代，将本地 generation 推进到二者最大值加一，并对全 Component 重新 composition，结果继续 dirty。

双 descriptor 容器回退会同时发布 loaded generation、被拒绝 generation 和精确 descriptor/CRC 诊断。冲突排序使用被拒绝 generation，而候选内容仍来自最后有效旧代，避免把“旧内容 + 更新损坏代”误判为 stale。`RecoveredReadOnly` 与 `Failed` 都禁止笔刷、图层 mutation、覆盖 Save 和 Optimize，只允许 Repair 或 Save Copy As；Repair 重新 invalidate/load，成功才退出只读状态。两个 descriptor 都无效或关键 block 不可恢复时，load 失败且 snapshot/report 输出保持空，不发布半成品。

New/Open/Reload Scene 在修改 Scene 前先调用 Terrain preflight：取消活动 stroke，但 dirty、load、composition、file operation、history quarantine 或未解决 conflict 会阻止切换。Scene 变更成功后先提交 Terrain session 清理，再清 Selection 与 undo/redo；若 Scene load/reload 失败，`SceneService` 原子保留当前内存 Scene 与 active path，workflow 不激活 fallback Scene，也不清 Selection、history 或 Terrain recovery/error 状态。已有异步 `Ctrl+S` continuation 未完成时，New/Open/Reload 不得取消或替换它。

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
- `build_terrain_lod_batches` 从不可变 snapshot 的 Component 根 min/max 和 9 级几何误差构建隐式 quadtree，按 view frustum 剔除并用投影像素误差选择 LOD。邻接修复只细化较粗一侧，保证可见水平/垂直邻居差不超过 1；每个非空 LOD 生成一个 `first_instance == 0` 的 batch，instance 固定按 `(lod, coord.z, coord.x)` 排序并携带较粗邻边掩码与 `[0,1]` morph factor。任何无效输入都保持调用方既有结果不变。
- dirty-weight staging 是 `stride == 0` 的 raw `StorageBuffer`，与 shader 的 `ByteAddressBuffer TerrainWeightUpload` 对齐。一次 `prepare_graph` 最多把一个 257 × 257 Component 的两路 RGBA8 payload 排入 staging，并声明三张 persistent external texture 的 `ComputeUAV` 写；后续 surface pass 必须把同三张 texture 声明为 `GraphicsSRV`。单 staging buffer 禁止在同一 graph 中先排入多份 copy 再 dispatch，否则多个 dispatch 会观察最后一份 payload。
- atlas compute 使用 8 × 8 thread group 覆盖 259 × 259 slot：内部 257 × 257 texel 和一圈 gutter 一次写入；高分辨率 slot 优先复用同坐标，其次使用空 slot，256 slot 已满时仍更新 coarse map。1025 × 1025 coarse map 以 1/8 采样写入前四层权重，非末端 Component 不拥有 +X/+Z 边界，末端 Component 写最终边界。只有 dispatch 成功后才提交 slot metadata 并消费该 pending upload；失败或被新 immutable generation 取代时保留/重建下一帧工作。Vulkan shader 编译器在 DXC rewriter 完成后，为行首 `RWTexture2D<unorm float4>` 补入 `vk::image_format("rgba8")`，使 SPIR-V storage image 格式与固定 RGBA8_UNORM atlas 一致；DX12 shader 文本和公共 RHI 契约不变，rewrite 版本进入 shader cache key。
- `build_terrain_shared_grid_indices` 为 LOD0..8 生成分辨率 256..1 的 row-major triangle-list，共 `6 × resolution²` 个 `uint32_t` index；非法 LOD 保持调用方输出不变。`TerrainRenderPass` 初始化时各创建一份共享 index buffer，不创建 per-Component vertex buffer；shader 以 indexed draw 的 `SV_VertexID` 还原网格坐标，并以 `SV_InstanceID` 读取 packed Component/LOD/morph/atlas residency。
- surface vertex shader 从全 Terrain packed R16 buffer 解码高度，用下一粗级的同三角形平面做 geomorph；较粗邻边无条件使用 coarse height，确保细边顶点与粗网格共线。法线在 global sample 空间读取跨 Component 邻样本做中心差分，再用 world-space 切线叉积支持有限正非均匀缩放。GBuffer 与 depth-only permutation 复用完全相同的高度/morph 路径，depth-only 不声明材质资源。
- GBuffer permutation 从高分辨率双 RGBA atlas 或 coarse map 读取权重，按权重降序、layer index 升序确定性选择最多四层并归一化；全零回退 Layer 0 = 1。BaseColor/Normal/ORM 使用三张 8-slice texture array 和独立 material sampler，输出既有 DeferredHQ 五目标布局（含 motion/temporal validity 与 oct normal）。
- `SceneRenderer` 在既有 `SceneGBufferPass` 前准备 atlas compute，在同一 clear pass 内先画 static mesh、再画 Terrain，禁止增加第二次 GBuffer clear。方向光 sunlight CSM 与普通 directional shadow 共用组合 caster callback，先提交 static mesh，再提交所有允许的 Terrain shadow caster；`DynamicOnly` shadow cache 更新不重复画静态 Terrain。
- Terrain surface 每个 LOD batch 的 `first_instance` 保持 0，真实实例基址由 root constants 传给 shader；实例 StorageBuffer 使用 GPU-only 资源与既有 staging upload，并保留 3 帧物理 ring，避免 CPU 覆盖 GPU 仍在读取的数据。禁止把 storage/UAV usage 与 CPU-write upload heap 组合，因为该资源描述在 DX12 上非法。首期同一 scene/view 只消费第一个有效主 Terrain，符合一个 8193² Scene Terrain 的批准目标；多 Terrain 同帧独立材质/atlas 绑定尚未实现。
- Terrain `content_generation` 集合按 temporal view 形成稳定签名；签名变化时只失效该 view 的 TAA history，新高度帧的 motion temporal-valid 关闭，不影响其他 viewport。Render Debug View 注册两张 atlas、coarse weights 和实际 LOD 色彩图；LOD debug 仅在被选中时增加 raster pass。
- Terrain capture-ready 要求每个可见 Terrain 的 accepted/published generation 与 immutable snapshot 一致、render asset 为 Ready、无 pending Component upload，并且最后一次 atlas update 所在提交帧之后至少完成过一个后续 scene prepare。它与粒子 capture-ready 取逻辑与，复用 readiness/present 流程，不引入固定帧数。
- `evaluate_terrain_readiness` 只接受同一 `content_generation` 的 load、compose、height upload 与 atlas stage；当前 generation 的任一 Failed 优先返回 Failed，stale generation 只能保持 Pending，全部 Ready 后仍须当前 Scene packet 成功才返回 Ready。`RenderAssetManager` 与 `SceneSubmissionSnapshot` 共用这一状态语义，不以 elapsed frame 数作为成功证据。
- `TerrainGate.AshTerrain` 由正式 Phase 1 writer 生成，使用 8193 × 8193 / 32 × 32 production layout，包含可见 Component 边界、九级误差范围、八个 one-hot 材质区域与一个四层混合区域；schema 6 `Terrain.scene.json` 固定一个 Terrain、一个主相机和一个方向光，并关闭粒子、体积光、AO、Bloom 与 TAA。fixture contract test 会通过正式 loader 反读，并验证相机视图确实产生全部九个 LOD batch。

## `.AshTerrain` 容器

`.AshTerrain` 是 little-endian version 1 分块容器：

- 固定 96-byte header、两个 index descriptor 槽以及 CRC32。
- block kind 覆盖 metadata、Base height、edit height/weight、composed Component、min/max 和 LOD error。
- block codec 为 `None` 或确定性 `Rle`。
- layer metadata 保持既有 version-1 字段布局，并可追加 `ASHL` revision 1 锁定状态 trailer；没有 trailer 的旧资产按全部 `locked=false` 加载，未知 revision、数量不匹配、非法布尔值或 trailer 尾随数据均 fail closed。
- `TerrainContainerRevision` 由文件大小和两个 descriptor 的 generation/index offset/index size/index CRC 组成，是同一 canonical path 上已提交物理版本的比较权威；generation 与 write time 都不能单独替代它。load 解码完成后会在 commit lease 下重新 inspect，只有 revision 未变化才发布 snapshot/report，否则返回可重试结果且输出保持空。
- Windows 使用按 lowercase canonical target 派生名称的 process-external named mutex 作为 commit lease。Save、Optimize、revision inspect 与 staged new-file publish 共用它；该 lease 只协调遵守 TerrainContainer API 的 writer，不宣称阻止绕过 API 的任意外部文件写入。
- `Busy` 表示另一合规 writer 正持有 lease；`SourceChanged` 表示取得 lease 后发现 expected revision 或 non-replacing destination 已变化。两者都属于可重试并发结果，不等同于容器损坏。
- 增量保存的 dirty payload 必须精确覆盖所有 `component.content_generation > persisted_generation` 的 Component；payload generation 与对应 Component 一致，而非强制等于最新 snapshot generation。writer 追加这一累积 dirty 集合和新索引，flush 后才覆盖较旧 descriptor；任一步失败都保留上一有效 generation。
- 首次写新容器时 dirty payload 必须为空，并把完整 snapshot 写入。完整 snapshot 和后续增量 snapshot 都允许多个 Component 保留不同的最后修改 generation。
- 加载选择 generation 最大的有效 descriptor。最新 descriptor 或 index 无效时可恢复上一 generation；两个 descriptor 都无效时返回 `Corrupt`。选中 index 后若 payload CRC、解码或逻辑 block 校验失败则直接返回错误，不再回退另一 descriptor，也不得发布快照。
- checked `save_terrain_container_incremental` 与 `optimize_terrain_container` 在 commit lease 内比较 expected revision；不匹配返回 `SourceChanged`。Optimize 把 live block 写入临时容器并验证，在最终 lease 内同时复核 source 与 staged revision 后才原子替换；失败时原容器仍可读。
- `publish_staged_terrain_container_new` 只发布已经验证的 staged container；它在 destination lease 内执行最终存在性检查和 non-replacing 原子发布，禁止 Save Copy As 绕过同一路径的 writer 序列化。

`TerrainContainerLoadReport` 记录实际 generation、是否恢复上一代、解码 block 数与 `source_revision`；`TerrainContainerSaveReport` 记录前后 generation、追加字节、写入 block 数与 `committed_revision`。

## 高度图导入与导出

`TerrainImport.h` 支持：

- RAW R16 / R32F：大小端与 X/Z 翻转。
- PNG：Windows Imaging Component 导入 8-bit 或 16-bit grayscale；8-bit 输入写入精度警告。导出固定要求 16-bit grayscale，并拒绝编码器静默降精度。
- EXR：TinyEXR 单通道 half/float，保持线性数值。

尺寸不匹配时必须显式选择 Reject、Crop 或确定性 Catmull-Rom；不会静默缩放。导入/导出支持取消，所有大小计算执行溢出检查。导入的可配置峰值内存上限默认 1 GiB；EXR 导出使用固定 1 GiB 峰值上限，RAW/PNG 导出按行流式处理并执行尺寸/分配检查，不声明统一 1 GiB 硬上限。导出源可选最终合成高度、Base 高度、指定高度编辑层或指定材质权重层。`import_terrain_height_to_container` 先完成并验证临时结果，再发布目标容器；失败或取消不替换已有有效数据。

## Editor 创建与高度图文件作业

Terrain Mode 的 Create Flat 固定以 `make_default_terrain_grid_layout()` 创建生产布局：8193 × 8193 高度采样、32 × 32 个 Component、每 Component 256 × 256 quad、1 m 采样间距。UI 显式提供 height minimum、height maximum 与 flat height；有限且 `minimum < maximum` 时提交 `height_offset = minimum`、`height_range = maximum - minimum`，不得为了测试或响应速度把生产默认静默降为小布局。Import 使用独立草稿显式提交源宽高、`height_offset` / `height_range`、PNG / RAW R16 / RAW R32F / EXR 格式、RAW 大小端、独立 X/Z flip、精确 EXR channel，以及 Reject / Crop / Catmull-Rom。Export 使用另一组独立草稿提交 PNG / RAW R16 / RAW R32F / EXR、RAW byte order、精确 EXR channel 与 half/float，并支持最终合成高度、Base 高度、指定高度层和指定材质权重层；Import 与 Export 的路径、格式和 codec 字段不得共享 UI state。

Create/Import 产生的 `.AshTerrain` target 必须以配置后的 AssetDatabase root 为边界：UI 默认使用 `terrain/NewTerrain.AshTerrain` 这类 root-relative 路径；canonical 解析后必须仍在 root 内且扩展名正确，目录联接/符号链接不可逃逸。提交时的不存在检查只提供快速诊断，最终结果必须先写到唯一 staged container、完成回读校验，再通过 `publish_staged_terrain_container_new` 的 named commit lease 和 non-replacing publish 创建目标；并发出现的目标必须原样保留。Import 只把唯一 staged path 交给 `import_terrain_height_to_container`，不能把最终路径交给其 replacing publish。Import source 与高度图 Export destination 属外部文件工作流，不作为 AssetDatabase 身份，也不经过 `.AshTerrain` containment resolver：绝对路径可位于 asset root 外，相对路径统一以 asset root 为基准解析。Export 固定为 create-new / never-overwrite / non-replacing，不提供覆盖开关；必须验证 parent、所选格式与扩展名，既有或竞态 destination 保持原样，失败/取消只清理本次唯一 stage。

四种 Export source 均支持 PNG、RAW R16、RAW R32F 与 EXR。最终合成、Base 和高度编辑层沿用 Terrain 线性高度语义；材质权重层输出选定 edit layer 的 `0..7` lane，PNG/RAW R16 使用独立于 Terrain 高度映射的固定 `[0,1]` normalized 映射，RAW R32F/EXR 直接输出 `[0,1]` 浮点值。禁止因来源是材质权重而静默禁用已批准格式。

这些操作复用 `TerrainEditorService` 的后台派发和主线程 completion：worker 只按值捕获 operation identity、路径、descriptor、共享 cancellation token，以及 Export 所需的 `shared_ptr<const TerrainAssetSnapshot>`；禁止捕获 mutable service、working set、panel state、intent 引用或 `_publishedSnapshot` 引用。`CancelFileOperation` 只接受 Running Import/Export，并触发对应 codec token；Create 不提供显式取消，Manage 也只在该两类作业 Running 时绘制 Cancel。取消与 final publish 共享原子 phase：取消赢家走 `Cancellable -> Cancelled` 并保持到 future 消费/reset，worker 发布赢家走 `Cancellable -> Publishing -> Completed`，非取消的提前失败可走 `Cancellable -> Completed`；worker 先 claim `Publishing` 后取消必须返回 false，不能在 final destination 已可发布后误报取消成功。取消必须进入独立的 `Cancelled` 终态，清理唯一 staged container 和 `.import.tmp`，且不得发布 final destination。Shutdown 对可取消的 Running job 先请求取消，再等待所有 Running worker 收敛后释放 service。失败、`Cancelled`、worker 拒绝、异常、stale completion 或目标竞态都不得替换当前 working set/cache、清 dirty/history。Create/Import 完成后只在 Editor `Update()` 边界 refresh catalog、取得最终路径对应的真实 `AssetId` 并发布/打开快照；若文件已 durable publish 但 catalog/open 失败，保留用户文件并进入 `PublishedAwaitingCatalog`，Manage 显示最终持久路径、重试状态与最近错误，禁止为伪装事务失败而删除已发布资产。PNG 8-bit precision warning 必须保留在文件操作状态中供 Manage 显示。

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
| `project/src/engine/Function/Asset/TerrainLayerStack.h/.cpp` | 图层栈原子编辑、稳定 ID、精确 dirty union 与可逆 patch 回放 |
| `project/src/engine/Function/Asset/TerrainSpatialData.h/.cpp` | Component min/max 层级与 LOD error 数据 |
| `project/src/engine/Function/Asset/TerrainContainer*.h/.cpp`、`TerrainBlockCodec.*` | version 1 容器、CRC、generation recovery、增量保存、优化与 RLE |
| `project/src/engine/Function/Asset/TerrainImport.*`、`TerrainRawCodec.cpp`、`TerrainPngCodecWin.cpp`、`TerrainExrCodec.cpp` | RAW/PNG/EXR 导入导出与取消/内存合同 |
| `project/src/engine/Function/Asset/AssetDatabase.h/.cpp` | Terrain 索引、同步/异步 cache、发布与精确失效 |
| `project/src/engine/Function/Scene/TerrainQuery.h/.cpp` | snapshot-local 高度、法线、射线与非阻塞预取查询 |
| `project/src/engine/Function/Scene/SceneComponents.h`、`Scene.h/.cpp` | Scene v6 Terrain 组件、验证、序列化、提取与独立 revision |
| `project/src/engine/Function/Scene/SceneQuery.h/.cpp` | world-space Scene Terrain 高度、法线和射线 adapter |
| `project/src/engine/Function/Render/TerrainRenderAsset.h/.cpp` | content-generation 状态、R16/权重 GPU 打包、不可变 pointer diff、资源 ownership 与 removal metadata |
| `project/src/engine/Function/Render/RenderAssetManager.h/.cpp` | Terrain request/finalize cache、pending/failed owner、activity epoch 与纯 readiness evaluator |
| `project/src/engine/Function/Render/TerrainRenderProxy.h/.cpp`、`RenderScene.*` | Terrain proxy、world bounds、frustum 裁剪与不可变 visible-frame snapshot |
| `project/src/engine/Function/Render/ScenePresentationSubsystem.cpp` | 独立 terrain revision、topology/transform 同步顺序、generation readiness 与帧快照发布 |
| `project/src/engine/Function/Render/TerrainLod.h/.cpp` | Component quadtree culling、投影误差选级、邻接修复、morph/edge metadata 与稳定 LOD batches |
| `project/src/engine/Function/Render/TerrainRenderPass.h/.cpp`、`Shaders/Terrain/*` | persistent weight resources 的 graph 注册、单 Component raw staging、gutter/coarse compute 更新、9 级共享 grid、GBuffer/shadow/LOD debug draw 与 capture readiness |
| `project/src/engine/Function/Render/SceneRenderer.h/.cpp`、`TemporalAAPass.*` | 既有 GBuffer/方向光 shadow 编排接入、Terrain debug 注册、按 view TAA 失效与粒子/Terrain readiness 合并 |
| `project/src/editor/Services/TerrainBrushOverlayRenderer.h/.cpp`、`Panels/ViewportPanelCanvas.cpp` | primary Scene 的 64 段 world-XZ brush preview、immutable snapshot 高度贴合与 Function overlay 提交 |
| `project/src/editor/Core/TerrainEditorSessionCore.*`、`Services/TerrainEditorService.*`、`Panels/Terrain/*` | mutable authoring 唯一所有者、persisted/effective-disk generation、异步创建/导入/导出/保存状态、外部冲突/恢复状态与 Manage UI |
| `project/src/editor/App/EditorActionCoordinator.*`、`SceneWorkflowCoordinator.*`、`EditorApplicationImpl.cpp`、`Services/UndoRedoService.*` | Terrain→Scene `Ctrl+S` continuation、Scene preflight/commit、selective Terrain history removal |
| `project/src/tests/Terrain/` | Phase 1 单元、Phase 2 readiness/fixture、故障注入和源码边界契约 |
| `project/src/tests/Scene/terrain_component_tests.cpp` | Scene v6、层级不变量、revision 与 world-query 契约 |

## 尚未实现（not implemented）

- Scene Terrain 尚未并入通用 mesh `ray_cast_scene`、Editor GPU pick 或完整角色控制器；当前 world adapter 是这些调用方可直接使用的 CPU 契约。
- Component 实际流送策略/LRU、真实材质层 texture-array cook/填充，以及多 Terrain 同帧独立材质/atlas 绑定。当前首期渲染路径面向一个 8193² Scene 主 Terrain。
- Terrain 专用 golden、300 FPS 最终性能验收和 Phase 2–4 的内存/流送压力验证。当前 `Terrain.scene.json` 是可见 integration fixture，尚未授权新增或 bless Terrain golden。

## 验证

- `RunTests.bat Debug` 与 `RunTests.bat Release`：Terrain layout/data/composition/brush/patch/layer-stack/spatial/query/container/import/AssetDatabase，以及 Editor create/import/export immutable jobs、路径/竞态/取消清理、stroke/layer command、world-space brush overlay、累积保存 generation、Save Copy As / Optimize、reload/conflict/recovery/selective-history 和 Terrain→Scene 生命周期契约。
- 依赖或工程生成发生变化时 fresh `generate_vs2022.bat`，并构建 Editor/Sandbox Debug 与 Release。
- `RunArchGate.bat`：Asset/Scene 公共边界无新增越界。
- `run.bat all Debug --smoke-test-seconds=120`：Editor/Sandbox × Vulkan/DX12 readiness 与正常退出。
- `RunRenderGate.bat`：渲染行为变更须保持双后端 golden SSIM 与跨后端 diff 通过；本阶段不新增或 bless Terrain golden。
- `scripts/AIDevDoctor.ps1 -Mode ValidatePlan`：核对 dirty path 对应的验证计划。

`SceneRenderer` integration slice 已通过 headless graph contract、DXIL/SPIR-V 全 permutation 离线编译、Debug/Release 全量单测与 Editor/Sandbox 构建、ArchGate，以及既有 sandbox/particles 的双后端 RenderGate 回归。production-size Terrain fixture 与纯 readiness contract 已加入。2026-07-14 的 Phase 2 exit gate 在隔离 worktree 中完成：Terrain Vulkan core/synchronization validation 与 DX12 GPU validation capture 均 readiness/clean exit 成功，fresh 日志零 error、warning、validation/device-loss/fatal；Vulkan SPIR-V 的三张权重 storage image 已确认与 RGBA8_UNORM view 匹配。Standard PerfGate 四组合均 PASS 且 Failures/Warnings 为空，未 bless；该报告的 Standard 历史 baseline 状态为 `MISSING`，因此 CPU/GPU 数值只作为本次运行健康证据，不替代 Phase 4 的 300 FPS 与历史回归验收。Terrain 专用 golden 仍未授权，且不得把既有非 Terrain 场景的 PASS 当作 Terrain 可见性证据。

2026-07-15 的 Phase 3 自动 exit gate 已通过 Debug/Release 全量测试、fresh generate 与 Editor/Sandbox Debug/Release 构建、ArchGate、AIDevDoctor、Terrain Editor 双后端定向 readiness、四组合 readiness、Standard / Empty / Debug / TimingValidation、既有 sandbox/particles RenderGate 和 Standard PerfGate 4/4；全部运行未 bless，并逐字节恢复四份运行配置。Standard 四格 baseline status 均为 `MISSING`，所以这里只证明采样健康、零 warning/failure，不声称完成历史性能回归比较。该证据不替代 `docs/VERIFY.md` 的双后端 Terrain Mode 人工清单；Terrain 专用 capture/golden 仍属于 Phase 4。

## 历史

- [SDD-2026-07-13-terrain-system](../../sdd/SDD-2026-07-13-terrain-system.md)
- [Phase 1 implementation plan](../../superpowers/plans/2026-07-13-terrain-phase-1-asset-core.md)
- [Phase 2 rendering implementation plan](../../superpowers/plans/2026-07-13-terrain-phase-2-rendering.md)
