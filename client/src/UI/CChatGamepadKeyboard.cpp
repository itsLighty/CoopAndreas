#include "stdafx.h"
#include "CChatGamepadKeyboard.h"

#include "CChat.h"

namespace
{
using Key = CChatGamepadKeyboard::Key;
using KeyAction = CChatGamepadKeyboard::KeyAction;

constexpr Key ROW_DIGITS[] = {
    {L"1", L"!", KeyAction::Character}, {L"2", L"@", KeyAction::Character},
    {L"3", L"#", KeyAction::Character}, {L"4", L"$", KeyAction::Character},
    {L"5", L"%", KeyAction::Character}, {L"6", L"^", KeyAction::Character},
    {L"7", L"&", KeyAction::Character}, {L"8", L"*", KeyAction::Character},
    {L"9", L"(", KeyAction::Character}, {L"0", L")", KeyAction::Character},
};

constexpr Key ROW_QWERTY[] = {
    {L"q", L"Q", KeyAction::Character}, {L"w", L"W", KeyAction::Character},
    {L"e", L"E", KeyAction::Character}, {L"r", L"R", KeyAction::Character},
    {L"t", L"T", KeyAction::Character}, {L"y", L"Y", KeyAction::Character},
    {L"u", L"U", KeyAction::Character}, {L"i", L"I", KeyAction::Character},
    {L"o", L"O", KeyAction::Character}, {L"p", L"P", KeyAction::Character},
};

constexpr Key ROW_HOME[] = {
    {L"a", L"A", KeyAction::Character}, {L"s", L"S", KeyAction::Character},
    {L"d", L"D", KeyAction::Character}, {L"f", L"F", KeyAction::Character},
    {L"g", L"G", KeyAction::Character}, {L"h", L"H", KeyAction::Character},
    {L"j", L"J", KeyAction::Character}, {L"k", L"K", KeyAction::Character},
    {L"l", L"L", KeyAction::Character}, {L"'", L"\"", KeyAction::Character},
};

constexpr Key ROW_BOTTOM[] = {
    {L"z", L"Z", KeyAction::Character}, {L"x", L"X", KeyAction::Character},
    {L"c", L"C", KeyAction::Character}, {L"v", L"V", KeyAction::Character},
    {L"b", L"B", KeyAction::Character}, {L"n", L"N", KeyAction::Character},
    {L"m", L"M", KeyAction::Character}, {L",", L"<", KeyAction::Character},
    {L".", L">", KeyAction::Character}, {L"/", L"?", KeyAction::Character},
};

constexpr Key ROW_ACTIONS[] = {
    {L"Shift", L"Shift", KeyAction::Shift}, {L"Caps", L"Caps", KeyAction::CapsLock},
    {L"Space", L"Space", KeyAction::Space}, {L"Back", L"Back", KeyAction::Backspace},
    {L"Clear", L"Clear", KeyAction::Clear}, {L"Cancel", L"Cancel", KeyAction::Cancel},
    {L"Send", L"Send", KeyAction::Send},
};

struct Row
{
    const Key* keys;
    size_t count;
};

constexpr Row KEY_ROWS[] = {
    {ROW_DIGITS, ARRAY_SIZE(ROW_DIGITS)},
    {ROW_QWERTY, ARRAY_SIZE(ROW_QWERTY)},
    {ROW_HOME, ARRAY_SIZE(ROW_HOME)},
    {ROW_BOTTOM, ARRAY_SIZE(ROW_BOTTOM)},
    {ROW_ACTIONS, ARRAY_SIZE(ROW_ACTIONS)},
};

constexpr float PANEL_X = 14.0f;
constexpr float PANEL_Y = 200.0f;
constexpr float PANEL_WIDTH = 612.0f;
constexpr float PANEL_HEIGHT = 244.0f;
constexpr float PANEL_PADDING = 7.0f;
constexpr float HEADER_HEIGHT = 22.0f;
constexpr float KEY_HEIGHT = 35.0f;
constexpr float KEY_GAP = 3.0f;
constexpr float FOOTER_HEIGHT = 29.0f;

void DrawVirtualRect(const CScreenTransform& transform, float left, float top, float right, float bottom,
    const CRGBA& color)
{
    CSprite2d::DrawRect(
        CRect(transform.X(left), transform.Y(top), transform.X(right), transform.Y(bottom)), color);
}
}

