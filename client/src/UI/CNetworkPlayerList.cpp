#include "stdafx.h"
#include "CNetworkPlayerList.h"
#include "CHudColours.h"

namespace
{
class ScopedFontState
{
public:
    ScopedFontState()
        : color(*CFont::m_Color), scale(*CFont::m_Scale), justify(CFont::m_bFontJustify),
          centre(CFont::m_bFontCentreAlign), right(CFont::m_bFontRightAlign), style(CFont::m_FontStyle),
          shadow(CFont::m_nFontShadow), outlineSize(CFont::m_nFontOutlineSize), outline(CFont::m_nFontOutline)
    {
    }

    ~ScopedFontState()
    {
        *CFont::m_Color = color;
        *CFont::m_Scale = scale;
        CFont::m_bFontJustify = justify;
        CFont::m_bFontCentreAlign = centre;
        CFont::m_bFontRightAlign = right;
        CFont::m_FontStyle = style;
        CFont::m_nFontShadow = shadow;
        CFont::m_nFontOutlineSize = outlineSize;
        CFont::m_nFontOutline = outline;
    }

private:
    CRGBA color;
    CVector2D scale;
    bool justify;
    bool centre;
    bool right;
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
          raster(plugin::GetRenderRaster(rwRENDERSTATETEXTURERASTER))
    {
    }

    ~ScopedRenderState()
    {
        plugin::SetRenderState(rwRENDERSTATETEXTUREFILTER, textureFilter);
        plugin::SetRenderState(rwRENDERSTATEZTESTENABLE, zTest);
        plugin::SetRenderState(rwRENDERSTATEZWRITEENABLE, zWrite);
        plugin::SetRenderRaster(raster);
    }

private:
    unsigned int textureFilter;
    unsigned int zTest;
    unsigned int zWrite;
    RwRaster* raster;
};
}

void CNetworkPlayerList::DrawBox(const CScreenTransform& transform, float fX, float fY)
{
    CRect rect = CRect(transform.X(fX), transform.Y(fY), transform.X(fX + BOX_WIDTH), transform.Y(fY + BOX_HEIGHT));

    CSprite2d::DrawRect(rect, CRGBA(0, 0, 0, 190));

    CFont::SetColor(CRGBA(225, 225, 225, 255));

    CFont::SetEdge(2);
    CFont::SetOrientation(eFontAlignment::ALIGN_LEFT);
    CFont::SetFontStyle(0);

    CFont::SetScaleForCurrentlanguage(transform.Width(0.6f), transform.Height(0.9f));

    CFont::PrintString(rect.left + transform.Width(8.0f), std::min(rect.bottom, rect.top) - transform.Height(10.0f), "Players");
}

void CNetworkPlayerList::DrawPing(const CScreenTransform& transform, CNetworkPlayer* pNetworkPlayer, float fX, float fY)
{
    uint32_t nRTT = pNetworkPlayer == nullptr ? CNetwork::GetRTT() : pNetworkPlayer->m_nRTT;
    uint32_t nPingStripesNum = PING_STRIPES;

    static const CRGBA colGreen = CRGBA(35, 176, 74, 255);
    static const CRGBA colYellow = CRGBA(253, 197, 0, 255);
    static const CRGBA colRed = CRGBA(255, 45, 45, 255);

    CRGBA pingColor;

    if (nRTT <= 60)
    {
        nPingStripesNum = PING_STRIPES;
        pingColor = colGreen;
    }
    else if (nRTT <= 100)
    {
        nPingStripesNum = PING_STRIPES - 1;
        pingColor = colGreen;
    }
    else if (nRTT <= 150)
    {
        nPingStripesNum = PING_STRIPES - 2;
        pingColor = colYellow;
    }
    else
    {
        nPingStripesNum = PING_STRIPES - 3;
        pingColor = colRed;
    }

    CRect rect = CRect(transform.X(fX + PING_OFFSET_X), transform.Y(fY + PING_OFFSET_Y),
        transform.X(fX + PING_OFFSET_X + PING_SCALE_X), transform.Y(fY + PING_OFFSET_Y + PING_SCALE_Y));

    for (uint8_t i = 0; i < nPingStripesNum; i++)
    {
        CSprite2d::DrawRect(rect, pingColor);

        rect.left += transform.Width(PING_SPACE_X);
        rect.right += transform.Width(PING_SPACE_X);
        rect.top -= transform.Height(PING_ADD_SCALE_Y);
    }

    for (; nPingStripesNum < PING_STRIPES; nPingStripesNum++)
    {
        pingColor.a = 100;
        CSprite2d::DrawRect(rect, pingColor);

        rect.left += transform.Width(PING_SPACE_X);
        rect.right += transform.Width(PING_SPACE_X);
        rect.top -= transform.Height(PING_ADD_SCALE_Y);
    }

    CFont::SetOrientation(eFontAlignment::ALIGN_CENTER);
    CFont::SetColor(CRGBA(255, 255, 255, 255));
    CFont::SetFontStyle(1);
    CFont::SetEdge(0);

    char buffer[16];
    _itoa_s(nRTT, buffer, sizeof(buffer), 10);

    CFont::SetScale(transform.Width(PING_COUNT_SCALE_X), transform.Height(PING_COUNT_SCALE_Y));
    CFont::PrintString(transform.X(fX + PING_OFFSET_X + PING_COUNT_OFFSET_X),
        transform.Y(fY + PING_OFFSET_Y + PING_COUNT_OFFSET_Y - PING_ADD_SCALE_Y * PING_STRIPES), buffer);
}

