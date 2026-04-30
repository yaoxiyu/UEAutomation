# AI 工作流快速启动指南

本文是新会话入口文档。处理 `UEEditorAutomation` 相关任务时，AI 先读本文即可开始工作。更详细设计见 `Docs/Automation_Workflow_Design.md`，接口细节见 `Docs/Task_Interface.md`，当前实现真相见 `Docs/Current_Truth.md`。

## 绝对硬约束

- 本项目所有 C++ 编译、构建、UBT、UAT、GenerateProjectFiles、启动编辑器、Live Coding 编译都必须由用户人工完成。
- AI 不允许执行任何等价 C++ 编译 / 构建 / 启动命令。
- AI 可以读取源码、修改源码、生成任务 JSON、提交编辑器自动化任务、等待 result、分析 result。
- 当 C++ 改动需要验证时，必须提示用户人工编译，并等待用户反馈。
- 蓝图编译不属于上述 C++ 编译禁令。凡是创建或修改蓝图结构、CDO、组件、引用关系的任务，Apply 后必须通过自动化任务执行 Blueprint compile + save，再重新 Observe/Verify，以暴露蓝图编译错误和持久化问题。
- 自动化任务中默认设置：

```json
{
  "compile_after_create": true,
  "open_after_success": false
}
```

只读任务、非蓝图资产分析/复制任务可以不设置蓝图编译；涉及蓝图写入的任务应显式保持 `compile_after_create: true`。

## 核心工作方式

`UEEditorAutomation` 的定位不是单一 Copy 工具，而是 UE 资产/蓝图自动化工作台。

## 启动时强制动作

新会话不能直接从自然语言目标开始临时摸索。读完本文后，必须先做本地能力发现：

```powershell
rg -n "copyq|CopyQ|CopyX|semantic_reconstruct|workflow|template|plan" Docs Config Samples -g "*.md" -g "*.json"
Get-ChildItem Docs -File
```

必须优先阅读：

```text
Docs/Current_Truth.md        当前真实可用接口和限制
Docs/Task_Interface.md       JSON task 协议
Docs/Tech_Debt.md            已知坑，尤其 Q -> CopyQ
Config/UEEditorAutomationTemplates.json
```

`Saved` 是运行产物目录，只允许保留 meta、listing、report、task result 摘要等可再生成产物。不要把 `Saved/*.js` 当作可复用入口。

仅当用户要求 AI 使用 `UEEditorAutomation` 完成蓝图/资产分析、创建、修改、复制、引用重写、验证等自动化任务时，任务结束后才必须执行任务收尾归档。普通问答、源码阅读、文档编辑、方案讨论、非 UE 自动化脚本的小改动，不触发该报告/zip 流程：

```text
1. 汇总本次生成/提交的任务 JSON、workflow plan、result 摘要、临时 JS 胶水脚本。
2. 在 Saved/TaskReports/ 输出一份文字为主的任务流程报告。
3. 报告文件名必须以 yyyyMMddHHmmss 开头，后接中文概述，例如：
   20260430193015_补齐晶源体卡牌立即变身流程报告.md
4. 报告内容说明本次任务目标、关键观察、计划、执行步骤、每个任务的 task_id/result 概况、验证结论、风险与人工后续动作；不要直接粘贴大段 JSON。
5. 将本次过程生成的 JSON 和 JS 打包成同名 zip，例如：
   20260430193015_补齐晶源体卡牌立即变身流程报告.zip
6. zip 创建成功后，删除本次过程散落的临时 JSON 和 JS；Saved 中只保留报告、zip、必要 meta/listing/report 等可再生成产物。
7. 若某些 JSON 已被文件队列归档到 C:/UEAutomation/tasks 或 result 目录，报告中记录 task_id 与 result 路径；不要删除 UEAutomation 的任务归档和 result。
```

硬约束：

```text
meta 只是分析缓存。
AI 可以用 meta 生成候选 plan，但不能把 meta 当作写入值或 diff 真相。
凡是涉及读值、写值、校验值，必须调用 C++ 原子接口读取 live UE 资产对象。
如果 meta 和 live 反射结果冲突，以 live 结果为准，并刷新 meta。
```

