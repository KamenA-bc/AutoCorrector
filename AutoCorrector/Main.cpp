#include <chrono>
#include <iomanip>
#include <iostream>
#include <string>
#include <vector>
#include <filesystem>
#include "AutoCorrector.h"
#include "KeyboardHookService.h"

CAutoCorrector corrector;

void RunTestCase(const std::string& wrong, const std::string& expected, const std::string& prevWord = "")
{
    const auto start = std::chrono::high_resolution_clock::now();
    const std::string actual = corrector.wordLookUp(wrong, prevWord);
    const auto end = std::chrono::high_resolution_clock::now();

    const double elapsedUs = std::chrono::duration<double, std::micro>(end - start).count();
    const bool isCorrect = (actual == expected);

    std::cout << "[" << (isCorrect ? "PASS" : "FAIL") << "] "
              << "(" << wrong << ")"
              << (!prevWord.empty() ? " [after '" + prevWord + "']" : "")
              << " == (" << expected << ") -> (" << actual << ") "
              << "[took " << std::fixed << std::setprecision(1) << elapsedUs << " us]\n";
}

void RunAllTests()
{
    std::cout << "--- 1. False-Positive Protection (Exact Words Guard) ---\n";
    RunTestCase("lot", "lot", "a");             // MUST NOT become "lost" or "not"
    RunTestCase("form", "form", "the");         // MUST NOT become "from"
    RunTestCase("heat", "heat", "the");         // MUST NOT become "that"
    RunTestCase("word", "word");                // known unigram
    RunTestCase("myVariable", "myVariable");    // camelCase identifier

    std::cout << "\n--- 2. Strict Distance Tiering (Tier 1 vs Tier 2) ---\n";
    RunTestCase("tehre", "there", "and");       // dist 1 "there" MUST beat high-frequency dist 2 "the"
    RunTestCase("peotry", "poetry");            // transposition

    std::cout << "\n--- 3. Modern Contraction Restoration ---\n";
    RunTestCase("dont", "don't");               // contraction
    RunTestCase("doesnt", "doesn't", "he");     // contraction with context
    RunTestCase("theyre", "they're");           // contraction

    std::cout << "\n--- 4. Compound Split Word Detection ---\n";
    RunTestCase("abunch", "a bunch", "have");   // split merged words

    std::cout << "\n--- 5. Standard Edit Distance Benchmarks ---\n";
    RunTestCase("speling", "spelling");         // single insertion
    RunTestCase("korrectud", "corrected");      // two substitutions
    RunTestCase("bycycle", "bicycle");          // single substitution
    RunTestCase("inconvient", "inconvenient");  // two insertions
    RunTestCase("arrainged", "arranged");       // single deletion
    RunTestCase("peotryy", "poetry");           // transpose + delete

    std::cout << "\n--- 6. Top 3 Suggestions Verification ---\n";
    const std::vector<std::string> top3 = corrector.getTopSuggestions("tehre", "and", 3);
    std::cout << "Top 3 suggestions for 'tehre' after 'and':\n";
    for (size_t i = 0; i < top3.size(); ++i)
    {
        std::cout << "  " << (i + 1) << ". " << top3[i] << '\n';
    }

    std::cout << "\n--- 7. Adaptive Personal Learner Verification ---\n";
    std::cout << "Simulating user typo acceptance: 'pythn' -> 'python'...\n";
    corrector.recordUserAcceptedCorrection("pythn", "python");
    const auto topPy = corrector.getTopSuggestions("pythn", "", 3);
    std::cout << "Top suggestions for 'pythn' after learning: "
              << (!topPy.empty() ? topPy.front() : "none") << '\n';

    std::cout << "---------------------------------------------------------\n\n";
}

