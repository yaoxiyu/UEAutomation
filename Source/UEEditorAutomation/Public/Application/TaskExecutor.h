#pragma once

#include "CoreMinimal.h"
#include "Protocol/AutomationProtocolTypes.h"

class ITaskExecutor
{
public:
    virtual ~ITaskExecutor() {}
    virtual FString GetTaskType() const = 0;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) = 0;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) = 0;
};

class FTaskExecutorRegistry
{
public:
    void RegisterExecutor(const TSharedRef<ITaskExecutor>& Executor);
    TSharedPtr<ITaskExecutor> FindExecutor(const FString& TaskType) const;
    void GetAllTaskTypes(TArray<FString>& OutTaskTypes) const;

private:
    TMap<FString, TSharedPtr<ITaskExecutor>> Executors;
};
