#include "Tokenizer.h"
#include <cctype>
#include <algorithm>

namespace AutoCorrect
{

CasePattern detectCase(std::string_view word) noexcept
{
    if (word.empty())
    {
        return CasePattern::Lower;
    }

    size_t upperCount = 0;
    size_t lowerCount = 0;
    bool firstIsUpper = isAlpha(word.front()) && (word.front() >= 'A' && word.front() <= 'Z');

    for (char c : word)
    {
        if (c >= 'A' && c <= 'Z')
        {
            ++upperCount;
        }
        else if (c >= 'a' && c <= 'z')
        {
            ++lowerCount;
        }
    }

    if (upperCount > 0 && lowerCount == 0)
    {
        return CasePattern::Upper;
    }
    if (firstIsUpper && upperCount == 1 && lowerCount > 0)
    {
        return CasePattern::Title;
    }
    if (upperCount == 0)
    {
        return CasePattern::Lower;
    }

    return CasePattern::MixedCode;
}

std::string applyCase(std::string_view target, CasePattern pattern)
{
    std::string result(target);
    if (result.empty())
    {
        return result;
    }

    switch (pattern)
    {
    case CasePattern::Lower:
        for (char& c : result)
        {
            c = toLower(c);
        }
        break;

    case CasePattern::Upper:
        for (char& c : result)
        {
            if (c >= 'a' && c <= 'z')
            {
                c = static_cast<char>(c - ('a' - 'A'));
            }
        }
        break;

    case CasePattern::Title:
        for (size_t i = 0; i < result.size(); ++i)
        {
            if (i == 0 && result[i] >= 'a' && result[i] <= 'z')
            {
                result[i] = static_cast<char>(result[i] - ('a' - 'A'));
            }
            else if (i > 0 && result[i] >= 'A' && result[i] <= 'Z')
            {
                result[i] = static_cast<char>(result[i] + ('a' - 'A'));
            }
        }
        break;

    case CasePattern::MixedCode:
        // Do not alter mixed / code identifiers
        break;
    }

    return result;
}

std::string toLowerString(std::string_view str)
{
    std::string result;
    result.reserve(str.size());
    for (char c : str)
    {
        result.push_back(toLower(c));
    }
    return result;
}

std::vector<std::string> tokenizeCorpus(std::string_view text)
{
    std::vector<std::string> tokens;
    tokens.reserve(text.size() / 6); // Approximation of word count

    const size_t len = text.size();
    size_t i = 0;

    while (i < len)
    {
        // Skip non-word characters
        while (i < len && !isAlpha(text[i]))
        {
            ++i;
        }

        if (i >= len)
        {
            break;
        }

        const size_t start = i;

        // Advance through letters and internal apostrophes
        while (i < len && (isAlpha(text[i]) || (text[i] == '\'' && i + 1 < len && isAlpha(text[i + 1]))))
        {
            ++i;
        }

        std::string_view tokenView = text.substr(start, i - start);

        // Strip trailing apostrophe if any
        while (!tokenView.empty() && tokenView.back() == '\'')
        {
            tokenView.remove_suffix(1);
        }

        if (!tokenView.empty())
        {
            tokens.push_back(toLowerString(tokenView));
        }
    }

    return tokens;
}

} // namespace AutoCorrect
