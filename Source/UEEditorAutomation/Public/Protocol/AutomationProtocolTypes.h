#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

struct FAutomationError
{
    FString Code;
    FString Message;
    FString Field;
};

struct FAutomationMetrics
{
    int64 DurationMs = 0;
    int64 CompileDurationMs = 0;
    int64 SaveDurationMs = 0;
    int32 ComponentCreateCount = 0;
    int32 PropertyAssignCount = 0;
    int32 WarningCount = 0;
    int32 ErrorCount = 0;

    // Phase 4 analysis-only fields. Default to 0 for non-analysis tasks.
    int64 AnalysisDurationMs = 0;
    int64 SourceResolveDurationMs = 0;
    int64 ReferenceScanDurationMs = 0;
    int32 CacheHitCount = 0;
    int32 CacheMissCount = 0;
    int32 AnalyzedBlueprintCount = 0;
    int32 ExportedPropertyCount = 0;
    int32 ReferenceNodeCount = 0;
    int32 ReferenceEdgeCount = 0;
    bool bReferenceGraphTruncated = false;
};

struct FAutomationPropertyValue
{
    FString Name;
    FString Type;
    TSharedPtr<FJsonValue> Value;
};

struct FAutomationTransformSpec
{
    bool bHasLocation = false;
    bool bHasRotation = false;
    bool bHasScale = false;
    FVector Location = FVector::ZeroVector;
    FRotator Rotation = FRotator::ZeroRotator;
    FVector Scale = FVector(1.0f, 1.0f, 1.0f);
};

struct FAutomationComponentSpec
{
    FString ComponentName;
    FString ComponentClass;
    FString AttachParent;
    FAutomationTransformSpec Transform;
    TArray<FAutomationPropertyValue> Properties;
};

struct FAutomationComponentOverride
{
    FString ComponentName;
    FAutomationTransformSpec Transform;
    TArray<FAutomationPropertyValue> Properties;
};

struct FAutomationAssetSpec
{
    FString AssetType;
    FString AssetName;
    FString PackagePath;
    FString AssetPath;
    FString ParentClass;
    FString BlueprintType;
};

struct FAutomationTemplateRef
{
    FString TemplateId;
};

struct FAutomationBatchBlueprintItem
{
    FAutomationAssetSpec Asset;
    FAutomationTemplateRef Template;
    TArray<FAutomationComponentOverride> ComponentOverrides;
    TArray<FAutomationPropertyValue> ClassDefaults;
};

struct FAutomationExecutionOptions
{
    FString Priority = TEXT("normal");
    FString IdempotencyKey;
    bool bSkipIfExists = false;
    bool bOverwriteIfExists = false;
    bool bOpenAfterSuccess = false;
    bool bSaveAfterSuccess = true;
    bool bCompileAfterCreate = true;
};

struct FAutomationOperation
{
    FString Op;
    FString ComponentLookupPolicy;
    FString TargetKind;
    FAutomationComponentSpec Component;
    TArray<FAutomationPropertyValue> Properties;
};

struct FAutomationAnalysisOptions
{
    bool bHasAnalysisBlock = false;
    bool bForceRefresh = false;
    bool bUseCache = true;
    bool bIncludeNativeCxx = true;
    bool bIncludeBlueprintSnapshot = true;
    bool bIncludeClassDefaults = true;
    bool bIncludeComponents = true;
    bool bIncludeReferences = true;
    bool bIncludeReferencers = true;
    bool bIncludeGraphSummary = true;
    bool bIncludeGraphPins = false;
    bool bExportOnlyEditableProperties = true;
    int32 ReferenceDepth = 1;
    int32 MaxNodes = 128;
    int32 MaxEdges = 512;
    int32 MaxPropertyDepth = 8;
    int32 MaxArrayElements = 128;
};

struct FAutomationAssetRedirect
{
    FString From;
    FString To;
};

