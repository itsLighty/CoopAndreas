#include "stdafx.h"
#include "CGangZoneWarSyncManager.h"
#include <CGangWars.h>
#include <CTheZones.h>
#include <CZoneInfo.h>
#include <algorithm>
#include <cmath>
#include <cstring>

using namespace Packets::World;

namespace
{
// The bundled plugin-sdk revision has the navigation-zone wrapper mislabeled as ZoneInfoArray and models CZone
// as 0x30 bytes. GTA SA 1.0 US actually keeps 0x11-byte CZoneInfo records separately. These addresses and the
// 0x20 navigation stride were audited against the game's 0x572180/0x572400/0x572440 routines.
constexpr uintptr_t ZONE_INFO_COUNT_ADDRESS = 0xBA1DE8;
constexpr uintptr_t ZONE_INFO_ARRAY_ADDRESS = 0xBA1DF0;
constexpr uintptr_t NAVIGATION_ZONE_COUNT_ADDRESS = 0xBA3794;
constexpr uintptr_t NAVIGATION_ZONE_ARRAY_ADDRESS = 0xBA3798;
constexpr uintptr_t GANG_WAR_RADAR_BLIP_ADDRESS = 0x96AB98;
constexpr size_t NAVIGATION_ZONE_STRIDE = 0x20;
constexpr uint32_t ACTIVE_WAR_PUBLISH_INTERVAL_MS = 250;
constexpr uint32_t IDLE_WAR_PUBLISH_INTERVAL_MS = 2000;
constexpr uint32_t ZONE_HEARTBEAT_INTERVAL_MS = 10000;
constexpr int RADAR_MODE_MASK = 0x60;
constexpr int RADAR_MODE_SHIFT = 5;

static_assert(sizeof(CZoneInfo) == 0x11, "GTA SA 1.0 US CZoneInfo layout must remain 0x11 bytes");

int GetZoneInfoCount()
{
    const int count = *reinterpret_cast<const int16_t*>(ZONE_INFO_COUNT_ADDRESS);
    return count >= 0 && count <= MAX_GANG_ZONE_INFOS ? count : -1;
}

int GetNavigationZoneCount()
{
    const int count = *reinterpret_cast<const int16_t*>(NAVIGATION_ZONE_COUNT_ADDRESS);
    return count >= 0 && count <= MAX_GANG_NAVIGATION_ZONES ? count : -1;
}

CZoneInfo* GetZoneInfoArray()
{
    return reinterpret_cast<CZoneInfo*>(ZONE_INFO_ARRAY_ADDRESS);
}

int& GetNativeGangWarRadarBlipHandle()
{
    // The bundled plugin-sdk declares this as CRadar*, but the 1.0 US field and save structure use an int handle.
    return *reinterpret_cast<int*>(GANG_WAR_RADAR_BLIP_ADDRESS);
}

int PointerToBoundedIndex(const void* pointer, uintptr_t base, size_t stride, int count)
{
    if (pointer == nullptr || count <= 0)
    {
        return INVALID_GANG_ZONE_INDEX;
    }
    const uintptr_t address = reinterpret_cast<uintptr_t>(pointer);
    if (address < base)
    {
        return INVALID_GANG_ZONE_INDEX;
    }
    const uintptr_t offset = address - base;
    if (offset % stride != 0 || offset / stride >= static_cast<uintptr_t>(count))
    {
        return INVALID_GANG_ZONE_INDEX;
    }
    return static_cast<int>(offset / stride);
}

CZone* NavigationZoneFromIndex(int index)
{
    return reinterpret_cast<CZone*>(NAVIGATION_ZONE_ARRAY_ADDRESS + index * NAVIGATION_ZONE_STRIDE);
}

uint32_t ElapsedSince(uint32_t now, uint32_t timestamp)
{
    return timestamp == 0 ? 0 : now - timestamp;
}

uint32_t BoundedElapsedSince(uint32_t now, uint32_t timestamp)
{
    return std::min(ElapsedSince(now, timestamp), GangWarState::MAX_REPLICATED_ELAPSED_MS);
}

bool CaptureWorldPosition(const CVector& source, WorldPositionCompressed& destination)
{
    if (!std::isfinite(source.x) || !std::isfinite(source.y) || !std::isfinite(source.z) || source.x < -3000.0f ||
        source.x > 3000.0f || source.y < -3000.0f || source.y > 3000.0f || source.z < -120.0f ||
        source.z > 1000.0f)
    {
        destination = CVector(0.0f, 0.0f, 0.0f);
        return false;
    }
    destination = source;
    return true;
}

bool IsWarObservable(const GangWarState& state)
{
    return state.lifecycle != eGangWarLifecycle::NOT_IN_WAR ||
           state.attackLifecycle != eGangAttackLifecycle::NO_ATTACK;
}

unsigned int GetStockGangBlipColour(int gang)
{
    // Audited copies of the 1.0 US gaGangColoursR/G/B tables used by CGangWars::GetGangColor at 0x44582F.
    static constexpr uint8_t RED[] = {200, 70, 255, 0, 255, 200, 240, 0, 255, 255};
    static constexpr uint8_t GREEN[] = {0, 200, 200, 0, 220, 200, 140, 200, 255, 255};
    static constexpr uint8_t BLUE[] = {200, 0, 0, 200, 190, 200, 240, 255, 255, 255};
    const unsigned int red = RED[gang];
    const unsigned int green = GREEN[gang];
    const unsigned int blue = BLUE[gang];
    return (blue | (((red << 8u) | green) << 8u)) << 8u | 0xFFu;
}

eRadarSprite GetStockGangBlipSprite(int gang)
{
    switch (gang)
    {
    case 1:  // GANG_BALLAS
        return RADAR_SPRITE_GANGP;
    case 2:  // GANG_VAGOS
    case 4:  // GANG_DANANGBOYS
        return RADAR_SPRITE_GANGY;
    case 3:  // GANG_RIFA
        return RADAR_SPRITE_GANGB;
    default:
        return RADAR_SPRITE_ENEMYATTACK;
    }
}

bool IsMatchingDefensiveWarBlip(const tRadarTrace& trace, const GangWarState& state, eRadarSprite sprite)
{
    return trace.m_bInUse && trace.m_nBlipType == BLIP_COORD && trace.m_nRadarSprite == sprite &&
           std::fabs(trace.m_vecPos.x - state.pointOfAttack.x) <= 0.01f &&
           std::fabs(trace.m_vecPos.y - state.pointOfAttack.y) <= 0.01f &&
           std::fabs(trace.m_vecPos.z - state.pointOfAttack.z) <= 0.01f;
}
}