bool CChatGamepadKeyboard::m_bActive = false;
bool CChatGamepadKeyboard::m_bShift = false;
bool CChatGamepadKeyboard::m_bCapsLock = false;
bool CChatGamepadKeyboard::m_bOpenChordHeld = false;
bool CChatGamepadKeyboard::m_bConfirmHeld = false;
bool CChatGamepadKeyboard::m_bCancelHeld = false;
bool CChatGamepadKeyboard::m_bBackspaceHeld = false;
bool CChatGamepadKeyboard::m_bShiftShortcutHeld = false;
bool CChatGamepadKeyboard::m_bControlLockHeld = false;
bool CChatGamepadKeyboard::m_bReleaseLockWhenButtonsUp = false;
bool CChatGamepadKeyboard::m_bSubmissionInFlight = false;
size_t CChatGamepadKeyboard::m_nSelectedRow = 0;
size_t CChatGamepadKeyboard::m_nSelectedColumn = 0;
CChatGamepadKeyboard::Direction CChatGamepadKeyboard::m_eHeldDirection =
    CChatGamepadKeyboard::Direction::None;
uint32_t CChatGamepadKeyboard::m_nDirectionStartedAt = 0;
uint32_t CChatGamepadKeyboard::m_nLastDirectionAt = 0;

bool CChatGamepadKeyboard::IsActive()
{
    return m_bActive;
}

size_t CChatGamepadKeyboard::GetRowCount()
{
    return ARRAY_SIZE(KEY_ROWS);
}

size_t CChatGamepadKeyboard::GetColumnCount(size_t row)
{
    return row < GetRowCount() ? KEY_ROWS[row].count : 0;
}

const CChatGamepadKeyboard::Key& CChatGamepadKeyboard::GetKey(size_t row, size_t column)
{
    assert(row < GetRowCount());
    assert(column < GetColumnCount(row));
    return KEY_ROWS[row].keys[column];
}

bool CChatGamepadKeyboard::CanOpen(CPad* pad, CPlayerPed* player)
{
    return CNetwork::m_bAuthenticated && pad != nullptr && player != nullptr && player->IsAlive() &&
        player->m_pIntelligence != nullptr && pad->DisablePlayerControls == 0 &&
        !FrontEndMenuManager.m_bMenuActive && !CTimer::m_UserPause && !CTimer::m_CodePause &&
        !CCutsceneMgr::ms_running && !TheCamera.m_bWideScreenOn;
}

bool CChatGamepadKeyboard::HasExternalControlBlock(CPad* pad)
{
    return pad == nullptr || (pad->DisablePlayerControls & ~CONTROL_LOCK_MASK) != 0;
}

void CChatGamepadKeyboard::Open(CPad* pad)
{
    if (m_bActive || pad == nullptr)
        return;

    if (!CChat::m_bInputActive)
        CChat::ToggleInput(true);

    m_bActive = true;
    m_bShift = false;
    m_bCapsLock = false;
    m_bReleaseLockWhenButtonsUp = false;
    m_bSubmissionInFlight = false;
    m_nSelectedRow = 1;
    m_nSelectedColumn = 0;
    ResetTransientInput();

    pad->DisablePlayerControls |= CONTROL_LOCK_MASK;
    m_bControlLockHeld = true;
    ConsumeGameplayInput(pad);
}

void CChatGamepadKeyboard::Close(bool clearDraft)
{
    if (clearDraft)
        CChat::ClearInputText();

    if (CChat::m_bInputActive)
        CChat::ToggleInput(false);
    else
        OnChatInputClosed();
}

void CChatGamepadKeyboard::BeginGamepadClose(CPad* pad)
{
    // The close button was sampled before CGame::Process. Neutralize that
    // sample now and retain the lock through button-up so Cross/Circle cannot
    // become a gameplay action on this or the following frame.
    ConsumeGameplayInput(pad);
    m_bReleaseLockWhenButtonsUp = true;
}

bool CChatGamepadKeyboard::ProcessDeferredControlRelease(CPad* pad, const CControllerState& state)
{
    if (!m_bReleaseLockWhenButtonsUp)
        return false;

    const bool closeButtonDown = state.ButtonCross != 0 || state.ButtonCircle != 0;
    if (m_bControlLockHeld && pad != nullptr)
        pad->DisablePlayerControls |= CONTROL_LOCK_MASK;
    ConsumeGameplayInput(pad);
    if (!closeButtonDown)
    {
        ReleaseControlLock(pad);
        m_bReleaseLockWhenButtonsUp = false;
    }
    return true;
}

void CChatGamepadKeyboard::ReleaseControlLock(CPad* pad)
{
    if (m_bControlLockHeld && pad != nullptr)
        pad->DisablePlayerControls &= ~CONTROL_LOCK_MASK;
    m_bControlLockHeld = false;
}

