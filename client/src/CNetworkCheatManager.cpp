#include "stdafx.h"

#include "CNetworkCheatManager.h"

#include "CLocalPlayer.h"
#include "CNetwork.h"
#include "CNetworkPlayerManager.h"
#include "CPacketFactory.h"
#include "CWeatherSync.h"

#include <CCutsceneMgr.h>
#include <CKeyGen.h>
#include <CTheScripts.h>
#include <algorithm>
#include <cstring>

using namespace Packets::Cheats;

namespace
{
constexpr int CHEAT_STRING_SIZE = 30;
constexpr int CHEAT_MIN_HASH_SIZE = 6;

eStockCheat FindTypedStockCheat()
{
    const int stringLength = static_cast<int>(std::strlen(CCheat::m_CheatString));
    if (stringLength < CHEAT_MIN_HASH_SIZE)
        return eStockCheat::COUNT;
    int hashLength = CHEAT_MIN_HASH_SIZE;
    for (int length = CHEAT_MIN_HASH_SIZE; length < CHEAT_STRING_SIZE; ++length, ++hashLength)
    {
        const unsigned int hash = CKeyGen::GetKey(CCheat::m_CheatString, hashLength);
        for (uint8_t index = 0; index < STOCK_CHEAT_COUNT; ++index)
        {
            if (static_cast<unsigned int>(CCheat::m_aCheatHashKeys[index]) != hash)
                continue;
            const eStockCheat cheat = static_cast<eStockCheat>(index);
            return IsStockCheatValid(cheat) ? cheat : eStockCheat::COUNT;
        }
    }
    return eStockCheat::COUNT;
}

bool IsMissionActive()
{
    return CTheScripts::IsPlayerOnAMission();
}
}  // namespace

bool CNetworkCheatManager::m_sessionActive = false;
bool CNetworkCheatManager::m_localPlayerIsAuthority = false;
bool CNetworkCheatManager::m_pendingPersistentApply = false;
int CNetworkCheatManager::m_authorityPlayerId = -1;
uint64_t CNetworkCheatManager::m_serverRunId = 0;
uint32_t CNetworkCheatManager::m_lastStateRevision = 0;
uint32_t CNetworkCheatManager::m_lastEventSequence = 0;
uint32_t CNetworkCheatManager::m_requestSequence = 0;
uint8_t CNetworkCheatManager::m_canonicalApplyDepth = 0;
int8_t CNetworkCheatManager::m_canonicalGameplaySpeedStep = 0;
float CNetworkCheatManager::m_offlineTimeScale = 1.0f;
CheatMask CNetworkCheatManager::m_offlineBaseline{};
CheatMask CNetworkCheatManager::m_canonicalState{};
std::array<CNetworkCheatManager::PendingAction, CNetworkCheatManager::PENDING_ACTION_CAPACITY>
    CNetworkCheatManager::m_pendingActions{};

bool CNetworkCheatManager::HasLocalPlayer()
{
    return FindPlayerPed(0) != nullptr;
}

bool CNetworkCheatManager::IsApplyingCanonicalCheat()
{
    return m_canonicalApplyDepth != 0;
}

uint32_t CNetworkCheatManager::NextRequestSequence()
{
    ++m_requestSequence;
    if (m_requestSequence == 0)
        ++m_requestSequence;
    return m_requestSequence;
}

void CNetworkCheatManager::AddToCheatStringHook(char lastPressedKey)
{
    if (!CNetwork::m_bAuthenticated)
    {
        // Only the keyboard call site is redirected, so the untouched retail routine remains callable here.
        plugin::Call<0x438480>(lastPressedKey);
        return;
    }
    if (CCutsceneMgr::ms_running)
        return;

    for (int index = CHEAT_STRING_SIZE - 2; index >= 1; --index)
        CCheat::m_CheatString[index] = CCheat::m_CheatString[index - 1];
    CCheat::m_CheatString[0] = lastPressedKey;
    CCheat::m_CheatString[CHEAT_STRING_SIZE - 1] = '\0';

    const eStockCheat cheat = FindTypedStockCheat();
    if (cheat == eStockCheat::COUNT)
        return; // Debug-menu and vehicle-spawner strings remain in the native buffer unchanged.

    CCheat::m_CheatString[0] = '\0';
    if (!CLocalPlayer::m_bIsHost)
    {
        CChat::AddMessage("{cecedb}[Cheat] Only the current host may use stock cheats online.");
        return;
    }
    if (IsMissionActive())
    {
        CChat::AddMessage("{cecedb}[Cheat] Stock cheats are disabled during missions.");
        return;
    }
    RequestCheat(cheat);
}

