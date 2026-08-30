#pragma once

#include "network/packets/players.h"

#include <cstdint>

class CPlayerGameplayStateSync
{
public:
    static void Process();
    static void ResetNetworkState();

private:
    static bool m_bHasLastSentState;
    static uint32_t m_nLastSentAt;
    static Packets::Players::PlayerGameplayState m_LastSentState;
};
