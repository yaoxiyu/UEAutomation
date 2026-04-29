# 项目当前真相

`UEEditorAutomation` 真实可用、已验证的状态。行为变化时更新本文件。
不要在这里写按版本组织的发布说明 / 验收清单。

## 工作模式

```
外部 AI / CI / 脚本
    -> 磁盘上的 JSON 任务，或本地 socket
    -> UE Editor 插件在运行中的编辑器内执行
    -> 结构化 *.result.json + 单任务 log
```

插件是 editor-only，加载阶段为 `PostEngineInit`。所有 UE 编译、重启、
编辑器启动都由用户手动完成。AI 代理绝不能调用 UBT、UAT、生成项目文件
或启动编辑器。

## 运行时目录

```
C:/UEAutomation/
  tasks/
    inbox/    待执行任务
    working/  执行中（启动时会被恢复）
    done/     成功
    failed/   失败（含结构化错误）
  results/    *.result.json
  logs/       *.log
```

插件本地缓存：

```
<PluginDir>/Saved/BlueprintMetaCache/    Phase 4 meta、目录列表、AI context
```

配置：

```
Config/UEEditorAutomationWhitelist.json    运行时白/黑名单
Config/UEEditorAutomationTemplates.json    蓝图模板
```

## 已实现能力

### 任务传输

- 文件轮询 daemon (`inbox -> working -> done/failed`)
- 启动时回收 stale `working` 任务，标记为 `RecoveredStaleWorkingTask`
 ；如设置 `MaxStartupStaleWorkingTaskRetries > 0`，会先按预算回投 inbox
- 可选本地 socket server，监听 `127.0.0.1:18777`
  （由 `bEnableUEAutomationSocketServer` 开关控制）
- 每任务独立的 `*.result.json` 与 `*.log`
- result/log 采用临时文件 + rename 的原子写路径；done/failed 任务归档
  遇到同名文件会追加 UTC 后缀，不覆盖历史任务文件

### 蓝图创建与修改

- `create_blueprint` —— 父类 + SCS 根/子组件 + attach 校验、transform、
  组件模板属性、CDO 默认值
- `create_blueprint_from_template` —— 模板注册表展开（父类、组件、
  默认值），支持 per-task 覆盖
- `batch_create_blueprints` —— 共享模板或 per-item 模板，重复目标检测，
  在创建任何一项之前先做存在性检查
- `modify_blueprint_components` —— 支持 `add_component` 和
  `update_component_properties`
- `modify_blueprint_defaults` —— 通过 ImportText 写入 CDO 属性

组件模板查找会同时遍历 SCS 节点和父类的 native UPROPERTY 组件字段。
`update_component_properties` 支持 `component_lookup_policy`，可在
`scs_first`、`native_first`、`scs_only`、`native_only` 间选择。

### 属性赋值

- 标量：bool、整型族、浮点族、name、str、text、enum
- 对象/类硬引用与软引用（null 写为 `None`）
- 容器递归：array、set、map（含 array of struct、map of struct）
- 结构体递归：struct 字段本身可以是容器、enum、子 struct、对象引用
- 特化路径：`StaticMesh`、`SkeletalMesh`、`CollisionProfileName`
- Phase 4 truncated 标记会在写入阶段被检测并拒绝
- 写入器会检查 `ImportText` 完整消费输入，并对写入后的属性做
  `ExportText -> ImportText -> Identical` 往返校验

### 非蓝图资产创建

- `create_data_asset`
- `create_material_instance`、`modify_material_instance`
- `create_blueprint_class`、`create_widget_blueprint`
- `create_data_table`、`create_curve_float`、`create_curve_vector`
- `create_animation_blueprint`、`create_blend_space`
- `create_level_sequence`、`create_physics_asset`
- `create_material_function`
- `create_gameplay_ability`、`create_gameplay_effect`
- `import_texture`、`import_sound_wave`
- `modify_asset_properties` —— 反射式顶层 UPROPERTY 写入
- `check_asset_rules`、`generate_audit_report`

PhysicsAsset 走非交互的 `FPhysicsAssetUtils::CreateFromSkeletalMesh`
路径，传入 `bSetToMesh=false`（不会回写到源 SkeletalMesh）。

`import_sound_wave` 在本工程 fork 的 UE 中，当 `Result` 为空时回退使用
`UAssetImportTask::ImportedObjectPaths`。

### Phase 4 只读蓝图分析

- `analyze_blueprint` —— native 父类链、父类 C++ 源码定位 + combined
  MD5、蓝图快照（CDO 默认值、SCS 组件含 native 组件、用户变量）、
  正向引用与反向引用、可选只读图节点摘要或完整 pin/edge
- `analyze_blueprint_reference_chain` —— 受限 BFS、为每个节点生成 meta、
  写出 `<asset>.graph.json`
- `refresh_blueprint_meta_cache` —— 强制刷新
- `export_blueprint_ai_context` —— 紧凑的 `<asset>.context.json`
- `analyze_asset` —— 任意非蓝图 `UObject` 顶层 UPROPERTY 导出；
  蓝图目标会走 `analyze_blueprint`

缓存状态：

