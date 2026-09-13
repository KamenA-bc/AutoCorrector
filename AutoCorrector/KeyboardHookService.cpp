#include "KeyboardHookService.h"
#include <iostream>
#include <vector>
#include <algorithm>

namespace AutoCorrect
{

KeyboardHookService* KeyboardHookService::s_instance = nullptr;

KeyboardHookService::KeyboardHookService(CAutoCorrector& corrector, ServiceMode mode)
    : m_corrector(corrector), m_mode(mode)
{
    s_instance = this;
}

KeyboardHookService::~KeyboardHookService()
{
    stop();
    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

#ifdef _WIN32

LRESULT CALLBACK KeyboardHookService::LowLevelKeyboardProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_instance != nullptr)
    {
        auto* kbd = reinterpret_cast<KBDLLHOOKSTRUCT*>(lParam);

        // Ignore injected events or events while synthesizing input to prevent re-entrancy loops
        if ((kbd->flags & LLKHF_INJECTED) || s_instance->m_isInjecting.load())
        {
            return CallNextHookEx(nullptr, nCode, wParam, lParam);
        }

        const bool isKeyDown = (wParam == WM_KEYDOWN || wParam == WM_SYSKEYDOWN);

        // Check exit hotkey: Ctrl + Shift + Q
        if (isKeyDown && kbd->vkCode == 'Q')
        {
            const bool isCtrl = (GetKeyState(VK_CONTROL) & 0x8000) != 0;
            const bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
            if (isCtrl && isShift)
            {
                std::cout << "\n[Daemon] Exit hotkey (Ctrl+Shift+Q) detected. Stopping service...\n";
                s_instance->stop();
                return 1;
            }
        }

        if (isKeyDown)
        {
            // If user presses Escape and overlay is visible, dismiss overlay
            if (kbd->vkCode == VK_ESCAPE && s_instance->m_overlay.isVisible())
            {
                s_instance->dismissOverlay();
                return CallNextHookEx(nullptr, nCode, wParam, lParam);
            }

            // Check Alt+1, Alt+2, Alt+3 for instant suggestion picking when overlay is visible
            if ((GetKeyState(VK_MENU) & 0x8000) != 0 && s_instance->m_overlay.isVisible())
            {
                if (kbd->vkCode >= '1' && kbd->vkCode <= '3')
                {
                    const size_t idx = static_cast<size_t>(kbd->vkCode - '0');
                    if (s_instance->m_overlay.selectSuggestionIndex(idx))
                    {
                        return 1; // Consume Alt+N keystroke
                    }
                }
            }

            // If user presses Backspace and an Undo is pending, intercept and revert
            if (kbd->vkCode == VK_BACK && s_instance->m_canUndo)
            {
                s_instance->performUndo();
                return 1; // Consume backspace
            }

            const bool consume = s_instance->handleKeystroke(kbd->vkCode, isKeyDown);
            if (consume)
            {
                return 1; // Consume event (e.g. Delimiter intercepted for atomic auto-replace)
            }
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

bool KeyboardHookService::handleKeystroke(DWORD vkCode, bool /*isKeyDown*/)
{
    // Ignore keystrokes when Ctrl or Alt is held (navigation, shortcuts, copy/paste)
    if ((GetKeyState(VK_CONTROL) & 0x8000) != 0 || (GetKeyState(VK_MENU) & 0x8000) != 0)
    {
        m_currentWord.clear();
        dismissOverlay();
        m_canUndo = false;
        return false;
    }

    // Handle Backspace
    if (vkCode == VK_BACK)
    {
        dismissOverlay();
        if (!m_currentWord.empty())
        {
            m_currentWord.pop_back();
        }
        m_canUndo = false;
        return false;
    }

    // Check for letters A-Z
    if (vkCode >= 'A' && vkCode <= 'Z')
    {
        dismissOverlay();
        const bool isShift = (GetKeyState(VK_SHIFT) & 0x8000) != 0;
        const bool isCaps = (GetKeyState(VK_CAPITAL) & 0x0001) != 0;
        const char c = (isShift ^ isCaps) ? static_cast<char>(vkCode) : static_cast<char>(vkCode - 'A' + 'a');

        m_currentWord.push_back(c);
        m_canUndo = false;
        return false;
    }

    // Check for apostrophe (OEM_7)
    if (vkCode == VK_OEM_7)
    {
        dismissOverlay();
        m_currentWord.push_back('\'');
        m_canUndo = false;
        return false;
    }

    // Check for Word Delimiters
    char delimiter = '\0';
    if (vkCode == VK_SPACE) delimiter = ' ';
    else if (vkCode == VK_RETURN) delimiter = '\n';
    else if (vkCode == VK_TAB) delimiter = '\t';
    else if (vkCode == VK_OEM_PERIOD) delimiter = '.';
    else if (vkCode == VK_OEM_COMMA) delimiter = ',';
    else if (vkCode == VK_OEM_1) delimiter = ';';
    else if (vkCode == VK_OEM_2) delimiter = '?';

    if (delimiter != '\0')
    {
        if (m_currentWord.size() >= 2)
        {
            // If the word is an exact known word, record usage and preserve as valid
            if (m_corrector.isWordInDictionary(m_currentWord))
            {
                dismissOverlay();
                m_corrector.getLearner().recordWordUsage(m_currentWord);
                m_previousWord = m_currentWord;
                m_currentWord.clear();
                m_canUndo = false;
                return false;
            }

            // Word is unknown/misspelled! Fetch top suggestions
            const auto suggestions = m_corrector.getTopSuggestions(m_currentWord, m_previousWord, 3);

            if (!suggestions.empty())
            {
                if (m_mode == ServiceMode::Overlay ||
                   (m_mode == ServiceMode::Hybrid && suggestions.size() > 1))
                {
                    // OVERLAY MODE:
                    // Display squiggly line at caret position
                    RECT wordRect{};
                    if (m_caretTracker.getWordScreenRect(m_currentWord.size(), wordRect))
                    {
                        m_activeTypo = m_currentWord;
                        m_activeDelimiter = delimiter;
                        m_hasActiveDelimiter = true;
                        m_overlay.showSquiggly(m_currentWord, wordRect, suggestions);

                        if (m_logCallback)
                        {
                            m_logCallback(m_currentWord, suggestions.front(), false);
                        }
                    }

                    m_previousWord = m_currentWord;
                    m_currentWord.clear();
                    m_canUndo = false;
                    return false; // Let delimiter pass to target app normally
                }
                else
                {
                    // AUTOREPLACE MODE:
                    // Atomic replacement: Consume delimiter so target app never sees it,
                    // backspace typo length, type correction, and type delimiter.
                    const std::string correction = suggestions.front();
                    performAutoReplaceNoRace(m_currentWord, correction, delimiter);
                    m_previousWord = correction;
                    m_currentWord.clear();
                    return true; // Consume original delimiter!
                }
            }
            else
            {
                dismissOverlay();
                m_previousWord = m_currentWord;
                m_currentWord.clear();
                m_canUndo = false;
                return false;
            }
        }
        else
        {
            dismissOverlay();
            m_canUndo = false;
            m_currentWord.clear();
            return false;
        }
    }

    // Any other key resets current word and dismisses overlay
    dismissOverlay();
    m_currentWord.clear();
    m_canUndo = false;
    return false;
}

void KeyboardHookService::performAutoReplaceNoRace(std::string_view typo,
                                                   std::string_view correction,
                                                   char delimiter)
{
    m_isInjecting.store(true);

    std::vector<INPUT> inputs;

    // The delimiter was CONSUMED by the hook and was NEVER sent to the target window.
    // So target window only has typo. Backspace exactly typo.size() times.
    for (size_t i = 0; i < typo.size(); ++i)
    {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = VK_BACK;
        inputs.push_back(down);

        INPUT up{};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = VK_BACK;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }

    // Type the corrected word using Unicode events
    for (char c : correction)
    {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(c));
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(down);

        INPUT up{};
        up.type = INPUT_KEYBOARD;
        up.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(c));
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }

    // Now type the delimiter
    if (delimiter != '\0')
    {
        INPUT delimDown{};
        delimDown.type = INPUT_KEYBOARD;
        delimDown.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(delimiter));
        delimDown.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(delimDown);

        INPUT delimUp{};
        delimUp.type = INPUT_KEYBOARD;
        delimUp.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(delimiter));
        delimUp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(delimUp);
    }

    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    m_isInjecting.store(false);

    // Arm Undo state
    m_canUndo = true;
    m_lastTypo = std::string(typo);
    m_lastReplacement = std::string(correction);
    m_lastDelimiter = delimiter;

    // Record accepted correction in AdaptiveLearner
    m_corrector.getLearner().recordCorrectionAccepted(typo, correction);

    if (m_logCallback)
    {
        m_logCallback(typo, correction, false);
    }
}

