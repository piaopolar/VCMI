#pragma once


#ifndef _MSC_VER
#include "CHeroHandler.h"
#include "CTownHandler.h"
#include "../lib/VCMI_Lib.h"
#endif
#include "../lib/CCreatureSet.h"
#include "CArtHandler.h"
#include "../lib/ConstTransitivePtr.h"
#include "int3.h"
#include "GameConstants.h"

/*
 * CObjectHandler.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

class CGameState;
class CArtifactInstance;
struct MetaString;
struct BattleInfo;
struct QuestInfo;
class IGameCallback;
struct BattleResult;
class CCPPObjectScript;
class CGObjectInstance;
class CScript;
class CObjectScript;
class CGHeroInstance;
class CTown;
class CHero;
class CBuilding;
class CSpell;
class CGTownInstance;
class CGTownBuilding;
class CArtifact;
class CGDefInfo;
class CSpecObjInfo;
class CCastleEvent;
struct TerrainTile;
struct InfoWindow;
struct Component;
struct BankConfig;
struct UpdateHeroSpeciality;
struct NewArtifact;
class CGBoat;
class CArtifactSet;
class CCommanderInstance;

class DLL_LINKAGE CQuest
{
public:
	enum Emission {MISSION_NONE = 0, MISSION_LEVEL = 1, MISSION_PRIMARY_STAT = 2, MISSION_KILL_HERO = 3, MISSION_KILL_CREATURE = 4,
		MISSION_ART = 5, MISSION_ARMY = 6, MISSION_RESOURCES = 7, MISSION_HERO = 8, MISSION_PLAYER = 9, MISSION_KEYMASTER = 10};
	enum Eprogress {NOT_ACTIVE, IN_PROGRESS, COMPLETE};

	si32 qid; //unique quets id for serialization / identification

	ui8 missionType, progress;
	si32 lastDay; //after this day (first day is 0) mission cannot be completed; if -1 - no limit

	ui32 m13489val;
	std::vector<ui32> m2stats;
	std::vector<ui16> m5arts; //artifacts id
	std::vector<CStackBasicDescriptor> m6creatures; //pair[cre id, cre count], CreatureSet info irrelevant
	std::vector<ui32> m7resources; //TODO: use resourceset?

	//following field are used only for kill creature/hero missions, the original objects became inaccessible after their removal, so we need to store info needed for messages / hover text
	ui8 textOption;
	CStackBasicDescriptor stackToKill; 
	ui8 stackDirection;
	std::string heroName; //backup of hero name
	si32 heroPortrait;

	std::string firstVisitText, nextVisitText, completedText;
	bool isCustomFirst, isCustomNext, isCustomComplete;

	virtual bool checkQuest (const CGHeroInstance * h) const; //determines whether the quest is complete or not
	virtual void getVisitText (MetaString &text, std::vector<Component> &components, bool isCustom, bool FirstVisit, const CGHeroInstance * h = NULL) const;
	virtual void getCompletionText (MetaString &text, std::vector<Component> &components, bool isCustom, const CGHeroInstance * h = NULL) const;
	virtual void getRolloverText (MetaString &text, bool onHover) const; //hover or quest log entry
	virtual void completeQuest (const CGHeroInstance * h) const {};
	virtual void addReplacements(MetaString &out, const std::string &base) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & qid & missionType & progress & lastDay & m13489val & m2stats & m5arts & m6creatures & m7resources
			& textOption & stackToKill & stackDirection & heroName & heroPortrait
			& firstVisitText & nextVisitText & completedText & isCustomFirst & isCustomNext & isCustomComplete;
	}
};

class DLL_LINKAGE IObjectInterface
{
public:
	static IGameCallback *cb;

	IObjectInterface();
	virtual ~IObjectInterface();

	virtual void onHeroVisit(const CGHeroInstance * h) const;
	virtual void onHeroLeave(const CGHeroInstance * h) const;
	virtual void newTurn() const;
	virtual void initObj(); //synchr
	virtual void setProperty(ui8 what, ui32 val);//synchr
//unified interface, AI helpers
	virtual bool wasVisited (ui8 player) const;
	virtual bool wasVisited (const CGHeroInstance * h) const;

	static void preInit(); //called before objs receive their initObj
	static void postInit();//caleed after objs receive their initObj
};

class DLL_LINKAGE IBoatGenerator
{
public:
	const CGObjectInstance *o;

	IBoatGenerator(const CGObjectInstance *O);
	virtual int getBoatType() const; //0 - evil (if a ship can be evil...?), 1 - good, 2 - neutral
	virtual void getOutOffsets(std::vector<int3> &offsets) const =0; //offsets to obj pos when we boat can be placed
	int3 bestLocation() const; //returns location when the boat should be placed
	int state() const; //0 - can buid, 1 - there is already a boat at dest tile, 2 - dest tile is blocked, 3 - no water
	void getProblemText(MetaString &out, const CGHeroInstance *visitor = NULL) const; 
};

class DLL_LINKAGE IShipyard : public IBoatGenerator
{
public:
	IShipyard(const CGObjectInstance *O);
	virtual void getBoatCost(std::vector<si32> &cost) const;

	static const IShipyard *castFrom(const CGObjectInstance *obj);
	static IShipyard *castFrom(CGObjectInstance *obj);
};

class DLL_LINKAGE IMarket
{
public:
	const CGObjectInstance *o;

	IMarket(const CGObjectInstance *O);
	virtual int getMarketEfficiency() const =0;
	virtual bool allowsTrade(EMarketMode::EMarketMode mode) const;
	virtual int availableUnits(EMarketMode::EMarketMode mode, int marketItemSerial) const; //-1 if unlimited
	virtual std::vector<int> availableItemsIds(EMarketMode::EMarketMode mode) const;

	bool getOffer(int id1, int id2, int &val1, int &val2, EMarketMode::EMarketMode mode) const; //val1 - how many units of id1 player has to give to receive val2 units 
	std::vector<EMarketMode::EMarketMode> availableModes() const;

	static const IMarket *castFrom(const CGObjectInstance *obj, bool verbose = true);
};

class DLL_LINKAGE CGObjectInstance : public IObjectInterface
{
protected:
	void getNameVis(std::string &hname) const;
	void giveDummyBonus(int heroID, ui8 duration = Bonus::ONE_DAY) const;
public:
	mutable std::string hoverName;
	int3 pos; //h3m pos
	si32 ID, subID; //normal ID (this one from OH3 maps ;]) - eg. town=98; hero=34
	si32 id;//number of object in CObjectHandler's vector		
	CGDefInfo * defInfo;
	ui8 animPhaseShift;

	ui8 tempOwner;
	ui8 blockVisit; //if non-zero then blocks the tile but is visitable from neighbouring tile

	virtual ui8 getPassableness() const; //bitmap - if the bit is set the corresponding player can pass through the visitable tiles of object, even if it's blockvis; if not set - default properties from definfo are used
	virtual int3 getSightCenter() const; //"center" tile from which the sight distance is calculated
	virtual int getSightRadious() const; //sight distance (should be used if player-owned structure)
	void getSightTiles(boost::unordered_set<int3, ShashInt3> &tiles) const; //returns reference to the set
	int getOwner() const;
	void setOwner(int ow);
	int getWidth() const; //returns width of object graphic in tiles
	int getHeight() const; //returns height of object graphic in tiles
	bool visitableAt(int x, int y) const; //returns true if object is visitable at location (x, y) form left top tile of image (x, y in tiles)
	int3 getVisitableOffset() const; //returns (x,y,0) offset to first visitable tile from bottom right obj tile (0,0,0) (h3m pos)
	int3 visitablePos() const;
	bool blockingAt(int x, int y) const; //returns true if object is blocking location (x, y) form left top tile of image (x, y in tiles)
	bool coveringAt(int x, int y) const; //returns true if object covers with picture location (x, y) form left top tile of maximal possible image (8 x 6 tiles) (x, y in tiles)
	bool hasShadowAt(int x, int y) const;//returns true if object covers with shadow location (x, y) form left top tile of maximal possible image (8 x 6 tiles) (x, y in tiles)
	std::set<int3> getBlockedPos() const; //returns set of positions blocked by this object
	bool isVisitable() const; //returns true if object is visitable
	bool operator<(const CGObjectInstance & cmp) const;  //screen printing priority comparing
	void hideTiles(int ourplayer, int radius) const;
	CGObjectInstance();
	virtual ~CGObjectInstance();
	//CGObjectInstance(const CGObjectInstance & right);
	//CGObjectInstance& operator=(const CGObjectInstance & right);
	virtual const std::string & getHoverText() const;

	//////////////////////////////////////////////////////////////////////////
	void initObj();
	void onHeroVisit(const CGHeroInstance * h) const;
	void setProperty(ui8 what, ui32 val);//synchr
	virtual void setPropertyDer(ui8 what, ui32 val);//synchr

	friend class CGameHandler;


	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & hoverName & pos & ID & subID & id & animPhaseShift & tempOwner & blockVisit & defInfo;
		//definfo is handled by map serializer
	}
};
class CGHeroPlaceholder : public CGObjectInstance
{
public:
	//subID stores id of hero type. If it's 0xff then following field is used
	ui8 power;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
		h & power;
	}
};

class DLL_LINKAGE CPlayersVisited: public CGObjectInstance
{
public:
	std::set<ui8> players; //players that visited this object

	bool wasVisited(ui8 player) const;
	virtual void setPropertyDer( ui8 what, ui32 val );

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
		h & players;
	}
};

class DLL_LINKAGE CArmedInstance: public CGObjectInstance, public CBonusSystemNode, public CCreatureSet
{
public:
	BattleInfo *battle; //set to the current battle, if engaged

	void randomizeArmy(int type);
	void updateMoraleBonusFromArmy();

	void armyChanged() OVERRIDE;

	//////////////////////////////////////////////////////////////////////////
	int valOfGlobalBonuses(CSelector selector) const; //used only for castle interface								???
	virtual CBonusSystemNode *whereShouldBeAttached(CGameState *gs);
	virtual CBonusSystemNode *whatShouldBeAttached();
	//////////////////////////////////////////////////////////////////////////

	CArmedInstance();

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
		h & static_cast<CBonusSystemNode&>(*this);
		h & static_cast<CCreatureSet&>(*this);
	}
};

class DLL_LINKAGE CGHeroInstance : public CArmedInstance, public IBoatGenerator, public CArtifactSet
{
public:
	enum SecondarySkill
	{
		PATHFINDING = 0, ARCHERY, LOGISTICS, SCOUTING, DIPLOMACY, NAVIGATION, LEADERSHIP, WISDOM, MYSTICISM,
		LUCK, BALLISTICS, EAGLE_EYE, NECROMANCY, ESTATES, FIRE_MAGIC, AIR_MAGIC, WATER_MAGIC, EARTH_MAGIC,
		SCHOLAR, TACTICS, ARTILLERY, LEARNING, OFFENCE, ARMORER, INTELLIGENCE, SORCERY, RESISTANCE,
		FIRST_AID
	};
	enum ECanDig
	{
		CAN_DIG, LACK_OF_MOVEMENT, WRONG_TERRAIN, TILE_OCCUPIED
	};
	//////////////////////////////////////////////////////////////////////////

	ui8 moveDir; //format:	123
					//		8 4
					//		765
	mutable ui8 isStanding, tacticFormationEnabled;

	//////////////////////////////////////////////////////////////////////////

	ConstTransitivePtr<CHero> type;
	ui64 exp; //experience points
	ui32 level; //current level of hero
	std::string name; //may be custom
	std::string biography; //if custom
	si32 portrait; //may be custom
	si32 mana; // remaining spell points
	std::vector<std::pair<ui8,ui8> > secSkills; //first - ID of skill, second - level of skill (1 - basic, 2 - adv., 3 - expert); if hero has ability (-1, -1) it meansthat it should have default secondary abilities
	ui32 movement; //remaining movement points
	ui8 sex;
	ui8 inTownGarrison; // if hero is in town garrison 
	ConstTransitivePtr<CGTownInstance> visitedTown; //set if hero is visiting town or in the town garrison
	ConstTransitivePtr<CCommanderInstance> commander;
	const CGBoat *boat; //set to CGBoat when sailing
	

	//std::vector<const CArtifact*> artifacts; //hero's artifacts from bag
	//std::map<ui16, const CArtifact*> artifWorn; //map<position,artifact_id>; positions: 0 - head; 1 - shoulders; 2 - neck; 3 - right hand; 4 - left hand; 5 - torso; 6 - right ring; 7 - left ring; 8 - feet; 9 - misc1; 10 - misc2; 11 - misc3; 12 - misc4; 13 - mach1; 14 - mach2; 15 - mach3; 16 - mach4; 17 - spellbook; 18 - misc5
	std::set<ui32> spells; //known spells (spell IDs)


	struct DLL_LINKAGE Patrol
	{
		Patrol(){patrolling=false;patrolRadious=-1;};
		ui8 patrolling;
		ui32 patrolRadious;
		template <typename Handler> void serialize(Handler &h, const int version)
		{
			h & patrolling & patrolRadious;
		}
	} patrol;

	struct DLL_LINKAGE HeroSpecial : CBonusSystemNode
	{
		bool growthsWithLevel;
		template <typename Handler> void serialize(Handler &h, const int version)
		{
			h & static_cast<CBonusSystemNode&>(*this);
			h & growthsWithLevel;
		}
	} speciality;

	//BonusList bonuses;
	//////////////////////////////////////////////////////////////////////////


	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CArmedInstance&>(*this);
		h & static_cast<CArtifactSet&>(*this);
		h & exp & level & name & biography & portrait & mana & secSkills & movement
			& sex & inTownGarrison & /*artifacts & artifWorn & */spells & patrol & moveDir;

		h & type & speciality & commander;
		BONUS_TREE_DESERIALIZATION_FIX
		//visitied town pointer will be restored by map serialization method
	}
	//////////////////////////////////////////////////////////////////////////
