#include "CNetworkPlayerManager.h"
#include "CMissionSessionServer.h"
#include "CCutsceneVoteManager.h"
#include "CGangZoneWarAuthorityManager.h"
#include "CPickupAuthorityManager.h"
#include "CStuntJumpAuthorityManager.h"
#include "CFireAuthorityManager.h"
#include "CCheatAuthorityManager.h"
#include "CRTTBroadcastManager.h"
#include "CPacketFactory.h"
#include "logger.h"
#include "network/packet.h"
#include "network/packets/system.h"
#include "serialize.h"
#include "stdafx.h"
#include <network/packets/vehicles.h>
#include <network/packets/peds.h>
#include <network/packets/scripts.h>
#include <network/packets/blips.h>
#include <array>
#include <cerrno>
#include <cstring>

#ifdef _WIN32
#include <bcrypt.h>
#else
#include <sys/random.h>
#endif

namespace
{
using Packets::System::ReconnectCredential;

struct ReconnectIdentity
{
    bool valid = false;
    ReconnectCredential credential{};
    bool previousCredentialValid = false;
    ReconnectCredential previousCredential{};
    char playerName[Config::MAX_NICKNAME_LENGTH + 1]{};
};

std::array<ReconnectIdentity, Config::MAX_SERVER_PLAYERS> g_reconnectIdentities{};

bool GenerateReconnectCredential(ReconnectCredential& credential)
{
#ifdef _WIN32
    return BCryptGenRandom(nullptr, credential.data(), static_cast<ULONG>(credential.size()),
               BCRYPT_USE_SYSTEM_PREFERRED_RNG) == 0;
#else
    size_t generated = 0;
    while (generated < credential.size())
    {
        const ssize_t result = getrandom(credential.data() + generated, credential.size() - generated, 0);
        if (result > 0)
        {
            generated += static_cast<size_t>(result);
            continue;
        }
        if (result < 0 && errno == EINTR)
        {
            continue;
        }
        return false;
    }
    return true;
#endif
}

bool CredentialsEqual(const ReconnectCredential& left, const ReconnectCredential& right)
{
    volatile uint8_t difference = 0;
    for (size_t i = 0; i < left.size(); ++i)
    {
        difference = static_cast<uint8_t>(difference | (left[i] ^ right[i]));
    }
    return difference == 0;
}

void InvalidateReconnectIdentity(int playerId)
{
    if (playerId >= 0 && playerId < Config::MAX_SERVER_PLAYERS)
    {
        g_reconnectIdentities[playerId] = {};
    }
}

void InvalidateUnreservedDisconnectedIdentities()
{
    const auto& missionState = CMissionSessionServer::GetState();
    for (int playerId = 0; playerId < Config::MAX_SERVER_PLAYERS; ++playerId)
    {
        if (!g_reconnectIdentities[playerId].valid || CNetworkPlayerManager::GetPlayer(playerId) != nullptr)
        {
            continue;
        }
        if (!missionState.IsActive() || !missionState.ContainsParticipant(playerId))
        {
            InvalidateReconnectIdentity(playerId);
        }
    }
}

bool IsNameTaken(const char* name)
{
#ifndef _DEBUG
    for (const auto* player : CNetworkPlayerManager::m_pPlayers)
    {
        if (strncmp(player->m_Name, name, sizeof(player->m_Name)) == 0)
        {
            return true;
        }
    }
#endif
    return false;
}

void RejectConnection(ENetPeer* peer, uint8_t reason, uint32_t version = 0)
{
    Packets::System::PlayerDisconnected disconnected{};
    disconnected.payload.playerid = -1;
    disconnected.payload.reason = reason;
    disconnected.payload.version = version;
    GetPacketFactory().SendPacketNoAuth_ENet(disconnected, peer);
    enet_peer_disconnect_later(peer, version);
}

bool ValidateClientVersion(ENetPeer* peer, uint32_t clientVersion)
{
    const uint32_t packedVersion = semver_parse(COOPANDREAS_VERSION, nullptr);
    if (packedVersion == clientVersion)
    {
        return true;
    }
    RejectConnection(peer, Packets::System::PlayerDisconnected::DISCONNECTION_REASON_VERSION_MISMATCH, packedVersion);
    return false;
}

int FindOrdinaryPlayerId()
{
    const auto& missionState = CMissionSessionServer::GetState();
    for (int playerId = 0; playerId < Config::MAX_SERVER_PLAYERS; ++playerId)
    {
        if (CNetworkPlayerManager::GetPlayer(playerId) == nullptr &&
            (!missionState.IsActive() || !missionState.ContainsParticipant(playerId)))
        {
            return playerId;
        }
    }
    return -1;
}

bool CanReclaimFrozenIdentity(const Packets::System::PlayerReconnectRequest& request)
{
    const int playerId = request.requestedPlayerId;
    if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS ||
        CNetworkPlayerManager::GetPlayer(playerId) != nullptr)
    {
        return false;
    }

