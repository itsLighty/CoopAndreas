#include "network/packets/peds.h"
#include "network/packet_types.h"
#include "stdafx.h"

namespace
{
bool HasValidTaskTarget(const CNetworkPed* owner, const Packets::Peds::SPedTaskSnapshot& task)
{
    if (task.type != Packets::Peds::ePedTaskSyncType::KILL_PED_ON_FOOT)
        return true;

    if (task.target.entityType == NETWORK_ENTITY_TYPE_PLAYER)
        return CNetworkPlayerManager::GetPlayer(task.target.entityId) != nullptr;
    if (task.target.entityType == NETWORK_ENTITY_TYPE_PED)
    {
        CNetworkPed* target = CNetworkPedManager::GetPed(task.target.entityId);
        return target != nullptr && target != owner;
    }
    return false;
}

bool CanAcceptGroupMembership(const CNetworkPed* member, const CNetworkPlayer* owner,
    const Packets::Peds::SPedGroupMembershipSnapshot& group, uint8_t memberHealth)
{
    if (!group.hasGroup)
        return true;

    if (!member || !owner || memberHealth == 0 || member->m_pSyncer != owner || !owner->m_pPeer ||
        CNetworkPlayerManager::GetPlayer(owner->m_iPlayerId) != owner ||
        group.leaderPlayerId.value != owner->m_iPlayerId ||
        group.followerSlot >= Packets::Peds::SPedGroupMembershipSnapshot::MAX_FOLLOWERS ||
        group.followerSlot == Packets::Peds::SPedGroupMembershipSnapshot::LEADER_MEMBER_SLOT)
    {
        return false;
    }

    size_t followerCount = 0;
    for (const CNetworkPed* other : CNetworkPedManager::m_pPeds)
    {
        if (other == member || !other->m_bGroupSnapshotInitialized || !other->m_groupSnapshot.hasGroup ||
            other->m_groupSnapshot.leaderPlayerId.value != owner->m_iPlayerId)
        {
            continue;
        }

        // A transferred ped can never silently remain in the old owner's group.
        if (other->m_pSyncer != owner || other->m_groupSnapshot.followerSlot == group.followerSlot)
            return false;

        ++followerCount;
    }

    return followerCount < Packets::Peds::SPedGroupMembershipSnapshot::MAX_FOLLOWERS;
}

bool CanonicalizeGroupSnapshot(CNetworkPed* ped, CNetworkPlayer* owner,
    Packets::Peds::SPedGroupMembershipSnapshot& incoming, uint8_t memberHealth)
{
    incoming.revision = 0;
    if (memberHealth == 0)
    {
        incoming.hasGroup = false;
        incoming.leaderPlayerId.value = 0;
        incoming.followerSlot = 0;
    }
    else if (incoming.hasGroup)
    {
        // The C2S wire omits leaderPlayerId. Membership is always bound to the authenticated ped owner.
        incoming.leaderPlayerId.value = owner->m_iPlayerId;
    }
    else
    {
        incoming.leaderPlayerId.value = 0;
        incoming.followerSlot = 0;
    }

    if (!CanAcceptGroupMembership(ped, owner, incoming, memberHealth))
        return false;

    if (!ped->m_bGroupSnapshotInitialized || !incoming.HasSameMembership(ped->m_groupSnapshot))
    {
        incoming.revision = CNetworkPedManager::AdvanceGroupRevision(ped);
        ped->m_groupSnapshot = incoming;
        ped->m_bGroupSnapshotInitialized = true;
    }
    else
    {
        incoming.revision = ped->m_nGroupRevision;
    }

    return true;
}

void CanonicalizeTaskSnapshot(CNetworkPed* ped, Packets::Peds::SPedTaskSnapshot& incoming)
{
    incoming.revision = 0;
    if (!ped->m_bTaskSnapshotInitialized || !incoming.HasSamePayload(ped->m_taskSnapshot))
    {
        ++ped->m_nTaskRevision;
        if (ped->m_nTaskRevision == 0)
            ++ped->m_nTaskRevision;
        incoming.revision = ped->m_nTaskRevision;
        ped->m_taskSnapshot = incoming;
        ped->m_bTaskSnapshotInitialized = true;
    }
    else
    {
        incoming.revision = ped->m_nTaskRevision;
    }
}

bool CanOwnPedDriverVehicle(CNetworkPed* ped, CNetworkVehicle* vehicle, CNetworkPlayer* player)
{
    if (!ped || !vehicle || vehicle->m_pSyncer != player || vehicle->m_pPlayers[0] != nullptr)
        return false;

    for (auto* other : CNetworkPedManager::m_pPeds)
    {
        if (other != ped && other->m_nVehicleId == vehicle->m_nVehicleId)
            return false;
    }
    return true;
}
}

