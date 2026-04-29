#pragma once

#include "CoreMinimal.h"

class UClass;

struct FAutomationCppSourceLocation
{
    FString ModuleName;
    FString HeaderPath;
    FString CppPath;
    FString SourceStatus; // "resolved" | "header_only" | "unresolved"
};

class FCppSourceResolver
{
public:
    bool Resolve(UClass* NativeClass, FAutomationCppSourceLocation& OutLocation, FString& OutError) const;

private:
    static bool TryClassMetadata(UClass* NativeClass, FString& OutHeaderPath);
    static bool TrySourceCodeNavigation(UClass* NativeClass, FString& OutHeaderPath);
    static bool TryScanSearchRoots(
        const FString& ModuleName,
        const FString& ClassNameNoPrefix,
        FString& OutHeaderPath,
        FString& OutCppPath);
};
