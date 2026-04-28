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

class FCreateMaterialInstanceTaskExecutor : public FAssetTaskExecutorBase
{
public:
    explicit FCreateMaterialInstanceTaskExecutor(const TSharedRef<FAssetAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FModifyMaterialInstanceTaskExecutor : public FAssetTaskExecutorBase
{
public:
    explicit FModifyMaterialInstanceTaskExecutor(const TSharedRef<FAssetAutomationService>& InService);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FCreateTypedAssetTaskExecutor : public FAssetTaskExecutorBase
{
public:
    FCreateTypedAssetTaskExecutor(const TSharedRef<FAssetAutomationService>& InService, const FString& InTaskType);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;

private:
    FString TaskType;
};

class FImportAssetTaskExecutor : public FAssetTaskExecutorBase
{
public:
    FImportAssetTaskExecutor(const TSharedRef<FAssetAutomationService>& InService, const FString& InTaskType);
    virtual FString GetTaskType() const override;
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;

private:
    FString TaskType;
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
