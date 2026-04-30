#include "Domain/PropertyAssignmentService.h"

#include "Components/PrimitiveComponent.h"
#include "Components/SkeletalMeshComponent.h"
#include "Components/StaticMeshComponent.h"
#include "Core/AutomationWhitelist.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Engine/SkeletalMesh.h"
#include "Engine/StaticMesh.h"
#include "GameplayTagContainer.h"
#include "UObject/UnrealType.h"

namespace
{
    FString DescribePropertyForAssignment(FProperty* Property)
    {
        if (!Property)
        {
            return TEXT("<null property>");
        }

        FString Description = FString::Printf(TEXT("%s '%s'"), *Property->GetClass()->GetName(), *Property->GetName());
        if (const FStructProperty* StructProperty = CastField<FStructProperty>(Property))
        {
            Description += FString::Printf(
                TEXT(" struct='%s' path='%s'"),
                StructProperty->Struct ? *StructProperty->Struct->GetName() : TEXT("<null>"),
                StructProperty->Struct ? *StructProperty->Struct->GetPathName() : TEXT("<null>"));
        }
        return Description;
    }

    bool IsStructNamed(const FStructProperty* StructProperty, const TCHAR* ExpectedName)
    {
        if (!StructProperty || !StructProperty->Struct || !ExpectedName)
        {
            return false;
        }

        const FName StructName = StructProperty->Struct->GetFName();
        if (StructName == FName(ExpectedName))
        {
            return true;
        }

        const FString StructPath = StructProperty->Struct->GetPathName();
        return StructPath.EndsWith(FString::Printf(TEXT(".%s"), ExpectedName));
    }

    bool IsStructPropertyNamed(FProperty* Property, const TCHAR* ExpectedName)
    {
        return IsStructNamed(CastField<FStructProperty>(Property), ExpectedName);
    }

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

