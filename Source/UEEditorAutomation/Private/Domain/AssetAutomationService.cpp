#include "Domain/AssetAutomationService.h"

#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "Core/AutomationWhitelist.h"
#include "Core/EditorAutomationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/DataAsset.h"
#include "Factories/DataAssetFactory.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleManager.h"
#include "Serialization/JsonWriter.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FAssetAutomationService::FAssetAutomationService(const TSharedRef<IBlueprintEditorAdapter>& InEditorAdapter)
    : EditorAdapter(InEditorAdapter)
{
}

bool FAssetAutomationService::CreateDataAsset(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    if (Request.Asset.AssetName.IsEmpty() || Request.Asset.PackagePath.IsEmpty() || Request.Asset.ParentClass.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("asset_name, package_path and parent_class are required."), TEXT("payload.asset"));
        return false;
    }

    if (EditorAdapter->DoesAssetExist(Request.Asset.PackagePath, Request.Asset.AssetName))
    {
        if (Request.Execution.bSkipIfExists)
        {
            OutResult.AddWarning(FString::Printf(TEXT("Asset '%s/%s' already exists; skipped."), *Request.Asset.PackagePath, *Request.Asset.AssetName));
            AddAssetOutput(Request.Asset, TEXT("data_asset"), OutResult);
            return true;
        }

        OutResult.AddError(TEXT("AssetAlreadyExists"), FString::Printf(TEXT("Asset '%s/%s' already exists."), *Request.Asset.PackagePath, *Request.Asset.AssetName), TEXT("payload.asset"));
        return false;
    }

    UClass* AssetClass = LoadDataAssetClass(Request.Asset.ParentClass, OutResult, TEXT("payload.asset.parent_class"));
    if (!AssetClass)
    {
        return false;
    }

    UDataAssetFactory* Factory = NewObject<UDataAssetFactory>();
    Factory->DataAssetClass = AssetClass;

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    UObject* Asset = AssetToolsModule.Get().CreateAsset(Request.Asset.AssetName, Request.Asset.PackagePath, AssetClass, Factory);
    if (!Asset)
    {
        OutResult.AddError(TEXT("AssetCreateFailed"), TEXT("DataAsset could not be created."), TEXT("payload.asset"));
        return false;
    }

    if (Request.ClassDefaults.Num() > 0)
    {
        if (!PropertyAssignmentService.AssignProperties(Asset, Request.ClassDefaults, OutResult, TEXT("payload.properties")))
        {
            return false;
        }
    }

    if (Request.Execution.bSaveAfterSuccess)
    {
        FString Error;
        if (!EditorAdapter->SaveAsset(Asset, Error))
        {
            OutResult.AddError(TEXT("AssetSaveFailed"), Error);
            return false;
        }
    }

    AddAssetOutput(Request.Asset, TEXT("data_asset"), OutResult);
    return true;
}

bool FAssetAutomationService::ModifyAssetProperties(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    UObject* Asset = LoadAsset(Request.TargetAsset.AssetPath, OutResult, TEXT("payload.target_asset.asset_path"));
    if (!Asset)
    {
        return false;
    }

    if (Request.ClassDefaults.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("properties must contain at least one property."), TEXT("payload.properties"));
        return false;
    }

    if (!PropertyAssignmentService.AssignProperties(Asset, Request.ClassDefaults, OutResult, TEXT("payload.properties")))
    {
        return false;
    }

    if (Request.Execution.bSaveAfterSuccess || Request.PostActions.Contains(TEXT("save_asset")))
    {
        FString Error;
        if (!EditorAdapter->SaveAsset(Asset, Error))
        {
            OutResult.AddError(TEXT("AssetSaveFailed"), Error);
            return false;
        }
    }

    FAutomationAssetSpec AssetSpec;
    AssetSpec.AssetPath = Request.TargetAsset.AssetPath;
    AddAssetOutput(AssetSpec, TEXT("asset"), OutResult);
    return true;
}

