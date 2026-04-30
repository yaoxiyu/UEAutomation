#include "Adapter/UEBlueprintEditorAdapter.h"

#if __has_include("AssetRegistry/AssetRegistryModule.h")
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#include "Components/ActorComponent.h"
#include "Components/SceneComponent.h"
#include "Editor.h"
#include "Editor/EditorEngine.h"
#include "FileHelpers.h"
#include "Engine/BlueprintGeneratedClass.h"
#include "Engine/InheritableComponentHandler.h"
#include "Kismet2/ComponentEditorUtils.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Subsystems/AssetEditorSubsystem.h"
#include "UObject/UnrealType.h"

UBlueprint* FUEBlueprintEditorAdapter::CreateBlueprintAsset(const FString& PackagePath, const FString& AssetName, UClass* ParentClass, FString& OutError)
{
    if (!ParentClass)
    {
        OutError = TEXT("Parent class is null.");
        return nullptr;
    }

    const FString PackageName = PackagePath / AssetName;
    UPackage* Package = CreatePackage(nullptr, *PackageName);
    if (!Package)
    {
        OutError = FString::Printf(TEXT("Failed to create package '%s'."), *PackageName);
        return nullptr;
    }

    UBlueprint* Blueprint = FKismetEditorUtilities::CreateBlueprint(
        ParentClass,
        Package,
        FName(*AssetName),
        BPTYPE_Normal,
        UBlueprint::StaticClass(),
        UBlueprintGeneratedClass::StaticClass(),
        FName(TEXT("UEEditorAutomation"))
    );

    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Failed to create Blueprint '%s'."), *PackageName);
        return nullptr;
    }

    FAssetRegistryModule::AssetCreated(Blueprint);
    Package->MarkPackageDirty();
    return Blueprint;
}

UBlueprint* FUEBlueprintEditorAdapter::LoadBlueprintAsset(const FString& AssetPath, FString& OutError)
{
    UObject* Asset = StaticLoadObject(UBlueprint::StaticClass(), nullptr, *AssetPath);
    UBlueprint* Blueprint = Cast<UBlueprint>(Asset);
    if (!Blueprint)
    {
        OutError = FString::Printf(TEXT("Blueprint asset '%s' not found."), *AssetPath);
        return nullptr;
    }
    // SavePackage rejects partially loaded packages. Force a full load so any
    // subsequent modify/save operation sees the entire package graph.
    if (UPackage* Package = Blueprint->GetOutermost())
    {
        Package->FullyLoad();
    }
    return Blueprint;
}

bool FUEBlueprintEditorAdapter::DoesAssetExist(const FString& PackagePath, const FString& AssetName) const
{
    const FString ObjectPath = FString::Printf(TEXT("%s/%s.%s"), *PackagePath, *AssetName, *AssetName);
    FAssetRegistryModule& AssetRegistryModule = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    return AssetRegistryModule.Get().GetAssetByObjectPath(*ObjectPath).IsValid();
}

bool FUEBlueprintEditorAdapter::AddComponentNode(UBlueprint* Blueprint, UClass* ComponentClass, const FString& ComponentName, const FString& AttachParentName, FString& OutError)
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        OutError = TEXT("Blueprint or SimpleConstructionScript is invalid.");
        return false;
    }

    if (!ComponentClass || !ComponentClass->IsChildOf(UActorComponent::StaticClass()))
    {
        OutError = FString::Printf(TEXT("Component class '%s' is invalid."), ComponentClass ? *ComponentClass->GetPathName() : TEXT("<null>"));
        return false;
    }

    if (FindSCSNodeInHierarchy(Blueprint, ComponentName))
    {
        OutError = FString::Printf(TEXT("Duplicate component name '%s'."), *ComponentName);
        return false;
    }

    USCS_Node* NewNode = Blueprint->SimpleConstructionScript->CreateNode(ComponentClass, FName(*ComponentName));
    if (!NewNode)
    {
        OutError = FString::Printf(TEXT("Failed to create SCS node '%s'."), *ComponentName);
        return false;
    }

    if (!AttachParentName.IsEmpty())
    {
        USCS_Node* ParentNode = FindSCSNode(Blueprint, AttachParentName);
        if (!ParentNode)
        {
            OutError = FString::Printf(TEXT("Attach parent '%s' not found."), *AttachParentName);
            return false;
        }
        ParentNode->AddChildNode(NewNode);
    }
    else
    {
        Blueprint->SimpleConstructionScript->AddNode(NewNode);
    }

    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

