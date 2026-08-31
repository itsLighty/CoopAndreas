#include "stdafx.h"
#include "CNetworkPed.h"
#include <CTaskSimpleCarSetPedInAsPassenger.h>
#include <CCarEnterExit.h>
#include <CTaskSimpleCarSetPedOut.h>
#include <CTaskSimpleStandStill.h>
#include <CTaskComplexKillPedOnFoot.h>
#include <CTaskComplexJump.h>
#include <CTaskComplexClimb.h>
#include <Hooks/PedHooks.h>
#include "CNetworkEntityStreamManager.h"

namespace
{
bool IsNewerRevision(uint16_t candidate, uint16_t current)
{
    const uint16_t delta = static_cast<uint16_t>(candidate - current);
    return delta != 0 && delta < 0x8000;
}
}

CNetworkPed::CNetworkPed(int pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[])
{
    m_nPedId = pedid;
    m_nModelId = modelId;
    m_nPedType = pedType;
    m_bSyncing = false;
    m_nCreatedBy = createdBy;
    m_vecLogicalPosition = pos;
    m_nLogicalArea = static_cast<uint8_t>(CGame::currArea);
    if (specialModelName)
    {
        strncpy_s(m_szSpecialModelName, specialModelName, _TRUNCATE);
    }
}

CNetworkPed::~CNetworkPed()
{
    if (m_bSyncing)
    {
        Packets::Peds::PedRemove packet{};
        packet.pedid = m_nPedId;
        GetPacketFactory().Send(packet);
    }
    else
    {
        StreamOut();
    }
    CNetworkEntityStreamManager::Forget(this);
}

bool CNetworkPed::Materialize()
{
    if (m_pPed)
        return true;

    if (m_nModelId < MODEL_MALE01 || m_nModelId > MODEL_SPECIAL10 ||
        CStreaming::ms_aInfoForModel[m_nModelId].m_nLoadState != LOADSTATE_LOADED)
        return false;
    CBaseModelInfo* modelInfo = CModelInfo::ms_modelInfoPtrs[m_nModelId];
    if (!modelInfo || !modelInfo->m_pRwObject || modelInfo->m_nTxdIndex < 0 || !CTxdStore::ms_pTxdPool)
        return false;
    TxdDef* txd = CTxdStore::ms_pTxdPool->GetAt(modelInfo->m_nTxdIndex);
    if (!txd || !txd->m_pRwDictionary)
        return false;

    int constructorModel = m_nModelId;
    if (m_nPedType == PED_TYPE_COP)
    {
        switch (m_nModelId)
        {
        case MODEL_LAPDM1: constructorModel = COP_TYPE_LAPDM1; break;
        case MODEL_CSHER: constructorModel = COP_TYPE_CSHER; break;
        case MODEL_SWAT: constructorModel = COP_TYPE_SWAT1; break;
        case MODEL_FBI: constructorModel = COP_TYPE_FBI; break;
        case MODEL_ARMY: constructorModel = COP_TYPE_ARMY; break;
        }
    }

    if (m_nPedType == PED_TYPE_COP)
        m_pPed = new CCopPed(static_cast<eCopType>(constructorModel));
    else if (m_nPedType == PED_TYPE_MEDIC || m_nPedType == PED_TYPE_FIREMAN)
        m_pPed = new CEmergencyPed(m_nPedType, constructorModel);
    else
        m_pPed = new CCivilianPed(m_nPedType, constructorModel);

    if (!m_pPed)
        return false;

    m_pPed->m_nCreatedBy = m_bSyncing ? m_nCreatedBy : 2;
    m_pPed->m_pIntelligence->SetPedDecisionMakerType(-1);
    m_pPed->m_pIntelligence->SetSeeingRange(30.0f);
    m_pPed->m_pIntelligence->SetHearingRange(30.0f);
    m_pPed->m_pIntelligence->m_fDmRadius = 0.0f;
    m_pPed->m_pIntelligence->m_nDmNumPedsToScan = 0;
    m_pPed->SetPosn(GetLogicalPosition());
    m_pPed->SetOrientation(0.0f, 0.0f, 0.0f);
    m_pPed->m_nAreaCode = m_nLogicalArea;
    m_pPed->m_bStreamingDontDelete = true;
    CWorld::Add(m_pPed);
    m_nLastPresentationChangeAt = GetTickCount();
    ApplyCachedPresentation();
    CNetworkPedGroupSyncManager::OnPedAvailable(m_nPedId);
    return true;
}