void CNetworkPlayerList::DrawName(const CScreenTransform& transform, CNetworkPlayer* pNetworkPlayer, float fX, float fY)
{
    char szPlayerName[Config::MAX_NICKNAME_LENGTH + 1];

    if (pNetworkPlayer == nullptr)
    {
        strcpy_s(szPlayerName, CLocalPlayer::m_Name);
    }
    else
    {
        strcpy_s(szPlayerName, pNetworkPlayer->GetName().c_str());
    }

    const size_t maxNameLength = sizeof(szPlayerName) / sizeof(char) - 1;
    const size_t nameLength = std::min(strlen(szPlayerName), maxNameLength);
    const float fNormalizedValue = nameLength > 1
        ? static_cast<float>(nameLength - 1) / static_cast<float>(maxNameLength - 1)
        : 0.0f;

    CFont::SetScale(transform.Width(MAX_NAME_SCALE_X - (MAX_NAME_SCALE_X - MIN_NAME_SCALE_X) * fNormalizedValue),
        transform.Height(MAX_NAME_SCALE_Y - (MAX_NAME_SCALE_Y - MIN_NAME_SCALE_Y) * fNormalizedValue));

    CFont::SetOrientation(eFontAlignment::ALIGN_LEFT);
    CFont::SetColor(CRGBA(255, 255, 255, 255));
    CFont::SetFontStyle(1);
    CFont::SetEdge(1);

    CFont::PrintString(transform.X(fX + NAME_OFFSET_X),
        transform.Y(fY + NAME_OFFSET_Y) + transform.Height(4.0f + MAX_NAME_OFFSET_Y * fNormalizedValue), szPlayerName);
}

void CNetworkPlayerList::DrawBars(const CScreenTransform& transform, CPlayerPed* pPlayerPed, float fX, float fY)
{
    if (!pPlayerPed)
        return;

    float fBarOffsetX = transform.X(fX + BAR_OFFSET_X + BOX_WIDTH / 2.0f);
    float fBarOffsetY = transform.Y(fY + BAR_OFFSET_Y);

    uint16_t barWidth = static_cast<uint16_t>(transform.Width(BAR_WIDTH));
    uint8_t barHeight = static_cast<uint8_t>(transform.Height(BAR_HEIGHT));

    if (pPlayerPed->m_fArmour != 0.0f)
    {
        CSprite2d::DrawBarChart(fBarOffsetX + barWidth,
            fBarOffsetY - transform.Height(BAR_OFFSET_Y + BAR_ARMOUR_OFFSET_Y), barWidth,
            barHeight, pPlayerPed->m_fArmour, 0, 0, 1, HudColour.GetRGBA(HUD_COLOUR_WHITE), CRGBA(0, 0, 0, 0));
    }

    CSprite2d::DrawBarChart(fBarOffsetX + barWidth, fBarOffsetY, barWidth, barHeight, pPlayerPed->m_fHealth, 0, 0, 1,
        HudColour.GetRGBA(HUD_COLOUR_RED), CRGBA(0, 0, 0, 0));
}

