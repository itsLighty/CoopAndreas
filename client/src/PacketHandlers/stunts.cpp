#include "stdafx.h"
#include "CStuntJumpSyncManager.h"
#include "network/packets/stunts.h"

PACKET_HANDLER(ePacketType::STUNT_ATTEMPT_RESULT, Packets::Stunts::StuntAttemptResult* result)
{
    // Attempt acknowledgements are request/nonce scoped and intentionally bypass state revision filtering.
    CStuntJumpSyncManager::HandleAttemptResult(*result);
}

PACKET_HANDLER(ePacketType::STUNT_STATE, Packets::Stunts::StuntStateEvent* state)
{
    CStuntJumpSyncManager::HandleState(*state);
}

PACKET_HANDLER(ePacketType::STUNT_DEFINITION, Packets::Stunts::StuntDefinitionAnnounce*)
{
    logger::warn("Ignored a server-originated stunt definition request");
}

PACKET_HANDLER(ePacketType::STUNT_ATTEMPT, Packets::Stunts::StuntAttempt*)
{
    logger::warn("Ignored a server-originated stunt attempt");
}
