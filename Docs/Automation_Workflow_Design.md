# UE 资产/蓝图自动化工作流设计

本文定义 `UEEditorAutomation` 后续演进方向：C++ 侧提供稳定、可组合、语义正确的 UE 原子能力；AI / 脚本 / 人类工具负责把目标拆解为有序调用。本文不是某个单一 CopyQ/CopyX 工具设计，而是面向多类蓝图与资产自动化任务的系统设计。

## 设计目标

当前插件已经具备蓝图创建、组件修改、属性写入、资产复制、引用重写、蓝图/资产分析等能力，但复杂任务仍大量依赖外部 JS 临时编排。CopyQ/CopyX 过程暴露了几个问题：

- JS 承担了过多 UE 语义决策，例如组件来源、可写字段、特殊类型写入、diff 分级。
- 每次复制不同目录都要生成或调整复杂脚本，使用成本高。
- exporter、writer、diff 的语义容易漂移，导致假阳性或假阴性。
- 对父蓝图 SCS 组件在子蓝图中的 `UInheritableComponentHandler` override 这类 UE 内部机制，必须由 C++ 原子接口正确处理，不能依赖脚本猜测。

目标形态：

```text
C++:
  UE 事实来源
  反射读写
  蓝图内部结构处理
  原子操作安全性
  结构化结果

AI / 编排层:
  业务目标理解
  工作流模板选择
  步骤规划
  参数组装
  错误诊断
  增量修复策略

脚本:
  可选传输胶水
  批量提交任务
  等待结果
  文件整理
```

核心原则：

```text
C++ 不硬编码“复刻整个技能目录”这类业务流程。
C++ 必须保证每个原子任务能正确接触 UE 的真实存储层。
AI 可以把自然语言目标编排成对 C++ 原子接口的可审计调用序列。
meta 只是 Snapshot/分析结果的性能缓存；任何涉及读值、写值、校验值的步骤，都必须重新读取 UE 实际资产对象，不能把 meta 当作真实值来源。
```

## 事实来源边界

`meta` 的定位是分析缓存，不是写入指令，也不是校验真相。它可以用于：

```text
- 快速发现候选资产、候选组件、候选字段。
- 生成初版 workflow plan。
- 帮助 AI 理解蓝图结构和 C++ 逻辑链路。
- 对昂贵分析结果做缓存，减少重复扫描。
```

它不能用于：

```text
- 作为写入值的唯一来源。
- 作为 expected/actual diff 的权威来源。
- 判断目标资产当前是否已经写入成功。
- 判断组件是否真实存在或 override 是否真实存在。
```

进入 `Mutation` 或 `Validation` 后，C++ 必须从真实 UE 对象读取当前状态：

```text
资产存在性:
  AssetRegistry / LoadObject / package 文件状态

蓝图 CDO:
  Blueprint->GeneratedClass->GetDefaultObject()

owned SCS component:
  USimpleConstructionScript / USCS_Node / ComponentTemplate

inherited SCS component:
  UInheritableComponentHandler override template
  或父类实际模板 + 当前类 override 状态

native component:
  CDO / native default subobject / editor-visible UPROPERTY component reference

属性值:
  FProperty on live UObject
  ExportTextItem / structured serializer / type-specific reader
```

因此正确链路是：

```text
Analyze:
  live UE asset -> snapshot -> meta cache

Plan:
  meta cache + user goal -> candidate workflow_plan

Apply:
  workflow_plan -> C++ atomic task
  C++ task re-loads live UE asset
  C++ task re-resolves component/property
  C++ task writes and verifies live object

Validate:
  C++ re-loads live source/target assets
  C++ reads live values
  C++ emits semantic diff
```

如果 `meta` 与 live asset 冲突，live asset 永远优先；同时结果中应输出 `stale_meta_warning`，提示重新刷新分析缓存。

## 当前源码基础

当前工程已经有以下模块，可作为新设计的基础：

```text
Application:
  TaskExecutor.h
  EditorAutomationApplicationService.h
  BlueprintTaskExecutors.h
  BlueprintAnalysisTaskExecutors.h
  AssetTaskExecutors.h
  AssetDuplicationTaskExecutors.h

Domain:
  BlueprintAutomationService
  AssetAutomationService
  AssetDuplicationService
  BlueprintAnalysisService
  BlueprintMetaCacheService
  PropertySnapshotService
  PropertyAssignmentService
  BlueprintSnapshotExporter
  AssetReferenceGraphService

Adapter:
  UEBlueprintEditorAdapter

Transport:
  FileTaskTransport
  SocketTaskServer
```