PACKET_HANDLER(ePacketType::PED_SPAWN, Packets::Peds::PedSpawn* pPedSpawn, CNetworkPlayer* pNetworkPlayer)
{
    static char allowedSpecialActors[52][8] = {"ANDRE", "BBTHIN", "BB", "CAT", "CESAR", "COPGRL1", "COPGRL2", "COPGRL3",
        "CLAUDE", "CROGRL1", "CROGRL2", "CROGRL3", "DWAYNE", "EMMET", "FORELLI", "GANGRL1", "GANGRL2", "GANGRL3",
        "GUNGRL1", "GUNGRL2", "GUNGRL3", "HERN", "JANITOR", "JETHRO", "JIZZY", "KENDL", "MACCER", "MADDOGG", "MECGRL1",
        "MECGRL2", "MECGRL3", "NURGRL1", "NURGRL2", "NURGRL3", "OGLOC", "PAUL", "PULASKI", "ROSE", "RYDER1", "RYDER2",
        "RYDER3", "SINDACO", "SMOKE", "SMOKEV", "SUZIE", "SWEET", "TBONE", "TENPEN", "TORINO", "TRUTH", "WUZIMU",
        "ZERO"};

    if (pPedSpawn->modelId >= MODEL_SPECIAL01 && pPedSpawn->modelId <= MODEL_SPECIAL10)
    {
        bool isSpecialModelValid = false;

        for (int i = 0; i < ARRAY_SIZE(allowedSpecialActors); i++)
        {
            if (strncasecmp(pPedSpawn->specialModelName, allowedSpecialActors[i], strlen(allowedSpecialActors[i])))
            {
                isSpecialModelValid = true;
                break;
            }
        }

        if (!isSpecialModelValid)
            return;
    }

    pPedSpawn->pedid = CNetworkPedManager::GetFreeId();
    GetPacketFactory().SendToAll(*pPedSpawn, pNetworkPlayer);

    CNetworkPed* pNetworkPed = new CNetworkPed(
        pPedSpawn->pedid, pNetworkPlayer, pPedSpawn->modelId, pPedSpawn->pedType, pPedSpawn->pos, pPedSpawn->createdBy);
    snprintf(pNetworkPed->m_szSpecialModelName, sizeof(pNetworkPed->m_szSpecialModelName), "%s",
        pPedSpawn->specialModelName);

    CNetworkPedManager::Add(pNetworkPed);

    // send it back to the syncer of the ped so that he knows the id
    Packets::Peds::PedConfirm pedConfirmPacket{};
    pedConfirmPacket.tempid = pPedSpawn->tempid;
    pedConfirmPacket.pedid = pPedSpawn->pedid;
    GetPacketFactory().Send(pedConfirmPacket, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_REMOVE, Packets::Peds::PedRemove* pPedRemove, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedRemove->pedid);

    if (!pNetworkPed)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to remove someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    for (auto p : CNetworkPlayerManager::m_pPlayers)
    {
        // check if this player has claimed the ped
        if (std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed) != p->m_vPedClaims.end())
        {
            // assign ped's syncer to this player
            CNetworkPedManager::ClearGroupMembership(pNetworkPed);
            pNetworkPed->m_pSyncer = p;

            if (auto* vehicle = CNetworkVehicleManager::GetVehicle(pNetworkPed->m_nVehicleId))
                vehicle->ReassignSyncer(p);

            // send ASSIGN_PED packet to the new host player to notify them they now own the ped
            Packets::Peds::AssignPedSyncer assignPedPacket{};
            assignPedPacket.pedid = pNetworkPed->m_nPedId;
            GetPacketFactory().Send(assignPedPacket, p);

            // send PED_SPAWN packet to the old syncer (peer) so it can respawn the ped locally
            Packets::Peds::PedSpawn pedSpawnPacket{};
            pedSpawnPacket.pedid = pNetworkPed->m_nPedId;
            pedSpawnPacket.modelId = pNetworkPed->m_nModelId;
            pedSpawnPacket.pedType = pNetworkPed->m_nPedType;
            pedSpawnPacket.pos = pNetworkPed->m_vecPos;
            snprintf(pedSpawnPacket.specialModelName, sizeof(pedSpawnPacket.specialModelName), "%s",
                pNetworkPed->m_szSpecialModelName);
            pedSpawnPacket.tempid = 0xFF;
            pedSpawnPacket.createdBy = pNetworkPed->m_nCreatedBy;
            GetPacketFactory().Send(pedSpawnPacket, pNetworkPlayer);

            // remove the ped from this player's claim list to avoid duplicate claims
            auto it = std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed);
            if (it != p->m_vPedClaims.end())
            {
                p->m_vPedClaims.erase(it);
            }

            // exit after assigning new host and respawning ped
            return;
        }
    }

    // if nobody claimed the ped, remove it from all players' claim lists
    for (auto p : CNetworkPlayerManager::m_pPlayers)
    {
        auto it = std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed);
        if (it != p->m_vPedClaims.end())
        {
            p->m_vPedClaims.erase(it);
        }
    }

    GetPacketFactory().SendToAll(*pPedRemove, pNetworkPlayer);
    CNetworkPedManager::Remove(pNetworkPed);
}

