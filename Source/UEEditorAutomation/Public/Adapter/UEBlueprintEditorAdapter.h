#pragma once

#include "Adapter/BlueprintEditorAdapter.h"

class FUEBlueprintEditorAdapter : public IBlueprintEditorAdapter
{
public:
    virtual UBlueprint* CreateBlueprintAsset(const FString& PackagePath, const FString& AssetName, UClass* ParentClass, FString& OutError) override;
    virtual UBlueprint* LoadBlueprintAsset(const FString& AssetPath, FString& OutError) override;
    virtual bool DoesAssetExist(const FString& PackagePath, const FString& AssetName) const override;
    virtual bool AddComponentNode(UBlueprint* Blueprint, UClass* ComponentClass, const FString& ComponentName, const FString& AttachParentName, FString& OutError) override;
    virtual bool RemoveComponentNode(UBlueprint* Blueprint, const FString& ComponentName, FString& OutError) override;
    virtual UObject* GetComponentTemplate(UBlueprint* Blueprint, const FString& ComponentName, const FString& LookupPolicy, FString& OutError) override;
    virtual bool ApplyComponentTransform(UObject* ComponentTemplate, const struct FAutomationTransformSpec& Transform, FString& OutError) override;
    virtual bool CompileBlueprint(UBlueprint* Blueprint, FString& OutError) override;
    virtual bool SaveAsset(UObject* Asset, FString& OutError) override;
    virtual bool OpenAsset(UObject* Asset, FString& OutError) override;

private:
    class USCS_Node* FindSCSNode(class UBlueprint* Blueprint, const FString& ComponentName) const;
    class USCS_Node* FindSCSNodeInHierarchy(class UBlueprint* Blueprint, const FString& ComponentName) const;
    UObject* FindNativeComponentTemplate(class UBlueprint* Blueprint, const FString& ComponentName) const;
};
