#pragma once

#include "Application/TaskExecutor.h"
#include "Domain/AssetAutomationService.h"

class FAssetTaskExecutorBase : public ITaskExecutor
{
public:
    explicit FAssetTaskExecutorBase(const TSharedRef<FAssetAutomationService>& InService);

protected:
    TSharedRef<FAssetAutomationService> Service;
};

class FCreateDataAssetTaskExecutor : public FAssetTaskExecutorBase
{
public:
    explicit FCreateDataAssetTaskExecutor(const TSharedRef<FAssetAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FModifyAssetPropertiesTaskExecutor : public FAssetTaskExecutorBase
{
public:
    explicit FModifyAssetPropertiesTaskExecutor(const TSharedRef<FAssetAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FCheckAssetRulesTaskExecutor : public FAssetTaskExecutorBase
{
public:
    explicit FCheckAssetRulesTaskExecutor(const TSharedRef<FAssetAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FGenerateAuditReportTaskExecutor : public FAssetTaskExecutorBase
{
public:
    explicit FGenerateAuditReportTaskExecutor(const TSharedRef<FAssetAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};
