#pragma once

struct CScreenTransform
{
	float screenWidth{};
	float screenHeight{};
	float safeLeft{};
	float safeTop{};
	float safeWidth{};
	float safeHeight{};
	float scaleX{};
	float scaleY{};
	bool valid{};

	float X(float virtualX) const { return safeLeft + virtualX * scaleX; }
	float Y(float virtualY) const { return safeTop + virtualY * scaleY; }
	float Width(float virtualWidth) const { return virtualWidth * scaleX; }
	float Height(float virtualHeight) const { return virtualHeight * scaleY; }
	float Right() const { return safeLeft + safeWidth; }
	float Bottom() const { return safeTop + safeHeight; }
};

class CUtil
{
public:
	static constexpr float DEFAULT_ASPECT_RATIO = 4.0f / 3.0f;
	static constexpr float SCREEN_BASE_WIDTH = 640.0f;
	static constexpr float SCREEN_BASE_HEIGHT = 448.0f;

	static bool CompareControllerStates(const CControllerState& state1, const CControllerState& state2);
	static void CopyControllerState(CControllerState& destination, const CControllerState& source);
	static bool IsDucked(CPed* ped);
	static bool isDifferenceGreaterThanPercent(float value1, float value2, int percent);
	static bool IsPositionUpdateNeeded(CVector pos, CVector update, int percent = 5);
	static int GetWeaponModelById(unsigned char id);
	static bool IsMeleeWeapon(unsigned char id);
	static void GiveWeaponByPacket(CNetworkPlayer* player, unsigned char weapon, unsigned short ammo, bool select = true);
	static void GiveWeaponByPacket(CNetworkPed* ped, unsigned char weapon, unsigned short ammo, bool select = true);
	static eVehicleType GetVehicleType(CVehicle* vehicle);
	static CNetworkPed* GetNetworkPedByTask(CTask* targetTask);
	static bool IsPedHasJetpack(CPed* ped);
	static void SetPlayerJetpack(CNetworkPlayer* ped, bool set);
	static std::string GetWeaponName(eWeaponType type);

	// BuildScreenTransform is intentionally independent of RenderWare so the
	// aspect-ratio math can be tested without a running game. GetScreenTransform
	// samples the live render state on every call; callers must not cache it.
	static CScreenTransform BuildScreenTransform(float screenWidth, float screenHeight, float renderAspect);
	static CScreenTransform GetScreenTransform();

	// Dimension-only compatibility helpers. Positions should use X/Y on the
	// current CScreenTransform so ultrawide and narrow-screen safe-area offsets
	// are retained.
	static float HUD_X(float a) { return GetScreenTransform().Width(a); }
	static float HUD_Y(float a) { return GetScreenTransform().Height(a); }
	static float SCREEN_SCALE_AR(float a) { return a; }
	static float SCREEN_SCALE_X(float a) { return GetScreenTransform().Width(a); }
	static float SCREEN_SCALE_Y(float a) { return GetScreenTransform().Height(a); }

};