GangZoneState CGangZoneWarSyncManager::m_LastSentZoneState{};
GangWarState CGangZoneWarSyncManager::m_LastSentWarState{};
GangZoneState CGangZoneWarSyncManager::m_AppliedZoneState{};
GangWarState CGangZoneWarSyncManager::m_AppliedWarState{};
GangZoneState CGangZoneWarSyncManager::m_PendingZoneState{};
GangWarState CGangZoneWarSyncManager::m_PendingWarState{};
uint32_t CGangZoneWarSyncManager::m_nNextZoneRevision = 0;
uint32_t CGangZoneWarSyncManager::m_nNextWarRevision = 0;
uint32_t CGangZoneWarSyncManager::m_nLastZonePublishTime = 0;
uint32_t CGangZoneWarSyncManager::m_nLastWarPublishTime = 0;
uint32_t CGangZoneWarSyncManager::m_nWarStateAppliedAt = 0;
int CGangZoneWarSyncManager::m_nExpectedAuthorityPlayerId = -1;
int CGangZoneWarSyncManager::m_nReplicatedRadarBlip = 0;
bool CGangZoneWarSyncManager::m_bLocalPlayerIsAuthority = false;
bool CGangZoneWarSyncManager::m_bHasLastSentZoneState = false;
bool CGangZoneWarSyncManager::m_bHasLastSentWarState = false;
bool CGangZoneWarSyncManager::m_bHasAppliedZoneState = false;
bool CGangZoneWarSyncManager::m_bHasAppliedWarState = false;
bool CGangZoneWarSyncManager::m_bHasPendingZoneState = false;
bool CGangZoneWarSyncManager::m_bHasPendingWarState = false;

void CGangZoneWarSyncManager::ProcessGangWars()
{
    if (!CNetwork::m_bAuthenticated)
    {
        ResetNetworkState();
        CGangWars::Update();
        return;
    }

    if (CLocalPlayer::m_bIsHost)
    {
        // This is the only online path that advances the native state machine. Its CWorld::Add calls continue
        // through the existing ped/vehicle entity synchronization hooks.
        CGangWars::Update();
        PublishAuthoritativeState();
        return;
    }

    ApplyAuthoritativeState(ApplyPendingState());
}

