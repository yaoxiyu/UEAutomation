#pragma once

#include "CoreMinimal.h"
#include "Protocol/AutomationProtocolTypes.h"

class FProperty;

class FPropertyAssignmentService
{
public:
    bool AssignProperties(UObject* Target, const TArray<FAutomationPropertyValue>& Properties, FAutomationTaskResult& OutResult, const FString& FieldPrefix, const TArray<FAutomationAssetRedirect>& Redirects = TArray<FAutomationAssetRedirect>()) const;
    bool AssignProperty(UObject* Target, const FAutomationPropertyValue& PropertyValue, FAutomationTaskResult& OutResult, const FString& FieldPrefix, const TArray<FAutomationAssetRedirect>& Redirects = TArray<FAutomationAssetRedirect>()) const;
    bool ExportAssignedPropertyText(UObject* Target, const FString& PropertyName, FString& OutText, FString& OutError) const;
    bool BuildExpectedImportText(FProperty* Property, const FAutomationPropertyValue& PropertyValue, const TArray<FAutomationAssetRedirect>& Redirects, FString& OutImportText, bool& bOutRawImportText, FString& OutError) const;
    bool BuildExpectedAssignedPropertyText(FProperty* Property, const FAutomationPropertyValue& PropertyValue, const TArray<FAutomationAssetRedirect>& Redirects, UObject* OwnerForPortFlags, FString& OutExpectedText, FString& OutError) const;

private:
    bool TryAssignSpecialProperty(UObject* Target, const FAutomationPropertyValue& PropertyValue, FAutomationTaskResult& OutResult, const FString& FieldPrefix, bool& bOutHandled) const;
    bool TryAssignSpecialReflectedProperty(UObject* Target, FProperty* Property, const FAutomationPropertyValue& PropertyValue, const TArray<FAutomationAssetRedirect>& Redirects, FString& OutError, bool& bOutHandled) const;
    bool AssignStructFieldsFromJson(void* StructAddress, class UScriptStruct* Struct, const TSharedPtr<class FJsonObject>& Object, const TArray<FAutomationAssetRedirect>& Redirects, FString& OutError) const;
    bool ImportTextValue(UObject* Target, FProperty* Property, const FAutomationPropertyValue& PropertyValue, const TArray<FAutomationAssetRedirect>& Redirects, FString& OutError) const;
    bool ValidateImportedPropertyRoundTrip(UObject* Target, FProperty* Property, const FString& PropertyName, FString& OutError) const;
    bool ValidateImportedPropertyMatchesText(UObject* Target, FProperty* Property, const FString& PropertyName, const FString& ImportText, FString& OutError) const;
    FString RewriteImportTextRedirects(const FString& ImportText, const TArray<FAutomationAssetRedirect>& Redirects) const;
    bool JsonValueToImportTextForProperty(const TSharedPtr<FJsonValue>& Value, FProperty* Property, FString& OutImportText, FString& OutError) const;
    bool JsonObjectToStructImportText(const TSharedPtr<class FJsonObject>& Object, class FStructProperty* Property, FString& OutImportText, FString& OutError) const;
    bool TryBuildSpecialStructImportText(const TSharedPtr<class FJsonObject>& Object, class FStructProperty* Property, FString& OutImportText, FString& OutError, bool& bOutHandled) const;
    bool JsonArrayToArrayImportText(const TArray<TSharedPtr<FJsonValue>>& Array, class FArrayProperty* Property, FString& OutImportText, FString& OutError) const;
    bool JsonArrayToSetImportText(const TArray<TSharedPtr<FJsonValue>>& Array, class FSetProperty* Property, FString& OutImportText, FString& OutError) const;
    bool JsonObjectToMapImportText(const TSharedPtr<class FJsonObject>& Object, class FMapProperty* Property, FString& OutImportText, FString& OutError) const;
    bool JsonMapArrayToMapImportText(const TArray<TSharedPtr<FJsonValue>>& Array, class FMapProperty* Property, FString& OutImportText, FString& OutError) const;
    FString JsonValueToString(const FAutomationPropertyValue& PropertyValue) const;
    FString NormalizeObjectPath(const FString& ObjectPath) const;
    FString EscapeImportString(const FString& Value) const;
};
