# 工程技术债

已知的限制、bug 和待重构点。债被偿还或新债被识别时更新本文件。
不要在这里写按版本组织的发布说明。

## 严重度图例

- **HIGH** —— 静默数据丢失、正确性问题或阻塞核心流程
- **MED** —— 有 workaround 但能力受损
- **LOW** —— 清理、打磨、易用性

---

## 属性写入器 (`PropertyAssignmentService`)

### TD-PA-1 [HIGH] 项目自定义 struct 类型未覆盖

递归的 `JsonValueToImportTextForProperty` 已经覆盖通用容器/struct，
但遇到内部字段非平凡的项目自定义 struct 仍会失败：

- `FGameplayAttribute`（`Attribute` 字段是 `FFieldPath`，序列化为
  `/Script/Module.Class:Field` 字符串，但没有 `FFieldPathProperty`
  的 import 路径）
- `FGameplayTagContainer`（深度结构，会超过默认
  `MaxPropertyExportDepth`）
- `FDataTableRowHandle`（`RowName` 是 `FName`，空字符串会被 ImportText
  拒绝，没有 `NAME_None` 回退）
- `FScalableFloat` / `FAttributeBasedFloat`（复合，含
  `FCurveTableRowHandle`）

症状：`Property 'X' does not support array/struct JSON conversion.`
或 struct 内空字符串 `FName` 被拒。

修复方向：建一个类型特化注册表，把已知项目 struct 名映射到专用
import-text 构造器。

### TD-PA-2 [MED] struct 内的 `FName` 空值被拒

struct 内字段为空 `FName` 时被当成无效 import text。应映射到字面量
`NAME_None`。

### TD-PA-3 [LOW] 没有 round-trip 校验

ImportText 写入后没有读回校验。复杂 struct 内部静默部分写入是可能的。

---

## 组件查找 (`UEBlueprintEditorAdapter::GetComponentTemplate`)

### TD-CL-1 [MED] `modify_blueprint_components` 中的 native 声明组件

`modify_blueprint_components` 按名字操作。当前实现同时遍历 SCS 节点和
父类的 native UPROPERTY 组件，但当 native 组件被同名 SCS 节点 shadow
时，SCS 版本无条件优先。多数场景这是想要的行为，但应该显式化并可配置。

---

## 引用重写 (`AssetDuplicationService::RedirectAssetReferences`)

### TD-RR-1 [MED] create-then-modify 流程下 CDO sub-object 引用残留

当一个蓝图通过 `create_blueprint` 从空骨架创建（而不是
`duplicate_asset`），它的 CDO sub-object 是被 native 父类 ctor 新建
的，**不是** 源 CDO sub-object 的字节副本。
`FArchiveReplaceObjectRef` 要求 from/to 两端都是已加载的 UObject；
create 流程下没有"Q 的 sub-object 已加载且配对到 CopyQ 的替换体"
这种关系，所以那些以路径字符串形式存储 sub-object 引用（例如
`Default__X_C:Component`）的字段不能被重绑。

Workaround：父类暴露 sub-object UPROPERTY 引用的资产，优先用
`duplicate_asset`。

修复方向：检测 `FObjectPropertyBase` 的值，如果路径前缀匹配源资产
的 `Default__X_C:`，直接做字符串替换，不依赖 UObject 指针替换。

### TD-RR-2 [LOW] `replaced=0` 语义不清

当前 `replaced=0` 既可能表示"archive 没找到要替换的引用"，也可能
表示"redirect map 是空的"。需要看 resolved/unresolved counter 才能
区分。应输出独立的状态字符串。

---

## Phase 4 分析 (`BlueprintAnalysisService` / `PropertySnapshotService`)

### TD-A-1 [MED] 属性导出深度默认值

`MaxPropertyExportDepth` 默认 8。某些 `FGameplayTagContainer` 嵌套
仍超过这个值。被截断的值会写成
`{ "truncated": true, "reason": "max_property_depth" }`，写入器会
正确拒绝；但在 Q -> CopyQ 重导时它们表现为静默缺失。要么调高上限，
要么对已知深结构做特化、不截断。

### TD-A-2 [LOW] 缺少非蓝图资产的分析

`analyze_blueprint` 对非 `UBlueprint` 资产返回
`AnalysisAssetNotBlueprint`。没有针对任意 `UObject` 资产顶层 UPROPERTY
导出的 `analyze_asset`。对 Q -> CopyQ 这种依赖 `duplicate_asset` 的
流程不阻塞，但有了通用 `analyze_asset` 后 CI 就能对非 BP DataAsset
做断言。

### TD-A-3 [LOW] 引用图 BFS 仅在蓝图节点扩展

`AssetReferenceGraphService` 只展开蓝图节点。DataAsset / GE / Curve
依赖被记录为叶节点。这是有意设计、有界限，但需要文档化。

---

## Daemon 与传输

### TD-T-1 [LOW] 没有 HTTP 传输

只实现了文件轮询和本地 socket。HTTP 传输被推迟，需要远程编排时再加。

### TD-T-2 [LOW] stale `working` 恢复是 single-shot

启动时把 working 任务移到 `failed` + `RecoveredStaleWorkingTask`。
没有"按 budget 重试"的选项给那些执行中崩溃的任务。

---

## 白名单与配置

### TD-CFG-1 [LOW] 空数组语义是隐式的

`UEEditorAutomationWhitelist.json` 中字段为空数组表示"不限制"。
有文档但没 JSON schema 校验保证。可加一个显式 `mode` 字段
（`"strict" | "open"`），或对格式异常的列表 fail-fast。

---

## Q -> CopyQ 编排（树外脚本）

### TD-O-1 [HIGH] 编排器跳过已知会失败的类型

Node 编排器（`Saved/copyq_orchestrator.js`）会丢弃它预期写入器会拒
绝的字段。写入器扩展之后这层过滤已经放宽，但仍会跳过任何 Phase 4
导出含 `truncated` 标记的字段。没有第二轮回退（先用更深 depth
`force_refresh` 重导后再写）。

### TD-O-2 [MED] 非蓝图比对用了 uasset MD5

diff 工具把所有 35/35 非 BP 文件标为不同，因为
`IAssetTools::DuplicateAsset` 会重写 `PackageGuid` 和内部 export 路径。
这是预期的，不是真差异。diff 工具应该对非 BP 用反序列化数据比对，
而不是字节哈希。

---

## 测试覆盖

### TD-TC-1 [LOW] 仓库里没有自动化回归

今天的校验靠手工：跑样例任务、肉眼看 result。验收清单是 Markdown。
一个小的 `automation_test.py` 在编辑器跑着的时候投递 `Samples/valid/`
全集并断言 result JSON，不需要完整 UE Automation Framework 也能
拦住协议回归。

---

## 清理

### TD-DOC-1 [DONE，待合并] Phase 文档已收敛

被替换为 `Current_Truth.md`、`Task_Interface.md` 和本文件。下列文件
已删除：

- `Phase1_Regression_Checklist.md`
- `Phase2_Template_Checklist.md`
- `Phase3_Extension_Checklist.md`
- `Phase4_Acceptance_Checklist.md`
- `Phase4_Blueprint_Analysis_Design.md`
- `Current_Implementation_Status.md`
- `Q_vs_CopyQ_Diff_Report.md`
- `AI_Task_Generation_Workflow.md`
- `Task_Interface_Usage.md`（合并入 `Task_Interface.md`）
