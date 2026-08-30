#pragma once
class CNetworkVehicle
{
private:
	CNetworkVehicle() {}
public:
	int m_nVehicleId = -1;
	CVehicle* m_pVehicle = nullptr;
	int m_nModelId = 0;
	char m_nPaintJob = -1;
	bool m_bSyncing = false;
	unsigned char m_nTempId = 255;
	unsigned char m_nCreatedBy;
	int m_nBlipHandle = -1;
	Packets::Vehicles::VehicleDriverUpdate m_playerDriverSnapshot{};
	CDamageManager m_oldDamageState{};
	int m_nPendingTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
	uint32_t m_nPendingTrailerSince = 0;
	int m_nAppliedTrailerId = Packets::Vehicles::VEHICLE_TRAILER_NONE;
	Packets::Vehicles::VehicleAuxState m_lastAuxState{};
	uint32_t m_nLastRadioCorrectionAt = 0;

	~CNetworkVehicle();
	CNetworkVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2, unsigned char createdBy);
	bool CreateVehicle(int vehicleid, int modelid, CVector pos, float rotation, unsigned char color1, unsigned char color2);
	bool HasDriver();
	void ApplyAuxState(const Packets::Vehicles::VehicleAuxState& state);
	void ResolvePendingTrailer();
	void DetachTrailerLinks();
	
	static CNetworkVehicle* CreateHosted(CVehicle* vehicle);
};

