#include "Domain/AssetAutomationService.h"

#include "AssetRegistryModule.h"
#include "AssetToolsModule.h"
#include "AssetImportTask.h"
#include "Core/AutomationWhitelist.h"
#include "Core/EditorAutomationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/Blueprint.h"
#include "Engine/DataAsset.h"
#include "Factories/DataAssetFactory.h"
#include "Factories/Factory.h"
#include "HAL/FileManager.h"
#include "Materials/MaterialInterface.h"
#include "Materials/MaterialInstanceConstant.h"
#include "Factories/MaterialInstanceConstantFactoryNew.h"
#include "Engine/Texture.h"
#include "Engine/SkeletalMesh.h"
#include "PhysicsEngine/PhysicsAsset.h"
#include "PhysicsAssetUtils.h"
#include "UObject/Class.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"
#include "Misc/FileHelper.h"
#include "Misc/PackageName.h"
#include "Misc/Paths.h"
#include "Modules/ModuleInterface.h"
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

bool FAssetAutomationService::CreateMaterialInstance(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
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
            AddAssetOutput(Request.Asset, TEXT("material_instance"), OutResult);
            return true;
        }

        OutResult.AddError(TEXT("AssetAlreadyExists"), FString::Printf(TEXT("Asset '%s/%s' already exists."), *Request.Asset.PackagePath, *Request.Asset.AssetName), TEXT("payload.asset"));
        return false;
    }

    UMaterialInterface* ParentMaterial = LoadObject<UMaterialInterface>(nullptr, *Request.Asset.ParentClass);
    if (!ParentMaterial)
    {
        OutResult.AddError(TEXT("InvalidParentAsset"), FString::Printf(TEXT("Parent material '%s' could not be loaded."), *Request.Asset.ParentClass), TEXT("payload.asset.parent_class"));
        return false;
    }

    UMaterialInstanceConstantFactoryNew* Factory = NewObject<UMaterialInstanceConstantFactoryNew>();
    Factory->InitialParent = ParentMaterial;

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    UObject* Asset = AssetToolsModule.Get().CreateAsset(Request.Asset.AssetName, Request.Asset.PackagePath, UMaterialInstanceConstant::StaticClass(), Factory);
    UMaterialInstanceConstant* MaterialInstance = Cast<UMaterialInstanceConstant>(Asset);
    if (!MaterialInstance)
    {
        OutResult.AddError(TEXT("AssetCreateFailed"), TEXT("MaterialInstanceConstant could not be created."), TEXT("payload.asset"));
        return false;
    }

    if (!ApplyMaterialInstanceParameters(MaterialInstance, Request.Parameters, OutResult, TEXT("payload.parameters")))
    {
        return false;
    }

    if (Request.Execution.bSaveAfterSuccess)
    {
        FString Error;
        if (!EditorAdapter->SaveAsset(MaterialInstance, Error))
        {
            OutResult.AddError(TEXT("AssetSaveFailed"), Error);
            return false;
        }
    }

    AddAssetOutput(Request.Asset, TEXT("material_instance"), OutResult);
    return true;
}

bool FAssetAutomationService::ModifyMaterialInstance(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult)
{
    UMaterialInstanceConstant* MaterialInstance = Cast<UMaterialInstanceConstant>(LoadAsset(Request.TargetAsset.AssetPath, OutResult, TEXT("payload.target_asset.asset_path")));
    if (!MaterialInstance)
    {
        if (OutResult.Errors.Num() == 0)
        {
            OutResult.AddError(TEXT("InvalidAssetType"), TEXT("Target asset is not a MaterialInstanceConstant."), TEXT("payload.target_asset.asset_path"));
        }
        return false;
    }

    if (!ApplyMaterialInstanceParameters(MaterialInstance, Request.Parameters, OutResult, TEXT("payload.parameters")))
    {
        return false;
    }

    if (Request.Execution.bSaveAfterSuccess || Request.PostActions.Contains(TEXT("save_asset")))
    {
        FString Error;
        if (!EditorAdapter->SaveAsset(MaterialInstance, Error))
        {
            OutResult.AddError(TEXT("AssetSaveFailed"), Error);
            return false;
        }
    }

    FAutomationAssetSpec AssetSpec;
    AssetSpec.AssetPath = Request.TargetAsset.AssetPath;
    AddAssetOutput(AssetSpec, TEXT("material_instance"), OutResult);
    return true;
}