void CNetworkCheatManager::BeginNetworkSession()
{
    if (m_sessionActive)
        return;
    m_offlineBaseline.fill(0);
    for (uint8_t index = 0; index < STOCK_CHEAT_COUNT; ++index)
    {
        const eStockCheat cheat = static_cast<eStockCheat>(index);
        SetCheatMaskBit(m_offlineBaseline, cheat, CCheat::m_aCheatsActive[index]);
    }
    m_offlineTimeScale = CTimer::ms_fTimeScale;
    m_canonicalGameplaySpeedStep = 0;
    ApplyGameplaySpeedStep(m_canonicalGameplaySpeedStep);
    m_canonicalState.fill(0);
    m_pendingActions.fill({});
    m_sessionActive = true;
    m_pendingPersistentApply = false;
    m_serverRunId = 0;
    m_lastStateRevision = 0;
    m_lastEventSequence = 0;
    m_requestSequence = 0;
}

void CNetworkCheatManager::ResetNetworkState()
{
    if (m_sessionActive)
    {
        if (HasLocalPlayer())
        {
            ApplyPersistentMask(m_offlineBaseline);
            for (uint8_t index = 0; index < STOCK_CHEAT_COUNT; ++index)
            {
                const eStockCheat cheat = static_cast<eStockCheat>(index);
                if (!IsPersistentCheat(cheat))
                    CCheat::m_aCheatsActive[index] = GetCheatMaskBit(m_offlineBaseline, cheat);
            }
        }
        else
        {
            for (uint8_t index = 0; index < STOCK_CHEAT_COUNT; ++index)
            {
                const eStockCheat cheat = static_cast<eStockCheat>(index);
                CCheat::m_aCheatsActive[index] = GetCheatMaskBit(m_offlineBaseline, cheat);
            }
        }
        CTimer::ms_fTimeScale = m_offlineTimeScale;
    }
    m_sessionActive = false;
    m_localPlayerIsAuthority = false;
    m_pendingPersistentApply = false;
    m_authorityPlayerId = -1;
    m_serverRunId = 0;
    m_lastStateRevision = 0;
    m_lastEventSequence = 0;
    m_requestSequence = 0;
    m_canonicalApplyDepth = 0;
    m_canonicalGameplaySpeedStep = 0;
    m_canonicalState.fill(0);
    m_pendingActions.fill({});
}

void CNetworkCheatManager::HandleAuthorityChanged(int authorityPlayerId, bool localPlayerIsAuthority)
{
    m_authorityPlayerId = authorityPlayerId;
    m_localPlayerIsAuthority = localPlayerIsAuthority;
    m_requestSequence = 0;
    m_pendingActions.fill({});
    if (m_sessionActive && HasLocalPlayer())
        ApplyPersistentMask(m_canonicalState);
    else
        m_pendingPersistentApply = m_sessionActive;
    if (m_sessionActive)
        ApplyGameplaySpeedStep(m_canonicalGameplaySpeedStep);
}

void CNetworkCheatManager::RequestCheat(eStockCheat cheat)
{
    if (!CNetwork::m_bAuthenticated || !m_localPlayerIsAuthority || !IsStockCheatValid(cheat) ||
        IsMissionActive())
    {
        return;
    }
    CheatRequest request{};
    request.requestSequence = NextRequestSequence();
    request.cheat = cheat;
    GetPacketFactory().Send(request);
}

void CNetworkCheatManager::ExecuteNative(eStockCheat cheat)
{
    const uint8_t index = static_cast<uint8_t>(cheat);
    if (!IsStockCheatValid(cheat) || index >= STOCK_CHEAT_COUNT)
        return;
    ++m_canonicalApplyDepth;
    if (IsLatchedStatSetter(cheat))
        CCheat::m_aCheatsActive[index] = true;
    else if (CCheat::m_aCheatFunctions[index] != nullptr)
        CCheat::m_aCheatFunctions[index]();
    else
        CCheat::m_aCheatsActive[index] = !CCheat::m_aCheatsActive[index];
    --m_canonicalApplyDepth;
}

void CNetworkCheatManager::ApplyGameplaySpeedStep(int8_t step)
{
    if (!IsGameplaySpeedStepValid(step))
        return;
    CTimer::ms_fTimeScale = GameplaySpeedStepToScale(step);
}

void CNetworkCheatManager::ApplyPersistentMask(
    const CheatMask& mask, bool hasCause, eStockCheat cause)
{
    if (!HasLocalPlayer())
    {
        m_pendingPersistentApply = true;
        return;
    }
    m_pendingPersistentApply = false;

    if (hasCause && IsPersistentCheat(cause))
    {
        const uint8_t causeIndex = static_cast<uint8_t>(cause);
        if (CCheat::m_aCheatsActive[causeIndex] != GetCheatMaskBit(mask, cause))
            ExecuteNative(cause);
    }

    // Native theme cheats can disable peers. A bounded reconciliation pass applies their side effects, then
    // seals the active flags to the server mask so iteration order can never leave a peer divergent.
    for (uint8_t pass = 0; pass < 3; ++pass)
    {
        bool changed = false;
        for (uint8_t index = 0; index < STOCK_CHEAT_COUNT; ++index)
        {
            const eStockCheat cheat = static_cast<eStockCheat>(index);
            if (!IsPersistentCheat(cheat))
                continue;
            const bool desired = GetCheatMaskBit(mask, cheat);
            if (CCheat::m_aCheatsActive[index] != desired)
            {
                ExecuteNative(cheat);
                changed = true;
            }
        }
        if (!changed)
            break;
    }
    for (uint8_t index = 0; index < STOCK_CHEAT_COUNT; ++index)
    {
        const eStockCheat cheat = static_cast<eStockCheat>(index);
        if (IsPersistentCheat(cheat))
            CCheat::m_aCheatsActive[index] = GetCheatMaskBit(mask, cheat);
    }
}