    const auto& missionState = CMissionSessionServer::GetState();
    const ReconnectIdentity& identity = g_reconnectIdentities[playerId];
    const bool currentCredentialMatches = CredentialsEqual(identity.credential, request.credential);
    const bool previousCredentialMatches = CredentialsEqual(identity.previousCredential, request.credential);
    return missionState.IsActive() && missionState.ContainsParticipant(playerId) && identity.valid &&
           strncmp(identity.playerName, request.name, sizeof(identity.playerName)) == 0 &&
           (currentCredentialMatches | (identity.previousCredentialValid && previousCredentialMatches));
}
}  // namespace

bool CNetwork::Init(unsigned short port)
{
    if (enet_initialize() != 0)  // try to init enet
    {
        printf("[ERROR] : ENET_INIT FAILED TO INITIALIZE\n");
        return false;
    }

    ENetAddress address;

    address.host = ENET_HOST_ANY;  // bind server ip
    address.port = port;           // bind server port

    // TODO: `ConfigManager::GetConfigMaxPlayers`
    ENetHost* pENetHost =
        enet_host_create(&address, Config::MAX_SERVER_PLAYERS, (int)ePacketChannel::COUNT, 0, 0);  // create enet host

    if (pENetHost == nullptr)
    {
        printf("[ERROR] : ENET_UDP_SERVER_SOCKET FAILED TO CREATE\n");
        return false;
    }

    printf("[!] : Server started on port %d\n", port);

    ENetEvent eNetEvent{};
    while (true)  // waiting for event
    {
        CServerTime::Update();
        CPickupAuthorityManager::Update();
        CStuntJumpAuthorityManager::Update();
        CFireAuthorityManager::Update();
        CRTTBroadcastManager::Update();

        while (enet_host_service(pENetHost, &eNetEvent, 1) > 0)
        {
            switch (eNetEvent.type)
            {
                case ENET_EVENT_TYPE_CONNECT:
                {
                    CNetwork::HandlePeerConnected(eNetEvent);
                    break;
                }
                case ENET_EVENT_TYPE_RECEIVE:
                {
                    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(eNetEvent.peer);
                    if (pNetworkPlayer != nullptr)
                    {
                        GetPacketFactory().Receive(
                            eNetEvent.packet->data, eNetEvent.packet->dataLength, pNetworkPlayer);
                    }
                    else
                    {
                        GetPacketFactory().ReceivePacketNoAuth_ENet(
                            eNetEvent.packet->data, eNetEvent.packet->dataLength, eNetEvent.peer);
                    }

                    enet_packet_destroy(eNetEvent.packet);
                    break;
                }
                case ENET_EVENT_TYPE_DISCONNECT:
                {
                    CNetwork::HandlePlayerDisconnected(eNetEvent);
                    break;
                }
                case ENET_EVENT_TYPE_NONE:
                    break;
            }
        }
    }

    enet_host_destroy(pENetHost);
    enet_deinitialize();
    printf("[!] : Server Shutdown (ENET_DEINITIALIZE)\n");
    return 0;
}

