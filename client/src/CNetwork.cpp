#include "CPacketFactory.h"
#include "CPacketBuffer.h"
#include "CEntryExitMarkerSync.h"
#include "CEntryExitTransitionSync.h"
#include "CMissionSessionClient.h"
#include "CNetworkAnimQueue.h"
#include "CNetworkCheckpoint.h"
#include "CNetworkPickupManager.h"
#include "CNetworkFireManager.h"
#include "CStuntJumpSyncManager.h"
#include "CNetworkPedManager.h"
#include "CNetworkPlayerManager.h"
#include "CNetworkVehicleManager.h"
#include "CServerTime.h"
#include "enet/enet.h"
#include "stdafx.h"
#include "CNetworkEntityStreamManager.h"
#include "CPlayerGameplayStateSync.h"
#include "CGangZoneWarSyncManager.h"
#include "../shared/semver.h"
#include <cassert>
#include <windows.h>

DWORD WINAPI CNetwork::InitAsync(LPVOID)
{
    assert(strcmp(m_IpAddress, "") != 0 && "Wrong IP passed");
    assert(m_nPort != 0 && "Wrong Port passed");

    if (enet_initialize() != 0)
    {  // try to init enet
        CChat::AddMessage("{cecedb}[Network] {ff0000}Failed to enet_initialize.");
        return false;
    }
    else
    {
        std::cout << "Success to enet_initialize" << std::endl;
    }

    m_pENetHost = enet_host_create(NULL, 1, (int)ePacketChannel::COUNT, 0, 0);  // create enet client
    if (m_pENetHost == NULL)                           // check client
        return false;

    ENetAddress address;                           // connection address

    enet_address_set_host(&address, m_IpAddress);  // set address ip
    address.port = m_nPort;                        // set address port

    uint32_t packedVersion = semver_parse(COOPANDREAS_VERSION, nullptr); // v0.1.0-alpha compat
    m_pPeer =
        enet_host_connect(m_pENetHost, &address, (int)ePacketChannel::COUNT, packedVersion);  // connect to the server
    if (m_pPeer == NULL)
    {                                                                                         // if not connected
        CChat::AddMessage("{cecedb}[Network] {ff0000}m_pPeer == NULL.");
        // std::cout << "Not Connected" << std::endl;
        return false;
    }

    CChat::AddMessage("{cecedb}[Network] Connecting to the server...");
    ENetEvent event;
    while (!m_bConnected)
    {
        if (enet_host_service(m_pENetHost, &event, 2000) > 0 && event.type == ENET_EVENT_TYPE_CONNECT)
        {
            m_bConnected = true;
            m_bAuthenticated = false;

            CChat::AddMessage("{cecedb}[Network] {00ff00}Successfully {cecedb}connected to the server.");

            if (HasReconnectCredentialForCurrentIdentity())
            {
                Packets::System::PlayerReconnectRequest reconnectRequest{};
                reconnectRequest.requestedPlayerId = ms_nReconnectPlayerId;
                reconnectRequest.version = packedVersion;
                reconnectRequest.credential = ms_reconnectCredential;
                strcpy_s(reconnectRequest.name, CLocalPlayer::m_Name);
                GetPacketFactory().Send(reconnectRequest);
            }
            else
            {
                Packets::System::PlayerConnected connectedPacket{};
                connectedPacket.payload.isAlreadyConnected = false;
                connectedPacket.payload.playerid = -1;
                connectedPacket.payload.version = packedVersion;
                strcpy_s(connectedPacket.payload.name, CLocalPlayer::m_Name);
                GetPacketFactory().Send(connectedPacket);
            }
        }
        else
        {
            // enet_peer_reset(m_pPeer);
            CChat::AddMessage("{cecedb}[Network] Failed to connect. Retrying...");
        }
    }

    return true;
}

void CNetwork::ProcessReceive()
{
#if true  // ENET LAYER
    ENetEvent eNetEvent{};
    while (m_bConnected && enet_host_service(m_pENetHost, &eNetEvent, 0) > 0)
    {
        switch (eNetEvent.type)
        {
            case ENET_EVENT_TYPE_RECEIVE:
            {
                ms_nBytesReceivedThisSecondCounter += eNetEvent.packet->dataLength;
                
                GetPacketFactory().Receive(eNetEvent.packet->data, eNetEvent.packet->dataLength);

                enet_packet_destroy(eNetEvent.packet);
                break;
            }
            case ENET_EVENT_TYPE_DISCONNECT:
            {
                //assert(false && "TODO");
                CNetwork::Disconnect();
            }
            case ENET_EVENT_TYPE_NONE:
            case ENET_EVENT_TYPE_CONNECT:
                break;
        }
    }
#endif
}

