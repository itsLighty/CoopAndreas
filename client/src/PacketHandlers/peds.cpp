#include "network/packets/peds.h"
#include "network/packet_types.h"
#include "CNetworkPedGroupSyncManager.h"
#include "stdafx.h"
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedInAsPassenger.h>

PACKET_HANDLER(ePacketType::PED_SPAWN, Packets::Peds::PedSpawn* pPedSpawn)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED SPAWN %d %d %.1f %.1f %.1f %d %d", packet->pedid, packet->modelId, packet->pos.x,
        packet->pos.y, packet->pos.z, packet->pedType, packet->createdBy);
#endif

    CNetworkPed* pNetworkPed = new CNetworkPed(pPedSpawn->pedid, pPedSpawn->modelId, pPedSpawn->pedType, pPedSpawn->pos,
        pPedSpawn->createdBy, pPedSpawn->specialModelName);

    CNetworkPedManager::Add(pNetworkPed);
}

PACKET_HANDLER(ePacketType::PED_CONFIRM, Packets::Peds::PedConfirm* pPedConfirm)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED CONFIRM %d %d", packet->pedid, packet->tempid);
#endif

    if (pPedConfirm->tempid < ARRAY_SIZE(CNetworkPedManager::m_apTempPeds))
    {
        CNetworkPed* pTempPed = CNetworkPedManager::m_apTempPeds[pPedConfirm->tempid];
        if (pTempPed)
        {
            pTempPed->m_nPedId = pPedConfirm->pedid;
            CNetworkPedManager::Add(pTempPed);
            CNetworkPedManager::m_apTempPeds[pPedConfirm->tempid] = nullptr;
        }
    }
}

PACKET_HANDLER(ePacketType::PED_REMOVE, Packets::Peds::PedRemove* pPedRemove)
{
#ifdef PACKET_DEBUG_MESSAGES
    CChat::AddMessage("PED REMOVE %d", pPedRemove->pedid);
#endif

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedRemove->pedid);
    if (pNetworkPed)
    {
        CNetworkPedManager::Remove(pNetworkPed);
        delete pNetworkPed;
    }
}

PACKET_HANDLER(ePacketType::ASSIGN_PED, Packets::Peds::AssignPedSyncer* pAssignPedSyncer)
{
    if (!pAssignPedSyncer->toggleOwnership)
    {
        CNetworkPedGroupSyncManager::ObserveRemoteMembership(pAssignPedSyncer->pedid, pAssignPedSyncer->group);
        return;
    }

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pAssignPedSyncer->pedid);

    if (!pNetworkPed)
    {
        return;
    }

    CNetworkPedGroupSyncManager::OnPedRemoved(pAssignPedSyncer->pedid);

    if (pNetworkPed->m_bSyncing)
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("NOT SYNCING PED %d ANYMORE", pAssignPedSyncer->pedid);
#endif
        pNetworkPed->ResetRemoteSyncState(false);
        pNetworkPed->m_bSyncing = false;

        if (auto pPed = pNetworkPed->m_pPed)
        {
            pPed->SetCharCreatedBy(MISSION_CHAR);
        }
    }
    else
    {
#ifdef PACKET_DEBUG_MESSAGES
        CChat::AddMessage("SYNCING VEHICLE %d", pAssignPedSyncer->pedid);
#endif
        pNetworkPed->ResetRemoteSyncState(false);
        pNetworkPed->m_bSyncing = true;
        pNetworkPed->m_bClaimOnRelease = false;

        if (auto pPed = pNetworkPed->m_pPed)
        {
            pPed->SetCharCreatedBy(pNetworkPed->m_nCreatedBy);
        }
    }
}

