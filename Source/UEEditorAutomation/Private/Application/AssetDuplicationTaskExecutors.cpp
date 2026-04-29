#include "Application/AssetDuplicationTaskExecutors.h"

#include "Protocol/AutomationProtocolTypes.h"

bool FAssetDuplicationTaskExecutorBase::Validate(const FAutomationTaskRequest& /*Request*/, FAutomationTaskResult& /*OutResult*/)
{
    return true;
}

bool FDuplicateAssetTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->DuplicateAsset(Request, OutResult);
}

bool FRedirectAssetReferencesTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->RedirectAssetReferences(Request, OutResult);
}

bool FListDirectoryAssetsTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->ListDirectoryAssets(Request, OutResult);
}
