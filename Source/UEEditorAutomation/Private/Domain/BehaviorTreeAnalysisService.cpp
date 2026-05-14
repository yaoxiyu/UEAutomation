#include "Domain/BehaviorTreeAnalysisService.h"

#include "BehaviorTree/BehaviorTree.h"
#include "BehaviorTree/BlackboardData.h"
#include "BehaviorTree/BTCompositeNode.h"
#include "BehaviorTree/BTDecorator.h"
#include "BehaviorTree/BTService.h"
#include "BehaviorTree/BTTaskNode.h"
#include "Core/StableJsonWriter.h"
#include "Domain/BlueprintMetaCacheService.h"
#include "Misc/DateTime.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"
#include "UObject/UObjectGlobals.h"

namespace
{
    FString SanitizePathForCache(const FString& AssetPath, const FString& Suffix)
    {
        FString Sanitized = AssetPath;
        Sanitized.RemoveFromStart(TEXT("/"));
        Sanitized.ReplaceInline(TEXT("/"), TEXT("__"));
        Sanitized.ReplaceInline(TEXT("."), TEXT("_"));
        Sanitized.ReplaceInline(TEXT(":"), TEXT("_"));
        return Sanitized + Suffix;
    }

    TSharedRef<FJsonObject> ExportNodeBase(const UBTNode* Node)
    {
        const TSharedRef<FJsonObject> Json = MakeShared<FJsonObject>();
        if (!Node)
        {
            return Json;
        }
        Json->SetStringField(TEXT("node_name"), Node->GetNodeName());
        Json->SetStringField(TEXT("class"), Node->GetClass()->GetName());
        Json->SetStringField(TEXT("class_path"), Node->GetClass()->GetPathName());
        Json->SetNumberField(TEXT("execution_index"), Node->GetExecutionIndex());
        Json->SetNumberField(TEXT("tree_depth"), Node->GetTreeDepth());

        FString Description = Node->GetStaticDescription();
        if (!Description.IsEmpty())
        {
            Json->SetStringField(TEXT("description"), Description);
        }
        return Json;
    }

    void ExportServices(const TArray<UBTService*>& Services, TArray<TSharedPtr<FJsonValue>>& OutArray)
    {
        for (const UBTService* Service : Services)
        {
            if (!Service)
            {
                continue;
            }
            TSharedRef<FJsonObject> ServiceJson = ExportNodeBase(Service);
            ServiceJson->SetStringField(TEXT("node_type"), TEXT("service"));

            // Interval and RandomDeviation are protected; read via UE reflection.
            if (FProperty* IntervalProp = Service->GetClass()->FindPropertyByName(TEXT("Interval")))
            {
                FString Value;
                IntervalProp->ExportTextItem(Value, IntervalProp->ContainerPtrToValuePtr<void>(Service), nullptr, nullptr, PPF_None);
                ServiceJson->SetStringField(TEXT("interval"), Value);
            }
            if (FProperty* DeviationProp = Service->GetClass()->FindPropertyByName(TEXT("RandomDeviation")))
            {
                FString Value;
                DeviationProp->ExportTextItem(Value, DeviationProp->ContainerPtrToValuePtr<void>(Service), nullptr, nullptr, PPF_None);
                ServiceJson->SetStringField(TEXT("random_deviation"), Value);
            }

            OutArray.Add(MakeShared<FJsonValueObject>(ServiceJson));
        }
    }

    void ExportDecorators(const TArray<UBTDecorator*>& Decorators, TArray<TSharedPtr<FJsonValue>>& OutArray)
    {
        for (const UBTDecorator* Decorator : Decorators)
        {
            if (!Decorator)
            {
                continue;
            }
            TSharedRef<FJsonObject> DecJson = ExportNodeBase(Decorator);
            DecJson->SetStringField(TEXT("node_type"), TEXT("decorator"));
            DecJson->SetBoolField(TEXT("inverse_condition"), Decorator->IsInversed());

            FString AbortMode;
            switch (Decorator->GetFlowAbortMode())
            {
            case EBTFlowAbortMode::None:          AbortMode = TEXT("None"); break;
            case EBTFlowAbortMode::LowerPriority: AbortMode = TEXT("LowerPriority"); break;
            case EBTFlowAbortMode::Self:           AbortMode = TEXT("Self"); break;
            case EBTFlowAbortMode::Both:           AbortMode = TEXT("Both"); break;
            default:                               AbortMode = TEXT("Unknown"); break;
            }
            DecJson->SetStringField(TEXT("flow_abort_mode"), AbortMode);
            OutArray.Add(MakeShared<FJsonValueObject>(DecJson));
        }
    }

