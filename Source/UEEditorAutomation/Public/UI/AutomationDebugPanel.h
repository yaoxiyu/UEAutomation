#pragma once

#include "CoreMinimal.h"
#include "Widgets/SCompoundWidget.h"

struct FAutomationDebugTaskRow
{
    FString TaskId;
    FString TaskType;
    FString Status;
    FString ResultPath;
    FString LogPath;
    FDateTime LastWriteTime;
};

class SAutomationDebugPanel : public SCompoundWidget
{
public:
    SLATE_BEGIN_ARGS(SAutomationDebugPanel) {}
    SLATE_END_ARGS()

    void Construct(const FArguments& InArgs);

private:
    using FTaskRowPtr = TSharedPtr<FAutomationDebugTaskRow>;

    TSharedRef<class ITableRow> GenerateRow(FTaskRowPtr Row, const TSharedRef<class STableViewBase>& OwnerTable) const;
    FReply Refresh();
    FReply OpenSelectedResult() const;
    FReply OpenSelectedLog() const;
    void RefreshRows();
    void ReadResultSummary(const FString& ResultPath, FAutomationDebugTaskRow& Row) const;
    FText GetSelectedSummaryText() const;
    bool HasSelectedResult() const;
    bool HasSelectedLog() const;

    TArray<FTaskRowPtr> Rows;
    TSharedPtr<class SListView<FTaskRowPtr>> ListView;
    FTaskRowPtr SelectedRow;
};
