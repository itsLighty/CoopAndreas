#include "stdafx.h"
#include "CPickupAuthorityManager.h"

#include <algorithm>
#include <cmath>

using namespace Packets::Pickups;

std::array<CPickupAuthorityManager::PickupSlot, PICKUP_POOL_CAPACITY> CPickupAuthorityManager::m_slots{};
std::array<CPickupAuthorityManager::PendingCollect, CPickupAuthorityManager::MAX_PENDING_COLLECT_REQUESTS>
    CPickupAuthorityManager::m_pendingCollects{};
std::array<CPickupAuthorityManager::PendingCreate, CPickupAuthorityManager::MAX_PENDING_CREATE_INTENTS>
    CPickupAuthorityManager::m_pendingCreates{};
std::array<CPickupAuthorityManager::RateLimitSlot, Config::MAX_SERVER_PLAYERS>
    CPickupAuthorityManager::m_collectRateLimits{};
std::array<CPickupAuthorityManager::RateLimitSlot, Config::MAX_SERVER_PLAYERS>
    CPickupAuthorityManager::m_createRateLimits{};
uint32_t CPickupAuthorityManager::m_nextRequestId = 0;

namespace
{
float DistanceSquared(const WorldPositionCompressed& left, const WorldPositionCompressed& right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}
}  // namespace

bool CPickupAuthorityManager::IsAuthenticatedPlayer(const CNetworkPlayer* player)
{
    return player != nullptr && player->m_iPlayerId >= 0 &&
           player->m_iPlayerId < Config::MAX_SERVER_PLAYERS &&
           CNetworkPlayerManager::GetPlayer(player->m_iPlayerId) == player;
}

bool CPickupAuthorityManager::IsCurrentHost(const CNetworkPlayer* player)
{
    return IsAuthenticatedPlayer(player) && player->m_bIsHost &&
           CNetworkPlayerManager::GetHost() == player;
}

bool CPickupAuthorityManager::CanAcceptRateLimitedEvent(CNetworkPlayer* player,
    std::array<RateLimitSlot, Config::MAX_SERVER_PLAYERS>& slots, uint16_t maximumEvents)
{
    if (!IsAuthenticatedPlayer(player) || player->m_pPeer == nullptr)
    {
        return false;
    }

    RateLimitSlot& slot = slots[player->m_iPlayerId];
    const uint32_t now = enet_time_get();
    const uint32_t connectId = player->m_pPeer->connectID;
    if (slot.owner != player || slot.connectId != connectId || now - slot.windowStartedAt >= RATE_WINDOW_MS)
    {
        slot.owner = player;
        slot.connectId = connectId;
        slot.windowStartedAt = now;
        slot.eventCount = 0;
    }
    if (slot.eventCount >= maximumEvents)
    {
        return false;
    }
    ++slot.eventCount;
    return true;
}

bool CPickupAuthorityManager::IsExpired(uint32_t now, uint32_t deadline)
{
    return deadline != 0 && static_cast<int32_t>(now - deadline) >= 0;
}

uint32_t CPickupAuthorityManager::NextRequestId()
{
    do
    {
        ++m_nextRequestId;
        if (m_nextRequestId == 0)
        {
            ++m_nextRequestId;
        }
    } while (FindPendingCollect(m_nextRequestId) != nullptr || FindPendingCreate(m_nextRequestId) != nullptr);
    return m_nextRequestId;
}

uint32_t CPickupAuthorityManager::NextRevision(uint32_t revision)
{
    ++revision;
    return revision == 0 ? 1 : revision;
}

bool CPickupAuthorityManager::MaterializeDueRespawn(PickupSlot& slot, CNetworkPlayer* host,
    uint32_t now, PickupStateEvent& respawn)
{
    if (slot.active || !slot.state.hasCompletionState || slot.respawnAt == 0 ||
        !IsExpired(now, slot.respawnAt) || !IsCurrentHost(host))
    {
        return false;
    }

    respawn = {};
    respawn.state.id.slot = slot.state.id.slot;
    respawn.state.id.generation = static_cast<uint16_t>(slot.lastGeneration + 1);
    if (respawn.state.id.generation == 0)
    {
        respawn.state.id.generation = 1;
    }
    respawn.state.revision = 1;
    respawn.state.authorityPlayerId = static_cast<uint8_t>(host->m_iPlayerId);
    respawn.state.active = true;
    respawn.state.creatorPlayerId = static_cast<uint8_t>(host->m_iPlayerId);
    respawn.state.metadata = slot.state.metadata;
    slot.active = true;
    slot.lastGeneration = respawn.state.id.generation;
    slot.lastRevision = respawn.state.revision;
    slot.expiresAt = respawn.state.metadata.expiresAfterMs == 0
        ? 0
        : now + respawn.state.metadata.expiresAfterMs;
    slot.respawnAt = 0;
    slot.state = respawn.state;
    return true;
}