// 	void getParents(TCNodes &out, const CBonusSystemNode *root = NULL) const;
// 	void getBonuses(BonusList &out, const CSelector &selector, const CBonusSystemNode *root = NULL) const;
	//////////////////////////////////////////////////////////////////////////
	int3 getSightCenter() const; //"center" tile from which the sight distance is calculated
	int getSightRadious() const; //sight distance (should be used if player-owned structure)
	//////////////////////////////////////////////////////////////////////////

	int getBoatType() const; //0 - evil (if a ship can be evil...?), 1 - good, 2 - neutral
	void getOutOffsets(std::vector<int3> &offsets) const; //offsets to obj pos when we boat can be placed

	//////////////////////////////////////////////////////////////////////////
	
	bool hasSpellbook() const;
	EAlignment::EAlignment getAlignment() const;
	const std::string &getBiography() const;
	bool needsLastStack()const;
	ui32 getTileCost(const TerrainTile &dest, const TerrainTile &from) const; //move cost - applying pathfinding skill, road and terrain modifiers. NOT includes diagonal move penalty, last move levelling
	ui32 getLowestCreatureSpeed() const;
	int3 getPosition(bool h3m = false) const; //h3m=true - returns position of hero object; h3m=false - returns position of hero 'manifestation'
	si32 manaRegain() const; //how many points of mana can hero regain "naturally" in one day
	bool canWalkOnSea() const;
	int getCurrentLuck(int stack=-1, bool town=false) const;
	int getSpellCost(const CSpell *sp) const; //do not use during battles -> bonuses from army would be ignored

	ui8 getSecSkillLevel(SecondarySkill skill) const; //0 - no skill
	void setSecSkillLevel(SecondarySkill which, int val, bool abs);// abs == 0 - changes by value; 1 - sets to value

	int maxMovePoints(bool onLand) const;
	int movementPointsAfterEmbark(int MPsBefore, int basicCost, bool disembark = false) const; 

