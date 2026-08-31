#include "CPickupAuthorityManager.h"

#include "CNetworkPlayer.h"
#include "CNetworkPlayerManager.h"
#include "CPacketFactory.h"
#include "CServerTime.h"
#include "logger.h"
#include "stdafx.h"

#include <algorithm>
#include <vector>

using namespace Packets::Pickups;

namespace
{
bool IsParticipantCollectionType(uint8_t pickupType)
{
    switch (pickupType)
    {
    case 1:   // in-shop weapon
    case 2:   // on-street/static
    case 3:   // once
    case 4:   // timed once
    case 5:   // slow timed once
    case 6:   // collectible
    case 8:   // money drop
    case 13:  // floating package
    case 14:  // floating package on water
    case 15:  // slow on-street
    case 19:  // persistent money class
    case 20:  // snapshot
    case 22:  // mission once
        return true;
    default:
        return false;
    }
}

bool IsSameDescriptor(const PickupDescriptor& left, const PickupDescriptor& right)
{
    return left.identity == right.identity && left.authorityEpoch == right.authorityEpoch &&
           left.revision == right.revision && left.position == right.position &&
           left.modelId == right.modelId && left.pickupType == right.pickupType &&
           left.ammoOrMoney == right.ammoOrMoney && left.moneyPerDay == right.moneyPerDay &&
           left.regenerationRemainingMs == right.regenerationRemainingMs &&
           left.revenueValue == right.revenueValue && left.areaCode == right.areaCode &&
           left.lifecycle == right.lifecycle && left.empty == right.empty && left.visible == right.visible;
}
}

bool CPickupAuthorityManager::IsCurrentHost(const CNetworkPlayer* sender)
{
    return sender != nullptr && sender->m_bIsHost && CNetworkPlayerManager::GetHost() == sender;
}

bool CPickupAuthorityManager::IsGenerationNewer(uint16_t candidate, uint16_t reference)
{
    return static_cast<int16_t>(candidate - reference) > 0;
}

uint32_t CPickupAuthorityManager::NextRevision(uint32_t revision)
{
    ++revision;
    return revision == 0 ? 1 : revision;
}

void CPickupAuthorityManager::AdvanceSnapshotRevision()
{
    ms_snapshotRevision = NextRevision(ms_snapshotRevision);
}

void CPickupAuthorityManager::Update()
{
    for (PendingRequest& pending : ms_pendingRequests)
    {
        if (pending.active && g_serverTime - pending.receivedAt > COLLECTION_REQUEST_TIMEOUT_MS)
            pending = {};
    }
}

bool CPickupAuthorityManager::HandleSpawn(CNetworkPlayer* sender, const PickupSpawn& packet)
{
    const PickupDescriptor& pickup = packet.pickup;
    if (!IsCurrentHost(sender) || !pickup.HasValidState() || pickup.authorityEpoch != ms_authorityEpoch ||
        pickup.lifecycle == ePickupLifecycle::REMOVED || pickup.revision != 1)
    {
        return false;
    }

    PickupRecord& record = ms_pickups[pickup.identity.slot];
    if (record.occupied)
    {
        if (record.pickup.identity == pickup.identity)
        {
            // Exact retransmits are idempotent; any changed field under the same identity is a spoof.
            return IsSameDescriptor(record.pickup, pickup);
        }
        if (record.pickup.lifecycle != ePickupLifecycle::REMOVED ||
            !IsGenerationNewer(pickup.identity.generation, record.pickup.identity.generation))
        {
            return false;
        }
    }

    record.occupied = true;
    record.pickup = pickup;
    AdvanceSnapshotRevision();
    PickupSpawn broadcast = packet;
    GetPacketFactory().SendToAll(broadcast);
    return true;
}

bool CPickupAuthorityManager::HandleState(CNetworkPlayer* sender, const PickupState& packet)
{
    const PickupDescriptor& pickup = packet.pickup;
    if (!IsCurrentHost(sender) || !pickup.HasValidState() || pickup.authorityEpoch != ms_authorityEpoch ||
        pickup.lifecycle == ePickupLifecycle::REMOVED)
    {
        return false;
    }

    PickupRecord& record = ms_pickups[pickup.identity.slot];
    if (!record.occupied || record.pickup.identity != pickup.identity ||
        pickup.revision != NextRevision(record.pickup.revision) ||
        pickup.modelId != record.pickup.modelId)
    {
        return false;
    }

    record.pickup = pickup;
    AdvanceSnapshotRevision();
    PickupState broadcast = packet;
    GetPacketFactory().SendToAll(broadcast);
    return true;
}