void CNetworkCheatManager::ShowAcceptedFeedback(eStockCheat, bool enabled)
{
    CCheat::m_bHasPlayerCheated = true;
    CStats::IncrementStat(STAT_TIMES_CHEATED, 1.0f);
    CHud::SetHelpMessage(TheText.Get(enabled ? "CHEAT1" : "CHEAT8"), true, false, false);
}

void CNetworkCheatManager::HandleState(const CheatStateEvent& state)
{
    if (!m_sessionActive || !state.HasValidPayload())
        return;
    if (m_serverRunId == 0)
    {
        m_serverRunId = state.serverRunId;
    }
    else if (m_serverRunId != state.serverRunId)
        return;
    if (m_authorityPlayerId >= 0 && state.authorityPlayerId != m_authorityPlayerId)
        return;
    if (m_lastStateRevision != 0 && !IsCheatSerialNewer(state.revision, m_lastStateRevision))
        return;
    m_lastStateRevision = state.revision;
    m_authorityPlayerId = state.authorityPlayerId;
    m_canonicalState = state.persistentMask;
    m_canonicalGameplaySpeedStep = state.gameplaySpeedStep;
    ApplyPersistentMask(m_canonicalState, state.hasCause, state.cause);
    ApplyGameplaySpeedStep(m_canonicalGameplaySpeedStep);

    if (state.hasCause && m_localPlayerIsAuthority &&
        state.authorityPlayerId == CNetworkPlayerManager::m_nMyId)
    {
        const bool enabled = IsCanonicalGameplaySpeedCheat(state.cause) ||
            GetCheatMaskBit(state.persistentMask, state.cause);
        ShowAcceptedFeedback(state.cause, enabled);
        CWeatherSync::SyncCurrentState();
    }
}

void CNetworkCheatManager::ExecuteTransient(eStockCheat cheat)
{
    if (IsMissionActive())
        return;
    ExecuteNative(cheat);
    if (m_localPlayerIsAuthority && IsAuthorityOnlyTransient(cheat))
        CWeatherSync::SyncCurrentState();
}

void CNetworkCheatManager::QueueTransient(uint32_t eventSequence, eStockCheat cheat)
{
    for (PendingAction& pending : m_pendingActions)
    {
        if (pending.valid)
            continue;
        pending.valid = true;
        pending.eventSequence = eventSequence;
        pending.receivedAt = GetTickCount();
        pending.cheat = cheat;
        return;
    }
}

void CNetworkCheatManager::HandleAction(const CheatActionEvent& action)
{
    if (!m_sessionActive || !action.HasValidPayload())
        return;
    if (m_serverRunId == 0)
    {
        m_serverRunId = action.serverRunId;
    }
    else if (m_serverRunId != action.serverRunId)
        return;
    if (m_authorityPlayerId >= 0 && action.authorityPlayerId != m_authorityPlayerId)
        return;
    if (m_lastEventSequence != 0 && !IsCheatSerialNewer(action.eventSequence, m_lastEventSequence))
        return;
    m_lastEventSequence = action.eventSequence;
    m_authorityPlayerId = action.authorityPlayerId;

    const bool executeHere = !IsAuthorityOnlyTransient(action.cheat) ||
        (m_localPlayerIsAuthority && action.authorityPlayerId == CNetworkPlayerManager::m_nMyId);
    if (executeHere)
    {
        if (HasLocalPlayer())
            ExecuteTransient(action.cheat);
        else
            QueueTransient(action.eventSequence, action.cheat);
    }
    if (m_localPlayerIsAuthority && action.authorityPlayerId == CNetworkPlayerManager::m_nMyId)
        ShowAcceptedFeedback(action.cheat, true);
}

void CNetworkCheatManager::Process()
{
    if (!CNetwork::m_bAuthenticated || !m_sessionActive)
        return;
    if (m_pendingPersistentApply && HasLocalPlayer())
    {
        ApplyPersistentMask(m_canonicalState);
        ApplyGameplaySpeedStep(m_canonicalGameplaySpeedStep);
    }
    if (!HasLocalPlayer())
        return;

    const uint32_t now = GetTickCount();
    for (PendingAction& pending : m_pendingActions)
    {
        if (!pending.valid)
            continue;
        if (now - pending.receivedAt <= PENDING_ACTION_LIFETIME_MS && !IsMissionActive())
            ExecuteTransient(pending.cheat);
        pending = {};
    }
}
