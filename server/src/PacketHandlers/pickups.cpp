#include "stdafx.h"
#include "CPickupAuthorityManager.h"
#include "network/packets/pickups.h"

PACKET_HANDLER(ePacketType::PICKUP_STATE, Packets::Pickups::PickupStateEvent* packet,
    CNetworkPlayer* player)
{
    CPickupAuthorityManager::HandleState(player, *packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_REQUEST, Packets::Pickups::PickupCollectRequest* packet,
    CNetworkPlayer* player)
{
    CPickupAuthorityManager::HandleCollectRequest(player, *packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_DECISION, Packets::Pickups::PickupCollectDecision* packet,
    CNetworkPlayer* player)
{
    CPickupAuthorityManager::HandleCollectDecision(player, *packet);
}

PACKET_HANDLER(ePacketType::PICKUP_CREATE_INTENT, Packets::Pickups::PickupCreateIntent* packet,
    CNetworkPlayer* player)
{
    CPickupAuthorityManager::HandleCreateIntent(player, *packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_RESULT, Packets::Pickups::PickupCollectResult*,
    CNetworkPlayer* player)
{
    logger::warn("%s tried to publish a server-authoritative pickup result", player->GetName().c_str());
}
