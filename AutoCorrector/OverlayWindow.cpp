#include "OverlayWindow.h"
#include <iostream>
#include <algorithm>

namespace AutoCorrect
{

OverlayWindow* OverlayWindow::s_instance = nullptr;

OverlayWindow::OverlayWindow()
{
    s_instance = this;
}

OverlayWindow::~OverlayWindow()
{
#ifdef _WIN32
    clearAll();

    if (m_mouseHook)
    {
        UnhookWindowsHookEx(m_mouseHook);
        m_mouseHook = nullptr;
    }

    if (m_hwndPopup)
    {
        KillTimer(m_hwndPopup, 1001);
        DestroyWindow(m_hwndPopup);
        m_hwndPopup = nullptr;
    }
    if (m_hFont)
    {
        DeleteObject(m_hFont);
        m_hFont = nullptr;
    }
    if (m_hFontBold)
    {
        DeleteObject(m_hFontBold);
        m_hFontBold = nullptr;
    }
#endif

    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

#ifdef _WIN32

LRESULT CALLBACK OverlayWindow::LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam)
{
    if (nCode == HC_ACTION && s_instance != nullptr)
    {
        if (wParam == WM_MOUSEMOVE)
        {
            auto* ms = reinterpret_cast<MSLLHOOKSTRUCT*>(lParam);
            s_instance->handleMouseMoveGlobal(ms->pt);
        }
    }

    return CallNextHookEx(nullptr, nCode, wParam, lParam);
}

bool OverlayWindow::initialize(HINSTANCE hInstance)
{
    m_hInstance = hInstance ? hInstance : GetModuleHandle(nullptr);

    // 1. Register Underline Window Class
    WNDCLASSEX wcUnderline{};
    wcUnderline.cbSize = sizeof(WNDCLASSEX);
    wcUnderline.style = CS_HREDRAW | CS_VREDRAW;
    wcUnderline.lpfnWndProc = UnderlineWndProc;
    wcUnderline.hInstance = m_hInstance;
    wcUnderline.lpszClassName = TEXT("AutoCorrectUnderlineItemWindow");
    wcUnderline.hCursor = LoadCursor(nullptr, IDC_HAND);
    RegisterClassEx(&wcUnderline);

    // 2. Register Suggestion Popup Window Class
    WNDCLASSEX wcPopup{};
    wcPopup.cbSize = sizeof(WNDCLASSEX);
    wcPopup.style = CS_HREDRAW | CS_VREDRAW;
    wcPopup.lpfnWndProc = PopupWndProc;
    wcPopup.hInstance = m_hInstance;
    wcPopup.lpszClassName = TEXT("AutoCorrectPopupCardWindow");
    wcPopup.hCursor = LoadCursor(nullptr, IDC_HAND);
    RegisterClassEx(&wcPopup);

    // 3. Create Suggestion Popup Window (initially hidden)
    m_hwndPopup = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        TEXT("AutoCorrectPopupCardWindow"),
        TEXT("AutoCorrectPopup"),
        WS_POPUP,
        0, 0, 220, 100,
        nullptr, nullptr, m_hInstance, nullptr
    );

    // 4. Create Modern Typography Fonts
    m_hFont = CreateFont(
        -13, 0, 0, 0, FW_NORMAL, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        TEXT("Segoe UI")
    );

    m_hFontBold = CreateFont(
        -13, 0, 0, 0, FW_SEMIBOLD, FALSE, FALSE, FALSE,
        DEFAULT_CHARSET, OUT_DEFAULT_PRECIS, CLIP_DEFAULT_PRECIS,
        CLEARTYPE_QUALITY, DEFAULT_PITCH | FF_DONTCARE,
        TEXT("Segoe UI")
    );

    // 5. Hook global mouse moves so hovering anywhere over the whole word opens suggestions
    m_mouseHook = SetWindowsHookEx(WH_MOUSE_LL, LowLevelMouseProc, m_hInstance, 0);

    // 6. Set 50ms tracking timer to follow scrolling/typing and remove manually corrected words
    if (m_hwndPopup)
    {
        SetTimer(m_hwndPopup, 1001, 50, nullptr);
    }

    return (m_hwndPopup != nullptr);
}

