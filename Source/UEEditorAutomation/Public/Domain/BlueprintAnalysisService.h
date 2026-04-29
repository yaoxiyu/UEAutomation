#pragma once

#include "CoreMinimal.h"

struct FAutomationTaskRequest;
struct FAutomationTaskResult;

class FBlueprintAnalysisService
{
public:
    bool AnalyzeBlueprint(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool AnalyzeReferenceChain(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool AnalyzeAsset(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool RefreshMetaCache(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);
    bool ExportAIContext(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult);

private:
    bool AnalyzeSingleBlueprint(
        const FString& AssetPath,
        const FAutomationTaskRequest& Request,
        FAutomationTaskResult& OutResult);
};
