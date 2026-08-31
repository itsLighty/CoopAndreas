#include "stdafx.h"

#include "CNetworkFireManager.h"

#include "CLocalPlayer.h"
#include "CNetwork.h"
#include "CNetworkPedManager.h"
#include "CNetworkPlayerManager.h"
#include "CNetworkVehicleManager.h"
#include "CPacketFactory.h"

#include <CFireManager.h>
#include <CTimer.h>
#include <algorithm>
#include <cmath>
#include <cstddef>

using namespace Packets::Fires;

namespace
{
constexpr uint32_t FIRE_HEARTBEAT_MS = 2000;
constexpr uint32_t FIRE_MIN_PUBLISH_INTERVAL_MS = 100;
constexpr uint32_t EXTINGUISH_REQUEST_TIMEOUT_MS = 1000;
constexpr uint8_t MAX_MATERIALIZATIONS_PER_TICK = 4;
constexpr uint8_t MAX_NEW_FIRE_INTENTS_PER_TICK = 8;
constexpr float WORLD_FIRE_REUSE_DISTANCE = 4.0f;

float DistanceSquared(const CVector& left, const CVector& right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}

bool IsNativeIndexValid(int index)
{
    return index >= 0 && index < MAX_NUM_FIRES;
}

int GetLocalArea()
{
    CPlayerPed* localPlayer = FindPlayerPed(0);
    return localPlayer != nullptr ? localPlayer->m_nAreaCode : -1;
}
}  // namespace

static_assert(MAX_NUM_FIRES == CNetworkFireManager::NATIVE_FIRE_CAPACITY,
    "The network fire map must cover the entire native fire pool");

bool CNetworkFireManager::m_initialized = false;
bool CNetworkFireManager::m_localPlayerIsAuthority = false;
int CNetworkFireManager::m_authorityPlayerId = -1;
uint64_t CNetworkFireManager::m_serverRunId = 0;
uint32_t CNetworkFireManager::m_authoritySequence = 0;
uint32_t CNetworkFireManager::m_requestSequence = 0;
uint8_t CNetworkFireManager::m_remoteMutationDepth = 0;
std::array<CNetworkFireManager::Slot, FIRE_SLOT_CAPACITY> CNetworkFireManager::m_slots{};
std::array<int, CNetworkFireManager::NATIVE_FIRE_CAPACITY> CNetworkFireManager::m_nativeOwners{};
std::array<uint32_t, CNetworkFireManager::NATIVE_FIRE_CAPACITY> CNetworkFireManager::m_nativeBirthEpochs{};
std::array<bool, CNetworkFireManager::NATIVE_FIRE_CAPACITY> CNetworkFireManager::m_nativeBirthBaselineActive{};
std::array<uint32_t, FIRE_SLOT_CAPACITY> CNetworkFireManager::m_generations{};
int CNetworkFireManager::m_pendingNativeBirthSlot = INVALID_NATIVE_SLOT;

void CNetworkFireManager::EnsureInitialized()
{
    if (m_initialized)
        return;
    m_nativeOwners.fill(INVALID_NETWORK_SLOT);
    m_initialized = true;
}

uint32_t CNetworkFireManager::NextAuthoritySequence()
{
    ++m_authoritySequence;
    if (m_authoritySequence == 0)
        ++m_authoritySequence;
    return m_authoritySequence;
}

uint32_t CNetworkFireManager::NextRequestId()
{
    ++m_requestSequence;
    if (m_requestSequence == 0)
        ++m_requestSequence;
    return m_requestSequence;
}

uint32_t CNetworkFireManager::NextGeneration(int slotIndex)
{
    ++m_generations[slotIndex];
    if (m_generations[slotIndex] == 0)
        ++m_generations[slotIndex];
    return m_generations[slotIndex];
}

bool CNetworkFireManager::IsApplyingRemoteState()
{
    return m_remoteMutationDepth != 0;
}

