#pragma once

#include "CoreMinimal.h"
#include "Dom/JsonObject.h"

struct FAutomationAnalysisOptions;

namespace UEAutomation { namespace MetaCache
{
    static constexpr int32 CurrentSchemaVersion = 2;

    extern UEEDITORAUTOMATION_API const TCHAR* StatusHit;
    extern UEEDITORAUTOMATION_API const TCHAR* StatusMissNoCache;
    extern UEEDITORAUTOMATION_API const TCHAR* StatusMissSchemaChanged;
    extern UEEDITORAUTOMATION_API const TCHAR* StatusMissSourceChanged;
    extern UEEDITORAUTOMATION_API const TCHAR* StatusPartialHitSourceSameAssetChanged;
    extern UEEDITORAUTOMATION_API const TCHAR* StatusMissOptionsChanged;
    extern UEEDITORAUTOMATION_API const TCHAR* StatusForcedRefresh;
    extern UEEDITORAUTOMATION_API const TCHAR* StatusMissSourceUnresolved;
}}

struct FAutomationCacheDecisionInput
{
    bool bForceRefresh = false;
    bool bSchemaCompatible = false;
    bool bMetaPresent = false;
    bool bSourceResolved = true;
    FString CurrentNativeParentClassPath;
    FString CurrentParentCppCombinedMd5;
    FString CurrentAssetPackageMd5;
    FString CurrentAnalysisOptionsDigest;
};

class FBlueprintMetaCacheService
{
public:
    FString ResolveCacheRoot(bool& bOutFallbackUsed, FString& OutFallbackReason) const;

    bool ResolveMetaPathForAsset(
        const FString& AssetPathOrPackageName,
        FString& OutMetaPath,
        FString& OutError) const;

    bool TryReadMeta(
        const FString& MetaPath,
        TSharedPtr<FJsonObject>& OutMeta,
        FString& OutError) const;

    bool WriteMetaAtomic(
        const FString& MetaPath,
        const TSharedRef<FJsonObject>& Meta,
        FString& OutError) const;

    bool IsSchemaCompatible(const TSharedPtr<FJsonObject>& Meta) const;

    // Compute the cache status string from old meta + current input.
    FString DecideCacheStatus(
        const TSharedPtr<FJsonObject>& OldMeta,
        const FAutomationCacheDecisionInput& Input) const;

    // Stable digest of analysis options that affect output content. Two
    // requests with the same digest are guaranteed to produce equivalent
    // exports for the same asset and parent C++ source.
    static FString ComputeOptionsDigest(const FAutomationAnalysisOptions& Options);

    // Convert /Game/Foo/Bar.Bar (or /Game/Foo/Bar) to its package name:
    //   /Game/Foo/Bar
    static FString NormalizeToPackageName(const FString& AssetPathOrPackageName);
};
