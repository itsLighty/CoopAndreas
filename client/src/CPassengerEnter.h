#pragma once
class CPassengerEnter
{
public:
	enum class InputSource
	{
		None,
		Keyboard,
		Gamepad,
	};

	static InputSource ConsumePassengerAction(bool keyboardDown, bool gamepadDPadUp, bool& actionHeld);
	static void Process();
};