void CChatGamepadKeyboard::OnChatInputClosed()
{
    CPad* pad = CPad::GetPad(0);
    if (!m_bReleaseLockWhenButtonsUp)
        ReleaseControlLock(pad);

    m_bActive = false;
    m_bShift = false;
    m_bCapsLock = false;
    m_bSubmissionInFlight = false;
    m_nSelectedRow = 0;
    m_nSelectedColumn = 0;
    ResetTransientInput();
}

void CChatGamepadKeyboard::ResetTransientInput()
{
    m_bConfirmHeld = false;
    m_bCancelHeld = false;
    m_bBackspaceHeld = false;
    m_bShiftShortcutHeld = false;
    m_eHeldDirection = Direction::None;
    m_nDirectionStartedAt = 0;
    m_nLastDirectionAt = 0;
}

void CChatGamepadKeyboard::ConsumeGameplayInput(CPad* pad)
{
    if (pad == nullptr)
        return;

    memset(&pad->PCTempJoyState, 0, sizeof(pad->PCTempJoyState));
    memset(&pad->NewState, 0, sizeof(pad->NewState));
    memset(&pad->OldState, 0, sizeof(pad->OldState));
}

bool CChatGamepadKeyboard::IsOpenChordDown(const CControllerState& state)
{
    return state.m_bChatIndicated != 0 || (state.Select != 0 && state.DPadDown != 0);
}

CChatGamepadKeyboard::Direction CChatGamepadKeyboard::ReadDirection(const CControllerState& state)
{
    if (state.DPadUp != 0)
        return Direction::Up;
    if (state.DPadDown != 0)
        return Direction::Down;
    if (state.DPadLeft != 0)
        return Direction::Left;
    if (state.DPadRight != 0)
        return Direction::Right;

    const int x = state.LeftStickX;
    const int y = state.LeftStickY;
    if (std::abs(x) < STICK_DEAD_ZONE && std::abs(y) < STICK_DEAD_ZONE)
        return Direction::None;

    if (std::abs(x) > std::abs(y))
        return x < 0 ? Direction::Left : Direction::Right;
    return y < 0 ? Direction::Up : Direction::Down;
}

bool CChatGamepadKeyboard::ConsumeButton(bool down, bool& held)
{
    if (!down)
    {
        held = false;
        return false;
    }

    if (held)
        return false;

    held = true;
    return true;
}

bool CChatGamepadKeyboard::ConsumeDirection(Direction direction, uint32_t now)
{
    if (direction == Direction::None)
    {
        m_eHeldDirection = Direction::None;
        m_nDirectionStartedAt = 0;
        m_nLastDirectionAt = 0;
        return false;
    }

    if (direction != m_eHeldDirection)
    {
        m_eHeldDirection = direction;
        m_nDirectionStartedAt = now;
        m_nLastDirectionAt = now;
        return true;
    }

    if (now - m_nDirectionStartedAt < NAV_INITIAL_REPEAT_DELAY_MS ||
        now - m_nLastDirectionAt < NAV_REPEAT_INTERVAL_MS)
        return false;

    m_nLastDirectionAt = now;
    return true;
}

void CChatGamepadKeyboard::MoveSelection(Direction direction)
{
    const size_t rowCount = GetRowCount();
    if (rowCount == 0)
        return;

    if (direction == Direction::Left || direction == Direction::Right)
    {
        const size_t columnCount = GetColumnCount(m_nSelectedRow);
        if (columnCount == 0)
            return;

        if (direction == Direction::Left)
            m_nSelectedColumn = m_nSelectedColumn == 0 ? columnCount - 1 : m_nSelectedColumn - 1;
        else
            m_nSelectedColumn = (m_nSelectedColumn + 1) % columnCount;
        return;
    }

    if (direction == Direction::Up)
        m_nSelectedRow = m_nSelectedRow == 0 ? rowCount - 1 : m_nSelectedRow - 1;
    else if (direction == Direction::Down)
        m_nSelectedRow = (m_nSelectedRow + 1) % rowCount;
    else
        return;

    const size_t columnCount = GetColumnCount(m_nSelectedRow);
    m_nSelectedColumn = columnCount == 0 ? 0 : std::min(m_nSelectedColumn, columnCount - 1);
}

std::wstring CChatGamepadKeyboard::GetKeyLabel(const Key& key)
{
    if (key.action != KeyAction::Character)
        return key.normalLabel;

    const bool isLetter = key.normalLabel[0] >= L'a' && key.normalLabel[0] <= L'z' &&
        key.normalLabel[1] == L'\0';
    const bool shifted = isLetter ? (m_bShift != m_bCapsLock) : m_bShift;
    return shifted ? key.shiftedLabel : key.normalLabel;
}

