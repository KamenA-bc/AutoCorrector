#include "AdaptiveLearner.h"
#include <fstream>
#include <iostream>
#include <sstream>
#include <algorithm>

namespace AutoCorrect
{

AdaptiveLearner::AdaptiveLearner(std::string profilePath)
    : m_profilePath(std::move(profilePath))
{
    loadProfile();
}

AdaptiveLearner::~AdaptiveLearner()
{
    if (m_dirty)
    {
        saveProfile();
    }
}

void AdaptiveLearner::recordWordUsage(std::string_view word)
{
    if (word.empty()) return;
    std::string w(word);
    m_wordFrequencies[w]++;
    m_dirty = true;
}

void AdaptiveLearner::recordCorrectionAccepted(std::string_view typo, std::string_view correction)
{
    if (typo.empty() || correction.empty()) return;
    m_acceptedCorrections[{std::string(typo), std::string(correction)}]++;
    recordWordUsage(correction);
    m_dirty = true;
}

void AdaptiveLearner::recordCorrectionRejected(std::string_view typo, std::string_view rejectedCorrection)
{
    if (typo.empty() || rejectedCorrection.empty()) return;
    m_rejectedCorrections.insert({std::string(typo), std::string(rejectedCorrection)});
    // Also whitelist the original typo as an intentional word
    recordWordUsage(typo);
    m_dirty = true;
}

bool AdaptiveLearner::isRejected(std::string_view typo, std::string_view candidate) const
{
    return m_rejectedCorrections.contains({std::string(typo), std::string(candidate)});
}

int AdaptiveLearner::getPersonalWordCount(std::string_view word) const
{
    auto it = m_wordFrequencies.find(std::string(word));
    return (it != m_wordFrequencies.end()) ? it->second : 0;
}

int AdaptiveLearner::getPersonalCorrectionCount(std::string_view typo, std::string_view candidate) const
{
    auto it = m_acceptedCorrections.find({std::string(typo), std::string(candidate)});
    return (it != m_acceptedCorrections.end()) ? it->second : 0;
}

double AdaptiveLearner::getPersonalScoreBoost(std::string_view typo, std::string_view candidate) const
{
    if (isRejected(typo, candidate))
    {
        return -999.0; // Rejected by user: never suggest!
    }

    double boost = 0.0;

    // Boost if user frequently accepts this correction for this specific typo
    const int corrCount = getPersonalCorrectionCount(typo, candidate);
    if (corrCount > 0)
    {
        boost += 2.0 * static_cast<double>(corrCount);
    }

    // Boost if the candidate word is part of the user's personal active vocabulary
    const int wordCount = getPersonalWordCount(candidate);
    if (wordCount > 0)
    {
        boost += 0.5 * std::min(static_cast<double>(wordCount), 10.0);
    }

    return boost;
}

bool AdaptiveLearner::saveProfile() const
{
    std::ofstream os(m_profilePath);
    if (!os.is_open()) return false;

    os << "{\n";

    // 1. Words
    os << "  \"words\": {\n";
    size_t i = 0;
    for (const auto& [word, count] : m_wordFrequencies)
    {
        os << "    \"" << word << "\": " << count;
        if (++i < m_wordFrequencies.size()) os << ",";
        os << "\n";
    }
    os << "  },\n";

    // 2. Accepted corrections
    os << "  \"corrections\": [\n";
    i = 0;
    for (const auto& [pair, count] : m_acceptedCorrections)
    {
        os << "    {\"typo\": \"" << pair.first << "\", \"correct\": \"" << pair.second << "\", \"count\": " << count << "}";
        if (++i < m_acceptedCorrections.size()) os << ",";
        os << "\n";
    }
    os << "  ],\n";

    // 3. Rejected corrections
    os << "  \"rejections\": [\n";
    i = 0;
    for (const auto& pair : m_rejectedCorrections)
    {
        os << "    {\"typo\": \"" << pair.first << "\", \"rejected\": \"" << pair.second << "\"}";
        if (++i < m_rejectedCorrections.size()) os << ",";
        os << "\n";
    }
    os << "  ]\n";

    os << "}\n";
    return true;
}

bool AdaptiveLearner::loadProfile()
{
    std::ifstream is(m_profilePath);
    if (!is.is_open()) return false;

    std::string line;
    std::string section;

    while (std::getline(is, line))
    {
        // Simple and robust line-oriented JSON parser
        if (line.find("\"words\":") != std::string::npos)
        {
            section = "words";
            continue;
        }
        if (line.find("\"corrections\":") != std::string::npos)
        {
            section = "corrections";
            continue;
        }
        if (line.find("\"rejections\":") != std::string::npos)
        {
            section = "rejections";
            continue;
        }

        if (section == "words")
        {
            auto firstQuote = line.find('\"');
            if (firstQuote != std::string::npos)
            {
                auto secondQuote = line.find('\"', firstQuote + 1);
                auto colon = line.find(':', secondQuote);
                if (secondQuote != std::string::npos && colon != std::string::npos)
                {
                    std::string word = line.substr(firstQuote + 1, secondQuote - firstQuote - 1);
                    std::string countStr = line.substr(colon + 1);
                    // trim comma
                    auto comma = countStr.find(',');
                    if (comma != std::string::npos) countStr = countStr.substr(0, comma);
                    try {
                        int count = std::stoi(countStr);
                        m_wordFrequencies[word] = count;
                    } catch (...) {}
                }
            }
        }
        else if (section == "corrections")
        {
            auto typoPos = line.find("\"typo\": \"");
            auto corrPos = line.find("\"correct\": \"");
            auto countPos = line.find("\"count\": ");

            if (typoPos != std::string::npos && corrPos != std::string::npos)
            {
                auto typoEnd = line.find('\"', typoPos + 9);
                auto corrEnd = line.find('\"', corrPos + 12);
                if (typoEnd != std::string::npos && corrEnd != std::string::npos)
                {
                    std::string typo = line.substr(typoPos + 9, typoEnd - (typoPos + 9));
                    std::string correct = line.substr(corrPos + 12, corrEnd - (corrPos + 12));
                    int count = 1;
                    if (countPos != std::string::npos)
                    {
                        try {
                            count = std::stoi(line.substr(countPos + 9));
                        } catch (...) {}
                    }
                    m_acceptedCorrections[{typo, correct}] = count;
                }
            }
        }
        else if (section == "rejections")
        {
            auto typoPos = line.find("\"typo\": \"");
            auto rejPos = line.find("\"rejected\": \"");

            if (typoPos != std::string::npos && rejPos != std::string::npos)
            {
                auto typoEnd = line.find('\"', typoPos + 9);
                auto rejEnd = line.find('\"', rejPos + 13);
                if (typoEnd != std::string::npos && rejEnd != std::string::npos)
                {
                    std::string typo = line.substr(typoPos + 9, typoEnd - (typoPos + 9));
                    std::string rejected = line.substr(rejPos + 13, rejEnd - (rejPos + 13));
                    m_rejectedCorrections.insert({typo, rejected});
                }
            }
        }
    }

    m_dirty = false;
    return true;
}

} // namespace AutoCorrect