void CNetwork::SendPacket(
    const uint8_t* data, int dataSize, ePacketChannel packetChannel, ePacketReliability packetReliability)
{
    if (!CNetwork::m_bConnected)
    {
#if DEBUG
        if (data != nullptr)
        {
            std::cerr << "PacketType #" << std::to_string(*reinterpret_cast<const uint16_t*>(data))
                      << " was sent while not connected";
        }
#endif
        return;
    }

#if true  // ENET LAYER
    uint32_t eNetPacketFlags = 0;
    if (packetReliability == ePacketReliability::RELIABLE)
    {
        eNetPacketFlags |= ENET_PACKET_FLAG_RELIABLE;
    }
    else if (packetReliability == ePacketReliability::UNRELIABLE)
    {
        // ...
    }

    ENetPacket* pENetPacket = enet_packet_create(data, dataSize, eNetPacketFlags);
    if (pENetPacket != nullptr)
    {
        enet_peer_send(m_pPeer, static_cast<uint8_t>(packetChannel), pENetPacket);
        ms_nBytesSentThisSecondCounter += dataSize;
    }
#endif
}

void CNetwork::Disconnect()
{
    const bool wasConnected = m_bConnected;
    const bool wasAuthenticated = m_bAuthenticated;
    m_bConnected = false;
    if (wasConnected && m_pPeer != nullptr)
    {
        enet_peer_disconnect_now(m_pPeer, 0);
    }
    ResetConnectionState();
    if (wasAuthenticated)
    {
        CPatch::TemporaryPatches();
    }
}

void CNetwork::DestroyTransport()
{
    if (m_pENetHost != nullptr)
    {
        enet_host_destroy(m_pENetHost);
        m_pENetHost = nullptr;
        enet_deinitialize();
    }
    m_pPeer = nullptr;
}

void CNetwork::StoreReconnectCredential(const Packets::System::PlayerReconnectCredential& packet)
{
    if (packet.playerId != CNetworkPlayerManager::m_nMyId)
    {
        logger::warn("Ignored a reconnect credential for unexpected player ID %d", packet.playerId);
        return;
    }

    ms_bHasReconnectCredential = true;
    ms_nReconnectPlayerId = packet.playerId;
    ms_reconnectCredential = packet.credential;
    strcpy_s(ms_reconnectIpAddress, m_IpAddress);
    ms_nReconnectPort = m_nPort;
    strcpy_s(ms_reconnectPlayerName, CLocalPlayer::m_Name);

    Packets::System::PlayerReconnectCredentialAck acknowledgement{};
    acknowledgement.playerId = packet.playerId;
    acknowledgement.credential = packet.credential;
    GetPacketFactory().Send(acknowledgement);
}

void CNetwork::ClearReconnectCredential()
{
    ms_bHasReconnectCredential = false;
    ms_nReconnectPlayerId = -1;
    ms_reconnectCredential.fill(0);
    ms_reconnectIpAddress[0] = '\0';
    ms_nReconnectPort = 0;
    ms_reconnectPlayerName[0] = '\0';
}

bool CNetwork::HasReconnectCredentialForCurrentIdentity()
{
    if (!ms_bHasReconnectCredential || ms_nReconnectPlayerId < 0 ||
        ms_nReconnectPlayerId >= Config::MAX_SERVER_PLAYERS)
    {
        return false;
    }
    if (strcmp(ms_reconnectIpAddress, m_IpAddress) != 0 || ms_nReconnectPort != m_nPort ||
        strcmp(ms_reconnectPlayerName, CLocalPlayer::m_Name) != 0)
    {
        ClearReconnectCredential();
        return false;
    }
    return true;
}

void CNetwork::ResetConnectionState()
{
    m_bAuthenticated = false;
    CLocalPlayer::m_bIsHost = false;

    // Cancel work that points at remote entities before deleting those entities.
    CEntryExitTransitionSync::Reset();
    CStuntJumpSyncManager::ResetNetworkState();
    GetPacketBuffer().Clear();
    CNetworkAnimQueue::Clear();
    CMissionSessionClient::Reset();
    CStatsSync::ResetNetworkState();
    CPlayerGameplayStateSync::ResetNetworkState();
    CGangZoneWarSyncManager::ResetNetworkState();
    CNetworkPickupManager::ResetNetworkState();
    CNetworkFireManager::ResetNetworkState();

    CNetworkPedManager::Clear();
    CNetworkPlayerManager::Clear();
    CNetworkVehicleManager::Clear();
    CNetworkEntityStreamManager::Reset();

    CNetworkCheckpoint::Remove();
    CEntryExitMarkerSync::ResetNetworkState();
    GetPacketFactory().ClearRecords();
    CServerTime::Reset();

    ms_nBytesReceivedThisSecond = 0;
    ms_nBytesReceivedThisSecondCounter = 0;
    ms_nBytesSentThisSecond = 0;
    ms_nBytesSentThisSecondCounter = 0;

}