PACKET_HANDLER(ePacketType::PED_ONFOOT, Packets::Peds::PedOnFoot* pPedOnFoot, CNetworkPlayer* pNetworkPlayer)
{
    if (!pPedOnFoot->HasValidAimState() || !pPedOnFoot->group.HasValidSemantics() ||
        !pPedOnFoot->group.FitsSerializedBudget() || !pPedOnFoot->task.HasValidSemantics() ||
        !pPedOnFoot->task.FitsSerializedBudget())
    {
        logger::warn("%s sent invalid ped aim/task state", pNetworkPlayer->GetName().c_str());
        return;
    }

    CNetworkPed* pPed = CNetworkPedManager::GetPed(pPedOnFoot->pedid);
    if (pPed)
    {
        if (pPed->m_pSyncer != pNetworkPlayer)
        {
            logger::warn("%s tries to update (on foot) someone else's ped", pNetworkPlayer->GetName().c_str());
            return;
        }

        if (!HasValidTaskTarget(pPed, pPedOnFoot->task))
        {
            logger::warn("%s sent a missing or self-referential ped task target",
                pNetworkPlayer->GetName().c_str());
            return;
        }

        if (!CanonicalizeGroupSnapshot(
                pPed, pNetworkPlayer, pPedOnFoot->group, pPedOnFoot->healthSnapshot.iHealth))
        {
            logger::warn("%s sent invalid, duplicate, oversized, or cross-owner ped group membership",
                pNetworkPlayer->GetName().c_str());
            return;
        }

        CNetworkPedManager::ReleaseVehicleUsage(pPed);
        CanonicalizeTaskSnapshot(pPed, pPedOnFoot->task);
        pPed->m_vecPos = pPedOnFoot->pos;
        GetPacketFactory().SendToAll(*pPedOnFoot, pNetworkPlayer);
    }
}