void CNetworkPed::StreamOut()
{
    if (!m_pPed || m_bSyncing)
        return;

    CNetworkPedGroupSyncManager::OnPedPresentationUnavailable(m_nPedId);
    ResetRemoteSyncState(true);
    if (m_nBlipHandle != -1 && IsPedPointerValid(m_pPed))
    {
        CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(m_pPed));
        m_nBlipHandle = -1;
    }
    if (IsPedPointerValid(m_pPed) && m_pPed->IsVTableValid())
    {
        if (m_pPed->m_nPedFlags.bInVehicle)
        {
            CVehicle* oldVehicle = m_pPed->m_pVehicle;
            plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(m_pPed),
                m_vecLogicalPosition.x, m_vecLogicalPosition.y, m_vecLogicalPosition.z);
            if (oldVehicle && m_pPed->m_pVehicle == oldVehicle)
            {
                if (oldVehicle->m_pDriver == m_pPed)
                    oldVehicle->m_pDriver = nullptr;
                for (CPed*& passenger : oldVehicle->m_apPassengers)
                {
                    if (passenger == m_pPed)
                        passenger = nullptr;
                }
                m_pPed->m_pVehicle = nullptr;
                m_pPed->m_nPedFlags.bInVehicle = false;
            }
        }
        CWorld::Remove(m_pPed);
        m_pPed->Remove();
        delete m_pPed;
    }
    m_pPed = nullptr;
    m_nLastPresentationChangeAt = GetTickCount();
}

void CNetworkPed::CacheOnFootSnapshot(const Packets::Peds::PedOnFoot& snapshot)
{
    m_onFootSnapshot = snapshot;
    m_bHasOnFootSnapshot = true;
    m_bHasDriverSnapshot = false;
    m_bHasPassengerSnapshot = false;
    m_presentationMode = PresentationMode::ON_FOOT;
    m_vecLogicalPosition = snapshot.pos;
    m_fHealth = snapshot.healthSnapshot.iHealth;
    m_vecVelocity = snapshot.velocity;
    m_fCurrentRotation = snapshot.currentRotation.m_angle;
    m_fAimingRotation = snapshot.aimingRotation.m_angle;
    m_fLookDirection = snapshot.lookDirection.m_angle;
    m_nMoveState = snapshot.moveState;
}

void CNetworkPed::CacheDriverSnapshot(const Packets::Peds::PedDriverUpdate& snapshot)
{
    m_driverSnapshot = snapshot;
    m_bHasDriverSnapshot = true;
    m_bHasPassengerSnapshot = false;
    m_presentationMode = PresentationMode::DRIVER;
    m_vecLogicalPosition = snapshot.pos;
    m_fHealth = snapshot.pedHealth.iHealth;
    m_vecVelocity = snapshot.velocity;
    if (CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(snapshot.vehicleid))
        m_nLogicalArea = vehicle->m_nLogicalArea;
}

void CNetworkPed::CachePassengerSnapshot(const Packets::Peds::PedPassengerSync& snapshot)
{
    m_passengerSnapshot = snapshot;
    m_bHasPassengerSnapshot = true;
    m_bHasDriverSnapshot = false;
    m_presentationMode = PresentationMode::PASSENGER;
    m_fHealth = snapshot.healthSnapshot.iHealth;
    if (CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(snapshot.vehicleid))
    {
        m_vecLogicalPosition = vehicle->GetLogicalPosition();
        m_nLogicalArea = vehicle->m_nLogicalArea;
    }
}

CVector CNetworkPed::GetLogicalPosition() const
{
    if (m_presentationMode == PresentationMode::PASSENGER)
    {
        if (CNetworkVehicle* vehicle = CNetworkVehicleManager::GetVehicle(m_passengerSnapshot.vehicleid))
            return vehicle->GetLogicalPosition();
    }
    return m_vecLogicalPosition;
}

