#include "Domain/CppSourceResolver.h"

#include "Domain/NativeParentClassResolver.h"
#include "HAL/FileManager.h"
#include "Misc/Paths.h"
#include "SourceCodeNavigation.h"
#include "UObject/Class.h"

namespace
{
    FString StripNativePrefix(const FString& Name)
    {
        if (Name.Len() < 2)
        {
            return Name;
        }
        const TCHAR First = Name[0];
        if ((First == TEXT('A') || First == TEXT('U') || First == TEXT('F') || First == TEXT('S') || First == TEXT('I') || First == TEXT('E') || First == TEXT('T'))
            && FChar::IsUpper(Name[1]))
        {
            return Name.RightChop(1);
        }
        return Name;
    }

    void CollectCandidateRoots(TArray<FString>& OutRoots)
    {
        IFileManager& FileManager = IFileManager::Get();
        OutRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source")));
        OutRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Plugins")));
        OutRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Source/Runtime")));
        OutRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Source/Editor")));
        OutRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Source/Developer")));
        OutRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Plugins")));
        OutRoots.RemoveAll([&](const FString& Path)
        {
            return !FileManager.DirectoryExists(*Path);
        });
    }

    bool ScanForFile(const FString& Root, const FString& Filename, FString& OutFound)
    {
        TArray<FString> Found;
        IFileManager::Get().FindFilesRecursive(Found, *Root, *Filename, true, false);
        if (Found.Num() == 0)
        {
            return false;
        }
        OutFound = Found[0];
        return true;
    }
}

bool FCppSourceResolver::TryClassMetadata(UClass* NativeClass, FString& OutHeaderPath)
{
    if (!NativeClass)
    {
        return false;
    }
    static const FName NAME_ModuleRelativePath(TEXT("ModuleRelativePath"));
    static const FName NAME_IncludePath(TEXT("IncludePath"));

    const FString Module = FNativeParentClassResolver::DeriveModuleNameFromClassPath(NativeClass->GetPathName());
    if (Module.IsEmpty())
    {
        return false;
    }

    const FString ModuleRelative = NativeClass->GetMetaData(NAME_ModuleRelativePath);
    const FString IncludePath = NativeClass->GetMetaData(NAME_IncludePath);
    const FString Relative = !ModuleRelative.IsEmpty() ? ModuleRelative : IncludePath;
    if (Relative.IsEmpty())
    {
        return false;
    }

    TArray<FString> CandidateRoots;
    CandidateRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Source") / Module));
    CandidateRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Source/Runtime") / Module));
    CandidateRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Source/Editor") / Module));
    CandidateRoots.Add(FPaths::ConvertRelativePathToFull(FPaths::EngineDir() / TEXT("Source/Developer") / Module));

    IFileManager& FileManager = IFileManager::Get();
    for (const FString& Root : CandidateRoots)
    {
        const FString Candidate = FPaths::ConvertRelativePathToFull(Root / Relative);
        if (FileManager.FileExists(*Candidate))
        {
            OutHeaderPath = Candidate;
            return true;
        }
    }

    // Fallback: search inside Project Plugins for any plugin that contains a module with this name.
    const FString PluginRoot = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir() / TEXT("Plugins"));
    if (FileManager.DirectoryExists(*PluginRoot))
    {
        TArray<FString> Found;
        FileManager.FindFilesRecursive(Found, *PluginRoot, *Relative.Replace(TEXT("\\"), TEXT("/")), true, false);
        for (const FString& Path : Found)
        {
            if (Path.Contains(FString::Printf(TEXT("/%s/"), *Module)))
            {
                OutHeaderPath = Path;
                return true;
            }
        }
        if (Found.Num() > 0)
        {
            OutHeaderPath = Found[0];
            return true;
        }
    }

    return false;
}

bool FCppSourceResolver::TrySourceCodeNavigation(UClass* NativeClass, FString& OutHeaderPath)
{
    if (!NativeClass)
    {
        return false;
    }
    return FSourceCodeNavigation::FindClassHeaderPath(NativeClass, OutHeaderPath);
}