如果需要脚本，只能作为本次任务的传输胶水临时生成，执行完成后删除。脚本不得承载 UE 语义规则，不得沉淀成下次任务入口。

只有以下内容可以复用：

```text
C++ 原子接口
Docs 中明确文档化的 workflow template
Config 中明确注册的模板
```

职责边界：

```text
C++:
  UE 原子能力内核
  负责真实读取/写入 UE 内部结构
  负责反射、SCS、CDO、UInheritableComponentHandler、引用替换、属性安全校验

AI:
  计划器、诊断器、执行协调器
  负责理解目标、选择工作流模板、生成 plan、调用 C++ 原子接口、验证结果

脚本:
  可选传输胶水
  只负责 submit/wait/collect，不承载核心 UE 语义
  UEEditorAutomation 蓝图/资产任务中临时生成，任务结束后先归档进 Saved/TaskReports 同名 zip，再删除；除非它被正式提升为 Docs/Config 中的模板
```

原则：

```text
不要把复杂业务逻辑塞进临时 JS，也不要复用历史 JS 作为工作流入口。
复杂任务必须先 Observe，生成可审计 plan，再 Apply，最后 Verify。
失败后生成增量 repair plan，不要无脑重跑全量。
```

## 标准三阶段

### 1. Observe

只读，不修改。

常用动作：

```text
list_directory_assets
analyze_blueprint
analyze_asset
refresh_blueprint_meta_cache
analyze_blueprint_reference_chain
export_blueprint_ai_context
读取 C++ 源码
读取已有 meta / listing / result
```

输出：

```text
workflow_plan.json 或等价 plan
```

plan 必须说明：

```text
要创建/复制哪些资产
要添加哪些 own SCS component
要更新哪些 own_scs_template
要更新哪些 scs_inherited_override
要更新哪些 native_template
要写哪些 CDO / DataAsset 字段
要做哪些引用重定向
哪些字段跳过，原因是什么
风险和假设是什么
```

### 2. Apply

只执行 plan，不边执行边重新猜。

常用动作：

```text
create_blueprint
duplicate_asset
modify_blueprint_components
modify_blueprint_defaults
copy_live_blueprint_values
modify_asset_properties
redirect_asset_references
```

要求：

```text
按阶段提交任务
保存 task ids 和 summary
检查 success / errors / warnings / field_results
不要自动触发 C++ 编译；蓝图写入任务应自动 Blueprint compile + save
```

### 3. Verify

重新观测，不信任 Apply 返回值。

常用动作：

```text
refresh_blueprint_meta_cache
semantic diff / 自定义 diff
scan residual source refs
读取 result 和日志
```

验证报告必须分级：

```text
Blocking:
  缺资产、缺组件、父类不一致、关键字段不同、残留源路径引用

Semantic-risk:
  inherited override 缺失、复杂 struct 部分不同、BodyInstance 关键字段不同

Noise:
  package md5 diff、[] vs [null]、默认数组展示差异
```

### 4. Archive

仅当本次工作实际使用 UEEditorAutomation 执行蓝图/资产自动化任务时，任务完成后必须进行收尾归档，不因 Verify 成功而省略：

```text
汇总本次 task JSON / workflow plan / result 摘要 / 临时 JS
生成 Saved/TaskReports/yyyyMMddHHmmss_中文概述.md
生成同名 zip，包含本次过程生成的 JSON 和 JS
确认 zip 可读后删除本次散落的临时 JSON 和 JS
最终回复中说明报告和 zip 路径
```

## 组件模型必须完整

一个蓝图的组件至少有三类来源，必须全部读取、diff、写回：

```text
1. native component
   C++ 构造函数 / UPROPERTY 默认子对象
   写入目标：native_template
   不能 add，只能更新 native template

2. scs_inherited component
   父蓝图 SimpleConstructionScript 添加的组件
   子类修改值存储在 UInheritableComponentHandler override template
   写入目标：scs_inherited_override
   不能 add，要创建/更新 inherited override template

3. scs_owned component
   当前蓝图自身 SimpleConstructionScript 添加的组件
   写入目标：own_scs_template
   缺失时 add_component，然后写自身模板属性
```

