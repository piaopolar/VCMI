#include "StdInc.h"
#include "CSpellHandler.h"


#include "CLodHandler.h"
#include "../lib/VCMI_Lib.h"
#include "../lib/JsonNode.h"
#include <cctype>
#include "GameConstants.h"
#include "BattleHex.h"

extern CLodHandler *bitmaph;

/*
 * CSpellHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */
using namespace boost::assign;

namespace SRSLPraserHelpers
{
	static int XYToHex(int x, int y)
	{
		return x + 17 * y;
	}

	static int XYToHex(std::pair<int, int> xy)
	{
		return XYToHex(xy.first, xy.second);
	}

	static int hexToY(int battleFieldPosition)
	{
		return battleFieldPosition/17;
	}

	static int hexToX(int battleFieldPosition)
	{
		int pos = battleFieldPosition - hexToY(battleFieldPosition) * 17;
		return pos;
	}

	static std::pair<int, int> hexToPair(int battleFieldPosition)
	{
		return std::make_pair(hexToX(battleFieldPosition), hexToY(battleFieldPosition));
	}

	//moves hex by one hex in given direction
	//0 - left top, 1 - right top, 2 - right, 3 - right bottom, 4 - left bottom, 5 - left
	static std::pair<int, int> gotoDir(int x, int y, int direction)
	{
		switch(direction)
		{
		case 0: //top left
			return std::make_pair(y%2 ? x-1 : x, y-1);
		case 1: //top right
			return std::make_pair(y%2 ? x : x+1, y-1);
		case 2:  //right
			return std::make_pair(x+1, y);
		case 3: //right bottom
			return std::make_pair(y%2 ? x : x+1, y+1);
		case 4: //left bottom
			return std::make_pair(y%2 ? x-1 : x, y+1);
		case 5: //left
			return std::make_pair(x-1, y);
		default:
			throw std::runtime_error("Disaster: wrong direction in SRSLPraserHelpers::gotoDir!\n");
		}
	}

	static std::pair<int, int> gotoDir(std::pair<int, int> xy, int direction)
	{
		return gotoDir(xy.first, xy.second, direction);
	}

	static bool isGoodHex(std::pair<int, int> xy)
	{
		return xy.first >=0 && xy.first < 17 && xy.second >= 0 && xy.second < 11;
	}

	//helper fonction for std::set<ui16> CSpell::rangeInHexes(unsigned int centralHex, ui8 schoolLvl ) const
	static std::set<ui16> getInRange(unsigned int center, int low, int high)
	{
		std::set<ui16> ret;
		if(low == 0)
		{
			ret.insert(center);
		}

		std::pair<int, int> mainPointForLayer[6]; //A, B, C, D, E, F points
		for(int b=0; b<6; ++b)
			mainPointForLayer[b] = hexToPair(center);

		for(int it=1; it<=high; ++it) //it - distance to the center
		{		
			for(int b=0; b<6; ++b)
				mainPointForLayer[b] = gotoDir(mainPointForLayer[b], b);

			if(it>=low)
			{
				std::pair<int, int> curHex;

				//adding lines (A-b, B-c, C-d, etc)
				for(int v=0; v<6; ++v)
				{
					curHex = mainPointForLayer[v];
					for(int h=0; h<it; ++h)
					{
						if(isGoodHex(curHex))
							ret.insert(XYToHex(curHex));
						curHex = gotoDir(curHex, (v+2)%6);
					}
				}

			} //if(it>=low)
		}

		return ret;
	}
}
using namespace SRSLPraserHelpers;
CSpellHandler::CSpellHandler()
{
	VLC->spellh = this;
}
std::vector<BattleHex> CSpell::rangeInHexes(BattleHex centralHex, ui8 schoolLvl, ui8 side, bool *outDroppedHexes) const
{
	std::vector<BattleHex> ret;

	if(id == Spells::FIRE_WALL  ||  id == Spells::FORCE_FIELD)
	{
		//Special case - shape of obstacle depends on caster's side
		//TODO make it possible through spell_info config

		BattleHex::EDir firstStep, secondStep;
		if(side)
		{
			firstStep = BattleHex::TOP_LEFT;
			secondStep = BattleHex::TOP_RIGHT;
		}
		else
		{
			firstStep = BattleHex::TOP_RIGHT;
			secondStep = BattleHex::TOP_LEFT;
		}

		//Adds hex to the ret if it's valid. Otherwise sets output arg flag if given.
		auto addIfValid = [&](BattleHex hex)
		{
			if(hex.isValid())
				ret.push_back(hex);
			else if(outDroppedHexes)
				*outDroppedHexes = true;
		};

		ret.push_back(centralHex);
		addIfValid(centralHex.moveInDir(firstStep, false));
		if(schoolLvl >= 2) //advanced versions of fire wall / force field cotnains of 3 hexes
			addIfValid(centralHex.moveInDir(firstStep, false).moveInDir(secondStep, false));

		return ret;
	}


	std::string rng = range[schoolLvl] + ','; //copy + artificial comma for easier handling

	if(rng.size() >= 1 && rng[0] != 'X') //there is at lest one hex in range
	{
		std::string number1, number2;
		int beg, end;
		bool readingFirst = true;
		for(int it=0; it<rng.size(); ++it)
		{
			if( std::isdigit(rng[it]) ) //reading numer
			{
				if(readingFirst)
					number1 += rng[it];
				else
					number2 += rng[it];
			}
			else if(rng[it] == ',') //comma
			{
				//calculating variables
				if(readingFirst)
				{
					beg = atoi(number1.c_str());
					number1 = "";
				}
				else
				{
					end = atoi(number2.c_str());
					number2 = "";
				}
				//obtaining new hexes
				std::set<ui16> curLayer;
				if(readingFirst)
				{
					curLayer = getInRange(centralHex, beg, beg);
				}
				else
				{
					curLayer = getInRange(centralHex, beg, end);
					readingFirst = true;
				}
				//adding abtained hexes
				for(std::set<ui16>::iterator it = curLayer.begin(); it != curLayer.end(); ++it)
				{
					ret.push_back(*it);
				}

			}
			else if(rng[it] == '-') //dash
			{
				beg = atoi(number1.c_str());
				number1 = "";
				readingFirst = false;
			}
		}
	}

	//remove duplicates (TODO check if actually needed)
	range::unique(ret);
	return ret;
}

