#include "Application/BlueprintTaskExecutors.h"

#include "Core/AutomationWhitelist.h"
#include "Misc/PackageName.h"

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

bool ValidateTemplateOverrides(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    for (int32 Index = 0; Index < Request.ComponentOverrides.Num(); ++Index)
    {
        const FAutomationComponentOverride& Override = Request.ComponentOverrides[Index];
        const FString FieldPrefix = FString::Printf(TEXT("payload.overrides.component_overrides[%d]"), Index);

        if (Override.ComponentName.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("component_name is required."), FieldPrefix + TEXT(".component_name"));
            return false;
        }

        if (Override.Properties.Num() == 0
            && !Override.Transform.bHasLocation
            && !Override.Transform.bHasRotation
            && !Override.Transform.bHasScale)
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("component override must contain properties or transform."), FieldPrefix);
            return false;
        }
    }

    return true;
}

bool ValidateBatchItemOverrides(const TArray<FAutomationComponentOverride>& ComponentOverrides, FAutomationTaskResult& OutResult, const FString& FieldPrefix)
{
    for (int32 Index = 0; Index < ComponentOverrides.Num(); ++Index)
    {
        const FAutomationComponentOverride& Override = ComponentOverrides[Index];
        const FString OverrideFieldPrefix = FString::Printf(TEXT("%s.overrides.component_overrides[%d]"), *FieldPrefix, Index);

        if (Override.ComponentName.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("component_name is required."), OverrideFieldPrefix + TEXT(".component_name"));
            return false;
        }

        if (Override.Properties.Num() == 0
            && !Override.Transform.bHasLocation
            && !Override.Transform.bHasRotation
            && !Override.Transform.bHasScale)
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("component override must contain properties or transform."), OverrideFieldPrefix);
            return false;
        }
    }

    return true;
}

bool IsAllowedBatchPackagePath(const FString& PackagePath)
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
        if (PackagePath.StartsWith(Root))
        {
            return true;
        }
    }
    return false;
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

FCopyLiveBlueprintValuesTaskExecutor::FCopyLiveBlueprintValuesTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FCopyLiveBlueprintValuesTaskExecutor::GetTaskType() const
{
    return TEXT("copy_live_blueprint_values");
}

bool FCopyLiveBlueprintValuesTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.SourceAssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("source_asset_path is required."), TEXT("payload.source_asset_path"));
        return false;
    }
    if (Request.TargetAsset.AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required."), TEXT("payload.target_asset.asset_path"));
        return false;
    }
    if (Request.ClassDefaults.Num() == 0 && Request.Operations.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("class_defaults or operations must contain at least one live value copy request."), TEXT("payload"));
        return false;
    }

    for (int32 Index = 0; Index < Request.ClassDefaults.Num(); ++Index)
    {
        if (Request.ClassDefaults[Index].Name.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("property name is required."), FString::Printf(TEXT("payload.class_defaults[%d].name"), Index));
            return false;
        }
    }

    for (int32 Index = 0; Index < Request.Operations.Num(); ++Index)
    {
        const FAutomationOperation& Operation = Request.Operations[Index];
        const FString FieldPrefix = FString::Printf(TEXT("payload.operations[%d]"), Index);
        if (Operation.Op != TEXT("copy_component_properties") && Operation.Op != TEXT("update_component_properties"))
        {
            OutResult.AddError(TEXT("InvalidOperation"), FString::Printf(TEXT("Unsupported live copy operation '%s'."), *Operation.Op), FieldPrefix + TEXT(".op"));
            return false;
        }
        if (!ValidateComponentName(Operation.Component, OutResult, FieldPrefix))
        {
            return false;
        }
        if (Operation.Properties.Num() == 0)
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("properties must contain at least one property name."), FieldPrefix + TEXT(".properties"));
            return false;
        }
        for (int32 PropertyIndex = 0; PropertyIndex < Operation.Properties.Num(); ++PropertyIndex)
        {
            if (Operation.Properties[PropertyIndex].Name.IsEmpty())
            {
                OutResult.AddError(TEXT("MissingRequiredField"), TEXT("property name is required."), FString::Printf(TEXT("%s.properties[%d].name"), *FieldPrefix, PropertyIndex));
                return false;
            }
        }
    }

    return true;
}

bool FCopyLiveBlueprintValuesTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->CopyLiveBlueprintValues(Request, OutResult);
}