    bool TryReadEnumInt(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, int64& OutValue)
    {
        if (!Object.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonObject>* EnumObject = nullptr;
        if (Object->TryGetObjectField(FieldName, EnumObject) && EnumObject && EnumObject->IsValid())
        {
            double Raw = 0.0;
            if ((*EnumObject)->TryGetNumberField(TEXT("raw"), Raw))
            {
                OutValue = static_cast<int64>(Raw);
                return true;
            }
        }

        double Number = 0.0;
        if (Object->TryGetNumberField(FieldName, Number))
        {
            OutValue = static_cast<int64>(Number);
            return true;
        }

        return false;
    }

    bool TryReadEnumName(const TSharedPtr<FJsonObject>& Object, const TCHAR* FieldName, FString& OutName)
    {
        if (!Object.IsValid())
        {
            return false;
        }

        const TSharedPtr<FJsonObject>* EnumObject = nullptr;
        if (Object->TryGetObjectField(FieldName, EnumObject) && EnumObject && EnumObject->IsValid())
        {
            if ((*EnumObject)->TryGetStringField(TEXT("name"), OutName) && !OutName.IsEmpty())
            {
                return true;
            }
        }

        return Object->TryGetStringField(FieldName, OutName) && !OutName.IsEmpty();
    }

    bool TryResolveEnumValue(UEnum* Enum, const FString& Name, int64& OutValue)
    {
        if (!Enum || Name.IsEmpty())
        {
            return false;
        }

        const int64 Direct = Enum->GetValueByNameString(Name);
        if (Direct != INDEX_NONE)
        {
            OutValue = Direct;
            return true;
        }

        const FString EnumName = Enum->GetName();
        const int64 Qualified = Enum->GetValueByNameString(EnumName + TEXT("::") + Name);
        if (Qualified != INDEX_NONE)
        {
            OutValue = Qualified;
            return true;
        }

        return false;
    }

    bool TryApplyBodyInstanceJson(UPrimitiveComponent* PrimitiveComponent, const TSharedPtr<FJsonObject>& Object, FString& OutError)
    {
        if (!PrimitiveComponent || !Object.IsValid())
        {
            return false;
        }

        int64 Raw = 0;
        FString EnumName;
        if (TryReadEnumInt(Object, TEXT("ObjectType"), Raw))
        {
            PrimitiveComponent->SetCollisionObjectType(static_cast<ECollisionChannel>(Raw));
        }
        else if (TryReadEnumName(Object, TEXT("ObjectType"), EnumName))
        {
            int64 EnumValue = 0;
            if (TryResolveEnumValue(StaticEnum<ECollisionChannel>(), EnumName, EnumValue))
            {
                PrimitiveComponent->SetCollisionObjectType(static_cast<ECollisionChannel>(EnumValue));
            }
        }

        if (TryReadEnumInt(Object, TEXT("CollisionEnabled"), Raw))
        {
            PrimitiveComponent->SetCollisionEnabled(static_cast<ECollisionEnabled::Type>(Raw));
        }
        else if (TryReadEnumName(Object, TEXT("CollisionEnabled"), EnumName))
        {
            int64 EnumValue = 0;
            if (TryResolveEnumValue(StaticEnum<ECollisionEnabled::Type>(), EnumName, EnumValue))
            {
                PrimitiveComponent->SetCollisionEnabled(static_cast<ECollisionEnabled::Type>(EnumValue));
            }
        }

        const TSharedPtr<FJsonObject>* CollisionResponses = nullptr;
        if (Object->TryGetObjectField(TEXT("CollisionResponses"), CollisionResponses) && CollisionResponses && CollisionResponses->IsValid())
        {
            const TArray<TSharedPtr<FJsonValue>>* ResponseArray = nullptr;
            if ((*CollisionResponses)->TryGetArrayField(TEXT("ResponseArray"), ResponseArray))
            {
                for (const TSharedPtr<FJsonValue>& EntryValue : *ResponseArray)
                {
                    const TSharedPtr<FJsonObject> Entry = EntryValue.IsValid() ? EntryValue->AsObject() : nullptr;
                    if (!Entry.IsValid())
                    {
                        continue;
                    }

                    FString ChannelName;
                    if (!Entry->TryGetStringField(TEXT("Channel"), ChannelName) || ChannelName.IsEmpty())
                    {
                        continue;
                    }

                    ECollisionChannel Channel = ECC_MAX;
                    int64 ChannelValue = 0;
                    if (TryResolveEnumValue(StaticEnum<ECollisionChannel>(), ChannelName, ChannelValue))
                    {
                        Channel = static_cast<ECollisionChannel>(ChannelValue);
                    }
                    if (Channel == ECC_MAX)
                    {
                        continue;
                    }

                    ECollisionResponse Response = ECR_Ignore;
                    if (TryReadEnumInt(Entry, TEXT("Response"), Raw))
                    {
                        Response = static_cast<ECollisionResponse>(Raw);
                    }
                    else if (TryReadEnumName(Entry, TEXT("Response"), EnumName))
                    {
                        int64 ResponseValue = 0;
                        if (TryResolveEnumValue(StaticEnum<ECollisionResponse>(), EnumName, ResponseValue))
                        {
                            Response = static_cast<ECollisionResponse>(ResponseValue);
                        }
                    }
                    PrimitiveComponent->SetCollisionResponseToChannel(Channel, Response);
                }
            }
        }

        FString CollisionProfileName;
        if (Object->TryGetStringField(TEXT("CollisionProfileName"), CollisionProfileName) && !CollisionProfileName.IsEmpty())
        {
            PrimitiveComponent->SetCollisionProfileName(FName(*CollisionProfileName));
        }

        PrimitiveComponent->Modify();
        return true;
    }

    bool TryNormalizeIntegerImportText(FProperty* Property, FString& InOutImportText)
    {
        FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property);
        if (!NumericProperty || NumericProperty->IsFloatingPoint())
        {
            return false;
        }

        FString Trimmed = InOutImportText;
        Trimmed.TrimStartAndEndInline();
        if (Trimmed.IsEmpty())
        {
            return false;
        }

        double Number = 0.0;
        if (!LexTryParseString(Number, *Trimmed))
        {
            return false;
        }

        const double Rounded = FMath::RoundToDouble(Number);
        if (FMath::Abs(Number - Rounded) > static_cast<double>(KINDA_SMALL_NUMBER))
        {
            return false;
        }

        InOutImportText = FString::Printf(TEXT("%lld"), static_cast<int64>(Rounded));
        return true;
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

    bool ExtractGameplayTagNamesFromImportText(const FString& ImportText, TArray<FName>& OutTagNames, FString& OutError)
    {
        OutTagNames.Reset();

        FString Remaining = ImportText;
        int32 SearchStart = 0;
        while (true)
        {
            int32 TagNameIndex = INDEX_NONE;
            if (!Remaining.FindChar(TEXT('T'), TagNameIndex))
            {
                break;
            }

            const int32 FullIndex = SearchStart + TagNameIndex;
            const FString Tail = ImportText.Mid(FullIndex);
            if (!Tail.StartsWith(TEXT("TagName=\"")))
            {
                SearchStart = FullIndex + 1;
                Remaining = ImportText.Mid(SearchStart);
                continue;
            }

            const int32 ValueStart = FullIndex + 9;
            int32 ValueEnd = INDEX_NONE;
            if (!ImportText.Mid(ValueStart).FindChar(TEXT('"'), ValueEnd))
            {
                OutError = FString::Printf(TEXT("Malformed GameplayTagContainer import text '%s'."), *ImportText);
                return false;
            }

            const FString TagName = ImportText.Mid(ValueStart, ValueEnd);
            if (!TagName.IsEmpty() && TagName != TEXT("None"))
            {
                OutTagNames.Add(FName(*TagName));
            }

            SearchStart = ValueStart + ValueEnd + 1;
            Remaining = ImportText.Mid(SearchStart);
        }

        return true;
    }

    bool TryAssignGameplayTagImportText(FProperty* Property, void* ValuePtr, const FString& ImportText, FString& OutError, bool& bOutHandled)
    {
        bOutHandled = false;

        FStructProperty* StructProperty = CastField<FStructProperty>(Property);
        if (!StructProperty)
        {
            return true;
        }

        TArray<FName> TagNames;

        if (IsStructNamed(StructProperty, TEXT("GameplayTag")))
        {
            bOutHandled = true;
            if (!ExtractGameplayTagNamesFromImportText(ImportText, TagNames, OutError))
            {
                return false;
            }

            FGameplayTag* TagValue = static_cast<FGameplayTag*>(ValuePtr);
            if (!TagValue)
            {
                OutError = TEXT("GameplayTag target memory is invalid.");
                return false;
            }

            if (TagNames.Num() == 0)
            {
                *TagValue = FGameplayTag();
                return true;
            }
            if (TagNames.Num() > 1)
            {
                OutError = FString::Printf(TEXT("GameplayTag property cannot accept multiple tags from '%s'."), *ImportText);
                return false;
            }

            const FGameplayTag RequestedTag = FGameplayTag::RequestGameplayTag(TagNames[0], /*ErrorIfNotFound*/false);
            if (!RequestedTag.IsValid())
            {
                OutError = FString::Printf(TEXT("Gameplay tag '%s' is not registered."), *TagNames[0].ToString());
                return false;
            }
            *TagValue = RequestedTag;
            return true;
        }

        if (!IsStructNamed(StructProperty, TEXT("GameplayTagContainer")))
        {
            return true;
        }

        bOutHandled = true;
        if (!ExtractGameplayTagNamesFromImportText(ImportText, TagNames, OutError))
        {
            return false;
        }

        FGameplayTagContainer* Container = static_cast<FGameplayTagContainer*>(ValuePtr);
        if (!Container)
        {
            OutError = TEXT("GameplayTagContainer target memory is invalid.");
            return false;
        }

        Container->Reset();
        for (const FName& TagName : TagNames)
        {
            const FGameplayTag Tag = FGameplayTag::RequestGameplayTag(TagName, /*ErrorIfNotFound*/false);
            if (!Tag.IsValid())
            {
                OutError = FString::Printf(TEXT("Gameplay tag '%s' is not registered."), *TagName.ToString());
                return false;
            }
            Container->AddTag(Tag);
        }

        return true;
    }

    bool HasOnlyTrailingWhitespace(const TCHAR* Text)
    {
        if (!Text)
        {
            return false;
        }
        while (*Text != TEXT('\0'))
        {
            if (!FChar::IsWhitespace(*Text))
            {
                return false;
            }
            ++Text;
        }
        return true;
    }
}

bool FPropertyAssignmentService::AssignProperties(UObject* Target, const TArray<FAutomationPropertyValue>& Properties, FAutomationTaskResult& OutResult, const FString& FieldPrefix, const TArray<FAutomationAssetRedirect>& Redirects) const
{
    bool bOk = true;
    for (int32 Index = 0; Index < Properties.Num(); ++Index)
    {
        const FString Field = FString::Printf(TEXT("%s[%d]"), *FieldPrefix, Index);
        bOk &= AssignProperty(Target, Properties[Index], OutResult, Field, Redirects);
    }
    return bOk;
}

bool FPropertyAssignmentService::AssignProperty(UObject* Target, const FAutomationPropertyValue& PropertyValue, FAutomationTaskResult& OutResult, const FString& FieldPrefix, const TArray<FAutomationAssetRedirect>& Redirects) const
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

