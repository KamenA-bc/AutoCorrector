#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <vector>
#include <iosfwd>
#include <cstdint>

namespace AutoCorrect
{

/**
 * @brief Contextual N-Gram Language Model providing unigram and bigram transition probabilities.
 */
class LanguageModel
{
public:
    LanguageModel() = default;

    /**
     * @brief Trains the language model on a stream of tokenized words.
     */
    void train(const std::vector<std::string>& tokens);

    /**
     * @brief Adds or updates frequency for an individual word.
     */
    void addWord(std::string_view word, int count = 1);

    /**
     * @brief Adds or updates frequency for a 2-word transition.
     */
    void addBigram(std::string_view w1, std::string_view w2, int count = 1);

    [[nodiscard]] int getUnigramCount(std::string_view word) const;
    [[nodiscard]] int getBigramCount(std::string_view w1, std::string_view w2) const;
    [[nodiscard]] size_t totalTokens() const noexcept { return m_totalTokens; }

    /**
     * @brief Computes a combined likelihood score for a candidate word given the typo input,
     * optional previous word context, and physical QWERTY edit distance.
     * Higher score = better candidate.
     */
    [[nodiscard]] double scoreCandidate(std::string_view candidate,
                                         std::string_view input,
                                         std::string_view prevWord,
                                         float qwertyEditDistance) const;

    /**
     * @brief Serializes the language model to a binary stream.
     */
    void serialize(std::ostream& os) const;

    /**
     * @brief Deserializes the language model from a binary stream.
     */
    bool deserialize(std::istream& is);

private:
    // Hash helper for pairs of strings (bigram key)
    struct PairHash
    {
        size_t operator()(const std::pair<std::string, std::string>& p) const noexcept
        {
            const size_t h1 = std::hash<std::string>{}(p.first);
            const size_t h2 = std::hash<std::string>{}(p.second);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    std::unordered_map<std::string, int> m_unigrams;
    std::unordered_map<std::pair<std::string, std::string>, int, PairHash> m_bigrams;
    size_t m_totalTokens{0};
};

} // namespace AutoCorrect