void CNetworkFireManager::BeginNativeBirthObservation(CEntity* replacedTarget, bool mayReplaceActive)
{
    EnsureInitialized();
    m_pendingNativeBirthSlot = INVALID_NATIVE_SLOT;
    for (int i = 0; i < MAX_NUM_FIRES; ++i)
    {
        const CFire& fire = gFireManager.m_aFires[i];
        m_nativeBirthBaselineActive[i] = fire.m_nFlags.bActive;
        if (m_pendingNativeBirthSlot == INVALID_NATIVE_SLOT && replacedTarget != nullptr &&
            fire.m_nFlags.bActive && fire.m_pEntityTarget == replacedTarget)
        {
            m_pendingNativeBirthSlot = i;
        }
    }
    if (m_pendingNativeBirthSlot != INVALID_NATIVE_SLOT)
        return;
    for (int i = 0; i < MAX_NUM_FIRES; ++i)
    {
        const CFire& fire = gFireManager.m_aFires[i];
        if (!fire.m_nFlags.bActive && !fire.m_nFlags.bCreatedByScript)
        {
            m_pendingNativeBirthSlot = i;
            return;
        }
    }
    if (!mayReplaceActive)
        return;
    for (int i = 0; i < MAX_NUM_FIRES; ++i)
    {
        const CFire& fire = gFireManager.m_aFires[i];
        if (fire.m_nFlags.bFirstGeneration || fire.m_nFlags.bCreatedByScript)
        {
            m_pendingNativeBirthSlot = i;
            return;
        }
    }
}

void CNetworkFireManager::EndNativeBirthObservation()
{
    EnsureInitialized();
    for (int i = 0; i < MAX_NUM_FIRES; ++i)
    {
        const bool becameActive = !m_nativeBirthBaselineActive[i] && gFireManager.m_aFires[i].m_nFlags.bActive;
        const bool replacedWhileActive = i == m_pendingNativeBirthSlot && m_nativeBirthBaselineActive[i] &&
                                         gFireManager.m_aFires[i].m_nFlags.bActive;
        if (!becameActive && !replacedWhileActive)
            continue;
        ++m_nativeBirthEpochs[i];
        if (m_nativeBirthEpochs[i] == 0)
            ++m_nativeBirthEpochs[i];
    }
    m_pendingNativeBirthSlot = INVALID_NATIVE_SLOT;
}

void CNetworkFireManager::RemoveNative(Slot& slot, bool extinguish)
{
    const int nativeSlot = slot.nativeSlot;
    if (!IsNativeIndexValid(nativeSlot) || m_nativeOwners[nativeSlot] != slot.id.slot)
    {
        slot.nativeSlot = INVALID_NATIVE_SLOT;
        slot.materializedByNetwork = false;
        slot.materializedAttached = false;
        return;
    }
    CFire& fire = gFireManager.m_aFires[nativeSlot];
    if (extinguish && fire.m_nFlags.bActive)
    {
        ++m_remoteMutationDepth;
        fire.Extinguish();
        --m_remoteMutationDepth;
    }
    m_nativeOwners[nativeSlot] = INVALID_NETWORK_SLOT;
    slot.nativeSlot = INVALID_NATIVE_SLOT;
    slot.materializedByNetwork = false;
    slot.materializedAttached = false;
}

void CNetworkFireManager::RecordNativeIdentity(Slot& slot, const CFire& fire)
{
    slot.nativeScriptReferenceToken = fire.m_nScriptReferenceIndex;
    slot.nativeDeadlineToken = fire.m_nTimeToBurn;
    slot.nativeFxIdentityToken = reinterpret_cast<uintptr_t>(fire.m_pFxSystem);
    slot.nativeFirstGenerationToken = fire.m_nFlags.bFirstGeneration;
    slot.nativeCreatedByScriptToken = fire.m_nFlags.bCreatedByScript;
    slot.nativeBirthEpochToken = IsNativeIndexValid(slot.nativeSlot)
        ? m_nativeBirthEpochs[slot.nativeSlot]
        : 0;
    slot.nativeLastPosition = fire.m_vecPosition;
}