FCopyBlueprintLiveOverridesTaskExecutor::FCopyBlueprintLiveOverridesTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FCopyBlueprintLiveOverridesTaskExecutor::GetTaskType() const
{
    return TEXT("copy_blueprint_live_overrides");
}

bool FCopyBlueprintLiveOverridesTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.SourceAssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("source_asset_path is required."), TEXT("payload.source_asset_path"));
        return false;
    }
    if (Request.TargetAsset.AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required."), TEXT("payload.target_asset.asset_path"));
        return false;
    }
    return true;
}

bool FCopyBlueprintLiveOverridesTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->CopyBlueprintLiveOverrides(Request, OutResult);
}

FDiagnoseBlueprintPropertyPersistenceTaskExecutor::FDiagnoseBlueprintPropertyPersistenceTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FDiagnoseBlueprintPropertyPersistenceTaskExecutor::GetTaskType() const
{
    return TEXT("diagnose_blueprint_property_persistence");
}

bool FDiagnoseBlueprintPropertyPersistenceTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.TargetAsset.AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required."), TEXT("payload.target_asset.asset_path"));
        return false;
    }

    const bool bHasCDOProperty = Request.ClassDefaults.Num() == 1;
    const bool bHasComponentProperty = Request.Operations.Num() == 1
        && Request.Operations[0].Op == TEXT("update_component_properties")
        && !Request.Operations[0].Component.ComponentName.IsEmpty()
        && Request.Operations[0].Properties.Num() == 1;
    if (!bHasCDOProperty && !bHasComponentProperty)
    {
        OutResult.AddError(
            TEXT("MissingRequiredField"),
            TEXT("Provide exactly one class_defaults property or one update_component_properties operation with exactly one property."),
            TEXT("payload"));
        return false;
    }
    return true;
}

bool FDiagnoseBlueprintPropertyPersistenceTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    return Service->DiagnoseBlueprintPropertyPersistence(Request, OutResult);
}

FCreateBlueprintFromTemplateTaskExecutor::FCreateBlueprintFromTemplateTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FBlueprintTaskExecutorBase(InService)
{
}

FString FCreateBlueprintFromTemplateTaskExecutor::GetTaskType() const
{
    return TEXT("create_blueprint_from_template");
}

bool FCreateBlueprintFromTemplateTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
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
    if (Request.Template.TemplateId.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("template_id is required."), TEXT("payload.template.template_id"));
        return false;
    }
    return ValidateTemplateOverrides(Request, OutResult);
}

bool FCreateBlueprintFromTemplateTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    FAutomationTaskRequest ExpandedRequest;
    if (!BuildExpandedRequest(Request, ExpandedRequest, OutResult))
    {
        return false;
    }

    OutResult.AddLog(FString::Printf(TEXT("create_blueprint_from_template: expanded template %s"), *Request.Template.TemplateId));
    return Service->CreateBlueprint(ExpandedRequest, OutResult);
}

bool FCreateBlueprintFromTemplateTaskExecutor::BuildExpandedRequest(const FAutomationTaskRequest& Request, FAutomationTaskRequest& OutExpandedRequest, FAutomationTaskResult& OutResult) const
{
    FAutomationBlueprintTemplate BlueprintTemplate;
    FString Error;
    if (!TemplateRegistry.FindTemplate(Request.Template.TemplateId, BlueprintTemplate, Error))
    {
        const FString Code = Error.Contains(TEXT("could not be loaded")) || Error.Contains(TEXT("not valid JSON")) || Error.Contains(TEXT("missing templates")) || Error.Contains(TEXT("invalid template")) || Error.Contains(TEXT("duplicate template_id"))
            ? TEXT("TemplateRegistryLoadFailed")
            : TEXT("TemplateNotFound");
        OutResult.AddError(Code, Error, TEXT("payload.template.template_id"));
        return false;
    }

    if (BlueprintTemplate.ParentClass.IsEmpty() && Request.Asset.ParentClass.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("Template or request must provide parent_class."), TEXT("payload.asset.parent_class"));
        return false;
    }

    OutExpandedRequest = Request;
    OutExpandedRequest.TaskType = TEXT("create_blueprint");
    OutExpandedRequest.Asset.ParentClass = Request.Asset.ParentClass.IsEmpty() ? BlueprintTemplate.ParentClass : Request.Asset.ParentClass;
    OutExpandedRequest.RootComponent = BlueprintTemplate.RootComponent;
    OutExpandedRequest.Components = BlueprintTemplate.Components;
    OutExpandedRequest.ClassDefaults = BlueprintTemplate.ClassDefaults;
    MergeProperties(OutExpandedRequest.ClassDefaults, Request.ClassDefaults);

    return ApplyComponentOverrides(OutExpandedRequest, Request, OutResult);
}

