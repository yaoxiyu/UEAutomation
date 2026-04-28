#pragma once

#include "CoreMinimal.h"
#include "Application/TaskExecutor.h"
#include "Application/TaskValidator.h"
#include "Transport/FileTaskTransport.h"

class FEditorAutomationApplicationService
{
public:
    void Initialize();
    void Shutdown();
    void Tick(float DeltaTime);

    FAutomationTaskResult ExecuteTask(const FAutomationTaskRequest& Request);
    FTaskExecutorRegistry& GetExecutorRegistry() { return ExecutorRegistry; }

private:
    void RecoverStaleWorkingTasks();

    bool bInitialized = false;
    bool bExecuting = false;
    float TimeSinceLastPoll = 0.0f;
    FFileTaskSource TaskSource;
    FFileTaskResultSink ResultSink;
    FTaskValidator Validator;
    FTaskExecutorRegistry ExecutorRegistry;
};
