#include "stdafx.h"
#include "CNetworkPickupManager.h"

#include "game_sa/CTagManager.h"
#include "network/packets/pickups.h"

#include <CPickup.h>
#include <CPickups.h>
#include <CPlayerInfo.h>
#include <CWeaponInfo.h>
#include <eModelID.h>

#include <algorithm>
#include <cmath>

using namespace Packets::Pickups;

std::array<CNetworkPickupManager::Slot, PICKUP_POOL_CAPACITY> CNetworkPickupManager::m_slots{};
std::array<int16_t, PICKUP_POOL_CAPACITY> CNetworkPickupManager::m_nativeToNetworkSlot{};
std::array<CNetworkPickupManager::ProvisionalNativePickup, PICKUP_POOL_CAPACITY>
    CNetworkPickupManager::m_provisionalNativePickups{};
std::array<CNetworkPickupManager::SuppressedJetpackDrop, Config::MAX_SERVER_PLAYERS>
    CNetworkPickupManager::m_suppressedJetpackDrops{};
bool CNetworkPickupManager::m_initialized = false;
bool CNetworkPickupManager::m_localPlayerIsAuthority = false;
uint8_t CNetworkPickupManager::m_authorityPlayerId = PICKUP_INVALID_PLAYER_ID;

namespace
{
constexpr float POSITION_EPSILON = 0.02f;
constexpr float NORMAL_COLLECT_DISTANCE = 2.0f;
constexpr float REQUEST_POSITION_TOLERANCE = 3.0f;
constexpr uint32_t DEFAULT_DROPPED_PICKUP_EXPIRY_MS = 30000;
constexpr uint32_t DEFAULT_JETPACK_EXPIRY_MS = 300000;
constexpr uint32_t STREET_PICKUP_RESPAWN_MS = 30000;
constexpr uint32_t SLOW_STREET_PICKUP_RESPAWN_MS = 36000;

float DistanceSquared(const CVector& left, const CVector& right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}

void NeutralizeNativePickup(int nativeSlot)
{
    if (nativeSlot < 0 || nativeSlot >= PICKUP_POOL_CAPACITY)
    {
        return;
    }
    CPickup& pickup = CPickups::aPickUps[nativeSlot];
    pickup.GetRidOfObjects();
    pickup.m_nPickupType = PICKUP_NONE;
    pickup.m_nFlags.bDisabled = true;
    pickup.m_nFlags.bVisible = false;
}

bool UsesCollectedPickupArray(ePickupKind kind)
{
    // Tags and snapshots are completed by CWorld::SprayPaintWorld and CPickups::PictureTaken respectively.
    // Every kind processed through CPickup::Update uses the unique pickup handle in the collected array.
    return kind != ePickupKind::TAG && kind != ePickupKind::SNAPSHOT;
}

bool AddNativeSlotToCollectedArray(int nativeSlot)
{
    if (nativeSlot < 0 || nativeSlot >= PICKUP_POOL_CAPACITY)
    {
        return false;
    }
    // 0x455240 takes a native pool index and packs GetUniquePickupIndex(index) internally. Passing an already
    // packed handle would index outside aPickUps when the function performs that conversion itself.
    CPickups::AddToCollectedPickupsArray(nativeSlot);
    return true;
}

unsigned char NativePickupTypeForKind(ePickupKind kind)
{
    switch (kind)
    {
    case ePickupKind::HORSESHOE:
    case ePickupKind::OYSTER:
        return PICKUP_COLLECTABLE1;
    case ePickupKind::SNAPSHOT:
        return PICKUP_SNAPSHOT;
    case ePickupKind::STATIC_WEAPON:
        return PICKUP_ON_STREET;
    case ePickupKind::STATIC_ARMOUR:
    case ePickupKind::STATIC_BRIBE:
        return PICKUP_ON_STREET_SLOW;
    case ePickupKind::DROPPED_MONEY:
        return PICKUP_MONEY;
    case ePickupKind::DROPPED_WEAPON:
    case ePickupKind::JETPACK:
        return PICKUP_ONCE_TIMEOUT;
    default:
        return PICKUP_NONE;
    }
}

uint32_t RemainingNativeLifetime(const CPickup& pickup, uint32_t fallback)
{
    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    if (pickup.m_nRegenerationTime > now)
    {
        return std::clamp(pickup.m_nRegenerationTime - now, 1000u, MAX_PICKUP_EXPIRY_MS);
    }
    return fallback;
}
}  // namespace

void CNetworkPickupManager::EnsureInitialized()
{
    if (m_initialized)
    {
        return;
    }
    m_nativeToNetworkSlot.fill(INVALID_NETWORK_SLOT);
    m_initialized = true;
}

void CNetworkPickupManager::ResetNetworkState()
{
    EnsureInitialized();
    for (Slot& slot : m_slots)
    {
        if (slot.ownsNativePickup && slot.nativeSlot != INVALID_NATIVE_SLOT)
        {
            NeutralizeNativePickup(slot.nativeSlot);
        }
    }
    m_slots.fill(Slot{});
    m_nativeToNetworkSlot.fill(INVALID_NETWORK_SLOT);
    m_provisionalNativePickups.fill(ProvisionalNativePickup{});
    m_suppressedJetpackDrops.fill(SuppressedJetpackDrop{});
    m_localPlayerIsAuthority = false;
    m_authorityPlayerId = PICKUP_INVALID_PLAYER_ID;
}

void CNetworkPickupManager::HandleAuthorityChanged(uint8_t authorityPlayerId, bool localPlayerIsAuthority)
{
    EnsureInitialized();
    m_authorityPlayerId = authorityPlayerId;
    m_localPlayerIsAuthority = localPlayerIsAuthority;
    for (Slot& slot : m_slots)
    {
        slot.localCollectPending = false;
        slot.localTagCompletionEvidence = false;
        slot.localCollectStartedAt = 0;
        if (slot.initialized)
        {
            slot.state.authorityPlayerId = authorityPlayerId;
        }
    }
}

uint16_t CNetworkPickupManager::NextGeneration(uint16_t generation)
{
    ++generation;
    return generation == 0 ? 1 : generation;
}