void CGangZoneWarSyncManager::HandleZoneState(const GangZoneState& state)
{
    if (!CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost || !state.HasValidState() ||
        !state.FitsSerializedBudget() ||
        !CanAcceptAuthority(state.authorityPlayerId))
    {
        logger::warn("Rejected invalid or unexpected gang-zone state");
        return;
    }

    const uint32_t referenceRevision = m_bHasPendingZoneState ? m_PendingZoneState.revision :
                                       m_bHasAppliedZoneState ? m_AppliedZoneState.revision : 0;
    if (referenceRevision != 0 && !IsGangWorldRevisionNewer(state.revision, referenceRevision))
    {
        logger::warn("Rejected stale gang-zone state revision");
        return;
    }
    m_PendingZoneState = state;
    m_bHasPendingZoneState = true;
}

void CGangZoneWarSyncManager::HandleWarState(const GangWarState& state)
{
    if (!CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost || !state.HasValidState() ||
        !state.FitsSerializedBudget() ||
        !CanAcceptAuthority(state.authorityPlayerId))
    {
        logger::warn("Rejected invalid or unexpected gang-war state");
        return;
    }

    const uint32_t referenceRevision = m_bHasPendingWarState ? m_PendingWarState.revision :
                                       m_bHasAppliedWarState ? m_AppliedWarState.revision : 0;
    if (referenceRevision != 0 && !IsGangWorldRevisionNewer(state.revision, referenceRevision))
    {
        logger::warn("Rejected stale gang-war state revision");
        return;
    }
    m_PendingWarState = state;
    m_bHasPendingWarState = true;
}

void CGangZoneWarSyncManager::HandleAuthorityChanged(int authorityPlayerId, bool localPlayerIsAuthority)
{
    if (m_bLocalPlayerIsAuthority && !localPlayerIsAuthority)
    {
        int& nativeBlip = GetNativeGangWarRadarBlipHandle();
        if (nativeBlip != 0 && CRadar::GetActualBlipArrayIndex(nativeBlip) != -1)
        {
            CRadar::ClearBlip(nativeBlip);
        }
        nativeBlip = 0;
    }

    bool transferredReplicatedBlip = false;
    if (!m_bLocalPlayerIsAuthority && localPlayerIsAuthority && m_bHasAppliedWarState &&
        m_AppliedWarState.attackLifecycle == eGangAttackLifecycle::WAR_NOTIFIED &&
        m_nReplicatedRadarBlip != 0 && CRadar::GetActualBlipArrayIndex(m_nReplicatedRadarBlip) != -1)
    {
        // Authority promotion is the one safe point to hand the visual-only peer marker to the native lifecycle.
        // From the next CGangWars::Update onward the new host owns its blink/clear behavior exactly as stock.
        GetNativeGangWarRadarBlipHandle() = m_nReplicatedRadarBlip;
        m_nReplicatedRadarBlip = 0;
        transferredReplicatedBlip = true;
    }
    if (!transferredReplicatedBlip)
    {
        ClearReplicatedRadarBlip();
    }
    m_nExpectedAuthorityPlayerId = authorityPlayerId >= 0 && authorityPlayerId < Config::MAX_SERVER_PLAYERS
        ? authorityPlayerId
        : -1;
    m_bLocalPlayerIsAuthority = localPlayerIsAuthority;
    m_bHasLastSentZoneState = false;
    m_bHasLastSentWarState = false;
    m_bHasAppliedZoneState = false;
    m_bHasAppliedWarState = false;
    m_bHasPendingZoneState = false;
    m_bHasPendingWarState = false;
    m_nLastZonePublishTime = 0;
    m_nLastWarPublishTime = 0;
    m_nWarStateAppliedAt = 0;
    m_nNextZoneRevision = 0;
    m_nNextWarRevision = 0;
}

void CGangZoneWarSyncManager::ResetNetworkState()
{
    HandleAuthorityChanged(-1, false);
    m_LastSentZoneState = GangZoneState{};
    m_LastSentWarState = GangWarState{};
    m_AppliedZoneState = GangZoneState{};
    m_AppliedWarState = GangWarState{};
    m_PendingZoneState = GangZoneState{};
    m_PendingWarState = GangWarState{};
}

