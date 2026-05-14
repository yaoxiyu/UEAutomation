#include "Application/TaskValidator.h"

#include "Core/AutomationWhitelist.h"
#include "Core/EditorAutomationSettings.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"

namespace
{
    FString StripExportPathDecorators(const FString& Path)
    {
        FString Result = Path.TrimStartAndEnd();
        int32 FirstQuote = INDEX_NONE;
        int32 LastQuote = INDEX_NONE;
        if (Result.FindChar(TEXT('\''), FirstQuote) && Result.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
        {
            Result = Result.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
        }
        return Result;
    }

    FString ObjectPathToPackagePath(const FString& ObjectPath)
    {
        FString Path = StripExportPathDecorators(ObjectPath);
        if (Path.IsEmpty())
        {
            return FString();
        }

        if (Path.EndsWith(TEXT("_C")))
        {
            Path.LeftChopInline(2);
        }

        int32 DotIndex = INDEX_NONE;
        if (Path.FindChar(TEXT('.'), DotIndex) && DotIndex > 0)
        {
            Path.LeftInline(DotIndex);
        }

        int32 SubObjectIndex = INDEX_NONE;
        if (Path.FindChar(TEXT(':'), SubObjectIndex) && SubObjectIndex > 0)
        {
            Path.LeftInline(SubObjectIndex);
        }

        return Path;
    }

    bool IsRootMatch(const FString& PackagePath, const FString& Root)
    {
        if (Root.IsEmpty())
        {
            return false;
        }
        if (PackagePath == Root)
        {
            return true;
        }
        return PackagePath.StartsWith(Root.EndsWith(TEXT("/")) ? Root : Root + TEXT("/"));
    }
}

bool FTaskValidator::ValidateCommon(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();

    if (Request.ProtocolVersion != Settings->SupportedProtocolVersion)
    {
        OutResult.AddError(TEXT("UnsupportedProtocolVersion"), TEXT("Unsupported protocol_version."), TEXT("protocol_version"));
        return false;
    }

    if (Request.TaskId.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("Missing task_id."), TEXT("task_id"));
        return false;
    }
    if (FPaths::GetCleanFilename(Request.TaskId) != Request.TaskId
        || Request.TaskId.Contains(TEXT(":"))
        || Request.TaskId.Contains(TEXT("*"))
        || Request.TaskId.Contains(TEXT("?"))
        || Request.TaskId.Contains(TEXT("\""))
        || Request.TaskId.Contains(TEXT("<"))
        || Request.TaskId.Contains(TEXT(">"))
        || Request.TaskId.Contains(TEXT("|")))
    {
        OutResult.AddError(TEXT("InvalidTaskId"), TEXT("task_id must be a safe file name stem."), TEXT("task_id"));
        return false;
    }

    const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();
    if (!Whitelist.bLoaded)
    {
        OutResult.AddError(TEXT("WhitelistLoadFailed"), Whitelist.LoadError, TEXT("security.whitelist"));
        return false;
    }

    if (Whitelist.AllowedTaskTypes.Num() > 0 && !Whitelist.AllowedTaskTypes.Contains(Request.TaskType))
    {
        OutResult.AddError(TEXT("InvalidTaskType"), FString::Printf(TEXT("Task type '%s' is not allowed."), *Request.TaskType), TEXT("task_type"));
        return false;
    }

    if (!ValidateAssetRoots(Request, OutResult))
    {
        return false;
    }

    return true;
}

