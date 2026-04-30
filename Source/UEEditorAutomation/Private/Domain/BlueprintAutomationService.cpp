#include "Domain/BlueprintAutomationService.h"

#include "Core/AutomationWhitelist.h"
#include "Components/ActorComponent.h"
#include "Components/PrimitiveComponent.h"
#include "Domain/BlueprintSnapshotExporter.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "PhysicsEngine/BodyInstance.h"
#include "UObject/UnrealType.h"

namespace
{
    void MarkBlueprintDefaultDataModified(UBlueprint* Blueprint)
    {
        if (!Blueprint)
        {
            return;
        }

        Blueprint->Modify();
        FBlueprintEditorUtils::MarkBlueprintAsModified(Blueprint);
        Blueprint->MarkPackageDirty();

        if (UClass* GeneratedClass = Blueprint->GeneratedClass)
        {
            GeneratedClass->MarkPackageDirty();
            if (UObject* CDO = GeneratedClass->GetDefaultObject())
            {
                CDO->MarkPackageDirty();
            }
        }
    }

    FString TruncateForResultMessage(const FString& Text)
    {
        constexpr int32 MaxChars = 512;
        return Text.Len() <= MaxChars
            ? Text
            : Text.Left(MaxChars) + TEXT("...<truncated>");
    }

    FString DescribeDiagnosticProperty(FProperty* Property)
    {
        if (!Property)
        {
            return TEXT("<null property>");
        }

        FString Description = FString::Printf(TEXT("%s '%s'"), *Property->GetClass()->GetName(), *Property->GetName());
        if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            Description += FString::Printf(
                TEXT(" struct='%s' path='%s'"),
                StructProperty->Struct ? *StructProperty->Struct->GetName() : TEXT("<null>"),
                StructProperty->Struct ? *StructProperty->Struct->GetPathName() : TEXT("<null>"));
        }
        return Description;
    }

    bool IsDelegateProperty(FProperty* Property)
    {
        return CastField<FDelegateProperty>(Property)
            || CastField<FMulticastDelegateProperty>(Property)
            || CastField<FMulticastInlineDelegateProperty>(Property)
            || CastField<FMulticastSparseDelegateProperty>(Property);
    }

    bool IsComponentObjectReferenceProperty(FProperty* Property)
    {
        const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property);
        return ObjectProperty
            && ObjectProperty->PropertyClass
            && ObjectProperty->PropertyClass->IsChildOf(UActorComponent::StaticClass());
    }

    bool IsMeaningfulLiveOverrideCandidate(FProperty* Property, const TArray<FString>& DeniedExportNames)
    {
        if (!Property)
        {
            return false;
        }
        for (const FString& DeniedName : DeniedExportNames)
        {
            if (DeniedName.Equals(Property->GetName(), ESearchCase::IgnoreCase))
            {
                return false;
            }
        }
        if (IsDelegateProperty(Property) || IsComponentObjectReferenceProperty(Property))
        {
            return false;
        }

        constexpr EPropertyFlags SkippedFlags =
            CPF_Transient
            | CPF_DuplicateTransient
            | CPF_NonPIEDuplicateTransient
            | CPF_Deprecated
            | CPF_EditorOnly;
        if (Property->HasAnyPropertyFlags(SkippedFlags))
        {
            return false;
        }

        return Property->HasAnyPropertyFlags(CPF_Edit);
    }

    bool JsonBoolField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, bool bDefault = false)
    {
        bool bValue = bDefault;
        return Object.IsValid() && Object->TryGetBoolField(FieldName, bValue) ? bValue : bDefault;
    }

    FString JsonStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
    {
        FString Value;
        if (Object.IsValid())
        {
            Object->TryGetStringField(FieldName, Value);
        }
        return Value;
    }

    FString CollisionResponsesToStableText(const UPrimitiveComponent* PrimitiveComponent)
    {
        if (!PrimitiveComponent)
        {
            return FString();
        }

        TArray<FString> Parts;
        for (int32 ChannelIndex = 0; ChannelIndex < static_cast<int32>(ECC_MAX); ++ChannelIndex)
        {
            const ECollisionChannel Channel = static_cast<ECollisionChannel>(ChannelIndex);
            Parts.Add(FString::Printf(TEXT("%d:%d"), ChannelIndex, static_cast<int32>(PrimitiveComponent->GetCollisionResponseToChannel(Channel))));
        }
        return FString::Join(Parts, TEXT(","));
    }
}

FBlueprintAutomationService::FBlueprintAutomationService(const TSharedRef<IBlueprintEditorAdapter>& InBlueprintAdapter)
    : BlueprintAdapter(InBlueprintAdapter)
{
}

bool FBlueprintAutomationService::CreateBlueprint(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    OutResult.AddLog(TEXT("create_blueprint: validate asset fields"));
    if (Request.Asset.AssetName.IsEmpty() || Request.Asset.PackagePath.IsEmpty() || Request.Asset.ParentClass.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("asset_name, package_path and parent_class are required."), TEXT("payload.asset"));
        return false;
    }

    OutResult.AddLog(FString::Printf(TEXT("create_blueprint: check existing asset %s/%s"), *Request.Asset.PackagePath, *Request.Asset.AssetName));
    if (BlueprintAdapter->DoesAssetExist(Request.Asset.PackagePath, Request.Asset.AssetName))
    {
        if (Request.Execution.bSkipIfExists)
        {
            OutResult.AddWarning(FString::Printf(TEXT("Asset '%s/%s' already exists; skipped."), *Request.Asset.PackagePath, *Request.Asset.AssetName));
            OutResult.AddLog(TEXT("create_blueprint: existing asset skipped by idempotency policy"));
            AddCreatedAssetOutput(Request, OutResult);
            return true;
        }

        if (Request.Execution.bOverwriteIfExists)
        {
            OutResult.AddError(TEXT("OverwriteNotSupported"), TEXT("overwrite_if_exists is not supported for create_blueprint in Phase 1."), TEXT("execution.overwrite_if_exists"));
            return false;
        }

        OutResult.AddError(TEXT("AssetAlreadyExists"), FString::Printf(TEXT("Asset '%s/%s' already exists."), *Request.Asset.PackagePath, *Request.Asset.AssetName), TEXT("payload.asset"));
        return false;
    }

    OutResult.AddLog(FString::Printf(TEXT("create_blueprint: load parent class %s"), *Request.Asset.ParentClass));
    UClass* ParentClass = LoadClassByPath(Request.Asset.ParentClass, OutResult, TEXT("payload.asset.parent_class"));
    if (!ParentClass)
    {
        return false;
    }

    FString Error;
    OutResult.AddLog(TEXT("create_blueprint: create Blueprint asset"));
    UBlueprint* Blueprint = BlueprintAdapter->CreateBlueprintAsset(Request.Asset.PackagePath, Request.Asset.AssetName, ParentClass, Error);
    if (!Blueprint)
    {
        OutResult.AddError(TEXT("BlueprintCreateFailed"), Error, TEXT("payload.asset"));
        return false;
    }

    if (!Request.RootComponent.ComponentName.IsEmpty())
    {
        OutResult.AddLog(FString::Printf(TEXT("create_blueprint: add root component %s"), *Request.RootComponent.ComponentName));
        if (!AddComponent(Blueprint, Request.RootComponent, OutResult, TEXT("payload.assembly.root_component")))
        {
            return false;
        }
    }

    for (int32 Index = 0; Index < Request.Components.Num(); ++Index)
    {
        OutResult.AddLog(FString::Printf(TEXT("create_blueprint: add component %s"), *Request.Components[Index].ComponentName));
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
        OutResult.AddLog(FString::Printf(TEXT("create_blueprint: assign %d class default properties"), Request.ClassDefaults.Num()));
        UObject* CDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
        if (!PropertyAssignmentService.AssignProperties(CDO, Request.ClassDefaults, OutResult, TEXT("payload.class_default_overrides")))
        {
            return false;
        }
        AddPropertyFieldResults(OutResult, TEXT("CDO"), TEXT("class_default_object"), Request.ClassDefaults, TEXT("create_blueprint"));
        MarkBlueprintDefaultDataModified(Blueprint);
        if (!CompileIfRequested(Blueprint, Request.Execution.bCompileAfterCreate, OutResult))
        {
            return false;
        }
    }

    if (!Request.RootComponent.ComponentName.IsEmpty() || Request.Components.Num() > 0)
    {
        MarkBlueprintDefaultDataModified(Blueprint);
    }

    if (!SaveIfRequested(Blueprint, Request.Execution.bSaveAfterSuccess, OutResult))
    {
        return false;
    }

    if (Request.Execution.bOpenAfterSuccess && !BlueprintAdapter->OpenAsset(Blueprint, Error))
    {
        OutResult.AddLog(TEXT("blueprint: open asset"));
        OutResult.AddError(TEXT("OpenAssetFailed"), Error);
        return false;
    }
    if (Request.Execution.bOpenAfterSuccess)
    {
        OutResult.AddLog(TEXT("blueprint: open asset"));
    }

    AddCreatedAssetOutput(Request, OutResult);
    return true;
}