PACKET_HANDLER(
    ePacketType::PED_DRIVER_UPDATE, Packets::Peds::PedDriverUpdate* pPedDriverUpdate, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedDriverUpdate->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to update (driver) someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedDriverUpdate->vehicleid);
    if (pNetworkVehicle == nullptr)
    {
        return;
    }

    if (!CanOwnPedDriverVehicle(pNetworkPed, pNetworkVehicle, pNetworkPlayer))
    {
        logger::warn("%s sent a ped driver update for an unowned or occupied vehicle",
            pNetworkPlayer->GetName().c_str());
        return;
    }

    if (pPedDriverUpdate->bSiren && !Packets::Peds::IsSirenCapableVehicleModel(pNetworkVehicle->m_nModelId))
    {
        logger::warn("%s enabled a siren on an unsupported vehicle", pNetworkPlayer->GetName().c_str());
        return;
    }

    if (!CanonicalizeGroupSnapshot(
            pNetworkPed, pNetworkPlayer, pPedDriverUpdate->group, pPedDriverUpdate->pedHealth.iHealth))
    {
        logger::warn("%s sent invalid ped-driver group membership", pNetworkPlayer->GetName().c_str());
        return;
    }

    if (pNetworkPed->m_nVehicleId != pNetworkVehicle->m_nVehicleId)
        CNetworkPedManager::ReleaseVehicleUsage(pNetworkPed);

    pNetworkVehicle->m_bUsedByPed = true;
    pNetworkPed->m_nVehicleId = pNetworkVehicle->m_nVehicleId;
    pNetworkVehicle->m_vecPosition = pPedDriverUpdate->pos;
    pNetworkVehicle->m_vecRotation = pPedDriverUpdate->rot;

    GetPacketFactory().SendToAll(*pPedDriverUpdate, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_PASSENGER_UPDATE, Packets::Peds::PedPassengerSync* pPedPassengerSync,
    CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedPassengerSync->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to update (passenger) someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pPedPassengerSync->vehicleid);
    if (!pNetworkVehicle)
    {
        logger::warn("%s sent a ped passenger update for an invalid vehicle", pNetworkPlayer->GetName().c_str());
        return;
    }

    if (!CanonicalizeGroupSnapshot(
            pNetworkPed, pNetworkPlayer, pPedPassengerSync->group, pPedPassengerSync->healthSnapshot.iHealth))
    {
        logger::warn("%s sent invalid ped-passenger group membership", pNetworkPlayer->GetName().c_str());
        return;
    }

    CNetworkPedManager::ReleaseVehicleUsage(pNetworkPed);

    GetPacketFactory().SendToAll(*pPedPassengerSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_SHOT_SYNC, Packets::Peds::PedShotSync* pPedShotSync, CNetworkPlayer* pNetworkPlayer)
{
    CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedShotSync->pedid);
    if (pNetworkPed == nullptr)
    {
        return;
    }

    if (pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        logger::warn("%s tries to update (shoot) someone else's ped", pNetworkPlayer->GetName().c_str());
        return;
    }

    GetPacketFactory().SendToAll(*pPedShotSync, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_SAY, Packets::Peds::PedSay* pPedSay, CNetworkPlayer* pNetworkPlayer)
{
    if (pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PLAYER)
    {
        pPedSay->entity.entityId = pNetworkPlayer->m_iPlayerId;
    }
    else if (pPedSay->entity.entityType == NETWORK_ENTITY_TYPE_PED)
    {
        CNetworkPed* pNetworkPed = CNetworkPedManager::GetPed(pPedSay->entity.entityId);
        if (pNetworkPed == nullptr)
        {
            return;
        }
    }

    GetPacketFactory().SendToAll(*pPedSay, pNetworkPlayer);
}

PACKET_HANDLER(ePacketType::PED_CLAIM_ON_RELEASE, Packets::Peds::PedClaimOnRelease* pPedClaimOnRelease,
    CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedClaimOnRelease->pedid);

    if (pNetworkPed && pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        if (std::find(pNetworkPlayer->m_vPedClaims.begin(), pNetworkPlayer->m_vPedClaims.end(), pNetworkPed) ==
            pNetworkPlayer->m_vPedClaims.end())
        {
            pNetworkPlayer->m_vPedClaims.push_back(pNetworkPed);
        }
    }
}

