#include "stdafx.h"
#include "CGangZoneWarAuthorityManager.h"

using namespace Packets::World;

GangZoneState CGangZoneWarAuthorityManager::m_ZoneState{};
GangWarState CGangZoneWarAuthorityManager::m_WarState{};
bool CGangZoneWarAuthorityManager::m_bHasZoneState = false;
bool CGangZoneWarAuthorityManager::m_bHasWarState = false;

bool CGangZoneWarAuthorityManager::HandleZoneState(
    CNetworkPlayer* pNetworkPlayer, const GangZoneState& state)
{
    if (!IsCurrentHost(pNetworkPlayer) || !state.HasValidState() || !state.FitsSerializedBudget() ||
        state.authorityPlayerId != pNetworkPlayer->m_iPlayerId)
    {
        logger::warn("Rejected unauthorized or malformed gang-zone state");
        return false;
    }
    if (m_bHasZoneState && !IsGangWorldRevisionNewer(state.revision, m_ZoneState.revision))
    {
        logger::warn("Rejected stale or replayed gang-zone state revision");
        return false;
    }

    m_ZoneState = state;
    m_ZoneState.authorityPlayerId = static_cast<uint8_t>(pNetworkPlayer->m_iPlayerId);
    m_bHasZoneState = true;
    GetPacketFactory().SendToAll(m_ZoneState, pNetworkPlayer);
    return true;
}

bool CGangZoneWarAuthorityManager::HandleWarState(
    CNetworkPlayer* pNetworkPlayer, const GangWarState& state)
{
    if (!IsCurrentHost(pNetworkPlayer) || !state.HasValidState() || !state.FitsSerializedBudget() ||
        state.authorityPlayerId != pNetworkPlayer->m_iPlayerId)
    {
        logger::warn("Rejected unauthorized or malformed gang-war state");
        return false;
    }
    if (m_bHasWarState && !IsGangWorldRevisionNewer(state.revision, m_WarState.revision))
    {
        logger::warn("Rejected stale or replayed gang-war state revision");
        return false;
    }

    m_WarState = state;
    m_WarState.authorityPlayerId = static_cast<uint8_t>(pNetworkPlayer->m_iPlayerId);
    m_bHasWarState = true;
    GetPacketFactory().SendToAll(m_WarState, pNetworkPlayer);
    return true;
}

void CGangZoneWarAuthorityManager::SendSnapshot(CNetworkPlayer* pNetworkPlayer)
{
    if (pNetworkPlayer == nullptr)
    {
        return;
    }
    if (m_bHasZoneState)
    {
        GetPacketFactory().Send(m_ZoneState, pNetworkPlayer);
    }
    if (m_bHasWarState)
    {
        GetPacketFactory().Send(m_WarState, pNetworkPlayer);
    }
}

void CGangZoneWarAuthorityManager::ResetForAuthorityChange()
{
    m_ZoneState = GangZoneState{};
    m_WarState = GangWarState{};
    m_bHasZoneState = false;
    m_bHasWarState = false;
}

bool CGangZoneWarAuthorityManager::IsCurrentHost(const CNetworkPlayer* pNetworkPlayer)
{
    return pNetworkPlayer != nullptr && pNetworkPlayer->m_bIsHost &&
           CNetworkPlayerManager::GetHost() == pNetworkPlayer;
}
