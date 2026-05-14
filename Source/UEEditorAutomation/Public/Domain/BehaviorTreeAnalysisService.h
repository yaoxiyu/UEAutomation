#pragma once

#include "CoreMinimal.h"

struct FAutomationTaskRequest;
struct FAutomationTaskResult;

class FBehaviorTreeAnalysisService
{
public:
    bool AnalyzeBehaviorTree(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);

private:
    bool AnalyzeSingleBehaviorTree(
        const FString& AssetPath,
        const FAutomationTaskRequest& Request,
        FAutomationTaskResult& OutResult);
};
