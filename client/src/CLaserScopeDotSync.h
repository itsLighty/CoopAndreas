#pragma once

class CLaserScopeDotSync
{
public:
    static constexpr uint32_t HEARTBEAT_INTERVAL_MS = 250;
    static constexpr uint32_t STALE_TIMEOUT_MS = 1000;

    static void Process();
    static void AppendLocalState(Packets::Players::PlayerCameraSync& packet);
    static void HandleRemoteState(
        CNetworkPlayer* player, const Packets::Players::PlayerCameraSync& packet);
    static bool ShouldSendHeartbeat(
        const Packets::Players::PlayerCameraSync& packet, uint32_t now, uint32_t lastSentAt);
};
