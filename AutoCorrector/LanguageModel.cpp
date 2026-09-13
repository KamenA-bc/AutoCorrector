#include "LanguageModel.h"
#include <cmath>
#include <iostream>

namespace AutoCorrect
{

void LanguageModel::train(const std::vector<std::string>& tokens)
{
    m_totalTokens += tokens.size();
    m_unigrams.reserve(m_unigrams.size() + tokens.size() / 2);
    m_bigrams.reserve(m_bigrams.size() + tokens.size());

    for (size_t i = 0; i < tokens.size(); ++i)
    {
        m_unigrams[tokens[i]]++;

        if (i > 0)
        {
            m_bigrams[{tokens[i - 1], tokens[i]}]++;
        }
    }
}

void LanguageModel::addWord(std::string_view word, int count)
{
    m_unigrams[std::string(word)] += count;
    m_totalTokens += count;
}

void LanguageModel::addBigram(std::string_view w1, std::string_view w2, int count)
{
    m_bigrams[{std::string(w1), std::string(w2)}] += count;
}

int LanguageModel::getUnigramCount(std::string_view word) const
{
    auto it = m_unigrams.find(std::string(word));
    return (it != m_unigrams.end()) ? it->second : 0;
}

int LanguageModel::getBigramCount(std::string_view w1, std::string_view w2) const
{
    auto it = m_bigrams.find({std::string(w1), std::string(w2)});
    return (it != m_bigrams.end()) ? it->second : 0;
}

double LanguageModel::scoreCandidate(std::string_view candidate,
                                     std::string_view /*input*/,
                                     std::string_view prevWord,
                                     float qwertyEditDistance) const
{
    // 1. Error model: physical QWERTY distance penalty
    // P(input | candidate) ~ exp(-3.0 * dist)
    const double errorLogLikelihood = -3.0 * static_cast<double>(qwertyEditDistance);

    // 2. Unigram frequency: log P(candidate)
    const int uCount = getUnigramCount(candidate);
    const double total = static_cast<double>(m_totalTokens > 0 ? m_totalTokens : 100000);
    const double unigramProb = (static_cast<double>(uCount) + 0.5) / (total + 50000.0);
    const double unigramLog = std::log(unigramProb);

    // 3. Contextual Bigram probability: log P(candidate | prevWord)
    double bigramLog = 0.0;
    if (!prevWord.empty())
    {
        const int prevCount = getUnigramCount(prevWord);
        const int biCount = getBigramCount(prevWord, candidate);

        if (prevCount > 0)
        {
            // Laplace-smoothed conditional probability
            const double condProb = (static_cast<double>(biCount) + 0.01) / (static_cast<double>(prevCount) + 1.0);
            bigramLog = std::log(condProb);
        }
        else
        {
            bigramLog = unigramLog; // Backoff to unigram
        }
    }

    // Weighted combined score:
    // Error distance is dominant, modulated by bigram context and unigram base rate
    if (!prevWord.empty())
    {
        return errorLogLikelihood + 0.4 * unigramLog + 0.8 * bigramLog;
    }

    return errorLogLikelihood + 0.5 * unigramLog;
}

void LanguageModel::serialize(std::ostream& os) const
{
    // Write total tokens
    const uint64_t total = static_cast<uint64_t>(m_totalTokens);
    os.write(reinterpret_cast<const char*>(&total), sizeof(total));

    // Write unigrams
    const uint32_t numUnigrams = static_cast<uint32_t>(m_unigrams.size());
    os.write(reinterpret_cast<const char*>(&numUnigrams), sizeof(numUnigrams));

    for (const auto& [word, count] : m_unigrams)
    {
        const uint16_t len = static_cast<uint16_t>(word.size());
        os.write(reinterpret_cast<const char*>(&len), sizeof(len));
        os.write(word.data(), len);
        const int32_t c = count;
        os.write(reinterpret_cast<const char*>(&c), sizeof(c));
    }

    // Write bigrams
    const uint32_t numBigrams = static_cast<uint32_t>(m_bigrams.size());
    os.write(reinterpret_cast<const char*>(&numBigrams), sizeof(numBigrams));

    for (const auto& [pair, count] : m_bigrams)
    {
        const uint16_t len1 = static_cast<uint16_t>(pair.first.size());
        os.write(reinterpret_cast<const char*>(&len1), sizeof(len1));
        os.write(pair.first.data(), len1);

        const uint16_t len2 = static_cast<uint16_t>(pair.second.size());
        os.write(reinterpret_cast<const char*>(&len2), sizeof(len2));
        os.write(pair.second.data(), len2);

        const int32_t c = count;
        os.write(reinterpret_cast<const char*>(&c), sizeof(c));
    }
}

bool LanguageModel::deserialize(std::istream& is)
{
    m_unigrams.clear();
    m_bigrams.clear();

    uint64_t total = 0;
    if (!is.read(reinterpret_cast<char*>(&total), sizeof(total)))
    {
        return false;
    }
    m_totalTokens = static_cast<size_t>(total);

    // Read unigrams
    uint32_t numUnigrams = 0;
    if (!is.read(reinterpret_cast<char*>(&numUnigrams), sizeof(numUnigrams)))
    {
        return false;
    }
    m_unigrams.reserve(numUnigrams);

    for (uint32_t i = 0; i < numUnigrams; ++i)
    {
        uint16_t len = 0;
        is.read(reinterpret_cast<char*>(&len), sizeof(len));
        std::string word(len, '\0');
        is.read(&word[0], len);
        int32_t count = 0;
        is.read(reinterpret_cast<char*>(&count), sizeof(count));
        m_unigrams[std::move(word)] = count;
    }

    // Read bigrams
    uint32_t numBigrams = 0;
    if (!is.read(reinterpret_cast<char*>(&numBigrams), sizeof(numBigrams)))
    {
        return false;
    }
    m_bigrams.reserve(numBigrams);

    for (uint32_t i = 0; i < numBigrams; ++i)
    {
        uint16_t len1 = 0;
        is.read(reinterpret_cast<char*>(&len1), sizeof(len1));
        std::string w1(len1, '\0');
        is.read(&w1[0], len1);

        uint16_t len2 = 0;
        is.read(reinterpret_cast<char*>(&len2), sizeof(len2));
        std::string w2(len2, '\0');
        is.read(&w2[0], len2);

        int32_t count = 0;
        is.read(reinterpret_cast<char*>(&count), sizeof(count));
        m_bigrams[{std::move(w1), std::move(w2)}] = count;
    }

    return true;
}

} // namespace AutoCorrect
