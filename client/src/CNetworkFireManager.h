#pragma once

#include "network/packets/fires.h"

#include <array>
#include <cstdint>

class CEntity;
class CFire;

class CNetworkFireManager
{
public:
    static constexpr int NATIVE_FIRE_CAPACITY = 60;
    static void Process();
    static void ResetNetworkState();
    static void HandleAuthorityChanged(int authorityPlayerId, bool localPlayerIsAuthority);
    static void HandleState(const Packets::Fires::FireStateEvent& state);
    static void HandleExtinguishRequest(const Packets::Fires::FireExtinguishRequest& request);
    static void ProcessAreaTransitions();
    static void BeginNativeBirthObservation(CEntity* replacedTarget, bool mayReplaceActive);
    static void EndNativeBirthObservation();
    static bool IsApplyingRemoteState();

private:
    static constexpr int INVALID_NATIVE_SLOT = -1;
    static constexpr int INVALID_NETWORK_SLOT = -1;

    struct Slot
    {
        bool initialized = false;
        bool active = false;
        bool materializedByNetwork = false;
        bool materializedAttached = false;
        bool pendingExtinguishRequest = false;
        bool forceExtinguishPublish = false;
        bool originatedLocally = false;
        bool localOriginalCreatedByScript = false;
        int nativeSlot = INVALID_NATIVE_SLOT;
        int8_t localOriginalGenerationsAllowed = 0;
        int16_t scriptReferenceIndex = -1;
        int16_t nativeScriptReferenceToken = -1;
        uint32_t nativeDeadlineToken = 0;
        uintptr_t nativeFxIdentityToken = 0;
        uint8_t nativeFirstGenerationToken = 0;
        uint8_t nativeCreatedByScriptToken = 0;
        uint32_t nativeBirthEpochToken = 0;
        CVector nativeLastPosition{};
        uint32_t revision = 0;
        uint32_t lastPublishedAt = 0;
        uint32_t pendingExtinguishAt = 0;
        Packets::Fires::FireId id{};
        Packets::Fires::FireDescriptor descriptor{};
        Packets::Fires::FireDescriptor lastPublishedDescriptor{};
    };

    struct PendingBirth
    {
        bool active = false;
        bool scriptCandidate = false;
        bool timedOut = false;
        uint8_t attempts = 0;
        int8_t originalGenerationsAllowed = 0;
        int16_t scriptReferenceIndex = -1;
        uint32_t requestId = 0;
        uint32_t firstSeenAt = 0;
        uint32_t lastSentAt = 0;
        uint32_t nativeBirthEpochToken = 0;
        Packets::Fires::FireDescriptor descriptor{};
    };

    static void EnsureInitialized();
    static void ProcessMaterializations();
    static void ObserveNativePool();
    static void ObserveManagedSlot(int slotIndex, uint32_t now);
    static bool Materialize(Slot& slot);
    static bool PendingMatchesDescriptor(const PendingBirth& pending,
        const Packets::Fires::FireDescriptor& descriptor);
    static bool TryAdoptPendingBirth(Slot& slot, const Packets::Fires::FireDescriptor& descriptor);
    static void ClearPendingBirth(int nativeSlot, bool restoreNativeState);
    static void SendPendingBirthIntent(int nativeSlot, PendingBirth& pending);
    static void RemoveNative(Slot& slot, bool extinguish);
    static void RecordNativeIdentity(Slot& slot, const CFire& fire);
    static bool NativeIdentityMatches(const Slot& slot, const CFire& fire);
    static void SendIntent(Slot& slot, Packets::Fires::eFireMutation mutation,
        const Packets::Fires::FireDescriptor& descriptor);
    static void SendExtinguishRequest(Slot& slot);
    static Packets::Fires::FireDescriptor CaptureDescriptor(const CFire& fire);
    static CEntity* ResolveAttachment(const Packets::Fires::FireDescriptor& descriptor);
    static bool DescriptorNeedsPublish(const Packets::Fires::FireDescriptor& current,
        const Packets::Fires::FireDescriptor& previous);
    static int FindFreeNetworkSlot();
    static uint32_t NextGeneration(int slotIndex);
    static uint32_t NextAuthoritySequence();
    static uint32_t NextRequestId();

    static bool m_initialized;
    static bool m_localPlayerIsAuthority;
    static int m_authorityPlayerId;
    static uint64_t m_serverRunId;
    static uint32_t m_authoritySequence;
    static uint32_t m_requestSequence;
    static uint8_t m_remoteMutationDepth;
    static std::array<Slot, Packets::Fires::FIRE_SLOT_CAPACITY> m_slots;
    static std::array<int, NATIVE_FIRE_CAPACITY> m_nativeOwners;
    static std::array<uint32_t, NATIVE_FIRE_CAPACITY> m_nativeBirthEpochs;
    static std::array<int8_t, NATIVE_FIRE_CAPACITY> m_nativeBirthOriginalGenerations;
    static std::array<bool, NATIVE_FIRE_CAPACITY> m_nativeBirthBaselineActive;
    static std::array<PendingBirth, NATIVE_FIRE_CAPACITY> m_pendingBirths;
    static std::array<uint32_t, Packets::Fires::FIRE_SLOT_CAPACITY> m_generations;
    static int m_pendingNativeBirthSlot;
};
