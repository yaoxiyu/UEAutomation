#pragma once

#include "CoreMinimal.h"
#include "Adapter/BlueprintEditorAdapter.h"
#include "Domain/PropertyAssignmentService.h"
#include "Protocol/AutomationProtocolTypes.h"

class FAssetAutomationService
{
public:
    explicit FAssetAutomationService(const TSharedRef<IBlueprintEditorAdapter>& InEditorAdapter);

    bool CreateDataAsset(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool ModifyAssetProperties(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool CheckAssetRules(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool GenerateAuditReport(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);

private:
    UObject* LoadAsset(const FString& AssetPath, FAutomationTaskResult& OutResult, const FString& Field) const;
    UClass* LoadDataAssetClass(const FString& ClassPath, FAutomationTaskResult& OutResult, const FString& Field) const;
    bool IsAllowedPackagePath(const FString& PackagePath) const;
    bool WriteAuditReport(const FString& OutputPath, FAutomationTaskResult& OutResult) const;
    FString ResolveReportPath(const FAutomationTaskRequest& Request) const;
    void AddAssetOutput(const FAutomationAssetSpec& Asset, const FString& AssetType, FAutomationTaskResult& OutResult) const;

    TSharedRef<IBlueprintEditorAdapter> EditorAdapter;
    FPropertyAssignmentService PropertyAssignmentService;
};