void CChatGamepadKeyboard::ActivateSelectedKey(CPad* pad)
{
    if (m_bSubmissionInFlight)
        return;

    const Key& key = GetKey(m_nSelectedRow, m_nSelectedColumn);
    switch (key.action)
    {
        case KeyAction::Character:
            CChat::InsertTextAtCaret(GetKeyLabel(key));
            m_bShift = false;
            break;
        case KeyAction::Shift:
            m_bShift = !m_bShift;
            break;
        case KeyAction::CapsLock:
            m_bCapsLock = !m_bCapsLock;
            break;
        case KeyAction::Space:
            CChat::InsertTextAtCaret(L" ");
            m_bShift = false;
            break;
        case KeyAction::Backspace:
            CChat::EraseCharacter(CChat::m_sInputText, 0);
            break;
        case KeyAction::Clear:
            CChat::ClearInputText();
            break;
        case KeyAction::Cancel:
            BeginGamepadClose(pad);
            Close(true);
            break;
        case KeyAction::Send:
            m_bSubmissionInFlight = true;
            BeginGamepadClose(pad);
            if (!CChat::SubmitInput())
            {
                m_bReleaseLockWhenButtonsUp = false;
                m_bSubmissionInFlight = false;
            }
            break;
    }
}

void CChatGamepadKeyboard::Process()
{
    CPad* pad = CPad::GetPad(0);
    CPlayerPed* player = FindPlayerPed(0);
    if (pad == nullptr)
    {
        if (m_bActive)
            Close(true);
        return;
    }

    const CControllerState gamepadState = pad->PCTempJoyState;
    if (ProcessDeferredControlRelease(pad, gamepadState))
        return;

    const bool openChordDown = IsOpenChordDown(gamepadState);
    if (!openChordDown)
        m_bOpenChordHeld = false;

    if (!m_bActive)
    {
        if (openChordDown && !m_bOpenChordHeld)
        {
            m_bOpenChordHeld = true;
            if (CanOpen(pad, player))
                Open(pad);
        }
        return;
    }

    const bool unavailable = !CNetwork::m_bAuthenticated || player == nullptr || !player->IsAlive() ||
        player->m_pIntelligence == nullptr || FrontEndMenuManager.m_bMenuActive || CTimer::m_UserPause ||
        CTimer::m_CodePause || CCutsceneMgr::ms_running || TheCamera.m_bWideScreenOn ||
        HasExternalControlBlock(pad) || !CChat::m_bInputActive;
    if (unavailable)
    {
        ConsumeGameplayInput(pad);
        Close(true);
        return;
    }

    // Other systems may rebuild this bitfield while chat is open. Retain our
    // dedicated bit without overwriting any unrelated control-lock reasons.
    pad->DisablePlayerControls |= CONTROL_LOCK_MASK;

    // Do not interpret the opening chord's D-pad component as keyboard navigation.
    if (m_bOpenChordHeld)
    {
        ConsumeGameplayInput(pad);
        return;
    }

    const uint32_t now = GetTickCount();
    const Direction direction = ReadDirection(gamepadState);
    if (ConsumeDirection(direction, now))
        MoveSelection(direction);

    const bool cancelPressed = ConsumeButton(gamepadState.ButtonCircle != 0, m_bCancelHeld);
    const bool confirmPressed = ConsumeButton(gamepadState.ButtonCross != 0, m_bConfirmHeld);
    const bool backspacePressed = ConsumeButton(gamepadState.ButtonSquare != 0, m_bBackspaceHeld);
    const bool shiftPressed = ConsumeButton(gamepadState.ButtonTriangle != 0, m_bShiftShortcutHeld);

    if (cancelPressed)
    {
        BeginGamepadClose(pad);
        Close(true);
        return;
    }
    if (backspacePressed)
        CChat::EraseCharacter(CChat::m_sInputText, 0);
    if (shiftPressed)
        m_bShift = !m_bShift;
    if (confirmPressed)
    {
        // Character keys also consume their confirm sample below; send/cancel
        // begin a deferred release before they close the chat.
        ActivateSelectedKey(pad);
        if (!m_bActive)
            return;
    }

    ConsumeGameplayInput(pad);
}

