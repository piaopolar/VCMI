#pragma once


#include "HeroBonus.h"
#include "GameConstants.h"
#include "CArtHandler.h"

class CCreature;
class CGHeroInstance;
class CArmedInstance;
class CCreatureArtifactSet;

class DLL_LINKAGE CStackBasicDescriptor
{
public:
	const CCreature *type;
	TQuantity count;

	CStackBasicDescriptor();
	CStackBasicDescriptor(TCreature id, TQuantity Count);
	CStackBasicDescriptor(const CCreature *c, TQuantity Count);

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & type & count;
	}
};

class DLL_LINKAGE CStackInstance : public CBonusSystemNode, public CStackBasicDescriptor, public CArtifactSet
{
protected:
	const CArmedInstance *_armyObj; //stack must be part of some army, army must be part of some object
public:
	int idRand; //hlp variable used during loading game -> "id" placeholder for randomization

	const CArmedInstance * const & armyObj; //stack must be part of some army, army must be part of some object
	expType experience;//commander needs same amount of exp as hero

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CBonusSystemNode&>(*this);
		h & static_cast<CStackBasicDescriptor&>(*this);
		h & static_cast<CArtifactSet&>(*this);
		h & _armyObj & experience;
		BONUS_TREE_DESERIALIZATION_FIX
	}

	//overrides CBonusSystemNode
	//void getParents(TCNodes &out, const CBonusSystemNode *source = NULL) const;  //retrieves list of parent nodes (nodes to inherit bonuses from), source is the prinary asker
	std::string bonusToString(Bonus *bonus, bool description) const; // how would bonus description look for this particular type of node
	std::string bonusToGraphics(Bonus *bonus) const; //file name of graphics from StackSkills , in future possibly others

	virtual ui64 getPower() const;
	int getQuantityID() const;
	std::string getQuantityTXT(bool capitalized = true) const;
	virtual int getExpRank() const;
	si32 magicResistance() const;
	int getCreatureID() const; //-1 if not available
	std::string getName() const; //plural or singular
	virtual void init();
	CStackInstance();
	CStackInstance(TCreature id, TQuantity count);
	CStackInstance(const CCreature *cre, TQuantity count);
	~CStackInstance();

	void setType(int creID);
	void setType(const CCreature *c);
	void setArmyObj(const CArmedInstance *ArmyObj);
	virtual void giveStackExp(expType exp);
	bool valid(bool allowUnrandomized) const;
	ui8 bearerType() const OVERRIDE; //from CArtifactSet
	virtual std::string nodeName() const OVERRIDE; //from CBonusSystemnode
	void deserializationFix();
};

class DLL_LINKAGE CCommanderInstance : public CStackInstance
{
public:
	//TODO: what if Commander is not a part of creature set?

	//commander class is determined by its base creature
	ui8 alive;
	ui8 level; //required only to count callbacks
	std::string name; // each Commander has different name
	std::vector <ui8> secondarySkills; //ID -> level
	std::set <ui8> specialSKills;
	//std::vector <CArtifactInstance *> arts;
	void init() OVERRIDE;
	CCommanderInstance();
	CCommanderInstance (TCreature id);
	~CCommanderInstance();
	void setAlive (bool alive);
	void giveStackExp (expType exp);
	void levelUp ();

	ui64 getPower() const {return 0;};
	int getExpRank() const;
	ui8 bearerType() const OVERRIDE; //from CArtifactSet

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & static_cast<CStackInstance&>(*this);
		h & alive & level & name & secondarySkills & specialSKills;
	}
};

DLL_LINKAGE std::ostream & operator<<(std::ostream & str, const CStackInstance & sth);

typedef std::map<TSlot, CStackInstance*> TSlots;
typedef std::map<TSlot, CStackBasicDescriptor> TSimpleSlots;

class IArmyDescriptor
{
public:
	virtual void clear() = 0;
	virtual bool setCreature(TSlot slot, TCreature cre, TQuantity count) = 0;
};

