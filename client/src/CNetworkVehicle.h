#pragma once
class CNetworkVehicle
{
private:
	CNetworkVehicle() {}
public:
	int m_nVehicleId = -1;
	CVehicle* m_pVehicle = nullptr;
	int m_nModelId = 0;
	CVector m_vecLogicalPosition{};
	float m_fSpawnRotation = 0.0f;
	uint8_t m_nSpawnColor1 = 0;
	uint8_t m_nSpawnColor2 = 0;
	uint8_t m_nLogicalArea = AREA_MAIN_MAP;
	bool m_bModelLeaseHeld = false;
	uint32_t m_nModelRequestStartedAt = 0;
	uint32_t m_nNextModelRequestAt = 0;
	uint32_t m_nLastPresentationChangeAt = 0;
	uint8_t m_nModelRequestAttempts = 0;
	char m_nPaintJob = -1;
	bool m_bSyncing = false;
	unsigned char m_nTempId = 255;
	unsigned char m_nCreatedBy;
	int m_nBlipHandle = -1;
	Packets::Vehicles::VehicleDriverUpdate m_playerDriverSnapshot{};
	Packets::Vehicles::VehicleIdleUpdate m_idleSnapshot{};
	Packets::Peds::PedDriverUpdate m_pedDriverSnapshot{};
	bool m_bHasIdleSnapshot = false;
	bool m_bHasDriverSnapshot = false;
	bool m_bHasPedDriverSnapshot = false;
	CDamageManager m_pendingDamageState{};
	bool m_bHasPendingDamageState = false;
	bool m_bPendingDamageApplied = false;
	static constexpr size_t MAX_PENDING_COMPONENTS = 16;
	std::array<int, MAX_PENDING_COMPONENTS> m_pendingComponents{};
	std::array<bool, MAX_PENDING_COMPONENTS> m_pendingComponentApplied{};
	uint8_t m_nPendingComponentCount = 0;
	CDamageManager m_oldDamageState{};
	int m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
	uint32_t m_nPendingTrailerSince = 0;
	int m_nAppliedTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
	Packets::Vehicles::VehicleAuxState m_lastAuxState{};
	uint32_t m_nLastRadioCorrectionAt = 0;

	~CNetworkVehicle();
	CNetworkVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy);
	bool CreateVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2);
	bool Materialize();
	void StreamOut();
	void ApplyCachedPresentation();
	void ProcessPendingPresentation();
	void CacheIdleSnapshot(const Packets::Vehicles::VehicleIdleUpdate& snapshot);
	void CacheDriverSnapshot(const Packets::Vehicles::VehicleDriverUpdate& snapshot);
	void CachePedDriverSnapshot(const Packets::Peds::PedDriverUpdate& snapshot);
	void CacheDamageState(const CDamageManager& damage);
	void CacheComponentAdd(int modelId);
	void CacheComponentRemove(int modelId);
	CVector GetLogicalPosition() const;
	bool HasDriver();
	void ApplyAuxState(const Packets::Vehicles::VehicleAuxState& state);
	void ResolvePendingTrailer();
	void DetachTrailerLinks();
	
	static CNetworkVehicle* CreateHosted(CVehicle* vehicle);
};