bool FBlueprintAutomationService::ModifyBlueprintComponents(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    FString Error;
    OutResult.AddLog(FString::Printf(TEXT("modify_blueprint_components: load target asset %s"), *Request.TargetAsset.AssetPath));
    UBlueprint* Blueprint = BlueprintAdapter->LoadBlueprintAsset(Request.TargetAsset.AssetPath, Error);
    if (!Blueprint)
    {
        OutResult.AddError(TEXT("AssetNotFound"), Error, TEXT("payload.target_asset.asset_path"));
        return false;
    }

    struct FComponentSnapshotBatch
    {
        FString ComponentName;
        FString LookupPolicy;
        TArray<FAssignedPropertySnapshot> Snapshots;
    };

    TArray<FComponentSnapshotBatch> SnapshotBatches;
    bool bModifiedBlueprintData = false;
    for (int32 Index = 0; Index < Request.Operations.Num(); ++Index)
    {
        const FAutomationOperation& Operation = Request.Operations[Index];
        const FString FieldPrefix = FString::Printf(TEXT("payload.operations[%d]"), Index);
        if (Operation.Op == TEXT("add_component"))
        {
            OutResult.AddLog(FString::Printf(TEXT("modify_blueprint_components: add component %s"), *Operation.Component.ComponentName));
            if (!AddComponent(Blueprint, Operation.Component, OutResult, FieldPrefix))
            {
                return false;
            }
            bModifiedBlueprintData = true;
        }
        else if (Operation.Op == TEXT("update_component_properties"))
        {
            OutResult.AddLog(FString::Printf(TEXT("modify_blueprint_components: update properties for %s"), *Operation.Component.ComponentName));
            const FString LookupPolicy = ResolveComponentLookupPolicy(Operation);
            const FString WriteTarget = ResolveComponentWriteTarget(Operation);
            UObject* Template = BlueprintAdapter->GetComponentTemplate(Blueprint, Operation.Component.ComponentName, LookupPolicy, Error);
            if (!Template)
            {
                OutResult.AddError(TEXT("ComponentNotFound"), Error, FieldPrefix + TEXT(".component_name"));
                return false;
            }
            OutResult.AddLog(FString::Printf(
                TEXT("modify_blueprint_components: resolved %s via %s as %s (template=%s outer=%s)"),
                *Operation.Component.ComponentName,
                *LookupPolicy,
                *WriteTarget,
                *Template->GetPathName(),
                Template->GetOuter() ? *Template->GetOuter()->GetPathName() : TEXT("<none>")));
            if (!PropertyAssignmentService.AssignProperties(Template, Operation.Properties, OutResult, FieldPrefix + TEXT(".properties"), Request.AssetRedirects))
            {
                return false;
            }
            AddPropertyFieldResults(OutResult, TEXT("Component.") + Operation.Component.ComponentName, WriteTarget, Operation.Properties, Operation.TargetKind);
            FComponentSnapshotBatch SnapshotBatch;
            SnapshotBatch.ComponentName = Operation.Component.ComponentName;
            SnapshotBatch.LookupPolicy = LookupPolicy;
            if (!CapturePropertySnapshots(
                Template,
                TEXT("Component.") + Operation.Component.ComponentName,
                Operation.Properties,
                FieldPrefix + TEXT(".properties"),
                Request.AssetRedirects,
                SnapshotBatch.Snapshots,
                OutResult))
            {
                return false;
            }
            SnapshotBatches.Add(MoveTemp(SnapshotBatch));
            bModifiedBlueprintData = true;
        }
        else
        {
            OutResult.AddError(TEXT("InvalidOperation"), FString::Printf(TEXT("Unsupported operation '%s'."), *Operation.Op), FieldPrefix + TEXT(".op"));
            return false;
        }
    }

    if (bModifiedBlueprintData)
    {
        MarkBlueprintDefaultDataModified(Blueprint);
    }

    if (!CompileSaveOpen(Blueprint, Request, OutResult))
    {
        return false;
    }

    for (const FComponentSnapshotBatch& SnapshotBatch : SnapshotBatches)
    {
        UObject* Template = BlueprintAdapter->GetComponentTemplate(Blueprint, SnapshotBatch.ComponentName, SnapshotBatch.LookupPolicy, Error);
        if (!Template)
        {
            OutResult.AddError(TEXT("ComponentPersistenceVerifyFailed"), Error, TEXT("post_write.component"));
            return false;
        }
        if (!VerifyPropertySnapshots(Template, SnapshotBatch.Snapshots, OutResult))
        {
            return false;
        }
    }

    AddTargetAssetOutput(Request, OutResult);
    return true;
}

bool FBlueprintAutomationService::ModifyBlueprintDefaults(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    FString Error;
    OutResult.AddLog(FString::Printf(TEXT("modify_blueprint_defaults: load target asset %s"), *Request.TargetAsset.AssetPath));
    UBlueprint* Blueprint = BlueprintAdapter->LoadBlueprintAsset(Request.TargetAsset.AssetPath, Error);
    if (!Blueprint)
    {
        OutResult.AddError(TEXT("AssetNotFound"), Error, TEXT("payload.target_asset.asset_path"));
        return false;
    }

    if (!CompileIfRequested(Blueprint, Request.Execution.bCompileAfterCreate, OutResult))
    {
        return false;
    }

    UObject* CDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
    OutResult.AddLog(FString::Printf(TEXT("modify_blueprint_defaults: assign %d class default properties"), Request.ClassDefaults.Num()));
    if (!PropertyAssignmentService.AssignProperties(CDO, Request.ClassDefaults, OutResult, TEXT("payload.class_defaults"), Request.AssetRedirects))
    {
        return false;
    }
    AddPropertyFieldResults(OutResult, TEXT("CDO"), TEXT("class_default_object"), Request.ClassDefaults, TEXT("modify_blueprint_defaults"));
    TArray<FAssignedPropertySnapshot> DefaultSnapshots;
    if (!CapturePropertySnapshots(CDO, TEXT("CDO"), Request.ClassDefaults, TEXT("payload.class_defaults"), Request.AssetRedirects, DefaultSnapshots, OutResult))
    {
        return false;
    }
    MarkBlueprintDefaultDataModified(Blueprint);

    if (!CompileSaveOpen(Blueprint, Request, OutResult))
    {
        return false;
    }

    UObject* PostWriteCDO = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
    if (!VerifyPropertySnapshots(PostWriteCDO, DefaultSnapshots, OutResult))
    {
        return false;
    }

    AddTargetAssetOutput(Request, OutResult);
    return true;
}