bool FAssetAutomationService::CheckAssetRules(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    TArray<FAutomationAssetSpec> Assets = Request.TargetAssets;
    if (!Request.TargetAsset.AssetPath.IsEmpty())
    {
        Assets.Insert(Request.TargetAsset, 0);
    }

    if (Assets.Num() == 0)
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_asset or target_assets is required."), TEXT("payload.target_assets"));
        return false;
    }

    const bool bCheckExists = Request.Rules.Num() == 0 || Request.Rules.Contains(TEXT("asset_exists"));
    const bool bCheckRoot = Request.Rules.Num() == 0 || Request.Rules.Contains(TEXT("asset_root_allowed"));

    for (int32 Index = 0; Index < Assets.Num(); ++Index)
    {
        const FAutomationAssetSpec& Asset = Assets[Index];
        const FString Field = Assets.Num() == 1 ? TEXT("payload.target_asset.asset_path") : FString::Printf(TEXT("payload.target_assets[%d].asset_path"), Index);

        if (Asset.AssetPath.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("asset_path is required."), Field);
            return false;
        }

        if (bCheckExists && !LoadObject<UObject>(nullptr, *Asset.AssetPath))
        {
            OutResult.AddError(TEXT("AssetRuleViolation"), FString::Printf(TEXT("Asset '%s' does not exist."), *Asset.AssetPath), Field);
            return false;
        }

        if (bCheckRoot)
        {
            FString PackageName;
            FString AssetName;
            Asset.AssetPath.Split(TEXT("."), &PackageName, &AssetName);
            if (!IsAllowedPackagePath(PackageName))
            {
                OutResult.AddError(TEXT("AssetRuleViolation"), FString::Printf(TEXT("Asset '%s' is outside allowed roots."), *Asset.AssetPath), Field);
                return false;
            }
        }

        AddAssetOutput(Asset, TEXT("asset"), OutResult);
    }

    return true;
}

bool FAssetAutomationService::GenerateAuditReport(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    const FString OutputPath = ResolveReportPath(Request);
    if (!WriteAuditReport(OutputPath, OutResult))
    {
        return false;
    }

    FAutomationAssetOutput Output;
    Output.AssetPath = OutputPath;
    Output.AssetName = FPaths::GetCleanFilename(OutputPath);
    Output.AssetType = TEXT("audit_report");
    OutResult.AssetOutputs.Add(Output);
    return true;
}

UObject* FAssetAutomationService::LoadAsset(const FString& AssetPath, FAutomationTaskResult& OutResult, const FString& Field) const
{
    if (AssetPath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("asset_path is required."), Field);
        return nullptr;
    }

    UObject* Asset = LoadObject<UObject>(nullptr, *AssetPath);
    if (!Asset)
    {
        OutResult.AddError(TEXT("AssetNotFound"), FString::Printf(TEXT("Asset '%s' could not be loaded."), *AssetPath), Field);
    }
    return Asset;
}

UClass* FAssetAutomationService::LoadDataAssetClass(const FString& ClassPath, FAutomationTaskResult& OutResult, const FString& Field) const
{
    const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();
    if (!Whitelist.bLoaded)
    {
        OutResult.AddError(TEXT("WhitelistLoadFailed"), Whitelist.LoadError, TEXT("security.whitelist"));
        return nullptr;
    }

    if (Whitelist.AllowedParentClasses.Num() > 0 && !Whitelist.AllowedParentClasses.Contains(ClassPath))
    {
        OutResult.AddError(TEXT("InvalidParentClass"), FString::Printf(TEXT("Asset class '%s' is not allowed."), *ClassPath), Field);
        return nullptr;
    }

    UClass* AssetClass = StaticLoadClass(UDataAsset::StaticClass(), nullptr, *ClassPath);
    if (!AssetClass)
    {
        OutResult.AddError(TEXT("InvalidParentClass"), FString::Printf(TEXT("DataAsset class '%s' not found."), *ClassPath), Field);
    }
    return AssetClass;
}

bool FAssetAutomationService::IsAllowedPackagePath(const FString& PackagePath) const
{
    const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();
    if (!Whitelist.bLoaded)
    {
        return false;
    }

    for (const FString& Root : Whitelist.AllowedAssetRoots)
    {
        if (PackagePath.StartsWith(Root))
        {
            return true;
        }
    }
    return false;
}

