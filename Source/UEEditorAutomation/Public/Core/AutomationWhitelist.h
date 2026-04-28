#pragma once

#include "CoreMinimal.h"

struct FAutomationWhitelist
{
    TArray<FString> AllowedTaskTypes;
    TArray<FString> AllowedAssetRoots;
    TArray<FString> AllowedParentClasses;
    TArray<FString> AllowedComponentClasses;
    TArray<FString> AllowedPropertyNames;
};

class FAutomationWhitelistProvider
{
public:
    static FAutomationWhitelist Load();

private:
    static FString ResolveWhitelistPath();
    static void ReadStringArray(const TSharedPtr<class FJsonObject>& JsonObject, const TCHAR* FieldName, TArray<FString>& OutValues);
};