// 	const CArtifact* getArtAtPos(ui16 pos) const; //NULL - no artifact
// 	const CArtifact * getArt(int pos) const;
// 	si32 getArtPos(int aid) const; //looks for equipped artifact with given ID and returns its slot ID or -1 if none(if more than one such artifact lower ID is returned)
// 	bool hasArt(ui32 aid) const; //checks if hero possess artifact of given id (either in backack or worn)

	//int getSpellSecLevel(int spell) const; //returns level of secondary ability (fire, water, earth, air magic) known to this hero and applicable to given spell; -1 if error
	static int3 convertPosition(int3 src, bool toh3m); //toh3m=true: manifest->h3m; toh3m=false: h3m->manifest
	double getHeroStrength() const;
	ui64 getTotalStrength() const;
	expType calculateXp(expType exp) const; //apply learning skill
	ui8 getSpellSchoolLevel(const CSpell * spell, int *outSelectedSchool = NULL) const; //returns level on which given spell would be cast by this hero (0 - none, 1 - basic etc); optionally returns number of selected school by arg - 0 - air magic, 1 - fire magic, 2 - water magic, 3 - earth magic,
	bool canCastThisSpell(const CSpell * spell) const; //determines if this hero can cast given spell; takes into account existing spell in spellbook, existing spellbook and artifact bonuses
	CStackBasicDescriptor calculateNecromancy (const BattleResult &battleResult) const;
	void showNecromancyDialog(const CStackBasicDescriptor &raisedStack) const;
	ECanDig diggingStatus() const; //0 - can dig; 1 - lack of movement; 2 - 

	//////////////////////////////////////////////////////////////////////////

	void initHero(); 
	void initHero(int SUBID); 

	void putArtifact(ui16 pos, CArtifactInstance *art);
	void putInBackpack(CArtifactInstance *art);
	void initExp();
	void initArmy(IArmyDescriptor *dst = NULL);
	//void giveArtifact (ui32 aid);
	void initHeroDefInfo();
	void pushPrimSkill(int which, int val);
	void UpdateSpeciality();
	void updateSkill(int which, int val);

	CGHeroInstance();
	virtual ~CGHeroInstance();
	//////////////////////////////////////////////////////////////////////////
	//
	ui8 bearerType() const OVERRIDE;
	//////////////////////////////////////////////////////////////////////////

	virtual CBonusSystemNode *whereShouldBeAttached(CGameState *gs) OVERRIDE;
	virtual std::string nodeName() const OVERRIDE;
	void deserializationFix();
	void setPropertyDer(ui8 what, ui32 val);//synchr
	void initObj();
	void onHeroVisit(const CGHeroInstance * h) const;
};

