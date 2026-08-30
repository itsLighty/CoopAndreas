#include "stdafx.h"
#include "CCommandCollectNetworkPlayersForTheMission.h"
#include "CMissionSessionClient.h"

namespace
{
constexpr int INVALID_SCM_CHAR_HANDLE = 0;

int GetValidNetworkPlayerPedHandle(CNetworkPlayer* networkPlayer)
{
	if (networkPlayer == nullptr || networkPlayer->m_pPed == nullptr || CPools::ms_pPedPool == nullptr ||
		!CPools::ms_pPedPool->IsObjectValid(networkPlayer->m_pPed))
	{
		return INVALID_SCM_CHAR_HANDLE;
	}

	const int pedHandle = CPools::GetPedRef(networkPlayer->m_pPed);
	return pedHandle > 0 && CPools::GetPed(pedHandle) == networkPlayer->m_pPed ? pedHandle :
		INVALID_SCM_CHAR_HANDLE;
}

int GetFrozenParticipantPedHandle(int playerId)
{
	if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
	{
		return INVALID_SCM_CHAR_HANDLE;
	}

	for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
	{
		if (networkPlayer != nullptr && networkPlayer->m_iPlayerId == playerId)
		{
			const int pedHandle = GetValidNetworkPlayerPedHandle(networkPlayer);
			if (pedHandle != INVALID_SCM_CHAR_HANDLE)
			{
				return pedHandle;
			}
		}
	}

	return INVALID_SCM_CHAR_HANDLE;
}
}

void CCommandCollectNetworkPlayersForTheMission::Process(CRunningScript* script)
{
	std::fill_n(ScriptParams, 3, INVALID_SCM_CHAR_HANDLE);

	const auto& missionSession = CMissionSessionClient::GetState();
	if (missionSession.IsActive())
	{
		uint8_t outputSlot = 0;
		for (uint8_t rosterIndex = 0;
			rosterIndex < missionSession.gameplayParticipantCount && outputSlot < 3;
			++rosterIndex)
		{
			const int participantId = missionSession.participantIds[rosterIndex];
			if (participantId == missionSession.hostId)
			{
				continue;
			}

			ScriptParams[outputSlot++] = GetFrozenParticipantPedHandle(participantId);
		}
	}
	else
	{
		// Legacy scripts outside an authoritative mission session retain vector order. Invalid entries still
		// consume their historical slot, but are now returned safely as an invalid SCM character handle.
		uint8_t outputSlot = 0;
		for (CNetworkPlayer* networkPlayer : CNetworkPlayerManager::m_pPlayers)
		{
			ScriptParams[outputSlot++] = GetValidNetworkPlayerPedHandle(networkPlayer);
			if (outputSlot >= 3)
			{
				break;
			}
		}
	}

	script->StoreParameters(3);
}
