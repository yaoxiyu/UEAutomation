#include "Domain/BlueprintAutomationService.h"

#include "Core/AutomationWhitelist.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"

FBlueprintAutomationService::FBlueprintAutomationService(const TSharedRef<IBlueprintEditorAdapter>& InBlueprintAdapter)
    : BlueprintAdapter(InBlueprintAdapter)
{
}

bool FBlueprintAutomationService::CreateBlueprint(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.Asset.AssetName.IsEmpty() || Request.Asset.PackagePath.IsEmpty() || Request.Asset.ParentClass.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("asset_name, package_path and parent_class are required."), TEXT("payload.asset"));
        return false;
    }

    if (BlueprintAdapter->DoesAssetExist(Request.Asset.PackagePath, Request.Asset.AssetName))
    {
        if (Request.Execution.bSkipIfExists)
        {
            OutResult.AddWarning(FString::Printf(TEXT("Asset '%s/%s' already exists; skipped."), *Request.Asset.PackagePath, *Request.Asset.AssetName));
            OutResult.bSuccess = true;
            OutResult.Status = TEXT("succeeded");
            return true;
        }

        OutResult.AddError(TEXT("AssetAlreadyExists"), FString::Printf(TEXT("Asset '%s/%s' already exists."), *Request.Asset.PackagePath, *Request.Asset.AssetName), TEXT("payload.asset"));
        return false;
    }

    UClass* ParentClass = LoadClassByPath(Request.Asset.ParentClass, OutResult, TEXT("payload.asset.parent_class"));
    if (!ParentClass)
    {
        return false;
    }

    FString Error;
    UBlueprint* Blueprint = BlueprintAdapter->CreateBlueprintAsset(Request.Asset.PackagePath, Request.Asset.AssetName, ParentClass, Error);
    if (!Blueprint)
    {
        OutResult.AddError(TEXT("BlueprintCreateFailed"), Error, TEXT("payload.asset"));
        return false;
    }

    if (!Request.RootComponent.ComponentName.IsEmpty())
    {
        if (!AddComponent(Blueprint, Request.RootComponent, OutResult, TEXT("payload.assembly.root_component")))
        {
            return false;
        }
    }

    for (int32 Index = 0; Index < Request.Components.Num(); ++Index)
    {
        if (!AddComponent(Blueprint, Request.Components[Index], OutResult, FString::Printf(TEXT("payload.assembly.components[%d]"), Index)))
        {
            return false;
        }
    }

    if (!CompileIfRequested(Blueprint, Request.Execution.bCompileAfterCreate, OutResult))
    {
        return false;
    }

    if (Request.ClassDefaults.Num() > 0)
    {
        UObject* CDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
        if (!PropertyAssignmentService.AssignProperties(CDO, Request.ClassDefaults, OutResult, TEXT("payload.class_default_overrides")))
        {
            return false;
        }
        if (!CompileIfRequested(Blueprint, true, OutResult))
        {
            return false;
        }
    }

    if (!SaveIfRequested(Blueprint, Request.Execution.bSaveAfterSuccess, OutResult))
    {
        return false;
    }

    if (Request.Execution.bOpenAfterSuccess && !BlueprintAdapter->OpenAsset(Blueprint, Error))
    {
        OutResult.AddError(TEXT("OpenAssetFailed"), Error);
        return false;
    }

    FAutomationAssetOutput Output;
    Output.AssetPath = FString::Printf(TEXT("%s/%s.%s"), *Request.Asset.PackagePath, *Request.Asset.AssetName, *Request.Asset.AssetName);
    Output.AssetName = Request.Asset.AssetName;
    Output.AssetType = TEXT("blueprint");
    OutResult.AssetOutputs.Add(Output);
    return true;
}