void KeyboardHookService::performSuggestionReplacement(std::string_view original,
                                                       std::string_view chosen,
                                                       char delimiter)
{
    m_isInjecting.store(true);

    std::vector<INPUT> inputs;

    // 1. Backspace delimiter (if present) + original typo
    const size_t backspaces = (m_hasActiveDelimiter ? 1 : 0) + original.size();
    for (size_t i = 0; i < backspaces; ++i)
    {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = VK_BACK;
        inputs.push_back(down);

        INPUT up{};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = VK_BACK;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }

    // 2. Type chosen replacement
    for (char c : chosen)
    {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(c));
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(down);

        INPUT up{};
        up.type = INPUT_KEYBOARD;
        up.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(c));
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }

    // 3. Re-type delimiter if was present
    if (m_hasActiveDelimiter && delimiter != '\0')
    {
        INPUT delimDown{};
        delimDown.type = INPUT_KEYBOARD;
        delimDown.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(delimiter));
        delimDown.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(delimDown);

        INPUT delimUp{};
        delimUp.type = INPUT_KEYBOARD;
        delimUp.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(delimiter));
        delimUp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(delimUp);
    }

    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    m_isInjecting.store(false);

    // Record accepted correction in AdaptiveLearner!
    m_corrector.getLearner().recordCorrectionAccepted(original, chosen);
    m_corrector.addUserWord(chosen, 50);
    m_previousWord = std::string(chosen);

    dismissOverlay();

    if (m_logCallback)
    {
        m_logCallback(original, chosen, false);
    }
}

