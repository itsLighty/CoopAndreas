#pragma once

#include <cstdint>

class CControllerState
{
public:
    int16_t LeftStickX{};
    int16_t LeftStickY{};
    int16_t RightStickX{};
    int16_t RightStickY{};
    int16_t LeftShoulder1{};
    int16_t LeftShoulder2{};
    int16_t RightShoulder1{};
    int16_t RightShoulder2{};
    int16_t DPadUp{};
    int16_t DPadDown{};
    int16_t DPadLeft{};
    int16_t DPadRight{};
    int16_t Start{};
    int16_t Select{};
    int16_t ButtonSquare{};
    int16_t ButtonTriangle{};
    int16_t ButtonCross{};
    int16_t ButtonCircle{};
    int16_t ShockButtonL{};
    int16_t ShockButtonR{};
    int16_t m_bChatIndicated{};
    int16_t m_bPedWalk{};
    int16_t m_bVehicleMouseLook{};
    int16_t m_bRadioTrackSkip{};
};

static_assert(sizeof(CControllerState) == 0x30,
    "CControllerState must retain GTA SA's wire-compatible layout");
