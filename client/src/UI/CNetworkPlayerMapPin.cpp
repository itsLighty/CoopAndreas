#include "stdafx.h"

namespace
{
bool GetPlayerMarkerPosition(const CVector& worldPosition, CVector2D& result)
{
	if (std::abs(CRadar::m_radarRange) <= 0.000001f)
		return false;

	CVector2D vec = CVector2D(worldPosition.x, worldPosition.y) - CRadar::vec2DRadarOrigin;
	CVector2D playerDirection = 
	{ 
		vec.x / CRadar::m_radarRange, 
		vec.y / CRadar::m_radarRange 
	};

	CVector2D rotatedPos = {
		CRadar::cachedSin * playerDirection.y + CRadar::cachedCos * playerDirection.x,
		CRadar::cachedCos * playerDirection.y - CRadar::cachedSin * playerDirection.x
	};
	CRadar::LimitRadarPoint(rotatedPos);

	CRadar::TransformRadarPointToScreenSpace(result, rotatedPos);

	return true;
}

float CalculateMarkerAngle(CNetworkPlayer* player)
{
	if (player->m_pPed == nullptr)
		return player->m_onFootSnapshotInterpolated.currentRotation.m_angle - CRadar::m_fRadarOrientation +
			(FrontEndMenuManager.m_bDrawRadarOrMap ? static_cast<float>(M_PI) : -static_cast<float>(M_PI));

	float baseAngle = player->m_pPed->m_nPhysicalFlags.bOnSolidSurface ? player->m_pPed->GetHeading() : player->m_onFootSnapshotInterpolated.currentRotation.m_angle;

	if (player->m_pPed->m_pVehicle && player->m_pPed->m_nPedFlags.bInVehicle)
	{
		baseAngle = player->m_pPed->m_pVehicle->GetHeading();
	}

	if (!FrontEndMenuManager.m_bDrawRadarOrMap)
	{
		return baseAngle - CRadar::m_fRadarOrientation - (float)M_PI;
	}
	else
	{
		return baseAngle - CRadar::m_fRadarOrientation + (float)M_PI;
	}
}
}

void CNetworkPlayerMapPin::Process()
{
	const CScreenTransform transform = CUtil::GetScreenTransform();
	if (!transform.valid || !RwD3D9GetCurrentD3DDevice())
		return;

	for (auto* player : CNetworkPlayerManager::m_pPlayers)
	{
		if (player == nullptr || (!player->m_bHasOnFootSnapshot && !player->m_bHasVehicleDriverSnapshot &&
			!player->m_bHasVehiclePassengerSnapshot))
			continue;
		CVector2D pos{};
		if (!GetPlayerMarkerPosition(player->GetMapPosition(), pos))
			continue;

		float angle = CalculateMarkerAngle(player);

		CRadar::DrawRotatingRadarSprite(
			&CRadar::RadarBlipSprites[RADAR_SPRITE_CENTRE],
			pos.x,
			pos.y,
			angle,
			CUtil::SCREEN_SCALE_X(5.0f),
			CUtil::SCREEN_SCALE_Y(5.0f),
			player->m_pPed && player->m_pPed->IsHidden() ? CRGBA{ 50, 50, 50, 255 } : CRGBA{ 255, 255, 255, 255 }
		);
	}
}
