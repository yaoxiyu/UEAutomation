#include "Domain/BlueprintMetaCacheService.h"

#include "Core/EditorAutomationSettings.h"
#include "Core/StableJsonWriter.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "Interfaces/IPluginManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Misc/SecureHash.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

namespace UEAutomation { namespace MetaCache
{
    const TCHAR* StatusHit = TEXT("hit");
    const TCHAR* StatusMissNoCache = TEXT("miss_no_cache");
    const TCHAR* StatusMissSchemaChanged = TEXT("miss_schema_changed");
    const TCHAR* StatusMissSourceChanged = TEXT("miss_source_changed");
    const TCHAR* StatusPartialHitSourceSameAssetChanged = TEXT("partial_hit_source_same_asset_changed");
    const TCHAR* StatusMissOptionsChanged = TEXT("miss_options_changed");
    const TCHAR* StatusForcedRefresh = TEXT("forced_refresh");
    const TCHAR* StatusMissSourceUnresolved = TEXT("miss_source_unresolved");
}}

namespace
{
    FString SanitizeSegment(const FString& Segment)
    {
        FString Result = Segment;
        const FString Invalid = TEXT("<>:\"|?*");
        for (TCHAR& Ch : Result)
        {
            if (Invalid.Contains(FString(1, &Ch)))
            {
                Ch = TEXT('_');
            }
        }
        return Result;
    }

    FString DefaultPluginCacheRoot()
    {
        const TSharedPtr<IPlugin> Plugin = IPluginManager::Get().FindPlugin(TEXT("UEEditorAutomation"));
        if (Plugin.IsValid())
        {
            return FPaths::ConvertRelativePathToFull(Plugin->GetBaseDir() / TEXT("Saved/BlueprintMetaCache"));
        }
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectPluginsDir() / TEXT("UEEditorAutomation/Saved/BlueprintMetaCache"));
    }

    FString ProjectFallbackCacheRoot()
    {
        return FPaths::ConvertRelativePathToFull(FPaths::ProjectSavedDir() / TEXT("UEAutomation/BlueprintMetaCache"));
    }

    bool TryEnsureWritableDir(const FString& Dir)
    {
        IFileManager& FileManager = IFileManager::Get();
        if (!FileManager.DirectoryExists(*Dir))
        {
            if (!FileManager.MakeDirectory(*Dir, true))
            {
                return false;
            }
        }
        // Probe write by creating and deleting a temp file. Windows treats the
        // folder "Read-only" attribute as a hint only - DirectoryExists and
        // MakeDirectory will both succeed even when the folder cannot accept
        // new files.
        const FString Probe = Dir / TEXT(".uea_write_probe.tmp");
        FArchive* Writer = FileManager.CreateFileWriter(*Probe, FILEWRITE_None);
        if (!Writer)
        {
            return false;
        }
        Writer->Close();
        delete Writer;
        FileManager.Delete(*Probe, false, true, true);
        return true;
    }
}

FString FBlueprintMetaCacheService::ResolveCacheRoot(bool& bOutFallbackUsed, FString& OutFallbackReason) const
{
    bOutFallbackUsed = false;
    OutFallbackReason.Reset();

    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    FString Root = Settings ? Settings->BlueprintMetaCacheDir.Path : FString();
    if (Root.IsEmpty())
    {
        Root = DefaultPluginCacheRoot();
    }
    else if (FPaths::IsRelative(Root))
    {
        Root = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Root);
    }

    if (TryEnsureWritableDir(Root))
    {
        return Root;
    }

    bOutFallbackUsed = true;
    OutFallbackReason = FString::Printf(TEXT("Configured cache root not writable: %s"), *Root);
    const FString Fallback = ProjectFallbackCacheRoot();
    TryEnsureWritableDir(Fallback);
    return Fallback;
}

FString FBlueprintMetaCacheService::NormalizeToPackageName(const FString& AssetPathOrPackageName)
{
    FString PackageName = AssetPathOrPackageName;
    PackageName.TrimStartAndEndInline();
    if (PackageName.IsEmpty())
    {
        return PackageName;
    }
    int32 DotIndex = INDEX_NONE;
    if (PackageName.FindChar(TEXT('.'), DotIndex))
    {
        PackageName.LeftInline(DotIndex);
    }
    return PackageName;
}

