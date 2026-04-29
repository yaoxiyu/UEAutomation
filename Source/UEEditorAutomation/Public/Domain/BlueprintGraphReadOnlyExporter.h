#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
struct FAutomationAnalysisOptions;
struct FAutomationTaskResult;

class FBlueprintGraphReadOnlyExporter
{
public:
    bool ExportGraphSummary(
        UBlueprint* Blueprint,
        const FAutomationAnalysisOptions& Options,
        const TSharedRef<FJsonObject>& OutJson,
        FAutomationTaskResult& OutResult) const;
};