bool FBlueprintAutomationService::ModifyBlueprintComponents(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    FString Error;
    UBlueprint* Blueprint = BlueprintAdapter->LoadBlueprintAsset(Request.TargetAsset.AssetPath, Error);
    if (!Blueprint)
    {
        OutResult.AddError(TEXT("AssetNotFound"), Error, TEXT("payload.target_asset.asset_path"));
        return false;
    }

    for (int32 Index = 0; Index < Request.Operations.Num(); ++Index)
    {
        const FAutomationOperation& Operation = Request.Operations[Index];
        const FString FieldPrefix = FString::Printf(TEXT("payload.operations[%d]"), Index);
        if (Operation.Op == TEXT("add_component"))
        {
            if (!AddComponent(Blueprint, Operation.Component, OutResult, FieldPrefix))
            {
                return false;
            }
        }
        else if (Operation.Op == TEXT("update_component_properties"))
        {
            UObject* Template = BlueprintAdapter->GetComponentTemplate(Blueprint, Operation.Component.ComponentName, Error);
            if (!Template)
            {
                OutResult.AddError(TEXT("ComponentNotFound"), Error, FieldPrefix + TEXT(".component_name"));
                return false;
            }
            if (!PropertyAssignmentService.AssignProperties(Template, Operation.Properties, OutResult, FieldPrefix + TEXT(".properties")))
            {
                return false;
            }
        }
        else
        {
            OutResult.AddError(TEXT("InvalidOperation"), FString::Printf(TEXT("Unsupported operation '%s'."), *Operation.Op), FieldPrefix + TEXT(".op"));
            return false;
        }
    }

    if (!CompileSaveOpen(Blueprint, Request, OutResult))
    {
        return false;
    }

    AddTargetAssetOutput(Request, OutResult);
    return true;
}

bool FBlueprintAutomationService::ModifyBlueprintDefaults(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    FString Error;
    UBlueprint* Blueprint = BlueprintAdapter->LoadBlueprintAsset(Request.TargetAsset.AssetPath, Error);
    if (!Blueprint)
    {
        OutResult.AddError(TEXT("AssetNotFound"), Error, TEXT("payload.target_asset.asset_path"));
        return false;
    }

    if (!CompileIfRequested(Blueprint, true, OutResult))
    {
        return false;
    }

    UObject* CDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
    if (!PropertyAssignmentService.AssignProperties(CDO, Request.ClassDefaults, OutResult, TEXT("payload.class_defaults")))
    {
        return false;
    }

    if (!CompileSaveOpen(Blueprint, Request, OutResult))
    {
        return false;
    }

    AddTargetAssetOutput(Request, OutResult);
    return true;
}

UClass* FBlueprintAutomationService::LoadClassByPath(const FString& ClassPath, FAutomationTaskResult& OutResult, const FString& Field) const
{
    const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();
    if (!Whitelist.bLoaded)
    {
        OutResult.AddError(TEXT("WhitelistLoadFailed"), Whitelist.LoadError, TEXT("security.whitelist"));
        return nullptr;
    }

    if (Field.Contains(TEXT("parent_class")) && Whitelist.AllowedParentClasses.Num() > 0 && !Whitelist.AllowedParentClasses.Contains(ClassPath))
    {
        OutResult.AddError(TEXT("InvalidParentClass"), FString::Printf(TEXT("Parent class '%s' is not allowed."), *ClassPath), Field);
        return nullptr;
    }

    if (Field.Contains(TEXT("component_class")) && Whitelist.AllowedComponentClasses.Num() > 0 && !Whitelist.AllowedComponentClasses.Contains(ClassPath))
    {
        OutResult.AddError(TEXT("ComponentClassNotAllowed"), FString::Printf(TEXT("Component class '%s' is not allowed."), *ClassPath), Field);
        return nullptr;
    }

    UClass* LoadedClass = StaticLoadClass(UObject::StaticClass(), nullptr, *ClassPath);
    if (!LoadedClass)
    {
        OutResult.AddError(Field.Contains(TEXT("component_class")) ? TEXT("ComponentClassNotFound") : TEXT("InvalidParentClass"), FString::Printf(TEXT("Class '%s' not found."), *ClassPath), Field);
    }
    return LoadedClass;
}

