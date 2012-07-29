#include "StdInc.h"
#include "CCampaignHandler.h"

#include "CLodHandler.h"
#include "../lib/VCMI_Lib.h"
#include "../lib/vcmi_endian.h"
#include "CGeneralTextHandler.h"
#include "StartInfo.h"
#include "CArtHandler.h" //for hero crossover
#include "CObjectHandler.h" //for hero crossover
#include "CHeroHandler.h"

namespace fs = boost::filesystem;

/*
 * CCampaignHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */


std::vector<CCampaignHeader> CCampaignHandler::getCampaignHeaders(GetMode mode)
{
	std::vector<CCampaignHeader> ret;

	std::string dirname = GameConstants::DATA_DIR + "/Maps";
	std::string ext = ".H3C";

	if(!boost::filesystem::exists(dirname))
	{
		tlog1 << "Cannot find " << dirname << " directory!\n";
	}

	if (mode == Custom || mode == ALL) //add custom campaigns
	{
		fs::path tie(dirname);
		fs::directory_iterator end_iter;
		for ( fs::directory_iterator file (tie); file!=end_iter; ++file )
		{
			if(fs::is_regular_file(file->status())
				&& boost::ends_with(file->path().filename().string(), ext))
			{
				ret.push_back( getHeader( file->path().string(), false ) );
			}
		}
	}
	if (mode == ALL) //add all lod campaigns
	{
		BOOST_FOREACH(Entry e, bitmaph->entries)
		{
			if( e.type == FILE_CAMPAIGN )
			{
				ret.push_back( getHeader(e.name, true) );
			}
		}
	}


	return ret;
}

CCampaignHeader CCampaignHandler::getHeader( const std::string & name, bool fromLod )
{
	int realSize;
	ui8 * cmpgn = getFile(name, fromLod, realSize);

	int it = 0;//iterator for reading
	CCampaignHeader ret = readHeaderFromMemory(cmpgn, it);
	ret.filename = name;
	ret.loadFromLod = fromLod;

	delete [] cmpgn;

	return ret;
}

CCampaign * CCampaignHandler::getCampaign( const std::string & name, bool fromLod )
{
	CCampaign * ret = new CCampaign();

	int realSize;
	ui8 * cmpgn = getFile(name, fromLod, realSize);

	int it = 0; //iterator for reading
	ret->header = readHeaderFromMemory(cmpgn, it);
	ret->header.filename = name;
	ret->header.loadFromLod = fromLod;

	int howManyScenarios = VLC->generaltexth->campaignRegionNames[ret->header.mapVersion].size();
	for(int g=0; g<howManyScenarios; ++g)
	{
		CCampaignScenario sc = readScenarioFromMemory(cmpgn, it, ret->header.version, ret->header.mapVersion);
		ret->scenarios.push_back(sc);
	}

	std::vector<ui32> h3mStarts = locateH3mStarts(cmpgn, it, realSize);

	assert(h3mStarts.size() <= howManyScenarios);
	//it looks like we can have more scenarios than we should..
	if(h3mStarts.size() > howManyScenarios)
	{
		tlog1<<"Our heuristic for h3m start points gave wrong results for campaign " << name <<std::endl;
		tlog1<<"Please send this campaign to VCMI Project team to help us fix this problem" << std::endl;
		delete [] cmpgn;
		assert(0);
		return NULL;
	}

	int scenarioID = 0;

	for (int g=0; g<h3mStarts.size(); ++g)
	{
		while(!ret->scenarios[scenarioID].isNotVoid()) //skip void scenarios
		{
			scenarioID++;
		}
		//set map piece appropriately
		if(g == h3mStarts.size() - 1)
		{
			ret->mapPieces[scenarioID] = std::string( cmpgn + h3mStarts[g], cmpgn + realSize );
		}
		else
		{
			ret->mapPieces[scenarioID] = std::string( cmpgn + h3mStarts[g], cmpgn + h3mStarts[g+1] );
		}
		scenarioID++;
	}

	delete [] cmpgn;

	return ret;
}

CCampaignHeader CCampaignHandler::readHeaderFromMemory( const ui8 *buffer, int & outIt )
{
	CCampaignHeader ret;
	ret.version = read_le_u32(buffer + outIt); outIt+=4;
	ret.mapVersion = buffer[outIt++]; //1 byte only
	ret.mapVersion -= 1; //change range of it from [1, 20] to [0, 19]
	ret.name = readString(buffer, outIt);
	ret.description = readString(buffer, outIt);
	if (ret.version > CampaignVersion::RoE)
		ret.difficultyChoosenByPlayer = readChar(buffer, outIt);
	else
		ret.difficultyChoosenByPlayer = 0;
	ret.music = readChar(buffer, outIt);
	return ret;
}

