# Task Interface Usage

This document describes the current JSON task contract for `UEEditorAutomation`.

## Operating Constraints

- The plugin is editor-only.
- The user owns all UE compilation, editor launch, and editor restart steps.
- AI agents must not run UE build, UBT, UAT, project generation, or editor launch commands.
- Runtime task files are safe to submit to `C:/UEAutomation/tasks/inbox` when the editor is already running.

## Transports

### File Queue

Default layout:

```text
C:/UEAutomation/
  tasks/
    inbox/
    working/
    done/
    failed/
  results/
  logs/
```

Submit a task:

```powershell
.\Scripts\Run-AutomationTask.ps1 -TaskPath .\Samples\valid\create_curve_float_basic.json -FailOnTaskFailure
```

Lifecycle:

```text
inbox/*.json -> working/*.json -> done/*.json or failed/*.json
```

Result path:

```text
C:/UEAutomation/results/<task_id>.result.json
```

### Local Socket

The socket server is optional and disabled by default. Enable `bEnableUEAutomationSocketServer` in project settings and restart the editor.

Default endpoint:

```text
127.0.0.1:18777
```

Send a task:

```powershell
.\Scripts\Send-SocketAutomationTask.ps1 -TaskPath .\Samples\valid\generate_audit_report_basic.json -FailOnTaskFailure
```

The socket protocol is one compressed JSON task followed by a newline. The response is the same result JSON contract as file queue execution.

## Common Request Shape

```json
{
  "protocol_version": 1,
  "task_id": "task_unique_id",
  "task_type": "create_curve_float",
  "timestamp_utc": "2026-04-28T00:00:00Z",
  "execution": {
    "idempotency_key": "task_unique_id",
    "skip_if_exists": false,
    "overwrite_if_exists": false,
    "save_after_success": true,
    "compile_after_create": true,
    "open_after_success": false
  },
  "payload": {}
}
```

Common fields:

- `protocol_version`: currently `1`.
- `task_id`: unique result/log identity.
- `task_type`: must be allowed by `Config/UEEditorAutomationWhitelist.json`.
- `execution.skip_if_exists`: create tasks may return success with a warning when the target already exists.
- `execution.overwrite_if_exists`: generally rejected or ignored unless a specific task supports it.
- `execution.save_after_success`: saves created or modified assets when true.
- `payload.asset.package_path`: must be under an allowed asset root.

## Result Shape

```json
{
  "protocol_version": 1,
  "task_id": "task_unique_id",
  "task_type": "create_curve_float",
  "success": true,
  "status": "succeeded",
  "asset_outputs": [
    {
      "asset_path": "/Game/Curves/AutoGen/CF_Automation.CF_Automation",
      "asset_name": "CF_Automation",
      "asset_type": "curve_float"
    }
  ],
  "warnings": [],
  "errors": [],
  "metrics": {
    "duration_ms": 0,
    "compile_duration_ms": 0,
    "save_duration_ms": 0,
    "component_create_count": 0,
    "property_assign_count": 0,
    "warning_count": 0,
    "error_count": 0
  },
  "log_path": "C:/UEAutomation/logs/task_unique_id.log"
}
```

On failure, inspect `errors[].code`, `errors[].field`, and then the log file.

## Blueprint Tasks

### `create_blueprint`

Creates a Blueprint and optionally adds SCS components and class defaults.

Required:

- `payload.asset.asset_name`
- `payload.asset.package_path`
- `payload.asset.parent_class`

Optional:

- `payload.assembly.root_component`
- `payload.assembly.components`
- `payload.class_default_overrides`

Sample:

```text
Samples/create_blueprint_actor.json
```

### `modify_blueprint_components`

Applies component operations to an existing Blueprint.

Required:

- `payload.target_asset.asset_path`
- `payload.operations`

Supported operations:

- `add_component`
- `update_component_properties`

Sample:

```text
Samples/modify_blueprint_components.json
```

### `modify_blueprint_defaults`

Assigns class default properties on a Blueprint CDO.

Required:

- `payload.target_asset.asset_path`
- `payload.class_defaults` or `payload.properties`

Sample:

```text
Samples/modify_blueprint_defaults.json
```

