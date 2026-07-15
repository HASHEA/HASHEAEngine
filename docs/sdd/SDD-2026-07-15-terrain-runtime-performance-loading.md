# SDD-2026-07-15-terrain-runtime-performance-loading: Terrain 阴影、加载与诊断收敛

## Status

Review（聊天方案已于 2026-07-15 获用户认可；本书面 SDD 待用户批准，批准前不修改生产代码）

## Context

Terrain 视觉稳定性人工复核确认远距摩尔纹与坡面条纹已经消失，但同一人工场景暴露了三个新的阻断问题：

- 同场景、同相机、同两盏投影方向光下，Terrain `casts_shadow=false/true` 的短 telemetry A/B 显示 Vulkan 从 `171.7 FPS / CPU avg 5.82 ms` 退化为 `27.9 FPS / 35.88 ms`，DX12 从 `124.6 FPS / 8.03 ms` 退化为 `27.5 FPS / 36.38 ms`。Vulkan GPU P50 仅从 `0.630 ms` 增至 `0.722 ms`，DX12 从 `0.655 ms` 增至 `3.256 ms`，证明首要瓶颈是 CPU 重复准备，DX12 GPU 阴影开销是需保留的次级差异。
- 同一 Debug Sandbox 进程的无 Terrain / Terrain 对照 wall time 为 Vulkan `19.28 / 33.03 s`、DX12 `17.56 / 31.37 s`，首次 Terrain 增量约 `13.7 s`。private peak 增量约 `1.7–1.8 GiB`。350 KiB 的 flat `.AshTerrain` 在首次发布时被展开为 1024 个 257² Component；`TerrainRenderAsset::accept_snapshot` 又为每个 Component 立即生成高度和两路 RGBA8 权重，光 CPU pending payload 就约 `646 MiB`，随后还创建三套 8-layer、full-mip 1024² fallback material array。
- 人工 Editor 会话在 9 秒内记录 `2531` 条完全相同的 `failed to rebuild RenderScene` error。`ScenePresentationSubsystem` 把 Terrain 暂不可用与真正失败都折叠为 `bool false`，保持 `render_scene_valid=false` 后每帧同步重试并重复 error；日志既不能定位资产路径，也放大了加载期间的 CPU/IO 压力。

该修复跨 Function Asset、Render、ScenePresentation、诊断与双后端可见路径，修改 Terrain draw preparation、资源发布和 Scene rebuild 状态合同，风险级别为 S2。批准前只允许本 SDD、只读证据和临时诊断输出，不修改生产代码、用户场景、baseline 或 golden。

## Goals

- Terrain GBuffer、普通方向光全部级联与 sunlight 级联在同一 scene/view/frame 内只执行一次主视图 LOD、Component 选择、packed instance 和 instance-buffer 准备。
- 保持现有画面语义：各 shadow pass 仍使用各自 light view-projection；Terrain LOD 继续由当前已经采用的主视图输入决定，不改变 caster 集合、级联数、shadow distance 或 bias。
- 消除首次加载时为全部 1024 Component 预生成双路权重 payload 的约 `516 MiB` 临时数据；高度打包和 GPU upload 使用有界生产/消费队列，不再持有 1024 份独立上传向量。
- fallback Terrain material arrays 由同一 Renderer 共享；同一 Renderer 生命周期内不随 Terrain asset/session 重复创建。
- RenderScene 明确区分 Terrain `Ready / Pending / Failed`。Pending 不阻断其他 Scene 内容、不输出 error，也不触发每帧同步解码；Failed 按状态转换记录一次带资产路径和真实原因的 error。
- Editor/Sandbox 在 Terrain 加载期间保持主循环响应；readiness 仍等待完整 Terrain generation 发布和既有后续 scene prepare 信号，不以固定帧数宣告成功。
- 在当前批准机器和同一临时场景 A/B 上，将 `casts_shadow=true` 的 CPU avg 控制在 shadow-off 的 `1.5×` 以内；首次 Terrain wall-time 增量控制在 `5 s` 内，private peak 增量控制在 `768 MiB` 内。
- Vulkan/DX12 validation、RenderGate 和 Standard PerfGate 不退化，不调整既有 threshold，不自动 bless。