bool FCppSourceResolver::TryScanSearchRoots(
    const FString& ModuleName,
    const FString& ClassNameNoPrefix,
    FString& OutHeaderPath,
    FString& OutCppPath)
{
    TArray<FString> Roots;
    CollectCandidateRoots(Roots);

    const FString HeaderName = ClassNameNoPrefix + TEXT(".h");
    const FString CppName = ClassNameNoPrefix + TEXT(".cpp");

    bool bFoundHeader = false;
    bool bFoundCpp = false;
    for (const FString& Root : Roots)
    {
        const FString ModuleRoot = ModuleName.IsEmpty() ? Root : FPaths::ConvertRelativePathToFull(Root / ModuleName);
        const FString SearchRoot = IFileManager::Get().DirectoryExists(*ModuleRoot) ? ModuleRoot : Root;

        if (!bFoundHeader && ScanForFile(SearchRoot, HeaderName, OutHeaderPath))
        {
            bFoundHeader = true;
        }
        if (!bFoundCpp && ScanForFile(SearchRoot, CppName, OutCppPath))
        {
            bFoundCpp = true;
        }
        if (bFoundHeader && bFoundCpp)
        {
            break;
        }
    }
    return bFoundHeader;
}

bool FCppSourceResolver::Resolve(UClass* NativeClass, FAutomationCppSourceLocation& OutLocation, FString& OutError) const
{
    if (!NativeClass)
    {
        OutError = TEXT("NativeClass is null");
        OutLocation.SourceStatus = TEXT("unresolved");
        return false;
    }

    OutLocation.ModuleName = FNativeParentClassResolver::DeriveModuleNameFromClassPath(NativeClass->GetPathName());

    FString HeaderPath;
    bool bFound = TryClassMetadata(NativeClass, HeaderPath);
    if (!bFound)
    {
        bFound = TrySourceCodeNavigation(NativeClass, HeaderPath);
    }

    FString ClassName;
    NativeClass->GetName(ClassName);
    const FString NoPrefix = StripNativePrefix(ClassName);

    FString CppPath;
    if (!bFound)
    {
        bFound = TryScanSearchRoots(OutLocation.ModuleName, NoPrefix, HeaderPath, CppPath);
    }
    else if (!HeaderPath.IsEmpty())
    {
        // Try to find sibling .cpp by replacing /Public/ with /Private/ or by scanning the module.
        FString HeaderDir = FPaths::GetPath(HeaderPath);
        FString CppCandidate = HeaderDir / (NoPrefix + TEXT(".cpp"));
        if (IFileManager::Get().FileExists(*CppCandidate))
        {
            CppPath = CppCandidate;
        }
        else
        {
            FString CppFromPublic = HeaderPath;
            CppFromPublic.ReplaceInline(TEXT("/Public/"), TEXT("/Private/"));
            CppFromPublic.ReplaceInline(TEXT(".h"), TEXT(".cpp"));
            if (IFileManager::Get().FileExists(*CppFromPublic))
            {
                CppPath = CppFromPublic;
            }
            else
            {
                FString FoundCpp;
                if (ScanForFile(FPaths::GetPath(HeaderDir), NoPrefix + TEXT(".cpp"), FoundCpp))
                {
                    CppPath = FoundCpp;
                }
            }
        }
    }

    OutLocation.HeaderPath = HeaderPath;
    OutLocation.CppPath = CppPath;
    if (HeaderPath.IsEmpty() && CppPath.IsEmpty())
    {
        OutError = TEXT("CppSourceResolveFailed");
        OutLocation.SourceStatus = TEXT("unresolved");
        return false;
    }
    if (CppPath.IsEmpty())
    {
        OutLocation.SourceStatus = TEXT("header_only");
        return true;
    }
    OutLocation.SourceStatus = TEXT("resolved");
    return true;
}
