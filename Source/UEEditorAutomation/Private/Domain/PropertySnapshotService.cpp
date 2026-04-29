#include "Domain/PropertySnapshotService.h"

#include "Dom/JsonObject.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "UObject/Class.h"
#include "UObject/PropertyPortFlags.h"
#include "UObject/UnrealType.h"

FPropertySnapshotService::FPropertySnapshotService() = default;

void FPropertySnapshotService::SetDeniedExportNames(const TArray<FString>& InNames)
{
    DeniedNames = InNames;
}

void FPropertySnapshotService::SetParentTargetForDiff(UObject* InParent)
{
    ParentTarget = InParent;
}

bool FPropertySnapshotService::IsDenied(const FString& PropertyName) const
{
    for (const FString& Denied : DeniedNames)
    {
        if (Denied.Equals(PropertyName, ESearchCase::IgnoreCase))
        {
            return true;
        }
    }
    return false;
}

bool FPropertySnapshotService::ShouldSkipForExport(const FProperty* Property) const
{
    if (!Property)
    {
        return true;
    }
    if (Property->HasAnyPropertyFlags(CPF_Transient | CPF_Deprecated | CPF_DuplicateTransient))
    {
        return true;
    }
    return false;
}

namespace
{
    FString PortText(const FProperty* Property, const void* Address)
    {
        FString Out;
        if (Property && Address)
        {
            Property->ExportTextItem(Out, Address, nullptr, nullptr, PPF_None);
        }
        return Out;
    }

    bool ValueDiffersFromParent(FProperty* Property, const void* Address, UObject* ParentTarget)
    {
        if (!ParentTarget || !Property || !Address)
        {
            return false;
        }
        FProperty* ParentProperty = ParentTarget->GetClass()->FindPropertyByName(Property->GetFName());
        if (!ParentProperty || ParentProperty->GetClass() != Property->GetClass())
        {
            return false;
        }
        const void* ParentAddress = ParentProperty->ContainerPtrToValuePtr<void>(ParentTarget);
        return !Property->Identical(Address, ParentAddress, PPF_None);
    }

    TSharedRef<FJsonValue> JsonForObjectRef(const UObject* Referenced)
    {
        if (!Referenced)
        {
            return MakeShared<FJsonValueNull>();
        }
        return MakeShared<FJsonValueString>(Referenced->GetPathName());
    }

    TSharedRef<FJsonValue> JsonForVectorLike(double X, double Y, double Z)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("X"), X);
        Object->SetNumberField(TEXT("Y"), Y);
        Object->SetNumberField(TEXT("Z"), Z);
        return MakeShared<FJsonValueObject>(Object);
    }

    bool IsDepthExemptStruct(const FProperty* Property)
    {
        const FStructProperty* StructProperty = CastField<FStructProperty>(Property);
        const UScriptStruct* Struct = StructProperty ? StructProperty->Struct : nullptr;
        if (!Struct)
        {
            return false;
        }

        const FString StructName = Struct->GetName();
        return StructName == TEXT("GameplayTag")
            || StructName == TEXT("GameplayTagContainer")
            || StructName == TEXT("DataTableRowHandle")
            || StructName == TEXT("ScalableFloat");
    }
}