bool CGangZoneWarSyncManager::CaptureZoneState(GangZoneState& state)
{
    const int zoneInfoCount = GetZoneInfoCount();
    const int localPlayerId = CNetworkPlayerManager::m_nMyId;
    if (zoneInfoCount <= 0 || localPlayerId < 0 || localPlayerId >= Config::MAX_SERVER_PLAYERS)
    {
        return false;
    }

    state.authorityPlayerId = static_cast<uint8_t>(localPlayerId);
    state.zoneInfoCount = static_cast<uint16_t>(zoneInfoCount);
    CZoneInfo* zoneInfos = GetZoneInfoArray();
    for (int zoneIndex = 0; zoneIndex < zoneInfoCount; ++zoneIndex)
    {
        std::memcpy(state.gangDensities[zoneIndex].data(), zoneInfos[zoneIndex].m_nGangDensity,
            GANG_DENSITY_COUNT);
    }
    return true;
}

bool CGangZoneWarSyncManager::CaptureWarState(GangWarState& state)
{
    const int zoneInfoCount = GetZoneInfoCount();
    const int navigationZoneCount = GetNavigationZoneCount();
    const int localPlayerId = CNetworkPlayerManager::m_nMyId;
    if (zoneInfoCount <= 0 || navigationZoneCount <= 0 || localPlayerId < 0 ||
        localPlayerId >= Config::MAX_SERVER_PLAYERS)
    {
        return false;
    }

    state.authorityPlayerId = static_cast<uint8_t>(localPlayerId);
    state.lifecycle = static_cast<eGangWarLifecycle>(CGangWars::State);
    state.attackLifecycle = static_cast<eGangAttackLifecycle>(CGangWars::State2);
    state.primaryGang = static_cast<int8_t>(CGangWars::Gang1);
    state.secondaryGang = static_cast<int8_t>(CGangWars::Gang2);
    state.warFerocity = static_cast<int8_t>(CGangWars::WarFerocity);
    state.fightZoneInfoIndex = static_cast<int16_t>(PointerToBoundedIndex(CGangWars::pZoneInfoToFightOver,
        ZONE_INFO_ARRAY_ADDRESS, sizeof(CZoneInfo), zoneInfoCount));
    state.fightNavigationZoneIndex = static_cast<int16_t>(PointerToBoundedIndex(CGangWars::pZoneToFightOver,
        NAVIGATION_ZONE_ARRAY_ADDRESS, NAVIGATION_ZONE_STRIDE, navigationZoneCount));
    state.trainingZoneInfoIndex = CGangWars::ZoneInfoForTraining >= 0 &&
            CGangWars::ZoneInfoForTraining < zoneInfoCount
        ? static_cast<int16_t>(CGangWars::ZoneInfoForTraining)
        : INVALID_GANG_ZONE_INDEX;
    state.gangWarsActive = CGangWars::bGangWarsActive;
    state.trainingMission = CGangWars::bTrainingMission;
    state.playerIsCloseBy = CGangWars::bPlayerIsCloseby;
    state.canTriggerWhenOnMission = CGangWars::bCanTriggerGangWarWhenOnAMission;
    state.playerIsOnMission = CGangWars::bIsPlayerOnAMission;

    if (CGangWars::NumSpecificZones < 0 || CGangWars::NumSpecificZones > MAX_SPECIFIC_GANG_ZONES)
    {
        return false;
    }
    state.specificZoneCount = static_cast<uint8_t>(CGangWars::NumSpecificZones);
    for (size_t index = 0; index < state.specificZoneCount; ++index)
    {
        if (CGangWars::aSpecificZones[index] < 0 || CGangWars::aSpecificZones[index] >= navigationZoneCount)
        {
            return false;
        }
        state.specificNavigationZoneIndices[index] = static_cast<uint16_t>(CGangWars::aSpecificZones[index]);
    }
    for (size_t index = 0; index < state.gangRatings.size(); ++index)
    {
        if (CGangWars::GangRatings[index] < 0 || CGangWars::GangRatings[index] > 2 ||
            CGangWars::GangRatingStrength[index] < 0 ||
            CGangWars::GangRatingStrength[index] > navigationZoneCount)
        {
            return false;
        }
        state.gangRatings[index] = static_cast<uint8_t>(CGangWars::GangRatings[index]);
        state.gangRatingStrength[index] = static_cast<uint16_t>(CGangWars::GangRatingStrength[index]);
    }

    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    if (state.attackLifecycle == eGangAttackLifecycle::NO_ATTACK)
    {
        state.fightTimerRemainingMs = 0;
    }
    else if (CGangWars::FightTimer < 0 ||
             static_cast<uint32_t>(CGangWars::FightTimer) > GangWarState::MAX_FIGHT_TIMER_MS)
    {
        return false;
    }
    else
    {
        state.fightTimerRemainingMs = static_cast<uint32_t>(CGangWars::FightTimer);
    }
    state.waveElapsedMs = state.lifecycle == eGangWarLifecycle::NOT_IN_WAR
        ? 0
        : BoundedElapsedSince(now, CGangWars::TimeStarted);
    state.timeOutsideFightAreaMs = state.lifecycle == eGangWarLifecycle::NOT_IN_WAR
        ? 0
        : BoundedElapsedSince(now, CGangWars::LastTimeInArea);
    state.timeTillNextAttackMs = std::clamp(CGangWars::TimeTillNextAttack,
        GangWarState::MIN_TIME_TILL_NEXT_ATTACK_MS, GangWarState::MAX_TIME_TILL_NEXT_ATTACK_MS);
    state.provocation = std::clamp(CGangWars::Provocation, 0.0f, GangWarState::MAX_PROVOCATION);
    // Despite its name, UpdateTerritoryUnderControlPercentage stores groveZones / allGangZones. Difficulty is
    // copied from that ratio at both war starts, so both native fields are fractions rather than 0..100 values.
    state.difficulty = std::clamp(CGangWars::Difficulty, 0.0f, 1.0f);
    state.territoryControl = std::clamp(CGangWars::TerritoryUnderControlPercentage, 0.0f, 1.0f);
    const bool validStartPosition = CaptureWorldPosition(CGangWars::CoorsOfPlayerAtStartOfWar, state.warStartPosition);
    const bool validAttackPosition = CaptureWorldPosition(CGangWars::PointOfAttack, state.pointOfAttack);
    if (IsWarObservable(state) && (!validStartPosition || !validAttackPosition))
    {
        return false;
    }
    return true;
}