    TSharedRef<FJsonObject> ExportNodeRecursive(const UBTCompositeNode* CompositeNode, int32 MaxDepth, int32 CurrentDepth)
    {
        TSharedRef<FJsonObject> NodeJson = ExportNodeBase(CompositeNode);
        NodeJson->SetStringField(TEXT("node_type"), TEXT("composite"));

        FString CompositeType = CompositeNode->GetClass()->GetName();
        if (CompositeType.Contains(TEXT("Selector")))
        {
            NodeJson->SetStringField(TEXT("composite_type"), TEXT("selector"));
        }
        else if (CompositeType.Contains(TEXT("Sequence")))
        {
            NodeJson->SetStringField(TEXT("composite_type"), TEXT("sequence"));
        }
        else if (CompositeType.Contains(TEXT("SimpleParallel")))
        {
            NodeJson->SetStringField(TEXT("composite_type"), TEXT("simple_parallel"));
        }
        else
        {
            NodeJson->SetStringField(TEXT("composite_type"), CompositeType);
        }

        // Services on this composite
        if (CompositeNode->Services.Num() > 0)
        {
            TArray<TSharedPtr<FJsonValue>> ServicesArray;
            ExportServices(CompositeNode->Services, ServicesArray);
            NodeJson->SetArrayField(TEXT("services"), ServicesArray);
        }

        // Children
        TArray<TSharedPtr<FJsonValue>> ChildrenArray;
        for (int32 i = 0; i < CompositeNode->Children.Num(); ++i)
        {
            const FBTCompositeChild& Child = CompositeNode->Children[i];
            const TSharedRef<FJsonObject> ChildEntry = MakeShared<FJsonObject>();

            // Decorators on this child connection
            if (Child.Decorators.Num() > 0)
            {
                TArray<TSharedPtr<FJsonValue>> DecoratorsArray;
                ExportDecorators(Child.Decorators, DecoratorsArray);
                ChildEntry->SetArrayField(TEXT("decorators"), DecoratorsArray);
            }

            // Child node
            if (Child.ChildComposite && CurrentDepth < MaxDepth)
            {
                TSharedRef<FJsonObject> ChildNodeJson = ExportNodeRecursive(Child.ChildComposite, MaxDepth, CurrentDepth + 1);
                ChildEntry->SetObjectField(TEXT("node"), ChildNodeJson);
            }
            else if (Child.ChildTask)
            {
                TSharedRef<FJsonObject> TaskJson = ExportNodeBase(Child.ChildTask);
                TaskJson->SetStringField(TEXT("node_type"), TEXT("task"));

                if (Child.ChildTask->Services.Num() > 0)
                {
                    TArray<TSharedPtr<FJsonValue>> TaskServicesArray;
                    ExportServices(Child.ChildTask->Services, TaskServicesArray);
                    TaskJson->SetArrayField(TEXT("services"), TaskServicesArray);
                }

                ChildEntry->SetObjectField(TEXT("node"), TaskJson);
            }
            else if (Child.ChildComposite)
            {
                // Depth limit reached - emit stub
                TSharedRef<FJsonObject> StubJson = ExportNodeBase(Child.ChildComposite);
                StubJson->SetStringField(TEXT("node_type"), TEXT("composite"));
                StubJson->SetBoolField(TEXT("truncated"), true);
                ChildEntry->SetObjectField(TEXT("node"), StubJson);
            }

            ChildrenArray.Add(MakeShared<FJsonValueObject>(ChildEntry));
        }
        NodeJson->SetArrayField(TEXT("children"), ChildrenArray);

        return NodeJson;
    }

    void CountNodes(const UBTCompositeNode* CompositeNode, int32& OutComposites, int32& OutTasks, int32& OutDecorators, int32& OutServices)
    {
        if (!CompositeNode)
        {
            return;
        }
        OutComposites++;
        OutServices += CompositeNode->Services.Num();

        for (int32 i = 0; i < CompositeNode->Children.Num(); ++i)
        {
            const FBTCompositeChild& Child = CompositeNode->Children[i];
            OutDecorators += Child.Decorators.Num();

            if (Child.ChildComposite)
            {
                CountNodes(Child.ChildComposite, OutComposites, OutTasks, OutDecorators, OutServices);
            }
            else if (Child.ChildTask)
            {
                OutTasks++;
                OutServices += Child.ChildTask->Services.Num();
            }
        }
    }
}

