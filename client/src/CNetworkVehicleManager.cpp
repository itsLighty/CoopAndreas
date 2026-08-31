#include "stdafx.h"
#include "CNetworkVehicle.h"
#include <CKeySync.h>
#include "CNetworkVehicleManager.h"
#include <CAEAudioHardware.h>
#include <CAERadioTrackManager.h>

namespace
{
int GetRadioTrackPlayTime()
{
    return plugin::CallMethodAndReturn<int, 0x4D8F60, CAEAudioHardware*>(&AEAudioHardware);
}
}

CNetworkVehicle* CNetworkVehicleManager::GetVehicle(int vehicleid)
{
	for (int i = 0; i != m_pVehicles.size(); i++)
	{
		if (m_pVehicles[i]->m_nVehicleId == vehicleid)
		{
			return m_pVehicles[i];
		}
	}
	return nullptr;
}
CNetworkVehicle* CNetworkVehicleManager::GetVehicle(CEntity* vehicle)
{
	// Streamed-out vehicles also carry a null native pointer. Null is never a valid entity identity.
	if (vehicle == nullptr)
		return nullptr;

	for (int i = 0; i != m_pVehicles.size(); i++)
	{
		if (m_pVehicles[i]->m_pVehicle == vehicle)
		{
			return m_pVehicles[i];
		}
	}

	return nullptr;
}

void CNetworkVehicleManager::Add(CNetworkVehicle* vehicle)
{
	CNetworkVehicleManager::m_pVehicles.push_back(vehicle);
	ResolvePendingVehicleState();
}

void CNetworkVehicleManager::Remove(CNetworkVehicle* vehicle, server_time_t boundaryTime)
{
	ClearVehicleRelations(vehicle, boundaryTime);

	auto it = std::find(m_pVehicles.begin(), m_pVehicles.end(), vehicle);
	if (it != m_pVehicles.end())
	{
		m_pVehicles.erase(it);
	}
}

void CNetworkVehicleManager::UpdateDriver(CVehicle* pVehicle)
{
	if (auto pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle))
	{
		Packets::Vehicles::VehicleDriverUpdate vehicleDriverUpdate{};
		CPlayerPed* pPlayerPed = FindPlayerPed(0);

		vehicleDriverUpdate.vehicleid = pNetworkVehicle->m_nVehicleId;
		vehicleDriverUpdate.pos = pVehicle->m_matrix->pos;
		vehicleDriverUpdate.roll = pVehicle->m_matrix->right;
		vehicleDriverUpdate.rot = pVehicle->m_matrix->up;
		vehicleDriverUpdate.velocity = pVehicle->m_vecMoveSpeed;
		vehicleDriverUpdate.turnSpeed = pVehicle->m_vecTurnSpeed;

		CWeapon& weapon = pPlayerPed->GetWeapon();
		vehicleDriverUpdate.playerWeapon.iWeaponType = weapon.m_eWeaponType;
		vehicleDriverUpdate.playerWeapon.iWeaponState = weapon.m_nState;
		vehicleDriverUpdate.playerWeapon.nAmmo = weapon.m_nAmmoInClip;

		vehicleDriverUpdate.playerHealth.iHealth = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fHealth, 0.0f, 255.0f));
		vehicleDriverUpdate.playerHealth.iArmour = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fArmour, 0.0f, 255.0f));
		CKeySync::CollectState(vehicleDriverUpdate.playerKeys);

		vehicleDriverUpdate.color1 = pVehicle->m_nPrimaryColor;
		vehicleDriverUpdate.color2 = pVehicle->m_nSecondaryColor;

		vehicleDriverUpdate.health = pVehicle->m_fHealth;

		vehicleDriverUpdate.paintjob = pVehicle->GetRemapIndex();

		if (pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
		{
			CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
			vehicleDriverUpdate.miscComponentAngle = pAutomobile->m_wMiscComponentAngle;
		}

		if (pVehicle->m_nVehicleType == VEHICLE_BIKE)
		{
			CBike* pBike = (CBike*)pVehicle;
			vehicleDriverUpdate.bikeLean = pBike->m_rideAnimData.m_fDesiredLeanAngle;
		}

		if (pVehicle->m_nVehicleSubType == VEHICLE_PLANE)
		{
			CPlane* pPlane = (CPlane*)pVehicle;
			vehicleDriverUpdate.planeGearState = pPlane->m_fLandingGearStatus;
		}

		vehicleDriverUpdate.locked = pVehicle->m_eDoorLock;
		vehicleDriverUpdate.auxState = CaptureAuxState(pNetworkVehicle);

		GetPacketFactory().Send(vehicleDriverUpdate);
	}
}