bool FBlueprintAutomationService::CopyLiveBlueprintValues(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    FString Error;
    OutResult.AddLog(FString::Printf(TEXT("copy_live_blueprint_values: load source asset %s"), *Request.SourceAssetPath));
    UBlueprint* SourceBlueprint = BlueprintAdapter->LoadBlueprintAsset(Request.SourceAssetPath, Error);
    if (!SourceBlueprint)
    {
        OutResult.AddError(TEXT("AssetNotFound"), Error, TEXT("payload.source_asset_path"));
        return false;
    }

    OutResult.AddLog(FString::Printf(TEXT("copy_live_blueprint_values: load target asset %s"), *Request.TargetAsset.AssetPath));
    UBlueprint* TargetBlueprint = BlueprintAdapter->LoadBlueprintAsset(Request.TargetAsset.AssetPath, Error);
    if (!TargetBlueprint)
    {
        OutResult.AddError(TEXT("AssetNotFound"), Error, TEXT("payload.target_asset.asset_path"));
        return false;
    }

    if (!CompileIfRequested(TargetBlueprint, Request.Execution.bCompileAfterCreate, OutResult))
    {
        return false;
    }

    TArray<FAssignedPropertySnapshot> DefaultSnapshots;
    UObject* SourceCDO = SourceBlueprint->GeneratedClass ? SourceBlueprint->GeneratedClass->GetDefaultObject() : nullptr;
    UObject* TargetCDO = TargetBlueprint->GeneratedClass ? TargetBlueprint->GeneratedClass->GetDefaultObject() : nullptr;
    if (Request.ClassDefaults.Num() > 0)
    {
        if (!SourceCDO || !TargetCDO)
        {
            OutResult.AddError(TEXT("BlueprintCDOUnavailable"), TEXT("Source or target GeneratedClass has no CDO."), TEXT("payload.class_defaults"));
            return false;
        }

        TArray<FAutomationPropertyValue> LiveProperties;
        for (int32 Index = 0; Index < Request.ClassDefaults.Num(); ++Index)
        {
            FAutomationPropertyValue LiveProperty;
            if (!BuildLiveImportProperty(SourceCDO, Request.ClassDefaults[Index].Name, LiveProperty, Error))
            {
                OutResult.AddError(TEXT("LivePropertyReadFailed"), Error, FString::Printf(TEXT("payload.class_defaults[%d]"), Index));
                return false;
            }
            LiveProperties.Add(LiveProperty);
        }

        if (!PropertyAssignmentService.AssignProperties(TargetCDO, LiveProperties, OutResult, TEXT("payload.class_defaults"), Request.AssetRedirects))
        {
            return false;
        }
        AddPropertyFieldResults(OutResult, TEXT("CDO"), TEXT("class_default_object"), LiveProperties, TEXT("copy_live_blueprint_values"));
        if (!CapturePropertySnapshots(TargetCDO, TEXT("CDO"), LiveProperties, TEXT("payload.class_defaults"), Request.AssetRedirects, DefaultSnapshots, OutResult))
        {
            return false;
        }
        MarkBlueprintDefaultDataModified(TargetBlueprint);
    }

    struct FComponentSnapshotBatch
    {
        FString ComponentName;
        FString TargetLookupPolicy;
        TArray<FAssignedPropertySnapshot> Snapshots;
    };

    TArray<FComponentSnapshotBatch> ComponentSnapshots;
    for (int32 OperationIndex = 0; OperationIndex < Request.Operations.Num(); ++OperationIndex)
    {
        const FAutomationOperation& Operation = Request.Operations[OperationIndex];
        const FString FieldPrefix = FString::Printf(TEXT("payload.operations[%d]"), OperationIndex);
        const FString SourceLookupPolicy = ResolveSourceComponentLookupPolicy(Operation);
        const FString TargetLookupPolicy = ResolveComponentLookupPolicy(Operation);
        const FString WriteTarget = ResolveComponentWriteTarget(Operation);

        UObject* SourceTemplate = BlueprintAdapter->GetComponentTemplate(SourceBlueprint, Operation.Component.ComponentName, SourceLookupPolicy, Error);
        if (!SourceTemplate)
        {
            OutResult.AddError(TEXT("ComponentNotFound"), Error, FieldPrefix + TEXT(".component_name"));
            return false;
        }
        UObject* TargetTemplate = BlueprintAdapter->GetComponentTemplate(TargetBlueprint, Operation.Component.ComponentName, TargetLookupPolicy, Error);
        if (!TargetTemplate)
        {
            OutResult.AddError(TEXT("ComponentNotFound"), Error, FieldPrefix + TEXT(".component_name"));
            return false;
        }

        TArray<FAutomationPropertyValue> LiveProperties;
        FComponentSnapshotBatch SnapshotBatch;
        SnapshotBatch.ComponentName = Operation.Component.ComponentName;
        SnapshotBatch.TargetLookupPolicy = TargetLookupPolicy;
        for (int32 PropertyIndex = 0; PropertyIndex < Operation.Properties.Num(); ++PropertyIndex)
        {
            bool bSpecialHandled = false;
            const FString PropertyFieldPrefix = FString::Printf(TEXT("%s.properties[%d]"), *FieldPrefix, PropertyIndex);
            if (!TryCopySpecialLiveProperty(
                SourceTemplate,
                TargetTemplate,
                Operation.Properties[PropertyIndex].Name,
                TEXT("Component.") + Operation.Component.ComponentName,
                WriteTarget,
                PropertyFieldPrefix,
                OutResult,
                SnapshotBatch.Snapshots,
                bSpecialHandled))
            {
                return false;
            }
            if (bSpecialHandled)
            {
                continue;
            }

            FAutomationPropertyValue LiveProperty;
            if (!BuildLiveImportProperty(SourceTemplate, Operation.Properties[PropertyIndex].Name, LiveProperty, Error))
            {
                OutResult.AddError(TEXT("LivePropertyReadFailed"), Error, PropertyFieldPrefix);
                return false;
            }
            LiveProperties.Add(LiveProperty);
        }

        OutResult.AddLog(FString::Printf(
            TEXT("copy_live_blueprint_values: copy component %s source=%s target=%s write_target=%s"),
            *Operation.Component.ComponentName,
            *SourceLookupPolicy,
            *TargetLookupPolicy,
            *WriteTarget));

        if (LiveProperties.Num() > 0 && !PropertyAssignmentService.AssignProperties(TargetTemplate, LiveProperties, OutResult, FieldPrefix + TEXT(".properties"), Request.AssetRedirects))
        {
            return false;
        }
        AddPropertyFieldResults(OutResult, TEXT("Component.") + Operation.Component.ComponentName, WriteTarget, LiveProperties, TEXT("copy_live_blueprint_values"));

        if (LiveProperties.Num() > 0 && !CapturePropertySnapshots(
            TargetTemplate,
            TEXT("Component.") + Operation.Component.ComponentName,
            LiveProperties,
            FieldPrefix + TEXT(".properties"),
            Request.AssetRedirects,
            SnapshotBatch.Snapshots,
            OutResult))
        {
            return false;
        }
        ComponentSnapshots.Add(MoveTemp(SnapshotBatch));
        MarkBlueprintDefaultDataModified(TargetBlueprint);
    }

    if (!CompileSaveOpen(TargetBlueprint, Request, OutResult))
    {
        return false;
    }

    UObject* PostWriteCDO = TargetBlueprint->GeneratedClass ? TargetBlueprint->GeneratedClass->GetDefaultObject() : nullptr;
    if (!VerifyPropertySnapshots(PostWriteCDO, DefaultSnapshots, OutResult))
    {
        return false;
    }

    for (const FComponentSnapshotBatch& SnapshotBatch : ComponentSnapshots)
    {
        UObject* Template = BlueprintAdapter->GetComponentTemplate(TargetBlueprint, SnapshotBatch.ComponentName, SnapshotBatch.TargetLookupPolicy, Error);
        if (!Template)
        {
            OutResult.AddError(TEXT("ComponentPersistenceVerifyFailed"), Error, TEXT("post_write.component"));
            return false;
        }
        if (!VerifyPropertySnapshots(Template, SnapshotBatch.Snapshots, OutResult))
        {
            return false;
        }
    }

    AddTargetAssetOutput(Request, OutResult);
    return true;
}

