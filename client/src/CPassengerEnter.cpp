#include "CPassengerEnter.h"
#include "stdafx.h"
#include "CNetworkVehicle.h"

#include <CCarEnterExit.h>
#include <CTaskComplexEnterCarAsPassenger.h>

namespace
{
constexpr float MAX_PASSENGER_ENTRY_DISTANCE = 8.0f;

bool bNearPassengerDoorMessageDisplayed = false;
bool bPassengerActionHeld = false;

// TODO(v0.3.1-alpha): use internal game's functions to find the nearest vehicle, look here 0x57073E

bool IsPlayerEnteringVehicle(const CPlayerPed* pPlayerPed)
{
    static const int taskTypes[] = {701, 700, 713, 712, 718, 800};
    for (int taskType : taskTypes)
    {
        if (pPlayerPed->m_pIntelligence->m_TaskMgr.FindActiveTaskByType(taskType))
            return true;
    }

    return false;
}

bool IsAvailablePassengerVehicle(CVehicle* pVehicle)
{
    return pVehicle != nullptr && CPools::ms_pVehiclePool != nullptr &&
           CPools::ms_pVehiclePool->IsObjectValid(pVehicle) && pVehicle->m_matrix != nullptr &&
           pVehicle->m_fHealth > 0.0f && pVehicle->m_nStatus != STATUS_WRECKED &&
           !pVehicle->m_nPhysicalFlags.bDestroyed && pVehicle->m_nMaxPassengers > 0 &&
           pVehicle->m_nNumPassengers < pVehicle->m_nMaxPassengers;
}

bool TryGetAvailablePassengerSeat(CPlayerPed* pPlayerPed, CVehicle* pVehicle, int& doorId, int& seatId)
{
    if (!IsAvailablePassengerVehicle(pVehicle))
        return false;

    CVector doorPosition;
    if (!CCarEnterExit::GetNearestCarPassengerDoor(
            pPlayerPed, pVehicle, &doorPosition, &doorId, true, true, true))
        return false;

    seatId = CCarEnterExit::ComputePassengerIndexFromCarDoor(pVehicle, doorId);
    return seatId >= 0 && seatId < pVehicle->m_nMaxPassengers && seatId < 8 &&
           pVehicle->m_apPassengers[seatId] == nullptr;
}

struct PassengerSeat
{
    CNetworkVehicle* pNetworkVehicle = nullptr;
    int doorId = -1;
    int seatId = -1;
};

PassengerSeat FindNearestPassengerSeat(CPlayerPed* pPlayerPed)
{
    PassengerSeat nearestSeat;
    float nearestDistance = MAX_PASSENGER_ENTRY_DISTANCE;

    if (pPlayerPed->m_matrix == nullptr)
        return nearestSeat;

    for (CNetworkVehicle* pNetworkVehicle : CNetworkVehicleManager::m_pVehicles)
    {
        if (pNetworkVehicle == nullptr || !IsAvailablePassengerVehicle(pNetworkVehicle->m_pVehicle))
            continue;

        CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
        const float distance = (pVehicle->m_matrix->pos - pPlayerPed->m_matrix->pos).Magnitude();
        if (distance > nearestDistance)
            continue;

        int doorId = -1;
        int seatId = -1;
        if (!TryGetAvailablePassengerSeat(pPlayerPed, pVehicle, doorId, seatId))
            continue;

        nearestDistance = distance;
        nearestSeat = {pNetworkVehicle, doorId, seatId};
    }

    return nearestSeat;
}

void UpdatePassengerDoorHint(CPlayerPed* pPlayerPed)
{
    if (bNearPassengerDoorMessageDisplayed)
        return;

    if (FindNearestPassengerSeat(pPlayerPed).pNetworkVehicle != nullptr)
    {
        CHud::SetHelpMessage(
            "Press G or D-pad Up to enter the vehicle as a passenger.", false, false, false);

        bNearPassengerDoorMessageDisplayed = true;
    }
}
} // namespace

CPassengerEnter::InputSource CPassengerEnter::ConsumePassengerAction(
    bool keyboardDown, bool gamepadDPadUp, bool& actionHeld)
{
    const bool actionDown = keyboardDown || gamepadDPadUp;
    if (!actionDown)
    {
        actionHeld = false;
        return InputSource::None;
    }

    if (actionHeld)
        return InputSource::None;

    actionHeld = true;
    return gamepadDPadUp ? InputSource::Gamepad : InputSource::Keyboard;
}

void CPassengerEnter::Process()
{
    CPlayerPed* pPlayerPed = FindPlayerPed(0);

    if (pPlayerPed == nullptr)
    {
        bPassengerActionHeld = false;
        return;
    }

    CPad* pPad = pPlayerPed->GetPadFromPlayer();
    if (pPad == nullptr)
    {
        bPassengerActionHeld = false;
        return;
    }

    const InputSource inputSource = ConsumePassengerAction(
        pPad->PCTempKeyState.DPadUp != 0, pPad->PCTempJoyState.DPadUp != 0, bPassengerActionHeld);

    if (pPlayerPed->m_nPedFlags.bInVehicle || !pPlayerPed->IsAlive() || pPlayerPed->m_pIntelligence == nullptr)
        return;

    if (CTimer::m_snTimeInMilliseconds / 1000 != CTimer::m_snPreviousTimeInMilliseconds / 1000)
        UpdatePassengerDoorHint(pPlayerPed);
        
    if (IsPlayerEnteringVehicle(pPlayerPed))
        return;

    if (inputSource == InputSource::None || pPad->bDisablePlayerEnterCar || pPad->DisablePlayerControls != 0)
        return;

    const PassengerSeat passengerSeat = FindNearestPassengerSeat(pPlayerPed);
    if (passengerSeat.pNetworkVehicle == nullptr)
        return;

    CVehicle* pVehicle = passengerSeat.pNetworkVehicle->m_pVehicle;
    CTaskComplexEnterCarAsPassenger* pEnterCarTask =
        new CTaskComplexEnterCarAsPassenger(pVehicle, passengerSeat.doorId, false);
    pPlayerPed->m_pIntelligence->m_TaskMgr.SetTask(pEnterCarTask, 3, false);

    Packets::Vehicles::VehicleEnter packet{};
    packet.vehicleid = passengerSeat.pNetworkVehicle->m_nVehicleId;
    packet.seatid = passengerSeat.seatId;
    packet.bForce = false;
    packet.bPassenger = true;
    GetPacketFactory().Send(packet);
}
