#pragma once

#include "network/packets/pickups.h"

#include <array>
#include <cstdint>

class CEntity;
class CPed;
class CPickup;

class CNetworkPickupManager
{
public:
    static void ProcessBeforeNativeUpdate();
    static bool IsManagedNativeSlot(int nativeSlot);
    static bool CanRenderNativeSlot(int nativeSlot);

    static void HandleState(const Packets::Pickups::PickupStateEvent& packet);
    static void HandleCollectRequest(const Packets::Pickups::PickupCollectRequest& packet);
    static void HandleCollectResult(const Packets::Pickups::PickupCollectResult& packet);
    static void HandleCreateIntent(const Packets::Pickups::PickupCreateIntent& packet);
    static void HandleAuthorityChanged(uint8_t authorityPlayerId, bool localPlayerIsAuthority);
    static void ResetNetworkState();

    static void NotifyLocalTagSprayed(CEntity* tagEntity, float previousTagStat,
        int32_t previousTaggedCount, uint8_t previousAlpha);
    static void RequestLocalSnapshotCapture();
    static void SuppressSyntheticJetpackDrop(const CVector& position);

private:
    static constexpr int TAG_NETWORK_SLOT_COUNT = 100;
    static constexpr int INVALID_NATIVE_SLOT = -1;
    static constexpr int INVALID_NETWORK_SLOT = -1;
    static constexpr int PROVISIONAL_NETWORK_SLOT = -2;
    static constexpr uint32_t LOCAL_REQUEST_TIMEOUT_MS = 12000;
    static constexpr uint32_t DENIED_RETRY_COOLDOWN_MS = 1000;
    static constexpr uint32_t MODEL_REQUEST_RETRY_MS = 250;
    static constexpr uint32_t SYNTHETIC_DROP_SUPPRESSION_MS = 2000;
    static constexpr uint8_t MAX_MODEL_REQUESTS_PER_TICK = 4;
    static constexpr uint8_t MAX_MATERIALIZATIONS_PER_TICK = 4;

    struct Slot
    {
        bool initialized = false;
        bool active = false;
        bool localCollectPending = false;
        bool localTagCompletionEvidence = false;
        bool ownsNativePickup = false;
        bool materialized = false;
        int nativeSlot = INVALID_NATIVE_SLOT;
        uint16_t lastGrantedGeneration = 0;
        uint32_t localCollectStartedAt = 0;
        uint32_t respawnAt = 0;
        uint32_t retryAfter = 0;
        uint32_t modelRequestedAt = 0;
        Packets::Pickups::PickupState state{};
    };

    struct ProvisionalNativePickup
    {
        bool active = false;
        bool creationIntentSent = false;
        Packets::Pickups::PickupMetadata metadata{};
    };

    struct SuppressedJetpackDrop
    {
        bool active = false;
        CVector position{};
        uint32_t expiresAt = 0;
    };

    static std::array<Slot, Packets::Pickups::PICKUP_POOL_CAPACITY> m_slots;
    static std::array<int16_t, Packets::Pickups::PICKUP_POOL_CAPACITY> m_nativeToNetworkSlot;
    static std::array<ProvisionalNativePickup, Packets::Pickups::PICKUP_POOL_CAPACITY>
        m_provisionalNativePickups;
    static std::array<SuppressedJetpackDrop, Config::MAX_SERVER_PLAYERS> m_suppressedJetpackDrops;
    static bool m_initialized;
    static bool m_localPlayerIsAuthority;
    static uint8_t m_authorityPlayerId;

    static void EnsureInitialized();
    static void ObserveTags();
    static void ObserveNativePool();
    static void ProcessLocalCollection();
    static void ProcessModelRequests();
    static void ProcessMaterializations();

    static bool TryBuildMetadata(int nativeSlot, const CPickup& pickup,
        Packets::Pickups::PickupMetadata& metadata);
    static bool MetadataMatches(const Packets::Pickups::PickupMetadata& left,
        const Packets::Pickups::PickupMetadata& right);
    static bool IsSyntheticJetpackDrop(const Packets::Pickups::PickupMetadata& metadata);
    static bool IsPickupEligibleForPed(const Slot& slot, CPed* ped, uint8_t interior,
        bool requireNormalCollectDistance, bool requireLocalTagEvidence = true);
    static bool IsTagReady(const Slot& slot);

    static int FindFreeNetworkSlot();
    static int FindMatchingProvisionalNativeSlot(const Packets::Pickups::PickupMetadata& metadata);
    static int FindTagIndex(CEntity* tagEntity);
    static int FindNetworkSlot(const Packets::Pickups::PickupId& id);
    static uint16_t NextGeneration(uint16_t generation);
    static uint32_t NextRevision(uint32_t revision);

    static bool PublishNewState(int networkSlot, int nativeSlot,
        const Packets::Pickups::PickupMetadata& metadata, uint8_t creatorPlayerId,
        uint32_t sourceIntentRequestId);
    static void PublishRemoval(int networkSlot);
    static void ApplyActiveState(int networkSlot, const Packets::Pickups::PickupState& state);
    static void RemoveNativePickup(Slot& slot, bool notifyScripts, bool preserveNativeHandle = false);
    static bool MaterializeNativePickup(Slot& slot);
    static bool BindNativePickup(Slot& slot, int nativeSlot);
    static void ReleaseModelIfUnused(int16_t modelId, int exceptNetworkSlot = INVALID_NETWORK_SLOT);
    static void SendCollectRequest(int networkSlot);
    static void GrantApprovedPickup(const Packets::Pickups::PickupState& state);
    static void CompleteTagVisual(const Packets::Pickups::PickupState& state, bool rewardCollector);
};