//simplified version of CCreatureSet
class DLL_LINKAGE CSimpleArmy : public IArmyDescriptor
{
public:
	TSimpleSlots army;
	void clear() OVERRIDE;
	bool setCreature(TSlot slot, TCreature cre, TQuantity count) OVERRIDE;
	operator bool() const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & army;
	}
};

class DLL_LINKAGE CCreatureSet : public IArmyDescriptor //seven combined creatures
{
	CCreatureSet(const CCreatureSet&);;
	CCreatureSet &operator=(const CCreatureSet&);
public:
	TSlots stacks; //slots[slot_id]->> pair(creature_id,creature_quantity)
	ui8 formation; //false - wide, true - tight

	CCreatureSet();
	virtual ~CCreatureSet();
	virtual void armyChanged();

	const CStackInstance &operator[](TSlot slot) const; 

	const TSlots &Slots() const {return stacks;}

	void addToSlot(TSlot slot, TCreature cre, TQuantity count, bool allowMerging = true); //Adds stack to slot. Slot must be empty or with same type creature
	void addToSlot(TSlot slot, CStackInstance *stack, bool allowMerging = true); //Adds stack to slot. Slot must be empty or with same type creature
	void clear() OVERRIDE;
	void setFormation(bool tight);
	CArmedInstance *castToArmyObj();

	//basic operations
	void putStack(TSlot slot, CStackInstance *stack); //adds new stack to the army, slot must be empty
	void setStackCount(TSlot slot, TQuantity count); //stack must exist!
	CStackInstance *detachStack(TSlot slot); //removes stack from army but doesn't destroy it (so it can be moved somewhere else or safely deleted)
	void setStackType(TSlot slot, const CCreature *type);
	void giveStackExp(expType exp);
	void setStackExp(TSlot slot, expType exp);

	//derivative 
	void eraseStack(TSlot slot); //slot must be occupied
	void joinStack(TSlot slot, CStackInstance * stack); //adds new stack to the existing stack of the same type
	void changeStackCount(TSlot slot, TQuantity toAdd); //stack must exist!
	bool setCreature (TSlot slot, TCreature type, TQuantity quantity) OVERRIDE; //replaces creature in stack; slots 0 to 6, if quantity=0 erases stack
	void setToArmy(CSimpleArmy &src); //erases all our army and moves stacks from src to us; src MUST NOT be an armed object! WARNING: use it wisely. Or better do not use at all.
	
	const CStackInstance& getStack(TSlot slot) const; //stack must exist
	const CStackInstance* getStackPtr(TSlot slot) const; //if stack doesn't exist, returns NULL
	const CCreature* getCreature(TSlot slot) const; //workaround of map issue;
	int getStackCount (TSlot slot) const;
	expType getStackExperience(TSlot slot) const;
	TSlot findStack(const CStackInstance *stack) const; //-1 if none
	TSlot getSlotFor(TCreature creature, ui32 slotsAmount = GameConstants::ARMY_SIZE) const; //returns -1 if no slot available
	TSlot getSlotFor(const CCreature *c, ui32 slotsAmount = GameConstants::ARMY_SIZE) const; //returns -1 if no slot available
	TSlot getFreeSlot(ui32 slotsAmount = GameConstants::ARMY_SIZE) const;
	bool mergableStacks(std::pair<TSlot, TSlot> &out, TSlot preferable = -1) const; //looks for two same stacks, returns slot positions;
	bool validTypes(bool allowUnrandomized = false) const; //checks if all types of creatures are set properly
	bool slotEmpty(TSlot slot) const;
	int stacksCount() const;
	virtual bool needsLastStack() const; //true if last stack cannot be taken
	ui64 getArmyStrength() const; //sum of AI values of creatures
	ui64 getPower (TSlot slot) const; //value of specific stack
	std::string getRoughAmount (TSlot slot) const; //rough size of specific stack
	bool hasStackAtSlot(TSlot slot) const;
	
	bool contains(const CStackInstance *stack) const;
	bool canBeMergedWith(const CCreatureSet &cs, bool allowMergingStacks = true) const;

	template <typename Handler> void serialize(Handler &h, const int version)
	{
		h & stacks & formation;
	}
	operator bool() const
	{
		return stacks.size() > 0;
	}
	void sweep();
};