bool CPickupAuthorityManager::HandleRemove(CNetworkPlayer* sender, const PickupRemove& packet)
{
    if (!IsCurrentHost(sender) || packet.authorityEpoch != ms_authorityEpoch || !packet.identity.IsValid())
        return false;

    PickupRecord& record = ms_pickups[packet.identity.slot];
    if (!record.occupied || record.pickup.identity != packet.identity ||
        record.pickup.lifecycle == ePickupLifecycle::REMOVED || packet.observedRevision != record.pickup.revision)
    {
        return false;
    }

    record.pickup.revision = NextRevision(record.pickup.revision);
    record.pickup.lifecycle = ePickupLifecycle::REMOVED;
    record.pickup.regenerationRemainingMs = 0;
    record.pickup.revenueValue = 0.0f;
    for (PendingRequest& pending : ms_pendingRequests)
    {
        if (pending.active && pending.identity == packet.identity)
            pending = {};
    }
    AdvanceSnapshotRevision();

    PickupRemove broadcast = packet;
    broadcast.observedRevision = record.pickup.revision;
    GetPacketFactory().SendToAll(broadcast);
    return true;
}

bool CPickupAuthorityManager::ConsumeRequestBudget(int playerId, uint32_t requestNonce)
{
    if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS || requestNonce == 0)
        return false;

    RequestRate& rate = ms_requestRates[playerId];
    if (rate.hasNonce && !IsRevisionNewer(requestNonce, rate.lastNonce))
        return false;

    if (g_serverTime - rate.windowStartedAt >= COLLECTION_REQUEST_WINDOW_MS)
    {
        rate.windowStartedAt = g_serverTime;
        rate.count = 0;
    }
    if (rate.count >= MAX_COLLECTION_REQUESTS_PER_SECOND)
        return false;

    ++rate.count;
    rate.lastNonce = requestNonce;
    rate.hasNonce = true;
    return true;
}

bool CPickupAuthorityManager::HandleCollectRequest(CNetworkPlayer* sender, const PickupCollectRequest& packet)
{
    if (sender == nullptr || packet.authorityEpoch != ms_authorityEpoch || !packet.identity.IsValid() ||
        !packet.HasValidClaim() || !ConsumeRequestBudget(sender->m_iPlayerId, packet.requestNonce))
    {
        return false;
    }

    PickupRecord& record = ms_pickups[packet.identity.slot];
    if (!record.occupied || record.pickup.identity != packet.identity ||
        record.pickup.lifecycle != ePickupLifecycle::ACTIVE || packet.observedRevision != record.pickup.revision)
    {
        return false;
    }
    if (!IsParticipantCollectionType(record.pickup.pickupType))
        return false;

    const bool snapshot = GetCollectibleKind(record.pickup) == eCollectibleKind::SNAPSHOT;
    const float maxDistance = snapshot ? MAX_SNAPSHOT_REQUEST_DISTANCE : MAX_PICKUP_REQUEST_DISTANCE;
    const float deltaX = packet.claimantPosition.x - record.pickup.position.x;
    const float deltaY = packet.claimantPosition.y - record.pickup.position.y;
    const float deltaZ = packet.claimantPosition.z - record.pickup.position.z;
    if (packet.areaCode != record.pickup.areaCode || (snapshot && !packet.cameraAttempt) ||
        deltaX * deltaX + deltaY * deltaY + deltaZ * deltaZ > maxDistance * maxDistance)
    {
        return false;
    }

    PendingRequest& pending = ms_pendingRequests[sender->m_iPlayerId];
    if (pending.active && g_serverTime - pending.receivedAt <= COLLECTION_REQUEST_TIMEOUT_MS)
        return false;

    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();
    if (host == nullptr)
        return false;

    pending.active = true;
    pending.identity = packet.identity;
    pending.authorityEpoch = packet.authorityEpoch;
    pending.observedRevision = packet.observedRevision;
    pending.requestNonce = packet.requestNonce;
    pending.receivedAt = g_serverTime;

    PickupCollectForward forward{};
    forward.identity = packet.identity;
    forward.authorityEpoch = packet.authorityEpoch;
    forward.observedRevision = packet.observedRevision;
    forward.requestNonce = packet.requestNonce;
    forward.claimantPlayerId = static_cast<uint8_t>(sender->m_iPlayerId);
    GetPacketFactory().Send(forward, host);
    return true;
}

