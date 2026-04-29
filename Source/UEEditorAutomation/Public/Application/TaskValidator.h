#pragma once

#include "CoreMinimal.h"
#include "Protocol/AutomationProtocolTypes.h"

class FTaskValidator
{
public:
    bool ValidateCommon(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const;

private:
    bool ValidateAssetRoots(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const;
    bool ValidatePackagePathRoot(const FString& PackagePath, const FString& Field, FAutomationTaskResult& OutResult) const;
    bool ValidateObjectPathRoot(const FString& ObjectPath, const FString& Field, FAutomationTaskResult& OutResult) const;
    bool IsAnalysisOnlyTask(const FString& TaskType) const;
    bool IsAllowedAssetRoot(const FString& PackagePath) const;
};
