# UEEditorAutomation

UE Editor C++ 插件，用于声明式资产自动化：蓝图、DataAsset、材质、
曲线、GAS、审计、只读蓝图/资产分析、资产复制、引用重写。

## 文档

项目内置 `.agents/skills/ue-editor-automation/`，AI 遇到“分析蓝图 / 创建蓝图 /
修改蓝图 / 排查蓝图或资产 bug / 复制资产 / 重写引用”等请求时优先按该 skill
调用本工具；不需要先阅读 Quickstart。其余文档按职责拆分：

- `Docs/Current_Truth.md` —— 当前真实能力、限制与实现行为
- `Docs/Task_Interface.md` —— JSON 任务协议与每个 task 的字段
- `Docs/Automation_Workflow_Design.md` —— 原子能力、AI 编排层与工作流模板设计
- `Docs/AI_Workflow_Quickstart.md` —— 详细工作流说明；当 skill 信息不足时再阅读
- `Docs/Tech_Debt.md` —— 已知限制与待重构点
- `Docs/CI_Integration.md` —— 辅助脚本说明

## 默认任务目录

```
C:/UEAutomation/
  tasks/{inbox, working, done, failed}/
  results/
  logs/
```

把 JSON 任务文件丢到 `tasks/inbox`。插件会移到 `working`，写
`results/<task_id>.result.json`，再把任务移到 `done` 或 `failed`。

## 样例

```
Samples/valid/      预期成功的任务
Samples/invalid/    预期返回结构化错误的任务
```

## 配置

`UEditorAutomationSettings`（项目设置）暴露：daemon 开关、轮询间隔、
路径、协议版本、白名单文件、模板文件、Phase 4 分析上限。

运行时策略：

```
Plugins/UEEditorAutomation/Config/UEEditorAutomationWhitelist.json
Plugins/UEEditorAutomation/Config/UEEditorAutomationTemplates.json
```

白名单中字段为空数组表示"不限制" —— 调宽自动化范围请改 JSON，
不要重新编译 C++。

蓝图写入任务可以在运行中的编辑器里执行 Blueprint compile + save，
这用于暴露蓝图编译错误，不属于 C++ 构建流程。

## 打包

给只使用编辑器的人分发时，先由有编译环境的人执行 BuildPlugin 打包。
脚本参数：

- `ProjectRoot`：必传。可传项目根目录，例如 `D:\YourProject`；也可传包含
  `Engine` 和项目目录的源码根目录，例如 `D:\YourSourceCheckout`
- `Configuration`：打包配置，默认 `Development`。`Development` 使用
  RunUAT BuildPlugin；`DebugGame` 使用 UBT 编译项目 Editor target 后整理插件目录
- `TargetName`：DebugGame 模式下的 Editor target，默认 `<uproject 文件名>Editor`
- `PackageArgs`：传给 Development BuildPlugin 的额外打包参数，默认
  `-TargetPlatforms=Win64`

PowerShell 示例：

```powershell
.\Scripts\Package-UEEditorAutomationPlugin.ps1 -ProjectRoot "D:\YourProject"
```

源码根目录示例：

```powershell
.\Scripts\Package-UEEditorAutomationPlugin.ps1 -ProjectRoot "D:\YourSourceCheckout"
```

如需覆盖默认打包参数：

```powershell
.\Scripts\Package-UEEditorAutomationPlugin.ps1 `
  -ProjectRoot "D:\YourProject" `
  -PackageArgs "-TargetPlatforms=Win64","-Rocket"
```

DebugGame 示例：

```powershell
.\Scripts\Package-UEEditorAutomationPlugin.ps1 `
  -ProjectRoot "D:\YourProject" `
  -Configuration DebugGame
```

默认输出目录：

```text
<ProjectRoot>\PluginPackages\UEEditorAutomation_UE4.25_Win64_<Configuration>
```

## 集成

把 `Plugins/UEEditorAutomation` 拷到 UE 项目并重新生成项目文件。
模块是 editor-only，加载阶段为 `PostEngineInit`。引擎版本敏感的代码
集中在：

```
Source/UEEditorAutomation/Private/Adapter/UEBlueprintEditorAdapter.cpp
Source/UEEditorAutomation/Private/UEEditorAutomationModule.cpp
```
