#pragma once

#include "config.h"

#include <array>
#include <cstddef>
#include <cstdint>
#include <deque>
#include <string>

struct RwTexDictionary;
struct RwTexture;

class CChatReactions
{
public:
    enum class Reaction : uint8_t
    {
        GoodChat,
        BadChat,
        ThumbUp,
        ThumbDown,
        Count
    };

    struct CommandResult
    {
        bool recognized{};
        bool accepted{};
        Reaction reaction{};
        const wchar_t* displayName{};
    };

    static inline constexpr size_t MAX_QUEUED_REACTIONS = 6;
    static inline constexpr size_t MAX_VISIBLE_REACTIONS = 4;
    static inline constexpr uint32_t REACTION_RATE_LIMIT_MS = 1200;
    static inline constexpr uint32_t REACTION_VISIBLE_TIME_MS = 3500;
    static inline constexpr uint32_t REACTION_FADE_TIME_MS = 700;

    // Canonical commands are "/react good", "/react bad", "/react up",
    // and "/react down". The controller-friendly "/r ..." forms are exact
    // aliases and can be entered with the existing gamepad OSK.
    static CommandResult HandleAuthenticatedMessage(
        const std::wstring& playerName, int playerId, const wchar_t* message);
    static void Draw();
    static void Reset();

private:
    struct ReactionToast
    {
        Reaction reaction{};
        std::wstring caption;
        uint32_t createdAt{};
    };

    struct SenderRateLimit
    {
        uint32_t lastAcceptedAt{};
        bool hasAccepted{};
    };

    static std::deque<ReactionToast> m_aToasts;
    static std::array<SenderRateLimit, Config::MAX_SERVER_PLAYERS> m_aSenderRateLimits;
    static std::array<RwTexture*, static_cast<size_t>(Reaction::Count)> m_aTextures;
    static RwTexDictionary* m_pTextureDictionary;
    static bool m_bSessionAuthenticated;
    static bool m_bTextureLoadAttempted;

    static bool EnsureAuthenticatedSession();
    static bool EnsureTexturesLoaded();
    static void ReleaseTextures();
};