bool FBlueprintAutomationService::AddComponent(UBlueprint* Blueprint, const FAutomationComponentSpec& Component, FAutomationTaskResult& OutResult, const FString& FieldPrefix)
{
    UClass* ComponentClass = LoadClassByPath(Component.ComponentClass, OutResult, FieldPrefix + TEXT(".component_class"));
    if (!ComponentClass)
    {
        return false;
    }

    FString Error;
    if (!BlueprintAdapter->AddComponentNode(Blueprint, ComponentClass, Component.ComponentName, Component.AttachParent, Error))
    {
        const FString Code = Error.Contains(TEXT("Duplicate")) ? TEXT("DuplicateComponentName") : TEXT("AttachParentNotFound");
        OutResult.AddError(Code, Error, FieldPrefix);
        return false;
    }
    OutResult.Metrics.ComponentCreateCount++;

    UObject* Template = BlueprintAdapter->GetComponentTemplate(Blueprint, Component.ComponentName, Error);
    if (!Template)
    {
        OutResult.AddError(TEXT("ComponentTemplateNotFound"), Error, FieldPrefix + TEXT(".component_name"));
        return false;
    }

    if (!BlueprintAdapter->ApplyComponentTransform(Template, Component.Transform, Error))
    {
        OutResult.AddError(TEXT("InvalidPropertyValue"), Error, FieldPrefix + TEXT(".transform"));
        return false;
    }

    return PropertyAssignmentService.AssignProperties(Template, Component.Properties, OutResult, FieldPrefix + TEXT(".properties"));
}

bool FBlueprintAutomationService::CompileSaveOpen(UBlueprint* Blueprint, const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (!CompileIfRequested(Blueprint, Request.PostActions.Contains(TEXT("compile_blueprint")) || Request.Execution.bCompileAfterCreate, OutResult))
    {
        return false;
    }
    return SaveIfRequested(Blueprint, Request.PostActions.Contains(TEXT("save_asset")) || Request.Execution.bSaveAfterSuccess, OutResult);
}

void FBlueprintAutomationService::AddTargetAssetOutput(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const
{
    if (Request.TargetAsset.AssetPath.IsEmpty())
    {
        return;
    }

    FAutomationAssetOutput Output;
    Output.AssetPath = Request.TargetAsset.AssetPath;
    Output.AssetType = TEXT("blueprint");

    int32 DotIndex = INDEX_NONE;
    if (Request.TargetAsset.AssetPath.FindLastChar(TEXT('.'), DotIndex))
    {
        Output.AssetName = Request.TargetAsset.AssetPath.Mid(DotIndex + 1);
    }
    else
    {
        int32 SlashIndex = INDEX_NONE;
        Output.AssetName = Request.TargetAsset.AssetPath.FindLastChar(TEXT('/'), SlashIndex)
            ? Request.TargetAsset.AssetPath.Mid(SlashIndex + 1)
            : Request.TargetAsset.AssetPath;
    }

    OutResult.AssetOutputs.Add(Output);
}

bool FBlueprintAutomationService::CompileIfRequested(UBlueprint* Blueprint, bool bCompile, FAutomationTaskResult& OutResult)
{
    if (!bCompile)
    {
        return true;
    }

    FString Error;
    const double StartSeconds = FPlatformTime::Seconds();
    if (!BlueprintAdapter->CompileBlueprint(Blueprint, Error))
    {
        OutResult.AddError(TEXT("BlueprintCompileFailed"), Error);
        return false;
    }
    OutResult.Metrics.CompileDurationMs += static_cast<int64>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
    return true;
}

bool FBlueprintAutomationService::SaveIfRequested(UBlueprint* Blueprint, bool bSave, FAutomationTaskResult& OutResult)
{
    if (!bSave)
    {
        return true;
    }

    FString Error;
    const double StartSeconds = FPlatformTime::Seconds();
    if (!BlueprintAdapter->SaveAsset(Blueprint, Error))
    {
        OutResult.AddError(TEXT("AssetSaveFailed"), Error);
        return false;
    }
    OutResult.Metrics.SaveDurationMs += static_cast<int64>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
    return true;
}