    FProperty* Property = Target->GetClass()->FindPropertyByName(FName(*PropertyValue.Name));
    if (!Property)
    {
        OutResult.AddError(TEXT("PropertyNotFound"), FString::Printf(TEXT("Property '%s' not found on '%s'."), *PropertyValue.Name, *Target->GetClass()->GetName()), FieldPrefix + TEXT(".name"));
        return false;
    }
    if (CastField<FDelegateProperty>(Property)
        || CastField<FMulticastDelegateProperty>(Property)
        || CastField<FMulticastInlineDelegateProperty>(Property)
        || CastField<FMulticastSparseDelegateProperty>(Property))
    {
        OutResult.AddWarning(FString::Printf(TEXT("Skipped delegate property '%s'; delegate bindings are not safe to reconstruct from exported meta."), *PropertyValue.Name));
        return true;
    }

    auto MarkAssignedPropertyDirty = [Target]()
    {
        Target->MarkPackageDirty();
    };

    bool bHandled = false;
    Target->Modify();
    if (!TryAssignSpecialProperty(Target, PropertyValue, OutResult, FieldPrefix, bHandled))
    {
        return false;
    }
    if (bHandled)
    {
        MarkAssignedPropertyDirty();
        OutResult.Metrics.PropertyAssignCount++;
        return true;
    }

