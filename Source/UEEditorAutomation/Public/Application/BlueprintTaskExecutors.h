#pragma once

#include "Application/TaskExecutor.h"
#include "Domain/BlueprintAutomationService.h"
#include "Domain/BlueprintTemplateRegistry.h"

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

class FCopyLiveBlueprintValuesTaskExecutor : public FBlueprintTaskExecutorBase
{
public:
    explicit FCopyLiveBlueprintValuesTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FCopyBlueprintLiveOverridesTaskExecutor : public FBlueprintTaskExecutorBase
{
public:
    explicit FCopyBlueprintLiveOverridesTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FDiagnoseBlueprintPropertyPersistenceTaskExecutor : public FBlueprintTaskExecutorBase
{
public:
    explicit FDiagnoseBlueprintPropertyPersistenceTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FCreateBlueprintFromTemplateTaskExecutor : public FBlueprintTaskExecutorBase
{
public:
    explicit FCreateBlueprintFromTemplateTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;

protected:
    bool BuildExpandedRequest(const FAutomationTaskRequest& Request, FAutomationTaskRequest& OutExpandedRequest, FAutomationTaskResult& OutResult) const;
    bool ApplyComponentOverrides(FAutomationTaskRequest& ExpandedRequest, const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) const;
    void MergeProperties(TArray<FAutomationPropertyValue>& TargetProperties, const TArray<FAutomationPropertyValue>& OverrideProperties) const;

    FBlueprintTemplateRegistry TemplateRegistry;
};

class FBatchCreateBlueprintsTaskExecutor : public FCreateBlueprintFromTemplateTaskExecutor
{
public:
    explicit FBatchCreateBlueprintsTaskExecutor(const TSharedRef<FBlueprintAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;

private:
    FAutomationTaskRequest BuildItemRequest(const FAutomationTaskRequest& BatchRequest, const FAutomationBatchBlueprintItem& Item) const;
};
