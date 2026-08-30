#pragma once
class CStatsSync
{
public:
	static constexpr inline size_t SYNCED_STATS_COUNT = Packets::Players::PLAYER_SKILL_STATS_COUNT;

	static std::array<eStats, SYNCED_STATS_COUNT> m_aeSyncedStats;
	static void ApplyNetworkPlayerContext(CNetworkPlayer* player);
	static void ApplyLocalContext();
	static void Process();
	static void ResetNetworkState();
	static void NotifyChanged();
	static int GetSyncIdByInternal(eStats stat);

private:
	static bool m_bStatsDirty;
	static bool m_bSentInitialStats;
	static uint32_t m_nLastStatsSentAt;
};

