#include "Core/EditorAutomationSettings.h"

UEditorAutomationSettings::UEditorAutomationSettings()
{
    bEnableDaemon = true;
    PollIntervalSeconds = 1.0f;
    MaxConcurrentTasks = 1;
    SupportedProtocolVersion = 1;

    TaskInboxDir.Path = TEXT("C:/UEAutomation/tasks/inbox");
    TaskWorkingDir.Path = TEXT("C:/UEAutomation/tasks/working");
    TaskDoneDir.Path = TEXT("C:/UEAutomation/tasks/done");
    TaskFailedDir.Path = TEXT("C:/UEAutomation/tasks/failed");
    ResultDir.Path = TEXT("C:/UEAutomation/results");
    LogDir.Path = TEXT("C:/UEAutomation/logs");

    AllowedTaskTypes = {
        TEXT("create_blueprint"),
        TEXT("modify_blueprint_components"),
        TEXT("modify_blueprint_defaults")
    };

    AllowedAssetRoots = {
        TEXT("/Game/Blueprints/AutoGen"),
        TEXT("/Game/UI/AutoGen"),
        TEXT("/Game/Test/AutoGen")
    };

    AllowedParentClasses = {
        TEXT("/Script/Engine.Actor")
    };

    AllowedComponentClasses = {
        TEXT("/Script/Engine.SceneComponent"),
        TEXT("/Script/Engine.StaticMeshComponent"),
        TEXT("/Script/Engine.SkeletalMeshComponent"),
        TEXT("/Script/Engine.BoxComponent"),
        TEXT("/Script/Engine.SphereComponent"),
        TEXT("/Script/Engine.CapsuleComponent"),
        TEXT("/Script/Engine.SpringArmComponent"),
        TEXT("/Script/Engine.CameraComponent"),
        TEXT("/Script/Engine.ArrowComponent"),
        TEXT("/Script/Engine.ChildActorComponent")
    };

    AllowedPropertyNames = {
        TEXT("StaticMesh"),
        TEXT("SkeletalMesh"),
        TEXT("RelativeLocation"),
        TEXT("RelativeRotation"),
        TEXT("RelativeScale3D"),
        TEXT("BoxExtent"),
        TEXT("SphereRadius"),
        TEXT("CapsuleRadius"),
        TEXT("CapsuleHalfHeight"),
        TEXT("CollisionProfileName"),
        TEXT("MaxHealth"),
        TEXT("MoveSpeed")
    };
}
