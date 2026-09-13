#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#endif

#include <atomic>
#include <functional>
#include <string>
#include <string_view>
#include <vector>
#include "AutoCorrector.h"
#include "CaretTracker.h"
#include "OverlayWindow.h"

namespace AutoCorrect
{

/**
 * @brief Mode of the Keyboard Hook Service.
 */
enum class ServiceMode
{
    Overlay,      // Shows squiggly underline + Top 3 suggestions on hover / Alt+1/2/3 (Default)
    AutoReplace,  // Immediate automatic correction upon delimiter with zero-race input injection
    Hybrid        // Auto-replaces highest-confidence typos, shows overlay for ambiguous words
};

/**
 * @brief System-wide background keyboard hook service for zero-friction auto-correction.
 * Listens to keystrokes across all Windows apps (Chrome, VS Code, Discord, Word, Notepad, etc.).
 * Supports:
 * 1. Non-intrusive red squiggly underline with interactive Top 3 suggestions card on hover / Alt+1/2/3
 * 2. Race-free direct auto-replacement with immediate Backspace undo
 * 3. Adaptive learning of personal typing patterns, vocabulary, and typo habits
 */
class KeyboardHookService
{
public:
    explicit KeyboardHookService(CAutoCorrector& corrector, ServiceMode mode = ServiceMode::Overlay);
    ~KeyboardHookService();

    KeyboardHookService(const KeyboardHookService&) = delete;
    KeyboardHookService& operator=(const KeyboardHookService&) = delete;

    /**
     * @brief Starts the Windows low-level keyboard hook and message pump.
     * Blocks until stop() is called or Ctrl+Shift+Q is pressed.
     */
    void run();

    /**
     * @brief Stops the keyboard hook and unregisters from Windows.
     */
    void stop();

    void setMode(ServiceMode mode) noexcept { m_mode = mode; }
    [[nodiscard]] ServiceMode getMode() const noexcept { return m_mode; }

    /**
     * @brief Callback invoked whenever an auto-correction, suggestion, or undo occurs.
     */
    void setLogCallback(std::function<void(std::string_view, std::string_view, bool)> callback)
    {
        m_logCallback = std::move(callback);
    }

private:
#ifdef _WIN32
    static LRESULT CALLBACK LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam);
    bool handleKeystroke(DWORD vkCode, bool isKeyDown);
    void performAutoReplaceNoRace(std::string_view typo, std::string_view correction, char delimiter);
    void performSuggestionReplacement(std::string_view original, std::string_view chosen, char delimiter);
    void performUndo();
    void dismissOverlay();

    HHOOK m_hook{nullptr};
    DWORD m_hookThreadId{0};
    CaretTracker m_caretTracker;
    OverlayWindow m_overlay;
#endif

    CAutoCorrector& m_corrector;
    ServiceMode m_mode{ServiceMode::Overlay};
    std::atomic<bool> m_running{false};
    std::atomic<bool> m_isInjecting{false};

    // Keystroke state tracking
    std::string m_currentWord;
    std::string m_previousWord;

    // Active overlay typo state
    std::string m_activeTypo;
    char m_activeDelimiter{' '};
    bool m_hasActiveDelimiter{false};

    // Undo state tracking
    bool m_canUndo{false};
    std::string m_lastTypo;
    std::string m_lastReplacement;
    char m_lastDelimiter{' '};

    std::function<void(std::string_view, std::string_view, bool)> m_logCallback;

    static KeyboardHookService* s_instance;
};

} // namespace AutoCorrect