void CNetworkPed::ApplyCachedPresentation()
{
    if (!m_pPed || !m_pPed->IsVTableValid())
        return;

    if (m_presentationMode == PresentationMode::DRIVER && m_bHasDriverSnapshot)
    {
        CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(m_driverSnapshot.vehicleid);
        CVehicle* vehicle = networkVehicle ? networkVehicle->m_pVehicle : nullptr;
        if (!vehicle || !vehicle->IsVTableValid())
            return;
        if (!m_pPed->m_nPedFlags.bInVehicle || m_pPed->m_pVehicle != vehicle || vehicle->m_pDriver != m_pPed)
            WarpIntoVehicleDriver(vehicle);
        ClearRemoteAim();
        ClearRemoteTask();
        vehicle->m_matrix->pos = m_driverSnapshot.pos;
        vehicle->m_matrix->right = m_driverSnapshot.roll;
        vehicle->m_matrix->up = m_driverSnapshot.rot;
        vehicle->m_vecMoveSpeed = m_driverSnapshot.velocity;
        vehicle->m_vecTurnSpeed = m_driverSnapshot.turnSpeed;
        ApplyWeaponSnapshot(m_driverSnapshot.pedWeapon);
        m_pPed->m_fHealth = m_driverSnapshot.pedHealth.iHealth;
        m_pPed->m_fArmour = m_driverSnapshot.pedHealth.iArmour;
        vehicle->m_nPrimaryColor = m_driverSnapshot.color1;
        vehicle->m_nSecondaryColor = m_driverSnapshot.color2;
        vehicle->m_fHealth = m_driverSnapshot.health;
        vehicle->m_eDoorLock = m_driverSnapshot.locked;
        vehicle->m_fGasPedal = m_fGasPedal = m_driverSnapshot.gasPedal;
        vehicle->m_fBreakPedal = m_fBreakPedal = m_driverSnapshot.breakPedal;
        vehicle->m_fSteerAngle = m_fSteerAngle = m_driverSnapshot.steerAngle;
        ApplyDriverSignals(vehicle, m_driverSnapshot.bHorn, m_driverSnapshot.bSiren);
        return;
    }

    if (m_presentationMode == PresentationMode::PASSENGER && m_bHasPassengerSnapshot)
    {
        CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(m_passengerSnapshot.vehicleid);
        CVehicle* vehicle = networkVehicle ? networkVehicle->m_pVehicle : nullptr;
        if (!vehicle || !vehicle->IsVTableValid())
            return;
        const int seat = m_passengerSnapshot.seatid;
        const bool inExpectedSeat = seat >= 0 && seat < vehicle->m_nMaxPassengers &&
            m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle == vehicle &&
            vehicle->m_apPassengers[seat] == m_pPed;
        if (!inExpectedSeat)
            WarpIntoVehiclePassenger(vehicle, seat);
        ClearDriverSignals();
        ClearRemoteAim();
        ClearRemoteTask();
        ApplyWeaponSnapshot(m_passengerSnapshot.weaponSnapshot);
        m_pPed->m_fHealth = m_passengerSnapshot.healthSnapshot.iHealth;
        m_pPed->m_fArmour = m_passengerSnapshot.healthSnapshot.iArmour;
        return;
    }

    if (!m_bHasOnFootSnapshot)
        return;
    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle && m_pPed->m_pVehicle->IsVTableValid())
        RemoveFromVehicle(m_pPed->m_pVehicle);
    ApplyWeaponSnapshot(m_onFootSnapshot.weaponSnapshot);
    m_pPed->SetPosn(m_onFootSnapshot.pos);
    m_pPed->m_fCurrentRotation = m_onFootSnapshot.currentRotation.m_angle;
    m_pPed->m_fAimingRotation = m_onFootSnapshot.aimingRotation.m_angle;
    m_pPed->m_fLookDirection = m_onFootSnapshot.lookDirection.m_angle;
    m_pPed->m_fHealth = m_onFootSnapshot.healthSnapshot.iHealth;
    m_pPed->m_fArmour = m_onFootSnapshot.healthSnapshot.iArmour;
    m_pPed->m_vecMoveSpeed = m_onFootSnapshot.velocity;
    if (CUtil::IsDucked(m_pPed) != m_onFootSnapshot.bDucked)
        CTaskSimpleDuckToggle(m_onFootSnapshot.bDucked).ProcessPed(m_pPed);
    m_pPed->m_nFightingStyle = m_onFootSnapshot.fightingStyle;
    if (m_pPed->m_fHealth <= 0.0f)
        ResetRemoteSyncState(true);
    else
    {
        ApplyAimSnapshot(m_onFootSnapshot.bAiming, m_onFootSnapshot.weaponAim);
        ApplyTaskSnapshot(m_onFootSnapshot.task);
    }
}