class DLL_LINKAGE CSpecObjInfo
{
public:
	virtual ~CSpecObjInfo(){};
	ui8 player; //owner
};

class DLL_LINKAGE CCreGenAsCastleInfo : public virtual CSpecObjInfo
{
public:
	bool asCastle;
	ui32 identifier;
	ui8 castles[2]; //allowed castles
};

class DLL_LINKAGE CCreGenLeveledInfo : public virtual CSpecObjInfo
{
public:
	ui8 minLevel, maxLevel; //minimal and maximal level of creature in dwelling: <0, 6>
};

class DLL_LINKAGE CCreGenLeveledCastleInfo : public CCreGenAsCastleInfo, public CCreGenLeveledInfo
{
};

class DLL_LINKAGE CGDwelling : public CArmedInstance
{
public:
	typedef std::vector<std::pair<ui32, std::vector<ui32> > > TCreaturesSet;

	CSpecObjInfo * info; //h3m info about dewlling
	TCreaturesSet creatures; //creatures[level] -> <vector of alternative ids (base creature and upgrades, creatures amount>

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CArmedInstance&>(*this) & creatures;
	}

	void initObj();
	void setProperty(ui8 what, ui32 val);
	void onHeroVisit(const CGHeroInstance * h) const;
	void newTurn() const;
	void heroAcceptsCreatures(const CGHeroInstance *h, ui32 answer) const;
	void fightOver(const CGHeroInstance *h, BattleResult *result) const;
	void wantsFight(const CGHeroInstance *h, ui32 answer) const;
};


class DLL_LINKAGE CGVisitableOPH : public CGObjectInstance //objects visitable only once per hero
{
public:
	std::set<si32> visitors; //ids of heroes who have visited this obj
	si8 ttype; //tree type - used only by trees of knowledge: 0 - give level for free; 1 - take 2000 gold; 2 - take 10 gems
	const std::string & getHoverText() const;

	void setPropertyDer(ui8 what, ui32 val);//synchr
	void onHeroVisit(const CGHeroInstance * h) const;
	void onNAHeroVisit(int heroID, bool alreadyVisited) const;
	void initObj();
	bool wasVisited (const CGHeroInstance * h) const;
	void treeSelected(int heroID, int resType, int resVal, expType expVal, ui32 result) const; //handle player's anwer to the Tree of Knowledge dialog
	void schoolSelected(int heroID, ui32 which) const;
	void arenaSelected(int heroID, int primSkill) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
		h & visitors & ttype;
	}
};
class DLL_LINKAGE CGTownBuilding : public IObjectInterface
{
///basic class for town structures handled as map objects
public:
	si32 ID; //from buildig list
	si32 id; //identifies its index on towns vector
	CGTownInstance *town;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & ID & id;
	}
};
class DLL_LINKAGE COPWBonus : public CGTownBuilding
{///used for OPW bonusing structures
public:
	std::set<si32> visitors;
	void setProperty(ui8 what, ui32 val);
	void onHeroVisit (const CGHeroInstance * h) const;

	COPWBonus (int index, CGTownInstance *TOWN);
	COPWBonus (){ID = 0; town = NULL;};
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGTownBuilding&>(*this);
		h & visitors;
	}
};

class DLL_LINKAGE CTownBonus : public CGTownBuilding
{
///used for one-time bonusing structures
///feel free to merge inheritance tree
public:
	std::set<si32> visitors;
	void setProperty(ui8 what, ui32 val);
	void onHeroVisit (const CGHeroInstance * h) const;

	CTownBonus (int index, CGTownInstance *TOWN);
	CTownBonus (){ID = 0; town = NULL;};
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGTownBuilding&>(*this);
		h & visitors;
	}
};

class DLL_LINKAGE CTownAndVisitingHero : public CBonusSystemNode
{
public:
	CTownAndVisitingHero();
};

struct DLL_LINKAGE GrowthInfo
{
	struct Entry
	{
		int count;
		std::string description;
		Entry(const std::string &format, int _count);
		Entry(int subID, EBuilding::EBuilding building, int _count);
	};

	std::vector<Entry> entries;
	int totalGrowth() const;
};

class DLL_LINKAGE CGTownInstance : public CGDwelling, public IShipyard, public IMarket
{
public:
	enum EFortLevel {NONE = 0, FORT = 1, CITADEL = 2, CASTLE = 3};

	CTownAndVisitingHero townAndVis;
	CTown * town;
	std::string name; // name of town
	si32 builded; //how many buildings has been built this turn
	si32 destroyed; //how many buildings has been destroyed this turn
	ConstTransitivePtr<CGHeroInstance> garrisonHero, visitingHero;
	ui32 identifier; //special identifier from h3m (only > RoE maps)
	si32 alignment;
	std::set<si32> forbiddenBuildings, builtBuildings;
	std::vector<CGTownBuilding*> bonusingBuildings;
	std::vector<ui32> possibleSpells, obligatorySpells;
	std::vector<std::vector<ui32> > spells; //spells[level] -> vector of spells, first will be available in guild
	std::list<CCastleEvent*> events;
	std::pair<si32, si32> bonusValue;//var to store town bonuses (rampart = resources from mystic pond);

