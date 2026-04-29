#pragma once

#include "CoreMinimal.h"

struct FAutomationFileFingerprint
{
    FString Role;
    FString AbsolutePath;
    FString RelativePath;
    FString Md5;
    int64 SizeBytes = 0;
    FString ModifiedTimeUtc;
};

class FAutomationFileFingerprintUtil
{
public:
    static bool FingerprintFile(
        const FString& AbsolutePath,
        const FString& Role,
        FAutomationFileFingerprint& OutFingerprint,
        FString& OutError);

    // Combined hash of one or more native parent C++ source files. Sort order is
    // deterministic based on Role then AbsolutePath so the result is stable.
    // Schema is fixed to "parent-cxx-fingerprint-v1".
    static FString BuildCombinedMd5(
        const FString& ClassPath,
        const TArray<FAutomationFileFingerprint>& Files);

    // Convert an absolute path to a path relative to ProjectDir or EngineDir
    // when possible; otherwise returns the original path.
    static FString MakeProjectOrEngineRelative(const FString& AbsolutePath);
};
