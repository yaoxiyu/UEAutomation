---
name: ue-editor-automation
description: Use when Codex needs to operate the local UEEditorAutomation plugin for Unreal Engine blueprint or asset work: analyze blueprints, create or duplicate blueprints, modify blueprint defaults/components/references, inspect asset dependency chains, copy similar blueprint features, redirect asset references, verify UE assets, or diagnose blueprint/asset bugs. Trigger for Chinese or English requests such as "分析蓝图", "创建蓝图", "修改蓝图", "排查bug", "参考某蓝图实现", "复制资产", "重写引用", "检查蓝图配置", "用 UEEditorAutomation". Do not use for ordinary C++ source edits, general discussion, or documentation-only changes unless the user explicitly asks to use UEEditorAutomation.
---

# UEEditorAutomation

## Scope

Use the running UE editor plugin through JSON tasks to read, create, modify, copy, and verify UE assets and blueprints. This skill replaces the need to first read `Docs/AI_Workflow_Quickstart.md`; load repository docs only when an interface detail is missing or behavior is unclear.

## Non-Negotiable Rules

- Never run C++ build, UBT, UAT, GenerateProjectFiles, Live Coding, or start/restart the UE editor.
- Do not run `UE4Editor.exe`, `UE5Editor.exe`, `UnrealEditor.exe`, or equivalent editor launch commands.
- Assume the editor/plugin is already running if task submission is needed. If it is not running, ask the user to start it manually.
- Use `compile_after_create: false` and `open_after_success: false` unless the user explicitly changes the project policy. Do not use automation as a hidden compile step.
- Treat meta cache as planning aid only. For write and verify, use live UE automation tasks and fresh analysis.
- Prefer C++ atomic tasks exposed by UEEditorAutomation over ad hoc scripts. Temporary scripts are only transport glue.
- Do not reuse historical `Saved/*.js` orchestration as workflow logic.
- Do not delete or move user assets unless the user explicitly asked for cleanup and the task plan names the exact target.

## Find The Plugin

The plugin root is the repository directory that contains `UEEditorAutomation.uplugin`.
If not already in that directory, locate it by searching for `UEEditorAutomation.uplugin`
from the current workspace. Use `rg`/PowerShell reads first. Do not rely on hardcoded
user, machine, or project paths. Do not start the editor.

## Standard Workflow

1. **Observe**
   - Read the user's asset paths, allowed directories, and constraints.
   - Inspect local files/directories with `rg`, `Get-ChildItem`, and existing docs as needed.
   - Use read-only tasks such as `list_directory_assets`, `analyze_blueprint`, `analyze_asset`, `refresh_blueprint_meta_cache`, `analyze_blueprint_reference_chain`, or `export_blueprint_ai_context`.
   - Record task ids, result paths, warnings, and artifacts.

2. **Plan**
   - State the minimal asset plan before edits when the task is non-trivial.
   - Identify assets to create/duplicate, CDO fields to write, components to update, references to redirect, and assumptions/risks.
   - Respect any user-specified allowed asset root.

3. **Apply**
   - Submit only planned JSON tasks.
   - Common mutation tasks: `duplicate_asset`, `create_blueprint`, `modify_blueprint_defaults`, `modify_blueprint_components`, `modify_asset_properties`, `redirect_asset_references`, `copy_live_blueprint_values`, `copy_blueprint_live_overrides`.
   - Check every result for `success`, `errors`, `warnings`, `field_results`, and `asset_outputs`.
   - Stop and report if an atomic capability is missing instead of bypassing UE semantics with large scripts.

4. **Verify**
   - Re-observe live assets after writes. Do not trust apply results alone.
   - Verify expected defaults/components/references through fresh `refresh_blueprint_meta_cache`, `analyze_blueprint`, `analyze_asset`, reference scans, or semantic diff.
   - Classify remaining issues as Blocking, Semantic-risk, or Noise.

5. **Archive**
   - Only when UEEditorAutomation tasks were used, write a task flow report under `<PluginDir>/Saved/TaskReports/`.
   - Report filename: `yyyyMMddHHmmss_中文概述.md`.
   - Explain goal, observations, plan, task ids/results, edits, verification, risks, and manual next steps in prose. Do not paste large JSON.
   - Zip this task's generated JSON/JS with the same basename, then delete scattered temporary JSON/JS. Do not delete official `C:/UEAutomation/tasks` or `C:/UEAutomation/results` archives.
   - Mention report and zip paths in the final answer.

## Task Submission

Use the plugin scripts from the plugin root:

```powershell
.\Scripts\Run-AutomationTask.ps1 -TaskPath <task.json> -FailOnTaskFailure
```

Task JSON defaults:

```json
{
  "protocol_version": 1,
  "execution": {
    "save_after_success": true,
    "compile_after_create": false,
    "open_after_success": false
  }
}
```

Use unique `task_id` values. Prefer temporary task files for transport, and archive them at the end when this skill performed UE automation.

## Reference

Read `references/task-protocol.md` for common task payload shapes and archive details. If deeper detail is needed, then read the repository docs:

- `Docs/Current_Truth.md`
- `Docs/Task_Interface.md`
- `Docs/AI_Workflow_Quickstart.md`
