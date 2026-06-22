# AshAgent 开发流程系统设计

> 本文是一次设计讨论的结论存档,描述把 HASHEAEngine 仓库做成一个「开发流程 agent」的整体蓝图。
> 状态:设计阶段,尚未实现。所有「待定 / 前置清单」项需在落地前逐一确认。

## 一、定位

- 单一代码树,引擎 / 编辑器 / 游戏不分仓。
- 用**角色 + 权限的社会化边界**替代物理分仓:失去编译期硬隔离,换取统一代码树、统一构建、改引擎与用引擎同上下文。
- 系统主要给 **AI(Claude session)** 使用;人若想直接改代码,走 ReadOnly 模式自己手写。

## 二、两层身份

- **User**:真人 / git 账号。追责主体,进 commit。
- **Role**:AI session。一会话一角色(不可中途切换,换角色开新会话)。权限与记账的主体。
- 同一真人可开多个不同角色的会话;本地因此可有多份 Role 文件,会话启动时选一个激活。

## 三、入口模式(SessionStart hook 二选一)

- **A. AshAgent**:选角色 + claim 一条需求 → 获得写权(限本模块);**所有代码写操作必须绑定一条 active 需求**。
- **B. ReadOnly**:AI 零写权,只做分析 / 讲解,人在 IDE 手写。

入口模式的选择决定 PreToolUse hook 的行为:
- ReadOnly → 对所有 Edit/Write 一律 block。
- AshAgent → 检查「当前操作绑定了 active 需求 + 路径在角色可写范围」。

## 四、角色

- **作者型**:EngineDev / EditorDev / GameDev / QA —— 各自模块可写,走需求系统。
- **QA 特权**:唯一能把需求推到 `verified` 的角色;也能发需求 / bug 单;约束 `verifier ≠ implementer`。
- **公共验证**:是**版本化的脚本 / slash command / subagent**(不是某人 Role 文件里的自然语言),任何角色做完自己的活后均可调用。命名上建议把「公共验证脚本/流程」与「QA 这个 Role」区分清楚,避免混淆。

## 五、权限模型

- **读**:所有角色全项目可读。
- **写**:仅以下五个目录受严格角色保护;其余位置全部公共可写。
  - `Engine`、`3rdparty`、`Editor`、`Sandbox`、`Game`
- **执行**:PreToolUse hook 读「角色 → 可写路径 JSON」+ 会话 active 需求状态。
  - 越界写 = **软警告 + 记账**(不硬 block;账并入需求系统)。软警告 = 不拦截但强制留痕,记录「谁 / 什么角色 / 改了哪个非本模块 / 为什么」,作为「绕过需求系统走捷径」的可追溯证据。
- 公共可写区因此覆盖:`product/config/Engine.ini`、`premake5.lua`、根 `README.md`、`docs/`、需求文件目录、Role 文件等跨角色都要碰的文件。

### 待定
- `3rdparty` 写权归属:归 EngineDev,还是单独设为「需特批的最严格区」(改第三方库常牵连所有人)。

## 六、数据 / 能力分离

原则:**md 只是壳(我是谁、我的性格、我能调哪些能力),逻辑全在版本化的脚本与数据里。** 这样多人协作时没有人能本地偷偷改掉公共能力的口径。

- **权限** = JSON(角色 → 可写路径)。
- **需求** = 结构化 JSON(真源)+ 渲染出的 md 视图(给人 / agent 读)。真源必须是结构化数据,为后期 GUI application 准备;不要让需求真源是 markdown。
- **公共验证** = slash command / subagent。
- **角色** = 模板(入库,团队共享、演进)+ 本地 Role 文件(`.gitignore`,可调性格 / 本地 hook;本地 hook 写进 `settings.local.json`)。
- **两份 root md** = AGENTS.md(共享内容唯一源)+ CLAUDE.md(`@AGENTS.md` 导入 + 仅 Claude 专属增量)。真正双份的差异部分加 hash 校验(脚本 / CI / Stop hook),不一致即报错,用机器查漂移而非靠纪律。

## 七、需求系统

### 类型
- **需求订单**(功能)
- **bug 订单**:`crash` / `性能` / `表现`

订单类型直接决定走验证流水线的哪条分支,以及能否 AI 自动判到底。

### 录入
做一个 **skill**:自然语言描述 → 生成规范 JSON 需求 → 合规审核(字段是否齐、验收标准是否可测、订单类型是否正确)→ 落盘 + 渲染 md 视图 →(可选)立即 claim。审核不过则打回补全。让「造需求」成本趋近于零,因此「写必须绑需求」不构成负担。

### 状态机
```
draft → reviewed → claimed → in-progress → done / awaiting-visual-approval → verified
```
- `reviewed`:reviewer subagent 查模板完整性通过。
- `done`:crash / 性能 / 功能 类开发自测通过。
- `awaiting-visual-approval`:表现订单专属,机器已尽力,卡在等人眼。
- `verified`:仅 QA 角色可迁移;`verifier ≠ implementer`。