当前可用 task 大致分为：

```text
Discovery:
  list_directory_assets

Snapshot:
  analyze_blueprint
  analyze_asset
  analyze_blueprint_reference_chain
  refresh_blueprint_meta_cache
  export_blueprint_ai_context

Mutation:
  create_blueprint
  create_blueprint_from_template
  batch_create_blueprints
  modify_blueprint_components
  modify_blueprint_defaults
  copy_live_blueprint_values
  create_data_asset / create_* typed assets
  modify_asset_properties
  duplicate_asset
  redirect_asset_references

Validation / Audit:
  check_asset_rules
  generate_audit_report
```

这些能力的方向是正确的，但接口语义还需要进一步收敛：组件来源、写入目标、属性安全策略、diff 语义需要成为 C++ 输出和输入协议的一部分。

## 分层架构

建议把系统稳定成五层。

### 1. Discovery 层

只负责发现资产、引用、符号和候选对象，不做修改。

职责：

- 目录资产枚举。
- 资产正向/反向引用查询。
- 蓝图图节点/函数/事件入口搜索。
- C++ 类、函数、UPROPERTY、UFUNCTION 符号定位。

代表接口：

```text
list_assets
find_references
find_referencers
search_blueprints
search_cpp_symbols
```

当前已有 `list_directory_assets` 与 `AssetReferenceGraphService`，后续应补齐搜索类接口。

### 2. Snapshot 层

只负责把 UE 内部状态导出为可信结构化数据。Snapshot 必须表达“值是什么、从哪里来、应该写到哪里、能不能写”。

蓝图组件 snapshot 必须覆盖三类组件：

```text
native:
  C++ 构造函数 / UPROPERTY 默认子对象

scs_inherited:
  父类蓝图 SimpleConstructionScript 添加的组件
  子类修改值存储在 UInheritableComponentHandler override template

scs_owned:
  当前蓝图自身 SimpleConstructionScript 添加的组件
```

推荐组件输出结构：

```json
{
  "component_name": "AttackComponent",
  "component_class": "/Script/PMGame.CyThrowComponent",
  "source_kind": "native | scs_inherited | scs_owned",
  "declared_in": "c++ | /Game/.../ParentBP | /Game/.../SelfBP",
  "template_kind": "native_template | inherited_parent_template | inherited_override_template | own_scs_template",
  "has_override_template": true,
  "writable_target_kind": "native_template | inheritable_component_handler | own_scs_template",
  "attach_parent": "",
  "properties": [
    {
      "name": "Projectiles",
      "ue_type": "ArrayProperty",
      "value": [],
      "import_text": "()",
      "editable": true,
      "blueprint_visible": false,
      "differs_from_parent": true,
      "safe_to_write": true,
      "write_mode": "import_text | json | skip",
      "skip_reason": null
    }
  ]
}
```

关键要求：

- `scs_inherited` 的 `differs_from_parent` 必须和直接父类实际模板比较，而不是和最初声明组件的祖先模板比较。
- `has_override_template` 必须只表示当前蓝图是否拥有自己的 `UInheritableComponentHandler` override，不应把父类已有 override 算作当前蓝图 override。
- native component 应按父 CDO 上同名/同 UPROPERTY component 做 diff。
- transient、delegate、runtime cache、editor-only 噪声字段应在 C++ 标注 `safe_to_write=false` 与 `skip_reason`。

### 3. Mutation 层

只负责原子修改，不做高层业务策略判断。调用者必须明确目标和原因；C++ 必须保证写入落到正确 UE 存储层。

代表接口：

```text
create_asset
duplicate_asset
create_blueprint
add_scs_component
update_component_properties
update_cdo_properties
update_asset_properties
redirect_references
save_asset
compile_blueprint
```

其中 `update_component_properties` 必须显式支持写入目标：

```json
{
  "task_type": "update_component_properties",
  "payload": {
    "target_asset": {
      "asset_path": "/Game/.../Child.Child"
    },
    "component_name": "ExampleSceneMapComponent",
    "target_kind": "scs_inherited_override",
    "properties": []
  }
}
```

`target_kind` 推荐值：

| target_kind | 写入位置 | 行为 |
|---|---|---|
| `own_scs_template` | 当前蓝图 SCS node template | 缺组件时由 `add_scs_component` 创建，不在 update 中隐式 add |
| `scs_inherited_override` | 当前蓝图 `UInheritableComponentHandler` override template | override 不存在时创建 |
| `native_template` | 目标蓝图 CDO/native component template | 不 add，只更新 |

