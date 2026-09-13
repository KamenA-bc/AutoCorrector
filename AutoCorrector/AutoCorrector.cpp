#include "AutoCorrector.h"
#include "QwertyModel.h"
#include <algorithm>
#include <cmath>
#include <fstream>
#include <iostream>
#include <cstdint>
#include <filesystem>

CAutoCorrector::CAutoCorrector()
    : m_learner("user_profile.json")
{
}

bool CAutoCorrector::isWordInDictionary(std::string_view word) const
{
    const std::string lower = AutoCorrect::toLowerString(word);
    return m_wordIndex.contains(lower);
}

float CAutoCorrector::damerauLevenshtein(std::string_view s1, std::string_view s2) noexcept
{
    const size_t len1 = s1.size();
    const size_t len2 = s2.size();

    if (len1 == 0) return static_cast<float>(len2);
    if (len2 == 0) return static_cast<float>(len1);

    if (std::abs(static_cast<int>(len1) - static_cast<int>(len2)) > 2)
    {
        return 99.0f;
    }

    constexpr size_t MAX_STACK = 48;
    if (len1 + 1 < MAX_STACK && len2 + 1 < MAX_STACK)
    {
        float d[MAX_STACK][MAX_STACK];

        for (size_t i = 0; i <= len1; ++i) d[i][0] = static_cast<float>(i);
        for (size_t j = 0; j <= len2; ++j) d[0][j] = static_cast<float>(j);

        for (size_t i = 1; i <= len1; ++i)
        {
            for (size_t j = 1; j <= len2; ++j)
            {
                const float cost = AutoCorrect::QwertyModel::substitutionCost(s1[i - 1], s2[j - 1]);
                const float del = d[i - 1][j] + 1.0f;
                const float ins = d[i][j - 1] + 1.0f;
                const float sub = d[i - 1][j - 1] + cost;
                float minVal = std::min({del, ins, sub});

                // Damerau adjacent transposition check
                if (i > 1 && j > 1 && s1[i - 1] == s2[j - 2] && s1[i - 2] == s2[j - 1])
                {
                    minVal = std::min(minVal, d[i - 2][j - 2] + 1.0f);
                }

                d[i][j] = minVal;
            }
        }

        return d[len1][len2];
    }

    return 99.0f;
}

static bool isApostropheOmission(std::string_view input, std::string_view cand) noexcept
{
    if (cand.size() != input.size() + 1) return false;
    size_t j = 0;
    for (size_t i = 0; i < cand.size(); ++i)
    {
        if (cand[i] == '\'') continue;
        if (j >= input.size() || input[j] != cand[i]) return false;
        ++j;
    }
    return (j == input.size());
}

void CAutoCorrector::generateDeletes(std::string_view word, int maxDist, std::unordered_set<std::string>& deletes)
{
    std::vector<std::string> queue;
    queue.emplace_back(word);

    for (int d = 1; d <= maxDist; ++d)
    {
        std::vector<std::string> nextQueue;
        for (const auto& w : queue)
        {
            if (w.size() <= 1) continue;

            for (size_t i = 0; i < w.size(); ++i)
            {
                std::string del;
                del.reserve(w.size() - 1);
                del.append(w, 0, i);
                del.append(w, i + 1, std::string::npos);

                if (deletes.insert(del).second)
                {
                    nextQueue.push_back(std::move(del));
                }
            }
        }
        queue = std::move(nextQueue);
    }
}

std::string CAutoCorrector::checkCompoundSplit(std::string_view lower) const
{
    const size_t len = lower.size();
    if (len < 4) return "";

    static const std::unordered_set<std::string_view> s_validTwoLetterWords = {
        "am", "an", "as", "at", "be", "by", "do", "go", "he", "if",
        "in", "is", "it", "me", "my", "no", "of", "on", "or", "so",
        "to", "up", "us", "we"
    };

    // Test splitting at position k
    for (size_t k = 1; k < len; ++k)
    {
        const std::string_view p1 = lower.substr(0, k);
        const std::string_view p2 = lower.substr(k);

        // Only 'a' and 'i' can be single-letter words
        if (p1.size() == 1 && p1 != "a" && p1 != "i") continue;
        if (p2.size() == 1 && p2 != "a" && p2 != "i") continue;

        // Two-letter words must be authentic English words
        if (p1.size() == 2 && !s_validTwoLetterWords.contains(p1)) continue;
        if (p2.size() == 2 && !s_validTwoLetterWords.contains(p2)) continue;

        if (p2.size() < 2) continue;

        auto it1 = m_wordIndex.find(std::string(p1));
        auto it2 = m_wordIndex.find(std::string(p2));

        if (it1 != m_wordIndex.end() && it2 != m_wordIndex.end())
        {
            // Both parts must be common established words
            if (m_words[it1->second].frequency >= 30 && m_words[it2->second].frequency >= 30)
            {
                std::string result;
                result.reserve(len + 1);
                result.append(p1);
                result.push_back(' ');
                result.append(p2);
                return result;
            }
        }
    }

    return "";
}