void KeyboardHookService::performUndo()
{
    m_isInjecting.store(true);

    std::vector<INPUT> inputs;

    // Target window has: replacement + delimiter
    const size_t totalBackspaces = (m_lastDelimiter != '\0' ? 1 : 0) + m_lastReplacement.size();
    for (size_t i = 0; i < totalBackspaces; ++i)
    {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wVk = VK_BACK;
        inputs.push_back(down);

        INPUT up{};
        up.type = INPUT_KEYBOARD;
        up.ki.wVk = VK_BACK;
        up.ki.dwFlags = KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }

    // Re-type original typo
    for (char c : m_lastTypo)
    {
        INPUT down{};
        down.type = INPUT_KEYBOARD;
        down.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(c));
        down.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(down);

        INPUT up{};
        up.type = INPUT_KEYBOARD;
        up.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(c));
        up.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(up);
    }

    // Re-type original delimiter
    if (m_lastDelimiter != '\0')
    {
        INPUT delimDown{};
        delimDown.type = INPUT_KEYBOARD;
        delimDown.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(m_lastDelimiter));
        delimDown.ki.dwFlags = KEYEVENTF_UNICODE;
        inputs.push_back(delimDown);

        INPUT delimUp{};
        delimUp.type = INPUT_KEYBOARD;
        delimUp.ki.wScan = static_cast<WORD>(static_cast<unsigned char>(m_lastDelimiter));
        delimUp.ki.dwFlags = KEYEVENTF_UNICODE | KEYEVENTF_KEYUP;
        inputs.push_back(delimUp);
    }

    SendInput(static_cast<UINT>(inputs.size()), inputs.data(), sizeof(INPUT));
    m_isInjecting.store(false);

    // Record rejected correction in AdaptiveLearner!
    m_corrector.getLearner().recordCorrectionRejected(m_lastTypo, m_lastReplacement);
    // Add typo to user dictionary so it won't be replaced again
    m_corrector.addUserWord(m_lastTypo, 100);

    if (m_logCallback)
    {
        m_logCallback(m_lastReplacement, m_lastTypo, true);
    }

    m_canUndo = false;
    m_currentWord.clear();
}

void KeyboardHookService::dismissOverlay()
{
    if (m_overlay.isVisible())
    {
        m_overlay.hide();
    }
    m_activeTypo.clear();
    m_hasActiveDelimiter = false;
}

void KeyboardHookService::run()
{
    m_hookThreadId = GetCurrentThreadId();

    // Initialize Overlay window
    m_overlay.initialize(GetModuleHandle(nullptr));
    m_overlay.setSuggestionCallback([this](std::string_view original, std::string_view chosen) {
        this->performSuggestionReplacement(original, chosen, this->m_activeDelimiter);
    });

    m_hook = SetWindowsHookEx(WH_KEYBOARD_LL, LowLevelKeyboardProc, GetModuleHandle(nullptr), 0);

    if (!m_hook)
    {
        std::cerr << "Error: Failed to register Windows Low-Level Keyboard Hook. Error code: "
                  << GetLastError() << '\n';
        return;
    }

    m_running.store(true);
    std::cout << "[Daemon] AutoCorrector Service is active (Mode: "
              << (m_mode == ServiceMode::Overlay ? "Interactive Overlay [Squiggly + Top 3 Suggestions]" : "Auto-Replace")
              << ").\n"
              << "[Daemon] Running globally in all apps (Notepad, Chrome, VS Code, Discord, Word, etc.).\n"
              << "[Daemon] - Misspelled words get red squiggly underlines.\n"
              << "[Daemon] - Hover over squiggly line to see Top 3 suggestions.\n"
              << "[Daemon] - Press Alt+1, Alt+2, or Alt+3 (or click) to apply suggestion.\n"
              << "[Daemon] - Press Ctrl+Shift+Q in any app or Ctrl+C here to stop.\n\n";

    MSG msg;
    while (m_running.load() && GetMessage(&msg, nullptr, 0, 0) > 0)
    {
        TranslateMessage(&msg);
        DispatchMessage(&msg);
    }

    stop();
}

void KeyboardHookService::stop()
{
    dismissOverlay();

    if (m_hook != nullptr)
    {
        UnhookWindowsHookEx(m_hook);
        m_hook = nullptr;
    }

    // Save user profile upon exit
    m_corrector.getLearner().saveProfile();

    if (m_running.exchange(false))
    {
        if (m_hookThreadId != 0)
        {
            PostThreadMessage(m_hookThreadId, WM_QUIT, 0, 0);
        }
    }
}

#else

void KeyboardHookService::run()
{
    std::cerr << "Windows low-level hook is only supported on Windows platforms.\n";
}

void KeyboardHookService::stop()
{
}

#endif

} // namespace AutoCorrect
