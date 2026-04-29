#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FAutomationAnalysisOptions;
struct FAutomationTaskResult;

struct FAutomationReferenceGraphMetrics
{
    int32 NodeCount = 0;
    int32 EdgeCount = 0;
    bool bTruncated = false;
    bool bCycleDetected = false;
};

class FAssetReferenceGraphService
{
public:
    // Direct dependencies / referencers for a single asset; produces the
    // `references` block of the meta JSON.
    bool ExportDirectReferences(
        const FString& AssetPath,
        const FAutomationAnalysisOptions& Options,
        const TSharedRef<FJsonObject>& OutJson,
        FAutomationTaskResult& OutResult) const;

    // Recursive graph; produces the `reference_graph` block.
    bool ExportReferenceGraph(
        const FString& RootAssetPath,
        const FAutomationAnalysisOptions& Options,
        const FString& MetaCacheRoot,
        const TSharedRef<FJsonObject>& OutJson,
        FAutomationReferenceGraphMetrics& OutMetrics,
        FAutomationTaskResult& OutResult) const;
};
