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
        Whitelist.LoadError = FString::Printf(TEXT("Whitelist file '%s' could not be loaded."), *WhitelistPath);
        return Whitelist;
    }

    TSharedPtr<FJsonObject> JsonObject;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, JsonObject) || !JsonObject.IsValid())
    {
        Whitelist.LoadError = FString::Printf(TEXT("Whitelist file '%s' is not valid JSON."), *WhitelistPath);
        return Whitelist;
    }

    FString PolicyMode;
    JsonObject->TryGetStringField(TEXT("policy_mode"), PolicyMode);
    if (PolicyMode.IsEmpty())
    {
        PolicyMode = TEXT("open");
    }
    if (PolicyMode != TEXT("open") && PolicyMode != TEXT("strict"))
    {
        Whitelist.LoadError = FString::Printf(TEXT("Whitelist policy_mode must be 'open' or 'strict', got '%s'."), *PolicyMode);
        return Whitelist;
    }
    Whitelist.bStrictMode = PolicyMode == TEXT("strict");

    if (!ReadStringArray(JsonObject, TEXT("allowed_task_types"), Whitelist.AllowedTaskTypes, Whitelist.LoadError)
        || !ReadStringArray(JsonObject, TEXT("allowed_asset_roots"), Whitelist.AllowedAssetRoots, Whitelist.LoadError)
        || !ReadStringArray(JsonObject, TEXT("allowed_parent_classes"), Whitelist.AllowedParentClasses, Whitelist.LoadError)
        || !ReadStringArray(JsonObject, TEXT("allowed_component_classes"), Whitelist.AllowedComponentClasses, Whitelist.LoadError)
        || !ReadStringArray(JsonObject, TEXT("allowed_property_names"), Whitelist.AllowedPropertyNames, Whitelist.LoadError)
        || !ReadStringArray(JsonObject, TEXT("denied_property_names_for_export"), Whitelist.DeniedPropertyNamesForExport, Whitelist.LoadError))
    {
        return Whitelist;
    }

    if (Whitelist.bStrictMode && (Whitelist.AllowedTaskTypes.Num() == 0 || Whitelist.AllowedAssetRoots.Num() == 0))
    {
        Whitelist.LoadError = TEXT("Whitelist policy_mode='strict' requires non-empty allowed_task_types and allowed_asset_roots.");
        return Whitelist;
    }

    Whitelist.bLoaded = true;

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

bool FAutomationWhitelistProvider::ReadStringArray(const TSharedPtr<FJsonObject>& JsonObject, const TCHAR* FieldName, TArray<FString>& OutValues, FString& OutError)
{
    if (!JsonObject.IsValid())
    {
        OutError = TEXT("Whitelist JSON object is invalid.");
        return false;
    }

    const TSharedPtr<FJsonValue>* RawField = JsonObject->Values.Find(FieldName);
    if (!RawField)
    {
        return true;
    }
    if (!RawField->IsValid() || (*RawField)->Type != EJson::Array)
    {
        OutError = FString::Printf(TEXT("Whitelist field '%s' must be an array of strings."), FieldName);
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>& Values = (*RawField)->AsArray();
    for (const TSharedPtr<FJsonValue>& Value : Values)
    {
        FString StringValue;
        if (Value.IsValid() && Value->TryGetString(StringValue) && !StringValue.IsEmpty())
        {
            OutValues.Add(StringValue);
            continue;
        }

        OutError = FString::Printf(TEXT("Whitelist field '%s' contains a non-string or empty value."), FieldName);
        return false;
    }

    return true;
}
