#include "Core/FileFingerprint.h"

#include "HAL/FileManager.h"
#include "HAL/PlatformFileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"

namespace
{
    FString FormatUtc(const FDateTime& Time)
    {
        return Time.ToIso8601();
    }
}

bool FAutomationFileFingerprintUtil::FingerprintFile(
    const FString& AbsolutePath,
    const FString& Role,
    FAutomationFileFingerprint& OutFingerprint,
    FString& OutError)
{
    if (AbsolutePath.IsEmpty())
    {
        OutError = TEXT("FileFingerprint: empty path");
        return false;
    }

    IFileManager& FileManager = IFileManager::Get();
    if (!FileManager.FileExists(*AbsolutePath))
    {
        OutError = FString::Printf(TEXT("FileFingerprint: file not found: %s"), *AbsolutePath);
        return false;
    }

    TArray<uint8> Bytes;
    if (!FFileHelper::LoadFileToArray(Bytes, *AbsolutePath))
    {
        OutError = FString::Printf(TEXT("FileFingerprint: failed to read: %s"), *AbsolutePath);
        return false;
    }

    OutFingerprint.Role = Role;
    OutFingerprint.AbsolutePath = AbsolutePath;
    OutFingerprint.RelativePath = MakeProjectOrEngineRelative(AbsolutePath);
    OutFingerprint.Md5 = FMD5::HashBytes(Bytes.GetData(), Bytes.Num());
    OutFingerprint.SizeBytes = Bytes.Num();
    const FDateTime Mtime = FileManager.GetTimeStamp(*AbsolutePath);
    OutFingerprint.ModifiedTimeUtc = (Mtime == FDateTime::MinValue()) ? FString() : FormatUtc(Mtime);
    return true;
}

FString FAutomationFileFingerprintUtil::BuildCombinedMd5(
    const FString& ClassPath,
    const TArray<FAutomationFileFingerprint>& Files)
{
    TArray<FAutomationFileFingerprint> Sorted = Files;
    Sorted.Sort([](const FAutomationFileFingerprint& A, const FAutomationFileFingerprint& B)
    {
        if (A.Role != B.Role)
        {
            return A.Role < B.Role;
        }
        return A.AbsolutePath < B.AbsolutePath;
    });

    FString Manifest;
    Manifest += TEXT("schema=parent-cxx-fingerprint-v1\n");
    Manifest += FString::Printf(TEXT("class_path=%s\n"), *ClassPath);
    for (const FAutomationFileFingerprint& File : Sorted)
    {
        Manifest += FString::Printf(TEXT("%s=%s\n"), *File.Role, *File.Md5);
    }

    FTCHARToUTF8 Utf8(*Manifest);
    return FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}

FString FAutomationFileFingerprintUtil::MakeProjectOrEngineRelative(const FString& AbsolutePath)
{
    FString Normalized = AbsolutePath;
    FPaths::NormalizeFilename(Normalized);

    auto TryRelative = [&](const FString& Base) -> bool
    {
        FString BaseFull = FPaths::ConvertRelativePathToFull(Base);
        FPaths::NormalizeDirectoryName(BaseFull);
        if (!BaseFull.EndsWith(TEXT("/")))
        {
            BaseFull += TEXT("/");
        }
        if (Normalized.StartsWith(BaseFull, ESearchCase::IgnoreCase))
        {
            Normalized = Normalized.RightChop(BaseFull.Len());
            return true;
        }
        return false;
    };

    if (TryRelative(FPaths::ProjectDir()))
    {
        return Normalized;
    }
    if (TryRelative(FPaths::EngineDir()))
    {
        return TEXT("Engine/") + Normalized;
    }
    return AbsolutePath;
}
