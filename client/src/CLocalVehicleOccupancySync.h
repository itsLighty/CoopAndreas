#pragma once

class CLocalVehicleOccupancySync
{
public:
    static void Process();
    static void Reset();

private:
    struct Occupancy
    {
        int vehicleId = -1;
        int8_t serverSeat = -1;

        bool IsValid() const { return vehicleId >= 0 && serverSeat >= 0; }
        bool operator==(const Occupancy& other) const
        {
            return vehicleId == other.vehicleId && serverSeat == other.serverSeat;
        }
        bool operator!=(const Occupancy& other) const { return !(*this == other); }
    };

    static Occupancy CaptureNativeOccupancy();
    static void SendEnterConfirmation(const Occupancy& occupancy);
    static void SendExitConfirmation();

    static inline Occupancy ms_confirmedOccupancy{};
    static inline uint32_t ms_nLastConfirmationAt = 0;
    static inline uint8_t ms_nConfirmationCount = 0;
};