uint32_t CNetworkPickupManager::NextRevision(uint32_t revision)
{
    ++revision;
    return revision == 0 ? 1 : revision;
}

int CNetworkPickupManager::FindFreeNetworkSlot()
{
    for (int i = TAG_NETWORK_SLOT_COUNT; i < PICKUP_POOL_CAPACITY; ++i)
    {
        if (!m_slots[i].initialized || (!m_slots[i].active && !m_slots[i].state.hasCompletionState &&
            m_slots[i].respawnAt == 0 && m_slots[i].nativeSlot == INVALID_NATIVE_SLOT))
        {
            return i;
        }
    }
    return INVALID_NETWORK_SLOT;
}

int CNetworkPickupManager::FindNetworkSlot(const PickupId& id)
{
    if (!id.IsValid())
    {
        return INVALID_NETWORK_SLOT;
    }
    Slot& slot = m_slots[id.slot];
    return slot.initialized && slot.state.id.generation == id.generation
        ? static_cast<int>(id.slot)
        : INVALID_NETWORK_SLOT;
}

int CNetworkPickupManager::FindTagIndex(CEntity* tagEntity)
{
    if (tagEntity == nullptr)
    {
        return -1;
    }
    for (int i = 0; i < TAG_NETWORK_SLOT_COUNT; ++i)
    {
        if (CTagManager::ms_tagDesc[i].m_pEntity == tagEntity)
        {
            return i;
        }
    }
    return -1;
}

bool CNetworkPickupManager::MetadataMatches(const PickupMetadata& left, const PickupMetadata& right)
{
    return left.kind == right.kind &&
           std::fabs(left.position.x - right.position.x) <= POSITION_EPSILON &&
           std::fabs(left.position.y - right.position.y) <= POSITION_EPSILON &&
           std::fabs(left.position.z - right.position.z) <= POSITION_EPSILON &&
           left.interior == right.interior && left.modelId == right.modelId &&
           left.reward == right.reward && left.ammo == right.ammo &&
           left.collectibleIndex == right.collectibleIndex &&
           left.expiresAfterMs == right.expiresAfterMs &&
           left.respawnsAfterMs == right.respawnsAfterMs;
}

bool CNetworkPickupManager::TryBuildMetadata(int nativeSlot, const CPickup& pickup, PickupMetadata& metadata)
{
    metadata = {};
    metadata.position = const_cast<CPickup&>(pickup).GetPosn();
    CPlayerPed* localPlayer = FindPlayerPed(0);
    metadata.interior = localPlayer != nullptr ? localPlayer->m_nAreaCode : 0;
    metadata.modelId = pickup.m_nModelIndex;
    metadata.collectibleIndex = PICKUP_INVALID_COLLECTIBLE_INDEX;

    int sameKindBefore = 0;
    for (int i = 0; i < nativeSlot; ++i)
    {
        const CPickup& earlier = CPickups::aPickUps[i];
        if (pickup.m_nModelIndex == MODEL_CJ_HORSE_SHOE && earlier.m_nModelIndex == MODEL_CJ_HORSE_SHOE)
        {
            ++sameKindBefore;
        }
        else if (pickup.m_nModelIndex == MODEL_CJ_OYSTER && earlier.m_nModelIndex == MODEL_CJ_OYSTER)
        {
            ++sameKindBefore;
        }
        else if (pickup.m_nPickupType == PICKUP_SNAPSHOT && earlier.m_nPickupType == PICKUP_SNAPSHOT)
        {
            ++sameKindBefore;
        }
    }

    if (pickup.m_nModelIndex == MODEL_CJ_HORSE_SHOE)
    {
        metadata.kind = ePickupKind::HORSESHOE;
        metadata.collectibleIndex = static_cast<int16_t>(std::min(sameKindBefore, 49));
    }
    else if (pickup.m_nModelIndex == MODEL_CJ_OYSTER)
    {
        metadata.kind = ePickupKind::OYSTER;
        metadata.collectibleIndex = static_cast<int16_t>(std::min(sameKindBefore, 49));
    }
    else if (pickup.m_nPickupType == PICKUP_SNAPSHOT)
    {
        metadata.kind = ePickupKind::SNAPSHOT;
        metadata.collectibleIndex = static_cast<int16_t>(std::min(sameKindBefore, 49));
    }
    else if (pickup.m_nModelIndex == MODEL_BODYARMOUR)
    {
        metadata.kind = ePickupKind::STATIC_ARMOUR;
        metadata.respawnsAfterMs = pickup.m_nPickupType == PICKUP_ON_STREET_SLOW
            ? SLOW_STREET_PICKUP_RESPAWN_MS
            : STREET_PICKUP_RESPAWN_MS;
    }
    else if (pickup.m_nModelIndex == MODEL_BRIBE)
    {
        metadata.kind = ePickupKind::STATIC_BRIBE;
        metadata.respawnsAfterMs = pickup.m_nPickupType == PICKUP_ON_STREET_SLOW
            ? SLOW_STREET_PICKUP_RESPAWN_MS
            : STREET_PICKUP_RESPAWN_MS;
    }
    else if (pickup.m_nModelIndex == MODEL_MONEY || pickup.m_nPickupType == PICKUP_MONEY ||
             pickup.m_nPickupType == PICKUP_MONEY_DOESNTDISAPPEAR)
    {
        metadata.kind = ePickupKind::DROPPED_MONEY;
        metadata.reward = std::clamp<int32_t>(static_cast<int32_t>(pickup.m_nAmmo), 1, MAX_PICKUP_REWARD);
        metadata.expiresAfterMs = RemainingNativeLifetime(pickup, DEFAULT_DROPPED_PICKUP_EXPIRY_MS);
    }
    else if (pickup.m_nModelIndex == MODEL_JETPACK)
    {
        metadata.kind = ePickupKind::JETPACK;
        metadata.expiresAfterMs = RemainingNativeLifetime(pickup, DEFAULT_JETPACK_EXPIRY_MS);
    }
    else
    {
        if (pickup.m_nModelIndex < 0 || pickup.m_nModelIndex > MAX_PICKUP_MODEL_ID ||
            CModelInfo::ms_modelInfoPtrs[pickup.m_nModelIndex] == nullptr)
        {
            return false;
        }
        const int weaponId = CPickups::WeaponForModel(pickup.m_nModelIndex);
        if (!IsGrantablePickupWeaponId(weaponId))
        {
            return false;
        }
        metadata.reward = weaponId;
        metadata.ammo = static_cast<uint16_t>(std::clamp<unsigned int>(pickup.m_nAmmo, 1, MAX_PICKUP_AMMO));
        if (pickup.m_nPickupType == PICKUP_ON_STREET || pickup.m_nPickupType == PICKUP_ON_STREET_SLOW)
        {
            metadata.kind = ePickupKind::STATIC_WEAPON;
            metadata.respawnsAfterMs = pickup.m_nPickupType == PICKUP_ON_STREET_SLOW
                ? SLOW_STREET_PICKUP_RESPAWN_MS
                : STREET_PICKUP_RESPAWN_MS;
        }
        else if (pickup.m_nPickupType == PICKUP_ONCE || pickup.m_nPickupType == PICKUP_ONCE_TIMEOUT ||
                 pickup.m_nPickupType == PICKUP_ONCE_TIMEOUT_SLOW ||
                 pickup.m_nPickupType == PICKUP_ONCE_FOR_MISSION)
        {
            metadata.kind = ePickupKind::DROPPED_WEAPON;
            metadata.expiresAfterMs = RemainingNativeLifetime(pickup, DEFAULT_DROPPED_PICKUP_EXPIRY_MS);
        }
        else
        {
            return false;
        }
    }
    return metadata.HasValidSemantics();
}

