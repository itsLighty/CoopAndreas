#include "stdafx.h"

#include "CCheatAuthorityManager.h"
#include "network/packet_handler.h"
#include "network/packets/cheats.h"

PACKET_HANDLER(ePacketType::CHEAT_REQUEST, Packets::Cheats::CheatRequest* request,
    CNetworkPlayer* player)
{
    CCheatAuthorityManager::HandleRequest(player, *request);
}

// CHEAT_STATE and CHEAT_ACTION are server-originated. Forged client events have no registered handler.