CPickupAuthorityManager::PendingCollect* CPickupAuthorityManager::FindPendingCollect(uint32_t requestId)
{
    for (PendingCollect& pending : m_pendingCollects)
    {
        if (pending.active && pending.requestId == requestId)
        {
            return &pending;
        }
    }
    return nullptr;
}

CPickupAuthorityManager::PendingCreate* CPickupAuthorityManager::FindPendingCreate(uint32_t requestId)
{
    for (PendingCreate& pending : m_pendingCreates)
    {
        if (pending.active && pending.requestId == requestId)
        {
            return &pending;
        }
    }
    return nullptr;
}

bool CPickupAuthorityManager::MetadataMatches(const PickupMetadata& left, const PickupMetadata& right)
{
    constexpr float POSITION_EPSILON = 0.01f;
    return left.kind == right.kind && std::fabs(left.position.x - right.position.x) <= POSITION_EPSILON &&
           std::fabs(left.position.y - right.position.y) <= POSITION_EPSILON &&
           std::fabs(left.position.z - right.position.z) <= POSITION_EPSILON &&
           left.interior == right.interior && left.modelId == right.modelId && left.reward == right.reward &&
           left.ammo == right.ammo && left.collectibleIndex == right.collectibleIndex &&
           left.expiresAfterMs == right.expiresAfterMs && left.respawnsAfterMs == right.respawnsAfterMs;
}

void CPickupAuthorityManager::SendDenied(const PendingCollect& pending)
{
    if (!pending.active || !IsAuthenticatedPlayer(pending.requester))
    {
        return;
    }
    PickupCollectResult result{};
    result.requestId = pending.requestId;
    result.id = pending.id;
    result.collectorPlayerId = static_cast<uint8_t>(pending.requester->m_iPlayerId);
    result.approved = false;
    GetPacketFactory().Send(result, pending.requester);
}

void CPickupAuthorityManager::ClearAllPending()
{
    for (PendingCollect& pending : m_pendingCollects)
    {
        SendDenied(pending);
        pending = {};
    }
    m_pendingCreates.fill(PendingCreate{});
}

void CPickupAuthorityManager::Update()
{
    const uint32_t now = enet_time_get();
    for (PendingCollect& pending : m_pendingCollects)
    {
        if (pending.active && now - pending.createdAt >= PENDING_REQUEST_TIMEOUT_MS)
        {
            SendDenied(pending);
            pending = {};
        }
    }
    for (PendingCreate& pending : m_pendingCreates)
    {
        if (pending.active && now - pending.createdAt >= PENDING_REQUEST_TIMEOUT_MS)
        {
            pending = {};
        }
    }

    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();
    for (PickupSlot& slot : m_slots)
    {
        PickupStateEvent respawn{};
        if (MaterializeDueRespawn(slot, host, now, respawn))
        {
            GetPacketFactory().SendToAll(respawn);
            continue;
        }
        if (!slot.active || !IsExpired(now, slot.expiresAt))
        {
            continue;
        }

        PickupStateEvent removal{};
        removal.state.id = slot.state.id;
        removal.state.revision = NextRevision(slot.lastRevision);
        removal.state.authorityPlayerId = host != nullptr
            ? static_cast<uint8_t>(host->m_iPlayerId)
            : slot.state.authorityPlayerId;
        removal.state.active = false;
        slot.active = false;
        slot.lastRevision = removal.state.revision;
        slot.expiresAt = 0;
        slot.respawnAt = 0;
        slot.state = removal.state;
        GetPacketFactory().SendToAll(removal);
    }
}