bool FBlueprintMetaCacheService::ResolveMetaPathForAsset(
    const FString& AssetPathOrPackageName,
    FString& OutMetaPath,
    FString& OutError) const
{
    const FString PackageName = NormalizeToPackageName(AssetPathOrPackageName);
    if (PackageName.IsEmpty() || !PackageName.StartsWith(TEXT("/")))
    {
        OutError = FString::Printf(TEXT("Invalid asset path or package name: %s"), *AssetPathOrPackageName);
        return false;
    }

    bool bFallback = false;
    FString FallbackReason;
    const FString Root = ResolveCacheRoot(bFallback, FallbackReason);

    FString Trimmed = PackageName;
    Trimmed.RemoveFromStart(TEXT("/"));

    TArray<FString> Segments;
    Trimmed.ParseIntoArray(Segments, TEXT("/"), true);
    if (Segments.Num() == 0)
    {
        OutError = FString::Printf(TEXT("Cannot derive segments from package name: %s"), *PackageName);
        return false;
    }

    FString RelativePath;
    for (int32 Index = 0; Index < Segments.Num(); ++Index)
    {
        const FString Sanitized = SanitizeSegment(Segments[Index]);
        if (Index > 0)
        {
            RelativePath /= Sanitized;
        }
        else
        {
            RelativePath = Sanitized;
        }
    }
    RelativePath += TEXT(".meta.json");

    OutMetaPath = FPaths::ConvertRelativePathToFull(Root / RelativePath);
    return true;
}

bool FBlueprintMetaCacheService::TryReadMeta(
    const FString& MetaPath,
    TSharedPtr<FJsonObject>& OutMeta,
    FString& OutError) const
{
    OutMeta.Reset();
    if (MetaPath.IsEmpty())
    {
        OutError = TEXT("Empty meta path");
        return false;
    }
    if (!IFileManager::Get().FileExists(*MetaPath))
    {
        // Not an error: treat as "no cache".
        return false;
    }

    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *MetaPath))
    {
        OutError = FString::Printf(TEXT("MetaCacheReadFailed: cannot read %s"), *MetaPath);
        return false;
    }

    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    TSharedPtr<FJsonObject> Root;
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutError = FString::Printf(TEXT("MetaCacheReadFailed: malformed JSON at %s"), *MetaPath);
        return false;
    }

    OutMeta = Root;
    return true;
}

bool FBlueprintMetaCacheService::WriteMetaAtomic(
    const FString& MetaPath,
    const TSharedRef<FJsonObject>& Meta,
    FString& OutError) const
{
    return FAutomationStableJsonWriter::WriteAtomic(MetaPath, Meta, OutError);
}

bool FBlueprintMetaCacheService::IsSchemaCompatible(const TSharedPtr<FJsonObject>& Meta) const
{
    if (!Meta.IsValid())
    {
        return false;
    }
    double Version = 0.0;
    if (!Meta->TryGetNumberField(TEXT("schema_version"), Version))
    {
        return false;
    }
    return static_cast<int32>(Version) == UEAutomation::MetaCache::CurrentSchemaVersion;
}

namespace
{
    FString GetStringChain(const TSharedPtr<FJsonObject>& Object, const TArray<const TCHAR*>& Path)
    {
        TSharedPtr<FJsonObject> Current = Object;
        for (int32 Index = 0; Index < Path.Num(); ++Index)
        {
            if (!Current.IsValid())
            {
                return FString();
            }
            const TCHAR* Field = Path[Index];
            if (Index == Path.Num() - 1)
            {
                FString Out;
                Current->TryGetStringField(Field, Out);
                return Out;
            }
            const TSharedPtr<FJsonObject>* Child = nullptr;
            if (!Current->TryGetObjectField(Field, Child) || !Child || !Child->IsValid())
            {
                return FString();
            }
            Current = *Child;
        }
        return FString();
    }
}