bool CNetworkFireManager::NativeIdentityMatches(const Slot& slot, const CFire& fire)
{
    const uintptr_t currentFxIdentity = reinterpret_cast<uintptr_t>(fire.m_pFxSystem);
    if (!IsNativeIndexValid(slot.nativeSlot) ||
        slot.nativeBirthEpochToken != m_nativeBirthEpochs[slot.nativeSlot] ||
        slot.nativeScriptReferenceToken != fire.m_nScriptReferenceIndex ||
        slot.nativeFirstGenerationToken != fire.m_nFlags.bFirstGeneration ||
        slot.nativeCreatedByScriptToken != fire.m_nFlags.bCreatedByScript)
    {
        return false;
    }
    if (fire.m_nTimeToBurn < slot.nativeDeadlineToken && !fire.m_nFlags.bBeingExtinguished)
        return false;
    if (slot.nativeFxIdentityToken != currentFxIdentity)
    {
        // GTA legitimately rebuilds the FX system when water crosses a strength boundary, when a fire grows,
        // or when nearby fires merge. A replacement fire, by contrast, is freshly reset to strength 1.
        const bool strengthTransition =
            std::fabs(fire.m_fStrength - slot.descriptor.strength) > 0.01f;
        const bool looksLikeFreshReplacement = !fire.m_nFlags.bBeingExtinguished &&
            fire.m_fStrength <= 1.01f && slot.descriptor.strength > 1.01f;
        if ((!fire.m_nFlags.bBeingExtinguished && !strengthTransition) || looksLikeFreshReplacement)
            return false;
    }
    // A world fire is stationary. A large discontinuity with no observed inactive frame means GTA reused the
    // native pool slot between our observations; it must not inherit the previous network generation.
    return slot.descriptor.attachmentType != eFireAttachmentType::WORLD ||
           DistanceSquared(slot.nativeLastPosition, fire.m_vecPosition) <=
               WORLD_FIRE_REUSE_DISTANCE * WORLD_FIRE_REUSE_DISTANCE;
}

void CNetworkFireManager::ResetNetworkState()
{
    EnsureInitialized();
    for (Slot& slot : m_slots)
    {
        // Fires that originated in the authority's native simulation resume ordinary offline behaviour.
        // Only presentations created by network replay are removed on disconnect.
        if (slot.materializedByNetwork && !slot.originatedLocally)
            RemoveNative(slot, true);
        else if (IsNativeIndexValid(slot.nativeSlot) && m_nativeOwners[slot.nativeSlot] == slot.id.slot)
            m_nativeOwners[slot.nativeSlot] = INVALID_NETWORK_SLOT;
        slot = {};
    }
    m_nativeOwners.fill(INVALID_NETWORK_SLOT);
    m_localPlayerIsAuthority = false;
    m_authorityPlayerId = -1;
    m_serverRunId = 0;
    m_authoritySequence = 0;
    m_requestSequence = 0;
    m_remoteMutationDepth = 0;
}

void CNetworkFireManager::HandleAuthorityChanged(int authorityPlayerId, bool localPlayerIsAuthority)
{
    EnsureInitialized();
    m_authorityPlayerId = authorityPlayerId;
    m_localPlayerIsAuthority = localPlayerIsAuthority;
    m_authoritySequence = 0;
    for (Slot& slot : m_slots)
    {
        slot.pendingExtinguishRequest = false;
        slot.pendingExtinguishAt = 0;
        slot.lastPublishedAt = 0;
        if (localPlayerIsAuthority && slot.active && IsNativeIndexValid(slot.nativeSlot))
        {
            CFire& fire = gFireManager.m_aFires[slot.nativeSlot];
            if (fire.m_nFlags.bActive)
            {
                fire.m_nFlags.bCreatedByScript = slot.descriptor.createdByScript;
                fire.m_nNumGenerationsAllowed = slot.descriptor.generationsAllowed;
                RecordNativeIdentity(slot, fire);
            }
        }
    }
}

