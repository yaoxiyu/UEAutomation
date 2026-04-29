#include "Domain/BlueprintAnalysisService.h"

#if __has_include("AssetRegistry/AssetRegistryModule.h")
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#include "Core/AutomationLog.h"
#include "Core/AutomationWhitelist.h"
#include "Core/EditorAutomationSettings.h"
#include "Core/FileFingerprint.h"
#include "Core/StableJsonWriter.h"
#include "Domain/AssetReferenceGraphService.h"
#include "Domain/BlueprintAISummaryBuilder.h"
#include "Domain/BlueprintGraphReadOnlyExporter.h"
#include "Domain/BlueprintMetaCacheService.h"
#include "Domain/BlueprintSnapshotExporter.h"
#include "Domain/ClassReflectionExporter.h"
#include "Domain/CppSourceResolver.h"
#include "Domain/NativeParentClassResolver.h"
#include "Engine/Blueprint.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/PackageName.h"
#include "Misc/SecureHash.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    bool ValidateAnalysisOptions(
        const FAutomationAnalysisOptions& Options,
        const UEditorAutomationSettings* Settings,
        FAutomationTaskResult& OutResult)
    {
        if (!Settings)
        {
            return true;
        }
        if (Options.ReferenceDepth < 0 || Options.ReferenceDepth > Settings->MaxReferenceAnalysisDepth)
        {
            OutResult.AddError(TEXT("InvalidAnalysisOptions"),
                FString::Printf(TEXT("reference_depth=%d exceeds settings max %d"), Options.ReferenceDepth, Settings->MaxReferenceAnalysisDepth),
                TEXT("payload.analysis.reference_depth"));
            return false;
        }
        if (Options.MaxNodes <= 0 || Options.MaxNodes > Settings->MaxReferenceGraphNodes)
        {
            OutResult.AddError(TEXT("InvalidAnalysisOptions"),
                FString::Printf(TEXT("max_nodes=%d exceeds settings max %d"), Options.MaxNodes, Settings->MaxReferenceGraphNodes),
                TEXT("payload.analysis.max_nodes"));
            return false;
        }
        if (Options.MaxEdges <= 0 || Options.MaxEdges > Settings->MaxReferenceGraphEdges)
        {
            OutResult.AddError(TEXT("InvalidAnalysisOptions"),
                FString::Printf(TEXT("max_edges=%d exceeds settings max %d"), Options.MaxEdges, Settings->MaxReferenceGraphEdges),
                TEXT("payload.analysis.max_edges"));
            return false;
        }
        if (Options.MaxPropertyDepth <= 0 || Options.MaxPropertyDepth > Settings->MaxPropertyExportDepth)
        {
            OutResult.AddError(TEXT("InvalidAnalysisOptions"),
                FString::Printf(TEXT("max_property_depth=%d exceeds settings max %d"), Options.MaxPropertyDepth, Settings->MaxPropertyExportDepth),
                TEXT("payload.analysis.max_property_depth"));
            return false;
        }
        if (Options.MaxArrayElements <= 0 || Options.MaxArrayElements > Settings->MaxArrayExportElements)
        {
            OutResult.AddError(TEXT("InvalidAnalysisOptions"),
                FString::Printf(TEXT("max_array_elements=%d exceeds settings max %d"), Options.MaxArrayElements, Settings->MaxArrayExportElements),
                TEXT("payload.analysis.max_array_elements"));
            return false;
        }
        return true;
    }

    FString FindPackageFilePath(const FString& PackageName)
    {
        FString Filename;
        if (!FPackageName::DoesPackageExist(PackageName, nullptr, &Filename))
        {
            return FString();
        }
        return FPaths::ConvertRelativePathToFull(Filename);
    }

    bool ComputeAssetFingerprint(const FString& PackageFilePath, FAutomationFileFingerprint& OutFingerprint)
    {
        FString Error;
        return FAutomationFileFingerprintUtil::FingerprintFile(PackageFilePath, TEXT("uasset"), OutFingerprint, Error);
    }

    void AddArtifact(
        FAutomationTaskResult& OutResult,
        const FString& Type,
        const FString& Path,
        const FString& AssetPath,
        const FString& CacheStatus,
        const FString& ParentCppMd5)
    {
        FAutomationArtifactOutput Artifact;
        Artifact.ArtifactType = Type;
        Artifact.Path = Path;
        Artifact.AssetPath = AssetPath;
        Artifact.CacheStatus = CacheStatus;
        Artifact.ParentCppMd5 = ParentCppMd5;
        OutResult.Artifacts.Add(Artifact);
    }
}

