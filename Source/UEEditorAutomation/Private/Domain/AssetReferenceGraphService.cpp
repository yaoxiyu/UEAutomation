#include "Domain/AssetReferenceGraphService.h"

#if __has_include("AssetRegistry/AssetRegistryModule.h")
#include "AssetRegistry/AssetRegistryModule.h"
#else
#include "AssetRegistryModule.h"
#endif
#if __has_include("AssetRegistry/IAssetRegistry.h")
#include "AssetRegistry/IAssetRegistry.h"
#else
#include "IAssetRegistry.h"
#endif
#include "Domain/BlueprintMetaCacheService.h"
#include "Misc/PackageName.h"
#include "Protocol/AutomationProtocolTypes.h"

namespace
{
    IAssetRegistry& GetAssetRegistry()
    {
        FAssetRegistryModule& Module = FModuleManager::LoadModuleChecked<FAssetRegistryModule>(TEXT("AssetRegistry"));
        return Module.Get();
    }

    FName ToPackageFName(const FString& AssetPath)
    {
        return FName(*FBlueprintMetaCacheService::NormalizeToPackageName(AssetPath));
    }

    FString PackageNameToObjectPath(const FName& PackageName, IAssetRegistry& Registry)
    {
        TArray<FAssetData> AssetData;
        Registry.GetAssetsByPackageName(PackageName, AssetData);
        if (AssetData.Num() > 0)
        {
            return AssetData[0].ObjectPath.ToString();
        }
        return PackageName.ToString();
    }

    FString GetAssetClass(const FName& PackageName, IAssetRegistry& Registry)
    {
        TArray<FAssetData> AssetData;
        Registry.GetAssetsByPackageName(PackageName, AssetData);
        if (AssetData.Num() > 0)
        {
            return AssetData[0].AssetClass.ToString();
        }
        return FString();
    }

    bool IsBlueprintClass(const FString& AssetClass)
    {
        if (AssetClass.IsEmpty())
        {
            return false;
        }
        return AssetClass == TEXT("Blueprint")
            || AssetClass == TEXT("WidgetBlueprint")
            || AssetClass == TEXT("AnimBlueprint");
    }
}

bool FAssetReferenceGraphService::ExportDirectReferences(
    const FString& AssetPath,
    const FAutomationAnalysisOptions& Options,
    const TSharedRef<FJsonObject>& OutJson,
    FAutomationTaskResult& OutResult) const
{
    const FName PackageName = ToPackageFName(AssetPath);
    if (PackageName.IsNone())
    {
        OutResult.AddError(TEXT("AnalysisAssetNotFound"), TEXT("Empty asset path"));
        return false;
    }

    IAssetRegistry& Registry = GetAssetRegistry();

    TArray<FName> Dependencies;
    Registry.GetDependencies(PackageName, Dependencies, EAssetRegistryDependencyType::Hard);
    TArray<FName> SoftDependencies;
    Registry.GetDependencies(PackageName, SoftDependencies, EAssetRegistryDependencyType::Soft);
    TArray<FName> Referencers;
    if (Options.bIncludeReferencers)
    {
        Registry.GetReferencers(PackageName, Referencers, EAssetRegistryDependencyType::Hard);
    }

    auto BuildEntry = [&Registry](const FName& Pkg, const TCHAR* Type, const TCHAR* Reason) -> TSharedRef<FJsonObject>
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        const FString AssetClass = GetAssetClass(Pkg, Registry);
        Object->SetStringField(TEXT("asset_path"), PackageNameToObjectPath(Pkg, Registry));
        Object->SetStringField(TEXT("asset_class"), AssetClass);
        Object->SetStringField(TEXT("dependency_type"), Type);
        Object->SetStringField(TEXT("reason"), Reason);
        return Object;
    };

    TArray<TSharedPtr<FJsonValue>> DepArray;
    TArray<TSharedPtr<FJsonValue>> RefArray;
    TArray<TSharedPtr<FJsonValue>> RefBlueprints;
    TArray<TSharedPtr<FJsonValue>> ReferencerBlueprints;

    for (const FName& Dep : Dependencies)
    {
        DepArray.Add(MakeShared<FJsonValueObject>(BuildEntry(Dep, TEXT("hard_package_dependency"), TEXT("AssetRegistry hard dependency"))));
        if (IsBlueprintClass(GetAssetClass(Dep, Registry)))
        {
            RefBlueprints.Add(MakeShared<FJsonValueString>(PackageNameToObjectPath(Dep, GetAssetRegistry())));
        }
    }
    for (const FName& Dep : SoftDependencies)
    {
        DepArray.Add(MakeShared<FJsonValueObject>(BuildEntry(Dep, TEXT("soft_package_dependency"), TEXT("AssetRegistry soft dependency"))));
        if (IsBlueprintClass(GetAssetClass(Dep, Registry)))
        {
            RefBlueprints.Add(MakeShared<FJsonValueString>(PackageNameToObjectPath(Dep, GetAssetRegistry())));
        }
    }
    for (const FName& Ref : Referencers)
    {
        RefArray.Add(MakeShared<FJsonValueObject>(BuildEntry(Ref, TEXT("referencer_package"), TEXT("AssetRegistry referencer"))));
        if (IsBlueprintClass(GetAssetClass(Ref, Registry)))
        {
            ReferencerBlueprints.Add(MakeShared<FJsonValueString>(PackageNameToObjectPath(Ref, GetAssetRegistry())));
        }
    }

    OutJson->SetArrayField(TEXT("direct_dependencies"), DepArray);
    OutJson->SetArrayField(TEXT("direct_referencers"), RefArray);
    OutJson->SetArrayField(TEXT("referenced_blueprints"), RefBlueprints);
    OutJson->SetArrayField(TEXT("referencer_blueprints"), ReferencerBlueprints);

    return true;
}

