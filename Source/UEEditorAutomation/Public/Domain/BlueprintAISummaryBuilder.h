#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UBlueprint;
struct FAutomationNativeParentInfo;

class FBlueprintAISummaryBuilder
{
public:
    // Builds the rule_based ai_summary block. Inputs are derived from the
    // already-built native_parent_cxx, blueprint_snapshot, references, and
    // graph blocks of the current meta document.
    static TSharedRef<FJsonObject> Build(
        UBlueprint* Blueprint,
        const FAutomationNativeParentInfo& ParentInfo,
        const TSharedPtr<FJsonObject>& NativeCxxJson,
        const TSharedPtr<FJsonObject>& BlueprintSnapshotJson,
        const TSharedPtr<FJsonObject>& ReferencesJson);
};
