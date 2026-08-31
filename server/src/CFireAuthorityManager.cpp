#include "stdafx.h"

#include "CFireAuthorityManager.h"

#include "CNetworkPedManager.h"
#include "CNetworkPlayer.h"
#include "CNetworkPlayerManager.h"
#include "CNetworkVehicleManager.h"
#include "CPacketFactory.h"
#include "logger.h"
#include "network/packets/fires.h"

#include <array>
#include <algorithm>
#include <chrono>
#include <cmath>
#include <cstdint>

namespace
{
using namespace Packets::Fires;

constexpr uint32_t HOST_MUTATION_RATE_LIMIT = 96;
constexpr uint32_t EXTINGUISH_REQUEST_RATE_LIMIT = 12;
constexpr uint64_t RATE_WINDOW_MS = 1000;
constexpr uint64_t PLAYER_OBSERVATION_FRESHNESS_MS = 1500;

struct CanonicalFire
{
    bool initialized = false;
    bool active = false;
    FireId id{};
    FireDescriptor descriptor{};
    uint32_t revision = 0;
    uint64_t expiresAtMs = 0;
};

struct RateWindow
{
    uint64_t startedAtMs = 0;
    uint32_t count = 0;
};

struct PlayerObservation
{
    bool valid = false;
    bool alive = false;
    CVector position{};
    uint8_t area = AREA_MAIN_MAP;
    uint64_t receivedAtMs = 0;
};

std::array<CanonicalFire, FIRE_SLOT_CAPACITY> g_fires{};
std::array<RateWindow, Config::MAX_SERVER_PLAYERS> g_mutationRates{};
std::array<RateWindow, Config::MAX_SERVER_PLAYERS> g_extinguishRates{};
std::array<PlayerObservation, Config::MAX_SERVER_PLAYERS> g_playerObservations{};
uint64_t g_serverRunId = 0;
uint32_t g_revision = 0;
uint32_t g_lastAuthoritySequence = 0;
uint8_t g_authorityPlayerId = FIRE_INVALID_PLAYER_ID;

uint64_t NowMs()
{
    return static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::milliseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
}

void EnsureRunId()
{
    if (g_serverRunId != 0)
        return;
    const uint64_t wallClock = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::system_clock::now().time_since_epoch()).count());
    const uint64_t monotonic = static_cast<uint64_t>(std::chrono::duration_cast<std::chrono::nanoseconds>(
        std::chrono::steady_clock::now().time_since_epoch()).count());
    g_serverRunId = wallClock ^ (monotonic << 1u) ^ 0x4649524552554E31ULL;
    if (g_serverRunId == 0)
        g_serverRunId = 1;
}

uint32_t NextRevision()
{
    ++g_revision;
    if (g_revision == 0)
        ++g_revision;
    return g_revision;
}

bool ConsumeRate(RateWindow& window, uint32_t maximum)
{
    const uint64_t now = NowMs();
    if (window.startedAtMs == 0 || now - window.startedAtMs >= RATE_WINDOW_MS)
    {
        window.startedAtMs = now;
        window.count = 0;
    }
    if (window.count >= maximum)
        return false;
    ++window.count;
    return true;
}

float DistanceSquared(const CVector& left, const CVector& right)
{
    const float x = left.x - right.x;
    const float y = left.y - right.y;
    const float z = left.z - right.z;
    return x * x + y * y + z * z;
}

bool AttachmentExists(const FireDescriptor& descriptor)
{
    switch (descriptor.attachmentType)
    {
    case eFireAttachmentType::WORLD:
        return true;
    case eFireAttachmentType::PLAYER:
        return CNetworkPlayerManager::GetPlayer(descriptor.attachmentId) != nullptr;
    case eFireAttachmentType::PED:
        return CNetworkPedManager::GetPed(descriptor.attachmentId) != nullptr;
    case eFireAttachmentType::VEHICLE:
        return CNetworkVehicleManager::GetVehicle(descriptor.attachmentId) != nullptr;
    default:
        return false;
    }
}

void DetachToWorld(FireDescriptor& descriptor)
{
    descriptor.attachmentType = eFireAttachmentType::WORLD;
    descriptor.attachmentId = FIRE_INVALID_ATTACHMENT_ID;
}

