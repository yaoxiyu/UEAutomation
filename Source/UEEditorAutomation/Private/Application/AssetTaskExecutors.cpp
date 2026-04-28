#include "Application/AssetTaskExecutors.h"

FAssetTaskExecutorBase::FAssetTaskExecutorBase(const TSharedRef<FAssetAutomationService>& InService)
    : Service(InService)
{
}

FCreateDataAssetTaskExecutor::FCreateDataAssetTaskExecutor(const TSharedRef<FAssetAutomationService>& InService)
    : FAssetTaskExecutorBase(InService)
{
}

FString FCreateDataAssetTaskExecutor::GetTaskType() const
{
    return TEXT("create_data_asset");
}

bool FCreateDataAssetTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.Asset.AssetName.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("asset_name is required."), TEXT("payload.asset.asset_name"));
        return false;
    }
    if (Request.Asset.PackagePath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("package_path is required."), TEXT("payload.asset.package_path"));
        return false;
    }
    if (Request.Asset.ParentClass.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("parent_class is required."), TEXT("payload.asset.parent_class"));
        return false;
    }
    return true;
}

bool FCreateDataAssetTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->CreateDataAsset(Request, OutResult);
}

FModifyAssetPropertiesTaskExecutor::FModifyAssetPropertiesTaskExecutor(const TSharedRef<FAssetAutomationService>& InService)
    : FAssetTaskExecutorBase(InService)
{
}

FString FModifyAssetPropertiesTaskExecutor::GetTaskType() const
{
    return TEXT("modify_asset_properties");
}

bool FModifyAssetPropertiesTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.TargetAsset.AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required."), TEXT("payload.target_asset.asset_path"));
        return false;
    }
    if (Request.ClassDefaults.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("properties must contain at least one property."), TEXT("payload.properties"));
        return false;
    }
    return true;
}

bool FModifyAssetPropertiesTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->ModifyAssetProperties(Request, OutResult);
}

FCheckAssetRulesTaskExecutor::FCheckAssetRulesTaskExecutor(const TSharedRef<FAssetAutomationService>& InService)
    : FAssetTaskExecutorBase(InService)
{
}

FString FCheckAssetRulesTaskExecutor::GetTaskType() const
{
    return TEXT("check_asset_rules");
}

bool FCheckAssetRulesTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.TargetAsset.AssetPath.IsEmpty() && Request.TargetAssets.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset or target_assets is required."), TEXT("payload.target_assets"));
        return false;
    }
    return true;
}

bool FCheckAssetRulesTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->CheckAssetRules(Request, OutResult);
}

FGenerateAuditReportTaskExecutor::FGenerateAuditReportTaskExecutor(const TSharedRef<FAssetAutomationService>& InService)
    : FAssetTaskExecutorBase(InService)
{
}

FString FGenerateAuditReportTaskExecutor::GetTaskType() const
{
    return TEXT("generate_audit_report");
}

bool FGenerateAuditReportTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return true;
}

bool FGenerateAuditReportTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->GenerateAuditReport(Request, OutResult);
}
