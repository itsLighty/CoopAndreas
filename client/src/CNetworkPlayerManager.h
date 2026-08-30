#pragma once
class CNetworkPlayerManager
{
public:
    static std::vector<CNetworkPlayer*> m_pPlayers;
    static CPad m_pPads[Config::MAX_SERVER_PLAYERS + 2];
    static int m_nMyId;
    static std::array<Packets::Players::PlayerStats, Config::MAX_SERVER_PLAYERS> m_pendingStats;
    static std::array<Packets::Players::PlayerGameplayState, Config::MAX_SERVER_PLAYERS> m_pendingGameplayStates;
    static std::array<bool, Config::MAX_SERVER_PLAYERS> m_hasPendingStats;
    static std::array<bool, Config::MAX_SERVER_PLAYERS> m_hasPendingGameplayState;

    static void Add(CNetworkPlayer* player);
    static void Remove(CNetworkPlayer* player);
    static void Clear();
    static CNetworkPlayer* GetPlayer(int playerid);
    static CNetworkPlayer* GetPlayer(SenderPlayerId playerid);
    static CNetworkPlayer* GetPlayer(CEntity* entity);
    static void ApplyOrQueueStats(const Packets::Players::PlayerStats& stats);
    static void ApplyOrQueueGameplayState(const Packets::Players::PlayerGameplayState& gameplayState);
};
