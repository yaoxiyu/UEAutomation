# UEEditorAutomation

UE Editor C++ plugin for declarative Blueprint asset automation.

## Phase 1 Scope

- File polling daemon
- `create_blueprint`
- `modify_blueprint_components`
- `modify_blueprint_defaults`
- Blueprint component tree assembly
- Component template property assignment
- Blueprint class default assignment
- Compile / save / open
- Structured `*.result.json`

Graph node generation, pin wiring, UI automation, socket/http transport, and broad reflection writes are intentionally out of scope.

## Default Task Directories

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

Drop a task JSON file into `tasks/inbox`. The plugin moves it to `working`, executes it on the editor ticker, writes `results/<task_id>.result.json`, then moves the task to `done` or `failed`.

## Samples

Sample tasks are split by expected outcome:

```text
Samples/valid/
Samples/invalid/
```

Use the invalid samples to verify structured error output after changing whitelist policy or task parsing.

## Configuration

Project settings are exposed by `UEditorAutomationSettings`:

- `bEnableDaemon`
- task/result/log directories
- `PollIntervalSeconds`
- `SupportedProtocolVersion`
- `WhitelistFilePath`

Security policy is loaded at runtime from:

```text
Plugins/UEEditorAutomation/Config/UEEditorAutomationWhitelist.json
```

The whitelist file controls:

- allowed task types
- allowed asset roots
- allowed parent classes
- allowed component classes
- allowed property names

Edit the JSON whitelist instead of recompiling C++ when broadening automation scope.

## Integration

Copy `Plugins/UEEditorAutomation` into a UE project and regenerate project files. The module is editor-only and loads at `PostEngineInit`.

For UE4.25, validate compiler details around `UPackage::SavePackage`, `USimpleConstructionScript`, and ticker APIs in your exact engine fork. UE version-specific API usage is concentrated in:

```text
Source/UEEditorAutomation/Private/Adapter/UEBlueprintEditorAdapter.cpp
Source/UEEditorAutomation/Private/UEEditorAutomationModule.cpp
```

## Result Contract

Results are machine-readable:

```json
{
  "protocol_version": 1,
  "task_id": "task_create_bp_001",
  "task_type": "create_blueprint",
  "success": true,
  "status": "succeeded",
  "asset_outputs": [],
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
  "log_path": ""
}
```
