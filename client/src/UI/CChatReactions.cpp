#include "stdafx.h"
#include "CChatReactions.h"

#include "CFileLoader.h"

#include <algorithm>
#include <string_view>

namespace
{
struct ReactionDefinition
{
    CChatReactions::Reaction reaction;
    const wchar_t* command;
    const wchar_t* shortCommand;
    const char* textureName;
    const wchar_t* displayName;
};

constexpr ReactionDefinition REACTION_DEFINITIONS[] = {
    {CChatReactions::Reaction::GoodChat, L"/react good", L"/r good", "goodcha", L"good chat"},
    {CChatReactions::Reaction::BadChat, L"/react bad", L"/r bad", "badchat", L"bad chat"},
    {CChatReactions::Reaction::ThumbUp, L"/react up", L"/r up", "thumbup", L"thumbs up"},
    {CChatReactions::Reaction::ThumbDown, L"/react down", L"/r down", "thumbdn", L"thumbs down"},
};

static_assert(ARRAY_SIZE(REACTION_DEFINITIONS) ==
    static_cast<size_t>(CChatReactions::Reaction::Count));

constexpr float PANEL_RIGHT_MARGIN = 12.0f;
constexpr float PANEL_TOP = 42.0f;
constexpr float PANEL_WIDTH = 210.0f;
constexpr float PANEL_HEIGHT = 42.0f;
constexpr float PANEL_GAP = 5.0f;
constexpr float ICON_SIZE = 32.0f;
constexpr float PANEL_PADDING = 5.0f;

class ScopedReactionRenderState
{
public:
    ScopedReactionRenderState()
        : textureFilter(plugin::GetRenderState(rwRENDERSTATETEXTUREFILTER)),
          zTest(plugin::GetRenderState(rwRENDERSTATEZTESTENABLE)),
          zWrite(plugin::GetRenderState(rwRENDERSTATEZWRITEENABLE)),
          raster(plugin::GetRenderRaster(rwRENDERSTATETEXTURERASTER))
    {
    }

