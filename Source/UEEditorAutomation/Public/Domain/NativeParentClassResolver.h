#pragma once

#include "CoreMinimal.h"

class UBlueprint;
class UClass;

struct FAutomationParentClassRecord
{
    FString ClassPath;
    FString DisplayName;
    bool bIsNative = false;
};

struct FAutomationNativeParentInfo
{
    FAutomationParentClassRecord ImmediateParent;
    FAutomationParentClassRecord NativeParent;
    FString NativeParentModuleName;
    TArray<FAutomationParentClassRecord> SuperChain;
};

class FNativeParentClassResolver
{
public:
    bool Resolve(UBlueprint* Blueprint, FAutomationNativeParentInfo& OutInfo, FString& OutError) const;
    bool ResolveFromClass(UClass* Class, FAutomationNativeParentInfo& OutInfo, FString& OutError) const;

    static FString DeriveModuleNameFromClassPath(const FString& ClassPath);
};
