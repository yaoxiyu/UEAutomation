#pragma once

#include "Application/TaskExecutor.h"
#include "Domain/BlueprintAnalysisService.h"

class FBlueprintAnalysisTaskExecutorBase : public ITaskExecutor
{
public:
    explicit FBlueprintAnalysisTaskExecutorBase(const TSharedRef<FBlueprintAnalysisService>& InService)
        : Service(InService) {}

    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;

protected:
    TSharedRef<FBlueprintAnalysisService> Service;
};

class FAnalyzeBlueprintTaskExecutor : public FBlueprintAnalysisTaskExecutorBase
{
public:
    using FBlueprintAnalysisTaskExecutorBase::FBlueprintAnalysisTaskExecutorBase;
    virtual FString GetTaskType() const override { return TEXT("analyze_blueprint"); }
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FAnalyzeBlueprintReferenceChainTaskExecutor : public FBlueprintAnalysisTaskExecutorBase
{
public:
    using FBlueprintAnalysisTaskExecutorBase::FBlueprintAnalysisTaskExecutorBase;
    virtual FString GetTaskType() const override { return TEXT("analyze_blueprint_reference_chain"); }
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FAnalyzeAssetTaskExecutor : public FBlueprintAnalysisTaskExecutorBase
{
public:
    using FBlueprintAnalysisTaskExecutorBase::FBlueprintAnalysisTaskExecutorBase;
    virtual FString GetTaskType() const override { return TEXT("analyze_asset"); }
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FRefreshBlueprintMetaCacheTaskExecutor : public FBlueprintAnalysisTaskExecutorBase
{
public:
    using FBlueprintAnalysisTaskExecutorBase::FBlueprintAnalysisTaskExecutorBase;
    virtual FString GetTaskType() const override { return TEXT("refresh_blueprint_meta_cache"); }
    virtual bool Validate(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};

class FExportBlueprintAIContextTaskExecutor : public FBlueprintAnalysisTaskExecutorBase
{
public:
    using FBlueprintAnalysisTaskExecutorBase::FBlueprintAnalysisTaskExecutorBase;
    virtual FString GetTaskType() const override { return TEXT("export_blueprint_ai_context"); }
    virtual bool Execute(const FAutomationTaskRequest& Request, FAutomationTaskResult& OutResult) override;
};
