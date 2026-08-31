#include "CNetworkPickupManager.h"

#include "CLocalPlayer.h"
#include "CNetwork.h"
#include "CNetworkPlayer.h"
#include "CNetworkPlayerManager.h"
#include "CPacketFactory.h"
#include "logger.h"
#include "stdafx.h"

#include <CPickup.h>
#include <CPickups.h>
#include <CPlayerInfo.h>
#include <CStats.h>
#include <CTimer.h>
#include <CWeapon.h>
#include <CWanted.h>

#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Packets::Pickups;

namespace
{
constexpr float PEER_COLLECTION_DISTANCE = 2.5f;
constexpr uint32_t REQUEST_RETRY_MS = 750;

constexpr std::array<uint16_t, COLLECTIBLE_KIND_COUNT> COLLECTIBLE_STATS = {
    0, STAT_HORSESHOES_COLLECTED, STAT_SNAPSHOTS_TAKEN, STAT_OYSTERS_COLLECTED};

float DistanceSquared(const CVector& left, const CVector& right)
{
    const float dx = left.x - right.x;
    const float dy = left.y - right.y;
    const float dz = left.z - right.z;
    return dx * dx + dy * dy + dz * dz;
}

class ScopedLocalRewardRollback
{
public:
    explicit ScopedLocalRewardRollback(bool active) : m_active(active)
    {
        if (!m_active)
            return;
        std::memcpy(m_intStats.data(), CStats::StatTypesInt, sizeof(m_intStats));
        std::memcpy(m_floatStats.data(), CStats::StatTypesFloat, sizeof(m_floatStats));
        CPlayerInfo& info = FindPlayerInfo();
        m_money = info.m_nMoney;
        m_displayMoney = info.m_nDisplayMoney;
        m_collectables = info.m_nCollectablesPickedUp;
        m_statMessages = CStats::TotalNumStatMessages;
        m_addToHealthCounter = CStats::m_AddToHealthCounter;
        if (CPlayerPed* local = FindPlayerPed(0); local && local->GetWanted())
        {
            m_hasWanted = true;
            std::memcpy(m_wanted.data(), local->GetWanted(), m_wanted.size());
        }
    }

    ~ScopedLocalRewardRollback()
    {
        if (!m_active)
            return;
        std::memcpy(CStats::StatTypesInt, m_intStats.data(), sizeof(m_intStats));
        std::memcpy(CStats::StatTypesFloat, m_floatStats.data(), sizeof(m_floatStats));
        CPlayerInfo& info = FindPlayerInfo();
        info.m_nMoney = m_money;
        info.m_nDisplayMoney = m_displayMoney;
        info.m_nCollectablesPickedUp = m_collectables;
        CStats::TotalNumStatMessages = m_statMessages;
        CStats::m_AddToHealthCounter = m_addToHealthCounter;
        if (m_hasWanted)
        {
            if (CPlayerPed* local = FindPlayerPed(0); local && local->GetWanted())
                std::memcpy(local->GetWanted(), m_wanted.data(), m_wanted.size());
        }
    }

private:
    bool m_active = false;
    bool m_hasWanted = false;
    int m_money = 0;
    int m_displayMoney = 0;
    uint32_t m_collectables = 0;
    uint32_t m_statMessages = 0;
    uint32_t m_addToHealthCounter = 0;
    std::array<int, 224> m_intStats{};
    std::array<float, 83> m_floatStats{};
    std::array<uint8_t, sizeof(CWanted)> m_wanted{};
};

}  // namespace

bool CNetworkPickupManager::IsNativeSlotCurrent(uint16_t slot, const LocalPickup& localPickup)
{
    return slot < MAX_PICKUPS && CPickups::aPickUps[slot].m_nReferenceIndex == localPickup.referenceIndex;
}

uint16_t CNetworkPickupManager::NextGeneration(uint16_t generation)
{
    ++generation;
    return generation == 0 ? 1 : generation;
}

uint32_t CNetworkPickupManager::NextRequestNonce()
{
    ++ms_requestNonce;
    return ms_requestNonce == 0 ? ++ms_requestNonce : ms_requestNonce;
}

uint32_t CNetworkPickupManager::GetRegenerationRemaining(const CPickup& pickup)
{
    if (!pickup.m_nFlags.bDisabled || pickup.m_nRegenerationTime <= CTimer::m_snTimeInMilliseconds)
        return 0;
    return std::min<uint32_t>(MAX_REGENERATION_MS,
        pickup.m_nRegenerationTime - CTimer::m_snTimeInMilliseconds);
}

