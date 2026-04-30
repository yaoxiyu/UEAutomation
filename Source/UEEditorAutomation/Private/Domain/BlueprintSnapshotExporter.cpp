#include "Domain/BlueprintSnapshotExporter.h"

#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Domain/PropertySnapshotService.h"
#include "Engine/Blueprint.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Engine/SCS_Node.h"
#include "Engine/SimpleConstructionScript.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "UObject/UnrealType.h"

namespace
{
    TSharedRef<FJsonObject> ExportTransform(USceneComponent* SceneTemplate)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        if (!SceneTemplate)
        {
            return Object;
        }
        const FVector Location = SceneTemplate->GetRelativeLocation();
        const FRotator Rotation = SceneTemplate->GetRelativeRotation();
        const FVector Scale = SceneTemplate->GetRelativeScale3D();

        TArray<TSharedPtr<FJsonValue>> LocationArray;
        LocationArray.Add(MakeShared<FJsonValueNumber>(Location.X));
        LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Y));
        LocationArray.Add(MakeShared<FJsonValueNumber>(Location.Z));
        Object->SetArrayField(TEXT("location"), LocationArray);

        TArray<TSharedPtr<FJsonValue>> RotationArray;
        RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Pitch));
        RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Yaw));
        RotationArray.Add(MakeShared<FJsonValueNumber>(Rotation.Roll));
        Object->SetArrayField(TEXT("rotation"), RotationArray);

        TArray<TSharedPtr<FJsonValue>> ScaleArray;
        ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.X));
        ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Y));
        ScaleArray.Add(MakeShared<FJsonValueNumber>(Scale.Z));
        Object->SetArrayField(TEXT("scale"), ScaleArray);

        return Object;
    }

    TSharedRef<FJsonObject> ExportComponentObject(
        UActorComponent* Template,
        UActorComponent* ParentTemplate,
        const FString& ComponentName,
        const FString& AttachParent,
        const FString& OwnerKind,
        const FAutomationAnalysisOptions& Options,
        const TArray<FString>& DeniedExportNames,
        FAutomationTaskResult& OutResult)
    {
        const TSharedRef<FJsonObject> CompObject = MakeShared<FJsonObject>();
        CompObject->SetStringField(TEXT("component_name"), ComponentName);
        CompObject->SetStringField(TEXT("component_class"), Template ? Template->GetClass()->GetPathName() : FString());
        CompObject->SetStringField(TEXT("attach_parent"), AttachParent);
        CompObject->SetStringField(TEXT("owner_kind"), OwnerKind);
        const bool bInherited = OwnerKind == TEXT("scs_inherited") || OwnerKind == TEXT("native");
        CompObject->SetBoolField(TEXT("inherited"), bInherited);
        CompObject->SetBoolField(TEXT("has_inheritable_override"), false);

        if (USceneComponent* SceneTemplate = Cast<USceneComponent>(Template))
        {
            CompObject->SetObjectField(TEXT("transform"), ExportTransform(SceneTemplate));
        }

        TArray<TSharedPtr<FJsonValue>> ComponentProperties;
        if (Template)
        {
            FPropertySnapshotService Snapshot;
            Snapshot.SetDeniedExportNames(DeniedExportNames);
            if (!ParentTemplate)
            {
                ParentTemplate = Template->GetClass()->GetDefaultObject<UActorComponent>();
            }
            if (ParentTemplate)
            {
                Snapshot.SetParentTargetForDiff(ParentTemplate);
            }
            Snapshot.ExportObjectProperties(Template, Options, ComponentProperties, OutResult);
        }
        CompObject->SetArrayField(TEXT("properties"), ComponentProperties);
        return CompObject;
    }

    void ExportNativeComponentProperties(
        UObject* CDO,
        const FAutomationAnalysisOptions& Options,
        const TArray<FString>& DeniedExportNames,
        TSet<FString>& KnownComponentNames,
        TArray<TSharedPtr<FJsonValue>>& Components,
        FAutomationTaskResult& OutResult)
    {
        if (!CDO)
        {
            return;
        }

        for (TFieldIterator<FObjectPropertyBase> It(CDO->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            FObjectPropertyBase* Property = *It;
            if (!Property || !Property->PropertyClass || !Property->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
            {
                continue;
            }

            UActorComponent* Template = Cast<UActorComponent>(Property->GetObjectPropertyValue_InContainer(CDO));
            if (!Template)
            {
                continue;
            }

            const FString ComponentName = Template->GetName().IsEmpty() ? Property->GetName() : Template->GetName();
            if (KnownComponentNames.Contains(ComponentName))
            {
                continue;
            }

            KnownComponentNames.Add(ComponentName);
            UActorComponent* ParentTemplate = nullptr;
            if (UClass* SuperClass = CDO->GetClass()->GetSuperClass())
            {
                if (UObject* SuperCDO = SuperClass->GetDefaultObject())
                {
                    FProperty* SuperProperty = SuperCDO->GetClass()->FindPropertyByName(Property->GetFName());
                    if (FObjectPropertyBase* SuperObjectProperty = CastField<FObjectPropertyBase>(SuperProperty))
                    {
                        ParentTemplate = Cast<UActorComponent>(SuperObjectProperty->GetObjectPropertyValue_InContainer(SuperCDO));
                    }
                }
            }

            Components.Add(MakeShared<FJsonValueObject>(
                ExportComponentObject(Template, ParentTemplate, ComponentName, FString(), TEXT("native"), Options, DeniedExportNames, OutResult)));
        }

        TArray<UObject*> DefaultSubobjects;
        CDO->GetDefaultSubobjects(DefaultSubobjects);
        for (UObject* DefaultSubobject : DefaultSubobjects)
        {
            UActorComponent* Template = Cast<UActorComponent>(DefaultSubobject);
            if (!Template)
            {
                continue;
            }

            const FString ComponentName = Template->GetName();
            if (ComponentName.IsEmpty() || KnownComponentNames.Contains(ComponentName))
            {
                continue;
            }

            KnownComponentNames.Add(ComponentName);

            UActorComponent* ParentTemplate = nullptr;
            if (UClass* SuperClass = CDO->GetClass()->GetSuperClass())
            {
                if (UObject* SuperCDO = SuperClass->GetDefaultObject())
                {
                    TArray<UObject*> ParentDefaultSubobjects;
                    SuperCDO->GetDefaultSubobjects(ParentDefaultSubobjects);
                    for (UObject* ParentDefaultSubobject : ParentDefaultSubobjects)
                    {
                        UActorComponent* Candidate = Cast<UActorComponent>(ParentDefaultSubobject);
                        if (Candidate && Candidate->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
                        {
                            ParentTemplate = Candidate;
                            break;
                        }
                    }
                }
            }

            Components.Add(MakeShared<FJsonValueObject>(
                ExportComponentObject(Template, ParentTemplate, ComponentName, FString(), TEXT("native"), Options, DeniedExportNames, OutResult)));
        }
    }

    void ExportSCSComponents(
        UBlueprint* OwnerBlueprint,
        UBlueprintGeneratedClass* TargetGeneratedClass,
        const FString& OwnerKind,
        const FAutomationAnalysisOptions& Options,
        const TArray<FString>& DeniedExportNames,
        TSet<FString>& KnownComponentNames,
        TArray<TSharedPtr<FJsonValue>>& Components,
        FAutomationTaskResult& OutResult)
    {
        if (!OwnerBlueprint || !OwnerBlueprint->SimpleConstructionScript)
        {
            return;
        }

        const TArray<USCS_Node*>& AllNodes = OwnerBlueprint->SimpleConstructionScript->GetAllNodes();
        for (USCS_Node* Node : AllNodes)
        {
            if (!Node)
            {
                continue;
            }
            if (Node->GetSCS() != OwnerBlueprint->SimpleConstructionScript)
            {
                continue;
            }

            const FString ComponentName = Node->GetVariableName().ToString();
            if (KnownComponentNames.Contains(ComponentName))
            {
                continue;
            }
            KnownComponentNames.Add(ComponentName);

            UActorComponent* Template = TargetGeneratedClass ? Node->GetActualComponentTemplate(TargetGeneratedClass) : nullptr;
            if (!Template)
            {
                Template = Node->ComponentTemplate;
            }
            if (!Template && Node->ComponentClass)
            {
                Template = Node->ComponentClass->GetDefaultObject<UActorComponent>();
            }

            UActorComponent* ParentTemplate = nullptr;
            bool bHasInheritableOverride = false;
            if (OwnerKind == TEXT("scs_inherited"))
            {
                UBlueprintGeneratedClass* ParentGeneratedClass = TargetGeneratedClass
                    ? Cast<UBlueprintGeneratedClass>(TargetGeneratedClass->GetSuperClass())
                    : nullptr;
                ParentTemplate = ParentGeneratedClass ? Node->GetActualComponentTemplate(ParentGeneratedClass) : nullptr;
                if (!ParentTemplate)
                {
                    UBlueprintGeneratedClass* OwnerGeneratedClass = Cast<UBlueprintGeneratedClass>(OwnerBlueprint->GeneratedClass);
                    ParentTemplate = OwnerGeneratedClass ? Node->GetActualComponentTemplate(OwnerGeneratedClass) : nullptr;
                    if (!ParentTemplate)
                    {
                        ParentTemplate = Node->ComponentTemplate;
                    }
                }

                if (TargetGeneratedClass)
                {
                    const FComponentKey ComponentKey(Node);
                    if (UInheritableComponentHandler* Handler = TargetGeneratedClass->GetInheritableComponentHandler(false))
                    {
                        if (Handler->GetOverridenComponentTemplate(ComponentKey))
                        {
                            bHasInheritableOverride = true;
                        }
                    }
                }
            }

            FString AttachParent;
            if (USCS_Node* ParentNode = OwnerBlueprint->SimpleConstructionScript->FindParentNode(Node))
            {
                AttachParent = ParentNode->GetVariableName().ToString();
            }

            const TSharedRef<FJsonObject> ComponentObject =
                ExportComponentObject(Template, ParentTemplate, ComponentName, AttachParent, OwnerKind, Options, DeniedExportNames, OutResult);
            ComponentObject->SetBoolField(TEXT("has_inheritable_override"), bHasInheritableOverride);
            Components.Add(MakeShared<FJsonValueObject>(ComponentObject));
        }
    }

    void ExportInheritedSCSComponents(
        UBlueprint* Blueprint,
        UBlueprintGeneratedClass* TargetGeneratedClass,
        const FAutomationAnalysisOptions& Options,
        const TArray<FString>& DeniedExportNames,
        TSet<FString>& KnownComponentNames,
        TArray<TSharedPtr<FJsonValue>>& Components,
        FAutomationTaskResult& OutResult)
    {
        if (!Blueprint)
        {
            return;
        }

        UClass* ParentClass = Blueprint->ParentClass;
        while (ParentClass)
        {
            UBlueprint* ParentBlueprint = Cast<UBlueprint>(ParentClass->ClassGeneratedBy);
            if (ParentBlueprint)
            {
                ExportSCSComponents(ParentBlueprint, TargetGeneratedClass, TEXT("scs_inherited"), Options, DeniedExportNames, KnownComponentNames, Components, OutResult);
            }
            ParentClass = ParentClass->GetSuperClass();
        }
    }
}

bool FBlueprintSnapshotExporter::ExportBlueprintSnapshot(
    UBlueprint* Blueprint,
    const FAutomationAnalysisOptions& Options,
    const TArray<FString>& DeniedExportNames,
    const TSharedRef<FJsonObject>& OutJson,
    FAutomationTaskResult& OutResult) const
{
    if (!Blueprint)
    {
        OutResult.AddError(TEXT("BlueprintSnapshotFailed"), TEXT("Blueprint is null"));
        return false;
    }

    OutJson->SetStringField(TEXT("status"), Blueprint->Status == BS_UpToDate ? TEXT("BS_UpToDate") :
        Blueprint->Status == BS_Dirty ? TEXT("BS_Dirty") :
        Blueprint->Status == BS_Error ? TEXT("BS_Error") : TEXT("BS_Unknown"));
    OutJson->SetBoolField(TEXT("is_dirty"), Blueprint->GetOutermost()->IsDirty());
    OutJson->SetStringField(TEXT("blueprint_type"),
        Blueprint->BlueprintType == BPTYPE_Normal ? TEXT("BPTYPE_Normal") :
        Blueprint->BlueprintType == BPTYPE_Const ? TEXT("BPTYPE_Const") :
        Blueprint->BlueprintType == BPTYPE_MacroLibrary ? TEXT("BPTYPE_MacroLibrary") :
        Blueprint->BlueprintType == BPTYPE_Interface ? TEXT("BPTYPE_Interface") :
        Blueprint->BlueprintType == BPTYPE_FunctionLibrary ? TEXT("BPTYPE_FunctionLibrary") :
        Blueprint->BlueprintType == BPTYPE_LevelScript ? TEXT("BPTYPE_LevelScript") :
        TEXT("Unknown"));

    // User variables.
    TArray<TSharedPtr<FJsonValue>> UserVariables;
    for (const FBPVariableDescription& Var : Blueprint->NewVariables)
    {
        const TSharedRef<FJsonObject> VarObject = MakeShared<FJsonObject>();
        VarObject->SetStringField(TEXT("name"), Var.VarName.ToString());
        VarObject->SetStringField(TEXT("type"), Var.VarType.PinCategory.ToString());
        VarObject->SetStringField(TEXT("category"), Var.Category.ToString());
        VarObject->SetStringField(TEXT("default_value"), Var.DefaultValue);

        const TSharedRef<FJsonObject> MetaObject = MakeShared<FJsonObject>();
        for (const FBPVariableMetaDataEntry& Entry : Var.MetaDataArray)
        {
            MetaObject->SetStringField(Entry.DataKey.ToString(), Entry.DataValue);
        }
        VarObject->SetObjectField(TEXT("metadata"), MetaObject);

        UserVariables.Add(MakeShared<FJsonValueObject>(VarObject));
    }
    OutJson->SetArrayField(TEXT("user_variables"), UserVariables);

    // CDO defaults: snapshot of generated class CDO.
    TArray<TSharedPtr<FJsonValue>> ClassDefaults;
    UClass* GeneratedClass = Blueprint->GeneratedClass;
    if (Options.bIncludeClassDefaults && GeneratedClass)
    {
        UObject* CDO = GeneratedClass->GetDefaultObject();
        UClass* ParentClass = GeneratedClass->GetSuperClass();
        UObject* ParentCDO = ParentClass ? ParentClass->GetDefaultObject() : nullptr;

        FPropertySnapshotService Snapshot;
        Snapshot.SetDeniedExportNames(DeniedExportNames);
        Snapshot.SetParentTargetForDiff(ParentCDO);

        if (CDO)
        {
            Snapshot.ExportObjectProperties(CDO, Options, ClassDefaults, OutResult);
        }
        else
        {
            OutResult.AddWarning(TEXT("BlueprintCDOUnavailable: GeneratedClass has no CDO; defaults skipped"));
        }
    }
    OutJson->SetArrayField(TEXT("class_defaults"), ClassDefaults);

    // SCS components.
    TArray<TSharedPtr<FJsonValue>> Components;
    TSet<FString> KnownComponentNames;
    if (Options.bIncludeComponents)
    {
        UBlueprintGeneratedClass* BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(GeneratedClass);
        ExportSCSComponents(Blueprint, BlueprintGeneratedClass, TEXT("scs_owned"), Options, DeniedExportNames, KnownComponentNames, Components, OutResult);
        ExportInheritedSCSComponents(Blueprint, BlueprintGeneratedClass, Options, DeniedExportNames, KnownComponentNames, Components, OutResult);
        if (GeneratedClass)
        {
            ExportNativeComponentProperties(GeneratedClass->GetDefaultObject(), Options, DeniedExportNames, KnownComponentNames, Components, OutResult);
        }
    }
    OutJson->SetArrayField(TEXT("components"), Components);

    return true;
}
