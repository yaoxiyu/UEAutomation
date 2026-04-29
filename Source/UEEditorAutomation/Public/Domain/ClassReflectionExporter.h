#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class UClass;
struct FAutomationTaskResult;

class FClassReflectionExporter
{
public:
    bool ExportClass(
        UClass* Class,
        const TSharedRef<FJsonObject>& OutJson,
        FAutomationTaskResult& OutResult) const;
};