`modify_blueprint_components` 新任务推荐使用：

```json
{
  "op": "update_component_properties",
  "component_name": "AttackComponent",
  "target_kind": "native_template",
  "properties": []
}
```

`target_kind` 可选值：

```text
native_template
own_scs_template
scs_inherited_override
```

旧字段 `component_lookup_policy` 仍兼容，但新 plan 优先使用 `target_kind`。

## 原子接口使用边界

C++ 提供原子接口，不负责决定整个业务流程。

AI 应优先组合这些接口：

```text
Discovery:
  list_directory_assets

Snapshot:
  analyze_blueprint
  analyze_asset
  refresh_blueprint_meta_cache
  analyze_blueprint_reference_chain
  export_blueprint_ai_context

Mutation:
  create_blueprint
  duplicate_asset
  modify_blueprint_components
  modify_blueprint_defaults
  modify_asset_properties
  redirect_asset_references

Validation:
  check_asset_rules
  generate_audit_report
  自定义 semantic diff / residual ref scan
```

如果某个任务需要 C++ 尚未提供的原子能力，优先：

```text
1. 明确缺失的原子能力
2. 修改 C++ 接口
3. 让用户人工编译
4. 再用新接口执行工作流
```

不要用大量 JS 去绕过 UE 语义缺口。

## 常见工作流模板

### 目录蓝图语义重建

适用：

```text
参考 /Game/Example/SourceFeature，在 /Game/Example/TargetFeature 中用创建方式复刻蓝图。
参考 /Game/Example/SourceAbility，在 /Game/Example/TargetAbility 中用创建方式复刻蓝图。
要求命名、引用关系、配置值一致，但不能 duplicate 蓝图。
```

本仓库没有固定 JS 入口。AI 必须按本模板生成本次 `workflow_plan.json`，再生成本次临时执行胶水或直接提交 JSON task。任务完成后删除临时脚本，只保留 plan、meta、report。

执行规则：

```text
1. 先刷新 source/target meta，确认 schema_version、generated_at_utc、task_id。meta 只用于生成候选计划，不作为写入和校验依据。
2. 生成本次 workflow plan，不提交任务。
3. 审计 plan：
   - create_blueprint 数量
   - add_component 数量
   - own_scs_template / scs_inherited_override / native_template 数量
   - CDO 写入数量
   - residual source ref 风险
4. 只有 plan 合理，才提交阶段任务。
5. Apply 阶段涉及从源蓝图复制配置时，优先使用 `copy_blueprint_live_overrides`，让 C++ 自己从 source live Blueprint 发现 CDO/native component/inherited SCS/owned SCS 的可写 override 并复制。只有在明确需要少数字段定点修复时，才使用 `copy_live_blueprint_values`。
6. Apply 后必须对目标蓝图执行 Blueprint compile + save；蓝图编译失败是 blocking，不能跳过。
7. Apply 后必须通过 C++ live 读取接口验证真实资产内容；验证通过后再 refresh meta。
8. Verify 必须跑 semantic diff 和 residual source ref scan。
```

关键禁止项：

```text
不要复用历史 Q/CopyQ 编排脚本；每个任务都从模板和当前 meta 生成本次 workflow。
不要只看 diff 数字，必须检查 Copy 目标中是否残留 source root 路径。
不要把 target 侧的 /Q/ 路径归一化成 /CopyQ/ 后再比较；target 残留 source ref 是 blocking。
不要把父蓝图 SCS component 当 own_scs_template 写；必须写 scs_inherited_override。
```

### 参考已有蓝图实现类似功能

适用：

```text
参考 A 技能/蓝图/资产链，实现 B。
```

流程：

```text
Observe:
  list reference assets
  snapshot reference blueprints/non-BP
  snapshot C++ parent class
  export reference graph

Plan:
  识别资产角色链路
  生成 create/duplicate/component/CDO/redirect plan

Apply:
  创建/复制资产
  添加 own SCS
  更新 native / inherited / own component
  更新 CDO / DataAsset
  重定向引用

Verify:
  snapshot target
  semantic diff
  residual source ref scan
```

