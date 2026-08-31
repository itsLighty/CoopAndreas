#include "stdafx.h"
#include "CStuntJumpAuthorityManager.h"
#include "network/packets/stunts.h"

PACKET_HANDLER(ePacketType::STUNT_DEFINITION, Packets::Stunts::StuntDefinitionAnnounce* packet,
    CNetworkPlayer* player)
{
    CStuntJumpAuthorityManager::HandleDefinition(player, *packet);
}

PACKET_HANDLER(ePacketType::STUNT_ATTEMPT, Packets::Stunts::StuntAttempt* packet,
    CNetworkPlayer* player)
{
    packet->playerId.value = player->m_iPlayerId;
    CStuntJumpAuthorityManager::HandleAttempt(player, *packet);
}

PACKET_HANDLER(ePacketType::STUNT_STATE, Packets::Stunts::StuntStateEvent*, CNetworkPlayer* player)
{
    logger::warn("%s tried to publish server-authoritative stunt state", player->GetName().c_str());
}

PACKET_HANDLER(
    ePacketType::STUNT_ATTEMPT_RESULT, Packets::Stunts::StuntAttemptResult*, CNetworkPlayer* player)
{
    logger::warn("%s tried to publish a server-only stunt attempt result", player->GetName().c_str());
}