bool CNetworkPickupManager::CaptureNativePickup(uint16_t slot, const PickupIdentity& identity,
    uint32_t revision, PickupDescriptor& outPickup)
{
    if (slot >= MAX_PICKUPS)
        return false;
    CPickup& native = CPickups::aPickUps[slot];
    if (native.m_nPickupType < MIN_PICKUP_TYPE || native.m_nPickupType > MAX_PICKUP_TYPE ||
        native.m_nModelIndex <= 0 || native.m_nModelIndex > MAX_PICKUP_MODEL_ID)
    {
        return false;
    }

    outPickup = {};
    outPickup.identity = identity;
    // A newly authenticated peer can run before its first server snapshot arrives.
    // Use a local-only placeholder epoch so the stock descriptor can be retained
    // and masked immediately; it is never transmitted by a non-authority.
    outPickup.authorityEpoch = ms_authorityEpoch == 0 ? 1 : ms_authorityEpoch;
    outPickup.revision = revision;
    outPickup.position = native.GetPosn();
    outPickup.modelId = static_cast<uint16_t>(native.m_nModelIndex);
    outPickup.pickupType = native.m_nPickupType;
    outPickup.ammoOrMoney = std::min<uint32_t>(native.m_nAmmo, MAX_PICKUP_AMMO_OR_MONEY);
    outPickup.moneyPerDay = native.m_nMoneyPerDay;
    outPickup.regenerationRemainingMs = GetRegenerationRemaining(native);
    outPickup.revenueValue = std::isfinite(native.m_fRevenueValue)
        ? std::clamp(native.m_fRevenueValue, 0.0f, static_cast<float>(MAX_PICKUP_AMMO_OR_MONEY))
        : 0.0f;
    outPickup.areaCode = native.m_pObject
        ? native.m_pObject->m_nAreaCode
        : (ms_localPickups[slot].tracked ? ms_localPickups[slot].pickup.areaCode : 0);
    outPickup.lifecycle = native.m_nFlags.bDisabled ? ePickupLifecycle::DISABLED : ePickupLifecycle::ACTIVE;
    outPickup.empty = native.m_nFlags.bEmpty;
    outPickup.visible = native.m_nFlags.bVisible;
    return outPickup.HasValidState();
}

bool CNetworkPickupManager::DescriptorsMatch(const PickupDescriptor& left, const PickupDescriptor& right)
{
    return left.modelId == right.modelId && left.pickupType == right.pickupType &&
           left.areaCode == right.areaCode && DistanceSquared(left.position, right.position) <= 0.25f;
}

bool CNetworkPickupManager::StateChanged(const PickupDescriptor& before, const PickupDescriptor& after)
{
    return before.modelId != after.modelId || before.pickupType != after.pickupType ||
           before.ammoOrMoney != after.ammoOrMoney || before.moneyPerDay != after.moneyPerDay ||
           std::abs(before.revenueValue - after.revenueValue) > 0.01f ||
           before.areaCode != after.areaCode || before.lifecycle != after.lifecycle ||
           before.empty != after.empty || before.visible != after.visible ||
           DistanceSquared(before.position, after.position) > 0.0001f;
}

int CNetworkPickupManager::FindIdentity(const PickupIdentity& identity)
{
    for (uint16_t slot = 0; slot < MAX_PICKUPS; ++slot)
    {
        if (ms_localPickups[slot].tracked && ms_localPickups[slot].bound &&
            ms_localPickups[slot].pickup.identity == identity)
        {
            return slot;
        }
    }
    return -1;
}

int CNetworkPickupManager::FindUnboundMatch(const PickupDescriptor& pickup)
{
    for (uint16_t slot = 0; slot < MAX_PICKUPS; ++slot)
    {
        const LocalPickup& local = ms_localPickups[slot];
        if (local.tracked && !local.bound && DescriptorsMatch(local.pickup, pickup))
            return slot;
    }
    return -1;
}

int CNetworkPickupManager::CreateMirror(const PickupDescriptor& pickup)
{
    ms_applyingNetworkState = true;
    const int handle = CPickups::GenerateNewOne(pickup.position, pickup.modelId, pickup.pickupType,
        pickup.ammoOrMoney, pickup.moneyPerDay, pickup.empty, nullptr);
    ms_applyingNetworkState = false;
    if (handle < 0)
        return -1;
    const int slot = CPickups::GetActualPickupIndex(handle);
    return slot >= 0 && slot < MAX_PICKUPS ? slot : -1;
}

void CNetworkPickupManager::MaskPeerPickup(int localSlot)
{
    if (localSlot < 0 || localSlot >= MAX_PICKUPS || !ms_localPickups[localSlot].tracked ||
        !IsNativeSlotCurrent(static_cast<uint16_t>(localSlot), ms_localPickups[localSlot]))
    {
        return;
    }
    CPickups::aPickUps[localSlot].m_nPickupType = PICKUP_NONE;
}

void CNetworkPickupManager::PromoteMirrorForAuthority(int localSlot)
{
    if (localSlot < 0 || localSlot >= MAX_PICKUPS || !ms_localPickups[localSlot].tracked ||
        !IsNativeSlotCurrent(static_cast<uint16_t>(localSlot), ms_localPickups[localSlot]))
    {
        return;
    }
    CPickup& native = CPickups::aPickUps[localSlot];
    native.m_nPickupType = ms_localPickups[localSlot].pickup.pickupType;
    native.m_nFlags.bDisabled = ms_localPickups[localSlot].pickup.lifecycle == ePickupLifecycle::DISABLED;
}

