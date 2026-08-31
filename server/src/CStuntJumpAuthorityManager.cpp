#include "stdafx.h"
#include "CStuntJumpAuthorityManager.h"

#include <chrono>
#include <cmath>

using namespace Packets::Stunts;

std::array<CStuntJumpAuthorityManager::Slot, STUNT_JUMP_CAPACITY> CStuntJumpAuthorityManager::m_slots{};
std::array<CStuntJumpAuthorityManager::Attempt, Config::MAX_SERVER_PLAYERS>
    CStuntJumpAuthorityManager::m_attempts{};
std::array<CStuntJumpAuthorityManager::RateLimit, Config::MAX_SERVER_PLAYERS>
    CStuntJumpAuthorityManager::m_definitionRates{};
std::array<CStuntJumpAuthorityManager::RateLimit, Config::MAX_SERVER_PLAYERS>
    CStuntJumpAuthorityManager::m_attemptRates{};
uint64_t CStuntJumpAuthorityManager::m_serverRunId = 0;
uint16_t CStuntJumpAuthorityManager::m_catalogCount = 0;
uint32_t CStuntJumpAuthorityManager::m_catalogHash = 0;
bool CStuntJumpAuthorityManager::m_catalogSealed = false;
uint8_t CStuntJumpAuthorityManager::m_authorityPlayerId = STUNT_INVALID_PLAYER_ID;
uint32_t CStuntJumpAuthorityManager::m_nextRevision = 0;
uint32_t CStuntJumpAuthorityManager::m_nextAwardSequence = 0;

namespace
{
float DistanceSquared(const CVector& left, const CVector& right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}

bool NearlyEqual(float left, float right)
{
    return std::abs(left - right) <= 0.011f;
}

bool PositionsMatch(const CVector& left, const CVector& right)
{
    return NearlyEqual(left.x, right.x) && NearlyEqual(left.y, right.y) && NearlyEqual(left.z, right.z);
}
}  // namespace

bool CStuntJumpAuthorityManager::IsAuthenticatedPlayer(const CNetworkPlayer* player)
{
    return player != nullptr && player->m_pPeer != nullptr && player->m_iPlayerId >= 0 &&
           player->m_iPlayerId < Config::MAX_SERVER_PLAYERS &&
           CNetworkPlayerManager::GetPlayer(player->m_iPlayerId) == player;
}

bool CStuntJumpAuthorityManager::IsCurrentHost(const CNetworkPlayer* player)
{
    return IsAuthenticatedPlayer(player) && player->m_bIsHost &&
           CNetworkPlayerManager::GetHost() == player;
}

bool CStuntJumpAuthorityManager::AcceptRate(CNetworkPlayer* player,
    std::array<RateLimit, Config::MAX_SERVER_PLAYERS>& rates, uint16_t maximumEvents)
{
    if (!IsAuthenticatedPlayer(player))
    {
        return false;
    }

    RateLimit& rate = rates[player->m_iPlayerId];
    const uint32_t now = enet_time_get();
    const uint32_t connectId = player->m_pPeer->connectID;
    if (rate.owner != player || rate.connectId != connectId || now - rate.windowStartedAt >= RATE_WINDOW_MS)
    {
        rate.owner = player;
        rate.connectId = connectId;
        rate.windowStartedAt = now;
        rate.eventCount = 0;
    }
    if (rate.eventCount >= maximumEvents)
    {
        return false;
    }
    ++rate.eventCount;
    return true;
}

uint64_t CStuntJumpAuthorityManager::EnsureServerRunId()
{
    if (m_serverRunId == 0)
    {
        const uint64_t wallClock = static_cast<uint64_t>(
            std::chrono::high_resolution_clock::now().time_since_epoch().count());
        const uint64_t monotonic = static_cast<uint64_t>(
            std::chrono::steady_clock::now().time_since_epoch().count());
        m_serverRunId = wallClock ^ (monotonic << 1) ^ 0x5354554E5452554EULL;
        if (m_serverRunId == 0)
        {
            m_serverRunId = 1;
        }
    }
    return m_serverRunId;
}

uint32_t CStuntJumpAuthorityManager::NextRevision()
{
    do
    {
        ++m_nextRevision;
    } while (m_nextRevision == 0);
    return m_nextRevision;
}