### 并发(无服务器阶段的「伪锁」)
- claim 前 `pull --rebase`;claim 后只 commit + push 该需求的状态变更(状态流转与代码提交分开)。
- claim 状态落在**每条需求自己的文件**里(不放全局大列表),把冲突面积降到最小——只有「抢同一条」才冲突。
- push 被拒(non-fast-forward）= 有人先 claim = 自动 pull → 重新检查这条是否仍 available → 不可用则让用户另选。即把 git reject 当锁失败信号处理。
- 后期有服务器后换成真文件锁,协议形状不变。

## 八、验证系统(订单类型 × 流水线)

这是原本 TBD 的部分,现已成形。核心思路:**需求模板内嵌「验收标准 + 所需验证矩阵」,把需求系统和验证系统焊死。** 复现 / 捕获字段直接映射现有 `run.bat` 入参与 scene/camera override,AI 读字段即可拼命令。

| 订单类型 | 判据性质 | AI 自测到 | 终态 | QA 做什么 |
|---|---|---|---|---|
| 需求·功能 | 半客观 | 构建 / 冒烟 / 层干净 | done | 看功能对不对 |
| bug·crash | 客观二值 | done(强制先复现 → 修 → 不崩) | done → QA 盖章 | 防作弊式修复,盖章 |
| bug·性能 | 客观连续值 | done(本机自比达标,灰区重跑) | done → QA 盖章 | 复核数字 + 场景没换,盖章 |
| bug·表现 | 主观 | 仅前置门 | awaiting-visual-approval | 对照期望看双端 before/after |

### crash 订单
- 复现分档:**A**(启动 / 冒烟内崩)、**B**(特定场景·配置·后端)、**C**(交互触发)。
  - A/B → AI 自动复现 + 回归。
  - C → `manual-repro` 转人工,但 `manual_steps` 必须写清完整交互逻辑(谁、在哪个面板、什么顺序、点 / 拖什么)。
- 复现字段直接映射 `run.bat` 入参:`target / backend / config / scene / engine_ini / app_args`。
- 流水线:**强制先复现**(跑不崩 = `cannot-reproduce` 转人)→ 修 → build → 同 repro 回归(不崩 = 过)→ 共享路径 / both 则补四格冒烟 → 扫日志无 layer error → done。
- 终态:必须 QA 显式盖章;QA 瞄 diff 防「用 catch 吞异常假装不崩」式修复。

### 性能订单
- 判据模型:**本机自比**(决定过不过,消除机器差异)+ **共享趋势**(看长期)。
- 共享趋势按 **device_fingerprint 分桶**(cpu / gpu / driver / ram / os build),同指纹内才连线对比;绝对值永不跨设备比,跨设备只看归一化百分比。
- 每个设备各自维护自己的 baseline 条目(key 带设备指纹)。
- `goal_kind` 三态全要:
  - `no_regression`:改了别的功能,别让性能掉(所有指标在阈值内)。
  - `improve_by`:本单就是来优化的(目标指标改善 ≥ X% 且其余不回归)。
  - `meet_cap`:绝对值达标(如内存别超 cap)。
- 流水线:claim → 录 before → 改 → build → 录 after → 按 goal_kind 判 → borderline 落在阈值 ±2% 灰区则**重跑一次**确认 → 上报趋势(带设备指纹)→ done。
- 终态:QA 复核数字 + 确认 `scene` 字段没被偷换(最易作弊点)。
- PerfGate 现有指标:CPU frame time(avg/p95/p99)、private bytes peak、engine heap peak、draw call count + 绝对内存 cap。

### 表现订单
- 判据主观,AI 判不了,只能归档证据转人。设计重心是「让人判得高效、有依据、可复现」。
- 必填三要素:`expectation`(期望视觉,具体)+ `capture_spec` 的固定 `scene` / `camera` / `frame`(保证 before/after 可比)。
- 流水线:claim → 抓 before(rdc + 截图)→ 改 → build → **前置门**(build / 不崩 / 层干净 / 双端都抓到帧)→ AI 客观线索 → `awaiting-visual-approval` → QA 对照 expectation 看 before/after。
- **双后端**:DX12 + Vulkan 各截一份给人对比(视觉差异常单后端,且双后端一致性是核心价值)。
- **AI 客观线索**(仅导航,不作判据):截图像素 diff 的热区位置、RenderDoc MCP 取的客观量(某 RT 格式 / 尺寸有无意外变化、某 pass draw count、直方图偏移等)。降低人的判断成本,但不决定 verified。
- 归档路径建议:`Intermediate/renderdoc_captures/<订单ID>/{before,after}/`,文件名带需求 ID,需求文件记录 rdc 路径。

## 九、上线前的前置清单

1. PerfGate `baselines: {}` 为空,需 bless 首批 baseline。
2. baseline schema 从扁平结构改为按 `device_fingerprint` 分桶 / 分键。
3. 权限映射 JSON(含 `3rdparty` 归属决定)。
4. 两份 md 的 `@` 导入 + hash 校验机制。

## 十、仍待定 / 可 later

- `3rdparty` 写权归属。
- 需求系统 GUI application(后期)。
- 共享趋势的 device_fingerprint 具体取哪些字段。
- 自动视觉回归(golden image diff):明确**先不做**,靠人眼 + rdc;待流程跑顺后,可优先用 RenderDoc MCP 取的稳定客观量(直方图 / RT 元数据)做自动比对,比像素级 diff 稳。

## 现有可复用的手柄(落地基础)

这套设计不是从零造,仓库已有的自动化即「驾驶手柄」:
- 构建:`build_editor.bat` / `build_sandbox.bat` / `scripts/InvokeMSBuild.ps1`
- 运行 + 冒烟:`run.bat [target] [backend] [config] [--smoke-test-seconds=N]`(自动备份 / 还原 `Engine.ini`、自动切后端)
- 验证矩阵:`run.bat all` = Sandbox/Editor × Vulkan/DX12 四格
- 性能门禁:`scripts/RunPerfGate.ps1` + `tools/perf/perf_gate_baselines.json`
- 上下文采集:`scripts/AIDevDoctor.ps1`(只读,产出 context/report/validation-plan)
- 规则库:`tools/ai-dev/rules/*.json`(路径风险、变更分组、验证规则)+ `tools/ai-dev/templates/*.template`
- 截帧分析:RenderDoc MCP
- 边界基线:根 `AGENTS.md`(当前面向 Codex)
