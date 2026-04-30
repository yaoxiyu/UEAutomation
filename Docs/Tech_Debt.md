# 工程技术债

已知的限制、bug 和待重构点。债被偿还或新债被识别时更新本文件。
不要在这里写按版本组织的发布说明。

## 严重度图例

- **HIGH** —— 静默数据丢失、正确性问题或阻塞核心流程
- **MED** —— 有 workaround 但能力受损
- **LOW** —— 清理、打磨、易用性

---

## 属性写入器 (`PropertyAssignmentService`)

### TD-PA-1 [PARTIAL] 项目自定义 struct 类型覆盖仍需项目样本验证

递归的 `JsonValueToImportTextForProperty` 已经覆盖通用容器/struct，
并已加入下列特化：

- `FGameplayAttribute`
- `FGameplayTag`
- `FGameplayTagContainer`
- `FDataTableRowHandle`
- `FScalableFloat`

剩余风险：`FAttributeBasedFloat` 以及项目自定义 GameplayEffect 复合字段
还缺少真实资产样本回归。写入阶段现在会检查 `ImportText` 是否完整消费，
并对写入后的属性做 `ExportText -> ImportText -> Identical` 往返校验。

修复方向：继续把项目样本中失败的 struct 纳入类型特化注册表。

### TD-PA-2 [DONE，待编译验证] struct 内的 `FName` 空值被拒

struct 内字段为空 `FName` 时被当成无效 import text。应映射到字面量
`None`，`FDataTableRowHandle.RowName` 通过专用构造器写为 `"None"`。

### TD-PA-3 [DONE，待编译验证] 没有 round-trip 校验

ImportText 写入后没有读回校验。复杂 struct 内部静默部分写入是可能的。

---

## 组件查找 (`UEBlueprintEditorAdapter::GetComponentTemplate`)

### TD-CL-1 [DONE，待编译验证] `modify_blueprint_components` 中的 native 声明组件

`modify_blueprint_components` 按名字操作。当前实现同时遍历 SCS 节点和
父类的 native UPROPERTY 组件，但当 native 组件被同名 SCS 节点 shadow
时，SCS 版本无条件优先。多数场景这是想要的行为，但应该显式化并可配置。

已增加 `component_lookup_policy`：`scs_first`、`native_first`、
`scs_only`、`native_only`。

---

## 引用重写 (`AssetDuplicationService::RedirectAssetReferences`)

### TD-RR-1 [DONE，待编译验证] create-then-modify 流程下 CDO sub-object 引用残留

当一个蓝图通过 `create_blueprint` 从空骨架创建（而不是
`duplicate_asset`），它的 CDO sub-object 是被 native 父类 ctor 新建
的，**不是** 源 CDO sub-object 的字节副本。
`FArchiveReplaceObjectRef` 要求 from/to 两端都是已加载的 UObject；
create 流程下没有"Q 的 sub-object 已加载且配对到 CopyQ 的替换体"
这种关系，所以那些以路径字符串形式存储 sub-object 引用（例如
`Default__X_C:Component`）的字段不能被重绑。

已增加 CDO 前缀路径重写：当属性值路径匹配源 CDO 前缀时，直接替换为
目标 CDO 前缀，再在已加载对象中查找目标 sub-object。

### TD-RR-2 [DONE，待编译验证] `replaced=0` 语义不清

当前 `replaced=0` 既可能表示"archive 没找到要替换的引用"，也可能
表示"redirect map 是空的"。需要看 resolved/unresolved counter 才能
区分。应输出独立的状态字符串。

已输出 `status=replaced | resolved_but_no_match | no_resolved_redirects`，
并记录 `replacement_map`、`cdo_prefixes`、`replaced`。

---

## Phase 4 分析 (`BlueprintAnalysisService` / `PropertySnapshotService`)

### TD-A-1 [DONE，待编译验证] 属性导出深度默认值