void CAutoCorrector::recordUserAcceptedCorrection(std::string_view typo, std::string_view correction)
{
    addUserWord(correction, 100);
    m_learner.recordCorrectionAccepted(typo, correction);
}

void CAutoCorrector::load(const std::string& filename)
{
    const std::string modernUnigrams = "frequency_dictionary_en_82_765.txt";
    const std::string modernBigrams = "frequency_bigramdictionary_en_243_342.txt";

    std::unordered_map<std::string, int> wordCounts;
    wordCounts.reserve(100000);

    bool loadedModern = false;
    if (std::filesystem::exists(modernUnigrams))
    {
        std::cout << "[Dictionary] Loading modern English unigram corpus (" << modernUnigrams << ")...\n";
        std::ifstream uniFile(modernUnigrams);
        std::string word;
        int64_t rawFreq;
        while (uniFile >> word >> rawFreq)
        {
            const std::string lower = AutoCorrect::toLowerString(word);
            const int scaled = static_cast<int>(std::clamp<int64_t>(rawFreq / 1000LL, 10LL, 2000000000LL));
            wordCounts[lower] = scaled;
            m_languageModel.addWord(lower, scaled);
        }
        loadedModern = true;

        if (std::filesystem::exists(modernBigrams))
        {
            std::cout << "[Dictionary] Loading Google Web 1T bigrams (" << modernBigrams << ")...\n";
            std::ifstream biFile(modernBigrams);
            std::string w1, w2;
            while (biFile >> w1 >> w2 >> rawFreq)
            {
                const int scaled = static_cast<int>(std::clamp<int64_t>(rawFreq / 1000LL, 1LL, 2000000000LL));
                m_languageModel.addBigram(AutoCorrect::toLowerString(w1), AutoCorrect::toLowerString(w2), scaled);
            }
        }
    }

    if (!loadedModern)
    {
        std::ifstream file(filename, std::ios_base::binary | std::ios_base::in);
        if (file.is_open())
        {
            file.seekg(0, std::ios_base::end);
            const auto length = file.tellg();
            file.seekg(0, std::ios_base::beg);

            std::string data(static_cast<std::size_t>(length), '\0');
            file.read(&data[0], length);

            const auto tokens = AutoCorrect::tokenizeCorpus(data);
            m_languageModel.train(tokens);

            for (const auto& token : tokens)
            {
                wordCounts[token]++;
            }
        }
    }

    // Modern contractions and essential technical words baseline
    const std::vector<std::pair<std::string, int>> modernBaseline = {
        {"don't", 1500000}, {"doesn't", 1200000}, {"didn't", 1200000},
        {"won't", 1000000}, {"can't", 1200000}, {"couldn't", 800000},
        {"shouldn't", 800000}, {"wouldn't", 800000}, {"they're", 1000000},
        {"you're", 1200000}, {"we're", 1000000}, {"it's", 2000000},
        {"that's", 1000000}, {"what's", 800000}, {"who's", 600000},
        {"there's", 1000000}, {"here's", 800000}, {"isn't", 1000000},
        {"aren't", 800000}, {"wasn't", 800000}, {"weren't", 600000},
        {"haven't", 800000}, {"hasn't", 600000}, {"hadn't", 600000},
        {"website", 500000}, {"online", 600000}, {"email", 600000},
        {"github", 400000}, {"code", 800000}, {"software", 500000},
        {"lot", 800000}, {"hate", 600000}, {"heat", 600000},
        {"cook", 700000}, {"coke", 500000}, {"bunch", 600000},
        {"python", 600000}, {"vscode", 500000}
    };

    for (const auto& [w, count] : modernBaseline)
    {
        wordCounts[w] = std::max(wordCounts[w], count);
        m_languageModel.addWord(w, count);
    }

    // Populate words vector and exact index
    m_words.clear();
    m_words.reserve(wordCounts.size());
    m_wordIndex.clear();
    m_wordIndex.reserve(wordCounts.size());

    for (auto& [w, count] : wordCounts)
    {
        const int id = static_cast<int>(m_words.size());
        m_words.push_back({w, count});
        m_wordIndex[w] = id;
    }

    // Build symmetric delete inverted index (distance <= 2)
    m_deletes.clear();
    m_deletes.reserve(wordCounts.size() * 20);

    for (int id = 0; id < static_cast<int>(m_words.size()); ++id)
    {
        const auto& w = m_words[id].word;

        // Index the word under itself (distance 0)
        m_deletes[w].push_back(id);

        // Index the word under its deletion variants (distance 1 and 2)
        std::unordered_set<std::string> dels;
        generateDeletes(w, 2, dels);
        for (const auto& d : dels)
        {
            m_deletes[d].push_back(id);
        }
    }
}

