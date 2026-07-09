# SDD-2026-07-08-dxc-drop-atl-dependency: DXC 路径去除 ATL 依赖（CComPtr → WRL ComPtr）（Mini SDD）

## Status
Done（2026-07-08 实施完成，验证通过）

## Context

CI（GitHub Actions windows runner）build-and-test 失败：`error C1083: Cannot open
include file: 'atlbase.h'` ×5。根因：runner 的 VS Enterprise / MSVC 14.44 未安装
ATL 组件；本地开发机装了 ATL 所以从未暴露。引擎对 ATL 的唯一使用是 DXC shader
编译路径的 `CComPtr`（DXCHelper.h/.cpp、VulkanShaderCompiler.h/.cpp，约 24 处；
DXCIncludeHandler.h 的 atl include 是死引用；DX12 后端无 CComPtr）。

方案对比：
1. **CComPtr → `Microsoft::WRL::ComPtr`（`<wrl/client.h>`，Windows SDK 自带，
   任何 runner 必有）——根除 ATL 依赖**（选定）
2. CI 装 ATL 组件——治标，且引擎无理由依赖 ATL

## 替换语义对照

| CComPtr | ComPtr | 说明 |
| --- | --- | --- |
| `.p` | `.Get()` | ComPtr 无公开裸指针成员 |
| `.Release()` | `.Reset()` | ComPtr 的 `Release()` 语义不同（返回计数） |
| `obj->QueryInterface(&ptr)` | `obj->QueryInterface(IID_PPV_ARGS(&ptr))` | 模板 Q** 推导对 ComPtrRef 失败 |
| 隐式转 T* 传参 | 显式 `.Get()` | ComPtr 无隐式转换 |
| `&ptr` out-param / `IID_PPV_ARGS` / `.Attach()` / bool 上下文 | 不变 | 兼容 |

## Goals

- `DXCIncludeHandler.h`：删除死的 atlbase/atlcomcli include（类自带非 ATL 的
  QueryInterface/AddRef/Release 实现）
- `DXCHelper.h/.cpp`、`VulkanShaderCompiler.h/.cpp`：全部 `CComPtr` →
  `Microsoft::WRL::ComPtr`，按上表机械替换，无行为变化
- 仓库内不再出现 `atlbase.h` / `atlcomcli.h` / `CComPtr`

## Non-goals

- 不动 DXC 编译逻辑、include handler 行为、shader 绑定约定
- 不动 DX12 后端（本就用 WRL ComPtr 风格 / 裸指针，无 ATL）

## Verification

- `build_editor.bat Debug` + `build_sandbox.bat Debug`（双后端构建）
- `RunTests.bat`、`RunArchGate.bat`
- `RunRenderGate.bat`（shader 编译路径 = 渲染改动必跑）

## 执行结果

- 按计划替换完毕，`project/src` 内 `CComPtr` / `atlbase` / `atlcomcli` 清零；
  DX12 侧本就是 WRL ComPtr（`DX12Wrapper.h` 别名），全仓风格统一
- 计划外发现：`atlbase.h` 之前顺带引入 COM 基础头，删除后 `IUnknown` 未定义
  （项目 `WIN32_LEAN_AND_MEAN` 下 `Windows.h` 不带 COM）——
  `DXCIncludeHandler.h` 显式补 `#include <unknwn.h>`
- 跨平台性无退化：涉及文件本就 Windows-only（`Windows.h` + `ASH_HAS_DXC`），
  WRL/unknwn 是 Windows SDK 标配，比 ATL（需单独安装组件）可用性更宽
- 验证：`build_editor.bat Debug` / `build_sandbox.bat Debug` 0 错误；
  `RunTests.bat` 16 用例 116 断言全绿 + All Memory Free；`RunArchGate.bat` PASS
  （35 legacy warns 不变）；`RunRenderGate.bat` PASS（vulkan 0.999919 /
  dx12 0.999916 / cross 0.999442）