void LoadOrBuildDictionary(bool forceRebuild = false)
{
    const std::string binFile = "dictionary.bin";
    const std::string textFile = "big.txt";

    bool loaded = false;

    // Load binary cache if exists and not forced rebuild
    if (!forceRebuild && std::filesystem::exists(binFile) && std::filesystem::exists(textFile))
    {
        const auto binTime = std::filesystem::last_write_time(binFile);
        const auto textTime = std::filesystem::last_write_time(textFile);

        if (binTime >= textTime)
        {
            std::cout << "Loading cached binary dictionary '" << binFile << "'...\n";
            const auto start = std::chrono::high_resolution_clock::now();
            if (corrector.loadBinary(binFile))
            {
                const auto end = std::chrono::high_resolution_clock::now();
                const double ms = std::chrono::duration<double, std::milli>(end - start).count();
                std::cout << "Binary dictionary loaded in " << std::fixed << std::setprecision(2)
                          << ms << " ms (instant startup)!\n\n";
                loaded = true;
            }
        }
    }

    if (!loaded)
    {
        std::cout << "Building dictionary and language model from '" << textFile << "'...\n";
        const auto start = std::chrono::high_resolution_clock::now();
        corrector.load(textFile);
        const auto end = std::chrono::high_resolution_clock::now();
        const double ms = std::chrono::duration<double, std::milli>(end - start).count();
        std::cout << "Corpus indexed in " << std::fixed << std::setprecision(2) << ms << " ms.\n";

        std::cout << "Saving binary cache to '" << binFile << "' for future instant startups...\n";
        corrector.saveBinary(binFile);
        std::cout << "Binary cache saved successfully.\n\n";
    }

    // Load user dictionary if available
    const std::string userFile = "user_words.txt";
    if (std::filesystem::exists(userFile))
    {
        std::cout << "Loading personal user dictionary '" << userFile << "'...\n";
        corrector.loadUserDictionary(userFile);
    }
}

int main(int argc, char* argv[])
{
    std::cout << "=====================================================\n"
              << "       AutoCorrector 3.0 (Smart Overlay & Learner)   \n"
              << "=====================================================\n";

    bool forceRebuild = false;
    std::string mode = "interactive";

    for (int i = 1; i < argc; ++i)
    {
        const std::string arg = argv[i];
        if (arg == "--daemon" || arg == "-d" || arg == "--overlay" || arg == "-o")
        {
            mode = "overlay";
        }
        else if (arg == "--auto" || arg == "-a")
        {
            mode = "auto";
        }
        else if (arg == "--bench" || arg == "-b")
        {
            mode = "bench";
        }
        else if (arg == "--rebuild" || arg == "-r")
        {
            forceRebuild = true;
        }
    }

    LoadOrBuildDictionary(forceRebuild);

    if (mode == "bench")
    {
        RunAllTests();
        return 0;
    }

    if (mode == "overlay" || mode == "auto")
    {
        const auto serviceMode = (mode == "overlay")
                                     ? AutoCorrect::ServiceMode::Overlay
                                     : AutoCorrect::ServiceMode::AutoReplace;

        AutoCorrect::KeyboardHookService service(corrector, serviceMode);

        service.setLogCallback([](std::string_view from, std::string_view to, bool isUndo) {
            if (isUndo)
            {
                std::cout << "[UNDO] Reverted '" << from << "' back to '" << to << "' (whitelisted)\n";
            }
            else
            {
                std::cout << "[NOTIFY] '" << from << "' -> '" << to << "'\n";
            }
        });

        service.run();
        return 0;
    }

    // Default: Run tests and provide interactive console prompt
    RunAllTests();

    std::string request;
    std::string prevWord;

    std::cout << "Interactive Mode:\n"
              << "- Type a misspelled word to get correction.\n"
              << "- Type two words ('prev curr') to test bigram contextual ranking.\n"
              << "- Type 'quit' to exit.\n\n"
              << "Commands to start the Background Windows Service:\n"
              << "  .\\AutoCorrector.exe --overlay   (Squiggly underline + Top 3 suggestions on hover / Alt+1/2/3)\n"
              << "  .\\AutoCorrector.exe --auto      (Direct auto-replacement upon Space/delimiter)\n\n";

    while (std::cout << "> " && std::cin >> request && request != "quit")
    {
        const auto start = std::chrono::high_resolution_clock::now();
        const auto suggestions = corrector.getTopSuggestions(request, prevWord, 3);
        const auto end = std::chrono::high_resolution_clock::now();
        const double elapsedUs = std::chrono::duration<double, std::micro>(end - start).count();

        if (!suggestions.empty())
        {
            std::cout << "Top suggestions [lookup: " << std::fixed << std::setprecision(1)
                      << elapsedUs << " us]:\n";
            for (size_t i = 0; i < suggestions.size(); ++i)
            {
                std::cout << "  " << (i + 1) << ". " << suggestions[i]
                          << (i == 0 ? " (best)" : "") << '\n';
            }
            std::cout << '\n';
            prevWord = suggestions.front();
        }
        else
        {
            std::cout << "No correction suggestion (word is unknown or already valid) [lookup: "
                      << std::fixed << std::setprecision(1) << elapsedUs << " us]\n\n";
            prevWord = request;
        }
    }

    return 0;
}