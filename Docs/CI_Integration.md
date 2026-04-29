# CI 集成

本插件不会自己编译或启动 UE，那些步骤由 CI 负责。

CI 协议：

1. CI 启动编辑器，确认 `UEEditorAutomation` 已启用。
2. CI 把 JSON 任务写入 `C:/UEAutomation/tasks/inbox`。
3. 插件写出 `C:/UEAutomation/results/<task_id>.result.json`。
4. CI 读 result JSON，`success` 为 false 时让 job 失败。

## 脚本

提交一个任务：

```powershell
.\Scripts\Submit-AutomationTask.ps1 -TaskPath .\Samples\valid\generate_audit_report_basic.json
```

等待 result：

```powershell
.\Scripts\Wait-AutomationResult.ps1 -TaskId task_generate_audit_report_basic_001 -FailOnTaskFailure
```

提交并等待：

```powershell
.\Scripts\Run-AutomationTask.ps1 -TaskPath .\Samples\valid\generate_audit_report_basic.json -FailOnTaskFailure
```

通过本地 socket 提交：

```powershell
.\Scripts\Send-SocketAutomationTask.ps1 -TaskPath .\Samples\valid\generate_audit_report_basic.json -FailOnTaskFailure
```

socket 默认禁用。在 `UEditorAutomationSettings` 打开
`bEnableUEAutomationSocketServer`；默认端口 `18777`。

## 退出码

`Wait-AutomationResult.ps1` 用：

- `0`：找到 result，且 `success=true` 或没传 `-FailOnTaskFailure`。
- `1`：等待 result 文件超时。
- `2`：找到 result，传了 `-FailOnTaskFailure` 且任务失败。

## 规则

- 不要让 AI 代理跑本项目的 CI 编译命令。
- UE 编译与编辑器启动放在插件脚本之外。
- 每次 CI run 用唯一的 `task_id`。
- 在 CI 校验阶段开始前清空 `tasks/inbox` 与 `tasks/working`。
- socket 只用于本机 `127.0.0.1` 上的可信自动化。