bool FBlueprintAnalysisService::AnalyzeBlueprint(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    if (!ValidateAnalysisOptions(Request.Analysis, Settings, OutResult))
    {
        return false;
    }
    return AnalyzeSingleBlueprint(Request.TargetAsset.AssetPath, Request, OutResult);
}

bool FBlueprintAnalysisService::AnalyzeReferenceChain(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    if (!ValidateAnalysisOptions(Request.Analysis, Settings, OutResult))
    {
        return false;
    }
    if (!AnalyzeSingleBlueprint(Request.TargetAsset.AssetPath, Request, OutResult))
    {
        return false;
    }

    FAssetReferenceGraphService GraphService;
    const TSharedRef<FJsonObject> GraphJson = MakeShared<FJsonObject>();
    FAutomationReferenceGraphMetrics GraphMetrics;
    FBlueprintMetaCacheService Cache;
    bool bFallback = false;
    FString FallbackReason;
    const FString CacheRoot = Cache.ResolveCacheRoot(bFallback, FallbackReason);
    if (!GraphService.ExportReferenceGraph(Request.TargetAsset.AssetPath, Request.Analysis, CacheRoot, GraphJson, GraphMetrics, OutResult))
    {
        return false;
    }

    OutResult.Metrics.ReferenceNodeCount = GraphMetrics.NodeCount;
    OutResult.Metrics.ReferenceEdgeCount = GraphMetrics.EdgeCount;
    OutResult.Metrics.bReferenceGraphTruncated = GraphMetrics.bTruncated;

    FString GraphPath;
    FString GraphPathError;
    if (Cache.ResolveMetaPathForAsset(Request.TargetAsset.AssetPath, GraphPath, GraphPathError))
    {
        GraphPath.ReplaceInline(TEXT(".meta.json"), TEXT(".graph.json"));
        FString WriteError;
        if (FAutomationStableJsonWriter::WriteAtomic(GraphPath, GraphJson, WriteError))
        {
            AddArtifact(OutResult, TEXT("reference_graph"), GraphPath, Request.TargetAsset.AssetPath, FString(), FString());
        }
        else
        {
            OutResult.AddWarning(FString::Printf(TEXT("ReferenceGraphWriteFailed: %s"), *WriteError));
        }
    }

    // Recurse to populate per-node meta when references are blueprint assets.
    const TArray<TSharedPtr<FJsonValue>>* NodeArray = nullptr;
    if (GraphJson->TryGetArrayField(TEXT("nodes"), NodeArray))
    {
        for (const TSharedPtr<FJsonValue>& Node : *NodeArray)
        {
            const TSharedPtr<FJsonObject> NodeObj = Node->AsObject();
            if (!NodeObj.IsValid())
            {
                continue;
            }
            FString NodeAssetPath;
            FString NodeType;
            NodeObj->TryGetStringField(TEXT("asset_path"), NodeAssetPath);
            NodeObj->TryGetStringField(TEXT("node_type"), NodeType);
            if (NodeType != TEXT("blueprint") || NodeAssetPath.IsEmpty() || NodeAssetPath == Request.TargetAsset.AssetPath)
            {
                continue;
            }
            FAutomationTaskResult ChildResult;
            ChildResult.TaskId = OutResult.TaskId;
            ChildResult.TaskType = OutResult.TaskType;
            AnalyzeSingleBlueprint(NodeAssetPath, Request, ChildResult);
            for (const FAutomationArtifactOutput& Art : ChildResult.Artifacts)
            {
                OutResult.Artifacts.Add(Art);
            }
            for (const FString& Warn : ChildResult.Warnings)
            {
                OutResult.Warnings.Add(Warn);
            }
            OutResult.Metrics.AnalyzedBlueprintCount += ChildResult.Metrics.AnalyzedBlueprintCount;
            OutResult.Metrics.CacheHitCount += ChildResult.Metrics.CacheHitCount;
            OutResult.Metrics.CacheMissCount += ChildResult.Metrics.CacheMissCount;
        }
    }

    return true;
}