CCampaignScenario CCampaignHandler::readScenarioFromMemory( const ui8 *buffer, int & outIt, int version, int mapVersion )
{
	struct HLP
	{
		//reads prolog/epilog info from memory
		static CCampaignScenario::SScenarioPrologEpilog prologEpilogReader( const ui8 *buffer, int & outIt )
		{
			CCampaignScenario::SScenarioPrologEpilog ret;
			ret.hasPrologEpilog = buffer[outIt++];
			if(ret.hasPrologEpilog)
			{
				ret.prologVideo = buffer[outIt++];
				ret.prologMusic = buffer[outIt++];
				ret.prologText = readString(buffer, outIt);
			}
			return ret;
		}
	};
	CCampaignScenario ret;
	ret.conquered = false;
	ret.mapName = readString(buffer, outIt);
	ret.packedMapSize = read_le_u32(buffer + outIt); outIt += 4;
	if(mapVersion == 18)//unholy alliance
	{
		ret.loadPreconditionRegions(read_le_u16(buffer + outIt)); outIt += 2;
	}
	else
	{
		ret.loadPreconditionRegions(buffer[outIt++]);
	}
	ret.regionColor = buffer[outIt++];
	ret.difficulty = buffer[outIt++];
	ret.regionText = readString(buffer, outIt);
	ret.prolog = HLP::prologEpilogReader(buffer, outIt);
	ret.epilog = HLP::prologEpilogReader(buffer, outIt);

	ret.travelOptions = readScenarioTravelFromMemory(buffer, outIt, version);

	return ret;
}

void CCampaignScenario::loadPreconditionRegions(ui32 regions)
{
	for (int i=0; i<32; i++) //for each bit in region. h3c however can only hold up to 16
	{
		if ( (1 << i) & regions)
			preconditionRegions.insert(i);
	}
}

CScenarioTravel CCampaignHandler::readScenarioTravelFromMemory( const ui8 * buffer, int & outIt , int version )
{
	CScenarioTravel ret;

	ret.whatHeroKeeps = buffer[outIt++];
	memcpy(ret.monstersKeptByHero, buffer+outIt, ARRAY_COUNT(ret.monstersKeptByHero));
	outIt += ARRAY_COUNT(ret.monstersKeptByHero);
	int artifBytes;
	if (version < CampaignVersion::SoD)
	{
		artifBytes = 17;
		ret.artifsKeptByHero[17] = 0;
	} 
	else
	{
		artifBytes = 18;
	}
	memcpy(ret.artifsKeptByHero, buffer+outIt, artifBytes);
	outIt += artifBytes;

	ret.startOptions = buffer[outIt++];
	
	switch(ret.startOptions)
	{
	case 0:
		//no bonuses. Seems to be OK
		break;
	case 1: //reading of bonuses player can choose
		{
			ret.playerColor = buffer[outIt++];
			ui8 numOfBonuses = buffer[outIt++];
			for (int g=0; g<numOfBonuses; ++g)
			{
				CScenarioTravel::STravelBonus bonus;
				bonus.type = buffer[outIt++];
				//hero: FFFD means 'most powerful' and FFFE means 'generated'
				switch(bonus.type)
				{
				case 0: //spell
					{
						bonus.info1 = read_le_u16(buffer + outIt); outIt += 2; //hero
						bonus.info2 = buffer[outIt++]; //spell ID
						break;
					}
				case 1: //monster
					{
						bonus.info1 = read_le_u16(buffer + outIt); outIt += 2; //hero
						bonus.info2 = read_le_u16(buffer + outIt); outIt += 2; //monster type
						bonus.info3 = read_le_u16(buffer + outIt); outIt += 2; //monster count
						break;
					}
				case 2: //building
					{
						bonus.info1 = buffer[outIt++]; //building ID (0 - town hall, 1 - city hall, 2 - capitol, etc)
						break;
					}
				case 3: //artifact
					{
						bonus.info1 = read_le_u16(buffer + outIt); outIt += 2; //hero
						bonus.info2 = read_le_u16(buffer + outIt); outIt += 2; //artifact ID
						break;
					}
				case 4: //spell scroll
					{
						bonus.info1 = read_le_u16(buffer + outIt); outIt += 2; //hero
						bonus.info2 = buffer[outIt++]; //spell ID
						break;
					}
				case 5: //prim skill
					{
						bonus.info1 = read_le_u16(buffer + outIt); outIt += 2; //hero
						bonus.info2 = read_le_u32(buffer + outIt); outIt += 4; //bonuses (4 bytes for 4 skills)
						break;
					}
				case 6: //sec skills
					{
						bonus.info1 = read_le_u16(buffer + outIt); outIt += 2; //hero
						bonus.info2 = buffer[outIt++]; //skill ID
						bonus.info3 = buffer[outIt++]; //skill level
						break;
					}
				case 7: //resources
					{
						bonus.info1 = buffer[outIt++]; //type
						//FD - wood+ore
						//FE - mercury+sulfur+crystal+gem
						bonus.info2 = read_le_u32(buffer + outIt); outIt += 4; //count
						break;
					}
				}
				ret.bonusesToChoose.push_back(bonus);
			}
			break;
		}
	case 2: //reading of players (colors / scenarios ?) player can choose
		{
			ui8 numOfBonuses = buffer[outIt++];
			for (int g=0; g<numOfBonuses; ++g)
			{
				CScenarioTravel::STravelBonus bonus;
				bonus.type = 8;
				bonus.info1 = buffer[outIt++]; //player color
				bonus.info2 = buffer[outIt++]; //from what scenario

				ret.bonusesToChoose.push_back(bonus);
			}
			break;
		}
	case 3: //heroes player can choose between
		{
			ui8 numOfBonuses = buffer[outIt++];
			for (int g=0; g<numOfBonuses; ++g)
			{
				CScenarioTravel::STravelBonus bonus;
				bonus.type = 9;
				bonus.info1 = buffer[outIt++]; //player color
				bonus.info2 = read_le_u16(buffer + outIt); outIt += 2; //hero, FF FF - random

				ret.bonusesToChoose.push_back(bonus);
			}
			break;
		}
	default:
		{
			tlog1<<"Corrupted h3c file"<<std::endl;
			break;
		}
	}

	return ret;
}

