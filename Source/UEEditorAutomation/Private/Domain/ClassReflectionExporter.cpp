#include "Domain/ClassReflectionExporter.h"

#include "Components/ActorComponent.h"
#include "Domain/NativeParentClassResolver.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "UObject/Class.h"
#include "UObject/MetaData.h"
#include "UObject/Package.h"
#include "UObject/UnrealType.h"

namespace
{
    FString GetCppTypeName(const FProperty* Property)
    {
        if (!Property)
        {
            return FString();
        }
        return Property->GetCPPType();
    }

    FString GetUEPropertyClassName(const FProperty* Property)
    {
        if (!Property)
        {
            return FString();
        }
        return Property->GetClass() ? Property->GetClass()->GetName() : FString();
    }

    void AppendFlagIf(TArray<TSharedPtr<FJsonValue>>& OutArray, const FProperty* Property, EPropertyFlags Flag, const TCHAR* Name)
    {
        if (Property->HasAnyPropertyFlags(Flag))
        {
            OutArray.Add(MakeShared<FJsonValueString>(Name));
        }
    }

    void AppendClassFlagIf(TArray<TSharedPtr<FJsonValue>>& OutArray, EClassFlags Flag, EClassFlags Flags, const TCHAR* Name)
    {
        if ((Flags & Flag) != 0)
        {
            OutArray.Add(MakeShared<FJsonValueString>(Name));
        }
    }

