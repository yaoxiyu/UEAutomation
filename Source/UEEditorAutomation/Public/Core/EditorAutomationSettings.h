#pragma once

#include "CoreMinimal.h"
#include "Engine/DeveloperSettings.h"
#include "EditorAutomationSettings.generated.h"

UCLASS(Config=Editor, DefaultConfig, meta=(DisplayName="UE Editor Automation"))
class UEEDITORAUTOMATION_API UEditorAutomationSettings : public UDeveloperSettings
{
    GENERATED_BODY()

public:
    UEditorAutomationSettings();

    UPROPERTY(Config, EditAnywhere, Category="Daemon")
    bool bEnableDaemon;

    UPROPERTY(Config, EditAnywhere, Category="Daemon", meta=(ClampMin="0.1"))
    float PollIntervalSeconds;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation Socket")
    bool bEnableUEAutomationSocketServer;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation Socket", meta=(ClampMin="1024", ClampMax="65535"))
    int32 UEAutomationSocketPort;

    UPROPERTY(Config, EditAnywhere, Category="Execution", meta=(ClampMin="1", ClampMax="1"))
    int32 MaxConcurrentTasks;

    UPROPERTY(Config, EditAnywhere, Category="Execution", meta=(ClampMin="0", ClampMax="10"))
    int32 MaxStartupStaleWorkingTaskRetries;

    UPROPERTY(Config, EditAnywhere, Category="Paths")
    FDirectoryPath TaskInboxDir;

    UPROPERTY(Config, EditAnywhere, Category="Paths")
    FDirectoryPath TaskWorkingDir;

    UPROPERTY(Config, EditAnywhere, Category="Paths")
    FDirectoryPath TaskDoneDir;

    UPROPERTY(Config, EditAnywhere, Category="Paths")
    FDirectoryPath TaskFailedDir;

    UPROPERTY(Config, EditAnywhere, Category="Paths")
    FDirectoryPath ResultDir;

    UPROPERTY(Config, EditAnywhere, Category="Paths")
    FDirectoryPath LogDir;

    UPROPERTY(Config, EditAnywhere, Category="Protocol")
    int32 SupportedProtocolVersion;

    UPROPERTY(Config, EditAnywhere, Category="Security")
    FFilePath WhitelistFilePath;

    UPROPERTY(Config, EditAnywhere, Category="Templates")
    FFilePath TemplateRegistryFilePath;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis")
    bool bEnableBlueprintMetaCache;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis")
    FDirectoryPath BlueprintMetaCacheDir;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis", meta=(ClampMin="0", ClampMax="8"))
    int32 MaxReferenceAnalysisDepth;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis", meta=(ClampMin="1", ClampMax="32"))
    int32 MaxPropertyExportDepth;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis", meta=(ClampMin="1", ClampMax="65536"))
    int32 MaxArrayExportElements;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis", meta=(ClampMin="1", ClampMax="65536"))
    int32 MaxReferenceGraphNodes;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis", meta=(ClampMin="1", ClampMax="262144"))
    int32 MaxReferenceGraphEdges;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis")
    bool bExportBlueprintGraphReadOnly;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis")
    bool bExportReferencers;

    UPROPERTY(Config, EditAnywhere, Category="UE Automation|Analysis")
    bool bAllowSourceUnresolvedCache;
};