PACKET_HANDLER(
    ePacketType::PED_CANCEL_CLAIM, Packets::Peds::PedCancelClaim* pPedCancelClaim, CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedCancelClaim->pedid);

    if (pNetworkPed && pNetworkPed->m_pSyncer != pNetworkPlayer)
    {
        auto it = std::find(pNetworkPlayer->m_vPedClaims.begin(), pNetworkPlayer->m_vPedClaims.end(), pNetworkPed);
        if (it != pNetworkPlayer->m_vPedClaims.end())
        {
            pNetworkPlayer->m_vPedClaims.erase(it);
        }
    }
}

PACKET_HANDLER(ePacketType::PED_RESET_ALL_CLAIMS, Packets::Peds::PedResetAllClaims* pPedResetAllClaims,
    CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedResetAllClaims->pedid);

    if (pNetworkPed)
    {
        if (pNetworkPed->m_pSyncer == pNetworkPlayer || pNetworkPlayer->m_bIsHost)
        {
            if (pNetworkPlayer->m_bIsHost && pNetworkPed->m_pSyncer != nullptr)
            {
                // here was a bug that i have not understood yet

                CNetworkPedManager::ClearGroupMembership(pNetworkPed);

                Packets::Peds::AssignPedSyncer assignPedPacket{};
                assignPedPacket.pedid = pPedResetAllClaims->pedid;
                GetPacketFactory().Send(assignPedPacket, pNetworkPlayer);
                GetPacketFactory().Send(assignPedPacket, pNetworkPed->m_pSyncer);  // unassign old

                pNetworkPed->m_pSyncer = pNetworkPlayer;
                if (auto* vehicle = CNetworkVehicleManager::GetVehicle(pNetworkPed->m_nVehicleId))
                    vehicle->ReassignSyncer(pNetworkPlayer);
            }

            for (auto p : CNetworkPlayerManager::m_pPlayers)
            {
                auto it = std::find(p->m_vPedClaims.begin(), p->m_vPedClaims.end(), pNetworkPed);
                if (it != p->m_vPedClaims.end())
                {
                    GetPacketFactory().Send(*pPedResetAllClaims, p);
                    p->m_vPedClaims.erase(it);
                }
            }
        }
    }
}

PACKET_HANDLER(ePacketType::PED_TAKE_HOST, Packets::Peds::PedTakeHost* pPedTakeHost, CNetworkPlayer* pNetworkPlayer)
{
    auto pNetworkPed = CNetworkPedManager::GetPed(pPedTakeHost->pedid);

    if (pNetworkPed)
    {
        if (pNetworkPed->m_pSyncer != pNetworkPlayer && pNetworkPlayer->m_bIsHost)
        {
            CNetworkPedManager::ClearGroupMembership(pNetworkPed);

            Packets::Peds::AssignPedSyncer assignPedPacket{};
            assignPedPacket.pedid = pPedTakeHost->pedid;
            GetPacketFactory().Send(assignPedPacket, pNetworkPlayer);

            auto it = std::find(pNetworkPlayer->m_vPedClaims.begin(), pNetworkPlayer->m_vPedClaims.end(), pNetworkPed);
            if (it != pNetworkPlayer->m_vPedClaims.end())
            {
                pNetworkPlayer->m_vPedClaims.erase(it);
            }

            GetPacketFactory().Send(assignPedPacket, pNetworkPed->m_pSyncer);

            if (pPedTakeHost->allowReturnToPreviousHost)
            {
                if (std::find(pNetworkPed->m_pSyncer->m_vPedClaims.begin(), pNetworkPed->m_pSyncer->m_vPedClaims.end(), pNetworkPed) ==
                    pNetworkPed->m_pSyncer->m_vPedClaims.end())
                {
                    pNetworkPed->m_pSyncer->m_vPedClaims.push_back(pNetworkPed);
                }
            }

            pNetworkPed->m_pSyncer = pNetworkPlayer;
            if (auto* vehicle = CNetworkVehicleManager::GetVehicle(pNetworkPed->m_nVehicleId))
                vehicle->ReassignSyncer(pNetworkPlayer);
        }
    }
}
