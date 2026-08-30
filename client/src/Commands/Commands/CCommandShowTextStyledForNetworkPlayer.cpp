#include "stdafx.h"
#include "CCommandShowTextStyledForNetworkPlayer.h"
#include <CMissionSessionClient.h>

void CCommandShowTextStyledForNetworkPlayer::Process(CRunningScript* script)
{
	char gxt[8];
	script->ReadTextLabelFromScript(gxt, 8);
	gxt[7] = '\0';

	if (_strnicmp(gxt, "M_FAIL", 6) == 0)
	{
		CMissionSessionClient::ReportScmMissionResult(Packets::Scripts::eMissionSessionResult::FAILED);
	}
	else if (_strnicmp(gxt, "M_PASS", 6) == 0)
	{
		CMissionSessionClient::ReportScmMissionResult(Packets::Scripts::eMissionSessionResult::SUCCEEDED);
	}

	script->CollectParameters(3);

	auto networkPlayer = CNetworkPlayerManager::GetPlayer(CPools::GetPed(ScriptParams[2]));

	Packets::Scripts::AddMessageGXT packet{};
	strncpy(packet.gxt, gxt, 8);
	packet.forWhoPlayerId = networkPlayer->m_iPlayerId;
	packet.type = Packets::Scripts::AddMessageGXT::eGXTMsgType::sync_COMMAND_PRINT_BIG;
	packet.time = ScriptParams[0];
	packet.flag = ScriptParams[1];
	GetPacketFactory().Send(packet);
}