void CNetworkPickupManager::ApplyAuthoritativeState(int localSlot, const PickupDescriptor& pickup)
{
    if (localSlot < 0 || localSlot >= MAX_PICKUPS)
        return;
    LocalPickup& local = ms_localPickups[localSlot];
    if (!local.tracked || !IsNativeSlotCurrent(static_cast<uint16_t>(localSlot), local))
        return;

    CPickup& native = CPickups::aPickUps[localSlot];
    native.m_nModelIndex = static_cast<int16_t>(pickup.modelId);
    native.m_nAmmo = pickup.ammoOrMoney;
    native.m_nMoneyPerDay = pickup.moneyPerDay;
    native.m_fRevenueValue = pickup.revenueValue;
    native.SetPosn(pickup.position.x, pickup.position.y, pickup.position.z);
    native.m_nFlags.bEmpty = pickup.empty;
    native.m_nFlags.bVisible = pickup.visible;
    native.m_nFlags.bDisabled = pickup.lifecycle == ePickupLifecycle::DISABLED;
    native.m_nRegenerationTime = pickup.lifecycle == ePickupLifecycle::DISABLED
        ? CTimer::m_snTimeInMilliseconds + pickup.regenerationRemainingMs
        : 0;

    if (pickup.lifecycle == ePickupLifecycle::DISABLED)
    {
        if (native.m_pObject)
            native.GetRidOfObjects();
    }
    else if (pickup.lifecycle == ePickupLifecycle::ACTIVE && !native.m_pObject)
    {
        native.m_nPickupType = pickup.pickupType;
        native.GiveUsAPickUpObject(&native.m_pObject, -1);
    }
    if (native.m_pObject)
        native.m_pObject->m_nAreaCode = pickup.areaCode;

    const bool becameActive = local.pickup.lifecycle != ePickupLifecycle::ACTIVE &&
                              pickup.lifecycle == ePickupLifecycle::ACTIVE;
    local.pickup = pickup;
    local.preAuthorityPickup = pickup;
    if (becameActive)
        local.collectionObserved = false;
    local.awaitingAuthority = false;
    if (!CLocalPlayer::m_bIsHost)
        MaskPeerPickup(localSlot);
    else
        PromoteMirrorForAuthority(localSlot);
}

void CNetworkPickupManager::BindAndApply(int localSlot, const PickupDescriptor& pickup,
    bool networkGenerated)
{
    if (localSlot < 0 || localSlot >= MAX_PICKUPS)
        return;
    CPickup& native = CPickups::aPickUps[localSlot];
    LocalPickup& local = ms_localPickups[localSlot];
    local.tracked = true;
    local.bound = true;
    local.networkGenerated = networkGenerated;
    local.awaitingAuthority = false;
    local.collectionObserved = false;
    local.referenceIndex = native.m_nReferenceIndex;
    local.nativeHandle = CPickups::GetUniquePickupIndex(localSlot);
    local.pickup = pickup;
    ms_generations[pickup.identity.slot] = pickup.identity.generation;
    ApplyAuthoritativeState(localSlot, pickup);
}

void CNetworkPickupManager::RemoveLocalPickup(int localSlot, bool markCollected)
{
    if (localSlot < 0 || localSlot >= MAX_PICKUPS)
        return;
    LocalPickup local = ms_localPickups[localSlot];
    ms_localPickups[localSlot] = {};
    if (!local.tracked || !IsNativeSlotCurrent(static_cast<uint16_t>(localSlot), local))
        return;

    CPickup& native = CPickups::aPickUps[localSlot];
    ms_applyingNetworkState = true;
    native.m_nPickupType = local.pickup.pickupType;
    if (markCollected && local.nativeHandle >= 0)
        CPickups::AddToCollectedPickupsArray(localSlot);
    native.Remove();
    ms_applyingNetworkState = false;
}

void CNetworkPickupManager::SendSpawn(LocalPickup& localPickup)
{
    PickupSpawn packet{};
    packet.pickup = localPickup.pickup;
    GetPacketFactory().Send(packet);
}

void CNetworkPickupManager::SendState(LocalPickup& localPickup, const PickupDescriptor& state)
{
    if (localPickup.awaitingAuthority)
        return;
    PickupState packet{};
    packet.pickup = state;
    packet.pickup.revision = localPickup.pickup.revision + 1;
    if (packet.pickup.revision == 0)
        packet.pickup.revision = 1;
    localPickup.pickup = packet.pickup;
    GetPacketFactory().Send(packet);
}

void CNetworkPickupManager::SendRemove(LocalPickup& localPickup, ePickupRemovalReason reason)
{
    if (localPickup.awaitingAuthority)
        return;
    PickupRemove packet{};
    packet.identity = localPickup.pickup.identity;
    packet.authorityEpoch = localPickup.pickup.authorityEpoch;
    packet.observedRevision = localPickup.pickup.revision;
    packet.reason = reason;
    localPickup.awaitingAuthority = true;
    localPickup.awaitingAuthoritySince = CTimer::m_snTimeInMilliseconds;
    localPickup.preAuthorityPickup = localPickup.pickup;
    GetPacketFactory().Send(packet);
}

void CNetworkPickupManager::SendHostCollection(LocalPickup& localPickup,
    const PickupDescriptor& resolvedPickup)
{
    if (localPickup.awaitingAuthority)
        return;
    PickupCollectDecision decision{};
    decision.identity = localPickup.pickup.identity;
    decision.authorityEpoch = localPickup.pickup.authorityEpoch;
    decision.observedRevision = localPickup.pickup.revision;
    decision.requestNonce = 0;
    decision.claimantPlayerId = static_cast<uint8_t>(CNetworkPlayerManager::m_nMyId);
    decision.accepted = true;
    decision.resolvedPickup = resolvedPickup;
    localPickup.preAuthorityPickup = localPickup.pickup;
    localPickup.pickup = resolvedPickup;
    localPickup.awaitingAuthority = true;
    localPickup.awaitingAuthoritySince = CTimer::m_snTimeInMilliseconds;
    localPickup.collectionObserved = true;
    GetPacketFactory().Send(decision);
}

