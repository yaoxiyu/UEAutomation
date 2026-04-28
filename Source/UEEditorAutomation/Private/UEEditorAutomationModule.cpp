#include "Application/BlueprintTaskExecutors.h"
#include "Application/EditorAutomationApplicationService.h"
#include "Adapter/UEBlueprintEditorAdapter.h"
#include "Core/AutomationLog.h"
#include "Modules/ModuleManager.h"

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
        TSharedRef<FUEBlueprintEditorAdapter> BlueprintAdapter = MakeShared<FUEBlueprintEditorAdapter>();
        TSharedRef<FBlueprintAutomationService> BlueprintService = MakeShared<FBlueprintAutomationService>(BlueprintAdapter);

        ApplicationService = MakeShared<FEditorAutomationApplicationService>();
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FCreateBlueprintTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FModifyBlueprintComponentsTaskExecutor>(BlueprintService));
        ApplicationService->GetExecutorRegistry().RegisterExecutor(MakeShared<FModifyBlueprintDefaultsTaskExecutor>(BlueprintService));
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
    bool HandleTicker(float DeltaTime)
    {
        if (ApplicationService.IsValid())
        {
            ApplicationService->Tick(DeltaTime);
        }
        return true;
    }

    TSharedPtr<FEditorAutomationApplicationService> ApplicationService;
    FDelegateHandle TickHandle;
};

IMPLEMENT_MODULE(FUEEditorAutomationModule, UEEditorAutomation)
