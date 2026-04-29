#include "Domain/NativeParentClassResolver.h"

#include "Engine/Blueprint.h"
#include "UObject/Class.h"
#include "UObject/UnrealType.h"

namespace
{
    bool IsClassNative(const UClass* Class)
    {
        if (!Class)
        {
            return false;
        }
        return Class->HasAnyClassFlags(CLASS_Native) && Class->ClassGeneratedBy == nullptr;
    }

    FString FormatClassPath(const UClass* Class)
    {
        if (!Class)
        {
            return FString();
        }
        return Class->GetPathName();
    }

    FString FormatDisplayName(const UClass* Class)
    {
        if (!Class)
        {
            return FString();
        }
        FString Name;
        Class->GetName(Name);
        if (IsClassNative(Class))
        {
            // Native classes typically have an A/U/F prefix in code; use class name with prefix.
            const FString Prefix = Class->GetPrefixCPP();
            if (!Prefix.IsEmpty())
            {
                return Prefix + Name;
            }
        }
        return Name;
    }
}

FString FNativeParentClassResolver::DeriveModuleNameFromClassPath(const FString& ClassPath)
{
    // Native class paths look like /Script/<Module>.<Class>
    if (!ClassPath.StartsWith(TEXT("/Script/")))
    {
        return FString();
    }
    const FString WithoutPrefix = ClassPath.RightChop(FString(TEXT("/Script/")).Len());
    int32 DotIndex = INDEX_NONE;
    if (WithoutPrefix.FindChar(TEXT('.'), DotIndex))
    {
        return WithoutPrefix.Left(DotIndex);
    }
    return WithoutPrefix;
}

bool FNativeParentClassResolver::Resolve(UBlueprint* Blueprint, FAutomationNativeParentInfo& OutInfo, FString& OutError) const
{
    if (!Blueprint)
    {
        OutError = TEXT("Blueprint is null");
        return false;
    }

    UClass* GeneratedClass = Blueprint->GeneratedClass;
    if (GeneratedClass)
    {
        FAutomationParentClassRecord SelfRecord;
        SelfRecord.ClassPath = FormatClassPath(GeneratedClass);
        SelfRecord.DisplayName = FormatDisplayName(GeneratedClass);
        SelfRecord.bIsNative = IsClassNative(GeneratedClass);
        OutInfo.SuperChain.Add(SelfRecord);
    }

    UClass* ImmediateParent = Blueprint->ParentClass;
    if (!ImmediateParent && GeneratedClass)
    {
        ImmediateParent = GeneratedClass->GetSuperClass();
    }
    if (!ImmediateParent)
    {
        OutError = TEXT("Cannot determine immediate parent class");
        return false;
    }

    OutInfo.ImmediateParent.ClassPath = FormatClassPath(ImmediateParent);
    OutInfo.ImmediateParent.DisplayName = FormatDisplayName(ImmediateParent);
    OutInfo.ImmediateParent.bIsNative = IsClassNative(ImmediateParent);

    UClass* Cursor = ImmediateParent;
    while (Cursor)
    {
        FAutomationParentClassRecord Record;
        Record.ClassPath = FormatClassPath(Cursor);
        Record.DisplayName = FormatDisplayName(Cursor);
        Record.bIsNative = IsClassNative(Cursor);
        OutInfo.SuperChain.Add(Record);

        if (Record.bIsNative && OutInfo.NativeParent.ClassPath.IsEmpty())
        {
            OutInfo.NativeParent = Record;
            OutInfo.NativeParentModuleName = DeriveModuleNameFromClassPath(Record.ClassPath);
        }

        Cursor = Cursor->GetSuperClass();
    }

    if (OutInfo.NativeParent.ClassPath.IsEmpty())
    {
        OutError = TEXT("NativeParentClassNotFound");
        return false;
    }
    return true;
}

bool FNativeParentClassResolver::ResolveFromClass(UClass* Class, FAutomationNativeParentInfo& OutInfo, FString& OutError) const
{
    if (!Class)
    {
        OutError = TEXT("Class is null");
        return false;
    }

    FAutomationParentClassRecord SelfRecord;
    SelfRecord.ClassPath = FormatClassPath(Class);
    SelfRecord.DisplayName = FormatDisplayName(Class);
    SelfRecord.bIsNative = IsClassNative(Class);
    OutInfo.SuperChain.Add(SelfRecord);
    OutInfo.ImmediateParent = SelfRecord;

    UClass* Cursor = Class;
    while (Cursor)
    {
        if (IsClassNative(Cursor) && OutInfo.NativeParent.ClassPath.IsEmpty())
        {
            FAutomationParentClassRecord Record;
            Record.ClassPath = FormatClassPath(Cursor);
            Record.DisplayName = FormatDisplayName(Cursor);
            Record.bIsNative = true;
            OutInfo.NativeParent = Record;
            OutInfo.NativeParentModuleName = DeriveModuleNameFromClassPath(Record.ClassPath);
            break;
        }
        Cursor = Cursor->GetSuperClass();
    }

    if (OutInfo.NativeParent.ClassPath.IsEmpty())
    {
        OutError = TEXT("NativeParentClassNotFound");
        return false;
    }
    return true;
}