bool FAssetAutomationService::CreateTypedAsset(const FAutomationTaskRequest& Request, const FString& TaskType, FAutomationTaskResult& OutResult)
{
    if (TaskType == TEXT("create_physics_asset"))
    {
        return CreatePhysicsAsset(Request, OutResult);
    }

    FFactoryAssetSpec Spec;
    if (!BuildFactoryAssetSpec(Request, TaskType, Spec, OutResult))
    {
        return false;
    }

    if (Spec.FactoryClassPath.IsEmpty())
    {
        return CreatePlainObjectAsset(Request, Spec.AssetClassPath, Spec.AssetType, OutResult);
    }

    return CreateAssetWithFactory(Request, Spec, OutResult);
}

bool FAssetAutomationService::ImportAsset(const FAutomationTaskRequest& Request, const FString& TaskType, FAutomationTaskResult& OutResult)
{
    if (Request.SourcePath.IsEmpty() || Request.Asset.PackagePath.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("source_path and asset.package_path are required."), TEXT("payload"));
        return false;
    }

    const FString FullSourcePath = FPaths::ConvertRelativePathToFull(Request.SourcePath);
    if (!FPaths::FileExists(FullSourcePath))
    {
        OutResult.AddError(TEXT("SourceFileNotFound"), FString::Printf(TEXT("Source file '%s' could not be found."), *FullSourcePath), TEXT("payload.source_path"));
        return false;
    }

    UAssetImportTask* ImportTask = NewObject<UAssetImportTask>();
    ImportTask->Filename = FullSourcePath;
    ImportTask->DestinationPath = Request.Asset.PackagePath;
    ImportTask->DestinationName = Request.Asset.AssetName;
    ImportTask->bAutomated = true;
    ImportTask->bSave = Request.Execution.bSaveAfterSuccess;
    ImportTask->bReplaceExisting = Request.Execution.bOverwriteIfExists;
    ImportTask->bReplaceExistingSettings = Request.Execution.bOverwriteIfExists;

    if (TaskType == TEXT("import_sound_wave"))
    {
        LoadModuleForClassPath(TEXT("/Script/AudioEditor.SoundFactory"));
    }
    else
    {
        const FString FactoryClassPath = TEXT("/Script/UnrealEd.TextureFactory");
        LoadModuleForClassPath(FactoryClassPath);
        UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, *FactoryClassPath);
        if (!FactoryClass)
        {
            OutResult.AddError(TEXT("FactoryClassNotFound"), FString::Printf(TEXT("Factory class '%s' could not be loaded."), *FactoryClassPath), TEXT("task_type"));
            return false;
        }
        ImportTask->Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    TArray<UAssetImportTask*> ImportTasks;
    ImportTasks.Add(ImportTask);
    AssetToolsModule.Get().ImportAssetTasks(ImportTasks);

    UObject* ImportedAsset = nullptr;
    if (ImportTask->Result.Num() > 0)
    {
        ImportedAsset = ImportTask->Result[0];
    }
    if (!ImportedAsset && ImportTask->ImportedObjectPaths.Num() > 0)
    {
        ImportedAsset = LoadObject<UObject>(nullptr, *ImportTask->ImportedObjectPaths[0]);
    }

    if (!ImportedAsset)
    {
        OutResult.AddError(TEXT("AssetImportFailed"), FString::Printf(TEXT("Import failed for '%s'."), *FullSourcePath), TEXT("payload.source_path"));
        return false;
    }

    FAutomationAssetSpec AssetSpec;
    AssetSpec.AssetPath = ImportedAsset->GetPathName();
    AddAssetOutput(AssetSpec, TaskType == TEXT("import_sound_wave") ? TEXT("sound_wave") : TEXT("texture"), OutResult);
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