void CNetworkFireManager::HandleState(const FireStateEvent& state)
{
    EnsureInitialized();
    if (!state.HasValidPayload() || !state.FitsSerializedBudget())
        return;
    if (m_serverRunId != 0 && m_serverRunId != state.serverRunId)
        ResetNetworkState();
    m_serverRunId = state.serverRunId;
    m_authorityPlayerId = state.authorityPlayerId;

    Slot& slot = m_slots[state.id.slot];
    if (slot.initialized && slot.revision != 0 && !IsFireSerialNewer(state.revision, slot.revision))
        return;
    if (slot.initialized && slot.id != state.id)
    {
        // A host can retire an old native occupant and publish a replacement before the old tombstone echoes
        // back. Never let that older generation tear down the provisional replacement.
        if (slot.id.slot == state.id.slot &&
            IsFireSerialNewer(slot.id.generation, state.id.generation))
        {
            return;
        }
        RemoveNative(slot, true);
        slot = {};
    }

    const FireDescriptor previousDescriptor = slot.descriptor;
    const bool hadMaterialization = IsNativeIndexValid(slot.nativeSlot);
    slot.initialized = true;
    slot.id = state.id;
    slot.revision = state.revision;
    slot.descriptor = state.descriptor;
    slot.pendingExtinguishRequest = false;
    slot.pendingExtinguishAt = 0;
    m_generations[state.id.slot] = state.id.generation;

    if (!state.active)
    {
        RemoveNative(slot, true);
        slot.active = false;
        return;
    }
    slot.active = true;
    if (hadMaterialization &&
        (previousDescriptor.attachmentType != slot.descriptor.attachmentType ||
            previousDescriptor.attachmentId != slot.descriptor.attachmentId ||
            previousDescriptor.area != slot.descriptor.area))
    {
        // Native attachment ownership cannot be retargeted in place. Recreate from the canonical descriptor
        // so entity A never keeps a fire that the server has moved to entity B or another dimension.
        RemoveNative(slot, true);
    }
    if (!m_localPlayerIsAuthority && IsNativeIndexValid(slot.nativeSlot) &&
        m_nativeOwners[slot.nativeSlot] == slot.id.slot)
    {
        CFire& fire = gFireManager.m_aFires[slot.nativeSlot];
        if (fire.m_nFlags.bActive)
        {
            fire.m_nTimeToBurn = CTimer::m_snTimeInMilliseconds + slot.descriptor.remainingLifetimeMs;
            RecordNativeIdentity(slot, fire);
        }
    }
}

CEntity* CNetworkFireManager::ResolveAttachment(const FireDescriptor& descriptor)
{
    switch (descriptor.attachmentType)
    {
    case eFireAttachmentType::PLAYER:
        if (descriptor.attachmentId == CNetworkPlayerManager::m_nMyId)
            return FindPlayerPed(0);
        if (CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(descriptor.attachmentId))
            return player->m_pPed;
        break;
    case eFireAttachmentType::PED:
        if (CNetworkPed* ped = CNetworkPedManager::GetPed(descriptor.attachmentId))
            return ped->m_pPed;
        break;
    case eFireAttachmentType::VEHICLE:
        if (CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(descriptor.attachmentId))
            return vehicle->m_pVehicle;
        break;
    default:
        break;
    }
    return nullptr;
}

