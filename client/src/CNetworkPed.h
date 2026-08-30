#pragma once
class CTask;
class CTaskSimpleUseGun;

class CNetworkPed
{
private:
	CNetworkPed() {}
public:
	int m_nPedId = -1;
	CPed* m_pPed = nullptr;
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

	CNetworkPed(int pedid, int modelId, ePedType pedType, CVector pos, unsigned char createdBy, char specialModelName[]);
	~CNetworkPed();
};