    FString Error;
    bool bReflectedHandled = false;
    if (!TryAssignSpecialReflectedProperty(Target, Property, PropertyValue, Redirects, Error, bReflectedHandled))
    {
        OutResult.AddError(TEXT("InvalidPropertyValue"), Error, FieldPrefix + TEXT(".value"));
        return false;
    }
    if (bReflectedHandled)
    {
        MarkAssignedPropertyDirty();
        OutResult.Metrics.PropertyAssignCount++;
        return true;
    }

    if (!ImportTextValue(Target, Property, PropertyValue, Redirects, Error))
    {
        OutResult.AddError(TEXT("InvalidPropertyValue"), Error, FieldPrefix + TEXT(".value"));
        return false;
    }

    MarkAssignedPropertyDirty();
    OutResult.Metrics.PropertyAssignCount++;
    return true;
}

bool FPropertyAssignmentService::ExportAssignedPropertyText(UObject* Target, const FString& PropertyName, FString& OutText, FString& OutError) const
{
    OutText.Reset();
    OutError.Reset();

    if (!Target)
    {
        OutError = TEXT("Target object is null.");
        return false;
    }

    FProperty* Property = Target->GetClass()->FindPropertyByName(FName(*PropertyName));
    if (!Property)
    {
        OutError = FString::Printf(TEXT("Property '%s' not found on '%s'."), *PropertyName, *Target->GetClass()->GetName());
        return false;
    }

    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
    Property->ExportTextItem(OutText, ValuePtr, nullptr, Target, PPF_None);
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

bool FPropertyAssignmentService::TryAssignSpecialReflectedProperty(UObject* Target, FProperty* Property, const FAutomationPropertyValue& PropertyValue, const TArray<FAutomationAssetRedirect>& Redirects, FString& OutError, bool& bOutHandled) const
{
    bOutHandled = false;
    if (!Target || !Property)
    {
        return true;
    }

    const bool bJsonStructValue = PropertyValue.Value.IsValid() && PropertyValue.Value->Type == EJson::Object;
    const bool bDeclaredStructValue = PropertyValue.Type.Equals(TEXT("struct"), ESearchCase::IgnoreCase);
    if (!bJsonStructValue && !bDeclaredStructValue)
    {
        return true;
    }

    const TSharedPtr<FJsonObject> Object = bJsonStructValue ? PropertyValue.Value->AsObject() : nullptr;
    if (!Object.IsValid())
    {
        return true;
    }

    if (PropertyValue.Name == TEXT("BodyInstance"))
    {
        UPrimitiveComponent* PrimitiveComponent = Cast<UPrimitiveComponent>(Target);
        if (!PrimitiveComponent)
        {
            return true;
        }
        bOutHandled = true;
        return TryApplyBodyInstanceJson(PrimitiveComponent, Object, OutError);
    }

    FStructProperty* StructProperty = CastField<FStructProperty>(Property);
    if (!StructProperty || !StructProperty->Struct)
    {
        return true;
    }

    void* StructAddress = StructProperty->ContainerPtrToValuePtr<void>(Target);
    bOutHandled = true;
    return AssignStructFieldsFromJson(StructAddress, StructProperty->Struct, Object, Redirects, OutError);
}

bool FPropertyAssignmentService::AssignStructFieldsFromJson(void* StructAddress, UScriptStruct* Struct, const TSharedPtr<FJsonObject>& Object, const TArray<FAutomationAssetRedirect>& Redirects, FString& OutError) const
{
    if (!StructAddress || !Struct || !Object.IsValid())
    {
        OutError = TEXT("Struct assignment target is invalid.");
        return false;
    }

    for (const TPair<FString, TSharedPtr<FJsonValue>>& Pair : Object->Values)
    {
        FProperty* InnerProperty = Struct->FindPropertyByName(FName(*Pair.Key));
        if (!InnerProperty)
        {
            continue;
        }

        void* InnerAddress = InnerProperty->ContainerPtrToValuePtr<void>(StructAddress);
        if (FStructProperty* InnerStructProperty = CastField<FStructProperty>(InnerProperty))
        {
            const TSharedPtr<FJsonObject> InnerObject = Pair.Value.IsValid() ? Pair.Value->AsObject() : nullptr;
            if (InnerObject.IsValid())
            {
                if (!AssignStructFieldsFromJson(InnerAddress, InnerStructProperty->Struct, InnerObject, Redirects, OutError))
                {
                    OutError = FString::Printf(TEXT("%s.%s"), *Pair.Key, *OutError);
                    return false;
                }
                continue;
            }
        }

        FString ImportText;
        if (!JsonValueToImportTextForProperty(Pair.Value, InnerProperty, ImportText, OutError))
        {
            OutError = FString::Printf(TEXT("Struct field '%s' is invalid: %s"), *Pair.Key, *OutError);
            return false;
        }
        ImportText = RewriteImportTextRedirects(ImportText, Redirects);

        const TCHAR* Result = InnerProperty->ImportText(*ImportText, InnerAddress, PPF_None, nullptr);
        if (!Result || !HasOnlyTrailingWhitespace(Result))
        {
            OutError = FString::Printf(TEXT("Failed to import '%s' into struct field '%s'."), *ImportText, *Pair.Key);
            return false;
        }
    }

    return true;
}

bool FPropertyAssignmentService::BuildExpectedImportText(FProperty* Property, const FAutomationPropertyValue& PropertyValue, const TArray<FAutomationAssetRedirect>& Redirects, FString& OutImportText, bool& bOutRawImportText, FString& OutError) const
{
    OutImportText.Reset();
    bOutRawImportText = false;

    if (!Property || !PropertyValue.Value.IsValid())
    {
        OutError = FString::Printf(TEXT("Property '%s' has no value."), *PropertyValue.Name);
        return false;
    }

    const FString Type = PropertyValue.Type.ToLower();
    bOutRawImportText = Type == TEXT("import_text") || Type == TEXT("raw_import_text");
    if (bOutRawImportText)
    {
        OutImportText = RewriteImportTextRedirects(JsonValueToString(PropertyValue), Redirects);
        TryNormalizeIntegerImportText(Property, OutImportText);
    }
    else
    {
        if (!JsonValueToImportTextForProperty(PropertyValue.Value, Property, OutImportText, OutError))
        {
            return false;
        }
        OutImportText = RewriteImportTextRedirects(OutImportText, Redirects);
    }

    if (OutImportText.IsEmpty() && PropertyValue.Type != TEXT("string") && PropertyValue.Type != TEXT("text"))
    {
        OutError = FString::Printf(TEXT("Property '%s' value cannot be converted."), *PropertyValue.Name);
        return false;
    }

    return true;
}

bool FPropertyAssignmentService::BuildExpectedAssignedPropertyText(FProperty* Property, const FAutomationPropertyValue& PropertyValue, const TArray<FAutomationAssetRedirect>& Redirects, UObject* OwnerForPortFlags, FString& OutExpectedText, FString& OutError) const
{
    OutExpectedText.Reset();
    FString ImportText;
    bool bRawImportText = false;
    if (!BuildExpectedImportText(Property, PropertyValue, Redirects, ImportText, bRawImportText, OutError))
    {
        return false;
    }

    uint8* Scratch = static_cast<uint8*>(FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment()));
    Property->InitializeValue(Scratch);

    bool bSpecialImportHandled = false;
    if (!TryAssignGameplayTagImportText(Property, Scratch, ImportText, OutError, bSpecialImportHandled))
    {
        Property->DestroyValue(Scratch);
        FMemory::Free(Scratch);
        return false;
    }

    if (!bSpecialImportHandled)
    {
        const TCHAR* ImportResult = Property->ImportText(*ImportText, Scratch, PPF_None, OwnerForPortFlags);
        if (!ImportResult || !HasOnlyTrailingWhitespace(ImportResult))
        {
            Property->DestroyValue(Scratch);
            FMemory::Free(Scratch);
            OutError = FString::Printf(TEXT("Expected value '%s' cannot be imported for property '%s'."), *ImportText, *PropertyValue.Name);
            return false;
        }
    }

    Property->ExportTextItem(OutExpectedText, Scratch, nullptr, OwnerForPortFlags, PPF_None);
    Property->DestroyValue(Scratch);
    FMemory::Free(Scratch);
    return true;
}