`MaxPropertyExportDepth` 默认 8。某些 `FGameplayTagContainer` 嵌套
仍超过这个值。被截断的值会写成
`{ "truncated": true, "reason": "max_property_depth" }`，写入器会
正确拒绝；但在 Q -> CopyQ 重导时它们表现为静默缺失。要么调高上限，
要么对已知深结构做特化、不截断。

已对 `FGameplayTag`、`FGameplayTagContainer`、`FDataTableRowHandle`、
`FScalableFloat` 增加导出特化，避免这些已知深结构因通用递归深度被截断。

### TD-A-2 [DONE，待编译验证] 缺少非蓝图资产的分析

`analyze_blueprint` 对非 `UBlueprint` 资产返回
`AnalysisAssetNotBlueprint`。没有针对任意 `UObject` 资产顶层 UPROPERTY
导出的 `analyze_asset`。对 Q -> CopyQ 这种依赖 `duplicate_asset` 的
流程不阻塞，但有了通用 `analyze_asset` 后 CI 就能对非 BP DataAsset
做断言。

已增加 `analyze_asset`，非蓝图资产写出 `asset_meta` artifact；蓝图目标
转到现有 `analyze_blueprint`。

### TD-A-3 [DONE] 引用图 BFS 仅在蓝图节点扩展

`AssetReferenceGraphService` 只展开蓝图节点。DataAsset / GE / Curve
依赖被记录为叶节点。这是有意设计、有界限，但需要文档化。

---

## Daemon 与传输

### TD-T-1 [LOW] 没有 HTTP 传输

只实现了文件轮询和本地 socket。HTTP 传输被推迟，需要远程编排时再加。

### TD-T-2 [DONE，待编译验证] stale `working` 恢复是 single-shot

启动时把 working 任务移到 `failed` + `RecoveredStaleWorkingTask`。
没有"按 budget 重试"的选项给那些执行中崩溃的任务。

已增加 `MaxStartupStaleWorkingTaskRetries`。默认 0 保持 fail-fast；大于 0
时启动恢复会按 `_retryN` 文件名计数把 stale working 任务回投 inbox。

---

## 白名单与配置

### TD-CFG-1 [DONE，待编译验证] 空数组语义是隐式的

`UEEditorAutomationWhitelist.json` 中字段为空数组表示"不限制"。
有文档但没 JSON schema 校验保证。可加一个显式 `mode` 字段
（`"strict" | "open"`），或对格式异常的列表 fail-fast。

已增加 `policy_mode`，并对 allow-list 字段做数组/字符串校验；
`strict` 模式要求 `allowed_task_types` 和 `allowed_asset_roots` 非空。

---

## Q -> CopyQ 编排（历史临时脚本）

### TD-O-1 [HIGH] 编排器跳过已知会失败的类型

历史 Node 编排器会丢弃它预期写入器会拒绝的字段。写入器扩展之后这
层过滤已经放宽，但仍会跳过任何 Phase 4 导出含 `truncated` 标记的
字段。没有第二轮回退（先用更深 depth `force_refresh` 重导后再写）。

注意：`Saved/*.js` 不再作为可复用入口。后续任务必须由 AI 按当前
workflow template 和当前 meta 生成本次 plan/临时胶水，执行后删除。

### TD-O-2 [MED] 非蓝图比对用了 uasset MD5

diff 工具把所有 35/35 非 BP 文件标为不同，因为
`IAssetTools::DuplicateAsset` 会重写 `PackageGuid` 和内部 export 路径。
这是预期的，不是真差异。diff 工具应该对非 BP 用反序列化数据比对，
而不是字节哈希。

---

## 测试覆盖

### TD-TC-1 [DONE] 仓库里没有自动化回归

今天的校验靠手工：跑样例任务、肉眼看 result。验收清单是 Markdown。
一个小的 `automation_test.py` 在编辑器跑着的时候投递 `Samples/valid/`
全集并断言 result JSON，不需要完整 UE Automation Framework 也能
拦住协议回归。

已新增 `Scripts/automation_test.py`。

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
