#pragma once
class CNetworkVehicleManager
{
public:
    static inline std::vector<CNetworkVehicle*> m_pVehicles;
    static inline CNetworkVehicle* m_apTempVehicles[255];

    static CNetworkVehicle* GetVehicle(int vehicleid);
    static CNetworkVehicle* GetVehicle(CEntity* vehicle);
    static void Add(CNetworkVehicle* vehicle);
    static void Remove(CNetworkVehicle* vehicle, server_time_t boundaryTime = 0);
    static void Clear();
    static void UpdateDriver(CVehicle* pVehicle);
    static void UpdateIdle();
    static void UpdatePassenger(CVehicle* pVehicle, CPlayerPed* pPlayerPed);
    static uint8_t AddToTempList(CNetworkVehicle* networkVehicle);
    static void RemoveHostedUnused();
    static void UpdateDamageSync();
    static Packets::Vehicles::VehicleAuxState CaptureAuxState(CNetworkVehicle* networkVehicle);
    static void ResolvePendingVehicleState();
    static void ClearVehicleRelations(CNetworkVehicle* vehicle, server_time_t boundaryTime = 0);
};

