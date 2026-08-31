#include "stdafx.h"

#include "CNetworkFireManager.h"
#include "network/packet_handler.h"
#include "network/packets/fires.h"

PACKET_HANDLER(ePacketType::FIRE_STATE, Packets::Fires::FireStateEvent* state)
{
    CNetworkFireManager::HandleState(*state);
}

PACKET_HANDLER(ePacketType::FIRE_EXTINGUISH_REQUEST, Packets::Fires::FireExtinguishRequest* request)
{
    CNetworkFireManager::HandleExtinguishRequest(*request);
}

// FIRE_STATE_INTENT is client-originated and intentionally has no client handler.