void CAutoCorrector::loadUserDictionary(const std::string& filename)
{
    std::ifstream file(filename);
    if (!file.is_open()) return;

    std::string word;
    while (file >> word)
    {
        if (word.empty() || word[0] == '#') continue;
        addUserWord(word, 100);
    }
}

void CAutoCorrector::addUserWord(std::string_view word, int frequency)
{
    const std::string lower = AutoCorrect::toLowerString(word);
    if (lower.empty()) return;

    auto it = m_wordIndex.find(lower);
    if (it != m_wordIndex.end())
    {
        m_words[it->second].frequency += frequency;
        m_languageModel.addWord(lower, frequency);
        m_learner.recordWordUsage(lower);
        return;
    }

    const int id = static_cast<int>(m_words.size());
    m_words.push_back({lower, frequency});
    m_wordIndex[lower] = id;
    m_languageModel.addWord(lower, frequency);
    m_learner.recordWordUsage(lower);

    m_deletes[lower].push_back(id);

    std::unordered_set<std::string> dels;
    generateDeletes(lower, 2, dels);
    for (const auto& d : dels)
    {
        m_deletes[d].push_back(id);
    }
}

bool CAutoCorrector::saveBinary(const std::string& filename) const
{
    std::ofstream os(filename, std::ios_base::binary);
    if (!os.is_open()) return false;

    // Magic header
    const char magic[8] = {'A', 'U', 'T', 'O', 'C', 'O', 'R', '3'};
    os.write(magic, sizeof(magic));

    // Words
    const uint32_t numWords = static_cast<uint32_t>(m_words.size());
    os.write(reinterpret_cast<const char*>(&numWords), sizeof(numWords));

    for (const auto& entry : m_words)
    {
        const uint16_t len = static_cast<uint16_t>(entry.word.size());
        os.write(reinterpret_cast<const char*>(&len), sizeof(len));
        os.write(entry.word.data(), len);
        const int32_t freq = entry.frequency;
        os.write(reinterpret_cast<const char*>(&freq), sizeof(freq));
    }

    // Deletes
    const uint32_t numDeletes = static_cast<uint32_t>(m_deletes.size());
    os.write(reinterpret_cast<const char*>(&numDeletes), sizeof(numDeletes));

    for (const auto& [delKey, ids] : m_deletes)
    {
        const uint16_t len = static_cast<uint16_t>(delKey.size());
        os.write(reinterpret_cast<const char*>(&len), sizeof(len));
        os.write(delKey.data(), len);

        const uint32_t idCount = static_cast<uint32_t>(ids.size());
        os.write(reinterpret_cast<const char*>(&idCount), sizeof(idCount));
        for (int id : ids)
        {
            const int32_t val = id;
            os.write(reinterpret_cast<const char*>(&val), sizeof(val));
        }
    }

    // Language Model
    m_languageModel.serialize(os);

    return true;
}

