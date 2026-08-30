#pragma once

#include "../CCustomCommand.h"

class CCommandLaunchMissionForCoop : public CCustomCommand
{
    void Process(CRunningScript* script) override;
};