当前 `modify_blueprint_components` 的 `component_lookup_policy` 可以兼容保留，但建议逐步迁移到更明确的 `target_kind`。

`copy_live_blueprint_values` 是跨资产复刻类 workflow 的核心原子接口：

```text
输入:
  source_asset_path
  target_asset.asset_path
  class_defaults[] property names
  operations[] component property names + target_kind

行为:
  load source live blueprint
  load target live blueprint
  read source CDO/component live FProperty values
  export to import_text inside C++
  write target CDO/component live object
  compile/save target
  re-read target live object and verify

禁止:
  从 meta 读取 value 作为写入内容
  source 读取时创建 inherited override
```

### 4. Validation 层

只负责比较、归一化、分级报告，不执行修改。

代表接口：

```text
semantic_diff_assets
semantic_diff_snapshots
validate_references
validate_missing_assets
validate_compile_status
scan_residual_source_refs
```

diff 输出应分级：

```text
Blocking:
  缺资产
  缺组件
  父类不一致
  关键玩法字段不同
  残留源路径引用

Semantic-risk:
  inherited override 源有目标无
  BodyInstance 关键字段不同
  GameplayAttribute owner 推导风险
  部分复杂 struct 字段不同

Noise:
  [] vs [null]
  package md5 不同
  默认数组展示差异
  引擎自动生成展示差异
```

Validation 不应信任 apply 返回值。必须重新 snapshot 目标资产后再 diff。

### 5. Orchestration 层

由 AI、脚本或人类工具承担。

职责：

- 选择工作流模板。
- 补齐输入参数。
- 生成可审计 plan。
- 分阶段提交 C++ 原子任务。
- 读取 result，诊断失败。
- 生成增量 repair plan。
- 输出最终报告。

Orchestration 可以用 JS、PowerShell、Python、直接 JSON 任务文件或 socket。脚本只是传输手段，不应该承载 UE 核心语义。

## 原子接口规范

### 请求结构

现有协议可继续使用：

```json
{
  "protocol_version": 1,
  "task_id": "...",
  "task_type": "...",
  "timestamp_utc": "...",
  "execution": {
    "save_after_success": true,
    "compile_after_create": true,
    "open_after_success": false
  },
  "payload": {}
}
```

`compile_after_create` 指已运行编辑器内的 Blueprint compile，不是 C++/UBT/Live Coding 编译。蓝图创建、组件修改、CDO/default 写入、引用重定向后必须执行 Blueprint compile + save，再进入 Verify。蓝图编译失败属于 blocking 问题，不能因为“不自动编译 C++”约束而跳过。

建议新增通用字段：

```json
{
  "payload": {
    "reason": "source property differs from direct parent",
    "expected_effect": "CopyX inherited component override should match X",
    "dry_run": false
  }
}
```

### Result 结构增强

当前 result 有 `success/errors/warnings/metrics/artifacts`。建议 mutation 类任务增加 `field_results`：

```json
{
  "success": true,
  "changed": true,
  "skipped": false,
  "field_results": [
    {
      "path": "Component.ExampleSceneMapComponent.VisionSwitch",
      "status": "written | skipped | failed",
      "write_target": "scs_inherited_override",
      "write_mode": "import_text",
      "reason": "differs_from_parent",
      "message": ""
    }
  ]
}
```

这样编排层不需要从日志猜“到底写到了哪里”。

## 工作流模板系统

任务不应定死为“复制目录”。建议引入开放的 Workflow Template 概念。模板只定义阶段、输入、推荐能力和产物，不硬编码具体资产。

模板注册结构：

```json
{
  "template_id": "blueprint.similar_feature_from_reference",
  "display_name": "参考已有蓝图实现类似功能",
  "required_inputs": ["reference_assets", "target_root", "feature_goal"],
  "optional_inputs": ["constraints", "naming_policy"],
  "phases": ["observe", "plan", "apply", "verify"],
  "recommended_capabilities": [
    "list_assets",
    "snapshot_blueprint",
    "snapshot_reference_graph",
    "create_blueprint",
    "update_component_properties",
    "semantic_diff_assets"
  ],
  "outputs": ["workflow_plan.json", "final_report.json"]
}
```

### 模板 1：参考已有蓝图实现类似功能

适用场景：

```text
参考 A 技能/蓝图/资产链，实现一个 B，逻辑类似但配置、引用、命名不同。
```

输入：

```text
reference_root 或 reference_assets
target_root
feature_goal
naming_policy
constraints
```