bool FBlueprintAutomationService::CopyBlueprintLiveOverrides(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    FString Error;
    OutResult.AddLog(FString::Printf(TEXT("copy_blueprint_live_overrides: load source asset %s"), *Request.SourceAssetPath));
    UBlueprint* SourceBlueprint = BlueprintAdapter->LoadBlueprintAsset(Request.SourceAssetPath, Error);
    if (!SourceBlueprint)
    {
        OutResult.AddError(TEXT("AssetNotFound"), Error, TEXT("payload.source_asset_path"));
        return false;
    }

    FAutomationTaskRequest CopyRequest;
    if (!BuildLiveOverrideCopyRequest(Request, SourceBlueprint, CopyRequest, OutResult))
    {
        return false;
    }

    OutResult.AddLog(FString::Printf(
        TEXT("copy_blueprint_live_overrides: planned %d CDO properties and %d component operations"),
        CopyRequest.ClassDefaults.Num(),
        CopyRequest.Operations.Num()));

    if (CopyRequest.ClassDefaults.Num() == 0 && CopyRequest.Operations.Num() == 0)
    {
        OutResult.AddWarning(TEXT("No live overrides found to copy."));
        AddTargetAssetOutput(Request, OutResult);
        return true;
    }

    return CopyLiveBlueprintValues(CopyRequest, OutResult);
}

bool FBlueprintAutomationService::DiagnoseBlueprintPropertyPersistence(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    FString Error;
    OutResult.AddLog(FString::Printf(TEXT("diagnose_blueprint_property_persistence: load target asset %s"), *Request.TargetAsset.AssetPath));
    UBlueprint* Blueprint = BlueprintAdapter->LoadBlueprintAsset(Request.TargetAsset.AssetPath, Error);
    if (!Blueprint)
    {
        OutResult.AddError(TEXT("AssetNotFound"), Error, TEXT("payload.target_asset.asset_path"));
        return false;
    }

    const bool bComponentTarget = Request.Operations.Num() == 1;
    FAutomationPropertyValue Property;
    FString OwnerPath;
    FString WriteTarget;
    FString LookupPolicy;
    FString ComponentName;

    UObject* Target = nullptr;
    if (bComponentTarget)
    {
        const FAutomationOperation& Operation = Request.Operations[0];
        ComponentName = Operation.Component.ComponentName;
        Property = Operation.Properties[0];
        LookupPolicy = ResolveComponentLookupPolicy(Operation);
        WriteTarget = ResolveComponentWriteTarget(Operation);
        OwnerPath = TEXT("Component.") + ComponentName;
        Target = BlueprintAdapter->GetComponentTemplate(Blueprint, ComponentName, LookupPolicy, Error);
        if (!Target)
        {
            OutResult.AddError(TEXT("ComponentNotFound"), Error, TEXT("payload.operations[0].component_name"));
            return false;
        }
    }
    else
    {
        Property = Request.ClassDefaults[0];
        OwnerPath = TEXT("CDO");
        WriteTarget = TEXT("class_default_object");
        Target = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
        if (!Target)
        {
            OutResult.AddError(TEXT("BlueprintCDOUnavailable"), TEXT("GeneratedClass has no CDO."), TEXT("payload.target_asset.asset_path"));
            return false;
        }
    }

    FString BeforeText;
    if (!AddDiagnosticReadback(OutResult, Target, Property.Name, TEXT("before_assign"), BeforeText))
    {
        return false;
    }

    FString ExpectedImportText;
    bool bExpectedRawImportText = false;
    FString ExpectedError;
    FString ExpectedAssignedText;
    FProperty* DiagnosticProperty = Target->GetClass()->FindPropertyByName(FName(*Property.Name));
    if (!PropertyAssignmentService.BuildExpectedImportText(DiagnosticProperty, Property, Request.AssetRedirects, ExpectedImportText, bExpectedRawImportText, ExpectedError))
    {
        OutResult.AddError(TEXT("DiagnosticExpectedValueFailed"), ExpectedError, TEXT("payload.diagnostic_property"));
        return false;
    }
    if (!PropertyAssignmentService.BuildExpectedAssignedPropertyText(DiagnosticProperty, Property, Request.AssetRedirects, Target, ExpectedAssignedText, ExpectedError))
    {
        OutResult.AddError(TEXT("DiagnosticExpectedValueFailed"), ExpectedError, TEXT("payload.diagnostic_property"));
        return false;
    }
    OutResult.AddFieldResult(
        OwnerPath + TEXT(".") + Property.Name,
        TEXT("diagnostic_expected"),
        WriteTarget,
        Property.Type,
        TEXT("diagnose_blueprint_property_persistence"),
        FString::Printf(
            TEXT("property=%s, import='%s', expected_export='%s'"),
            *DescribeDiagnosticProperty(DiagnosticProperty),
            *TruncateForResultMessage(ExpectedImportText),
            *TruncateForResultMessage(ExpectedAssignedText)));

    if (!PropertyAssignmentService.AssignProperty(Target, Property, OutResult, TEXT("payload.diagnostic_property"), Request.AssetRedirects))
    {
        return false;
    }
    MarkBlueprintDefaultDataModified(Blueprint);

    FString AfterAssignText;
    if (!AddDiagnosticReadback(OutResult, Target, Property.Name, TEXT("after_assign"), AfterAssignText))
    {
        return false;
    }

    if (!CompileIfRequested(Blueprint, true, OutResult))
    {
        return false;
    }

    UObject* PostCompileTarget = nullptr;
    if (bComponentTarget)
    {
        PostCompileTarget = BlueprintAdapter->GetComponentTemplate(Blueprint, ComponentName, LookupPolicy, Error);
        if (!PostCompileTarget)
        {
            OutResult.AddError(TEXT("ComponentNotFoundAfterCompile"), Error, TEXT("payload.operations[0].component_name"));
            return false;
        }
    }
    else
    {
        PostCompileTarget = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : nullptr;
    }

    FString AfterCompileText;
    if (!AddDiagnosticReadback(OutResult, PostCompileTarget, Property.Name, TEXT("after_compile"), AfterCompileText))
    {
        return false;
    }

    if (!SaveIfRequested(Blueprint, true, OutResult))
    {
        return false;
    }

    FString AfterSaveText;
    if (!AddDiagnosticReadback(OutResult, PostCompileTarget, Property.Name, TEXT("after_save"), AfterSaveText))
    {
        return false;
    }

    UBlueprint* ReloadedBlueprint = BlueprintAdapter->LoadBlueprintAsset(Request.TargetAsset.AssetPath, Error);
    if (!ReloadedBlueprint)
    {
        OutResult.AddError(TEXT("AssetReloadFailed"), Error, TEXT("payload.target_asset.asset_path"));
        return false;
    }

    UObject* ReloadedTarget = nullptr;
    if (bComponentTarget)
    {
        ReloadedTarget = BlueprintAdapter->GetComponentTemplate(ReloadedBlueprint, ComponentName, LookupPolicy, Error);
        if (!ReloadedTarget)
        {
            OutResult.AddError(TEXT("ComponentNotFoundAfterReload"), Error, TEXT("payload.operations[0].component_name"));
            return false;
        }
    }
    else
    {
        ReloadedTarget = ReloadedBlueprint->GeneratedClass ? ReloadedBlueprint->GeneratedClass->GetDefaultObject() : nullptr;
    }

    FString AfterReloadText;
    if (!AddDiagnosticReadback(OutResult, ReloadedTarget, Property.Name, TEXT("after_reload"), AfterReloadText))
    {
        return false;
    }

    FString FinalStatus = TEXT("persisted");
    if (AfterAssignText != ExpectedAssignedText)
    {
        FinalStatus = TEXT("not_assigned");
    }
    else if (AfterAssignText != AfterCompileText)
    {
        FinalStatus = TEXT("lost_after_compile");
    }
    else if (AfterCompileText != AfterSaveText)
    {
        FinalStatus = TEXT("lost_after_save");
    }
    else if (AfterSaveText != AfterReloadText)
    {
        FinalStatus = TEXT("lost_after_reload");
    }

    OutResult.AddFieldResult(
        OwnerPath + TEXT(".") + Property.Name,
        FinalStatus,
        WriteTarget,
        Property.Type,
        TEXT("diagnose_blueprint_property_persistence"),
        FString::Printf(
            TEXT("before='%s', after_assign='%s', after_compile='%s', after_save='%s', after_reload='%s'"),
            *TruncateForResultMessage(BeforeText),
            *TruncateForResultMessage(AfterAssignText),
            *TruncateForResultMessage(AfterCompileText),
            *TruncateForResultMessage(AfterSaveText),
            *TruncateForResultMessage(AfterReloadText)));

    AddTargetAssetOutput(Request, OutResult);
    return true;
}

