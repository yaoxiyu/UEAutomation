#pragma once

#include "CoreMinimal.h"

struct FAutomationWhitelist
{
    bool bLoaded = false;
    FString LoadError;
    TArray<FString> AllowedTaskTypes;
    TArray<FString> AllowedAssetRoots;
    TArray<FString> AllowedParentClasses;
    TArray<FString> AllowedComponentClasses;
    TArray<FString> AllowedPropertyNames;
    TArray<FString> DeniedPropertyNamesForExport;
};

class FAutomationWhitelistProvider
{
public:
    static FAutomationWhitelist Load();

private:
    static FString ResolveWhitelistPath();
    static void ReadStringArray(const TSharedPtr<class FJsonObject>& JsonObject, const TCHAR* FieldName, TArray<FString>& OutValues);
};