bool FAssetAutomationService::BuildFactoryAssetSpec(const FAutomationTaskRequest& Request, const FString& TaskType, FFactoryAssetSpec& OutSpec, FAutomationTaskResult& OutResult) const
{
    if (TaskType == TEXT("create_blueprint_class"))
    {
        OutSpec.AssetType = TEXT("blueprint");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.Blueprint");
        OutSpec.FactoryClassPath = TEXT("/Script/UnrealEd.BlueprintFactory");
        OutSpec.FactoryClassProperties.Add(TEXT("ParentClass"), Request.Asset.ParentClass);
    }
    else if (TaskType == TEXT("create_widget_blueprint"))
    {
        OutSpec.AssetType = TEXT("widget_blueprint");
        OutSpec.AssetClassPath = TEXT("/Script/UMGEditor.WidgetBlueprint");
        OutSpec.FactoryClassPath = TEXT("/Script/UMGEditor.WidgetBlueprintFactory");
        OutSpec.FactoryClassProperties.Add(TEXT("ParentClass"), Request.Asset.ParentClass.IsEmpty() ? TEXT("/Script/UMG.UserWidget") : Request.Asset.ParentClass);
    }
    else if (TaskType == TEXT("create_data_table"))
    {
        const FString RowStruct = !Request.Asset.ParentClass.IsEmpty() ? Request.Asset.ParentClass : GetParameterString(Request, TEXT("row_struct"));
        if (RowStruct.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("row_struct or asset.parent_class is required for create_data_table."), TEXT("payload.asset.parent_class"));
            return false;
        }

        OutSpec.AssetType = TEXT("data_table");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.DataTable");
        OutSpec.FactoryClassPath = TEXT("/Script/UnrealEd.DataTableFactory");
        OutSpec.FactoryObjectProperties.Add(TEXT("Struct"), RowStruct);
    }
    else if (TaskType == TEXT("create_curve_float"))
    {
        OutSpec.AssetType = TEXT("curve_float");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.CurveFloat");
        OutSpec.FactoryClassPath = TEXT("/Script/UnrealEd.CurveFloatFactory");
    }
    else if (TaskType == TEXT("create_curve_vector"))
    {
        OutSpec.AssetType = TEXT("curve_vector");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.CurveVector");
        OutSpec.FactoryClassPath = TEXT("/Script/UnrealEd.CurveVectorFactory");
    }
    else if (TaskType == TEXT("create_animation_blueprint"))
    {
        const FString TargetSkeleton = GetParameterString(Request, TEXT("target_skeleton"));
        if (TargetSkeleton.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_skeleton is required for create_animation_blueprint."), TEXT("payload.parameters.target_skeleton"));
            return false;
        }

        OutSpec.AssetType = TEXT("animation_blueprint");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.AnimBlueprint");
        OutSpec.FactoryClassPath = TEXT("/Script/UnrealEd.AnimBlueprintFactory");
        OutSpec.FactoryClassProperties.Add(TEXT("ParentClass"), Request.Asset.ParentClass.IsEmpty() ? TEXT("/Script/Engine.AnimInstance") : Request.Asset.ParentClass);
        OutSpec.FactoryObjectProperties.Add(TEXT("TargetSkeleton"), TargetSkeleton);
        OutSpec.FactoryObjectProperties.Add(TEXT("PreviewSkeletalMesh"), GetParameterString(Request, TEXT("preview_skeletal_mesh")));
    }
    else if (TaskType == TEXT("create_blend_space"))
    {
        const FString TargetSkeleton = GetParameterString(Request, TEXT("target_skeleton"));
        if (TargetSkeleton.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("target_skeleton is required for create_blend_space."), TEXT("payload.parameters.target_skeleton"));
            return false;
        }

        OutSpec.AssetType = TEXT("blend_space");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.BlendSpace");
        OutSpec.FactoryClassPath = TEXT("/Script/UnrealEd.BlendSpaceFactoryNew");
        OutSpec.FactoryObjectProperties.Add(TEXT("TargetSkeleton"), TargetSkeleton);
        OutSpec.FactoryObjectProperties.Add(TEXT("PreviewSkeletalMesh"), GetParameterString(Request, TEXT("preview_skeletal_mesh")));
    }
    else if (TaskType == TEXT("create_level_sequence"))
    {
        OutSpec.AssetType = TEXT("level_sequence");
        OutSpec.AssetClassPath = TEXT("/Script/LevelSequence.LevelSequence");
        OutSpec.FactoryClassPath = TEXT("/Script/LevelSequenceEditor.LevelSequenceFactoryNew");
    }
    else if (TaskType == TEXT("create_physics_asset"))
    {
        OutSpec.AssetType = TEXT("physics_asset");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.PhysicsAsset");
    }
    else if (TaskType == TEXT("create_material_function"))
    {
        OutSpec.AssetType = TEXT("material_function");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.MaterialFunction");
        OutSpec.FactoryClassPath = TEXT("/Script/UnrealEd.MaterialFunctionFactoryNew");
    }
    else if (TaskType == TEXT("create_gameplay_ability"))
    {
        OutSpec.AssetType = TEXT("gameplay_ability");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.Blueprint");
        OutSpec.FactoryClassPath = TEXT("/Script/UnrealEd.BlueprintFactory");
        OutSpec.FactoryClassProperties.Add(TEXT("ParentClass"), Request.Asset.ParentClass.IsEmpty() ? TEXT("/Script/GameplayAbilities.GameplayAbility") : Request.Asset.ParentClass);
    }
    else if (TaskType == TEXT("create_gameplay_effect"))
    {
        OutSpec.AssetType = TEXT("gameplay_effect");
        OutSpec.AssetClassPath = TEXT("/Script/Engine.Blueprint");
        OutSpec.FactoryClassPath = TEXT("/Script/UnrealEd.BlueprintFactory");
        OutSpec.FactoryClassProperties.Add(TEXT("ParentClass"), Request.Asset.ParentClass.IsEmpty() ? TEXT("/Script/GameplayAbilities.GameplayEffect") : Request.Asset.ParentClass);
    }
    else
    {
        OutResult.AddError(TEXT("InvalidTaskType"), FString::Printf(TEXT("Typed asset task '%s' is not supported."), *TaskType), TEXT("task_type"));
        return false;
    }

    if (OutSpec.AssetClassPath.IsEmpty())
    {
        OutResult.AddError(TEXT("InvalidAssetType"), FString::Printf(TEXT("No asset class is configured for '%s'."), *TaskType), TEXT("task_type"));
        return false;
    }
    return true;
}

