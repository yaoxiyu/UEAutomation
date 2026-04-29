#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

class FAutomationStableJsonWriter
{
public:
    // Pretty-print JSON to a string. Field order follows the FJsonObject->Values
    // insertion order; callers must add fields in the desired order.
    static bool SerializePretty(const TSharedRef<FJsonObject>& Root, FString& OutText);

    // Atomically write JSON to AbsolutePath. Writes to a sibling .tmp file then
    // moves it over the target. Creates parent directories as needed. On
    // failure, returns false and fills OutError; the existing target is left
    // untouched.
    static bool WriteAtomic(
        const FString& AbsolutePath,
        const TSharedRef<FJsonObject>& Root,
        FString& OutError);
};