int CNetworkPickupManager::FindMatchingProvisionalNativeSlot(const PickupMetadata& metadata)
{
    for (int nativeSlot = 0; nativeSlot < PICKUP_POOL_CAPACITY; ++nativeSlot)
    {
        if (m_nativeToNetworkSlot[nativeSlot] == PROVISIONAL_NETWORK_SLOT &&
            m_provisionalNativePickups[nativeSlot].active &&
            MetadataMatches(m_provisionalNativePickups[nativeSlot].metadata, metadata))
        {
            return nativeSlot;
        }
    }

    // A canonical state can arrive before the next pool-observation tick. Discover and reserve the matching
    // native pickup here so SCM variables keep the handle returned by their original creation opcode.
    for (int nativeSlot = 0; nativeSlot < PICKUP_POOL_CAPACITY; ++nativeSlot)
    {
        if (m_nativeToNetworkSlot[nativeSlot] != INVALID_NETWORK_SLOT ||
            CPickups::aPickUps[nativeSlot].m_nPickupType == PICKUP_NONE)
        {
            continue;
        }
        PickupMetadata observed{};
        if (TryBuildMetadata(nativeSlot, CPickups::aPickUps[nativeSlot], observed) &&
            MetadataMatches(observed, metadata))
        {
            m_nativeToNetworkSlot[nativeSlot] = PROVISIONAL_NETWORK_SLOT;
            m_provisionalNativePickups[nativeSlot].active = true;
            m_provisionalNativePickups[nativeSlot].metadata = observed;
            return nativeSlot;
        }
    }
    return INVALID_NATIVE_SLOT;
}

bool CNetworkPickupManager::BindNativePickup(Slot& slot, int nativeSlot)
{
    const int networkSlot = static_cast<int>(&slot - m_slots.data());
    if (nativeSlot < 0 || nativeSlot >= PICKUP_POOL_CAPACITY ||
        (m_nativeToNetworkSlot[nativeSlot] != INVALID_NETWORK_SLOT &&
            m_nativeToNetworkSlot[nativeSlot] != PROVISIONAL_NETWORK_SLOT &&
            m_nativeToNetworkSlot[nativeSlot] != static_cast<int16_t>(networkSlot)))
    {
        return false;
    }
    const int pickupHandle = CPickups::GetUniquePickupIndex(nativeSlot);
    if (CPickups::GetActualPickupIndex(pickupHandle) != nativeSlot)
    {
        return false;
    }
    slot.nativeSlot = nativeSlot;
    m_nativeToNetworkSlot[nativeSlot] = static_cast<int16_t>(networkSlot);
    m_provisionalNativePickups[nativeSlot] = {};
    return true;
}

bool CNetworkPickupManager::PublishNewState(int networkSlot, int nativeSlot,
    const PickupMetadata& metadata, uint8_t creatorPlayerId, uint32_t sourceIntentRequestId)
{
    if (!m_localPlayerIsAuthority || networkSlot < 0 || networkSlot >= PICKUP_POOL_CAPACITY ||
        creatorPlayerId >= Config::MAX_SERVER_PLAYERS || !metadata.HasValidSemantics())
    {
        return false;
    }

    Slot& slot = m_slots[networkSlot];
    PickupStateEvent event{};
    event.state.id.slot = static_cast<uint16_t>(networkSlot);
    event.state.id.generation = NextGeneration(slot.initialized ? slot.state.id.generation : 0);
    event.state.revision = 1;
    event.state.authorityPlayerId = m_authorityPlayerId;
    event.state.active = true;
    event.state.creatorPlayerId = creatorPlayerId;
    event.state.sourceIntentRequestId = sourceIntentRequestId;
    event.state.metadata = metadata;
    if (!event.HasValidPayload() || !event.FitsSerializedBudget())
    {
        return false;
    }

    if (nativeSlot != INVALID_NATIVE_SLOT)
    {
        if (slot.nativeSlot != INVALID_NATIVE_SLOT && slot.nativeSlot != nativeSlot)
        {
            m_nativeToNetworkSlot[slot.nativeSlot] = INVALID_NETWORK_SLOT;
        }
        if (!BindNativePickup(slot, nativeSlot))
        {
            return false;
        }
        slot.ownsNativePickup = false;
    }
    ApplyActiveState(networkSlot, event.state);
    GetPacketFactory().Send(event);
    return true;
}