bool FCreateBlueprintFromTemplateTaskExecutor::ApplyComponentOverrides(FAutomationTaskRequest& ExpandedRequest, const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const
{
    for (int32 OverrideIndex = 0; OverrideIndex < Request.ComponentOverrides.Num(); ++OverrideIndex)
    {
        const FAutomationComponentOverride& Override = Request.ComponentOverrides[OverrideIndex];
        FAutomationComponentSpec* TargetComponent = nullptr;

        if (ExpandedRequest.RootComponent.ComponentName == Override.ComponentName)
        {
            TargetComponent = &ExpandedRequest.RootComponent;
        }
        else
        {
            for (FAutomationComponentSpec& Component : ExpandedRequest.Components)
            {
                if (Component.ComponentName == Override.ComponentName)
                {
                    TargetComponent = &Component;
                    break;
                }
            }
        }

        if (!TargetComponent)
        {
            OutResult.AddError(
                TEXT("TemplateOverrideComponentNotFound"),
                FString::Printf(TEXT("Template '%s' does not contain component '%s'."), *Request.Template.TemplateId, *Override.ComponentName),
                FString::Printf(TEXT("payload.overrides.component_overrides[%d].component_name"), OverrideIndex));
            return false;
        }

        if (Override.Transform.bHasLocation)
        {
            TargetComponent->Transform.bHasLocation = true;
            TargetComponent->Transform.Location = Override.Transform.Location;
        }
        if (Override.Transform.bHasRotation)
        {
            TargetComponent->Transform.bHasRotation = true;
            TargetComponent->Transform.Rotation = Override.Transform.Rotation;
        }
        if (Override.Transform.bHasScale)
        {
            TargetComponent->Transform.bHasScale = true;
            TargetComponent->Transform.Scale = Override.Transform.Scale;
        }
        MergeProperties(TargetComponent->Properties, Override.Properties);
    }

    return true;
}

void FCreateBlueprintFromTemplateTaskExecutor::MergeProperties(TArray<FAutomationPropertyValue>& TargetProperties, const TArray<FAutomationPropertyValue>& OverrideProperties) const
{
    for (const FAutomationPropertyValue& OverrideProperty : OverrideProperties)
    {
        bool bReplaced = false;
        for (FAutomationPropertyValue& TargetProperty : TargetProperties)
        {
            if (TargetProperty.Name == OverrideProperty.Name)
            {
                TargetProperty = OverrideProperty;
                bReplaced = true;
                break;
            }
        }

        if (!bReplaced)
        {
            TargetProperties.Add(OverrideProperty);
        }
    }
}

FBatchCreateBlueprintsTaskExecutor::FBatchCreateBlueprintsTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService)
    : FCreateBlueprintFromTemplateTaskExecutor(InService)
{
}

FString FBatchCreateBlueprintsTaskExecutor::GetTaskType() const
{
    return TEXT("batch_create_blueprints");
}

