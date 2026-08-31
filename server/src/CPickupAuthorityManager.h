#pragma once

#include "network/packets/pickups.h"

#include <array>
#include <cstdint>

class CNetworkPlayer;

class CPickupAuthorityManager
{
public:
    static void Update();
    static bool HandleState(CNetworkPlayer* player, const Packets::Pickups::PickupStateEvent& packet);
    static bool HandleCollectRequest(
        CNetworkPlayer* player, const Packets::Pickups::PickupCollectRequest& packet);
    static bool HandleCollectDecision(
        CNetworkPlayer* player, const Packets::Pickups::PickupCollectDecision& packet);
    static bool HandleCreateIntent(
        CNetworkPlayer* player, const Packets::Pickups::PickupCreateIntent& packet);
    static void SendActiveStates(CNetworkPlayer* player);
    static void HandlePlayerDisconnected(CNetworkPlayer* player);
    static void HandleAuthorityChange(CNetworkPlayer* newHost);

private:
    static constexpr size_t MAX_PENDING_COLLECT_REQUESTS = 64;
    static constexpr size_t MAX_PENDING_CREATE_INTENTS = 32;
    static constexpr uint32_t PENDING_REQUEST_TIMEOUT_MS = 10000;
    static constexpr uint32_t RATE_WINDOW_MS = 1000;
    static constexpr uint16_t MAX_COLLECT_REQUESTS_PER_WINDOW = 20;
    static constexpr uint16_t MAX_CREATE_INTENTS_PER_WINDOW = 8;

    struct PickupSlot
    {
        bool active = false;
        uint16_t lastGeneration = 0;
        uint16_t collectedGeneration = 0;
        uint32_t lastRevision = 0;
        uint32_t expiresAt = 0;
        Packets::Pickups::PickupState state{};
    };

    struct PendingCollect
    {
        bool active = false;
        uint32_t requestId = 0;
        uint32_t createdAt = 0;
        CNetworkPlayer* requester = nullptr;
        Packets::Pickups::PickupId id{};
    };

    struct PendingCreate
    {
        bool active = false;
        uint32_t requestId = 0;
        uint32_t createdAt = 0;
        CNetworkPlayer* requester = nullptr;
        Packets::Pickups::PickupMetadata metadata{};
    };

    struct RateLimitSlot
    {
        CNetworkPlayer* owner = nullptr;
        uint32_t connectId = 0;
        uint32_t windowStartedAt = 0;
        uint16_t eventCount = 0;
    };

    static std::array<PickupSlot, Packets::Pickups::PICKUP_POOL_CAPACITY> m_slots;
    static std::array<PendingCollect, MAX_PENDING_COLLECT_REQUESTS> m_pendingCollects;
    static std::array<PendingCreate, MAX_PENDING_CREATE_INTENTS> m_pendingCreates;
    static std::array<RateLimitSlot, Config::MAX_SERVER_PLAYERS> m_collectRateLimits;
    static std::array<RateLimitSlot, Config::MAX_SERVER_PLAYERS> m_createRateLimits;
    static uint32_t m_nextRequestId;

    static bool IsAuthenticatedPlayer(const CNetworkPlayer* player);
    static bool IsCurrentHost(const CNetworkPlayer* player);
    static bool CanAcceptRateLimitedEvent(CNetworkPlayer* player,
        std::array<RateLimitSlot, Config::MAX_SERVER_PLAYERS>& slots, uint16_t maximumEvents);
    static bool IsExpired(uint32_t now, uint32_t deadline);
    static uint32_t NextRequestId();
    static uint32_t NextRevision(uint32_t revision);
    static PendingCollect* FindPendingCollect(uint32_t requestId);
    static PendingCreate* FindPendingCreate(uint32_t requestId);
    static bool MetadataMatches(
        const Packets::Pickups::PickupMetadata& left, const Packets::Pickups::PickupMetadata& right);
    static void SendDenied(const PendingCollect& pending);
    static void ClearAllPending();
};