void CNetwork::HandlePeerConnected(ENetEvent& event)
{
    printf("[Game] : A new client connected from %i.%i.%i.%i:%u.\n", event.peer->address.host & 0xFF,
        (event.peer->address.host >> 8) & 0xFF, (event.peer->address.host >> 16) & 0xFF,
        (event.peer->address.host >> 24) & 0xFF, event.peer->address.port);

    enet_peer_timeout(event.peer, 0, 30000, 60000);  // timeoutLimit, timeoutMinimum, timeoutMaximum
}

void CNetwork::HandlePlayerDisconnected(ENetEvent& event)
{
    CNetworkPlayer* pNetworkPlayer = CNetworkPlayerManager::GetPlayer(event.peer);

    if (pNetworkPlayer == nullptr)
    {
        return;
    }

    CMissionSessionServer::HandlePlayerDisconnected(pNetworkPlayer);
    CPickupAuthorityManager::HandlePlayerDisconnected(pNetworkPlayer);
    CStuntJumpAuthorityManager::HandlePlayerDisconnected(pNetworkPlayer);
    CFireAuthorityManager::HandlePlayerDisconnected(pNetworkPlayer);
    CCheatAuthorityManager::HandlePlayerDisconnected(pNetworkPlayer);

    const int disconnectedPlayerId = pNetworkPlayer->m_iPlayerId;
    const auto& missionState = CMissionSessionServer::GetState();
    if (!missionState.IsActive() || !missionState.ContainsParticipant(disconnectedPlayerId))
    {
        InvalidateReconnectIdentity(disconnectedPlayerId);
    }

    CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(pNetworkPlayer->m_nVehicleId);

    if (vehicle != nullptr && pNetworkPlayer->m_nSeatId >= 0 &&
        pNetworkPlayer->m_nSeatId < static_cast<int8_t>(ARRAY_SIZE(vehicle->m_pPlayers)))
    {
        vehicle->m_pPlayers[pNetworkPlayer->m_nSeatId] = nullptr;
    }

    if (Packets::Scripts::g_pLastEnExPlayerOwner == pNetworkPlayer)
    {
        Packets::Scripts::g_pLastEnExPlayerOwner = nullptr;
    }
    if (Packets::Blips::g_pLastStaticBlipsOwner == pNetworkPlayer)
    {
        Packets::Blips::g_pLastStaticBlipsOwner = nullptr;
    }

    CNetworkPedManager::RemoveAllHostedAndNotify(pNetworkPlayer);
    CNetworkVehicleManager::RemoveAllHostedAndNotify(pNetworkPlayer);

    CNetworkPlayerManager::Remove(pNetworkPlayer);

    Packets::System::PlayerDisconnected playerDisconnected{};
    playerDisconnected.payload.playerid = disconnectedPlayerId;
    playerDisconnected.payload.reason = Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NOTHING;
    GetPacketFactory().SendToAll(playerDisconnected);

    printf("[Game] : %i Disconnected.\n", disconnectedPlayerId);

    delete pNetworkPlayer;

    CNetworkPlayerManager::AssignHostToFirstPlayer();
}

void CNetwork::HandlePlayerConnected(ENetPeer* pENetPeer, Packets::System::PlayerConnected& playerConnected)
{
    InvalidateUnreservedDisconnectedIdentities();
    if (!ValidateClientVersion(pENetPeer, playerConnected.payload.version))
    {
        return;
    }
    if (IsNameTaken(playerConnected.payload.name))
    {
        RejectConnection(pENetPeer, Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NAME_TAKEN);
        return;
    }

    const int freeId = FindOrdinaryPlayerId();
    if (freeId < 0)
    {
        logger::warn("Rejected a connection because no non-reserved player ID is available");
        RejectConnection(pENetPeer, Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NOTHING);
        return;
    }

    CompletePlayerConnection(
        pENetPeer, playerConnected.payload.name, playerConnected.payload.version, freeId, nullptr);
}