void CGangZoneWarSyncManager::PublishAuthoritativeState()
{
    if (!m_bLocalPlayerIsAuthority && m_nExpectedAuthorityPlayerId >= 0)
    {
        return;
    }

    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    GangZoneState zoneState{};
    if (CaptureZoneState(zoneState))
    {
        const bool changed = !m_bHasLastSentZoneState || !HasSameZonePayload(zoneState, m_LastSentZoneState);
        if (changed || now - m_nLastZonePublishTime >= ZONE_HEARTBEAT_INTERVAL_MS)
        {
            zoneState.revision = NextNonZeroRevision(m_nNextZoneRevision);
            if (zoneState.HasValidState() && zoneState.FitsSerializedBudget())
            {
                GetPacketFactory().Send(zoneState);
                m_LastSentZoneState = zoneState;
                m_bHasLastSentZoneState = true;
                m_nLastZonePublishTime = now;
            }
        }
    }

    GangWarState warState{};
    if (!CaptureWarState(warState))
    {
        return;
    }
    const bool active = IsWarObservable(warState);
    const uint32_t interval = active ? ACTIVE_WAR_PUBLISH_INTERVAL_MS : IDLE_WAR_PUBLISH_INTERVAL_MS;
    const bool changed = !m_bHasLastSentWarState ||
                         !HasSameWarDiscretePayload(warState, m_LastSentWarState);
    if (changed || now - m_nLastWarPublishTime >= interval)
    {
        warState.revision = NextNonZeroRevision(m_nNextWarRevision);
        if (warState.HasValidState() && warState.FitsSerializedBudget())
        {
            GetPacketFactory().Send(warState);
            m_LastSentWarState = warState;
            m_bHasLastSentWarState = true;
            m_nLastWarPublishTime = now;
        }
    }
}

bool CGangZoneWarSyncManager::ApplyPendingState()
{
    bool forceRadarRefresh = false;
    if (m_bHasPendingZoneState && GetZoneInfoCount() == m_PendingZoneState.zoneInfoCount)
    {
        m_AppliedZoneState = m_PendingZoneState;
        m_bHasAppliedZoneState = true;
        m_bHasPendingZoneState = false;
        forceRadarRefresh = true;
    }
    if (m_bHasPendingWarState && CanApplyWarState(m_PendingWarState))
    {
        m_AppliedWarState = m_PendingWarState;
        m_bHasAppliedWarState = true;
        m_bHasPendingWarState = false;
        m_nWarStateAppliedAt = CTimer::m_snTimeInMilliseconds;
        forceRadarRefresh = true;
    }
    return forceRadarRefresh;
}

