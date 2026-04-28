# Phase 1 Regression Checklist

This checklist verifies the Phase 1 editor automation contract after C++ changes are compiled and the editor is restarted.

## Preconditions

- UE Editor is running with `UEEditorAutomation` enabled.
- Do not run build commands from AI automation. Compilation is performed manually by the user.
- Task directories exist under `C:/UEAutomation/`.
- `Config/UEEditorAutomationWhitelist.json` is valid JSON.
- Test asset exists unless the case explicitly creates it:
  `/Game/Blueprints/AutoGen/BP_AutoActor.BP_AutoActor`

## Queue Health

Before each run:

- `C:/UEAutomation/tasks/inbox` is empty.
- `C:/UEAutomation/tasks/working` is empty.
- Previous result files may remain in `C:/UEAutomation/results`.

## Core Success Cases

### Create Blueprint

Source sample:

```text
Samples/create_blueprint_actor.json
```

Expected:

- `success: true`
- `status: succeeded`
- `asset_outputs[0].asset_path` is populated
- `component_create_count` includes root plus declared components
- task moves to `tasks/done`

### Modify Component Properties

Source sample:

```text
Samples/modify_blueprint_components.json
```

Expected:

- `success: true`
- `property_assign_count: 1`
- `asset_outputs[0].asset_path` is `/Game/Blueprints/AutoGen/BP_AutoActor.BP_AutoActor`

### Modify Actor Defaults

Source sample:

```text
Samples/modify_blueprint_defaults.json
```

Expected:

- `success: true`
- `property_assign_count: 2`
- writes `InitialLifeSpan` and `bCanBeDamaged`

### Open After Success

Create a unique blueprint with:

```json
"open_after_success": true
```

Expected:

- `success: true`
- task log contains `step=blueprint: open asset`
- asset editor opens the generated Blueprint in UE

## Idempotency

### Skip Existing Asset

Source sample:

```text
Samples/valid/create_blueprint_skip_if_exists.json
```

Expected:

- `success: true`
- warning says asset already exists and was skipped
- `asset_outputs[0].asset_path` is populated
- no compile/save work is performed

### Overwrite Existing Asset Rejected

Source sample:

```text
Samples/invalid/create_blueprint_overwrite_existing.json
```

Expected:

- `success: false`
- error code: `OverwriteNotSupported`
- field: `execution.overwrite_if_exists`

## Startup Recovery

Place a valid task JSON directly into:

```text
C:/UEAutomation/tasks/working/
```

Then restart the editor.

Expected:

- task moves to `tasks/failed`
- result is written
- error code: `RecoveredStaleWorkingTask`
- task log contains `startup_recovery: stale working task detected`

## Whitelist Runtime Behavior

Temporarily rename:

```text
Config/UEEditorAutomationWhitelist.json
```

Submit any valid task.

Expected:

- `success: false`
- error code: `WhitelistLoadFailed`
- field: `security.whitelist`

Restore the whitelist immediately after this test.

## Component Property Specializations

### Collision Profile

Source sample:

```text
Samples/valid/modify_blueprint_components_collision_profile.json
```

Expected:

- `success: true`
- `property_assign_count: 1`
- no `PropertyNotFound` for `CollisionProfileName`

### Static Mesh

Source sample:

```text
Samples/valid/modify_blueprint_components_static_mesh.json
```

Before running, use a unique component name if the sample component already exists.

Expected:

- `success: true`
- `component_create_count: 1`
- `property_assign_count: 1`
- accepts bare object path `/Engine/BasicShapes/Cube.Cube`

### Invalid Static Mesh Rolls Back

Source sample:

```text
Samples/invalid/modify_blueprint_components_invalid_static_mesh.json
```

Use the same unique component name twice.

Expected on both runs:

- `success: false`
- error code: `InvalidPropertyValue`
- `component_create_count: 0`
- second run must not produce `DuplicateComponentName`
- task log contains `rollback SCS node`

## Validation Failures

### Create Duplicate Component

Source sample:

```text
Samples/invalid/create_blueprint_duplicate_component.json
```

Expected:

- `success: false`
- error code: `DuplicateComponentName`
- `duration_ms: 0` or near-zero

### Create Missing Attach Parent

Source sample:

```text
Samples/invalid/create_blueprint_missing_attach_parent.json
```

Expected:

- `success: false`
- error code: `AttachParentNotFound`
- field points to `.attach_parent`

### Modify Empty Operations

Source sample:

```text
Samples/invalid/modify_blueprint_components_empty_operations.json
```

Expected:

- `success: false`
- error code: `MissingRequiredField`
- field: `payload.operations`

### Disallowed Component

Source sample:

```text
Samples/invalid/modify_blueprint_components_disallowed_component.json
```

Expected:

- `success: false`
- error code: `ComponentClassNotAllowed`

### Disallowed Property

Source sample:

```text
Samples/invalid/modify_blueprint_defaults_disallowed_property.json
```

Expected:

- `success: false`
- error code: `PropertyAssignmentNotAllowed`

## Notes

- Several samples use fixed component names. If a previous successful run created that component, edit the component name before rerunning the success case.
- Failure cases should not save the asset unless explicitly expected.
- For AI-driven retries, prefer reading `result.json` first, then task log.
