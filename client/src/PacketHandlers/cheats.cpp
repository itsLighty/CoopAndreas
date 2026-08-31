#include "stdafx.h"

#include "CNetworkCheatManager.h"
#include "network/packet_handler.h"
#include "network/packets/cheats.h"

PACKET_HANDLER(ePacketType::CHEAT_STATE, Packets::Cheats::CheatStateEvent* state)
{
    CNetworkCheatManager::HandleState(*state);
}

PACKET_HANDLER(ePacketType::CHEAT_ACTION, Packets::Cheats::CheatActionEvent* action)
{
    CNetworkCheatManager::HandleAction(*action);
}

// CHEAT_REQUEST is client-originated and intentionally has no client handler.
