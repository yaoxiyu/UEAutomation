#include "Application/TaskExecutor.h"

void FTaskExecutorRegistry::RegisterExecutor(const TSharedRef<ITaskExecutor>& Executor)
{
    Executors.Add(Executor->GetTaskType(), Executor);
}

TSharedPtr<ITaskExecutor> FTaskExecutorRegistry::FindExecutor(const FString& TaskType) const
{
    const TSharedPtr<ITaskExecutor>* Found = Executors.Find(TaskType);
    return Found ? *Found : nullptr;
}

void FTaskExecutorRegistry::GetAllTaskTypes(TArray<FString>& OutTaskTypes) const
{
    OutTaskTypes.Reset(Executors.Num());
    Executors.GenerateKeyArray(OutTaskTypes);
    OutTaskTypes.Sort();
}
