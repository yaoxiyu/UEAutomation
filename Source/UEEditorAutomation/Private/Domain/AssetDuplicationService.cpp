#include "Domain/AssetDuplicationService.h"

#if __has_include("AssetRegistry/AssetRegistryModule.h")
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#include "AssetToolsModule.h"
#include "Core/StableJsonWriter.h"
#include "Domain/BlueprintMetaCacheService.h"
#include "Engine/Blueprint.h"
#include "FileHelpers.h"
#include "IAssetTools.h"
#include "Kismet2/BlueprintEditorUtils.h"
#include "Kismet2/CompilerResultsLog.h"
#include "Kismet2/KismetEditorUtilities.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "Serialization/ArchiveReplaceObjectRef.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    UObject* LoadAssetByPath(const FString& AssetPath)
    {
        UObject* Asset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
        if (Asset)
        {
            // SavePackage refuses packages that have only been partially loaded.
            // StaticLoadObject brings in the requested object plus its dependency
            // chain but may leave sibling exports as placeholders. Force a full
            // load so any later SavePackage / DuplicateAsset call sees a complete
            // package graph.
            if (UPackage* Package = Asset->GetOutermost())
            {
                Package->FullyLoad();
            }
        }
        return Asset;
    }

    bool DoesAssetExist(const FString& ObjectPath)
    {
        FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        return Module.Get().GetAssetByObjectPath(*ObjectPath).IsValid();
    }

    void AddRedirectIfValid(TMap<UObject*, UObject*>& ReplacementMap, UObject* From, UObject* To)
    {
        if (From && To && From != To)
        {
            ReplacementMap.Add(From, To);
        }
    }

    void AddBlueprintGeneratedObjectRedirects(TMap<UObject*, UObject*>& ReplacementMap, UBlueprint* FromBP, UBlueprint* ToBP)
    {
        if (!FromBP || !ToBP || !FromBP->GeneratedClass || !ToBP->GeneratedClass)
        {
            return;
        }

        AddRedirectIfValid(ReplacementMap, FromBP->GeneratedClass, ToBP->GeneratedClass);

        UObject* FromCDO = FromBP->GeneratedClass->GetDefaultObject();
        UObject* ToCDO = ToBP->GeneratedClass->GetDefaultObject();
        AddRedirectIfValid(ReplacementMap, FromCDO, ToCDO);

        if (!FromCDO || !ToCDO)
        {
            return;
        }

        for (TFieldIterator<FObjectPropertyBase> It(FromCDO->GetClass(), EFieldIteratorFlags::IncludeSuper); It; ++It)
        {
            FObjectPropertyBase* FromProperty = *It;
            if (!FromProperty)
            {
                continue;
            }

            FObjectPropertyBase* ToProperty = FindFProperty<FObjectPropertyBase>(ToCDO->GetClass(), FromProperty->GetFName());
            if (!ToProperty)
            {
                continue;
            }

            UObject* FromObject = FromProperty->GetObjectPropertyValue_InContainer(FromCDO);
            UObject* ToObject = ToProperty->GetObjectPropertyValue_InContainer(ToCDO);
            AddRedirectIfValid(ReplacementMap, FromObject, ToObject);
        }
    }
}

bool FAssetDuplicationService::DuplicateAsset(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.SourceAssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("source_asset_path is required"), TEXT("payload.source_asset_path"));
        return false;
    }
    if (Request.DestinationPackagePath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("destination_package_path is required"), TEXT("payload.destination_package_path"));
        return false;
    }

    UObject* Source = LoadAssetByPath(Request.SourceAssetPath);
    if (!Source)
    {
        OutResult.AddError(TEXT("AssetNotFound"), FString::Printf(TEXT("Source asset not found: %s"), *Request.SourceAssetPath), TEXT("payload.source_asset_path"));
        return false;
    }

    FString DestName = Request.DestinationAssetName;
    if (DestName.IsEmpty())
    {
        DestName = Source->GetName();
    }

    const FString DestObjectPath = FString::Printf(TEXT("%s/%s.%s"), *Request.DestinationPackagePath, *DestName, *DestName);
    if (!Request.bOverwriteDestination && DoesAssetExist(DestObjectPath))
    {
        FAutomationAssetOutput Output;
        Output.AssetPath = DestObjectPath;
        Output.AssetName = DestName;
        Output.AssetType = Source->GetClass()->GetName();
        OutResult.AssetOutputs.Add(Output);
        OutResult.AddWarning(FString::Printf(TEXT("DestinationExists: %s (skipped)"), *DestObjectPath));
        OutResult.bSuccess = true;
        OutResult.Status = TEXT("succeeded");
        return true;
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    UObject* Duplicated = AssetToolsModule.Get().DuplicateAsset(DestName, Request.DestinationPackagePath, Source);
    if (!Duplicated)
    {
        OutResult.AddError(TEXT("DuplicateAssetFailed"), FString::Printf(TEXT("Failed to duplicate %s -> %s/%s"),
            *Request.SourceAssetPath, *Request.DestinationPackagePath, *DestName));
        return false;
    }

    UPackage* Package = Duplicated->GetOutermost();
    if (Package)
    {
        Package->MarkPackageDirty();
        FAssetRegistryModule::AssetCreated(Duplicated);

        TArray<UPackage*> Packages;
        Packages.Add(Package);
        FEditorFileUtils::PromptForCheckoutAndSave(Packages, /*bCheckDirty*/false, /*bPromptToSave*/false);
    }

    FAutomationAssetOutput Output;
    Output.AssetPath = Duplicated->GetPathName();
    Output.AssetName = Duplicated->GetName();
    Output.AssetType = Duplicated->GetClass()->GetName();
    OutResult.AssetOutputs.Add(Output);

    OutResult.bSuccess = true;
    OutResult.Status = TEXT("succeeded");
    return true;
}

