#include "stdafx.h"

#include "CCheatAuthorityManager.h"

#include "CMissionSessionServer.h"
#include "CNetworkPlayer.h"
#include "CNetworkPlayerManager.h"
#include "CPacketFactory.h"
#include "logger.h"
#include "network/packets/cheats.h"

#include <array>
#include <chrono>
#include <cstdint>

namespace
{
using namespace Packets::Cheats;

constexpr uint32_t CHEAT_REQUEST_RATE_LIMIT = 8;
constexpr uint64_t RATE_WINDOW_MS = 1000;

struct RateWindow
{
    uint64_t startedAtMs = 0;
    uint32_t count = 0;
};

CheatMask g_persistentState{};
std::array<uint32_t, Config::MAX_SERVER_PLAYERS> g_lastRequestSequences{};
std::array<RateWindow, Config::MAX_SERVER_PLAYERS> g_requestRates{};
uint64_t g_serverRunId = 0;
uint32_t g_stateRevision = 0;
uint32_t g_eventSequence = 0;
uint8_t g_authorityPlayerId = CHEAT_INVALID_PLAYER_ID;
int8_t g_gameplaySpeedStep = 0;

uint64_t NowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void EnsureRunId()
{
    if (g_serverRunId != 0)
        return;
    const uint64_t wallClock = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    const uint64_t monotonic = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    g_serverRunId = wallClock ^ (monotonic << 1u) ^ 0x434845415452554EULL;
    if (g_serverRunId == 0)
        g_serverRunId = 1;
}

uint32_t NextNonZero(uint32_t& value)
{
    ++value;
    if (value == 0)
        ++value;
    return value;
}

bool ConsumeRate(RateWindow& window)
{
    const uint64_t now = NowMs();
    if (window.startedAtMs == 0 || now - window.startedAtMs >= RATE_WINDOW_MS)
    {
        window.startedAtMs = now;
        window.count = 0;
    }
    if (window.count >= CHEAT_REQUEST_RATE_LIMIT)
        return false;
    ++window.count;
    return true;
}

void DisableSpecialThemePeers(eStockCheat active)
{
    constexpr eStockCheat specialThemes[] = {eStockCheat::BEACH_PARTY, eStockCheat::CHEAP_TRAFFIC,
        eStockCheat::FAST_TRAFFIC, eStockCheat::FUNHOUSE_THEME, eStockCheat::COUNTRY_TRAFFIC};
    for (const eStockCheat cheat : specialThemes)
    {
        if (cheat != active)
            SetCheatMaskBit(g_persistentState, cheat, false);
    }
}

void ApplyPersistentTransition(eStockCheat cheat)
{
    const bool enabled = !GetCheatMaskBit(g_persistentState, cheat);
    SetCheatMaskBit(g_persistentState, cheat, enabled);
    if (!enabled)
    {
        if (cheat == eStockCheat::PEDS_ATTACK_WITH_ROCKETS)
        {
            SetCheatMaskBit(g_persistentState, eStockCheat::EVERYBODY_ATTACKS_PLAYER,
                !GetCheatMaskBit(g_persistentState, eStockCheat::EVERYBODY_ATTACKS_PLAYER));
            SetCheatMaskBit(g_persistentState, eStockCheat::EVERYONE_ARMED, true);
        }
        return;
    }

    switch (cheat)
    {
    case eStockCheat::PINK_TRAFFIC:
        SetCheatMaskBit(g_persistentState, eStockCheat::BLACK_TRAFFIC, false);
        break;
    case eStockCheat::BLACK_TRAFFIC:
        SetCheatMaskBit(g_persistentState, eStockCheat::PINK_TRAFFIC, false);
        break;
    case eStockCheat::BEACH_PARTY:
    case eStockCheat::CHEAP_TRAFFIC:
    case eStockCheat::FAST_TRAFFIC:
    case eStockCheat::FUNHOUSE_THEME:
    case eStockCheat::COUNTRY_TRAFFIC:
        DisableSpecialThemePeers(cheat);
        break;
    case eStockCheat::NINJA_THEME:
        DisableSpecialThemePeers(cheat);
        SetCheatMaskBit(g_persistentState, eStockCheat::BLACK_TRAFFIC, true);
        SetCheatMaskBit(g_persistentState, eStockCheat::PINK_TRAFFIC, false);
        break;
    case eStockCheat::PEDS_ATTACK_WITH_ROCKETS:
        SetCheatMaskBit(g_persistentState, eStockCheat::EVERYONE_ARMED, false);
        SetCheatMaskBit(g_persistentState, eStockCheat::EVERYBODY_ATTACKS_PLAYER,
            !GetCheatMaskBit(g_persistentState, eStockCheat::EVERYBODY_ATTACKS_PLAYER));
        break;
    default:
        break;
    }
}

void ApplyGameplaySpeedTransition(eStockCheat cheat)
{
    if (cheat == eStockCheat::FASTER_GAMEPLAY && g_gameplaySpeedStep < GAMEPLAY_SPEED_STEP_MAX)
        ++g_gameplaySpeedStep;
    else if (cheat == eStockCheat::SLOWER_GAMEPLAY && g_gameplaySpeedStep > GAMEPLAY_SPEED_STEP_MIN)
        --g_gameplaySpeedStep;
}

CheatStateEvent BuildStateEvent(bool hasCause, uint32_t requestSequence, eStockCheat cause)
{
    CheatStateEvent event{};
    event.serverRunId = g_serverRunId;
    event.revision = g_stateRevision;
    event.authorityPlayerId = g_authorityPlayerId;
    event.gameplaySpeedStep = g_gameplaySpeedStep;
    event.hasCause = hasCause;
    event.requestSequence = hasCause ? requestSequence : 0;
    event.cause = cause;
    event.persistentMask = g_persistentState;
    return event;
}

void BroadcastState(bool hasCause = false, uint32_t requestSequence = 0,
    eStockCheat cause = eStockCheat::FASTER_CLOCK)
{
    if (g_authorityPlayerId >= Config::MAX_SERVER_PLAYERS)
        return;
    CheatStateEvent event = BuildStateEvent(hasCause, requestSequence, cause);
    if (event.HasValidPayload())
        GetPacketFactory().SendToAll(event);
}
}  // namespace