bool CPickupAuthorityManager::HandleCollectDecision(CNetworkPlayer* sender, const PickupCollectDecision& packet)
{
    if (!IsCurrentHost(sender) || packet.authorityEpoch != ms_authorityEpoch || !packet.identity.IsValid())
        return false;

    const bool hostNativeCollection = packet.requestNonce == 0 &&
                                      packet.claimantPlayerId == sender->m_iPlayerId;
    PendingRequest* pending = nullptr;
    if (!hostNativeCollection)
    {
        if (packet.claimantPlayerId >= Config::MAX_SERVER_PLAYERS)
            return false;
        pending = &ms_pendingRequests[packet.claimantPlayerId];
        if (!pending->active || pending->identity != packet.identity ||
            pending->authorityEpoch != packet.authorityEpoch ||
            pending->observedRevision != packet.observedRevision ||
            pending->requestNonce != packet.requestNonce ||
            g_serverTime - pending->receivedAt > COLLECTION_REQUEST_TIMEOUT_MS)
        {
            return false;
        }
    }

    PickupRecord& record = ms_pickups[packet.identity.slot];
    if (!record.occupied || record.pickup.identity != packet.identity ||
        record.pickup.lifecycle != ePickupLifecycle::ACTIVE || packet.observedRevision != record.pickup.revision)
    {
        if (pending)
            *pending = {};
        return false;
    }

    CNetworkPlayer* claimant = CNetworkPlayerManager::GetPlayer(packet.claimantPlayerId);
    if (claimant == nullptr)
    {
        if (pending)
            *pending = {};
        return false;
    }

    PickupCollectResult result{};
    result.pickup = record.pickup;
    result.requestNonce = packet.requestNonce;
    result.claimantPlayerId = packet.claimantPlayerId;
    result.accepted = packet.accepted;

    if (pending)
        *pending = {};

    if (!packet.accepted)
    {
        GetPacketFactory().Send(result, claimant);
        return true;
    }

    if (!packet.resolvedPickup.HasValidState() || packet.resolvedPickup.identity != record.pickup.identity ||
        packet.resolvedPickup.authorityEpoch != ms_authorityEpoch ||
        packet.resolvedPickup.revision != record.pickup.revision ||
        packet.resolvedPickup.modelId != record.pickup.modelId ||
        packet.resolvedPickup.pickupType != record.pickup.pickupType)
    {
        return false;
    }
    record.pickup = packet.resolvedPickup;
    record.pickup.revision = NextRevision(record.pickup.revision);
    result.pickup = record.pickup;
    result.collectibleKind = GetCollectibleKind(record.pickup);
    if (result.collectibleKind != eCollectibleKind::NONE)
    {
        const size_t progressIndex = static_cast<size_t>(result.collectibleKind);
        ms_collectibleProgress[progressIndex] = std::min<uint8_t>(MAX_COLLECTIBLE_PROGRESS,
            static_cast<uint8_t>(ms_collectibleProgress[progressIndex] + 1));
        result.collectibleProgress = ms_collectibleProgress[progressIndex];
    }

    for (PendingRequest& other : ms_pendingRequests)
    {
        if (other.active && other.identity == packet.identity)
            other = {};
    }
    AdvanceSnapshotRevision();
    GetPacketFactory().SendToAll(result);
    return true;
}

bool CPickupAuthorityManager::HandleProgressSnapshot(CNetworkPlayer* sender,
    const PickupSnapshotChunk& packet)
{
    if (!IsCurrentHost(sender) || !packet.HasValidState() || !packet.FitsSerializedBudget() ||
        packet.authorityEpoch != ms_authorityEpoch || packet.snapshotRevision != ms_snapshotRevision ||
        packet.authorityPlayerId != sender->m_iPlayerId || packet.chunkIndex != 0 ||
        packet.chunkCount != 1 || packet.entryCount != 0 || packet.collectibleProgress[0] != 0)
    {
        return false;
    }

    bool changed = false;
    for (size_t index = 1; index < ms_collectibleProgress.size(); ++index)
    {
        if (packet.collectibleProgress[index] < ms_collectibleProgress[index])
            return false;
        changed = changed || packet.collectibleProgress[index] != ms_collectibleProgress[index];
    }
    if (!changed)
        return true;

    ms_collectibleProgress = packet.collectibleProgress;
    AdvanceSnapshotRevision();
    SendSnapshotToAll();
    return true;
}