void CNetworkPickupManager::ScanAuthorityPool()
{
    if (ms_authorityEpoch == 0)
        return;
    for (uint16_t slot = 0; slot < MAX_PICKUPS; ++slot)
    {
        CPickup& native = CPickups::aPickUps[slot];
        LocalPickup& local = ms_localPickups[slot];
        if (local.awaitingAuthority &&
            CTimer::m_snTimeInMilliseconds - local.awaitingAuthoritySince > COLLECTION_REQUEST_TIMEOUT_MS)
        {
            local.awaitingAuthority = false;
            local.awaitingAuthoritySince = 0;
            local.collectionObserved = false;
            if (local.bound && IsNativeSlotCurrent(slot, local) && local.preAuthorityPickup.HasValidState())
                ApplyAuthoritativeState(slot, local.preAuthorityPickup);
        }
        if (!local.tracked)
        {
            if (native.m_nPickupType == PICKUP_NONE)
                continue;
            PickupIdentity identity{};
            identity.slot = slot;
            identity.generation = NextGeneration(ms_generations[slot]);
            ms_generations[slot] = identity.generation;
            PickupDescriptor pickup{};
            if (!CaptureNativePickup(slot, identity, 1, pickup))
                continue;
            local.tracked = true;
            local.bound = true;
            local.networkGenerated = false;
            local.referenceIndex = native.m_nReferenceIndex;
            local.nativeHandle = CPickups::GetUniquePickupIndex(slot);
            local.pickup = pickup;
            SendSpawn(local);
            continue;
        }

        if (!IsNativeSlotCurrent(slot, local) || native.m_nPickupType == PICKUP_NONE)
        {
            if (local.bound && !local.awaitingAuthority)
            {
                const bool snapshotCollected = GetCollectibleKind(local.pickup) == eCollectibleKind::SNAPSHOT &&
                                               CWeapon::bPhotographHasBeenTaken;
                const bool collected = !local.collectionObserved &&
                    ((local.nativeHandle >= 0 && CPickups::IsPickUpPickedUp(local.nativeHandle)) ||
                        snapshotCollected);
                if (collected)
                {
                    PickupDescriptor resolved = local.pickup;
                    resolved.lifecycle = ePickupLifecycle::REMOVED;
                    resolved.regenerationRemainingMs = 0;
                    resolved.revenueValue = 0.0f;
                    SendHostCollection(local, resolved);
                }
                else
                    SendRemove(local, ePickupRemovalReason::SCRIPT);
            }
            continue;
        }

        PickupDescriptor current{};
        if (!CaptureNativePickup(slot, local.pickup.identity, local.pickup.revision, current))
            continue;
        const bool collected = !local.collectionObserved && local.nativeHandle >= 0 &&
                               CPickups::IsPickUpPickedUp(local.nativeHandle);
        if (collected)
        {
            SendHostCollection(local, current);
        }
        else if (!local.awaitingAuthority && StateChanged(local.pickup, current))
        {
            SendState(local, current);
        }
    }
}

void CNetworkPickupManager::ScanPeerPool()
{
    for (uint16_t slot = 0; slot < MAX_PICKUPS; ++slot)
    {
        CPickup& native = CPickups::aPickUps[slot];
        LocalPickup& local = ms_localPickups[slot];
        if (local.tracked)
        {
            if (!IsNativeSlotCurrent(slot, local))
            {
                if (local.bound)
                {
                    const PickupDescriptor pickup = local.pickup;
                    ms_localPickups[slot] = {};
                    const int replacement = CreateMirror(pickup);
                    if (replacement >= 0)
                        BindAndApply(replacement, pickup, true);
                }
                else
                {
                    ms_localPickups[slot] = {};
                }
                continue;
            }
            if (local.bound)
            {
                MaskPeerPickup(slot);
                continue;
            }
        }

        if (ms_applyingNetworkState || native.m_nPickupType == PICKUP_NONE)
            continue;

        PickupIdentity temporaryIdentity{};
        temporaryIdentity.slot = slot;
        temporaryIdentity.generation = 1;
        PickupDescriptor pickup{};
        if (!CaptureNativePickup(slot, temporaryIdentity, 1, pickup))
            continue;

        // If the server mirror arrived before this script-created equivalent, prefer the script's native handle.
        int generatedMatch = -1;
        for (uint16_t candidate = 0; candidate < MAX_PICKUPS; ++candidate)
        {
            if (candidate != slot && ms_localPickups[candidate].tracked && ms_localPickups[candidate].bound &&
                ms_localPickups[candidate].networkGenerated &&
                DescriptorsMatch(ms_localPickups[candidate].pickup, pickup))
            {
                generatedMatch = candidate;
                break;
            }
        }
        if (generatedMatch >= 0)
        {
            const PickupDescriptor authoritative = ms_localPickups[generatedMatch].pickup;
            RemoveLocalPickup(generatedMatch, false);
            BindAndApply(slot, authoritative, false);
        }
        else
        {
            local.tracked = true;
            local.bound = false;
            local.networkGenerated = false;
            local.referenceIndex = native.m_nReferenceIndex;
            local.nativeHandle = CPickups::GetUniquePickupIndex(slot);
            local.pickup = pickup;
            MaskPeerPickup(slot);
        }
    }
}

