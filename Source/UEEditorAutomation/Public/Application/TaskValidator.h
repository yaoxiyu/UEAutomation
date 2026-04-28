#pragma once

#include "CoreMinimal.h"
#include "Protocol/AutomationProtocolTypes.h"

class FTaskValidator
{
public:
    bool ValidateCommon(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const;

private:
    bool IsAllowedAssetRoot(const FString& PackagePath) const;
};