void CCheatAuthorityManager::HandleRequest(CNetworkPlayer* player, const CheatRequest& request)
{
    EnsureRunId();
    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();
    if (player == nullptr || player != host || !player->m_bIsHost ||
        player->m_iPlayerId != g_authorityPlayerId || player->m_iPlayerId < 0 ||
        player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS || !request.HasValidPayload())
    {
        logger::warn("Rejected a cheat request without current host authority");
        return;
    }
    if (CMissionSessionServer::GetState().IsActive())
    {
        logger::warn("Rejected a cheat request while a mission session is active");
        return;
    }

    uint32_t& lastSequence = g_lastRequestSequences[player->m_iPlayerId];
    if ((lastSequence != 0 && !IsCheatSerialNewer(request.requestSequence, lastSequence)) ||
        !ConsumeRate(g_requestRates[player->m_iPlayerId]))
    {
        return;
    }
    lastSequence = request.requestSequence;

    if (IsPersistentCheat(request.cheat))
    {
        ApplyPersistentTransition(request.cheat);
        NextNonZero(g_stateRevision);
        BroadcastState(true, request.requestSequence, request.cheat);
        return;
    }

    if (IsCanonicalGameplaySpeedCheat(request.cheat))
    {
        ApplyGameplaySpeedTransition(request.cheat);
        NextNonZero(g_stateRevision);
        BroadcastState(true, request.requestSequence, request.cheat);
        return;
    }

    CheatActionEvent event{};
    event.serverRunId = g_serverRunId;
    event.eventSequence = NextNonZero(g_eventSequence);
    event.authorityPlayerId = g_authorityPlayerId;
    event.requestSequence = request.requestSequence;
    event.cheat = request.cheat;
    if (event.HasValidPayload())
        GetPacketFactory().SendToAll(event);
}

void CCheatAuthorityManager::HandlePlayerDisconnected(CNetworkPlayer* player)
{
    if (player == nullptr || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS)
        return;
    g_lastRequestSequences[player->m_iPlayerId] = 0;
    g_requestRates[player->m_iPlayerId] = {};
}

void CCheatAuthorityManager::HandleAuthorityChange(CNetworkPlayer* newAuthority)
{
    if (newAuthority == nullptr || newAuthority->m_iPlayerId < 0 ||
        newAuthority->m_iPlayerId >= Config::MAX_SERVER_PLAYERS || !newAuthority->m_bIsHost)
    {
        g_persistentState.fill(0);
        g_lastRequestSequences.fill(0);
        g_requestRates.fill({});
        g_serverRunId = 0;
        g_stateRevision = 0;
        g_eventSequence = 0;
        g_authorityPlayerId = CHEAT_INVALID_PLAYER_ID;
        g_gameplaySpeedStep = 0;
        return;
    }

    EnsureRunId();
    g_authorityPlayerId = static_cast<uint8_t>(newAuthority->m_iPlayerId);
    g_lastRequestSequences[g_authorityPlayerId] = 0;
    g_requestRates[g_authorityPlayerId] = {};
    NextNonZero(g_stateRevision);
    BroadcastState();
}

void CCheatAuthorityManager::SendSnapshot(CNetworkPlayer* player)
{
    EnsureRunId();
    if (player == nullptr || g_authorityPlayerId >= Config::MAX_SERVER_PLAYERS || g_stateRevision == 0)
        return;
    CheatStateEvent event = BuildStateEvent(false, 0, eStockCheat::FASTER_CLOCK);
    if (event.HasValidPayload())
        GetPacketFactory().Send(event, player);
}
