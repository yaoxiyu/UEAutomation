#include "Domain/PropertyAssignmentService.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/AutomationWhitelist.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "UObject/UnrealType.h"

namespace
{
    FString NormalizeImportPath(const FString& Value)
    {
        int32 FirstQuote = INDEX_NONE;
        int32 LastQuote = INDEX_NONE;
        if (Value.FindChar(TEXT('\''), FirstQuote) && Value.FindLastChar(TEXT('\''), LastQuote) && LastQuote > FirstQuote)
        {
            return Value.Mid(FirstQuote + 1, LastQuote - FirstQuote - 1);
        }
        return Value;
    }

    bool IsTruncatedObject(const TSharedPtr<FJsonObject>& Object)
    {
        bool bTruncated = false;
        return Object.IsValid() && Object->TryGetBoolField(TEXT("truncated"), bTruncated) && bTruncated;
    }

    bool IsTruncatedValue(const TSharedPtr<FJsonValue>& Value)
    {
        return Value.IsValid() && Value->Type == EJson::Object && IsTruncatedObject(Value->AsObject());
    }

    FString JsonStringOrNone(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName)
    {
        if (!Object.IsValid())
        {
            return TEXT("None");
        }

        const TSharedPtr<FJsonValue>* Value = Object->Values.Find(FieldName);
        if (!Value || !Value->IsValid() || (*Value)->Type == EJson::Null)
        {
            return TEXT("None");
        }
        if ((*Value)->Type == EJson::Object)
        {
            FString Name;
            if ((*Value)->AsObject()->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
            {
                return Name;
            }
            if ((*Value)->AsObject()->TryGetStringField(TEXT("TagName"), Name) && !Name.IsEmpty())
            {
                return Name;
            }
        }

        FString StringValue = (*Value)->AsString();
        return StringValue.IsEmpty() ? TEXT("None") : StringValue;
    }

    FString JsonNumberOrDefault(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, const TCHAR* DefaultValue)
    {
        if (!Object.IsValid())
        {
            return DefaultValue;
        }
        double Number = 0.0;
        if (Object->TryGetNumberField(FieldName, Number))
        {
            return FString::SanitizeFloat(Number);
        }
        return DefaultValue;
    }

    FString JsonBoolOrDefault(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, bool bDefault)
    {
        bool bValue = bDefault;
        if (Object.IsValid())
        {
            Object->TryGetBoolField(FieldName, bValue);
        }
        return bValue ? TEXT("True") : TEXT("False");
    }

    bool BuildGameplayTagImportTextFromValue(const TSharedPtr<FJsonValue>& Value, FString& OutText, FString& OutError)
    {
        if (!Value.IsValid() || Value->Type == EJson::Null)
        {
            OutText = TEXT("(TagName=\"None\")");
            return true;
        }
        if (IsTruncatedValue(Value))
        {
            OutError = TEXT("GameplayTag value is truncated.");
            return false;
        }

        FString TagName;
        if (Value->Type == EJson::Object)
        {
            const TSharedPtr<FJsonObject> TagObject = Value->AsObject();
            const TSharedPtr<FJsonValue>* TagNameValue = TagObject->Values.Find(TEXT("TagName"));
            if (TagNameValue && IsTruncatedValue(*TagNameValue))
            {
                OutError = TEXT("GameplayTag.TagName is truncated.");
                return false;
            }
            TagObject->TryGetStringField(TEXT("TagName"), TagName);
        }
        else
        {
            TagName = Value->AsString();
        }
        if (TagName.IsEmpty())
        {
            TagName = TEXT("None");
        }
        OutText = FString::Printf(TEXT("(TagName=\"%s\")"), *TagName);
        return true;
    }
}

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

    if (Whitelist.AllowedPropertyNames.Num() > 0 && !Whitelist.AllowedPropertyNames.Contains(PropertyValue.Name))
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

