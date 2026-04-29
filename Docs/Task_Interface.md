# 接口使用文档

`UEEditorAutomation` 的 JSON 任务协议。下面所有示例都是当前代码可解析的。

## 操作约束

- 插件 editor-only。
- 编辑器运行时把任务文件投递到 `C:/UEAutomation/tasks/inbox`。
- AI 代理不能调用 UBT、UAT、生成项目文件、启动编辑器。

## 传输方式

### 文件队列

```
C:/UEAutomation/
  tasks/{inbox,working,done,failed}/
  results/
  logs/
```

```powershell
.\Scripts\Run-AutomationTask.ps1 -TaskPath .\Samples\valid\<task>.json -FailOnTaskFailure
```

生命周期：`inbox/*.json -> working/*.json -> done/*.json 或 failed/*.json`。
结果路径：`C:/UEAutomation/results/<task_id>.result.json`。

### 本地 socket

默认禁用。在项目设置打开 `bEnableUEAutomationSocketServer`，重启编辑器。

```
127.0.0.1:18777
```

```powershell
.\Scripts\Send-SocketAutomationTask.ps1 -TaskPath .\Samples\valid\<task>.json -FailOnTaskFailure
```

协议：一条 JSON 任务后跟换行。响应即同样的 result JSON。

## 通用请求结构

```json
{
  "protocol_version": 1,
  "task_id": "task_unique_id",
  "task_type": "<allowed_task_types 中的一项>",
  "timestamp_utc": "2026-04-29T00:00:00Z",
  "execution": {
    "idempotency_key": "task_unique_id",
    "skip_if_exists": false,
    "overwrite_if_exists": false,
    "save_after_success": true,
    "compile_after_create": true,
    "open_after_success": false
  },
  "payload": { ... }
}
```

## 通用 result 结构

完整 schema 见 `Current_Truth.md`。失败时先看 `errors[].code` 与
`errors[].field`，再看 log 文件。

## 任务清单

### 蓝图创建 / 修改

#### `create_blueprint`

```
required:  payload.asset.{asset_name, package_path, parent_class}
optional:  payload.assembly.{root_component, components}
           payload.class_default_overrides
sample:    Samples/create_blueprint_actor.json
```

#### `modify_blueprint_components`

```
required:  payload.target_asset.asset_path
           payload.operations[]   op: add_component | update_component_properties
sample:    Samples/modify_blueprint_components.json
```

`update_component_properties` 把 `component_name` 写在 operation
顶层（不要嵌在 `component` 子对象里）。除 SCS 节点外，父类的 native
UPROPERTY 组件也能用同样的名字访问。

同名 SCS/native 组件可用 `component_lookup_policy` 显式控制：

| policy | 行为 |
|---|---|
| `scs_first` | 默认值。先查 SCS 层级，再查 native UPROPERTY 组件 |
| `native_first` | 先查 native UPROPERTY 组件，再查 SCS |
| `scs_only` | 只查 SCS |
| `native_only` | 只查 native UPROPERTY 组件 |

#### `modify_blueprint_defaults`

```
required:  payload.target_asset.asset_path
           payload.class_defaults[]  （或 payload.properties[]）
sample:    Samples/modify_blueprint_defaults.json
```

属性 `value` 接受的格式：

| `type` | `value` |
|---|---|
| `bool` / `int` / `float` | 标量 |
| `string` / `name` / `text` / `enum` | 字符串 |
| `object_path` / `class_path` / `soft_object_path` / `soft_class_path` | UE 路径字符串 |
| `vector` | `[X, Y, Z]` |
| `rotator` | `[Pitch, Yaw, Roll]` |
| `vector2d` | `[X, Y]` |
| `color` / `linear_color` | `[R, G, B, A]` |
| `array` | 数组，元素按其元素类型规则 |
| `set` | 数组 |
| `map` | 对象 `{key:value, ...}` 或数组 `[{key, value}]` |
| `struct` | 对象，字段名作为键，字段值递归遵循同样规则 |

### 模板任务

#### `create_blueprint_from_template`

```
required:  payload.asset.{asset_name, package_path}
           payload.template.template_id（必须存在于模板注册表）
optional:  payload.overrides.{component_overrides, class_default_overrides}
sample:    Samples/valid/create_blueprint_from_template_actor_basic.json
```

#### `batch_create_blueprints`

```
required:  payload.items[]，每个 item 必须含 asset；
           模板通过 payload.shared_template 或 items[].template 指定
sample:    Samples/valid/batch_create_blueprints_actor_basic.json
```

### Typed 资产创建

所有 `create_*` 都需要 `payload.asset.{asset_name, package_path}`。
其它要求：

| task | 额外要求 |
|---|---|
| `create_data_asset` | `asset.parent_class` |
| `create_material_instance` | `asset.parent_class` 是父材质；可选 `payload.parameters` |
| `create_blueprint_class` | `asset.parent_class` |
| `create_widget_blueprint` | 父类默认 `/Script/UMG.UserWidget` |
| `create_data_table` | row struct，通过 `asset.parent_class` 或 `parameters[].name="row_struct"` |
| `create_curve_float`、`create_curve_vector` | 无 |
| `create_animation_blueprint` | `parameters[].name="target_skeleton"`；可选 `preview_skeletal_mesh` |
| `create_blend_space` | 同上 |
| `create_level_sequence` | 无 |
| `create_physics_asset` | `parameters[].name="target_skeletal_mesh"` |
| `create_material_function` | 无 |
| `create_gameplay_ability` | 父类默认 `/Script/GameplayAbilities.GameplayAbility` |
| `create_gameplay_effect` | 父类默认 `/Script/GameplayAbilities.GameplayEffect` |

