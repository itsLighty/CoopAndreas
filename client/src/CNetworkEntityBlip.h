#pragma once
class CNetworkEntityBlip
{
public:
	static void UpdateEntityBlip(Packets::Blips::UpdateEntityBlip* packet);
	static void RemoveEntityBlip(Packets::Blips::RemoveEntityBlip* packet);
	static void ClearEntityBlips();
	static bool HasDesiredPedBlip(int pedId);
	static bool HasDesiredVehicleBlip(int vehicleId);
	static void Update();
};