bool FPropertyAssignmentService::ImportTextValue(UObject* Target, FProperty* Property, const FAutomationPropertyValue& PropertyValue, const TArray<FAutomationAssetRedirect>& Redirects, FString& OutError) const
{
    FString ImportText;
    bool bRawImportText = false;
    if (!BuildExpectedImportText(Property, PropertyValue, Redirects, ImportText, bRawImportText, OutError))
    {
        return false;
    }
    (void)bRawImportText;

    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
    bool bSpecialImportHandled = false;
    if (!TryAssignGameplayTagImportText(Property, ValuePtr, ImportText, OutError, bSpecialImportHandled))
    {
        return false;
    }

    if (!bSpecialImportHandled)
    {
        const TCHAR* Result = Property->ImportText(*ImportText, ValuePtr, PPF_None, Target);
        if (!Result)
        {
            OutError = FString::Printf(
                TEXT("Failed to import '%s' into property '%s' (%s)."),
                *ImportText,
                *PropertyValue.Name,
                *DescribePropertyForAssignment(Property));
            return false;
        }
        if (!HasOnlyTrailingWhitespace(Result))
        {
            OutError = FString::Printf(TEXT("Property '%s' only partially imported '%s'. Remaining text starts with '%s'."),
                *PropertyValue.Name, *ImportText, Result);
            return false;
        }
    }

    if (bRawImportText)
    {
        // Raw UE import text is the authoritative transport format for types
        // such as GAS FieldPath, FGameplayAttribute and localized FText. These
        // can be accepted by the real target object while failing scratch
        // round-trip or producing regenerated text keys, so full-consumption
        // ImportText validation is the meaningful safety check here.
        return true;
    }

    FString ExpectedText;
    FString ExpectedError;
    if (!BuildExpectedAssignedPropertyText(Property, PropertyValue, Redirects, Target, ExpectedText, ExpectedError))
    {
        OutError = FString::Printf(
            TEXT("Property '%s' was assigned but expected-value construction failed for %s: %s"),
            *PropertyValue.Name,
            *DescribePropertyForAssignment(Property),
            *ExpectedError);
        return false;
    }

    FString ActualText;
    Property->ExportTextItem(ActualText, ValuePtr, nullptr, Target, PPF_None);
    if (ActualText != ExpectedText)
    {
        OutError = FString::Printf(
            TEXT("Property '%s' assignment did not take effect for %s. Import text: '%s'. Expected exported value: '%s'. Actual exported value: '%s'."),
            *PropertyValue.Name,
            *DescribePropertyForAssignment(Property),
            *ImportText,
            *ExpectedText,
            *ActualText);
        return false;
    }

    if (!ValidateImportedPropertyRoundTrip(Target, Property, PropertyValue.Name, OutError))
    {
        return false;
    }

    return true;
}

