#include "stdafx.h"

#include "CFireAuthorityManager.h"
#include "network/packet_handler.h"
#include "network/packets/fires.h"

PACKET_HANDLER(ePacketType::FIRE_STATE_INTENT, Packets::Fires::FireStateIntent* intent,
    CNetworkPlayer* player)
{
    CFireAuthorityManager::HandleStateIntent(player, *intent);
}

PACKET_HANDLER(ePacketType::FIRE_EXTINGUISH_REQUEST, Packets::Fires::FireExtinguishRequest* request,
    CNetworkPlayer* player)
{
    CFireAuthorityManager::HandleExtinguishRequest(player, *request);
}

// FIRE_STATE is server-originated. Registering no handler means a forged client event is rejected by dispatch.
