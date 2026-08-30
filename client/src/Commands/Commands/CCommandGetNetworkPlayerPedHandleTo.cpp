#include "stdafx.h"
#include "CCommandGetNetworkPlayerPedHandleTo.h"
#include "CMissionSessionClient.h"

namespace
{
constexpr int INVALID_SCM_CHAR_HANDLE = 0;

int GetValidNetworkPlayerPedHandle(int playerId)
{
	if (playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
	{
		return INVALID_SCM_CHAR_HANDLE;
	}

	const auto& missionSession = CMissionSessionClient::GetState();
	if (missionSession.IsActive() && !missionSession.ContainsGameplayParticipant(playerId))
	{
		return INVALID_SCM_CHAR_HANDLE;
	}

	for (CNetworkPlayer* candidate : CNetworkPlayerManager::m_pPlayers)
	{
		if (candidate == nullptr || candidate->m_iPlayerId != playerId || candidate->m_pPed == nullptr ||
			CPools::ms_pPedPool == nullptr || !CPools::ms_pPedPool->IsObjectValid(candidate->m_pPed))
		{
			continue;
		}

		const int pedHandle = CPools::GetPedRef(candidate->m_pPed);
		if (pedHandle > 0 && CPools::GetPed(pedHandle) == candidate->m_pPed)
		{
			return pedHandle;
		}
	}

	return INVALID_SCM_CHAR_HANDLE;
}
}

void CCommandGetNetworkPlayerPedHandleTo::Process(CRunningScript* script)
{
	script->CollectParameters(1);
	ScriptParams[0] = GetValidNetworkPlayerPedHandle(ScriptParams[0]);
	script->StoreParameters(1);
}
