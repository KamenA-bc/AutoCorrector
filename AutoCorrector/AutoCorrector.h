#pragma once

#include <string>
#include <string_view>
#include <unordered_map>
#include <unordered_set>
#include <vector>
#include "Tokenizer.h"
#include "LanguageModel.h"
#include "AdaptiveLearner.h"

/**
 * @brief AutoCorrector 3.0: High-accuracy spelling engine with strict distance tiers,
 * compound word splitting, top-3 suggestions, and adaptive personal typing pattern learning.
 */
class CAutoCorrector
{
public:
    struct WordEntry
    {
        std::string word;
        int frequency{0};
    };

    struct CandidateScore
    {
        std::string word;
        double score{-1e9};
        float distance{99.0f};
    };

    CAutoCorrector();
    ~CAutoCorrector() = default;

    // Rule of Zero: standard container members handle copy/move safely
    CAutoCorrector(const CAutoCorrector&) = default;
    CAutoCorrector& operator=(const CAutoCorrector&) = default;
    CAutoCorrector(CAutoCorrector&&) noexcept = default;
    CAutoCorrector& operator=(CAutoCorrector&&) noexcept = default;

    void load(const std::string& filename);
    void loadUserDictionary(const std::string& filename);
    void addUserWord(std::string_view word, int frequency = 100);

    bool saveBinary(const std::string& filename) const;
    bool loadBinary(const std::string& filename);

    /**
     * @brief Returns the single best correction (or empty if none / already correct).
     */
    [[nodiscard]] std::string wordLookUp(std::string_view word) const;
    [[nodiscard]] std::string wordLookUp(std::string_view word, std::string_view prevWord) const;

    [[nodiscard]] std::string wordLookUp(const std::string& word) const
    {
        return wordLookUp(std::string_view(word));
    }

    /**
     * @brief Returns the Top N suggestions for a misspelled word, ranked by likelihood.
     */
    [[nodiscard]] std::vector<std::string> getTopSuggestions(std::string_view word,
                                                              std::string_view prevWord = "",
                                                              size_t maxCount = 3) const;

    /**
     * @brief Checks if an unknown word is two merged words (e.g. "abunch" -> "a bunch").
     */
    [[nodiscard]] std::string checkCompoundSplit(std::string_view lower) const;

    /**
     * @brief Records that the user accepted a correction, updating personal vocabulary and typo models.
     */
    void recordUserAcceptedCorrection(std::string_view typo, std::string_view correction);

    [[nodiscard]] AutoCorrect::AdaptiveLearner& getLearner() noexcept
    {
        return m_learner;
    }

    [[nodiscard]] const AutoCorrect::AdaptiveLearner& getLearner() const noexcept
    {
        return m_learner;
    }

    [[nodiscard]] const AutoCorrect::LanguageModel& getLanguageModel() const noexcept
    {
        return m_languageModel;
    }

    [[nodiscard]] bool isWordInDictionary(std::string_view word) const;

private:
    static float damerauLevenshtein(std::string_view s1, std::string_view s2) noexcept;
    static void generateDeletes(std::string_view word, int maxDist, std::unordered_set<std::string>& deletes);

    // Dictionary representation:
    std::vector<WordEntry> m_words;
    std::unordered_map<std::string, int> m_wordIndex;
    std::unordered_map<std::string, std::vector<int>> m_deletes;

    // Contextual language model
    AutoCorrect::LanguageModel m_languageModel;

    // Personal adaptive typing pattern learner
    AutoCorrect::AdaptiveLearner m_learner;
};
