#ifdef _WIN32
#include <initguid.h>
#endif
#include "CaretTracker.h"
#include <iostream>

namespace AutoCorrect
{

CaretTracker::CaretTracker()
{
#ifdef _WIN32
    const HRESULT hr = CoInitializeEx(nullptr, COINIT_APARTMENTTHREADED);
    m_coInitialized = SUCCEEDED(hr);

    CoCreateInstance(CLSID_CUIAutomation,
                     nullptr,
                     CLSCTX_INPROC_SERVER,
                     IID_IUIAutomation,
                     reinterpret_cast<void**>(&m_pAutomation));
#endif
}

CaretTracker::~CaretTracker()
{
#ifdef _WIN32
    if (m_pAutomation != nullptr)
    {
        m_pAutomation->Release();
        m_pAutomation = nullptr;
    }

    if (m_coInitialized)
    {
        CoUninitialize();
    }
#endif
}

bool CaretTracker::getWordScreenRect(size_t wordLen, RECT& outRect)
{
#ifdef _WIN32
    // 1. First try Windows UI Automation (works in modern browsers, Electron, VS Code, Discord, Word)
    if (getViaUIA(wordLen, outRect))
    {
        return true;
    }

    // 2. Fall back to classic Win32 GetGUIThreadInfo (works in Notepad, WordPad, classic editors)
    if (getViaWin32(wordLen, outRect))
    {
        return true;
    }

    // 3. Fallback: mouse cursor position
    POINT pt;
    if (GetCursorPos(&pt))
    {
        const int charWidth = 9;
        const int wordWidth = static_cast<int>(wordLen > 0 ? wordLen : 5) * charWidth;
        outRect.left = pt.x - wordWidth;
        outRect.top = pt.y - 20;
        outRect.right = pt.x;
        outRect.bottom = pt.y;
        return true;
    }
#else
    (void)wordLen;
    (void)outRect;
#endif
    return false;
}

#ifdef _WIN32

bool CaretTracker::getViaWin32(size_t wordLen, RECT& outRect)
{
    HWND fg = GetForegroundWindow();
    if (!fg) return false;

    DWORD threadId = GetWindowThreadProcessId(fg, nullptr);
    GUITHREADINFO gti{};
    gti.cbSize = sizeof(GUITHREADINFO);

    if (GetGUIThreadInfo(threadId, &gti) && gti.hwndCaret)
    {
        POINT pt1 = {gti.rcCaret.left, gti.rcCaret.top};
        POINT pt2 = {gti.rcCaret.right, gti.rcCaret.bottom};
        ClientToScreen(gti.hwndCaret, &pt1);
        ClientToScreen(gti.hwndCaret, &pt2);

        const int charWidth = 9;
        const int wordWidth = static_cast<int>(wordLen > 0 ? wordLen : 4) * charWidth;

        outRect.left = pt1.x - wordWidth;
        outRect.top = pt1.y;
        outRect.right = pt1.x;
        outRect.bottom = pt2.y > pt1.y ? pt2.y : pt1.y + 18;
        return true;
    }

    return false;
}

bool CaretTracker::getViaUIA(size_t wordLen, RECT& outRect)
{
    if (!m_pAutomation) return false;

    IUIAutomationElement* pFocused = nullptr;
    if (FAILED(m_pAutomation->GetFocusedElement(&pFocused)) || !pFocused)
    {
        return false;
    }

    IUIAutomationTextPattern* pTextPattern = nullptr;
    const HRESULT hr = pFocused->GetCurrentPattern(UIA_TextPatternId, reinterpret_cast<IUnknown**>(&pTextPattern));

    if (SUCCEEDED(hr) && pTextPattern)
    {
        IUIAutomationTextRangeArray* pRanges = nullptr;
        if (SUCCEEDED(pTextPattern->GetSelection(&pRanges)) && pRanges)
        {
            int length = 0;
            pRanges->get_Length(&length);

            if (length > 0)
            {
                IUIAutomationTextRange* pRange = nullptr;
                if (SUCCEEDED(pRanges->GetElement(0, &pRange)) && pRange)
                {
                    SAFEARRAY* pRects = nullptr;
                    if (SUCCEEDED(pRange->GetBoundingRectangles(&pRects)) && pRects)
                    {
                        double* pData = nullptr;
                        if (SUCCEEDED(SafeArrayAccessData(pRects, reinterpret_cast<void**>(&pData))))
                        {
                            long uBound = 0;
                            SafeArrayGetUBound(pRects, 1, &uBound);

                            if (uBound >= 3)
                            {
                                const double x = pData[0];
                                const double y = pData[1];
                                const double w = pData[2];
                                const double h = pData[3];

                                const int charWidth = 9;
                                const int wordWidth = static_cast<int>(wordLen > 0 ? wordLen : 4) * charWidth;

                                outRect.left = static_cast<LONG>(x - wordWidth);
                                outRect.top = static_cast<LONG>(y);
                                outRect.right = static_cast<LONG>(x + (w > 2.0 ? w : 2.0));
                                outRect.bottom = static_cast<LONG>(y + (h > 10.0 ? h : 18.0));

                                SafeArrayUnaccessData(pRects);
                                SafeArrayDestroy(pRects);
                                pRange->Release();
                                pRanges->Release();
                                pTextPattern->Release();
                                pFocused->Release();
                                return true;
                            }
                            SafeArrayUnaccessData(pRects);
                        }
                        SafeArrayDestroy(pRects);
                    }
                    pRange->Release();
                }
            }
            pRanges->Release();
        }
        pTextPattern->Release();
    }

    pFocused->Release();
    return false;
}

#endif

} // namespace AutoCorrect
