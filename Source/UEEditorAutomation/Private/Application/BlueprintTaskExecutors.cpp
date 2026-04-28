#include "Application/BlueprintTaskExecutors.h"

FBlueprintTaskExecutorBase::FBlueprintTaskExecutorBase(const TSharedRef<FBlueprintAutomationService>& InService)
    : Service(InService)
{
}

bool FBlueprintTaskExecutorBase::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.TaskType == TEXT("create_blueprint"))
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
    }
    else if (Request.TargetAsset.AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required."), TEXT("payload.target_asset.asset_path"));
        return false;
    }

    return true;
}

FCreateBlueprintTaskExecutor::FCreateBlueprintTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FCreateBlueprintTaskExecutor::GetTaskType() const
{
    return TEXT("create_blueprint");
}

bool FCreateBlueprintTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->CreateBlueprint(Request, OutResult);
}

FModifyBlueprintComponentsTaskExecutor::FModifyBlueprintComponentsTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FModifyBlueprintComponentsTaskExecutor::GetTaskType() const
{
    return TEXT("modify_blueprint_components");
}

bool FModifyBlueprintComponentsTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->ModifyBlueprintComponents(Request, OutResult);
}

FModifyBlueprintDefaultsTaskExecutor::FModifyBlueprintDefaultsTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FModifyBlueprintDefaultsTaskExecutor::GetTaskType() const
{
    return TEXT("modify_blueprint_defaults");
}

bool FModifyBlueprintDefaultsTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->ModifyBlueprintDefaults(Request, OutResult);
}