TSharedPtr<FJsonValue> FPropertySnapshotService::ExportPropertyValue(
    FProperty* Property,
    const void* PropertyAddress,
    int32 RecursionDepth,
    const FAutomationAnalysisOptions& Options,
    FAutomationTaskResult& OutResult) const
{
    if (!Property || !PropertyAddress)
    {
        return MakeShared<FJsonValueNull>();
    }

    if (RecursionDepth >= Options.MaxPropertyDepth && !IsDepthExemptStruct(Property))
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetBoolField(TEXT("truncated"), true);
        Object->SetStringField(TEXT("reason"), TEXT("max_property_depth"));
        return MakeShared<FJsonValueObject>(Object);
    }

    if (FBoolProperty* BoolProperty = CastField<FBoolProperty>(Property))
    {
        return MakeShared<FJsonValueBoolean>(BoolProperty->GetPropertyValue(PropertyAddress));
    }
    if (FNumericProperty* NumericProperty = CastField<FNumericProperty>(Property))
    {
        if (NumericProperty->IsEnum())
        {
            const int64 Raw = NumericProperty->GetSignedIntPropertyValue(PropertyAddress);
            const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            Object->SetNumberField(TEXT("raw"), Raw);
            if (UEnum* EnumDef = NumericProperty->GetIntPropertyEnum())
            {
                Object->SetStringField(TEXT("name"), EnumDef->GetNameStringByValue(Raw));
            }
            return MakeShared<FJsonValueObject>(Object);
        }
        if (NumericProperty->IsFloatingPoint())
        {
            return MakeShared<FJsonValueNumber>(NumericProperty->GetFloatingPointPropertyValue(PropertyAddress));
        }
        return MakeShared<FJsonValueNumber>(NumericProperty->GetSignedIntPropertyValue(PropertyAddress));
    }
    if (FByteProperty* ByteProperty = CastField<FByteProperty>(Property))
    {
        const uint8 Raw = ByteProperty->GetPropertyValue(PropertyAddress);
        if (UEnum* EnumDef = ByteProperty->Enum)
        {
            const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            Object->SetNumberField(TEXT("raw"), Raw);
            Object->SetStringField(TEXT("name"), EnumDef->GetNameStringByValue(Raw));
            return MakeShared<FJsonValueObject>(Object);
        }
        return MakeShared<FJsonValueNumber>(Raw);
    }
    if (FEnumProperty* EnumProperty = CastField<FEnumProperty>(Property))
    {
        const int64 Raw = EnumProperty->GetUnderlyingProperty()->GetSignedIntPropertyValue(PropertyAddress);
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetNumberField(TEXT("raw"), Raw);
        if (UEnum* EnumDef = EnumProperty->GetEnum())
        {
            Object->SetStringField(TEXT("name"), EnumDef->GetNameStringByValue(Raw));
        }
        return MakeShared<FJsonValueObject>(Object);
    }
    if (FStrProperty* StrProperty = CastField<FStrProperty>(Property))
    {
        return MakeShared<FJsonValueString>(StrProperty->GetPropertyValue(PropertyAddress));
    }
    if (FNameProperty* NameProperty = CastField<FNameProperty>(Property))
    {
        return MakeShared<FJsonValueString>(NameProperty->GetPropertyValue(PropertyAddress).ToString());
    }
    if (FTextProperty* TextProperty = CastField<FTextProperty>(Property))
    {
        return MakeShared<FJsonValueString>(TextProperty->GetPropertyValue(PropertyAddress).ToString());
    }
    if (FClassProperty* ClassProperty = CastField<FClassProperty>(Property))
    {
        UObject* Object = ClassProperty->GetObjectPropertyValue(PropertyAddress);
        return JsonForObjectRef(Object);
    }
    if (FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
    {
        UObject* Object = ObjectProperty->GetObjectPropertyValue(PropertyAddress);
        return JsonForObjectRef(Object);
    }
    if (FSoftObjectProperty* SoftObjectProperty = CastField<FSoftObjectProperty>(Property))
    {
        return MakeShared<FJsonValueString>(SoftObjectProperty->GetPropertyValue(PropertyAddress).ToString());
    }
    if (FSoftClassProperty* SoftClassProperty = CastField<FSoftClassProperty>(Property))
    {
        return MakeShared<FJsonValueString>(SoftClassProperty->GetPropertyValue(PropertyAddress).ToString());
    }
    if (FStructProperty* StructProperty = CastField<FStructProperty>(Property))
    {
        UScriptStruct* Struct = StructProperty->Struct;
        if (Struct)
        {
            const FString StructName = Struct->GetName();
            if (StructName == TEXT("Vector"))
            {
                const FVector* V = static_cast<const FVector*>(PropertyAddress);
                return JsonForVectorLike(V->X, V->Y, V->Z);
            }
            if (StructName == TEXT("Vector2D"))
            {
                const FVector2D* V = static_cast<const FVector2D*>(PropertyAddress);
                const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
                Object->SetNumberField(TEXT("X"), V->X);
                Object->SetNumberField(TEXT("Y"), V->Y);
                return MakeShared<FJsonValueObject>(Object);
            }
            if (StructName == TEXT("Rotator"))
            {
                const FRotator* R = static_cast<const FRotator*>(PropertyAddress);
                const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
                Object->SetNumberField(TEXT("Pitch"), R->Pitch);
                Object->SetNumberField(TEXT("Yaw"), R->Yaw);
                Object->SetNumberField(TEXT("Roll"), R->Roll);
                return MakeShared<FJsonValueObject>(Object);
            }
            if (StructName == TEXT("Color"))
            {
                const FColor* C = static_cast<const FColor*>(PropertyAddress);
                const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
                Object->SetNumberField(TEXT("R"), C->R);
                Object->SetNumberField(TEXT("G"), C->G);
                Object->SetNumberField(TEXT("B"), C->B);
                Object->SetNumberField(TEXT("A"), C->A);
                return MakeShared<FJsonValueObject>(Object);
            }
            if (StructName == TEXT("LinearColor"))
            {
                const FLinearColor* C = static_cast<const FLinearColor*>(PropertyAddress);
                const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
                Object->SetNumberField(TEXT("R"), C->R);
                Object->SetNumberField(TEXT("G"), C->G);
                Object->SetNumberField(TEXT("B"), C->B);
                Object->SetNumberField(TEXT("A"), C->A);
                return MakeShared<FJsonValueObject>(Object);
            }
            if (StructName == TEXT("GameplayTag"))
            {
                const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
                if (FProperty* TagNameProperty = Struct->FindPropertyByName(TEXT("TagName")))
                {
                    const void* TagNameAddress = TagNameProperty->ContainerPtrToValuePtr<void>(PropertyAddress);
                    Object->SetField(TEXT("TagName"), ExportPropertyValue(TagNameProperty, TagNameAddress, 0, Options, OutResult));
                }
                return MakeShared<FJsonValueObject>(Object);
            }
            if (StructName == TEXT("GameplayTagContainer"))
            {
                const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
                if (FProperty* TagsProperty = Struct->FindPropertyByName(TEXT("GameplayTags")))
                {
                    const void* TagsAddress = TagsProperty->ContainerPtrToValuePtr<void>(PropertyAddress);
                    Object->SetField(TEXT("GameplayTags"), ExportPropertyValue(TagsProperty, TagsAddress, 0, Options, OutResult));
                }
                return MakeShared<FJsonValueObject>(Object);
            }
            if (StructName == TEXT("DataTableRowHandle") || StructName == TEXT("ScalableFloat"))
            {
                const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
                for (TFieldIterator<FProperty> It(Struct); It; ++It)
                {
                    FProperty* Inner = *It;
                    if (!Inner || ShouldSkipForExport(Inner) || IsDenied(Inner->GetName()))
                    {
                        continue;
                    }
                    const void* InnerAddress = Inner->ContainerPtrToValuePtr<void>(PropertyAddress);
                    Object->SetField(Inner->GetName(), ExportPropertyValue(Inner, InnerAddress, 0, Options, OutResult));
                }
                return MakeShared<FJsonValueObject>(Object);
            }
        }

        // Generic struct: walk inner properties up to depth limit.
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        for (TFieldIterator<FProperty> It(Struct); It; ++It)
        {
            FProperty* Inner = *It;
            if (ShouldSkipForExport(Inner))
            {
                continue;
            }
            if (IsDenied(Inner->GetName()))
            {
                continue;
            }
            const void* InnerAddress = Inner->ContainerPtrToValuePtr<void>(PropertyAddress);
            Object->SetField(Inner->GetName(),
                ExportPropertyValue(Inner, InnerAddress, RecursionDepth + 1, Options, OutResult));
        }
        return MakeShared<FJsonValueObject>(Object);
    }
    if (FArrayProperty* ArrayProperty = CastField<FArrayProperty>(Property))
    {
        FScriptArrayHelper Helper(ArrayProperty, PropertyAddress);
        const int32 Count = Helper.Num();
        const int32 Limit = FMath::Min(Count, FMath::Max(0, Options.MaxArrayElements));
        TArray<TSharedPtr<FJsonValue>> ItemArray;
        for (int32 Index = 0; Index < Limit; ++Index)
        {
            const uint8* InnerAddress = Helper.GetRawPtr(Index);
            ItemArray.Add(ExportPropertyValue(ArrayProperty->Inner, InnerAddress, RecursionDepth + 1, Options, OutResult));
        }
        if (Count > Limit)
        {
            const TSharedRef<FJsonObject> Trunc = MakeShared<FJsonObject>();
            Trunc->SetBoolField(TEXT("truncated"), true);
            Trunc->SetNumberField(TEXT("total"), Count);
            Trunc->SetNumberField(TEXT("returned"), Limit);
            ItemArray.Add(MakeShared<FJsonValueObject>(Trunc));
        }
        return MakeShared<FJsonValueArray>(ItemArray);
    }
    if (FSetProperty* SetProperty = CastField<FSetProperty>(Property))
    {
        FScriptSetHelper Helper(SetProperty, PropertyAddress);
        const int32 Count = Helper.Num();
        const int32 Limit = FMath::Min(Count, FMath::Max(0, Options.MaxArrayElements));
        TArray<TSharedPtr<FJsonValue>> ItemArray;
        int32 Returned = 0;
        for (int32 Index = 0; Index < Helper.GetMaxIndex() && Returned < Limit; ++Index)
        {
            if (!Helper.IsValidIndex(Index))
            {
                continue;
            }
            const uint8* ElemAddress = Helper.GetElementPtr(Index);
            ItemArray.Add(ExportPropertyValue(SetProperty->ElementProp, ElemAddress, RecursionDepth + 1, Options, OutResult));
            ++Returned;
        }
        if (Count > Limit)
        {
            const TSharedRef<FJsonObject> Trunc = MakeShared<FJsonObject>();
            Trunc->SetBoolField(TEXT("truncated"), true);
            Trunc->SetNumberField(TEXT("total"), Count);
            Trunc->SetNumberField(TEXT("returned"), Limit);
            ItemArray.Add(MakeShared<FJsonValueObject>(Trunc));
        }
        return MakeShared<FJsonValueArray>(ItemArray);
    }
    if (FMapProperty* MapProperty = CastField<FMapProperty>(Property))
    {
        FScriptMapHelper Helper(MapProperty, PropertyAddress);
        const int32 Count = Helper.Num();
        const int32 Limit = FMath::Min(Count, FMath::Max(0, Options.MaxArrayElements));
        TArray<TSharedPtr<FJsonValue>> ItemArray;
        int32 Returned = 0;
        for (int32 Index = 0; Index < Helper.GetMaxIndex() && Returned < Limit; ++Index)
        {
            if (!Helper.IsValidIndex(Index))
            {
                continue;
            }
            const uint8* KeyAddr = Helper.GetKeyPtr(Index);
            const uint8* ValueAddr = Helper.GetValuePtr(Index);
            const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
            Object->SetField(TEXT("key"), ExportPropertyValue(MapProperty->KeyProp, KeyAddr, RecursionDepth + 1, Options, OutResult));
            Object->SetField(TEXT("value"), ExportPropertyValue(MapProperty->ValueProp, ValueAddr, RecursionDepth + 1, Options, OutResult));
            ItemArray.Add(MakeShared<FJsonValueObject>(Object));
            ++Returned;
        }
        if (Count > Limit)
        {
            const TSharedRef<FJsonObject> Trunc = MakeShared<FJsonObject>();
            Trunc->SetBoolField(TEXT("truncated"), true);
            Trunc->SetNumberField(TEXT("total"), Count);
            Trunc->SetNumberField(TEXT("returned"), Limit);
            ItemArray.Add(MakeShared<FJsonValueObject>(Trunc));
        }
        return MakeShared<FJsonValueArray>(ItemArray);
    }

    // Fallback: ExportTextItem.
    return MakeShared<FJsonValueString>(PortText(Property, PropertyAddress));
}

