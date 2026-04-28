#pragma once

#include "CoreMinimal.h"
#include "Protocol/AutomationProtocolTypes.h"

class FProperty;

class FPropertyAssignmentService
{
public:
    bool AssignProperties(UObject* Target, const TArray<FAutomationPropertyValue>& Properties, FAutomationTaskResult& OutResult, const FString& FieldPrefix) const;
    bool AssignProperty(UObject* Target, const FAutomationPropertyValue& PropertyValue, FAutomationTaskResult& OutResult, const FString& FieldPrefix) const;

private:
    bool TryAssignSpecialProperty(UObject* Target, const FAutomationPropertyValue& PropertyValue, FAutomationTaskResult& OutResult, const FString& FieldPrefix, bool& bOutHandled) const;
    bool ImportTextValue(UObject* Target, FProperty* Property, const FAutomationPropertyValue& PropertyValue, FString& OutError) const;
    bool JsonValueToImportTextForProperty(const TSharedPtr<FJsonValue>& Value, FProperty* Property, FString& OutImportText, FString& OutError) const;
    bool JsonObjectToStructImportText(const TSharedPtr<class FJsonObject>& Object, FString& OutImportText, FString& OutError) const;
    bool JsonArrayToArrayImportText(const TArray<TSharedPtr<FJsonValue>>& Array, class FArrayProperty* Property, FString& OutImportText, FString& OutError) const;
    bool JsonArrayToSetImportText(const TArray<TSharedPtr<FJsonValue>>& Array, class FSetProperty* Property, FString& OutImportText, FString& OutError) const;
    bool JsonObjectToMapImportText(const TSharedPtr<class FJsonObject>& Object, class FMapProperty* Property, FString& OutImportText, FString& OutError) const;
    bool JsonMapArrayToMapImportText(const TArray<TSharedPtr<FJsonValue>>& Array, class FMapProperty* Property, FString& OutImportText, FString& OutError) const;
    FString JsonValueToImportText(const FAutomationPropertyValue& PropertyValue) const;
    FString JsonValueToString(const FAutomationPropertyValue& PropertyValue) const;
    FString NormalizeObjectPath(const FString& ObjectPath) const;
    FString EscapeImportString(const FString& Value) const;
};