bool CNetworkFireManager::Materialize(Slot& slot)
{
    if (!slot.active || IsNativeIndexValid(slot.nativeSlot))
        return false;
    if (!gFireManager.PlentyFiresAvailable())
        return false;
    if (GetLocalArea() != slot.descriptor.area)
        return false;
    CEntity* target = ResolveAttachment(slot.descriptor);
    const auto birthEpochsBefore = m_nativeBirthEpochs;
    CFire* fire = nullptr;
    ++m_remoteMutationDepth;
    if (target != nullptr)
    {
        fire = gFireManager.StartFire(target, nullptr, slot.descriptor.strength, 1,
            slot.descriptor.remainingLifetimeMs, 0);
    }
    else
    {
        fire = gFireManager.StartFire(slot.descriptor.fallbackPosition, slot.descriptor.strength, 1, nullptr,
            slot.descriptor.remainingLifetimeMs, 0, 0);
    }
    --m_remoteMutationDepth;
    if (fire == nullptr)
    {
        // The retail attached-fire routine can create a fire without returning its address. The birth hooks
        // still identify the exact newly occupied native slot, so adoption never relies on a stale pointer.
        for (int i = 0; i < MAX_NUM_FIRES; ++i)
        {
            if (birthEpochsBefore[i] != m_nativeBirthEpochs[i] &&
                gFireManager.m_aFires[i].m_nFlags.bActive &&
                m_nativeOwners[i] == INVALID_NETWORK_SLOT)
            {
                fire = &gFireManager.m_aFires[i];
                break;
            }
        }
    }
    if (fire == nullptr)
        return false;
    const ptrdiff_t nativeIndex = fire - &gFireManager.m_aFires[0];
    if (nativeIndex < 0 || nativeIndex >= MAX_NUM_FIRES)
    {
        ++m_remoteMutationDepth;
        fire->Extinguish();
        --m_remoteMutationDepth;
        return false;
    }
    if (m_nativeOwners[nativeIndex] != INVALID_NETWORK_SLOT)
    {
        ++m_remoteMutationDepth;
        fire->Extinguish();
        --m_remoteMutationDepth;
        return false;
    }
    slot.nativeSlot = static_cast<int>(nativeIndex);
    slot.materializedByNetwork = true;
    slot.materializedAttached = target != nullptr;
    m_nativeOwners[nativeIndex] = slot.id.slot;
    // Followers use the script-fire damage policy for presentation copies: GTA still renders and lets water
    // extinguish them, but CFire::ProcessFire will not independently damage a replicated vehicle on every peer.
    fire->m_nFlags.bCreatedByScript = m_localPlayerIsAuthority
        ? slot.descriptor.createdByScript
        : true;
    fire->m_nFlags.bMakesNoise = slot.descriptor.makesNoise;
    fire->m_fStrength = slot.descriptor.strength;
    fire->m_nNumGenerationsAllowed = m_localPlayerIsAuthority ? slot.descriptor.generationsAllowed : 0;
    fire->m_nRemovalDist = slot.descriptor.removalDistance;
    if (slot.originatedLocally && slot.scriptReferenceIndex >= 0)
        fire->m_nScriptReferenceIndex = slot.scriptReferenceIndex;
    RecordNativeIdentity(slot, *fire);
    return true;
}

void CNetworkFireManager::ProcessMaterializations()
{
    uint8_t materialized = 0;
    for (Slot& slot : m_slots)
    {
        if (!slot.active || IsNativeIndexValid(slot.nativeSlot) || slot.pendingExtinguishRequest ||
            GetLocalArea() != slot.descriptor.area)
            continue;
        if (Materialize(slot) && ++materialized >= MAX_MATERIALIZATIONS_PER_TICK)
            break;
    }
}

void CNetworkFireManager::ProcessAreaTransitions()
{
    EnsureInitialized();
    if (!CNetwork::m_bAuthenticated)
        return;
    const int localArea = GetLocalArea();
    for (Slot& slot : m_slots)
    {
        if (slot.active && IsNativeIndexValid(slot.nativeSlot) && localArea != slot.descriptor.area)
            RemoveNative(slot, true);
    }
}