void CGangZoneWarSyncManager::ApplyAuthoritativeState(bool forceRadarRefresh)
{
    bool densitiesChanged = false;
    if (m_bHasAppliedZoneState && GetZoneInfoCount() == m_AppliedZoneState.zoneInfoCount)
    {
        CZoneInfo* zoneInfos = GetZoneInfoArray();
        for (size_t zoneIndex = 0; zoneIndex < m_AppliedZoneState.zoneInfoCount; ++zoneIndex)
        {
            if (std::memcmp(zoneInfos[zoneIndex].m_nGangDensity,
                    m_AppliedZoneState.gangDensities[zoneIndex].data(), GANG_DENSITY_COUNT) != 0)
            {
                std::memcpy(zoneInfos[zoneIndex].m_nGangDensity,
                    m_AppliedZoneState.gangDensities[zoneIndex].data(), GANG_DENSITY_COUNT);
                densitiesChanged = true;
            }
        }
    }

    if (!m_bHasAppliedWarState || !CanApplyWarState(m_AppliedWarState))
    {
        if (forceRadarRefresh || densitiesChanged)
        {
            CTheZones::FillZonesWithGangColours(!CGangWars::bGangWarsActive);
        }
        return;
    }

    const GangWarState& state = m_AppliedWarState;
    const uint32_t now = CTimer::m_snTimeInMilliseconds;
    const uint32_t sinceSnapshot = now - m_nWarStateAppliedAt;
    CGangWars::State = static_cast<eGangWarState>(state.lifecycle);
    CGangWars::State2 = static_cast<eGangAttackState>(state.attackLifecycle);
    CGangWars::Gang1 = state.primaryGang;
    CGangWars::Gang2 = state.secondaryGang;
    CGangWars::WarFerocity = state.warFerocity;
    CGangWars::pZoneInfoToFightOver = state.fightZoneInfoIndex >= 0
        ? &GetZoneInfoArray()[state.fightZoneInfoIndex]
        : nullptr;
    CGangWars::pZoneToFightOver = state.fightNavigationZoneIndex >= 0
        ? NavigationZoneFromIndex(state.fightNavigationZoneIndex)
        : nullptr;
    CGangWars::ZoneInfoForTraining = state.trainingZoneInfoIndex;
    CGangWars::bGangWarsActive = state.gangWarsActive;
    CGangWars::bTrainingMission = state.trainingMission;
    CGangWars::bPlayerIsCloseby = state.playerIsCloseBy;
    CGangWars::bCanTriggerGangWarWhenOnAMission = state.canTriggerWhenOnMission;
    CGangWars::bIsPlayerOnAMission = state.playerIsOnMission;
    CGangWars::NumSpecificZones = state.specificZoneCount;
    for (size_t index = 0; index < MAX_SPECIFIC_GANG_ZONES; ++index)
    {
        CGangWars::aSpecificZones[index] = index < state.specificZoneCount
            ? state.specificNavigationZoneIndices[index]
            : 0;
    }
    for (size_t index = 0; index < state.gangRatings.size(); ++index)
    {
        CGangWars::GangRatings[index] = state.gangRatings[index];
        CGangWars::GangRatingStrength[index] = state.gangRatingStrength[index];
    }
    const uint32_t displayedFightTimerMs = state.attackLifecycle == eGangAttackLifecycle::NO_ATTACK
        ? state.fightTimerRemainingMs
        : state.fightTimerRemainingMs > sinceSnapshot ? state.fightTimerRemainingMs - sinceSnapshot : 0;
    CGangWars::FightTimer = displayedFightTimerMs;
    CGangWars::TimeStarted = now - std::min<uint32_t>(
        state.waveElapsedMs + std::min(sinceSnapshot, GangWarState::MAX_REPLICATED_ELAPSED_MS),
        GangWarState::MAX_REPLICATED_ELAPSED_MS);
    CGangWars::LastTimeInArea = now - std::min<uint32_t>(state.timeOutsideFightAreaMs +
            std::min(sinceSnapshot, GangWarState::MAX_REPLICATED_ELAPSED_MS),
        GangWarState::MAX_REPLICATED_ELAPSED_MS);
    CGangWars::TimeTillNextAttack = state.lifecycle == eGangWarLifecycle::NOT_IN_WAR &&
            state.attackLifecycle == eGangAttackLifecycle::NO_ATTACK
        ? std::max(state.timeTillNextAttackMs - static_cast<float>(sinceSnapshot),
              GangWarState::MIN_TIME_TILL_NEXT_ATTACK_MS)
        : state.timeTillNextAttackMs;
    CGangWars::Provocation = state.provocation;
    CGangWars::Difficulty = state.difficulty;
    CGangWars::TerritoryUnderControlPercentage = state.territoryControl;
    CGangWars::CoorsOfPlayerAtStartOfWar = state.warStartPosition;
    CGangWars::PointOfAttack = state.pointOfAttack;
    UpdateReplicatedRadarBlip(state, displayedFightTimerMs);

    if (forceRadarRefresh || densitiesChanged)
    {
        CTheZones::FillZonesWithGangColours(!state.gangWarsActive);
    }
    if (IsWarObservable(state) && CGangWars::pZoneInfoToFightOver != nullptr)
    {
        CZoneInfo* fightZone = CGangWars::pZoneInfoToFightOver;
        fightZone->m_nFlags = static_cast<char>((static_cast<uint8_t>(fightZone->m_nFlags) & ~RADAR_MODE_MASK) |
            (2 << RADAR_MODE_SHIFT));
        fightZone->m_ZoneColor = CRGBA(255, 0, 0, 160);
    }

    // The stock CGangWars::RadarBlip global remains untouched. Peers own a separate local marker so no host
    // lifecycle, actor, reward, or save side effect can observe or clear it.
}

