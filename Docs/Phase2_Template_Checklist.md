# Phase 2 Template Checklist

This checklist verifies the first Phase 2 template-production slice after C++ changes are compiled and the editor is restarted.

## Preconditions

- UE Editor is running with `UEEditorAutomation` enabled.
- Compilation is performed manually by the user.
- `C:/UEAutomation/tasks/inbox` and `C:/UEAutomation/tasks/working` are empty.
- Runtime config files exist:
  - `Config/UEEditorAutomationWhitelist.json`
  - `Config/UEEditorAutomationTemplates.json`

## Success Case

### Create Blueprint From Template

Source sample:

```text
Samples/valid/create_blueprint_from_template_actor_basic.json
```

Expected:

- `success: true`
- `status: succeeded`
- `asset_outputs[0].asset_path` is `/Game/Blueprints/AutoGen/BP_TemplateActor_001.BP_TemplateActor_001`
- `component_create_count: 2`
- `property_assign_count` includes template properties plus overrides
- task log contains `create_blueprint_from_template: expanded template actor_basic_v1`

### Batch Create Blueprints

Source sample:

```text
Samples/valid/batch_create_blueprints_actor_basic.json
```

Expected:

- `success: true`
- `status: succeeded`
- `asset_outputs` contains two Blueprint assets
- `component_create_count: 4`
- task log contains `batch_create_blueprints: create item 1/2`

## Failure Cases

### Missing Template

Source sample:

```text
Samples/invalid/create_blueprint_from_template_missing_template.json
```

Expected:

- `success: false`
- error code: `TemplateNotFound`
- field: `payload.template.template_id`

### Missing Override Component

Source sample:

```text
Samples/invalid/create_blueprint_from_template_missing_component_override.json
```

Expected:

- `success: false`
- error code: `TemplateOverrideComponentNotFound`
- no Blueprint asset is created

### Duplicate Batch Asset

Source sample:

```text
Samples/invalid/batch_create_blueprints_duplicate_asset.json
```

Expected:

- `success: false`
- error code: `DuplicateBatchAsset`
- failure happens during validation

## Runtime Config Behavior

Temporarily rename:

```text
Config/UEEditorAutomationTemplates.json
```

Submit the valid template sample.

Expected:

- `success: false`
- error code: `TemplateRegistryLoadFailed`
- field: `payload.template.template_id`

Restore the template registry immediately after this test.

## Property Expansion

These cases require the template success case to create:

```text
/Game/Blueprints/AutoGen/BP_TemplateActor_001.BP_TemplateActor_001
```

### Array Property

Source sample:

```text
Samples/valid/modify_blueprint_defaults_actor_tags_array.json
```

Expected:

- `success: true`
- `property_assign_count: 1`
- writes Actor `Tags` through the `array` type path

### Struct Property

Source sample:

```text
Samples/valid/modify_blueprint_components_struct_box_extent.json
```

Expected:

- `success: true`
- `property_assign_count: 1`
- writes `HitBox.BoxExtent` through the `struct` type path

### Array On Scalar Rejected

Source sample:

```text
Samples/invalid/modify_blueprint_defaults_array_on_scalar.json
```

Expected:

- `success: false`
- error code: `InvalidPropertyValue`
- field points to `payload.class_defaults[0].value`

## Debug Panel MVP

Open the nomad tab named:

```text
UE Automation
```

Expected:

- recent `*.result.json` files are listed
- selecting a row enables `Open Result`
- selecting a row with an existing log enables `Open Log`
- `Refresh` reloads the result directory
