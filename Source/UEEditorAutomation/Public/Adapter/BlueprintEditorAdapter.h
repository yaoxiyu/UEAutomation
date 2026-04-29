#pragma once

#include "CoreMinimal.h"

class UBlueprint;

class IBlueprintEditorAdapter
{
public:
    virtual ~IBlueprintEditorAdapter() {}

    virtual UBlueprint* CreateBlueprintAsset(const FString& PackagePath, const FString& AssetName, UClass* ParentClass, FString& OutError) = 0;
    virtual UBlueprint* LoadBlueprintAsset(const FString& AssetPath, FString& OutError) = 0;
    virtual bool DoesAssetExist(const FString& PackagePath, const FString& AssetName) const = 0;
    virtual bool AddComponentNode(UBlueprint* Blueprint, UClass* ComponentClass, const FString& ComponentName, const FString& AttachParentName, FString& OutError) = 0;
    virtual bool RemoveComponentNode(UBlueprint* Blueprint, const FString& ComponentName, FString& OutError) = 0;
    virtual UObject* GetComponentTemplate(UBlueprint* Blueprint, const FString& ComponentName, const FString& LookupPolicy, FString& OutError) = 0;
    virtual bool ApplyComponentTransform(UObject* ComponentTemplate, const struct FAutomationTransformSpec& Transform, FString& OutError) = 0;
    virtual bool CompileBlueprint(UBlueprint* Blueprint, FString& OutError) = 0;
    virtual bool SaveAsset(UObject* Asset, FString& OutError) = 0;
    virtual bool OpenAsset(UObject* Asset, FString& OutError) = 0;
};
