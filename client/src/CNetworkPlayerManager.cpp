#include "stdafx.h"

std::vector<CNetworkPlayer*> CNetworkPlayerManager::m_pPlayers;
CPad CNetworkPlayerManager::m_pPads[Config::MAX_SERVER_PLAYERS + 2];
int CNetworkPlayerManager::m_nMyId = -1;
std::array<Packets::Players::PlayerStats, Config::MAX_SERVER_PLAYERS> CNetworkPlayerManager::m_pendingStats{};
std::array<Packets::Players::PlayerGameplayState, Config::MAX_SERVER_PLAYERS>
    CNetworkPlayerManager::m_pendingGameplayStates{};
std::array<bool, Config::MAX_SERVER_PLAYERS> CNetworkPlayerManager::m_hasPendingStats{};
std::array<bool, Config::MAX_SERVER_PLAYERS> CNetworkPlayerManager::m_hasPendingGameplayState{};

void CNetworkPlayerManager::Add(CNetworkPlayer* player)
{
    if (!player)
        return;
    if (CNetworkPlayer* existing = GetPlayer(player->m_iPlayerId))
    {
        Remove(existing);
        delete existing;
    }
    m_pPlayers.push_back(player);

    const int playerId = player->m_iPlayerId;
    if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }

    if (m_hasPendingStats[playerId])
    {
        ApplyOrQueueStats(m_pendingStats[playerId]);
        m_hasPendingStats[playerId] = false;
        m_pendingStats[playerId] = {};
    }
    if (m_hasPendingGameplayState[playerId])
    {
        player->ApplyGameplayState(m_pendingGameplayStates[playerId]);
        m_hasPendingGameplayState[playerId] = false;
        m_pendingGameplayStates[playerId] = {};
    }
}

void CNetworkPlayerManager::Remove(CNetworkPlayer* player)
{
    auto it = std::find(m_pPlayers.begin(), m_pPlayers.end(), player);
    if (it != m_pPlayers.end())
    {
        m_pPlayers.erase(it);
    }

    if (player->m_iPlayerId >= 0 && player->m_iPlayerId < Config::MAX_SERVER_PLAYERS)
    {
        m_hasPendingStats[player->m_iPlayerId] = false;
        m_hasPendingGameplayState[player->m_iPlayerId] = false;
        m_pendingStats[player->m_iPlayerId] = {};
        m_pendingGameplayStates[player->m_iPlayerId] = {};
    }
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(SenderPlayerId playerid)
{
    return GetPlayer(playerid.value);
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(int playerid)
{
    for (int i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_iPlayerId == playerid)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}

CNetworkPlayer* CNetworkPlayerManager::GetPlayer(CEntity* entity)
{
    // A streamed-out or respawning network player legitimately has no native ped. Never let a null
    // lookup identify that wrapper merely because both pointers are null.
    if (entity == nullptr)
        return nullptr;

    for (int i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_pPed == entity)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}

void CNetworkPlayerManager::Clear()
{
    while (!m_pPlayers.empty())
    {
        CNetworkPlayer* player = m_pPlayers.back();
        m_pPlayers.pop_back();
        delete player;
    }
    m_nMyId = -1;
    m_hasPendingStats.fill(false);
    m_hasPendingGameplayState.fill(false);
    m_pendingStats.fill(Packets::Players::PlayerStats{});
    m_pendingGameplayStates.fill(Packets::Players::PlayerGameplayState{});
}

void CNetworkPlayerManager::ApplyOrQueueStats(const Packets::Players::PlayerStats& stats)
{
    const int playerId = stats.playerid.value;
    if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }

    if (CNetworkPlayer* pNetworkPlayer = GetPlayer(playerId))
    {
        for (size_t i = 0; i < CStatsSync::SYNCED_STATS_COUNT; ++i)
        {
            pNetworkPlayer->m_stats[CStatsSync::m_aeSyncedStats[i]] = stats.stats[i];
        }
        return;
    }

    m_pendingStats[playerId] = stats;
    m_hasPendingStats[playerId] = true;
}

void CNetworkPlayerManager::ApplyOrQueueGameplayState(
    const Packets::Players::PlayerGameplayState& gameplayState)
{
    const int playerId = gameplayState.playerid.value;
    if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }

    if (CNetworkPlayer* pNetworkPlayer = GetPlayer(playerId))
    {
        pNetworkPlayer->ApplyGameplayState(gameplayState);
        return;
    }

    m_pendingGameplayStates[playerId] = gameplayState;
    m_hasPendingGameplayState[playerId] = true;
}
