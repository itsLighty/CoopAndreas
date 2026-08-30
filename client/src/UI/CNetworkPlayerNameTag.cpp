#include "stdafx.h"

namespace
{
constexpr float NAME_TAG_DESIGN_WIDTH = 1920.0f;
constexpr float NAME_TAG_DESIGN_HEIGHT = 1080.0f;

constexpr float ToVirtualX(float value)
{
	return value * CUtil::SCREEN_BASE_WIDTH / NAME_TAG_DESIGN_WIDTH;
}

constexpr float ToVirtualY(float value)
{
	return value * CUtil::SCREEN_BASE_HEIGHT / NAME_TAG_DESIGN_HEIGHT;
}

class ScopedFontState
{
public:
	ScopedFontState()
		: color(*CFont::m_Color), scale(*CFont::m_Scale), dropColor(*CFont::m_FontDropColor),
		  justify(CFont::m_bFontJustify), centre(CFont::m_bFontCentreAlign), right(CFont::m_bFontRightAlign),
		  background(CFont::m_bFontBackground), enlargeBackground(CFont::m_bEnlargeBackgroundBox),
		  style(CFont::m_FontStyle), shadow(CFont::m_nFontShadow), outlineSize(CFont::m_nFontOutlineSize),
		  outline(CFont::m_nFontOutline)
	{
	}

	~ScopedFontState()
	{
		*CFont::m_Color = color;
		*CFont::m_Scale = scale;
		*CFont::m_FontDropColor = dropColor;
		CFont::m_bFontJustify = justify;
		CFont::m_bFontCentreAlign = centre;
		CFont::m_bFontRightAlign = right;
		CFont::m_bFontBackground = background;
		CFont::m_bEnlargeBackgroundBox = enlargeBackground;
		CFont::m_FontStyle = style;
		CFont::m_nFontShadow = shadow;
		CFont::m_nFontOutlineSize = outlineSize;
		CFont::m_nFontOutline = outline;
	}

private:
	CRGBA color;
	CVector2D scale;
	CRGBA dropColor;
	bool justify;
	bool centre;
	bool right;
	bool background;
	bool enlargeBackground;
	unsigned char style;
	unsigned char shadow;
	unsigned char outlineSize;
	unsigned char outline;
};

class ScopedRenderState
{
public:
	ScopedRenderState()
		: textureFilter(plugin::GetRenderState(rwRENDERSTATETEXTUREFILTER)),
		  zTest(plugin::GetRenderState(rwRENDERSTATEZTESTENABLE)),
		  zWrite(plugin::GetRenderState(rwRENDERSTATEZWRITEENABLE)),
		  shadeMode(plugin::GetRenderState(rwRENDERSTATESHADEMODE)),
		  raster(plugin::GetRenderRaster(rwRENDERSTATETEXTURERASTER))
	{
	}

	~ScopedRenderState()
	{
		plugin::SetRenderState(rwRENDERSTATETEXTUREFILTER, textureFilter);
		plugin::SetRenderState(rwRENDERSTATEZTESTENABLE, zTest);
		plugin::SetRenderState(rwRENDERSTATEZWRITEENABLE, zWrite);
		plugin::SetRenderState(rwRENDERSTATESHADEMODE, shadeMode);
		plugin::SetRenderRaster(raster);
	}

private:
	unsigned int textureFilter;
	unsigned int zTest;
	unsigned int zWrite;
	unsigned int shadeMode;
	RwRaster* raster;
};

uint8_t GetHudAlpha(float distance)
{
	if (distance < 45.0f)
		return 255;

	if (distance > CNetworkPlayerNameTag::MAX_DRAW_NICKNAME_DISTANCE)
		return 0;

	return static_cast<uint8_t>((1.0f - (distance - 45.0f) / 5.0f) * 255.0f);
}

void DrawNickName(const CScreenTransform& transform, float x, float y, float scale, unsigned char alpha, const char* name)
{
	if (!name)
		return;

	CFont::SetOrientation(eFontAlignment::ALIGN_LEFT);
	CFont::SetFontStyle(1);
	CFont::SetColor(CRGBA(255, 255, 0, alpha));
	CFont::SetBackground(false, false);
	CFont::SetDropColor(CRGBA(0, 0, 0, alpha));
	CFont::SetDropShadowPosition(1);
	CFont::SetScale(
		transform.Width(ToVirtualX(0.6f) * scale),
		transform.Height(ToVirtualY(1.22f) * scale));
	CFont::PrintString(x, y, name);
}

void DrawWeaponIcon(const CScreenTransform& transform, CPed* ped, float x, float y, float scale, unsigned char alpha)
{
	if (!ped)
		return;

	ScopedRenderState renderState;
	const float width = transform.Width(47.0f / 2.0f) * scale;
	const float height = transform.Height(58.0f / 2.0f) * scale;
	const float halfWidth = width / 2.0f;
	const float halfHeight = height / 2.0f;

	RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, RWRSTATE(rwFILTERLINEAR));

	const auto modelId = CUtil::GetWeaponModelById(ped->m_aWeapons[ped->m_nActiveWeaponSlot].m_eWeaponType);
	if (modelId <= 0)
	{
		CHud::Sprites[0].Draw({ x, y, width + x, height + y }, CRGBA(255, 255, 255, alpha));
		return;
	}

	auto* modelInfo = CModelInfo::GetModelInfo(modelId);
	if (!modelInfo || !CTxdStore::ms_pTxdPool)
		return;