void CNetwork::HandlePlayerReconnect(
    ENetPeer* pENetPeer, Packets::System::PlayerReconnectRequest& reconnectRequest)
{
    InvalidateUnreservedDisconnectedIdentities();
    if (!ValidateClientVersion(pENetPeer, reconnectRequest.version))
    {
        return;
    }

    if (IsNameTaken(reconnectRequest.name))
    {
        RejectConnection(pENetPeer, Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NAME_TAKEN);
        return;
    }

    const bool reclaimedIdentity = CanReclaimFrozenIdentity(reconnectRequest);
    const int playerId = reclaimedIdentity ? reconnectRequest.requestedPlayerId : FindOrdinaryPlayerId();
    if (playerId < 0)
    {
        logger::warn("Rejected an invalid reconnect claim because no non-reserved player ID is available");
        RejectConnection(pENetPeer, Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NOTHING);
        return;
    }

    if (!reclaimedIdentity)
    {
        logger::warn("Rejected a reconnect claim for reserved player ID %d; assigning an ordinary ID instead",
            reconnectRequest.requestedPlayerId);
    }
    CompletePlayerConnection(pENetPeer, reconnectRequest.name, reconnectRequest.version, playerId,
        reclaimedIdentity ? &reconnectRequest.credential : nullptr);
}

