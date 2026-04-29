#include "Core/EditorAutomationSettings.h"

UEditorAutomationSettings::UEditorAutomationSettings()
{
    bEnableDaemon = true;
    bEnableUEAutomationSocketServer = false;
    PollIntervalSeconds = 1.0f;
    UEAutomationSocketPort = 18777;
    MaxConcurrentTasks = 1;
    SupportedProtocolVersion = 1;

    TaskInboxDir.Path = TEXT("C:/UEAutomation/tasks/inbox");
    TaskWorkingDir.Path = TEXT("C:/UEAutomation/tasks/working");
    TaskDoneDir.Path = TEXT("C:/UEAutomation/tasks/done");
    TaskFailedDir.Path = TEXT("C:/UEAutomation/tasks/failed");
    ResultDir.Path = TEXT("C:/UEAutomation/results");
    LogDir.Path = TEXT("C:/UEAutomation/logs");
    WhitelistFilePath.FilePath = TEXT("Plugins/UEEditorAutomation/Config/UEEditorAutomationWhitelist.json");
    TemplateRegistryFilePath.FilePath = TEXT("Plugins/UEEditorAutomation/Config/UEEditorAutomationTemplates.json");

    bEnableBlueprintMetaCache = true;
    BlueprintMetaCacheDir.Path = FString();
    MaxReferenceAnalysisDepth = 2;
    MaxPropertyExportDepth = 8;
    MaxArrayExportElements = 128;
    MaxReferenceGraphNodes = 128;
    MaxReferenceGraphEdges = 512;
    bExportBlueprintGraphReadOnly = true;
    bExportReferencers = true;
    bAllowSourceUnresolvedCache = false;

    // Policy and production templates are runtime data. Keep them in Config JSON
    // so projects can adjust automation scope without rebuilding this editor plugin.
}
