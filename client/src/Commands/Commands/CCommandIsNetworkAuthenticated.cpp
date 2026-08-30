#include "stdafx.h"
#include "CCommandIsNetworkAuthenticated.h"
#include "CNetwork.h"

void CCommandIsNetworkAuthenticated::Process(CRunningScript* script)
{
    script->UpdateCompareFlag(CNetwork::m_bAuthenticated);
}