bool CAutoCorrector::loadBinary(const std::string& filename)
{
    std::ifstream is(filename, std::ios_base::binary);
    if (!is.is_open()) return false;

    char magic[8];
    if (!is.read(magic, sizeof(magic))) return false;
    if (std::string_view(magic, 8) != "AUTOCOR3") return false;

    // Words
    uint32_t numWords = 0;
    if (!is.read(reinterpret_cast<char*>(&numWords), sizeof(numWords))) return false;

    m_words.clear();
    m_words.reserve(numWords);
    m_wordIndex.clear();
    m_wordIndex.reserve(numWords);

    for (uint32_t i = 0; i < numWords; ++i)
    {
        uint16_t len = 0;
        is.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string w(len, '\0');
        is.read(&w[0], len);
        int32_t freq = 0;
        is.read(reinterpret_cast<char*>(&freq), sizeof(freq));

        const int id = static_cast<int>(m_words.size());
        m_words.push_back({w, freq});
        m_wordIndex[w] = id;
    }

    // Deletes
    uint32_t numDeletes = 0;
    if (!is.read(reinterpret_cast<char*>(&numDeletes), sizeof(numDeletes))) return false;

    m_deletes.clear();
    m_deletes.reserve(numDeletes);

    for (uint32_t i = 0; i < numDeletes; ++i)
    {
        uint16_t len = 0;
        is.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string delKey(len, '\0');
        is.read(&delKey[0], len);

        uint32_t idCount = 0;
        is.read(reinterpret_cast<char*>(&idCount), sizeof(idCount));
        std::vector<int> ids(idCount);
        for (uint32_t j = 0; j < idCount; ++j)
        {
            int32_t id = 0;
            is.read(reinterpret_cast<char*>(&id), sizeof(id));
            ids[j] = id;
        }

        m_deletes[std::move(delKey)] = std::move(ids);
    }

    // Language Model
    if (!m_languageModel.deserialize(is)) return false;

    return true;
}

