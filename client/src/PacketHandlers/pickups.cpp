#include "CNetworkPickupManager.h"
#include "network/packet_handler.h"
#include "network/packet_types.h"
#include "network/packets/pickups.h"
#include "stdafx.h"

PACKET_HANDLER(ePacketType::PICKUP_SPAWN, Packets::Pickups::PickupSpawn* packet)
{
    CNetworkPickupManager::HandleSpawn(*packet);
}

PACKET_HANDLER(ePacketType::PICKUP_STATE, Packets::Pickups::PickupState* packet)
{
    CNetworkPickupManager::HandleState(*packet);
}

PACKET_HANDLER(ePacketType::PICKUP_REMOVE, Packets::Pickups::PickupRemove* packet)
{
    CNetworkPickupManager::HandleRemove(*packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_FORWARD, Packets::Pickups::PickupCollectForward* packet)
{
    CNetworkPickupManager::HandleCollectForward(*packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_RESULT, Packets::Pickups::PickupCollectResult* packet)
{
    CNetworkPickupManager::HandleCollectResult(*packet);
}

PACKET_HANDLER(ePacketType::PICKUP_SNAPSHOT_CHUNK, Packets::Pickups::PickupSnapshotChunk* packet)
{
    CNetworkPickupManager::HandleSnapshotChunk(*packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_REQUEST, Packets::Pickups::PickupCollectRequest*) {}
PACKET_HANDLER(ePacketType::PICKUP_COLLECT_DECISION, Packets::Pickups::PickupCollectDecision*) {}