## Non-goals

- 不在本包实现完整 world-partition、磁盘分页、LRU、跨场景 residency 或无穷大 Terrain streaming。
- 不把 Terrain shadow 限制为只响应 sunlight，也不静默减少普通方向光的级联数、分辨率或投影距离。
- 不改变 Graphics/RHI、RenderGraph API、descriptor/root binding 或后端资源抽象；双后端继续消费同一 Function 层 draw/resource 合同。
- 不引入 GPU-driven Terrain LOD、mesh shader、compute culling 或 indirect draw；这些需要独立 S2/S3 证据。
- 不在本包修改 Terrain 编辑层模型、实时笔刷或 Hierarchy 直达编辑；这些由配套 authoring SDD 负责。
- 不用关闭 validation、TAA、阴影或放宽 readiness/error 判定掩盖问题。
- 不保证所有主机的绝对启动秒数相同；绝对目标只用于当前批准机器的同刻 A/B，结构性回归由准备次数、payload 上界和相对指标锁定。

## Current implementation

- Entry points:
  - `SceneRenderer` 构造组合 shadow callback；普通方向光和 sunlight 每个需要 draw 的 cascade 都调用 `TerrainRenderPass::render_shadow`。
  - `TerrainRenderPass::render_surface` 每次调用都执行 `make_lod_view`、`build_terrain_lod_batches`、packed instance 生成和 `ensure_instance_buffer`。
  - `RenderScene::rebuild_terrains_from_scene` 同步调用 `AssetDatabase::load_terrain_by_path`，随后请求并接受 `TerrainRenderAsset`。
  - `TerrainRenderAsset::accept_snapshot` 遍历全部 Component，立即生成每 Component 高度和两张权重 vector；`finalize_gpu_resources` 再创建资源并逐 Component 更新高度 buffer。
  - `ScenePresentationSubsystem` 用单一 `bool render_scene_valid` 表示所有 rebuild 结果；false 时下一帧再次完整 rebuild 并再次输出 error。
- Modules:
  - Function Asset 拥有不可变 snapshot、容器解码和共享 async future。
  - Function Render 拥有 TerrainRenderAsset、LOD、draw preparation 和 shadow/GBuffer 编排。
  - ScenePresentation 负责从 mutable Scene 更新 RenderScene 和 readiness snapshot。
- Data flow:
  - 场景 Terrain path → 同步 AssetDatabase load → immutable snapshot → TerrainRenderAsset eager pack → GPU finalize → RenderTerrainProxy → VisibleRenderFrame → 每 pass 重复 Terrain draw preparation。
- Known constraints:
  - production layout 固定 8193² samples、32×32 Component、每 Component 257² samples。
  - 当前同一 scene/view 只消费第一个有效主 Terrain。
  - shadow 与 GBuffer 已明确共用主视图 LOD 输入；shadow context 只替换 `view_projection` 和 atlas viewport/scissor。
  - sunlight cascade 0 每帧全量 draw；outer static-cache 命中时以 `DynamicOnly` 调用 callback，Terrain 会跳过；普通非-sunlight directional 的 4 个 cascade 每帧仍以 `All` 调用。

