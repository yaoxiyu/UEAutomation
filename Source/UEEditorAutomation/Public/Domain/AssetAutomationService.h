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
    bool CreateMaterialInstance(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool ModifyMaterialInstance(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool CreateTypedAsset(const FAutomationTaskRequest& Request, const FString& TaskType, FAutomationTaskResult& OutResult);
    bool ImportAsset(const FAutomationTaskRequest& Request, const FString& TaskType, FAutomationTaskResult& OutResult);
    bool ModifyAssetProperties(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool CheckAssetRules(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool GenerateAuditReport(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);

private:
    struct FFactoryAssetSpec
    {
        FString AssetType;
        FString AssetClassPath;
        FString FactoryClassPath;
        TMap<FString, FString> FactoryObjectProperties;
        TMap<FString, FString> FactoryClassProperties;
    };

    UObject* LoadAsset(const FString& AssetPath, FAutomationTaskResult& OutResult, const FString& Field) const;
    UClass* LoadDataAssetClass(const FString& ClassPath, FAutomationTaskResult& OutResult, const FString& Field) const;
    bool BuildFactoryAssetSpec(const FAutomationTaskRequest& Request, const FString& TaskType, FFactoryAssetSpec& OutSpec, FAutomationTaskResult& OutResult) const;
    bool CreatePhysicsAsset(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const;
    bool CreateAssetWithFactory(const FAutomationTaskRequest& Request, const FFactoryAssetSpec& Spec, FAutomationTaskResult& OutResult) const;
    bool CreatePlainObjectAsset(const FAutomationTaskRequest& Request, const FString& AssetClassPath, const FString& AssetType, FAutomationTaskResult& OutResult) const;
    bool ConfigureFactory(UObject* Factory, const FFactoryAssetSpec& Spec, FAutomationTaskResult& OutResult) const;
    bool SetObjectOrClassProperty(UObject* Target, const FString& PropertyName, const FString& ObjectPath, bool bRequireClass, FAutomationTaskResult& OutResult, const FString& Field) const;
    bool LoadModuleForClassPath(const FString& ClassPath) const;
    FString GetParameterString(const FAutomationTaskRequest& Request, const FString& Name) const;
    bool ApplyMaterialInstanceParameters(class UMaterialInstanceConstant* MaterialInstance, const TArray<FAutomationPropertyValue>& Parameters, FAutomationTaskResult& OutResult, const FString& FieldPrefix) const;
    bool TryReadLinearColor(const FAutomationPropertyValue& Parameter, FLinearColor& OutColor) const;
    bool IsAllowedPackagePath(const FString& PackagePath) const;
    bool WriteAuditReport(const FString& OutputPath, FAutomationTaskResult& OutResult) const;
    FString ResolveReportPath(const FAutomationTaskRequest& Request) const;
    void AddAssetOutput(const FAutomationAssetSpec& Asset, const FString& AssetType, FAutomationTaskResult& OutResult) const;

    TSharedRef<IBlueprintEditorAdapter> EditorAdapter;
    FPropertyAssignmentService PropertyAssignmentService;
};
