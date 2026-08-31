#include "CPickupAuthorityManager.h"
#include "network/packet_handler.h"
#include "network/packet_types.h"
#include "network/packets/pickups.h"
#include "stdafx.h"

PACKET_HANDLER(ePacketType::PICKUP_SPAWN, Packets::Pickups::PickupSpawn* packet, CNetworkPlayer* sender)
{
    CPickupAuthorityManager::HandleSpawn(sender, *packet);
}

PACKET_HANDLER(ePacketType::PICKUP_STATE, Packets::Pickups::PickupState* packet, CNetworkPlayer* sender)
{
    CPickupAuthorityManager::HandleState(sender, *packet);
}

PACKET_HANDLER(ePacketType::PICKUP_REMOVE, Packets::Pickups::PickupRemove* packet, CNetworkPlayer* sender)
{
    CPickupAuthorityManager::HandleRemove(sender, *packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_REQUEST,
    Packets::Pickups::PickupCollectRequest* packet, CNetworkPlayer* sender)
{
    CPickupAuthorityManager::HandleCollectRequest(sender, *packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_DECISION,
    Packets::Pickups::PickupCollectDecision* packet, CNetworkPlayer* sender)
{
    CPickupAuthorityManager::HandleCollectDecision(sender, *packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_FORWARD, Packets::Pickups::PickupCollectForward*, CNetworkPlayer*) {}
PACKET_HANDLER(ePacketType::PICKUP_COLLECT_RESULT, Packets::Pickups::PickupCollectResult*, CNetworkPlayer*) {}
PACKET_HANDLER(ePacketType::PICKUP_SNAPSHOT_CHUNK,
    Packets::Pickups::PickupSnapshotChunk* packet, CNetworkPlayer* sender)
{
    CPickupAuthorityManager::HandleProgressSnapshot(sender, *packet);
}