CSpell::ETargetType CSpell::getTargetType() const //TODO: parse these at game launch
{
	if(attributes.find("CREATURE_TARGET_1") != std::string::npos
		|| attributes.find("CREATURE_TARGET_2") != std::string::npos)
		return CREATURE_EXPERT_MASSIVE;

	if(attributes.find("CREATURE_TARGET") != std::string::npos)
		return CREATURE;

	if(attributes.find("OBSTACLE_TARGET") != std::string::npos)
		return OBSTACLE;
	
	return NO_TARGET;
}

bool CSpell::isPositive() const
{
	return positiveness == POSITIVE;
}

bool CSpell::isNegative() const
{
	return positiveness == NEGATIVE;
}

bool CSpell::isRisingSpell() const
{
	return vstd::contains(VLC->spellh->risingSpells, id);
}

static bool startsWithX(const std::string &s)
{
	return s.size() && s[0] == 'x';
}

bool DLL_LINKAGE isInScreenRange(const int3 &center, const int3 &pos)
{
	int3 diff = pos - center;
	if(diff.x >= -9  &&  diff.x <= 9  &&  diff.y >= -8  &&  diff.y <= 8)
		return true;
	else
		return false;
}

void CSpellHandler::loadSpells()
{
	std::string buf = bitmaph->getTextFile("SPTRAITS.TXT"), pom;
	int andame = buf.size(), i=0; //buf iterator
	for(int z=0; z<5; ++z)
		loadToIt(pom,buf,i,3);

	bool combSpells=false; //true, if we are reading combat spells
	bool creatureAbility=false; //if true, only creature can use this spell
	int ifHit = 0;
	while(i<andame)
	{
		if(spells.size()==81)
			break;
		CSpell * nsp = new CSpell; //new currently being read spell

		loadToIt(nsp->name,buf,i,4);
		if(nsp->name == std::string(""))
		{
			if(ifHit == 0)
			{
				combSpells = true;
			}
			if(ifHit == 1)
			{
				creatureAbility = true;
			}
			for(int z=0; z<3; ++z)
				loadToIt(pom,buf,i,3);
			loadToIt(nsp->name,buf,i,4);
			++ifHit;
		}

		loadToIt(nsp->abbName,buf,i,4);
		loadToIt(nsp->level,buf,i,4);
		loadToIt(pom,buf,i,4);
		nsp->earth = startsWithX(pom);
		loadToIt(pom,buf,i,4);
		nsp->water = startsWithX(pom);
		loadToIt(pom,buf,i,4);
		nsp->fire = startsWithX(pom);
		loadToIt(pom,buf,i,4);
		nsp->air = startsWithX(pom);

		nsp->costs.resize(4);
		for (int z = 0; z < 4 ; z++)
			loadToIt(nsp->costs[z],buf,i,4);
		loadToIt(nsp->power,buf,i,4);
		nsp->powers.resize(4);
		for (int z = 0; z < 4 ; z++)
			loadToIt(nsp->powers[z],buf,i,4);

		nsp->probabilities.resize(9);
		for (int z = 0; z < 9 ; z++)
			loadToIt(nsp->probabilities[z],buf,i,4);

		nsp->AIVals.resize(4);
		for (int z = 0; z < 4 ; z++)
			loadToIt(nsp->AIVals[z],buf,i,4);

		nsp->descriptions.resize(4);
		for (int z = 0; z < 4 ; z++)
		{
			loadToIt(nsp->descriptions[z],buf,i,4);
			boost::algorithm::replace_all(nsp->descriptions[z],"\"","");
		}

		loadToIt(nsp->attributes,buf,i,3);
		nsp->id = spells.size();
		nsp->combatSpell = combSpells;
		nsp->creatureAbility = creatureAbility;
		nsp->mainEffectAnim = -1;
		spells.push_back(nsp);
	}
	boost::replace_first (spells[47]->attributes, "2", ""); // disrupting ray will now affect single creature

	//loading of additional spell traits
	const JsonNode config(GameConstants::DATA_DIR + "/config/spell_info.json");

	BOOST_FOREACH(const JsonNode &spell, config["spells"].Vector())
	{
		//reading exact info
		int spellID = spell["id"].Float();

		spells[spellID]->positiveness = spell["effect"].Float();
		spells[spellID]->mainEffectAnim = spell["anim"].Float();

		spells[spellID]->range.resize(4);
		int idx = 0;
		BOOST_FOREACH(const JsonNode &range, spell["ranges"].Vector())
			spells[spellID]->range[idx++] = range.String();
	}

	spells.push_back(spells[80]); //clone Acid Breath attributes for Acid Breath damage effect
	//forgetfulness needs to get targets automatically on expert level
	boost::replace_first(spells[61]->attributes, "CREATURE_TARGET", "CREATURE_TARGET_2"); //TODO: use flags instead?

	damageSpells += 11, 13, 15, 16, 17, 18, 19, 20, 21, 22, 23, 24, 25, 26, 57, 77;
	risingSpells += 38, 39, 40;
	mindSpells += 50, 59, 60, 61, 62;
}