FireDescriptor CNetworkFireManager::CaptureDescriptor(const CFire& fire)
{
    FireDescriptor descriptor{};
    descriptor.fallbackPosition = fire.m_vecPosition;
    if (CPlayerPed* localPlayer = FindPlayerPed(0))
        descriptor.area = localPlayer->m_nAreaCode;
    descriptor.strength = std::clamp(fire.m_fStrength, 0.0f, FIRE_MAX_STRENGTH);
    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    const uint32_t nativeRemaining = fire.m_nTimeToBurn - now;
    descriptor.remainingLifetimeMs = fire.m_nTimeToBurn > now
        ? std::clamp(nativeRemaining, FIRE_MIN_LIFETIME_MS, FIRE_MAX_LIFETIME_MS)
        : FIRE_MAX_LIFETIME_MS;
    descriptor.generationsAllowed = static_cast<int8_t>(
        std::clamp<int>(fire.m_nNumGenerationsAllowed, 0, FIRE_MAX_GENERATIONS));
    descriptor.removalDistance = fire.m_nRemovalDist;
    descriptor.createdByScript = fire.m_nFlags.bCreatedByScript;
    descriptor.makesNoise = fire.m_nFlags.bMakesNoise;

    CEntity* target = fire.m_pEntityTarget; // Compared by identity only; it is never dereferenced.
    if (target == nullptr)
        return descriptor;
    if (target == FindPlayerPed(0))
    {
        descriptor.attachmentType = eFireAttachmentType::PLAYER;
        descriptor.attachmentId = static_cast<uint16_t>(CNetworkPlayerManager::m_nMyId);
        descriptor.fallbackPosition = FindPlayerPed(0)->GetPosition();
        descriptor.area = FindPlayerPed(0)->m_nAreaCode;
    }
    else if (CNetworkPlayer* player = CNetworkPlayerManager::GetPlayer(target))
    {
        descriptor.attachmentType = eFireAttachmentType::PLAYER;
        descriptor.attachmentId = static_cast<uint16_t>(player->m_iPlayerId);
        descriptor.fallbackPosition = player->GetLogicalPosition();
        descriptor.area = player->m_nLogicalArea;
    }
    else if (CNetworkPed* ped = CNetworkPedManager::GetPed(target))
    {
        descriptor.attachmentType = eFireAttachmentType::PED;
        descriptor.attachmentId = static_cast<uint16_t>(ped->m_nPedId);
        descriptor.fallbackPosition = ped->GetLogicalPosition();
        descriptor.area = ped->m_nLogicalArea;
    }
    else if (CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(target))
    {
        descriptor.attachmentType = eFireAttachmentType::VEHICLE;
        descriptor.attachmentId = static_cast<uint16_t>(vehicle->m_nVehicleId);
        descriptor.fallbackPosition = vehicle->GetLogicalPosition();
        descriptor.area = vehicle->m_nLogicalArea;
    }
    return descriptor;
}

bool CNetworkFireManager::DescriptorNeedsPublish(
    const FireDescriptor& current, const FireDescriptor& previous)
{
    return current.attachmentType != previous.attachmentType || current.attachmentId != previous.attachmentId ||
           current.area != previous.area ||
           DistanceSquared(current.fallbackPosition, previous.fallbackPosition) > 0.25f ||
           std::fabs(current.strength - previous.strength) > 0.05f ||
           current.generationsAllowed != previous.generationsAllowed ||
           current.removalDistance != previous.removalDistance ||
           current.createdByScript != previous.createdByScript || current.makesNoise != previous.makesNoise;
}

void CNetworkFireManager::SendIntent(
    Slot& slot, eFireMutation mutation, const FireDescriptor& descriptor)
{
    if (!m_localPlayerIsAuthority || !CNetwork::m_bAuthenticated || !descriptor.HasValidSemantics())
        return;
    FireStateIntent intent{};
    intent.authoritySequence = NextAuthoritySequence();
    intent.mutation = mutation;
    intent.id = slot.id;
    intent.descriptor = descriptor;
    GetPacketFactory().Send(intent);
    slot.lastPublishedAt = GetTickCount();
    slot.lastPublishedDescriptor = descriptor;
}

void CNetworkFireManager::SendExtinguishRequest(Slot& slot)
{
    if (m_localPlayerIsAuthority || slot.pendingExtinguishRequest || !CNetwork::m_bAuthenticated)
        return;
    CPlayerPed* localPlayer = FindPlayerPed(0);
    if (localPlayer == nullptr)
        return;
    FireExtinguishRequest request{};
    request.requestId = NextRequestId();
    request.id = slot.id;
    request.requesterPosition = localPlayer->GetPosition();
    GetPacketFactory().Send(request);
    slot.pendingExtinguishRequest = true;
    slot.pendingExtinguishAt = GetTickCount();
}