bool FBlueprintAutomationService::DoesAssetExist(const FString& PackagePath, const FString& AssetName) const
{
    return BlueprintAdapter->DoesAssetExist(PackagePath, AssetName);
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
    OutResult.AddLog(FString::Printf(TEXT("component: load class %s"), *Component.ComponentClass));
    UClass* ComponentClass = LoadClassByPath(Component.ComponentClass, OutResult, FieldPrefix + TEXT(".component_class"));
    if (!ComponentClass)
    {
        return false;
    }

    FString Error;
    OutResult.AddLog(FString::Printf(TEXT("component: create SCS node %s"), *Component.ComponentName));
    if (!BlueprintAdapter->AddComponentNode(Blueprint, ComponentClass, Component.ComponentName, Component.AttachParent, Error))
    {
        const FString Code = Error.Contains(TEXT("Duplicate")) ? TEXT("DuplicateComponentName") : TEXT("AttachParentNotFound");
        OutResult.AddError(Code, Error, FieldPrefix);
        return false;
    }

    UObject* Template = BlueprintAdapter->GetComponentTemplate(Blueprint, Component.ComponentName, TEXT("scs_first"), Error);
    if (!Template)
    {
        OutResult.AddError(TEXT("ComponentTemplateNotFound"), Error, FieldPrefix + TEXT(".component_name"));
        BlueprintAdapter->RemoveComponentNode(Blueprint, Component.ComponentName, Error);
        return false;
    }

    if (!BlueprintAdapter->ApplyComponentTransform(Template, Component.Transform, Error))
    {
        OutResult.AddError(TEXT("InvalidPropertyValue"), Error, FieldPrefix + TEXT(".transform"));
        BlueprintAdapter->RemoveComponentNode(Blueprint, Component.ComponentName, Error);
        return false;
    }

    OutResult.AddLog(FString::Printf(TEXT("component: assign %d properties to %s"), Component.Properties.Num(), *Component.ComponentName));
    if (!PropertyAssignmentService.AssignProperties(Template, Component.Properties, OutResult, FieldPrefix + TEXT(".properties")))
    {
        OutResult.AddLog(FString::Printf(TEXT("component: rollback SCS node %s after property assignment failure"), *Component.ComponentName));
        BlueprintAdapter->RemoveComponentNode(Blueprint, Component.ComponentName, Error);
        return false;
    }
    AddPropertyFieldResults(OutResult, TEXT("Component.") + Component.ComponentName, TEXT("own_scs_template"), Component.Properties, TEXT("add_component"));

    OutResult.Metrics.ComponentCreateCount++;
    return true;
}

bool FBlueprintAutomationService::BuildLiveOverrideCopyRequest(const FAutomationTaskRequest& Request, UBlueprint* SourceBlueprint, FAutomationTaskRequest& OutCopyRequest, FAutomationTaskResult& OutResult) const
{
    const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();
    if (!Whitelist.bLoaded)
    {
        OutResult.AddError(TEXT("WhitelistLoadFailed"), Whitelist.LoadError, TEXT("security.whitelist"));
        return false;
    }

    OutCopyRequest = Request;
    OutCopyRequest.TaskType = TEXT("copy_live_blueprint_values");
    OutCopyRequest.ClassDefaults.Reset();
    OutCopyRequest.Operations.Reset();

    AppendLiveDefaultOverrideNames(SourceBlueprint, Whitelist.DeniedPropertyNamesForExport, OutCopyRequest.ClassDefaults);
    if (!AppendLiveComponentOverrideOperations(SourceBlueprint, Whitelist.DeniedPropertyNamesForExport, OutCopyRequest.Operations, OutResult))
    {
        return false;
    }

    return true;
}

