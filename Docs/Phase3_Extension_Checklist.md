# Phase 3 Extension Checklist

This checklist verifies the first Phase 3 platform-extension slice after C++ changes are compiled and the editor is restarted.

## Scope

Implemented in this slice:

- `create_data_asset`
- `modify_asset_properties`
- `check_asset_rules`
- `generate_audit_report`
- explicit debug panel menu entry
- controlled `set` and `map` property import paths

Still deferred:

- local RPC / socket
- CI runner scripts
- more concrete asset types beyond DataAsset
- deep unrestricted property reflection

## Preconditions

- UE Editor is running with `UEEditorAutomation` enabled.
- Compilation is performed manually by the user.
- `C:/UEAutomation/tasks/inbox` and `C:/UEAutomation/tasks/working` are empty.
- `Config/UEEditorAutomationWhitelist.json` is valid JSON.

## Success Cases

### Create DataAsset

Source sample:

```text
Samples/valid/create_data_asset_basic.json
```

Expected:

- `success: true`
- `asset_outputs[0].asset_type` is `data_asset`
- output asset path is `/Game/Data/AutoGen/DA_AutomationBasic_001.DA_AutomationBasic_001`

### Check Asset Rules

Source sample:

```text
Samples/valid/check_asset_rules_basic.json
```

Expected:

- `success: true`
- output contains the checked asset

### Generate Audit Report

Source sample:

```text
Samples/valid/generate_audit_report_basic.json
```

Expected:

- `success: true`
- output type is `audit_report`
- report JSON is written under `Saved/UEAutomationReports/`
- report includes `result_count`, `success_count`, `failure_count`, `task_type_counts`, and `error_counts`

## Failure Cases

### Missing Asset Rule Violation

Source sample:

```text
Samples/invalid/check_asset_rules_missing_asset.json
```

Expected:

- `success: false`
- error code: `AssetRuleViolation`

### DataAsset Invalid Class

Source sample:

```text
Samples/invalid/create_data_asset_disallowed_class.json
```

Expected:

- `success: false`
- error code: `InvalidParentClass`

### Modify Asset Missing Property

Source sample:

```text
Samples/invalid/modify_asset_properties_missing_property.json
```

Expected:

- `success: false`
- error code: `PropertyNotFound`

## Debug Panel

Expected:

- `Window` menu contains `UE Automation`.
- Selecting it opens the debug panel.
- Recent result rows are visible.
- selected rows can open result and log files.

## Property Expansion

`set` and `map` imports are supported only when the target UE property is actually `FSetProperty` or `FMapProperty`.

No generic sample is included because stock Engine assets do not expose a stable whitelisted set/map property suitable for project-independent validation.