bool FPropertySnapshotService::ExportObjectProperties(
    UObject* Target,
    const FAutomationAnalysisOptions& Options,
    TArray<TSharedPtr<FJsonValue>>& OutProperties,
    FAutomationTaskResult& OutResult) const
{
    if (!Target)
    {
        OutResult.AddError(TEXT("PropertySnapshotFailed"), TEXT("Target is null"));
        return false;
    }

    UClass* Class = Target->GetClass();
    for (TFieldIterator<FProperty> It(Class); It; ++It)
    {
        FProperty* Property = *It;
        if (ShouldSkipForExport(Property))
        {
            continue;
        }
        if (IsDenied(Property->GetName()))
        {
            continue;
        }
        if (Options.bExportOnlyEditableProperties)
        {
            const bool bEditable = Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst);
            const bool bBpVisible = Property->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintReadOnly);
            if (!bEditable && !bBpVisible)
            {
                continue;
            }
        }

        const void* Address = Property->ContainerPtrToValuePtr<void>(Target);
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        Object->SetStringField(TEXT("name"), Property->GetName());
        Object->SetStringField(TEXT("ue_type"), Property->GetClass() ? Property->GetClass()->GetName() : FString());
        Object->SetField(TEXT("value"), ExportPropertyValue(Property, Address, 0, Options, OutResult));
        Object->SetBoolField(TEXT("editable"),
            Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst));
        Object->SetBoolField(TEXT("blueprint_visible"),
            Property->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintReadOnly));
        Object->SetBoolField(TEXT("differs_from_parent"),
            ValueDiffersFromParent(Property, Address, ParentTarget));
        OutProperties.Add(MakeShared<FJsonValueObject>(Object));
    }
    return true;
}
