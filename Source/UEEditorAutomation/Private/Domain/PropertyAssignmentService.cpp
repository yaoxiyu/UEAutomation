#include "Domain/PropertyAssignmentService.h"

#include "Core/AutomationWhitelist.h"
#include "Dom/JsonValue.h"
#include "UObject/UnrealType.h"

bool FPropertyAssignmentService::AssignProperties(UObject* Target, const TArray<FAutomationPropertyValue>& Properties, FAutomationTaskResult& OutResult, const FString& FieldPrefix) const
{
    bool bOk = true;
    for (int32 Index = 0; Index < Properties.Num(); ++Index)
    {
        const FString Field = FString::Printf(TEXT("%s[%d]"), *FieldPrefix, Index);
        bOk &= AssignProperty(Target, Properties[Index], OutResult, Field);
    }
    return bOk;
}

bool FPropertyAssignmentService::AssignProperty(UObject* Target, const FAutomationPropertyValue& PropertyValue, FAutomationTaskResult& OutResult, const FString& FieldPrefix) const
{
    if (!Target)
    {
        OutResult.AddError(TEXT("PropertyAssignmentFailed"), TEXT("Target object is null."), FieldPrefix);
        return false;
    }

    const FAutomationWhitelist Whitelist = FAutomationWhitelistProvider::Load();
    if (!Whitelist.AllowedPropertyNames.Contains(PropertyValue.Name))
    {
        OutResult.AddError(TEXT("PropertyAssignmentNotAllowed"), FString::Printf(TEXT("Property '%s' is not allowed."), *PropertyValue.Name), FieldPrefix + TEXT(".name"));
        return false;
    }

    FProperty* Property = Target->GetClass()->FindPropertyByName(FName(*PropertyValue.Name));
    if (!Property)
    {
        OutResult.AddError(TEXT("PropertyNotFound"), FString::Printf(TEXT("Property '%s' not found on '%s'."), *PropertyValue.Name, *Target->GetClass()->GetName()), FieldPrefix + TEXT(".name"));
        return false;
    }

    FString Error;
    if (!ImportTextValue(Target, Property, PropertyValue, Error))
    {
        OutResult.AddError(TEXT("InvalidPropertyValue"), Error, FieldPrefix + TEXT(".value"));
        return false;
    }

    Target->Modify();
    OutResult.Metrics.PropertyAssignCount++;
    return true;
}

bool FPropertyAssignmentService::ImportTextValue(UObject* Target, FProperty* Property, const FAutomationPropertyValue& PropertyValue, FString& OutError) const
{
    if (!PropertyValue.Value.IsValid())
    {
        OutError = FString::Printf(TEXT("Property '%s' has no value."), *PropertyValue.Name);
        return false;
    }

    const FString ImportText = JsonValueToImportText(PropertyValue);
    if (ImportText.IsEmpty() && PropertyValue.Type != TEXT("string") && PropertyValue.Type != TEXT("text"))
    {
        OutError = FString::Printf(TEXT("Property '%s' value cannot be converted."), *PropertyValue.Name);
        return false;
    }

    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
    const TCHAR* Result = Property->ImportText(*ImportText, ValuePtr, PPF_None, Target);
    if (!Result)
    {
        OutError = FString::Printf(TEXT("Failed to import '%s' into property '%s'."), *ImportText, *PropertyValue.Name);
        return false;
    }

    return true;
}

FString FPropertyAssignmentService::JsonValueToImportText(const FAutomationPropertyValue& PropertyValue) const
{
    const FString Type = PropertyValue.Type.ToLower();
    const TSharedPtr<FJsonValue>& Value = PropertyValue.Value;

    if (Type == TEXT("bool"))
    {
        return Value->AsBool() ? TEXT("True") : TEXT("False");
    }
    if (Type == TEXT("int") || Type == TEXT("int64") || Type == TEXT("float") || Type == TEXT("double"))
    {
        return FString::SanitizeFloat(Value->AsNumber());
    }
    if (Type == TEXT("name") || Type == TEXT("string") || Type == TEXT("text") || Type == TEXT("enum"))
    {
        return Value->AsString();
    }
    if (Type == TEXT("object_path") || Type == TEXT("soft_object_path") || Type == TEXT("class_path") || Type == TEXT("soft_class_path"))
    {
        return Value->AsString();
    }

    if (Type == TEXT("vector"))
    {
        const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
        if (Array.Num() != 3)
        {
            return FString();
        }
        return FString::Printf(TEXT("(X=%s,Y=%s,Z=%s)"),
            *FString::SanitizeFloat(Array[0]->AsNumber()),
            *FString::SanitizeFloat(Array[1]->AsNumber()),
            *FString::SanitizeFloat(Array[2]->AsNumber()));
    }

    if (Type == TEXT("rotator"))
    {
        const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
        if (Array.Num() != 3)
        {
            return FString();
        }
        return FString::Printf(TEXT("(Pitch=%s,Yaw=%s,Roll=%s)"),
            *FString::SanitizeFloat(Array[0]->AsNumber()),
            *FString::SanitizeFloat(Array[1]->AsNumber()),
            *FString::SanitizeFloat(Array[2]->AsNumber()));
    }

    if (Type == TEXT("vector2d"))
    {
        const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
        if (Array.Num() != 2)
        {
            return FString();
        }
        return FString::Printf(TEXT("(X=%s,Y=%s)"),
            *FString::SanitizeFloat(Array[0]->AsNumber()),
            *FString::SanitizeFloat(Array[1]->AsNumber()));
    }

    if (Type == TEXT("color") || Type == TEXT("linear_color"))
    {
        const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
        if (Array.Num() != 4)
        {
            return FString();
        }
        return FString::Printf(TEXT("(R=%s,G=%s,B=%s,A=%s)"),
            *FString::SanitizeFloat(Array[0]->AsNumber()),
            *FString::SanitizeFloat(Array[1]->AsNumber()),
            *FString::SanitizeFloat(Array[2]->AsNumber()),
            *FString::SanitizeFloat(Array[3]->AsNumber()));
    }

    if (Type == TEXT("transform"))
    {
        const TSharedPtr<FJsonObject> Object = Value->AsObject();
        if (!Object.IsValid())
        {
            return FString();
        }
        return TEXT("()");
    }

    return Value->AsString();
}