FString FPropertyAssignmentService::RewriteImportTextRedirects(const FString& ImportText, const TArray<FAutomationAssetRedirect>& Redirects) const
{
    FString Rewritten = ImportText;
    for (const FAutomationAssetRedirect& Redirect : Redirects)
    {
        if (Redirect.From.IsEmpty() || Redirect.To.IsEmpty())
        {
            continue;
        }

        Rewritten = Rewritten.Replace(*Redirect.From, *Redirect.To, ESearchCase::CaseSensitive);

        FString FromPackage;
        FString FromObject;
        FString ToPackage;
        FString ToObject;
        if (Redirect.From.Split(TEXT("."), &FromPackage, &FromObject, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
            && Redirect.To.Split(TEXT("."), &ToPackage, &ToObject, ESearchCase::CaseSensitive, ESearchDir::FromEnd)
            && !FromPackage.IsEmpty()
            && !ToPackage.IsEmpty())
        {
            Rewritten = Rewritten.Replace(*FromPackage, *ToPackage, ESearchCase::CaseSensitive);
        }
    }
    return Rewritten;
}

bool FPropertyAssignmentService::ValidateImportedPropertyRoundTrip(UObject* Target, FProperty* Property, const FString& PropertyName, FString& OutError) const
{
    if (!Target || !Property)
    {
        OutError = FString::Printf(TEXT("Property '%s' round-trip validation target is invalid."), *PropertyName);
        return false;
    }

    if (IsStructPropertyNamed(Property, TEXT("BodyInstance")))
    {
        // FBodyInstance exports editor-facing collision data but keeps derived
        // physics/runtime fields outside the text form. ImportText followed by
        // ExportText equality is meaningful; full Identical() round-trip is not.
        return true;
    }

    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
    FString ExportedText;
    Property->ExportTextItem(ExportedText, ValuePtr, nullptr, Target, PPF_None);

    uint8* Scratch = static_cast<uint8*>(FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment()));
    Property->InitializeValue(Scratch);
    bool bSpecialImportHandled = false;
    if (!TryAssignGameplayTagImportText(Property, Scratch, ExportedText, OutError, bSpecialImportHandled))
    {
        Property->DestroyValue(Scratch);
        FMemory::Free(Scratch);
        return false;
    }
    if (!bSpecialImportHandled)
    {
        const TCHAR* ImportResult = Property->ImportText(*ExportedText, Scratch, PPF_None, Target);
        if (!ImportResult || !HasOnlyTrailingWhitespace(ImportResult))
        {
            Property->DestroyValue(Scratch);
            FMemory::Free(Scratch);
            OutError = FString::Printf(TEXT("Property '%s' failed export/import round-trip for exported value '%s'."), *PropertyName, *ExportedText);
            return false;
        }
    }

    const bool bIdentical = Property->Identical(ValuePtr, Scratch, PPF_None);
    Property->DestroyValue(Scratch);
    FMemory::Free(Scratch);
    if (!bIdentical)
    {
        OutError = FString::Printf(TEXT("Property '%s' did not survive export/import round-trip. Exported value: '%s'."), *PropertyName, *ExportedText);
        return false;
    }

    return true;
}

