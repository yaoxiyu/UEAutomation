#include "Domain/BlueprintTemplateRegistry.h"

#include "Core/EditorAutomationSettings.h"
#include "Dom/JsonObject.h"
#include "Dom/JsonValue.h"
#include "Misc/FileHelper.h"
#include "Misc/Paths.h"
#include "Serialization/JsonReader.h"
#include "Serialization/JsonSerializer.h"

bool FBlueprintTemplateRegistry::FindTemplate(const FString& TemplateId, FAutomationBlueprintTemplate& OutTemplate, FString& OutError) const
{
    TArray<FAutomationBlueprintTemplate> Templates;
    if (!LoadTemplates(Templates, OutError))
    {
        return false;
    }

    for (const FAutomationBlueprintTemplate& Template : Templates)
    {
        if (Template.TemplateId == TemplateId)
        {
            OutTemplate = Template;
            return true;
        }
    }

    OutError = FString::Printf(TEXT("Blueprint template '%s' was not found."), *TemplateId);
    return false;
}

FString FBlueprintTemplateRegistry::ResolveTemplateRegistryPath() const
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    FString Path = Settings->TemplateRegistryFilePath.FilePath;
    if (Path.IsEmpty())
    {
        Path = TEXT("Plugins/UEEditorAutomation/Config/UEEditorAutomationTemplates.json");
    }

    if (FPaths::IsRelative(Path))
    {
        Path = FPaths::ConvertRelativePathToFull(FPaths::ProjectDir(), Path);
    }

    return Path;
}

bool FBlueprintTemplateRegistry::LoadTemplates(TArray<FAutomationBlueprintTemplate>& OutTemplates, FString& OutError) const
{
    const FString RegistryPath = ResolveTemplateRegistryPath();

    FString JsonText;
    if (!FFileHelper::LoadFileToString(JsonText, *RegistryPath))
    {
        OutError = FString::Printf(TEXT("Template registry '%s' could not be loaded."), *RegistryPath);
        return false;
    }

    TSharedPtr<FJsonObject> Root;
    const TSharedRef<TJsonReader<>> Reader = TJsonReaderFactory<>::Create(JsonText);
    if (!FJsonSerializer::Deserialize(Reader, Root) || !Root.IsValid())
    {
        OutError = FString::Printf(TEXT("Template registry '%s' is not valid JSON."), *RegistryPath);
        return false;
    }

    const TArray<TSharedPtr<FJsonValue>>* TemplatesArray = nullptr;
    if (!Root->TryGetArrayField(TEXT("templates"), TemplatesArray))
    {
        OutError = FString::Printf(TEXT("Template registry '%s' is missing templates array."), *RegistryPath);
        return false;
    }

    TSet<FString> TemplateIds;
    for (const TSharedPtr<FJsonValue>& Value : *TemplatesArray)
    {
        FAutomationBlueprintTemplate Template;
        if (ParseTemplate(Value->AsObject(), Template))
        {
            if (TemplateIds.Contains(Template.TemplateId))
            {
                OutError = FString::Printf(TEXT("Template registry '%s' contains duplicate template_id '%s'."), *RegistryPath, *Template.TemplateId);
                return false;
            }
            if (!ValidateTemplate(Template, OutError))
            {
                OutError = FString::Printf(TEXT("Template registry '%s' contains invalid template '%s': %s"), *RegistryPath, *Template.TemplateId, *OutError);
                return false;
            }
            TemplateIds.Add(Template.TemplateId);
            OutTemplates.Add(Template);
        }
    }

    return true;
}