int CNetworkFireManager::FindFreeNetworkSlot()
{
    for (int i = 0; i < FIRE_SLOT_CAPACITY; ++i)
    {
        if (!m_slots[i].active && !IsNativeIndexValid(m_slots[i].nativeSlot))
            return i;
    }
    return INVALID_NETWORK_SLOT;
}

void CNetworkFireManager::ObserveManagedSlot(int slotIndex, uint32_t now)
{
    Slot& slot = m_slots[slotIndex];
    if (!slot.active || !IsNativeIndexValid(slot.nativeSlot) ||
        m_nativeOwners[slot.nativeSlot] != slotIndex)
    {
        return;
    }
    CFire& fire = gFireManager.m_aFires[slot.nativeSlot];
    if (GetLocalArea() != slot.descriptor.area)
    {
        RemoveNative(slot, true);
        return;
    }
    if (!fire.m_nFlags.bActive)
    {
        m_nativeOwners[slot.nativeSlot] = INVALID_NETWORK_SLOT;
        slot.nativeSlot = INVALID_NATIVE_SLOT;
        slot.materializedByNetwork = false;
        slot.materializedAttached = false;
        if (m_localPlayerIsAuthority)
        {
            SendIntent(slot, eFireMutation::EXTINGUISH, slot.descriptor);
            slot.forceExtinguishPublish = false;
        }
        else
        {
            SendExtinguishRequest(slot);
        }
        return;
    }
    if (!NativeIdentityMatches(slot, fire))
    {
        m_nativeOwners[slot.nativeSlot] = INVALID_NETWORK_SLOT;
        slot.nativeSlot = INVALID_NATIVE_SLOT;
        slot.materializedByNetwork = false;
        slot.materializedAttached = false;
        if (m_localPlayerIsAuthority)
        {
            const FireDescriptor retiredDescriptor = slot.descriptor;
            SendIntent(slot, eFireMutation::EXTINGUISH, retiredDescriptor);
            slot.active = false;
        }
        return;
    }

    if (!m_localPlayerIsAuthority)
    {
        fire.m_nNumGenerationsAllowed = 0; // Followers render one canonical fire and never create spread children.
        fire.m_nFlags.bCreatedByScript = true; // Suppress duplicate follower-side vehicle damage loops.
        if (fire.m_nFlags.bBeingExtinguished || fire.m_fStrength + 0.05f < slot.descriptor.strength)
            SendExtinguishRequest(slot);
        if (slot.materializedByNetwork)
        {
            const bool attachmentAvailable = ResolveAttachment(slot.descriptor) != nullptr;
            if (attachmentAvailable != slot.materializedAttached)
            {
                RemoveNative(slot, true);
                return;
            }
            fire.m_vecPosition = slot.descriptor.fallbackPosition;
            fire.m_fStrength = slot.descriptor.strength;
            fire.m_nRemovalDist = slot.descriptor.removalDistance;
            fire.m_nFlags.bMakesNoise = slot.descriptor.makesNoise;
        }
        RecordNativeIdentity(slot, fire);
        return;
    }

    FireDescriptor current = CaptureDescriptor(fire);
    if (!current.HasValidSemantics())
        return;
    slot.descriptor = current;
    if (slot.forceExtinguishPublish)
    {
        ++m_remoteMutationDepth;
        fire.Extinguish();
        --m_remoteMutationDepth;
        slot.forceExtinguishPublish = false;
        SendIntent(slot, eFireMutation::EXTINGUISH, current);
        return;
    }
    slot.nativeLastPosition = fire.m_vecPosition;
    if (slot.lastPublishedAt == 0 ||
        (now - slot.lastPublishedAt >= FIRE_MIN_PUBLISH_INTERVAL_MS &&
            DescriptorNeedsPublish(current, slot.lastPublishedDescriptor)) ||
        now - slot.lastPublishedAt >= FIRE_HEARTBEAT_MS)
    {
        SendIntent(slot, eFireMutation::UPSERT, current);
    }
}