void CNetworkPed::ProcessPendingPresentation()
{
    if (!m_pPed || m_bSyncing || !m_pPed->IsVTableValid())
        return;
    if (m_presentationMode == PresentationMode::DRIVER && m_bHasDriverSnapshot)
    {
        CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(m_driverSnapshot.vehicleid);
        CVehicle* vehicle = networkVehicle ? networkVehicle->m_pVehicle : nullptr;
        if (vehicle && vehicle->IsVTableValid() &&
            (!m_pPed->m_nPedFlags.bInVehicle || m_pPed->m_pVehicle != vehicle || vehicle->m_pDriver != m_pPed))
            WarpIntoVehicleDriver(vehicle);
        if (vehicle)
            ApplyDriverSignals(vehicle, m_driverSnapshot.bHorn, m_driverSnapshot.bSiren);
        return;
    }
    if (m_presentationMode == PresentationMode::PASSENGER && m_bHasPassengerSnapshot)
    {
        CNetworkVehicle* networkVehicle = CNetworkVehicleManager::GetVehicle(m_passengerSnapshot.vehicleid);
        CVehicle* vehicle = networkVehicle ? networkVehicle->m_pVehicle : nullptr;
        if (!vehicle || !vehicle->IsVTableValid())
            return;
        const int seat = m_passengerSnapshot.seatid;
        const bool inExpectedSeat = seat >= 0 && seat < vehicle->m_nMaxPassengers &&
            m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle == vehicle &&
            vehicle->m_apPassengers[seat] == m_pPed;
        if (!inExpectedSeat)
            WarpIntoVehiclePassenger(vehicle, seat);
        return;
    }
    if (m_bHasOnFootSnapshot && m_pPed->m_fHealth > 0.0f)
    {
        ApplyAimSnapshot(m_onFootSnapshot.bAiming, m_onFootSnapshot.weaponAim);
        ApplyTaskSnapshot(m_onFootSnapshot.task);
    }
}

CNetworkPed* CNetworkPed::CreateHosted(CPed* ped)
{
    ped->field_54C += 5000; // m_nTimeTillWeNeedThisPed

    CNetworkPed* networkPed = new CNetworkPed();

    networkPed->m_pPed = ped;
    networkPed->m_nPedId = 0;
    networkPed->m_nCreatedBy = ped->m_nCreatedBy;
    networkPed->m_bSyncing = true;
    networkPed->m_nTempId = CNetworkPedManager::AddToTempList(networkPed);

    Packets::Peds::PedSpawn packet{};
    packet.tempid = networkPed->m_nTempId;
    packet.pedid = networkPed->m_nPedId;
    packet.modelId = static_cast<eModelID>(ped->m_nModelIndex);
    packet.pos = ped->m_matrix->pos;
    packet.pedType = static_cast<ePedType>(ped->m_nPedType);
    packet.createdBy = static_cast<eCharCreatedBy>(ped->m_nCreatedBy);

    if (packet.modelId >= MODEL_SPECIAL01 && packet.modelId <= MODEL_SPECIAL10)
    {
        strcpy_s(packet.specialModelName, PedHooks::ms_aszLoadedSpecialModels[packet.modelId - MODEL_SPECIAL01]);
        packet.specialModelName[7] = '\0';
    }
    GetPacketFactory().Send(packet);

    return networkPed;
}