void CNetworkPickupManager::PublishRemoval(int networkSlot)
{
    if (!m_localPlayerIsAuthority || networkSlot < 0 || networkSlot >= PICKUP_POOL_CAPACITY)
    {
        return;
    }
    Slot& slot = m_slots[networkSlot];
    if (!slot.active)
    {
        return;
    }
    PickupStateEvent event{};
    event.state.id = slot.state.id;
    event.state.revision = NextRevision(slot.state.revision);
    event.state.authorityPlayerId = m_authorityPlayerId;
    event.state.active = false;
    GetPacketFactory().Send(event);
    RemoveNativePickup(slot, false);
    slot.initialized = true;
    slot.active = false;
    slot.state = event.state;
}

bool CNetworkPickupManager::MaterializeNativePickup(Slot& slot)
{
    if (!slot.active || slot.state.metadata.kind == ePickupKind::TAG)
    {
        return true;
    }
    const PickupMetadata& metadata = slot.state.metadata;
    const int modelId = metadata.modelId;
    if (modelId < 0 || CStreaming::ms_aInfoForModel[modelId].m_nLoadState != LOADSTATE_LOADED)
    {
        return false;
    }

    if (slot.nativeSlot == INVALID_NATIVE_SLOT)
    {
        const int provisionalSlot = FindMatchingProvisionalNativeSlot(metadata);
        if (provisionalSlot != INVALID_NATIVE_SLOT)
        {
            slot.ownsNativePickup = false;
            if (!BindNativePickup(slot, provisionalSlot))
            {
                return false;
            }
        }
        else
        {
            const unsigned char nativeType = NativePickupTypeForKind(metadata.kind);
            if (nativeType == PICKUP_NONE)
            {
                return false;
            }
            const unsigned int nativeAmmo = metadata.kind == ePickupKind::DROPPED_MONEY
                ? static_cast<unsigned int>(metadata.reward)
                : metadata.ammo;
            const int pickupHandle = CPickups::GenerateNewOne(
                CVector(metadata.position.x, metadata.position.y, metadata.position.z),
                static_cast<unsigned int>(modelId), nativeType, nativeAmmo, 0, false, nullptr);
            if (pickupHandle < 0)
            {
                return false;
            }
            const int generatedNativeSlot = CPickups::GetActualPickupIndex(pickupHandle);
            if (generatedNativeSlot < 0 || generatedNativeSlot >= PICKUP_POOL_CAPACITY ||
                !BindNativePickup(slot, generatedNativeSlot))
            {
                if (generatedNativeSlot >= 0 && generatedNativeSlot < PICKUP_POOL_CAPACITY)
                {
                    NeutralizeNativePickup(generatedNativeSlot);
                }
                return false;
            }
            slot.ownsNativePickup = true;
        }
    }

    CPickup& pickup = CPickups::aPickUps[slot.nativeSlot];
    // The reference index belongs to the native pickup handle. Never rewrite it when reusing an SCM-created
    // pickup; GenerateNewOne above establishes a fresh reference generation for genuinely new/reused slots.
    pickup.GetRidOfObjects();
    pickup.m_fRevenueValue = 0.0f;
    pickup.m_pObject = nullptr;
    pickup.m_nAmmo = metadata.kind == ePickupKind::DROPPED_MONEY
        ? static_cast<unsigned int>(metadata.reward)
        : metadata.ammo;
    pickup.m_nRegenerationTime = 0;
    pickup.SetPosn(metadata.position.x, metadata.position.y, metadata.position.z);
    pickup.m_nMoneyPerDay = 0;
    pickup.m_nModelIndex = modelId;
    pickup.m_nPickupType = NativePickupTypeForKind(metadata.kind);
    pickup.m_nFlags.bDisabled = false;
    pickup.m_nFlags.bEmpty = false;
    pickup.m_nFlags.bHelpMessageDisplayed = false;
    pickup.m_nFlags.bVisible = false;
    pickup.m_nFlags.nPropertyTextIndex = 0;
    slot.materialized = true;
    return true;
}

void CNetworkPickupManager::ReleaseModelIfUnused(int16_t modelId, int exceptNetworkSlot)
{
    if (modelId < 0 || modelId > MAX_PICKUP_MODEL_ID)
    {
        return;
    }
    for (int i = 0; i < PICKUP_POOL_CAPACITY; ++i)
    {
        if (i != exceptNetworkSlot && m_slots[i].active && m_slots[i].state.metadata.modelId == modelId)
        {
            return;
        }
    }
    CStreaming::SetModelIsDeletable(modelId);
    CStreaming::SetModelTxdIsDeletable(modelId);
}

void CNetworkPickupManager::RemoveNativePickup(Slot& slot, bool notifyScripts, bool preserveNativeHandle)
{
    if (slot.nativeSlot == INVALID_NATIVE_SLOT)
    {
        return;
    }
    const int nativeSlot = slot.nativeSlot;
    const int16_t modelId = slot.state.metadata.modelId;
    if (notifyScripts && UsesCollectedPickupArray(slot.state.metadata.kind))
    {
        AddNativeSlotToCollectedArray(nativeSlot);
    }
    CPickup& pickup = CPickups::aPickUps[nativeSlot];
    pickup.GetRidOfObjects();
    pickup.m_nFlags.bDisabled = true;
    pickup.m_nFlags.bVisible = false;
    slot.materialized = false;
    if (!preserveNativeHandle)
    {
        NeutralizeNativePickup(nativeSlot);
        m_nativeToNetworkSlot[nativeSlot] = INVALID_NETWORK_SLOT;
        m_provisionalNativePickups[nativeSlot] = {};
        slot.nativeSlot = INVALID_NATIVE_SLOT;
        slot.ownsNativePickup = false;
    }
    ReleaseModelIfUnused(modelId, slot.state.id.slot);
}

