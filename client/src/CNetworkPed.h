#pragma once
#include "CNetworkTransformInterpolator.h"
class CTask;
class CTaskSimpleUseGun;

class CNetworkPed
{
private:
	CNetworkPed() {}
public:
	int m_nPedId = -1;
	CPed* m_pPed = nullptr;
	int m_nModelId = MODEL_MALE01;
	char m_szSpecialModelName[8]{};
	CVector m_vecLogicalPosition{};
	uint8_t m_nLogicalArea = AREA_MAIN_MAP;
	bool m_bModelLeaseHeld = false;
	uint32_t m_nModelRequestStartedAt = 0;
	uint32_t m_nNextModelRequestAt = 0;
	uint32_t m_nLastPresentationChangeAt = 0;
	uint8_t m_nModelRequestAttempts = 0;
	enum class PresentationMode : uint8_t { SPAWN, ON_FOOT, DRIVER, PASSENGER };
	PresentationMode m_presentationMode = PresentationMode::SPAWN;
	bool m_bHasOnFootSnapshot = false;
	bool m_bHasDriverSnapshot = false;
	bool m_bHasPassengerSnapshot = false;
	Packets::Peds::PedOnFoot m_onFootSnapshot{};
	Packets::Peds::PedDriverUpdate m_driverSnapshot{};
	Packets::Peds::PedPassengerSync m_passengerSnapshot{};
	bool m_bSyncing = false;
	unsigned char m_nTempId = 255;
	ePedType m_nPedType;
	unsigned char m_nCreatedBy;
	CVector m_vecVelocity{0.0f, 0.0f, 0.0f};
	float m_fAimingRotation = 0.0f;
	float m_fCurrentRotation = 0.0f;
	float m_fLookDirection = 0.0f;
	eMoveState m_nMoveState = eMoveState::PEDMOVE_NONE;
	float m_fMoveBlendRatio = 0.0f;
	CAutoPilot m_autoPilot;
	float m_fGasPedal = 0.0f;
	float m_fBreakPedal = 0.0f;
	float m_fSteerAngle = 0.0f;
	float m_fHealth = 100.0f;
	int m_nBlipHandle = -1;
	bool m_bClaimOnRelease = false;
	Packets::Peds::SPedTaskSnapshot m_lastRemoteTask{};
	bool m_bRemoteTaskInitialized = false;
	CTask* m_pRemotePrimaryTask = nullptr;
	CTaskSimpleUseGun* m_pRemoteAimTask = nullptr;
	CVehicle* m_pRemoteSignalVehicle = nullptr;
	CNetworkTransformInterpolator m_transformInterpolator{};

	static CNetworkPed* CreateHosted(CPed* ped);
	void WarpIntoVehicleDriver(CVehicle* vehicle);
	void WarpIntoVehiclePassenger(CVehicle* vehicle, int seatid);
	void RemoveFromVehicle(CVehicle* vehicle);
	void ClaimOnRelease();
	void CancelClaim();

	void ApplyWeaponSnapshot(Packets::Players::SWeaponSnapshot& weaponSnapshot);
	void CaptureTaskSnapshot(Packets::Peds::SPedTaskSnapshot& snapshot) const;
	void ApplyTaskSnapshot(const Packets::Peds::SPedTaskSnapshot& snapshot);
	void ApplyAimSnapshot(bool aiming, const CVector& target);
	void ApplyDriverSignals(CVehicle* vehicle, bool horn, bool siren);
	void ClearRemoteTask(bool resetRevision = true);
	void ClearRemoteAim();
	void ClearDriverSignals();
	void ResetRemoteSyncState(bool abortTasks = true);
	void ValidateRemoteTaskTarget();
	bool Materialize();
	void StreamOut();
	void ApplyCachedPresentation();
	void ProcessPendingPresentation();
	bool CacheOnFootSnapshot(const Packets::Peds::PedOnFoot& snapshot);
	bool CacheDriverSnapshot(const Packets::Peds::PedDriverUpdate& snapshot);
	bool CachePassengerSnapshot(const Packets::Peds::PedPassengerSync& snapshot);
	void ProcessTransformInterpolation();
	bool ResetTransformInterpolation(server_time_t boundaryTime = 0);
	CVector GetLogicalPosition() const;

	CNetworkPed(int pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[]);
	~CNetworkPed();
};

