#pragma once

#include "network/packets/world.h"

class CGangZoneWarSyncManager
{
public:
    // Replaces the single CGame::Process call to CGangWars::Update. Offline it calls the stock function unchanged;
    // online only the authenticated host advances the lifecycle and creates attack-wave entities.
    static void ProcessGangWars();
    static void HandleZoneState(const Packets::World::GangZoneState& state);
    static void HandleWarState(const Packets::World::GangWarState& state);
    static void HandleAuthorityChanged(int authorityPlayerId, bool localPlayerIsAuthority);
    static void ResetNetworkState();

private:
    static Packets::World::GangZoneState m_LastSentZoneState;
    static Packets::World::GangWarState m_LastSentWarState;
    static Packets::World::GangZoneState m_AppliedZoneState;
    static Packets::World::GangWarState m_AppliedWarState;
    static Packets::World::GangZoneState m_PendingZoneState;
    static Packets::World::GangWarState m_PendingWarState;
    static uint32_t m_nNextZoneRevision;
    static uint32_t m_nNextWarRevision;
    static uint32_t m_nLastZonePublishTime;
    static uint32_t m_nLastWarPublishTime;
    static uint32_t m_nWarStateAppliedAt;
    static int m_nExpectedAuthorityPlayerId;
    static int m_nReplicatedRadarBlip;
    static bool m_bLocalPlayerIsAuthority;
    static bool m_bHasLastSentZoneState;
    static bool m_bHasLastSentWarState;
    static bool m_bHasAppliedZoneState;
    static bool m_bHasAppliedWarState;
    static bool m_bHasPendingZoneState;
    static bool m_bHasPendingWarState;

    static bool CaptureZoneState(Packets::World::GangZoneState& state);
    static bool CaptureWarState(Packets::World::GangWarState& state);
    static void PublishAuthoritativeState();
    static bool ApplyPendingState();
    static void ApplyAuthoritativeState(bool forceRadarRefresh);
    static void UpdateReplicatedRadarBlip(const Packets::World::GangWarState& state, uint32_t displayedFightTimerMs);
    static void ClearReplicatedRadarBlip();
    static bool CanAcceptAuthority(uint8_t authorityPlayerId);
    static bool CanApplyWarState(const Packets::World::GangWarState& state);
    static bool HasSameZonePayload(
        const Packets::World::GangZoneState& left, const Packets::World::GangZoneState& right);
    static bool HasSameWarDiscretePayload(
        const Packets::World::GangWarState& left, const Packets::World::GangWarState& right);
    static uint32_t NextNonZeroRevision(uint32_t& revision);
};