void CNetworkPickupManager::ApplyActiveState(int networkSlot, const PickupState& state)
{
    Slot& slot = m_slots[networkSlot];
    const bool identityChanged = !slot.initialized || slot.state.id.generation != state.id.generation;
    const bool metadataChanged = slot.initialized && !MetadataMatches(slot.state.metadata, state.metadata);
    if (slot.nativeSlot != INVALID_NATIVE_SLOT && metadataChanged)
    {
        slot.respawnAt = 0;
        RemoveNativePickup(slot, false);
    }
    if (slot.nativeSlot == INVALID_NATIVE_SLOT)
    {
        const int matchingNativeSlot = FindMatchingProvisionalNativeSlot(state.metadata);
        if (matchingNativeSlot != INVALID_NATIVE_SLOT)
        {
            BindNativePickup(slot, matchingNativeSlot);
            slot.ownsNativePickup = false;
        }
    }
    slot.initialized = true;
    slot.active = true;
    slot.localCollectPending = false;
    slot.localTagCompletionEvidence = false;
    slot.localCollectStartedAt = 0;
    slot.retryAfter = 0;
    slot.respawnAt = 0;
    slot.state = state;
    if (identityChanged)
    {
        slot.lastGrantedGeneration = 0;
    }
    if (identityChanged && slot.nativeSlot != INVALID_NATIVE_SLOT)
    {
        CPickup& pickup = CPickups::aPickUps[slot.nativeSlot];
        PickupMetadata observed{};
        slot.materialized = !pickup.m_nFlags.bDisabled && pickup.m_nPickupType != PICKUP_NONE &&
            TryBuildMetadata(slot.nativeSlot, pickup, observed) && MetadataMatches(observed, state.metadata);
    }
}

void CNetworkPickupManager::HandleState(const PickupStateEvent& packet)
{
    EnsureInitialized();
    if (!packet.HasValidPayload() || !packet.FitsSerializedBudget())
    {
        return;
    }
    const PickupState& incoming = packet.state;
    Slot& slot = m_slots[incoming.id.slot];
    if (slot.initialized)
    {
        const bool sameGeneration = incoming.id.generation == slot.state.id.generation;
        const bool newerGeneration = IsPickupGenerationNewer(incoming.id.generation, slot.state.id.generation);
        const bool authorityReplay = sameGeneration && incoming.revision == slot.state.revision &&
            incoming.authorityPlayerId != slot.state.authorityPlayerId;
        if (!sameGeneration && !newerGeneration)
        {
            return;
        }
        if (sameGeneration && !authorityReplay &&
            !IsPickupRevisionNewer(incoming.revision, slot.state.revision))
        {
            return;
        }
    }
    m_authorityPlayerId = incoming.authorityPlayerId;
    if (incoming.active)
    {
        ApplyActiveState(incoming.id.slot, incoming);
        return;
    }
    const bool wasInitialized = slot.initialized;
    const bool wasActive = slot.active;
    if (incoming.hasCompletionState && slot.nativeSlot == INVALID_NATIVE_SLOT &&
        incoming.metadata.kind != ePickupKind::TAG)
    {
        const int matchingNativeSlot = FindMatchingProvisionalNativeSlot(incoming.metadata);
        if (matchingNativeSlot != INVALID_NATIVE_SLOT)
        {
            BindNativePickup(slot, matchingNativeSlot);
            slot.ownsNativePickup = false;
        }
    }
    slot.initialized = true;
    slot.active = false;
    slot.localCollectPending = false;
    slot.localTagCompletionEvidence = false;
    slot.localCollectStartedAt = 0;
    slot.state = incoming;
    slot.respawnAt = incoming.hasCompletionState && incoming.respawnRemainingMs > 0
        ? GetTickCount() + incoming.respawnRemainingMs
        : 0;
    if (incoming.hasCompletionState && incoming.metadata.kind == ePickupKind::TAG)
    {
        CompleteTagVisual(incoming, false);
    }
    const bool notifyScripts = incoming.hasCompletionState &&
        (wasActive || !wasInitialized);
    RemoveNativePickup(slot, notifyScripts, incoming.respawnRemainingMs > 0);
}

bool CNetworkPickupManager::IsTagReady(const Slot& slot)
{
    if (!slot.active || slot.state.metadata.kind != ePickupKind::TAG)
    {
        return true;
    }
    const int index = slot.state.metadata.collectibleIndex;
    return index >= 0 && index < TAG_NETWORK_SLOT_COUNT &&
           CTagManager::ms_tagDesc[index].m_pEntity != nullptr &&
           CTagManager::ms_tagDesc[index].m_nAlpha == 255;
}

bool CNetworkPickupManager::IsPickupEligibleForPed(const Slot& slot, CPed* ped,
    uint8_t interior, bool requireNormalCollectDistance, bool requireLocalTagEvidence)
{
    if (!slot.active || ped == nullptr || ped->m_fHealth <= 0.0f ||
        interior != slot.state.metadata.interior)
    {
        return false;
    }
    const float maxDistance = requireNormalCollectDistance &&
        slot.state.metadata.kind != ePickupKind::TAG && slot.state.metadata.kind != ePickupKind::SNAPSHOT
        ? NORMAL_COLLECT_DISTANCE
        : MAX_PICKUP_COLLECT_DISTANCE;
    if (DistanceSquared(ped->GetPosition(), slot.state.metadata.position) > maxDistance * maxDistance)
    {
        return false;
    }
    if (ped->m_nPedFlags.bInVehicle && slot.state.metadata.kind != ePickupKind::STATIC_BRIBE)
    {
        return false;
    }
    switch (slot.state.metadata.kind)
    {
    case ePickupKind::TAG:
        return !requireLocalTagEvidence || IsTagReady(slot) || slot.localTagCompletionEvidence;
    case ePickupKind::STATIC_ARMOUR:
        return ped->m_fArmour < 99.0f;
    case ePickupKind::STATIC_BRIBE:
        return static_cast<CPlayerPed*>(ped)->GetWantedLevel() > 0;
    case ePickupKind::JETPACK:
        return !CUtil::IsPedHasJetpack(ped);
    default:
        return true;
    }
}

