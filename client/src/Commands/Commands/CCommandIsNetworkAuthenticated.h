#pragma once

#include "../CCustomCommand.h"

class CCommandIsNetworkAuthenticated : public CCustomCommand
{
    void Process(CRunningScript* script) override;
};
