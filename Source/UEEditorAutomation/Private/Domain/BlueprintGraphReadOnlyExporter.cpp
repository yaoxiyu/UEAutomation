#include "Domain/BlueprintGraphReadOnlyExporter.h"

#include "EdGraph/EdGraph.h"
#include "EdGraph/EdGraphNode.h"
#include "EdGraph/EdGraphPin.h"
#include "Engine/Blueprint.h"
#include "K2Node_CallFunction.h"
#include "K2Node_Event.h"
#include "K2Node_VariableGet.h"
#include "K2Node_VariableSet.h"
#include "Protocol/AutomationProtocolTypes.h"

namespace
{
    void CollectGraphs(UBlueprint* Blueprint, TArray<UEdGraph*>& OutGraphs, TArray<FString>& OutGraphTypes)
    {
        if (!Blueprint)
        {
            return;
        }
        for (UEdGraph* Graph : Blueprint->UbergraphPages)
        {
            if (Graph)
            {
                OutGraphs.Add(Graph);
                OutGraphTypes.Add(TEXT("ubergraph"));
            }
        }
        for (UEdGraph* Graph : Blueprint->FunctionGraphs)
        {
            if (Graph)
            {
                OutGraphs.Add(Graph);
                OutGraphTypes.Add(TEXT("function"));
            }
        }
        for (UEdGraph* Graph : Blueprint->MacroGraphs)
        {
            if (Graph)
            {
                OutGraphs.Add(Graph);
                OutGraphTypes.Add(TEXT("macro"));
            }
        }
        for (UEdGraph* Graph : Blueprint->DelegateSignatureGraphs)
        {
            if (Graph)
            {
                OutGraphs.Add(Graph);
                OutGraphTypes.Add(TEXT("delegate"));
            }
        }
    }

    FString DescribeFunctionRef(UEdGraphNode* Node)
    {
        if (UK2Node_CallFunction* Call = Cast<UK2Node_CallFunction>(Node))
        {
            return Call->FunctionReference.GetMemberName().ToString();
        }
        return FString();
    }

    FString DescribeVariableRef(UEdGraphNode* Node)
    {
        if (UK2Node_VariableGet* Get = Cast<UK2Node_VariableGet>(Node))
        {
            return Get->GetVarName().ToString();
        }
        if (UK2Node_VariableSet* Set = Cast<UK2Node_VariableSet>(Node))
        {
            return Set->GetVarName().ToString();
        }
        return FString();
    }
}