void CNetworkPed::WarpIntoVehicleDriver(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    auto task = CTaskSimpleCarSetPedInAsDriver(vehicle, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPed::WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    if (m_pPed->m_nPedFlags.bInVehicle && m_pPed->m_pVehicle)
    {
        RemoveFromVehicle(m_pPed->m_pVehicle);
    }

    m_pPed->m_pIntelligence->FlushImmediately(false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 1; // 1 - never
    }

    int doorId = CCarEnterExit::ComputeTargetDoorToEnterAsPassenger(vehicle, seatid);
    auto task = CTaskSimpleCarSetPedInAsPassenger(vehicle, doorId, nullptr);
    task.m_bWarpingInToCar = true;
    task.ProcessPed(m_pPed);
}

void CNetworkPed::RemoveFromVehicle(CVehicle* vehicle)
{
    assert(m_pPed != nullptr);

    if (!vehicle->IsVTableValid() || !m_pPed->IsVTableValid())
    {
        return;
    }

    ClearDriverSignals();
    ClearRemoteAim();
    ClearRemoteTask();

    m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);

    if (!m_bSyncing)
    {
        m_pPed->m_nPedFlags.CantBeKnockedOffBike = 2; // 2 - normal
    }

    auto task = CTaskSimpleCarSetPedOut(vehicle, 1, false);
    task.m_bWarpingOutOfCar = true;
    task.ProcessPed(m_pPed);

    m_pPed->m_pIntelligence->FlushImmediately(true); // create a default primary task (fix bug)
}

void CNetworkPed::ClaimOnRelease()
{
    if (m_bClaimOnRelease || m_bSyncing)
        return;

    Packets::Peds::PedClaimOnRelease packet{};
    packet.pedid = m_nPedId;
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = true;
}

void CNetworkPed::CancelClaim()
{
    if (!m_bClaimOnRelease || m_bSyncing)
        return;

    Packets::Peds::PedCancelClaim packet{};
    packet.pedid = m_nPedId;
    GetPacketFactory().Send(packet);

    m_bClaimOnRelease = false;
}

void CNetworkPed::ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot)
{
    if (m_pPed == nullptr)
    {
        return;
    }

    // TODO refactor CUtil
    CUtil::GiveWeaponByPacket(this, weaponSnapshot.iWeaponType, weaponSnapshot.nAmmo);
    m_pPed->m_aWeapons[m_pPed->m_nActiveWeaponSlot].m_nState = static_cast<eWeaponState>(weaponSnapshot.iWeaponState);
}

void CNetworkPed::CaptureTaskSnapshot(Packets::Peds::SPedTaskSnapshot& snapshot) const
{
    snapshot = {};
    if (!m_pPed || !m_pPed->m_pIntelligence || m_pPed->m_nPedFlags.bInVehicle || m_pPed->m_fHealth <= 0.0f)
        return;

    CTask* task = m_pPed->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY];
    if (!task)
        return;

    switch (task->GetTaskType())
    {
    case TASK_SIMPLE_STAND_STILL:
    {
        const auto* stand = static_cast<CTaskSimpleStandStill*>(task);
        snapshot.type = Packets::Peds::ePedTaskSyncType::STAND_STILL;
        snapshot.standTime = std::clamp(stand->m_nTime, -1, 600000);
        snapshot.standLooped = stand->m_bLooped;
        snapshot.standUseIdleStance = stand->m_bUseAnimIdleStance;
        break;
    }
    case TASK_COMPLEX_WANDER:
    {
        const auto* wander = static_cast<CTaskComplexWander*>(task);
        snapshot.type = Packets::Peds::ePedTaskSyncType::WANDER;
        snapshot.wanderMoveState = static_cast<eMoveState>(std::clamp(wander->m_nMoveState, (int)PEDMOVE_STILL,
            (int)PEDMOVE_SPRINT));
        snapshot.wanderDirection = std::min<uint8_t>(wander->m_nDir, 7);
        snapshot.wanderSensibly = wander->m_bWanderSensibly;
        snapshot.wanderRadius = std::clamp(wander->m_fTargetRadius, 0.1f, 50.0f);
        break;
    }
    case TASK_COMPLEX_KILL_PED_ON_FOOT:
    {
        const auto* kill = static_cast<CTaskComplexKillPedOnFoot*>(task);
        if (!kill->m_pTarget)
            return;

        snapshot.target.SetEntity(kill->m_pTarget);
        if ((snapshot.target.entityType != NETWORK_ENTITY_TYPE_PLAYER &&
                snapshot.target.entityType != NETWORK_ENTITY_TYPE_PED) ||
            snapshot.target.GetEntity() != kill->m_pTarget)
        {
            snapshot = {};
            return;
        }

        snapshot.type = Packets::Peds::ePedTaskSyncType::KILL_PED_ON_FOOT;
        snapshot.killTime = std::clamp(kill->m_nTime, -1, 600000);
        snapshot.killAttackFlags = static_cast<uint8_t>(std::min<unsigned int>(kill->m_nAttackFlags, 255));
        snapshot.killActionDelay = static_cast<int>(std::min<unsigned int>(kill->m_nActionDelay, 60000));
        snapshot.killActionChance = static_cast<uint8_t>(std::min<unsigned int>(kill->m_nActionChance, 100));
        snapshot.killUnknownFlag = kill->field_20 != 0;
        break;
    }
    case TASK_COMPLEX_JUMP:
    {
        const auto* jump = static_cast<CTaskComplexJump*>(task);
        snapshot.type = Packets::Peds::ePedTaskSyncType::JUMP;
        snapshot.jumpType = static_cast<uint8_t>(std::min<unsigned int>(jump->m_nType, 1));
        break;
    }
    case TASK_COMPLEX_CLIMB:
        snapshot.type = Packets::Peds::ePedTaskSyncType::CLIMB;
        break;
    default:
        break;
    }
}