FString FBlueprintMetaCacheService::DecideCacheStatus(
    const TSharedPtr<FJsonObject>& OldMeta,
    const FAutomationCacheDecisionInput& Input) const
{
    using namespace UEAutomation::MetaCache;

    if (Input.bForceRefresh)
    {
        return StatusForcedRefresh;
    }
    if (!OldMeta.IsValid() || !Input.bMetaPresent)
    {
        return StatusMissNoCache;
    }
    if (!Input.bSchemaCompatible)
    {
        return StatusMissSchemaChanged;
    }
    if (!Input.bSourceResolved)
    {
        return StatusMissSourceUnresolved;
    }

    const FString OldNativeParent = GetStringChain(OldMeta, { TEXT("parent_class"), TEXT("native_parent"), TEXT("class_path") });
    if (!OldNativeParent.Equals(Input.CurrentNativeParentClassPath, ESearchCase::CaseSensitive))
    {
        return StatusMissSourceChanged;
    }

    const FString OldCombinedMd5 = GetStringChain(OldMeta, { TEXT("source_fingerprint"), TEXT("combined_md5") });
    if (!OldCombinedMd5.Equals(Input.CurrentParentCppCombinedMd5, ESearchCase::IgnoreCase))
    {
        return StatusMissSourceChanged;
    }

    const FString OldAssetMd5 = GetStringChain(OldMeta, { TEXT("asset"), TEXT("asset_fingerprint"), TEXT("package_file_md5") });
    if (!OldAssetMd5.Equals(Input.CurrentAssetPackageMd5, ESearchCase::IgnoreCase))
    {
        return StatusPartialHitSourceSameAssetChanged;
    }

    const FString OldOptionsDigest = GetStringChain(OldMeta, { TEXT("cache"), TEXT("analysis_options_digest") });
    if (!OldOptionsDigest.Equals(Input.CurrentAnalysisOptionsDigest, ESearchCase::IgnoreCase))
    {
        return StatusMissOptionsChanged;
    }

    return StatusHit;
}

FString FBlueprintMetaCacheService::ComputeOptionsDigest(const FAutomationAnalysisOptions& Options)
{
    FString Manifest;
    Manifest += TEXT("schema=analysis-options-v1\n");
    Manifest += FString::Printf(TEXT("include_native_cxx=%d\n"), Options.bIncludeNativeCxx ? 1 : 0);
    Manifest += FString::Printf(TEXT("include_blueprint_snapshot=%d\n"), Options.bIncludeBlueprintSnapshot ? 1 : 0);
    Manifest += FString::Printf(TEXT("include_class_defaults=%d\n"), Options.bIncludeClassDefaults ? 1 : 0);
    Manifest += FString::Printf(TEXT("include_components=%d\n"), Options.bIncludeComponents ? 1 : 0);
    Manifest += FString::Printf(TEXT("include_references=%d\n"), Options.bIncludeReferences ? 1 : 0);
    Manifest += FString::Printf(TEXT("include_referencers=%d\n"), Options.bIncludeReferencers ? 1 : 0);
    Manifest += FString::Printf(TEXT("include_graph_summary=%d\n"), Options.bIncludeGraphSummary ? 1 : 0);
    Manifest += FString::Printf(TEXT("include_graph_pins=%d\n"), Options.bIncludeGraphPins ? 1 : 0);
    Manifest += FString::Printf(TEXT("export_only_editable_properties=%d\n"), Options.bExportOnlyEditableProperties ? 1 : 0);
    Manifest += FString::Printf(TEXT("reference_depth=%d\n"), Options.ReferenceDepth);
    Manifest += FString::Printf(TEXT("max_nodes=%d\n"), Options.MaxNodes);
    Manifest += FString::Printf(TEXT("max_edges=%d\n"), Options.MaxEdges);
    Manifest += FString::Printf(TEXT("max_property_depth=%d\n"), Options.MaxPropertyDepth);
    Manifest += FString::Printf(TEXT("max_array_elements=%d\n"), Options.MaxArrayElements);

    FTCHARToUTF8 Utf8(*Manifest);
    return FMD5::HashBytes(reinterpret_cast<const uint8*>(Utf8.Get()), Utf8.Length());
}