## Proposal

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Terrain draw preparation | 新增 Function-internal、frame-bounded 的 immutable `TerrainPreparedDraw`：一次生成 LOD result、batch offsets、packed instances 与单一 instance buffer；GBuffer 和所有允许的 shadow pass 只消费该对象 | `Function/Render/TerrainRenderPass.*`、`TerrainLod.*`、`SceneRenderer.*` |
| Shadow integration | 组合 shadow callback 捕获同一 prepared draw；各 cascade 只替换 object-to-clip/view context 并提交已有 batches。`DynamicOnly` 继续跳过 Terrain | `Function/Render/SceneRenderer.*`、`DirectionalLightShadowPass.*`、`SunLightShadowPass.*`（仅必要契约/测试） |
| Terrain CPU upload pipeline | `accept_snapshot` 只验证 Component identity/shape 并捕获 const Component 引用，不再 eager 构造双权重 vector；高度 pack 由既有 worker 能力生成有界 chunk，render-thread 按 byte/time work budget 消费 | `Function/Render/TerrainRenderAsset.*`、`RenderAssetManager.*`、必要的 Function Asset helper |
| Weight residency | 未绘制权重保持隐式 material lane 0；只有 Component 进入/更新 weight atlas slot 时才生成该 Component 的两路 RGBA8 payload，继续复用单份 raw staging buffer 和“一 graph 一 Component”合同 | `Function/Render/TerrainRenderAsset.*`、`TerrainRenderPass.*` |
| Shared fallback material | Renderer/RenderAssetManager 持有一份 Terrain fallback texture arrays；TerrainRenderAsset 只持共享引用，不重复构造同内容 full-mip arrays | `Function/Render/RenderAssetManager.*`、`TerrainRenderAsset.*` |
| Terrain resolve state | Terrain rebuild 返回 typed `Ready/Pending/Failed` 结果和稳定 diagnostic；共享 async request 只启动一次，Pending 后由 future/catalog/publication state 变化驱动，不在每帧同步 `get()` | `Function/Render/RenderScene.*`、`Function/Asset/AssetDatabase.*`、`ScenePresentationSubsystem.*` |
| Readiness / logging | RenderScene 的通用有效性与 Terrain resolve/readiness 分离；Pending 保留其他 Scene 内容和最后有效 Terrain proxy，Failed transition 只输出一次 path+reason，automation readiness 对 Pending/Failed 均 fail closed | `Function/Render/ScenePresentationSubsystem.*`、`ScenePresentationReadiness.*`、tests |
| Evidence | 增加准备次数、pending payload bytes、height upload progress 与 shadow draw 次数的 CPU contract；临时/非 bless A/B 继续产出 PerfGate JSON，不修改 baseline | tests、`docs/specs/features/terrain.md`、相关 render/asset spec |

### API / contract changes

1. **Prepared draw lifetime and key**
   - 每个 `VisibleRenderFrame + primary SceneRenderViewContext + Terrain snapshot/publication identity` 最多产生一个 prepared draw。
   - prepared draw 生命周期覆盖当帧 RenderGraph 所有捕获它的 pass；不得跨 frame 复用 mutable instance-buffer slot，也不得被第二 viewport 错用。
   - identity 至少覆盖 scene runtime/content epoch、entity id、snapshot pointer + content/residency generation、world transform、主视图矩阵/camera、output extent 和 render frame index。
   - shadow pass 的 light view-projection 不进入 LOD key，因为当前批准合同就是主视图 LOD；它只进入每次 draw 的 constants。

2. **Instance-buffer ownership**
   - 同一 prepared draw 只申请/更新一次物理 ring slot。GBuffer 与 N 个 shadow pass 绑定同一 buffer 和 batch offsets，不得因 pass 数增加逻辑 slot。
   - ring 的 GPU lifetime 继续遵守现有 frame completion 规则；不能为了共享而把 GPU-only storage buffer 改成 CPU upload heap。
   - 无 Terrain、不可见 Terrain 或 `casts_shadow=false` 仍快速返回，不创建无用 shadow preparation。

3. **Bounded CPU upload**
   - Pending height work item 只保存 const Component 引用、coord、generation 和 pack 状态；禁止同时持有全部 1024 个 Component 的 packed height + 两路 weight vector。
   - worker producer 和 render-thread consumer 之间有明确 byte cap；默认 cap 以少量 Component chunk 计，不以“等待固定帧数”定义成功。
   - 每次 scene prepare 按 byte 与 wall-clock work budget 消费；达到预算后保留 Pending，下一次 prepare 继续。完整 generation 发布仍要求全部 height upload 完成。
   - stale generation 的 worker/chunk 不得发布；取消或 asset replacement 后丢弃时必须释放其预算并保持上一已发布 generation。

