# Current Implementation Status

This document records the real local implementation state of `UEEditorAutomation`.
Use it as the first source of truth before comparing the codebase with the broader system design document.

## Operating Rules For AI Agents

- Do not run UE build, compile, UBT, UAT, project generation, or editor launch commands.
- The user performs all compilation and editor restarts manually.
- AI may edit source/config/sample/docs files, submit JSON tasks to `C:/UEAutomation/tasks/inbox`, and inspect `results` / `logs`.
- Prefer batching related C++ changes, then ask the user to compile once.
- If validation fails, read `*.result.json` first, then the task log.

## Runtime Layout

Default task exchange root:

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

The editor plugin polls `tasks/inbox`, moves a task to `working`, writes `results/<task_id>.result.json`, then moves the task to `done` or `failed`.

Runtime policy/config files:

```text
Config/UEEditorAutomationWhitelist.json
Config/UEEditorAutomationTemplates.json
```

## Phase 1 Status

Phase 1 is implemented and regression-tested.

Implemented task types:

- `create_blueprint`
- `modify_blueprint_components`
- `modify_blueprint_defaults`

Implemented transport and execution behavior:

- File polling daemon.
- `inbox -> working -> done/failed` lifecycle.
- Startup recovery for stale `working` tasks with `RecoveredStaleWorkingTask`.
- Structured `*.result.json`.
- Per-task log files.
- Result metrics:
  - `duration_ms`
  - `compile_duration_ms`
  - `save_duration_ms`
  - `component_create_count`
  - `property_assign_count`
  - `warning_count`
  - `error_count`

Implemented Blueprint behavior:

- Create Actor Blueprint assets.
- Add root and child SCS components.
- Attach child components by declared parent name.
- Apply component transforms.
- Assign component template properties.
- Assign Blueprint class defaults.
- Compile, save, and optionally open assets.
- Output `asset_outputs` for create and modify tasks.

Implemented validation and safety:

- Runtime whitelist for task types, asset roots, parent classes, component classes, and property names.
- Asset root validation.
- Parent class and component class whitelist validation.
- Property whitelist validation.
- Create-time duplicate component name detection.
- Create-time attach-parent-before-child validation.
- Modify operation validation:
  - non-empty operations
  - required `op`
  - required component names/classes
  - non-empty property lists for `update_component_properties`
- `overwrite_if_exists` is rejected for create tasks.
- `skip_if_exists` is supported for create tasks.

Implemented property support:

- Basic import-text based assignment for scalar and common UE value types.
- Special assignment paths:
  - `StaticMeshComponent.StaticMesh`
  - `SkeletalMeshComponent.SkeletalMesh`
  - `PrimitiveComponent.CollisionProfileName`
- Add-component rollback on property/transform failure, so failed adds do not leave duplicate SCS nodes.

Known Phase 1 non-goals still not implemented:

- Blueprint graph node creation.
- Pin wiring.
- EventGraph / ConstructionScript graph automation.
- UI automation.
- Socket / HTTP transport.
- Arbitrary unrestricted reflection writes.

Main Phase 1 verification doc:

```text
Docs/Phase1_Regression_Checklist.md
```

## Phase 2 Status

Phase 2 template-production features are implemented and validated.

Implemented task types:

- `create_blueprint_from_template`
- `batch_create_blueprints`

Implemented template system:

- Runtime template registry loaded from `Config/UEEditorAutomationTemplates.json`.
- Template entries define:
  - `template_id`
  - parent class
  - root component
  - child components
  - component transforms
  - component properties
  - class defaults
- Template tasks expand into the same domain path as `create_blueprint`.
- Request `asset.parent_class` can override the template parent class.

Implemented overrides:

- Component property overrides by component name.
- Component transform overrides by component name.
- Class default overrides by property name.
- Override of a missing component fails with `TemplateOverrideComponentNotFound`.

Implemented template registry validation:

- Missing template file / invalid JSON / missing `templates` array returns `TemplateRegistryLoadFailed`.
- Duplicate `template_id` returns `TemplateRegistryLoadFailed`.
- Invalid template structure returns `TemplateRegistryLoadFailed`.
- Missing requested template returns `TemplateNotFound`.
- Template component names must be unique.
- Template attach parents must be declared before children.

Implemented batch behavior:

- Shared template via `payload.shared_template`.
- Per-item template override via `items[].template`.
- Per-item asset definition.
- Per-item component and class-default overrides.
- Duplicate asset targets inside one batch fail with `DuplicateBatchAsset`.
- Batch checks target asset existence before creating any item, reducing partial success risk.
- `overwrite_if_exists` is rejected for batch creation.

Implemented Phase 2 property expansion:

- Controlled `array` property import.
- Controlled JSON-object `struct` property import.
- `type: array` is accepted only when the real UE property is `FArrayProperty`.
- `type: struct` is accepted only when the real UE property is `FStructProperty`.
- Array elements currently support scalar values and nested struct values through UE import text.
- Struct JSON fields currently support scalar values only.
- Controlled `set` and `map` paths are implemented when the real UE property is `FSetProperty` or `FMapProperty`.
- Arbitrary deep nesting and arbitrary custom struct policies are not implemented.

Implemented debug panel MVP:

- Nomad tab registered as `UE Automation`.
- Lists recent `C:/UEAutomation/results/*.result.json`.
- Can refresh the list.
- Can open selected result file.
- Can open selected log file.

