#pragma once

#include <string>
#include <string_view>
#include <vector>

namespace AutoCorrect
{

enum class CasePattern
{
    Lower,      // "speling" -> "spelling"
    Title,      // "Speling" -> "Spelling"
    Upper,      // "SPELING" -> "SPELLING"
    MixedCode   // "myVariable", "camelCase" -> do not alter
};

/**
 * @brief Analyzes the casing pattern of an input word.
 */
[[nodiscard]] CasePattern detectCase(std::string_view word) noexcept;

/**
 * @brief Re-applies the detected casing pattern to a corrected word.
 */
[[nodiscard]] std::string applyCase(std::string_view target, CasePattern pattern);

/**
 * @brief Checks if a character is a valid word character, including internal apostrophe.
 */
[[nodiscard]] constexpr bool isAlpha(char c) noexcept
{
    return (c >= 'a' && c <= 'z') || (c >= 'A' && c <= 'Z');
}

/**
 * @brief Normalizes a single character to lowercase ASCII.
 */
[[nodiscard]] constexpr char toLower(char c) noexcept
{
    if (c >= 'A' && c <= 'Z')
    {
        return static_cast<char>(c + ('a' - 'A'));
    }
    return c;
}

/**
 * @brief Normalizes a word to lowercase while retaining internal apostrophes.
 */
[[nodiscard]] std::string toLowerString(std::string_view str);

/**
 * @brief Tokenizes an entire text corpus into clean words, handling contractions and stripping punctuation.
 */
[[nodiscard]] std::vector<std::string> tokenizeCorpus(std::string_view text);

} // namespace AutoCorrect