bool FAssetDuplicationService::RedirectAssetReferences(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.TargetAsset.AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required"), TEXT("payload.target_asset.asset_path"));
        return false;
    }
    if (Request.AssetRedirects.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("redirects[] is required"), TEXT("payload.redirects"));
        return false;
    }

    UObject* Target = LoadAssetByPath(Request.TargetAsset.AssetPath);
    if (!Target)
    {
        OutResult.AddError(TEXT("AssetNotFound"), FString::Printf(TEXT("Target asset not found: %s"), *Request.TargetAsset.AssetPath), TEXT("payload.target_asset.asset_path"));
        return false;
    }

    // Build replacement map. For Blueprint, also map source generated class to dest generated class.
    TMap<UObject*, UObject*> ReplacementMap;
    int32 ResolvedCount = 0;
    int32 UnresolvedCount = 0;
    for (const FAutomationAssetRedirect& Redirect : Request.AssetRedirects)
    {
        UObject* From = LoadAssetByPath(Redirect.From);
        UObject* To = LoadAssetByPath(Redirect.To);
        if (!From || !To)
        {
            OutResult.AddWarning(FString::Printf(TEXT("RedirectAssetMissing: %s -> %s"), *Redirect.From, *Redirect.To));
            ++UnresolvedCount;
            continue;
        }
        AddRedirectIfValid(ReplacementMap, From, To);
        ++ResolvedCount;

        if (UBlueprint* FromBP = Cast<UBlueprint>(From))
        {
            UBlueprint* ToBP = Cast<UBlueprint>(To);
            AddBlueprintGeneratedObjectRedirects(ReplacementMap, FromBP, ToBP);
            if (ToBP && FromBP->SkeletonGeneratedClass && ToBP->SkeletonGeneratedClass)
            {
                AddRedirectIfValid(ReplacementMap, FromBP->SkeletonGeneratedClass, ToBP->SkeletonGeneratedClass);
            }
        }
    }

    if (ReplacementMap.Num() == 0)
    {
        OutResult.AddError(TEXT("NoResolvableRedirects"), TEXT("None of the redirects could be resolved to loaded UObjects."));
        return false;
    }

    int32 ReplacedCount = 0;
    {
        FArchiveReplaceObjectRef<UObject> Archive(
            Target,
            ReplacementMap,
            /*bNullPrivateRefs*/false,
            /*bIgnoreOuterRef*/true,
            /*bIgnoreArchetypeRef*/true);
        ReplacedCount = Archive.GetCount();
    }

    // Blueprint: also apply on GeneratedClass + CDO + nodes graphs.
    if (UBlueprint* TargetBP = Cast<UBlueprint>(Target))
    {
        if (TargetBP->GeneratedClass)
        {
            FArchiveReplaceObjectRef<UObject> ArClass(TargetBP->GeneratedClass, ReplacementMap, false, true, true);
            ReplacedCount += ArClass.GetCount();
            if (UObject* CDO = TargetBP->GeneratedClass->GetDefaultObject())
            {
                FArchiveReplaceObjectRef<UObject> ArCDO(CDO, ReplacementMap, false, true, true);
                ReplacedCount += ArCDO.GetCount();
            }
        }
        FBlueprintEditorUtils::MarkBlueprintAsStructurallyModified(TargetBP);
        FKismetEditorUtilities::CompileBlueprint(TargetBP);
    }

    Target->MarkPackageDirty();
    UPackage* Package = Target->GetOutermost();
    if (Package)
    {
        TArray<UPackage*> Packages;
        Packages.Add(Package);
        FEditorFileUtils::PromptForCheckoutAndSave(Packages, false, false);
    }

    FAutomationAssetOutput Output;
    Output.AssetPath = Target->GetPathName();
    Output.AssetName = Target->GetName();
    Output.AssetType = Target->GetClass()->GetName();
    OutResult.AssetOutputs.Add(Output);

    OutResult.AddLog(FString::Printf(TEXT("RedirectAssetReferences: resolved=%d unresolved=%d replaced=%d"),
        ResolvedCount, UnresolvedCount, ReplacedCount));
    OutResult.bSuccess = true;
    OutResult.Status = TEXT("succeeded");
    return true;
}