void CGangZoneWarSyncManager::UpdateReplicatedRadarBlip(
    const GangWarState& state, uint32_t displayedFightTimerMs)
{
    if (!CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost ||
        state.attackLifecycle != eGangAttackLifecycle::WAR_NOTIFIED || state.primaryGang < 0)
    {
        ClearReplicatedRadarBlip();
        return;
    }

    if (m_nReplicatedRadarBlip != 0 && CRadar::GetActualBlipArrayIndex(m_nReplicatedRadarBlip) == -1)
    {
        m_nReplicatedRadarBlip = 0;
    }

    const eRadarSprite sprite = GetStockGangBlipSprite(state.primaryGang);
    if (m_nReplicatedRadarBlip == 0)
    {
        // A general static-blip snapshot may have recreated the same host marker after clearing our handle.
        // Remove only exact defensive-war duplicates before creating the manager-owned replacement.
        for (int index = 0; index < MAX_RADAR_TRACES; ++index)
        {
            if (IsMatchingDefensiveWarBlip(CRadar::ms_RadarTrace[index], state, sprite))
            {
                CRadar::ClearActualBlip(index);
            }
        }
        m_nReplicatedRadarBlip = CRadar::SetCoordBlip(BLIP_COORD, state.pointOfAttack,
            GetStockGangBlipColour(state.primaryGang), BLIP_DISPLAY_BLIP_ONLY, nullptr);
        if (m_nReplicatedRadarBlip == 0)
        {
            return;
        }
    }

    const int actualIndex = CRadar::GetActualBlipArrayIndex(m_nReplicatedRadarBlip);
    if (actualIndex == -1)
    {
        m_nReplicatedRadarBlip = 0;
        return;
    }
    CRadar::ms_RadarTrace[actualIndex].m_vecPos = state.pointOfAttack;
    CRadar::SetBlipSprite(m_nReplicatedRadarBlip, sprite);
    CRadar::ChangeBlipColour(m_nReplicatedRadarBlip, GetStockGangBlipColour(state.primaryGang));

    int blinkPeriod = 7;
    if (displayedFightTimerMs > 120000)
    {
        blinkPeriod = 10;
    }
    else if (displayedFightTimerMs > 60000)
    {
        blinkPeriod = 9;
    }
    else if (displayedFightTimerMs > 30000)
    {
        blinkPeriod = 8;
    }
    const eBlipDisplay display = ((CTimer::m_snTimeInMilliseconds >> blinkPeriod) % 2u) != 0
        ? BLIP_DISPLAY_NEITHER
        : BLIP_DISPLAY_BLIP_ONLY;
    CRadar::ChangeBlipDisplay(m_nReplicatedRadarBlip, display);
}

