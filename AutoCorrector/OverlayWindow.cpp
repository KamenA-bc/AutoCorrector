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
    hide();

    if (m_hwndPopup) DestroyWindow(m_hwndPopup);
    if (m_hwndUnderline) DestroyWindow(m_hwndUnderline);
    if (m_hFont) DeleteObject(m_hFont);
    if (m_hFontBold) DeleteObject(m_hFontBold);
#endif

    if (s_instance == this)
    {
        s_instance = nullptr;
    }
}

#ifdef _WIN32

bool OverlayWindow::initialize(HINSTANCE hInstance)
{
    if (!hInstance)
    {
        hInstance = GetModuleHandle(nullptr);
    }

    // 1. Register Underline Window Class
    WNDCLASSEX wcUnderline{};
    wcUnderline.cbSize = sizeof(WNDCLASSEX);
    wcUnderline.lpfnWndProc = UnderlineWndProc;
    wcUnderline.hInstance = hInstance;
    wcUnderline.lpszClassName = TEXT("AutoCorrectUnderlineWindow");
    wcUnderline.hCursor = LoadCursor(nullptr, IDC_HAND);
    RegisterClassEx(&wcUnderline);

    // 2. Register Suggestion Popup Window Class
    WNDCLASSEX wcPopup{};
    wcPopup.cbSize = sizeof(WNDCLASSEX);
    wcPopup.lpfnWndProc = PopupWndProc;
    wcPopup.hInstance = hInstance;
    wcPopup.lpszClassName = TEXT("AutoCorrectPopupCardWindow");
    wcPopup.hCursor = LoadCursor(nullptr, IDC_HAND);
    RegisterClassEx(&wcPopup);

    // 3. Create Underline Window (layered, transparent colorkey)
    m_hwndUnderline = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_LAYERED | WS_EX_NOACTIVATE,
        TEXT("AutoCorrectUnderlineWindow"),
        TEXT("AutoCorrectSquiggly"),
        WS_POPUP,
        0, 0, 100, 10,
        nullptr, nullptr, hInstance, nullptr
    );

    if (m_hwndUnderline)
    {
        SetLayeredWindowAttributes(m_hwndUnderline, RGB(0, 0, 0), 0, LWA_COLORKEY);
    }

    // 4. Create Suggestion Popup Window
    m_hwndPopup = CreateWindowEx(
        WS_EX_TOPMOST | WS_EX_TOOLWINDOW | WS_EX_NOACTIVATE,
        TEXT("AutoCorrectPopupCardWindow"),
        TEXT("AutoCorrectPopup"),
        WS_POPUP,
        0, 0, 220, 100,
        nullptr, nullptr, hInstance, nullptr
    );

    // 5. Create Modern Typography Fonts
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

    return (m_hwndUnderline != nullptr && m_hwndPopup != nullptr);
}

