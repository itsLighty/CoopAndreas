#include "network/packets/blips.h"
#include "network/packet_types.h"
#include "stdafx.h"
#include "CMissionSessionServer.h"

namespace
{
bool HasMissionUiAuthority(const CNetworkPlayer* pNetworkPlayer)
{
    if (CMissionSessionServer::GetState().IsActive())
    {
        return CMissionSessionServer::IsAuthoritativeHost(pNetworkPlayer);
    }

    // Preserve the legacy host relay outside an authoritative mission session.
    return pNetworkPlayer != nullptr && pNetworkPlayer->m_bIsHost;
}

bool IsAllowedMissionTarget(const CNetworkPlayer* pNetworkPlayer)
{
    return pNetworkPlayer != nullptr &&
           (!CMissionSessionServer::GetState().IsActive() ||
               CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer));
}

template <typename PacketType>
void ForwardTargetedMissionEffect(PacketType& packet, CNetworkPlayer* pNetworkPlayer)
{
    if (!HasMissionUiAuthority(pNetworkPlayer))
    {
        logger::warn("%s tried to send a targeted blip or checkpoint without mission host authority",
            pNetworkPlayer ? pNetworkPlayer->GetName().c_str() : "An unknown player");
        return;
    }

    CNetworkPlayer* pTargetPlayer = CNetworkPlayerManager::GetPlayer(packet.forWhoPlayerId);
    if (!IsAllowedMissionTarget(pTargetPlayer))
    {
        logger::warn("Rejected a blip or checkpoint targeting player %d outside the active gameplay roster",
            packet.forWhoPlayerId);
        return;
    }

    GetPacketFactory().Send(packet, pTargetPlayer);
}

template <typename PacketType>
void SendToMissionRecipients(PacketType& packet, CNetworkPlayer* pNetworkPlayerToIgnore)
{
    if (!CMissionSessionServer::GetState().IsActive())
    {
        GetPacketFactory().SendToAll(packet, pNetworkPlayerToIgnore);
        return;
    }

    for (CNetworkPlayer* pNetworkPlayer : CNetworkPlayerManager::m_pPlayers)
    {
        if (pNetworkPlayer != pNetworkPlayerToIgnore &&
            CMissionSessionServer::IsGameplayParticipant(pNetworkPlayer))
        {
            GetPacketFactory().Send(packet, pNetworkPlayer);
        }
    }
}
}

PACKET_HANDLER(ePacketType::UPDATE_ENTITY_BLIP, Packets::Blips::UpdateEntityBlip* pUpdateEntityBlip, CNetworkPlayer* pNetworkPlayer)
{
    ForwardTargetedMissionEffect(*pUpdateEntityBlip, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::REMOVE_ENTITY_BLIP, Packets::Blips::RemoveEntityBlip* pRemoveEntityBlip, CNetworkPlayer* pNetworkPlayer)
{
    ForwardTargetedMissionEffect(*pRemoveEntityBlip, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::CLEAR_ENTITY_BLIPS, Packets::Blips::ClearEntityBlips* pClearEntityBlips, CNetworkPlayer* pNetworkPlayer)
{
    ForwardTargetedMissionEffect(*pClearEntityBlips, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::UPDATE_CHECKPOINT, Packets::Blips::UpdateCheckpoint* pUpdateCheckpoint, CNetworkPlayer* pNetworkPlayer)
{
    ForwardTargetedMissionEffect(*pUpdateCheckpoint, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::REMOVE_CHECKPOINT, Packets::Blips::RemoveCheckpoint* pRemoveCheckpoint, CNetworkPlayer* pNetworkPlayer)
{
    ForwardTargetedMissionEffect(*pRemoveCheckpoint, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::CREATE_STATIC_BLIP, Packets::Blips::StaticBlipsSnapshot* pCreateStaticBlip, CNetworkPlayer* pNetworkPlayer)
{
    if (!HasMissionUiAuthority(pNetworkPlayer))
    {
        logger::warn("%s tried to send a static blip snapshot without mission host authority",
            pNetworkPlayer ? pNetworkPlayer->GetName().c_str() : "An unknown player");
        return;
    }

    Packets::Blips::g_lastStaticBlipsData = *pCreateStaticBlip;
    Packets::Blips::g_lastStaticBlipsData.serverTime = 0;
    Packets::Blips::g_pLastStaticBlipsOwner = pNetworkPlayer;
    SendToMissionRecipients(*pCreateStaticBlip, pNetworkPlayer);
}
