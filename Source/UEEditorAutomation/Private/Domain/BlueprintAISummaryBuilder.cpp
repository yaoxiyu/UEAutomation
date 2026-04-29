#include "Domain/BlueprintAISummaryBuilder.h"

#include "Domain/NativeParentClassResolver.h"
#include "Engine/Blueprint.h"

namespace
{
    void CollectStringField(const TSharedPtr<FJsonObject>& Object, const TCHAR* ArrayField, TArray<TSharedPtr<FJsonValue>>& Out)
    {
        if (!Object.IsValid())
        {
            return;
        }
        const TArray<TSharedPtr<FJsonValue>>* Array = nullptr;
        if (!Object->TryGetArrayField(ArrayField, Array))
        {
            return;
        }
        for (const TSharedPtr<FJsonValue>& Value : *Array)
        {
            FString Str;
            if (Value->TryGetString(Str) && !Str.IsEmpty())
            {
                Out.Add(MakeShared<FJsonValueString>(Str));
            }
        }
    }
}

TSharedRef<FJsonObject> FBlueprintAISummaryBuilder::Build(
    UBlueprint* Blueprint,
    const FAutomationNativeParentInfo& ParentInfo,
    const TSharedPtr<FJsonObject>& NativeCxxJson,
    const TSharedPtr<FJsonObject>& BlueprintSnapshotJson,
    const TSharedPtr<FJsonObject>& ReferencesJson)
{
    const TSharedRef<FJsonObject> Summary = MakeShared<FJsonObject>();
    Summary->SetStringField(TEXT("summary_source"), TEXT("rule_based"));

    const FString BlueprintName = Blueprint ? Blueprint->GetName() : FString();
    Summary->SetStringField(TEXT("role_summary"),
        FString::Printf(TEXT("Blueprint %s inherits from %s (module %s)."),
            *BlueprintName,
            *ParentInfo.NativeParent.DisplayName,
            *ParentInfo.NativeParentModuleName));

    // Logic summary from graph subobject.
    FString LogicSummary;
    if (BlueprintSnapshotJson.IsValid())
    {
        const TSharedPtr<FJsonObject>* GraphsObj = nullptr;
        if (BlueprintSnapshotJson->TryGetObjectField(TEXT("graphs"), GraphsObj) && GraphsObj && GraphsObj->IsValid())
        {
            TArray<TSharedPtr<FJsonValue>> Events;
            TArray<TSharedPtr<FJsonValue>> Calls;
            CollectStringField(*GraphsObj, TEXT("entry_events"), Events);
            CollectStringField(*GraphsObj, TEXT("function_calls"), Calls);
            if (Events.Num() > 0 || Calls.Num() > 0)
            {
                FString EventList;
                for (const TSharedPtr<FJsonValue>& V : Events)
                {
                    EventList += V->AsString() + TEXT(" ");
                }
                FString CallList;
                for (const TSharedPtr<FJsonValue>& V : Calls)
                {
                    CallList += V->AsString() + TEXT(" ");
                }
                LogicSummary = FString::Printf(TEXT("Entry events: %s; calls: %s"),
                    EventList.IsEmpty() ? TEXT("(none)") : *EventList,
                    CallList.IsEmpty() ? TEXT("(none)") : *CallList);
            }
        }
    }
    Summary->SetStringField(TEXT("logic_summary"), LogicSummary);

    // Important parameters: editable defaults from blueprint snapshot.
    TArray<TSharedPtr<FJsonValue>> ImportantParameters;
    if (BlueprintSnapshotJson.IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* Defaults = nullptr;
        if (BlueprintSnapshotJson->TryGetArrayField(TEXT("class_defaults"), Defaults))
        {
            for (const TSharedPtr<FJsonValue>& Item : *Defaults)
            {
                const TSharedPtr<FJsonObject> Obj = Item->AsObject();
                if (!Obj.IsValid())
                {
                    continue;
                }
                bool bEditable = false;
                Obj->TryGetBoolField(TEXT("editable"), bEditable);
                if (!bEditable)
                {
                    continue;
                }
                FString Name;
                Obj->TryGetStringField(TEXT("name"), Name);
                if (!Name.IsEmpty())
                {
                    ImportantParameters.Add(MakeShared<FJsonValueString>(Name));
                }
            }
        }
    }
    Summary->SetArrayField(TEXT("important_parameters"), ImportantParameters);

    // Important references.
    TArray<TSharedPtr<FJsonValue>> ImportantReferences;
    if (ReferencesJson.IsValid())
    {
        CollectStringField(ReferencesJson, TEXT("referenced_blueprints"), ImportantReferences);
    }
    Summary->SetArrayField(TEXT("important_references"), ImportantReferences);

    Summary->SetStringField(TEXT("confidence"), TEXT("medium"));

    TArray<TSharedPtr<FJsonValue>> Limitations;
    Limitations.Add(MakeShared<FJsonValueString>(TEXT("Graph summary is read-only topology; no runtime simulation.")));
    Limitations.Add(MakeShared<FJsonValueString>(TEXT("Reference graph is bounded by max_nodes/max_edges/depth.")));
    if (NativeCxxJson.IsValid())
    {
        const TSharedPtr<FJsonObject>* ClassObj = nullptr;
        if (!NativeCxxJson->TryGetObjectField(TEXT("class"), ClassObj))
        {
            Limitations.Add(MakeShared<FJsonValueString>(TEXT("Native parent reflection block missing or partial.")));
        }
    }
    Summary->SetArrayField(TEXT("limitations"), Limitations);

    return Summary;
}