void CNetworkPickupManager::RequestNearbyCollections()
{
    CPlayerPed* player = FindPlayerPed(0);
    if (!player || player->m_fHealth <= 0.0f)
        return;
    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    for (uint16_t slot = 0; slot < MAX_PICKUPS; ++slot)
    {
        LocalPickup& local = ms_localPickups[slot];
        if (!local.tracked || !local.bound || local.awaitingAuthority ||
            local.pickup.lifecycle != ePickupLifecycle::ACTIVE ||
            player->m_nAreaCode != local.pickup.areaCode ||
            now - local.lastRequestAt < REQUEST_RETRY_MS)
        {
            continue;
        }

        const bool snapshot = GetCollectibleKind(local.pickup) == eCollectibleKind::SNAPSHOT;
        const float maxDistance = snapshot ? MAX_SNAPSHOT_REQUEST_DISTANCE : PEER_COLLECTION_DISTANCE;
        if (DistanceSquared(player->GetPosition(), local.pickup.position) > maxDistance * maxDistance)
            continue;
        if (snapshot && (player->GetWeapon().m_eWeaponType != WEAPON_CAMERA ||
                            !CWeapon::bPhotographHasBeenTaken))
        {
            continue;
        }

        PickupCollectRequest request{};
        request.identity = local.pickup.identity;
        request.authorityEpoch = local.pickup.authorityEpoch;
        request.observedRevision = local.pickup.revision;
        request.requestNonce = NextRequestNonce();
        request.claimantPosition = player->GetPosition();
        request.areaCode = player->m_nAreaCode;
        request.cameraAttempt = snapshot;
        local.lastRequestAt = now;
        local.pendingRequestNonce = request.requestNonce;
        GetPacketFactory().Send(request);
    }
}

bool CNetworkPickupManager::CaptureResolvedPickup(LocalPickup& localPickup, PickupDescriptor& outPickup)
{
    const int localSlot = static_cast<int>(&localPickup - ms_localPickups.data());
    if (localSlot < 0 || localSlot >= MAX_PICKUPS || !IsNativeSlotCurrent(localSlot, localPickup))
        return false;

    CPickup& native = CPickups::aPickUps[localSlot];
    outPickup = localPickup.pickup;
    outPickup.position = native.GetPosn();
    outPickup.modelId = native.m_nModelIndex > 0 && native.m_nModelIndex <= MAX_PICKUP_MODEL_ID
        ? static_cast<uint16_t>(native.m_nModelIndex)
        : localPickup.pickup.modelId;
    if (native.m_nPickupType >= MIN_PICKUP_TYPE && native.m_nPickupType <= MAX_PICKUP_TYPE)
        outPickup.pickupType = native.m_nPickupType;
    outPickup.ammoOrMoney = std::min<uint32_t>(native.m_nAmmo, MAX_PICKUP_AMMO_OR_MONEY);
    outPickup.moneyPerDay = native.m_nMoneyPerDay;
    outPickup.regenerationRemainingMs = GetRegenerationRemaining(native);
    outPickup.revenueValue = std::isfinite(native.m_fRevenueValue)
        ? std::clamp(native.m_fRevenueValue, 0.0f, static_cast<float>(MAX_PICKUP_AMMO_OR_MONEY))
        : 0.0f;
    outPickup.areaCode = native.m_pObject ? native.m_pObject->m_nAreaCode : localPickup.pickup.areaCode;
    outPickup.lifecycle = native.m_nPickupType == PICKUP_NONE
        ? ePickupLifecycle::REMOVED
        : (native.m_nFlags.bDisabled ? ePickupLifecycle::DISABLED : ePickupLifecycle::ACTIVE);
    outPickup.empty = native.m_nFlags.bEmpty;
    outPickup.visible = native.m_nFlags.bVisible;
    if (outPickup.lifecycle == ePickupLifecycle::REMOVED)
    {
        outPickup.regenerationRemainingMs = 0;
        outPickup.revenueValue = 0.0f;
    }
    return outPickup.HasValidState();
}

bool CNetworkPickupManager::ValidateAndCollectForPlayer(LocalPickup& localPickup, uint8_t claimantPlayerId,
    PickupDescriptor& outResolvedPickup)
{
    int localSlot = static_cast<int>(&localPickup - ms_localPickups.data());
    if (localSlot < 0 || localSlot >= MAX_PICKUPS || !IsNativeSlotCurrent(localSlot, localPickup) ||
        localPickup.pickup.lifecycle != ePickupLifecycle::ACTIVE)
    {
        return false;
    }

    CPlayerPed* ped = nullptr;
    int internalPlayerId = -1;
    if (claimantPlayerId == CNetworkPlayerManager::m_nMyId)
    {
        ped = FindPlayerPed(0);
        internalPlayerId = 0;
    }
    else if (CNetworkPlayer* networkPlayer = CNetworkPlayerManager::GetPlayer(claimantPlayerId))
    {
        ped = networkPlayer->m_pPed;
        internalPlayerId = networkPlayer->GetInternalId();
    }
    if (!ped || internalPlayerId < 0 || ped->m_fHealth <= 0.0f ||
        ped->m_nAreaCode != localPickup.pickup.areaCode)
    {
        return false;
    }

    const bool snapshot = GetCollectibleKind(localPickup.pickup) == eCollectibleKind::SNAPSHOT;
    const float maxDistance = snapshot ? MAX_SNAPSHOT_REQUEST_DISTANCE : MAX_PICKUP_REQUEST_DISTANCE;
    if (DistanceSquared(ped->GetPosition(), localPickup.pickup.position) > maxDistance * maxDistance ||
        (snapshot && ped->GetWeapon().m_eWeaponType != WEAPON_CAMERA))
    {
        return false;
    }

    CPickup& native = CPickups::aPickUps[localSlot];
    bool collected = false;
    if (snapshot)
    {
        // PictureTaken grants global local-player rewards, so the host only
        // validates and advances the canonical pickup. The claimant later runs
        // the stock reward path after the server's exactly-once result.
        native.Remove();
        collected = true;
    }
    else
    {
        const int previousFocus = CWorld::PlayerInFocus;
        CWorld::PlayerInFocus = internalPlayerId;
        {
            // Several stock pickup cases still touch player-0 money, stats, or
            // wanted state even when passed another player index. Preserve those
            // globals while using the native routine for exact eligibility and
            // pickup lifecycle resolution against the remote participant.
            ScopedLocalRewardRollback rollback(claimantPlayerId != CNetworkPlayerManager::m_nMyId);
            collected = native.Update(ped, ped->m_pVehicle, internalPlayerId);
        }
        CWorld::PlayerInFocus = previousFocus;
    }
    if (!collected)
        return false;
    localPickup.collectionObserved = true;
    return CaptureResolvedPickup(localPickup, outResolvedPickup);
}

