#pragma once

#include "CoreMinimal.h"
#include "Adapter/BlueprintEditorAdapter.h"
#include "Domain/PropertyAssignmentService.h"
#include "Protocol/AutomationProtocolTypes.h"

class FBlueprintAutomationService
{
public:
    explicit FBlueprintAutomationService(const TSharedRef<IBlueprintEditorAdapter>& InBlueprintAdapter);

    bool CreateBlueprint(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool ModifyBlueprintComponents(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool ModifyBlueprintDefaults(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool CopyLiveBlueprintValues(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool CopyBlueprintLiveOverrides(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool DiagnoseBlueprintPropertyPersistence(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool DoesAssetExist(const FString& PackagePath, const FString& AssetName) const;

private:
    struct FAssignedPropertySnapshot
    {
        FString OwnerPath;
        FString PropertyName;
        FString ExpectedText;
        FString FieldPrefix;
        FString SnapshotKind;
        TMap<FString, FString> ExpectedSemanticValues;
    };

    UClass* LoadClassByPath(const FString& ClassPath, FAutomationTaskResult& OutResult, const FString& Field) const;
    bool AddComponent(UBlueprint* Blueprint, const FAutomationComponentSpec& Component, FAutomationTaskResult& OutResult, const FString& FieldPrefix);
    bool BuildLiveOverrideCopyRequest(const FAutomationTaskRequest& Request, UBlueprint* SourceBlueprint, FAutomationTaskRequest& OutCopyRequest, FAutomationTaskResult& OutResult) const;
    void AppendLiveDefaultOverrideNames(UBlueprint* SourceBlueprint, const TArray<FString>& DeniedExportNames, TArray<FAutomationPropertyValue>& OutProperties) const;
    bool AppendLiveComponentOverrideOperations(UBlueprint* SourceBlueprint, const TArray<FString>& DeniedExportNames, TArray<FAutomationOperation>& OutOperations, FAutomationTaskResult& OutResult) const;
    FString ResolveComponentLookupPolicy(const FAutomationOperation& Operation) const;
    FString ResolveSourceComponentLookupPolicy(const FAutomationOperation& Operation) const;
    FString ResolveComponentWriteTarget(const FAutomationOperation& Operation) const;
    bool BuildLiveImportProperty(UObject* Source, const FString& PropertyName, FAutomationPropertyValue& OutProperty, FString& OutError) const;
    bool TryCopySpecialLiveProperty(UObject* Source, UObject* Target, const FString& PropertyName, const FString& OwnerPath, const FString& WriteTarget, const FString& FieldPrefix, FAutomationTaskResult& OutResult, TArray<FAssignedPropertySnapshot>& OutSnapshots, bool& bOutHandled) const;
    bool CopyPrimitiveBodyInstance(class UPrimitiveComponent* Source, class UPrimitiveComponent* Target, FString& OutError) const;
    bool CapturePrimitiveBodyInstanceSnapshot(class UPrimitiveComponent* Source, const FString& OwnerPath, const FString& FieldPrefix, TArray<FAssignedPropertySnapshot>& OutSnapshots, FAutomationTaskResult& OutResult) const;
    bool VerifyPrimitiveBodyInstanceSnapshot(class UPrimitiveComponent* Target, const FAssignedPropertySnapshot& Snapshot, FAutomationTaskResult& OutResult) const;
    void AddPropertyFieldResults(FAutomationTaskResult& OutResult, const FString& OwnerPath, const FString& WriteTarget, const TArray<FAutomationPropertyValue>& Properties, const FString& Reason = FString()) const;
    bool CapturePropertySnapshots(UObject* Target, const FString& OwnerPath, const TArray<FAutomationPropertyValue>& Properties, const FString& FieldPrefix, const TArray<FAutomationAssetRedirect>& Redirects, TArray<FAssignedPropertySnapshot>& OutSnapshots, FAutomationTaskResult& OutResult) const;
    bool VerifyPropertySnapshots(UObject* Target, const TArray<FAssignedPropertySnapshot>& Snapshots, FAutomationTaskResult& OutResult) const;
    bool AddDiagnosticReadback(FAutomationTaskResult& OutResult, UObject* Target, const FString& PropertyName, const FString& Phase, FString& OutText) const;
    bool CompileSaveOpen(UBlueprint* Blueprint, const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    void AddCreatedAssetOutput(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const;
    void AddTargetAssetOutput(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const;
    bool CompileIfRequested(UBlueprint* Blueprint, bool bCompile, FAutomationTaskResult& OutResult);
    bool SaveIfRequested(UBlueprint* Blueprint, bool bSave, FAutomationTaskResult& OutResult);

    TSharedRef<IBlueprintEditorAdapter> BlueprintAdapter;
    FPropertyAssignmentService PropertyAssignmentService;
};
