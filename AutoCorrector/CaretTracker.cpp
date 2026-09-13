#ifdef _WIN32
#include <initguid.h>
#endif
#include "CaretTracker.h"
#include <iostream>
#ifdef _WIN32
#include <oleacc.h>
#endif

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

bool CaretTracker::getWordScreenRect(std::string_view word,
                                     bool hasTrailingDelimiter,
                                     RECT& outRect,
                                     IUIAutomationTextRange** ppOutRange)
{
#ifdef _WIN32
    HWND fg = GetForegroundWindow();
    if (fg)
    {
        // Wake up Chromium accessibility tree (VS Code, Chrome, Edge, Discord, Electron)
        IAccessible* pAcc = nullptr;
        AccessibleObjectFromWindow(fg, OBJID_CLIENT, IID_IAccessible, reinterpret_cast<void**>(&pAcc));
        if (pAcc) pAcc->Release();
    }

    // 1. First try Windows UI Automation (works in modern editors, Electron, VS Code, Discord, Word)
    if (getViaUIA(word, hasTrailingDelimiter, outRect, ppOutRange))
    {
        return true;
    }

    // 2. Fall back to classic Win32 GetGUIThreadInfo with exact font metrics (Notepad, WordPad, Win32)
    if (getViaWin32(word, hasTrailingDelimiter, outRect))
    {
        return true;
    }

    // Never fall back to mouse cursor position - that causes lines to appear randomly at mouse pointer!
#else
    (void)word;
    (void)hasTrailingDelimiter;
    (void)outRect;
    (void)ppOutRange;
#endif
    return false;
}

#ifdef _WIN32

bool CaretTracker::getViaWin32(std::string_view word, bool hasTrailingDelimiter, RECT& outRect)
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

        // Measure exact font metrics for this window
        HDC hdc = GetDC(gti.hwndCaret);
        HFONT hFont = reinterpret_cast<HFONT>(SendMessage(gti.hwndCaret, WM_GETFONT, 0, 0));
        HGDIOBJ oldFont = hFont ? SelectObject(hdc, hFont) : nullptr;

        SIZE szWord{};
        SIZE szDelim{};
        if (!GetTextExtentPoint32A(hdc, word.data(), static_cast<int>(word.size()), &szWord))
        {
            szWord.cx = static_cast<LONG>(word.size() * 11);
            szWord.cy = 18;
        }

        if (hasTrailingDelimiter)
        {
            if (!GetTextExtentPoint32A(hdc, " ", 1, &szDelim))
            {
                szDelim.cx = 7;
            }
        }

        if (oldFont) SelectObject(hdc, oldFont);
        ReleaseDC(gti.hwndCaret, hdc);

        const LONG delimOffset = hasTrailingDelimiter ? szDelim.cx : 0;
        outRect.left = pt1.x - delimOffset - szWord.cx;
        outRect.right = pt1.x - delimOffset;
        outRect.top = pt1.y;
        outRect.bottom = (pt2.y > pt1.y) ? pt2.y : (pt1.y + (szWord.cy > 0 ? szWord.cy : 18));
        return true;
    }

    return false;
}

bool CaretTracker::getViaUIA(std::string_view /*word*/,
                             bool hasTrailingDelimiter,
                             RECT& outRect,
                             IUIAutomationTextRange** ppOutRange)
{
    if (!m_pAutomation) return false;

    IUIAutomationElement* pFocused = nullptr;
    if (FAILED(m_pAutomation->GetFocusedElement(&pFocused)) || !pFocused)
    {
        HWND fg = GetForegroundWindow();
        if (fg)
        {
            m_pAutomation->ElementFromHandle(fg, &pFocused);
        }
    }

    if (!pFocused) return false;

    IUIAutomationTextPattern* pTextPattern = nullptr;
    HRESULT hr = pFocused->GetCurrentPattern(UIA_TextPatternId, reinterpret_cast<IUnknown**>(&pTextPattern));

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
                    // If caret is right after a delimiter, step back 1 character onto the word
                    if (hasTrailingDelimiter)
                    {
                        int moved = 0;
                        pRange->MoveEndpointByUnit(TextPatternRangeEndpoint_End, TextUnit_Character, -1, &moved);
                    }

                    // Expand to enclose the entire word
                    pRange->ExpandToEnclosingUnit(TextUnit_Word);

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

                                if (w > 0 && h > 0)
                                {
                                    outRect.left = static_cast<LONG>(x);
                                    outRect.top = static_cast<LONG>(y);
                                    outRect.right = static_cast<LONG>(x + w);
                                    outRect.bottom = static_cast<LONG>(y + h);

                                    SafeArrayUnaccessData(pRects);
                                    SafeArrayDestroy(pRects);

                                    if (ppOutRange)
                                    {
                                        pRange->Clone(ppOutRange);
                                    }

                                    pRange->Release();
                                    pRanges->Release();
                                    pTextPattern->Release();
                                    pFocused->Release();
                                    return true;
                                }
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
