#include "stdafx.h"
#include "CPlayerGameplayStateSync.h"
#include "CMissionSessionClient.h"

#include <algorithm>
#include <cmath>

namespace
{
constexpr uint32_t MIN_GAMEPLAY_STATE_SYNC_INTERVAL_MS = 100;
constexpr float BREATH_CHANGE_THRESHOLD = 0.1f;
constexpr float MAX_HEALTH_CHANGE_THRESHOLD = 0.1f;

bool CollectLocalGameplayState(Packets::Players::PlayerGameplayState& state)
{
    CPlayerPed* pLocalPlayer = FindPlayerPed(0);
    if (pLocalPlayer == nullptr || pLocalPlayer->m_pPlayerData == nullptr ||
        pLocalPlayer->m_pPlayerData->m_pWanted == nullptr)
    {
        return false;
    }

    CPlayerInfo* pPlayerInfo = pLocalPlayer->GetPlayerInfoForThisPlayerPed();
    if (pPlayerInfo == nullptr)
    {
        return false;
    }

    state.wantedLevel = static_cast<uint8_t>(std::clamp(pLocalPlayer->GetWantedLevel(),
        static_cast<int>(Packets::Players::PlayerGameplayState::MIN_WANTED_LEVEL),
        static_cast<int>(Packets::Players::PlayerGameplayState::MAX_WANTED_LEVEL)));
    state.money = std::clamp(pPlayerInfo->m_nMoney, Packets::Players::PlayerGameplayState::MIN_MONEY,
        Packets::Players::PlayerGameplayState::MAX_MONEY);

    const float breath = pLocalPlayer->m_pPlayerData->m_fBreath;
    state.breath = std::isfinite(breath)
        ? std::clamp(breath, Packets::Players::PlayerGameplayState::MIN_BREATH,
              Packets::Players::PlayerGameplayState::MAX_BREATH)
        : Packets::Players::PlayerGameplayState::MIN_BREATH;

    const float maximumHealth = pLocalPlayer->m_fMaxHealth;
    state.maximumHealth = std::isfinite(maximumHealth)
        ? std::clamp(maximumHealth, Packets::Players::PlayerGameplayState::MIN_MAX_HEALTH,
              Packets::Players::PlayerGameplayState::MAX_MAX_HEALTH)
        : Packets::Players::PlayerGameplayState::MIN_MAX_HEALTH;
    return true;
}

bool HasMeaningfulChange(const Packets::Players::PlayerGameplayState& previous,
    const Packets::Players::PlayerGameplayState& current)
{
    return previous.wantedLevel != current.wantedLevel || previous.money != current.money ||
           std::fabs(previous.breath - current.breath) >= BREATH_CHANGE_THRESHOLD ||
           std::fabs(previous.maximumHealth - current.maximumHealth) >= MAX_HEALTH_CHANGE_THRESHOLD;
}
}  // namespace

bool CPlayerGameplayStateSync::m_bHasLastSentState = false;
uint32_t CPlayerGameplayStateSync::m_nLastSentAt = 0;
Packets::Players::PlayerGameplayState CPlayerGameplayStateSync::m_LastSentState{};

void CPlayerGameplayStateSync::Process()
{
    if (!CNetwork::m_bAuthenticated || CNetworkPlayerManager::m_nMyId < 0 ||
        CMissionSessionClient::IsSpectator())
    {
        ResetNetworkState();
        return;
    }

    Packets::Players::PlayerGameplayState currentState{};
    if (!CollectLocalGameplayState(currentState))
    {
        return;
    }

    if (m_bHasLastSentState && !HasMeaningfulChange(m_LastSentState, currentState))
    {
        return;
    }

    const uint32_t now = GetTickCount();
    if (m_bHasLastSentState && now - m_nLastSentAt < MIN_GAMEPLAY_STATE_SYNC_INTERVAL_MS)
    {
        return;
    }

    GetPacketFactory().Send(currentState);
    m_LastSentState = currentState;
    m_bHasLastSentState = true;
    m_nLastSentAt = now;
}

void CPlayerGameplayStateSync::ResetNetworkState()
{
    m_bHasLastSentState = false;
    m_nLastSentAt = 0;
    m_LastSentState = {};
}
