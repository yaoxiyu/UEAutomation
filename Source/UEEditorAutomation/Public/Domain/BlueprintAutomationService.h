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
    bool DoesAssetExist(const FString& PackagePath, const FString& AssetName) const;

private:
    UClass* LoadClassByPath(const FString& ClassPath, FAutomationTaskResult& OutResult, const FString& Field) const;
    bool AddComponent(UBlueprint* Blueprint, const FAutomationComponentSpec& Component, FAutomationTaskResult& OutResult, const FString& FieldPrefix);
    bool CompileSaveOpen(UBlueprint* Blueprint, const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    void AddCreatedAssetOutput(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const;
    void AddTargetAssetOutput(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const;
    bool CompileIfRequested(UBlueprint* Blueprint, bool bCompile, FAutomationTaskResult& OutResult);
    bool SaveIfRequested(UBlueprint* Blueprint, bool bSave, FAutomationTaskResult& OutResult);

    TSharedRef<IBlueprintEditorAdapter> BlueprintAdapter;
    FPropertyAssignmentService PropertyAssignmentService;
};