uint32_t CStuntJumpAuthorityManager::NextAwardSequence()
{
    do
    {
        ++m_nextAwardSequence;
    } while (m_nextAwardSequence == 0);
    return m_nextAwardSequence;
}

size_t CStuntJumpAuthorityManager::CountRegistered()
{
    size_t count = 0;
    for (const Slot& slot : m_slots)
    {
        count += slot.registered ? 1u : 0u;
    }
    return count;
}

size_t CStuntJumpAuthorityManager::CountCompleted()
{
    size_t count = 0;
    for (const Slot& slot : m_slots)
    {
        count += slot.registered && slot.completed ? 1u : 0u;
    }
    return count;
}

uint32_t CStuntJumpAuthorityManager::ComputeCatalogHash()
{
    uint32_t hash = 2166136261u;
    for (const Slot& slot : m_slots)
    {
        if (slot.registered)
        {
            hash = AccumulateCatalogHash(hash, slot.id);
        }
    }
    return hash == 0 ? 1u : hash;
}

bool CStuntJumpAuthorityManager::DefinitionsMatch(
    const StuntDefinition& left, const StuntDefinition& right)
{
    return left.reward == right.reward && PositionsMatch(left.start.minimum, right.start.minimum) &&
           PositionsMatch(left.start.maximum, right.start.maximum) &&
           PositionsMatch(left.finish.minimum, right.finish.minimum) &&
           PositionsMatch(left.finish.maximum, right.finish.maximum) &&
           PositionsMatch(left.camera, right.camera);
}

bool CStuntJumpAuthorityManager::IsExactDriver(const CNetworkPlayer* player, uint16_t vehicleId)
{
    if (!IsAuthenticatedPlayer(player) || !player->m_bHasOnFootSnapshot || !player->m_bIsAlive ||
        player->m_nVehicleId != vehicleId || player->m_nSeatId != 0)
    {
        return false;
    }
    const CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(vehicleId);
    return vehicle != nullptr && vehicle->m_pPlayers[0] == player;
}

void CStuntJumpAuthorityManager::ResetUnsealedCatalog()
{
    if (m_catalogSealed)
    {
        return;
    }
    m_slots = {};
    m_catalogCount = 0;
    m_catalogHash = 0;
}

StuntStateEvent CStuntJumpAuthorityManager::MakeState(const Slot& slot)
{
    StuntStateEvent state{};
    state.serverRunId = EnsureServerRunId();
    state.catalogCount = m_catalogCount;
    state.catalogHash = m_catalogHash;
    state.revision = slot.revision;
    state.authorityPlayerId = m_authorityPlayerId;
    state.id = slot.id;
    state.definition = slot.definition;
    state.completed = slot.completed;
    state.completedByPlayerId = slot.completedByPlayerId;
    state.collectorSessionNonce = slot.collectorSessionNonce;
    state.awardSequence = slot.awardSequence;
    state.rewardAmount = slot.rewardAmount;
    state.allCompleted = slot.allCompleted;
    return state;
}

void CStuntJumpAuthorityManager::SendState(const Slot& slot, CNetworkPlayer* player)
{
    if (!m_catalogSealed || !slot.registered || m_authorityPlayerId >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }
    StuntStateEvent state = MakeState(slot);
    if (!state.HasValidPayload() || !state.FitsSerializedBudget())
    {
        logger::warn("Refused to serialize an invalid stunt state");
        return;
    }
    if (player != nullptr)
    {
        GetPacketFactory().Send(state, player);
    }
    else
    {
        GetPacketFactory().SendToAll(state);
    }
}

void CStuntJumpAuthorityManager::BroadcastCatalog()
{
    if (!m_catalogSealed)
    {
        return;
    }
    for (const Slot& slot : m_slots)
    {
        if (slot.registered)
        {
            SendState(slot);
        }
    }
}

void CStuntJumpAuthorityManager::SendAttemptResult(CNetworkPlayer* player, uint32_t requestId,
    uint64_t clientSessionNonce, eStuntAttemptAction action, const StuntId& id, bool accepted,
    eStuntAttemptResultReason reason, uint16_t retryAfterMs)
{
    if (!IsAuthenticatedPlayer(player))
    {
        return;
    }
    StuntAttemptResult result{};
    result.requestId = requestId;
    result.clientSessionNonce = clientSessionNonce;
    result.action = action;
    result.id = id;
    result.accepted = accepted;
    result.reason = reason;
    result.retryAfterMs = retryAfterMs;
    if (result.HasValidPayload())
    {
        GetPacketFactory().Send(result, player);
    }
}