std::vector<std::string> CAutoCorrector::getTopSuggestions(std::string_view word,
                                                           std::string_view prevWord,
                                                           size_t maxCount) const
{
    if (word.empty()) return {};

    // 1. Detect casing pattern (Title, Upper, Lower, MixedCode)
    const auto pattern = AutoCorrect::detectCase(word);
    if (pattern == AutoCorrect::CasePattern::MixedCode)
    {
        return {}; // Code identifiers (camelCase, etc.) are correctly formed
    }

    const std::string lower = AutoCorrect::toLowerString(word);
    const std::string prevLower = AutoCorrect::toLowerString(prevWord);

    // 2. Exact Match Guard: If word is already a valid dictionary word, never flag it as a typo!
    const auto exactIt = m_wordIndex.find(lower);
    if (exactIt != m_wordIndex.end())
    {
        const int exactFreq = m_words[exactIt->second].frequency;

        // Exception: Handle rare Gutenberg typos like "dont" (freq 2) when "don't" (freq 1500) exists
        if (exactFreq < 5 && lower.find('\'') == std::string::npos)
        {
            auto itDel = m_deletes.find(lower);
            if (itDel != m_deletes.end())
            {
                for (int candId : itDel->second)
                {
                    const auto& candWord = m_words[candId].word;
                    if (isApostropheOmission(lower, candWord) && m_words[candId].frequency > exactFreq * 50)
                    {
                        return {AutoCorrect::applyCase(candWord, pattern)};
                    }
                }
            }
        }

        // Established valid words (e.g. "lot", "form", "hear", "this", "well") are INVIOLABLE
        return {};
    }

    // 3. Typo candidates gathering via SymSpell deletes
    std::unordered_set<std::string> inputDeletes;
    generateDeletes(lower, 2, inputDeletes);
    inputDeletes.insert(lower);

    std::vector<int> candidates;
    candidates.reserve(128);

    for (const auto& delKey : inputDeletes)
    {
        const auto it = m_deletes.find(delKey);
        if (it != m_deletes.end())
        {
            candidates.insert(candidates.end(), it->second.begin(), it->second.end());
        }
    }

    std::sort(candidates.begin(), candidates.end());
    candidates.erase(std::unique(candidates.begin(), candidates.end()), candidates.end());

    // 4. Strict Distance Tiers:
    // Tier 1: Single edits, transpositions, adjacent QWERTY slips, apostrophe omissions
    std::vector<CandidateScore> tier1;
    // Tier 2: Two distinct edits
    std::vector<CandidateScore> tier2;

    for (const int wordId : candidates)
    {
        const auto& candWord = m_words[wordId].word;

        if (std::abs(static_cast<int>(candWord.size()) - static_cast<int>(lower.size())) > 2)
        {
            continue;
        }

        // Skip if user explicitly rejected this suggestion in their profile
        if (m_learner.isRejected(lower, candWord))
        {
            continue;
        }

        float dist = damerauLevenshtein(lower, candWord);

        // Intentional shortcut: omitted apostrophe (e.g. "theyre" -> "they're", "doesnt" -> "doesn't")
        if (isApostropheOmission(lower, candWord))
        {
            dist = 0.2f;
        }

        if (dist > 2.5f)
        {
            continue;
        }

        // Combined score: Error likelihood + Language model + Personal typing profile boost
        double score = m_languageModel.scoreCandidate(candWord, lower, prevLower, dist);
        score += m_learner.getPersonalScoreBoost(lower, candWord);

        if (dist <= 1.45f)
        {
            tier1.push_back({candWord, score, dist});
        }
        else
        {
            tier2.push_back({candWord, score, dist});
        }
    }

    // Check compound split (e.g. "abunch" -> "a bunch", "alot" -> "a lot")
    const std::string splitWord = checkCompoundSplit(lower);
    if (!splitWord.empty())
    {
        // Realistic compound split log probability (dist 1.25f so genuine 1-edit single words like "bicycle" win over "by cycle")
        const double splitScore = -3.0 * 1.25 + 0.5 * std::log(100.0 / 1100000.0);
        tier1.push_back({splitWord, splitScore, 1.25f});
    }

    // Sort function by score descending
    auto sortByScore = [](const CandidateScore& a, const CandidateScore& b) {
        return a.score > b.score;
    };

    std::vector<std::string> results;

    // First, populate candidates from Tier 1 (distance <= 1.45)
    if (!tier1.empty())
    {
        std::sort(tier1.begin(), tier1.end(), sortByScore);
        for (const auto& item : tier1)
        {
            if (results.size() >= maxCount) break;
            const std::string formatted = AutoCorrect::applyCase(item.word, pattern);
            if (std::find(results.begin(), results.end(), formatted) == results.end())
            {
                results.push_back(formatted);
            }
        }
    }

    // Always fill remaining slots up to maxCount from Tier 2 so user receives all top suggestions!
    if (results.size() < maxCount && !tier2.empty())
    {
        std::sort(tier2.begin(), tier2.end(), sortByScore);
        for (const auto& item : tier2)
        {
            if (results.size() >= maxCount) break;
            const std::string formatted = AutoCorrect::applyCase(item.word, pattern);
            if (std::find(results.begin(), results.end(), formatted) == results.end())
            {
                results.push_back(formatted);
            }
        }
    }

    return results;
}

std::string CAutoCorrector::wordLookUp(std::string_view word) const
{
    return wordLookUp(word, "");
}

std::string CAutoCorrector::wordLookUp(std::string_view word, std::string_view prevWord) const
{
    if (word.empty()) return "";

    const auto pattern = AutoCorrect::detectCase(word);
    if (pattern == AutoCorrect::CasePattern::MixedCode)
    {
        return std::string(word); // Code identifiers remain untouched
    }

    const std::string lower = AutoCorrect::toLowerString(word);

    // If already a valid dictionary word, check if it's an apostrophe typo (like "dont" -> "don't")
    const auto exactIt = m_wordIndex.find(lower);
    if (exactIt != m_wordIndex.end())
    {
        const int exactFreq = m_words[exactIt->second].frequency;
        // Rare typo exception: "dont" (freq 2) when "don't" (freq 1500) exists
        if (exactFreq < 5 && lower.find('\'') == std::string::npos)
        {
            const auto suggestions = getTopSuggestions(word, prevWord, 1);
            if (!suggestions.empty() && suggestions.front() != lower)
            {
                return suggestions.front();
            }
        }
        return std::string(word); // Valid word returns itself!
    }

    const auto suggestions = getTopSuggestions(word, prevWord, 1);
    if (!suggestions.empty())
    {
        return suggestions.front();
    }

    return "";
}