void CNetworkPed::ApplyTaskSnapshot(const Packets::Peds::SPedTaskSnapshot& snapshot)
{
    if (m_bSyncing || !m_pPed || !m_pPed->m_pIntelligence || m_pPed->m_nPedFlags.bInVehicle ||
        m_pPed->m_fHealth <= 0.0f || !snapshot.HasValidSemantics())
    {
        return;
    }

    if (m_bRemoteTaskInitialized)
    {
        if (snapshot.revision == m_lastRemoteTask.revision ||
            !IsNewerRevision(snapshot.revision, m_lastRemoteTask.revision))
        {
            return;
        }
    }

    ClearRemoteTask(false);

    CTask* newTask = nullptr;
    switch (snapshot.type)
    {
    case Packets::Peds::ePedTaskSyncType::NONE:
        break;
    case Packets::Peds::ePedTaskSyncType::STAND_STILL:
        newTask = new CTaskSimpleStandStill(
            snapshot.standTime, snapshot.standLooped, snapshot.standUseIdleStance, 8.0f);
        break;
    case Packets::Peds::ePedTaskSyncType::WANDER:
    {
        auto* wander = plugin::CallAndReturn<CTaskComplexWander*, 0x673D00>(m_pPed);
        if (wander)
        {
            wander->m_nMoveState = snapshot.wanderMoveState;
            wander->m_nDir = snapshot.wanderDirection;
            wander->m_bWanderSensibly = snapshot.wanderSensibly;
            wander->m_fTargetRadius = snapshot.wanderRadius;
            newTask = wander;
        }
        break;
    }
    case Packets::Peds::ePedTaskSyncType::KILL_PED_ON_FOOT:
    {
        CNetworkEntitySerializer targetReference = snapshot.target;
        CEntity* target = targetReference.GetEntity();
        if (!target || target->m_nType != ENTITY_TYPE_PED)
            return;
        newTask = new CTaskComplexKillPedOnFoot(static_cast<CPed*>(target), snapshot.killTime,
            snapshot.killAttackFlags, snapshot.killActionDelay, snapshot.killActionChance,
            snapshot.killUnknownFlag ? 1 : 0);
        break;
    }
    case Packets::Peds::ePedTaskSyncType::JUMP:
        newTask = new CTaskComplexJump(snapshot.jumpType);
        break;
    case Packets::Peds::ePedTaskSyncType::CLIMB:
        // CTaskComplexClimb intentionally has no wire parameters: its zero-argument constructor rebuilds the
        // transient climb probe locally from the synchronized ped and world collision.
        newTask = new CTaskComplexClimb();
        break;
    }

    if (snapshot.type != Packets::Peds::ePedTaskSyncType::NONE && !newTask)
        return;

    if (newTask)
    {
        m_pPed->m_pIntelligence->m_TaskMgr.SetTask(newTask, TASK_PRIMARY_PRIMARY, false);
        m_pRemotePrimaryTask = newTask;
    }

    m_lastRemoteTask = snapshot;
    m_bRemoteTaskInitialized = true;
}