void CNetworkFireManager::ObserveNativePool()
{
    const uint32_t now = GetTickCount();
    for (int slotIndex = 0; slotIndex < FIRE_SLOT_CAPACITY; ++slotIndex)
        ObserveManagedSlot(slotIndex, now);

    uint8_t newIntents = 0;
    for (int nativeIndex = 0; nativeIndex < MAX_NUM_FIRES; ++nativeIndex)
    {
        if (m_nativeOwners[nativeIndex] != INVALID_NETWORK_SLOT)
            continue;
        CFire& fire = gFireManager.m_aFires[nativeIndex];
        if (!fire.m_nFlags.bActive)
            continue;
        if (!m_localPlayerIsAuthority)
        {
            ++m_remoteMutationDepth;
            fire.Extinguish();
            --m_remoteMutationDepth;
            continue;
        }
        const int slotIndex = FindFreeNetworkSlot();
        if (slotIndex == INVALID_NETWORK_SLOT)
            break;
        FireDescriptor descriptor = CaptureDescriptor(fire);
        if (!descriptor.HasValidSemantics())
            continue;
        Slot& slot = m_slots[slotIndex];
        slot = {};
        slot.initialized = true;
        slot.active = true;
        slot.originatedLocally = true;
        slot.nativeSlot = nativeIndex;
        slot.materializedAttached = descriptor.attachmentType != eFireAttachmentType::WORLD;
        slot.scriptReferenceIndex = fire.m_nScriptReferenceIndex;
        slot.id.slot = static_cast<uint8_t>(slotIndex);
        slot.id.generation = NextGeneration(slotIndex);
        slot.descriptor = descriptor;
        m_nativeOwners[nativeIndex] = slotIndex;
        RecordNativeIdentity(slot, fire);
        SendIntent(slot, eFireMutation::UPSERT, descriptor);
        if (++newIntents >= MAX_NEW_FIRE_INTENTS_PER_TICK)
            break;
    }
}

void CNetworkFireManager::Process()
{
    EnsureInitialized();
    if (!CNetwork::m_bAuthenticated)
        return; // Offline GTA fire creation, scripts, spread, damage and extinction remain untouched.

    for (Slot& slot : m_slots)
    {
        if (slot.pendingExtinguishRequest &&
            GetTickCount() - slot.pendingExtinguishAt >= EXTINGUISH_REQUEST_TIMEOUT_MS)
        {
            slot.pendingExtinguishRequest = false;
            slot.pendingExtinguishAt = 0;
        }
    }
    ProcessMaterializations();
    ObserveNativePool();
}

void CNetworkFireManager::HandleExtinguishRequest(const FireExtinguishRequest& request)
{
    EnsureInitialized();
    if (!m_localPlayerIsAuthority || !request.HasValidPayload() ||
        request.requesterPlayerId.value == CNetworkPlayerManager::m_nMyId)
    {
        return;
    }
    Slot& slot = m_slots[request.id.slot];
    if (!slot.active || slot.id != request.id)
        return;
    CNetworkPlayer* requester = CNetworkPlayerManager::GetPlayer(request.requesterPlayerId.value);
    if (requester == nullptr ||
        requester->m_nLogicalArea != slot.descriptor.area ||
        DistanceSquared(requester->GetLogicalPosition(), slot.descriptor.fallbackPosition) >
            FIRE_MAX_EXTINGUISH_DISTANCE * FIRE_MAX_EXTINGUISH_DISTANCE)
    {
        return;
    }
    if (IsNativeIndexValid(slot.nativeSlot) && gFireManager.m_aFires[slot.nativeSlot].m_nFlags.bActive)
    {
        slot.forceExtinguishPublish = true;
    }
}
