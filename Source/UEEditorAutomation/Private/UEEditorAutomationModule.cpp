#include "Application/BlueprintTaskExecutors.h"
#include "Application/AssetTaskExecutors.h"
#include "Application/EditorAutomationApplicationService.h"
#include "Adapter/UEBlueprintEditorAdapter.h"
#include "Core/AutomationLog.h"
#include "Framework/Docking/TabManager.h"
#include "Framework/MultiBox/MultiBoxBuilder.h"
#include "LevelEditor.h"
#include "Modules/ModuleManager.h"
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
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateDataAssetTaskExecutor>(AssetService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FModifyAssetPropertiesTaskExecutor>(AssetService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCheckAssetRulesTaskExecutor>(AssetService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FGenerateAuditReportTaskExecutor>(AssetService));
        ApplicationService->Initialize();

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
        return true;
    }

    TSharedPtr<FEditorAutomationApplicationService> ApplicationService;
    TSharedPtr<FExtender> MenuExtender;
    FDelegateHandle TickHandle;
    const FName DebugPanelTabName = TEXT("UEEditorAutomationDebugPanel");
};

IMPLEMENT_MODULE(FUEEditorAutomationModule, UEEditorAutomation)