### 参考文档实现蓝图配置

适用：

```text
根据策划文档、表格、文本规则配置蓝图或 DataAsset。
```

流程：

```text
Observe:
  解析文档
  查相似资产
  snapshot 目标父类反射字段

Plan:
  文档字段 -> UE 属性映射
  枚举 / GameplayTag / Curve / DataAsset 引用解析
  记录 assumptions

Apply:
  创建/更新资产
  写属性
  绑定引用

Verify:
  输出需求覆盖率
  输出未解析项
  检查 invalid refs / enum mismatch / missing tags
```

### 蓝图功能分析报告

适用：

```text
分析蓝图功能、C++ 逻辑链路、性能、安全、合理性。
```

开始要求：

```text
分析任务开始时必须先确认分析模式，不能自行默认模式。

可选模式：
  analysis:
    分析模式。允许参考 meta/listing/result 原始产物作为线索和证据，
    重点输出功能职责、主要逻辑链路、关键非默认值、核心风险和验证点。
    可按 quick / standard / deep 控制篇幅，但必须声明证据边界。
  audit:
    审计模式。meta 只能用于列资产、生成候选清单和定位历史 result，
    不能作为参数真相或结论来源。必须通过 UEEditorAutomation live
    只读任务重新反射读取目标范围内所有蓝图的 CDO、所有组件模板、
    所有组件参数、引用链、graph summary/pins，并分析每个蓝图自身
    C++ 父类和每个组件 C++ 类的实现风险。

如果用户已经明确要求“审计”“全面审计”“逐资产逐字段”“暴露潜在问题”
等强语义，必须使用 audit。其它功能说明、机制理解、初步排查默认使用
analysis。报告开头必须写明采用的模式、原因和证据边界。

默认禁止读取旧分析报告、历史总结、旧 Markdown 报告来作为本次分析依据，
避免复述旧结论。除非用户明确要求“继续上次报告”“基于旧报告补充”
或指定某份报告路径，否则 Observe 阶段只能读取 live UE 只读结果、
meta/listing/result 原始产物、资产文件、C++ 源码、配置表和数据表。
如果确实读取了旧报告，必须在报告开头声明旧报告路径、使用原因，以及
哪些结论已用 live/meta/C++ 证据重新验证。
```

流程：

