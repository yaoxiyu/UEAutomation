#pragma once

#include "CoreMinimal.h"

struct FAutomationTaskRequest;
struct FAutomationTaskResult;

class FAssetDuplicationService
{
public:
    bool DuplicateAsset(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool RedirectAssetReferences(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool ListDirectoryAssets(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool DeleteDirectoryAssets(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
};
