#pragma once

#include "CoreMinimal.h"
#include "Protocol/AutomationProtocolTypes.h"

struct FAutomationBlueprintTemplate
{
    FString TemplateId;
    FString ParentClass;
    FAutomationComponentSpec RootComponent;
    TArray<FAutomationComponentSpec> Components;
    TArray<FAutomationPropertyValue> ClassDefaults;
};

class FBlueprintTemplateRegistry
{
public:
    bool FindTemplate(const FString& TemplateId, FAutomationBlueprintTemplate& OutTemplate, FString& OutError) const;

private:
    FString ResolveTemplateRegistryPath() const;
    bool LoadTemplates(TArray<FAutomationBlueprintTemplate>& OutTemplates, FString& OutError) const;
    bool ParseTemplate(const TSharedPtr<class FJsonObject>& Object, FAutomationBlueprintTemplate& OutTemplate) const;
    bool ValidateTemplate(const FAutomationBlueprintTemplate& Template, FString& OutError) const;
    bool ParseComponentSpec(const TSharedPtr<class FJsonObject>& Object, FAutomationComponentSpec& OutSpec) const;
    bool ParsePropertyArray(const TArray<TSharedPtr<class FJsonValue>>* Array, TArray<FAutomationPropertyValue>& OutProperties) const;
    bool ParseVector(const TArray<TSharedPtr<class FJsonValue>>& Array, FVector& OutVector) const;
    bool ParseRotator(const TArray<TSharedPtr<class FJsonValue>>& Array, FRotator& OutRotator) const;
};