bool FBatchCreateBlueprintsTaskExecutor::Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.BatchItems.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("items must contain at least one blueprint item."), TEXT("payload.items"));
        return false;
    }

    if (!ValidateTemplateOverrides(Request, OutResult))
    {
        return false;
    }

    if (Request.SharedTemplate.TemplateId.IsEmpty())
    {
        for (int32 Index = 0; Index < Request.BatchItems.Num(); ++Index)
        {
            if (Request.BatchItems[Index].Template.TemplateId.IsEmpty())
            {
                OutResult.AddError(TEXT("MissingRequiredField"), TEXT("shared_template or item template_id is required."), FString::Printf(TEXT("payload.items[%d].template.template_id"), Index));
                return false;
            }
        }
    }

    TSet<FString> AssetKeys;
    for (int32 Index = 0; Index < Request.BatchItems.Num(); ++Index)
    {
        const FAutomationBatchBlueprintItem& Item = Request.BatchItems[Index];
        const FString FieldPrefix = FString::Printf(TEXT("payload.items[%d]"), Index);

        if (Item.Asset.AssetName.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("asset_name is required."), FieldPrefix + TEXT(".asset.asset_name"));
            return false;
        }
        if (Item.Asset.PackagePath.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("package_path is required."), FieldPrefix + TEXT(".asset.package_path"));
            return false;
        }
        if (!FPackageName::IsValidLongPackageName(Item.Asset.PackagePath))
        {
            OutResult.AddError(TEXT("InvalidPackagePath"), FString::Printf(TEXT("Invalid package path '%s'."), *Item.Asset.PackagePath), FieldPrefix + TEXT(".asset.package_path"));
            return false;
        }
        if (!IsAllowedBatchPackagePath(Item.Asset.PackagePath))
        {
            OutResult.AddError(TEXT("AssetRootNotAllowed"), FString::Printf(TEXT("Package path '%s' is outside allowed roots."), *Item.Asset.PackagePath), FieldPrefix + TEXT(".asset.package_path"));
            return false;
        }

        const FString AssetKey = Item.Asset.PackagePath / Item.Asset.AssetName;
        if (AssetKeys.Contains(AssetKey))
        {
            OutResult.AddError(TEXT("DuplicateBatchAsset"), FString::Printf(TEXT("Duplicate batch asset '%s'."), *AssetKey), FieldPrefix + TEXT(".asset"));
            return false;
        }
        AssetKeys.Add(AssetKey);

        if (!ValidateBatchItemOverrides(Item.ComponentOverrides, OutResult, FieldPrefix))
        {
            return false;
        }
    }

    return true;
}

bool FBatchCreateBlueprintsTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    TArray<FAutomationTaskRequest> ExpandedRequests;
    for (const FAutomationBatchBlueprintItem& Item : Request.BatchItems)
    {
        FAutomationTaskRequest ItemRequest = BuildItemRequest(Request, Item);
        FAutomationTaskRequest ExpandedRequest;
        if (!BuildExpandedRequest(ItemRequest, ExpandedRequest, OutResult))
        {
            return false;
        }
        ExpandedRequests.Add(ExpandedRequest);
    }

    for (int32 Index = 0; Index < ExpandedRequests.Num(); ++Index)
    {
        const FAutomationTaskRequest& ExpandedRequest = ExpandedRequests[Index];
        if (Service->DoesAssetExist(ExpandedRequest.Asset.PackagePath, ExpandedRequest.Asset.AssetName))
        {
            if (ExpandedRequest.Execution.bSkipIfExists)
            {
                continue;
            }

            if (ExpandedRequest.Execution.bOverwriteIfExists)
            {
                OutResult.AddError(TEXT("OverwriteNotSupported"), TEXT("overwrite_if_exists is not supported for batch_create_blueprints in Phase 2."), FString::Printf(TEXT("payload.items[%d].asset"), Index));
                return false;
            }

            OutResult.AddError(
                TEXT("AssetAlreadyExists"),
                FString::Printf(TEXT("Asset '%s/%s' already exists."), *ExpandedRequest.Asset.PackagePath, *ExpandedRequest.Asset.AssetName),
                FString::Printf(TEXT("payload.items[%d].asset"), Index));
            return false;
        }
    }

    for (int32 Index = 0; Index < ExpandedRequests.Num(); ++Index)
    {
        OutResult.AddLog(FString::Printf(TEXT("batch_create_blueprints: create item %d/%d"), Index + 1, ExpandedRequests.Num()));
        if (!Service->CreateBlueprint(ExpandedRequests[Index], OutResult))
        {
            return false;
        }
    }

    return true;
}

FAutomationTaskRequest FBatchCreateBlueprintsTaskExecutor::BuildItemRequest(const FAutomationTaskRequest& BatchRequest, const FAutomationBatchBlueprintItem& Item) const
{
    FAutomationTaskRequest ItemRequest = BatchRequest;
    ItemRequest.TaskType = TEXT("create_blueprint_from_template");
    ItemRequest.Asset = Item.Asset;
    ItemRequest.Template = Item.Template.TemplateId.IsEmpty() ? BatchRequest.SharedTemplate : Item.Template;
    ItemRequest.ComponentOverrides = BatchRequest.ComponentOverrides;
    ItemRequest.ComponentOverrides.Append(Item.ComponentOverrides);
    ItemRequest.ClassDefaults = BatchRequest.ClassDefaults;
    MergeProperties(ItemRequest.ClassDefaults, Item.ClassDefaults);
    ItemRequest.BatchItems.Reset();
    return ItemRequest;
}