bool CNetworkPickupManager::ExecuteLocalGrant(LocalPickup& localPickup)
{
    const int localSlot = static_cast<int>(&localPickup - ms_localPickups.data());
    if (localSlot < 0 || localSlot >= MAX_PICKUPS || !IsNativeSlotCurrent(localSlot, localPickup))
        return false;
    CPlayerPed* player = FindPlayerPed(0);
    if (!player || player->m_fHealth <= 0.0f)
        return false;

    CPickup& native = CPickups::aPickUps[localSlot];
    native.m_nPickupType = localPickup.pickup.pickupType;
    native.m_nFlags.bDisabled = false;
    const bool snapshot = GetCollectibleKind(localPickup.pickup) == eCollectibleKind::SNAPSHOT;
    if (snapshot)
    {
        const CVector approvedPhotoPoint = TheCamera.GetPosition() + TheCamera.GetForward() * 5.0f;
        native.SetPosn(approvedPhotoPoint.x, approvedPhotoPoint.y, approvedPhotoPoint.z);
        CPickups::PictureTaken();
        return native.m_nPickupType == PICKUP_NONE;
    }
    const CVector originalPosition = native.GetPosn();
    native.SetPosn(player->GetPosition().x, player->GetPosition().y, player->GetPosition().z);
    const bool granted = native.Update(player, player->m_pVehicle, 0);
    if (granted)
        CPickups::AddToCollectedPickupsArray(localSlot);
    if (native.m_nPickupType != PICKUP_NONE)
        native.SetPosn(originalPosition.x, originalPosition.y, originalPosition.z);
    return granted;
}

void CNetworkPickupManager::HandleSpawn(const PickupSpawn& packet)
{
    if (!packet.pickup.HasValidState() || packet.pickup.authorityEpoch < ms_authorityEpoch)
        return;
    ms_authorityEpoch = packet.pickup.authorityEpoch;
    if (FindIdentity(packet.pickup.identity) >= 0)
        return;
    int localSlot = FindUnboundMatch(packet.pickup);
    bool networkGenerated = false;
    if (localSlot < 0)
    {
        localSlot = CreateMirror(packet.pickup);
        networkGenerated = true;
    }
    if (localSlot >= 0)
        BindAndApply(localSlot, packet.pickup, networkGenerated);
}

void CNetworkPickupManager::HandleState(const PickupState& packet)
{
    if (!packet.pickup.HasValidState() || packet.pickup.authorityEpoch != ms_authorityEpoch)
        return;
    const int localSlot = FindIdentity(packet.pickup.identity);
    if (localSlot < 0)
    {
        PickupSpawn spawn{};
        spawn.pickup = packet.pickup;
        HandleSpawn(spawn);
        return;
    }
    LocalPickup& local = ms_localPickups[localSlot];
    if (!IsRevisionNewer(packet.pickup.revision, local.pickup.revision))
        return;
    ApplyAuthoritativeState(localSlot, packet.pickup);
}

void CNetworkPickupManager::HandleRemove(const PickupRemove& packet)
{
    if (packet.authorityEpoch != ms_authorityEpoch)
        return;
    const int localSlot = FindIdentity(packet.identity);
    if (localSlot < 0 || !IsRevisionNewer(packet.observedRevision, ms_localPickups[localSlot].pickup.revision))
        return;
    RemoveLocalPickup(localSlot, false);
}

void CNetworkPickupManager::HandleCollectForward(const PickupCollectForward& packet)
{
    PickupCollectDecision decision{};
    decision.identity = packet.identity;
    decision.authorityEpoch = packet.authorityEpoch;
    decision.observedRevision = packet.observedRevision;
    decision.requestNonce = packet.requestNonce;
    decision.claimantPlayerId = packet.claimantPlayerId;

    if (CLocalPlayer::m_bIsHost && packet.authorityEpoch == ms_authorityEpoch)
    {
        const int localSlot = FindIdentity(packet.identity);
        if (localSlot >= 0 && ms_localPickups[localSlot].pickup.revision == packet.observedRevision)
        {
            decision.accepted = ValidateAndCollectForPlayer(ms_localPickups[localSlot], packet.claimantPlayerId,
                decision.resolvedPickup);
            if (decision.accepted)
            {
                ms_localPickups[localSlot].awaitingAuthority = true;
                ms_localPickups[localSlot].awaitingAuthoritySince = CTimer::m_snTimeInMilliseconds;
            }
        }
    }
    GetPacketFactory().Send(decision);
}

