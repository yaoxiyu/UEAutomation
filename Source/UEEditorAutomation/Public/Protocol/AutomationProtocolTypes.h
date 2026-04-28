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

struct FAutomationAssetSpec
{
    FString AssetType;
    FString AssetName;
    FString PackagePath;
    FString AssetPath;
    FString ParentClass;
    FString BlueprintType;
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
    FAutomationComponentSpec Component;
    TArray<FAutomationPropertyValue> Properties;
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
    FAutomationComponentSpec RootComponent;
    TArray<FAutomationComponentSpec> Components;
    TArray<FAutomationPropertyValue> ClassDefaults;
    TArray<FAutomationOperation> Operations;
    TArray<FString> PostActions;
    FString SourcePath;
};

struct FAutomationAssetOutput
{
    FString AssetPath;
    FString AssetName;
    FString AssetType;
};

struct FAutomationTaskResult
{
    int32 ProtocolVersion = 1;
    FString TaskId;
    FString TaskType;
    bool bSuccess = false;
    FString Status = TEXT("failed");
    TArray<FAutomationAssetOutput> AssetOutputs;
    TArray<FString> Warnings;
    TArray<FAutomationError> Errors;
    TArray<FString> LogLines;
    FAutomationMetrics Metrics;
    FString LogPath;

    void AddError(const FString& Code, const FString& Message, const FString& Field = FString());
    void AddWarning(const FString& Message);
    void AddLog(const FString& Message);
};

class FAutomationProtocolJson
{
public:
    static bool ParseRequest(const FString& JsonText, FAutomationTaskRequest& OutRequest, FAutomationTaskResult& OutResult);
    static bool SerializeResult(const FAutomationTaskResult& Result, FString& OutJsonText);

private:
    static bool ParseAssetSpec(const TSharedPtr<FJsonObject>& Object, FAutomationAssetSpec& OutSpec);
    static bool ParseComponentSpec(const TSharedPtr<FJsonObject>& Object, FAutomationComponentSpec& OutSpec);
    static bool ParsePropertyArray(const TArray<TSharedPtr<FJsonValue>>* Array, TArray<FAutomationPropertyValue>& OutProperties);
    static bool ParseVector(const TArray<TSharedPtr<FJsonValue>>& Array, FVector& OutVector);
    static bool ParseRotator(const TArray<TSharedPtr<FJsonValue>>& Array, FRotator& OutRotator);
};