void OverlayWindow::handleMouseMoveGlobal(POINT pt)
{
    // If mouse is inside the popup card, keep popup open
    if (m_popupVisible && m_hwndPopup)
    {
        RECT rcPopup;
        GetWindowRect(m_hwndPopup, &rcPopup);
        InflateRect(&rcPopup, 6, 6);
        if (PtInRect(&rcPopup, pt))
        {
            return;
        }
    }

    // Check if mouse is hovering over ANY word on screen (the whole word, not just the 4px line!)
    for (size_t i = 0; i < m_items.size(); ++i)
    {
        RECT wordBox = m_items[i].screenRect;
        // Expand box to cover the entire body of the word + padding + underline
        wordBox.top -= 4;
        wordBox.bottom += 8;
        wordBox.left -= 4;
        wordBox.right += 4;

        if (PtInRect(&wordBox, pt))
        {
            showPopupForIndex(i);
            return;
        }
    }

    // If mouse is far away from words and popup, hide the popup card
    if (m_popupVisible)
    {
        hidePopup();
    }
}

void OverlayWindow::showPopupForIndex(size_t index)
{
    if (index >= m_items.size() || !m_hwndPopup) return;

    if (m_activeHoveredIndex == static_cast<int>(index) && m_popupVisible)
    {
        return; // Already showing for this word
    }

    m_activeHoveredIndex = static_cast<int>(index);
    const auto& item = m_items[index];

    const int popupW = 220;
    const int itemH = 28;
    const int popupH = static_cast<int>(item.suggestions.size()) * itemH + 12;

    int popupX = item.screenRect.left;
    int popupY = item.screenRect.top - popupH - 4;
    if (popupY < 20)
    {
        popupY = item.screenRect.bottom + 8;
    }

    SetWindowPos(m_hwndPopup, HWND_TOPMOST, popupX, popupY, popupW, popupH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    m_popupVisible = true;
    m_hoveredSuggestionIndex = -1;
    InvalidateRect(m_hwndPopup, nullptr, TRUE);
}

void OverlayWindow::updatePositions()
{
    HWND fg = GetForegroundWindow();

    for (size_t i = 0; i < m_items.size(); )
    {
        auto& item = m_items[i];

        // 1. Hide underlines if the user switched to a different application
        if (item.targetHwnd && item.targetHwnd != fg)
        {
            if (item.hwndUnderline && IsWindowVisible(item.hwndUnderline))
            {
                ShowWindow(item.hwndUnderline, SW_HIDE);
            }
            ++i;
            continue;
        }
        else
        {
            if (item.hwndUnderline && !IsWindowVisible(item.hwndUnderline))
            {
                ShowWindow(item.hwndUnderline, SW_SHOWNOACTIVATE);
            }
        }

        // 2. Check if word was manually corrected or deleted by the user!
        if (item.pTextRange != nullptr)
        {
            BSTR bstr = nullptr;
            if (SUCCEEDED(item.pTextRange->GetText(-1, &bstr)) && bstr)
            {
                std::wstring ws(bstr);
                SysFreeString(bstr);
                std::string curText(ws.begin(), ws.end());
                while (!curText.empty() && (curText.back() == ' ' || curText.back() == '\n' || curText.back() == '\r' || curText.back() == '\t'))
                {
                    curText.pop_back();
                }

                if (curText != item.word)
                {
                    // User manually corrected the word or deleted it! Remove underline immediately!
                    removeUnderline(item.id);
                    continue; // item erased, don't increment i
                }
            }

            // 3. Dynamically query current bounding box to follow typing, scrolling, or window moves!
            SAFEARRAY* pRects = nullptr;
            if (SUCCEEDED(item.pTextRange->GetBoundingRectangles(&pRects)) && pRects)
            {
                double* pData = nullptr;
                if (SUCCEEDED(SafeArrayAccessData(pRects, reinterpret_cast<void**>(&pData))))
                {
                    long uBound = 0;
                    SafeArrayGetUBound(pRects, 1, &uBound);
                    if (uBound >= 3)
                    {
                        const LONG x = static_cast<LONG>(pData[0]);
                        const LONG y = static_cast<LONG>(pData[1]);
                        const LONG w = static_cast<LONG>(pData[2]);
                        const LONG h = static_cast<LONG>(pData[3]);

                        if (w > 0 && h > 0)
                        {
                            if (item.screenRect.left != x || item.screenRect.top != y ||
                                item.screenRect.right != (x + w) || item.screenRect.bottom != (y + h))
                            {
                                item.screenRect.left = x;
                                item.screenRect.top = y;
                                item.screenRect.right = x + w;
                                item.screenRect.bottom = y + h;

                                SetWindowPos(item.hwndUnderline, HWND_TOPMOST, x, y + h - 2, w, 6, SWP_NOACTIVATE);
                            }
                        }
                    }
                    SafeArrayUnaccessData(pRects);
                }
                SafeArrayDestroy(pRects);
            }
        }
        ++i;
    }
}

void OverlayWindow::addUnderline(std::string_view word,
                                const RECT& wordScreenRect,
                                const std::vector<std::string>& topSuggestions,
                                IUIAutomationTextRange* pTextRange,
                                HWND targetHwnd)
{
    if (word.empty() || topSuggestions.empty())
    {
        if (pTextRange) pTextRange->Release();
        return;
    }

    // Check if an underline already exists near this position
    for (auto& item : m_items)
    {
        if (std::abs(item.screenRect.left - wordScreenRect.left) < 8 &&
            std::abs(item.screenRect.top - wordScreenRect.top) < 8)
        {
            item.word = std::string(word);
            item.screenRect = wordScreenRect;
            item.suggestions = topSuggestions;
            if (item.pTextRange) item.pTextRange->Release();
            item.pTextRange = pTextRange;
            item.targetHwnd = targetHwnd;

            const int x = wordScreenRect.left;
            const int y = wordScreenRect.bottom - 2;
            const int w = std::max(16L, wordScreenRect.right - wordScreenRect.left);
            const int h = 6;
            SetWindowPos(item.hwndUnderline, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
            InvalidateRect(item.hwndUnderline, nullptr, TRUE);
            return;
        }
    }

    const size_t id = m_nextId++;
    const int x = wordScreenRect.left;
    const int y = wordScreenRect.bottom - 2;
    const int w = std::max(16L, wordScreenRect.right - wordScreenRect.left);
    const int h = 6;

    HWND hwnd = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        TEXT("AutoCorrectUnderlineItemWindow"),
        TEXT("AutoCorrectSquiggly"),
        WS_POPUP | WS_VISIBLE,
        x, y, w, h,
        nullptr, nullptr, m_hInstance, nullptr
    );

    if (!hwnd)
    {
        if (pTextRange) pTextRange->Release();
        return;
    }

    SetLayeredWindowAttributes(hwnd, RGB(0, 0, 0), 0, LWA_COLORKEY);
    SetWindowLongPtr(hwnd, GWLP_USERDATA, static_cast<LONG_PTR>(id));

    UnderlineItem item;
    item.id = id;
    item.word = std::string(word);
    item.screenRect = wordScreenRect;
    item.suggestions = topSuggestions;
    item.hwndUnderline = hwnd;
    item.pTextRange = pTextRange;
    item.targetHwnd = targetHwnd;

    m_items.push_back(std::move(item));

    InvalidateRect(hwnd, nullptr, TRUE);
    // CRITICAL: We DO NOT show m_hwndPopup! It only shows when the user hovers over the word.
}

void OverlayWindow::removeUnderline(size_t id)
{
    for (auto it = m_items.begin(); it != m_items.end(); ++it)
    {
        if (it->id == id)
        {
            if (it->hwndUnderline)
            {
                DestroyWindow(it->hwndUnderline);
            }
            if (it->pTextRange)
            {
                it->pTextRange->Release();
            }
            m_items.erase(it);
            break;
        }
    }

    hidePopup();
}

void OverlayWindow::clearAll()
{
    hidePopup();

    for (auto& item : m_items)
    {
        if (item.hwndUnderline)
        {
            DestroyWindow(item.hwndUnderline);
        }
        if (item.pTextRange)
        {
            item.pTextRange->Release();
        }
    }
    m_items.clear();
}

void OverlayWindow::hidePopup()
{
    m_popupVisible = false;
    m_activeHoveredIndex = -1;
    m_hoveredSuggestionIndex = -1;

    if (m_hwndPopup)
    {
        ShowWindow(m_hwndPopup, SW_HIDE);
    }
}

bool OverlayWindow::selectSuggestionIndex(size_t index)
{
    UnderlineItem targetItem;
    bool found = false;

    if (m_activeHoveredIndex >= 0 && m_activeHoveredIndex < static_cast<int>(m_items.size()))
    {
        targetItem = m_items[m_activeHoveredIndex];
        found = true;
    }
    else if (!m_items.empty())
    {
        // Default to most recently typed misspelled word
        targetItem = m_items.back();
        found = true;
    }

    if (found && index >= 1 && index <= targetItem.suggestions.size())
    {
        const std::string chosen = targetItem.suggestions[index - 1];
        const size_t targetId = targetItem.id;

        hidePopup();

        if (m_onSelected)
        {
            m_onSelected(targetItem, chosen);
        }

        removeUnderline(targetId);
        return true;
    }

    return false;
}

void OverlayWindow::paintUnderline(HWND hwnd, HDC hdc)
{
    RECT rc;
    GetClientRect(hwnd, &rc);

    // Black background matches LWA_COLORKEY and renders transparent
    HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &rc, hBlack);
    DeleteObject(hBlack);

    HPEN hRedPen = CreatePen(PS_SOLID, 2, RGB(235, 45, 45));
    HGDIOBJ hOldPen = SelectObject(hdc, hRedPen);

    // Draw sinusoidal squiggly wave across the entire word width
    const int w = rc.right;
    bool up = true;
    MoveToEx(hdc, 0, 3, nullptr);

    for (int x = 0; x < w; x += 3)
    {
        int y = up ? 1 : 4;
        LineTo(hdc, x, y);
        up = !up;
    }

    SelectObject(hdc, hOldPen);
    DeleteObject(hRedPen);
}