void CNetworkPickupManager::HandleCollectResult(const PickupCollectResult& packet)
{
    if (!packet.HasValidState() || packet.pickup.authorityEpoch != ms_authorityEpoch)
        return;
    const int localSlot = FindIdentity(packet.pickup.identity);
    if (localSlot < 0)
        return;
    LocalPickup& local = ms_localPickups[localSlot];
    if (!packet.accepted)
    {
        if (packet.claimantPlayerId == CNetworkPlayerManager::m_nMyId &&
            local.pendingRequestNonce == packet.requestNonce)
        {
            local.pendingRequestNonce = 0;
        }
        return;
    }
    if (!IsRevisionNewer(packet.pickup.revision, local.pickup.revision))
        return;

    const bool claimantIsLocal = packet.claimantPlayerId == CNetworkPlayerManager::m_nMyId;
    const bool authorityAlreadyApplied = CLocalPlayer::m_bIsHost;
    bool nativeGranted = false;
    if (claimantIsLocal && !authorityAlreadyApplied)
        nativeGranted = ExecuteLocalGrant(local);
    else if (!claimantIsLocal && !authorityAlreadyApplied &&
             GetCollectibleKind(local.pickup) != eCollectibleKind::SNAPSHOT)
        CPickups::AddToCollectedPickupsArray(localSlot);

    if (claimantIsLocal && !authorityAlreadyApplied && !nativeGranted)
        logger::warn("Authority approved pickup %u:%u but the bounded stock grant did not apply",
            packet.pickup.identity.slot, packet.pickup.identity.generation);

    if (packet.collectibleKind != eCollectibleKind::NONE)
        ms_collectibleProgress[static_cast<size_t>(packet.collectibleKind)] = packet.collectibleProgress;
    ApplyCollectibleProgress();

    local.pickup = packet.pickup;
    local.pendingRequestNonce = 0;
    local.awaitingAuthority = false;
    local.awaitingAuthoritySince = 0;
    local.collectionObserved = true;
    if (packet.pickup.lifecycle == ePickupLifecycle::REMOVED)
        RemoveLocalPickup(localSlot, false);
    else
        ApplyAuthoritativeState(localSlot, local.pickup);
}

void CNetworkPickupManager::MergeLocalCollectibleProgress()
{
    for (size_t index = 1; index < COLLECTIBLE_STATS.size(); ++index)
    {
        const float value = CStats::GetStatValue(COLLECTIBLE_STATS[index]);
        const uint8_t bounded = std::isfinite(value)
            ? static_cast<uint8_t>(std::clamp(value, 0.0f, static_cast<float>(MAX_COLLECTIBLE_PROGRESS)))
            : 0;
        ms_collectibleProgress[index] = std::max(ms_collectibleProgress[index], bounded);
    }
}

void CNetworkPickupManager::ApplyCollectibleProgress()
{
    for (size_t index = 1; index < COLLECTIBLE_STATS.size(); ++index)
        CStats::SetStatValue(COLLECTIBLE_STATS[index], ms_collectibleProgress[index]);
}

void CNetworkPickupManager::PublishCollectibleProgress()
{
    if (!CLocalPlayer::m_bIsHost || !ms_authorityReady || ms_progressPublishedEpoch == ms_authorityEpoch)
        return;
    PickupSnapshotChunk progress{};
    progress.authorityEpoch = ms_authorityEpoch;
    progress.snapshotRevision = ms_snapshotRevision;
    progress.authorityPlayerId = static_cast<uint8_t>(CNetworkPlayerManager::m_nMyId);
    progress.collectibleProgress = ms_collectibleProgress;
    GetPacketFactory().Send(progress);
    ms_progressPublishedEpoch = ms_authorityEpoch;
}