void CNetworkVehicleManager::UpdateIdle()
{
	CNetworkVehicleManager::RemoveHostedUnused();
	ResolvePendingVehicleState();

	for (auto pNetworkVehicle : m_pVehicles)
	{
		CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
		if (pVehicle == nullptr)
			continue;

		if (pNetworkVehicle->m_bSyncing && !pNetworkVehicle->HasDriver())
		{
			Packets::Vehicles::VehicleIdleUpdate packet{};
			packet.vehicleid = pNetworkVehicle->m_nVehicleId;

			packet.pos = pVehicle->m_matrix->pos;
			packet.roll = pVehicle->m_matrix->right;
			packet.rot = pVehicle->m_matrix->up;
			packet.velocity = pVehicle->m_vecMoveSpeed;
			packet.turnSpeed = pVehicle->m_vecTurnSpeed;
			packet.color1 = pVehicle->m_nPrimaryColor;
			packet.color2 = pVehicle->m_nSecondaryColor;
			packet.health = pVehicle->m_fHealth;
			packet.paintjob = pVehicle->GetRemapIndex();

			if (CUtil::GetVehicleType(pVehicle) == eVehicleType::VEHICLE_PLANE)
			{
				CPlane* plane = (CPlane*)pVehicle;
				packet.planeGearState = plane->m_fLandingGearStatus;
			}

			packet.locked = pVehicle->m_eDoorLock;
			packet.auxState = CaptureAuxState(pNetworkVehicle);
			GetPacketFactory().Send(packet);
		}
	}
}

void CNetworkVehicleManager::UpdatePassenger(CVehicle* pVehicle, CPlayerPed* pPlayerPed)
{
	 CNetworkVehicle* pNetworkVehicle = CNetworkVehicleManager::GetVehicle(pVehicle);
	 if (pNetworkVehicle)
	 {
		 Packets::Vehicles::VehiclePassengerUpdate packet{};

		 CWeapon& weapon = pPlayerPed->GetWeapon();
		 packet.playerWeapon.iWeaponType = weapon.m_eWeaponType;
		 packet.playerWeapon.iWeaponState = weapon.m_nState;
		 packet.playerWeapon.nAmmo = weapon.m_nAmmoInClip;

		 packet.playerHealth.iHealth = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fHealth, 0.0f, 255.0f));
		 packet.playerHealth.iArmour = static_cast<uint8_t>(std::clamp(pPlayerPed->m_fArmour, 0.0f, 255.0f));
		 CKeySync::CollectState(packet.playerKeys);

		 packet.vehicleid = pNetworkVehicle->m_nVehicleId;
		 packet.driveby = CDriveBy::IsPedInDriveby(pPlayerPed);

		 for (int i = 0; i < pVehicle->m_nMaxPassengers; i++)
		 {
			 if (pVehicle->m_apPassengers[i] == pPlayerPed)
			 {
				 packet.seatid = i;
				 break;
			 }
		 }
		 GetPacketFactory().Send(packet);
	 }
}

uint8_t CNetworkVehicleManager::AddToTempList(CNetworkVehicle* networkVehicle)
{
	for (uint8_t i = 0; i < ARRAY_SIZE(m_apTempVehicles); i++)
	{
		if (m_apTempVehicles[i] == nullptr)
		{
			m_apTempVehicles[i] = networkVehicle;
			return i;
		}
	}

	return 255;
}

void CNetworkVehicleManager::RemoveHostedUnused()
{
	for (auto it = CNetworkVehicleManager::m_pVehicles.begin(); it != CNetworkVehicleManager::m_pVehicles.end();)
	{
		if ((*it)->m_bSyncing)
		{
			CVehicle* vehicle = (*it)->m_pVehicle;
			if (!IsVehiclePointerValid(vehicle))
			{
				(*it)->m_pVehicle = nullptr;
				ClearVehicleRelations(*it);
				delete* it;
				it = m_pVehicles.erase(it);
				continue;
			}
		}
		++it;
	}
}

void CNetworkVehicleManager::UpdateDamageSync()
{
	for (auto pNetworkVehicle : m_pVehicles)
	{
		if (pNetworkVehicle->m_bSyncing)
		{
			CVehicle* pVehicle = pNetworkVehicle->m_pVehicle;
			if (pVehicle && pVehicle->m_nVehicleType == VEHICLE_AUTOMOBILE)
			{
				CAutomobile* pAutomobile = (CAutomobile*)pVehicle;
				if (pNetworkVehicle->m_oldDamageState != pAutomobile->m_damageManager)
				{
					pNetworkVehicle->m_oldDamageState = pAutomobile->m_damageManager;
					Packets::Vehicles::VehicleDamage packet{};
					packet.vehicleid = pNetworkVehicle->m_nVehicleId;
					packet.damageManager = pAutomobile->m_damageManager;
					GetPacketFactory().Send(packet);
				}
			}
		}
	}
}

