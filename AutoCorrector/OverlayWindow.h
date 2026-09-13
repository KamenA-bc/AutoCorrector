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

#include <functional>
#include <string>
#include <vector>

namespace AutoCorrect
{

/**
 * @brief Manages the non-activating transparent overlay window that renders red squiggly
 * underlines beneath misspelled words and displays the interactive Top 3 suggestion card.
 */
class OverlayWindow
{
public:
    using SuggestionCallback = std::function<void(std::string_view original, std::string_view chosen)>;

    OverlayWindow();
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    /**
     * @brief Initializes the overlay window class and handles.
     */
    bool initialize(HINSTANCE hInstance);

    /**
     * @brief Shows red squiggly underline at the specified word screen coordinates with candidate suggestions.
     */
    void showSquiggly(std::string_view word,
                      const RECT& wordScreenRect,
                      const std::vector<std::string>& topSuggestions);

    /**
     * @brief Hides the squiggly line and any suggestion popup.
     */
    void hide();

    /**
     * @brief Sets callback triggered when a suggestion is clicked or chosen via hotkey.
     */
    void setSuggestionCallback(SuggestionCallback callback)
    {
        m_onSelected = std::move(callback);
    }

    /**
     * @brief Selects suggestion by 1-based index (e.g. 1, 2, 3).
     */
    bool selectSuggestionIndex(size_t index);

    [[nodiscard]] bool isVisible() const noexcept { return m_visible; }
    [[nodiscard]] std::string_view currentWord() const noexcept { return m_currentWord; }

private:
#ifdef _WIN32
    static LRESULT CALLBACK UnderlineWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);

    void paintUnderline(HDC hdc);
    void paintPopup(HDC hdc);
    void onPopupMouseMove(int x, int y);
    void onPopupLButtonDown(int x, int y);

    HWND m_hwndUnderline{nullptr};
    HWND m_hwndPopup{nullptr};
    HFONT m_hFont{nullptr};
    HFONT m_hFontBold{nullptr};
#endif

    bool m_visible{false};
    bool m_popupVisible{false};
    int m_hoveredIndex{-1};

    std::string m_currentWord;
    std::vector<std::string> m_suggestions;
    RECT m_wordRect{0, 0, 0, 0};

    SuggestionCallback m_onSelected;

    static OverlayWindow* s_instance;
};

} // namespace AutoCorrect