void CPickupAuthorityManager::SendSnapshot(CNetworkPlayer* recipient)
{
    if (recipient == nullptr || ms_authorityEpoch == 0 || CNetworkPlayerManager::GetHost() == nullptr)
        return;

    std::vector<PickupDescriptor> snapshotPickups;
    snapshotPickups.reserve(MAX_PICKUPS);
    for (const PickupRecord& record : ms_pickups)
    {
        // Removed records are bounded tombstones. They let a late-joining future
        // authority continue each native origin slot's generation without reuse.
        if (record.occupied)
            snapshotPickups.push_back(record.pickup);
    }

    if (ms_snapshotRevision == 0)
        AdvanceSnapshotRevision();
    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();
    if (host == nullptr)
        return;
    const size_t chunkCount = std::max<size_t>(1,
        (snapshotPickups.size() + MAX_SNAPSHOT_ENTRIES - 1) / MAX_SNAPSHOT_ENTRIES);
    size_t totalSnapshotBytes = 0;
    for (size_t chunkIndex = 0; chunkIndex < chunkCount; ++chunkIndex)
    {
        PickupSnapshotChunk chunk{};
        chunk.authorityEpoch = ms_authorityEpoch;
        chunk.snapshotRevision = ms_snapshotRevision;
        chunk.authorityPlayerId = static_cast<uint8_t>(host->m_iPlayerId);
        chunk.chunkIndex = static_cast<uint8_t>(chunkIndex);
        chunk.chunkCount = static_cast<uint8_t>(chunkCount);
        chunk.collectibleProgress = ms_collectibleProgress;
        const size_t begin = chunkIndex * MAX_SNAPSHOT_ENTRIES;
        const size_t end = std::min(snapshotPickups.size(), begin + MAX_SNAPSHOT_ENTRIES);
        chunk.entryCount = static_cast<uint8_t>(end - begin);
        for (size_t index = begin; index < end; ++index)
            chunk.entries[index - begin] = snapshotPickups[index];
        const size_t chunkBytes = chunk.MeasureSerializedBytes();
        totalSnapshotBytes += chunkBytes;
        if (chunkBytes == 0 || chunkBytes > MAX_SNAPSHOT_BYTES ||
            totalSnapshotBytes > MAX_SNAPSHOT_TOTAL_BYTES)
        {
            logger::error("Refusing oversized pickup snapshot chunk %u", static_cast<unsigned>(chunkIndex));
            return;
        }
        GetPacketFactory().Send(chunk, recipient);
    }
}

void CPickupAuthorityManager::SendSnapshotToAll()
{
    for (CNetworkPlayer* player : CNetworkPlayerManager::m_pPlayers)
        SendSnapshot(player);
}

void CPickupAuthorityManager::HandleAuthorityChanged(CNetworkPlayer* newHost)
{
    if (newHost == nullptr)
    {
        ResetSession();
        return;
    }

    ms_authorityEpoch = NextRevision(ms_authorityEpoch);
    for (PickupRecord& record : ms_pickups)
    {
        if (record.occupied)
            record.pickup.authorityEpoch = ms_authorityEpoch;
    }
    ms_pendingRequests.fill(PendingRequest{});
    AdvanceSnapshotRevision();
    SendSnapshotToAll();
}

void CPickupAuthorityManager::HandlePlayerDisconnected(int playerId)
{
    if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
        return;
    ms_pendingRequests[playerId] = {};
    ms_requestRates[playerId] = {};
}

void CPickupAuthorityManager::ResetSession()
{
    ms_pickups.fill(PickupRecord{});
    ms_pendingRequests.fill(PendingRequest{});
    ms_requestRates.fill(RequestRate{});
    ms_collectibleProgress.fill(0);
    ms_authorityEpoch = 0;
    ms_snapshotRevision = 0;
}
