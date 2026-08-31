#pragma once

#include "network/packets/pickups.h"

#include <array>
#include <cstdint>

class CPickup;

class CNetworkPickupManager
{
public:
    static void PrepareForNativePickupUpdate();
    static void Process();
    static void HandleSpawn(const Packets::Pickups::PickupSpawn& packet);
    static void HandleState(const Packets::Pickups::PickupState& packet);
    static void HandleRemove(const Packets::Pickups::PickupRemove& packet);
    static void HandleCollectForward(const Packets::Pickups::PickupCollectForward& packet);
    static void HandleCollectResult(const Packets::Pickups::PickupCollectResult& packet);
    static void HandleSnapshotChunk(const Packets::Pickups::PickupSnapshotChunk& packet);
    static void HandleAuthorityChanged(uint8_t authorityPlayerId, bool localIsAuthority);
    static void ResetNetworkState();

    static uint32_t GetAuthorityEpoch() { return ms_authorityEpoch; }

private:
    struct LocalPickup
    {
        bool tracked = false;
        bool bound = false;
        bool networkGenerated = false;
        bool awaitingAuthority = false;
        bool collectionObserved = false;
        int16_t referenceIndex = 0;
        int nativeHandle = -1;
        uint32_t lastRequestAt = 0;
        uint32_t pendingRequestNonce = 0;
        uint32_t awaitingAuthoritySince = 0;
        Packets::Pickups::PickupDescriptor pickup{};
        Packets::Pickups::PickupDescriptor preAuthorityPickup{};
    };

    static bool CaptureNativePickup(uint16_t slot, const Packets::Pickups::PickupIdentity& identity,
        uint32_t revision, Packets::Pickups::PickupDescriptor& outPickup);
    static bool DescriptorsMatch(const Packets::Pickups::PickupDescriptor& left,
        const Packets::Pickups::PickupDescriptor& right);
    static bool StateChanged(const Packets::Pickups::PickupDescriptor& before,
        const Packets::Pickups::PickupDescriptor& after);
    static int FindIdentity(const Packets::Pickups::PickupIdentity& identity);
    static int FindUnboundMatch(const Packets::Pickups::PickupDescriptor& pickup);
    static int CreateMirror(const Packets::Pickups::PickupDescriptor& pickup);
    static void BindAndApply(int localSlot, const Packets::Pickups::PickupDescriptor& pickup,
        bool networkGenerated);
    static void ApplyAuthoritativeState(int localSlot, const Packets::Pickups::PickupDescriptor& pickup);
    static void RemoveLocalPickup(int localSlot, bool markCollected);
    static void ScanAuthorityPool();
    static void ScanPeerPool();
    static void RequestNearbyCollections();
    static void SendSpawn(LocalPickup& localPickup);
    static void SendState(LocalPickup& localPickup, const Packets::Pickups::PickupDescriptor& state);
    static void SendRemove(LocalPickup& localPickup, Packets::Pickups::ePickupRemovalReason reason);
    static void SendHostCollection(LocalPickup& localPickup,
        const Packets::Pickups::PickupDescriptor& resolvedPickup);
    static bool ValidateAndCollectForPlayer(LocalPickup& localPickup, uint8_t claimantPlayerId,
        Packets::Pickups::PickupDescriptor& outResolvedPickup);
    static bool ExecuteLocalGrant(LocalPickup& localPickup);
    static bool CaptureResolvedPickup(LocalPickup& localPickup,
        Packets::Pickups::PickupDescriptor& outPickup);
    static void MergeLocalCollectibleProgress();
    static void ApplyCollectibleProgress();
    static void PublishCollectibleProgress();
    static uint16_t NextGeneration(uint16_t generation);
    static uint32_t NextRequestNonce();
    static uint32_t GetRegenerationRemaining(const CPickup& pickup);
    static bool IsNativeSlotCurrent(uint16_t slot, const LocalPickup& localPickup);
    static void MaskPeerPickup(int localSlot);
    static void PromoteMirrorForAuthority(int localSlot);
    static void CompleteSnapshot();

    inline static std::array<LocalPickup, Packets::Pickups::MAX_PICKUPS> ms_localPickups{};
    inline static std::array<uint16_t, Packets::Pickups::MAX_PICKUPS> ms_generations{};
    inline static std::array<Packets::Pickups::PickupDescriptor, Packets::Pickups::MAX_PICKUPS>
        ms_snapshotEntries{};
    inline static std::array<bool, Packets::Pickups::MAX_SNAPSHOT_CHUNKS> ms_snapshotChunksReceived{};
    inline static std::array<uint8_t, Packets::Pickups::COLLECTIBLE_KIND_COUNT> ms_collectibleProgress{};
    inline static uint16_t ms_snapshotEntryCount = 0;
    inline static uint8_t ms_snapshotChunkCount = 0;
    inline static uint32_t ms_snapshotRevision = 0;
    inline static uint32_t ms_authorityEpoch = 0;
    inline static uint32_t ms_requestNonce = 0;
    inline static uint32_t ms_progressPublishedEpoch = 0;
    inline static uint8_t ms_snapshotAuthorityPlayerId = UINT8_MAX;
    inline static bool ms_authorityReady = false;
    inline static bool ms_applyingNetworkState = false;
};