void CStuntJumpAuthorityManager::SendAttemptResult(CNetworkPlayer* player,
    const StuntAttempt& packet, bool accepted, eStuntAttemptResultReason reason, uint16_t retryAfterMs)
{
    SendAttemptResult(player, packet.requestId, packet.clientSessionNonce, packet.action, packet.id,
        accepted, reason, retryAfterMs);
}

void CStuntJumpAuthorityManager::RejectActiveAttempt(Attempt& attempt, eStuntAttemptResultReason reason)
{
    if (!attempt.active)
    {
        return;
    }
    CNetworkPlayer* player = attempt.playerId < Config::MAX_SERVER_PLAYERS
        ? CNetworkPlayerManager::GetPlayer(attempt.playerId)
        : nullptr;
    if (IsAuthenticatedPlayer(player) && player->m_pPeer->connectID == attempt.connectId)
    {
        SendAttemptResult(player, attempt.requestId, attempt.clientSessionNonce,
            attempt.lastAcceptedAction, attempt.id, false, reason);
    }
    attempt = {};
}

void CStuntJumpAuthorityManager::RejectCompetingAttempts(const StuntId& id)
{
    for (Attempt& attempt : m_attempts)
    {
        if (attempt.active && attempt.id == id)
        {
            RejectActiveAttempt(attempt, eStuntAttemptResultReason::ALREADY_COMPLETED);
        }
    }
}

void CStuntJumpAuthorityManager::Update()
{
    const uint32_t now = enet_time_get();
    for (Attempt& attempt : m_attempts)
    {
        if (attempt.active && now - attempt.startedAt > ATTEMPT_TIMEOUT_MS)
        {
            RejectActiveAttempt(attempt, eStuntAttemptResultReason::TIMEOUT);
        }
    }
}

bool CStuntJumpAuthorityManager::HandleDefinition(
    CNetworkPlayer* player, const StuntDefinitionAnnounce& packet)
{
    if (!IsCurrentHost(player) || !AcceptRate(player, m_definitionRates, MAX_DEFINITIONS_PER_WINDOW) ||
        !packet.HasValidPayload())
    {
        return false;
    }

    EnsureServerRunId();
    if (m_catalogCount == 0)
    {
        m_catalogCount = packet.catalogCount;
        m_catalogHash = packet.catalogHash;
    }
    if (packet.catalogCount != m_catalogCount || packet.catalogHash != m_catalogHash)
    {
        logger::warn("Host announced a conflicting stunt catalog");
        return false;
    }

    Slot& slot = m_slots[packet.id.slot];
    if (slot.registered)
    {
        if (slot.id != packet.id || !DefinitionsMatch(slot.definition, packet.definition))
        {
            logger::warn("Host attempted to redefine stunt slot %u", packet.id.slot);
            return false;
        }
        return true;
    }
    if (m_catalogSealed)
    {
        return false;
    }

    slot.registered = true;
    slot.completed = packet.initiallyCompleted;
    slot.id = packet.id;
    slot.definition = packet.definition;
    slot.revision = NextRevision();

    if (CountRegistered() > m_catalogCount)
    {
        ResetUnsealedCatalog();
        return false;
    }
    if (CountRegistered() == m_catalogCount)
    {
        if (ComputeCatalogHash() != m_catalogHash)
        {
            logger::warn("Rejected a stunt catalog with a mismatched aggregate hash");
            ResetUnsealedCatalog();
            return false;
        }
        m_catalogSealed = true;
        BroadcastCatalog();
    }
    return true;
}