bool CPickupAuthorityManager::HandleState(CNetworkPlayer* player, const PickupStateEvent& packet)
{
    if (!IsCurrentHost(player) || !packet.HasValidPayload() || !packet.FitsSerializedBudget() ||
        packet.state.authorityPlayerId != player->m_iPlayerId)
    {
        logger::warn("Rejected unauthorized or malformed pickup state");
        return false;
    }

    const PickupState& incoming = packet.state;
    PickupSlot& slot = m_slots[incoming.id.slot];
    const bool firstGeneration = slot.lastGeneration == 0;
    const bool sameGeneration = !firstGeneration && incoming.id.generation == slot.lastGeneration;
    const bool newerGeneration = !firstGeneration &&
        IsPickupGenerationNewer(incoming.id.generation, slot.lastGeneration);

    if (incoming.active)
    {
        if ((!firstGeneration && !sameGeneration && !newerGeneration) ||
            (newerGeneration && slot.active) || (sameGeneration && !slot.active) ||
            (sameGeneration && !IsPickupRevisionNewer(incoming.revision, slot.lastRevision)) ||
            (sameGeneration && (incoming.creatorPlayerId != slot.state.creatorPlayerId ||
                incoming.sourceIntentRequestId != slot.state.sourceIntentRequestId ||
                incoming.metadata.kind != slot.state.metadata.kind)))
        {
            logger::warn("Rejected stale, replayed, or identity-changing pickup state");
            return false;
        }

        PendingCreate* sourceIntent = nullptr;
        if (sameGeneration)
        {
            // Creation intents are consumed exactly once when a generation is first published. Later revisions
            // retain the canonical creator/source identity without requiring the expired pending entry.
        }
        else if (incoming.sourceIntentRequestId == 0)
        {
            if (incoming.creatorPlayerId != player->m_iPlayerId)
            {
                logger::warn("Rejected pickup state with spoofed creator identity");
                return false;
            }
        }
        else
        {
            sourceIntent = FindPendingCreate(incoming.sourceIntentRequestId);
            if (sourceIntent == nullptr || !IsAuthenticatedPlayer(sourceIntent->requester) ||
                incoming.creatorPlayerId != sourceIntent->requester->m_iPlayerId ||
                !MetadataMatches(incoming.metadata, sourceIntent->metadata))
            {
                logger::warn("Rejected pickup state that did not match its canonical creation intent");
                return false;
            }
        }

        PickupStateEvent canonical = packet;
        canonical.state.authorityPlayerId = static_cast<uint8_t>(player->m_iPlayerId);
        slot.active = true;
        slot.lastGeneration = canonical.state.id.generation;
        slot.lastRevision = canonical.state.revision;
        slot.state = canonical.state;
        slot.expiresAt = canonical.state.metadata.expiresAfterMs == 0
            ? 0
            : enet_time_get() + canonical.state.metadata.expiresAfterMs;
        slot.respawnAt = 0;
        if (sourceIntent != nullptr)
        {
            *sourceIntent = {};
        }
        GetPacketFactory().SendToAll(canonical, player);
        return true;
    }

    if (incoming.hasCompletionState || !sameGeneration || !slot.active ||
        !IsPickupRevisionNewer(incoming.revision, slot.lastRevision))
    {
        logger::warn("Rejected stale or unknown pickup removal");
        return false;
    }

    PickupStateEvent canonical = packet;
    canonical.state.authorityPlayerId = static_cast<uint8_t>(player->m_iPlayerId);
    slot.active = false;
    slot.lastRevision = canonical.state.revision;
    slot.expiresAt = 0;
    slot.respawnAt = 0;
    slot.state = canonical.state;
    GetPacketFactory().SendToAll(canonical, player);
    return true;
}

