#include "Core/StableJsonWriter.h"

#include "HAL/FileManager.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Policies/CondensedJsonPrintPolicy.h"
#include "Policies/PrettyJsonPrintPolicy.h"
#include "Serialization/JsonSerializer.h"
#include "Serialization/JsonWriter.h"

bool FAutomationStableJsonWriter::SerializePretty(const TSharedRef<FJsonObject>& Root, FString& OutText)
{
    OutText.Reset();
    const TSharedRef<TJsonWriter<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>> Writer =
        TJsonWriterFactory<TCHAR, TPrettyJsonPrintPolicy<TCHAR>>::Create(&OutText);
    return FJsonSerializer::Serialize(Root, Writer);
}

bool FAutomationStableJsonWriter::WriteAtomic(
    const FString& AbsolutePath,
    const TSharedRef<FJsonObject>& Root,
    FString& OutError)
{
    if (AbsolutePath.IsEmpty())
    {
        OutError = TEXT("StableJsonWriter: empty path");
        return false;
    }

    FString JsonText;
    if (!SerializePretty(Root, JsonText))
    {
        OutError = TEXT("StableJsonWriter: pretty serialization failed");
        return false;
    }

    IFileManager& FileManager = IFileManager::Get();
    const FString Dir = FPaths::GetPath(AbsolutePath);
    if (!Dir.IsEmpty() && !FileManager.DirectoryExists(*Dir))
    {
        if (!FileManager.MakeDirectory(*Dir, true))
        {
            OutError = FString::Printf(TEXT("StableJsonWriter: failed to create directory: %s"), *Dir);
            return false;
        }
    }

    const FString TempPath = AbsolutePath + TEXT(".tmp");
    if (!FFileHelper::SaveStringToFile(JsonText, *TempPath, FFileHelper::EEncodingOptions::ForceUTF8WithoutBOM))
    {
        OutError = FString::Printf(TEXT("StableJsonWriter: failed to write temp file: %s"), *TempPath);
        return false;
    }

    if (FileManager.FileExists(*AbsolutePath))
    {
        FileManager.Delete(*AbsolutePath, false, true);
    }

    if (!FileManager.Move(*AbsolutePath, *TempPath, true, true))
    {
        OutError = FString::Printf(TEXT("StableJsonWriter: failed to move %s -> %s"), *TempPath, *AbsolutePath);
        FileManager.Delete(*TempPath, false, true);
        return false;
    }

    return true;
}