std::vector<ui32> CCampaignHandler::locateH3mStarts( const ui8 * buffer, int start, int size )
{
	std::vector<ui32> ret;
	for(int g=start; g<size; ++g)
	{
		if(startsAt(buffer, size, g))
		{
			ret.push_back(g);
		}
	}

	return ret;
}

bool CCampaignHandler::startsAt( const ui8 * buffer, int size, int pos )
{
	struct HLP
	{
		static ui8 at(const ui8 * buffer, int size, int place)
		{
			if(place < size)
				return buffer[place];

			throw std::runtime_error("Out of bounds!");
		}
	};
	try
	{
		//check minimal length of given region
		HLP::at(buffer, size, 100);
		//check version

		ui8 tmp = HLP::at(buffer, size, pos);
		if(!(tmp == 0x0e || tmp == 0x15 || tmp == 0x1c || tmp == 0x33))
		{
			return false;
		}
		//3 bytes following version
		if(HLP::at(buffer, size, pos+1) != 0 || HLP::at(buffer, size, pos+2) != 0 || HLP::at(buffer, size, pos+3) != 0)
		{
			return false;
		}
		//unknown strange byte
		tmp = HLP::at(buffer, size, pos+4);
		if(tmp != 0 && tmp != 1 )
		{
			return false;
		}
		//size of map
		int mapsize = read_le_u32(buffer + pos+5);
		if(mapsize < 10 || mapsize > 530) 
		{
			return false;
		}

		//underground or not
		tmp = HLP::at(buffer, size, pos+9);
		if( tmp != 0 && tmp != 1 )
		{
			return false;
		}

		//map name
		int len = read_le_u32(buffer + pos+10);
		if(len < 0 || len > 100)
		{
			return false;
		}
		for(int t=0; t<len; ++t)
		{
			tmp = HLP::at(buffer, size, pos+14+t);
			if(tmp == 0 || (tmp > 15 && tmp < 32)) //not a valid character
			{
				return false;
			}
		}

	}
	catch (...)
	{
		return false;
	}
	return true;
}

ui8 * CCampaignHandler::getFile( const std::string & name, bool fromLod, int & outSize )
{
	ui8 * cmpgn = 0;
	if(fromLod)
	{
		if (bitmaph->haveFile(name, FILE_CAMPAIGN))
			cmpgn = bitmaph->giveFile(name, FILE_CAMPAIGN, &outSize);
		else if (bitmaph_ab->haveFile(name, FILE_CAMPAIGN))
			cmpgn = bitmaph_ab->giveFile(name, FILE_CAMPAIGN, &outSize);
		else
			tlog1 << "Cannot find file: " << name << std::endl;
		cmpgn = CLodHandler::getUnpackedData(cmpgn, outSize, &outSize);
	}
	else
	{
		cmpgn = CLodHandler::getUnpackedFile(name, &outSize);
	}
	return cmpgn;
}