void CNetwork::CompletePlayerConnection(
    ENetPeer* pENetPeer, const char* name, uint32_t version, int freeId,
    const ReconnectCredential* acceptedReconnectCredential)
{
    ReconnectCredential nextCredential{};
    if (!GenerateReconnectCredential(nextCredential))
    {
        logger::error("Could not obtain cryptographically secure randomness for a reconnect credential");
        RejectConnection(pENetPeer, Packets::System::PlayerDisconnected::DISCONNECTION_REASON_NOTHING);
        return;
    }

    ReconnectIdentity& identity = g_reconnectIdentities[freeId];
    identity = {};
    identity.valid = true;
    identity.credential = nextCredential;
    if (acceptedReconnectCredential != nullptr)
    {
        identity.previousCredentialValid = true;
        identity.previousCredential = *acceptedReconnectCredential;
    }
    snprintf(identity.playerName, sizeof(identity.playerName), "%s", name);

    CNetworkPlayer* pNewNetworkPlayer = new CNetworkPlayer(pENetPeer, freeId);
    snprintf(pNewNetworkPlayer->m_Name, sizeof(pNewNetworkPlayer->m_Name), "%s", name);
    CNetworkPlayerManager::Add(pNewNetworkPlayer);

    char buffer[23];
    semver_t playerVersion;
    semver_unpack(version, &playerVersion);
    semver_to_string(&playerVersion, buffer, sizeof(buffer));
    buffer[22] = '\0';
    logger::info("playerId %d name %s version %s%s", freeId, name, buffer,
        acceptedReconnectCredential != nullptr ? " (reclaimed frozen identity)" : "");

    // Send the NEW player TO OLD players
    Packets::System::PlayerConnected playerConnected{};
    playerConnected.payload.playerid = freeId;
    playerConnected.payload.isAlreadyConnected = false;
    playerConnected.payload.version = version;
    snprintf(playerConnected.payload.name, sizeof(playerConnected.payload.name), "%s", name);
    GetPacketFactory().SendToAll(playerConnected, pNewNetworkPlayer);

    // Let the new player know his id
    Packets::System::PlayerHandshake playerHandshake{};
    playerHandshake.yourid = freeId;
    GetPacketFactory().Send(playerHandshake, pNewNetworkPlayer);

    // The credential is sent separately to preserve the legacy handshake wire layout. A reclaimed connection
    // temporarily accepts the prior credential until its first authenticated packet proves delivery of this one.
    Packets::System::PlayerReconnectCredential reconnectCredential{};
    reconnectCredential.playerId = freeId;
    reconnectCredential.credential = nextCredential;
    GetPacketFactory().Send(reconnectCredential, pNewNetworkPlayer);

    // Send OLD players TO the NEW one
    Packets::System::PlayerConnected oldPlayerConnected{};
    oldPlayerConnected.payload.isAlreadyConnected = true;

    for (auto* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer->m_iPlayerId == freeId)
        {
            continue;
        }
        oldPlayerConnected.payload.playerid = pNetworkPlayer->m_iPlayerId;
        snprintf(oldPlayerConnected.payload.name, sizeof(oldPlayerConnected.payload.name), "%s",
            pNetworkPlayer->m_Name);
        GetPacketFactory().Send(oldPlayerConnected, pNewNetworkPlayer);
    }

    const auto& missionState = CMissionSessionServer::GetState();
    const bool mayReceiveGameplayState =
        !missionState.IsActive() || missionState.ContainsGameplayParticipant(freeId);

    for (auto i : CNetworkPlayerManager::m_pPlayers)
    {
        if (i->m_iPlayerId == freeId)
            continue;

        const bool sourceMayPublishGameplayState =
            !missionState.IsActive() || missionState.ContainsGameplayParticipant(i->m_iPlayerId);

        if (i->m_ucSyncFlags.bStatsModified && mayReceiveGameplayState && sourceMayPublishGameplayState)
        {
            Packets::Players::PlayerStats statsPacket{};
            statsPacket.playerid = i->m_iPlayerId;
            for (size_t j = 0; j < ARRAY_SIZE(i->m_afStats); j++)
            {
                statsPacket.stats[j] = i->m_afStats[j];
            }
            GetPacketFactory().Send(statsPacket, pNewNetworkPlayer);
        }

        if (i->m_ucSyncFlags.bGameplayStateModified && mayReceiveGameplayState &&
            sourceMayPublishGameplayState)
        {
            Packets::Players::PlayerGameplayState gameplayState = i->m_gameplayState;
            gameplayState.playerid.value = i->m_iPlayerId;
            GetPacketFactory().Send(gameplayState, pNewNetworkPlayer);
        }

        if (i->m_ucSyncFlags.bClothesModified)
        {
            Packets::Players::RebuildPlayer rebuildPacket{};
            rebuildPacket.playerid = i->m_iPlayerId;
            rebuildPacket.clothesDesc = i->m_pPedClothesDesc;
            GetPacketFactory().Send(rebuildPacket, pNewNetworkPlayer);
        }

        if (i->m_waypointState.place)
        {
            i->m_waypointState.playerid = i->m_iPlayerId;
            GetPacketFactory().Send(i->m_waypointState, pNewNetworkPlayer);
        }
    }

    for (auto i : CNetworkVehicleManager::m_pVehicles)
    {
        Packets::Vehicles::VehicleSpawn vehicleSpawnPacket{};
        vehicleSpawnPacket.vehicleid = i->m_nVehicleId;
        vehicleSpawnPacket.modelid = i->m_nModelId;
        vehicleSpawnPacket.pos = i->m_vecPosition;
        vehicleSpawnPacket.rot = static_cast<float>(
            i->m_vecRotation.z * (3.141592f / 180.0f));  // convert to radians TODO(v0.3.1-alpha): is this wrong?
        vehicleSpawnPacket.color1 = i->m_nPrimaryColor;
        vehicleSpawnPacket.color2 = i->m_nSecondaryColor;
        GetPacketFactory().Send(vehicleSpawnPacket, pNewNetworkPlayer);

        Packets::Vehicles::VehicleDamage vehicleDamagePacket{};
        vehicleDamagePacket.vehicleid = i->m_nVehicleId;
        vehicleDamagePacket.damageManager = i->m_damageManager;
        GetPacketFactory().Send(vehicleDamagePacket, pNewNetworkPlayer);

        for (int component : i->m_pComponents)
        {
            Packets::Vehicles::VehicleComponentAdd vehicleComponentAdd{};
            vehicleComponentAdd.vehicleid = i->m_nVehicleId;
            vehicleComponentAdd.componentid = component;
            GetPacketFactory().Send(vehicleComponentAdd, pNewNetworkPlayer);
        }
    }

    for (auto i : CNetworkPedManager::m_pPeds)
    {
        Packets::Peds::PedSpawn packet{};
        packet.pedid = i->m_nPedId;
        packet.modelId = i->m_nModelId;
        packet.pos = i->m_vecPos;
        packet.pedType = i->m_nPedType;
        packet.createdBy = i->m_nCreatedBy;
        snprintf(packet.specialModelName, sizeof(packet.specialModelName), "%s", i->m_szSpecialModelName);
        GetPacketFactory().Send(packet, pNewNetworkPlayer);
    }

    // Enqueue authoritative mission classification before considering cached mission-world state. Active-session
    // spectators never receive the cached EnEx packet, so cross-channel delivery cannot expose it to them first.
    CMissionSessionServer::SendSnapshot(pNewNetworkPlayer);
    CCutsceneVoteManager::SendSnapshot(pNewNetworkPlayer);
    CGangZoneWarAuthorityManager::SendSnapshot(pNewNetworkPlayer);
    CPickupAuthorityManager::SendActiveStates(pNewNetworkPlayer);
    CStuntJumpAuthorityManager::SendSnapshot(pNewNetworkPlayer);
    CFireAuthorityManager::SendSnapshot(pNewNetworkPlayer);
    CCheatAuthorityManager::SendSnapshot(pNewNetworkPlayer);

    const bool mayReceiveCachedEnEx = !missionState.IsActive() || missionState.ContainsGameplayParticipant(freeId);
    if (mayReceiveCachedEnEx && Packets::Scripts::g_pLastEnExPlayerOwner)
    {
        if (std::find(CNetworkPlayerManager::m_pPlayers.begin(), CNetworkPlayerManager::m_pPlayers.end(),
                Packets::Scripts::g_pLastEnExPlayerOwner) != CNetworkPlayerManager::m_pPlayers.end())
        {
            GetPacketFactory().Send(Packets::Scripts::g_lastEnExData, pNewNetworkPlayer);
        }
    }

    if (mayReceiveCachedEnEx && Packets::Blips::g_pLastStaticBlipsOwner)
    {
        if (std::find(CNetworkPlayerManager::m_pPlayers.begin(), CNetworkPlayerManager::m_pPlayers.end(),
                Packets::Blips::g_pLastStaticBlipsOwner) != CNetworkPlayerManager::m_pPlayers.end())
        {
            GetPacketFactory().Send(Packets::Blips::g_lastStaticBlipsData, pNewNetworkPlayer);
        }
    }

    CNetworkPlayerManager::AssignHostToFirstPlayer();
}