bool FBlueprintTemplateRegistry::ParseTemplate(const TSharedPtr<FJsonObject>& Object, FAutomationBlueprintTemplate& OutTemplate) const
{
    if (!Object.IsValid())
    {
        return false;
    }

    Object->TryGetStringField(TEXT("template_id"), OutTemplate.TemplateId);

    const TSharedPtr<FJsonObject>* AssetObject = nullptr;
    if (Object->TryGetObjectField(TEXT("asset"), AssetObject) && AssetObject && AssetObject->IsValid())
    {
        (*AssetObject)->TryGetStringField(TEXT("parent_class"), OutTemplate.ParentClass);
    }
    Object->TryGetStringField(TEXT("parent_class"), OutTemplate.ParentClass);

    const TSharedPtr<FJsonObject>* AssemblyObject = nullptr;
    if (Object->TryGetObjectField(TEXT("assembly"), AssemblyObject) && AssemblyObject && AssemblyObject->IsValid())
    {
        const TSharedPtr<FJsonObject>* RootComponentObject = nullptr;
        if ((*AssemblyObject)->TryGetObjectField(TEXT("root_component"), RootComponentObject) && RootComponentObject && RootComponentObject->IsValid())
        {
            ParseComponentSpec(*RootComponentObject, OutTemplate.RootComponent);
        }

        const TArray<TSharedPtr<FJsonValue>>* ComponentsArray = nullptr;
        if ((*AssemblyObject)->TryGetArrayField(TEXT("components"), ComponentsArray))
        {
            for (const TSharedPtr<FJsonValue>& Value : *ComponentsArray)
            {
                FAutomationComponentSpec Component;
                if (ParseComponentSpec(Value->AsObject(), Component))
                {
                    OutTemplate.Components.Add(Component);
                }
            }
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* ClassDefaultsArray = nullptr;
    if (Object->TryGetArrayField(TEXT("class_defaults"), ClassDefaultsArray)
        || Object->TryGetArrayField(TEXT("class_default_overrides"), ClassDefaultsArray))
    {
        ParsePropertyArray(ClassDefaultsArray, OutTemplate.ClassDefaults);
    }

    return !OutTemplate.TemplateId.IsEmpty();
}

bool FBlueprintTemplateRegistry::ValidateTemplate(const FAutomationBlueprintTemplate& Template, FString& OutError) const
{
    if (Template.TemplateId.IsEmpty())
    {
        OutError = TEXT("template_id is required.");
        return false;
    }
    if (Template.ParentClass.IsEmpty())
    {
        OutError = TEXT("parent_class is required.");
        return false;
    }

    TSet<FString> KnownComponents;
    if (!Template.RootComponent.ComponentName.IsEmpty())
    {
        if (Template.RootComponent.ComponentClass.IsEmpty())
        {
            OutError = TEXT("root_component.component_class is required.");
            return false;
        }
        KnownComponents.Add(Template.RootComponent.ComponentName);
    }

    for (const FAutomationComponentSpec& Component : Template.Components)
    {
        if (Component.ComponentName.IsEmpty())
        {
            OutError = TEXT("component_name is required.");
            return false;
        }
        if (Component.ComponentClass.IsEmpty())
        {
            OutError = FString::Printf(TEXT("component_class is required for component '%s'."), *Component.ComponentName);
            return false;
        }
        if (KnownComponents.Contains(Component.ComponentName))
        {
            OutError = FString::Printf(TEXT("duplicate component '%s'."), *Component.ComponentName);
            return false;
        }
        if (!Component.AttachParent.IsEmpty() && !KnownComponents.Contains(Component.AttachParent))
        {
            OutError = FString::Printf(TEXT("attach_parent '%s' is not declared before component '%s'."), *Component.AttachParent, *Component.ComponentName);
            return false;
        }
        KnownComponents.Add(Component.ComponentName);
    }

    return true;
}

bool FBlueprintTemplateRegistry::ParseComponentSpec(const TSharedPtr<FJsonObject>& Object, FAutomationComponentSpec& OutSpec) const
{
    if (!Object.IsValid())
    {
        return false;
    }

    Object->TryGetStringField(TEXT("component_name"), OutSpec.ComponentName);
    Object->TryGetStringField(TEXT("component_class"), OutSpec.ComponentClass);
    Object->TryGetStringField(TEXT("attach_parent"), OutSpec.AttachParent);

    const TSharedPtr<FJsonObject>* TransformObject = nullptr;
    if (Object->TryGetObjectField(TEXT("transform"), TransformObject) && TransformObject && TransformObject->IsValid())
    {
        const TArray<TSharedPtr<FJsonValue>>* LocationArray = nullptr;
        if ((*TransformObject)->TryGetArrayField(TEXT("location"), LocationArray))
        {
            OutSpec.Transform.bHasLocation = ParseVector(*LocationArray, OutSpec.Transform.Location);
        }

        const TArray<TSharedPtr<FJsonValue>>* RotationArray = nullptr;
        if ((*TransformObject)->TryGetArrayField(TEXT("rotation"), RotationArray))
        {
            OutSpec.Transform.bHasRotation = ParseRotator(*RotationArray, OutSpec.Transform.Rotation);
        }

        const TArray<TSharedPtr<FJsonValue>>* ScaleArray = nullptr;
        if ((*TransformObject)->TryGetArrayField(TEXT("scale"), ScaleArray))
        {
            OutSpec.Transform.bHasScale = ParseVector(*ScaleArray, OutSpec.Transform.Scale);
        }
    }

    const TArray<TSharedPtr<FJsonValue>>* PropertiesArray = nullptr;
    if (Object->TryGetArrayField(TEXT("properties"), PropertiesArray))
    {
        ParsePropertyArray(PropertiesArray, OutSpec.Properties);
    }

    return true;
}

bool FBlueprintTemplateRegistry::ParsePropertyArray(const TArray<TSharedPtr<FJsonValue>>* Array, TArray<FAutomationPropertyValue>& OutProperties) const
{
    if (!Array)
    {
        return false;
    }

    for (const TSharedPtr<FJsonValue>& Value : *Array)
    {
        const TSharedPtr<FJsonObject> Object = Value->AsObject();
        if (!Object.IsValid())
        {
            continue;
        }

        FAutomationPropertyValue Property;
        Object->TryGetStringField(TEXT("name"), Property.Name);
        Object->TryGetStringField(TEXT("type"), Property.Type);
        const TSharedPtr<FJsonValue>* FieldValue = Object->Values.Find(TEXT("value"));
        Property.Value = FieldValue ? *FieldValue : nullptr;
        OutProperties.Add(Property);
    }
    return true;
}

bool FBlueprintTemplateRegistry::ParseVector(const TArray<TSharedPtr<FJsonValue>>& Array, FVector& OutVector) const
{
    if (Array.Num() != 3)
    {
        return false;
    }
    OutVector = FVector(Array[0]->AsNumber(), Array[1]->AsNumber(), Array[2]->AsNumber());
    return true;
}

bool FBlueprintTemplateRegistry::ParseRotator(const TArray<TSharedPtr<FJsonValue>>& Array, FRotator& OutRotator) const
{
    if (Array.Num() != 3)
    {
        return false;
    }
    OutRotator = FRotator(Array[0]->AsNumber(), Array[1]->AsNumber(), Array[2]->AsNumber());
    return true;
}