bool FBlueprintAnalysisService::RefreshMetaCache(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    if (!ValidateAnalysisOptions(Request.Analysis, Settings, OutResult))
    {
        return false;
    }

    FAutomationTaskRequest Forced = Request;
    Forced.Analysis.bForceRefresh = true;

    bool bAnyFailure = false;
    if (Request.TargetAssets.Num() == 0)
    {
        if (!AnalyzeSingleBlueprint(Forced.TargetAsset.AssetPath, Forced, OutResult))
        {
            bAnyFailure = true;
        }
    }
    else
    {
        for (const FAutomationAssetSpec& Asset : Request.TargetAssets)
        {
            if (!AnalyzeSingleBlueprint(Asset.AssetPath, Forced, OutResult))
            {
                bAnyFailure = true;
            }
        }
    }
    return !bAnyFailure;
}

bool FBlueprintAnalysisService::ExportAIContext(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (!AnalyzeBlueprint(Request, OutResult))
    {
        return false;
    }

    // Build a compact AI context derived from the freshly written meta of the
    // target asset. The full meta must already exist by this point.
    FBlueprintMetaCacheService Cache;
    FString MetaPath;
    FString PathError;
    if (!Cache.ResolveMetaPathForAsset(Request.TargetAsset.AssetPath, MetaPath, PathError))
    {
        OutResult.AddError(TEXT("AnalysisAssetNotFound"), PathError, TEXT("payload.target_asset.asset_path"));
        return false;
    }

    TSharedPtr<FJsonObject> Meta;
    FString ReadError;
    if (!Cache.TryReadMeta(MetaPath, Meta, ReadError) || !Meta.IsValid())
    {
        OutResult.AddError(TEXT("MetaCacheReadFailed"), ReadError);
        return false;
    }

    bool bFallback = false;
    FString FallbackReason;
    const FString CacheRoot = Cache.ResolveCacheRoot(bFallback, FallbackReason);
    FString ContextPath = MetaPath;
    ContextPath.ReplaceInline(TEXT(".meta.json"), TEXT(".context.json"));

    const TSharedRef<FJsonObject> Context = MakeShared<FJsonObject>();
    Context->SetStringField(TEXT("schema_version"), TEXT("ai-context-v1"));
    Context->SetStringField(TEXT("target_asset"), Request.TargetAsset.AssetPath);

    const TSharedPtr<FJsonObject>* ParentClass = nullptr;
    if (Meta->TryGetObjectField(TEXT("parent_class"), ParentClass) && ParentClass)
    {
        Context->SetObjectField(TEXT("parent_class"), *ParentClass);
    }

    const TSharedPtr<FJsonObject>* BlueprintSnapshot = nullptr;
    if (Meta->TryGetObjectField(TEXT("blueprint_snapshot"), BlueprintSnapshot) && BlueprintSnapshot)
    {
        const TArray<TSharedPtr<FJsonValue>>* Defaults = nullptr;
        if ((*BlueprintSnapshot)->TryGetArrayField(TEXT("class_defaults"), Defaults))
        {
            TArray<TSharedPtr<FJsonValue>> Filtered;
            for (const TSharedPtr<FJsonValue>& Item : *Defaults)
            {
                const TSharedPtr<FJsonObject> Obj = Item->AsObject();
                if (!Obj.IsValid())
                {
                    continue;
                }
                bool bEditable = false;
                Obj->TryGetBoolField(TEXT("editable"), bEditable);
                if (bEditable)
                {
                    Filtered.Add(Item);
                }
            }
            Context->SetArrayField(TEXT("editable_class_defaults"), Filtered);
        }
    }

    const TSharedPtr<FJsonObject>* References = nullptr;
    if (Meta->TryGetObjectField(TEXT("references"), References) && References)
    {
        const TArray<TSharedPtr<FJsonValue>>* RefBps = nullptr;
        if ((*References)->TryGetArrayField(TEXT("referenced_blueprints"), RefBps))
        {
            Context->SetArrayField(TEXT("referenced_blueprints"), *RefBps);
        }
    }

    Context->SetStringField(TEXT("suggested_task_types"),
        TEXT("modify_blueprint_defaults | modify_blueprint_components | create_blueprint | create_blueprint_from_template"));

    FString WriteError;
    if (!FAutomationStableJsonWriter::WriteAtomic(ContextPath, Context, WriteError))
    {
        OutResult.AddError(TEXT("MetaCacheWriteFailed"), WriteError);
        return false;
    }
    AddArtifact(OutResult, TEXT("ai_context"), ContextPath, Request.TargetAsset.AssetPath, FString(), FString());
    return true;
}