void OverlayWindow::showSquiggly(std::string_view word,
                                 const RECT& wordScreenRect,
                                 const std::vector<std::string>& topSuggestions)
{
    if (word.empty() || topSuggestions.empty() || !m_hwndUnderline)
    {
        hide();
        return;
    }

    m_currentWord = std::string(word);
    m_suggestions = topSuggestions;
    m_wordRect = wordScreenRect;
    m_hoveredIndex = -1;

    // Dimensions for squiggly underline
    const int x = wordScreenRect.left;
    const int y = wordScreenRect.bottom - 3;
    const int w = std::max(20L, wordScreenRect.right - wordScreenRect.left);
    const int h = 6;

    // Position and show squiggly underline
    SetWindowPos(m_hwndUnderline, HWND_TOPMOST, x, y, w, h, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(m_hwndUnderline, nullptr, TRUE);
    m_visible = true;

    // Position Popup Card immediately above the word
    const int popupW = 210;
    const int itemH = 28;
    const int popupH = static_cast<int>(m_suggestions.size()) * itemH + 12;
    int popupX = wordScreenRect.left;
    int popupY = wordScreenRect.top - popupH - 4;

    // Keep on screen if near top
    if (popupY < 20)
    {
        popupY = wordScreenRect.bottom + 8;
    }

    SetWindowPos(m_hwndPopup, HWND_TOPMOST, popupX, popupY, popupW, popupH, SWP_SHOWWINDOW | SWP_NOACTIVATE);
    InvalidateRect(m_hwndPopup, nullptr, TRUE);
    m_popupVisible = true;
}

void OverlayWindow::hide()
{
    m_visible = false;
    m_popupVisible = false;
    m_hoveredIndex = -1;
    m_currentWord.clear();
    m_suggestions.clear();

    if (m_hwndUnderline) ShowWindow(m_hwndUnderline, SW_HIDE);
    if (m_hwndPopup) ShowWindow(m_hwndPopup, SW_HIDE);
}

bool OverlayWindow::selectSuggestionIndex(size_t index)
{
    if (index >= 1 && index <= m_suggestions.size())
    {
        const std::string chosen = m_suggestions[index - 1];
        const std::string orig = m_currentWord;

        hide();

        if (m_onSelected)
        {
            m_onSelected(orig, chosen);
        }
        return true;
    }
    return false;
}

void OverlayWindow::paintUnderline(HDC hdc)
{
    RECT rc;
    GetClientRect(m_hwndUnderline, &rc);

    // Black background matches LWA_COLORKEY and renders 100% transparent
    HBRUSH hBlack = CreateSolidBrush(RGB(0, 0, 0));
    FillRect(hdc, &rc, hBlack);
    DeleteObject(hBlack);

    HPEN hRedPen = CreatePen(PS_SOLID, 2, RGB(235, 45, 45));
    HGDIOBJ hOldPen = SelectObject(hdc, hRedPen);

    // Draw sinusoidal squiggly wave
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

    for (size_t i = 0; i < m_suggestions.size(); ++i)
    {
        const int y = startY + static_cast<int>(i) * itemH;
        RECT itemRc = {rc.left + 4, y, rc.right - 4, y + itemH};

        // Highlight hovered item
        if (static_cast<int>(i) == m_hoveredIndex)
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
        DrawTextA(hdc, m_suggestions[i].c_str(), -1, &wordRc, DT_LEFT | DT_SINGLELINE | DT_END_ELLIPSIS);

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
    const int itemH = 28;
    const int startY = 6;
    int idx = (y - startY) / itemH;

    if (idx >= 0 && idx < static_cast<int>(m_suggestions.size()))
    {
        if (idx != m_hoveredIndex)
        {
            m_hoveredIndex = idx;
            InvalidateRect(m_hwndPopup, nullptr, FALSE);
        }
    }
    else
    {
        if (m_hoveredIndex != -1)
        {
            m_hoveredIndex = -1;
            InvalidateRect(m_hwndPopup, nullptr, FALSE);
        }
    }
}

void OverlayWindow::onPopupLButtonDown(int /*x*/, int y)
{
    const int itemH = 28;
    const int startY = 6;
    int idx = (y - startY) / itemH;

    if (idx >= 0 && idx < static_cast<int>(m_suggestions.size()))
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
        if (s_instance) s_instance->paintUnderline(hdc);
        EndPaint(hwnd, &ps);
        return 0;
    }
    case WM_MOUSEMOVE:
    {
        if (s_instance && !s_instance->m_popupVisible)
        {
            // Hover over squiggly underline shows popup
            ShowWindow(s_instance->m_hwndPopup, SW_SHOWNOACTIVATE);
            s_instance->m_popupVisible = true;
        }
        return 0;
    }
    case WM_LBUTTONDOWN:
    {
        if (s_instance)
        {
            // Left click on squiggly line chooses #1 suggestion
            s_instance->selectSuggestionIndex(1);
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
void OverlayWindow::showSquiggly(std::string_view, const RECT&, const std::vector<std::string>&) {}
void OverlayWindow::hide() {}
bool OverlayWindow::selectSuggestionIndex(size_t) { return false; }

#endif

} // namespace AutoCorrect
