#include "Application/BlueprintTaskExecutors.h"
#include "Application/AssetTaskExecutors.h"
#include "Application/AssetDuplicationTaskExecutors.h"
#include "Application/BlueprintAnalysisTaskExecutors.h"
#include "Application/EditorAutomationApplicationService.h"
#include "Adapter/UEBlueprintEditorAdapter.h"
#include "Core/AutomationLog.h"
#include "Domain/AssetDuplicationService.h"
#include "Domain/BehaviorTreeAnalysisService.h"
#include "Domain/BlueprintAnalysisService.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
#include "Transport/SocketTaskServer.h"
#include "UI/AutomationDebugPanel.h"
#include "Widgets/Docking/SDockTab.h"

#if ENGINE_MAJOR_VERSION >= 5
#include "Containers/Map.h"
#include "Containers/Ticker.h"
#else
#include "Containers/Ticker.h"
#endif

class FUEEditorAutomationModule : public IModuleInterface
{
public:
    virtual void StartupModule() override
    {
        RegisterDebugPanel();

        TSharedRef<FUEBlueprintEditorAdapter> BlueprintAdapter = MakeShared<FUEBlueprintEditorAdapter>();
        TSharedRef<FBlueprintAutomationService> BlueprintService = MakeShared<FBlueprintAutomationService>(BlueprintAdapter);
        TSharedRef<FAssetAutomationService> AssetService = MakeShared<FAssetAutomationService>(BlueprintAdapter);

        ApplicationService = MakeShared<FEditorAutomationApplicationService>();
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateBlueprintTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateBlueprintFromTemplateTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FBatchCreateBlueprintsTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FModifyBlueprintComponentsTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FModifyBlueprintDefaultsTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCopyLiveBlueprintValuesTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCopyBlueprintLiveOverridesTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FDiagnoseBlueprintPropertyPersistenceTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateDataAssetTaskExecutor>(AssetService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FModifyAssetPropertiesTaskExecutor>(AssetService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateMaterialInstanceTaskExecutor>(AssetService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FModifyMaterialInstanceTaskExecutor>(AssetService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_blueprint_class")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_widget_blueprint")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_data_table")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_curve_float")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_curve_vector")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_animation_blueprint")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_blend_space")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_level_sequence")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_physics_asset")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_material_function")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_gameplay_ability")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateTypedAssetTaskExecutor>(AssetService, TEXT("create_gameplay_effect")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FImportAssetTaskExecutor>(AssetService, TEXT("import_texture")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FImportAssetTaskExecutor>(AssetService, TEXT("import_sound_wave")));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCheckAssetRulesTaskExecutor>(AssetService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FGenerateAuditReportTaskExecutor>(AssetService));