void CNetworkPickupManager::HandleSnapshotChunk(const PickupSnapshotChunk& packet)
{
    if (!packet.HasValidState() || !packet.FitsSerializedBudget() ||
        packet.authorityEpoch < ms_authorityEpoch ||
        (packet.authorityEpoch == ms_authorityEpoch && ms_snapshotRevision != 0 &&
            packet.snapshotRevision != ms_snapshotRevision &&
            !IsRevisionNewer(packet.snapshotRevision, ms_snapshotRevision)))
    {
        return;
    }

    if (packet.authorityEpoch != ms_authorityEpoch || packet.snapshotRevision != ms_snapshotRevision)
    {
        ms_authorityEpoch = packet.authorityEpoch;
        ms_snapshotRevision = packet.snapshotRevision;
        ms_snapshotAuthorityPlayerId = packet.authorityPlayerId;
        ms_snapshotChunkCount = packet.chunkCount;
        ms_snapshotEntryCount = 0;
        ms_snapshotChunksReceived.fill(false);
    }
    if (packet.chunkCount != ms_snapshotChunkCount || ms_snapshotChunksReceived[packet.chunkIndex] ||
        packet.authorityPlayerId != ms_snapshotAuthorityPlayerId ||
        ms_snapshotEntryCount + packet.entryCount > MAX_PICKUPS)
    {
        return;
    }

    bool hasEarlierChunk = false;
    for (bool received : ms_snapshotChunksReceived)
        hasEarlierChunk = hasEarlierChunk || received;
    if (hasEarlierChunk && ms_collectibleProgress != packet.collectibleProgress)
        return;
    for (uint8_t incoming = 0; incoming < packet.entryCount; ++incoming)
    {
        for (uint16_t existing = 0; existing < ms_snapshotEntryCount; ++existing)
        {
            if (ms_snapshotEntries[existing].identity.slot == packet.entries[incoming].identity.slot)
                return;
        }
        for (uint8_t earlier = 0; earlier < incoming; ++earlier)
        {
            if (packet.entries[earlier].identity.slot == packet.entries[incoming].identity.slot)
                return;
        }
    }

    ms_snapshotChunksReceived[packet.chunkIndex] = true;
    ms_collectibleProgress = packet.collectibleProgress;
    for (uint8_t index = 0; index < packet.entryCount; ++index)
        ms_snapshotEntries[ms_snapshotEntryCount++] = packet.entries[index];

    for (uint8_t index = 0; index < ms_snapshotChunkCount; ++index)
    {
        if (!ms_snapshotChunksReceived[index])
            return;
    }
    CompleteSnapshot();
    if (CLocalPlayer::m_bIsHost && ms_snapshotAuthorityPlayerId == CNetworkPlayerManager::m_nMyId)
    {
        MergeLocalCollectibleProgress();
        ApplyCollectibleProgress();
        ms_authorityReady = true;
        PublishCollectibleProgress();
    }
    else
    {
        ApplyCollectibleProgress();
    }
}

void CNetworkPickupManager::CompleteSnapshot()
{
    std::array<bool, MAX_PICKUPS> retained{};
    for (uint16_t entryIndex = 0; entryIndex < ms_snapshotEntryCount; ++entryIndex)
    {
        const PickupDescriptor& pickup = ms_snapshotEntries[entryIndex];
        ms_generations[pickup.identity.slot] = pickup.identity.generation;
        if (pickup.lifecycle == ePickupLifecycle::REMOVED)
            continue;
        int localSlot = FindIdentity(pickup.identity);
        if (localSlot < 0)
        {
            int match = FindUnboundMatch(pickup);
            bool generated = false;
            if (match < 0)
            {
                match = CreateMirror(pickup);
                generated = true;
            }
            if (match >= 0)
            {
                BindAndApply(match, pickup, generated);
                localSlot = match;
            }
        }
        else if (pickup.revision == ms_localPickups[localSlot].pickup.revision ||
                 IsRevisionNewer(pickup.revision, ms_localPickups[localSlot].pickup.revision))
        {
            ApplyAuthoritativeState(localSlot, pickup);
        }
        if (localSlot >= 0)
            retained[localSlot] = true;
    }

    for (uint16_t slot = 0; slot < MAX_PICKUPS; ++slot)
    {
        if (ms_localPickups[slot].tracked && ms_localPickups[slot].bound && !retained[slot])
            RemoveLocalPickup(slot, false);
    }
}

void CNetworkPickupManager::HandleAuthorityChanged(uint8_t, bool localIsAuthority)
{
    ms_authorityReady = localIsAuthority && ms_snapshotAuthorityPlayerId == CNetworkPlayerManager::m_nMyId &&
                        ms_authorityEpoch != 0;
    for (uint16_t slot = 0; slot < MAX_PICKUPS; ++slot)
    {
        if (!ms_localPickups[slot].tracked)
            continue;
        if (localIsAuthority && ms_authorityReady)
            PromoteMirrorForAuthority(slot);
        else
            MaskPeerPickup(slot);
    }
}

void CNetworkPickupManager::Process()
{
    if (!CNetwork::m_bAuthenticated)
        return;
    if (CLocalPlayer::m_bIsHost)
    {
        if (ms_authorityReady)
            ScanAuthorityPool();
        return;
    }
    ScanPeerPool();
    RequestNearbyCollections();
}

void CNetworkPickupManager::PrepareForNativePickupUpdate()
{
    if (CNetwork::m_bAuthenticated && !CLocalPlayer::m_bIsHost)
        ScanPeerPool();
}

void CNetworkPickupManager::ResetNetworkState()
{
    for (uint16_t slot = 0; slot < MAX_PICKUPS; ++slot)
    {
        LocalPickup local = ms_localPickups[slot];
        if (!local.tracked || !IsNativeSlotCurrent(slot, local))
            continue;
        if (local.networkGenerated)
            RemoveLocalPickup(slot, false);
        else
        {
            CPickup& native = CPickups::aPickUps[slot];
            native.m_nPickupType = local.pickup.pickupType;
            native.m_nFlags.bDisabled = local.pickup.lifecycle == ePickupLifecycle::DISABLED;
        }
    }
    ms_localPickups.fill(LocalPickup{});
    ms_generations.fill(0);
    ms_snapshotEntries.fill(PickupDescriptor{});
    ms_snapshotChunksReceived.fill(false);
    ms_collectibleProgress.fill(0);
    ms_snapshotEntryCount = 0;
    ms_snapshotChunkCount = 0;
    ms_snapshotRevision = 0;
    ms_authorityEpoch = 0;
    ms_requestNonce = 0;
    ms_progressPublishedEpoch = 0;
    ms_snapshotAuthorityPlayerId = UINT8_MAX;
    ms_authorityReady = false;
    ms_applyingNetworkState = false;
}
