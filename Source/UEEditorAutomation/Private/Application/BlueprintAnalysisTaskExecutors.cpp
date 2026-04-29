#include "Application/BlueprintAnalysisTaskExecutors.h"

#include "Protocol/AutomationProtocolTypes.h"

bool FBlueprintAnalysisTaskExecutorBase::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.TargetAsset.AssetPath.IsEmpty() && Request.TargetAssets.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required"), TEXT("payload.target_asset.asset_path"));
        return false;
    }
    return true;
}

bool FAnalyzeBlueprintTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->AnalyzeBlueprint(Request, OutResult);
}

bool FAnalyzeBlueprintReferenceChainTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->AnalyzeReferenceChain(Request, OutResult);
}

bool FAnalyzeAssetTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->AnalyzeAsset(Request, OutResult);
}

bool FRefreshBlueprintMetaCacheTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.TargetAsset.AssetPath.IsEmpty() && Request.TargetAssets.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"),
            TEXT("target_asset or target_assets[] required"),
            TEXT("payload.target_asset.asset_path"));
        return false;
    }
    return true;
}

bool FRefreshBlueprintMetaCacheTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->RefreshMetaCache(Request, OutResult);
}

bool FExportBlueprintAIContextTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->ExportAIContext(Request, OutResult);
}