bool FBehaviorTreeAnalysisService::AnalyzeBehaviorTree(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.TargetAsset.AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset.asset_path is required"), TEXT("payload.target_asset.asset_path"));
        return false;
    }
    return AnalyzeSingleBehaviorTree(Request.TargetAsset.AssetPath, Request, OutResult);
}

bool FBehaviorTreeAnalysisService::AnalyzeSingleBehaviorTree(
    const FString& AssetPath,
    const FAutomationTaskRequest& Request,
    FAutomationTaskResult& OutResult)
{
    const FDateTime AnalysisStart = FDateTime::UtcNow();
    OutResult.AddLog(FString::Printf(TEXT("AnalyzeBT: load %s"), *AssetPath));

    UObject* Asset = StaticLoadObject(UBehaviorTree::StaticClass(), nullptr, *AssetPath);
    UBehaviorTree* BT = Cast<UBehaviorTree>(Asset);
    if (!BT)
    {
        UObject* AnyAsset = StaticLoadObject(UObject::StaticClass(), nullptr, *AssetPath);
        if (AnyAsset)
        {
            OutResult.AddError(TEXT("AssetNotBehaviorTree"),
                FString::Printf(TEXT("Asset is not a BehaviorTree: %s (class: %s)"), *AssetPath, *AnyAsset->GetClass()->GetName()),
                TEXT("payload.target_asset.asset_path"));
        }
        else
        {
            OutResult.AddError(TEXT("AssetNotFound"),
                FString::Printf(TEXT("Asset not found: %s"), *AssetPath),
                TEXT("payload.target_asset.asset_path"));
        }
        return false;
    }

    OutResult.AddLog(TEXT("AnalyzeBT: build meta"));

    // Root JSON document
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema_version"), 1);
    Root->SetStringField(TEXT("artifact_type"), TEXT("behavior_tree_meta"));
    Root->SetStringField(TEXT("generated_at_utc"), FDateTime::UtcNow().ToIso8601());

    const TSharedRef<FJsonObject> Generator = MakeShared<FJsonObject>();
    Generator->SetStringField(TEXT("plugin_name"), TEXT("UEEditorAutomation"));
    Generator->SetStringField(TEXT("task_id"), OutResult.TaskId);
    Root->SetObjectField(TEXT("generator"), Generator);

    // Asset info
    const TSharedRef<FJsonObject> AssetJson = MakeShared<FJsonObject>();
    AssetJson->SetStringField(TEXT("asset_path"), BT->GetPathName());
    AssetJson->SetStringField(TEXT("asset_class"), BT->GetClass()->GetName());
    AssetJson->SetStringField(TEXT("asset_name"), BT->GetName());
    Root->SetObjectField(TEXT("asset"), AssetJson);

    // Blackboard
    UBlackboardData* BB = BT->GetBlackboardAsset();
    if (BB)
    {
        const TSharedRef<FJsonObject> BBJson = MakeShared<FJsonObject>();
        BBJson->SetStringField(TEXT("asset_path"), BB->GetPathName());
        BBJson->SetStringField(TEXT("asset_name"), BB->GetName());

        if (BB->Parent)
        {
            BBJson->SetStringField(TEXT("parent"), BB->Parent->GetPathName());
        }

        TArray<TSharedPtr<FJsonValue>> KeysArray;
        const TArray<FBlackboardEntry>& Keys = BB->Keys;
        for (const FBlackboardEntry& Entry : Keys)
        {
            const TSharedRef<FJsonObject> KeyJson = MakeShared<FJsonObject>();
            KeyJson->SetStringField(TEXT("name"), Entry.EntryName.ToString());
            KeyJson->SetStringField(TEXT("key_type"), Entry.KeyType ? Entry.KeyType->GetClass()->GetName() : TEXT("None"));
            KeyJson->SetBoolField(TEXT("instance_synced"), Entry.bInstanceSynced != 0);
#if WITH_EDITORONLY_DATA
            if (!Entry.EntryDescription.IsEmpty())
            {
                KeyJson->SetStringField(TEXT("description"), Entry.EntryDescription);
            }
#endif
            KeysArray.Add(MakeShared<FJsonValueObject>(KeyJson));
        }
        BBJson->SetArrayField(TEXT("keys"), KeysArray);

        // Include parent keys if available
        if (BB->Parent)
        {
            TArray<TSharedPtr<FJsonValue>> ParentKeysArray;
            const TArray<FBlackboardEntry>& ParentKeys = BB->Parent->Keys;
            for (const FBlackboardEntry& Entry : ParentKeys)
            {
                const TSharedRef<FJsonObject> KeyJson = MakeShared<FJsonObject>();
                KeyJson->SetStringField(TEXT("name"), Entry.EntryName.ToString());
                KeyJson->SetStringField(TEXT("key_type"), Entry.KeyType ? Entry.KeyType->GetClass()->GetName() : TEXT("None"));
                ParentKeysArray.Add(MakeShared<FJsonValueObject>(KeyJson));
            }
            if (ParentKeysArray.Num() > 0)
            {
                BBJson->SetArrayField(TEXT("parent_keys"), ParentKeysArray);
            }
        }

        Root->SetObjectField(TEXT("blackboard"), BBJson);
    }

    // Root decorators (used when BT is injected as subtree)
    if (BT->RootDecorators.Num() > 0)
    {
        TArray<TSharedPtr<FJsonValue>> RootDecoratorsArray;
        ExportDecorators(BT->RootDecorators, RootDecoratorsArray);
        Root->SetArrayField(TEXT("root_decorators"), RootDecoratorsArray);
    }

    // Tree structure
    int32 MaxDepth = Request.Analysis.MaxPropertyDepth > 0 ? Request.Analysis.MaxPropertyDepth : 32;
    if (BT->RootNode)
    {
        TSharedRef<FJsonObject> TreeJson = ExportNodeRecursive(BT->RootNode, MaxDepth, 0);
        Root->SetObjectField(TEXT("tree"), TreeJson);

        // Statistics
        int32 CompositeCount = 0, TaskCount = 0, DecoratorCount = 0, ServiceCount = 0;
        CountNodes(BT->RootNode, CompositeCount, TaskCount, DecoratorCount, ServiceCount);

        const TSharedRef<FJsonObject> Stats = MakeShared<FJsonObject>();
        Stats->SetNumberField(TEXT("composite_count"), CompositeCount);
        Stats->SetNumberField(TEXT("task_count"), TaskCount);
        Stats->SetNumberField(TEXT("decorator_count"), DecoratorCount);
        Stats->SetNumberField(TEXT("service_count"), ServiceCount);
        Stats->SetNumberField(TEXT("total_node_count"), CompositeCount + TaskCount + DecoratorCount + ServiceCount);
        Root->SetObjectField(TEXT("statistics"), Stats);
    }
    else
    {
        OutResult.AddWarning(TEXT("BehaviorTree has no RootNode."));
    }

    // Write to cache
    bool bFallback = false;
    FString FallbackReason;
    FBlueprintMetaCacheService Cache;
    const FString CacheRoot = Cache.ResolveCacheRoot(bFallback, FallbackReason);
    if (bFallback)
    {
        OutResult.AddWarning(FString::Printf(TEXT("CacheRootFallback: %s"), *FallbackReason));
    }

    const FString ArtifactPath = FPaths::ConvertRelativePathToFull(
        CacheRoot / TEXT("BehaviorTreeMeta") / SanitizePathForCache(BT->GetPathName(), TEXT(".bt.json")));

    FString WriteError;
    if (!FAutomationStableJsonWriter::WriteAtomic(ArtifactPath, Root, WriteError))
    {
        OutResult.AddError(TEXT("MetaCacheWriteFailed"), WriteError);
        return false;
    }
    OutResult.AddLog(FString::Printf(TEXT("AnalyzeBT: wrote %s"), *ArtifactPath));

    OutResult.Metrics.AnalysisDurationMs += (FDateTime::UtcNow() - AnalysisStart).GetTotalMilliseconds();
    OutResult.Metrics.AnalyzedBlueprintCount++;

    FAutomationAssetOutput Output;
    Output.AssetPath = AssetPath;
    Output.AssetName = BT->GetName();
    Output.AssetType = TEXT("behavior_tree");
    OutResult.AssetOutputs.Add(Output);

    FAutomationArtifactOutput Artifact;
    Artifact.ArtifactType = TEXT("behavior_tree_meta");
    Artifact.Path = ArtifactPath;
    Artifact.AssetPath = AssetPath;
    Artifact.CacheStatus = TEXT("written");
    OutResult.Artifacts.Add(Artifact);

    OutResult.bSuccess = true;
    OutResult.Status = TEXT("succeeded");
    return true;
}