bool CPickupAuthorityManager::HandleCollectRequest(CNetworkPlayer* player, const PickupCollectRequest& packet)
{
    if (!IsAuthenticatedPlayer(player) || !packet.HasValidPayload() || packet.requestId != 0 ||
        !CanAcceptRateLimitedEvent(player, m_collectRateLimits, MAX_COLLECT_REQUESTS_PER_WINDOW))
    {
        logger::warn("Rejected malformed or rate-limited pickup collection request");
        return false;
    }

    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();
    PickupSlot& slot = m_slots[packet.id.slot];
    const uint32_t now = enet_time_get();
    if (host == nullptr || !slot.active || slot.state.id.generation != packet.id.generation ||
        IsExpired(now, slot.expiresAt) || packet.interior != slot.state.metadata.interior ||
        DistanceSquared(packet.requesterPosition, slot.state.metadata.position) >
            MAX_PICKUP_COLLECT_DISTANCE * MAX_PICKUP_COLLECT_DISTANCE)
    {
        logger::warn("Rejected stale, expired, or out-of-range pickup collection request");
        return false;
    }

    for (const PendingCollect& pending : m_pendingCollects)
    {
        if (pending.active && pending.requester == player && pending.id == packet.id)
        {
            logger::warn("Rejected duplicate pending pickup collection request");
            return false;
        }
    }

    auto freePending = std::find_if(m_pendingCollects.begin(), m_pendingCollects.end(),
        [](const PendingCollect& pending) { return !pending.active; });
    if (freePending == m_pendingCollects.end())
    {
        logger::warn("Rejected pickup collection request because the pending table is full");
        return false;
    }

    PickupCollectRequest canonical = packet;
    canonical.requestId = NextRequestId();
    canonical.requesterPlayerId.value = player->m_iPlayerId;
    freePending->active = true;
    freePending->requestId = canonical.requestId;
    freePending->createdAt = now;
    freePending->requester = player;
    freePending->id = packet.id;
    GetPacketFactory().Send(canonical, host);
    return true;
}

bool CPickupAuthorityManager::HandleCollectDecision(CNetworkPlayer* player, const PickupCollectDecision& packet)
{
    if (!IsCurrentHost(player) || packet.requestId == 0 || !packet.id.IsValid())
    {
        logger::warn("Rejected unauthorized or malformed pickup collection decision");
        return false;
    }

    PendingCollect* pending = FindPendingCollect(packet.requestId);
    if (pending == nullptr || !(pending->id == packet.id))
    {
        logger::warn("Rejected spoofed, stale, or unknown pickup collection decision");
        return false;
    }

    PickupSlot& slot = m_slots[packet.id.slot];
    const bool stateCanStillGrant = slot.active && slot.state.id == packet.id &&
        !IsExpired(enet_time_get(), slot.expiresAt);
    if (!packet.approved || !stateCanStillGrant || !IsAuthenticatedPlayer(pending->requester))
    {
        SendDenied(*pending);
        *pending = {};
        return !packet.approved;
    }

    PickupCollectResult granted{};
    granted.requestId = pending->requestId;
    granted.id = pending->id;
    granted.collectorPlayerId = static_cast<uint8_t>(pending->requester->m_iPlayerId);
    granted.approved = true;
    granted.grantedState = slot.state;

    slot.active = false;
    slot.collectedGeneration = slot.lastGeneration;
    slot.expiresAt = 0;
    const bool permanentCollectible = IsCollectiblePickupKind(granted.grantedState.metadata.kind) &&
        granted.grantedState.metadata.respawnsAfterMs == 0;
    const bool scheduledRespawn = granted.grantedState.metadata.respawnsAfterMs > 0;
    PickupStateEvent completion{};
    completion.state.id = granted.grantedState.id;
    completion.state.revision = NextRevision(slot.lastRevision);
    completion.state.authorityPlayerId = granted.grantedState.authorityPlayerId;
    completion.state.active = false;
    completion.state.hasCompletionState = permanentCollectible || scheduledRespawn;
    if (completion.state.hasCompletionState)
    {
        completion.state.metadata = granted.grantedState.metadata;
        completion.state.respawnRemainingMs = scheduledRespawn
            ? granted.grantedState.metadata.respawnsAfterMs
            : 0;
    }
    slot.lastRevision = completion.state.revision;
    slot.state = completion.state;
    slot.respawnAt = scheduledRespawn
        ? enet_time_get() + granted.grantedState.metadata.respawnsAfterMs
        : 0;
    *pending = {};

    for (PendingCollect& competing : m_pendingCollects)
    {
        if (competing.active && competing.id == packet.id)
        {
            SendDenied(competing);
            competing = {};
        }
    }
    GetPacketFactory().SendToAll(granted);
    if (completion.state.hasCompletionState)
    {
        GetPacketFactory().SendToAll(completion);
    }
    return true;
}