阶段：

```text
Observe:
  list reference assets
  snapshot reference blueprints
  snapshot reference non-BP assets
  snapshot C++ parent class and reflected fields
  export reference graph

Plan:
  identify asset role graph:
    Ability -> Weapon -> Projectile/Summon -> GE/DataAsset/Effect
  decide create/duplicate strategy
  decide CDO/component/property write set
  build redirect map

Apply:
  create/duplicate assets
  add owned SCS components
  update native/scs_owned/scs_inherited_override properties
  update CDO
  redirect references

Verify:
  snapshot target
  semantic diff against reference pattern
  scan stale refs
  output final report
```

### 模板 2：参考文档实现蓝图配置

适用场景：

```text
根据策划文档、表格说明、文本规则创建或修改蓝图/DataAsset 配置。
```

输入：

```text
design_doc
target_assets 或 target_root
schema hints
allowed parent classes
reference examples optional
```

阶段：

```text
Observe:
  parse text
  inspect similar assets
  snapshot target parent C++ reflected fields

Plan:
  map document terms to UE properties
  resolve enums, GameplayTags, DataAsset refs, Curve refs
  record assumptions and unresolved items

Apply:
  create/update assets
  write properties
  link references

Verify:
  property coverage report
  unresolved requirement list
  invalid refs / enum mismatch / missing tags
```

输出必须包含：

```text
文档要求覆盖率
每条要求对应的 UE 字段
未能自动落地的原因
```

### 模板 3：蓝图功能分析报告

适用场景：

```text
分析蓝图功能、C++ 链路、性能、安全性、合理性。
```

输入：

```text
target_blueprints
analysis_focus: performance | networking | safety | gameplay | all
reference_context optional
```

阶段：

```text
Observe:
  snapshot blueprint
  export graph summary/pins as needed
  snapshot native parent C++ context
  export reference graph

Analyze:
  identify entry events and state transitions
  identify Tick/Timer/Async/Latent usage
  inspect network authority/client execution
  inspect asset load/reference closure
  inspect GameplayEffect/Attribute/Tag dependencies
  inspect component hierarchy and collision setup

Report:
  findings by severity
  evidence paths
  performance risks
  safety/null reference risks
  design consistency risks
```

### 模板 4：蓝图 bug 查找

适用场景：

```text
根据 bug 描述、复现步骤、可疑资产，定位原因和修复建议。
```

输入：

```text
bug_description
repro_steps
suspect_assets optional
expected_behavior optional
actual_behavior optional
```

阶段：

```text
Observe:
  search related blueprints, C++ symbols, tags, events, assets
  snapshot suspect chain
  export graph and C++ context

Reason:
  build hypotheses:
    input condition -> blueprint event -> C++ call -> state mutation -> result
  locate breakpoints:
    config missing
    wrong reference
    branch condition false
    authority/client mismatch
    lifecycle ordering
    inherited override missing

Verify:
  collect evidence per hypothesis
  rank likely causes
  generate minimal repair plan or manual verification points
```

## 标准工作流方法

无论使用哪个模板，推荐固定三阶段。

### Phase 1：Observe

只读，不修改：

```text
list assets
snapshot source
snapshot target if exists
snapshot C++ context if needed
export reference graph if needed
generate workflow_plan.json
```

输出必须回答：

```text
要创建哪些资产
要添加哪些 own SCS component
要写哪些 inherited override
要写哪些 native override
要写哪些 CDO 字段
哪些字段跳过，为什么
哪些地方需要人工确认
```

### Phase 2：Apply

只执行 plan，不重新推理：

```text
apply A: create / duplicate assets
apply B: add owned SCS components
apply C: update component properties
apply D: update CDO / asset properties
apply E: redirect references
apply F: save assets
```

每个 step 输出：

```text
success / failed / skipped
field_results
changed assets
warnings
errors
```

### Phase 3：Verify

重新观测，不信任 apply 返回值：

```text
snapshot target
semantic diff source vs target / expected spec
scan residual source refs
generate final_report.json
```

失败时只生成增量修复：

```text
repair_plan.json
```

不要无脑重跑全量流程。

## Workflow Plan 格式

推荐 plan 结构：

