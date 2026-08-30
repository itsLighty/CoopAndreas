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
    if (modelId >= 290 && modelId <= 299)
        CStreaming::RequestSpecialModel(modelId, specialModelName, 0);
    else
        CStreaming::RequestModel(modelId, 0);

    CStreaming::LoadAllRequestedModels(false);

    if (pedType == PED_TYPE_COP)
    {
        switch (modelId) 
        {
        case MODEL_LAPDM1:
            modelId = COP_TYPE_LAPDM1;
            break;
        case MODEL_CSHER:
            modelId = COP_TYPE_CSHER;
            break;
        case MODEL_SWAT:
            modelId = COP_TYPE_SWAT1;
            break;
        case MODEL_FBI:
            modelId = COP_TYPE_FBI;
            break;
        case MODEL_ARMY:
            modelId = COP_TYPE_ARMY;
            break;
        }
    }

    if (pedType == PED_TYPE_COP)
    {
        m_pPed = new CCopPed((eCopType)modelId);
    }
    else if (pedType == PED_TYPE_MEDIC || pedType == PED_TYPE_FIREMAN)
    {
        m_pPed = new CEmergencyPed(pedType, modelId);
    }
    else
    {
        m_pPed = new CCivilianPed(pedType, modelId);
    }

    m_pPed->m_nCreatedBy = 2;
    m_pPed->m_pIntelligence->SetPedDecisionMakerType(-1);
    m_pPed->m_pIntelligence->SetSeeingRange(30.0);
    m_pPed->m_pIntelligence->SetHearingRange(30.0);
    m_pPed->m_pIntelligence->m_fDmRadius = 0.0f;
    m_pPed->m_pIntelligence->m_nDmNumPedsToScan = 0;
    
    m_pPed->SetPosn(pos);
    m_pPed->SetOrientation(0.f, 0.f, 0.f);
    CWorld::Add(m_pPed);

    m_nPedId = pedid;
    m_nPedType = pedType;
    m_bSyncing = false;
    m_nCreatedBy = createdBy;

    // THIS IS AN EXPERIMENTAL SOLUTION FOR THE 0x4D68BA CRASH
    m_pPed->m_bStreamingDontDelete = true;
}

CNetworkPed::~CNetworkPed()
{
    ResetRemoteSyncState(true);

    if (m_bSyncing)
    {
        Packets::Peds::PedRemove packet{};
        packet.pedid = m_nPedId;
        GetPacketFactory().Send(packet);
    }
    else
    {

        if (m_pPed && m_pPed->m_matrix->m_pOwner)
        {
            if (m_nBlipHandle != -1)
            {
                CRadar::ClearBlipForEntity(eBlipType::BLIP_CHAR, CPools::GetPedRef(m_pPed));
                //CChat::AddMessage("REMOVE THE FUCKING BLIP");
            }

            if (m_pPed->m_nPedFlags.bInVehicle)
            {
                plugin::Command<Commands::WARP_CHAR_FROM_CAR_TO_COORD>(CPools::GetPedRef(m_pPed), 0.f, 0.f, 0.f);
            }

            CWorld::Remove(m_pPed);
            //CWorld::RemoveReferencesToDeletedObject(m_pPed);
            m_pPed->Remove();
            delete m_pPed;
        }
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
