#pragma once


#include "BattleHex.h"
#include "../client/FunctionList.h"
#include "CObstacleInstance.h"
#include "ResourceSet.h"
#include "int3.h"
#include "GameConstants.h"

/*
 * IGameCallback.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

struct SetMovePoints;
struct GiveBonus;
class CGObjectInstance;
class CGTownInstance;
class CGHeroInstance;
struct BlockingDialog;
struct InfoWindow;
struct MetaString;
struct ShowInInfobox;
struct BattleResult;
struct Component;
class CGameState;
struct PlayerSettings;
struct CPackForClient;
class CArtHandler;
class CArtifact;
class CArmedInstance;
struct TerrainTile;
struct PlayerState;
class CTown;
struct StackLocation;
struct ArtifactLocation;
class CArtifactInstance;
struct StartInfo;
struct InfoAboutTown;
struct UpgradeInfo;
struct SThievesGuildInfo;
struct CPath;
class CGDwelling;
struct InfoAboutHero;
class CMapHeader;
struct BattleAction;
class CStack;
class CSpell;
class CCreatureSet;
class CCreature;
class CStackBasicDescriptor;
struct TeamState;
struct QuestInfo;
class CGCreature;

typedef std::vector<const CStack*> TStacks;

namespace boost
{class shared_mutex;}

class DLL_LINKAGE CCallbackBase
{
protected:
	CGameState *gs;
	int player; // -1 gives access to all information, otherwise limited to knowledge of given player

	CCallbackBase(CGameState *GS, int Player)
		: gs(GS), player(Player)
	{}
	CCallbackBase()
	: gs(NULL), player(-1)
	{}

public:
	boost::shared_mutex &getGsMutex(); //just return a reference to mutex, does not lock nor anything
};

class DLL_LINKAGE CBattleInfoCallback : public virtual CCallbackBase
{
public:
	enum EStackOwnership
	{
		ONLY_MINE, ONLY_ENEMY, MINE_AND_ENEMY
	};
	enum ERandomSpell
	{
		RANDOM_GENIE, RANDOM_AIMED
	};

	//battle
	int battleGetBattlefieldType(); //   1. sand/shore   2. sand/mesas   3. dirt/birches   4. dirt/hills   5. dirt/pines   6. grass/hills   7. grass/pines   8. lava   9. magic plains   10. snow/mountains   11. snow/trees   12. subterranean   13. swamp/trees   14. fiery fields   15. rock lands   16. magic clouds   17. lucid pools   18. holy ground   19. clover field   20. evil fog   21. "favourable winds" text on magic plains background   22. cursed ground   23. rough   24. ship to ship   25. ship
	//int battleGetObstaclesAtTile(BattleHex tile); //returns bitfield
	shared_ptr<const CObstacleInstance> battleGetObstacleOnPos(BattleHex tile, bool onlyBlocking = true);
	std::vector<shared_ptr<const CObstacleInstance> > battleGetAllObstacles(); //returns all obstacles on the battlefield
	const CStack * battleGetStackByID(int ID, bool onlyAlive = true); //returns stack info by given ID
	const CStack * battleGetStackByPos(BattleHex pos, bool onlyAlive = true); //returns stack info by given pos
	BattleHex battleGetPos(int stack); //returns position (tile ID) of stack
	TStacks battleGetStacks(EStackOwnership whose = MINE_AND_ENEMY, bool onlyAlive = true); //returns stacks on battlefield
	void getStackQueue( std::vector<const CStack *> &out, int howMany ); //returns vector of stack in order of their move sequence
    void battleGetStackCountOutsideHexes(bool *ac); // returns hexes which when in front of a stack cause us to move the amount box back
	std::vector<BattleHex> battleGetAvailableHexes(const CStack * stack, bool addOccupiable, std::vector<BattleHex> * attackable = NULL); //returns numbers of hexes reachable by creature with id ID
	std::vector<int> battleGetDistances(const CStack * stack, BattleHex hex = BattleHex::INVALID, BattleHex * predecessors = NULL); //returns vector of distances to [dest hex number]
	std::set<BattleHex> battleGetAttackedHexes(const CStack* attacker, BattleHex destinationTile, BattleHex attackerPos = BattleHex::INVALID);
	bool battleCanShoot(const CStack * stack, BattleHex dest); //returns true if unit with id ID can shoot to dest
	bool battleCanCastSpell(); //returns true, if caller can cast a spell
	ESpellCastProblem::ESpellCastProblem battleCanCastThisSpell(const CSpell * spell); //determines if given spell can be casted (and returns problem description)
	ESpellCastProblem::ESpellCastProblem battleCanCastThisSpell(const CSpell * spell, BattleHex destination); //if hero can cast spell here
	ESpellCastProblem::ESpellCastProblem battleCanCreatureCastThisSpell(const CSpell * spell, BattleHex destination); //determines if creature can cast a spell here
	si32 battleGetRandomStackSpell(const CStack * stack, ERandomSpell mode);
	bool battleCanFlee(); //returns true if caller can flee from the battle
	int battleGetSurrenderCost(); //returns cost of surrendering battle, -1 if surrendering is not possible
	const CGTownInstance * battleGetDefendedTown(); //returns defended town if current battle is a siege, NULL instead
	ui8 battleGetWallState(int partOfWall); //for determining state of a part of the wall; format: parameter [0] - keep, [1] - bottom tower, [2] - bottom wall, [3] - below gate, [4] - over gate, [5] - upper wall, [6] - uppert tower, [7] - gate; returned value: 1 - intact, 2 - damaged, 3 - destroyed; 0 - no battle
	int battleGetWallUnderHex(BattleHex hex); //returns part of destructible wall / gate / keep under given hex or -1 if not found
	std::pair<ui32, ui32> battleEstimateDamage(const CStack * attacker, const CStack * defender, std::pair<ui32, ui32> * retaliationDmg = NULL); //estimates damage dealt by attacker to defender; it may be not precise especially when stack has randomly working bonuses; returns pair <min dmg, max dmg>
	ui8 battleGetSiegeLevel(); //returns 0 when there is no siege, 1 if fort, 2 is citadel, 3 is castle
	const CGHeroInstance * battleGetFightingHero(ui8 side) const; //returns hero corresponding to given side (0 - attacker, 1 - defender)
	si8 battleHasDistancePenalty(const CStack * stack, BattleHex destHex); //checks if given stack has distance penalty
	si8 battleHasWallPenalty(const CStack * stack, BattleHex destHex); //checks if given stack has wall penalty
	si8 battleHasShootingPenalty(const CStack * stack, BattleHex destHex)
	{
		return battleHasDistancePenalty(stack, destHex) || battleHasWallPenalty(stack, destHex);
	}
	si8 battleCanTeleportTo(const CStack * stack, BattleHex destHex, int telportLevel); //checks if teleportation of given stack to given position can take place
	si8 battleGetTacticDist(); //returns tactic distance for calling player or 0 if player is not in tactic phase
	ui8 battleGetMySide(); //return side of player in battle (attacker/defender)
	int battleGetMoatDmg(); //what dmg unit will suffer if ending turn in the moat

	//convenience methods using the ones above
	TStacks battleGetAllStacks() //returns all stacks, alive or dead or undead or mechanical :)
	{
		return battleGetStacks(MINE_AND_ENEMY, false);
	}
};

class DLL_LINKAGE CGameInfoCallback : public virtual CCallbackBase
{
protected:
	CGameInfoCallback();
	CGameInfoCallback(CGameState *GS, int Player);
	bool hasAccess(int playerId) const;
	bool isVisible(int3 pos, int Player) const;
	bool isVisible(const CGObjectInstance *obj, int Player) const;
	bool isVisible(const CGObjectInstance *obj) const;

	bool canGetFullInfo(const CGObjectInstance *obj) const; //true we player owns obj or ally owns obj or privileged mode
	bool isOwnedOrVisited(const CGObjectInstance *obj) const;

public:
	//various
	int getDate(int mode=0)const; //mode=0 - total days in game, mode=1 - day of week, mode=2 - current week, mode=3 - current month
	const StartInfo * getStartInfo(bool beforeRandomization = false)const;
	bool isAllowed(int type, int id); //type: 0 - spell; 1- artifact; 2 - secondary skill

	//player
	const PlayerState * getPlayer(int color, bool verbose = true) const;
	int getResource(int Player, int which) const;
	bool isVisible(int3 pos) const;
	int getPlayerRelations(ui8 color1, ui8 color2) const;// 0 = enemy, 1 = ally, 2 = same player 
	void getThievesGuildInfo(SThievesGuildInfo & thi, const CGObjectInstance * obj); //get thieves' guild info obtainable while visiting given object
	int getPlayerStatus(int player) const; //-1 if no such player
	int getCurrentPlayer() const; //player that currently makes move // TODO synchronous turns
	virtual int getLocalPlayer() const; //player that is currently owning given client (if not a client, then returns current player)
	const PlayerSettings * getPlayerSettings(int color) const;


	//armed object
	void getUpgradeInfo(const CArmedInstance *obj, int stackPos, UpgradeInfo &out)const;

	//hero
	const CGHeroInstance* getHero(int objid) const;
	const CGHeroInstance* getHeroWithSubid(int subid) const;
	int getHeroCount(int player, bool includeGarrisoned) const;
	bool getHeroInfo(const CGObjectInstance *hero, InfoAboutHero &dest) const;
	int getSpellCost(const CSpell * sp, const CGHeroInstance * caster) const; //when called during battle, takes into account creatures' spell cost reduction
	int estimateSpellDamage(const CSpell * sp, const CGHeroInstance * hero) const; //estimates damage of given spell; returns 0 if spell causes no dmg
	bool verifyPath(CPath * path, bool blockSea)const;
	const CGHeroInstance* getSelectedHero(int player) const; //NULL if no hero is selected
	const CGHeroInstance* getSelectedHero() const; //of current (active) player

	//objects
	const CGObjectInstance* getObj(int objid, bool verbose = true) const;
	std::vector <const CGObjectInstance * > getBlockingObjs(int3 pos)const;
	std::vector <const CGObjectInstance * > getVisitableObjs(int3 pos, bool verbose = true)const;
	std::vector <const CGObjectInstance * > getFlaggableObjects(int3 pos) const;
	std::vector <std::string > getObjDescriptions(int3 pos)const; //returns descriptions of objects at pos in order from the lowest to the highest
	int getOwner(int heroID) const;
	const CGObjectInstance *getObjByQuestIdentifier(int identifier) const; //NULL if object has been removed (eg. killed)

	//map
	int3 guardingCreaturePosition (int3 pos) const;
	const CMapHeader * getMapHeader()const;
	int3 getMapSize() const; //returns size of map - z is 1 for one - level map and 2 for two level map
	const TerrainTile * getTile(int3 tile, bool verbose = true) const;
	bool isInTheMap(const int3 &pos) const;

	//town
	const CGTownInstance* getTown(int objid) const;
	int howManyTowns(int Player) const;
	const CGTownInstance * getTownInfo(int val, bool mode)const; //mode = 0 -> val = player town serial; mode = 1 -> val = object id (serial)
	std::vector<const CGHeroInstance *> getAvailableHeroes(const CGObjectInstance * townOrTavern) const; //heroes that can be recruited
	std::string getTavernGossip(const CGObjectInstance * townOrTavern) const; 
	int canBuildStructure(const CGTownInstance *t, int ID);//// 0 - no more than one capitol, 1 - lack of water, 2 - forbidden, 3 - Add another level to Mage Guild, 4 - already built, 5 - cannot build, 6 - cannot afford, 7 - build, 8 - lack of requirements
	std::set<int> getBuildingRequiments(const CGTownInstance *t, int ID);
	virtual bool getTownInfo(const CGObjectInstance *town, InfoAboutTown &dest) const;
	const CTown *getNativeTown(int color) const;

	//from gs
	const TeamState *getTeam(ui8 teamID) const;
	const TeamState *getPlayerTeam(ui8 color) const;
	std::set<int> getBuildingRequiments(const CGTownInstance *t, int ID) const;
	int canBuildStructure(const CGTownInstance *t, int ID) const;// 0 - no more than one capitol, 1 - lack of water, 2 - forbidden, 3 - Add another level to Mage Guild, 4 - already built, 5 - cannot build, 6 - cannot afford, 7 - build, 8 - lack of requirements
};


class DLL_LINKAGE CPlayerSpecificInfoCallback : public CGameInfoCallback
{
public:
	int howManyTowns() const;
	int howManyHeroes(bool includeGarrisoned = true) const;
	int3 getGrailPos(double &outKnownRatio);
	int getMyColor() const;

	std::vector <const CGTownInstance *> getTownsInfo(bool onlyOur = true) const; //true -> only owned; false -> all visible
	int getHeroSerial(const CGHeroInstance * hero, bool includeGarrisoned=true) const;
	const CGTownInstance* getTownBySerial(int serialId) const; // serial id is [0, number of towns)
	const CGHeroInstance* getHeroBySerial(int serialId, bool includeGarrisoned=true) const; // serial id is [0, number of heroes)
	std::vector <const CGHeroInstance *> getHeroesInfo(bool onlyOur = true) const; //true -> only owned; false -> all visible
	std::vector <const CGDwelling *> getMyDwellings() const; //returns all dwellings that belong to player
	std::vector <const CGObjectInstance * > getMyObjects() const; //returns all objects flagged by belonging player
	std::vector <QuestInfo> getMyQuests() const;

	int getResourceAmount(int type)const;
	TResources getResourceAmount() const;
	const std::vector< std::vector< std::vector<ui8> > > & getVisibilityMap()const; //returns visibility map 
	const PlayerSettings * getPlayerSettings(int color) const;
};

class DLL_LINKAGE CPrivilagedInfoCallback : public CGameInfoCallback
{
public:
	CGameState * gameState ();
	void getFreeTiles (std::vector<int3> &tiles) const; //used for random spawns
	void getTilesInRange(boost::unordered_set<int3, ShashInt3> &tiles, int3 pos, int radious, int player=-1, int mode=0) const;  //mode 1 - only unrevealed tiles; mode 0 - all, mode -1 -  only unrevealed
	void getAllTiles (boost::unordered_set<int3, ShashInt3> &tiles, int player=-1, int level=-1, int surface=0) const; //returns all tiles on given level (-1 - both levels, otherwise number of level); surface: 0 - land and water, 1 - only land, 2 - only water
	ui16 getRandomArt (int flags);
	ui16 getArtSync (ui32 rand, int flags); //synchronous
	void pickAllowedArtsSet(std::vector<const CArtifact*> &out); //gives 3 treasures, 3 minors, 1 major -> used by Black Market and Artifact Merchant
	void erasePickedArt (si32 id);
	void getAllowedSpells(std::vector<ui16> &out, ui16 level);
};

class DLL_LINKAGE CNonConstInfoCallback : public CPrivilagedInfoCallback
{
public:
	PlayerState *getPlayer(ui8 color, bool verbose = true);
	TeamState *getTeam(ui8 teamID);//get team by team ID
	TeamState *getPlayerTeam(ui8 color);// get team by player color
	CGHeroInstance *getHero(int objid);
	CGTownInstance *getTown(int objid);
	TerrainTile * getTile(int3 pos);
};

class DLL_LINKAGE IGameEventRealizer
{
public:
	virtual void commitPackage(CPackForClient *pack) = 0;

	virtual void showInfoDialog(InfoWindow *iw);
	virtual void setObjProperty(int objid, int prop, si64 val);


	virtual void showInfoDialog(const std::string &msg, int player);
};

class DLL_LINKAGE IGameEventCallback : public IGameEventRealizer
{
public:
	virtual void changeSpells(int hid, bool give, const std::set<ui32> &spells)=0;
	virtual bool removeObject(int objid)=0;
	virtual void setBlockVis(int objid, bool bv)=0;
	virtual void setOwner(int objid, ui8 owner)=0;
	virtual void setHoverName(int objid, MetaString * name)=0;
	virtual void changePrimSkill(int ID, int which, si64 val, bool abs=false)=0;
	virtual void changeSecSkill(int ID, int which, int val, bool abs=false)=0; 
	virtual void showBlockingDialog(BlockingDialog *iw, const CFunctionList<void(ui32)> &callback)=0;
	virtual ui32 showBlockingDialog(BlockingDialog *iw) =0; //synchronous version of above //TODO:
	virtual void showGarrisonDialog(int upobj, int hid, bool removableUnits, const boost::function<void()> &cb) =0; //cb will be called when player closes garrison window
	virtual void showThievesGuildWindow(int player, int requestingObjId) =0;
	virtual void giveResource(int player, int which, int val)=0;

	virtual void giveCreatures(const CArmedInstance *objid, const CGHeroInstance * h, const CCreatureSet &creatures, bool remove) =0;
	virtual void takeCreatures(int objid, const std::vector<CStackBasicDescriptor> &creatures) =0;
	virtual bool changeStackCount(const StackLocation &sl, TQuantity count, bool absoluteValue = false) =0;
	virtual bool changeStackType(const StackLocation &sl, CCreature *c) =0;
	virtual bool insertNewStack(const StackLocation &sl, const CCreature *c, TQuantity count = -1) =0; //count -1 => moves whole stack
	virtual bool eraseStack(const StackLocation &sl, bool forceRemoval = false) =0;
	virtual bool swapStacks(const StackLocation &sl1, const StackLocation &sl2) =0;
	virtual bool addToSlot(const StackLocation &sl, const CCreature *c, TQuantity count) =0; //makes new stack or increases count of already existing
	virtual void tryJoiningArmy(const CArmedInstance *src, const CArmedInstance *dst, bool removeObjWhenFinished, bool allowMerging) =0; //merges army from src do dst or opens a garrison window
	virtual bool moveStack(const StackLocation &src, const StackLocation &dst, TQuantity count) = 0;

	virtual void giveHeroNewArtifact(const CGHeroInstance *h, const CArtifact *artType, int pos) = 0;
	virtual void giveHeroArtifact(const CGHeroInstance *h, const CArtifactInstance *a, int pos) = 0; //pos==-1 - first free slot in backpack=0; pos==-2 - default if available or backpack
	virtual void putArtifact(const ArtifactLocation &al, const CArtifactInstance *a) = 0;
	virtual void removeArtifact(const ArtifactLocation &al) = 0;
	virtual bool moveArtifact(const ArtifactLocation &al1, const ArtifactLocation &al2) = 0;

	virtual void showCompInfo(ShowInInfobox * comp)=0;
	virtual void heroVisitCastle(int obj, int heroID)=0;
	virtual void stopHeroVisitCastle(int obj, int heroID)=0;
	virtual void startBattleI(const CArmedInstance *army1, const CArmedInstance *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, bool creatureBank = false, boost::function<void(BattleResult*)> cb = 0, const CGTownInstance *town = NULL)=0; //use hero=NULL for no hero
	virtual void startBattleI(const CArmedInstance *army1, const CArmedInstance *army2, int3 tile, boost::function<void(BattleResult*)> cb = 0, bool creatureBank = false)=0; //if any of armies is hero, hero will be used
	virtual void startBattleI(const CArmedInstance *army1, const CArmedInstance *army2, boost::function<void(BattleResult*)> cb = 0, bool creatureBank = false)=0; //if any of armies is hero, hero will be used, visitable tile of second obj is place of battle
	virtual void setAmount(int objid, ui32 val)=0;
	virtual bool moveHero(si32 hid, int3 dst, ui8 instant, ui8 asker = 255)=0;
	virtual void giveHeroBonus(GiveBonus * bonus)=0;
	virtual void setMovePoints(SetMovePoints * smp)=0;
	virtual void setManaPoints(int hid, int val)=0;
	virtual void giveHero(int id, int player)=0;
	virtual void changeObjPos(int objid, int3 newPos, ui8 flags)=0;
	virtual void sendAndApply(CPackForClient * info)=0;
	virtual void heroExchange(si32 hero1, si32 hero2)=0; //when two heroes meet on adventure map
	virtual void addQuest(int player, QuestInfo & quest){};
};

/// Interface class for handling general game logic and actions
class DLL_LINKAGE IGameCallback : public CPrivilagedInfoCallback, public IGameEventCallback
{
public:
	virtual ~IGameCallback(){};

	//do sth
	const CGObjectInstance *putNewObject(int ID, int subID, int3 pos);
	const CGCreature *putNewMonster(int creID, int count, int3 pos);

	friend struct CPack;
	friend struct CPackForClient;
	friend struct CPackForServer;
};