        TSharedRef<FBlueprintAnalysisService> AnalysisService = MakeShared<FBlueprintAnalysisService>();
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FAnalyzeBlueprintTaskExecutor>(AnalysisService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FAnalyzeBlueprintReferenceChainTaskExecutor>(AnalysisService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FAnalyzeAssetTaskExecutor>(AnalysisService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FRefreshBlueprintMetaCacheTaskExecutor>(AnalysisService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FExportBlueprintAIContextTaskExecutor>(AnalysisService));

        TSharedRef<FBehaviorTreeAnalysisService> BTAnalysisService = MakeShared<FBehaviorTreeAnalysisService>();
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FAnalyzeBehaviorTreeTaskExecutor>(BTAnalysisService));

        TSharedRef<FAssetDuplicationService> DuplicationService = MakeShared<FAssetDuplicationService>();
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FDuplicateAssetTaskExecutor>(DuplicationService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FRedirectAssetReferencesTaskExecutor>(DuplicationService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FListDirectoryAssetsTaskExecutor>(DuplicationService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FDeleteDirectoryAssetsTaskExecutor>(DuplicationService));

        ApplicationService->Initialize();

        SocketTaskServer = MakeShared<FSocketTaskServer>();
        SocketTaskServer->Initialize(ApplicationService.ToSharedRef());

#if ENGINE_MAJOR_VERSION >= 5
        TickHandle = FTSTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FUEEditorAutomationModule::HandleTicker));
#else
        TickHandle = FTicker::GetCoreTicker().AddTicker(FTickerDelegate::CreateRaw(this, &FUEEditorAutomationModule::HandleTicker));
#endif
        UE_LOG(LogUEEditorAutomation, Log, TEXT("UEEditorAutomation module started."));
    }

    virtual void ShutdownModule() override
    {
        UnregisterDebugPanel();

        if (TickHandle.IsValid())
        {
#if ENGINE_MAJOR_VERSION >= 5
            FTSTicker::GetCoreTicker().RemoveTicker(TickHandle);
#else
            FTicker::GetCoreTicker().RemoveTicker(TickHandle);
#endif
            TickHandle.Reset();
        }

        if (ApplicationService.IsValid())
        {
            ApplicationService->Shutdown();
            ApplicationService.Reset();
        }
        if (SocketTaskServer.IsValid())
        {
            SocketTaskServer->Shutdown();
            SocketTaskServer.Reset();
        }
        UE_LOG(LogUEEditorAutomation, Log, TEXT("UEEditorAutomation module stopped."));
    }

private:
    void RegisterDebugPanel()
    {
        FGlobalTabmanager::Get()->RegisterNomadTabSpawner(
            DebugPanelTabName,
            FOnSpawnTab::CreateRaw(this, &FUEEditorAutomationModule::SpawnDebugPanelTab))
            .SetDisplayName(FText::FromString(TEXT("UE Automation")));

        if (FModuleManager::Get().IsModuleLoaded(TEXT("LevelEditor")))
        {
            FLevelEditorModule& LevelEditorModule = FModuleManager::LoadModuleChecked<FLevelEditorModule>(TEXT("LevelEditor"));
            MenuExtender = MakeShared<FExtender>();
            MenuExtender->AddMenuExtension(
                TEXT("WindowLayout"),
                EExtensionHook::After,
                nullptr,
                FMenuExtensionDelegate::CreateRaw(this, &FUEEditorAutomationModule::AddDebugPanelMenuEntry));
            LevelEditorModule.GetMenuExtensibilityManager()->AddExtender(MenuExtender);
        }
    }

    void UnregisterDebugPanel()
    {
        FGlobalTabmanager::Get()->UnregisterNomadTabSpawner(DebugPanelTabName);
    }

    TSharedRef<SDockTab> SpawnDebugPanelTab(const FSpawnTabArgs& Args)
    {
        return SNew(SDockTab)
            .TabRole(ETabRole::NomadTab)
            [
                SNew(SAutomationDebugPanel)
            ];
    }

    void AddDebugPanelMenuEntry(FMenuBuilder& MenuBuilder)
    {
        MenuBuilder.AddMenuEntry(
            FText::FromString(TEXT("UE Automation")),
            FText::FromString(TEXT("Open UE Editor Automation debug panel.")),
            FSlateIcon(),
            FUIAction(FExecuteAction::CreateRaw(this, &FUEEditorAutomationModule::OpenDebugPanel)));
    }

    void OpenDebugPanel()
    {
        FGlobalTabmanager::Get()->InvokeTab(DebugPanelTabName);
    }

    bool HandleTicker(float DeltaTime)
    {
        if (ApplicationService.IsValid())
        {
            ApplicationService->Tick(DeltaTime);
        }
        if (SocketTaskServer.IsValid())
        {
            SocketTaskServer->Tick();
        }
        return true;
    }

    TSharedPtr<FEditorAutomationApplicationService> ApplicationService;
    TSharedPtr<FSocketTaskServer> SocketTaskServer;
    TSharedPtr<FExtender> MenuExtender;
    FDelegateHandle TickHandle;
    const FName DebugPanelTabName = TEXT("UEEditorAutomationDebugPanel");
};

IMPLEMENT_MODULE(FUEEditorAutomationModule, UEEditorAutomation)
