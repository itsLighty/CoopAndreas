#include "stdafx.h"

#include "CNetworkPickupManager.h"
#include "network/packet_types.h"
#include "network/packets/pickups.h"

PACKET_HANDLER(ePacketType::PICKUP_STATE, Packets::Pickups::PickupStateEvent* packet)
{
    CNetworkPickupManager::HandleState(*packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_REQUEST, Packets::Pickups::PickupCollectRequest* packet)
{
    CNetworkPickupManager::HandleCollectRequest(*packet);
}

PACKET_HANDLER(ePacketType::PICKUP_COLLECT_RESULT, Packets::Pickups::PickupCollectResult* packet)
{
    CNetworkPickupManager::HandleCollectResult(*packet);
}

PACKET_HANDLER(ePacketType::PICKUP_CREATE_INTENT, Packets::Pickups::PickupCreateIntent* packet)
{
    CNetworkPickupManager::HandleCreateIntent(*packet);
}
