#include "UI/AutomationDebugPanel.h"

#include "Core/EditorAutomationSettings.h"
#include "Dom/JsonObject.h"
#include "HAL/FileManager.h"
#include "HAL/PlatformProcess.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"
#include "Widgets/Input/SButton.h"
#include "Widgets/Layout/SBorder.h"
#include "Widgets/Layout/SScrollBox.h"
#include "Widgets/SBoxPanel.h"
#include "Widgets/Text/STextBlock.h"
#include "Widgets/Views/SListView.h"
#include "Widgets/Views/STableRow.h"

void SAutomationDebugPanel::Construct(const FArguments& InArgs)
{
    RefreshRows();

    ChildSlot
    [
        SNew(SBorder)
        .Padding(8.0f)
        [
            SNew(SVerticalBox)
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 0.0f, 0.0f, 8.0f)
            [
                SNew(SHorizontalBox)
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Refresh")))
                    .OnClicked(this, &SAutomationDebugPanel::Refresh)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                .Padding(0.0f, 0.0f, 6.0f, 0.0f)
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Open Result")))
                    .IsEnabled(this, &SAutomationDebugPanel::HasSelectedResult)
                    .OnClicked(this, &SAutomationDebugPanel::OpenSelectedResult)
                ]
                + SHorizontalBox::Slot()
                .AutoWidth()
                [
                    SNew(SButton)
                    .Text(FText::FromString(TEXT("Open Log")))
                    .IsEnabled(this, &SAutomationDebugPanel::HasSelectedLog)
                    .OnClicked(this, &SAutomationDebugPanel::OpenSelectedLog)
                ]
            ]
            + SVerticalBox::Slot()
            .FillHeight(1.0f)
            [
                SAssignNew(ListView, SListView<FTaskRowPtr>)
                .ListItemsSource(&Rows)
                .OnGenerateRow(this, &SAutomationDebugPanel::GenerateRow)
                .OnSelectionChanged_Lambda([this](FTaskRowPtr Row, ESelectInfo::Type)
                {
                    SelectedRow = Row;
                })
            ]
            + SVerticalBox::Slot()
            .AutoHeight()
            .Padding(0.0f, 8.0f, 0.0f, 0.0f)
            [
                SNew(STextBlock)
                .Text(this, &SAutomationDebugPanel::GetSelectedSummaryText)
            ]
        ]
    ];
}

TSharedRef<ITableRow> SAutomationDebugPanel::GenerateRow(FTaskRowPtr Row, const TSharedRef<STableViewBase>& OwnerTable) const
{
    const FString TaskId = Row.IsValid() ? Row->TaskId : FString();
    const FString TaskType = Row.IsValid() ? Row->TaskType : FString();
    const FString Status = Row.IsValid() ? Row->Status : FString();
    const FString Time = Row.IsValid() ? Row->LastWriteTime.ToString(TEXT("%Y-%m-%d %H:%M:%S")) : FString();

    return SNew(STableRow<FTaskRowPtr>, OwnerTable)
        [
            SNew(SHorizontalBox)
            + SHorizontalBox::Slot()
            .FillWidth(0.34f)
            .Padding(2.0f)
            [
                SNew(STextBlock).Text(FText::FromString(TaskId))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(0.24f)
            .Padding(2.0f)
            [
                SNew(STextBlock).Text(FText::FromString(TaskType))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(0.16f)
            .Padding(2.0f)
            [
                SNew(STextBlock).Text(FText::FromString(Status))
            ]
            + SHorizontalBox::Slot()
            .FillWidth(0.26f)
            .Padding(2.0f)
            [
                SNew(STextBlock).Text(FText::FromString(Time))
            ]
        ];
}

FReply SAutomationDebugPanel::Refresh()
{
    RefreshRows();
    if (ListView.IsValid())
    {
        ListView->RequestListRefresh();
    }
    return FReply::Handled();
}

FReply SAutomationDebugPanel::OpenSelectedResult() const
{
    if (SelectedRow.IsValid() && !SelectedRow->ResultPath.IsEmpty())
    {
        FPlatformProcess::LaunchFileInDefaultExternalApplication(*SelectedRow->ResultPath);
    }
    return FReply::Handled();
}

FReply SAutomationDebugPanel::OpenSelectedLog() const
{
    if (SelectedRow.IsValid() && !SelectedRow->LogPath.IsEmpty())
    {
        FPlatformProcess::LaunchFileInDefaultExternalApplication(*SelectedRow->LogPath);
    }
    return FReply::Handled();
}

void SAutomationDebugPanel::RefreshRows()
{
    Rows.Reset();
    SelectedRow.Reset();

    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    TArray<FString> Files;
    IFileManager::Get().FindFiles(Files, *(Settings->ResultDir.Path / TEXT("*.result.json")), true, false);

    Files.Sort([Settings](const FString& A, const FString& B)
    {
        const FString PathA = Settings->ResultDir.Path / A;
        const FString PathB = Settings->ResultDir.Path / B;
        return IFileManager::Get().GetTimeStamp(*PathA) > IFileManager::Get().GetTimeStamp(*PathB);
    });

    const int32 MaxRows = FMath::Min(Files.Num(), 100);
    for (int32 Index = 0; Index < MaxRows; ++Index)
    {
        const FString ResultPath = Settings->ResultDir.Path / Files[Index];
        FTaskRowPtr Row = MakeShared<FAutomationDebugTaskRow>();
        Row->ResultPath = ResultPath;
        Row->TaskId = FPaths::GetBaseFilename(ResultPath).Replace(TEXT(".result"), TEXT(""));
        Row->Status = TEXT("<unknown>");
        Row->LastWriteTime = IFileManager::Get().GetTimeStamp(*ResultPath);
        ReadResultSummary(ResultPath, *Row);
        Rows.Add(Row);
    }
}

void SAutomationDebugPanel::ReadResultSummary(const FString& ResultPath, FAutomationDebugTaskRow& Row) const
{
    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *ResultPath))
    {
        return;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        return;
    }

    Root->TryGetStringField(TEXT("task_id"), Row.TaskId);
    Root->TryGetStringField(TEXT("task_type"), Row.TaskType);
    Root->TryGetStringField(TEXT("status"), Row.Status);
    Root->TryGetStringField(TEXT("log_path"), Row.LogPath);
}

FText SAutomationDebugPanel::GetSelectedSummaryText() const
{
    if (!SelectedRow.IsValid())
    {
        return FText::FromString(TEXT("No task selected."));
    }

    return FText::FromString(FString::Printf(TEXT("%s | %s | %s"), *SelectedRow->TaskId, *SelectedRow->TaskType, *SelectedRow->Status));
}

bool SAutomationDebugPanel::HasSelectedResult() const
{
    return SelectedRow.IsValid() && !SelectedRow->ResultPath.IsEmpty() && FPaths::FileExists(SelectedRow->ResultPath);
}

bool SAutomationDebugPanel::HasSelectedLog() const
{
    return SelectedRow.IsValid() && !SelectedRow->LogPath.IsEmpty() && FPaths::FileExists(SelectedRow->LogPath);
}
