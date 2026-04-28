#include "Transport/SocketTaskServer.h"

#include "Application/EditorAutomationApplicationService.h"
#include "Core/AutomationLog.h"
#include "Core/EditorAutomationSettings.h"
#include "Common/TcpSocketBuilder.h"
#include "Protocol/AutomationProtocolTypes.h"
#include "Sockets.h"
#include "SocketSubsystem.h"

void FSocketTaskServer::Initialize(const TSharedRef<FEditorAutomationApplicationService>& InApplicationService)
{
    ApplicationService = InApplicationService;
    StartListening();
}

void FSocketTaskServer::Shutdown()
{
    for (int32 Index = Clients.Num() - 1; Index >= 0; --Index)
    {
        CloseClient(Index);
    }

    if (ListenSocket)
    {
        ListenSocket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(ListenSocket);
        ListenSocket = nullptr;
    }
}

void FSocketTaskServer::Tick()
{
    if (!ListenSocket && !StartListening())
    {
        return;
    }

    AcceptClients();
    ProcessClients();
}

bool FSocketTaskServer::StartListening()
{
    const UEditorAutomationSettings* Settings = GetDefault<UEditorAutomationSettings>();
    if (!Settings->bEnableUEAutomationSocketServer)
    {
        return false;
    }

    if (ListenSocket)
    {
        return true;
    }

    const FIPv4Endpoint Endpoint(FIPv4Address::InternalLoopback, Settings->UEAutomationSocketPort);
    ListenSocket = FTcpSocketBuilder(TEXT("UEEditorAutomationSocketServer"))
        .AsReusable()
        .BoundToEndpoint(Endpoint)
        .Listening(8)
        .WithReceiveBufferSize(2 * 1024 * 1024);

    if (!ListenSocket)
    {
        UE_LOG(LogUEEditorAutomation, Warning, TEXT("Socket server failed to listen on 127.0.0.1:%d."), Settings->UEAutomationSocketPort);
        return false;
    }

    ListenSocket->SetNonBlocking(true);
    UE_LOG(LogUEEditorAutomation, Log, TEXT("Socket server listening on 127.0.0.1:%d."), Settings->UEAutomationSocketPort);
    return true;
}

void FSocketTaskServer::AcceptClients()
{
    bool bHasPendingConnection = false;
    while (ListenSocket && ListenSocket->HasPendingConnection(bHasPendingConnection) && bHasPendingConnection)
    {
        FSocket* ClientSocket = ListenSocket->Accept(TEXT("UEEditorAutomationClient"));
        if (!ClientSocket)
        {
            return;
        }

        ClientSocket->SetNonBlocking(true);

        FSocketAutomationClient Client;
        Client.Socket = ClientSocket;
        Clients.Add(Client);
    }
}

void FSocketTaskServer::ProcessClients()
{
    for (int32 Index = Clients.Num() - 1; Index >= 0; --Index)
    {
        FSocketAutomationClient& Client = Clients[Index];
        if (!Client.Socket)
        {
            CloseClient(Index);
            continue;
        }

        uint32 PendingDataSize = 0;
        while (Client.Socket->HasPendingData(PendingDataSize) && PendingDataSize > 0)
        {
            TArray<uint8> Data;
            Data.SetNumUninitialized(FMath::Min<uint32>(PendingDataSize, 65536));

            int32 BytesRead = 0;
            if (!Client.Socket->Recv(Data.GetData(), Data.Num(), BytesRead) || BytesRead <= 0)
            {
                break;
            }

            Data.SetNum(BytesRead);
            Client.Buffer.Append(Data);
        }

        int32 NewlineIndex = INDEX_NONE;
        if (Client.Buffer.Find(static_cast<uint8>('\n'), NewlineIndex))
        {
            TArray<uint8> LineBytes;
            LineBytes.Append(Client.Buffer.GetData(), NewlineIndex);
            LineBytes.Add(0);

            const FString JsonLine = FString(UTF8_TO_TCHAR(reinterpret_cast<const char*>(LineBytes.GetData()))).TrimStartAndEnd();
            ProcessLine(JsonLine, Client.Socket);
            CloseClient(Index);
        }
    }
}

void FSocketTaskServer::ProcessLine(const FString& JsonLine, FSocket* ClientSocket)
{
    FAutomationTaskRequest Request;
    FAutomationTaskResult Result;

    if (FAutomationProtocolJson::ParseRequest(JsonLine, Request, Result))
    {
        if (TSharedPtr<FEditorAutomationApplicationService> Service = ApplicationService.Pin())
        {
            Result = Service->ExecuteTask(Request);
        }
        else
        {
            Result.AddError(TEXT("SocketServerUnavailable"), TEXT("Application service is unavailable."));
        }
    }

    FString ResponseText;
    FAutomationProtocolJson::SerializeResult(Result, ResponseText);
    SendResponse(ClientSocket, ResponseText + TEXT("\n"));
}

void FSocketTaskServer::SendResponse(FSocket* ClientSocket, const FString& ResponseText)
{
    if (!ClientSocket)
    {
        return;
    }

    FTCHARToUTF8 Utf8Text(*ResponseText);
    int32 BytesSent = 0;
    ClientSocket->Send(reinterpret_cast<const uint8*>(Utf8Text.Get()), Utf8Text.Length(), BytesSent);
}

void FSocketTaskServer::CloseClient(int32 Index)
{
    if (!Clients.IsValidIndex(Index))
    {
        return;
    }

    if (Clients[Index].Socket)
    {
        Clients[Index].Socket->Close();
        ISocketSubsystem::Get(PLATFORM_SOCKETSUBSYSTEM)->DestroySocket(Clients[Index].Socket);
    }

    Clients.RemoveAt(Index);
}