bool FUEBlueprintEditorAdapter::RemoveComponentNode(UBlueprint* Blueprint, const FString& ComponentName, FString& OutError)
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        OutError = TEXT("Blueprint or SimpleConstructionScript is invalid.");
        return false;
    }

    USCS_Node* Node = FindSCSNode(Blueprint, ComponentName);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Component '%s' not found."), *ComponentName);
        return false;
    }

    Blueprint->SimpleConstructionScript->RemoveNode(Node);
    FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(Blueprint);
    return true;
}

UObject* FUEBlueprintEditorAdapter::GetComponentTemplate(UBlueprint* Blueprint, const FString& ComponentName, const FString& LookupPolicy, FString& OutError)
{
    const FString Policy = LookupPolicy.IsEmpty() ? TEXT("scs_first") : LookupPolicy.ToLower();
    if (Policy != TEXT("scs_first")
        && Policy != TEXT("native_first")
        && Policy != TEXT("scs_only")
        && Policy != TEXT("native_only")
        && Policy != TEXT("scs_inherited_override"))
    {
        OutError = FString::Printf(TEXT("Unsupported component_lookup_policy '%s'."), *LookupPolicy);
        return nullptr;
    }

    if (Policy == TEXT("native_first") || Policy == TEXT("native_only"))
    {
        if (UObject* NativeTemplate = FindNativeComponentTemplate(Blueprint, ComponentName))
        {
            if (const UActorComponent* NativeComponent = Cast<UActorComponent>(NativeTemplate))
            {
                if (NativeComponent->CreationMethod == EComponentCreationMethod::Native
                    && !FComponentEditorUtils::GetPropertyForEditableNativeComponent(NativeComponent))
                {
                    OutError = FString::Printf(
                        TEXT("Native component '%s' is not editable as Blueprint defaults. UE requires native components to be referenced by an editor-visible UPROPERTY before component template overrides can persist."),
                        *ComponentName);
                    return nullptr;
                }
            }
            return NativeTemplate;
        }
        if (Policy == TEXT("native_only"))
        {
            OutError = FString::Printf(TEXT("Native component '%s' not found."), *ComponentName);
            return nullptr;
        }
    }

    USCS_Node* Node = nullptr;
    if (Policy != TEXT("native_only"))
    {
        Node = Policy == TEXT("scs_only")
            ? FindSCSNode(Blueprint, ComponentName)
            : FindSCSNodeInHierarchy(Blueprint, ComponentName);
    }
    if (!Node)
    {
        if (Policy == TEXT("scs_first"))
        {
            if (UObject* NativeTemplate = FindNativeComponentTemplate(Blueprint, ComponentName))
            {
                return NativeTemplate;
            }
        }

        OutError = FString::Printf(TEXT("Component '%s' not found."), *ComponentName);
        return nullptr;
    }

    UBlueprintGeneratedClass* BlueprintGeneratedClass = nullptr;
    if (Blueprint && Blueprint->GeneratedClass)
    {
        BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get());
    }

    if (Policy == TEXT("scs_inherited_override"))
    {
        if (!BlueprintGeneratedClass)
        {
            OutError = FString::Printf(TEXT("Blueprint generated class is invalid for inherited component '%s'."), *ComponentName);
            return nullptr;
        }

        if (Blueprint->SimpleConstructionScript && Node->GetSCS() == Blueprint->SimpleConstructionScript)
        {
            UObject* OwnTemplate = Node->GetActualComponentTemplate(BlueprintGeneratedClass);
            return OwnTemplate ? OwnTemplate : Node->ComponentTemplate;
        }

        UInheritableComponentHandler* Handler = Blueprint->GetInheritableComponentHandler(true);
        if (!Handler)
        {
            OutError = FString::Printf(TEXT("Failed to create inheritable component handler for '%s'."), *ComponentName);
            return nullptr;
        }

        const FComponentKey ComponentKey(Node);
        UObject* Template = Handler->GetOverridenComponentTemplate(ComponentKey);
        if (!Template)
        {
            Blueprint->Modify();
            Handler->Modify();
            Template = Handler->CreateOverridenComponentTemplate(ComponentKey);
        }
        if (!Template)
        {
            OutError = FString::Printf(TEXT("Failed to create inherited component override template '%s'."), *ComponentName);
            return nullptr;
        }
        return Template;
    }

    UObject* Template = BlueprintGeneratedClass ? Node->GetActualComponentTemplate(BlueprintGeneratedClass) : nullptr;
    if (!Template)
    {
        Template = Node->ComponentTemplate;
    }

    if (!Template)
    {
        OutError = FString::Printf(TEXT("Component template '%s' is invalid."), *ComponentName);
    }
    return Template;
}