Important debug panel caveat:

- The tab spawner is registered, but a dedicated top-level menu entry may still need explicit menu extension if the tab is hard to discover in a specific UE editor layout.

Main Phase 2 verification doc:

```text
Docs/Phase2_Template_Checklist.md
```

## Last Validated Phase 2 Cases

The following cases passed after manual compile and editor restart:

- `create_blueprint_from_template` success.
- `batch_create_blueprints` success.
- `array` writes Actor `Tags`.
- `struct` writes `HitBox.BoxExtent`.
- Missing template returns `TemplateNotFound`.
- Missing override component returns `TemplateOverrideComponentNotFound`.
- Duplicate batch asset returns `DuplicateBatchAsset`.
- `array` on scalar property returns `InvalidPropertyValue`.

## Key Source Areas

Protocol parsing:

```text
Source/UEEditorAutomation/Public/Protocol/AutomationProtocolTypes.h
Source/UEEditorAutomation/Private/Protocol/AutomationProtocolTypes.cpp
```

Task execution:

```text
Source/UEEditorAutomation/Public/Application/BlueprintTaskExecutors.h
Source/UEEditorAutomation/Private/Application/BlueprintTaskExecutors.cpp
```

Blueprint domain service:

```text
Source/UEEditorAutomation/Public/Domain/BlueprintAutomationService.h
Source/UEEditorAutomation/Private/Domain/BlueprintAutomationService.cpp
```

Template registry:

```text
Source/UEEditorAutomation/Public/Domain/BlueprintTemplateRegistry.h
Source/UEEditorAutomation/Private/Domain/BlueprintTemplateRegistry.cpp
```

Property assignment:

```text
Source/UEEditorAutomation/Public/Domain/PropertyAssignmentService.h
Source/UEEditorAutomation/Private/Domain/PropertyAssignmentService.cpp
```

UE adapter:

```text
Source/UEEditorAutomation/Public/Adapter/BlueprintEditorAdapter.h
Source/UEEditorAutomation/Public/Adapter/UEBlueprintEditorAdapter.h
Source/UEEditorAutomation/Private/Adapter/UEBlueprintEditorAdapter.cpp
```

Debug panel:

```text
Source/UEEditorAutomation/Public/UI/AutomationDebugPanel.h
Source/UEEditorAutomation/Private/UI/AutomationDebugPanel.cpp
```

Module registration:

```text
Source/UEEditorAutomation/Private/UEEditorAutomationModule.cpp
Source/UEEditorAutomation/UEEditorAutomation.Build.cs
```

## Recommended Next Work

Near-term:

- Add DataTable row import if production needs table content automation, not just asset creation.
- Add Blueprint graph node automation if production needs EventGraph / ConstructionScript generation.
- Add tests/samples for invalid template registry structure.
- Improve batch result semantics if partial-success reporting is ever required.

## Phase 3 Status

The first Phase 3 extension slice is implemented and validated after manual user compilation and editor restart.

Implemented and validated task types in this slice:

- `create_data_asset`
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
- `modify_asset_properties`
- `check_asset_rules`
- `generate_audit_report`

Implemented platform extensions:

- DataAsset creation as the first non-Blueprint asset type.
- MaterialInstanceConstant creation and parameter modification.
- Concrete asset creation through task-specific dynamic factories for Blueprint, Widget Blueprint, DataTable, Curve, Animation Blueprint, BlendSpace, LevelSequence, PhysicsAsset, MaterialFunction, and GAS Blueprint assets.
- Texture and SoundWave import tasks from source files.
- Generic UObject asset property modification through the existing whitelist and property assignment service.
- Asset rule checking for existence and allowed-root checks.
- JSON audit report generation from existing result files.
- Explicit `Window -> UE Automation` debug panel menu entry.
- Controlled `set` and `map` property import paths.
- Optional local socket server for trusted localhost task execution.
- CI helper scripts for file and socket task submission.

Last validated Phase 3 cases:

- `create_data_asset`
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
- `check_asset_rules`
- `generate_audit_report`
- trusted-localhost socket execution

Important Phase 3 implementation notes:

- `create_physics_asset` uses `FPhysicsAssetUtils::CreateFromSkeletalMesh` instead of `UPhysicsAssetFactory::CreatePhysicsAssetFromMesh`, because the factory path opens a modal body creation dialog and blocks automation.
- `create_physics_asset` passes `bSetToMesh=false`, so validation creates the new PhysicsAsset without writing it back to the source SkeletalMesh.
- `import_sound_wave` relies on the engine-registered AudioEditor sound factory. In this engine fork, `ImportAssetTasks` may populate `ImportedObjectPaths` without populating `Result`; the plugin falls back to loading the first imported object path.
- `import_texture` uses an explicit `TextureFactory` and also supports the `ImportedObjectPaths` fallback.
- The socket server is optional and controlled by `bEnableUEAutomationSocketServer`; it was validated on `127.0.0.1:18777`.

Main Phase 3 verification doc:

```text
Docs/Phase3_Extension_Checklist.md
```

Deferred unless real production cases require them:

- Deep nested JSON-to-UE property conversion.
- Custom project-specific struct policies.
- HTTP transport.
- DataTable row import.
- Blueprint graph node automation.
