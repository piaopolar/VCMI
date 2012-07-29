#pragma once



#ifndef _MSC_VER
#include "CCreatureHandler.h"
#include "VCMI_Lib.h"
#include "map.h"
#endif

#include "HeroBonus.h"
#include "CCreatureSet.h"
#include "ConstTransitivePtr.h"
#include "IGameCallback.h"
#include "ResourceSet.h"
#include "int3.h"
#include "CObjectHandler.h"


/*
 * CGameState.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

class CTown;
class CScriptCallback;
class CCallback;
class IGameCallback;
class CLuaCallback;
class CCPPObjectScript;
class CCreatureSet;
class CStack;
class CQuest;
class CGHeroInstance;
class CGTownInstance;
class CArmedInstance;
class CGDwelling;
class CGDefInfo;
class CObjectScript;
class CGObjectInstance;
class CCreature;
struct Mapa;
struct StartInfo;
struct SDL_Surface;
class CMapHandler;
class CPathfinder;
struct SetObjectProperty;
struct MetaString;
struct CPack;
class CSpell;
struct TerrainTile;
class CHeroClass;
class CCampaign;
class CCampaignState;
class IModableArt;
class CGGarrison;
class CGameInfo;
struct QuestInfo;
class CQuest;

namespace boost
{
	class shared_mutex;
}

//numbers of creatures are exact numbers if detailed else they are quantity ids (0 - a few, 1 - several and so on; additionally -1 - unknown)
struct ArmyDescriptor : public std::map<TSlot, CStackBasicDescriptor>
{
	bool isDetailed; 
	DLL_LINKAGE ArmyDescriptor(const CArmedInstance *army, bool detailed); //not detailed -> quantity ids as count
	DLL_LINKAGE ArmyDescriptor();

	DLL_LINKAGE int getStrength() const;
};

struct DLL_LINKAGE InfoAboutArmy
{
	ui8 owner;
	std::string name;

	ArmyDescriptor army;

	InfoAboutArmy();
	InfoAboutArmy(const CArmedInstance *Army, bool detailed);

	void initFromArmy(const CArmedInstance *Army, bool detailed);
};

struct DLL_LINKAGE InfoAboutHero : public InfoAboutArmy
{
private:
	void assign(const InfoAboutHero & iah);
public:
	struct DLL_LINKAGE Details
	{
		std::vector<si32> primskills;
		si32 mana, luck, morale;
	} *details;

	const CHeroClass *hclass;
	int portrait;

	InfoAboutHero();
	InfoAboutHero(const InfoAboutHero & iah);
	InfoAboutHero(const CGHeroInstance *h, bool detailed);
	~InfoAboutHero();

	InfoAboutHero & operator=(const InfoAboutHero & iah);

	void initFromHero(const CGHeroInstance *h, bool detailed);
};

/// Struct which holds a int information about a town
struct DLL_LINKAGE InfoAboutTown : public InfoAboutArmy
{
	struct DLL_LINKAGE Details
	{
		si32 hallLevel, goldIncome;
		bool customRes;
		bool garrisonedHero;

	} *details;

	CTown *tType;

	si32 built;
	si32 fortLevel; //0 - none

	InfoAboutTown();
	InfoAboutTown(const CGTownInstance *t, bool detailed);
	~InfoAboutTown();
	void initFromTown(const CGTownInstance *t, bool detailed);
};

// typedef si32 TResourceUnit;
// typedef std::vector<si32> TResourceVector;
// typedef std::set<si32> TResourceSet;

struct DLL_LINKAGE SThievesGuildInfo
{
	std::vector<ui8> playerColors; //colors of players that are in-game

	std::vector< std::vector< ui8 > > numOfTowns, numOfHeroes, gold, woodOre, mercSulfCrystGems, obelisks, artifacts, army, income; // [place] -> [colours of players]

	std::map<ui8, InfoAboutHero> colorToBestHero; //maps player's color to his best heros' 

	std::map<ui8, si8> personality; // color to personality // -1 - human, AI -> (00 - random, 01 -  warrior, 02 - builder, 03 - explorer)
	std::map<ui8, si32> bestCreature; // color to ID // id or -1 if not known

// 	template <typename Handler> void serialize(Handler &h, const int version)
// 	{
// 		h & playerColors & numOfTowns & numOfHeroes & gold & woodOre & mercSulfCrystGems & obelisks & artifacts & army & income;
// 		h & colorToBestHero & personality & bestCreature;
// 	}

};

struct DLL_LINKAGE PlayerState : public CBonusSystemNode
{
public:
	enum EStatus {INGAME, LOSER, WINNER};
	ui8 color;
	ui8 human; //true if human controlled player, false for AI
	ui32 currentSelection; //id of hero/town, 0xffffffff if none
	ui8 team;
	TResources resources;
	std::vector<ConstTransitivePtr<CGHeroInstance> > heroes;
	std::vector<ConstTransitivePtr<CGTownInstance> > towns;
	std::vector<ConstTransitivePtr<CGHeroInstance> > availableHeroes; //heroes available in taverns
	std::vector<ConstTransitivePtr<CGDwelling> > dwellings; //used for town growth
	std::vector<QuestInfo> quests; //store info about all received quests

	ui8 enteredWinningCheatCode, enteredLosingCheatCode; //if true, this player has entered cheat codes for loss / victory
	ui8 status; //0 - in game, 1 - loser, 2 - winner <- uses EStatus enum
	ui8 daysWithoutCastle;

	PlayerState();
	std::string nodeName() const OVERRIDE;

	//override
	//void getParents(TCNodes &out, const CBonusSystemNode *root = NULL) const; 
	//void getBonuses(BonusList &out, const CSelector &selector, const CBonusSystemNode *root = NULL) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & color & human & currentSelection & team & resources & status;
		h & heroes & towns & availableHeroes & dwellings;
		h & getBonusList(); //FIXME FIXME FIXME 
		h & status & daysWithoutCastle;
		h & enteredLosingCheatCode & enteredWinningCheatCode;
		h & static_cast<CBonusSystemNode&>(*this);
	}
};

struct DLL_LINKAGE TeamState : public CBonusSystemNode
{
public:
	ui8 id; //position in gameState::teams
	std::set<ui8> players; // members of this team
	std::vector<std::vector<std::vector<ui8> > >  fogOfWarMap; //true - visible, false - hidden
	
	TeamState();
	
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & id & players & fogOfWarMap;
		h & static_cast<CBonusSystemNode&>(*this);
	}

};

struct UpgradeInfo
{
	int oldID; //creature to be upgraded
	std::vector<int> newID; //possible upgrades
	std::vector<TResources> cost; // cost[upgrade_serial] -> set of pairs<resource_ID,resource_amount>; cost is for single unit (not entire stack)
	UpgradeInfo(){oldID = -1;};
};

struct CPathNode
{
	bool accessible; //true if a hero can be on this node
	int dist; //distance from the first node of searching; -1 is infinity
	CPathNode * theNodeBefore;
	int3 coord; //coordiantes
	bool visited;
};

struct DLL_LINKAGE CGPathNode
{
	enum EAccessibility
	{
		ACCESSIBLE=1, //tile can be entered and passed
		VISITABLE, //tile can be entered as the last tile in path
		BLOCKVIS,  //visitable from neighbouring tile but not passable
		BLOCKED //tile can't be entered nor visited
	};

	ui8 accessible; //the enum above
	ui8 land;
	ui8 turns;
	ui32 moveRemains;
	CGPathNode * theNodeBefore;
	int3 coord; //coordinates

	CGPathNode();
	bool reachable() const;
};


struct DLL_LINKAGE CPath
{
	std::vector<CPathNode> nodes; //just get node by node

	int3 startPos() const; // start point
	int3 endPos() const; //destination point
	void convert(ui8 mode); //mode=0 -> from 'manifest' to 'object'
};

struct DLL_LINKAGE CGPath
{
	std::vector<CGPathNode> nodes; //just get node by node

	int3 startPos() const; // start point
	int3 endPos() const; //destination point
	void convert(ui8 mode); //mode=0 -> from 'manifest' to 'object'
};

struct DLL_LINKAGE CPathsInfo
{
	bool isValid;
	const CGHeroInstance *hero;
	int3 hpos;
	int3 sizes;
	CGPathNode ***nodes; //[w][h][level]

	bool getPath(const int3 &dst, CGPath &out);
	CPathsInfo(const int3 &Sizes);
	~CPathsInfo();
};

struct DLL_EXPORT DuelParameters
{
	si32 terType, bfieldType;
	struct DLL_EXPORT SideSettings
	{
		struct DLL_EXPORT StackSettings
		{
			si32 type;
			si32 count;
			template <typename Handler> void serialize(Handler &h, const int version)
			{
				h & type & count;
			}

			StackSettings();
			StackSettings(si32 Type, si32 Count);
		} stacks[GameConstants::ARMY_SIZE];

		si32 heroId; //-1 if none
		std::vector<si32> heroPrimSkills; //may be empty
		std::map<si32, CArtifactInstance*> artifacts;
		std::vector<std::pair<si32, si8> > heroSecSkills; //may be empty; pairs <id, level>, level [0-3]
		std::set<si32> spells;

		SideSettings();
		template <typename Handler> void serialize(Handler &h, const int version)
		{
			h & stacks & heroId & heroPrimSkills & artifacts & heroSecSkills & spells;
		}
	} sides[2];

	std::vector<shared_ptr<CObstacleInstance> > obstacles;

	static DuelParameters fromJSON(const std::string &fname);

	struct CusomCreature
	{
		int id;
		int attack, defense, dmg, HP, speed, shoots;

		CusomCreature() 
		{
			id = attack = defense = dmg = HP = speed = shoots = -1;
		}
		template <typename Handler> void serialize(Handler &h, const int version)
		{
			h & id & attack & defense & dmg & HP & speed & shoots;
		}
	};

	std::vector<CusomCreature> creatures;

	DuelParameters();
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & terType & bfieldType & sides & obstacles & creatures;
	}
};

class CPathfinder : private CGameInfoCallback
{
private:
	bool useSubterraneanGates;
	bool allowEmbarkAndDisembark;
	CPathsInfo &out;
	const CGHeroInstance *hero;
	const std::vector<std::vector<std::vector<ui8> > > &FoW;

	std::list<CGPathNode*> mq; //BFS queue -> nodes to be checked


	int3 curPos;
	CGPathNode *cp; //current (source) path node -> we took it from the queue
	CGPathNode *dp; //destination node -> it's a neighbour of cp that we consider
	const TerrainTile *ct, *dt; //tile info for both nodes
	ui8 useEmbarkCost; //0 - usual movement; 1 - embark; 2 - disembark
	int destTopVisObjID;


	CGPathNode *getNode(const int3 &coord);
	void initializeGraph();
	bool goodForLandSeaTransition(); //checks if current move will be between sea<->land. If so, checks it legality (returns false if movement is not possible) and sets useEmbarkCost

	CGPathNode::EAccessibility evaluateAccessibility(const TerrainTile *tinfo) const;
	bool canMoveBetween(const int3 &a, const int3 &b) const; //checks only for visitable objects that may make moving between tiles impossible, not other conditions (like tiles itself accessibility)
	bool canStepOntoDst() const;

public:
	CPathfinder(CPathsInfo &_out, CGameState *_gs, const CGHeroInstance *_hero);
	void calculatePaths(int3 src = int3(-1,-1,-1), int movement = -1); //calculates possible paths for hero, by default uses current hero position and movement left; returns pointer to newly allocated CPath or NULL if path does not exists
};


struct BattleInfo;

class DLL_LINKAGE CGameState : public CNonConstInfoCallback
{
public:
	ConstTransitivePtr<StartInfo> scenarioOps, initialOpts; //second one is a copy of settings received from pregame (not randomized)
	ConstTransitivePtr<CCampaignState> campaign;
	ui8 currentPlayer; //ID of player currently having turn
	ConstTransitivePtr<BattleInfo> curB; //current battle
	ui32 day; //total number of days in game
	ConstTransitivePtr<Mapa> map;
	bmap<ui8, PlayerState> players; //ID <-> player state
	bmap<ui8, TeamState> teams; //ID <-> team state
	bmap<int, ConstTransitivePtr<CGDefInfo> > villages, forts, capitols; //def-info for town graphics
	CBonusSystemNode globalEffects;
	bmap<const CGHeroInstance*, const CGObjectInstance*> ongoingVisits;

	struct DLL_LINKAGE HeroesPool
	{
		bmap<ui32, ConstTransitivePtr<CGHeroInstance> > heroesPool; //[subID] - heroes available to buy; NULL if not available
		bmap<ui32,ui8> pavailable; // [subid] -> which players can recruit hero (binary flags)

		CGHeroInstance * pickHeroFor(bool native, int player, const CTown *town, bmap<ui32, ConstTransitivePtr<CGHeroInstance> > &available, const CHeroClass *bannedClass = NULL) const;

		template <typename Handler> void serialize(Handler &h, const int version)
		{
			h & heroesPool & pavailable;
		}
	} hpool; //we have here all heroes available on this map that are not hired

	boost::shared_mutex *mx;

	void init(StartInfo * si);

	void initDuel();
	void loadTownDInfos();
	void randomizeObject(CGObjectInstance *cur);
	std::pair<int,int> pickObject(CGObjectInstance *obj); //chooses type of object to be randomized, returns <type, subtype>
	int pickHero(int owner);
	void giveHeroArtifact(CGHeroInstance *h, int aid);

	void apply(CPack *pack);
	int battleGetBattlefieldType(int3 tile) const;//   1. sand/shore   2. sand/mesas   3. dirt/birches   4. dirt/hills   5. dirt/pines   6. grass/hills   7. grass/pines   8. lava   9. magic plains   10. snow/mountains   11. snow/trees   12. subterranean   13. swamp/trees   14. fiery fields   15. rock lands   16. magic clouds   17. lucid pools   18. holy ground   19. clover field   20. evil fog   21. "favourable winds" text on magic plains background   22. cursed ground   23. rough   24. ship to ship   25. ship
	UpgradeInfo getUpgradeInfo(const CStackInstance &stack);
	int getPlayerRelations(ui8 color1, ui8 color2);// 0 = enemy, 1 = ally, 2 = same player
	bool checkForVisitableDir(const int3 & src, const int3 & dst) const; //check if src tile is visitable from dst tile
	bool checkForVisitableDir(const int3 & src, const TerrainTile *pom, const int3 & dst) const; //check if src tile is visitable from dst tile
	bool getPath(int3 src, int3 dest, const CGHeroInstance * hero, CPath &ret); //calculates path between src and dest; returns pointer to newly allocated CPath or NULL if path does not exists
	void calculatePaths(const CGHeroInstance *hero, CPathsInfo &out, int3 src = int3(-1,-1,-1), int movement = -1); //calculates possible paths for hero, by default uses current hero position and movement left; returns pointer to newly allocated CPath or NULL if path does not exists
	int3 guardingCreaturePosition (int3 pos) const;
	int victoryCheck(ui8 player) const; //checks if given player is winner; -1 if std victory, 1 if special victory, 0 else
	int lossCheck(ui8 player) const; //checks if given player is loser;  -1 if std loss, 1 if special, 0 else
	ui8 checkForStandardWin() const; //returns color of player that accomplished standard victory conditions or 255 if no winner
	bool checkForStandardLoss(ui8 player) const; //checks if given player lost the game
	void obtainPlayersStats(SThievesGuildInfo & tgi, int level); //fills tgi with info about other players that is available at given level of thieves' guild
	bmap<ui32, ConstTransitivePtr<CGHeroInstance> > unusedHeroesFromPool(); //heroes pool without heroes that are available in taverns
	BattleInfo * setupBattle(int3 tile, const CArmedInstance *armies[2], const CGHeroInstance * heroes[2], bool creatureBank, const CGTownInstance *town);

	void buildBonusSystemTree();
	void attachArmedObjects();
	void buildGlobalTeamPlayerTree();
	void deserializationFix();

	bool isVisible(int3 pos, int player);
	bool isVisible(const CGObjectInstance *obj, int player);

	CGameState(); //c-tor
	virtual ~CGameState(); //d-tor
	void getNeighbours(const TerrainTile &srct, int3 tile, std::vector<int3> &vec, const boost::logic::tribool &onLand, bool limitCoastSailing);
	int getMovementCost(const CGHeroInstance *h, const int3 &src, const int3 &dest, int remainingMovePoints=-1, bool checkLast=true);
	int getDate(int mode=0) const; //mode=0 - total days in game, mode=1 - day of week, mode=2 - current week, mode=3 - current month
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & scenarioOps & initialOpts & currentPlayer & day & map & players & teams & hpool & globalEffects & campaign;
		h & villages & forts & capitols;
		if(!h.saving)
		{
			loadTownDInfos();
		}
		BONUS_TREE_DESERIALIZATION_FIX
	}

	friend class CCallback;
	friend class CLuaCallback;
	friend class CClient;
	friend void initGameState(Mapa * map, CGameInfo * cgi);
	friend class IGameCallback;
	friend class CMapHandler;
	friend class CGameHandler;
};
 
struct DLL_LINKAGE QuestInfo //universal interface for human and AI
{
	const CQuest * quest;
	const CGObjectInstance * obj; //related object, most likely Seer Hut
	int3 tile;

	QuestInfo(){};
	QuestInfo (const CQuest * Quest, const CGObjectInstance * Obj, int3 Tile) :
		quest (Quest), obj (Obj), tile (Tile){};

	bool operator== (const QuestInfo qi) const
	{
		return (quest == qi.quest && obj == qi.obj);
	}

	//std::vector<std::string> > texts //allow additional info for quest log?

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & quest & obj & tile;
	}
};