	//////////////////////////////////////////////////////////////////////////
	static std::vector<const CArtifact *> merchantArtifacts; //vector of artifacts available at Artifact merchant, NULLs possible (for making empty space when artifact is bought)
	static std::vector<int> universitySkills;//skills for university of magic

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGDwelling&>(*this);
		h & name & builded & destroyed & identifier & alignment & forbiddenBuildings & builtBuildings & bonusValue
			& possibleSpells & obligatorySpells & spells & /*strInfo & */events & bonusingBuildings;

		for (std::vector<CGTownBuilding*>::iterator i = bonusingBuildings.begin(); i!=bonusingBuildings.end(); i++)
			(*i)->town = this;

		h & town & townAndVis;
		BONUS_TREE_DESERIALIZATION_FIX
		//garrison/visiting hero pointers will be restored in the map serialization
	}
	//////////////////////////////////////////////////////////////////////////

	virtual CBonusSystemNode *whatShouldBeAttached() OVERRIDE;
	std::string nodeName() const OVERRIDE;
	void deserializationFix();
	void recreateBuildingsBonuses();
	bool addBonusIfBuilt(int building, int type, int val, TPropagatorPtr prop, int subtype = -1); //returns true if building is built and bonus has been added
	bool addBonusIfBuilt(int building, int type, int val, int subtype = -1); //convienence version of above
	void setVisitingHero(CGHeroInstance *h);
	void setGarrisonedHero(CGHeroInstance *h);
	const CArmedInstance *getUpperArmy() const; //garrisoned hero if present or the town itself
// 	void getParents(TCNodes &out, const CBonusSystemNode *root = NULL) const;
// 	void getBonuses(BonusList &out, const CSelector &selector, const CBonusSystemNode *root = NULL) const;
	//////////////////////////////////////////////////////////////////////////

	ui8 getPassableness() const; //bitmap - if the bit is set the corresponding player can pass through the visitable tiles of object, even if it's blockvis; if not set - default properties from definfo are used
	int3 getSightCenter() const; //"center" tile from which the sight distance is calculated
	int getSightRadious() const; //returns sight distance
	int getBoatType() const; //0 - evil (if a ship can be evil...?), 1 - good, 2 - neutral
	void getOutOffsets(std::vector<int3> &offsets) const; //offsets to obj pos when we boat can be placed
	int getMarketEfficiency() const; //=market count
	bool allowsTrade(EMarketMode::EMarketMode mode) const;
	std::vector<int> availableItemsIds(EMarketMode::EMarketMode mode) const;
	void setPropertyDer(ui8 what, ui32 val);
	void newTurn() const;

	//////////////////////////////////////////////////////////////////////////

	bool needsLastStack() const;
	CGTownInstance::EFortLevel fortLevel() const;
	int hallLevel() const; // -1 - none, 0 - village, 1 - town, 2 - city, 3 - capitol
	int mageGuildLevel() const; // -1 - none, 0 - village, 1 - town, 2 - city, 3 - capitol
	bool creatureDwelling(const int & level, bool upgraded=false) const;
	int getHordeLevel(const int & HID) const; //HID - 0 or 1; returns creature level or -1 if that horde structure is not present
	int creatureGrowth(const int & level) const;
	GrowthInfo getGrowthInfo(int level) const;
	bool hasFort() const;
	bool hasCapitol() const;
	bool hasBuilt(int buildingID) const;
	int dailyIncome() const; //calculates daily income of this town
	int spellsAtLevel(int level, bool checkGuild) const; //levels are counted from 1 (1 - 5)
	void removeCapitols (ui8 owner) const;
	bool armedGarrison() const; //true if town has creatures in garrison or garrisoned hero

	CGTownInstance();
	virtual ~CGTownInstance();

	//////////////////////////////////////////////////////////////////////////


	void fightOver(const CGHeroInstance *h, BattleResult *result) const;
	void onHeroVisit(const CGHeroInstance * h) const;
	void onHeroLeave(const CGHeroInstance * h) const;
	void initObj();
};
class DLL_LINKAGE CGPandoraBox : public CArmedInstance
{
public:
	std::string message;

	//gained things:
	ui32 gainedExp;
	si32 manaDiff; //amount of gained / lost mana
	si32 moraleDiff; //morale modifier
	si32 luckDiff; //luck modifier
	std::vector<si32> resources;//gained / lost resources
	std::vector<si32> primskills;//gained / lost resources
	std::vector<si32> abilities; //gained abilities
	std::vector<si32> abilityLevels; //levels of gained abilities
	std::vector<si32> artifacts; //gained artifacts
	std::vector<si32> spells; //gained spells
	CCreatureSet creatures; //gained creatures

	void initObj();
	void onHeroVisit(const CGHeroInstance * h) const;
	void open (const CGHeroInstance * h, ui32 accept) const;
	void endBattle(const CGHeroInstance *h, BattleResult *result) const;
	void giveContents(const CGHeroInstance *h, bool afterBattle) const;
	void getText( InfoWindow &iw, bool &afterBattle, int val, int negative, int positive, const CGHeroInstance * h ) const;
	void getText( InfoWindow &iw, bool &afterBattle, int text, const CGHeroInstance * h ) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CArmedInstance&>(*this);
		h & message & gainedExp & manaDiff & moraleDiff & luckDiff & resources & primskills
			& abilities & abilityLevels & artifacts & spells & creatures;
	}
};

class DLL_LINKAGE CGEvent : public CGPandoraBox  //event objects
{
public:
	ui8 removeAfterVisit; //true if event is removed after occurring
	ui8 availableFor; //players whom this event is available for
	ui8 computerActivate; //true if computre player can activate this event
	ui8 humanActivate; //true if human player can activate this event

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGPandoraBox &>(*this);
		h & removeAfterVisit & availableFor & computerActivate & humanActivate;
	}
	
	void onHeroVisit(const CGHeroInstance * h) const;
	void activated(const CGHeroInstance * h) const;

};

