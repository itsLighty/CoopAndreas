#include "stdafx.h"

void CNetworkPlayerWaypoint::Process()
{
	const CScreenTransform transform = CUtil::GetScreenTransform();
	if (!transform.valid || !RwD3D9GetCurrentD3DDevice())
		return;

	for (auto* player : CNetworkPlayerManager::m_pPlayers)
	{
		if (!player || !player->m_waypointState.place)
			continue;

		CVector vecWaypointPos = CVector(player->m_waypointState.position.x, player->m_waypointState.position.y, 0.0f);

		CVector2D radar;
		CRadar::TransformRealWorldPointToRadarSpace(radar, vecWaypointPos);
		
		CRadar::LimitRadarPoint(radar);

		CVector2D screen;
		CRadar::TransformRadarPointToScreenSpace(screen, radar);

		CRadar::DrawRadarSprite(eRadarSprite::RADAR_SPRITE_WAYPOINT, screen.x, screen.y, 255);

		if (FrontEndMenuManager.m_bDrawRadarOrMap)
		{
			if (!CDXFont::m_pD3DXFont)
				CDXFont::GetTextWidth(L" ");
			if (!CDXFont::m_pD3DXFont)
				continue;

			const std::wstring name = CUnicode::ConvertUtf8ToUtf16(player->GetName());
			// 8 virtual units is the radar sprite size; place the label above it.
			screen.y -= transform.Width(8.0f) / 2.0f * offsetY;

			CRadar::LimitToMap(&screen.x, &screen.y);

			const int textWidth = CDXFont::GetTextWidth(name);
			const int textX = static_cast<int>(std::lround(screen.x)) - textWidth / 2;
			const int textY = static_cast<int>(std::lround(screen.y));
			CDXFont::Draw(textX, textY, name, D3DCOLOR_RGBA(181, 24, 24, 255));
		}
	}
}
