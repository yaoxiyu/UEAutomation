using UnrealBuildTool;

public class UEEditorAutomation : ModuleRules
{
    public UEEditorAutomation(ReadOnlyTargetRules Target) : base(Target)
    {
        PCHUsage = PCHUsageMode.UseExplicitOrSharedPCHs;

        PublicDependencyModuleNames.AddRange(new[]
        {
            "Core",
            "CoreUObject",
            "Engine",
            "GameplayTags",
            "Json",
            "JsonUtilities"
        });

        PrivateDependencyModuleNames.AddRange(new[]
        {
            "AssetRegistry",
            "AssetTools",
            "BlueprintGraph",
            "EditorStyle",
            "InputCore",
            "KismetCompiler",
            "LevelEditor",
            "Projects",
            "Networking",
            "Slate",
            "SlateCore",
            "Sockets",
            "UnrealEd"
        });
    }
}
