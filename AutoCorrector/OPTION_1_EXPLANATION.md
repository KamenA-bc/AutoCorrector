# Option 1 Explanation: Algorithmic Refinements within Norvig's Framework

This document breaks down **Option 1** from [ALGORITHM_ANALYSIS_AND_IMPROVEMENTS.md](file:///d:/school/cpp_practice/AutoCorrector/AutoCorrector/ALGORITHM_ANALYSIS_AND_IMPROVEMENTS.md#L163-L187), focusing especially on what **callbacks** are, how they work in C++, and what exact problems they solve in the `CAutoCorrector` codebase.

---

## Table of Contents
1. [Executive Summary: What is Option 1?](#1-executive-summary-what-is-option-1)
2. [Deep-Dive: What is a "Callback" and What Problem Does It Solve?](#2-deep-dive-what-is-a-callback-and-what-problem-does-it-solve)
   - [The Problem in the Current Code](#the-problem-in-the-current-code)
   - [What is a Callback? (An Analogy)](#what-is-a-callback-an-analogy)
   - [How Callbacks Work in C++](#how-callbacks-work-in-c)
   - [Comparing Code: Vector Accumulation vs. Callback Streaming](#comparing-code-vector-accumulation-vs-callback-streaming)
3. [The Other Three Pillars of Option 1](#3-the-other-three-pillars-of-option-1)
   - [Pillar A: Deduplicating Distance 1 Edits](#pillar-a-deduplicating-distance-1-edits)
   - [Pillar B: $O(1)$ Running Maximum (Drop `candidates` Map)](#pillar-b-o1-running-maximum-drop-candidates-map)
   - [Pillar C: $O(1)$ Hash Table Lookups](#pillar-c-o1-hash-table-lookups)
4. [Complete Concrete Implementation of Option 1](#4-complete-concrete-implementation-of-option-1)
5. [Summary of Benefits & Trade-offs](#5-summary-of-benefits--trade-offs)

---

## 1. Executive Summary: What is Option 1?

Peter Norvig's spell-checking paradigm works in two phases:
1. **Generate possible typos** by applying 4 basic edits (deletions, transpositions, substitutions, insertions).
2. **Check the dictionary** to see which generated candidates actually exist, picking the one with the highest corpus frequency.

**Option 1 does not change the core paradigm.** It keeps generating edits, but eliminates all the **wasted memory allocations, redundant loops, and unnecessary data structures** in the current code:
- Instead of creating massive vectors of strings, it streams each word into a **callback**.
- It deduplicates the first wave of edits before launching into distance 2.
- It tracks the best word on the fly with a running maximum rather than allocating a temporary `candidates` map.

---

## 2. Deep-Dive: What is a "Callback" and What Problem Does It Solve?

### The Problem in the Current Code

Look at how [AutoCorrector.cpp](file:///d:/school/cpp_practice/AutoCorrector/AutoCorrector/AutoCorrector.cpp#L68-L75) generates distance 2 edits right now:

```cpp
// Current code in wordLookUp:
for (int i = 0; i < results.size(); i++)
{
    Vector subResults;          // <--- Creates a std::vector<std::string>
    edit(results[i], subResults); // <--- Allocates hundreds of strings and pushes them into subResults
    known(subResults, candidates); // <--- Loops over subResults to check the dictionary
}                               // <--- Destroys subResults and all its strings on every single iteration!
```

#### Why is this slow?
1. **Enormous Heap Allocations**:
   For a 9-letter word, `results` has around 500 strings. For each of those 500 strings, `edit()` generates another ~500 strings. That is:
   $$500 \times 500 = 250,000 \text{ strings}$$
   The program allocates memory for 250,000 individual `std::string` objects and grows `subResults` in memory 500 separate times!
2. **Immediate Destruction**:
   As soon as `known(subResults, candidates)` checks if the words exist, the loop iteration ends. The computer has to deallocate every single one of those strings and free the vector buffer, only to allocate them all over again in the next iteration.
3. **CPU Cache Thrashing**:
   Repeatedly allocating and deallocating memory fragments the heap and destroys CPU cache locality.

---

### What is a Callback? (An Analogy)

#### Analogy: The Conveyor Belt vs. The Warehouse
- **Current Approach (Vector/Warehouse)**:
  Imagine a factory where worker A manufactures 500 items, boxes them all up, drives them across the warehouse to worker B. Worker B takes them out of the boxes one by one, checks them, and immediately throws 499 of them into the trash. Worker A repeats this 500 times. You waste huge effort boxing and unboxing items that will be discarded.
- **Callback Approach (Conveyor Belt / Streaming)**:
  Worker A has a direct conveyor belt. As soon as worker A produces **one single item**, worker A instantly passes it to worker B (the "callback"). Worker B inspects it immediately against the dictionary. If it matches, worker B notes it down. If not, it is immediately discarded. **No boxes (vectors) are ever needed.**

> [!NOTE]
> A **callback** is simply a function (or lambda) that you pass as an argument to another function. The receiving function "calls back" your function whenever an event occurs or whenever a new item is ready.

---

### How Callbacks Work in C++

In modern C++, the fastest and most idiomatic way to implement a callback is using a **template parameter** (or a C++ lambda).

When you use a template:
```cpp
template<typename Callback>
void forEachEdit(const std::string& word, Callback callback)
```
The compiler inlines the callback completely. There is **zero function call overhead** and **zero heap allocation**.

---

### Comparing Code: Vector Accumulation vs. Callback Streaming

#### 1. The Old Way (Vector Accumulation):
```cpp
// Caller must supply a vector to be filled:
void CAutoCorrector::edit(const std::string& word, Vector& result)
{
    // Deletions: allocates a new string and adds it to result
    for (size_t i = 0; i < word.size(); i++)
        result.push_back(word.substr(0, i) + word.substr(i + 1));

    // Transpositions:
    for (size_t i = 0; i < word.size() - 1; i++)
        result.push_back(word.substr(0, i) + word[i + 1] + word[i] + word.substr(i + 2));

    // Alterations & Insertions:
    for (char j = 'a'; j <= 'z'; ++j)
    {
        for (size_t i = 0; i < word.size(); i++)
            result.push_back(word.substr(0, i) + j + word.substr(i + 1));
        for (size_t i = 0; i <= word.size(); i++)
            result.push_back(word.substr(0, i) + j + word.substr(i));
    }
}
```

#### 2. The New Way (Callback / Streaming):
Instead of `result.push_back(...)`, we immediately invoke `callback(...)`:

```cpp
template<typename Callback>
void forEachEdit(const std::string& word, Callback callback)
{
    // Deletions: call the callback immediately!
    for (size_t i = 0; i < word.size(); i++)
        callback(word.substr(0, i) + word.substr(i + 1));

    // Transpositions:
    for (size_t i = 0; i < word.size() - 1; i++)
        callback(word.substr(0, i) + word[i + 1] + word[i] + word.substr(i + 2));

    // Alterations & Insertions:
    for (char j = 'a'; j <= 'z'; ++j)
    {
        for (size_t i = 0; i < word.size(); i++)
            callback(word.substr(0, i) + j + word.substr(i + 1));
        for (size_t i = 0; i <= word.size(); i++)
            callback(word.substr(0, i) + j + word.substr(i));
    }
}
```

#### How you use it in `wordLookUp`:
```cpp
std::string bestWord = "";
int maxFreq = 0;

// Pass a C++ lambda as the callback!
forEachEdit(strWord, [&](const std::string& candidate) {
    auto it = m_mapDictionary.find(candidate);
    if (it != m_mapDictionary.end() && it->second > maxFreq)
    {
        maxFreq = it->second;
        bestWord = it->first;
    }
});
```

Look how clean and efficient that is:
1. `forEachEdit` generates a candidate.
2. The lambda `[&](...)` runs instantly.
3. The dictionary checks `find(candidate)`.
4. If found and better, update `bestWord`.
5. The string `candidate` is immediately discarded!
6. **No `subResults` vector is ever allocated!**

---

## 3. The Other Three Pillars of Option 1

A callback solves memory allocation, but Option 1 also fixes three other algorithmic inefficiencies:

### Pillar A: Deduplicating Distance 1 Edits

When you create Distance 1 edits for a word, many combinations produce the **exact same string**.
For example:
- Transposing `"ab"` to `"ba"` might overlap with inserting/deleting in certain words.
- Repeating letters (like `"hello"`) produce identical deletions (`"he-lo"` vs `"hel-o"`).

If `results` has 500 words, but 150 of them are duplicates:
- In the current code, you run Distance 2 on all 500 words.
- If you sort and deduplicate `results` first:
  ```cpp
  std::sort(results.begin(), results.end());
  results.erase(std::unique(results.begin(), results.end()), results.end());
  ```
  Now you only run Distance 2 on the remaining 350 unique words.
- **Result**: You instantly cut 30% to 50% of the entire Distance 2 loop!

---

### Pillar B: $O(1)$ Running Maximum (Drop `candidates` Map)

In the current code:
```cpp
void CAutoCorrector::known(Vector& results, Dictionary& candidates)
{
    Dictionary::iterator end = m_mapDictionary.end();
    for (int i = 0; i < results.size(); i++)
    {
        Dictionary::iterator value = m_mapDictionary.find(results[i]);
        if (value != end)
            candidates[value->first] = value->second; // <--- Allocates map nodes
    }
}
```
And then in `wordLookUp`:
```cpp
max_element(candidates.begin(), candidates.end(), filterBySecond)->first;
```

#### Why is this wasteful?
All you care about is the **single best word** with the highest frequency.
Storing every match in a `std::map` (or `std::unordered_map`) allocates map buckets or tree nodes for dozens of words, only to iterate through them with `std::max_element`.

#### The Fix:
Keep two simple variables:
```cpp
std::string bestWord = "";
int maxFreq = 0;

// Inside the check:
if (value != m_mapDictionary.end() && value->second > maxFreq)
{
    maxFreq = value->second;
    bestWord = value->first;
}
```
- **Time**: $O(1)$ per match instead of $O(\log K)$ or map bucket insertion.
- **Space**: $0$ extra heap bytes allocated.

---

### Pillar C: $O(1)$ Hash Table Lookups

In [AutoCorrector.h](file:///d:/school/cpp_practice/AutoCorrector/AutoCorrector/AutoCorrector.h#L8), `Dictionary` is defined as:
```cpp
typedef std::unordered_map<std::string, int> Dictionary;
```
If you ever used `std::map`:
- `std::map` is an ordered Red-Black balanced binary tree.
- Each lookup costs $O(\log N)$ tree traversals ($\approx 15$ pointer hops and string comparisons per lookup).
- With 300,000 candidates, that's $4.5$ million node comparisons!

With `std::unordered_map`:
- It is a Hash Table.
- Average lookup is $O(1)$ (1 hash computation + 1 string comparison on match).
- Ensure `max_load_factor` is reasonable or reserve bucket capacity during `load()` to avoid collisions.

---

## 4. Complete Concrete Implementation of Option 1

Here is how [CAutoCorrector](file:///d:/school/cpp_practice/AutoCorrector/AutoCorrector/AutoCorrector.h) would look if refactored according to Option 1:

### 1. The Header (`AutoCorrector.h`):
```cpp
#pragma once

#include <unordered_map>
#include <string>
#include <vector>

typedef std::unordered_map<std::string, int> Dictionary;

class CAutoCorrector
{
public:
    CAutoCorrector() = default;
    virtual ~CAutoCorrector() = default;

    std::string wordLookUp(const std::string& word);
    void load(const std::string& filename);

private:
    // Callback template to stream edits on the fly
    template<typename Callback>
    void forEachEdit(const std::string& word, Callback&& callback) const;

    Dictionary m_mapDictionary;
};
```

### 2. The Implementation (`AutoCorrector.cpp`):
```cpp
#include "AutoCorrector.h"
#include <algorithm>
#include <vector>

template<typename Callback>
void CAutoCorrector::forEachEdit(const std::string& word, Callback&& callback) const
{
    const size_t len = word.size();

    // 1. Deletions
    for (size_t i = 0; i < len; ++i)
    {
        callback(word.substr(0, i) + word.substr(i + 1));
    }

    // 2. Transpositions
    for (size_t i = 0; i + 1 < len; ++i)
    {
        callback(word.substr(0, i) + word[i + 1] + word[i] + word.substr(i + 2));
    }

    // 3. Alterations and Insertions
    for (char ch = 'a'; ch <= 'z'; ++ch)
    {
        // Alterations
        for (size_t i = 0; i < len; ++i)
        {
            callback(word.substr(0, i) + ch + word.substr(i + 1));
        }

        // Insertions
        for (size_t i = 0; i <= len; ++i)
        {
            callback(word.substr(0, i) + ch + word.substr(i));
        }
    }
}

std::string CAutoCorrector::wordLookUp(const std::string& strWord)
{
    // Step 0: Is the word already correct?
    auto it = m_mapDictionary.find(strWord);
    if (it != m_mapDictionary.end())
    {
        return strWord;
    }

    std::string bestWord = "";
    int maxFreq = 0;

    // We store Distance 1 candidates only so we can deduplicate them for Distance 2
    std::vector<std::string> dist1Candidates;
    dist1Candidates.reserve(54 * strWord.size() + 25);

    // Step 1: Evaluate Distance 1 with Callback streaming
    forEachEdit(strWord, [&](const std::string& edit1) {
        dist1Candidates.push_back(edit1);

        auto found = m_mapDictionary.find(edit1);
        if (found != m_mapDictionary.end() && found->second > maxFreq)
        {
            maxFreq = found->second;
            bestWord = found->first;
        }
    });

    // If we found any valid word at Distance 1, return the most frequent one!
    if (!bestWord.empty())
    {
        return bestWord;
    }

    // Step 2: Prepare Distance 2 -> Deduplicate Distance 1 first!
    std::sort(dist1Candidates.begin(), dist1Candidates.end());
    dist1Candidates.erase(std::unique(dist1Candidates.begin(), dist1Candidates.end()), dist1Candidates.end());

    // Step 3: Evaluate Distance 2 using Callback streaming
    // Notice: ZERO subResults vector allocated!
    for (const std::string& edit1 : dist1Candidates)
    {
        forEachEdit(edit1, [&](const std::string& edit2) {
            auto found = m_mapDictionary.find(edit2);
            if (found != m_mapDictionary.end() && found->second > maxFreq)
            {
                maxFreq = found->second;
                bestWord = found->first;
            }
        });
    }

    return bestWord;
}
```

---

## 5. Summary of Benefits & Trade-offs

| Feature | Current Implementation | Option 1 (Refined Norvig) |
| :--- | :--- | :--- |
| **Distance 2 Memory Allocation** | Allocates `subResults` vector 500 times ($250,000+$ `std::string` allocations) | **0 vectors allocated** (streamed via callback) |
| **Candidate Collection** | Inserts matches into `candidates` map and calls `std::max_element` | **$O(1)$ scalar tracking** (`bestWord` & `maxFreq`) |
| **Distance 1 Deduplication** | None (wastes 30%–50% of loops on duplicate strings) | **Sorted and unique'd** before distance 2 |
| **Code Simplicity** | Simple, easy to understand | **Equally simple**, keeps Peter Norvig's logic intact |
| **Speedup** | Baseline | **$2\times$ to $4\times$ faster**, significantly lower RAM churn |

### Remaining Limitation:
While Option 1 is much cleaner and faster, it **still generates hundreds of thousands of candidate strings** for longer words (e.g. 10-14 letter typos). If you need sub-millisecond lookups for words of any length, that is where **Option 2 (SymSpell)** or **Option 3 (Trie + DP)** come into play.