void CNetworkPed::ApplyAimSnapshot(bool aiming, const CVector& target)
{
    if (m_bSyncing || !m_pPed || !m_pPed->m_pIntelligence || !aiming || m_pPed->m_nPedFlags.bInVehicle ||
        m_pPed->m_fHealth <= 0.0f)
    {
        ClearRemoteAim();
        return;
    }

    CTask* attackTask = m_pPed->m_pIntelligence->m_TaskMgr.m_aSecondaryTasks[TASK_SECONDARY_ATTACK];
    if (!m_pRemoteAimTask || attackTask != m_pRemoteAimTask)
    {
        ClearRemoteAim();
        m_pRemoteAimTask = new CTaskSimpleUseGun(nullptr, target, 1, 1, false);
        m_pPed->m_pIntelligence->m_TaskMgr.SetTaskSecondary(m_pRemoteAimTask, TASK_SECONDARY_ATTACK);
    }

    m_pRemoteAimTask->m_pTarget = nullptr;
    m_pRemoteAimTask->m_vecTarget = target;
}

void CNetworkPed::ApplyDriverSignals(CVehicle* vehicle, bool horn, bool siren)
{
    if (!vehicle || !IsVehiclePointerValid(vehicle) || !vehicle->IsVTableValid())
    {
        ClearDriverSignals();
        return;
    }

    if (m_pRemoteSignalVehicle != vehicle)
        ClearDriverSignals();

    m_pRemoteSignalVehicle = vehicle;
    vehicle->m_nHornCounter = horn ? std::max(vehicle->m_nHornCounter, 2u) : 0u;
    vehicle->m_nVehicleFlags.bSirenOrAlarm = siren && vehicle->UsesSiren();
}

void CNetworkPed::ClearRemoteTask(bool resetRevision)
{
    if (m_pPed && m_pPed->m_pIntelligence && m_pRemotePrimaryTask &&
        m_pPed->m_pIntelligence->m_TaskMgr.m_aPrimaryTasks[TASK_PRIMARY_PRIMARY] == m_pRemotePrimaryTask)
    {
        m_pPed->m_pIntelligence->m_TaskMgr.SetTask(nullptr, TASK_PRIMARY_PRIMARY, false);
    }
    m_pRemotePrimaryTask = nullptr;
    if (resetRevision)
    {
        m_lastRemoteTask = {};
        m_bRemoteTaskInitialized = false;
    }
}

void CNetworkPed::ClearRemoteAim()
{
    if (m_pPed && m_pPed->m_pIntelligence && m_pRemoteAimTask &&
        m_pPed->m_pIntelligence->m_TaskMgr.m_aSecondaryTasks[TASK_SECONDARY_ATTACK] == m_pRemoteAimTask)
    {
        m_pPed->m_pIntelligence->m_TaskMgr.SetTaskSecondary(nullptr, TASK_SECONDARY_ATTACK);
    }
    m_pRemoteAimTask = nullptr;
}

void CNetworkPed::ClearDriverSignals()
{
    if (m_pRemoteSignalVehicle && IsVehiclePointerValid(m_pRemoteSignalVehicle) &&
        m_pRemoteSignalVehicle->IsVTableValid())
    {
        m_pRemoteSignalVehicle->m_nHornCounter = 0;
        m_pRemoteSignalVehicle->m_nVehicleFlags.bSirenOrAlarm = false;
    }
    m_pRemoteSignalVehicle = nullptr;
}

void CNetworkPed::ResetRemoteSyncState(bool abortTasks)
{
    ClearDriverSignals();
    if (abortTasks)
    {
        ClearRemoteAim();
        ClearRemoteTask();
    }
    else
    {
        m_pRemoteAimTask = nullptr;
        m_pRemotePrimaryTask = nullptr;
        m_lastRemoteTask = {};
        m_bRemoteTaskInitialized = false;
    }
}

void CNetworkPed::ValidateRemoteTaskTarget()
{
    if (!m_bSyncing && m_bRemoteTaskInitialized &&
        m_lastRemoteTask.type == Packets::Peds::ePedTaskSyncType::KILL_PED_ON_FOOT &&
        !m_lastRemoteTask.target.GetEntity())
    {
        ClearRemoteTask(false);
    }
}
