#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonValue.h"

class UObject;
class FProperty;
struct FAutomationAnalysisOptions;
struct FAutomationTaskResult;

class FPropertySnapshotService
{
public:
    FPropertySnapshotService();

    bool ExportObjectProperties(
        UObject* Target,
        const FAutomationAnalysisOptions& Options,
        TArray<TSharedPtr<FJsonValue>>& OutProperties,
        FAutomationTaskResult& OutResult) const;

    void SetDeniedExportNames(const TArray<FString>& DeniedNames);

    // Compare optional ParentTarget to mark differs_from_parent.
    void SetParentTargetForDiff(UObject* ParentTarget);

private:
    TSharedPtr<FJsonValue> ExportPropertyValue(
        FProperty* Property,
        const void* PropertyAddress,
        int32 RecursionDepth,
        const FAutomationAnalysisOptions& Options,
        FAutomationTaskResult& OutResult) const;

    bool ShouldSkipForExport(const FProperty* Property) const;
    bool IsDenied(const FString& PropertyName) const;

    TArray<FString> DeniedNames;
    UObject* ParentTarget = nullptr;
};
