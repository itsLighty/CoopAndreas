#pragma once

#include "CNetworkTransformInterpolator.h"

class CObject;

class CNetworkPlayer
{
public:
	CPlayerPed* m_pPed = nullptr;
	int m_iPlayerId;
	CVector m_vecLogicalPosition{};
	CVector m_vecMapPosition{};
	uint8_t m_nLogicalArea = AREA_MAIN_MAP;
	bool m_bHasOnFootSnapshot = false;
	uint32_t m_nLastPresentationChangeAt = 0;
	int m_nPendingVehicleId = -1;
	int8_t m_nPendingVehicleSeat = -1;
	bool m_bPendingVehiclePassenger = false;
	bool m_bPendingVehicleForce = false;
	bool m_bPendingVehicleConfirmed = false;
	bool m_bHasPendingVehicleRelation = false;
	bool m_bHasVehicleDriverSnapshot = false;
	bool m_bHasVehiclePassengerSnapshot = false;
	Packets::Vehicles::VehicleDriverUpdate m_vehicleDriverSnapshot{};
	Packets::Vehicles::VehiclePassengerUpdate m_vehiclePassengerSnapshot{};
	bool m_bHasPendingTask = false;
	Packets::Players::SetPlayerTask m_pendingTask{};
	uint32_t m_nPendingTaskGeneration = 0;
	uint32_t m_nAppliedTaskGeneration = 0;
	bool m_bHasPendingEnExTransition = false;
	Packets::Players::EnExTransition m_pendingEnExTransition{};
	uint32_t m_nPendingEnExTransitionGeneration = 0;
	uint32_t m_nAppliedEnExTransitionGeneration = 0;
	CNetworkTransformInterpolator m_transformInterpolator{};

	Packets::Players::OnFootUpdate m_onFootSnapshotInterpolated{};
	
	uint32_t m_startedInterpolatingCameraAt = 0;
	bool m_bHasCameraSnapshot = false;
	Packets::Players::PlayerCameraSync m_cameraSnapshotOld{};
	Packets::Players::PlayerCameraSync m_cameraSnapshot{};
	uint32_t m_nLaserScopeDotReceivedAt = 0;
	
	signed short m_oShockButtonL;
	signed short m_lShockButtonL;

	Packets::Players::PlayerPlaceWaypoint m_waypointState{};

	char m_Name[Config::MAX_NICKNAME_LENGTH + 1] = { 0 };
	uint16_t m_nRTT = 0;

	CControllerState m_oldControllerState{};
	CControllerState m_newControllerState{};

	CNetworkPlayerStats m_stats{};
	Packets::Players::PlayerGameplayState m_gameplayState{};
	bool m_bHasGameplayState = false;
	CPedClothesDesc m_pPedClothesDesc{};
	bool m_bHasBeenConnectedBeforeMe = false;

	bool m_bExtrapolating = false;

	bool m_bRequestedDuckTask = false;
	int m_nSyncedAnimationState = Packets::Players::PLAYER_ANIMATION_NONE;
	uint16_t m_nSyncedAnimationSequence = 0;
	uint8_t m_nSyncedAnimationProgress = 0;
	uint32_t m_nSyncedAnimationReceivedAt = 0;
	bool m_bHasSyncedAnimationSequence = false;
	int m_nSyncedParachuteState = Packets::Players::PLAYER_PARACHUTE_NONE;
	int m_nAppliedParachuteState = Packets::Players::PLAYER_PARACHUTE_NONE;
	uint16_t m_nSyncedParachuteSequence = 0;
	uint8_t m_nSyncedParachuteProgress = 0;
	float m_fSyncedParachutePitch = 0.0f;
	float m_fSyncedParachuteRoll = 0.0f;
	float m_fSyncedParachuteHeading = 0.0f;
	uint32_t m_nSyncedParachuteReceivedAt = 0;
	bool m_bHasSyncedParachuteSequence = false;
	bool m_bParachuteResourcesAcquired = false;
	bool m_bParachuteCanopyAttached = false;
	CObject* m_pSyncedParachuteCanopy = nullptr;

	bool m_bIsHost = false;

	~CNetworkPlayer();
	CNetworkPlayer(int id, CVector position);

	void CreatePed(int id, CVector position);
	void DestroyPed();
	bool Materialize(CVector position);
	void StreamOut();
	void ApplyCachedPresentation();
	void ProcessPendingPresentation();
	void ReconcilePendingVehiclePresentation();
	bool CacheOnFootSnapshot(const Packets::Players::OnFootUpdate& snapshot);
	bool CacheVehicleDriverSnapshot(const Packets::Vehicles::VehicleDriverUpdate& snapshot);
	bool CacheVehiclePassengerSnapshot(const Packets::Vehicles::VehiclePassengerUpdate& snapshot);
	void CacheOnFootRotation(server_time_t serverTime, float currentRotation, float aimingRotation);
	void CacheVehicleRelation(int vehicleId, int seatId, bool passenger, bool force, bool confirmed = false);
	void ClearVehicleRelation(bool resetInterpolation = true);
	void ProcessTransformInterpolation();
	bool ResetTransformInterpolation(server_time_t boundaryTime = 0);
	bool ResetTransformInterpolationForCrossChannelBoundary(server_time_t boundaryTime);
	bool SnapOnFootTransform(CVector position, float currentRotation, float aimingRotation,
		server_time_t boundaryTime = 0);
	CVector GetLogicalPosition() const;
	CVector GetMapPosition() const;
	void Respawn(server_time_t boundaryTime = 0);
	int GetInternalId();
	std::string GetName();
	char GetWeaponSkill(eWeaponType weaponType);
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	void RemoveFromVehicle(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	void EnterVehiclePassenger(CVehicle* vehicle, int seatid);
	void HandleTask(Packets::Players::SetPlayerTask& packet);
	void ApplyTaskPresentation(Packets::Players::SetPlayerTask& packet);
	void ApplyPendingTaskOnce();
	void HandleSyncedAnimation(const Packets::Players::SetPlayerTask& packet);
	void HandleSyncedParachute(const Packets::Players::SetPlayerTask& packet);
	void ApplySyncedAnimation();
	void ProcessSyncedParachute();
	void ApplySyncedParachute();
	void FadeSyncedAnimation();
	void ClearSyncedAnimationState();
	void ClearSyncedParachuteState();
	void DestroySyncedParachutePresentation();
	void ClearLaserScopeDotState();
	void ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot);
	void ApplyGameplayState(const Packets::Players::PlayerGameplayState& gameplayState);
};