void CNetworkPickupManager::SendCollectRequest(int networkSlot)
{
    if (!CNetwork::m_bAuthenticated || networkSlot < 0 || networkSlot >= PICKUP_POOL_CAPACITY)
    {
        return;
    }
    Slot& slot = m_slots[networkSlot];
    CPlayerPed* localPlayer = FindPlayerPed(0);
    const uint32_t now = GetTickCount();
    if (slot.localCollectPending || localPlayer == nullptr ||
        (slot.retryAfter != 0 && static_cast<int32_t>(now - slot.retryAfter) < 0) ||
        !IsPickupEligibleForPed(slot, localPlayer, localPlayer->m_nAreaCode, true, true))
    {
        return;
    }
    PickupCollectRequest request{};
    request.requestId = 0;
    request.requesterPlayerId.value = CNetworkPlayerManager::m_nMyId;
    request.id = slot.state.id;
    request.requesterPosition = localPlayer->GetPosition();
    request.interior = localPlayer->m_nAreaCode;
    GetPacketFactory().Send(request);
    slot.localCollectPending = true;
    slot.localCollectStartedAt = now;
}

void CNetworkPickupManager::HandleCollectRequest(const PickupCollectRequest& packet)
{
    EnsureInitialized();
    if (!m_localPlayerIsAuthority || packet.requestId == 0 || !packet.id.IsValid())
    {
        return;
    }
    const int networkSlot = FindNetworkSlot(packet.id);
    bool approved = networkSlot != INVALID_NETWORK_SLOT;
    CPed* requesterPed = nullptr;
    if (approved && packet.requesterPlayerId.value == CNetworkPlayerManager::m_nMyId)
    {
        requesterPed = FindPlayerPed(0);
    }
    else if (approved)
    {
        CNetworkPlayer* requester = CNetworkPlayerManager::GetPlayer(packet.requesterPlayerId.value);
        requesterPed = requester != nullptr ? requester->m_pPed : nullptr;
    }
    if (requesterPed == nullptr || packet.interior != requesterPed->m_nAreaCode ||
        DistanceSquared(packet.requesterPosition, requesterPed->GetPosition()) >
            REQUEST_POSITION_TOLERANCE * REQUEST_POSITION_TOLERANCE)
    {
        approved = false;
    }
    const bool requesterIsLocalHost = approved &&
        packet.requesterPlayerId.value == CNetworkPlayerManager::m_nMyId;
    if (approved && !IsPickupEligibleForPed(m_slots[networkSlot], requesterPed,
            requesterPed->m_nAreaCode, true, requesterIsLocalHost))
    {
        approved = false;
    }
    PickupCollectDecision decision{};
    decision.requestId = packet.requestId;
    decision.id = packet.id;
    decision.approved = approved;
    GetPacketFactory().Send(decision);
    if (requesterIsLocalHost && networkSlot != INVALID_NETWORK_SLOT)
    {
        m_slots[networkSlot].localTagCompletionEvidence = false;
    }
}

void CNetworkPickupManager::GrantApprovedPickup(const PickupState& state)
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (localPlayer == nullptr)
    {
        return;
    }
    const PickupMetadata& metadata = state.metadata;
    switch (metadata.kind)
    {
    case ePickupKind::TAG:
        // CompleteTagVisual owns the exactly-once local statistic and visual transition.
        break;
    case ePickupKind::HORSESHOE:
        CPickups::PickedUpHorseShoe();
        break;
    case ePickupKind::OYSTER:
        CPickups::PickedUpOyster();
        break;
    case ePickupKind::SNAPSHOT:
        CStats::IncrementStat(STAT_SNAPSHOTS_TAKEN, 1.0f);
        localPlayer->GetPlayerInfoForThisPlayerPed()->m_nMoney += 100;
        break;
    case ePickupKind::STATIC_WEAPON:
    case ePickupKind::DROPPED_WEAPON:
        localPlayer->GiveDelayedWeapon(static_cast<eWeaponType>(metadata.reward), metadata.ammo);
        break;
    case ePickupKind::STATIC_ARMOUR:
    case ePickupKind::STATIC_BRIBE:
    case ePickupKind::JETPACK:
        CPickups::GivePlayerGoodiesWithPickUpMI(static_cast<uint16_t>(metadata.modelId), 0);
        break;
    case ePickupKind::DROPPED_MONEY:
        localPlayer->GetPlayerInfoForThisPlayerPed()->m_nMoney += metadata.reward;
        break;
    default:
        break;
    }
}

void CNetworkPickupManager::CompleteTagVisual(const PickupState& state, bool rewardCollector)
{
    if (state.metadata.kind != ePickupKind::TAG)
    {
        return;
    }
    const int index = state.metadata.collectibleIndex;
    if (index < 0 || index >= TAG_NETWORK_SLOT_COUNT || CTagManager::ms_tagDesc[index].m_pEntity == nullptr)
    {
        return;
    }
    const float previousTagStat = CStats::GetStatValue(STAT_TAGS_SPRAYED);
    const int32_t previousTaggedCount = CTagManager::ms_numTagged;
    CTagManager::SetAlpha(CTagManager::ms_tagDesc[index].m_pEntity, 255);
    CStats::SetStatValue(STAT_TAGS_SPRAYED,
        rewardCollector ? previousTagStat + 1.0f : previousTagStat);
    CTagManager::ms_numTagged = rewardCollector ? previousTaggedCount + 1 : previousTaggedCount;
}

void CNetworkPickupManager::HandleCollectResult(const PickupCollectResult& packet)
{
    EnsureInitialized();
    const int networkSlot = FindNetworkSlot(packet.id);
    if (networkSlot == INVALID_NETWORK_SLOT)
    {
        return;
    }
    Slot& slot = m_slots[networkSlot];
    if (!packet.approved)
    {
        if (packet.collectorPlayerId == CNetworkPlayerManager::m_nMyId)
        {
            slot.localCollectPending = false;
            slot.localTagCompletionEvidence = false;
            slot.localCollectStartedAt = 0;
            slot.retryAfter = GetTickCount() + DENIED_RETRY_COOLDOWN_MS;
        }
        return;
    }
    if (!packet.grantedState.active || !(packet.grantedState.id == packet.id) ||
        slot.lastGrantedGeneration == packet.id.generation)
    {
        return;
    }

    slot.state = packet.grantedState;
    slot.lastGrantedGeneration = packet.id.generation;
    slot.localCollectPending = false;
    slot.localTagCompletionEvidence = false;
    slot.localCollectStartedAt = 0;
    slot.retryAfter = 0;
    slot.respawnAt = 0;
    const bool preserveNativeHandle = packet.grantedState.metadata.respawnsAfterMs > 0;
    RemoveNativePickup(slot, true, preserveNativeHandle);
    slot.active = false;
    const bool localCollector = packet.collectorPlayerId == CNetworkPlayerManager::m_nMyId;
    if (packet.grantedState.metadata.kind == ePickupKind::TAG)
    {
        CompleteTagVisual(packet.grantedState, localCollector);
    }
    if (localCollector)
    {
        GrantApprovedPickup(packet.grantedState);
    }
}