bool FAssetAutomationService::CreatePhysicsAsset(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const
{
    const FString TargetSkeletalMesh = GetParameterString(Request, TEXT("target_skeletal_mesh"));
    if (Request.Asset.AssetName.IsEmpty() || Request.Asset.PackagePath.IsEmpty() || TargetSkeletalMesh.IsEmpty())
    {
        OutResult.AddError(TEXT("MissingRequiredField"), TEXT("asset_name, package_path and target_skeletal_mesh are required."), TEXT("payload"));
        return false;
    }

    if (EditorAdapter->DoesAssetExist(Request.Asset.PackagePath, Request.Asset.AssetName))
    {
        if (Request.Execution.bSkipIfExists)
        {
            OutResult.AddWarning(FString::Printf(TEXT("Asset '%s/%s' already exists; skipped."), *Request.Asset.PackagePath, *Request.Asset.AssetName));
            AddAssetOutput(Request.Asset, TEXT("physics_asset"), OutResult);
            return true;
        }

        OutResult.AddError(TEXT("AssetAlreadyExists"), FString::Printf(TEXT("Asset '%s/%s' already exists."), *Request.Asset.PackagePath, *Request.Asset.AssetName), TEXT("payload.asset"));
        return false;
    }

    USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *TargetSkeletalMesh);
    if (!SkeletalMesh)
    {
        OutResult.AddError(TEXT("FactoryPropertyValueNotFound"), FString::Printf(TEXT("SkeletalMesh '%s' could not be loaded."), *TargetSkeletalMesh), TEXT("payload.parameters.target_skeletal_mesh"));
        return false;
    }

    const FString PackageName = Request.Asset.PackagePath / Request.Asset.AssetName;
    UPackage* Package = CreatePackage(nullptr, *PackageName);
    if (!Package)
    {
        OutResult.AddError(TEXT("AssetCreateFailed"), FString::Printf(TEXT("Package '%s' could not be created."), *PackageName), TEXT("payload.asset"));
        return false;
    }

    UPhysicsAsset* PhysicsAsset = NewObject<UPhysicsAsset>(Package, *Request.Asset.AssetName, RF_Public | RF_Standalone | RF_Transactional);
    if (!PhysicsAsset)
    {
        OutResult.AddError(TEXT("AssetCreateFailed"), TEXT("PhysicsAsset object could not be created."), TEXT("payload.asset"));
        return false;
    }

    FText ErrorMessage;
    FPhysAssetCreateParams CreateParams;
    if (!FPhysicsAssetUtils::CreateFromSkeletalMesh(PhysicsAsset, SkeletalMesh, CreateParams, ErrorMessage, false))
    {
        PhysicsAsset->ClearFlags(RF_Public | RF_Standalone);
        OutResult.AddError(TEXT("AssetCreateFailed"), ErrorMessage.ToString(), TEXT("payload.asset"));
        return false;
    }

    FAssetRegistryModule::AssetCreated(PhysicsAsset);
    Package->MarkPackageDirty();

    if (Request.Execution.bSaveAfterSuccess)
    {
        FString Error;
        if (!EditorAdapter->SaveAsset(PhysicsAsset, Error))
        {
            OutResult.AddError(TEXT("AssetSaveFailed"), Error);
            return false;
        }
    }

    AddAssetOutput(Request.Asset, TEXT("physics_asset"), OutResult);
    return true;
}