bool FPropertyAssignmentService::ValidateImportedPropertyMatchesText(UObject* Target, FProperty* Property, const FString& PropertyName, const FString& ImportText, FString& OutError) const
{
    if (!Target || !Property)
    {
        OutError = FString::Printf(TEXT("Property '%s' text validation target is invalid."), *PropertyName);
        return false;
    }

    uint8* Scratch = static_cast<uint8*>(FMemory::Malloc(Property->GetSize(), Property->GetMinAlignment()));
    Property->InitializeValue(Scratch);
    const TCHAR* ImportResult = Property->ImportText(*ImportText, Scratch, PPF_None, Target);
    if (!ImportResult || !HasOnlyTrailingWhitespace(ImportResult))
    {
        // Some FieldPath/GAS text accepted by the real target cannot be
        // reconstructed safely on scratch memory. Keep full-consumption target
        // ImportText as the fallback for those cases.
        Property->DestroyValue(Scratch);
        FMemory::Free(Scratch);
        return true;
    }

    void* ValuePtr = Property->ContainerPtrToValuePtr<void>(Target);
    const bool bIdentical = Property->Identical(ValuePtr, Scratch, PPF_None);
    FString ActualText;
    if (!bIdentical)
    {
        Property->ExportTextItem(ActualText, ValuePtr, nullptr, Target, PPF_None);
    }

    Property->DestroyValue(Scratch);
    FMemory::Free(Scratch);

    if (!bIdentical)
    {
        OutError = FString::Printf(
            TEXT("Property '%s' accepted import text but target value does not match imported scratch value. Import text: '%s'. Actual value: '%s'."),
            *PropertyName,
            *ImportText,
            *ActualText);
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

    if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
    {
        if (NumericProperty->IsInteger())
        {
            OutImportText = FString::Printf(TEXT("%lld"), static_cast<int64>(FMath::RoundToDouble(Value->AsNumber())));
        }
        else
        {
            OutImportText = FString::SanitizeFloat(Value->AsNumber());
        }
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