可用样例见 `Samples/valid/`。

### 导入任务

```
import_texture     payload.source_path + payload.asset.{asset_name, package_path}
import_sound_wave  同上
```

样例：`Samples/valid/import_texture_template.json`、
`Samples/valid/import_sound_wave_template.json`。

### 修改 / 校验

```
modify_material_instance  target_asset + parameters[]
                          parameter type: scalar/float、color/linear_color/vector ([R,G,B,A])、texture（路径）
modify_asset_properties   target_asset + properties[]   反射式顶层 UPROPERTY
check_asset_rules         target_asset 或 target_assets[]；可选 rules[]
generate_audit_report     可选 report.{path, format}
```

### Phase 4 只读分析

输出 meta 路径：

```
<PluginDir>/Saved/BlueprintMetaCache/<package-path>.meta.json
```

`payload.analysis` 通用键：

```
force_refresh       (bool, 默认 false)
use_cache           (bool, 默认 true)
include_native_cxx
include_blueprint_snapshot
include_class_defaults
include_components
include_references
include_referencers
include_graph_summary
include_graph_pins  (默认 false)
reference_depth     (int，受 settings.MaxReferenceAnalysisDepth 限制)
max_nodes / max_edges / max_property_depth / max_array_elements
export_only_editable_properties (默认 true)
```

#### `analyze_blueprint`

单蓝图分析。源/资产/options 任一变化时重写 meta。
样例：`Samples/valid/analyze_blueprint_basic.json`。

#### `analyze_asset`

任意 `UObject` 资产顶层 UPROPERTY 导出。非蓝图资产会写出
`BlueprintMetaCache/AssetMeta/<asset>.asset.json`；如果目标是蓝图，
会转到 `analyze_blueprint` 路径。

#### `analyze_blueprint_reference_chain`

递归 BFS。同时写出 `<asset>.graph.json`，并对每个引用蓝图生成对应
meta。
样例：`Samples/valid/analyze_blueprint_reference_chain.json`。

#### `refresh_blueprint_meta_cache`

强制重新分析。`payload.target_assets[]`（推荐）或 `payload.target_asset`。
样例：`Samples/valid/refresh_blueprint_meta_cache.json`。

#### `export_blueprint_ai_context`

紧凑的 `<asset>.context.json`，含 native parent、editable defaults、
被引用蓝图、建议的 task type。如果缓存 miss 会先跑一次
`analyze_blueprint`。
样例：`Samples/valid/export_blueprint_ai_context.json`。

### 资产复制与引用重写

#### `duplicate_asset`

`IAssetTools::DuplicateAsset` 的字节级复制。完整保留蓝图图节点、
GE/GA 逻辑、Curve 关键帧、DataTable 行等内部数据。

```json
{
  "task_type": "duplicate_asset",
  "payload": {
    "source_asset_path": "/Game/.../Q/X.X",
    "destination_package_path": "/Game/.../CopyQ",
    "destination_asset_name": "X",
    "overwrite_destination": false
  }
}
```

#### `redirect_asset_references`

在单个目标资产里重写对象引用。蓝图目标还会扫
`GeneratedClass`、CDO、同名 CDO sub-object 字段（让 native 组件
绑定能切换）。

```json
{
  "task_type": "redirect_asset_references",
  "payload": {
    "target_asset": { "asset_path": "/Game/.../CopyQ/X.X" },
    "redirects": [
      { "from": "/Game/.../Q/Y.Y", "to": "/Game/.../CopyQ/Y.Y" },
      { "from": "/Game/.../Q/Y.Y_C", "to": "/Game/.../CopyQ/Y.Y_C" }
    ]
  }
}
```

#### `list_directory_assets`

Asset Registry 目录枚举。artifact 写到
`BlueprintMetaCache/DirectoryListings/`。

```json
{
  "task_type": "list_directory_assets",
  "payload": {
    "directory_path": "/Game/PaperMan/CyAbilities/Yugiri/Q",
    "recursive": true
  }
}
```

## 安全策略

```
Config/UEEditorAutomationWhitelist.json
```

可控字段（**空数组 = 不限制**）：

- `policy_mode`: `open` 或 `strict`
- `allowed_task_types`
- `allowed_asset_roots`
- `allowed_parent_classes`
- `allowed_component_classes`
- `allowed_property_names`
- `denied_property_names_for_export`

调宽自动化范围请改 JSON，不要在 C++ 里硬编码。
`policy_mode="open"` 时空数组表示不限制；`policy_mode="strict"` 时
`allowed_task_types` 与 `allowed_asset_roots` 必须非空，否则加载失败。
Phase 4 分析任务是只读，不受 asset-root 白名单约束 ——
`allowed_task_types` 是唯一开关。

非只读任务会校验所有读写资产路径，包括 `source_asset_path`、
`destination_package_path`、`target_asset.asset_path`、`redirects[]`、
`directory_path` 和 batch item 的 `asset.package_path`。