class DLL_LINKAGE CGCreature : public CArmedInstance //creatures on map
{
public:
	ui32 identifier; //unique code for this monster (used in missions)
	si8 character; //character of this set of creatures (0 - the most friendly, 4 - the most hostile) => on init changed to -4 (compliant) ... 10 value (savage)
	std::string message; //message printed for attacking hero
	std::vector<ui32> resources; //[res_id], resources given to hero that has won with monsters
	si32 gainedArtifact; //ID of artifact gained to hero, -1 if none
	ui8 neverFlees; //if true, the troops will never flee
	ui8 notGrowingTeam; //if true, number of units won't grow
	ui64 temppower; //used to handle fractional stack growth for tiny stacks

	void fight(const CGHeroInstance *h) const;
	void onHeroVisit(const CGHeroInstance * h) const;
	const std::string & getHoverText() const;

	void flee( const CGHeroInstance * h ) const;
	void endBattle(BattleResult *result) const;
	void fleeDecision(const CGHeroInstance *h, ui32 pursue) const;
	void joinDecision(const CGHeroInstance *h, int cost, ui32 accept) const;
	void initObj();
	void newTurn() const;
	void setPropertyDer(ui8 what, ui32 val);
	int takenAction(const CGHeroInstance *h, bool allowJoin=true) const; //action on confrontation: -2 - fight, -1 - flee, >=0 - will join for given value of gold (may be 0)

	struct DLL_LINKAGE RestoredCreature // info about merging stacks after battle back into one
	{
		si32 basicType;
		template <typename Handler> void serialize(Handler &h, const int version)
		{
			h & basicType;
		}
	} restore;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CArmedInstance&>(*this);
		h & identifier & character & message & resources & gainedArtifact & neverFlees & notGrowingTeam & temppower & restore;
	}
}; 


class DLL_LINKAGE CGSignBottle : public CGObjectInstance //signs and ocean bottles
{
public:
	std::string message;

	void onHeroVisit(const CGHeroInstance * h) const;
	void initObj();

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
		h & message;
	}
};

class DLL_LINKAGE CGSeerHut : public CArmedInstance, public CQuest //army is used when giving reward
{
public:
	ui8 rewardType; //type of reward: 0 - no reward; 1 - experience; 2 - mana points; 3 - morale bonus; 4 - luck bonus; 5 - resources; 6 - main ability bonus (attak, defence etd.); 7 - secondary ability gain; 8 - artifact; 9 - spell; 10 - creature
	si32 rID; //reward ID
	si32 rVal; //reward value
	std::string seerName;

	void initObj();
	const std::string & getHoverText() const;
	void setPropertyDer (ui8 what, ui32 val);
	int checkDirection() const; //calculates the region of map where monster is placed
	void newTurn() const;
	void onHeroVisit (const CGHeroInstance * h) const;
	void getRolloverText (MetaString &text, bool onHover) const;
	void getCompletionText(MetaString &text, std::vector<Component> &components, bool isCustom, const CGHeroInstance * h = NULL) const;
	void finishQuest (const CGHeroInstance * h, ui32 accept) const; //common for both objects
	void completeQuest (const CGHeroInstance * h) const;

	void setObjToKill(); //remember creatures / heroes to kill after they are initialized
	const CGHeroInstance *getHeroToKill(bool allowNull = false) const;
	const CGCreature *getCreatureToKill(bool allowNull = false) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this) & static_cast<CQuest&>(*this);
		h & rewardType & rID & rVal & textOption & seerName;
	}
};

class DLL_LINKAGE CGQuestGuard : public CGSeerHut
{
public:
	void initObj();
	void completeQuest (const CGHeroInstance * h) const;
 
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGSeerHut&>(*this);
	}
};

class DLL_LINKAGE CGWitchHut : public CPlayersVisited
{
public:
	std::vector<si32> allowedAbilities;
	ui32 ability;

	const std::string & getHoverText() const;
	void onHeroVisit(const CGHeroInstance * h) const;
	void initObj();
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CPlayersVisited&>(*this);
		h & allowedAbilities & ability;
	}
};


class DLL_LINKAGE CGScholar : public CGObjectInstance
{
public:
	ui8 bonusType; //255 - random, 0 - primary skill, 1 - secondary skill, 2 - spell
	ui16 bonusID; //ID of skill/spell

	void giveAnyBonus(const CGHeroInstance * h) const;
	void onHeroVisit(const CGHeroInstance * h) const;
	void initObj();
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
		h & bonusType & bonusID;
	}
};

class DLL_LINKAGE CGGarrison : public CArmedInstance
{
public:
	ui8 removableUnits;

	ui8 getPassableness() const;
	void onHeroVisit (const CGHeroInstance *h) const;
	void fightOver (const CGHeroInstance *h, BattleResult *result) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CArmedInstance&>(*this);
		h & removableUnits;
	}
};

class DLL_LINKAGE CGArtifact : public CArmedInstance
{
public:
	CArtifactInstance *storedArtifact;
	std::string message;

	void onHeroVisit(const CGHeroInstance * h) const;
	void fightForArt(ui32 agreed, const CGHeroInstance *h) const;
	void endBattle(BattleResult *result, const CGHeroInstance *h) const;
	void pick( const CGHeroInstance * h ) const;
	void initObj();	

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CArmedInstance&>(*this);
		h & message & storedArtifact;
	}
};

class DLL_LINKAGE CGResource : public CArmedInstance
{
public:
	ui32 amount; //0 if random
	std::string message;

	void onHeroVisit(const CGHeroInstance * h) const;
	void collectRes(int player) const;
	void initObj();
	void fightForRes(ui32 agreed, const CGHeroInstance *h) const;
	void endBattle(BattleResult *result, const CGHeroInstance *h) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CArmedInstance&>(*this);
		h & amount & message;
	}
};

class DLL_LINKAGE CGPickable : public CGObjectInstance //campfire, treasure chest, Flotsam, Shipwreck Survivor, Sea Chest
{
public:
	ui32 type, val1, val2;