bool CPickupAuthorityManager::HandleCreateIntent(CNetworkPlayer* player, const PickupCreateIntent& packet)
{
    if (!IsAuthenticatedPlayer(player) || IsCurrentHost(player) || packet.requestId != 0 ||
        !packet.HasValidPayload() ||
        !CanAcceptRateLimitedEvent(player, m_createRateLimits, MAX_CREATE_INTENTS_PER_WINDOW))
    {
        logger::warn("Rejected unauthorized, malformed, or rate-limited pickup creation intent");
        return false;
    }

    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();
    if (host == nullptr)
    {
        return false;
    }
    for (const PendingCreate& pending : m_pendingCreates)
    {
        if (pending.active && pending.requester == player && MetadataMatches(pending.metadata, packet.metadata))
        {
            logger::warn("Rejected duplicate pending pickup creation intent");
            return false;
        }
    }

    auto freePending = std::find_if(m_pendingCreates.begin(), m_pendingCreates.end(),
        [](const PendingCreate& pending) { return !pending.active; });
    if (freePending == m_pendingCreates.end())
    {
        logger::warn("Rejected pickup creation intent because the pending table is full");
        return false;
    }

    PickupCreateIntent canonical = packet;
    canonical.requestId = NextRequestId();
    canonical.requesterPlayerId.value = player->m_iPlayerId;
    freePending->active = true;
    freePending->requestId = canonical.requestId;
    freePending->createdAt = enet_time_get();
    freePending->requester = player;
    freePending->metadata = packet.metadata;
    GetPacketFactory().Send(canonical, host);
    return true;
}

void CPickupAuthorityManager::SendActiveStates(CNetworkPlayer* player)
{
    if (!IsAuthenticatedPlayer(player) || CNetworkPlayerManager::GetHost() == nullptr)
    {
        return;
    }

    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();
    const uint32_t now = enet_time_get();
    for (PickupSlot& slot : m_slots)
    {
        PickupStateEvent dueRespawn{};
        if (MaterializeDueRespawn(slot, host, now, dueRespawn))
        {
            // This includes the joining player and prevents respawnAt-now from underflowing at the deadline.
            GetPacketFactory().SendToAll(dueRespawn);
            continue;
        }
        if ((!slot.active && !slot.state.hasCompletionState) ||
            (slot.active && IsExpired(now, slot.expiresAt)))
        {
            continue;
        }
        PickupStateEvent replay{};
        replay.state = slot.state;
        if (slot.expiresAt != 0)
        {
            replay.state.metadata.expiresAfterMs = slot.expiresAt - now;
        }
        if (!slot.active && slot.respawnAt != 0)
        {
            replay.state.respawnRemainingMs = IsExpired(now, slot.respawnAt)
                ? 1
                : std::max(1u, slot.respawnAt - now);
        }
        GetPacketFactory().Send(replay, player);
    }
}

void CPickupAuthorityManager::HandlePlayerDisconnected(CNetworkPlayer* player)
{
    if (player == nullptr)
    {
        return;
    }
    if (IsCurrentHost(player))
    {
        ClearAllPending();
    }
    else
    {
        for (PendingCollect& pending : m_pendingCollects)
        {
            if (pending.active && pending.requester == player)
            {
                pending = {};
            }
        }
        for (PendingCreate& pending : m_pendingCreates)
        {
            if (pending.active && pending.requester == player)
            {
                pending = {};
            }
        }
    }
    if (player->m_iPlayerId >= 0 && player->m_iPlayerId < Config::MAX_SERVER_PLAYERS)
    {
        m_collectRateLimits[player->m_iPlayerId] = {};
        m_createRateLimits[player->m_iPlayerId] = {};
    }
}

void CPickupAuthorityManager::HandleAuthorityChange(CNetworkPlayer* newHost)
{
    ClearAllPending();
    m_collectRateLimits.fill(RateLimitSlot{});
    m_createRateLimits.fill(RateLimitSlot{});
    if (!IsCurrentHost(newHost))
    {
        return;
    }

    for (PickupSlot& slot : m_slots)
    {
        if (!slot.active && !slot.state.hasCompletionState)
        {
            continue;
        }
        slot.state.authorityPlayerId = static_cast<uint8_t>(newHost->m_iPlayerId);
    }
    SendActiveStates(newHost);
}
