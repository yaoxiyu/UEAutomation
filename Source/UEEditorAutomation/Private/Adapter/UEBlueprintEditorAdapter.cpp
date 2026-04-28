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
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "UObject/Package.h"
#include "UObject/SavePackage.h"
#include "Engine/SimpleConstructionScript.h"
#include "Engine/SCS_Node.h"
#include "Subsystems/AssetEditorSubsystem.h"

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

    if (FindSCSNode(Blueprint, ComponentName))
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

UObject* FUEBlueprintEditorAdapter::GetComponentTemplate(UBlueprint* Blueprint, const FString& ComponentName, FString& OutError)
{
    USCS_Node* Node = FindSCSNode(Blueprint, ComponentName);
    if (!Node)
    {
        OutError = FString::Printf(TEXT("Component '%s' not found."), *ComponentName);
        return nullptr;
    }

    UBlueprintGeneratedClass* BlueprintGeneratedClass = nullptr;
    if (Blueprint && Blueprint->GeneratedClass)
    {
        BlueprintGeneratedClass = Cast<UBlueprintGeneratedClass>(Blueprint->GeneratedClass.Get());
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
    const FString PackageFilename = FPackageName::LongPackageNameToFilename(Package->GetName(), FPackageName::GetAssetPackageExtension());

#if ENGINE_MAJOR_VERSION >= 5
    FSavePackageArgs SaveArgs;
    SaveArgs.TopLevelFlags = RF_Public | RF_Standalone;
    SaveArgs.SaveFlags = SAVE_NoError;
    const bool bSaved = UPackage::SavePackage(Package, Asset, *PackageFilename, SaveArgs);
#else
    const bool bSaved = UPackage::SavePackage(Package, Asset, RF_Public | RF_Standalone, *PackageFilename, GError, nullptr, false, true, SAVE_NoError, nullptr);
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

USCS_Node* FUEBlueprintEditorAdapter::FindSCSNode(UBlueprint* Blueprint, const FString& ComponentName) const
{
    if (!Blueprint || !Blueprint->SimpleConstructionScript)
    {
        return nullptr;
    }

    const TArray<USCS_Node*> Nodes = Blueprint->SimpleConstructionScript->GetAllNodes();
    for (USCS_Node* Node : Nodes)
    {
        if (Node && Node->GetVariableName().ToString() == ComponentName)
        {
            return Node;
        }
    }
    return nullptr;
}
