#pragma once

#include "network/packets/stunts.h"

#include <array>
#include <cstdint>

class CNetworkPlayer;

class CStuntJumpAuthorityManager
{
public:
    static void Update();
    static bool HandleDefinition(
        CNetworkPlayer* player, const Packets::Stunts::StuntDefinitionAnnounce& packet);
    static bool HandleAttempt(CNetworkPlayer* player, const Packets::Stunts::StuntAttempt& packet);
    static void SendSnapshot(CNetworkPlayer* player);
    static void HandlePlayerDisconnected(CNetworkPlayer* player);
    static void HandleAuthorityChange(CNetworkPlayer* newHost);

private:
    static constexpr uint32_t ATTEMPT_TIMEOUT_MS = 20000;
    static constexpr uint32_t MIN_COMPLETION_TIME_MS = 100;
    static constexpr uint32_t DRIVER_SNAPSHOT_MAX_AGE_MS = 1000;
    static constexpr uint32_t RATE_WINDOW_MS = 1000;
    static constexpr uint16_t MAX_DEFINITIONS_PER_WINDOW = 48;
    static constexpr uint16_t MAX_ATTEMPT_EVENTS_PER_WINDOW = 8;

    struct Slot
    {
        bool registered = false;
        bool completed = false;
        Packets::Stunts::StuntId id{};
        Packets::Stunts::StuntDefinition definition{};
        uint32_t revision = 0;
        uint8_t completedByPlayerId = Packets::Stunts::STUNT_INVALID_PLAYER_ID;
        uint64_t collectorSessionNonce = 0;
        uint32_t completionRequestId = 0;
        uint32_t awardSequence = 0;
        int32_t rewardAmount = 0;
        bool allCompleted = false;
    };

    struct Attempt
    {
        bool active = false;
        uint8_t playerId = Packets::Stunts::STUNT_INVALID_PLAYER_ID;
        uint32_t connectId = 0;
        uint32_t requestId = 0;
        uint64_t clientSessionNonce = 0;
        Packets::Stunts::StuntId id{};
        uint16_t vehicleId = 0;
        uint32_t startedAt = 0;
        uint32_t finishHitAt = 0;
        Packets::Stunts::eStuntAttemptAction lastAcceptedAction =
            Packets::Stunts::eStuntAttemptAction::START;
        CVector startPosition{};
    };

    struct RateLimit
    {
        CNetworkPlayer* owner = nullptr;
        uint32_t connectId = 0;
        uint32_t windowStartedAt = 0;
        uint16_t eventCount = 0;
    };

    static std::array<Slot, Packets::Stunts::STUNT_JUMP_CAPACITY> m_slots;
    static std::array<Attempt, Config::MAX_SERVER_PLAYERS> m_attempts;
    static std::array<RateLimit, Config::MAX_SERVER_PLAYERS> m_definitionRates;
    static std::array<RateLimit, Config::MAX_SERVER_PLAYERS> m_attemptRates;
    static uint64_t m_serverRunId;
    static uint16_t m_catalogCount;
    static uint32_t m_catalogHash;
    static bool m_catalogSealed;
    static uint8_t m_authorityPlayerId;
    static uint32_t m_nextRevision;
    static uint32_t m_nextAwardSequence;

    static bool IsAuthenticatedPlayer(const CNetworkPlayer* player);
    static bool IsCurrentHost(const CNetworkPlayer* player);
    static bool AcceptRate(CNetworkPlayer* player,
        std::array<RateLimit, Config::MAX_SERVER_PLAYERS>& rates, uint16_t maximumEvents);
    static uint64_t EnsureServerRunId();
    static uint32_t NextRevision();
    static uint32_t NextAwardSequence();
    static size_t CountRegistered();
    static size_t CountCompleted();
    static uint32_t ComputeCatalogHash();
    static bool DefinitionsMatch(const Packets::Stunts::StuntDefinition& left,
        const Packets::Stunts::StuntDefinition& right);
    static bool IsExactDriver(const CNetworkPlayer* player, uint16_t vehicleId);
    static void ResetUnsealedCatalog();
    static Packets::Stunts::StuntStateEvent MakeState(const Slot& slot);
    static void SendState(const Slot& slot, CNetworkPlayer* player = nullptr);
    static void BroadcastCatalog();
    static void SendAttemptResult(CNetworkPlayer* player, uint32_t requestId,
        uint64_t clientSessionNonce, Packets::Stunts::eStuntAttemptAction action,
        const Packets::Stunts::StuntId& id, bool accepted,
        Packets::Stunts::eStuntAttemptResultReason reason, uint16_t retryAfterMs = 0);
    static void SendAttemptResult(CNetworkPlayer* player,
        const Packets::Stunts::StuntAttempt& packet, bool accepted,
        Packets::Stunts::eStuntAttemptResultReason reason, uint16_t retryAfterMs = 0);
    static void RejectActiveAttempt(Attempt& attempt,
        Packets::Stunts::eStuntAttemptResultReason reason);
    static void RejectCompetingAttempts(const Packets::Stunts::StuntId& id);
};
