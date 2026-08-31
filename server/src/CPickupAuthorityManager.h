#pragma once

#include "network/packets/pickups.h"

#include <array>

class CNetworkPlayer;

class CPickupAuthorityManager
{
public:
    static void Update();
    static bool HandleSpawn(CNetworkPlayer* sender, const Packets::Pickups::PickupSpawn& packet);
    static bool HandleState(CNetworkPlayer* sender, const Packets::Pickups::PickupState& packet);
    static bool HandleRemove(CNetworkPlayer* sender, const Packets::Pickups::PickupRemove& packet);
    static bool HandleCollectRequest(CNetworkPlayer* sender, const Packets::Pickups::PickupCollectRequest& packet);
    static bool HandleCollectDecision(CNetworkPlayer* sender, const Packets::Pickups::PickupCollectDecision& packet);
    static bool HandleProgressSnapshot(CNetworkPlayer* sender,
        const Packets::Pickups::PickupSnapshotChunk& packet);
    static void SendSnapshot(CNetworkPlayer* recipient);
    static void HandleAuthorityChanged(CNetworkPlayer* newHost);
    static void HandlePlayerDisconnected(int playerId);
    static void ResetSession();

    static uint32_t GetAuthorityEpoch() { return ms_authorityEpoch; }
    static uint32_t GetSnapshotRevision() { return ms_snapshotRevision; }

private:
    struct PickupRecord
    {
        bool occupied = false;
        Packets::Pickups::PickupDescriptor pickup{};
    };

    struct PendingRequest
    {
        bool active = false;
        Packets::Pickups::PickupIdentity identity{};
        uint32_t authorityEpoch = 0;
        uint32_t observedRevision = 0;
        uint32_t requestNonce = 0;
        uint32_t receivedAt = 0;
    };

    struct RequestRate
    {
        uint32_t windowStartedAt = 0;
        uint32_t lastNonce = 0;
        uint8_t count = 0;
        bool hasNonce = false;
    };

    static bool IsCurrentHost(const CNetworkPlayer* sender);
    static bool IsGenerationNewer(uint16_t candidate, uint16_t reference);
    static uint32_t NextRevision(uint32_t revision);
    static void AdvanceSnapshotRevision();
    static bool ConsumeRequestBudget(int playerId, uint32_t requestNonce);
    static void SendSnapshotToAll();

    inline static std::array<PickupRecord, Packets::Pickups::MAX_PICKUPS> ms_pickups{};
    inline static std::array<PendingRequest, Config::MAX_SERVER_PLAYERS> ms_pendingRequests{};
    inline static std::array<RequestRate, Config::MAX_SERVER_PLAYERS> ms_requestRates{};
    inline static std::array<uint8_t, Packets::Pickups::COLLECTIBLE_KIND_COUNT> ms_collectibleProgress{};
    inline static uint32_t ms_authorityEpoch = 0;
    inline static uint32_t ms_snapshotRevision = 0;
};