```text
Observe:
  do not read old analysis reports unless explicitly requested by user
  analysis mode:
    可读取现有 meta/listing/result 原始产物、资产文件、C++ 源码、
    配置表和数据表。
    必要时补充 live 只读 analyze_blueprint / analyze_asset。

  audit mode:
    必须重新提交 live 只读 Observe 任务，不允许只依赖现有 meta。
    必须覆盖目标目录下所有蓝图；若包含非蓝图资产，至少列出并分析
    与蓝图引用链直接相关的 DataAsset / Curve / DataTable / GE-like 资产。
    对每个蓝图导出：
      CDO 全量可反射参数，包括默认值和 inherited/default 对比信息
      native / inherited / own component 列表
      每个 component template 的全量可反射参数
      graph summary；audit 需要定位执行顺序时导出 graph pins/edges
      direct dependencies / referencers / referenced blueprints
      native parent C++ context
    对每个组件类导出或定位 native C++ context。
    若某个资产 live 读取失败，必须记录为 Blocking 或 Coverage Gap。

Analyze:
  对每个目标蓝图建立结构化条目：
    asset path
    parent class
    native C++ header/cpp path
    parent C++ core responsibility and key execution functions
    blueprint role in runtime chain
    own / inherited / native components
    non-default CDO fields
    non-default component fields
    direct dependencies and referencers

  必须分析蓝图父类 C++：
    提炼父类核心功能、生命周期入口、执行端(authority/client)、
    Tick/Timer/Async/Latent 行为、复制/预测相关字段、关键 virtual/
    BlueprintNativeEvent/BlueprintImplementableEvent。

  必须审计 C++ 实现风险，而不只描述用途：
    读取关键 .h/.cpp 实现，定位蓝图配置会实际调用到的函数链。
    分析函数内的前置条件、空指针/弱指针检查、Authority 判断、状态机分支、
    失败路径、资源释放、事件解绑、对象生命周期、缓存失效、容器遍历、
    runtime load / async load、Tick / Timer 注册与注销。
    判断 C++ 默认行为与当前蓝图非默认值组合后是否可能产生边界问题。
    若父类依赖子类配置，必须指出哪些字段是硬前提，当前蓝图是否满足。
    若只读证据不足以确认，必须输出“需要 live/运行时验证”的具体断点。

  审计模式额外要求：
    不能只分析“非默认值”。必须遍历所有 live 反射导出的 CDO 字段和
    component 字段，并按以下分类输出：
      explicit_override: 当前蓝图显式覆盖且影响运行时
      inherited_default: 继承默认，当前蓝图未改但语义关键
      noise/editor_only: 编辑器展示、缓存、瞬态或无运行时意义
      unresolved: 反射失败、截断、无法解释或需要运行时确认
    对所有 explicit_override 必须解释设计含义、C++ 消费位置、潜在风险。
    对关键 inherited_default 必须说明为什么默认值是安全的或存在隐性风险。
    对 unresolved 必须给出后续 live/人工验证点。

    必须分析每个 own/inherited/native component 的 C++ 类：
      构造默认值和 BeginPlay/Initialize/OnRegister/EndPlay/Tick 等生命周期
      replication、collision、movement、visibility、attachment、event binding
      该组件参数如何被 C++ 使用，是否与蓝图配置匹配
      是否存在 tick 过重、重复扫描、未解绑事件、空引用、生命周期错序风险

  必须分析蓝图 add / own components：
    组件名、组件类、组件来源(own_scs / inherited / native)、attach 关系、
    collision / movement / replication / tick / visual / ability 相关配置、
    组件参与的运行时链路和潜在风险。

  必须分析非默认值：
    数值：伤害、半径、持续时间、冷却、生命周期、延迟、TickInterval。
    引用：Ability、GameplayEffect、Weapon、Summon、Projectile、Detector、
      Filter、GameplayCue、DataAsset、Curve、Mesh、Effect。
    GameplayTag：触发、阻塞、拥有、免疫、状态切换、事件。
    网络：Authority、LocallyControlled、Simulated、Replicates、
      ReplicateInput、NetExecutionPolicy。
    生命周期：Spawn/Destroy、CommitCost、ApplyCooldown、EndAbility、
      Cancel、RemoveEffect。
    对每个关键非默认值说明“这个值为什么重要 / 可能意味着什么 / 风险是什么”。

  入口事件和状态流
  Tick/Timer/Async/Latent
  网络同步和 authority/client
  GameplayEffect/Attribute/Tag
  资产依赖
  空引用/循环引用/运行时加载风险

  必须评价当前设计逻辑链路是否合理：
    输入/触发条件 -> Check/PreCheck -> Activator/Giver -> Weapon/Actor/Component
    -> StateAtom/Detector -> GE/Tag/Attribute -> End/Cancel/Destroy/Cleanup。
    判断链路是否闭环、是否存在重复职责、是否过度依赖隐式 GameplayTag、
    是否把运行时状态藏在难以追踪的事件里、是否存在配置与 C++ 语义错配、
    是否可以用更集中/更数据化/更低频/更少跨资产引用的方式实现。

  必须输出优化空间：
    性能优化：减少 Tick/Timer/Detector 扫描、缩小半径、缓存目标、
      降低运行时加载、复用对象池、减少不必要的客户端/模拟端执行。
    架构优化：减少跨角色引用、统一 Tag 命名和生命周期事件、抽取共享 Atom、
      明确 Cost/CD/EndAbility 时序、把隐式状态改成显式状态机或数据表。
    可维护性优化：降低蓝图引用链深度、减少重复 GE/Detector 配置、
      为关键字段增加命名/注释/验证规则、补充自动化检查。

  强制风险扫描：
    生命周期闭环是否完整
    Cost/CD/EndAbility 时序是否可能错误
    Client/Server 执行端是否一致
    Tick/Timer/Detector 是否可能过密
    GameplayTag 口径是否统一
    是否存在跨角色/旧资产引用残留
    GE 免疫、死亡、移除条件是否冲突
    控制权、输入、武器切换是否可能残留状态

Report:
  报告必须标注分析模式和证据等级：
    strong: live/meta 字段、C++ 源码、AssetRegistry 依赖
    medium: 项目框架约定、父类职责推导、引用链推导
    weak: 仅资产名、目录名、命名约定推断

  audit mode report must include:
    覆盖率：目标蓝图数、成功 live 反射数、失败数、跳过资产及原因
    每蓝图 C++ 父类实现审计
    每蓝图组件清单与组件 C++ 类实现审计
    每蓝图 CDO 字段分类统计和关键字段解释
    每组件参数分类统计和关键字段解释
    逻辑链路合理性评价
    风险分级：Blocking / High / Medium / Low / Noise
    优化建议：性能、网络同步、生命周期、架构、可维护性
    需要人工或运行时验证的断点

  findings by severity
  evidence paths
  C++ 实现风险与可优化点
  当前设计链路合理性评价
  替代设计或局部优化建议，并说明收益和代价
  修复建议或验证点
```