    FString ImportText;
    const FString Type = PropertyValue.Type.ToLower();
    if (Type == TEXT("array"))
    {
        if (!CastField<FArrayProperty>(Property))
        {
            OutError = FString::Printf(TEXT("Property '%s' is not an array property."), *PropertyValue.Name);
            return false;
        }
        if (!JsonValueToImportTextForProperty(PropertyValue.Value, Property, ImportText, OutError))
        {
            return false;
        }
    }
    else if (Type == TEXT("struct"))
    {
        if (!CastField<FStructProperty>(Property))
        {
            OutError = FString::Printf(TEXT("Property '%s' is not a struct property."), *PropertyValue.Name);
            return false;
        }
        if (!JsonValueToImportTextForProperty(PropertyValue.Value, Property, ImportText, OutError))
        {
            return false;
        }
    }
    else if (Type == TEXT("set"))
    {
        if (!CastField<FSetProperty>(Property))
        {
            OutError = FString::Printf(TEXT("Property '%s' is not a set property."), *PropertyValue.Name);
            return false;
        }
        if (!JsonValueToImportTextForProperty(PropertyValue.Value, Property, ImportText, OutError))
        {
            return false;
        }
    }
    else if (Type == TEXT("map"))
    {
        if (!CastField<FMapProperty>(Property))
        {
            OutError = FString::Printf(TEXT("Property '%s' is not a map property."), *PropertyValue.Name);
            return false;
        }
        if (!JsonValueToImportTextForProperty(PropertyValue.Value, Property, ImportText, OutError))
        {
            return false;
        }
    }
    else
    {
        ImportText = JsonValueToImportText(PropertyValue);
    }

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

bool FPropertyAssignmentService::JsonValueToImportTextForProperty(const TSharedPtr<FJsonValue>& Value, FProperty* Property, FString& OutImportText, FString& OutError) const
{
    if (!Value.IsValid() || !Property)
    {
        OutError = TEXT("JSON value or target property is invalid.");
        return false;
    }

    if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        if (Value->Type != EJson::Array)
        {
            OutError = FString::Printf(TEXT("Array property '%s' requires an array value."), *Property->GetName());
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
        return JsonArrayToArrayImportText(Array, ArrayProperty, OutImportText, OutError);
    }

    if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        if (Value->Type != EJson::Array)
        {
            OutError = FString::Printf(TEXT("Set property '%s' requires an array value."), *Property->GetName());
            return false;
        }
        const TArray<TSharedPtr<FJsonValue>>& Array = Value->AsArray();
        return JsonArrayToSetImportText(Array, SetProperty, OutImportText, OutError);
    }

    if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        if (Value->Type == EJson::Object)
        {
            return JsonObjectToMapImportText(Value->AsObject(), MapProperty, OutImportText, OutError);
        }
        if (Value->Type == EJson::Array)
        {
            return JsonMapArrayToMapImportText(Value->AsArray(), MapProperty, OutImportText, OutError);
        }
        OutError = FString::Printf(TEXT("Map property '%s' requires an object or key/value array."), *Property->GetName());
        return false;
    }

    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        const TSharedPtr<FJsonObject> Object = Value->AsObject();
        if (!Object.IsValid())
        {
            OutError = FString::Printf(TEXT("Struct property '%s' requires an object value."), *Property->GetName());
            return false;
        }
        return JsonObjectToStructImportText(Object, StructProperty, OutImportText, OutError);
    }

    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        if (Value->Type == EJson::Number)
        {
            OutImportText = FString::Printf(TEXT("%lld"), static_cast<int64>(Value->AsNumber()));
            return true;
        }
        if (Value->Type == EJson::Object)
        {
            FString Name;
            if (Value->AsObject()->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
            {
                OutImportText = Name;
                return true;
            }
        }
        OutImportText = Value->AsString();
        return !OutImportText.IsEmpty() || EnumProperty->GetEnum() == nullptr;
    }

    if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
    {
        if (ByteProperty->Enum)
        {
            if (Value->Type == EJson::Object)
            {
                FString Name;
                if (Value->AsObject()->TryGetStringField(TEXT("name"), Name) && !Name.IsEmpty())
                {
                    OutImportText = Name;
                    return true;
                }
            }
            OutImportText = Value->AsString();
            return !OutImportText.IsEmpty();
        }
    }

    if (CastField<FClassProperty>(Property) || CastField<FObjectPropertyBase>(Property))
    {
        if (Value->Type == EJson::Null)
        {
            OutImportText = TEXT("None");
            return true;
        }
        const FString ObjectPath = NormalizeObjectPath(Value->AsString());
        OutImportText = ObjectPath.IsEmpty() ? TEXT("None") : ObjectPath;
        return true;
    }

    if (CastField<FSoftObjectProperty>(Property) || CastField<FSoftClassProperty>(Property))
    {
        if (Value->Type == EJson::Null)
        {
            OutImportText = TEXT("None");
            return true;
        }
        OutImportText = EscapeImportString(NormalizeObjectPath(Value->AsString()));
        return true;
    }

    if (Property->GetClass() && Property->GetClass()->GetName().Contains(TEXT("FieldPath")))
    {
        if (Value->Type == EJson::Null)
        {
            OutImportText = TEXT("None");
            return true;
        }
        const FString FieldPath = Value->AsString();
        OutImportText = FieldPath.IsEmpty() ? TEXT("None") : FieldPath;
        return true;
    }

    if (CastField<FNameProperty>(Property))
    {
        OutImportText = Value->AsString();
        if (OutImportText.IsEmpty())
        {
            OutImportText = TEXT("None");
        }
        return Value->Type == EJson::String;
    }

    if (CastField<FStrProperty>(Property))
    {
        OutImportText = EscapeImportString(Value->AsString());
        return true;
    }

    if (CastField<FTextProperty>(Property))
    {
        OutImportText = EscapeImportString(Value->AsString());
        return true;
    }

    if (CastField<FBoolProperty>(Property))
    {
        OutImportText = Value->AsBool() ? TEXT("True") : TEXT("False");
        return true;
    }

    if (CastField<FNumericProperty>(Property))
    {
        OutImportText = FString::SanitizeFloat(Value->AsNumber());
        return true;
    }

    OutError = FString::Printf(TEXT("Property '%s' does not support array/struct JSON conversion."), *Property->GetName());
    return false;
}

