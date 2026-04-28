#include "Transport/FileTaskTransport.h"

#include "Core/EditorAutomationSettings.h"
#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Protocol/AutomationProtocolTypes.h"

void FFileTaskSource::EnsureDirectories() const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    IFileManager::Get().MakeDirectory(*Settings->TaskInboxDir.Path, true);
    IFileManager::Get().MakeDirectory(*Settings->TaskWorkingDir.Path, true);
    IFileManager::Get().MakeDirectory(*Settings->TaskDoneDir.Path, true);
    IFileManager::Get().MakeDirectory(*Settings->TaskFailedDir.Path, true);
}

bool FFileTaskSource::TryAcquireNextTask(FDiscoveredAutomationTask& OutTask) const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();

    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Settings->TaskInboxDir.Path / TEXT("*.json")), true, false);
    Files.Sort();
    if (Files.Num() == 0)
    {
        return false;
    }

    OutTask.OriginalPath = Settings->TaskInboxDir.Path / Files[0];
    OutTask.WorkingPath = Settings->TaskWorkingDir.Path / Files[0];
    if (!MoveFileReplacing(OutTask.OriginalPath, OutTask.WorkingPath))
    {
        return false;
    }

    return FFileHelper::LoadFileToString(OutTask.JsonText, *OutTask.WorkingPath);
}

bool FFileTaskSource::MoveToDone(const FDiscoveredAutomationTask& Task) const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    return MoveFileReplacing(Task.WorkingPath, Settings->TaskDoneDir.Path / FPaths::GetCleanFilename(Task.WorkingPath));
}

bool FFileTaskSource::MoveToFailed(const FDiscoveredAutomationTask& Task) const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    return MoveFileReplacing(Task.WorkingPath, Settings->TaskFailedDir.Path / FPaths::GetCleanFilename(Task.WorkingPath));
}

bool FFileTaskSource::MoveFileReplacing(const FString& From, const FString& To)
{
    IFileManager::Get().Delete(*To, false, true);
    return IFileManager::Get().Move(*To, *From, true, true);
}

void FFileTaskResultSink::EnsureDirectories() const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    IFileManager::Get().MakeDirectory(*Settings->ResultDir.Path, true);
    IFileManager::Get().MakeDirectory(*Settings->LogDir.Path, true);
}

bool FFileTaskResultSink::WriteResult(FAutomationTaskResult& Result) const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();

    Result.LogPath = Settings->LogDir.Path / FString::Printf(TEXT("%s.log"), *Result.TaskId);
    FString LogText;
    LogText += FString::Printf(TEXT("task_id=%s\n"), *Result.TaskId);
    LogText += FString::Printf(TEXT("task_type=%s\n"), *Result.TaskType);
    LogText += FString::Printf(TEXT("status=%s\n"), *Result.Status);
    LogText += FString::Printf(TEXT("success=%s\n"), Result.bSuccess ? TEXT("true") : TEXT("false"));
    LogText += FString::Printf(TEXT("duration_ms=%lld\n"), Result.Metrics.DurationMs);
    for (const FString& Line : Result.LogLines)
    {
        LogText += FString::Printf(TEXT("step=%s\n"), *Line);
    }
    for (const FString& Warning : Result.Warnings)
    {
        LogText += FString::Printf(TEXT("warning=%s\n"), *Warning);
    }
    for (const FAutomationError& Error : Result.Errors)
    {
        LogText += FString::Printf(TEXT("error code=%s field=%s message=%s\n"), *Error.Code, *Error.Field, *Error.Message);
    }
    FFileHelper::SaveStringToFile(LogText, *Result.LogPath);

    FString JsonText;
    if (!FAutomationProtocolJson::SerializeResult(Result, JsonText))
    {
        return false;
    }

    const FString ResultPath = Settings->ResultDir.Path / FString::Printf(TEXT("%s.result.json"), *Result.TaskId);
    return FFileHelper::SaveStringToFile(JsonText, *ResultPath);
}
