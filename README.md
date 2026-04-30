# UEEditorAutomation

UE Editor C++ 插件，用于声明式资产自动化：蓝图、DataAsset、材质、
曲线、GAS、审计、只读蓝图/资产分析、资产复制、引用重写。

## 文档

- `Docs/Current_Truth.md` —— 实现的能力以及它怎么工作
- `Docs/Task_Interface.md` —— JSON 任务协议与每个 task 的字段
- `Docs/Automation_Workflow_Design.md` —— C++ 原子能力、AI 编排层与工作流模板设计
- `Docs/AI_Workflow_Quickstart.md` —— 新 AI 会话只读这一份即可按工作流方式开始
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

轻量回归脚本：

```
python .\Scripts\automation_test.py --samples-dir .\Samples
```

要求编辑器已由用户或 CI 手动启动；脚本不会启动编辑器或触发 C++ 编译。
蓝图写入任务可以在运行中的编辑器里执行 Blueprint compile + save，
这用于暴露蓝图编译错误，不属于 C++ 构建流程。

## 集成

把 `Plugins/UEEditorAutomation` 拷到 UE 项目并重新生成项目文件。
模块是 editor-only，加载阶段为 `PostEngineInit`。引擎版本敏感的代码
集中在：

```
Source/UEEditorAutomation/Private/Adapter/UEBlueprintEditorAdapter.cpp
Source/UEEditorAutomation/Private/UEEditorAutomationModule.cpp
```