void CNetworkPlayerList::DrawWeaponIcon(const CScreenTransform& transform, CPlayerPed* pPlayerPed, float fX, float fY)
{
    if (!pPlayerPed)
        return;

    ScopedRenderState renderState;
    RwRenderStateSet(rwRENDERSTATETEXTUREFILTER, RWRSTATE(rwFILTERLINEAR));

    int nModelId = CUtil::GetWeaponModelById(pPlayerPed->GetWeapon().m_eWeaponType);

    float fWidth = transform.Width(47.0f / 2.0f);
    float fHeight = transform.Height(58.0f / 2.0f);
    float fHalfWidth = fWidth / 2.0f;
    float fHalfHeight = fHeight / 2.0f;

    float fOffsetX = transform.X(fX + BOX_WIDTH) - fWidth - transform.Width(7.0f);
    float fOffsetY = transform.Y(fY + NAME_OFFSET_Y) - fHalfHeight / 2.0f;

    if (nModelId <= 0)
    {
        CHud::Sprites[0].Draw({fOffsetX, fOffsetY, fWidth + fOffsetX, fHeight + fOffsetY}, CRGBA(255, 255, 255, 255));
        return;
    }

    CBaseModelInfo* mi = CModelInfo::GetModelInfo(nModelId);
    if (!mi || !CTxdStore::ms_pTxdPool)
        return;

    TxdDef* txd = CTxdStore::ms_pTxdPool->GetAt(mi->m_nTxdIndex);
    if (txd == nullptr)
    {
        return;
    }

    RwTexture* texture =
        RwTexDictionaryFindHashNamedTexture(txd->m_pRwDictionary, CKeyGen::AppendStringToKey(mi->m_nKey, "ICON"));
    if (texture == nullptr)
    {
        return;
    }

    RwRenderStateSet(rwRENDERSTATEZTESTENABLE, RWRSTATE(FALSE));
    RwRenderStateSet(rwRENDERSTATETEXTURERASTER, RWRSTATE(RwTextureGetRaster(texture)));

    CSprite::RenderOneXLUSprite(fOffsetX + fHalfWidth, fOffsetY + fHalfHeight, 1.0f, fHalfWidth, fHalfHeight, 255, 255,
        255, 255, 1.0f, 255, 0, 0);

}

void CNetworkPlayerList::DrawSeparator(const CScreenTransform& transform, float fCenterBoxX, float fCenterBoxY, float fColumnY)
{
    float fSeparatorX = fCenterBoxX + SEPARATOR_PADDING_X;
    float fSeparatorY = fColumnY + fCenterBoxY / 2.0f + SEPARATOR_OFFSET_Y;

    CSprite2d::DrawRect(
        CRect(transform.X(fSeparatorX), transform.Y(fSeparatorY),
            transform.X(fSeparatorX + BOX_WIDTH + SEPARATOR_WIDTH), transform.Y(fSeparatorY + SEPARATOR_HEIGHT)),
        CRGBA(169, 169, 169, 130));
}

void CNetworkPlayerList::Draw()
{
    const CScreenTransform transform = CUtil::GetScreenTransform();
    if (CPad::NewKeyState.tab == 0 || !transform.valid || !RwD3D9GetCurrentD3DDevice())
    {
        return;
    }

    ScopedFontState fontState;

    float fBoxX = CUtil::SCREEN_BASE_WIDTH / 2.0f - BOX_WIDTH / 2.0f;
    float fBoxY = CUtil::SCREEN_BASE_HEIGHT / 2.0f - BOX_HEIGHT / 2.0f;

    const size_t nPlayerCount = CNetworkPlayerManager::m_pPlayers.size();
    std::vector<int> vPlayerId;

    vPlayerId.reserve(nPlayerCount + 1);

    for (size_t i = 0; i < nPlayerCount; i++)
    {
        if (CNetworkPlayerManager::m_pPlayers[i])
            vPlayerId.push_back(CNetworkPlayerManager::m_pPlayers[i]->m_iPlayerId);
    }

    vPlayerId.push_back(CNetworkPlayerManager::m_nMyId);
    std::sort(vPlayerId.begin(), vPlayerId.end());

    DrawBox(transform, fBoxX, fBoxY);

    for (size_t i = 0; i < vPlayerId.size() && i < Config::MAX_SERVER_PLAYERS; i++)
    {
        CNetworkPlayer* pNetworkPlayer = nullptr;
        CPlayerPed* pPlayerPed = nullptr;

        if (vPlayerId[i] == CNetworkPlayerManager::m_nMyId)
        {
            pPlayerPed = FindPlayerPed(0);
        }
        else
        {
            for (CNetworkPlayer* pPlayer : CNetworkPlayerManager::m_pPlayers)
            {
                if (pPlayer && pPlayer->m_iPlayerId == vPlayerId[i])
                {
                    pNetworkPlayer = pPlayer;
                    pPlayerPed = pPlayer->m_pPed;
                    break;
                }
            }
        }

        float fColumnY = fBoxY + i * COLUMN_HEIGHT;

        DrawPing(transform, pNetworkPlayer, fBoxX, fColumnY);
        DrawName(transform, pNetworkPlayer, fBoxX, fColumnY);
        DrawBars(transform, pPlayerPed, fBoxX, fColumnY);
        DrawWeaponIcon(transform, pPlayerPed, fBoxX, fColumnY);

        if (i + 1 >= vPlayerId.size())
        {
            continue;
        }

        DrawSeparator(transform, fBoxX, fBoxY, fColumnY);
    }
}