bool CCampaign::conquerable( int whichScenario ) const
{
	//check for void scenraio
	if (!scenarios[whichScenario].isNotVoid())
	{
		return false;
	}

	if (scenarios[whichScenario].conquered)
	{
		return false;
	}
	//check preconditioned regions
	for (int g=0; g<scenarios.size(); ++g)
	{
		if( vstd::contains(scenarios[whichScenario].preconditionRegions, g) && !scenarios[g].conquered)
			return false; //prerequisite does not met
			
	}
	return true;
}

CCampaign::CCampaign()
{

}

bool CCampaignScenario::isNotVoid() const
{
	return mapName.size() > 0;
}

void CCampaignScenario::prepareCrossoverHeroes( std::vector<CGHeroInstance *> heroes )
{
	crossoverHeroes = heroes;

	
	if (!(travelOptions.whatHeroKeeps & 1))
	{
		//trimming experience
		BOOST_FOREACH(CGHeroInstance * cgh, crossoverHeroes)
		{
			cgh->initExp();
		}
	}
	if (!(travelOptions.whatHeroKeeps & 2))
	{
		//trimming prim skills
		BOOST_FOREACH(CGHeroInstance * cgh, crossoverHeroes)
		{
#define RESET_PRIM_SKILL(NAME, VALNAME) \
			cgh->getBonus(Selector::type(Bonus::PRIMARY_SKILL) && \
				Selector::subtype(PrimarySkill::NAME) && \
				Selector::sourceType(Bonus::HERO_BASE_SKILL) )->val = cgh->type->heroClass->VALNAME;

			RESET_PRIM_SKILL(ATTACK, initialAttack);
			RESET_PRIM_SKILL(DEFENSE, initialDefence);
			RESET_PRIM_SKILL(SPELL_POWER, initialPower);
			RESET_PRIM_SKILL(KNOWLEDGE, initialKnowledge);
#undef RESET_PRIM_SKILL
		}
	}
	if (!(travelOptions.whatHeroKeeps & 4))
	{
		//trimming sec skills
		BOOST_FOREACH(CGHeroInstance * cgh, crossoverHeroes)
		{
			cgh->secSkills = cgh->type->secSkillsInit;
		}
	}
	if (!(travelOptions.whatHeroKeeps & 8))
	{
		//trimming spells
		BOOST_FOREACH(CGHeroInstance * cgh, crossoverHeroes)
		{
			cgh->spells.clear();
		}
	}
	if (!(travelOptions.whatHeroKeeps & 16))
	{
		//trimming artifacts
		BOOST_FOREACH(CGHeroInstance * hero, crossoverHeroes)
		{
			size_t totalArts = GameConstants::BACKPACK_START + hero->artifactsInBackpack.size();
			for (size_t i=0; i<totalArts; i++ )
			{
				const ArtSlotInfo *info = hero->getSlot(i);
				if (!info)
					continue;
				
				const CArtifactInstance *art = info->artifact;
				if (!art)//FIXME: check spellbook and catapult behaviour
					continue;

				int id  = art->artType->id;
				assert( 8*18 > id );//number of arts that fits into h3m format
				bool takeable = travelOptions.artifsKeptByHero[id / 8] & ( 1 << (id%8) );

				if (takeable)
					hero->eraseArtSlot(i);
			}
		}
	}

	//trimming creatures
	BOOST_FOREACH(CGHeroInstance * cgh, crossoverHeroes)
	{
		for (TSlots::const_iterator j = cgh->Slots().begin(); j != cgh->Slots().end(); j++)
		{
			if (!(travelOptions.monstersKeptByHero[j->first / 8] & (1 << (j->first%8)) ))
			{
				cgh->eraseStack(j->first);
				j = cgh->Slots().begin();
			}
		}
	}
}

bool CScenarioTravel::STravelBonus::isBonusForHero() const
{
	return type == 0 || type == 1 || type == 3 || type == 4 || type == 5 || type == 6;
}

void CCampaignState::initNewCampaign( const StartInfo &si )
{
	assert(si.mode == StartInfo::CAMPAIGN);
	campaignName = si.mapname;
	currentMap = si.whichMapInCampaign;

	//check if campaign is in lod or not
	bool inLod = campaignName.find('/') == std::string::npos;

	camp = CCampaignHandler::getCampaign(campaignName, inLod); //TODO lod???
	for (ui8 i = 0; i < camp->mapPieces.size(); i++)
		mapsRemaining.push_back(i);
}

void CCampaignState::mapConquered( const std::vector<CGHeroInstance*> & heroes )
{
	camp->scenarios[currentMap].prepareCrossoverHeroes(heroes);
	mapsConquered.push_back(currentMap);
	mapsRemaining -= currentMap;
	camp->scenarios[currentMap].conquered = true;
}