4. **Lazy weights and fallback resources**
   - 空 `component.weights` 等价于 material lane 0 = 255，不生成 per-Component RGBA payload。
   - atlas residency 请求非空/已绘制权重时才 quantize/pack 当前 Component；payload 在该次 graph 消费后释放。
   - fallback base-color/normal/ORM arrays 以 Renderer device lifetime 为 key；device reset/shutdown 后统一释放，不能形成跨 device 静态单例。

5. **Typed Terrain resolve result**
   - `Ready`：snapshot、render asset 和 proxy identity 均可安全提交。
   - `Pending`：共享 async load、CPU pack 或 GPU publication 尚未完成；不是 error。Scene 可以继续提交其他 primitives，capture/readiness 保持 Pending。
   - `Failed`：稳定非重试失败或资源 publication 失败；包含 canonical asset path 和 last-error。Scene 其他内容仍可见，但 Terrain readiness 为 Failed，自动化不能宣告成功。
   - 同一 asset/revision/status 未变化时不重新创建 request、不重跑同步 decode、不重复 log；catalog/revision/generation/future completion 才触发下一状态转换。

6. **Diagnostics and stop rules**
   - Pending transition最多一条 info/debug，Failed transition最多一条 error；恢复 Ready 可记录一条 info。无状态变化时为零日志。
   - telemetry 至少能区分 Terrain CPU prepare、Terrain shadow draw count、pending height bytes/components 和 published generation；不要求本包扩展 GPU pass schema。
   - validation/debug-layer error、Terrain generation 永久 Pending、ready 状态仍有 pending upload、A/B threshold 超标或任一 PerfGate FAIL 都阻断完成。

### Backend impact

- Vulkan/DX12 共用同一 prepared draw、lazy payload 和 typed resolve contract；不允许 backend 特化泄漏到 Function Scene/Editor。
- 两后端仍使用各自既有 staging/upload 实现；本包只改变 upload 的数量、大小和调度，不改变 RHI API。
- DX12 A/B 显示 shadow-on GPU P50 约 `3.256 ms`，高于 Vulkan `0.722 ms`。CPU 修复后必须重新测量；若 CPU 已达标而 DX12 GPU 仍使总目标失败，只能以新的独立根因和 SDD 处理，不能在本包静默降级级联质量。

### Performance

- 结构性门槛：同一 frame/view/Terrain 的 `build_terrain_lod_batches` 和 instance-buffer update 都必须为 1 次，与 shadowed directional light/cascade 数无关。
- CPU A/B：shadow-on `cpu_frame_time_ms.avg <= 1.5 × shadow-off`，Vulkan/DX12 各自独立判断。
- Loading A/B：相同配置下 Terrain 与无 Terrain 的进程 wall-time 增量 `<= 5 s`；private peak 增量 `<= 768 MiB`。首轮 shader/driver cache 不与热态样本拼接，必须同一预处理合同下成对采样。
- 不修改 Standard/VFP baseline 或 threshold；`RunPerfGate Standard` 任一 FAIL 阻断，WARN 交用户裁定。

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| Focused RED/GREEN | prepared draw 一次性、N cascade 复用、DynamicOnly skip、bounded upload、lazy empty weights、shared fallback、typed resolve/log transition | `RunTests.bat Debug --test-case="*Terrain*"` 加精确新 filters |
| 全量单测 | Debug/Release、legacy bridge、内存释放 | `RunTests.bat Debug`；`RunTests.bat Release` |
| 双配置构建 | Function Render/Asset、Editor/Sandbox | `build_editor.bat Debug/Release`；`build_sandbox.bat Debug/Release` |
| 架构/计划 | Base ← Graphics ← Function ← Editor/Sandbox 与 SDD/spec | `RunArchGate.bat`；`AIDevDoctor.ps1 -Mode ValidatePlan` |
| Readiness | Editor/Sandbox × Vulkan/DX12，完整 Terrain generation 后信号退出 | `run.bat all Debug --smoke-test-seconds=120` |
| Validation | bounded upload、resource lifetime、shadow reuse | Terrain 场景 Vulkan validation + DX12 debug layer/GPU validation |
| 渲染回归 | shadow/GBuffer 画面及跨后端 | `RunRenderGate.bat`（non-bless） |
| 性能回归 | 通用四组合 | `RunPerfGate.bat -Profile Standard`（non-bless） |
| Terrain 专项 A/B | none/off/on、同场景同相机双后端，CPU/GPU/memory/wall time | readiness/perf telemetry 临时副本；不改用户场景、不 bless |
| 日志审计 | Pending 无 error、Failed transition 单条、fresh reject 0 | 自动测试 + 上述运行产生的 session logs |

