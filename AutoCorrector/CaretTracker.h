#pragma once

#ifdef _WIN32
#ifndef WIN32_LEAN_AND_MEAN
#define WIN32_LEAN_AND_MEAN
#endif
#ifndef NOMINMAX
#define NOMINMAX
#endif
#include <windows.h>
#include <uiautomation.h>
#endif

#include <string>
#include <string_view>

namespace AutoCorrect
{

/**
 * @brief Tracks the active application's caret and text bounding rectangle on Windows screens.
 * Uses Windows UI Automation (UIA) with fallback to Win32 GetGUIThreadInfo and GDI font metrics.
 */
class CaretTracker
{
public:
    CaretTracker();
    ~CaretTracker();

    CaretTracker(const CaretTracker&) = delete;
    CaretTracker& operator=(const CaretTracker&) = delete;

    /**
     * @brief Retrieves the exact screen bounding rectangle of the ENTIRE typed word.
     * @param word The exact word text (used for font measurement).
     * @param hasTrailingDelimiter True if caret is currently 1 position past the word (after space/punctuation).
     * @param outRect Output screen rectangle (pixels) spanning from first character to last character.
     * @param ppOutRange Optional output pointer to receive cloned IUIAutomationTextRange (caller must ->Release()).
     * @return true if successfully located, false otherwise.
     */
    bool getWordScreenRect(std::string_view word,
                           bool hasTrailingDelimiter,
                           RECT& outRect,
                           IUIAutomationTextRange** ppOutRange = nullptr);

private:
#ifdef _WIN32
    bool getViaUIA(std::string_view word, bool hasTrailingDelimiter, RECT& outRect, IUIAutomationTextRange** ppOutRange);
    bool getViaWin32(std::string_view word, bool hasTrailingDelimiter, RECT& outRect);

    IUIAutomation* m_pAutomation{nullptr};
    bool m_coInitialized{false};
#endif
};

} // namespace AutoCorrect
