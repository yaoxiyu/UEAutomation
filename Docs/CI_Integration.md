# CI Integration

This plugin does not build or launch UE by itself. CI should own those steps.

The supported CI contract is:

1. CI starts the editor with `UEEditorAutomation` enabled.
2. CI submits JSON tasks into `C:/UEAutomation/tasks/inbox`.
3. The plugin writes `C:/UEAutomation/results/<task_id>.result.json`.
4. CI reads the result JSON and fails the job when `success` is false.

## Scripts

Submit a task:

```powershell
.\Scripts\Submit-AutomationTask.ps1 -TaskPath .\Samples\valid\generate_audit_report_basic.json
```

Wait for a result:

```powershell
.\Scripts\Wait-AutomationResult.ps1 -TaskId task_generate_audit_report_basic_001 -FailOnTaskFailure
```

Submit and wait:

```powershell
.\Scripts\Run-AutomationTask.ps1 -TaskPath .\Samples\valid\generate_audit_report_basic.json -FailOnTaskFailure
```

Submit through the optional local socket server:

```powershell
.\Scripts\Send-SocketAutomationTask.ps1 -TaskPath .\Samples\valid\generate_audit_report_basic.json -FailOnTaskFailure
```

The socket server is disabled by default. Enable `bEnableUEAutomationSocketServer` in `UEditorAutomationSettings`; the default port is `18777`.

## Exit Codes

`Wait-AutomationResult.ps1` uses:

- `0`: result was found and either success was true or `-FailOnTaskFailure` was not used.
- `1`: timed out waiting for the result file.
- `2`: result was found, `-FailOnTaskFailure` was used, and the task failed.

## Rules

- Do not let AI agents run CI build commands for this project.
- Keep UE compilation and editor launch outside the plugin scripts.
- Prefer unique `task_id` values per CI run.
- Clean `tasks/inbox` and `tasks/working` before a CI validation pass.
- Use the socket path only for local trusted automation on `127.0.0.1`.
