#include "stdafx.h"
#include "CCommandLaunchMissionForCoop.h"

#include "CMissionSessionClient.h"

void CCommandLaunchMissionForCoop::Process(CRunningScript* script)
{
    script->CollectParameters(1);
    CMissionSessionClient::RequestScmLaunch(ScriptParams[0]);
}