bool FBlueprintGraphReadOnlyExporter::ExportGraphSummary(
    UBlueprint* Blueprint,
    const FAutomationAnalysisOptions& Options,
    const TSharedRef<FJsonObject>& OutJson,
    FAutomationTaskResult& OutResult) const
{
    if (!Blueprint)
    {
        OutResult.AddError(TEXT("BlueprintGraphExportFailed"), TEXT("Blueprint is null"));
        return false;
    }

    TArray<UEdGraph*> Graphs;
    TArray<FString> GraphTypes;
    CollectGraphs(Blueprint, Graphs, GraphTypes);

    int32 TotalNodes = 0;
    TSet<FString> EntryEvents;
    TSet<FString> FunctionCalls;
    TSet<FString> VariableReads;
    TSet<FString> VariableWrites;

    TArray<TSharedPtr<FJsonValue>> GraphArray;
    int32 TotalNodesEmitted = 0;
    bool bTruncated = false;

    for (int32 GraphIndex = 0; GraphIndex < Graphs.Num(); ++GraphIndex)
    {
        UEdGraph* Graph = Graphs[GraphIndex];
        if (!Graph)
        {
            continue;
        }

        const TSharedRef<FJsonObject> GraphObject = MakeShared<FJsonObject>();
        GraphObject->SetStringField(TEXT("name"), Graph->GetName());
        GraphObject->SetStringField(TEXT("graph_type"), GraphTypes[GraphIndex]);
        GraphObject->SetNumberField(TEXT("node_count"), Graph->Nodes.Num());

        TArray<TSharedPtr<FJsonValue>> NodeArray;
        TArray<TSharedPtr<FJsonValue>> EdgeArray;

        for (UEdGraphNode* Node : Graph->Nodes)
        {
            if (!Node)
            {
                continue;
            }
            ++TotalNodes;

            if (Options.bIncludeGraphPins)
            {
                if (TotalNodesEmitted >= Options.MaxNodes)
                {
                    bTruncated = true;
                    break;
                }
                ++TotalNodesEmitted;
            }

            const FString NodeId = Node->NodeGuid.ToString();
            const FString NodeClass = Node->GetClass()->GetName();
            const FString NodeTitle = Node->GetNodeTitle(ENodeTitleType::ListView).ToString();

            if (UK2Node_Event* EventNode = Cast<UK2Node_Event>(Node))
            {
                EntryEvents.Add(EventNode->EventReference.GetMemberName().ToString());
            }
            const FString FuncName = DescribeFunctionRef(Node);
            if (!FuncName.IsEmpty())
            {
                FunctionCalls.Add(FuncName);
            }
            if (Cast<UK2Node_VariableGet>(Node))
            {
                VariableReads.Add(DescribeVariableRef(Node));
            }
            else if (Cast<UK2Node_VariableSet>(Node))
            {
                VariableWrites.Add(DescribeVariableRef(Node));
            }

            if (Options.bIncludeGraphPins)
            {
                const TSharedRef<FJsonObject> NodeObject = MakeShared<FJsonObject>();
                NodeObject->SetStringField(TEXT("node_id"), NodeId);
                NodeObject->SetStringField(TEXT("node_class"), NodeClass);
                NodeObject->SetStringField(TEXT("title"), NodeTitle);
                NodeObject->SetStringField(TEXT("function_reference"), FuncName);
                NodeObject->SetStringField(TEXT("variable_reference"), DescribeVariableRef(Node));

                TArray<TSharedPtr<FJsonValue>> PinArray;
                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin)
                    {
                        continue;
                    }
                    const TSharedRef<FJsonObject> PinObject = MakeShared<FJsonObject>();
                    PinObject->SetStringField(TEXT("name"), Pin->PinName.ToString());
                    PinObject->SetStringField(TEXT("direction"), Pin->Direction == EGPD_Input ? TEXT("input") : TEXT("output"));
                    PinObject->SetStringField(TEXT("category"), Pin->PinType.PinCategory.ToString());
                    TArray<TSharedPtr<FJsonValue>> LinkedTo;
                    for (UEdGraphPin* Linked : Pin->LinkedTo)
                    {
                        if (!Linked || !Linked->GetOwningNode())
                        {
                            continue;
                        }
                        LinkedTo.Add(MakeShared<FJsonValueString>(
                            Linked->GetOwningNode()->NodeGuid.ToString() + TEXT(".") + Linked->PinName.ToString()));
                    }
                    PinObject->SetArrayField(TEXT("linked_to"), LinkedTo);
                    PinArray.Add(MakeShared<FJsonValueObject>(PinObject));
                }
                NodeObject->SetArrayField(TEXT("pins"), PinArray);
                NodeArray.Add(MakeShared<FJsonValueObject>(NodeObject));

                for (UEdGraphPin* Pin : Node->Pins)
                {
                    if (!Pin || Pin->Direction != EGPD_Output)
                    {
                        continue;
                    }
                    for (UEdGraphPin* Linked : Pin->LinkedTo)
                    {
                        if (!Linked || !Linked->GetOwningNode())
                        {
                            continue;
                        }
                        if (EdgeArray.Num() >= Options.MaxEdges)
                        {
                            bTruncated = true;
                            break;
                        }
                        const TSharedRef<FJsonObject> EdgeObject = MakeShared<FJsonObject>();
                        EdgeObject->SetStringField(TEXT("from"), NodeId + TEXT(".") + Pin->PinName.ToString());
                        EdgeObject->SetStringField(TEXT("to"),
                            Linked->GetOwningNode()->NodeGuid.ToString() + TEXT(".") + Linked->PinName.ToString());
                        EdgeObject->SetStringField(TEXT("pin_category"), Pin->PinType.PinCategory.ToString());
                        EdgeArray.Add(MakeShared<FJsonValueObject>(EdgeObject));
                    }
                    if (bTruncated)
                    {
                        break;
                    }
                }
            }
        }

        if (Options.bIncludeGraphPins)
        {
            GraphObject->SetArrayField(TEXT("nodes"), NodeArray);
            GraphObject->SetArrayField(TEXT("edges"), EdgeArray);
        }
        GraphArray.Add(MakeShared<FJsonValueObject>(GraphObject));
    }

    auto SetToArray = [](const TSet<FString>& Set) -> TArray<TSharedPtr<FJsonValue>>
    {
        TArray<TSharedPtr<FJsonValue>> Out;
        for (const FString& Item : Set)
        {
            if (!Item.IsEmpty())
            {
                Out.Add(MakeShared<FJsonValueString>(Item));
            }
        }
        return Out;
    };

    OutJson->SetBoolField(TEXT("summary_only"), !Options.bIncludeGraphPins);
    OutJson->SetNumberField(TEXT("graph_count"), Graphs.Num());
    OutJson->SetNumberField(TEXT("node_count"), TotalNodes);
    OutJson->SetArrayField(TEXT("entry_events"), SetToArray(EntryEvents));
    OutJson->SetArrayField(TEXT("function_calls"), SetToArray(FunctionCalls));
    OutJson->SetArrayField(TEXT("variable_reads"), SetToArray(VariableReads));
    OutJson->SetArrayField(TEXT("variable_writes"), SetToArray(VariableWrites));

    if (Options.bIncludeGraphPins)
    {
        OutJson->SetArrayField(TEXT("graphs"), GraphArray);
        OutJson->SetBoolField(TEXT("truncated"), bTruncated);
        if (bTruncated)
        {
            OutResult.AddWarning(TEXT("PropertyExportTruncated: graph pins/edges exceeded max_nodes/max_edges"));
        }
    }
    return true;
}