void OverlayWindow::paintPopup(HDC hdc)
{
    if (m_activeHoveredIndex < 0 || m_activeHoveredIndex >= static_cast<int>(m_items.size()))
    {
        return;
    }

    const auto& item = m_items[m_activeHoveredIndex];

    RECT rc;
    GetClientRect(m_hwndPopup, &rc);

    // Dark Mode Theme Background
    HBRUSH hBg = CreateSolidBrush(RGB(32, 34, 40));
    FillRect(hdc, &rc, hBg);
    DeleteObject(hBg);

    // Border
    HPEN hBorder = CreatePen(PS_SOLID, 1, RGB(70, 75, 90));
    HGDIOBJ hOldPen = SelectObject(hdc, hBorder);
    HGDIOBJ hOldBrush = SelectObject(hdc, GetStockObject(HOLLOW_BRUSH));
    Rectangle(hdc, rc.left, rc.top, rc.right, rc.bottom);
    SelectObject(hdc, hOldPen);
    SelectObject(hdc, hOldBrush);
    DeleteObject(hBorder);

    SetBkMode(hdc, TRANSPARENT);
    HGDIOBJ hOldFont = SelectObject(hdc, m_hFont);

    const int itemH = 28;
    const int startY = 6;

    for (size_t i = 0; i < item.suggestions.size(); ++i)
    {
        const int y = startY + static_cast<int>(i) * itemH;
        RECT itemRc = {rc.left + 4, y, rc.right - 4, y + itemH};

        // Highlight hovered item
        if (static_cast<int>(i) == m_hoveredSuggestionIndex)
        {
            HBRUSH hHover = CreateSolidBrush(RGB(55, 60, 75));
            FillRect(hdc, &itemRc, hHover);
            DeleteObject(hHover);
        }

        // Draw index badge: "1.", "2.", "3."
        SelectObject(hdc, m_hFontBold);
        SetTextColor(hdc, RGB(90, 180, 255));
        std::string badge = std::to_string(i + 1) + ".";
        RECT badgeRc = {itemRc.left + 8, y + 4, itemRc.left + 26, y + itemH};
        DrawTextA(hdc, badge.c_str(), -1, &badgeRc, DT_LEFT | DT_SINGLELINE);

        // Draw suggestion word
        SelectObject(hdc, m_hFont);
        SetTextColor(hdc, RGB(240, 242, 245));
        RECT wordRc = {itemRc.left + 28, y + 4, itemRc.right - 55, y + itemH};
        DrawTextA(hdc, item.suggestions[i].c_str(), -1, &wordRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

        // Draw shortcut hint "(Alt+N)"
        SetTextColor(hdc, RGB(140, 145, 160));
        std::string shortcut = "(Alt+" + std::to_string(i + 1) + ")";
        RECT shortRc = {itemRc.right - 55, y + 4, itemRc.right - 8, y + itemH};
        DrawTextA(hdc, shortcut.c_str(), -1, &shortRc, DT_RIGHT | DT_SINGLELINE);
    }

    SelectObject(hdc, hOldFont);
}

void OverlayWindow::onPopupMouseMove(int /*x*/, int y)
{
    if (m_activeHoveredIndex < 0 || m_activeHoveredIndex >= static_cast<int>(m_items.size()))
    {
        return;
    }

    const auto& item = m_items[m_activeHoveredIndex];
    const int itemH = 28;
    const int startY = 6;
    int idx = (y - startY) / itemH;

    if (idx >= 0 && idx < static_cast<int>(item.suggestions.size()))
    {
        if (idx != m_hoveredSuggestionIndex)
        {
            m_hoveredSuggestionIndex = idx;
            InvalidateRect(m_hwndPopup, nullptr, FALSE);
        }
    }
    else
    {
        if (m_hoveredSuggestionIndex != -1)
        {
            m_hoveredSuggestionIndex = -1;
            InvalidateRect(m_hwndPopup, nullptr, FALSE);
        }
    }
}

void OverlayWindow::onPopupLButtonDown(int /*x*/, int y)
{
    if (m_activeHoveredIndex < 0 || m_activeHoveredIndex >= static_cast<int>(m_items.size()))
    {
        return;
    }

    const auto& item = m_items[m_activeHoveredIndex];
    const int itemH = 28;
    const int startY = 6;
    int idx = (y - startY) / itemH;

    if (idx >= 0 && idx < static_cast<int>(item.suggestions.size()))
    {
        selectSuggestionIndex(static_cast<size_t>(idx + 1));
    }
}

LRESULT CALLBACK OverlayWindow::UnderlineWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (s_instance) s_instance->paintUnderline(hwnd, hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        // Clicking directly on the squiggly line chooses the #1 suggestion
        if (s_instance)
        {
            const auto id = static_cast<size_t>(GetWindowLongPtr(hwnd, GWLP_USERDATA));
            for (size_t i = 0; i < s_instance->m_items.size(); ++i)
            {
                if (s_instance->m_items[i].id == id)
                {
                    s_instance->m_activeHoveredIndex = static_cast<int>(i);
                    s_instance->selectSuggestionIndex(1);
                    break;
                }
            }
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

LRESULT CALLBACK OverlayWindow::PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam)
{
    switch (msg)
    {
    case WM_PAINT:
    {
        PAINTSTRUCT ps;
        HDC hdc = BeginPaint(hwnd, &ps);
        if (s_instance) s_instance->paintPopup(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_TIMER:
    {
        if (s_instance && wParam == 1001)
        {
            s_instance->updatePositions();
        }
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (s_instance)
        {
            s_instance->onPopupMouseMove(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        if (s_instance)
        {
            s_instance->onPopupLButtonDown(LOWORD(lParam), HIWORD(lParam));
        }
        return 0;
    }
    default:
        break;
    }
    return DefWindowProc(hwnd, msg, wParam, lParam);
}

#else

bool OverlayWindow::initialize(HINSTANCE) { return false; }
void OverlayWindow::addUnderline(std::string_view, const RECT&, const std::vector<std::string>&, IUIAutomationTextRange*, HWND) {}
void OverlayWindow::removeUnderline(size_t) {}
void OverlayWindow::clearAll() {}
void OverlayWindow::showPopupForIndex(size_t) {}
void OverlayWindow::hidePopup() {}
bool OverlayWindow::selectSuggestionIndex(size_t) { return false; }
void OverlayWindow::updatePositions() {}

#endif

} // namespace AutoCorrect
