#pragma once

#include "CVector2D.h"
#include <ePedPieceTypes.h>

enum eWeaponState : unsigned int
{
    WEAPONSTATE_READY,
    WEAPONSTATE_FIRING,
    WEAPONSTATE_RELOADING,
    WEAPONSTATE_OUT_OF_AMMO,
    WEAPONSTATE_MELEE_MADECONTACT
};
