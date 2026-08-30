#pragma once

#include <cstddef>
#include <cstdint>
#include <string>

class CControllerState;
class CPad;
class CPlayerPed;

class CChatGamepadKeyboard
{
public:
    enum class KeyAction : uint8_t
    {
        Character,
        Shift,
        CapsLock,
        Space,
        Backspace,
        Clear,
        Cancel,
        Send
    };

    // Dedicated chat-OSK lock in an otherwise unused high bit. It must only be
    // acquired/released with bitwise operations so unrelated game control
    // reasons in DisablePlayerControls are preserved.
    static inline constexpr uint16_t CONTROL_LOCK_MASK = 0x400;
    static inline constexpr int16_t STICK_DEAD_ZONE = 64;
    static inline constexpr uint32_t NAV_INITIAL_REPEAT_DELAY_MS = 350;
    static inline constexpr uint32_t NAV_REPEAT_INTERVAL_MS = 90;

    static void Process();
    static void Draw();
    static void OnChatInputClosed();
    static bool IsActive();

    struct Key
    {
        const wchar_t* normalLabel;
        const wchar_t* shiftedLabel;
        KeyAction action;
    };

private:
    enum class Direction : uint8_t
    {
        None,
        Up,
        Down,
        Left,
        Right
    };

    static bool m_bActive;
    static bool m_bShift;
    static bool m_bCapsLock;
    static bool m_bOpenChordHeld;
    static bool m_bConfirmHeld;
    static bool m_bCancelHeld;
    static bool m_bBackspaceHeld;
    static bool m_bShiftShortcutHeld;
    static bool m_bControlLockHeld;
    static bool m_bReleaseLockWhenButtonsUp;
    static bool m_bSubmissionInFlight;
    static size_t m_nSelectedRow;
    static size_t m_nSelectedColumn;
    static Direction m_eHeldDirection;
    static uint32_t m_nDirectionStartedAt;
    static uint32_t m_nLastDirectionAt;

    static bool CanOpen(CPad* pad, CPlayerPed* player);
    static bool HasExternalControlBlock(CPad* pad);
    static void Open(CPad* pad);
    static void Close(bool clearDraft);
    static void BeginGamepadClose(CPad* pad);
    static bool ProcessDeferredControlRelease(CPad* pad, const CControllerState& state);
    static void ReleaseControlLock(CPad* pad);
    static void ResetTransientInput();
    static void ConsumeGameplayInput(CPad* pad);
    static bool IsOpenChordDown(const CControllerState& state);
    static Direction ReadDirection(const CControllerState& state);
    static bool ConsumeButton(bool down, bool& held);
    static bool ConsumeDirection(Direction direction, uint32_t now);
    static void MoveSelection(Direction direction);
    static void ActivateSelectedKey(CPad* pad);
    static std::wstring GetKeyLabel(const Key& key);
    static size_t GetRowCount();
    static size_t GetColumnCount(size_t row);
    static const Key& GetKey(size_t row, size_t column);
};