bool FPropertyAssignmentService::JsonObjectToStructImportText(const TSharedPtr<FJsonObject>& Object, FStructProperty* Property, FString& OutImportText, FString& OutError) const
{
    if (!Object.IsValid())
    {
        OutError = TEXT("Struct value must be a JSON object.");
        return false;
    }
    if (!Property || !Property->Struct)
    {
        OutError = TEXT("Struct property metadata is invalid.");
        return false;
    }
    bool bTruncated = false;
    if (Object->TryGetBoolField(TEXT("truncated"), bTruncated) && bTruncated)
    {
        OutError = FString::Printf(TEXT("Struct property '%s' was exported as truncated and cannot be safely imported."), *Property->GetName());
        return false;
    }
    bool bSpecialHandled = false;
    if (!TryBuildSpecialStructImportText(Object, Property, OutImportText, OutError, bSpecialHandled))
    {
        return false;
    }
    if (bSpecialHandled)
    {
        return true;
    }

    TArray<FString> Fields;
    for (TFieldIterator<FProperty> It(Property->Struct); It; ++It)
    {
        FProperty* InnerProperty = *It;
        if (!InnerProperty)
        {
            continue;
        }

        const TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(InnerProperty->GetName());
        if (!FieldValue || !FieldValue->IsValid())
        {
            continue;
        }

        FString ValueText;
        if (!JsonValueToImportTextForProperty(*FieldValue, InnerProperty, ValueText, OutError))
        {
            OutError = FString::Printf(TEXT("Struct field '%s' is invalid: %s"), *InnerProperty->GetName(), *OutError);
            return false;
        }

        Fields.Add(FString::Printf(TEXT("%s=%s"), *InnerProperty->GetName(), *ValueText));
    }

    OutImportText = FString::Printf(TEXT("(%s)"), *FString::Join(Fields, TEXT(",")));
    return true;
}