bool FAssetAutomationService::WriteAuditReport(const FString& OutputPath, FAutomationTaskResult& OutResult) const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();

    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Settings->ResultDir.Path / TEXT("*.result.json")), true, false);

    int32 SuccessCount = 0;
    int32 FailureCount = 0;
    TMap<FString, int32> ErrorCounts;
    TMap<FString, int32> TaskTypeCounts;

    for (const FString& File : Files)
    {
        FString JsonText;
        if (!FFileHelper::LoadFileToString(JsonText, *(Settings->ResultDir.Path / File)))
        {
            continue;
        }

        TSharedPtr<FJsonObject> Root;
        const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
        if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
        {
            continue;
        }

        bool bSuccess = false;
        Root->TryGetBoolField(TEXT("success"), bSuccess);
        bSuccess ? ++SuccessCount : ++FailureCount;

        FString TaskType;
        if (Root->TryGetStringField(TEXT("task_type"), TaskType))
        {
            TaskTypeCounts.FindOrAdd(TaskType)++;
        }

        const TArray<TSharedPtr<FJsonValue>>* Errors = nullptr;
        if (Root->TryGetArrayField(TEXT("errors"), Errors))
        {
            for (const TSharedPtr<FJsonValue>& ErrorValue : *Errors)
            {
                const TSharedPtr<FJsonObject> ErrorObject = ErrorValue->AsObject();
                FString Code;
                if (ErrorObject.IsValid() && ErrorObject->TryGetStringField(TEXT("code"), Code))
                {
                    ErrorCounts.FindOrAdd(Code)++;
                }
            }
        }
    }

    const TSharedRef<FJsonObject> Report = MakeShared<FJsonObject>();
    Report->SetNumberField(TEXT("result_count"), Files.Num());
    Report->SetNumberField(TEXT("success_count"), SuccessCount);
    Report->SetNumberField(TEXT("failure_count"), FailureCount);

    const TSharedRef<FJsonObject> TaskTypes = MakeShared<FJsonObject>();
    for (const TPair<FString, int32>& Pair : TaskTypeCounts)
    {
        TaskTypes->SetNumberField(Pair.Key, Pair.Value);
    }
    Report->SetObjectField(TEXT("task_type_counts"), TaskTypes);

    const TSharedRef<FJsonObject> Errors = MakeShared<FJsonObject>();
    for (const TPair<FString, int32>& Pair : ErrorCounts)
    {
        Errors->SetNumberField(Pair.Key, Pair.Value);
    }
    Report->SetObjectField(TEXT("error_counts"), Errors);

    FString JsonText;
    const TSharedRef<TJsonWriter<>> Writer = TJsonWriterFactory<>::Create(&JsonText);
    if (!FJsonSerializer::Serialize(Report, Writer))
    {
        OutResult.AddError(TEXT("AuditReportFailed"), TEXT("Audit report JSON serialization failed."), TEXT("payload.report"));
        return false;
    }

    IFileManager::Get().MakeDirectory(*FPaths::GetPath(OutputPath), true);
    if (!FFileHelper::SaveStringToFile(JsonText, *OutputPath))
    {
        OutResult.AddError(TEXT("AuditReportFailed"), FString::Printf(TEXT("Audit report '%s' could not be written."), *OutputPath), TEXT("payload.report.path"));
        return false;
    }

    return true;
}

FString FAssetAutomationService::ResolveReportPath(const FAutomationTaskRequest& Request) const
{
    if (!Request.ReportPath.IsEmpty())
    {
        return FPaths::IsRelative(Request.ReportPath)
            ? FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Request.ReportPath)
            : Request.ReportPath;
    }

    return FString::Printf(TEXT("C:/UEAutomation/reports/%s.audit.json"), *Request.TaskId);
}

void FAssetAutomationService::AddAssetOutput(const FAutomationAssetSpec& Asset, const FString& AssetType, FAutomationTaskResult& OutResult) const
{
    FAutomationAssetOutput Output;
    Output.AssetType = AssetType;
    Output.AssetPath = !Asset.AssetPath.IsEmpty()
        ? Asset.AssetPath
        : FString::Printf(TEXT("%s/%s.%s"), *Asset.PackagePath, *Asset.AssetName, *Asset.AssetName);

    int32 DotIndex = INDEX_NONE;
    if (Output.AssetPath.FindLastChar(TEXT('.'), DotIndex))
    {
        Output.AssetName = Output.AssetPath.Mid(DotIndex + 1);
    }
    else
    {
        Output.AssetName = Asset.AssetName;
    }

    OutResult.AssetOutputs.Add(Output);
}
