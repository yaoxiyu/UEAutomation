# UEEditorAutomation Task Protocol Notes

## Common Read Tasks

- `list_directory_assets`: enumerate assets in `/Game/...` directories.
- `analyze_blueprint`: snapshot parent class, defaults, components, references, and graph summaries for a blueprint.
- `analyze_asset`: inspect non-blueprint top-level UObject properties; blueprint targets route to blueprint analysis.
- `refresh_blueprint_meta_cache`: force fresh analysis for one or more blueprints.
- `analyze_blueprint_reference_chain`: bounded dependency/reference BFS.
- `export_blueprint_ai_context`: compact context for reasoning.

## Common Write Tasks

- `duplicate_asset`: duplicate one asset into another package path.
- `create_blueprint`: create a blueprint from parent class, optional SCS components, and defaults.
- `modify_blueprint_defaults`: write CDO properties; use `import_text` for complex UE property text.
- `modify_blueprint_components`: add/update component templates. Prefer `target_kind` values:
  - `native_template`
  - `own_scs_template`
  - `scs_inherited_override`
- `copy_live_blueprint_values`: copy named CDO/component properties from source live object to target live object.
- `copy_blueprint_live_overrides`: let C++ discover source overrides and copy them to target.
- `modify_asset_properties`: write non-blueprint asset top-level UPROPERTY values.
- `redirect_asset_references`: replace object references inside an asset/blueprint.

## Minimal Task Skeleton

```json
{
  "protocol_version": 1,
  "task_id": "unique_task_id",
  "task_type": "analyze_blueprint",
  "timestamp_utc": "2026-04-30T00:00:00Z",
  "execution": {
    "idempotency_key": "unique_task_id",
    "skip_if_exists": false,
    "overwrite_if_exists": false,
    "save_after_success": true,
    "compile_after_create": false,
    "open_after_success": false
  },
  "payload": {}
}
```

## Analysis Options

Keep options within project limits. Known safe values:

```json
{
  "force_refresh": true,
  "use_cache": false,
  "include_blueprint_snapshot": true,
  "include_class_defaults": true,
  "include_components": true,
  "include_references": true,
  "include_referencers": true,
  "include_graph_summary": true,
  "include_graph_pins": false,
  "reference_depth": 1,
  "max_nodes": 128,
  "max_edges": 512,
  "max_property_depth": 8,
  "max_array_elements": 128,
  "export_only_editable_properties": true
}
```

## Archive Report Contents

Include:

- User goal and constraints.
- Observed assets and important fields/references.
- Plan and assumptions.
- Apply task ids with success/warning/error summary.
- Verification task ids and conclusions.
- Files/assets changed.
- Blocking issues, semantic risks, and manual validation needed.

Do not include huge raw JSON. Link or name result files instead.