### 蓝图 bug 查找

适用：

```text
根据 bug 描述、复现步骤、可疑资产定位原因。
```

流程：

```text
Observe:
  搜索相关蓝图、C++ symbol、tag、event、asset refs
  snapshot suspect chain

Reason:
  建立假设：
    输入条件 -> 蓝图事件 -> C++ 调用 -> 状态变化 -> 结果
  找断点：
    配置缺失
    引用错误
    分支条件不满足
    网络端执行不一致
    生命周期顺序问题
    inherited override 未生效

Verify:
  为每个假设给证据
  排序原因概率
  输出最小修复 plan 或人工验证点
```

## Plan 格式建议

复杂任务必须生成可审计 plan。建议结构：

```json
{
  "workflow_id": "example_001",
  "template_id": "asset.semantic_reconstruct",
  "goal": "Replicate /Game/.../X to /Game/.../CopyX",
  "inputs": {
    "source_root": "/Game/...",
    "target_root": "/Game/..."
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
          "id": "C2-001",
          "action": "modify_blueprint_components",
          "target_asset": "/Game/.../BP.BP",
          "component_name": "SomeComponent",
          "target_kind": "scs_inherited_override",
          "reason": "source differs from direct parent",
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

## 属性策略

默认可写：

```text
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

特殊类型：

```text
FGameplayAttribute:
  AttributeOwner 可由 Attribute 推导，diff 可归一化

BodyInstance:
  用 UPrimitiveComponent setter 写关键碰撞字段
  注意 profile / object type / response 的写入顺序

ActorGroupInstance:
  项目 struct 可走反射递归字段写入

Delegate:
  skip，不写，不参与语义 diff
```

## 新会话执行 Checklist

开始任何复杂任务前：

```text
1. 读本文
2. 读用户目标
3. 判断适用模板
4. 先 Observe，不修改
5. 产出 plan
6. 如用户要求执行，再 Apply
7. Verify 必须重新 snapshot/diff
8. 如果本次使用 UEEditorAutomation 执行蓝图/资产自动化任务，任务完成后执行 Archive：输出 Saved/TaskReports 下的流程报告和同名 zip，清理本次临时 JSON/JS
9. 需要 C++ 编译时，请用户人工编译；蓝图修改后的 Blueprint compile + save 由自动化任务执行
```

若用户明确要求“开始执行”，可以直接进入 Observe -> plan -> Apply -> Verify，但仍要遵守不编译约束。

## 常用本地路径

```text
任务 inbox:
  C:/UEAutomation/tasks/inbox

任务结果:
  C:/UEAutomation/results

任务日志:
  C:/UEAutomation/logs

meta cache:
  Saved/BlueprintMetaCache

任务流程报告与归档:
  Saved/TaskReports

设计文档:
  Docs/Automation_Workflow_Design.md

接口文档:
  Docs/Task_Interface.md

当前实现真相:
  Docs/Current_Truth.md
```
