# SDD-0001: 渲染验证安全网（RenderGate：帧 dump + golden 回归 + 双后端 diff）

## Status

Implementing（2026-07-03 批准；open questions 决议：golden 分后端各存一份；v1 只用默认 Sandbox 场景）

## Context

当前视觉正确性完全依赖人眼确认（`docs/VERIFY.md` 标记的最大自动化缺口）。这封顶了 AI 自主修渲染 bug 的能力，也让 SSAO banding、体积光 banding 这类修复无法自动回归。本 SDD 建立最小可用的自动视觉验证闭环，风险级别 S2（涉及 RHI 接口新增 + 双后端实现）。

## Goals

- Sandbox 支持自动化模式：渲到指定帧号 → 把最终 LDR 画面 dump 成 PNG → 退出
- Golden 回归：dump 图与仓库内 golden image 做 SSIM 感知对比，低于阈值判 FAIL
- 双后端交叉 diff：Vulkan 与 DX12 渲同一帧互相对比，不一致即 bug（免维护的 oracle）
- 一条命令跑完整个矩阵并出报告：`RunRenderGate.bat`

## Non-goals

- 真 offscreen/headless（swapchain 强依赖可见窗口；v1 用普通窗口自动运行，不解决无显示环境）
- Editor viewport 的截图验证（Sandbox 先行，Editor 后续 SDD）
- CI 集成、多机 golden 管理（golden 先按本机水位 bless）
- HDR 中间 RT 的数值级比对（只比 tone-map 后 LDR 最终画面）

## Current implementation

- Entry points: `project/src/engine/EntryPoint.h:58-124` 已有 `--smoke-test[=N帧]`、`--smoke-test-seconds` 解析，`Application::set_max_frame_count()` 驱动退出
- 回读基础: Vulkan `VulkanStagingBuffer(bReadback)`（`Graphics/Vulkan/VulkanStagingBuffer.h`）、DX12 `DX12StagingHeapKind::Readback`（`Graphics/DirectX12/DX12StagingBufferPool.h`）均已存在，但无 RHI 层公开 readback API
- 最终画面: `PostProcessToneMapPass` 输出到 swapchain backbuffer（`Renderer::get_back_buffer()`，Sandbox 无 ImGui 合成层）
- 图像编码: `project/thirdparty/stb/stb_image_write.h` 已在库
- 确定性: TAA jitter 由 `frame_index` + Halton 驱动（`TemporalAAConfig.h`），进程从零跑到固定帧号则序列完全一致；shader 无 time 驱动动画；相机 pose 来自 scene json
- 编排参考: `scripts/RunPerfGate.ps1` 的进程启动、Engine.ini 后端切换、报告目录范式
- Known constraints: 场景 `scene_config` 无命令行覆盖；Sandbox 加载的场景当前不可从命令行指定

## Proposal

### Module changes

| Module | Change | Files |
| --- | --- | --- |
| Graphics（RHI 抽象） | 新增 backbuffer 回读接口（见下） | `DynamicRHI.h` 或 `RHIResource.h` + 新实现文件 |
| Graphics/Vulkan | 实现回读：barrier 到 TRANSFER_SRC → copy 到 readback staging → fence 等待 → map | `Vulkan/` 若干 |
| Graphics/DirectX12 | 实现回读：transition 到 COPY_SOURCE → CopyTextureRegion 到 readback heap → fence → Map | `DirectX12/` 若干 |
| Function/Render | `RenderDevice`/`Renderer` 暴露 `request_backbuffer_capture(callback/路径)`，present 前触发回读，像素转 RGBA8 后经 stb 写 PNG | `RenderDevice.*`、`Renderer.*` |
| EntryPoint / Application | 新参数：`--dump-frame=<png路径>`（配合已有 `--smoke-test=N`，在最后一帧截图）；`--scene=<scene.json路径>`（Sandbox 覆盖默认场景） | `EntryPoint.h`、`Application.*`、`sandbox/App/` |
| 新工具 AshImageDiff | 独立 console exe（premake 新 project，仅依赖 stb）：加载两张 PNG，输出 SSIM、逐像素统计、diff 热力图 PNG；退出码表达 pass/fail | `tools/imagediff/`（源码）+ `premake5.lua` |
| 脚本 | `scripts/RunRenderGate.ps1` + 根 `RunRenderGate.bat`：矩阵编排、golden 对比、交叉对比、报告、`-BlessGolden` | `scripts/`、根目录 |
| Golden 存储 | `tools/render/goldens/<scene>/<backend>.png`，入库 | `tools/render/` |