	auto* txd = CTxdStore::ms_pTxdPool->GetAt(modelInfo->m_nTxdIndex);
	if (!txd || !txd->m_pRwDictionary)
		return;

	auto* texture = RwTexDictionaryFindHashNamedTexture(
		txd->m_pRwDictionary, CKeyGen::AppendStringToKey(modelInfo->m_nKey, "ICON"));
	if (!texture)
		return;

	RwRenderStateSet(rwRENDERSTATEZTESTENABLE, RWRSTATE(FALSE));
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(RwTextureGetRaster(texture)));
	CSprite::RenderOneXLUSprite(
		x + halfWidth, y + halfHeight, 1.0f, halfWidth, halfHeight,
		255u, 255u, 255u, alpha, 1.0f, alpha, 0, 0);
}

void DrawBarChartScale(
	const CScreenTransform& transform,
	float x,
	float y,
	float width,
	float height,
	float scale,
	float progress,
	CRGBA color)
{
	ScopedRenderState renderState;
	RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(NULL));
	RwRenderStateSet(rwRENDERSTATESHADEMODE, RWRSTATE(rwSHADEMODEFLAT));

	progress = std::clamp(progress, 0.0f, 100.0f);
	const float endX = x + width;
	const float currentX = std::min(x + width * progress / 100.0f, endX);

	CSprite2d::DrawRect({ x, y, currentX, y + height }, color);
	CSprite2d::DrawRect(
		{ currentX, y, endX, y + height },
		{ uint8_t(color.r / 2.0f), uint8_t(color.g / 2.0f), uint8_t(color.b / 2.0f), color.a });

	const float borderWidth = transform.Width(2.0f) * scale;
	const float borderHeight = transform.Height(2.0f) * scale;
	const CRect rects[] = {
		{ x, y, endX, y + borderHeight },
		{ x, y + height - borderHeight, endX, y + height },
		{ x, y, x + borderWidth, y + height },
		{ endX - borderWidth, y, endX, y + height }
	};

	const auto black = CRGBA{ 0, 0, 0, color.a };
	for (const CRect& rect : rects)
		CSprite2d::DrawRect(rect, black);
}
}

void CNetworkPlayerNameTag::Process()
{
	const CScreenTransform transform = CUtil::GetScreenTransform();
	if (!transform.valid || !RwD3D9GetCurrentD3DDevice() || CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn)
		return;

	ScopedFontState fontState;
	for (auto* player : CNetworkPlayerManager::m_pPlayers)
	{
		if (!player || !player->m_pPed)
			continue;

		CVector localPlayerCamPos = TheCamera.m_aCams[TheCamera.m_nActiveCam].m_vecSource;
		CVector networkPlayerPos{};

		if (player->m_pPed->m_pRwClump)
			player->m_pPed->GetBonePosition(*reinterpret_cast<RwV3d*>(&networkPlayerPos), 5, false);
		else
		{
			networkPlayerPos = player->m_pPed->GetPosition();
			networkPlayerPos.z += 0.5f;
		}
		networkPlayerPos.z += 0.3f;

		const float distance = (localPlayerCamPos - networkPlayerPos).Magnitude();
		const uint8_t alpha = GetHudAlpha(distance);
		if (alpha == 0 || !player->m_pPed->IsVisible())
			continue;

		if (!CWorld::GetIsLineOfSightClear(
			localPlayerCamPos, networkPlayerPos, true, false, false, true, false, false, false))
			continue;

		RwV3d out{};
		float projectedWidth = 0.0f;
		float projectedHeight = 0.0f;
		if (!CSprite::CalcScreenCoors(
			*reinterpret_cast<RwV3d*>(&networkPlayerPos), &out, &projectedWidth, &projectedHeight, false, false))
			continue;

		const float normalizedDistance = distance / MAX_DRAW_NICKNAME_DISTANCE;
		const float scale = std::clamp(1.2f - normalizedDistance, 0.7f, 1.0f);
		const float barWidth = transform.Width(ToVirtualX(100.0f) * scale);
		const float barHeight = transform.Height(ToVirtualY(14.0f) * scale);

		if (player->m_onFootSnapshotInterpolated.healthSnapshot.iHealth >= 10.0f || GetTickCount() % 500 > 150)
		{
			DrawBarChartScale(transform, out.x, out.y, barWidth, barHeight, scale,
				player->m_onFootSnapshotInterpolated.healthSnapshot.iHealth, CRGBA(180, 25, 29, alpha));
		}

		const bool hasArmour = player->m_onFootSnapshotInterpolated.healthSnapshot.iArmour > 0.0f;
		if (hasArmour)
		{
			DrawBarChartScale(
				transform,
				out.x,
				out.y - transform.Height(ToVirtualY(12.0f) * scale),
				barWidth,
				barHeight,
				scale,
				player->m_onFootSnapshotInterpolated.healthSnapshot.iArmour,
				CRGBA(225, 225, 225, alpha));
		}

		const float nicknameOffsetY = hasArmour ? 24.0f * scale : 12.0f * scale;
		DrawNickName(
			transform,
			out.x + transform.Width(ToVirtualX(4.8f)),
			out.y - transform.Height(ToVirtualY(nicknameOffsetY + 8.0f)),
			scale,
			alpha,
			player->GetName().c_str());

		DrawWeaponIcon(
			transform,
			player->m_pPed,
			out.x - transform.Width(ToVirtualX(70.0f) * scale),
			out.y - transform.Height(ToVirtualY(44.0f) * scale),
			scale,
			alpha);
	}
}