void CNetworkPickupManager::HandleCreateIntent(const PickupCreateIntent& packet)
{
    EnsureInitialized();
    if (!m_localPlayerIsAuthority || packet.requestId == 0 || !packet.HasValidPayload() ||
        packet.requesterPlayerId.value >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }
    const int networkSlot = FindFreeNetworkSlot();
    if (networkSlot == INVALID_NETWORK_SLOT)
    {
        return;
    }
    PublishNewState(networkSlot, INVALID_NATIVE_SLOT, packet.metadata,
        packet.requesterPlayerId.value, packet.requestId);
}

bool CNetworkPickupManager::IsSyntheticJetpackDrop(const PickupMetadata& metadata)
{
    if (metadata.kind != ePickupKind::JETPACK)
    {
        return false;
    }
    const uint32_t now = GetTickCount();
    for (SuppressedJetpackDrop& suppressed : m_suppressedJetpackDrops)
    {
        if (suppressed.active && static_cast<int32_t>(now - suppressed.expiresAt) >= 0)
        {
            suppressed = {};
        }
        if (suppressed.active && DistanceSquared(suppressed.position, metadata.position) <= 9.0f)
        {
            suppressed = {};
            return true;
        }
    }
    return false;
}

void CNetworkPickupManager::SuppressSyntheticJetpackDrop(const CVector& position)
{
    EnsureInitialized();
    SuppressedJetpackDrop* target = nullptr;
    for (SuppressedJetpackDrop& entry : m_suppressedJetpackDrops)
    {
        if (!entry.active)
        {
            target = &entry;
            break;
        }
    }
    if (target == nullptr)
    {
        target = &m_suppressedJetpackDrops[0];
    }
    target->active = true;
    target->position = position;
    target->expiresAt = GetTickCount() + SYNTHETIC_DROP_SUPPRESSION_MS;
}

void CNetworkPickupManager::ObserveTags()
{
    if (!m_localPlayerIsAuthority || m_authorityPlayerId >= Config::MAX_SERVER_PLAYERS)
    {
        return;
    }
    for (int i = 0; i < TAG_NETWORK_SLOT_COUNT; ++i)
    {
        tTagDesc& tag = CTagManager::ms_tagDesc[i];
        if (tag.m_pEntity == nullptr || tag.m_nAlpha == 255 || m_slots[i].active)
        {
            continue;
        }
        PickupMetadata metadata{};
        metadata.kind = ePickupKind::TAG;
        metadata.position = tag.m_pEntity->GetPosition();
        metadata.interior = tag.m_pEntity->m_nAreaCode;
        metadata.modelId = -1;
        metadata.collectibleIndex = static_cast<int16_t>(i);
        PublishNewState(i, INVALID_NATIVE_SLOT, metadata,
            static_cast<uint8_t>(CNetworkPlayerManager::m_nMyId), 0);
    }
}

void CNetworkPickupManager::ObserveNativePool()
{
    for (int nativeSlot = 0; nativeSlot < PICKUP_POOL_CAPACITY; ++nativeSlot)
    {
        CPickup& pickup = CPickups::aPickUps[nativeSlot];
        const int mappedNetworkSlot = m_nativeToNetworkSlot[nativeSlot];
        if (mappedNetworkSlot == PROVISIONAL_NETWORK_SLOT)
        {
            if (pickup.m_nPickupType == PICKUP_NONE)
            {
                m_nativeToNetworkSlot[nativeSlot] = INVALID_NETWORK_SLOT;
                m_provisionalNativePickups[nativeSlot] = {};
            }
            continue;
        }
        if (mappedNetworkSlot != INVALID_NETWORK_SLOT)
        {
            Slot& mapped = m_slots[mappedNetworkSlot];
            if (mapped.active && pickup.m_nPickupType == PICKUP_NONE)
            {
                if (m_localPlayerIsAuthority)
                {
                    PublishRemoval(mappedNetworkSlot);
                }
                else
                {
                    mapped.materialized = false;
                }
            }
            continue;
        }
        if (pickup.m_nPickupType == PICKUP_NONE)
        {
            continue;
        }

        PickupMetadata metadata{};
        if (!TryBuildMetadata(nativeSlot, pickup, metadata))
        {
            continue;
        }
        if (IsSyntheticJetpackDrop(metadata))
        {
            NeutralizeNativePickup(nativeSlot);
            continue;
        }
        if (m_localPlayerIsAuthority)
        {
            const int networkSlot = FindFreeNetworkSlot();
            if (networkSlot != INVALID_NETWORK_SLOT)
            {
                PublishNewState(networkSlot, nativeSlot, metadata,
                    static_cast<uint8_t>(CNetworkPlayerManager::m_nMyId), 0);
            }
        }
        else
        {
            m_nativeToNetworkSlot[nativeSlot] = PROVISIONAL_NETWORK_SLOT;
            ProvisionalNativePickup& provisional = m_provisionalNativePickups[nativeSlot];
            provisional.active = true;
            provisional.metadata = metadata;
            if (IsCreationIntentPickupKind(metadata.kind) && !provisional.creationIntentSent)
            {
                PickupCreateIntent intent{};
                intent.requestId = 0;
                intent.requesterPlayerId.value = CNetworkPlayerManager::m_nMyId;
                intent.metadata = metadata;
                GetPacketFactory().Send(intent);
                provisional.creationIntentSent = true;
            }
        }
    }
}

