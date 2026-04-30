#pragma once

#include "Application/TaskExecutor.h"
#include "Domain/AssetDuplicationService.h"

class FAssetDuplicationTaskExecutorBase : public ITaskExecutor
{
public:
    explicit FAssetDuplicationTaskExecutorBase(const TSharedRef<FAssetDuplicationService>& InService) : Service(InService) {}
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;

protected:
    TSharedRef<FAssetDuplicationService> Service;
};

class FDuplicateAssetTaskExecutor : public FAssetDuplicationTaskExecutorBase
{
public:
    using FAssetDuplicationTaskExecutorBase::FAssetDuplicationTaskExecutorBase;
    virtual FString GetTaskType() const override { return TEXT("duplicate_asset"); }
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FRedirectAssetReferencesTaskExecutor : public FAssetDuplicationTaskExecutorBase
{
public:
    using FAssetDuplicationTaskExecutorBase::FAssetDuplicationTaskExecutorBase;
    virtual FString GetTaskType() const override { return TEXT("redirect_asset_references"); }
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FListDirectoryAssetsTaskExecutor : public FAssetDuplicationTaskExecutorBase
{
public:
    using FAssetDuplicationTaskExecutorBase::FAssetDuplicationTaskExecutorBase;
    virtual FString GetTaskType() const override { return TEXT("list_directory_assets"); }
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FDeleteDirectoryAssetsTaskExecutor : public FAssetDuplicationTaskExecutorBase
{
public:
    using FAssetDuplicationTaskExecutorBase::FAssetDuplicationTaskExecutorBase;
    virtual FString GetTaskType() const override { return TEXT("delete_directory_assets"); }
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};