bool ResolveFreshPlayerObservation(CNetworkPlayer* player, CVector& position, uint8_t& area)
{
    if (player == nullptr || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS)
        return false;
    const PlayerObservation& observation = g_playerObservations[player->m_iPlayerId];
    const uint64_t now = NowMs();
    if (!observation.valid || !observation.alive || observation.receivedAtMs == 0 ||
        now - observation.receivedAtMs > PLAYER_OBSERVATION_FRESHNESS_MS)
    {
        return false;
    }
    position = observation.position;
    area = observation.area;
    if (player->m_nVehicleId >= 0)
    {
        CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(player->m_nVehicleId);
        if (vehicle == nullptr)
            return false;
        position = vehicle->m_vecPosition;
    }
    return true;
}

FireStateEvent BuildStateEvent(const CanonicalFire& fire)
{
    FireStateEvent event{};
    event.serverRunId = g_serverRunId;
    event.revision = fire.revision;
    event.authorityPlayerId = g_authorityPlayerId;
    event.id = fire.id;
    event.active = fire.active;
    event.descriptor = fire.descriptor;
    if (fire.active)
    {
        const uint64_t now = NowMs();
        const uint64_t remaining = fire.expiresAtMs > now ? fire.expiresAtMs - now : FIRE_MIN_LIFETIME_MS;
        event.descriptor.remainingLifetimeMs = static_cast<uint32_t>(
            std::clamp<uint64_t>(remaining, FIRE_MIN_LIFETIME_MS, FIRE_MAX_LIFETIME_MS));
    }
    return event;
}

void Broadcast(const CanonicalFire& fire)
{
    if (g_authorityPlayerId >= Config::MAX_SERVER_PLAYERS)
        return;
    FireStateEvent event = BuildStateEvent(fire);
    if (event.HasValidPayload() && event.FitsSerializedBudget())
        GetPacketFactory().SendToAll(event);
}

void Publish(CanonicalFire& fire, bool active)
{
    fire.active = active;
    fire.revision = NextRevision();
    Broadcast(fire);
}

bool HasValidGenerationTransition(const CanonicalFire& fire, const FireId& incoming)
{
    if (!fire.initialized)
        return true;
    if (fire.active && fire.id == incoming)
        return true;
    return IsFireSerialNewer(incoming.generation, fire.id.generation);
}
}  // namespace

void CFireAuthorityManager::HandleStateIntent(CNetworkPlayer* player, const FireStateIntent& intent)
{
    EnsureRunId();
    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();
    if (player == nullptr || player != host || !player->m_bIsHost ||
        player->m_iPlayerId != g_authorityPlayerId || !intent.HasValidPayload())
    {
        logger::warn("Rejected a fire mutation without current host authority");
        return;
    }
    if (!ConsumeRate(g_mutationRates[player->m_iPlayerId], HOST_MUTATION_RATE_LIMIT))
        return;
    if (g_lastAuthoritySequence != 0 &&
        !IsFireSerialNewer(intent.authoritySequence, g_lastAuthoritySequence))
    {
        return;
    }

    CanonicalFire& fire = g_fires[intent.id.slot];
    if (intent.mutation == eFireMutation::UPSERT)
    {
        if (!HasValidGenerationTransition(fire, intent.id))
            return;
        FireDescriptor descriptor = intent.descriptor;
        if (!AttachmentExists(descriptor))
            DetachToWorld(descriptor);

        fire.initialized = true;
        fire.id = intent.id;
        fire.descriptor = descriptor;
        fire.expiresAtMs = NowMs() + descriptor.remainingLifetimeMs;
        // Every accepted authority heartbeat gets a new canonical revision. This renews both the server lease
        // and follower native deadlines; replaying the same authority sequence remains idempotently ignored.
        Publish(fire, true);
    }
    else
    {
        if (!fire.initialized || !fire.active || fire.id != intent.id)
            return;
        fire.descriptor = intent.descriptor;
        Publish(fire, false);
    }
    g_lastAuthoritySequence = intent.authoritySequence;
}