bool CStuntJumpAuthorityManager::HandleAttempt(CNetworkPlayer* player, const StuntAttempt& packet)
{
    if (!IsAuthenticatedPlayer(player) || !packet.HasValidPayload())
    {
        return false;
    }
    if (!AcceptRate(player, m_attemptRates, MAX_ATTEMPT_EVENTS_PER_WINDOW))
    {
        SendAttemptResult(player, packet, false, eStuntAttemptResultReason::RATE_LIMITED, 250);
        return false;
    }
    if (!m_catalogSealed)
    {
        SendAttemptResult(player, packet, false, eStuntAttemptResultReason::CATALOG_NOT_READY, 250);
        return false;
    }
    if (packet.id.slot >= m_slots.size())
    {
        SendAttemptResult(player, packet, false, eStuntAttemptResultReason::INVALID_STUNT);
        return false;
    }

    Slot& slot = m_slots[packet.id.slot];
    if (!slot.registered || slot.id != packet.id)
    {
        SendAttemptResult(player, packet, false, eStuntAttemptResultReason::INVALID_STUNT);
        return false;
    }

    Attempt& attempt = m_attempts[player->m_iPlayerId];
    const bool matches = attempt.active && attempt.connectId == player->m_pPeer->connectID &&
                         attempt.requestId == packet.requestId &&
                         attempt.clientSessionNonce == packet.clientSessionNonce &&
                         attempt.id == packet.id && attempt.vehicleId == packet.vehicleId;

    if (slot.completed)
    {
        const bool duplicateCompletion = packet.action == eStuntAttemptAction::COMPLETE &&
            slot.completedByPlayerId == player->m_iPlayerId &&
            slot.collectorSessionNonce == packet.clientSessionNonce &&
            slot.completionRequestId == packet.requestId;
        SendAttemptResult(player, packet, duplicateCompletion,
            duplicateCompletion ? eStuntAttemptResultReason::NONE
                                : eStuntAttemptResultReason::ALREADY_COMPLETED);
        return duplicateCompletion;
    }

    if (matches && packet.action == eStuntAttemptAction::START)
    {
        SendAttemptResult(player, packet, true, eStuntAttemptResultReason::NONE);
        return true;
    }
    if (matches && packet.action == eStuntAttemptAction::HIT_FINISH && attempt.finishHitAt != 0)
    {
        SendAttemptResult(player, packet, true, eStuntAttemptResultReason::NONE);
        return true;
    }
    if (packet.action == eStuntAttemptAction::CANCEL)
    {
        if (!matches)
        {
            SendAttemptResult(player, packet, false, eStuntAttemptResultReason::INVALID_TRANSITION);
            return false;
        }
        attempt = {};
        SendAttemptResult(player, packet, true, eStuntAttemptResultReason::NONE);
        return true;
    }

    const uint32_t now = enet_time_get();
    CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(packet.vehicleId);
    if (!IsExactDriver(player, packet.vehicleId) || vehicle == nullptr ||
        vehicle->m_nLastDriverSnapshotAt == 0 ||
        now - vehicle->m_nLastDriverSnapshotAt > DRIVER_SNAPSHOT_MAX_AGE_MS ||
        DistanceSquared(packet.position, vehicle->m_vecPosition) > 64.0f ||
        DistanceSquared(packet.moveSpeed, vehicle->m_vecVelocity) > 0.25f)
    {
        SendAttemptResult(
            player, packet, false, eStuntAttemptResultReason::DRIVER_SNAPSHOT_NOT_READY, 150);
        return false;
    }

    if (packet.action == eStuntAttemptAction::START)
    {
        const float speedSquared = vehicle->m_vecVelocity.x * vehicle->m_vecVelocity.x +
                                   vehicle->m_vecVelocity.y * vehicle->m_vecVelocity.y +
                                   vehicle->m_vecVelocity.z * vehicle->m_vecVelocity.z;
        if (attempt.active || !slot.definition.start.Contains(vehicle->m_vecPosition, 3.0f) ||
            !slot.definition.start.Contains(packet.position, 1.0f) || speedSquared < 0.16f)
        {
            SendAttemptResult(player, packet, false,
                attempt.active ? eStuntAttemptResultReason::INVALID_TRANSITION
                               : eStuntAttemptResultReason::OUT_OF_RANGE);
            return false;
        }
        attempt.active = true;
        attempt.playerId = static_cast<uint8_t>(player->m_iPlayerId);
        attempt.connectId = player->m_pPeer->connectID;
        attempt.requestId = packet.requestId;
        attempt.clientSessionNonce = packet.clientSessionNonce;
        attempt.id = packet.id;
        attempt.vehicleId = packet.vehicleId;
        attempt.startedAt = now;
        attempt.lastAcceptedAction = eStuntAttemptAction::START;
        attempt.startPosition = vehicle->m_vecPosition;
        SendAttemptResult(player, packet, true, eStuntAttemptResultReason::NONE);
        return true;
    }

    if (!matches)
    {
        SendAttemptResult(player, packet, false, eStuntAttemptResultReason::INVALID_TRANSITION);
        return false;
    }

    const uint32_t elapsed = now - attempt.startedAt;
    const float travelDistanceSquared = DistanceSquared(attempt.startPosition, vehicle->m_vecPosition);
    const float maximumTravelDistance = static_cast<float>(elapsed) * 0.25f + 25.0f;
    if (packet.action == eStuntAttemptAction::HIT_FINISH)
    {
        if (attempt.finishHitAt != 0 || elapsed < MIN_COMPLETION_TIME_MS ||
            elapsed > ATTEMPT_TIMEOUT_MS ||
            !slot.definition.finish.Contains(vehicle->m_vecPosition, 4.0f) ||
            !slot.definition.finish.Contains(packet.position, 1.0f) ||
            travelDistanceSquared > maximumTravelDistance * maximumTravelDistance)
        {
            attempt = {};
            SendAttemptResult(player, packet, false,
                elapsed > ATTEMPT_TIMEOUT_MS ? eStuntAttemptResultReason::TIMEOUT
                                             : eStuntAttemptResultReason::OUT_OF_RANGE);
            return false;
        }
        attempt.finishHitAt = now;
        attempt.lastAcceptedAction = eStuntAttemptAction::HIT_FINISH;
        SendAttemptResult(player, packet, true, eStuntAttemptResultReason::NONE);
        return true;
    }

    if (attempt.finishHitAt == 0 || elapsed > ATTEMPT_TIMEOUT_MS ||
        now - attempt.finishHitAt > 5000 ||
        travelDistanceSquared > maximumTravelDistance * maximumTravelDistance)
    {
        const bool timedOut = elapsed > ATTEMPT_TIMEOUT_MS ||
                              (attempt.finishHitAt != 0 && now - attempt.finishHitAt > 5000);
        attempt = {};
        SendAttemptResult(player, packet, false,
            timedOut ? eStuntAttemptResultReason::TIMEOUT
                     : eStuntAttemptResultReason::OUT_OF_RANGE);
        return false;
    }

    const bool completesCatalog = CountCompleted() + 1 == m_catalogCount;
    slot.completed = true;
    slot.completedByPlayerId = static_cast<uint8_t>(player->m_iPlayerId);
    slot.collectorSessionNonce = packet.clientSessionNonce;
    slot.completionRequestId = packet.requestId;
    slot.awardSequence = NextAwardSequence();
    slot.rewardAmount = completesCatalog ? 10000 : slot.definition.reward;
    slot.allCompleted = completesCatalog;
    slot.revision = NextRevision();
    attempt = {};
    SendAttemptResult(player, packet, true, eStuntAttemptResultReason::NONE);
    RejectCompetingAttempts(slot.id);
    SendState(slot);
    return true;
}

