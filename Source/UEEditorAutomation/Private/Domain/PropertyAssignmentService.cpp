#include "Domain/PropertyAssignmentService.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/AutomationWhitelist.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
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
    if (!Whitelist.bLoaded)
    {
        OutResult.AddError(TEXT("WhitelistLoadFailed"), Whitelist.LoadError, TEXT("security.whitelist"));
        return false;
    }

    if (!Whitelist.AllowedPropertyNames.Contains(PropertyValue.Name))
    {
        OutResult.AddError(TEXT("PropertyAssignmentNotAllowed"), FString::Printf(TEXT("Property '%s' is not allowed."), *PropertyValue.Name), FieldPrefix + TEXT(".name"));
        return false;
    }

    bool bHandled = false;
    if (!TryAssignSpecialProperty(Target, PropertyValue, OutResult, FieldPrefix, bHandled))
    {
        return false;
    }
    if (bHandled)
    {
        Target->Modify();
        OutResult.Metrics.PropertyAssignCount++;
        return true;
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

bool FPropertyAssignmentService::TryAssignSpecialProperty(UObject* Target, const FAutomationPropertyValue& PropertyValue, FAutomationTaskResult& OutResult, const FString& FieldPrefix, bool& bOutHandled) const
{
    bOutHandled = false;

    if (PropertyValue.Name == TEXT("CollisionProfileName"))
    {
        UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Target);
        if (!PrimitiveComponent)
        {
            return true;
        }

        const FString ProfileName = JsonValueToString(PropertyValue);
        if (ProfileName.IsEmpty())
        {
            OutResult.AddError(TEXT("InvalidPropertyValue"), TEXT("CollisionProfileName requires a non-empty string value."), FieldPrefix + TEXT(".value"));
            return false;
        }

        PrimitiveComponent->SetCollisionProfileName(FName(*ProfileName));
        bOutHandled = true;
        return true;
    }

    if (PropertyValue.Name == TEXT("StaticMesh"))
    {
        UStaticMeshComponent* StaticMeshComponent = Cast<UStaticMeshComponent>(Target);
        if (!StaticMeshComponent)
        {
            return true;
        }

        const FString MeshPath = NormalizeObjectPath(JsonValueToString(PropertyValue));
        UStaticMesh* StaticMesh = LoadObject<UStaticMesh>(nullptr, *MeshPath);
        if (!StaticMesh)
        {
            OutResult.AddError(TEXT("InvalidPropertyValue"), FString::Printf(TEXT("StaticMesh '%s' could not be loaded."), *MeshPath), FieldPrefix + TEXT(".value"));
            return false;
        }

        StaticMeshComponent->SetStaticMesh(StaticMesh);
        bOutHandled = true;
        return true;
    }

    if (PropertyValue.Name == TEXT("SkeletalMesh"))
    {
        USkeletalMeshComponent* SkeletalMeshComponent = Cast<USkeletalMeshComponent>(Target);
        if (!SkeletalMeshComponent)
        {
            return true;
        }

        const FString MeshPath = NormalizeObjectPath(JsonValueToString(PropertyValue));
        USkeletalMesh* SkeletalMesh = LoadObject<USkeletalMesh>(nullptr, *MeshPath);
        if (!SkeletalMesh)
        {
            OutResult.AddError(TEXT("InvalidPropertyValue"), FString::Printf(TEXT("SkeletalMesh '%s' could not be loaded."), *MeshPath), FieldPrefix + TEXT(".value"));
            return false;
        }

        SkeletalMeshComponent->SetSkeletalMesh(SkeletalMesh);
        bOutHandled = true;
        return true;
    }

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

FString FPropertyAssignmentService::JsonValueToString(const FAutomationPropertyValue& PropertyValue) const
{
    return PropertyValue.Value.IsValid() ? PropertyValue.Value->AsString() : FString();
}

FString FPropertyAssignmentService::NormalizeObjectPath(const FString& ObjectPath) const
{
    int32 FirstQuote = INDEX_NONE;
    int32 LastQuote = INDEX_NONE;
    if (ObjectPath.FindChar(TEXT('\''), FirstQuote) && ObjectPath.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
    {
        return ObjectPath.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
    }
    return ObjectPath;
}