void CNetworkPickupManager::ProcessModelRequests()
{
    const uint32_t now = GetTickCount();
    uint8_t requestedThisTick = 0;
    for (Slot& slot : m_slots)
    {
        if (!slot.active || slot.state.metadata.modelId < 0)
        {
            continue;
        }
        const int modelId = slot.state.metadata.modelId;
        if (CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED)
        {
            continue;
        }
        if (slot.modelRequestedAt == 0 || now - slot.modelRequestedAt >= MODEL_REQUEST_RETRY_MS)
        {
            CStreaming::RequestModel(modelId,
                eStreamingFlags::GAME_REQUIRED | eStreamingFlags::PRIORITY_REQUEST);
            slot.modelRequestedAt = now;
            if (++requestedThisTick >= MAX_MODEL_REQUESTS_PER_TICK)
            {
                break;
            }
        }
    }
}

void CNetworkPickupManager::ProcessMaterializations()
{
    uint8_t materializedThisTick = 0;
    for (Slot& slot : m_slots)
    {
        if (!slot.active || slot.materialized || slot.state.metadata.kind == ePickupKind::TAG)
        {
            continue;
        }
        if (slot.state.metadata.modelId >= 0 &&
            CStreaming::ms_aInfoForModel[slot.state.metadata.modelId].m_nLoadState != LOADSTATE_LOADED)
        {
            continue;
        }
        MaterializeNativePickup(slot);
        if (++materializedThisTick >= MAX_MATERIALIZATIONS_PER_TICK)
        {
            break;
        }
    }
}

void CNetworkPickupManager::ProcessLocalCollection()
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (localPlayer == nullptr)
    {
        return;
    }
    const uint32_t now = GetTickCount();
    for (int i = 0; i < PICKUP_POOL_CAPACITY; ++i)
    {
        Slot& slot = m_slots[i];
        if (slot.localCollectPending && now - slot.localCollectStartedAt >= LOCAL_REQUEST_TIMEOUT_MS)
        {
            slot.localCollectPending = false;
            slot.localTagCompletionEvidence = false;
            slot.localCollectStartedAt = 0;
            slot.retryAfter = now + DENIED_RETRY_COOLDOWN_MS;
        }
        if (!slot.active || slot.localCollectPending || slot.state.metadata.kind == ePickupKind::TAG ||
            slot.state.metadata.kind == ePickupKind::SNAPSHOT)
        {
            continue;
        }
        if (IsPickupEligibleForPed(slot, localPlayer, localPlayer->m_nAreaCode, true))
        {
            SendCollectRequest(i);
        }
    }
}

void CNetworkPickupManager::ProcessBeforeNativeUpdate()
{
    EnsureInitialized();
    if (!CNetwork::m_bAuthenticated || CNetworkPlayerManager::m_nMyId < 0)
    {
        return;
    }
    ObserveTags();
    ObserveNativePool();
    ProcessModelRequests();
    ProcessMaterializations();
    ProcessLocalCollection();
}

bool CNetworkPickupManager::IsManagedNativeSlot(int nativeSlot)
{
    EnsureInitialized();
    return nativeSlot >= 0 && nativeSlot < PICKUP_POOL_CAPACITY &&
           m_nativeToNetworkSlot[nativeSlot] != INVALID_NETWORK_SLOT;
}

bool CNetworkPickupManager::CanRenderNativeSlot(int nativeSlot)
{
    if (!IsManagedNativeSlot(nativeSlot))
    {
        return true;
    }
    const int networkSlot = m_nativeToNetworkSlot[nativeSlot];
    if (networkSlot == PROVISIONAL_NETWORK_SLOT)
    {
        const int modelId = m_provisionalNativePickups[nativeSlot].metadata.modelId;
        return m_provisionalNativePickups[nativeSlot].active && modelId >= 0 &&
            CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED;
    }
    const Slot& slot = m_slots[networkSlot];
    const int modelId = slot.state.metadata.modelId;
    return slot.active && slot.materialized && modelId >= 0 &&
           CStreaming::ms_aInfoForModel[modelId].m_nLoadState == LOADSTATE_LOADED;
}

void CNetworkPickupManager::NotifyLocalTagSprayed(CEntity* tagEntity, float previousTagStat,
    int32_t previousTaggedCount, uint8_t previousAlpha)
{
    EnsureInitialized();
    const int tagIndex = FindTagIndex(tagEntity);
    if (tagIndex >= 0 && m_slots[tagIndex].active && IsTagReady(m_slots[tagIndex]))
    {
        m_slots[tagIndex].localTagCompletionEvidence = true;
        SendCollectRequest(tagIndex);
        if (!m_slots[tagIndex].localCollectPending)
        {
            m_slots[tagIndex].localTagCompletionEvidence = false;
        }
    }
    if (tagEntity != nullptr)
    {
        CTagManager::SetAlpha(tagEntity, std::min<uint8_t>(previousAlpha, 254));
    }
    CStats::SetStatValue(STAT_TAGS_SPRAYED, previousTagStat);
    CTagManager::ms_numTagged = previousTaggedCount;
}

void CNetworkPickupManager::RequestLocalSnapshotCapture()
{
    EnsureInitialized();
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (!CNetwork::m_bAuthenticated || localPlayer == nullptr)
    {
        return;
    }
    int bestSlot = INVALID_NETWORK_SLOT;
    float bestDistance = MAX_PICKUP_COLLECT_DISTANCE * MAX_PICKUP_COLLECT_DISTANCE;
    for (int i = 0; i < PICKUP_POOL_CAPACITY; ++i)
    {
        Slot& slot = m_slots[i];
        if (!slot.active || slot.localCollectPending || slot.state.metadata.kind != ePickupKind::SNAPSHOT ||
            !IsPickupEligibleForPed(slot, localPlayer, localPlayer->m_nAreaCode, false))
        {
            continue;
        }
        const float distance = DistanceSquared(TheCamera.GetPosition(), slot.state.metadata.position);
        if (distance <= bestDistance && TheCamera.IsSphereVisible(slot.state.metadata.position, 0.2f))
        {
            bestDistance = distance;
            bestSlot = i;
        }
    }
    if (bestSlot != INVALID_NETWORK_SLOT)
    {
        SendCollectRequest(bestSlot);
    }
}