void FBlueprintAutomationService::AppendLiveDefaultOverrideNames(UBlueprint* SourceBlueprint, const TArray<FString>& DeniedExportNames, TArray<FAutomationPropertyValue>& OutProperties) const
{
    UClass* GeneratedClass = SourceBlueprint ? SourceBlueprint->GeneratedClass : nullptr;
    UObject* CDO = GeneratedClass ? GeneratedClass->GetDefaultObject() : nullptr;
    UClass* ParentClass = GeneratedClass ? GeneratedClass->GetSuperClass() : nullptr;
    UObject* ParentCDO = ParentClass ? ParentClass->GetDefaultObject() : nullptr;
    if (!CDO || !ParentCDO)
    {
        return;
    }

    for (TFieldIterator<FProperty> It(CDO->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        if (!IsMeaningfulLiveOverrideCandidate(Property, DeniedExportNames))
        {
            continue;
        }
        FProperty* ParentProperty = ParentCDO->GetClass()->FindPropertyByName(Property->GetFName());
        if (!ParentProperty || ParentProperty->GetClass() != Property->GetClass())
        {
            continue;
        }

        const void* ValuePtr = Property->ContainerPtrToValuePtr<void>(CDO);
        const void* ParentValuePtr = ParentProperty->ContainerPtrToValuePtr<void>(ParentCDO);
        if (Property->Identical(ValuePtr, ParentValuePtr, PPF_None))
        {
            continue;
        }

        FAutomationPropertyValue PropertyName;
        PropertyName.Name = Property->GetName();
        OutProperties.Add(PropertyName);
    }
}

bool FBlueprintAutomationService::AppendLiveComponentOverrideOperations(UBlueprint* SourceBlueprint, const TArray<FString>& DeniedExportNames, TArray<FAutomationOperation>& OutOperations, FAutomationTaskResult& OutResult) const
{
    FAutomationAnalysisOptions Options;
    Options.bForceRefresh = true;
    Options.bUseCache = false;
    Options.bIncludeNativeCxx = false;
    Options.bIncludeBlueprintSnapshot = true;
    Options.bIncludeClassDefaults = false;
    Options.bIncludeComponents = true;
    Options.bIncludeReferences = false;
    Options.bIncludeReferencers = false;
    Options.bIncludeGraphSummary = false;
    Options.bExportOnlyEditableProperties = true;

    const TSharedRef<FJsonObject> SnapshotJson = MakeShared<FJsonObject>();
    FBlueprintSnapshotExporter SnapshotExporter;
    if (!SnapshotExporter.ExportBlueprintSnapshot(SourceBlueprint, Options, DeniedExportNames, SnapshotJson, OutResult))
    {
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* Components = nullptr;
    if (!SnapshotJson->TryGetArrayField(TEXT("components"), Components) || !Components)
    {
        return true;
    }

    for (const TSharedPtr<FJsonValue>& ComponentValue : *Components)
    {
        const TSharedPtr<FJsonObject> ComponentObject = ComponentValue.IsValid() ? ComponentValue->AsObject() : nullptr;
        if (!ComponentObject.IsValid())
        {
            continue;
        }

        TArray<FAutomationPropertyValue> CandidateProperties;
        const TArray<TSharedPtr<FJsonValue>>* PropertyValues = nullptr;
        if (ComponentObject->TryGetArrayField(TEXT("properties"), PropertyValues) && PropertyValues)
        {
            for (const TSharedPtr<FJsonValue>& PropertyValue : *PropertyValues)
            {
                const TSharedPtr<FJsonObject> PropertyObject = PropertyValue.IsValid() ? PropertyValue->AsObject() : nullptr;
                if (!PropertyObject.IsValid() || !JsonBoolField(PropertyObject, TEXT("differs_from_parent")))
                {
                    continue;
                }

                FAutomationPropertyValue PropertyName;
                PropertyName.Name = JsonStringField(PropertyObject, TEXT("name"));
                if (!PropertyName.Name.IsEmpty())
                {
                    CandidateProperties.Add(PropertyName);
                }
            }
        }

        if (CandidateProperties.Num() == 0)
        {
            continue;
        }

        const FString OwnerKind = JsonStringField(ComponentObject, TEXT("owner_kind"));
        FAutomationOperation Operation;
        Operation.Op = TEXT("copy_component_properties");
        Operation.Component.ComponentName = JsonStringField(ComponentObject, TEXT("component_name"));
        if (OwnerKind == TEXT("native"))
        {
            Operation.TargetKind = TEXT("native_template");
        }
        else if (OwnerKind == TEXT("scs_inherited"))
        {
            Operation.TargetKind = TEXT("scs_inherited_override");
        }
        else if (OwnerKind == TEXT("scs_owned"))
        {
            Operation.TargetKind = TEXT("own_scs_template");
        }
        else
        {
            Operation.ComponentLookupPolicy = TEXT("scs_first");
        }

        FString Error;
        UObject* SourceTemplate = BlueprintAdapter->GetComponentTemplate(SourceBlueprint, Operation.Component.ComponentName, ResolveSourceComponentLookupPolicy(Operation), Error);
        if (!SourceTemplate)
        {
            OutResult.AddWarning(FString::Printf(TEXT("Live component override scan skipped '%s': %s"), *Operation.Component.ComponentName, *Error));
            continue;
        }

        for (const FAutomationPropertyValue& CandidateProperty : CandidateProperties)
        {
            FProperty* Property = SourceTemplate->GetClass()->FindPropertyByName(FName(*CandidateProperty.Name));
            if (IsMeaningfulLiveOverrideCandidate(Property, DeniedExportNames))
            {
                Operation.Properties.Add(CandidateProperty);
            }
        }

        if (Operation.Properties.Num() == 0)
        {
            continue;
        }

        if (!Operation.Component.ComponentName.IsEmpty())
        {
            OutOperations.Add(MoveTemp(Operation));
        }
    }

    return true;
}

FString FBlueprintAutomationService::ResolveComponentLookupPolicy(const FAutomationOperation& Operation) const
{
    const FString TargetKind = Operation.TargetKind.ToLower();
    if (TargetKind == TEXT("native_template") || TargetKind == TEXT("native"))
    {
        return TEXT("native_first");
    }
    if (TargetKind == TEXT("own_scs_template") || TargetKind == TEXT("scs_owned"))
    {
        return TEXT("scs_only");
    }
    if (TargetKind == TEXT("scs_inherited_override") || TargetKind == TEXT("inheritable_component_handler"))
    {
        return TEXT("scs_inherited_override");
    }
    return Operation.ComponentLookupPolicy.IsEmpty() ? TEXT("scs_first") : Operation.ComponentLookupPolicy;
}

FString FBlueprintAutomationService::ResolveSourceComponentLookupPolicy(const FAutomationOperation& Operation) const
{
    const FString TargetKind = Operation.TargetKind.ToLower();
    if (TargetKind == TEXT("native_template") || TargetKind == TEXT("native"))
    {
        return TEXT("native_first");
    }
    if (TargetKind == TEXT("own_scs_template") || TargetKind == TEXT("scs_owned"))
    {
        return TEXT("scs_only");
    }
    if (TargetKind == TEXT("scs_inherited_override") || TargetKind == TEXT("inheritable_component_handler"))
    {
        return TEXT("scs_first");
    }
    return Operation.ComponentLookupPolicy.IsEmpty() ? TEXT("scs_first") : Operation.ComponentLookupPolicy;
}

FString FBlueprintAutomationService::ResolveComponentWriteTarget(const FAutomationOperation& Operation) const
{
    if (!Operation.TargetKind.IsEmpty())
    {
        return Operation.TargetKind;
    }
    const FString LookupPolicy = Operation.ComponentLookupPolicy.ToLower();
    if (LookupPolicy == TEXT("native_first") || LookupPolicy == TEXT("native_only"))
    {
        return TEXT("native_template");
    }
    if (LookupPolicy == TEXT("scs_inherited_override"))
    {
        return TEXT("scs_inherited_override");
    }
    return TEXT("component_template");
}

bool FBlueprintAutomationService::BuildLiveImportProperty(UObject* Source, const FString& PropertyName, FAutomationPropertyValue& OutProperty, FString& OutError) const
{
    OutProperty = FAutomationPropertyValue();
    OutProperty.Name = PropertyName;
    OutProperty.Type = TEXT("import_text");

    if (!Source)
    {
        OutError = TEXT("Source object is null.");
        return false;
    }
    if (PropertyName.IsEmpty())
    {
        OutError = TEXT("Property name is empty.");
        return false;
    }

    FString ExportedText;
    if (!PropertyAssignmentService.ExportAssignedPropertyText(Source, PropertyName, ExportedText, OutError))
    {
        return false;
    }

    OutProperty.Value = MakeShared<FJsonValueString>(ExportedText);
    return true;
}

bool FBlueprintAutomationService::TryCopySpecialLiveProperty(UObject* Source, UObject* Target, const FString& PropertyName, const FString& OwnerPath, const FString& WriteTarget, const FString& FieldPrefix, FAutomationTaskResult& OutResult, TArray<FAssignedPropertySnapshot>& OutSnapshots, bool& bOutHandled) const
{
    bOutHandled = false;
    if (PropertyName != TEXT("BodyInstance"))
    {
        return true;
    }

    UPrimitiveComponent* SourcePrimitive = Cast<UPrimitiveComponent>(Source);
    UPrimitiveComponent* TargetPrimitive = Cast<UPrimitiveComponent>(Target);
    if (!SourcePrimitive || !TargetPrimitive)
    {
        return true;
    }

    FString Error;
    if (!CopyPrimitiveBodyInstance(SourcePrimitive, TargetPrimitive, Error))
    {
        OutResult.AddError(TEXT("SpecialLivePropertyCopyFailed"), Error, FieldPrefix);
        return false;
    }

    OutResult.AddFieldResult(
        OwnerPath + TEXT(".") + PropertyName,
        TEXT("written"),
        WriteTarget,
        TEXT("special_body_instance"),
        TEXT("copy_live_blueprint_values"),
        TEXT("copied from live source primitive component"));

    if (!CapturePrimitiveBodyInstanceSnapshot(SourcePrimitive, OwnerPath, FieldPrefix, OutSnapshots, OutResult))
    {
        return false;
    }
    if (!VerifyPrimitiveBodyInstanceSnapshot(TargetPrimitive, OutSnapshots.Last(), OutResult))
    {
        return false;
    }

    bOutHandled = true;
    return true;
}

bool FBlueprintAutomationService::CopyPrimitiveBodyInstance(UPrimitiveComponent* Source, UPrimitiveComponent* Target, FString& OutError) const
{
    if (!Source || !Target)
    {
        OutError = TEXT("Primitive BodyInstance copy target is invalid.");
        return false;
    }

    Target->Modify();

    UScriptStruct* BodyStruct = FBodyInstance::StaticStruct();
    if (!BodyStruct)
    {
        OutError = TEXT("FBodyInstance reflected struct is unavailable.");
        return false;
    }

    uint8* SourceStructData = reinterpret_cast<uint8*>(&Source->BodyInstance);
    uint8* TargetStructData = reinterpret_cast<uint8*>(&Target->BodyInstance);
    constexpr EPropertyFlags SkippedFlags =
        CPF_Transient
        | CPF_DuplicateTransient
        | CPF_NonPIEDuplicateTransient
        | CPF_Deprecated
        | CPF_EditorOnly;
    for (TFieldIterator<FProperty> It(BodyStruct); It; ++It)
    {
        FProperty* Property = *It;
        if (!Property || Property->HasAnyPropertyFlags(SkippedFlags))
        {
            continue;
        }

        const void* SourceValue = Property->ContainerPtrToValuePtr<void>(SourceStructData);
        void* TargetValue = Property->ContainerPtrToValuePtr<void>(TargetStructData);
        Property->CopyCompleteValue(TargetValue, SourceValue);
    }

    Target->SetCollisionEnabled(Source->GetCollisionEnabled());
    Target->SetCollisionObjectType(Source->GetCollisionObjectType());
    Target->SetCollisionResponseToChannels(Source->GetCollisionResponseToChannels());
    Target->SetGenerateOverlapEvents(Source->GetGenerateOverlapEvents());
    Target->BodyInstance.SetResponseToChannels(Source->BodyInstance.GetResponseToChannels());
    Target->BodyInstance.SetObjectType(Source->BodyInstance.GetObjectType());
    Target->BodyInstance.SetCollisionEnabled(Source->BodyInstance.GetCollisionEnabled(false), false);
    Target->BodyInstance.SetCollisionProfileName(Source->BodyInstance.GetCollisionProfileName());
    Target->SetCollisionProfileName(Source->GetCollisionProfileName(), false);
    Target->MarkPackageDirty();
    return true;
}

bool FBlueprintAutomationService::CapturePrimitiveBodyInstanceSnapshot(UPrimitiveComponent* Source, const FString& OwnerPath, const FString& FieldPrefix, TArray<FAssignedPropertySnapshot>& OutSnapshots, FAutomationTaskResult& OutResult) const
{
    if (!Source)
    {
        OutResult.AddError(TEXT("PropertyPersistenceCaptureFailed"), TEXT("Source primitive component is null."), FieldPrefix);
        return false;
    }

    FAssignedPropertySnapshot Snapshot;
    Snapshot.OwnerPath = OwnerPath;
    Snapshot.PropertyName = TEXT("BodyInstance");
    Snapshot.FieldPrefix = FieldPrefix;
    Snapshot.SnapshotKind = TEXT("primitive_body_instance");
    Snapshot.ExpectedSemanticValues.Add(TEXT("CollisionProfileName"), Source->GetCollisionProfileName().ToString());
    Snapshot.ExpectedSemanticValues.Add(TEXT("CollisionEnabled"), FString::FromInt(static_cast<int32>(Source->GetCollisionEnabled())));
    Snapshot.ExpectedSemanticValues.Add(TEXT("ObjectType"), FString::FromInt(static_cast<int32>(Source->GetCollisionObjectType())));
    Snapshot.ExpectedSemanticValues.Add(TEXT("CollisionResponses"), CollisionResponsesToStableText(Source));
    Snapshot.ExpectedSemanticValues.Add(TEXT("GenerateOverlapEvents"), Source->GetGenerateOverlapEvents() ? TEXT("true") : TEXT("false"));
    OutSnapshots.Add(MoveTemp(Snapshot));
    return true;
}

bool FBlueprintAutomationService::VerifyPrimitiveBodyInstanceSnapshot(UPrimitiveComponent* Target, const FAssignedPropertySnapshot& Snapshot, FAutomationTaskResult& OutResult) const
{
    if (!Target)
    {
        OutResult.AddError(TEXT("PropertyPersistenceVerifyFailed"), TEXT("Target primitive component is null."), Snapshot.FieldPrefix);
        return false;
    }

    TMap<FString, FString> ActualValues;
    ActualValues.Add(TEXT("CollisionProfileName"), Target->GetCollisionProfileName().ToString());
    ActualValues.Add(TEXT("CollisionEnabled"), FString::FromInt(static_cast<int32>(Target->GetCollisionEnabled())));
    ActualValues.Add(TEXT("ObjectType"), FString::FromInt(static_cast<int32>(Target->GetCollisionObjectType())));
    ActualValues.Add(TEXT("CollisionResponses"), CollisionResponsesToStableText(Target));
    ActualValues.Add(TEXT("GenerateOverlapEvents"), Target->GetGenerateOverlapEvents() ? TEXT("true") : TEXT("false"));

    for (const TPair<FString, FString>& ExpectedPair : Snapshot.ExpectedSemanticValues)
    {
        const FString* Actual = ActualValues.Find(ExpectedPair.Key);
        if (!Actual || *Actual != ExpectedPair.Value)
        {
            const TCHAR* ActualText = Actual ? **Actual : TEXT("<missing>");
            OutResult.AddError(
                TEXT("PropertyNotPersistedAfterCompile"),
                FString::Printf(
                    TEXT("%s.BodyInstance semantic value '%s' changed after compile/save. Expected '%s', actual '%s'."),
                    *Snapshot.OwnerPath,
                    *ExpectedPair.Key,
                    *ExpectedPair.Value,
                    ActualText),
                Snapshot.FieldPrefix);
            return false;
        }
    }

    return true;
}

void FBlueprintAutomationService::AddPropertyFieldResults(FAutomationTaskResult& OutResult, const FString& OwnerPath, const FString& WriteTarget, const TArray<FAutomationPropertyValue>& Properties, const FString& Reason) const
{
    for (const FAutomationPropertyValue& Property : Properties)
    {
        OutResult.AddFieldResult(
            OwnerPath + TEXT(".") + Property.Name,
            TEXT("written"),
            WriteTarget,
            Property.Type,
            Reason);
    }
}

bool FBlueprintAutomationService::CapturePropertySnapshots(UObject* Target, const FString& OwnerPath, const TArray<FAutomationPropertyValue>& Properties, const FString& FieldPrefix, const TArray<FAutomationAssetRedirect>& Redirects, TArray<FAssignedPropertySnapshot>& OutSnapshots, FAutomationTaskResult& OutResult) const
{
    if (!Target)
    {
        OutResult.AddError(TEXT("PropertyPersistenceCaptureFailed"), TEXT("Target object is null."), FieldPrefix);
        return false;
    }

    for (int32 Index = 0; Index < Properties.Num(); ++Index)
    {
        const FAutomationPropertyValue& Property = Properties[Index];
        const FString PropertyField = FString::Printf(TEXT("%s[%d]"), *FieldPrefix, Index);
        FProperty* TargetProperty = Target->GetClass()->FindPropertyByName(FName(*Property.Name));
        if (!TargetProperty)
        {
            OutResult.AddError(
                TEXT("PropertyPersistenceCaptureFailed"),
                FString::Printf(TEXT("Property '%s' not found on '%s'."), *Property.Name, *Target->GetClass()->GetName()),
                PropertyField);
            return false;
        }

        FString ExportedText;
        FString Error;
        if (!PropertyAssignmentService.ExportAssignedPropertyText(Target, Property.Name, ExportedText, Error))
        {
            OutResult.AddError(
                TEXT("PropertyPersistenceCaptureFailed"),
                Error,
                PropertyField);
            return false;
        }

        FString ExpectedText;
        if (Property.Type.Equals(TEXT("import_text"), ESearchCase::IgnoreCase)
            || Property.Type.Equals(TEXT("raw_import_text"), ESearchCase::IgnoreCase))
        {
            // For raw UE text, the target's post-import export is the stable
            // persistence snapshot. Scratch reconstruction can differ for
            // FieldPath/GAS/FText while the assigned live object is correct.
            ExpectedText = ExportedText;
        }
        else
        {
            FString ExpectedError;
            if (!PropertyAssignmentService.BuildExpectedAssignedPropertyText(TargetProperty, Property, Redirects, Target, ExpectedText, ExpectedError))
            {
                OutResult.AddError(TEXT("PropertyExpectedValueBuildFailed"), ExpectedError, PropertyField);
                return false;
            }
        }

        if (ExportedText != ExpectedText)
        {
            const FString ExpectedForMessage = TruncateForResultMessage(ExpectedText);
            const FString ActualForMessage = TruncateForResultMessage(ExportedText);
            OutResult.AddError(
                TEXT("PropertyAssignmentDidNotTakeEffect"),
                FString::Printf(
                    TEXT("%s.%s was not assigned. Expected exported value '%s', actual '%s'."),
                    *OwnerPath,
                    *Property.Name,
                    *ExpectedForMessage,
                    *ActualForMessage),
                PropertyField);
            OutResult.AddFieldResult(
                OwnerPath + TEXT(".") + Property.Name,
                TEXT("not_assigned"),
                TEXT("post_assign_readback"),
                TEXT("export_text"),
                TEXT("assignment_verification"),
                FString::Printf(TEXT("expected '%s', actual '%s'"), *ExpectedForMessage, *ActualForMessage));
            return false;
        }

        FAssignedPropertySnapshot Snapshot;
        Snapshot.OwnerPath = OwnerPath;
        Snapshot.PropertyName = Property.Name;
        Snapshot.ExpectedText = ExpectedText;
        Snapshot.FieldPrefix = PropertyField;
        OutSnapshots.Add(MoveTemp(Snapshot));
    }

    return true;
}

bool FBlueprintAutomationService::VerifyPropertySnapshots(UObject* Target, const TArray<FAssignedPropertySnapshot>& Snapshots, FAutomationTaskResult& OutResult) const
{
    if (!Target)
    {
        OutResult.AddError(TEXT("PropertyNotPersistedAfterCompile"), TEXT("Target object is null after compile/save."), TEXT("post_write.target"));
        return false;
    }

    bool bOk = true;
    for (const FAssignedPropertySnapshot& Snapshot : Snapshots)
    {
        if (Snapshot.SnapshotKind == TEXT("primitive_body_instance"))
        {
            UPrimitiveComponent* PrimitiveTarget = Cast<UPrimitiveComponent>(Target);
            if (!VerifyPrimitiveBodyInstanceSnapshot(PrimitiveTarget, Snapshot, OutResult))
            {
                bOk = false;
            }
            continue;
        }

        FString ActualText;
        FString Error;
        if (!PropertyAssignmentService.ExportAssignedPropertyText(Target, Snapshot.PropertyName, ActualText, Error))
        {
            OutResult.AddError(TEXT("PropertyPersistenceVerifyFailed"), Error, Snapshot.FieldPrefix);
            bOk = false;
            continue;
        }

        if (ActualText != Snapshot.ExpectedText)
        {
            const FString ExpectedForMessage = TruncateForResultMessage(Snapshot.ExpectedText);
            const FString ActualForMessage = TruncateForResultMessage(ActualText);
            OutResult.AddError(
                TEXT("PropertyNotPersistedAfterCompile"),
                FString::Printf(
                    TEXT("%s.%s changed after compile/save. Expected exported value '%s', actual '%s'."),
                    *Snapshot.OwnerPath,
                    *Snapshot.PropertyName,
                    *ExpectedForMessage,
                    *ActualForMessage),
                Snapshot.FieldPrefix);
            OutResult.AddFieldResult(
                Snapshot.OwnerPath + TEXT(".") + Snapshot.PropertyName,
                TEXT("not_persisted_after_compile"),
                TEXT("post_compile_readback"),
                TEXT("export_text"),
                TEXT("persistence_verification"),
                FString::Printf(TEXT("expected '%s', actual '%s'"), *ExpectedForMessage, *ActualForMessage));
            bOk = false;
        }
    }

    return bOk;
}

bool FBlueprintAutomationService::AddDiagnosticReadback(FAutomationTaskResult& OutResult, UObject* Target, const FString& PropertyName, const FString& Phase, FString& OutText) const
{
    FString Error;
    if (!PropertyAssignmentService.ExportAssignedPropertyText(Target, PropertyName, OutText, Error))
    {
        OutResult.AddError(TEXT("DiagnosticReadbackFailed"), Error, Phase);
        return false;
    }

    OutResult.AddFieldResult(
        PropertyName,
        Phase,
        TEXT("diagnostic_readback"),
        TEXT("export_text"),
        TEXT("diagnose_blueprint_property_persistence"),
        TruncateForResultMessage(OutText));
    return true;
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

void FBlueprintAutomationService::AddCreatedAssetOutput(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const
{
    FAutomationAssetOutput Output;
    Output.AssetPath = FString::Printf(TEXT("%s/%s.%s"), *Request.Asset.PackagePath, *Request.Asset.AssetName, *Request.Asset.AssetName);
    Output.AssetName = Request.Asset.AssetName;
    Output.AssetType = TEXT("blueprint");
    OutResult.AssetOutputs.Add(Output);
}

bool FBlueprintAutomationService::CompileIfRequested(UBlueprint* Blueprint, bool bCompile, FAutomationTaskResult& OutResult)
{
    if (!bCompile)
    {
        return true;
    }

    FString Error;
    OutResult.AddLog(TEXT("blueprint: compile"));
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
    OutResult.AddLog(TEXT("blueprint: save asset"));
    const double StartSeconds = FPlatformTime::Seconds();
    if (!BlueprintAdapter->SaveAsset(Blueprint, Error))
    {
        OutResult.AddError(TEXT("AssetSaveFailed"), Error);
        return false;
    }
    OutResult.Metrics.SaveDurationMs += static_cast<int64>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
    return true;
}
