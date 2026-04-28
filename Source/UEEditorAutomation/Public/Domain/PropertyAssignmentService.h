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
    bool ImportTextValue(UObject* Target, FProperty* Property, const FAutomationPropertyValue& PropertyValue, FString& OutError) const;
    FString JsonValueToImportText(const FAutomationPropertyValue& PropertyValue) const;
};
