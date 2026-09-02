#include "AutoCorrector.h"
#include <algorithm>
#include <iostream>
#include <fstream>

using namespace std;

bool filterBySecond(const std::pair<std::string, int>& left,const std::pair<std::string, int>& right)
{
	return left.second < right.second;
}

char filterNonAlphabetic(char& letter)
{
	if (letter < 0)
		return '-';
	if (isalpha(letter))
		return tolower(letter);
	return '-';
}


void CAutoCorrector::load(const std::string& filename)
{
	ifstream file(filename.c_str(), ios_base::binary | ios_base::in);

	file.seekg(0, ios_base::end);
	std::streampos length = file.tellg();
	file.seekg(0, ios_base::beg);

	string data(static_cast<std::size_t>(length), '\0');

	file.read(&data[0], length);

	transform(data.begin(), data.end(), data.begin(), filterNonAlphabetic);

	for (string::size_type i = 0; i != string::npos;)
	{
		const string::size_type firstNonFiltered = data.find_first_not_of('-', i + 1);
		if (firstNonFiltered == string::npos)
			break;

		const string::size_type end = data.find('-', firstNonFiltered);
		m_mapDictionary[data.substr(firstNonFiltered, end - firstNonFiltered)]++;

		i = end;
	}
}

std::string CAutoCorrector::wordLookUp(const std::string& strWord)
{
	Vector results;
	Dictionary candidates;

	if(m_mapDictionary.find(strWord) != m_mapDictionary.end()) 
	{
		return strWord;
	}

	edit(strWord, results);
	known(results, candidates);

	if (candidates.size() > 0)
	{
		return max_element(candidates.begin(), candidates.end(), filterBySecond)->first;
	}

	for (int i = 0; i < results.size(); i++)
	{
		Vector subResults;

		edit(results[i], subResults);
		known(subResults, candidates);
	}

	if (candidates.size() > 0)
	{
		return max_element(candidates.begin(), candidates.end(), filterBySecond)->first;
	}

	return "";
}

void CAutoCorrector::edit(const std::string& word, Vector& result)
{
	for (string::size_type i = 0; i < word.size(); i++)    result.push_back(word.substr(0, i) + word.substr(i + 1)); //deletions
	for (string::size_type i = 0; i < word.size() - 1; i++) result.push_back(word.substr(0, i) + word[i + 1] + word[i] + word.substr(i + 2)); //transposition

	for (char j = 'a'; j <= 'z'; ++j)
	{
		for (string::size_type i = 0; i < word.size(); i++)    result.push_back(word.substr(0, i) + j + word.substr(i + 1)); //alterations
		for (string::size_type i = 0; i < word.size() + 1; i++) result.push_back(word.substr(0, i) + j + word.substr(i)); //insertion
	}
}

void CAutoCorrector::known(Vector& results, Dictionary& candidates)
{
	Dictionary::iterator end = m_mapDictionary.end();
	for (int i = 0; i < results.size(); i++)
	{
		Dictionary::iterator value = m_mapDictionary.find(results[i]);

		if (value != end)
		{
			candidates[value->first] = value->second;
		}
	}
}