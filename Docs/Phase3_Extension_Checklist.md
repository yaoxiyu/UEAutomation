# Phase 3 Extension Checklist

This checklist records the validation coverage for the first Phase 3 platform-extension slice after C++ changes are compiled and the editor is restarted.

## Scope

Implemented in this slice:

- `create_data_asset`
- `modify_asset_properties`
- `check_asset_rules`
- `generate_audit_report`
- explicit debug panel menu entry
- controlled `set` and `map` property import paths
- CI helper scripts
- optional local socket task server
- `create_material_instance`
- `modify_material_instance`
- `create_blueprint_class`
- `create_widget_blueprint`
- `create_data_table`
- `create_curve_float`
- `create_curve_vector`
- `create_animation_blueprint`
- `create_blend_space`
- `create_level_sequence`
- `create_physics_asset`
- `create_material_function`
- `create_gameplay_ability`
- `create_gameplay_effect`
- `import_texture`
- `import_sound_wave`

Still deferred:

- CI-owned editor launch/build orchestration
- DataTable row import
- deep unrestricted property reflection

## Preconditions

- UE Editor is running with `UEEditorAutomation` enabled.
- Compilation is performed manually by the user.
- `C:/UEAutomation/tasks/inbox` and `C:/UEAutomation/tasks/working` are empty.
- `Config/UEEditorAutomationWhitelist.json` is valid JSON.

## Validated Status

The listed Phase 3 task types have been validated after manual compilation and editor restart. Project-specific validation used generated assets under `/Game/*/AutoGen` and read-only references to existing project assets.

`create_physics_asset` is validated through the non-interactive `FPhysicsAssetUtils::CreateFromSkeletalMesh` path. Do not route it through `UPhysicsAssetFactory::CreatePhysicsAssetFromMesh`, because that factory opens a modal body creation dialog.

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

### Create Material Instance

Source sample:

```text
Samples/valid/create_material_instance_basic.json
```

Expected:

- `success: true`
- `asset_outputs[0].asset_type` is `material_instance`
- output asset path is `/Game/Materials/AutoGen/MI_AutomationBasic_001.MI_AutomationBasic_001`

### Modify Material Instance

Source sample:

```text
Samples/valid/modify_material_instance_scalar.json
```

Expected:

- `success: true`
- `property_assign_count: 1`

### Concrete Asset Factories

Project-independent samples:

```text
Samples/valid/create_blueprint_class_actor.json
Samples/valid/create_widget_blueprint_basic.json
Samples/valid/create_data_table_basic.json
Samples/valid/create_curve_float_basic.json
Samples/valid/create_curve_vector_basic.json
Samples/valid/create_level_sequence_basic.json
Samples/valid/create_material_function_basic.json
Samples/valid/create_gameplay_ability_basic.json
Samples/valid/create_gameplay_effect_basic.json
```

Expected:

- `success: true`
- output `asset_type` matches the requested concrete asset family
- output asset is saved under the corresponding allowed `/Game/*/AutoGen` root

Project-specific samples requiring real project assets:

```text
Samples/valid/create_animation_blueprint_template.json
Samples/valid/create_blend_space_template.json
Samples/valid/create_physics_asset_template.json
Samples/valid/import_texture_template.json
Samples/valid/import_sound_wave_template.json
```

Before running these, replace placeholder skeleton, skeletal mesh, texture, or wav paths with real project paths.

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

### Material Instance Missing Parent

Source sample:

```text
Samples/invalid/create_material_instance_missing_parent.json
```

Expected:

- `success: false`
- error code: `InvalidParentAsset`

### Blueprint Class Disallowed Parent

Source sample:

```text
Samples/invalid/create_blueprint_class_disallowed_parent.json
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

## CI Scripts

Expected:

- `Scripts/Submit-AutomationTask.ps1` submits a task JSON to inbox.
- `Scripts/Wait-AutomationResult.ps1` waits for a result file and can return non-zero on task failure.
- `Scripts/Run-AutomationTask.ps1` submits and waits in one command.

## Socket Server

Enable `bEnableUEAutomationSocketServer` in project settings and restart the editor.

Expected:

- socket server listens on `127.0.0.1:18777` by default.
- `Scripts/Send-SocketAutomationTask.ps1` can send one newline-terminated task JSON.
- the socket response is the same result JSON contract used by file tasks.

## Property Expansion

`set` and `map` imports are supported only when the target UE property is actually `FSetProperty` or `FMapProperty`.

No generic sample is included because stock Engine assets do not expose a stable whitelisted set/map property suitable for project-independent validation.
