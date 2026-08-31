#pragma once

class CNetworkPlayer;
class CNetworkPed;
class CNetworkVehicle;

class CNetworkEntityStreamManager
{
public:
    static void Process();
    static void Reset();

    static void Forget(CNetworkPlayer* player);
    static void Forget(CNetworkPed* ped);
    static void Forget(CNetworkVehicle* vehicle);
};
