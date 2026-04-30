# 接口使用文档

`UEEditorAutomation` 的 JSON 任务协议。下面所有示例都是当前代码可解析的。

## 操作约束

- 插件 editor-only。
- 编辑器运行时把任务文件投递到 `C:/UEAutomation/tasks/inbox`。
- AI 代理不能调用 UBT、UAT、生成项目文件、启动编辑器，不能触发 C++ / Live Coding 编译。
- `compile_after_create` 表示在已运行的编辑器中执行 Blueprint compile，不是 C++ 编译。创建或修改蓝图结构、CDO、组件、引用关系后，应设置 `compile_after_create: true` 并 `save_after_success: true`，用来暴露蓝图编译错误和持久化问题。
- 只读分析任务、非蓝图资产复制/分析任务不需要蓝图编译。

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

任务级收尾归档由 AI 编排器负责，不由 UE 插件自动完成。仅当用户要求 AI
使用 UEEditorAutomation 执行蓝图/资产分析、创建、修改、复制、引用重写、
验证等自动化任务时，AI 必须把本次生成/提交的任务 JSON、workflow plan、
result 摘要和临时 JS 汇总为文字流程报告，输出到：

```
<PluginDir>/Saved/TaskReports/yyyyMMddHHmmss_中文概述.md
```

随后将本次过程生成的 JSON 和 JS 打包为同名 `.zip`，确认压缩包可读后删除
散落的临时 JSON/JS。报告中记录 task_id、result 路径、成功/失败状态和验证
结论，不直接粘贴大段 JSON。`C:/UEAutomation/tasks` 与 `C:/UEAutomation/results`
中的正式归档不删除。普通问答、源码阅读、文档编辑、方案讨论、非 UE 自动化
脚本的小改动，不触发该报告/zip 流程。

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

对蓝图写入任务，推荐保持 `compile_after_create: true`。如果任务只是列目录、分析资产、复制非蓝图资源，可以省略或设为 false。

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
| `scs_inherited_override` | 查父蓝图 SCS 组件，并在目标子蓝图的 `UInheritableComponentHandler` 中创建/获取 override template |

新任务推荐使用更明确的 `target_kind`，旧的 `component_lookup_policy`
继续兼容：

| target_kind | 写入目标 |
|---|---|
| `own_scs_template` | 当前蓝图自身 SCS component template |
| `scs_inherited_override` | 当前蓝图对父类 SCS component 的 inherited override template |
| `native_template` | C++ native component template |

示例：

```json
{
  "op": "update_component_properties",
  "component_name": "ExampleSceneMapComponent",
  "target_kind": "scs_inherited_override",
  "properties": [
    { "name": "VisionSwitch", "type": "import_text", "value": "(bVisibleToCustom=True)" }
  ]
}
```

#### `modify_blueprint_defaults`

```
required:  payload.target_asset.asset_path
           payload.class_defaults[]  （或 payload.properties[]）
sample:    Samples/modify_blueprint_defaults.json
```

#### `copy_live_blueprint_values`

从 source live 蓝图对象读取真实属性值，再写入 target live 蓝图对象。该任务不使用
meta 里的 value，也不信任调用方传入的 property value；`properties[]` /
`class_defaults[]` 只提供要复制的属性名。

```
required:  payload.source_asset_path
           payload.target_asset.asset_path
           payload.class_defaults[] 或 payload.operations[]
```

CDO 字段复制：

```json
{
  "task_type": "copy_live_blueprint_values",
  "payload": {
    "source_asset_path": "/Game/A/A.A",
    "target_asset": { "asset_path": "/Game/B/B.B" },
    "class_defaults": [
      { "name": "WeaponNameTag" }
    ]
  }
}
```

组件字段复制：

```json
{
  "op": "copy_component_properties",
  "component_name": "StateEquip",
  "target_kind": "native_template | scs_inherited_override | own_scs_template",
  "properties": [
    { "name": "LooseGameplayTags" }
  ]
}
```

读取 source 组件时不会创建 override。写入 target 时才按 `target_kind`
解析真实写入位置：

| target_kind | source 读取 | target 写入 |
|---|---|---|
| `native_template` | native component template | native component template |
| `own_scs_template` | 当前蓝图 SCS template | 当前蓝图 SCS template |
| `scs_inherited_override` | live effective SCS template，副作用为 0 | 当前蓝图 ICH override template，不存在则创建 |

写入后会重新读取 target live 对象做 post-write / post-compile 验证。

#### `copy_blueprint_live_overrides`

更高层的蓝图配置复刻原子接口。调用方只提供 source / target / redirects，
C++ 会从 source live Blueprint 读取真实结构并自动生成本次复制计划：

- CDO：只复制 `CPF_Edit`、非 transient / deprecated / delegate、且相对父类不同的字段。
- CDO component object refs：跳过，例如 `RootComponent`、`SkinComponent`、native component 指针。
- components：读取 owned SCS、inherited SCS、native components；只复制相对父模板不同的属性。
- 写入目标由 C++ 按组件来源决定：`own_scs_template`、`scs_inherited_override`、`native_template`。
- 属性值仍由 C++ 从 source live UObject `ExportTextItem` 读取，meta 只可作为缓存/观察结果。

```json
{
  "task_type": "copy_blueprint_live_overrides",
  "payload": {
    "source_asset_path": "/Game/A/A.A",
    "target_asset": { "asset_path": "/Game/B/B.B" },
    "redirects": [
      { "from": "/Game/A/A.A", "to": "/Game/B/B.B" },
      { "from": "/Game/A/A.A_C", "to": "/Game/B/B.B_C" }
    ]
  }
}
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
| `import_text` | UE `FProperty::ExportTextItem` 文本，写入时直接交给目标 `FProperty::ImportText` |

`import_text` 用于复杂 GAS / 项目自定义结构体、嵌套 Map/Array/Struct
等不适合从 JSON 反推 ImportText 的字段。Phase 4 meta 会随每个导出
属性写出 `import_text`，编排器可在重放到 CopyQ 前对文本里的资产路径
做 Q -> CopyQ 替换。

`modify_blueprint_defaults` 与 `modify_blueprint_components` 也可携带
`payload.redirects[]`。当属性类型为 `import_text` 时，写入器会在
`ImportText` 前按 redirects 替换完整对象路径与 package 前缀。

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

每个导出的 `class_defaults[]` 与组件 `properties[]` 条目包含：

```
name / ue_type / value / import_text / editable / blueprint_visible / differs_from_parent
```

`value` 受 `max_property_depth` 与 `max_array_elements` 限制，可能出现
`truncated:true`；`import_text` 是 UE 原生属性文本，不受 JSON 深度截断影响。

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
    "directory_path": "/Game/Example/SourceFeature",
    "recursive": true
  }
}
```

#### `delete_directory_assets`

按 Asset Registry 枚举并删除目录下资产。用于任务前清理目标目录，当前
只接受 `/Game/...` package path，并继续受 `allowed_asset_roots` 白名单约束。

```json
{
  "task_type": "delete_directory_assets",
  "payload": {
    "directory_path": "/Game/Example/TargetFeature",
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
