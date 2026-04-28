#include "Application/TaskValidator.h"

#include "Core/EditorAutomationSettings.h"
#include "Misc/PackageName.h"

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

    if (!Settings->AllowedTaskTypes.Contains(Request.TaskType))
    {
        OutResult.AddError(TEXT("InvalidTaskType"), FString::Printf(TEXT("Task type '%s' is not allowed."), *Request.TaskType), TEXT("task_type"));
        return false;
    }

    const FString PackagePath = !Request.Asset.PackagePath.IsEmpty() ? Request.Asset.PackagePath : Request.TargetAsset.PackagePath;
    if (!PackagePath.IsEmpty() && !IsAllowedAssetRoot(PackagePath))
    {
        OutResult.AddError(TEXT("AssetRootNotAllowed"), FString::Printf(TEXT("Package path '%s' is outside allowed roots."), *PackagePath), TEXT("payload.asset.package_path"));
        return false;
    }

    if (!Request.Asset.PackagePath.IsEmpty() && !FPackageName::IsValidLongPackageName(Request.Asset.PackagePath))
    {
        OutResult.AddError(TEXT("InvalidPackagePath"), FString::Printf(TEXT("Invalid package path '%s'."), *Request.Asset.PackagePath), TEXT("payload.asset.package_path"));
        return false;
    }

    return true;
}

bool FTaskValidator::IsAllowedAssetRoot(const FString& PackagePath) const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    for (const FString& Root : Settings->AllowedAssetRoots)
    {
        if (PackagePath.StartsWith(Root))
        {
            return true;
        }
    }
    return false;
}