```json
{
  "workflow_id": "example_copy_feature_001",
  "template_id": "asset.semantic_reconstruct",
  "goal": "Replicate /Game/Example/SourceFeature to /Game/Example/TargetFeature",
  "inputs": {
    "source_root": "/Game/Example/SourceFeature",
    "target_root": "/Game/Example/TargetFeature"
  },
  "assumptions": [],
  "phases": [
    {
      "id": "observe",
      "status": "done",
      "artifacts": []
    },
    {
      "id": "apply",
      "status": "pending",
      "actions": [
        {
          "id": "C2-Preview-CySceneMap",
          "action": "update_component_properties",
          "target_asset": "/Game/.../Preview.Preview",
          "component_name": "ExamplePreviewSceneMapComponent",
          "target_kind": "scs_inherited_override",
          "reason": "source property differs from direct parent",
          "properties": []
        }
      ]
    }
  ],
  "verification": {
    "checks": [
      "semantic_diff",
      "scan_residual_source_refs"
    ]
  }
}
```

## 类型与属性策略

C++ snapshot 与 mutation 必须统一属性策略。

### 可写字段

默认可写：

```text
editable 或 blueprint_visible 的字段
source differs_from_parent=true 的 CDO 字段
source differs_from_parent=true 的 component 字段
own SCS component 模板字段
native/inherited override 字段
```

默认跳过：

```text
delegate / multicast delegate
transient / duplicate transient
deprecated
runtime cache
editor-only runtime 展示字段
truncated 且无 import_text 的字段
默认噪声字段
```

C++ 应输出：

```json
{
  "safe_to_write": false,
  "skip_reason": "delegate | transient | truncated | default_noise | unsupported"
}
```

### 特殊类型

特殊类型策略应在 C++ 内统一实现，不应散落在 JS。

```text
FGameplayAttribute:
  AttributeOwner 可由 Attribute 推导
  diff 中 null owner 和可推导 owner 可等价

BodyInstance:
  不盲写整个 struct
  通过 UPrimitiveComponent setter 写 CollisionProfile/ObjectType/Enabled/Responses
  注意 SetObjectType/SetResponse 会使 profile 变 Custom，profile 需要最后处理或等价归一化

ActorGroupInstance:
  项目 struct 可走反射递归字段写入

Delegate / MulticastDelegate:
  skip，不写，不参与语义 diff

Array default noise:
  [] / [null] 对 AssetUserData、OverrideMaterials、RuntimeVirtualTextures 等等价
```

## 当前 CopyQ/CopyX 脚本的迁移路线

短期保留脚本作为编排样例，但逐步把语义沉到 C++。

### Stage 1：强化原子接口

- `snapshot_blueprint` 输出完整组件来源和可写目标。
- `update_component_properties` 支持 `target_kind`。
- mutation result 输出 `field_results`。
- C++ 内实现属性安全策略。

### Stage 2：C++ semantic diff 原子接口

新增：

```text
semantic_diff_assets
semantic_diff_snapshots
scan_residual_source_refs
```

替代历史临时 diff 脚本中的语义归一化逻辑。

### Stage 3：Plan 仍由 AI 生成，但 plan schema 标准化

AI 读取 snapshot，生成 `workflow_plan.json`。执行层只按 plan 提交原子任务。

脚本只负责：

```text
submit
wait
collect result
write report
```

### Stage 4：模板注册

新增 `Docs/Workflow_Templates.md` 或配置文件：

```text
Config/UEEditorAutomationWorkflowTemplates.json
```

定义模板输入、阶段、推荐能力、输出产物。

## 安全与工程约束

- 不自动执行 C++ 编译、UBT/UAT、GenerateProjectFiles、启动编辑器或 Live Coding。蓝图写入任务必须执行 Blueprint compile + save；`compile_after_create` 对蓝图任务默认应为 true。只有 C++ 代码变更需要验证时，才要求用户人工编译后再继续。
- 所有 mutation 必须受 whitelist 约束。
- 所有 plan 必须可审计、可重放。
- 删除资产必须单独设计安全接口，不能混在复制/重建流程里。
- 任何跨资产批量任务都应可断点续跑，并输出每个资产/字段级结果。
- Snapshot schema 变更必须提升 meta schema 或 options digest，避免旧 meta 假干净。

## 成功标准

系统成熟后，用户自然语言请求：

```text
参考 A 做一个 B
根据文档配置技能
分析这个蓝图性能风险
根据 bug 描述查原因
把 X 复刻到 CopyX
```

AI 应能：

```text
选择模板
调用 C++ 原子接口观察
生成 plan
执行或只报告
验证
输出最终结论和可追踪证据
```

使用者不需要理解临时 JS，不需要知道 `UInheritableComponentHandler`、`USCS_Node`、CDO、ImportText 等细节；这些细节由 C++ 原子接口和 snapshot 语义承担。