void CFireAuthorityManager::HandleExtinguishRequest(
    CNetworkPlayer* player, FireExtinguishRequest request)
{
    EnsureRunId();
    if (player == nullptr || !request.HasValidPayload() || player->m_iPlayerId < 0 ||
        player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS ||
        !ConsumeRate(g_extinguishRates[player->m_iPlayerId], EXTINGUISH_REQUEST_RATE_LIMIT))
    {
        return;
    }
    CVector canonicalRequesterPosition{};
    uint8_t canonicalRequesterArea = AREA_MAIN_MAP;
    if (!ResolveFreshPlayerObservation(player, canonicalRequesterPosition, canonicalRequesterArea))
        return;

    CanonicalFire& fire = g_fires[request.id.slot];
    if (!fire.initialized || !fire.active || fire.id != request.id ||
        canonicalRequesterArea != fire.descriptor.area ||
        DistanceSquared(canonicalRequesterPosition, fire.descriptor.fallbackPosition) >
            FIRE_MAX_EXTINGUISH_DISTANCE * FIRE_MAX_EXTINGUISH_DISTANCE)
    {
        return;
    }
    CNetworkPlayer* host = CNetworkPlayerManager::GetHost();
    if (host == nullptr || host == player || host->m_iPlayerId != g_authorityPlayerId)
        return;
    request.requesterPlayerId.value = player->m_iPlayerId;
    GetPacketFactory().Send(request, host);
}

void CFireAuthorityManager::HandlePlayerDisconnected(CNetworkPlayer* player)
{
    if (player == nullptr || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS)
        return;
    g_mutationRates[player->m_iPlayerId] = {};
    g_extinguishRates[player->m_iPlayerId] = {};
    g_playerObservations[player->m_iPlayerId] = {};
    for (CanonicalFire& fire : g_fires)
    {
        if (!fire.active || fire.descriptor.attachmentType != eFireAttachmentType::PLAYER ||
            fire.descriptor.attachmentId != player->m_iPlayerId)
        {
            continue;
        }
        DetachToWorld(fire.descriptor);
        fire.revision = NextRevision();
        if (player->m_iPlayerId != g_authorityPlayerId)
            Broadcast(fire);
    }
}

void CFireAuthorityManager::ObservePlayerMovement(
    CNetworkPlayer* player, const CVector& position, bool alive)
{
    if (player == nullptr || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS)
        return;
    PlayerObservation& observation = g_playerObservations[player->m_iPlayerId];
    observation.valid = true;
    observation.alive = alive;
    observation.position = position;
    observation.receivedAtMs = NowMs();
}

void CFireAuthorityManager::ObservePlayerArea(CNetworkPlayer* player, uint8_t area)
{
    if (player == nullptr || player->m_iPlayerId < 0 || player->m_iPlayerId >= Config::MAX_SERVER_PLAYERS ||
        area >= MAX_VISIBLE_AREAS)
    {
        return;
    }
    PlayerObservation& observation = g_playerObservations[player->m_iPlayerId];
    observation.area = area;
    // An EnEx event is allowed to change dimension, but its claimed position is never an authorization input
    // and does not refresh an otherwise stale movement observation.
}

void CFireAuthorityManager::MarkPlayerUnavailable(CNetworkPlayer* player)
{
    if (player != nullptr && player->m_iPlayerId >= 0 && player->m_iPlayerId < Config::MAX_SERVER_PLAYERS)
        g_playerObservations[player->m_iPlayerId] = {};
}

void CFireAuthorityManager::HandleAuthorityChange(CNetworkPlayer* newAuthority)
{
    EnsureRunId();
    g_authorityPlayerId = newAuthority != nullptr
        ? static_cast<uint8_t>(newAuthority->m_iPlayerId)
        : FIRE_INVALID_PLAYER_ID;
    g_lastAuthoritySequence = 0;
    if (newAuthority == nullptr)
        return;
    for (CanonicalFire& fire : g_fires)
    {
        if (!fire.active)
            continue;
        fire.revision = NextRevision();
        Broadcast(fire);
    }
}

void CFireAuthorityManager::SendSnapshot(CNetworkPlayer* player)
{
    EnsureRunId();
    if (player == nullptr || g_authorityPlayerId >= Config::MAX_SERVER_PLAYERS)
        return;
    for (const CanonicalFire& fire : g_fires)
    {
        if (!fire.active)
            continue;
        FireStateEvent event = BuildStateEvent(fire);
        if (event.HasValidPayload() && event.FitsSerializedBudget())
            GetPacketFactory().Send(event, player);
    }
}

void CFireAuthorityManager::Update()
{
    EnsureRunId();
    const uint64_t now = NowMs();
    for (CanonicalFire& fire : g_fires)
    {
        if (!fire.active)
            continue;
        if (!AttachmentExists(fire.descriptor))
        {
            DetachToWorld(fire.descriptor);
            fire.revision = NextRevision();
            Broadcast(fire);
        }
        if (now >= fire.expiresAtMs)
            Publish(fire, false);
    }
}