    ~ScopedReactionRenderState()
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

const ReactionDefinition* FindReactionDefinition(const wchar_t* message)
{
    if (message == nullptr)
        return nullptr;

    size_t length = 0;
    while (length <= Config::MAX_CHAT_MESSAGE_LENGTH && message[length] != L'\0')
        ++length;
    if (length == 0 || length > Config::MAX_CHAT_MESSAGE_LENGTH)
        return nullptr;

    const std::wstring_view command(message, length);
    for (const ReactionDefinition& definition : REACTION_DEFINITIONS)
    {
        if (command == definition.command || command == definition.shortCommand)
            return &definition;
    }
    return nullptr;
}

std::wstring BuildCaption(const std::wstring& playerName, int playerId, const wchar_t* displayName)
{
    std::wstring boundedName = playerName.substr(0, Config::MAX_NICKNAME_LENGTH);
    return boundedName + L" (" + std::to_wstring(playerId) + L")  " + displayName;
}
}

std::deque<CChatReactions::ReactionToast> CChatReactions::m_aToasts{};
std::array<CChatReactions::SenderRateLimit, Config::MAX_SERVER_PLAYERS>
    CChatReactions::m_aSenderRateLimits{};
std::array<RwTexture*, static_cast<size_t>(CChatReactions::Reaction::Count)>
    CChatReactions::m_aTextures{};
RwTexDictionary* CChatReactions::m_pTextureDictionary = nullptr;
bool CChatReactions::m_bSessionAuthenticated = false;
bool CChatReactions::m_bTextureLoadAttempted = false;

bool CChatReactions::EnsureAuthenticatedSession()
{
    if (!CNetwork::m_bAuthenticated)
    {
        if (m_bSessionAuthenticated || !m_aToasts.empty() || m_pTextureDictionary != nullptr)
            Reset();
        return false;
    }

    if (!m_bSessionAuthenticated)
    {
        m_aToasts.clear();
        m_aSenderRateLimits.fill(SenderRateLimit{});
        m_bSessionAuthenticated = true;
    }
    return true;
}

void CChatReactions::ReleaseTextures()
{
    m_aTextures.fill(nullptr);
    if (m_pTextureDictionary != nullptr)
    {
        RwTexDictionaryDestroy(m_pTextureDictionary);
        m_pTextureDictionary = nullptr;
    }
    m_bTextureLoadAttempted = false;
}

void CChatReactions::Reset()
{
    m_aToasts.clear();
    m_aSenderRateLimits.fill(SenderRateLimit{});
    ReleaseTextures();
    m_bSessionAuthenticated = false;
}

bool CChatReactions::EnsureTexturesLoaded()
{
    if (m_pTextureDictionary != nullptr)
        return true;
    if (m_bTextureLoadAttempted)
        return false;

    m_bTextureLoadAttempted = true;
    m_pTextureDictionary = CFileLoader::LoadTexDictionary("models\\txd\\LD_CHAT.txd");
    if (m_pTextureDictionary == nullptr)
        return false;

    for (const ReactionDefinition& definition : REACTION_DEFINITIONS)
    {
        const size_t index = static_cast<size_t>(definition.reaction);
        m_aTextures[index] =
            RwTexDictionaryFindNamedTexture(m_pTextureDictionary, definition.textureName);
        if (m_aTextures[index] == nullptr)
        {
            ReleaseTextures();
            // Do not repeatedly hit the filesystem during this session when a
            // modified or incomplete LD_CHAT dictionary is installed.
            m_bTextureLoadAttempted = true;
            return false;
        }
    }
    return true;
}

CChatReactions::CommandResult CChatReactions::HandleAuthenticatedMessage(
    const std::wstring& playerName, int playerId, const wchar_t* message)
{
    CommandResult result{};
    if (!EnsureAuthenticatedSession() || playerId < 0 || playerId >= Config::MAX_SERVER_PLAYERS)
        return result;

    const ReactionDefinition* definition = FindReactionDefinition(message);
    if (definition == nullptr)
        return result;

    result.recognized = true;
    result.reaction = definition->reaction;
    result.displayName = definition->displayName;

    const uint32_t now = GetTickCount();
    SenderRateLimit& rateLimit = m_aSenderRateLimits[static_cast<size_t>(playerId)];
    if (rateLimit.hasAccepted && now - rateLimit.lastAcceptedAt < REACTION_RATE_LIMIT_MS)
        return result;

    rateLimit.hasAccepted = true;
    rateLimit.lastAcceptedAt = now;
    if (m_aToasts.size() >= MAX_QUEUED_REACTIONS)
        m_aToasts.pop_front();
    m_aToasts.push_back({definition->reaction,
        BuildCaption(playerName, playerId, definition->displayName), now});
    result.accepted = true;
    return result;
}

void CChatReactions::Draw()
{
    if (!EnsureAuthenticatedSession())
        return;

    const uint32_t now = GetTickCount();
    while (!m_aToasts.empty() && now - m_aToasts.front().createdAt >= REACTION_VISIBLE_TIME_MS)
        m_aToasts.pop_front();
    if (m_aToasts.empty())
        return;

    const CScreenTransform transform = CUtil::GetScreenTransform();
    if (!transform.valid || RwD3D9GetCurrentD3DDevice() == nullptr || !EnsureTexturesLoaded())
        return;

    CDXFont::GetTextWidth(L" ");
    if (CDXFont::m_pD3DXFont == nullptr || CDXFont::m_fFontSize == 0)
        return;

    ScopedReactionRenderState renderState;
    plugin::SetRenderState(rwRENDERSTATETEXTUREFILTER, rwFILTERLINEAR);
    plugin::SetRenderState(rwRENDERSTATEZTESTENABLE, FALSE);
    plugin::SetRenderState(rwRENDERSTATEZWRITEENABLE, FALSE);

    const size_t firstVisible = m_aToasts.size() > MAX_VISIBLE_REACTIONS
        ? m_aToasts.size() - MAX_VISIBLE_REACTIONS : 0;
    size_t row = 0;
    for (size_t index = firstVisible; index < m_aToasts.size(); ++index, ++row)
    {
        const ReactionToast& toast = m_aToasts[index];
        const uint32_t age = now - toast.createdAt;
        uint8_t alpha = 255;
        if (age > REACTION_VISIBLE_TIME_MS - REACTION_FADE_TIME_MS)
        {
            const uint32_t fadeAge = age - (REACTION_VISIBLE_TIME_MS - REACTION_FADE_TIME_MS);
            alpha = static_cast<uint8_t>(255 - (fadeAge * 255) / REACTION_FADE_TIME_MS);
        }

        const float top = PANEL_TOP + static_cast<float>(row) * (PANEL_HEIGHT + PANEL_GAP);
        const float left = CUtil::SCREEN_BASE_WIDTH - PANEL_RIGHT_MARGIN - PANEL_WIDTH;
        const CRect panelRect(transform.X(left), transform.Y(top),
            transform.X(left + PANEL_WIDTH), transform.Y(top + PANEL_HEIGHT));
        CSprite2d::DrawRect(panelRect, CRGBA(8, 12, 20, static_cast<unsigned char>((210 * alpha) / 255)));

        const float iconLeft = left + PANEL_PADDING;
        const float iconTop = top + (PANEL_HEIGHT - ICON_SIZE) * 0.5f;
        RwTexture* texture = m_aTextures[static_cast<size_t>(toast.reaction)];
        plugin::SetRenderRaster(RwTextureGetRaster(texture));
        CSprite2d::DrawTxRect(
            CRect(transform.X(iconLeft), transform.Y(iconTop),
                transform.X(iconLeft + ICON_SIZE), transform.Y(iconTop + ICON_SIZE)),
            CRGBA(255, 255, 255, alpha));

        CDXFont::Draw(static_cast<int>(std::lround(transform.X(left + PANEL_PADDING + ICON_SIZE + 7.0f))),
            static_cast<int>(std::lround(transform.Y(top + 12.0f))), toast.caption,
            D3DCOLOR_RGBA(255, 255, 255, alpha));
    }
}
