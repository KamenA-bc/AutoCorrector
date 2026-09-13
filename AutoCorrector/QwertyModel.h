#pragma once

#include <cmath>
#include <array>
#include <string_view>
#include "Tokenizer.h"

namespace AutoCorrect
{

struct KeyCoord
{
    float row{0.0f};
    float col{0.0f};
    bool valid{false};
};

/**
 * @brief Provides physical Euclidean keyboard key coordinates and distance metrics for QWERTY layout.
 */
class QwertyModel
{
public:
    static constexpr KeyCoord getKeyCoord(char c) noexcept
    {
        const char lower = toLower(c);
        switch (lower)
        {
        // Row 1 (QWERTY)
        case 'q': return {1.0f, 0.0f, true};
        case 'w': return {1.0f, 1.0f, true};
        case 'e': return {1.0f, 2.0f, true};
        case 'r': return {1.0f, 3.0f, true};
        case 't': return {1.0f, 4.0f, true};
        case 'y': return {1.0f, 5.0f, true};
        case 'u': return {1.0f, 6.0f, true};
        case 'i': return {1.0f, 7.0f, true};
        case 'o': return {1.0f, 8.0f, true};
        case 'p': return {1.0f, 9.0f, true};

        // Row 2 (ASDF) - slightly offset by 0.25 on standard keyboards
        case 'a': return {2.0f, 0.25f, true};
        case 's': return {2.0f, 1.25f, true};
        case 'd': return {2.0f, 2.25f, true};
        case 'f': return {2.0f, 3.25f, true};
        case 'g': return {2.0f, 4.25f, true};
        case 'h': return {2.0f, 5.25f, true};
        case 'j': return {2.0f, 6.25f, true};
        case 'k': return {2.0f, 7.25f, true};
        case 'l': return {2.0f, 8.25f, true};
        case '\'': return {2.0f, 9.25f, true};

        // Row 3 (ZXCV) - offset by 0.75
        case 'z': return {3.0f, 0.75f, true};
        case 'x': return {3.0f, 1.75f, true};
        case 'c': return {3.0f, 2.75f, true};
        case 'v': return {3.0f, 3.75f, true};
        case 'b': return {3.0f, 4.75f, true};
        case 'n': return {3.0f, 5.75f, true};
        case 'm': return {3.0f, 6.75f, true};

        default: return {0.0f, 0.0f, false};
        }
    }

    /**
     * @brief Computes physical distance penalty for substituting c1 with c2.
     * Adjacent keys (fat-finger errors) return 0.6f - 0.8f.
     * Distant keys return 1.0f - 1.4f.
     */
    static float substitutionCost(char c1, char c2) noexcept
    {
        if (c1 == c2)
        {
            return 0.0f;
        }

        const KeyCoord k1 = getKeyCoord(c1);
        const KeyCoord k2 = getKeyCoord(c2);

        if (!k1.valid || !k2.valid)
        {
            return 1.0f; // Default standard cost
        }

        const float dRow = k1.row - k2.row;
        const float dCol = k1.col - k2.col;
        const float euclidean = std::sqrt(dRow * dRow + dCol * dCol);

        // Directly adjacent or diagonal neighbor (e.g. w-e, a-s, w-s)
        if (euclidean <= 1.42f)
        {
            return 0.65f; // Highly likely fat-finger typo
        }
        if (euclidean <= 2.25f)
        {
            return 0.85f; // Near neighbor
        }

        return 1.15f; // Distant key
    }
};

} // namespace AutoCorrect
