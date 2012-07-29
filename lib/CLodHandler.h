#pragma once



/*
 * CLodhandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

namespace boost
{class mutex;}
namespace NLoadHandlerHelp
{
	const int dmHelp=0, dmNoExtractingMask=1;
	//std::string P1,P2,CurDir;
	const int fCHUNK = 50000;
}

struct LodEntry {
	char filename[16];
	ui32 offset;				/* little endian */
	ui32 uncompressedSize;	/* little endian */
	ui32 unused;				/* little endian */
	ui32 size;				/* little endian */
};

static inline char readChar(const ui8 * bufor, int &i)
{
	return bufor[i++];
}

DLL_LINKAGE std::string readString(const ui8 * bufor, int &i);

enum LodFileType{
	FILE_ANY,
	FILE_TEXT,
	FILE_ANIMATION,
	FILE_MASK,
	FILE_CAMPAIGN,
	FILE_MAP,
	FILE_FONT,
	FILE_GRAPHICS,
	FILE_OTHER
};


struct Entry
{
	// Info extracted from LOD file
	std::string name,
		    realName;//for external files - case\extension may not match 
	int offset, //from beginning
		realSize, //size without compression
		size;	//and with
	LodFileType type;// file type determined by extension

	bool operator == (const Entry & comp) const
	{
		return (type==comp.type || comp.type== FILE_ANY) && name==comp.name;
	}
	
	Entry(std::string con, LodFileType TYPE): name(con), type(TYPE){};
	Entry(std::string con): name(con){};
	Entry(){};
};
namespace boost
{
template<>
struct hash<Entry> : public std::unary_function<Entry, std::size_t>
{
private:
	hash<std::string> stringHasher;
public:
	std::size_t operator()(Entry const& en) const
	{
		//do NOT improve this hash function as we need same-name hash collisions for find to work properly
		return stringHasher(en.name);
	}
};

}

class DLL_LINKAGE CLodHandler
{
	std::map<std::string, LodFileType> extMap;// to convert extensions to file type
	
	std::ifstream LOD;
	ui32 totalFiles;
	boost::mutex *mutex;
	std::string myDir; //load files from this dir instead of .lod file

	void initEntry(Entry &e, const std::string name);
	int infs2(ui8 * in, int size, int realSize, ui8*& out, int wBits=15); //zlib fast handler

public:
	//convert string to upper case and remove extension. If extension!=NULL -> it will be copied here (including dot)
	void convertName(std::string &filename, std::string *extension=NULL);

	boost::unordered_set<Entry> entries;

	CLodHandler();
	~CLodHandler();
	void init(const std::string lodFile, const std::string dirName);
	std::string getFileName(std::string lodFile, LodFileType type=FILE_ANY);
	ui8 * giveFile(std::string defName, LodFileType type=FILE_ANY, int * length=NULL); //returns pointer to the decompressed data - it must be deleted when no longer needed!
	bool haveFile(std::string name, LodFileType type=FILE_ANY);//check if file is present in lod
	std::string getTextFile(std::string name, LodFileType type=FILE_TEXT); //extracts one file
	void extractFile(const std::string FName, const std::string name, LodFileType type=FILE_ANY); //extracts a specific file

	//unpack data from memory, input data will be deleted
	static ui8 * getUnpackedData(ui8 *data, size_t inputSize, int * outputSize);
	//loads given file, decompresses and returns
	static ui8 * getUnpackedFile(const std::string & path, int * sizeOut);
};
