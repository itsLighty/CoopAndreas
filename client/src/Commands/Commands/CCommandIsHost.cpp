#include "stdafx.h"
#include "CCommandIsHost.h"
#include "CNetwork.h"

void CCommandIsHost::Process(CRunningScript* script)
{
	// Offline scripts are necessarily authoritative. Once connected, the
	// server-assigned host flag is the only source of script authority.
	script->UpdateCompareFlag(!CNetwork::m_bAuthenticated || CLocalPlayer::m_bIsHost);
}