## Task breakdown

1. 固化现有 RED：N 个 shadow pass 触发 N 次 LOD/instance prepare、空权重 eager 分配、RenderScene Pending 每帧 error；记录当前 A/B report 作为非 baseline 诊断。
2. 引入 immutable prepared draw，先让 GBuffer/ordinary/sunlight 共用 CPU batch，再验证两后端画面不变。
3. 将 instance-buffer ring 从 pass-count 归属改为 prepared-draw 归属，补 frame/view/stale identity 和 GPU lifetime 测试。
4. 删除 eager weight packing；按 atlas residency 生成单 Component payload，保持现有 raw staging/graph barrier 合同。
5. 实现有界 height pack/upload producer-consumer，并把 fallback arrays 提升为 Renderer-owned shared resources。
6. 引入 typed Terrain resolve/readiness 和状态转换日志；Pending 保留其他 Scene 内容，Failed fail closed automation。
7. 跑 focused/full/build/ArchGate/AIDevDoctor，再按协调窗口串行双后端 readiness、validation、RenderGate、Standard PerfGate 和 Terrain none/off/on A/B。
8. 回写 `docs/specs/features/terrain.md`、相关 render/asset spec 与本 SDD validation record；未达到全部门槛不得标记 Done。

## Risks

| Risk | Mitigation |
| --- | --- |
| prepared draw 捕获错误 viewport 或 stale snapshot | key 覆盖 frame/view/snapshot/transform/output；对象 immutable 且 frame-bounded；多 viewport/asset replacement RED |
| buffer 共享导致 CPU 覆盖 GPU 使用中的数据 | 保留 frame completion/ring 规则，只减少同帧逻辑 slot，不缩短物理 lifetime |
| 异步 pack 完成顺序破坏 generation 原子性 | chunk 携带 asset+generation+coord；stale 结果丢弃；完整 generation 才 publish |
| 分批上传让 Terrain 长时间不可见 | 以 byte/wall-clock budget 保持 Editor 响应，同时给出 5 秒完成门槛；readiness 不用固定帧数 |
| lazy weights 首次靠近 Component 时产生 hitch | 单 Component staging、wall-clock budget 和 atlas residency 测试；空权重走零分配 fast path |
| shared fallback 跨 device 泄漏 | 由 Renderer/RenderAssetManager 实例持有并随 device shutdown 释放，不用全局 static |
| Pending 被误当成功导致错误截图 | Scene 可继续显示，但 Terrain capture/readiness 必须 Pending；automation deadline 仍 fail closed |
| DX12 GPU shadow 成本在 CPU 修复后仍偏高 | 保留独立 GPU 指标；不降画质，未达总目标则另开精确 SDD |

## Open questions

- 无。聊天阶段已确认：保留全部 shadow 语义，采用 CPU 准备复用与有界加载，不以降低画质换性能。