bool FPropertyAssignmentService::TryBuildSpecialStructImportText(
    const TSharedPtr<FJsonObject>& Object,
    FStructProperty* Property,
    FString& OutImportText,
    FString& OutError,
    bool& bOutHandled) const
{
    bOutHandled = false;
    if (!Object.IsValid() || !Property || !Property->Struct)
    {
        return true;
    }

    const FString StructName = Property->Struct->GetName();

    if (StructName == TEXT("GameplayTag"))
    {
        bOutHandled = true;
        TSharedPtr<FJsonValue> TagValue = MakeShared<FJsonValueObject>(Object);
        return BuildGameplayTagImportTextFromValue(TagValue, OutImportText, OutError);
    }

    if (StructName == TEXT("GameplayTagContainer"))
    {
        bOutHandled = true;
        const TArray<TSharedPtr<FJsonValue>>* Tags = nullptr;
        if (!Object->TryGetArrayField(TEXT("GameplayTags"), Tags))
        {
            OutImportText = TEXT("(GameplayTags=())");
            return true;
        }

        TArray<FString> TagTexts;
        for (int32 Index = 0; Index < Tags->Num(); ++Index)
        {
            FString TagText;
            if (!BuildGameplayTagImportTextFromValue((*Tags)[Index], TagText, OutError))
            {
                OutError = FString::Printf(TEXT("GameplayTagContainer tag %d is invalid: %s"), Index, *OutError);
                return false;
            }
            TagTexts.Add(TagText);
        }

        OutImportText = FString::Printf(TEXT("(GameplayTags=(%s))"), *FString::Join(TagTexts, TEXT(",")));
        return true;
    }

    if (StructName == TEXT("GameplayAttribute"))
    {
        bOutHandled = true;
        const FString AttributeName = JsonStringOrNone(Object, TEXT("AttributeName"));
        const FString Attribute = NormalizeImportPath(JsonStringOrNone(Object, TEXT("Attribute")));
        const FString AttributeOwner = NormalizeImportPath(JsonStringOrNone(Object, TEXT("AttributeOwner")));
        OutImportText = FString::Printf(TEXT("(AttributeName=\"%s\",Attribute=%s,AttributeOwner=%s)"),
            *AttributeName,
            *Attribute,
            *AttributeOwner);
        return true;
    }

    if (StructName == TEXT("DataTableRowHandle"))
    {
        bOutHandled = true;
        const FString DataTable = NormalizeImportPath(JsonStringOrNone(Object, TEXT("DataTable")));
        const FString RowName = JsonStringOrNone(Object, TEXT("RowName"));
        OutImportText = FString::Printf(TEXT("(DataTable=%s,RowName=\"%s\")"), *DataTable, *RowName);
        return true;
    }

    if (StructName == TEXT("ScalableFloat"))
    {
        bOutHandled = true;
        const FString Value = JsonNumberOrDefault(Object, TEXT("Value"), TEXT("0"));
        const FString bEnableMobile = JsonBoolOrDefault(Object, TEXT("bEnableMobile"), false);
        const FString MobileValue = JsonNumberOrDefault(Object, TEXT("MobileValue"), TEXT("0"));

        FString CurveText = TEXT("(DataTable=None,RowName=\"None\")");
        const TSharedPtr<FJsonObject>* CurveObject = nullptr;
        if (Object->TryGetObjectField(TEXT("Curve"), CurveObject) && CurveObject && CurveObject->IsValid())
        {
            FStructProperty* CurveProperty = CastField<FStructProperty>(Property->Struct->FindPropertyByName(TEXT("Curve")));
            if (CurveProperty)
            {
                if (!JsonObjectToStructImportText(*CurveObject, CurveProperty, CurveText, OutError))
                {
                    OutError = FString::Printf(TEXT("ScalableFloat.Curve is invalid: %s"), *OutError);
                    return false;
                }
            }
        }

        OutImportText = FString::Printf(TEXT("(Value=%s,bEnableMobile=%s,MobileValue=%s,Curve=%s)"),
            *Value,
            *bEnableMobile,
            *MobileValue,
            *CurveText);
        return true;
    }

    return true;
}

bool FPropertyAssignmentService::JsonArrayToArrayImportText(const TArray<TSharedPtr<FJsonValue>>& Array, FArrayProperty* Property, FString& OutImportText, FString& OutError) const
{
    if (!Property || !Property->Inner)
    {
        OutError = TEXT("Array property has no inner property.");
        return false;
    }

    TArray<FString> Elements;
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        FString ElementText;
        if (!JsonValueToImportTextForProperty(Array[Index], Property->Inner, ElementText, OutError))
        {
            OutError = FString::Printf(TEXT("Array element %d is invalid: %s"), Index, *OutError);
            return false;
        }
        Elements.Add(ElementText);
    }

    OutImportText = FString::Printf(TEXT("(%s)"), *FString::Join(Elements, TEXT(",")));
    return true;
}