void CStuntJumpAuthorityManager::SendSnapshot(CNetworkPlayer* player)
{
    if (!IsAuthenticatedPlayer(player) || !m_catalogSealed)
    {
        return;
    }
    for (const Slot& slot : m_slots)
    {
        if (slot.registered)
        {
            SendState(slot, player);
        }
    }
}

void CStuntJumpAuthorityManager::HandlePlayerDisconnected(CNetworkPlayer* player)
{
    if (player == nullptr || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }
    m_attempts[player->m_iPlayerId] = {};
    m_definitionRates[player->m_iPlayerId] = {};
    m_attemptRates[player->m_iPlayerId] = {};
}

void CStuntJumpAuthorityManager::HandleAuthorityChange(CNetworkPlayer* newHost)
{
    const uint8_t newAuthority = IsAuthenticatedPlayer(newHost)
        ? static_cast<uint8_t>(newHost->m_iPlayerId)
        : STUNT_INVALID_PLAYER_ID;
    if (newAuthority == m_authorityPlayerId)
    {
        return;
    }

    m_authorityPlayerId = newAuthority;
    for (Attempt& attempt : m_attempts)
    {
        RejectActiveAttempt(attempt, eStuntAttemptResultReason::INVALID_TRANSITION);
    }
    if (m_catalogSealed && m_authorityPlayerId < Config::MAX_SERVER_PLAYERS)
    {
        for (Slot& slot : m_slots)
        {
            if (slot.registered)
            {
                slot.revision = NextRevision();
            }
        }
        BroadcastCatalog();
    }
}