bool FAssetAutomationService::CreateAssetWithFactory(const FAutomationTaskRequest& Request, const FFactoryAssetSpec& Spec, FAutomationTaskResult& OutResult) const
{
    if (EditorAdapter->DoesAssetExist(Request.Asset.PackagePath, Request.Asset.AssetName))
    {
        if (Request.Execution.bSkipIfExists)
        {
            OutResult.AddWarning(FString::Printf(TEXT("Asset '%s/%s' already exists; skipped."), *Request.Asset.PackagePath, *Request.Asset.AssetName));
            AddAssetOutput(Request.Asset, Spec.AssetType, OutResult);
            return true;
        }

        OutResult.AddError(TEXT("AssetAlreadyExists"), FString::Printf(TEXT("Asset '%s/%s' already exists."), *Request.Asset.PackagePath, *Request.Asset.AssetName), TEXT("payload.asset"));
        return false;
    }

    LoadModuleForClassPath(Spec.FactoryClassPath);
    LoadModuleForClassPath(Spec.AssetClassPath);

    UClass* FactoryClass = StaticLoadClass(UFactory::StaticClass(), nullptr, *Spec.FactoryClassPath);
    if (!FactoryClass)
    {
        OutResult.AddError(TEXT("FactoryClassNotFound"), FString::Printf(TEXT("Factory class '%s' could not be loaded."), *Spec.FactoryClassPath), TEXT("task_type"));
        return false;
    }

    UClass* AssetClass = StaticLoadClass(UObject::StaticClass(), nullptr, *Spec.AssetClassPath);
    if (!AssetClass)
    {
        OutResult.AddError(TEXT("AssetClassNotFound"), FString::Printf(TEXT("Asset class '%s' could not be loaded."), *Spec.AssetClassPath), TEXT("task_type"));
        return false;
    }

    UFactory* Factory = NewObject<UFactory>(GetTransientPackage(), FactoryClass);
    if (!ConfigureFactory(Factory, Spec, OutResult))
    {
        return false;
    }

    FAssetToolsModule& AssetToolsModule = FModuleManager::LoadModuleChecked<FAssetToolsModule>(TEXT("AssetTools"));
    UObject* Asset = AssetToolsModule.Get().CreateAsset(Request.Asset.AssetName, Request.Asset.PackagePath, AssetClass, Factory);
    if (!Asset)
    {
        OutResult.AddError(TEXT("AssetCreateFailed"), FString::Printf(TEXT("Asset '%s/%s' could not be created."), *Request.Asset.PackagePath, *Request.Asset.AssetName), TEXT("payload.asset"));
        return false;
    }

    if (Request.ClassDefaults.Num() > 0)
    {
        UObject* AssignTarget = Asset;
        if (UBlueprint* Blueprint = Cast<UBlueprint>(Asset))
        {
            AssignTarget = Blueprint->GeneratedClass ? Blueprint->GeneratedClass->GetDefaultObject() : Asset;
        }

        if (!PropertyAssignmentService.AssignProperties(AssignTarget, Request.ClassDefaults, OutResult, TEXT("payload.properties")))
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

    AddAssetOutput(Request.Asset, Spec.AssetType, OutResult);
    return true;
}

bool FAssetAutomationService::CreatePlainObjectAsset(const FAutomationTaskRequest& Request, const FString& AssetClassPath, const FString& AssetType, FAutomationTaskResult& OutResult) const
{
    if (EditorAdapter->DoesAssetExist(Request.Asset.PackagePath, Request.Asset.AssetName))
    {
        if (Request.Execution.bSkipIfExists)
        {
            OutResult.AddWarning(FString::Printf(TEXT("Asset '%s/%s' already exists; skipped."), *Request.Asset.PackagePath, *Request.Asset.AssetName));
            AddAssetOutput(Request.Asset, AssetType, OutResult);
            return true;
        }

        OutResult.AddError(TEXT("AssetAlreadyExists"), FString::Printf(TEXT("Asset '%s/%s' already exists."), *Request.Asset.PackagePath, *Request.Asset.AssetName), TEXT("payload.asset"));
        return false;
    }

    LoadModuleForClassPath(AssetClassPath);
    UClass* AssetClass = StaticLoadClass(UObject::StaticClass(), nullptr, *AssetClassPath);
    if (!AssetClass)
    {
        OutResult.AddError(TEXT("AssetClassNotFound"), FString::Printf(TEXT("Asset class '%s' could not be loaded."), *AssetClassPath), TEXT("task_type"));
        return false;
    }

    const FString PackageName = Request.Asset.PackagePath / Request.Asset.AssetName;
    UPackage* Package = CreatePackage(nullptr, *PackageName);
    UObject* Asset = NewObject<UObject>(Package, AssetClass, FName(*Request.Asset.AssetName), RF_Public | RF_Standalone);
    if (!Asset)
    {
        OutResult.AddError(TEXT("AssetCreateFailed"), FString::Printf(TEXT("Asset '%s' could not be created."), *PackageName), TEXT("payload.asset"));
        return false;
    }

    if (Request.ClassDefaults.Num() > 0 && !PropertyAssignmentService.AssignProperties(Asset, Request.ClassDefaults, OutResult, TEXT("payload.properties")))
    {
        return false;
    }

    FAssetRegistryModule::AssetCreated(Asset);
    Package->MarkPackageDirty();

    if (Request.Execution.bSaveAfterSuccess)
    {
        FString Error;
        if (!EditorAdapter->SaveAsset(Asset, Error))
        {
            OutResult.AddError(TEXT("AssetSaveFailed"), Error);
            return false;
        }
    }

    AddAssetOutput(Request.Asset, AssetType, OutResult);
    return true;
}

bool FAssetAutomationService::ConfigureFactory(UObject* Factory, const FFactoryAssetSpec& Spec, FAutomationTaskResult& OutResult) const
{
    if (!Factory)
    {
        OutResult.AddError(TEXT("FactoryCreateFailed"), TEXT("Factory object is invalid."), TEXT("task_type"));
        return false;
    }

    for (const TPair<FString, FString>& Pair : Spec.FactoryObjectProperties)
    {
        if (Pair.Value.IsEmpty())
        {
            continue;
        }
        if (!SetObjectOrClassProperty(Factory, Pair.Key, Pair.Value, false, OutResult, FString::Printf(TEXT("payload.parameters.%s"), *Pair.Key)))
        {
            return false;
        }
    }

    for (const TPair<FString, FString>& Pair : Spec.FactoryClassProperties)
    {
        if (Pair.Value.IsEmpty())
        {
            continue;
        }

        const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();
        if (!Whitelist.bLoaded)
        {
            OutResult.AddError(TEXT("WhitelistLoadFailed"), Whitelist.LoadError, TEXT("security.whitelist"));
            return false;
        }
        if (Pair.Key == TEXT("ParentClass") && Whitelist.AllowedParentClasses.Num() > 0 && !Whitelist.AllowedParentClasses.Contains(Pair.Value))
        {
            OutResult.AddError(TEXT("InvalidParentClass"), FString::Printf(TEXT("Parent class '%s' is not allowed."), *Pair.Value), TEXT("payload.asset.parent_class"));
            return false;
        }

        if (!SetObjectOrClassProperty(Factory, Pair.Key, Pair.Value, true, OutResult, FString::Printf(TEXT("payload.parameters.%s"), *Pair.Key)))
        {
            return false;
        }
    }

    return true;
}

bool FAssetAutomationService::SetObjectOrClassProperty(UObject* Target, const FString& PropertyName, const FString& ObjectPath, bool bRequireClass, FAutomationTaskResult& OutResult, const FString& Field) const
{
    FObjectPropertyBase* ObjectProperty = FindFProperty<FObjectPropertyBase>(Target->GetClass(), FName(*PropertyName));
    if (!ObjectProperty)
    {
        OutResult.AddError(TEXT("FactoryPropertyNotFound"), FString::Printf(TEXT("Factory property '%s' was not found on '%s'."), *PropertyName, *Target->GetClass()->GetPathName()), Field);
        return false;
    }

    LoadModuleForClassPath(ObjectPath);

    UObject* LoadedObject = bRequireClass
        ? StaticLoadClass(UObject::StaticClass(), nullptr, *ObjectPath)
        : StaticLoadObject(UObject::StaticClass(), nullptr, *ObjectPath);
    if (!LoadedObject)
    {
        OutResult.AddError(TEXT("FactoryPropertyValueNotFound"), FString::Printf(TEXT("Object '%s' could not be loaded for factory property '%s'."), *ObjectPath, *PropertyName), Field);
        return false;
    }

    ObjectProperty->SetObjectPropertyValue_InContainer(Target, LoadedObject);
    return true;
}

bool FAssetAutomationService::LoadModuleForClassPath(const FString& ClassPath) const
{
    FString Prefix;
    FString Remainder;
    if (!ClassPath.Split(TEXT("/Script/"), &Prefix, &Remainder))
    {
        return false;
    }

    FString ModuleName;
    FString ClassName;
    if (!Remainder.Split(TEXT("."), &ModuleName, &ClassName) || ModuleName.IsEmpty())
    {
        return false;
    }

    if (FModuleManager::Get().IsModuleLoaded(*ModuleName))
    {
        return true;
    }

    return FModuleManager::Get().LoadModulePtr<IModuleInterface>(*ModuleName) != nullptr;
}

FString FAssetAutomationService::GetParameterString(const FAutomationTaskRequest& Request, const FString& Name) const
{
    for (const FAutomationPropertyValue& Parameter : Request.Parameters)
    {
        if (Parameter.Name.Equals(Name, ESearchCase::IgnoreCase) && Parameter.Value.IsValid())
        {
            FString StringValue;
            if (Parameter.Value->TryGetString(StringValue))
            {
                return StringValue;
            }
        }
    }
    return FString();
}

bool FAssetAutomationService::ApplyMaterialInstanceParameters(UMaterialInstanceConstant* MaterialInstance, const TArray<FAutomationPropertyValue>& Parameters, FAutomationTaskResult& OutResult, const FString& FieldPrefix) const
{
    if (!MaterialInstance)
    {
        OutResult.AddError(TEXT("InvalidAssetType"), TEXT("Material instance is invalid."), FieldPrefix);
        return false;
    }

    for (int32 Index = 0; Index < Parameters.Num(); ++Index)
    {
        const FAutomationPropertyValue& Parameter = Parameters[Index];
        const FString Field = FString::Printf(TEXT("%s[%d]"), *FieldPrefix, Index);
        const FString Type = Parameter.Type.ToLower();

        if (Parameter.Name.IsEmpty())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("parameter name is required."), Field + TEXT(".name"));
            return false;
        }
        if (!Parameter.Value.IsValid())
        {
            OutResult.AddError(TEXT("MissingRequiredField"), TEXT("parameter value is required."), Field + TEXT(".value"));
            return false;
        }

        if (Type == TEXT("float") || Type == TEXT("scalar"))
        {
            MaterialInstance->SetScalarParameterValueEditorOnly(FName(*Parameter.Name), Parameter.Value->AsNumber());
        }
        else if (Type == TEXT("linear_color") || Type == TEXT("color") || Type == TEXT("vector"))
        {
            FLinearColor Color;
            if (!TryReadLinearColor(Parameter, Color))
            {
                OutResult.AddError(TEXT("InvalidPropertyValue"), FString::Printf(TEXT("Parameter '%s' requires color array [R,G,B,A]."), *Parameter.Name), Field + TEXT(".value"));
                return false;
            }
            MaterialInstance->SetVectorParameterValueEditorOnly(FName(*Parameter.Name), Color);
        }
        else if (Type == TEXT("texture") || Type == TEXT("object_path") || Type == TEXT("soft_object_path"))
        {
            UTexture* Texture = LoadObject<UTexture>(nullptr, *Parameter.Value->AsString());
            if (!Texture)
            {
                OutResult.AddError(TEXT("InvalidPropertyValue"), FString::Printf(TEXT("Texture '%s' could not be loaded."), *Parameter.Value->AsString()), Field + TEXT(".value"));
                return false;
            }
            MaterialInstance->SetTextureParameterValueEditorOnly(FName(*Parameter.Name), Texture);
        }
        else
        {
            OutResult.AddError(TEXT("PropertyTypeMismatch"), FString::Printf(TEXT("Unsupported material parameter type '%s'."), *Parameter.Type), Field + TEXT(".type"));
            return false;
        }

        OutResult.Metrics.PropertyAssignCount++;
    }

    MaterialInstance->PostEditChange();
    MaterialInstance->MarkPackageDirty();
    return true;
}

bool FAssetAutomationService::TryReadLinearColor(const FAutomationPropertyValue& Parameter, FLinearColor& OutColor) const
{
    const TArray<TSharedPtr<FJsonValue>>& Array = Parameter.Value->AsArray();
    if (Array.Num() != 4)
    {
        return false;
    }

    OutColor = FLinearColor(
        Array[0]->AsNumber(),
        Array[1]->AsNumber(),
        Array[2]->AsNumber(),
        Array[3]->AsNumber());
    return true;
}

bool FAssetAutomationService::IsAllowedPackagePath(const FString& PackagePath) const
{
    const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();
    if (!Whitelist.bLoaded)
    {
        return false;
    }

    if (Whitelist.AllowedAssetRoots.Num() == 0)
    {
        return true;
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
