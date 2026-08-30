#include "stdafx.h"
#include "CMissionSessionClient.h"

#include <algorithm>
#include <cmath>

int m_anStoredIntStats[224];
float m_afStoredFloatStats[83];

constexpr int MAX_INT_STATS = sizeof(m_anStoredIntStats) / sizeof(int);
constexpr int MAX_FLOAT_STATS = sizeof(m_afStoredFloatStats) / sizeof(float);
constexpr uint32_t MIN_STATS_SYNC_INTERVAL_MS = 100;

bool CStatsSync::m_bStatsDirty = true;
bool CStatsSync::m_bSentInitialStats = false;
uint32_t CStatsSync::m_nLastStatsSentAt = 0;

std::array<eStats, CStatsSync::SYNCED_STATS_COUNT> CStatsSync::m_aeSyncedStats =
{
    STAT_PISTOL_SKILL,
    STAT_SILENCED_PISTOL_SKILL,
    STAT_DESERT_EAGLE_SKILL,
    STAT_SHOTGUN_SKILL,
    STAT_SAWN_OFF_SHOTGUN_SKILL,
    STAT_COMBAT_SHOTGUN_SKILL,
    STAT_MACHINE_PISTOL_SKILL,
    STAT_SMG_SKILL,
    STAT_AK_47_SKILL,
    STAT_M4_SKILL,
    STAT_RIFLE_SKILL
};

void CStatsSync::ApplyNetworkPlayerContext(CNetworkPlayer* player)
{
    for (int i = 0; i < MAX_INT_STATS; ++i)
        m_anStoredIntStats[i] = CStats::StatTypesInt[i];

    for (int i = 0; i < MAX_FLOAT_STATS; ++i)
        m_afStoredFloatStats[i] = CStats::StatTypesFloat[i];

    for (int i = 0; i < MAX_INT_STATS; ++i)
        CStats::StatTypesInt[i] = player->m_stats.m_aStatsInt[i];

    for (int i = 0; i < MAX_FLOAT_STATS; ++i)
        CStats::StatTypesFloat[i] = player->m_stats.m_aStatsFloat[i];
}

void CStatsSync::ApplyLocalContext()
{
    for (int i = 0; i < MAX_INT_STATS; ++i)
        CStats::StatTypesInt[i] = m_anStoredIntStats[i];

    for (int i = 0; i < MAX_FLOAT_STATS; ++i)
        CStats::StatTypesFloat[i] = m_afStoredFloatStats[i];
}


void CStatsSync::NotifyChanged()
{
    m_bStatsDirty = true;
}

void CStatsSync::Process()
{
    if (!CNetwork::m_bAuthenticated || CMissionSessionClient::IsSpectator())
    {
        ResetNetworkState();
        return;
    }

    if (m_bSentInitialStats && !m_bStatsDirty)
    {
        return;
    }

    const uint32_t now = GetTickCount();
    if (m_bSentInitialStats && now - m_nLastStatsSentAt < MIN_STATS_SYNC_INTERVAL_MS)
    {
        return;
    }

    Packets::Players::PlayerStats packet{};

    for (size_t i = 0; i < CStatsSync::SYNCED_STATS_COUNT; i++)
    {
        const float statValue = CStats::GetStatValue(m_aeSyncedStats[i]);
        packet.stats[i] = std::isfinite(statValue)
            ? std::clamp(statValue, Packets::Players::PlayerStats::MIN_STAT_VALUE,
                  Packets::Players::PlayerStats::MAX_STAT_VALUE)
            : Packets::Players::PlayerStats::MIN_STAT_VALUE;
    }

    GetPacketFactory().Send(packet);
    m_bStatsDirty = false;
    m_bSentInitialStats = true;
    m_nLastStatsSentAt = now;
}

void CStatsSync::ResetNetworkState()
{
    m_bStatsDirty = true;
    m_bSentInitialStats = false;
    m_nLastStatsSentAt = 0;
}

int CStatsSync::GetSyncIdByInternal(eStats stat)
{
    for (size_t i = 0; i < CStatsSync::SYNCED_STATS_COUNT; i++)
    {
        if (m_aeSyncedStats[i] == stat)
            return static_cast<int>(i);
    }

    return -1;
}