PACKET_HANDLER(ePacketType::PED_ONFOOT, Packets::Peds::PedOnFoot* pPedOnFoot)
{
    if (!pPedOnFoot->HasValidAimState() || !pPedOnFoot->group.HasValidSemantics() ||
        !pPedOnFoot->group.FitsSerializedBudget() || !pPedOnFoot->task.HasValidSemantics())
        return;

    // Cache membership before resolving the ped. PED_SPAWN and PED_ONFOOT use different ENet channels, so a
    // valid canonical update can arrive first and must remain pending until both member and leader stream in.
    CNetworkPedGroupSyncManager::ObserveRemoteMembership(pPedOnFoot->pedid, pPedOnFoot->group);

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedOnFoot->pedid);

    if (!pNetworkPed)
    {
        return;
    }

    CPed* pPed = pNetworkPed->m_pPed;
    if (!pPed)
    {
        return;
    }

    CVehicle* pVehicle = pPed->m_pVehicle;
    if (pVehicle && pPed->m_nPedFlags.bInVehicle && pVehicle->IsVTableValid())
    {
        // plugin::Command<Commands::TASK_LEAVE_CAR>(CPools::GetPedRef(ped->m_pPed),
        // CPools::GetVehicleRef(ped->m_pPed->m_pVehicle));
        // plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(ped->m_pPed), packet->pos.x,
        // packet->pos.y, packet->pos.z);
        pNetworkPed->RemoveFromVehicle(pVehicle);
    }

    pNetworkPed->ApplyWeaponSnapshot(pPedOnFoot->weaponSnapshot);
    pPed->SetPosn(pPedOnFoot->pos);

    pNetworkPed->m_fCurrentRotation = pPed->m_fCurrentRotation = pPedOnFoot->currentRotation.m_angle;
    pNetworkPed->m_fAimingRotation = pPed->m_fAimingRotation = pPedOnFoot->aimingRotation.m_angle;
    pNetworkPed->m_fLookDirection = pPed->m_fLookDirection = pPedOnFoot->lookDirection.m_angle;

    pNetworkPed->m_fHealth = pPed->m_fHealth = pPedOnFoot->healthSnapshot.iHealth;
    pPed->m_fArmour = pPedOnFoot->healthSnapshot.iArmour;

    pNetworkPed->m_vecVelocity = pPedOnFoot->velocity;
    pNetworkPed->m_nMoveState = pPedOnFoot->moveState;

    if (CUtil::IsDucked(pPed) != pPedOnFoot->bDucked)
    {
        CTaskSimpleDuckToggle task = CTaskSimpleDuckToggle(pPedOnFoot->bDucked);
        task.ProcessPed(pPed);
    }

    if (pPed->m_fHealth <= 0.0f)
    {
        pNetworkPed->ResetRemoteSyncState(true);
    }
    else
    {
        pNetworkPed->ApplyAimSnapshot(pPedOnFoot->bAiming, pPedOnFoot->weaponAim);
        pNetworkPed->ApplyTaskSnapshot(pPedOnFoot->task);
    }

    pPed->m_nFightingStyle = pPedOnFoot->fightingStyle;
}

PACKET_HANDLER(ePacketType::PED_DRIVER_UPDATE, Packets::Peds::PedDriverUpdate* pPedDriverUpdate)
{
    CNetworkPedGroupSyncManager::ObserveRemoteMembership(pPedDriverUpdate->pedid, pPedDriverUpdate->group);

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedDriverUpdate->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
    if (pVehicle == nullptr || !pVehicle->IsVTableValid())
    {
        return;
    }

    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    CPed* pPed = pNetworkPed->m_pPed;
    if (pPed == nullptr || !pPed->IsVTableValid())
    {
        return;
    }

    if (pPed->m_pVehicle != pVehicle || !pPed->m_nPedFlags.bInVehicle)
    {
        pNetworkPed->WarpIntoVehicleDriver(pVehicle);
    }
    pNetworkPed->ClearRemoteAim();
    pNetworkPed->ClearRemoteTask();
    pVehicle->m_matrix->pos = pPedDriverUpdate->pos;
    pVehicle->m_matrix->right = pPedDriverUpdate->roll;
    pVehicle->m_matrix->up = pPedDriverUpdate->rot;
    pNetworkPed->m_vecVelocity = pPedDriverUpdate->velocity;
    pVehicle->m_vecMoveSpeed = pPedDriverUpdate->velocity;
    pVehicle->m_vecTurnSpeed = pPedDriverUpdate->turnSpeed;

    pNetworkPed->ApplyWeaponSnapshot(pPedDriverUpdate->pedWeapon);

    pNetworkPed->m_fHealth = pPed->m_fHealth = pPedDriverUpdate->pedHealth.iHealth;
    pPed->m_fArmour = pPedDriverUpdate->pedHealth.iArmour;

    pVehicle->m_nPrimaryColor = pPedDriverUpdate->color1;
    pVehicle->m_nSecondaryColor = pPedDriverUpdate->color2;

    pVehicle->m_fHealth = pPedDriverUpdate->health;

    if (pNetworkVehicle->m_nPaintJob != pPedDriverUpdate->paintjob)
    {
        pVehicle->SetRemap(pPedDriverUpdate->paintjob);
    }

    if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
    {
        CBike* pBike = (CBike*)pVehicle;
        pBike->m_rideAnimData.m_fDesiredLeanAngle = pPedDriverUpdate->bikeLean;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
    {
        CPlane* pPlane = (CPlane*)pVehicle;
        pPlane->m_fLandingGearStatus = pPedDriverUpdate->planeGearState;
    }
    if (pVehicle->m_nVehicleSubType == VEHICLE_BMX)
    {
        CBmx* pBmx = (CBmx*)pVehicle;
        pBmx->m_fControlPedaling = pPedDriverUpdate->controlPedaling;
    }
    if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
    {
        ((CAutomobile*)pVehicle)->m_wMiscComponentAngle = pPedDriverUpdate->miscComponentAngle;
    }
    pVehicle->m_eDoorLock = pPedDriverUpdate->locked;

    pVehicle->m_fGasPedal = pNetworkPed->m_fGasPedal = pPedDriverUpdate->gasPedal;
    pVehicle->m_fBreakPedal = pNetworkPed->m_fBreakPedal = pPedDriverUpdate->breakPedal;
    pVehicle->m_fSteerAngle = pNetworkPed->m_fSteerAngle = pPedDriverUpdate->steerAngle;
    pNetworkPed->ApplyDriverSignals(pVehicle, pPedDriverUpdate->bHorn, pPedDriverUpdate->bSiren);

    if (pPed->m_fHealth <= 0.0f)
        pNetworkPed->ResetRemoteSyncState(true);
}

PACKET_HANDLER(ePacketType::PED_PASSENGER_UPDATE, Packets::Peds::PedPassengerSync* pPedPassengerSync)
{
    CNetworkPedGroupSyncManager::ObserveRemoteMembership(pPedPassengerSync->pedid, pPedPassengerSync->group);

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid);
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);

    if (pNetworkVehicle == nullptr || pNetworkPed == nullptr)
        return;

    if (pNetworkVehicle->m_pVehicle == nullptr)
        return;

    if (pNetworkPed->m_pPed == nullptr)
        return;

    if (!pNetworkVehicle->m_pVehicle->IsVTableValid() || !pNetworkPed->m_pPed->IsVTableValid())
        return;

    if (!pNetworkPed->m_pPed->m_nPedFlags.bInVehicle || pNetworkVehicle->m_pVehicle->m_pDriver == pNetworkPed->m_pPed)
    {
        pNetworkPed->WarpIntoVehiclePassenger(pNetworkVehicle->m_pVehicle, pPedPassengerSync->seatid);
    }

    pNetworkPed->ClearDriverSignals();
    pNetworkPed->ClearRemoteAim();
    pNetworkPed->ClearRemoteTask();

    pNetworkPed->ApplyWeaponSnapshot(pPedPassengerSync->weaponSnapshot);

    pNetworkPed->m_fHealth = pNetworkPed->m_pPed->m_fHealth = pPedPassengerSync->healthSnapshot.iHealth;
    pNetworkPed->m_pPed->m_fArmour = pPedPassengerSync->healthSnapshot.iArmour;
}

