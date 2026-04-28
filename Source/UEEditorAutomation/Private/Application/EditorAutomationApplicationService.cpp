#include "Application/EditorAutomationApplicationService.h"

#include "Core/AutomationLog.h"
#include "Core/EditorAutomationSettings.h"
#include "Misc/DateTime.h"

void FEditorAutomationApplicationService::Initialize()
{
    TaskSource.EnsureDirectories();
    ResultSink.EnsureDirectories();
    bInitialized = true;
}

void FEditorAutomationApplicationService::Shutdown()
{
    bInitialized = false;
}

void FEditorAutomationApplicationService::Tick(float DeltaTime)
{
    if (!bInitialized || bExecuting)
    {
        return;
    }

    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    if (!Settings->bEnableDaemon)
    {
        return;
    }

    TimeSinceLastPoll += DeltaTime;
    if (TimeSinceLastPoll < Settings->PollIntervalSeconds)
    {
        return;
    }
    TimeSinceLastPoll = 0.0f;

    FDiscoveredAutomationTask DiscoveredTask;
    if (!TaskSource.TryAcquireNextTask(DiscoveredTask))
    {
        return;
    }

    bExecuting = true;
    FAutomationTaskRequest Request;
    FAutomationTaskResult Result;
    Request.SourcePath = DiscoveredTask.WorkingPath;

    if (FAutomationProtocolJson::ParseRequest(DiscoveredTask.JsonText, Request, Result))
    {
        Request.SourcePath = DiscoveredTask.WorkingPath;
        Result = ExecuteTask(Request);
    }

    ResultSink.WriteResult(Result);
    if (Result.bSuccess)
    {
        TaskSource.MoveToDone(DiscoveredTask);
    }
    else
    {
        TaskSource.MoveToFailed(DiscoveredTask);
    }
    bExecuting = false;
}

FAutomationTaskResult FEditorAutomationApplicationService::ExecuteTask(const FAutomationTaskRequest& Request)
{
    const double StartSeconds = FPlatformTime::Seconds();

    FAutomationTaskResult Result;
    Result.ProtocolVersion = Request.ProtocolVersion;
    Result.TaskId = Request.TaskId;
    Result.TaskType = Request.TaskType;
    Result.Status = TEXT("validating");

    if (!Validator.ValidateCommon(Request, Result))
    {
        Result.Metrics.DurationMs = static_cast<int64>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        return Result;
    }

    TSharedPtr<ITaskExecutor> Executor = ExecutorRegistry.FindExecutor(Request.TaskType);
    if (!Executor.IsValid())
    {
        Result.AddError(TEXT("InvalidTaskType"), FString::Printf(TEXT("No executor registered for '%s'."), *Request.TaskType), TEXT("task_type"));
        Result.Metrics.DurationMs = static_cast<int64>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        return Result;
    }

    if (!Executor->Validate(Request, Result))
    {
        Result.Metrics.DurationMs = static_cast<int64>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);
        return Result;
    }

    Result.Status = TEXT("executing");
    Executor->Execute(Request, Result);
    Result.Metrics.DurationMs = static_cast<int64>((FPlatformTime::Seconds() - StartSeconds) * 1000.0);

    if (Result.Errors.Num() == 0)
    {
        Result.bSuccess = true;
        Result.Status = TEXT("succeeded");
    }
    return Result;
}
