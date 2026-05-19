#pragma once

#include "Application/TaskExecutor.h"

class FListTasksTaskExecutor : public ITaskExecutor
{
public:
    explicit FListTasksTaskExecutor(const FTaskExecutorRegistry& InRegistry)
        : Registry(InRegistry)
    {
    }

    virtual FString GetTaskType() const override { return TEXT("list_tasks"); }
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override { return true; }
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;

private:
    const FTaskExecutorRegistry& Registry;
};
