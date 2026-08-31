#include "stdafx.h"
#include "CNetworkStaticBlip.h"
#include "CEntryExit.h"
#include <CEntryExitManager.h>

namespace
{
constexpr uint32_t HOST_SNAPSHOT_INTERVAL_MS = 1000;
constexpr uint32_t CLIENT_VALIDATION_INTERVAL_MS = 500;

bool IsMatchingTrace(const tRadarTrace& trace, const Packets::Blips::_StaticBlipPayload& expected)
{
    const eBlipType expectedType = expected.type ? BLIP_COORD : BLIP_CONTACTPOINT;
    if (!trace.m_bInUse || trace.m_nBlipType != expectedType || trace.m_nRadarSprite != expected.sprite ||
        trace.m_nBlipDisplay != expected.display || trace.m_bShortRange != expected.shortRange ||
        trace.m_bFriendly != expected.friendly ||
        trace.m_nCoordBlipAppearance != expected.coordBlipAppearance || trace.m_nBlipSize != expected.size ||
        trace.m_nColour != expected.color)
    {
        return false;
    }

    constexpr float POSITION_EPSILON_SQUARED = 1.0f;
    const CVector delta = trace.m_vecPos - expected.position;
    return delta.x * delta.x + delta.y * delta.y + delta.z * delta.z <= POSITION_EPSILON_SQUARED;
}

bool SnapshotMatchesRadar(const Packets::Blips::StaticBlipsSnapshot& snapshot)
{
    bool matched[MAX_RADAR_TRACES]{};
    for (size_t expectedIndex = 0; expectedIndex < snapshot.countBlips; ++expectedIndex)
    {
        bool found = false;
        for (int traceIndex = 0; traceIndex < MAX_RADAR_TRACES; ++traceIndex)
        {
            if (!matched[traceIndex] && IsMatchingTrace(
                    CRadar::ms_RadarTrace[traceIndex], snapshot.blips[expectedIndex]))
            {
                matched[traceIndex] = true;
                found = true;
                break;
            }
        }
        if (!found)
            return false;
    }
    return true;
}

void ApplySnapshot(const Packets::Blips::StaticBlipsSnapshot& packet)
{
    for (int i = 0; i < MAX_RADAR_TRACES; i++)
    {
        auto& trace = CRadar::ms_RadarTrace[i];

        if ((trace.m_nBlipType == BLIP_CONTACTPOINT || trace.m_nBlipType == BLIP_COORD) &&
            CNetworkStaticBlip::IsAllowedSyncingRadarSprite(static_cast<eRadarSprite>(trace.m_nRadarSprite)))
        {
            CRadar::ClearActualBlip(i);
        }
    }

    for (size_t i = 0; i < packet.countBlips; i++)
    {
        const Packets::Blips::_StaticBlipPayload& blipState = packet.blips[i];

        if (!CNetworkStaticBlip::IsAllowedSyncingRadarSprite(static_cast<eRadarSprite>(blipState.sprite)))
            continue;

        int blip = CRadar::SetCoordBlip(
            static_cast<eBlipType>(blipState.type ? eBlipType::BLIP_COORD : eBlipType::BLIP_CONTACTPOINT),
            blipState.position, 0, static_cast<eBlipDisplay>(blipState.display), nullptr);
        CRadar::SetBlipSprite(blip, blipState.sprite);
        CRadar::ChangeBlipDisplay(blip, static_cast<eBlipDisplay>(blipState.display));

        if (const auto index = CRadar::GetActualBlipArrayIndex(blip); index != -1)
        {
            CRadar::ms_RadarTrace[index].m_bShortRange = blipState.shortRange;
            CRadar::ms_RadarTrace[index].m_bFriendly = blipState.friendly;
            CRadar::ms_RadarTrace[index].m_nCoordBlipAppearance = blipState.coordBlipAppearance;
            CRadar::ms_RadarTrace[index].m_nBlipSize = blipState.size;
            CRadar::ms_RadarTrace[index].m_nColour = blipState.color;
        }
    }
}
}

void CNetworkStaticBlip::Create(const Packets::Blips::StaticBlipsSnapshot& packet)
{
    ms_lastAuthoritativeSnapshot = packet;
    ms_lastAuthoritativeSnapshot.serverTime = 0;
    ms_bHasAuthoritativeSnapshot = true;
    ApplySnapshot(ms_lastAuthoritativeSnapshot);
}

void CNetworkStaticBlip::Send()
{
    Packets::Blips::StaticBlipsSnapshot packet{};
    packet.countBlips = 0;
    for (int i = 0; i < MAX_RADAR_TRACES; i++)
    {
        auto& trace = CRadar::ms_RadarTrace[i];

        if ((trace.m_nBlipType != eBlipType::BLIP_CONTACTPOINT && trace.m_nBlipType != eBlipType::BLIP_COORD) ||
            !IsAllowedSyncingRadarSprite(static_cast<eRadarSprite>(trace.m_nRadarSprite)))
        {
            continue;
        }

        Packets::Blips::_StaticBlipPayload& blipState = packet.blips[packet.countBlips];

        if (trace.m_pEntryExit)
        {
            auto& rect = trace.m_pEntryExit->m_recEntrance;
            blipState.position = CVector(
                (rect.right + rect.left) * 0.5f, (rect.bottom + rect.top) * 0.5f, trace.m_pEntryExit->m_fEntranceZ);
        }
        else
        {
            blipState.position = trace.m_vecPos;
        }

        blipState.display = trace.m_nBlipDisplay;
        blipState.sprite = trace.m_nRadarSprite;
        blipState.type = trace.m_nBlipType == eBlipType::BLIP_COORD;
        blipState.trackingBlip = trace.m_bInUse;
        blipState.shortRange = trace.m_bShortRange;
        blipState.friendly = trace.m_bFriendly;
        blipState.coordBlipAppearance = trace.m_nCoordBlipAppearance;
        blipState.size = trace.m_nBlipSize;
        blipState.color = trace.m_nColour;

        ++packet.countBlips;
    }
    GetPacketFactory().Send(packet);
    ms_bNeedToSendAfterThisFrame = false;
    ms_nLastHostSyncAt = GetTickCount();
}

void CNetworkStaticBlip::Process()
{
    if (!CNetwork::m_bAuthenticated)
        return;

    const uint32_t now = GetTickCount();
    if (CLocalPlayer::m_bIsHost)
    {
        if (ms_bNeedToSendAfterThisFrame || now - ms_nLastHostSyncAt >= HOST_SNAPSHOT_INTERVAL_MS)
            Send();
        return;
    }

    if (!ms_bHasAuthoritativeSnapshot || now - ms_nLastValidationAt < CLIENT_VALIDATION_INTERVAL_MS)
        return;
    ms_nLastValidationAt = now;
    if (!SnapshotMatchesRadar(ms_lastAuthoritativeSnapshot))
        ApplySnapshot(ms_lastAuthoritativeSnapshot);
}

void CNetworkStaticBlip::Reset()
{
    ms_bNeedToSendAfterThisFrame = true;
    ms_bHasAuthoritativeSnapshot = false;
    ms_nLastHostSyncAt = 0;
    ms_nLastValidationAt = 0;
    ms_lastAuthoritativeSnapshot = {};
}