bool FAssetReferenceGraphService::ExportReferenceGraph(
    const FString& RootAssetPath,
    const FAutomationAnalysisOptions& Options,
    const FString& MetaCacheRoot,
    const TSharedRef<FJsonObject>& OutJson,
    FAutomationReferenceGraphMetrics& OutMetrics,
    FAutomationTaskResult& OutResult) const
{
    const FName RootPkg = ToPackageFName(RootAssetPath);
    if (RootPkg.IsNone())
    {
        OutResult.AddError(TEXT("AnalysisAssetNotFound"), TEXT("Empty root asset path"));
        return false;
    }

    IAssetRegistry& Registry = GetAssetRegistry();

    OutJson->SetStringField(TEXT("root_asset"), PackageNameToObjectPath(RootPkg, Registry));
    OutJson->SetNumberField(TEXT("max_depth"), Options.ReferenceDepth);
    OutJson->SetBoolField(TEXT("include_referencers"), Options.bIncludeReferencers);

    TMap<FName, int32> DepthByPackage;
    TArray<FName> Frontier;
    DepthByPackage.Add(RootPkg, 0);
    Frontier.Add(RootPkg);

    TArray<TSharedPtr<FJsonValue>> NodeArray;
    TArray<TSharedPtr<FJsonValue>> EdgeArray;
    TSet<FString> EmittedEdges;
    bool bCycle = false;

    auto BlueprintMetaCache = FBlueprintMetaCacheService();

    auto AppendNode = [&](const FName& Pkg, int32 Depth)
    {
        if (NodeArray.Num() >= Options.MaxNodes)
        {
            OutMetrics.bTruncated = true;
            return;
        }
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        const FString AssetClass = GetAssetClass(Pkg, Registry);
        Object->SetStringField(TEXT("asset_path"), PackageNameToObjectPath(Pkg, Registry));
        Object->SetStringField(TEXT("node_type"), IsBlueprintClass(AssetClass) ? TEXT("blueprint") : AssetClass);
        Object->SetNumberField(TEXT("depth"), Depth);
        FString MetaPath;
        FString MetaError;
        if (BlueprintMetaCache.ResolveMetaPathForAsset(Pkg.ToString(), MetaPath, MetaError))
        {
            Object->SetStringField(TEXT("meta_path"), MetaPath);
        }
        NodeArray.Add(MakeShared<FJsonValueObject>(Object));
    };

    auto AppendEdge = [&](const FName& From, const FName& To, const TCHAR* EdgeType, const TCHAR* Reason)
    {
        if (EdgeArray.Num() >= Options.MaxEdges)
        {
            OutMetrics.bTruncated = true;
            return;
        }
        const FString Key = From.ToString() + TEXT("->") + To.ToString() + TEXT("|") + EdgeType;
        if (EmittedEdges.Contains(Key))
        {
            return;
        }
        EmittedEdges.Add(Key);
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("from"), PackageNameToObjectPath(From, Registry));
        Object->SetStringField(TEXT("to"), PackageNameToObjectPath(To, Registry));
        Object->SetStringField(TEXT("edge_type"), EdgeType);
        Object->SetStringField(TEXT("reason"), Reason);
        EdgeArray.Add(MakeShared<FJsonValueObject>(Object));
    };

    AppendNode(RootPkg, 0);

    while (Frontier.Num() > 0 && !OutMetrics.bTruncated)
    {
        const FName Current = Frontier[0];
        Frontier.RemoveAt(0);
        const int32 Depth = DepthByPackage[Current];
        if (Depth >= Options.ReferenceDepth)
        {
            continue;
        }

        TArray<FName> Hard;
        TArray<FName> Soft;
        Registry.GetDependencies(Current, Hard, EAssetRegistryDependencyType::Hard);
        Registry.GetDependencies(Current, Soft, EAssetRegistryDependencyType::Soft);

        TArray<FName> Referencers;
        if (Options.bIncludeReferencers)
        {
            Registry.GetReferencers(Current, Referencers, EAssetRegistryDependencyType::Hard);
        }

        auto Visit = [&](const FName& Other, const TCHAR* EdgeType, const TCHAR* Reason, bool bOutbound)
        {
            if (!Options.bIncludeReferences && bOutbound)
            {
                return;
            }
            if (DepthByPackage.Contains(Other))
            {
                if (Other == RootPkg)
                {
                    bCycle = true;
                }
                if (bOutbound)
                {
                    AppendEdge(Current, Other, EdgeType, Reason);
                }
                else
                {
                    AppendEdge(Other, Current, EdgeType, Reason);
                }
                return;
            }
            DepthByPackage.Add(Other, Depth + 1);
            AppendNode(Other, Depth + 1);
            if (bOutbound)
            {
                AppendEdge(Current, Other, EdgeType, Reason);
            }
            else
            {
                AppendEdge(Other, Current, EdgeType, Reason);
            }
            // Only recurse into Blueprint nodes.
            if (IsBlueprintClass(GetAssetClass(Other, Registry)))
            {
                Frontier.Add(Other);
            }
        };

        for (const FName& Dep : Hard)
        {
            Visit(Dep, TEXT("hard_package_dependency"), TEXT("AssetRegistry hard dependency"), true);
        }
        for (const FName& Dep : Soft)
        {
            Visit(Dep, TEXT("soft_package_dependency"), TEXT("AssetRegistry soft dependency"), true);
        }
        for (const FName& Ref : Referencers)
        {
            Visit(Ref, TEXT("referencer_package"), TEXT("AssetRegistry referencer"), false);
        }
    }

    OutMetrics.NodeCount = NodeArray.Num();
    OutMetrics.EdgeCount = EdgeArray.Num();
    OutMetrics.bCycleDetected = bCycle;

    OutJson->SetArrayField(TEXT("nodes"), NodeArray);
    OutJson->SetArrayField(TEXT("edges"), EdgeArray);
    OutJson->SetBoolField(TEXT("truncated"), OutMetrics.bTruncated);
    OutJson->SetBoolField(TEXT("cycle_detected"), bCycle);

    if (OutMetrics.bTruncated)
    {
        OutResult.AddWarning(TEXT("ReferenceGraphLimitExceeded: max_nodes/max_edges hit"));
    }
    return true;
}