bool FUEBlueprintEditorAdapter::ApplyComponentTransform(UObject* ComponentTemplate, const FAutomationTransformSpec& Transform, FString& OutError)
{
    USceneComponent* SceneComponent = Cast<USceneComponent>(ComponentTemplate);
    if (!SceneComponent)
    {
        return true;
    }

    if (Transform.bHasLocation)
    {
        SceneComponent->SetRelativeLocation(Transform.Location);
    }
    if (Transform.bHasRotation)
    {
        SceneComponent->SetRelativeRotation(Transform.Rotation);
    }
    if (Transform.bHasScale)
    {
        SceneComponent->SetRelativeScale3D(Transform.Scale);
    }
    SceneComponent->Modify();
    return true;
}

bool FUEBlueprintEditorAdapter::CompileBlueprint(UBlueprint* Blueprint, FString& OutError)
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null.");
        return false;
    }

    FKismetEditorUtilities::CompileBlueprint(Blueprint);
    if (Blueprint->Status == BS_Error)
    {
        OutError = FString::Printf(TEXT("Blueprint '%s' compile failed."), *Blueprint->GetPathName());
        return false;
    }
    return true;
}

bool FUEBlueprintEditorAdapter::SaveAsset(UObject* Asset, FString& OutError)
{
    if (!Asset || !Asset->GetOutermost())
    {
        OutError = TEXT("Asset or package is invalid.");
        return false;
    }

    UPackage* Package = Asset->GetOutermost();
    Package->FullyLoad();
    const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

#if ENGINE_MAJOR_VERSION >= 5
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    const bool bSaved = UPackage::SavePackage(Package, nullptr, *PackageFilename, SaveArgs);
#else
    const bool bSaved = UPackage::SavePackage(Package, nullptr, RF_Public | RF_Standalone, *PackageFilename, GError, nullptr, false, true, SAVE_NoError, nullptr);
#endif

    if (!bSaved)
    {
        OutError = FString::Printf(TEXT("Failed to save asset '%s'."), *Asset->GetPathName());
        return false;
    }
    return true;
}

bool FUEBlueprintEditorAdapter::OpenAsset(UObject* Asset, FString& OutError)
{
    if (!Asset || !GEditor)
    {
        OutError = TEXT("Asset or GEditor is invalid.");
        return false;
    }
    return GEditor->GetEditorSubsystem<UAssetEditorSubsystem>()->OpenEditorForAsset(Asset);
}

UObject* FUEBlueprintEditorAdapter::FindNativeComponentTemplate(UBlueprint* Blueprint, const FString& ComponentName) const
{
    if (!Blueprint || !Blueprint->GeneratedClass)
    {
        return nullptr;
    }

    UObject* CDO = Blueprint->GeneratedClass->GetDefaultObject();
    if (!CDO)
    {
        return nullptr;
    }

    for (TFieldIterator<FObjectPropertyBase> It(CDO->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FObjectPropertyBase* Property = *It;
        if (!Property || !Property->PropertyClass || !Property->PropertyClass->IsChildOf(UActorComponent::StaticClass()))
        {
            continue;
        }

        UObject* Value = Property->GetObjectPropertyValue_InContainer(CDO);
        if (!Value)
        {
            continue;
        }

        if (Property->GetName().Equals(ComponentName, ESearchCase::IgnoreCase)
            || Value->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
        {
            return Value;
        }
    }

    TArray<UObject*> DefaultSubobjects;
    CDO->GetDefaultSubobjects(DefaultSubobjects);
    for (UObject* DefaultSubobject : DefaultSubobjects)
    {
        UActorComponent* Component = Cast<UActorComponent>(DefaultSubobject);
        if (!Component)
        {
            continue;
        }

        if (Component->GetName().Equals(ComponentName, ESearchCase::IgnoreCase))
        {
            return Component;
        }
    }

    return nullptr;
}

USCS_Node* FUEBlueprintEditorAdapter::FindSCSNode(UBlueprint* Blueprint, const FString& ComponentName) const
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        return nullptr;
    }

    const TArray<USCS_Node*> Nodes = Blueprint->SimpleConstructionScript->GetAllNodes();
    for (USCS_Node* Node : Nodes)
    {
        if (Node
            && Node->GetSCS() == Blueprint->SimpleConstructionScript
            && Node->GetVariableName().ToString() == ComponentName)
        {
            return Node;
        }
    }
    return nullptr;
}

USCS_Node* FUEBlueprintEditorAdapter::FindSCSNodeInHierarchy(UBlueprint* Blueprint, const FString& ComponentName) const
{
    UBlueprint* Cursor = Blueprint;
    while (Cursor)
    {
        if (USCS_Node* Node = FindSCSNode(Cursor, ComponentName))
        {
            return Node;
        }

        UClass* ParentClass = Cursor->ParentClass;
        Cursor = ParentClass ? Cast<UBlueprint>(ParentClass->ClassGeneratedBy) : nullptr;
    }
    return nullptr;
}
