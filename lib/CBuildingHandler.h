#pragma once


#include "../lib/ConstTransitivePtr.h"
#include "ResourceSet.h"

/*
 * CBuildingHandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

//enum EbuildingType {NEUTRAL=-1, CASTLE, RAMPART, TOWER, INFERNO, NECROPOLIS, DUNGEON, STRONGHOLD, FORTRESS, CONFLUX};
class DLL_LINKAGE CBuilding //a typical building encountered in every castle ;]
{
public:
	si32 tid, bid; //town ID and structure ID
	TResources resources;
	std::string name;
	std::string description;

	const std::string &Name() const;
	const std::string &Description() const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & tid & bid & resources & name & description;
	}
	CBuilding(int TID = -1, int BID = -1);
};

class DLL_LINKAGE CBuildingHandler
{
public:
	typedef bmap<int, ConstTransitivePtr<CBuilding> > TBuildingsMap;
	std::vector< TBuildingsMap > buildings; ///< vector by castle ID, second the building ID (in ERM-U format)
	bmap<int, std::pair<std::string,std::vector< std::vector< std::vector<int> > > > > hall; //map<castle ID, pair<hall bg name, std::vector< std::vector<building id> >[5]> - external vector is the vector of buildings in the row, internal is the list of buildings for the specific slot

	void loadBuildings(); //main loader
	~CBuildingHandler(); //d-tor
	static int campToERMU(int camp, int townType, std::set<si32> builtBuildings);

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & buildings & hall;
	}
};
