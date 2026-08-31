#include "stdafx.h"
#include "CGangZoneWarAuthorityManager.h"
#include "CPickupAuthorityManager.h"
#include "CStuntJumpAuthorityManager.h"
#include "CFireAuthorityManager.h"
#include "CCheatAuthorityManager.h"

std::vector<CNetworkPlayer*> CNetworkPlayerManager::m_pPlayers;

void CNetworkPlayerManager::Add(CNetworkPlayer* player)
{
    m_pPlayers.push_back(player);
}

void CNetworkPlayerManager::Remove(CNetworkPlayer* player)
{
    auto it = std::find(m_pPlayers.begin(), m_pPlayers.end(), player);
    if (it != m_pPlayers.end())
    {
        m_pPlayers.erase(it);
    }
}

// find player instance by id
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

// find player instance by enetpeer
CNetworkPlayer* CNetworkPlayerManager::GetPlayer(ENetPeer* peer)
{
    for (int i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_pPeer == peer)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}

int CNetworkPlayerManager::GetFreeID()
{
    for (int i = 0; i != Config::MAX_SERVER_PLAYERS; i++)
    {
        if (CNetworkPlayerManager::GetPlayer(i) == nullptr)
            return i;
    }
    return -1;  // server is full
}

CNetworkPlayer* CNetworkPlayerManager::GetHost()
{
    for (int i = 0; i != m_pPlayers.size(); i++)
    {
        if (m_pPlayers[i]->m_bIsHost)
        {
            return m_pPlayers[i];
        }
    }
    return nullptr;
}

void CNetworkPlayerManager::AssignHostToFirstPlayer()
{
    if (CNetworkPlayerManager::m_pPlayers.size() <= 0)
    {
        CGangZoneWarAuthorityManager::ResetForAuthorityChange();
        CPickupAuthorityManager::HandleAuthorityChange(nullptr);
        CStuntJumpAuthorityManager::HandleAuthorityChange(nullptr);
        CFireAuthorityManager::HandleAuthorityChange(nullptr);
        CCheatAuthorityManager::HandleAuthorityChange(nullptr);
        return;
    }

    CNetworkPlayer* player = CNetworkPlayerManager::m_pPlayers.front();
    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();

    if (player == host)
        return;

    // Cached world state belongs to one concrete authority. Never let a replacement host inherit a stale
    // revision stream; its first fresh snapshot establishes the new cache.
    CGangZoneWarAuthorityManager::ResetForAuthorityChange();

    if (host != nullptr)
        host->m_bIsHost = false;

    player->m_bIsHost = true;

    Packets::System::PlayerAssignHost playerAssignHost{};
    playerAssignHost.playerid = player->m_iPlayerId;
    GetPacketFactory().SendToAll(playerAssignHost);
    CPickupAuthorityManager::HandleAuthorityChange(player);
    CStuntJumpAuthorityManager::HandleAuthorityChange(player);
    CFireAuthorityManager::HandleAuthorityChange(player);
    CCheatAuthorityManager::HandleAuthorityChange(player);
}
