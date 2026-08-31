#pragma once

#include "network/packets/blips.h"

class CNetworkStaticBlip
{
public:
	//static inline std::vector<SMissionMarker> ms_vMissionMarkers;
	static inline bool ms_bNeedToSendAfterThisFrame = true;
	static inline bool ms_bHasAuthoritativeSnapshot = false;
	static inline uint32_t ms_nLastHostSyncAt = 0;
	static inline uint32_t ms_nLastValidationAt = 0;
	static inline Packets::Blips::StaticBlipsSnapshot ms_lastAuthoritativeSnapshot{};

	static void Create(const Packets::Blips::StaticBlipsSnapshot& packet);
	static void Send();
	static void Process();
	static void Reset();

	static inline bool IsAllowedSyncingRadarSprite(eRadarSprite sprite)
	{
		return sprite == 0 || sprite == 1 || (sprite >= 5 && sprite <= 40) || (sprite >= 42 && sprite <= 55) || (sprite >= 58 && sprite <= 63);
	}
};