Packets::Vehicles::VehicleAuxState CNetworkVehicleManager::CaptureAuxState(CNetworkVehicle* networkVehicle)
{
	Packets::Vehicles::VehicleAuxState state{};
	if (!networkVehicle || !networkVehicle->m_pVehicle || !networkVehicle->m_pVehicle->IsVTableValid())
	{
		return state;
	}

	CVehicle* vehicle = networkVehicle->m_pVehicle;
	if (Packets::Vehicles::IsHydraulicSyncModel(static_cast<uint16_t>(networkVehicle->m_nModelId)) &&
		vehicle->m_nVehicleType == VEHICLE_AUTOMOBILE && vehicle->m_nHandlingFlags.bHydraulicInst)
	{
		auto* automobile = static_cast<CAutomobile*>(vehicle);
		state.hydraulicsActive = true;
		state.hydraulicControlAngle = static_cast<uint16_t>(
			std::clamp<int>(automobile->m_wMiscComponentAngle, 0, Packets::Vehicles::VEHICLE_HYDRAULIC_ANGLE_MAX));
		for (int wheel = 0; wheel < 4; ++wheel)
		{
			state.hydraulicSuspension[wheel] = std::clamp(automobile->wheelsDistancesToGround1[wheel], 0.0f, 1.0f);
		}
	}

	if (Packets::Vehicles::CanTowTrailerModel(static_cast<uint16_t>(networkVehicle->m_nModelId)) && vehicle->m_pTrailer)
	{
		if (auto* trailer = GetVehicle(vehicle->m_pTrailer))
		{
			if (Packets::Vehicles::CanAttachTrailerModel(static_cast<uint16_t>(networkVehicle->m_nModelId),
				static_cast<uint16_t>(trailer->m_nModelId)))
			{
				state.trailerId = trailer->m_nVehicleId;
			}
		}
	}

	CPlayerPed* localPlayer = FindPlayerPed(0);
	if (localPlayer && localPlayer->m_nPedFlags.bInVehicle && localPlayer->m_pVehicle == vehicle &&
		AudioEngine.IsVehicleRadioActive() && AudioEngine.IsRadioOn())
	{
		const int station = static_cast<int>(AERadioTrackManager.m_TempSettings.m_nCurrentRadioStation);
		if (station >= 0 && station < Packets::Vehicles::VEHICLE_RADIO_OFF)
		{
			state.radioActive = true;
			state.radioStation = station;
			state.radioTrackId = std::clamp(
				AEAudioHardware.GetActiveTrackID(), Packets::Vehicles::VEHICLE_RADIO_TRACK_NONE,
				Packets::Vehicles::VEHICLE_RADIO_TRACK_MAX);
			state.radioPlayTimeMs = state.radioTrackId == Packets::Vehicles::VEHICLE_RADIO_TRACK_NONE
				? 0
				: std::clamp(GetRadioTrackPlayTime(), 0, Packets::Vehicles::VEHICLE_RADIO_PLAY_TIME_MAX_MS);
		}
	}

	return state;
}

void CNetworkVehicleManager::ResolvePendingVehicleState()
{
	for (auto* vehicle : m_pVehicles)
	{
		if (vehicle)
		{
			vehicle->ResolvePendingTrailer();
		}
	}
}

void CNetworkVehicleManager::ClearVehicleRelations(CNetworkVehicle* vehicle, server_time_t boundaryTime)
{
	if (!vehicle)
		return;

	// VEHICLE_REMOVE and pool reuse share the same teardown path. Detach every native occupant before the GTA
	// vehicle is deleted so no local or remote ped retains a stale m_pVehicle/bInVehicle reference.
	vehicle->StreamOut();
	vehicle->DetachTrailerLinks();
	for (auto* player : CNetworkPlayerManager::m_pPlayers)
	{
		if (player && player->m_bHasPendingVehicleRelation && player->m_nPendingVehicleId == vehicle->m_nVehicleId)
		{
			player->ResetTransformInterpolation(boundaryTime);
			player->m_vecLogicalPosition = vehicle->GetLogicalPosition();
			player->ClearVehicleRelation(false);
		}
	}
	for (auto* ped : CNetworkPedManager::m_pPeds)
	{
		if (!ped)
			continue;
		const bool wasDriver = ped->m_bHasDriverSnapshot &&
			ped->m_driverSnapshot.vehicleid == vehicle->m_nVehicleId;
		const bool wasPassenger = ped->m_bHasPassengerSnapshot &&
			ped->m_passengerSnapshot.vehicleid == vehicle->m_nVehicleId;
		if (wasDriver || wasPassenger)
		{
			ped->ResetTransformInterpolation(boundaryTime);
			ped->m_vecLogicalPosition = vehicle->GetLogicalPosition();
			ped->m_bHasDriverSnapshot = false;
			ped->m_bHasPassengerSnapshot = false;
			ped->m_presentationMode = ped->m_bHasOnFootSnapshot
				? CNetworkPed::PresentationMode::ON_FOOT
				: CNetworkPed::PresentationMode::SPAWN;
		}
	}
	for (auto* other : m_pVehicles)
	{
		if (other && other != vehicle &&
			(other->m_nPendingTrailerId == vehicle->m_nVehicleId ||
				other->m_nAppliedTrailerId == vehicle->m_nVehicleId))
		{
			other->m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
			other->m_nPendingTrailerSince = 0;
			other->m_nAppliedTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
			other->m_lastAuxState.trailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
		}
	}
}

void CNetworkVehicleManager::Clear()
{
	while (!m_pVehicles.empty())
	{
		CNetworkVehicle* vehicle = m_pVehicles.back();
		m_pVehicles.pop_back();
		delete vehicle;
	}
	std::fill(std::begin(m_apTempVehicles), std::end(m_apTempVehicles), nullptr);
}
