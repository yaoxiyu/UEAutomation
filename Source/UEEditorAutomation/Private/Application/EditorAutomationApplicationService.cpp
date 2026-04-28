#include "Application/EditorAutomationApplicationService.h"

#include "Core/AutomationLog.h"
#include "Core/EditorAutomationSettings.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"

void FEditorAutomationApplicationService::Initialize()
{
    TaskSource.EnsureDirectories();
    ResultSink.EnsureDirectories();
    RecoverStaleWorkingTasks();
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

void FEditorAutomationApplicationService::RecoverStaleWorkingTasks()
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();

    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Settings->TaskWorkingDir.Path / TEXT("*.json")), true, false);
    Files.Sort();

    for (const FString& File : Files)
    {
        FDiscoveredAutomationTask Task;
        Task.WorkingPath = Settings->TaskWorkingDir.Path / File;
        Task.OriginalPath = Task.WorkingPath;

        FAutomationTaskRequest Request;
        FAutomationTaskResult Result;
        if (FFileHelper::LoadFileToString(Task.JsonText, *Task.WorkingPath)
            && FAutomationProtocolJson::ParseRequest(Task.JsonText, Request, Result))
        {
            Result.ProtocolVersion = Request.ProtocolVersion > 0 ? Request.ProtocolVersion : Result.ProtocolVersion;
            Result.TaskId = Request.TaskId;
            Result.TaskType = Request.TaskType;
        }

        if (Result.TaskId.IsEmpty())
        {
            Result.TaskId = FPaths::GetBaseFilename(Task.WorkingPath);
        }
        if (Result.TaskType.IsEmpty())
        {
            Result.TaskType = TEXT("<unknown>");
        }

        Result.AddLog(FString::Printf(TEXT("startup_recovery: stale working task detected at %s"), *Task.WorkingPath));
        Result.AddError(TEXT("RecoveredStaleWorkingTask"), TEXT("Task was found in working during editor startup and was moved to failed."), TEXT("tasks.working"));
        Result.Metrics.DurationMs = 0;

        ResultSink.WriteResult(Result);
        TaskSource.MoveToFailed(Task);
    }
}
