#pragma once

#include <ePedType.h>

enum eMoveState
{
    PEDMOVE_NONE = 0,
    PEDMOVE_STILL,
    PEDMOVE_TURN_L,
    PEDMOVE_TURN_R,
    PEDMOVE_WALK,
    PEDMOVE_JOG,
    PEDMOVE_RUN,
    PEDMOVE_SPRINT
};