    TSharedRef<FJsonObject> BuildMetadata(const FField* Field)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        if (!Field)
        {
            return Object;
        }
        const TMap<FName, FString>* Map = Field->GetMetaDataMap();
        if (!Map)
        {
            return Object;
        }
        for (const auto& Pair : *Map)
        {
            Object->SetStringField(Pair.Key.ToString(), Pair.Value);
        }
        return Object;
    }

    TSharedRef<FJsonObject> BuildUObjectMetadata(const UObject* Object)
    {
        const TSharedRef<FJsonObject> Result = MakeShared<FJsonObject>();
        if (!Object)
        {
            return Result;
        }
#if WITH_EDITORONLY_DATA
        if (TMap<FName, FString>* Map = UMetaData::GetMapForObject(Object))
        {
            for (const auto& Pair : *Map)
            {
                Result->SetStringField(Pair.Key.ToString(), Pair.Value);
            }
        }
#endif
        return Result;
    }

    TSharedRef<FJsonObject> BuildClassMetadata(const UClass* Class)
    {
        return BuildUObjectMetadata(Class);
    }

    TSharedRef<FJsonObject> BuildPropertyJson(const FProperty* Property, const UClass* DeclaredOnClass)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        if (!Property)
        {
            return Object;
        }
        Object->SetStringField(TEXT("name"), Property->GetName());
        Object->SetStringField(TEXT("cpp_type"), GetCppTypeName(Property));
        Object->SetStringField(TEXT("ue_type"), GetUEPropertyClassName(Property));

        Object->SetStringField(TEXT("category"), Property->GetMetaData(TEXT("Category")));

        TArray<TSharedPtr<FJsonValue>> Flags;
        AppendFlagIf(Flags, Property, CPF_Edit, TEXT("Edit"));
        AppendFlagIf(Flags, Property, CPF_EditConst, TEXT("EditConst"));
        AppendFlagIf(Flags, Property, CPF_BlueprintVisible, TEXT("BlueprintVisible"));
        AppendFlagIf(Flags, Property, CPF_BlueprintReadOnly, TEXT("BlueprintReadOnly"));
        AppendFlagIf(Flags, Property, CPF_BlueprintAssignable, TEXT("BlueprintAssignable"));
        AppendFlagIf(Flags, Property, CPF_BlueprintCallable, TEXT("BlueprintCallable"));
        AppendFlagIf(Flags, Property, CPF_Config, TEXT("Config"));
        AppendFlagIf(Flags, Property, CPF_SaveGame, TEXT("SaveGame"));
        AppendFlagIf(Flags, Property, CPF_Transient, TEXT("Transient"));
        AppendFlagIf(Flags, Property, CPF_Deprecated, TEXT("Deprecated"));
        AppendFlagIf(Flags, Property, CPF_DuplicateTransient, TEXT("DuplicateTransient"));
        AppendFlagIf(Flags, Property, CPF_ExposeOnSpawn, TEXT("ExposeOnSpawn"));
        AppendFlagIf(Flags, Property, CPF_RepNotify, TEXT("RepNotify"));
        AppendFlagIf(Flags, Property, CPF_Net, TEXT("Net"));
        Object->SetArrayField(TEXT("flags"), Flags);

        Object->SetObjectField(TEXT("metadata"), BuildMetadata(Property));
        Object->SetBoolField(TEXT("is_editable"), Property->HasAnyPropertyFlags(CPF_Edit) && !Property->HasAnyPropertyFlags(CPF_EditConst));
        Object->SetBoolField(TEXT("is_blueprint_visible"),
            Property->HasAnyPropertyFlags(CPF_BlueprintVisible | CPF_BlueprintReadOnly));

        if (DeclaredOnClass)
        {
            Object->SetStringField(TEXT("declared_on_class_path"), DeclaredOnClass->GetPathName());
        }

        const TSharedRef<FJsonObject> Source = MakeShared<FJsonObject>();
        Source->SetStringField(TEXT("file"),
            DeclaredOnClass ? DeclaredOnClass->GetMetaData(TEXT("ModuleRelativePath")) : FString());
        Object->SetObjectField(TEXT("source_location"), Source);

        return Object;
    }

    TSharedRef<FJsonObject> BuildFunctionJson(UFunction* Function)
    {
        const TSharedRef<FJsonObject> Object = MakeShared<FJsonObject>();
        if (!Function)
        {
            return Object;
        }
        Object->SetStringField(TEXT("name"), Function->GetName());

        TArray<TSharedPtr<FJsonValue>> Flags;
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintCallable))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintCallable")));
        }
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintPure))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintPure")));
        }
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintEvent))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintEvent")));
        }
        if (Function->HasAnyFunctionFlags(FUNC_BlueprintAuthorityOnly))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("BlueprintAuthorityOnly")));
        }
        if (Function->HasAnyFunctionFlags(FUNC_Net))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("Net")));
        }
        if (Function->HasAnyFunctionFlags(FUNC_Static))
        {
            Flags.Add(MakeShared<FJsonValueString>(TEXT("Static")));
        }
        Object->SetArrayField(TEXT("flags"), Flags);

        Object->SetStringField(TEXT("category"), Function->GetMetaData(TEXT("Category")));
        Object->SetObjectField(TEXT("metadata"), BuildUObjectMetadata(Function));

        FString ReturnType;
        TArray<TSharedPtr<FJsonValue>> Parameters;
        for (TFieldIterator<FProperty> It(Function); It; ++It)
        {
            FProperty* Param = *It;
            if (!Param)
            {
                continue;
            }
            if (Param->HasAnyPropertyFlags(CPF_ReturnParm))
            {
                ReturnType = GetCppTypeName(Param);
                continue;
            }
            const TSharedRef<FJsonObject> ParamObject = MakeShared<FJsonObject>();
            ParamObject->SetStringField(TEXT("name"), Param->GetName());
            ParamObject->SetStringField(TEXT("cpp_type"), GetCppTypeName(Param));
            ParamObject->SetStringField(TEXT("ue_type"), GetUEPropertyClassName(Param));

            FString Direction = TEXT("in");
            if (Param->HasAnyPropertyFlags(CPF_OutParm))
            {
                Direction = Param->HasAnyPropertyFlags(CPF_ReferenceParm) ? TEXT("ref") : TEXT("out");
            }
            ParamObject->SetStringField(TEXT("direction"), Direction);
            Parameters.Add(MakeShared<FJsonValueObject>(ParamObject));
        }
        Object->SetStringField(TEXT("return_type"), ReturnType.IsEmpty() ? TEXT("void") : ReturnType);
        Object->SetArrayField(TEXT("parameters"), Parameters);

        return Object;
    }

    bool IsActorComponentProperty(const FProperty* Property, UClass*& OutComponentClass)
    {
        OutComponentClass = nullptr;
        if (!Property)
        {
            return false;
        }
        if (const FObjectPropertyBase* ObjectProperty = CastField<FObjectPropertyBase>(Property))
        {
            UClass* PropertyClass = ObjectProperty->PropertyClass;
            if (PropertyClass && PropertyClass->IsChildOf(UActorComponent::StaticClass()))
            {
                OutComponentClass = PropertyClass;
                return true;
            }
        }
        return false;
    }
}

