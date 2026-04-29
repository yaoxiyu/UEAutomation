#include "Protocol/AutomationProtocolTypes.h"

#include "Dom/JsonObject.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

void FAutomationTaskResult::AddError(const FString& Code, const FString& Message, const FString& Field)
{
    FAutomationError Error;
    Error.Code = Code;
    Error.Message = Message;
    Error.Field = Field;
    Errors.Add(Error);
    Metrics.ErrorCount = Errors.Num();
    bSuccess = false;
    Status = TEXT("failed");
}

void FAutomationTaskResult::AddWarning(const FString& Message)
{
    Warnings.Add(Message);
    Metrics.WarningCount = Warnings.Num();
}

void FAutomationTaskResult::AddLog(const FString& Message)
{
    LogLines.Add(Message);
}

bool FAutomationProtocolJson::ParseRequest(const FString& JsonText, FAutomationTaskRequest& OutRequest, FAutomationTaskResult& OutResult)
{
    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutResult.AddError(TEXT("MalformedJson"), TEXT("Task file is not valid JSON."));
        return false;
    }

    double ProtocolVersion = 0.0;
    Root->TryGetNumberField(TEXT("protocol_version"), ProtocolVersion);
    OutRequest.ProtocolVersion = static_cast<int32>(ProtocolVersion);
    Root->TryGetStringField(TEXT("task_id"), OutRequest.TaskId);
    Root->TryGetStringField(TEXT("task_type"), OutRequest.TaskType);
    Root->TryGetStringField(TEXT("timestamp_utc"), OutRequest.TimestampUtc);

    OutResult.ProtocolVersion = OutRequest.ProtocolVersion;
    OutResult.TaskId = OutRequest.TaskId;
    OutResult.TaskType = OutRequest.TaskType;

    const TSharedPtr<FJsonObject>* ExecutionObject = nullptr;
    if (Root->TryGetObjectField(TEXT("execution"), ExecutionObject) && ExecutionObject && ExecutionObject->IsValid())
    {
        (*ExecutionObject)->TryGetStringField(TEXT("priority"), OutRequest.Execution.Priority);
        (*ExecutionObject)->TryGetStringField(TEXT("idempotency_key"), OutRequest.Execution.IdempotencyKey);
        (*ExecutionObject)->TryGetBoolField(TEXT("skip_if_exists"), OutRequest.Execution.bSkipIfExists);
        (*ExecutionObject)->TryGetBoolField(TEXT("overwrite_if_exists"), OutRequest.Execution.bOverwriteIfExists);
        (*ExecutionObject)->TryGetBoolField(TEXT("open_after_success"), OutRequest.Execution.bOpenAfterSuccess);
        (*ExecutionObject)->TryGetBoolField(TEXT("save_after_success"), OutRequest.Execution.bSaveAfterSuccess);
        (*ExecutionObject)->TryGetBoolField(TEXT("compile_after_create"), OutRequest.Execution.bCompileAfterCreate);
    }

    const TSharedPtr<FJsonObject>* PayloadObject = nullptr;
    if (!Root->TryGetObjectField(TEXT("payload"), PayloadObject) || !PayloadObject || !PayloadObject->IsValid())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("Missing payload object."), TEXT("payload"));
        return false;
    }

    const TSharedPtr<FJsonObject>* AssetObject = nullptr;
    if ((*PayloadObject)->TryGetObjectField(TEXT("asset"), AssetObject) && AssetObject && AssetObject->IsValid())
    {
        ParseAssetSpec(*AssetObject, OutRequest.Asset);
    }

    (*PayloadObject)->TryGetStringField(TEXT("source_path"), OutRequest.SourcePath);

    const TSharedPtr<FJsonObject>* TemplateObject = nullptr;
    if ((*PayloadObject)->TryGetObjectField(TEXT("template"), TemplateObject) && TemplateObject && TemplateObject->IsValid())
    {
        (*TemplateObject)->TryGetStringField(TEXT("template_id"), OutRequest.Template.TemplateId);
    }

    const TSharedPtr<FJsonObject>* SharedTemplateObject = nullptr;
    if ((*PayloadObject)->TryGetObjectField(TEXT("shared_template"), SharedTemplateObject) && SharedTemplateObject && SharedTemplateObject->IsValid())
    {
        (*SharedTemplateObject)->TryGetStringField(TEXT("template_id"), OutRequest.SharedTemplate.TemplateId);
    }

    const TSharedPtr<FJsonObject>* TargetAssetObject = nullptr;
    if ((*PayloadObject)->TryGetObjectField(TEXT("target_asset"), TargetAssetObject) && TargetAssetObject && TargetAssetObject->IsValid())
    {
        ParseAssetSpec(*TargetAssetObject, OutRequest.TargetAsset);
    }

    const TArray<TSharedPtr<FJsonValue>>* TargetAssetsArray = nullptr;
    if ((*PayloadObject)->TryGetArrayField(TEXT("target_assets"), TargetAssetsArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *TargetAssetsArray)
        {
            FAutomationAssetSpec Asset;
            if (ParseAssetSpec(Value->AsObject(), Asset))
            {
                OutRequest.TargetAssets.Add(Asset);
            }
        }
    }

    const TSharedPtr<FJsonObject>* AssemblyObject = nullptr;
    if ((*PayloadObject)->TryGetObjectField(TEXT("assembly"), AssemblyObject) && AssemblyObject && AssemblyObject->IsValid())
    {
        const TSharedPtr<FJsonObject>* RootComponentObject = nullptr;
        if ((*AssemblyObject)->TryGetObjectField(TEXT("root_component"), RootComponentObject) && RootComponentObject && RootComponentObject->IsValid())
        {
            ParseComponentSpec(*RootComponentObject, OutRequest.RootComponent);
        }

        const TArray<TSharedPtr<FJsonValue>>* ComponentsArray = nullptr;
        if ((*AssemblyObject)->TryGetArrayField(TEXT("components"), ComponentsArray))
        {
            for (const TSharedPtr<FJsonValue>& Value : *ComponentsArray)
            {
                FAutomationComponentSpec Spec;
                if (ParseComponentSpec(Value->AsObject(), Spec))
                {
                    OutRequest.Components.Add(Spec);
                }
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* ClassDefaultsArray = nullptr;
    if ((*PayloadObject)->TryGetArrayField(TEXT("class_default_overrides"), ClassDefaultsArray)
        || (*PayloadObject)->TryGetArrayField(TEXT("class_defaults"), ClassDefaultsArray)
        || (*PayloadObject)->TryGetArrayField(TEXT("properties"), ClassDefaultsArray))
    {
        ParsePropertyArray(ClassDefaultsArray, OutRequest.ClassDefaults);
    }

    const TArray<TSharedPtr<FJsonValue>>* ParametersArray = nullptr;
    if ((*PayloadObject)->TryGetArrayField(TEXT("parameters"), ParametersArray))
    {
        ParsePropertyArray(ParametersArray, OutRequest.Parameters);
    }

    const TSharedPtr<FJsonObject>* OverridesObject = nullptr;
    if ((*PayloadObject)->TryGetObjectField(TEXT("overrides"), OverridesObject) && OverridesObject && OverridesObject->IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* ComponentOverridesArray = nullptr;
        if ((*OverridesObject)->TryGetArrayField(TEXT("component_overrides"), ComponentOverridesArray))
        {
            for (const TSharedPtr<FJsonValue>& Value : *ComponentOverridesArray)
            {
                FAutomationComponentOverride Override;
                if (ParseComponentOverride(Value->AsObject(), Override))
                {
                    OutRequest.ComponentOverrides.Add(Override);
                }
            }
        }

        const TArray<TSharedPtr<FJsonValue>>* ClassDefaultOverridesArray = nullptr;
        if ((*OverridesObject)->TryGetArrayField(TEXT("class_default_overrides"), ClassDefaultOverridesArray)
            || (*OverridesObject)->TryGetArrayField(TEXT("class_defaults"), ClassDefaultOverridesArray))
        {
            ParsePropertyArray(ClassDefaultOverridesArray, OutRequest.ClassDefaults);
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* OperationsArray = nullptr;
    if ((*PayloadObject)->TryGetArrayField(TEXT("operations"), OperationsArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *OperationsArray)
        {
            const TSharedPtr<FJsonObject> OperationObject = Value->AsObject();
            if (!OperationObject.IsValid())
            {
                continue;
            }

            FAutomationOperation Operation;
            OperationObject->TryGetStringField(TEXT("op"), Operation.Op);
            ParseComponentSpec(OperationObject, Operation.Component);
            const TArray<TSharedPtr<FJsonValue>>* PropertiesArray = nullptr;
            if (OperationObject->TryGetArrayField(TEXT("properties"), PropertiesArray))
            {
                ParsePropertyArray(PropertiesArray, Operation.Properties);
            }
            OutRequest.Operations.Add(Operation);
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* ItemsArray = nullptr;
    if ((*PayloadObject)->TryGetArrayField(TEXT("items"), ItemsArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *ItemsArray)
        {
            const TSharedPtr<FJsonObject> ItemObject = Value->AsObject();
            if (!ItemObject.IsValid())
            {
                continue;
            }

            FAutomationBatchBlueprintItem Item;
            const TSharedPtr<FJsonObject>* ItemAssetObject = nullptr;
            if (ItemObject->TryGetObjectField(TEXT("asset"), ItemAssetObject) && ItemAssetObject && ItemAssetObject->IsValid())
            {
                ParseAssetSpec(*ItemAssetObject, Item.Asset);
            }

            const TSharedPtr<FJsonObject>* ItemTemplateObject = nullptr;
            if (ItemObject->TryGetObjectField(TEXT("template"), ItemTemplateObject) && ItemTemplateObject && ItemTemplateObject->IsValid())
            {
                (*ItemTemplateObject)->TryGetStringField(TEXT("template_id"), Item.Template.TemplateId);
            }

            const TSharedPtr<FJsonObject>* ItemOverridesObject = nullptr;
            if (ItemObject->TryGetObjectField(TEXT("overrides"), ItemOverridesObject) && ItemOverridesObject && ItemOverridesObject->IsValid())
            {
                const TArray<TSharedPtr<FJsonValue>>* ComponentOverridesArray = nullptr;
                if ((*ItemOverridesObject)->TryGetArrayField(TEXT("component_overrides"), ComponentOverridesArray))
                {
                    for (const TSharedPtr<FJsonValue>& OverrideValue : *ComponentOverridesArray)
                    {
                        FAutomationComponentOverride Override;
                        if (ParseComponentOverride(OverrideValue->AsObject(), Override))
                        {
                            Item.ComponentOverrides.Add(Override);
                        }
                    }
                }

                const TArray<TSharedPtr<FJsonValue>>* ClassDefaultOverridesArray = nullptr;
                if ((*ItemOverridesObject)->TryGetArrayField(TEXT("class_default_overrides"), ClassDefaultOverridesArray)
                    || (*ItemOverridesObject)->TryGetArrayField(TEXT("class_defaults"), ClassDefaultOverridesArray))
                {
                    ParsePropertyArray(ClassDefaultOverridesArray, Item.ClassDefaults);
                }
            }

            OutRequest.BatchItems.Add(Item);
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* PostActionsArray = nullptr;
    if ((*PayloadObject)->TryGetArrayField(TEXT("post_actions"), PostActionsArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *PostActionsArray)
        {
            const TSharedPtr<FJsonObject> ActionObject = Value->AsObject();
            FString Action;
            if (ActionObject.IsValid() && ActionObject->TryGetStringField(TEXT("action"), Action))
            {
                OutRequest.PostActions.Add(Action);
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* RulesArray = nullptr;
    if ((*PayloadObject)->TryGetArrayField(TEXT("rules"), RulesArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *RulesArray)
        {
            FString Rule;
            if (Value.IsValid() && Value->TryGetString(Rule) && !Rule.IsEmpty())
            {
                OutRequest.Rules.Add(Rule);
            }
        }
    }

    const TSharedPtr<FJsonObject>* ReportObject = nullptr;
    if ((*PayloadObject)->TryGetObjectField(TEXT("report"), ReportObject) && ReportObject && ReportObject->IsValid())
    {
        (*ReportObject)->TryGetStringField(TEXT("path"), OutRequest.ReportPath);
        (*ReportObject)->TryGetStringField(TEXT("format"), OutRequest.ReportFormat);
    }

    const TSharedPtr<FJsonObject>* AnalysisObject = nullptr;
    if ((*PayloadObject)->TryGetObjectField(TEXT("analysis"), AnalysisObject) && AnalysisObject && AnalysisObject->IsValid())
    {
        FAutomationAnalysisOptions& Analysis = OutRequest.Analysis;
        Analysis.bHasAnalysisBlock = true;
        (*AnalysisObject)->TryGetBoolField(TEXT("force_refresh"), Analysis.bForceRefresh);
        (*AnalysisObject)->TryGetBoolField(TEXT("use_cache"), Analysis.bUseCache);
        (*AnalysisObject)->TryGetBoolField(TEXT("include_native_cxx"), Analysis.bIncludeNativeCxx);
        (*AnalysisObject)->TryGetBoolField(TEXT("include_blueprint_snapshot"), Analysis.bIncludeBlueprintSnapshot);
        (*AnalysisObject)->TryGetBoolField(TEXT("include_class_defaults"), Analysis.bIncludeClassDefaults);
        (*AnalysisObject)->TryGetBoolField(TEXT("include_components"), Analysis.bIncludeComponents);
        (*AnalysisObject)->TryGetBoolField(TEXT("include_references"), Analysis.bIncludeReferences);
        (*AnalysisObject)->TryGetBoolField(TEXT("include_referencers"), Analysis.bIncludeReferencers);
        (*AnalysisObject)->TryGetBoolField(TEXT("include_graph_summary"), Analysis.bIncludeGraphSummary);
        (*AnalysisObject)->TryGetBoolField(TEXT("include_graph_pins"), Analysis.bIncludeGraphPins);
        (*AnalysisObject)->TryGetBoolField(TEXT("export_only_editable_properties"), Analysis.bExportOnlyEditableProperties);

        double NumberValue = 0.0;
        if ((*AnalysisObject)->TryGetNumberField(TEXT("reference_depth"), NumberValue))
        {
            Analysis.ReferenceDepth = static_cast<int32>(NumberValue);
        }
        if ((*AnalysisObject)->TryGetNumberField(TEXT("max_nodes"), NumberValue))
        {
            Analysis.MaxNodes = static_cast<int32>(NumberValue);
        }
        if ((*AnalysisObject)->TryGetNumberField(TEXT("max_edges"), NumberValue))
        {
            Analysis.MaxEdges = static_cast<int32>(NumberValue);
        }
        if ((*AnalysisObject)->TryGetNumberField(TEXT("max_property_depth"), NumberValue))
        {
            Analysis.MaxPropertyDepth = static_cast<int32>(NumberValue);
        }
        if ((*AnalysisObject)->TryGetNumberField(TEXT("max_array_elements"), NumberValue))
        {
            Analysis.MaxArrayElements = static_cast<int32>(NumberValue);
        }
    }

    // duplicate_asset / redirect_asset_references payload
    (*PayloadObject)->TryGetStringField(TEXT("source_asset_path"), OutRequest.SourceAssetPath);
    (*PayloadObject)->TryGetStringField(TEXT("destination_package_path"), OutRequest.DestinationPackagePath);
    (*PayloadObject)->TryGetStringField(TEXT("destination_asset_name"), OutRequest.DestinationAssetName);
    (*PayloadObject)->TryGetBoolField(TEXT("overwrite_destination"), OutRequest.bOverwriteDestination);

    // list_directory_assets payload
    (*PayloadObject)->TryGetStringField(TEXT("directory_path"), OutRequest.DirectoryPath);
    if (!(*PayloadObject)->TryGetBoolField(TEXT("recursive"), OutRequest.bRecursive))
    {
        OutRequest.bRecursive = true;
    }

    const TArray<TSharedPtr<FJsonValue>>* RedirectsArray = nullptr;
    if ((*PayloadObject)->TryGetArrayField(TEXT("redirects"), RedirectsArray))
    {
        for (const TSharedPtr<FJsonValue>& Value : *RedirectsArray)
        {
            const TSharedPtr<FJsonObject> Object = Value->AsObject();
            if (!Object.IsValid())
            {
                continue;
            }
            FAutomationAssetRedirect Redirect;
            Object->TryGetStringField(TEXT("from"), Redirect.From);
            Object->TryGetStringField(TEXT("to"), Redirect.To);
            if (!Redirect.From.IsEmpty() && !Redirect.To.IsEmpty())
            {
                OutRequest.AssetRedirects.Add(Redirect);
            }
        }
    }

    return true;
}

bool FAutomationProtocolJson::SerializeResult(const FAutomationTaskResult& Result, FString& OutJsonText)
{
    const TSharedRef<FJsonObject> Root = MakeShared<FJsonObject>();
    Root->SetNumberField(TEXT("protocol_version"), Result.ProtocolVersion);
    Root->SetStringField(TEXT("task_id"), Result.TaskId);
    Root->SetStringField(TEXT("task_type"), Result.TaskType);
    Root->SetBoolField(TEXT("success"), Result.bSuccess);
    Root->SetStringField(TEXT("status"), Result.Status);

    TArray<TSharedPtr<FJsonValue>> AssetOutputs;
    for (const FAutomationAssetOutput& Output : Result.AssetOutputs)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("asset_path"), Output.AssetPath);
        Object->SetStringField(TEXT("asset_name"), Output.AssetName);
        Object->SetStringField(TEXT("asset_type"), Output.AssetType);
        AssetOutputs.Add(MakeShared<FJsonValueObject>(Object));
    }
    Root->SetArrayField(TEXT("asset_outputs"), AssetOutputs);

    TArray<TSharedPtr<FJsonValue>> Artifacts;
    for (const FAutomationArtifactOutput& Artifact : Result.Artifacts)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("artifact_type"), Artifact.ArtifactType);
        Object->SetStringField(TEXT("path"), Artifact.Path);
        Object->SetStringField(TEXT("asset_path"), Artifact.AssetPath);
        Object->SetStringField(TEXT("cache_status"), Artifact.CacheStatus);
        Object->SetStringField(TEXT("parent_cpp_md5"), Artifact.ParentCppMd5);
        Artifacts.Add(MakeShared<FJsonValueObject>(Object));
    }
    Root->SetArrayField(TEXT("artifacts"), Artifacts);

    TArray<TSharedPtr<FJsonValue>> Warnings;
    for (const FString& Warning : Result.Warnings)
    {
        Warnings.Add(MakeShared<FJsonValueString>(Warning));
    }
    Root->SetArrayField(TEXT("warnings"), Warnings);

    TArray<TSharedPtr<FJsonValue>> Errors;
    for (const FAutomationError& Error : Result.Errors)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("code"), Error.Code);
        Object->SetStringField(TEXT("message"), Error.Message);
        Object->SetStringField(TEXT("field"), Error.Field);
        Errors.Add(MakeShared<FJsonValueObject>(Object));
    }
    Root->SetArrayField(TEXT("errors"), Errors);

    const TSharedRef<FJsonObject> Metrics = MakeShared<FJsonObject>();
    Metrics->SetNumberField(TEXT("duration_ms"), Result.Metrics.DurationMs);
    Metrics->SetNumberField(TEXT("compile_duration_ms"), Result.Metrics.CompileDurationMs);
    Metrics->SetNumberField(TEXT("save_duration_ms"), Result.Metrics.SaveDurationMs);
    Metrics->SetNumberField(TEXT("component_create_count"), Result.Metrics.ComponentCreateCount);
    Metrics->SetNumberField(TEXT("property_assign_count"), Result.Metrics.PropertyAssignCount);
    Metrics->SetNumberField(TEXT("warning_count"), Result.Metrics.WarningCount);
    Metrics->SetNumberField(TEXT("error_count"), Result.Metrics.ErrorCount);
    Metrics->SetNumberField(TEXT("analysis_duration_ms"), Result.Metrics.AnalysisDurationMs);
    Metrics->SetNumberField(TEXT("source_resolve_duration_ms"), Result.Metrics.SourceResolveDurationMs);
    Metrics->SetNumberField(TEXT("reference_scan_duration_ms"), Result.Metrics.ReferenceScanDurationMs);
    Metrics->SetNumberField(TEXT("cache_hit_count"), Result.Metrics.CacheHitCount);
    Metrics->SetNumberField(TEXT("cache_miss_count"), Result.Metrics.CacheMissCount);
    Metrics->SetNumberField(TEXT("analyzed_blueprint_count"), Result.Metrics.AnalyzedBlueprintCount);
    Metrics->SetNumberField(TEXT("exported_property_count"), Result.Metrics.ExportedPropertyCount);
    Metrics->SetNumberField(TEXT("reference_node_count"), Result.Metrics.ReferenceNodeCount);
    Metrics->SetNumberField(TEXT("reference_edge_count"), Result.Metrics.ReferenceEdgeCount);
    Metrics->SetBoolField(TEXT("reference_graph_truncated"), Result.Metrics.bReferenceGraphTruncated);
    Root->SetObjectField(TEXT("metrics"), Metrics);
    Root->SetStringField(TEXT("log_path"), Result.LogPath);

    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&OutJsonText);
    return FJsonSerializer::Serialize(Root, Writer);
}

bool FAutomationProtocolJson::ParseAssetSpec(const TSharedPtr<FJsonObject>& Object, FAutomationAssetSpec& OutSpec)
{
    if (!Object.IsValid())
    {
        return false;
    }
    Object->TryGetStringField(TEXT("asset_type"), OutSpec.AssetType);
    Object->TryGetStringField(TEXT("asset_name"), OutSpec.AssetName);
    Object->TryGetStringField(TEXT("package_path"), OutSpec.PackagePath);
    Object->TryGetStringField(TEXT("asset_path"), OutSpec.AssetPath);
    Object->TryGetStringField(TEXT("parent_class"), OutSpec.ParentClass);
    Object->TryGetStringField(TEXT("blueprint_type"), OutSpec.BlueprintType);
    return true;
}

bool FAutomationProtocolJson::ParseComponentSpec(const TSharedPtr<FJsonObject>& Object, FAutomationComponentSpec& OutSpec)
{
    if (!Object.IsValid())
    {
        return false;
    }

    Object->TryGetStringField(TEXT("component_name"), OutSpec.ComponentName);
    Object->TryGetStringField(TEXT("component_class"), OutSpec.ComponentClass);
    Object->TryGetStringField(TEXT("attach_parent"), OutSpec.AttachParent);

    const TSharedPtr<FJsonObject>* TransformObject = nullptr;
    if (Object->TryGetObjectField(TEXT("transform"), TransformObject) && TransformObject && TransformObject->IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* LocationArray = nullptr;
        if ((*TransformObject)->TryGetArrayField(TEXT("location"), LocationArray))
        {
            OutSpec.Transform.bHasLocation = ParseVector(*LocationArray, OutSpec.Transform.Location);
        }

        const TArray<TSharedPtr<FJsonValue>>* RotationArray = nullptr;
        if ((*TransformObject)->TryGetArrayField(TEXT("rotation"), RotationArray))
        {
            OutSpec.Transform.bHasRotation = ParseRotator(*RotationArray, OutSpec.Transform.Rotation);
        }

        const TArray<TSharedPtr<FJsonValue>>* ScaleArray = nullptr;
        if ((*TransformObject)->TryGetArrayField(TEXT("scale"), ScaleArray))
        {
            OutSpec.Transform.bHasScale = ParseVector(*ScaleArray, OutSpec.Transform.Scale);
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* PropertiesArray = nullptr;
    if (Object->TryGetArrayField(TEXT("properties"), PropertiesArray))
    {
        ParsePropertyArray(PropertiesArray, OutSpec.Properties);
    }

    return true;
}

bool FAutomationProtocolJson::ParseComponentOverride(const TSharedPtr<FJsonObject>& Object, FAutomationComponentOverride& OutSpec)
{
    if (!Object.IsValid())
    {
        return false;
    }

    Object->TryGetStringField(TEXT("component_name"), OutSpec.ComponentName);

    const TSharedPtr<FJsonObject>* TransformObject = nullptr;
    if (Object->TryGetObjectField(TEXT("transform"), TransformObject) && TransformObject && TransformObject->IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* LocationArray = nullptr;
        if ((*TransformObject)->TryGetArrayField(TEXT("location"), LocationArray))
        {
            OutSpec.Transform.bHasLocation = ParseVector(*LocationArray, OutSpec.Transform.Location);
        }

        const TArray<TSharedPtr<FJsonValue>>* RotationArray = nullptr;
        if ((*TransformObject)->TryGetArrayField(TEXT("rotation"), RotationArray))
        {
            OutSpec.Transform.bHasRotation = ParseRotator(*RotationArray, OutSpec.Transform.Rotation);
        }

        const TArray<TSharedPtr<FJsonValue>>* ScaleArray = nullptr;
        if ((*TransformObject)->TryGetArrayField(TEXT("scale"), ScaleArray))
        {
            OutSpec.Transform.bHasScale = ParseVector(*ScaleArray, OutSpec.Transform.Scale);
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* PropertiesArray = nullptr;
    if (Object->TryGetArrayField(TEXT("properties"), PropertiesArray))
    {
        ParsePropertyArray(PropertiesArray, OutSpec.Properties);
    }

    return true;
}

bool FAutomationProtocolJson::ParsePropertyArray(const TArray<TSharedPtr<FJsonValue>>* Array, TArray<FAutomationPropertyValue>& OutProperties)
{
    if (!Array)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Array)
    {
        const TSharedPtr<FJsonObject> Object = Value->AsObject();
        if (!Object.IsValid())
        {
            continue;
        }

        FAutomationPropertyValue Property;
        Object->TryGetStringField(TEXT("name"), Property.Name);
        Object->TryGetStringField(TEXT("type"), Property.Type);
        const TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(TEXT("value"));
        Property.Value = FieldValue ? *FieldValue : nullptr;
        OutProperties.Add(Property);
    }
    return true;
}

bool FAutomationProtocolJson::ParseVector(const TArray<TSharedPtr<FJsonValue>>& Array, FVector& OutVector)
{
    if (Array.Num() != 3)
    {
        return false;
    }
    OutVector = FVector(Array[0]->AsNumber(), Array[1]->AsNumber(), Array[2]->AsNumber());
    return true;
}

bool FAutomationProtocolJson::ParseRotator(const TArray<TSharedPtr<FJsonValue>>& Array, FRotator& OutRotator)
{
    if (Array.Num() != 3)
    {
        return false;
    }
    OutRotator = FRotator(Array[0]->AsNumber(), Array[1]->AsNumber(), Array[2]->AsNumber());
    return true;
}