PACKET_HANDLER(ePacketType::PED_SHOT_SYNC, Packets::Peds::PedShotSync* pPedShotSync)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedShotSync->pedid);

    if (pNetworkPed && pNetworkPed->m_pPed)
    {
        if (pNetworkPed->m_pPed->GetWeapon().m_eWeaponType != pPedShotSync->weaponType)
        {
            pNetworkPed->m_pPed->SetCurrentWeapon(pPedShotSync->weaponType);
        }
        pNetworkPed->m_pPed->GetWeapon().Fire(
            pNetworkPed->m_pPed, &pPedShotSync->origin, &pPedShotSync->effect, nullptr, &pPedShotSync->target, nullptr);
    }
}

PACKET_HANDLER(ePacketType::PED_SAY, Packets::Peds::PedSay* pPedSay)
{
    // CChat::AddMessage("PedSay %d %d %d %d %d", pPedSay->phraseId, pPedSay->startTimeDelay, pPedSay->overrideSilence,
    // pPedSay->isForceAudible, pPedSay->isFrontEnd);

    CPed* pPed = (CPed*)pPedSay->entity.GetEntity();
    if (pPed == nullptr || !pPed->IsVTableValid())
        return;

    const bool isPlayerCommand = pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PLAYER &&
                                 Packets::Peds::IsDeliberatePlayerVoiceCommand(pPedSay->phraseId);
    if (isPlayerCommand && (!pPed->IsPlayer() || !pPed->IsAlive() ||
        !Packets::Peds::HasStockPlayerVoiceCommandArguments(pPedSay->phraseId, pPedSay->startTimeDelay,
            pPedSay->overrideSilence, pPedSay->isForceAudible, pPedSay->isFrontEnd)))
    {
        return;
    }

    // Call the native implementation directly. Going through CPed::Say would re-enter PedHooks and echo the event.
    // The speech entity remains attached to the streamed remote player, so GTA's normal spatial audio path is used.
    plugin::CallMethodAndReturn<int16_t, 0x4E6550, CAEPedSpeechAudioEntity*, int, int16_t, uint32_t, float, bool,
        bool, bool>(&pPed->m_pedSpeech, AE_SPEECH_PED, pPedSay->phraseId, pPedSay->startTimeDelay, 1.0f,
        pPedSay->overrideSilence, pPedSay->isForceAudible, pPedSay->isFrontEnd);
}


PACKET_HANDLER(ePacketType::PED_RESET_ALL_CLAIMS, Packets::Peds::PedResetAllClaims* pPedResetAllClaims)
{
    if (auto pNetworkPed = CNetworkPedManager::GetPed(pPedResetAllClaims->pedid))
    {
        if (!pNetworkPed->m_bSyncing)
        {
            pNetworkPed->m_bClaimOnRelease = false;
        }
    }
}
