#include "Transport/FileTaskTransport.h"

#include "Core/EditorAutomationSettings.h"
#include "HAL/FileManager.h"
#include "Misc/DateTime.h"
#include "Misc/FileHelper.h"
#include "Misc/Guid.h"
#include "Misc/Paths.h"
#include "Protocol/AutomationProtocolTypes.h"

namespace
{
    FString MakeSafeFileStem(const FString& Stem)
    {
        FString Safe = FPaths::GetCleanFilename(Stem);
        Safe.ReplaceInline(TEXT("\\"), TEXT("_"));
        Safe.ReplaceInline(TEXT("/"), TEXT("_"));
        Safe.ReplaceInline(TEXT(":"), TEXT("_"));
        Safe.ReplaceInline(TEXT("*"), TEXT("_"));
        Safe.ReplaceInline(TEXT("?"), TEXT("_"));
        Safe.ReplaceInline(TEXT("\""), TEXT("_"));
        Safe.ReplaceInline(TEXT("<"), TEXT("_"));
        Safe.ReplaceInline(TEXT(">"), TEXT("_"));
        Safe.ReplaceInline(TEXT("|"), TEXT("_"));
        return Safe.IsEmpty() ? TEXT("unknown_task") : Safe;
    }

    bool WriteStringAtomic(const FString& Path, const FString& Text)
    {
        if (Path.IsEmpty())
        {
            return false;
        }

        IFileManager& FileManager = IFileManager::Get();
        const FString Dir = FPaths::GetPath(Path);
        if (!Dir.IsEmpty() && !FileManager.DirectoryExists(*Dir))
        {
            FileManager.MakeDirectory(*Dir, true);
        }

        const FString TempPath = Path + TEXT(".tmp");
        if (!FFileHelper::SaveStringToFile(Text, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
        {
            return false;
        }
        if (!FileManager.Move(*Path, *TempPath, true, true))
        {
            FileManager.Delete(*TempPath, false, true);
            return false;
        }
        return true;
    }
}

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
    const FString DesiredWorkingPath = Settings->TaskWorkingDir.Path / Files[0];
    if (!MoveFileToUniquePath(OutTask.OriginalPath, DesiredWorkingPath, OutTask.WorkingPath))
    {
        return false;
    }

    return FFileHelper::LoadFileToString(OutTask.JsonText, *OutTask.WorkingPath);
}

bool FFileTaskSource::MoveToDone(const FDiscoveredAutomationTask& Task) const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    FString ActualPath;
    return MoveFileToUniquePath(Task.WorkingPath, Settings->TaskDoneDir.Path / FPaths::GetCleanFilename(Task.WorkingPath), ActualPath);
}

bool FFileTaskSource::MoveToFailed(const FDiscoveredAutomationTask& Task) const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    FString ActualPath;
    return MoveFileToUniquePath(Task.WorkingPath, Settings->TaskFailedDir.Path / FPaths::GetCleanFilename(Task.WorkingPath), ActualPath);
}

bool FFileTaskSource::MoveFileToUniquePath(const FString& From, const FString& DesiredTo, FString& OutActualTo)
{
    OutActualTo = MakeUniquePath(DesiredTo);
    return IFileManager::Get().Move(*OutActualTo, *From, false, true);
}

FString FFileTaskSource::MakeUniquePath(const FString& DesiredPath)
{
    IFileManager& FileManager = IFileManager::Get();
    const FString Dir = FPaths::GetPath(DesiredPath);
    if (!Dir.IsEmpty() && !FileManager.DirectoryExists(*Dir))
    {
        FileManager.MakeDirectory(*Dir, true);
    }

    if (!FileManager.FileExists(*DesiredPath))
    {
        return DesiredPath;
    }

    const FString Base = FPaths::GetBaseFilename(DesiredPath);
    const FString Ext = FPaths::GetExtension(DesiredPath, true);
    const FString Stamp = FDateTime::UtcNow().ToString(TEXT("%Y%m%dT%H%M%S%fZ"));
    for (int32 Counter = 1; Counter < 1000; ++Counter)
    {
        const FString Candidate = Dir / FString::Printf(TEXT("%s_%s_%03d%s"), *Base, *Stamp, Counter, *Ext);
        if (!FileManager.FileExists(*Candidate))
        {
            return Candidate;
        }
    }

    return Dir / FString::Printf(TEXT("%s_%s%s"), *Base, *FGuid::NewGuid().ToString(EGuidFormats::Digits), *Ext);
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

    const FString SafeTaskId = MakeSafeFileStem(Result.TaskId);
    Result.LogPath = Settings->LogDir.Path / FString::Printf(TEXT("%s.log"), *SafeTaskId);
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
    if (!WriteStringAtomic(Result.LogPath, LogText))
    {
        return false;
    }

    FString JsonText;
    if (!FAutomationProtocolJson::SerializeResult(Result, JsonText))
    {
        return false;
    }

    const FString ResultPath = Settings->ResultDir.Path / FString::Printf(TEXT("%s.result.json"), *SafeTaskId);
    return WriteStringAtomic(ResultPath, JsonText);
}