### API / contract changes

- RHI 新增最小接口（草案）：`DynamicRHI::read_back_texture(RHITexture* src, ReadbackResult& out)`，同步语义（内部 flush + fence 等待），仅支持 backbuffer 格式，明确文档标注"验证/调试用途，非热路径"
- 命令行契约新增 `--dump-frame` / `--scene`，写入 `docs/CODEBASE_MAP.md` 入口节
- 正常渲染路径行为零变化：不请求 capture 时不产生任何额外拷贝/barrier

### Backend impact

- Vulkan：swapchain image 需带 `VK_IMAGE_USAGE_TRANSFER_SRC_BIT`（检查现有创建 flags，缺则补，属低风险扩展）
- DX12：backbuffer PRESENT→COPY_SOURCE→PRESENT 状态往返
- 两后端 readback 像素格式统一转 RGBA8（处理 BGRA/RGBA 与 sRGB 差异，转换在 CPU 侧做）

### Performance

- 仅在 `--dump-frame` 显式请求时发生回读，正常运行与 PerfGate 路径零开销
- PerfGate 阈值无需调整；本改动自身需跑 PerfGate 全矩阵确认无回归

## Verification plan

| 验证 | 覆盖 | 命令 |
| --- | --- | --- |
| 构建 + 现有门禁 | RHI 改动无回归 | `RunPerfGate.bat -Profile Standard` |
| dump 确定性 | 同后端连续两次 dump 逐位一致（或 SSIM≈1.0） | 跑两次 `run.bat sandbox vulkan Debug --smoke-test=120 --dump-frame=a.png/b.png` + AshImageDiff |
| 交叉后端 | Vulkan vs DX12 同帧 SSIM ≥ 阈值（初始 0.99，实测后定） | `RunRenderGate.bat` |
| golden 回归 | 对已知 bug 修复（如 SSAO banding）能表达差异 | 人工制造一次退化验证 gate 会 FAIL |
| validation 干净 | 回读路径无 validation/debug-layer 报错 | Engine.ini 开启两侧 validation 跑一轮 |

## Task breakdown

1. RHI readback API + Vulkan/DX12 实现 + RenderDevice capture 钩子（不接命令行，行为零变化）——验收：单元性冒烟，validation 干净
2. `--dump-frame` / `--scene` 参数 + PNG 落盘——验收：手跑两后端各出一张正确 PNG
3. AshImageDiff 工具（premake project + SSIM + diff 热力图）——验收：相同图 SSIM=1，人为改动图能量化
4. `RunRenderGate.ps1/.bat` + golden 目录 + `-BlessGolden` + `summary.md/json` 报告——验收：一条命令全矩阵跑通，报告落 `Intermediate/test-reports/render-gate/<ts>/`
5. 回写文档：`VERIFY.md` 变更矩阵把"人眼确认"替换为 RenderGate 命令、`CODEBASE_MAP.md`、`AGENTS.md` 命令节——验收：文档与实际行为一致

每步独立提交、独立可验证；步骤 1 是唯一动 Graphics 层的步骤。

## Risks

| Risk | Mitigation |
| --- | --- |
| swapchain usage flags 改动引后端兼容问题 | 仅加 TRANSFER_SRC/允许 COPY_SOURCE，validation 全开验证；PerfGate 回归 |
| 双后端存在合法的微小像素差（光栅化规则、精度） | 用 SSIM 感知阈值而非逐位比较；阈值以实测数据定，先宽后紧 |
| 换机器/驱动导致 golden 漂移 | golden 记录生成环境；漂移时 `-BlessGolden` 重建并人工过目一次 |
| 窗口被遮挡/最小化影响渲染 | gate 脚本运行期间不最小化窗口；文档写明约束 |
| readback 同步实现引入死锁/卡顿 | 同步接口只在退出前最后一帧调用一次，路径极窄 |

## Open questions

- golden 是否分后端各存一份（初始方案：是，交叉 diff 另算），还是只存一份以 DX12 为准？
- 初始 golden 场景集：只用默认 Sandbox.scene.json，还是同时准备 feature 开关变体场景（AO-only、Bloom-only…）？建议 v1 只用默认场景 + 后续按 feature spec 增补。
