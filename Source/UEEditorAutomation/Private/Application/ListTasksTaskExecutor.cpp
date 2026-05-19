#include "Application/ListTasksTaskExecutor.h"

#include "Core/StableJsonWriter.h"
#include "Domain/BlueprintMetaCacheService.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/Paths.h"

bool FListTasksTaskExecutor::Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    TArray<FString> TaskTypes;
    Registry.GetAllTaskTypes(TaskTypes);

    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("schema_version"), 1);
    Root->SetStringField(TEXT("artifact_type"), TEXT("task_catalog"));
    Root->SetNumberField(TEXT("count"), TaskTypes.Num());

    TArray<TSharedPtr<FJsonValue>> Tasks;
    for (const FString& TaskType : TaskTypes)
    {
        Tasks.Add(MakeShared<FJsonValueString>(TaskType));
    }
    Root->SetArrayField(TEXT("task_types"), Tasks);

    bool bFallback = false;
    FString FallbackReason;
    FBlueprintMetaCacheService Cache;
    const FString CacheRoot = Cache.ResolveCacheRoot(bFallback, FallbackReason);
    if (bFallback)
    {
        OutResult.AddWarning(FString::Printf(TEXT("CacheRootFallback: %s"), *FallbackReason));
    }

    const FString ArtifactPath = FPaths::ConvertRelativePathToFull(CacheRoot / TEXT("TaskCatalog/list_tasks.json"));
    FString WriteError;
    if (!FAutomationStableJsonWriter::WriteAtomic(ArtifactPath, Root, WriteError))
    {
        OutResult.AddError(TEXT("TaskCatalogWriteFailed"), WriteError);
        return false;
    }

    FAutomationArtifactOutput Artifact;
    Artifact.ArtifactType = TEXT("task_catalog");
    Artifact.Path = ArtifactPath;
    OutResult.Artifacts.Add(Artifact);

    return true;
}
