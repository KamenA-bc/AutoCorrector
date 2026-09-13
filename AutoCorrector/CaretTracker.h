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

namespace AutoCorrect
{

/**
 * @brief Tracks the active application's caret and text bounding rectangle on Windows screens.
 * Uses Windows UI Automation (UIA) with fallback to Win32 GetGUIThreadInfo.
 */
class CaretTracker
{
public:
    CaretTracker();
    ~CaretTracker();

    CaretTracker(const CaretTracker&) = delete;
    CaretTracker& operator=(const CaretTracker&) = delete;

    /**
     * @brief Retrieves the screen bounding rectangle of the active text caret or typed word.
     * @param wordLen Length of the current word to estimate bounding box if caret rect is a point.
     * @param outRect Output screen rectangle (pixels).
     * @return true if successfully located, false otherwise.
     */
    bool getWordScreenRect(size_t wordLen, RECT& outRect);

private:
#ifdef _WIN32
    bool getViaUIA(size_t wordLen, RECT& outRect);
    bool getViaWin32(size_t wordLen, RECT& outRect);

    IUIAutomation* m_pAutomation{nullptr};
    bool m_coInitialized{false};
#endif
};

} // namespace AutoCorrect