bool FPropertyAssignmentService::JsonArrayToSetImportText(const TArray<TSharedPtr<FJsonValue>>& Array, FSetProperty* Property, FString& OutImportText, FString& OutError) const
{
    if (!Property || !Property->ElementProp)
    {
        OutError = TEXT("Set property has no element property.");
        return false;
    }

    TArray<FString> Elements;
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        FString ElementText;
        if (!JsonValueToImportTextForProperty(Array[Index], Property->ElementProp, ElementText, OutError))
        {
            OutError = FString::Printf(TEXT("Set element %d is invalid: %s"), Index, *OutError);
            return false;
        }
        Elements.Add(ElementText);
    }

    OutImportText = FString::Printf(TEXT("(%s)"), *FString::Join(Elements, TEXT(",")));
    return true;
}

bool FPropertyAssignmentService::JsonObjectToMapImportText(const TSharedPtr<FJsonObject>& Object, FMapProperty* Property, FString& OutImportText, FString& OutError) const
{
    if (!Object.IsValid())
    {
        OutError = TEXT("Map value must be a JSON object.");
        return false;
    }

    TArray<FString> Pairs;
    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
    {
        const TSharedPtr<FJsonValue> KeyValue = MakeShared<FJsonValueString>(Pair.Key);
        FString KeyText;
        FString ValueText;
        if (!JsonValueToImportTextForProperty(KeyValue, Property->KeyProp, KeyText, OutError))
        {
            OutError = FString::Printf(TEXT("Map key '%s' is invalid: %s"), *Pair.Key, *OutError);
            return false;
        }
        if (!JsonValueToImportTextForProperty(Pair.Value, Property->ValueProp, ValueText, OutError))
        {
            OutError = FString::Printf(TEXT("Map value for key '%s' is invalid: %s"), *Pair.Key, *OutError);
            return false;
        }
        Pairs.Add(FString::Printf(TEXT("(Key=%s,Value=%s)"), *KeyText, *ValueText));
    }

    OutImportText = FString::Printf(TEXT("(%s)"), *FString::Join(Pairs, TEXT(",")));
    return true;
}

bool FPropertyAssignmentService::JsonMapArrayToMapImportText(const TArray<TSharedPtr<FJsonValue>>& Array, FMapProperty* Property, FString& OutImportText, FString& OutError) const
{
    TArray<FString> Pairs;
    for (int32 Index = 0; Index < Array.Num(); ++Index)
    {
        const TSharedPtr<FJsonObject> PairObject = Array[Index]->AsObject();
        if (!PairObject.IsValid())
        {
            OutError = FString::Printf(TEXT("Map pair %d must be an object."), Index);
            return false;
        }

        const TSharedPtr<FJsonValue>* KeyValue = PairObject->Values.Find(TEXT("key"));
        const TSharedPtr<FJsonValue>* ValueValue = PairObject->Values.Find(TEXT("value"));
        if (!KeyValue || !ValueValue)
        {
            OutError = FString::Printf(TEXT("Map pair %d requires key and value fields."), Index);
            return false;
        }

        FString KeyText;
        FString ValueText;
        if (!JsonValueToImportTextForProperty(*KeyValue, Property->KeyProp, KeyText, OutError))
        {
            OutError = FString::Printf(TEXT("Map pair %d key is invalid: %s"), Index, *OutError);
            return false;
        }
        if (!JsonValueToImportTextForProperty(*ValueValue, Property->ValueProp, ValueText, OutError))
        {
            OutError = FString::Printf(TEXT("Map pair %d value is invalid: %s"), Index, *OutError);
            return false;
        }
        Pairs.Add(FString::Printf(TEXT("(Key=%s,Value=%s)"), *KeyText, *ValueText));
    }

    OutImportText = FString::Printf(TEXT("(%s)"), *FString::Join(Pairs, TEXT(",")));
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

FString FPropertyAssignmentService::EscapeImportString(const FString& Value) const
{
    FString Escaped = Value;
    Escaped.ReplaceInline(TEXT("\\"), TEXT("\\\\"));
    Escaped.ReplaceInline(TEXT("\""), TEXT("\\\""));
    return FString::Printf(TEXT("\"%s\""), *Escaped);
}