```
hit
miss_no_cache
miss_schema_changed
miss_source_changed
partial_hit_source_same_asset_changed
miss_options_changed
forced_refresh
miss_source_unresolved
```

如果 `<PluginDir>/Saved/BlueprintMetaCache/` 不可写，插件会回退到
`<ProjectSaved>/UEAutomation/BlueprintMetaCache/`，并附 warning
`CacheRootFallback`。

### 资产复制与引用重写

- `duplicate_asset` —— 走 `IAssetTools::DuplicateAsset`，字节级复制
  （含蓝图图节点、GE/GA 逻辑、Curve 关键帧、DataTable 行）。源资产
  会先 `Package->FullyLoad()`，避免 `SavePackage` 因 partial-load 拒绝。
- `redirect_asset_references` —— `FArchiveReplaceObjectRef<UObject>`
  扫描目标资产、`GeneratedClass`、CDO。蓝图目标还会遍历 Q-CDO /
  CopyQ-CDO 同名 `FObjectPropertyBase` 字段并加入 replacement map，
  让 native sub-object 引用能被重绑定；还会对匹配源 CDO 前缀的
  sub-object 路径引用做目标 CDO 前缀替换。完成后重新编译目标蓝图。
- `list_directory_assets` —— Asset Registry 目录枚举，artifact 写到
  `BlueprintMetaCache/DirectoryListings/`。

### 调试面板

- 全局 Nomad tab `UE Automation`
- Level Editor 菜单显式注入 `Window -> UE Automation`
- 列出最近的 result，可打开 result 与 log 文件

## Result 协议

```json
{
  "protocol_version": 1,
  "task_id": "...",
  "task_type": "...",
  "success": true,
  "status": "succeeded",
  "asset_outputs": [
    { "asset_path": "...", "asset_name": "...", "asset_type": "..." }
  ],
  "artifacts": [
    {
      "artifact_type": "blueprint_meta | reference_graph | ai_context | directory_listing",
      "path": "...",
      "asset_path": "...",
      "cache_status": "...",
      "parent_cpp_md5": "..."
    }
  ],
  "warnings": ["..."],
  "errors": [
    { "code": "...", "message": "...", "field": "payload.x" }
  ],
  "metrics": {
    "duration_ms": 0,
    "compile_duration_ms": 0,
    "save_duration_ms": 0,
    "component_create_count": 0,
    "property_assign_count": 0,
    "warning_count": 0,
    "error_count": 0,
    "analysis_duration_ms": 0,
    "source_resolve_duration_ms": 0,
    "reference_scan_duration_ms": 0,
    "cache_hit_count": 0,
    "cache_miss_count": 0,
    "analyzed_blueprint_count": 0,
    "exported_property_count": 0,
    "reference_node_count": 0,
    "reference_edge_count": 0,
    "reference_graph_truncated": false
  },
  "log_path": "C:/UEAutomation/logs/<task_id>.log"
}
```

## 源码位置

```
Public/Protocol/AutomationProtocolTypes.h            JSON 协议类型
Public/Application/EditorAutomationApplicationService.h  任务生命周期
Public/Application/TaskExecutor.h                    Executor 接口
Public/Application/BlueprintTaskExecutors.h          蓝图相关 executor
Public/Application/AssetTaskExecutors.h              资产创建 executor
Public/Application/BlueprintAnalysisTaskExecutors.h  Phase 4 executor
Public/Application/AssetDuplicationTaskExecutors.h   duplicate / redirect / list
Public/Domain/BlueprintAutomationService.h           蓝图创建/修改
Public/Domain/BlueprintTemplateRegistry.h            模板注册表
Public/Domain/AssetAutomationService.h               typed asset 创建
Public/Domain/PropertyAssignmentService.h            ImportText 写入
Public/Domain/BlueprintAnalysisService.h             Phase 4 总编排
Public/Domain/BlueprintMetaCacheService.h            meta 路径与缓存判定
Public/Domain/NativeParentClassResolver.h            蓝图父类链
Public/Domain/CppSourceResolver.h                    .h/.cpp 定位
Public/Domain/ClassReflectionExporter.h              UPROPERTY/UFUNCTION 导出
Public/Domain/PropertySnapshotService.h              CDO/组件值导出
Public/Domain/BlueprintSnapshotExporter.h            蓝图快照导出
Public/Domain/BlueprintGraphReadOnlyExporter.h       只读图节点导出
Public/Domain/AssetReferenceGraphService.h           依赖图
Public/Domain/BlueprintAISummaryBuilder.h            规则化 ai_summary
Public/Domain/AssetDuplicationService.h              duplicate / redirect / list
Public/Adapter/UEBlueprintEditorAdapter.h            隔离 UE API 差异
Public/Core/EditorAutomationSettings.h               UDeveloperSettings
Public/Core/AutomationWhitelist.h                    运行时白名单
Public/Core/FileFingerprint.h                        文件 MD5/mtime
Public/Core/StableJsonWriter.h                       原子 pretty-print
Public/UI/AutomationDebugPanel.h                     编辑器 UI
Private/...                                          实现
Private/UEEditorAutomationModule.cpp                 模块 + executor 注册
Private/Transport/FileTaskTransport.cpp              文件 daemon
Private/Transport/SocketTaskServer.cpp               本地 socket
```