bool FBlueprintAnalysisService::AnalyzeSingleBlueprint(
    const FString& AssetPath,
    const FAutomationTaskRequest& Request,
    FAutomationTaskResult& OutResult)
{
    if (AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required"), TEXT("payload.target_asset.asset_path"));
        return false;
    }

    const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();

    const FDateTime AnalysisStart = FDateTime::UtcNow();
    OutResult.AddLog(FString::Printf(TEXT("Analyze: load blueprint %s"), *AssetPath));

    UObject* Asset = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath);
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
    if (!Blueprint)
    {
        // Distinguish "not found" from "wrong asset class".
        UObject* AnyAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
        if (AnyAsset)
        {
            OutResult.AddError(TEXT("AnalysisAssetNotBlueprint"),
                FString::Printf(TEXT("Asset is not a Blueprint: %s"), *AssetPath),
                TEXT("payload.target_asset.asset_path"));
        }
        else
        {
            OutResult.AddError(TEXT("AnalysisAssetNotFound"),
                FString::Printf(TEXT("Blueprint asset not found: %s"), *AssetPath),
                TEXT("payload.target_asset.asset_path"));
        }
        return false;
    }

    // Resolve native parent + source.
    OutResult.AddLog(TEXT("Analyze: resolve parent class"));
    FNativeParentClassResolver ParentResolver;
    FAutomationNativeParentInfo ParentInfo;
    FString ParentError;
    if (!ParentResolver.Resolve(Blueprint, ParentInfo, ParentError))
    {
        OutResult.AddError(TEXT("NativeParentClassNotFound"), ParentError, TEXT("payload.target_asset.asset_path"));
        return false;
    }

    OutResult.AddLog(TEXT("Analyze: resolve cpp source"));
    const FDateTime SourceStart = FDateTime::UtcNow();
    UClass* NativeClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ParentInfo.NativeParent.ClassPath);
    FCppSourceResolver SourceResolver;
    FAutomationCppSourceLocation Source;
    FString SourceError;
    const bool bSourceResolved = SourceResolver.Resolve(NativeClass, Source, SourceError);
    if (!bSourceResolved)
    {
        OutResult.AddWarning(FString::Printf(TEXT("SourceUnresolved: %s (%s)"), *ParentInfo.NativeParent.ClassPath, *SourceError));
    }
    OutResult.Metrics.SourceResolveDurationMs = (FDateTime::UtcNow() - SourceStart).GetTotalMilliseconds();

    // Compute parent C++ combined MD5.
    OutResult.AddLog(TEXT("Analyze: compute parent C++ MD5"));
    TArray<FAutomationFileFingerprint> ParentFiles;
    if (!Source.HeaderPath.IsEmpty())
    {
        FAutomationFileFingerprint Header;
        FString Error;
        if (FAutomationFileFingerprintUtil::FingerprintFile(Source.HeaderPath, TEXT("header"), Header, Error))
        {
            ParentFiles.Add(Header);
        }
        else
        {
            OutResult.AddWarning(FString::Printf(TEXT("CppMd5Failed (header): %s"), *Error));
        }
    }
    if (!Source.CppPath.IsEmpty())
    {
        FAutomationFileFingerprint Cpp;
        FString Error;
        if (FAutomationFileFingerprintUtil::FingerprintFile(Source.CppPath, TEXT("cpp"), Cpp, Error))
        {
            ParentFiles.Add(Cpp);
        }
        else
        {
            OutResult.AddWarning(FString::Printf(TEXT("CppMd5Failed (cpp): %s"), *Error));
        }
    }
    const FString CombinedMd5 = ParentFiles.Num() > 0
        ? FAutomationFileFingerprintUtil::BuildCombinedMd5(ParentInfo.NativeParent.ClassPath, ParentFiles)
        : FString();

    // Compute asset fingerprint.
    OutResult.AddLog(TEXT("Analyze: compute asset fingerprint"));
    const FString PackageName = FBlueprintMetaCacheService::NormalizeToPackageName(AssetPath);
    const FString PackageFilePath = FindPackageFilePath(PackageName);
    FAutomationFileFingerprint AssetFingerprint;
    if (!PackageFilePath.IsEmpty())
    {
        ComputeAssetFingerprint(PackageFilePath, AssetFingerprint);
    }

    // Resolve meta path.
    FBlueprintMetaCacheService Cache;
    FString MetaPath;
    FString MetaPathError;
    if (!Cache.ResolveMetaPathForAsset(AssetPath, MetaPath, MetaPathError))
    {
        OutResult.AddError(TEXT("MetaCacheWriteFailed"), MetaPathError);
        return false;
    }

    bool bFallbackCacheRoot = false;
    FString FallbackReason;
    const FString CacheRoot = Cache.ResolveCacheRoot(bFallbackCacheRoot, FallbackReason);
    if (bFallbackCacheRoot)
    {
        OutResult.AddWarning(FString::Printf(TEXT("CacheRootFallback: %s"), *FallbackReason));
    }

    // Read old meta and decide cache status.
    OutResult.AddLog(TEXT("Analyze: cache decision"));
    TSharedPtr<FJsonObject> OldMeta;
    FString OldMetaError;
    bool bMetaPresent = Cache.TryReadMeta(MetaPath, OldMeta, OldMetaError);
    if (!OldMetaError.IsEmpty())
    {
        OutResult.AddWarning(FString::Printf(TEXT("MetaCacheReadFailed: %s"), *OldMetaError));
        bMetaPresent = false;
    }

    FAutomationCacheDecisionInput Decision;
    Decision.bForceRefresh = Request.Analysis.bForceRefresh || !Request.Analysis.bUseCache;
    Decision.bSchemaCompatible = Cache.IsSchemaCompatible(OldMeta);
    Decision.bMetaPresent = bMetaPresent;
    Decision.bSourceResolved = bSourceResolved;
    Decision.CurrentNativeParentClassPath = ParentInfo.NativeParent.ClassPath;
    Decision.CurrentParentCppCombinedMd5 = CombinedMd5;
    Decision.CurrentAssetPackageMd5 = AssetFingerprint.Md5;
    Decision.CurrentAnalysisOptionsDigest = FBlueprintMetaCacheService::ComputeOptionsDigest(Request.Analysis);

    const FString CacheStatus = Cache.DecideCacheStatus(OldMeta, Decision);
    OutResult.AddLog(FString::Printf(TEXT("Analyze: cache_status=%s"), *CacheStatus));

    if (CacheStatus == UEAutomation::MetaCache::StatusHit && Request.Analysis.bUseCache)
    {
        OutResult.Metrics.CacheHitCount++;
        OutResult.Metrics.AnalyzedBlueprintCount++;
        OutResult.Metrics.AnalysisDurationMs += (FDateTime::UtcNow() - AnalysisStart).GetTotalMilliseconds();
        FAutomationAssetOutput Output;
        Output.AssetPath = AssetPath;
        Output.AssetName = Blueprint->GetName();
        Output.AssetType = TEXT("blueprint");
        OutResult.AssetOutputs.Add(Output);
        AddArtifact(OutResult, TEXT("blueprint_meta"), MetaPath, AssetPath, CacheStatus, CombinedMd5);
        OutResult.bSuccess = true;
        OutResult.Status = TEXT("succeeded");
        return true;
    }

    OutResult.Metrics.CacheMissCount++;

    // Build new meta.
    OutResult.AddLog(TEXT("Analyze: build meta"));
    const TSharedRef<FJsonObject> Meta = MakeShared<FJsonObject>();
    Meta->SetNumberField(TEXT("schema_version"), UEAutomation::MetaCache::CurrentSchemaVersion);
    Meta->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());

    const TSharedRef<FJsonObject> Generator = MakeShared<FJsonObject>();
    Generator->SetStringField(TEXT("plugin_name"), TEXT("UEEditorAutomation"));
    Generator->SetStringField(TEXT("task_id"), OutResult.TaskId);
    Meta->SetObjectField(TEXT("generator"), Generator);

    const TSharedRef<FJsonObject> CacheJson = MakeShared<FJsonObject>();
    CacheJson->SetStringField(TEXT("cache_status"), CacheStatus);
    CacheJson->SetStringField(TEXT("meta_path"), MetaPath);
    CacheJson->SetStringField(TEXT("analysis_options_digest"), Decision.CurrentAnalysisOptionsDigest);
    CacheJson->SetBoolField(TEXT("source_match"), CacheStatus == UEAutomation::MetaCache::StatusHit
        || CacheStatus == UEAutomation::MetaCache::StatusPartialHitSourceSameAssetChanged);
    CacheJson->SetBoolField(TEXT("asset_match"), CacheStatus == UEAutomation::MetaCache::StatusHit);
    CacheJson->SetBoolField(TEXT("cache_root_fallback"), bFallbackCacheRoot);
    Meta->SetObjectField(TEXT("cache"), CacheJson);

    // asset block.
    const TSharedRef<FJsonObject> AssetJson = MakeShared<FJsonObject>();
    AssetJson->SetStringField(TEXT("object_path"), Blueprint->GetPathName());
    AssetJson->SetStringField(TEXT("package_name"), PackageName);
    AssetJson->SetStringField(TEXT("asset_name"), Blueprint->GetName());
    AssetJson->SetStringField(TEXT("asset_class"), Blueprint->GetClass()->GetName());
    if (UClass* GeneratedClass = Blueprint->GeneratedClass)
    {
        AssetJson->SetStringField(TEXT("generated_class_path"), GeneratedClass->GetPathName());
    }
    AssetJson->SetStringField(TEXT("package_file_path"), PackageFilePath);

    const TSharedRef<FJsonObject> AssetFp = MakeShared<FJsonObject>();
    AssetFp->SetStringField(TEXT("package_file_md5"), AssetFingerprint.Md5);
    AssetFp->SetNumberField(TEXT("package_file_size_bytes"), AssetFingerprint.SizeBytes);
    AssetFp->SetStringField(TEXT("package_file_mtime_utc"), AssetFingerprint.ModifiedTimeUtc);
    AssetFp->SetBoolField(TEXT("package_dirty"), Blueprint->GetOutermost() ? Blueprint->GetOutermost()->IsDirty() : false);
    AssetJson->SetObjectField(TEXT("asset_fingerprint"), AssetFp);
    Meta->SetObjectField(TEXT("asset"), AssetJson);

    // parent_class block.
    const TSharedRef<FJsonObject> ParentJson = MakeShared<FJsonObject>();
    auto BuildClassRecord = [](const FAutomationParentClassRecord& Record) -> TSharedRef<FJsonObject>
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("class_path"), Record.ClassPath);
        Object->SetStringField(TEXT("display_name"), Record.DisplayName);
        Object->SetBoolField(TEXT("is_native"), Record.bIsNative);
        return Object;
    };
    ParentJson->SetObjectField(TEXT("immediate_parent"), BuildClassRecord(ParentInfo.ImmediateParent));
    const TSharedRef<FJsonObject> NativeParentJson = BuildClassRecord(ParentInfo.NativeParent);
    NativeParentJson->SetStringField(TEXT("module_name"), ParentInfo.NativeParentModuleName);
    NativeParentJson->SetStringField(TEXT("header_path"), Source.HeaderPath);
    NativeParentJson->SetStringField(TEXT("cpp_path"), Source.CppPath);
    NativeParentJson->SetStringField(TEXT("source_status"), Source.SourceStatus);
    ParentJson->SetObjectField(TEXT("native_parent"), NativeParentJson);

    TArray<TSharedPtr<FJsonValue>> SuperChain;
    for (const FAutomationParentClassRecord& Record : ParentInfo.SuperChain)
    {
        SuperChain.Add(MakeShared<FJsonValueObject>(BuildClassRecord(Record)));
    }
    ParentJson->SetArrayField(TEXT("super_chain"), SuperChain);
    Meta->SetObjectField(TEXT("parent_class"), ParentJson);

    // source_fingerprint block.
    const TSharedRef<FJsonObject> SourceFp = MakeShared<FJsonObject>();
    SourceFp->SetNumberField(TEXT("fingerprint_version"), 1);
    SourceFp->SetStringField(TEXT("class_path"), ParentInfo.NativeParent.ClassPath);
    SourceFp->SetStringField(TEXT("combined_md5"), CombinedMd5);
    {
        FString HeaderMd5;
        FString CppMd5;
        TArray<TSharedPtr<FJsonValue>> FileArray;
        for (const FAutomationFileFingerprint& File : ParentFiles)
        {
            const TSharedRef<FJsonObject> FileObject = MakeShared<FJsonObject>();
            FileObject->SetStringField(TEXT("role"), File.Role);
            FileObject->SetStringField(TEXT("path"), File.AbsolutePath);
            FileObject->SetStringField(TEXT("relative_path"), File.RelativePath);
            FileObject->SetStringField(TEXT("md5"), File.Md5);
            FileObject->SetNumberField(TEXT("size_bytes"), File.SizeBytes);
            FileObject->SetStringField(TEXT("mtime_utc"), File.ModifiedTimeUtc);
            FileArray.Add(MakeShared<FJsonValueObject>(FileObject));
            if (File.Role == TEXT("header"))
            {
                HeaderMd5 = File.Md5;
            }
            else if (File.Role == TEXT("cpp"))
            {
                CppMd5 = File.Md5;
            }
        }
        SourceFp->SetStringField(TEXT("header_md5"), HeaderMd5);
        SourceFp->SetStringField(TEXT("cpp_md5"), CppMd5);
        SourceFp->SetArrayField(TEXT("files"), FileArray);
    }
    Meta->SetObjectField(TEXT("source_fingerprint"), SourceFp);

    // native_parent_cxx block.
    TSharedPtr<FJsonObject> NativeCxxJson;
    if (Request.Analysis.bIncludeNativeCxx && NativeClass)
    {
        FClassReflectionExporter ReflectionExporter;
        const TSharedRef<FJsonObject> ReflectionJson = MakeShared<FJsonObject>();
        ReflectionExporter.ExportClass(NativeClass, ReflectionJson, OutResult);
        Meta->SetObjectField(TEXT("native_parent_cxx"), ReflectionJson);
        NativeCxxJson = ReflectionJson;
    }

    // blueprint_snapshot block.
    TSharedPtr<FJsonObject> SnapshotJson;
    if (Request.Analysis.bIncludeBlueprintSnapshot)
    {
        FBlueprintSnapshotExporter SnapshotExporter;
        const TSharedRef<FJsonObject> Snapshot = MakeShared<FJsonObject>();
        SnapshotExporter.ExportBlueprintSnapshot(Blueprint, Request.Analysis,
            Whitelist.DeniedPropertyNamesForExport, Snapshot, OutResult);

        if (Request.Analysis.bIncludeGraphSummary)
        {
            FBlueprintGraphReadOnlyExporter GraphExporter;
            const TSharedRef<FJsonObject> GraphsJson = MakeShared<FJsonObject>();
            GraphExporter.ExportGraphSummary(Blueprint, Request.Analysis, GraphsJson, OutResult);
            Snapshot->SetObjectField(TEXT("graphs"), GraphsJson);
        }
        Meta->SetObjectField(TEXT("blueprint_snapshot"), Snapshot);
        SnapshotJson = Snapshot;
    }

    // references block.
    TSharedPtr<FJsonObject> RefJsonPtr;
    if (Request.Analysis.bIncludeReferences || Request.Analysis.bIncludeReferencers)
    {
        const FDateTime RefStart = FDateTime::UtcNow();
        FAssetReferenceGraphService RefService;
        const TSharedRef<FJsonObject> RefJson = MakeShared<FJsonObject>();
        RefService.ExportDirectReferences(AssetPath, Request.Analysis, RefJson, OutResult);
        Meta->SetObjectField(TEXT("references"), RefJson);
        RefJsonPtr = RefJson;
        OutResult.Metrics.ReferenceScanDurationMs += (FDateTime::UtcNow() - RefStart).GetTotalMilliseconds();
    }

    // ai_summary block.
    {
        const TSharedRef<FJsonObject> AiSummary =
            FBlueprintAISummaryBuilder::Build(Blueprint, ParentInfo, NativeCxxJson, SnapshotJson, RefJsonPtr);
        Meta->SetObjectField(TEXT("ai_summary"), AiSummary);
    }

    // Warnings echo.
    {
        TArray<TSharedPtr<FJsonValue>> WarningArray;
        for (const FString& Warning : OutResult.Warnings)
        {
            WarningArray.Add(MakeShared<FJsonValueString>(Warning));
        }
        Meta->SetArrayField(TEXT("warnings"), WarningArray);
    }

    // Write meta.
    FString WriteError;
    if (!Cache.WriteMetaAtomic(MetaPath, Meta, WriteError))
    {
        OutResult.AddError(TEXT("MetaCacheWriteFailed"), WriteError);
        return false;
    }
    OutResult.AddLog(FString::Printf(TEXT("Analyze: wrote meta %s"), *MetaPath));
    OutResult.Metrics.AnalyzedBlueprintCount++;
    OutResult.Metrics.AnalysisDurationMs += (FDateTime::UtcNow() - AnalysisStart).GetTotalMilliseconds();

    FAutomationAssetOutput Output;
    Output.AssetPath = AssetPath;
    Output.AssetName = Blueprint->GetName();
    Output.AssetType = TEXT("blueprint");
    OutResult.AssetOutputs.Add(Output);
    AddArtifact(OutResult, TEXT("blueprint_meta"), MetaPath, AssetPath, CacheStatus, CombinedMd5);

    OutResult.bSuccess = true;
    OutResult.Status = TEXT("succeeded");
    return true;
}
