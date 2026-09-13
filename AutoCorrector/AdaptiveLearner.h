#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>

namespace AutoCorrect
{

/**
 * @brief Learns personal typing habits, vocabulary, and mistake patterns over time.
 * Persists data to a local JSON/text file so the autocorrector adapts specifically
 * to the individual user.
 */
class AdaptiveLearner
{
public:
    explicit AdaptiveLearner(std::string profilePath = "user_profile.json");
    ~AdaptiveLearner();

    // Rule of Zero: standard containers manage resources
    AdaptiveLearner(const AdaptiveLearner&) = default;
    AdaptiveLearner& operator=(const AdaptiveLearner&) = default;
    AdaptiveLearner(AdaptiveLearner&&) noexcept = default;
    AdaptiveLearner& operator=(AdaptiveLearner&&) noexcept = default;

    /**
     * @brief Loads persistent profile from disk.
     */
    bool loadProfile();

    /**
     * @brief Saves profile to disk.
     */
    bool saveProfile() const;

    /**
     * @brief Records that the user typed or accepted a word.
     */
    void recordWordUsage(std::string_view word);

    /**
     * @brief Records that the user accepted a correction: typo -> correction.
     */
    void recordCorrectionAccepted(std::string_view typo, std::string_view correction);

    /**
     * @brief Records that the user rejected an auto-correction (undid it or chose original).
     */
    void recordCorrectionRejected(std::string_view typo, std::string_view rejectedCorrection);

    /**
     * @brief Checks if a correction was previously rejected for this typo.
     */
    [[nodiscard]] bool isRejected(std::string_view typo, std::string_view candidate) const;

    /**
     * @brief Returns personal frequency count for a word.
     */
    [[nodiscard]] int getPersonalWordCount(std::string_view word) const;

    /**
     * @brief Returns count of times user accepted candidate for typo.
     */
    [[nodiscard]] int getPersonalCorrectionCount(std::string_view typo, std::string_view candidate) const;

    /**
     * @brief Computes a personalization score adjustment for a candidate.
     */
    [[nodiscard]] double getPersonalScoreBoost(std::string_view typo, std::string_view candidate) const;

private:
    struct PairHash
    {
        size_t operator()(const std::pair<std::string, std::string>& p) const noexcept
        {
            const size_t h1 = std::hash<std::string>{}(p.first);
            const size_t h2 = std::hash<std::string>{}(p.second);
            return h1 ^ (h2 + 0x9e3779b9 + (h1 << 6) + (h1 >> 2));
        }
    };

    std::string m_profilePath;

    // 1. Personal word frequencies
    std::unordered_map<std::string, int> m_wordFrequencies;

    // 2. Personal accepted corrections: (typo, correction) -> count
    std::unordered_map<std::pair<std::string, std::string>, int, PairHash> m_acceptedCorrections;

    // 3. Rejected corrections: (typo, rejectedCandidate)
    std::unordered_set<std::pair<std::string, std::string>, PairHash> m_rejectedCorrections;

    bool m_dirty{false};
};

} // namespace AutoCorrect