	void onHeroVisit(const CGHeroInstance * h) const;
	void initObj();
	void chosen(int which, int heroID) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
		h & type & val1 & val2;
	}
};

class DLL_LINKAGE CGShrine : public CPlayersVisited
{
public:
	ui8 spell; //number of spell or 255 if random
	void onHeroVisit(const CGHeroInstance * h) const;
	void initObj();
	const std::string & getHoverText() const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CPlayersVisited&>(*this);;
		h & spell;
	}
};

class DLL_LINKAGE CGMine : public CArmedInstance
{
public: 
	ui8 producedResource;
	ui32 producedQuantity;

	void offerLeavingGuards(const CGHeroInstance *h) const;
	void endBattle(BattleResult *result, ui8 attackingPlayer) const;
	void fight(ui32 agreed, const CGHeroInstance *h) const;
	
	void onHeroVisit(const CGHeroInstance * h) const;

	void flagMine(ui8 player) const;
	void newTurn() const;
	void initObj();	
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CArmedInstance&>(*this);
		h & producedResource & producedQuantity;
	}
	ui32 defaultResProduction();
};

class DLL_LINKAGE CGVisitableOPW : public CGObjectInstance //objects visitable OPW
{
public:
	ui8 visited; //true if object has been visited this week

	void setPropertyDer(ui8 what, ui32 val);//synchr
	bool wasVisited(ui8 player) const;
	void onHeroVisit(const CGHeroInstance * h) const;
	void newTurn() const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
		h & visited;
	}
};

class DLL_LINKAGE CGTeleport : public CGObjectInstance //teleports and subterranean gates
{
public:
	static std::map<int,std::map<int, std::vector<int> > > objs; //teleports: map[ID][subID] => vector of ids
	static std::vector<std::pair<int, int> > gates; //subterranean gates: pairs of ids
	void onHeroVisit(const CGHeroInstance * h) const;
	void initObj();	
	static void postInit();
	static int getMatchingGate(int id); //receives id of one subterranean gate and returns id of the paired one, -1 if none

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
};

class DLL_LINKAGE CGBonusingObject : public CGObjectInstance //objects giving bonuses to luck/morale/movement
{
public:
	bool wasVisited (const CGHeroInstance * h) const;
	void onHeroVisit(const CGHeroInstance * h) const;
	const std::string & getHoverText() const;
	void initObj();	

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
};

class DLL_LINKAGE CGMagicSpring : public CGVisitableOPW 
{///unfortunatelly, this one is quite different than others 
public: 
	void onHeroVisit(const CGHeroInstance * h) const; 
	const std::string & getHoverText() const; 

	template <typename Handler> void serialize(Handler &h, const int version) 
	{ 
		h & static_cast<CGObjectInstance&>(*this); 
		h & visited; 
	} 
}; 

class DLL_LINKAGE CGMagicWell : public CGObjectInstance //objects giving bonuses to luck/morale/movement
{
public:
	void onHeroVisit(const CGHeroInstance * h) const;
	const std::string & getHoverText() const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
};

class DLL_LINKAGE CGSirens : public CGObjectInstance
{
public:
	void onHeroVisit(const CGHeroInstance * h) const;
	const std::string & getHoverText() const;
	void initObj();	

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
};

class DLL_LINKAGE CGObservatory : public CGObjectInstance //Redwood observatory
{
public:
	void onHeroVisit(const CGHeroInstance * h) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
};


class DLL_LINKAGE CGKeys : public CGObjectInstance //Base class for Keymaster and guards
{	
public:
	static std::map <ui8, std::set <ui8> > playerKeyMap; //[players][keysowned]
	//SubID 0 - lightblue, 1 - green, 2 - red, 3 - darkblue, 4 - brown, 5 - purple, 6 - white, 7 - black

	void setPropertyDer (ui8 what, ui32 val);
	const std::string getName() const; //depending on color
	bool wasMyColorVisited (int player) const;
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
};

class DLL_LINKAGE CGKeymasterTent : public CGKeys
{
public:
	bool wasVisited (ui8 player) const;
	void onHeroVisit(const CGHeroInstance * h) const;
	const std::string & getHoverText() const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
};

class DLL_LINKAGE CGBorderGuard : public CGKeys, public CQuest
{
public:
	void initObj();
	const std::string & getHoverText() const;
	void getVisitText (MetaString &text, std::vector<Component> &components, bool isCustom, bool FirstVisit, const CGHeroInstance * h = NULL) const;
	void getRolloverText (MetaString &text, bool onHover) const;
	bool checkQuest (const CGHeroInstance * h) const;
	void onHeroVisit(const CGHeroInstance * h) const;
	void openGate(const CGHeroInstance *h, ui32 accept) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CQuest&>(*this);
		h & static_cast<CGObjectInstance&>(*this);
		h & blockVisit;
	}
};

class DLL_LINKAGE CGBorderGate : public CGBorderGuard //not fully imlemented, waiting for garrison
{
public:
	void onHeroVisit(const CGHeroInstance * h) const;
	ui8 getPassableness() const;
};

class DLL_LINKAGE CGBoat : public CGObjectInstance 
{
public:
	ui8 direction;
	const CGHeroInstance *hero;  //hero on board

	void initObj();	

	CGBoat()
	{
		hero = NULL;
		direction = 4;
	}
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this) & direction;
	}
};

class DLL_LINKAGE CGOnceVisitable : public CPlayersVisited
///wagon, corpse, lean to, warriors tomb
{
public:
	ui8 artOrRes; //0 - nothing; 1 - artifact; 2 - resource
	ui32 bonusType, //id of res or artifact
		bonusVal; //resource amount (or not used)

	void onHeroVisit(const CGHeroInstance * h) const;
	const std::string & getHoverText() const;
	void initObj();	
	void searchTomb(const CGHeroInstance *h, ui32 accept) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CPlayersVisited&>(*this);;
		h & artOrRes & bonusType & bonusVal;
	}
};