void CNetwork::ConfirmReconnectCredential(
    CNetworkPlayer* pNetworkPlayer, const Packets::System::PlayerReconnectCredentialAck& acknowledgement)
{
    if (pNetworkPlayer == nullptr || pNetworkPlayer->m_iPlayerId < 0 ||
        pNetworkPlayer->m_iPlayerId >= Config::MAX_SERVER_PLAYERS ||
        acknowledgement.playerId != pNetworkPlayer->m_iPlayerId)
    {
        return;
    }

    ReconnectIdentity& identity = g_reconnectIdentities[pNetworkPlayer->m_iPlayerId];
    if (!identity.valid || !CredentialsEqual(identity.credential, acknowledgement.credential))
    {
        logger::warn("%s sent an invalid reconnect credential acknowledgement",
            pNetworkPlayer->GetName().c_str());
        return;
    }
    identity.previousCredentialValid = false;
    identity.previousCredential.fill(0);
}

void CNetwork::SendPacketNoAuth_ENet(ENetPeer* pENetPeer, const uint8_t* data, int dataSize,
    ePacketChannel packetChannel, ePacketReliability packetReliability)
{
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
        enet_peer_send(pENetPeer, static_cast<uint8_t>(packetChannel), pENetPacket);
    }
}

void CNetwork::SendPacket(CNetworkPlayer* pNetworkPlayer, const uint8_t* data, int dataSize,
    ePacketChannel packetChannel, ePacketReliability packetReliability)
{
    assert(pNetworkPlayer != nullptr);

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
        enet_peer_send(pNetworkPlayer->m_pPeer, static_cast<uint8_t>(packetChannel), pENetPacket);
    }
}

void CNetwork::SendPacketToAll(const uint8_t* data, int dataSize, ePacketChannel packetChannel,
    ePacketReliability packetReliability, CNetworkPlayer* pNetworkPlayerToIgnore)
{
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
        for (auto* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
        {
            if (pNetworkPlayer != pNetworkPlayerToIgnore)
            {
                enet_peer_send(pNetworkPlayer->m_pPeer, static_cast<uint8_t>(packetChannel), pENetPacket);
            }
        }
    }
}