bool FAssetDuplicationService::ListDirectoryAssets(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.DirectoryPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("directory_path is required"), TEXT("payload.directory_path"));
        return false;
    }

    FString PackagePath = Request.DirectoryPath;
    if (PackagePath.EndsWith(TEXT("/")))
    {
        PackagePath.LeftChopInline(1);
    }

    FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
    IAssetRegistry& Registry = Module.Get();

    // Ensure the registry has scanned the path on disk (Project content roots are
    // usually scanned at startup; explicit scan is safe and idempotent).
    Registry.ScanPathsSynchronous({ PackagePath }, /*bForceRescan*/false);

    TArray<FAssetData> AssetDataArray;
    Registry.GetAssetsByPath(FName(*PackagePath), AssetDataArray, Request.bRecursive, /*bIncludeOnlyOnDiskAssets*/false);

    TArray<TSharedPtr<FJsonValue>> ItemsArray;
    for (const FAssetData& Data : AssetDataArray)
    {
        const TSharedRef<FJsonObject> Item = MakeShared<FJsonObject>();
        Item->SetStringField(TEXT("asset_path"), Data.ObjectPath.ToString());
        Item->SetStringField(TEXT("package_name"), Data.PackageName.ToString());
        Item->SetStringField(TEXT("asset_name"), Data.AssetName.ToString());
        Item->SetStringField(TEXT("asset_class"), Data.AssetClass.ToString());

        FString ParentClass;
        if (!Data.GetTagValue(FName(TEXT("ParentClass")), ParentClass))
        {
            // Some asset types report parent under "NativeParentClass".
            Data.GetTagValue(FName(TEXT("NativeParentClass")), ParentClass);
        }
        // Tag values may be exported as "Class'/Path.Name'" - strip quoting.
        if (ParentClass.StartsWith(TEXT("Class'")) && ParentClass.EndsWith(TEXT("'")))
        {
            ParentClass = ParentClass.Mid(6, ParentClass.Len() - 7);
        }
        Item->SetStringField(TEXT("parent_class"), ParentClass);

        ItemsArray.Add(MakeShared<FJsonValueObject>(Item));
    }

    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetStringField(TEXT("directory_path"), PackagePath);
    Root->SetBoolField(TEXT("recursive"), Request.bRecursive);
    Root->SetNumberField(TEXT("count"), ItemsArray.Num());
    Root->SetArrayField(TEXT("assets"), ItemsArray);

    // Write under the meta cache root so the artifact path is consistent with
    // other phase4 outputs.
    FBlueprintMetaCacheService Cache;
    bool bFallback = false;
    FString FallbackReason;
    const FString CacheRoot = Cache.ResolveCacheRoot(bFallback, FallbackReason);
    if (bFallback)
    {
        OutResult.AddWarning(FString::Printf(TEXT("CacheRootFallback: %s"), *FallbackReason));
    }

    FString Sanitized = PackagePath;
    Sanitized.RemoveFromStart(TEXT("/"));
    Sanitized.ReplaceInline(TEXT("/"), TEXT("__"));
    const FString ArtifactPath = FPaths::ConvertRelativePathToFull(CacheRoot / TEXT("DirectoryListings") / (Sanitized + TEXT(".listing.json")));

    FString WriteError;
    if (!FAutomationStableJsonWriter::WriteAtomic(ArtifactPath, Root, WriteError))
    {
        OutResult.AddError(TEXT("MetaCacheWriteFailed"), WriteError);
        return false;
    }

    FAutomationArtifactOutput Artifact;
    Artifact.ArtifactType = TEXT("directory_listing");
    Artifact.Path = ArtifactPath;
    Artifact.AssetPath = PackagePath;
    OutResult.Artifacts.Add(Artifact);

    OutResult.AddLog(FString::Printf(TEXT("ListDirectoryAssets: %d assets under %s"), ItemsArray.Num(), *PackagePath));
    OutResult.bSuccess = true;
    OutResult.Status = TEXT("succeeded");
    return true;
}
