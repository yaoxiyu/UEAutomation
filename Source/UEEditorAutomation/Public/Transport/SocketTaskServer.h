#pragma once

#include "CoreMinimal.h"

class FEditorAutomationApplicationService;
class FSocket;

struct FSocketAutomationClient
{
    FSocket* Socket = nullptr;
    TArray<uint8> Buffer;
};

class FSocketTaskServer
{
public:
    void Initialize(const TSharedRef<FEditorAutomationApplicationService>& InApplicationService);
    void Shutdown();
    void Tick();

private:
    bool StartListening();
    void AcceptClients();
    void ProcessClients();
    void ProcessLine(const FString& JsonLine, FSocket* ClientSocket);
    void SendResponse(FSocket* ClientSocket, const FString& ResponseText);
    void CloseClient(int32 Index);

    TWeakPtr<FEditorAutomationApplicationService> ApplicationService;
    FSocket* ListenSocket = nullptr;
    TArray<FSocketAutomationClient> Clients;
};