bool FTaskValidator::ValidateAssetRoots(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const
{
    if (!ValidatePackagePathRoot(Request.Asset.PackagePath, TEXT("payload.asset.package_path"), OutResult))
    {
        return false;
    }
    if (!ValidatePackagePathRoot(Request.TargetAsset.PackagePath, TEXT("payload.target_asset.package_path"), OutResult))
    {
        return false;
    }
    if (!ValidatePackagePathRoot(Request.DestinationPackagePath, TEXT("payload.destination_package_path"), OutResult))
    {
        return false;
    }
    if (!ValidatePackagePathRoot(Request.DirectoryPath, TEXT("payload.directory_path"), OutResult))
    {
        return false;
    }

    if (!IsAnalysisOnlyTask(Request.TaskType))
    {
        if (!ValidateObjectPathRoot(Request.TargetAsset.AssetPath, TEXT("payload.target_asset.asset_path"), OutResult))
        {
            return false;
        }
        if (!ValidateObjectPathRoot(Request.SourceAssetPath, TEXT("payload.source_asset_path"), OutResult))
        {
            return false;
        }
        for (int32 Index = 0; Index < Request.AssetRedirects.Num(); ++Index)
        {
            const FAutomationAssetRedirect& Redirect = Request.AssetRedirects[Index];
            if (!ValidateObjectPathRoot(Redirect.From, FString::Printf(TEXT("payload.redirects[%d].from"), Index), OutResult))
            {
                return false;
            }
            if (!ValidateObjectPathRoot(Redirect.To, FString::Printf(TEXT("payload.redirects[%d].to"), Index), OutResult))
            {
                return false;
            }
        }
    }

    for (int32 Index = 0; Index < Request.TargetAssets.Num(); ++Index)
    {
        const FAutomationAssetSpec& Asset = Request.TargetAssets[Index];
        if (!ValidatePackagePathRoot(Asset.PackagePath, FString::Printf(TEXT("payload.target_assets[%d].package_path"), Index), OutResult))
        {
            return false;
        }
        if (!IsAnalysisOnlyTask(Request.TaskType)
            && !ValidateObjectPathRoot(Asset.AssetPath, FString::Printf(TEXT("payload.target_assets[%d].asset_path"), Index), OutResult))
        {
            return false;
        }
    }

    for (int32 Index = 0; Index < Request.BatchItems.Num(); ++Index)
    {
        if (!ValidatePackagePathRoot(Request.BatchItems[Index].Asset.PackagePath, FString::Printf(TEXT("payload.items[%d].asset.package_path"), Index), OutResult))
        {
            return false;
        }
    }

    return true;
}

bool FTaskValidator::ValidatePackagePathRoot(const FString& PackagePath, const FString& Field, FAutomationTaskResult& OutResult) const
{
    if (PackagePath.IsEmpty())
    {
        return true;
    }
    if (!FPackageName::IsValidLongPackageName(PackagePath))
    {
        OutResult.AddError(TEXT("InvalidPackagePath"), FString::Printf(TEXT("Invalid package path '%s'."), *PackagePath), Field);
        return false;
    }
    if (!IsAllowedAssetRoot(PackagePath))
    {
        OutResult.AddError(TEXT("AssetRootNotAllowed"), FString::Printf(TEXT("Package path '%s' is outside allowed roots."), *PackagePath), Field);
        return false;
    }
    return true;
}

bool FTaskValidator::ValidateObjectPathRoot(const FString& ObjectPath, const FString& Field, FAutomationTaskResult& OutResult) const
{
    if (ObjectPath.IsEmpty())
    {
        return true;
    }

    const FString PackagePath = ObjectPathToPackagePath(ObjectPath);
    if (PackagePath.IsEmpty() || !FPackageName::IsValidLongPackageName(PackagePath))
    {
        OutResult.AddError(TEXT("InvalidAssetPath"), FString::Printf(TEXT("Invalid asset path '%s'."), *ObjectPath), Field);
        return false;
    }
    if (!IsAllowedAssetRoot(PackagePath))
    {
        OutResult.AddError(TEXT("AssetRootNotAllowed"), FString::Printf(TEXT("Asset path '%s' is outside allowed roots."), *ObjectPath), Field);
        return false;
    }
    return true;
}

bool FTaskValidator::IsAnalysisOnlyTask(const FString& TaskType) const
{
    return TaskType == TEXT("analyze_blueprint")
        || TaskType == TEXT("analyze_blueprint_reference_chain")
        || TaskType == TEXT("analyze_asset")
        || TaskType == TEXT("analyze_behavior_tree")
        || TaskType == TEXT("refresh_blueprint_meta_cache")
        || TaskType == TEXT("export_blueprint_ai_context");
}

bool FTaskValidator::IsAllowedAssetRoot(const FString& PackagePath) const
{
    const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();
    if (!Whitelist.bLoaded)
    {
        return false;
    }

    if (Whitelist.AllowedAssetRoots.Num() == 0)
    {
        return true;
    }

    for (const FString& Root : Whitelist.AllowedAssetRoots)
    {
        if (IsRootMatch(PackagePath, Root))
        {
            return true;
        }
    }
    return false;
}
