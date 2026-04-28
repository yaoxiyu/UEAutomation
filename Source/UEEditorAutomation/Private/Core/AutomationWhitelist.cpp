#include "Core/AutomationWhitelist.h"

#include "Core/EditorAutomationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

FAutomationWhitelist FAutomationWhitelistProvider::Load()
{
    FAutomationWhitelist Whitelist;

    FString JsonText;
    const FString WhitelistPath = ResolveWhitelistPath();
    if (!FFileHelper::LoadFileToString(JsonText, *WhitelistPath))
    {
        return Whitelist;
    }

    TSharedPtr<FJsonObject> JsonObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        return Whitelist;
    }

    ReadStringArray(JsonObject, TEXT("allowed_task_types"), Whitelist.AllowedTaskTypes);
    ReadStringArray(JsonObject, TEXT("allowed_asset_roots"), Whitelist.AllowedAssetRoots);
    ReadStringArray(JsonObject, TEXT("allowed_parent_classes"), Whitelist.AllowedParentClasses);
    ReadStringArray(JsonObject, TEXT("allowed_component_classes"), Whitelist.AllowedComponentClasses);
    ReadStringArray(JsonObject, TEXT("allowed_property_names"), Whitelist.AllowedPropertyNames);

    return Whitelist;
}

FString FAutomationWhitelistProvider::ResolveWhitelistPath()
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    FString Path = Settings->WhitelistFilePath.FilePath;
    if (Path.IsEmpty())
    {
        Path = TEXT("Plugins/UEEditorAutomation/Config/UEEditorAutomationWhitelist.json");
    }

    if (FPaths::IsRelative(Path))
    {
        Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
    }

    return Path;
}

void FAutomationWhitelistProvider::ReadStringArray(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, TArray<FString>& OutValues)
{
    const TArray<TSharedPtr<FJsonValue>>* Values = nullptr;
    if (!JsonObject.IsValid() || !JsonObject->TryGetArrayField(FieldName, Values))
    {
        return;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Values)
    {
        FString StringValue;
        if (Value.IsValid() && Value->TryGetString(StringValue) && !StringValue.IsEmpty())
        {
            OutValues.Add(StringValue);
        }
    }
}