bool FClassReflectionExporter::ExportClass(
    UClass* Class,
    const TSharedRef<FJsonObject>& OutJson,
    FAutomationTaskResult& OutResult) const
{
    if (!Class)
    {
        OutResult.AddError(TEXT("ClassReflectionExportFailed"), TEXT("Class is null"));
        return false;
    }

    const TSharedRef<FJsonObject> ClassObject = MakeShared<FJsonObject>();
    FString ClassName;
    Class->GetName(ClassName);
    const FString Prefix = Class->GetPrefixCPP();
    ClassObject->SetStringField(TEXT("name"), Prefix + ClassName);
    ClassObject->SetStringField(TEXT("class_path"), Class->GetPathName());
    ClassObject->SetStringField(TEXT("module_name"),
        FNativeParentClassResolver::DeriveModuleNameFromClassPath(Class->GetPathName()));
    if (UClass* Super = Class->GetSuperClass())
    {
        ClassObject->SetStringField(TEXT("super_class_path"), Super->GetPathName());
    }
    else
    {
        ClassObject->SetStringField(TEXT("super_class_path"), FString());
    }

    TArray<TSharedPtr<FJsonValue>> ClassFlagArray;
    const EClassFlags ClassFlags = Class->ClassFlags;
    AppendClassFlagIf(ClassFlagArray, CLASS_Native, ClassFlags, TEXT("Native"));
    AppendClassFlagIf(ClassFlagArray, CLASS_Abstract, ClassFlags, TEXT("Abstract"));
    AppendClassFlagIf(ClassFlagArray, CLASS_Deprecated, ClassFlags, TEXT("Deprecated"));
    AppendClassFlagIf(ClassFlagArray, CLASS_Interface, ClassFlags, TEXT("Interface"));
    AppendClassFlagIf(ClassFlagArray, CLASS_Const, ClassFlags, TEXT("Const"));
    AppendClassFlagIf(ClassFlagArray, CLASS_Transient, ClassFlags, TEXT("Transient"));
    AppendClassFlagIf(ClassFlagArray, CLASS_Config, ClassFlags, TEXT("Config"));
    ClassObject->SetArrayField(TEXT("class_flags"), ClassFlagArray);

    ClassObject->SetObjectField(TEXT("metadata"), BuildClassMetadata(Class));
    OutJson->SetObjectField(TEXT("class"), ClassObject);

    TArray<TSharedPtr<FJsonValue>> Properties;
    TArray<TSharedPtr<FJsonValue>> Components;
    for (TFieldIterator<FProperty> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        FProperty* Property = *It;
        UClass* DeclaredOn = Property->GetOwnerClass();
        Properties.Add(MakeShared<FJsonValueObject>(BuildPropertyJson(Property, DeclaredOn)));

        UClass* ComponentClass = nullptr;
        if (IsActorComponentProperty(Property, ComponentClass))
        {
            const TSharedRef<FJsonObject> ComponentObject = MakeShared<FJsonObject>();
            ComponentObject->SetStringField(TEXT("name"), Property->GetName());
            ComponentObject->SetStringField(TEXT("cpp_type"), GetCppTypeName(Property));
            ComponentObject->SetStringField(TEXT("class_path"), ComponentClass ? ComponentClass->GetPathName() : FString());
            TArray<TSharedPtr<FJsonValue>> CompFlags;
            AppendFlagIf(CompFlags, Property, CPF_Edit, TEXT("Edit"));
            AppendFlagIf(CompFlags, Property, CPF_BlueprintVisible, TEXT("BlueprintVisible"));
            AppendFlagIf(CompFlags, Property, CPF_BlueprintReadOnly, TEXT("BlueprintReadOnly"));
            ComponentObject->SetArrayField(TEXT("flags"), CompFlags);
            Components.Add(MakeShared<FJsonValueObject>(ComponentObject));
        }
    }
    OutJson->SetArrayField(TEXT("properties"), Properties);
    OutJson->SetArrayField(TEXT("components_declared_in_cxx"), Components);

    TArray<TSharedPtr<FJsonValue>> Functions;
    for (TFieldIterator<UFunction> It(Class, EFieldIteratorFlags::IncludeSuper); It; ++It)
    {
        UFunction* Function = *It;
        if (!Function)
        {
            continue;
        }
        if (!Function->HasAnyFunctionFlags(FUNC_BlueprintCallable | FUNC_BlueprintPure | FUNC_BlueprintEvent))
        {
            continue;
        }
        Functions.Add(MakeShared<FJsonValueObject>(BuildFunctionJson(Function)));
    }
    OutJson->SetArrayField(TEXT("functions"), Functions);

    return true;
}
