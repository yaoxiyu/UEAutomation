#include "Application/BlueprintTaskExecutors.h"

namespace
{
bool ValidateComponentName(const FAutomationComponentSpec& Component, FAutomationTaskResult& OutResult, const FString& FieldPrefix)
{
    if (Component.ComponentName.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("component_name is required."), FieldPrefix + TEXT(".component_name"));
        return false;
    }
    return true;
}

bool ValidateComponentClass(const FAutomationComponentSpec& Component, FAutomationTaskResult& OutResult, const FString& FieldPrefix)
{
    if (Component.ComponentClass.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("component_class is required."), FieldPrefix + TEXT(".component_class"));
        return false;
    }
    return true;
}

bool ValidateCreateComponentTree(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    TSet<FString> KnownComponents;

    if (!Request.RootComponent.ComponentName.IsEmpty())
    {
        if (!ValidateComponentClass(Request.RootComponent, OutResult, TEXT("payload.assembly.root_component")))
        {
            return false;
        }
        KnownComponents.Add(Request.RootComponent.ComponentName);
    }

    for (int32 Index = 0; Index < Request.Components.Num(); ++Index)
    {
        const FAutomationComponentSpec& Component = Request.Components[Index];
        const FString FieldPrefix = FString::Printf(TEXT("payload.assembly.components[%d]"), Index);

        if (!ValidateComponentName(Component, OutResult, FieldPrefix) || !ValidateComponentClass(Component, OutResult, FieldPrefix))
        {
            return false;
        }

        if (KnownComponents.Contains(Component.ComponentName))
        {
            OutResult.AddError(TEXT("DuplicateComponentName"), FString::Printf(TEXT("Duplicate component name '%s'."), *Component.ComponentName), FieldPrefix + TEXT(".component_name"));
            return false;
        }

        if (!Component.AttachParent.IsEmpty() && !KnownComponents.Contains(Component.AttachParent))
        {
            OutResult.AddError(TEXT("AttachParentNotFound"), FString::Printf(TEXT("Attach parent '%s' is not declared before component '%s'."), *Component.AttachParent, *Component.ComponentName), FieldPrefix + TEXT(".attach_parent"));
            return false;
        }

        KnownComponents.Add(Component.ComponentName);
    }

    return true;
}

bool ValidateModifyComponentOperations(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.Operations.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("operations must contain at least one operation."), TEXT("payload.operations"));
        return false;
    }

    for (int32 Index = 0; Index < Request.Operations.Num(); ++Index)
    {
        const FAutomationOperation& Operation = Request.Operations[Index];
        const FString FieldPrefix = FString::Printf(TEXT("payload.operations[%d]"), Index);

        if (Operation.Op.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("op is required."), FieldPrefix + TEXT(".op"));
            return false;
        }

        if (Operation.Op == TEXT("add_component"))
        {
            if (!ValidateComponentName(Operation.Component, OutResult, FieldPrefix) || !ValidateComponentClass(Operation.Component, OutResult, FieldPrefix))
            {
                return false;
            }
        }
        else if (Operation.Op == TEXT("update_component_properties"))
        {
            if (!ValidateComponentName(Operation.Component, OutResult, FieldPrefix))
            {
                return false;
            }
            if (Operation.Properties.Num() == 0)
            {
                OutResult.AddError(TEXT("MissingRequiredField"), TEXT("properties must contain at least one property."), FieldPrefix + TEXT(".properties"));
                return false;
            }
        }
    }

    return true;
}
}

FBlueprintTaskExecutorBase::FBlueprintTaskExecutorBase(const TSharedRef<FBlueprintAutomationService>& InService)
    : Service(InService)
{
}

bool FBlueprintTaskExecutorBase::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.TaskType == TEXT("create_blueprint"))
    {
        if (Request.Asset.AssetName.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("asset_name is required."), TEXT("payload.asset.asset_name"));
            return false;
        }
        if (Request.Asset.PackagePath.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("package_path is required."), TEXT("payload.asset.package_path"));
            return false;
        }
        if (Request.Asset.ParentClass.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("parent_class is required."), TEXT("payload.asset.parent_class"));
            return false;
        }
        return ValidateCreateComponentTree(Request, OutResult);
    }

    if (Request.TargetAsset.AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required."), TEXT("payload.target_asset.asset_path"));
        return false;
    }

    if (Request.TaskType == TEXT("modify_blueprint_components"))
    {
        return ValidateModifyComponentOperations(Request, OutResult);
    }

    if (Request.TaskType == TEXT("modify_blueprint_defaults") && Request.ClassDefaults.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("class_defaults must contain at least one property."), TEXT("payload.class_defaults"));
        return false;
    }

    return true;
}

FCreateBlueprintTaskExecutor::FCreateBlueprintTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FCreateBlueprintTaskExecutor::GetTaskType() const
{
    return TEXT("create_blueprint");
}

bool FCreateBlueprintTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->CreateBlueprint(Request, OutResult);
}

FModifyBlueprintComponentsTaskExecutor::FModifyBlueprintComponentsTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FModifyBlueprintComponentsTaskExecutor::GetTaskType() const
{
    return TEXT("modify_blueprint_components");
}

bool FModifyBlueprintComponentsTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->ModifyBlueprintComponents(Request, OutResult);
}

FModifyBlueprintDefaultsTaskExecutor::FModifyBlueprintDefaultsTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FModifyBlueprintDefaultsTaskExecutor::GetTaskType() const
{
    return TEXT("modify_blueprint_defaults");
}

bool FModifyBlueprintDefaultsTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->ModifyBlueprintDefaults(Request, OutResult);
}
