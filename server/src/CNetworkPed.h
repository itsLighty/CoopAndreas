#pragma once

#include "CVector.h"
#include "network/packets/peds.h"

#include <eModelID.h>
#include <ePedType.h>

class CNetworkPlayer;

class CNetworkPed
{
public:
    CNetworkPed(
        int pedid, CNetworkPlayer* syncer, eModelID modelId, ePedType pedType, CVector pos, eCharCreatedBy createdBy);

    int m_nPedId;
    CNetworkPlayer* m_pSyncer;
    eModelID m_nModelId;
    ePedType m_nPedType;
    CVector m_vecPos;
    eCharCreatedBy m_nCreatedBy;
    char m_szSpecialModelName[8];
    int m_nVehicleId = -1;
    Packets::Peds::SPedTaskSnapshot m_taskSnapshot{};
    bool m_bTaskSnapshotInitialized = false;
    uint16_t m_nTaskRevision = 0;
    Packets::Peds::SPedGroupMembershipSnapshot m_groupSnapshot{};
    bool m_bGroupSnapshotInitialized = false;
    uint16_t m_nGroupRevision = 0;
};