## Template Tasks

### `create_blueprint_from_template`

Creates a Blueprint from a template in `Config/UEEditorAutomationTemplates.json`.

Required:

- `payload.asset.asset_name`
- `payload.asset.package_path`
- `payload.template.template_id`

Optional:

- `payload.overrides.component_overrides`
- `payload.overrides.class_default_overrides`

Sample:

```text
Samples/valid/create_blueprint_from_template_actor_basic.json
```

### `batch_create_blueprints`

Creates multiple Blueprint assets from a shared template or per-item templates.

Required:

- `payload.items[]`
- each item requires `asset`
- template must be supplied by `payload.shared_template` or `items[].template`

Sample:

```text
Samples/valid/batch_create_blueprints_actor_basic.json
```

## Asset Creation Tasks

All create tasks require:

- `payload.asset.asset_name`
- `payload.asset.package_path`

Some tasks also require `parent_class` or `parameters`.

Task types:

- `create_data_asset`: requires `payload.asset.parent_class`.
- `create_material_instance`: requires `payload.asset.parent_class` as the parent material path; accepts `payload.parameters`.
- `create_blueprint_class`: requires or defaults through `payload.asset.parent_class`.
- `create_widget_blueprint`: defaults parent to `/Script/UMG.UserWidget` if omitted.
- `create_data_table`: requires row struct through `payload.asset.parent_class` or `parameters[].name = "row_struct"`.
- `create_curve_float`
- `create_curve_vector`
- `create_animation_blueprint`: requires `parameters[].name = "target_skeleton"`; optional `preview_skeletal_mesh`.
- `create_blend_space`: requires `target_skeleton`; optional `preview_skeletal_mesh`.
- `create_level_sequence`
- `create_physics_asset`: requires `parameters[].name = "target_skeletal_mesh"`.
- `create_material_function`
- `create_gameplay_ability`: defaults parent to `/Script/GameplayAbilities.GameplayAbility`.
- `create_gameplay_effect`: defaults parent to `/Script/GameplayAbilities.GameplayEffect`.

Validated samples live under:

```text
Samples/valid/
```

PhysicsAsset note:

`create_physics_asset` uses a non-interactive `FPhysicsAssetUtils::CreateFromSkeletalMesh` path and does not assign the generated PhysicsAsset back to the source SkeletalMesh.

## Import Tasks

### `import_texture`

Required:

- `payload.source_path`
- `payload.asset.asset_name`
- `payload.asset.package_path`

Sample:

```text
Samples/valid/import_texture_template.json
```

### `import_sound_wave`

Required:

- `payload.source_path`
- `payload.asset.asset_name`
- `payload.asset.package_path`

Sample:

```text
Samples/valid/import_sound_wave_template.json
```

Import implementation note:

This engine fork may populate `UAssetImportTask.ImportedObjectPaths` without filling `Result`; the plugin reads both.

## Modification And Validation Tasks

### `modify_material_instance`

Required:

- `payload.target_asset.asset_path`
- `payload.parameters`

Parameter types:

- `scalar` / `float`
- `linear_color` / `color` / `vector`, value `[R, G, B, A]`
- `texture`, value object path

Sample:

```text
Samples/valid/modify_material_instance_scalar.json
```

### `modify_asset_properties`

Assigns whitelisted properties on a loaded asset.

Required:

- `payload.target_asset.asset_path`
- `payload.properties`

### `check_asset_rules`

Checks asset existence and allowed-root policy.

Required:

- `payload.target_asset.asset_path` or `payload.target_assets[]`

Optional:

- `payload.rules`, defaults to `asset_exists` and `asset_root_allowed`.

### `generate_audit_report`

Generates a JSON report from existing result files.

Optional:

- `payload.report.path`
- `payload.report.format`

Sample:

```text
Samples/valid/generate_audit_report_basic.json
```

## Security Policy

Runtime whitelist:

```text
Config/UEEditorAutomationWhitelist.json
```

Controls:

- allowed task types
- allowed asset roots
- allowed parent classes
- allowed component classes
- allowed property names

Edit the whitelist JSON when broadening automation scope; do not hardcode broad policy in C++.
