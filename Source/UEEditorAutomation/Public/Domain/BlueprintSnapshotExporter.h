#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
struct FAutomationAnalysisOptions;
struct FAutomationTaskResult;

class FBlueprintSnapshotExporter
{
public:
    bool ExportBlueprintSnapshot(
        UBlueprint* Blueprint,
        const FAutomationAnalysisOptions& Options,
        const TArray<FString>& DeniedExportNames,
        const TSharedRef<FJsonObject>& OutJson,
        FAutomationTaskResult& OutResult) const;
};
