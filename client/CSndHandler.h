#pragma once

#include <iosfwd>
#include <SDL_stdinc.h>


/*
 * CSndHandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

namespace boost 
{ 
	namespace iostreams 
	{
		class mapped_file_source;
	}
}

struct MemberFile
{
	std::ifstream * ifs;
	int length;
};

// sound entry structure in catalog
struct soundEntry
{
	char filename[40];
	Uint32 offset;				/* little endian */
	Uint32 size;				/* little endian */
};

// video entry structure in catalog
struct videoEntry
{
	char filename[40];
	Uint32 offset;				/* little endian */
};

class CMediaHandler
{
protected:
	struct Entry
	{
		std::string name;
		ui32 size;
		ui32 offset;
		const char *data;
	};

	std::vector<boost::iostreams::mapped_file_source *> mfiles;
	boost::iostreams::mapped_file_source *add_file(std::string fname, bool important = true); //if not important, then we don't print warning when the file is missing

public:
	std::vector<Entry> entries;
	std::map<std::string, int> fimap; // map of file and index
	~CMediaHandler(); //d-tor
	void extract(std::string srcfile, std::string dstfile, bool caseSens=true); //saves selected file
	const char * extract (std::string srcfile, int & size); //return selecte file data, NULL if file doesn't exist
	void extract(int index, std::string dstfile); //saves selected file
	MemberFile getFile(std::string name);//nie testowane - sprawdzic
	const char * extract (int index, int & size); //return selecte file - NIE TESTOWANE
};

class CSndHandler: public CMediaHandler
{
public:
	void add_file(std::string fname, bool important = true); //if not important, then we don't print warning when the file is missing
};

class CVidHandler: public CMediaHandler
{
public:
	void add_file(std::string fname);
};
