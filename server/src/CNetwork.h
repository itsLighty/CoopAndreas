#pragma once

#include "network/packet.h"
#include "network/packets/system.h"
#include <unordered_map>
class CNetworkPlayer;
class CNetwork
{
public:
    CNetwork() {}

    static bool Init(unsigned short port);
    static void SendPacketNoAuth_ENet(ENetPeer* pENetPeer, const uint8_t* data, int dataSize,
        ePacketChannel packetChannel, ePacketReliability packetReliability);
    static void SendPacket(CNetworkPlayer* pNetworkPlayer, const uint8_t* data, int dataSize,
        ePacketChannel packetChannel, ePacketReliability packetReliability);
    static void SendPacketToAll(const uint8_t* data, int dataSize, ePacketChannel packetChannel,
        ePacketReliability packetReliability, CNetworkPlayer* pNetworkPlayerToIgnore = nullptr);

    static void HandlePlayerConnected(ENetPeer* peer, Packets::System::PlayerConnected& playerConnected);
    static void HandlePlayerReconnect(ENetPeer* peer, Packets::System::PlayerReconnectRequest& reconnectRequest);
    static void ConfirmReconnectCredential(
        CNetworkPlayer* player, const Packets::System::PlayerReconnectCredentialAck& acknowledgement);
    ~CNetwork() {}

private:
    static void CompletePlayerConnection(
        ENetPeer* peer, const char* name, uint32_t version, int playerId,
        const Packets::System::ReconnectCredential* acceptedReconnectCredential);
    static void HandlePeerConnected(ENetEvent& event);
    static void HandlePlayerDisconnected(ENetEvent& event);
};
