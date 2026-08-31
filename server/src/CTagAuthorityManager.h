#pragma once

#include "network/packets/world.h"

#include <array>

class CNetworkPlayer;

class CTagAuthorityManager
{
public:
    static bool HandleUpdate(CNetworkPlayer* sender, const Packets::World::TagUpdate& packet);
    static bool HandleSnapshot(CNetworkPlayer* sender, const Packets::World::UpdateAllTags& packet);
    static void SendSnapshot(CNetworkPlayer* recipient);
    static void HandlePlayerDisconnected(int playerId);
    static void ResetSession();

private:
    struct RateState
    {
        uint32_t windowStartedAt = 0;
        uint8_t count = 0;
    };

    static bool IsCurrentHost(const CNetworkPlayer* sender);
    static int FindTag(const Packets::World::TagUpdate::Payload& tag);
    static bool ConsumeRate(int playerId);

    inline static bool ms_hasSnapshot = false;
    inline static Packets::World::UpdateAllTags ms_snapshot{};
    inline static std::array<bool, 100> ms_validTags{};
    inline static std::array<RateState, Config::MAX_SERVER_PLAYERS> ms_rates{};
};
