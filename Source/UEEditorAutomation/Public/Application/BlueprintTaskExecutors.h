#pragma once

#include "Application/TaskExecutor.h"
#include "Domain/BlueprintAutomationService.h"

class FBlueprintTaskExecutorBase : public ITaskExecutor
{
public:
    explicit FBlueprintTaskExecutorBase(const TSharedRef<FBlueprintAutomationService>& InService);
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;

protected:
    TSharedRef<FBlueprintAutomationService> Service;
};

class FCreateBlueprintTaskExecutor : public FBlueprintTaskExecutorBase
{
public:
    explicit FCreateBlueprintTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FModifyBlueprintComponentsTaskExecutor : public FBlueprintTaskExecutorBase
{
public:
    explicit FModifyBlueprintComponentsTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FModifyBlueprintDefaultsTaskExecutor : public FBlueprintTaskExecutorBase
{
public:
    explicit FModifyBlueprintDefaultsTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};