void CGangZoneWarSyncManager::ClearReplicatedRadarBlip()
{
    if (m_nReplicatedRadarBlip != 0)
    {
        if (CRadar::GetActualBlipArrayIndex(m_nReplicatedRadarBlip) != -1)
        {
            CRadar::ClearBlip(m_nReplicatedRadarBlip);
        }
        m_nReplicatedRadarBlip = 0;
    }

    // If a general static-blip snapshot replaced our handle immediately before disconnect/migration, remove the
    // exact replicated defensive marker as well. This does not touch unrelated mission markers.
    if (m_bHasAppliedWarState &&
        m_AppliedWarState.attackLifecycle == eGangAttackLifecycle::WAR_NOTIFIED &&
        m_AppliedWarState.primaryGang >= 0)
    {
        const eRadarSprite sprite = GetStockGangBlipSprite(m_AppliedWarState.primaryGang);
        for (int index = 0; index < MAX_RADAR_TRACES; ++index)
        {
            if (IsMatchingDefensiveWarBlip(CRadar::ms_RadarTrace[index], m_AppliedWarState, sprite))
            {
                CRadar::ClearActualBlip(index);
            }
        }
    }
}

bool CGangZoneWarSyncManager::CanAcceptAuthority(uint8_t authorityPlayerId)
{
    if (authorityPlayerId >= Config::MAX_SERVER_PLAYERS)
    {
        return false;
    }
    if (m_nExpectedAuthorityPlayerId >= 0 && authorityPlayerId != m_nExpectedAuthorityPlayerId)
    {
        return false;
    }
    if (m_nExpectedAuthorityPlayerId < 0)
    {
        // The server stamps this identity. This also repairs the existing late-join path where an unchanged host
        // does not receive another PLAYER_ASSIGN_HOST broadcast.
        m_nExpectedAuthorityPlayerId = authorityPlayerId;
    }
    return true;
}

bool CGangZoneWarSyncManager::CanApplyWarState(const GangWarState& state)
{
    const int zoneInfoCount = GetZoneInfoCount();
    const int navigationZoneCount = GetNavigationZoneCount();
    if (zoneInfoCount <= 0 || navigationZoneCount <= 0 ||
        (state.fightZoneInfoIndex >= zoneInfoCount) ||
        (state.fightNavigationZoneIndex >= navigationZoneCount) ||
        (state.trainingZoneInfoIndex >= zoneInfoCount))
    {
        return false;
    }
    for (size_t index = 0; index < state.specificZoneCount; ++index)
    {
        if (state.specificNavigationZoneIndices[index] >= navigationZoneCount)
        {
            return false;
        }
    }
    return true;
}

bool CGangZoneWarSyncManager::HasSameZonePayload(const GangZoneState& left, const GangZoneState& right)
{
    if (left.authorityPlayerId != right.authorityPlayerId || left.zoneInfoCount != right.zoneInfoCount)
    {
        return false;
    }
    for (size_t zoneIndex = 0; zoneIndex < left.zoneInfoCount; ++zoneIndex)
    {
        if (left.gangDensities[zoneIndex] != right.gangDensities[zoneIndex])
        {
            return false;
        }
    }
    return true;
}

bool CGangZoneWarSyncManager::HasSameWarDiscretePayload(const GangWarState& left, const GangWarState& right)
{
    return left.authorityPlayerId == right.authorityPlayerId && left.lifecycle == right.lifecycle &&
           left.attackLifecycle == right.attackLifecycle && left.primaryGang == right.primaryGang &&
           left.secondaryGang == right.secondaryGang && left.warFerocity == right.warFerocity &&
           left.fightZoneInfoIndex == right.fightZoneInfoIndex &&
           left.fightNavigationZoneIndex == right.fightNavigationZoneIndex &&
           left.trainingZoneInfoIndex == right.trainingZoneInfoIndex &&
           left.gangWarsActive == right.gangWarsActive && left.trainingMission == right.trainingMission &&
           left.canTriggerWhenOnMission == right.canTriggerWhenOnMission &&
           left.playerIsOnMission == right.playerIsOnMission &&
           left.specificZoneCount == right.specificZoneCount &&
           left.specificNavigationZoneIndices == right.specificNavigationZoneIndices &&
           left.gangRatings == right.gangRatings && left.gangRatingStrength == right.gangRatingStrength &&
           left.difficulty == right.difficulty && left.territoryControl == right.territoryControl &&
           left.warStartPosition == right.warStartPosition && left.pointOfAttack == right.pointOfAttack;
}

uint32_t CGangZoneWarSyncManager::NextNonZeroRevision(uint32_t& revision)
{
    do
    {
        ++revision;
    } while (revision == 0);
    return revision;
}
