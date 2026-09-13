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

#include <functional>
#include <string>
#include <string_view>
#include <vector>

namespace AutoCorrect
{

struct UnderlineItem
{
    size_t id{0};
    std::string word;
    RECT screenRect{0, 0, 0, 0};
    std::vector<std::string> suggestions;
#ifdef _WIN32
    HWND hwndUnderline{nullptr};
    IUIAutomationTextRange* pTextRange{nullptr};
    HWND targetHwnd{nullptr};
#endif
};

/**
 * @brief Manages non-activating transparent overlay windows that render red squiggly
 * underlines beneath EVERY misspelled word on screen, and displays an interactive
 * Top 3 suggestion card when the user hovers over ANY part of the word.
 * Automatically tracks word movement (scrolling, typing) and removes underlines
 * if the user manually corrects the word.
 */
class OverlayWindow
{
public:
    using SuggestionCallback = std::function<void(const UnderlineItem& item, std::string_view chosen)>;

    OverlayWindow();
    ~OverlayWindow();

    OverlayWindow(const OverlayWindow&) = delete;
    OverlayWindow& operator=(const OverlayWindow&) = delete;

    bool initialize(HINSTANCE hInstance);

    /**
     * @brief Adds a misspelled word to be underlined on screen.
     * Crucial: This does NOT show any popup! It ONLY creates/shows the red squiggly underline.
     */
    void addUnderline(std::string_view word,
                      const RECT& wordScreenRect,
                      const std::vector<std::string>& topSuggestions,
                      IUIAutomationTextRange* pTextRange = nullptr,
                      HWND targetHwnd = nullptr);

    /**
     * @brief Removes a specific word's underline by its ID.
     */
    void removeUnderline(size_t id);

    /**
     * @brief Clears all active squiggly underlines on screen.
     */
    void clearAll();

    /**
     * @brief Displays the Top 3 suggestion card for a specific word index.
     */
    void showPopupForIndex(size_t index);

    /**
     * @brief Hides the suggestion popup card.
     */
    void hidePopup();

    /**
     * @brief Sets callback triggered when a suggestion is clicked or chosen via hotkey.
     */
    void setSuggestionCallback(SuggestionCallback callback)
    {
        m_onSelected = std::move(callback);
    }

    /**
     * @brief Selects suggestion by 1-based index (1, 2, 3) for the currently hovered or most recent word.
     */
    bool selectSuggestionIndex(size_t index);

    [[nodiscard]] bool isPopupVisible() const noexcept { return m_popupVisible; }
    [[nodiscard]] size_t getActiveWordCount() const noexcept { return m_items.size(); }
    [[nodiscard]] const UnderlineItem* getMostRecentItem() const noexcept
    {
        return m_items.empty() ? nullptr : &m_items.back();
    }

    void updatePositions();

private:
#ifdef _WIN32
    static LRESULT CALLBACK UnderlineWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK PopupWndProc(HWND hwnd, UINT msg, WPARAM wParam, LPARAM lParam);
    static LRESULT CALLBACK LowLevelMouseProc(int nCode, WPARAM wParam, LPARAM lParam);

    void paintUnderline(HWND hwnd, HDC hdc);
    void paintPopup(HDC hdc);
    void onPopupMouseMove(int x, int y);
    void onPopupLButtonDown(int x, int y);
    void handleMouseMoveGlobal(POINT pt);

    HINSTANCE m_hInstance{nullptr};
    HWND m_hwndPopup{nullptr};
    HHOOK m_mouseHook{nullptr};
    HFONT m_hFont{nullptr};
    HFONT m_hFontBold{nullptr};
#endif

    std::vector<UnderlineItem> m_items;
    size_t m_nextId{1};

    int m_activeHoveredIndex{-1};
    bool m_popupVisible{false};
    int m_hoveredSuggestionIndex{-1};

    SuggestionCallback m_onSelected;

    static OverlayWindow* s_instance;
};

} // namespace AutoCorrect