struct FAutomationTaskRequest
{
    int32 ProtocolVersion = 0;
    FString TaskId;
    FString TaskType;
    FString TimestampUtc;
    FAutomationExecutionOptions Execution;
    FAutomationAssetSpec Asset;
    FAutomationAssetSpec TargetAsset;
    TArray<FAutomationAssetSpec> TargetAssets;
    FAutomationTemplateRef Template;
    FAutomationTemplateRef SharedTemplate;
    FAutomationComponentSpec RootComponent;
    TArray<FAutomationComponentSpec> Components;
    TArray<FAutomationComponentOverride> ComponentOverrides;
    TArray<FAutomationPropertyValue> ClassDefaults;
    TArray<FAutomationBatchBlueprintItem> BatchItems;
    TArray<FAutomationOperation> Operations;
    TArray<FAutomationPropertyValue> Parameters;
    TArray<FString> Rules;
    TArray<FString> PostActions;
    FString ReportPath;
    FString ReportFormat;
    FString SourcePath;
    FString TaskFilePath;
    FAutomationAnalysisOptions Analysis;

    // duplicate_asset / redirect_asset_references payload
    FString SourceAssetPath;
    FString DestinationPackagePath;
    FString DestinationAssetName;
    bool bOverwriteDestination = false;
    TArray<FAutomationAssetRedirect> AssetRedirects;

    // list_directory_assets payload
    FString DirectoryPath;
    bool bRecursive = true;
};

struct FAutomationAssetOutput
{
    FString AssetPath;
    FString PackageName;
    FString PackageFilePath;
    FString AssetName;
    FString AssetType;
};

struct FAutomationArtifactOutput
{
    FString ArtifactType;
    FString Path;
    FString AssetPath;
    FString CacheStatus;
    FString ParentCppMd5;
};

struct FAutomationFieldResult
{
    FString Path;
    FString Status;
    FString WriteTarget;
    FString WriteMode;
    FString Reason;
    FString Message;
};

struct FAutomationTaskResult
{
    int32 ProtocolVersion = 1;
    FString TaskId;
    FString TaskType;
    bool bSuccess = false;
    FString Status = TEXT("failed");
    TArray<FAutomationAssetOutput> AssetOutputs;
    TArray<FAutomationArtifactOutput> Artifacts;
    TArray<FAutomationFieldResult> FieldResults;
    TArray<FString> Warnings;
    TArray<FAutomationError> Errors;
    TArray<FString> LogLines;
    FAutomationMetrics Metrics;
    FString LogPath;

    void AddError(const FString& Code, const FString& Message, const FString& Field = FString());
    void AddWarning(const FString& Message);
    void AddLog(const FString& Message);
    void AddFieldResult(const FString& Path, const FString& Status, const FString& WriteTarget, const FString& WriteMode, const FString& Reason = FString(), const FString& Message = FString());
};

class FAutomationProtocolJson
{
public:
    static bool ParseRequest(const FString& JsonText, FAutomationTaskRequest& OutRequest, FAutomationTaskResult& OutResult);
    static bool SerializeResult(const FAutomationTaskResult& Result, FString& OutJsonText);

private:
    static bool ParseAssetSpec(const TSharedPtr<FJsonObject>& Object, FAutomationAssetSpec& OutSpec);
    static bool ParseComponentSpec(const TSharedPtr<FJsonObject>& Object, FAutomationComponentSpec& OutSpec);
    static bool ParseComponentOverride(const TSharedPtr<FJsonObject>& Object, FAutomationComponentOverride& OutSpec);
    static bool ParsePropertyArray(const TArray<TSharedPtr<FJsonValue>>* Array, TArray<FAutomationPropertyValue>& OutProperties);
    static bool ParseVector(const TArray<TSharedPtr<FJsonValue>>& Array, FVector& OutVector);
    static bool ParseRotator(const TArray<TSharedPtr<FJsonValue>>& Array, FRotator& OutRotator);
};
