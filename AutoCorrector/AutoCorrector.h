#pragma once

#include <map>
#include <string>
#include <vector>

typedef std::vector<std::string> Vector;
typedef std::map<std::string, int> Dictionary;

class CAutoCorrector
{
public:
	CAutoCorrector() = default;
	virtual ~CAutoCorrector() = default;

	std::string wordLookUp(const std::string& word);
	void load(const std::string& filename);

private:
	void known(Vector& results, Dictionary& candidates);
	void edit(const std::string & strWord, Vector &results);


private:


	Dictionary m_mapDictionary;
};