class DLL_LINKAGE CBank : public CArmedInstance
{
	public:
	int index; //banks have unusal numbering - see ZCRBANK.txt and initObj()
	BankConfig *bc;
	double multiplier; //for improved banks script
	std::vector<ui32> artifacts; //fixed and deterministic
	ui32 daycounter;

	void initObj();
	const std::string & getHoverText() const;
	void setPropertyDer (ui8 what, ui32 val);
	void initialize() const;
	void reset(ui16 var1);
	void newTurn() const;
	bool wasVisited (ui8 player) const;
	virtual void onHeroVisit (const CGHeroInstance * h) const;
	virtual void fightGuards (const CGHeroInstance *h, ui32 accept) const;
	virtual void endBattle (const CGHeroInstance *h, const BattleResult *result) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CArmedInstance&>(*this);
		h & index & multiplier & artifacts & daycounter & bc;
	}
};
class DLL_LINKAGE CGPyramid : public CBank
{
public:
	ui16 spell;

	void initObj();
	const std::string & getHoverText() const;
	void newTurn() const {}; //empty, no reset
	void onHeroVisit (const CGHeroInstance * h) const;
	void endBattle (const CGHeroInstance *h, const BattleResult *result) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CBank&>(*this);
		h & spell;
	}
};

class CGShipyard : public CGObjectInstance, public IShipyard
{
public:
	void getOutOffsets(std::vector<int3> &offsets) const; //offsets to obj pos when we boat can be placed
	CGShipyard();
	void onHeroVisit(const CGHeroInstance * h) const;
};

class DLL_LINKAGE CGMagi : public CGObjectInstance
{
public:
	static std::map <si32, std::vector<si32> > eyelist; //[subID][id], supports multiple sets as in H5

	void initObj();
	void onHeroVisit(const CGHeroInstance * h) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
};



class DLL_LINKAGE CCartographer : public CPlayersVisited
{
///behaviour varies depending on surface and  floor 
public:
	void onHeroVisit( const CGHeroInstance * h ) const;
	void buyMap (const CGHeroInstance *h, ui32 accept) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CPlayersVisited&>(*this);
	}
};

class DLL_LINKAGE CGDenOfthieves : public CGObjectInstance
{
	void onHeroVisit (const CGHeroInstance * h) const;
};

class DLL_LINKAGE CGObelisk : public CPlayersVisited
{
public:
	static ui8 obeliskCount; //how many obelisks are on map
	static std::map<ui8, ui8> visited; //map: team_id => how many obelisks has been visited

	void setPropertyDer (ui8 what, ui32 val);
	void onHeroVisit(const CGHeroInstance * h) const;
	void initObj();
	const std::string & getHoverText() const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CPlayersVisited&>(*this);
	}
};

class DLL_LINKAGE CGLighthouse : public CGObjectInstance
{
public:
	void onHeroVisit(const CGHeroInstance * h) const;
	void initObj();
	const std::string & getHoverText() const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
	void giveBonusTo( ui8 player ) const;
};

class DLL_LINKAGE CGMarket : public CGObjectInstance, public IMarket
{
public:
	CGMarket();
	void onHeroVisit(const CGHeroInstance * h) const; //open trading window
	
	int getMarketEfficiency() const;
	bool allowsTrade(EMarketMode::EMarketMode mode) const;
	int availableUnits(EMarketMode::EMarketMode mode, int marketItemSerial) const; //-1 if unlimited
	std::vector<int> availableItemsIds(EMarketMode::EMarketMode mode) const;
	
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGObjectInstance&>(*this);
	}
};

class DLL_LINKAGE CGBlackMarket : public CGMarket
{
public:
	std::vector<const CArtifact *> artifacts; //available artifacts

	void newTurn() const; //reset artifacts for black market every month
	std::vector<int> availableItemsIds(EMarketMode::EMarketMode mode) const;
	
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGMarket&>(*this);
		h & artifacts;
	}
};

class DLL_LINKAGE CGUniversity : public CGMarket
{
public:
	std::vector<int> skills; //available skills
	
	std::vector<int> availableItemsIds(EMarketMode::EMarketMode mode) const;
	void initObj();//set skills for trade
	void onHeroVisit(const CGHeroInstance * h) const; //open window
	
	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CGMarket&>(*this);
		h & skills;
	}
};

struct BankConfig
{
	BankConfig() {level = chance = upgradeChance = combatValue = value = rewardDifficulty = easiest = 0; };
	ui8 level; //1 - 4, how hard the battle will be
	ui8 chance; //chance for this level being chosen
	ui8 upgradeChance; //chance for creatures to be in upgraded versions
	std::vector< std::pair <ui16, ui32> > guards; //creature ID, amount
	ui32 combatValue; //how hard are guards of this level
	std::vector<si32> resources; //resources given in case of victory
	std::vector< std::pair <ui16, ui32> > creatures; //creatures granted in case of victory (creature ID, amount)
	std::vector<ui16> artifacts; //number of artifacts given in case of victory [0] -> treasure, [1] -> minor [2] -> major [3] -> relic
	ui32 value; //overall value of given things
	ui32 rewardDifficulty; //proportion of reward value to difficulty of guards; how profitable is this creature Bank config
	ui16 easiest; //?!?

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & level & chance & upgradeChance & guards & combatValue & resources & creatures & artifacts & value & rewardDifficulty & easiest;
	}
};

class DLL_LINKAGE CObjectHandler
{
public:
	std::map<si32, si32> cregens; //type 17. dwelling subid -> creature ID
	std::map <ui32, std::vector < ConstTransitivePtr<BankConfig> > > banksInfo; //[index][preset]
	std::map <ui32, std::string> creBanksNames; //[crebank index] -> name of this creature bank
	std::vector<ui32> resVals; //default values of resources in gold

	void loadObjects();
	int bankObjToIndex (const CGObjectInstance * obj);

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & cregens & banksInfo & creBanksNames & resVals;
	}
};