void CChatGamepadKeyboard::Draw()
{
    if (!m_bActive || !CChat::m_bInputActive || FrontEndMenuManager.m_bMenuActive)
        return;

    const CScreenTransform transform = CUtil::GetScreenTransform();
    if (!transform.valid || RwD3D9GetCurrentD3DDevice() == nullptr)
        return;

    CDXFont::GetTextWidth(L" ");
    if (CDXFont::m_pD3DXFont == nullptr || CDXFont::m_fFontSize == 0)
        return;

    DrawVirtualRect(transform, PANEL_X, PANEL_Y, PANEL_X + PANEL_WIDTH, PANEL_Y + PANEL_HEIGHT,
        CRGBA(8, 12, 20, 225));
    DrawVirtualRect(transform, PANEL_X + 1.0f, PANEL_Y + 1.0f, PANEL_X + PANEL_WIDTH - 1.0f,
        PANEL_Y + HEADER_HEIGHT, CRGBA(24, 37, 58, 245));

    const std::wstring title = L"Controller chat keyboard";
    CDXFont::Draw(static_cast<int>(std::lround(transform.X(PANEL_X + PANEL_PADDING))),
        static_cast<int>(std::lround(transform.Y(PANEL_Y + 3.0f))), title, D3DCOLOR_RGBA(245, 245, 245, 255));

    const float contentLeft = PANEL_X + PANEL_PADDING;
    const float contentWidth = PANEL_WIDTH - PANEL_PADDING * 2.0f;
    const float rowsTop = PANEL_Y + HEADER_HEIGHT + 4.0f;

    for (size_t row = 0; row < GetRowCount(); ++row)
    {
        const size_t columnCount = GetColumnCount(row);
        if (columnCount == 0)
            continue;

        const float keyWidth = (contentWidth - KEY_GAP * static_cast<float>(columnCount - 1)) /
            static_cast<float>(columnCount);
        const float top = rowsTop + static_cast<float>(row) * (KEY_HEIGHT + KEY_GAP);
        for (size_t column = 0; column < columnCount; ++column)
        {
            const Key& key = GetKey(row, column);
            const bool selected = row == m_nSelectedRow && column == m_nSelectedColumn;
            const bool activeModifier = (key.action == KeyAction::Shift && m_bShift) ||
                (key.action == KeyAction::CapsLock && m_bCapsLock);
            const float left = contentLeft + static_cast<float>(column) * (keyWidth + KEY_GAP);
            const CRGBA fill = selected ? CRGBA(42, 128, 206, 255) :
                activeModifier ? CRGBA(39, 126, 86, 245) : CRGBA(37, 48, 66, 240);

            DrawVirtualRect(transform, left, top, left + keyWidth, top + KEY_HEIGHT, fill);
            if (selected)
            {
                constexpr float border = 1.5f;
                DrawVirtualRect(transform, left, top, left + keyWidth, top + border, CRGBA(255, 214, 76, 255));
                DrawVirtualRect(transform, left, top + KEY_HEIGHT - border, left + keyWidth, top + KEY_HEIGHT,
                    CRGBA(255, 214, 76, 255));
                DrawVirtualRect(transform, left, top, left + border, top + KEY_HEIGHT, CRGBA(255, 214, 76, 255));
                DrawVirtualRect(transform, left + keyWidth - border, top, left + keyWidth, top + KEY_HEIGHT,
                    CRGBA(255, 214, 76, 255));
            }

            const std::wstring label = GetKeyLabel(key);
            const int textWidth = CDXFont::GetTextWidth(label);
            const int textX = static_cast<int>(std::lround(transform.X(left) +
                (transform.Width(keyWidth) - static_cast<float>(textWidth)) * 0.5f));
            const int textY = static_cast<int>(std::lround(transform.Y(top) +
                (transform.Height(KEY_HEIGHT) - static_cast<float>(CDXFont::m_fFontSize)) * 0.5f));
            CDXFont::Draw(textX, textY, label, D3DCOLOR_RGBA(255, 255, 255, 255));
        }
    }

    const float footerY = PANEL_Y + PANEL_HEIGHT - FOOTER_HEIGHT;
    CDXFont::Draw(static_cast<int>(std::lround(transform.X(PANEL_X + PANEL_PADDING))),
        static_cast<int>(std::lround(transform.Y(footerY))),
        L"D-pad/stick: move   A/Cross: select   X/Square: back",
        D3DCOLOR_RGBA(195, 207, 224, 255));
    CDXFont::Draw(static_cast<int>(std::lround(transform.X(PANEL_X + PANEL_PADDING))),
        static_cast<int>(std::lround(transform.Y(footerY + 13.0f))),
        L"Y/Triangle: shift   B/Circle: cancel   Back/Select + D-pad Down: open",
        D3DCOLOR_RGBA(195, 207, 224, 255));
}
