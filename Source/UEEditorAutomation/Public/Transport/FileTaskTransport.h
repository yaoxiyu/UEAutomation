#pragma once

#include "CoreMinimal.h"
#include "Protocol/AutomationProtocolTypes.h"

struct FDiscoveredAutomationTask
{
    FString OriginalPath;
    FString WorkingPath;
    FString JsonText;
};

class FFileTaskSource
{
public:
    void EnsureDirectories() const;
    bool TryAcquireNextTask(FDiscoveredAutomationTask& OutTask) const;
    bool MoveToDone(const FDiscoveredAutomationTask& Task) const;
    bool MoveToFailed(const FDiscoveredAutomationTask& Task) const;

private:
    static bool MoveFileReplacing(const FString& From, const FString& To);
};

class FFileTaskResultSink
{
public:
    void EnsureDirectories() const;
    bool WriteResult(FAutomationTaskResult& Result) const;
};
