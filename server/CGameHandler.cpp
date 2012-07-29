#include "StdInc.h"

#include "../lib/int3.h"
#include "../lib/CCampaignHandler.h"
#include "../lib/StartInfo.h"
#include "../lib/CArtHandler.h"
#include "../lib/CBuildingHandler.h"
#include "../lib/CDefObjInfoHandler.h"
#include "../lib/CHeroHandler.h"
#include "../lib/CObjectHandler.h"
#include "../lib/CSpellHandler.h"
#include "../lib/CGeneralTextHandler.h"
#include "../lib/CTownHandler.h"
#include "../lib/CCreatureHandler.h"
#include "../lib/CGameState.h"
#include "../lib/BattleState.h"
#include "../lib/CondSh.h"
#include "../lib/NetPacks.h"
#include "../lib/VCMI_Lib.h"
#include "../lib/map.h"
#include "../lib/VCMIDirs.h"
#include "../client/CSoundBase.h"
#include "CGameHandler.h"
#include "../lib/CCreatureSet.h"
#include "../lib/CThreadHelper.h"
#include "../lib/GameConstants.h"
#include "../lib/RegisterTypes.h"

/*
 * CGameHandler.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */


#ifndef _MSC_VER
#include <boost/thread/xtime.hpp>
#endif
#include <boost/random/linear_congruential.hpp>
#include <boost/range/algorithm/random_shuffle.hpp>
extern bool end2;
#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

#define COMPLAIN_RET_IF(cond, txt) do {if(cond){complain(txt); return;}} while(0)
#define COMPLAIN_RET(txt) {complain(txt); return false;}
#define COMPLAIN_RETF(txt, FORMAT) {complain(boost::str(boost::format(txt) % FORMAT)); return false;}
#define NEW_ROUND 		BattleNextRound bnr;\
		bnr.round = gs->curB->round + 1;\
		sendAndApply(&bnr);

CondSh<bool> battleMadeAction;
CondSh<BattleResult *> battleResult(NULL);
template <typename T> class CApplyOnGH;

class CBaseForGHApply
{
public:
	virtual bool applyOnGH(CGameHandler *gh, CConnection *c, void *pack, ui8 player) const =0;
	virtual ~CBaseForGHApply(){}
	template<typename U> static CBaseForGHApply *getApplier(const U * t=NULL)
	{
		return new CApplyOnGH<U>;
	}
};

template <typename T> class CApplyOnGH : public CBaseForGHApply
{
public:
	bool applyOnGH(CGameHandler *gh, CConnection *c, void *pack, ui8 player) const
	{
		T *ptr = static_cast<T*>(pack);
		ptr->c = c;
		ptr->player = player;
		return ptr->applyGh(gh);
	}
};

static CApplier<CBaseForGHApply> *applier = NULL;

CMP_stack cmpst ;

static inline double distance(int3 a, int3 b)
{
	return std::sqrt( (double)(a.x-b.x)*(a.x-b.x) + (a.y-b.y)*(a.y-b.y) );
}
static void giveExp(BattleResult &r)
{
	r.exp[0] = 0;
	r.exp[1] = 0;
	for(std::map<ui32,si32>::iterator i = r.casualties[!r.winner].begin(); i!=r.casualties[!r.winner].end(); i++)
	{
		r.exp[r.winner] += VLC->creh->creatures[i->first]->valOfBonuses(Bonus::STACK_HEALTH) * i->second;
	}
}

PlayerStatus PlayerStatuses::operator[](ui8 player)
{
	boost::unique_lock<boost::mutex> l(mx);
	if(players.find(player) != players.end())
	{
		return players[player];
	}
	else
	{
		throw std::runtime_error("No such player!");
	}
}
void PlayerStatuses::addPlayer(ui8 player)
{
	boost::unique_lock<boost::mutex> l(mx);
	players[player];
}

int PlayerStatuses::getQueriesCount(ui8 player)
{
	boost::unique_lock<boost::mutex> l(mx);
	if(players.find(player) != players.end())
	{
		return players[player].queries.size();
	}
	else
	{
		return 0;
	}
}

bool PlayerStatuses::checkFlag(ui8 player, bool PlayerStatus::*flag)
{
	boost::unique_lock<boost::mutex> l(mx);
	if(players.find(player) != players.end())
	{
		return players[player].*flag;
	}
	else
	{
		throw std::runtime_error("No such player!");
	}
}
void PlayerStatuses::setFlag(ui8 player, bool PlayerStatus::*flag, bool val)
{
	boost::unique_lock<boost::mutex> l(mx);
	if(players.find(player) != players.end())
	{
		players[player].*flag = val;
	}
	else
	{
		throw std::runtime_error("No such player!");
	}
	cv.notify_all();
}
void PlayerStatuses::addQuery(ui8 player, ui32 id)
{
	boost::unique_lock<boost::mutex> l(mx);
	if(players.find(player) != players.end())
	{
		players[player].queries.insert(id);
	}
	else
	{
		throw std::runtime_error("No such player!");
	}
	cv.notify_all();
}
void PlayerStatuses::removeQuery(ui8 player, ui32 id)
{
	boost::unique_lock<boost::mutex> l(mx);
	if(players.find(player) != players.end())
	{
		assert(vstd::contains(players[player].queries, id));
		players[player].queries.erase(id);
	}
	else
	{
		throw std::runtime_error("No such player!");
	}
	cv.notify_all();
}

template <typename T>
void callWith(std::vector<T> args, boost::function<void(T)> fun, ui32 which)
{
	fun(args[which]);
}

void CGameHandler::levelUpHero(int ID, int skill)
{
	changeSecSkill(ID, skill, 1, 0);
	levelUpHero(ID);
}

void CGameHandler::levelUpHero(int ID)
{
	CGHeroInstance *hero = static_cast<CGHeroInstance *>(gs->map->objects[ID].get());

	// required exp for at least 1 lvl-up hasn't been reached
	if (hero->exp < VLC->heroh->reqExp(hero->level+1))
	{
		afterBattleCallback();
		return;
	}

	//give prim skill
	tlog5 << hero->name <<" got level "<<hero->level<<std::endl;
	int r = rand()%100, pom=0, x=0;
	int std::pair<int,int>::*g  =  (hero->level>9) ? (&std::pair<int,int>::second) : (&std::pair<int,int>::first);
	for(;x<GameConstants::PRIMARY_SKILLS;x++)
	{
		pom += hero->type->heroClass->primChance[x].*g;
		if(r<pom)
			break;
	}
	tlog5 << "The hero gets the primary skill with the no. " << x << " with a probability of " << r << "%." <<  std::endl;
	SetPrimSkill sps;
	sps.id = ID;
	sps.which = x;
	sps.abs = false;
	sps.val = 1;
	sendAndApply(&sps);

	HeroLevelUp hlu;
	hlu.heroid = ID;
	hlu.primskill = x;
	hlu.level = hero->level+1;

	//picking sec. skills for choice
	std::set<int> basicAndAdv, expert, none;
	for(int i=0;i<GameConstants::SKILL_QUANTITY;i++)
		if (isAllowed(2,i))
			none.insert(i);

	for(unsigned i=0;i<hero->secSkills.size();i++)
	{
		if(hero->secSkills[i].second < 3)
			basicAndAdv.insert(hero->secSkills[i].first);
		else
			expert.insert(hero->secSkills[i].first);
		none.erase(hero->secSkills[i].first);
	}

	//first offered skill
	if(basicAndAdv.size())
	{
		int s = hero->type->heroClass->chooseSecSkill(basicAndAdv);//upgrade existing
		hlu.skills.push_back(s);
		basicAndAdv.erase(s);
	}
	else if(none.size() && hero->secSkills.size() < hero->type->heroClass->skillLimit)
	{
		hlu.skills.push_back(hero->type->heroClass->chooseSecSkill(none)); //give new skill
		none.erase(hlu.skills.back());
	}

	//second offered skill
	if(none.size() && hero->secSkills.size() < hero->type->heroClass->skillLimit) //hero have free skill slot
	{
		hlu.skills.push_back(hero->type->heroClass->chooseSecSkill(none)); //new skill
	}
	else if(basicAndAdv.size())
	{
		hlu.skills.push_back(hero->type->heroClass->chooseSecSkill(basicAndAdv)); //upgrade existing
	}

	if(hlu.skills.size() > 1) //apply and ask for secondary skill
	{
		boost::function<void(ui32)> callback = boost::function<void(ui32)>(boost::bind(callWith<ui16>,hlu.skills,boost::function<void(ui16)>(boost::bind(&CGameHandler::levelUpHero,this,ID,_1)),_1));
		applyAndAsk(&hlu,hero->tempOwner,callback); //call levelUpHero when client responds
	}
	else if(hlu.skills.size() == 1) //apply, give only possible skill  and send info
	{
		sendAndApply(&hlu);
		levelUpHero(ID, hlu.skills.back());
	}
	else //apply and send info
	{
		sendAndApply(&hlu);
		levelUpHero(ID);
	}
}

void CGameHandler::levelUpCommander (const CCommanderInstance * c, int skill)
{
	SetCommanderProperty scp;

	auto hero = dynamic_cast<const CGHeroInstance *>(c->armyObj);
	if (hero)
		scp.heroid = hero->id;
	else
	{
		complain ("Commander is not led by hero!");
		return;
	}

	scp.accumulatedBonus.subtype = 0;
	scp.accumulatedBonus.additionalInfo = 0;
	scp.accumulatedBonus.duration = Bonus::PERMANENT;
	scp.accumulatedBonus.turnsRemain = 0;
	scp.accumulatedBonus.source = Bonus::COMMANDER;
	scp.accumulatedBonus.valType = Bonus::BASE_NUMBER;
	if (skill <= ECommander::SPELL_POWER)
	{
		scp.which = SetCommanderProperty::BONUS;

		auto difference = [](std::vector< std::vector <ui8> > skillLevels, std::vector <ui8> secondarySkills, int skill)->int
		{
			int s = std::min (skill, (int)ECommander::SPELL_POWER); //spell power level controls also casts and resistance
			return skillLevels[skill][secondarySkills[s]] - (secondarySkills[s] ? skillLevels[skill][secondarySkills[s]-1] : 0);
		};

		switch (skill)
		{
			case ECommander::ATTACK:
				scp.accumulatedBonus.type = Bonus::PRIMARY_SKILL;
				scp.accumulatedBonus.subtype = PrimarySkill::ATTACK;
				break;
			case ECommander::DEFENSE:
				scp.accumulatedBonus.type = Bonus::PRIMARY_SKILL;
				scp.accumulatedBonus.subtype = PrimarySkill::DEFENSE;
				break;
			case ECommander::HEALTH:
				scp.accumulatedBonus.type = Bonus::STACK_HEALTH;
				scp.accumulatedBonus.valType = Bonus::PERCENT_TO_BASE;
				break;
			case ECommander::DAMAGE:
				scp.accumulatedBonus.type = Bonus::CREATURE_DAMAGE;
				scp.accumulatedBonus.subtype = 0;
				scp.accumulatedBonus.valType = Bonus::PERCENT_TO_BASE;
				break;
			case ECommander::SPEED:
				scp.accumulatedBonus.type = Bonus::STACKS_SPEED;
				break;
			case ECommander::SPELL_POWER:
				scp.accumulatedBonus.type = Bonus::MAGIC_RESISTANCE;
				scp.accumulatedBonus.val = difference (VLC->creh->skillLevels, c->secondarySkills, ECommander::RESISTANCE);
				sendAndApply (&scp); //additional pack
				scp.accumulatedBonus.type = Bonus::CASTS;
				scp.accumulatedBonus.val = difference (VLC->creh->skillLevels, c->secondarySkills, ECommander::CASTS);
				sendAndApply (&scp); //additional pack
				scp.accumulatedBonus.type = Bonus::CREATURE_ENCHANT_POWER; //send normally
				break;
		}

		scp.accumulatedBonus.val = difference (VLC->creh->skillLevels, c->secondarySkills, skill);
		sendAndApply (&scp);

		scp.which = SetCommanderProperty::SECONDARY_SKILL;
		scp.additionalInfo = skill;
		scp.amount = c->secondarySkills[skill] + 1;
		sendAndApply (&scp);
	}
	else if (skill >= 100)
	{
		scp.which = SetCommanderProperty::SPECIAL_SKILL;
		scp.accumulatedBonus = VLC->creh->skillRequirements[skill-100].first;
		scp.additionalInfo = skill; //unnormalized
		sendAndApply (&scp);
	}
	levelUpCommander (c);
}

void CGameHandler::levelUpCommander(const CCommanderInstance * c)
{
	if (c->experience < VLC->heroh->reqExp (c->level + 1))
	{
		return;
	}
	CommanderLevelUp clu;

	auto hero = dynamic_cast<const CGHeroInstance *>(c->armyObj);
	if (hero)
		clu.heroid = hero->id;
	else
	{
		complain ("Commander is not led by hero!");
		return;
	}

	//picking sec. skills for choice

	for (int i = 0; i <= ECommander::SPELL_POWER; ++i)
	{
		if (c->secondarySkills[i] < ECommander::MAX_SKILL_LEVEL)
			clu.skills.push_back(i);
	}
	int i = 100;
	BOOST_FOREACH (auto specialSkill, VLC->creh->skillRequirements)
	{
		if (c->secondarySkills[specialSkill.second.first] == ECommander::MAX_SKILL_LEVEL &&
			c->secondarySkills[specialSkill.second.second] == ECommander::MAX_SKILL_LEVEL &&
			!vstd::contains (c->specialSKills, i))
			clu.skills.push_back (i);
		++i;
	}
	int skillAmount = clu.skills.size();

	if (skillAmount > 1) //apply and ask for secondary skill
	{
		auto callback = boost::function<void(ui32)>(boost::bind(callWith<ui32>, clu.skills, boost::function<void(ui32)>(boost::bind(&CGameHandler::levelUpCommander, this, c, _1)), _1));
		applyAndAsk (&clu, c->armyObj->tempOwner, callback); //call levelUpCommander when client responds
	}
	else if (skillAmount == 1) //apply, give only possible skill and send info
	{
		sendAndApply(&clu);
		levelUpCommander(c, clu.skills.back());
	}
	else //apply and send info
	{
		sendAndApply(&clu);
		levelUpCommander(c);
	}
}

void CGameHandler::changePrimSkill(int ID, int which, si64 val, bool abs)
{
	SetPrimSkill sps;
	sps.id = ID;
	sps.which = which;
	sps.abs = abs;
	sps.val = val;
	sendAndApply(&sps);

	//only for exp - hero may level up
	if (which == 4)
	{
		levelUpHero(ID);
		CGHeroInstance *h = static_cast<CGHeroInstance *>(gs->map->objects[ID].get());
		if (h->commander && h->commander->alive)
		{
			SetCommanderProperty scp;
			scp.heroid = h->id;
			scp.which = SetCommanderProperty::EXPERIENCE;
			scp.amount = val;
			sendAndApply (&scp);
			levelUpCommander (h->commander);
			CBonusSystemNode::treeHasChanged();
		}
	}
}

void CGameHandler::changeSecSkill( int ID, int which, int val, bool abs/*=false*/ )
{
	SetSecSkill sss;
	sss.id = ID;
	sss.which = which;
	sss.val = val;
	sss.abs = abs;
	sendAndApply(&sss);

	if(which == 7) //Wisdom
	{
		const CGHeroInstance *h = getHero(ID);
		if(h && h->visitedTown)
			giveSpells(h->visitedTown, h);
	}
}

void CGameHandler::startBattle( const CArmedInstance *armies[2], int3 tile, const CGHeroInstance *heroes[2], bool creatureBank, boost::function<void(BattleResult*)> cb, const CGTownInstance *town /*= NULL*/ )
{
	battleEndCallback = new boost::function<void(BattleResult*)>(cb);
	{
		setupBattle(tile, armies, heroes, creatureBank, town); //initializes stacks, places creatures on battlefield, blocks and informs player interfaces
	}

	runBattle();
}

void CGameHandler::endBattle(int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2)
{
	bool duel = gs->initialOpts->mode == StartInfo::DUEL;
	BattleResultsApplied resultsApplied;

	const CArmedInstance *bEndArmy1 = gs->curB->belligerents[0];
	const CArmedInstance *bEndArmy2 = gs->curB->belligerents[1];
	resultsApplied.player1 = bEndArmy1->tempOwner;
	resultsApplied.player2 = bEndArmy2->tempOwner;
	const CGHeroInstance *victoriousHero = gs->curB->heroes[battleResult.data->winner];

	int result = battleResult.get()->result;

	if(!duel)
	{
		//unblock engaged players
		if(bEndArmy1->tempOwner<GameConstants::PLAYER_LIMIT)
			states.setFlag(bEndArmy1->tempOwner, &PlayerStatus::engagedIntoBattle, false);
		if(bEndArmy2 && bEndArmy2->tempOwner<GameConstants::PLAYER_LIMIT)
			states.setFlag(bEndArmy2->tempOwner, &PlayerStatus::engagedIntoBattle, false);
	}

	//end battle, remove all info, free memory
	giveExp(*battleResult.data);
	if (hero1)
		battleResult.data->exp[0] = hero1->calculateXp(battleResult.data->exp[0]);//scholar skill
	if (hero2)
		battleResult.data->exp[1] = hero2->calculateXp(battleResult.data->exp[1]);

	ui8 sides[2];
	for(int i=0; i<2; ++i)
	{
		sides[i] = gs->curB->sides[i];
	}
	ui8 loser = sides[!battleResult.data->winner];

	CasualtiesAfterBattle cab1(bEndArmy1, gs->curB), cab2(bEndArmy2, gs->curB); //calculate casualties before deleting battle
	ChangeSpells cs; //for Eagle Eye

	if(victoriousHero)
	{
		if(int eagleEyeLevel = victoriousHero->getSecSkillLevel(CGHeroInstance::EAGLE_EYE))
		{
			int maxLevel = eagleEyeLevel + 1;
			double eagleEyeChance = victoriousHero->valOfBonuses(Bonus::SECONDARY_SKILL_PREMY, CGHeroInstance::EAGLE_EYE);
			BOOST_FOREACH(const CSpell *sp, gs->curB->usedSpellsHistory[!battleResult.data->winner])
				if(sp->level <= maxLevel && !vstd::contains(victoriousHero->spells, sp->id) && rand() % 100 < eagleEyeChance)
					cs.spells.insert(sp->id);
		}
	}

	ConstTransitivePtr <CGHeroInstance> winnerHero = battleResult.data->winner != 0 ? hero2 : hero1;
	ConstTransitivePtr <CGHeroInstance> loserHero = battleResult.data->winner != 0 ? hero1 : hero2;
	std::vector<ui32> arts; //display them in window

	//TODO: display loot in window
	if (result < BattleResult::SURRENDER && winnerHero)
	{
		if (loserHero)
		{
			auto artifactsWorn = loserHero->artifactsWorn; //TODO: wrap it into a function, somehow (boost::variant -_-)
			BOOST_FOREACH (auto artSlot, artifactsWorn)
			{
				MoveArtifact ma;
				ma.src = ArtifactLocation (loserHero, artSlot.first);
				const CArtifactInstance * art =  ma.src.getArt();
				if (art && !art->artType->isBig()) // don't move war machines or locked arts (spellbook)
				{
					arts.push_back (art->artType->id);
					ma.dst = ArtifactLocation (winnerHero, art->firstAvailableSlot(winnerHero));
					sendAndApply(&ma);
				}
			}
			while (!loserHero->artifactsInBackpack.empty())
			{
				//we assume that no big artifacts can be found
				MoveArtifact ma;
				ma.src = ArtifactLocation (loserHero, GameConstants::BACKPACK_START); //backpack automatically shifts arts to beginning
				const CArtifactInstance * art =  ma.src.getArt();
				arts.push_back (art->artType->id);
				ma.dst = ArtifactLocation (winnerHero, art->firstAvailableSlot(winnerHero));
				sendAndApply(&ma);
			}
			if (loserHero->commander) //TODO: what if commanders belong to no hero?
			{
				artifactsWorn = loserHero->commander->artifactsWorn;
				BOOST_FOREACH (auto artSlot, artifactsWorn)
				{
					MoveArtifact ma;
					ma.src = ArtifactLocation (loserHero->commander.get(), artSlot.first);
					const CArtifactInstance * art =  ma.src.getArt();
					if (art && !art->artType->isBig())
					{
						arts.push_back (art->artType->id);
						ma.dst = ArtifactLocation (winnerHero, art->firstAvailableSlot(winnerHero));
						sendAndApply(&ma);
					}
				}
			}
		}
		BOOST_FOREACH (auto armySlot, gs->curB->belligerents[!battleResult.data->winner]->stacks)
		{
			auto artifactsWorn = armySlot.second->artifactsWorn;
			BOOST_FOREACH (auto artSlot, artifactsWorn)
			{
				MoveArtifact ma;
				ma.src = ArtifactLocation (armySlot.second, artSlot.first);
				const CArtifactInstance * art =  ma.src.getArt();
				if (art && !art->artType->isBig())
				{
					arts.push_back (art->artType->id);
					ma.dst = ArtifactLocation (winnerHero, art->firstAvailableSlot(winnerHero));
					sendAndApply(&ma);
				}
			}
		}
	}

	sendAndApply(battleResult.data); //after this point casualties objects are destroyed

	if (arts.size()) //display loot
	{
		InfoWindow iw;
		iw.player = winnerHero->tempOwner;

		iw.text.addTxt (MetaString::GENERAL_TXT, 30); //You have captured enemy artifact

		BOOST_FOREACH (auto id, arts) //TODO; separate function to display loot for various ojects?
		{
			iw.components.push_back (Component (Component::ARTIFACT, id, 0, 0));
			if(iw.components.size() >= 14)
			{
				sendAndApply(&iw);
				iw.components.clear();
				iw.text.addTxt (MetaString::GENERAL_TXT, 30); //repeat
			}
		}
		if (iw.components.size())
		{
			sendAndApply(&iw);
		}
	}
	//Eagle Eye secondary skill handling
	if(cs.spells.size())
	{
		cs.learn = 1;
		cs.hid = victoriousHero->id;

		InfoWindow iw;
		iw.player = victoriousHero->tempOwner;
		iw.text.addTxt(MetaString::GENERAL_TXT, 221); //Through eagle-eyed observation, %s is able to learn %s
		iw.text.addReplacement(victoriousHero->name);

		std::ostringstream names;
		for(int i = 0; i < cs.spells.size(); i++)
		{
			names << "%s";
			if(i < cs.spells.size() - 2)
				names << ", ";
			else if(i < cs.spells.size() - 1)
				names << "%s";
		}
		names << ".";

		iw.text.addReplacement(names.str());

		std::set<ui32>::iterator it = cs.spells.begin();
		for(int i = 0; i < cs.spells.size(); i++, it++)
		{
			iw.text.addReplacement(MetaString::SPELL_NAME, *it);
			if(i == cs.spells.size() - 2) //we just added pre-last name
				iw.text.addReplacement(MetaString::GENERAL_TXT, 141); // " and "
			iw.components.push_back(Component(Component::SPELL, *it, 0, 0));
		}

		sendAndApply(&iw);
		sendAndApply(&cs);
	}

	// Necromancy if applicable.
	const CStackBasicDescriptor raisedStack = winnerHero ? winnerHero->calculateNecromancy(*battleResult.data) : CStackBasicDescriptor();
	// Give raised units to winner and show dialog, if any were raised,
	// units will be given after casualities are taken
	const TSlot necroSlot = raisedStack.type ? winnerHero->getSlotFor(raisedStack.type) : -1;

	if(!duel)
	{
		cab1.takeFromArmy(this); cab2.takeFromArmy(this); //take casualties after battle is deleted

		//if one hero has lost we will erase him
		if(battleResult.data->winner!=0 && hero1)
		{
			RemoveObject ro(hero1->id);
			sendAndApply(&ro);
		}
		if(battleResult.data->winner!=1 && hero2)
		{
			RemoveObject ro(hero2->id);
			sendAndApply(&ro);
		}

		//give exp
		if (battleResult.data->exp[0] && hero1)
			changePrimSkill(hero1->id,4,battleResult.data->exp[0]);
		else if (battleResult.data->exp[1] && hero2)
			changePrimSkill(hero2->id,4,battleResult.data->exp[1]);
		else
			afterBattleCallback();
	}

	if (necroSlot != -1)
	{
		winnerHero->showNecromancyDialog(raisedStack);
		addToSlot(StackLocation(winnerHero, necroSlot), raisedStack.type, raisedStack.count);
	}
	sendAndApply(&resultsApplied);

	if(duel)
	{
		CSaveFile resultFile("result.vdrst");
		resultFile << *battleResult.data;
		return;
	}

	if(visitObjectAfterVictory && winnerHero == hero1)
	{
		visitObjectOnTile(*getTile(winnerHero->getPosition()), winnerHero);
	}
	visitObjectAfterVictory = false;

	winLoseHandle(1<<sides[0] | 1<<sides[1]); //handle victory/loss of engaged players

	if(result == BattleResult::SURRENDER || result == BattleResult::ESCAPE) //loser has escaped or surrendered
	{
		SetAvailableHeroes sah;
		sah.player = loser;
		sah.hid[0] = loserHero->subID;
		if(result == 1) //retreat
		{
			sah.army[0].clear();
			sah.army[0].setCreature(0, VLC->creh->nameToID[loserHero->type->refTypeStack[0]],1);
		}

		if(const CGHeroInstance *another =  getPlayer(loser)->availableHeroes[1])
			sah.hid[1] = another->subID;
		else
			sah.hid[1] = -1;

		sendAndApply(&sah);
	}
}
void CGameHandler::afterBattleCallback() //object interaction after leveling up is done
{
	if(battleEndCallback && *battleEndCallback)
	{
		boost::function<void(BattleResult*)> *backup = battleEndCallback;
		battleEndCallback = NULL; //we need to set it to NULL or else we have infinite recursion when after battle callback gives exp (eg Pandora Box with exp)
		(*backup)(battleResult.data);
		delete backup;
	}
}
void CGameHandler::prepareAttack(BattleAttack &bat, const CStack *att, const CStack *def, int distance, int targetHex)
{
	bat.bsa.clear();
	bat.stackAttacking = att->ID;
	int attackerLuck = att->LuckVal();
	const CGHeroInstance * h0 = gs->curB->heroes[0],
		* h1 = gs->curB->heroes[1];
	bool noLuck = false;
	if((h0 && NBonus::hasOfType(h0, Bonus::BLOCK_LUCK)) ||
	   (h1 && NBonus::hasOfType(h1, Bonus::BLOCK_LUCK)))
	{
		noLuck = true;
	}

	if(!noLuck && attackerLuck > 0  &&  rand()%24 < attackerLuck) //TODO?: negative luck option?
	{
		bat.flags |= BattleAttack::LUCKY;
	}
	if (rand()%100 < att->valOfBonuses(Bonus::DOUBLE_DAMAGE_CHANCE))
	{
		bat.flags |= BattleAttack::DEATH_BLOW;
	}

	if(att->getCreature()->idNumber == 146)
	{
		static const int artilleryLvlToChance[] = {0, 50, 75, 100};
		const CGHeroInstance * owner = gs->curB->getHero(att->owner);
		int chance = artilleryLvlToChance[owner->getSecSkillLevel(CGHeroInstance::ARTILLERY)];
		if(chance > rand() % 100)
		{
			bat.flags |= BattleAttack::BALLISTA_DOUBLE_DMG;
		}
	}
	// only primary target
	applyBattleEffects(bat, att, def, distance, false);

	if (!bat.shot()) //multiple-hex attack - only in meele
	{
		std::set<CStack*> attackedCreatures = gs->curB->getAttackedCreatures(att, targetHex);
		//TODO: get exact attacked hex for defender

		BOOST_FOREACH(CStack * stack, attackedCreatures)
		{
			if (stack != def) //do not hit same stack twice
			{
				applyBattleEffects(bat, att, stack, distance, true);
			}
		}
	}

	const Bonus * bonus = att->getBonus(Selector::type(Bonus::SPELL_LIKE_ATTACK));
	if (bonus && (bat.shot())) //TODO: make it work in meele?
	{
		bat.bsa.front().flags |= BattleStackAttacked::EFFECT;
		bat.bsa.front().effect = VLC->spellh->spells[bonus->subtype]->mainEffectAnim; //hopefully it does not interfere with any other effect?

		std::set<CStack*> attackedCreatures = gs->curB->getAttackedCreatures(VLC->spellh->spells[bonus->subtype], bonus->val, att->owner, targetHex);
		//TODO: get exact attacked hex for defender

		BOOST_FOREACH(CStack * stack, attackedCreatures)
		{
			if (stack != def) //do not hit same stack twice
			{
				applyBattleEffects(bat, att, stack, distance, true);
			}
		}
	}
}
void CGameHandler::applyBattleEffects(BattleAttack &bat, const CStack *att, const CStack *def, int distance, bool secondary) //helper function for prepareAttack
{
	BattleStackAttacked bsa;
	if (secondary)
		bsa.flags |= BattleStackAttacked::SECONDARY; //all other targets do not suffer from spells & spell-like abilities
	bsa.attackerID = att->ID;
	bsa.stackAttacked = def->ID;
	bsa.damageAmount = gs->curB->calculateDmg(att, def, gs->curB->battleGetOwner(att), gs->curB->battleGetOwner(def), bat.shot(), distance, bat.lucky(), bat.deathBlow(), bat.ballistaDoubleDmg());
	def->prepareAttacked(bsa); //calculate casualties

	//life drain handling
	if (att->hasBonusOfType(Bonus::LIFE_DRAIN) && def->isLiving())
	{
		StacksHealedOrResurrected shi;
		shi.lifeDrain = (ui8)true;
		shi.tentHealing = (ui8)false;
		shi.drainedFrom = def->ID;

		StacksHealedOrResurrected::HealInfo hi;
		hi.stackID = att->ID;
		hi.healedHP = std::min<int>(bsa.damageAmount, att->MaxHealth() - att->firstHPleft + att->MaxHealth() * (att->baseAmount - att->count) );
		hi.lowLevelResurrection = false;
		shi.healedStacks.push_back(hi);

		if (hi.healedHP > 0)
		{
			bsa.healedStacks.push_back(shi);
		}
	}
	bat.bsa.push_back(bsa); //add this stack to the list of victims after drain life has been calculated

	//fire shield handling
	if (!bat.shot() && def->hasBonusOfType(Bonus::FIRE_SHIELD) && !att->hasBonusOfType (Bonus::FIRE_IMMUNITY) && !bsa.killed() )
	{
		BattleStackAttacked bsa;
		bsa.stackAttacked = att->ID; //invert
		bsa.attackerID = def->ID;
		bsa.flags |= BattleStackAttacked::EFFECT;
		bsa.effect = 11;

		bsa.damageAmount = (bsa.damageAmount * def->valOfBonuses(Bonus::FIRE_SHIELD)) / 100;
		att->prepareAttacked(bsa);
		bat.bsa.push_back(bsa);
	}
}
void CGameHandler::handleConnection(std::set<int> players, CConnection &c)
{
	setThreadName("CGameHandler::handleConnection");
	srand(time(NULL));

	try
	{
		while(1)//server should never shut connection first //was: while(!end2)
		{
			CPack *pack = NULL;
			ui8 player = 255;
			si32 requestID = -999;
			int packType = 0;

			{
				boost::unique_lock<boost::mutex> lock(*c.rmx);
				c >> player >> requestID >> pack; //get the package
				packType = typeList.getTypeID(pack); //get the id of type

				tlog5 << boost::format("Received client message (request %d by player %d) of type with ID=%d (%s).\n")
					% requestID % (int)player % packType % typeid(*pack).name();
			}

			//prepare struct informing that action was applied
			PackageApplied applied;
			applied.player = player;
			applied.result = false;
			applied.packType = packType;
			applied.requestID = requestID;

			CBaseForGHApply *apply = applier->apps[packType]; //and appropriae applier object
			if(isBlockedByQueries(pack, packType, player))
			{
				complain(boost::str(boost::format("Player %d has to answer queries  before attempting any further actions (count=%d)!") % (int)player % states.getQueriesCount(player)));
				{
					boost::unique_lock<boost::mutex> lock(*c.wmx);
					c << &applied;
				}
			}
			else if(apply)
			{
				bool result = apply->applyOnGH(this,&c,pack, player);
				tlog5 << "Message successfully applied (result=" << result << ")!\n";

				//send confirmation that we've applied the package
				applied.result = result;
				{
					boost::unique_lock<boost::mutex> lock(*c.wmx);
					c << &applied;
				}
			}
			else
			{
				tlog1 << "Message cannot be applied, cannot find applier (unregistered type)!\n";
			}

			vstd::clear_pointer(pack);
		}
	}
	catch(boost::system::system_error &e) //for boost errors just log, not crash - probably client shut down connection
	{
		assert(!c.connected); //make sure that connection has been marked as broken
		tlog1 << e.what() << std::endl;
		end2 = true;
	}
	HANDLE_EXCEPTION(end2 = true);

	tlog1 << "Ended handling connection\n";
}

int CGameHandler::moveStack(int stack, BattleHex dest)
{
	int ret = 0;

	CStack *curStack = gs->curB->getStack(stack),
		*stackAtEnd = gs->curB->getStackT(dest);

	assert(curStack);
	assert(dest < GameConstants::BFIELD_SIZE);

	if (gs->curB->tacticDistance)
	{
		assert(gs->curB->isInTacticRange(dest));
	}

	//initing necessary tables
	bool accessibility[GameConstants::BFIELD_SIZE];
	std::vector<BattleHex> accessible = gs->curB->getAccessibility(curStack, false, NULL, true);
	for(int b=0; b<GameConstants::BFIELD_SIZE; ++b)
	{
		accessibility[b] = false;
	}
	for(int g=0; g<accessible.size(); ++g)
	{
		accessibility[accessible[g]] = true;
	}

	//shifting destination (if we have double wide stack and we can occupy dest but not be exactly there)
	if(!stackAtEnd && curStack->doubleWide() && !accessibility[dest])
	{
		if(curStack->attackerOwned)
		{
			if(accessibility[dest+1])
				dest += BattleHex::RIGHT;
		}
		else
		{
			if(accessibility[dest-1])
				dest += BattleHex::LEFT;
		}
	}

	if((stackAtEnd && stackAtEnd!=curStack && stackAtEnd->alive()) || !accessibility[dest])
		return 0;

	bool accessibilityWithOccupyable[GameConstants::BFIELD_SIZE];
	std::vector<BattleHex> accOc = gs->curB->getAccessibility(curStack, true, NULL, true);
	for(int b=0; b<GameConstants::BFIELD_SIZE; ++b)
	{
		accessibilityWithOccupyable[b] = false;
	}
	for(int g=0; g<accOc.size(); ++g)
	{
		accessibilityWithOccupyable[accOc[g]] = true;
	}

	//if(dists[dest] > curStack->creature->speed && !(stackAtEnd && dists[dest] == curStack->creature->speed+1)) //we can attack a stack if we can go to adjacent hex
	//	return false;

	std::pair< std::vector<BattleHex>, int > path = gs->curB->getPath(curStack->position, dest, accessibilityWithOccupyable, curStack->hasBonusOfType(Bonus::FLYING), curStack->doubleWide(), curStack->attackerOwned);

	ret = path.second;

	int creSpeed = gs->curB->tacticDistance ? GameConstants::BFIELD_SIZE : curStack->Speed();

	if(curStack->hasBonusOfType(Bonus::FLYING))
	{
		if(path.second <= creSpeed && path.first.size() > 0)
		{
			//inform clients about move
			BattleStackMoved sm;
			sm.stack = curStack->ID;
			std::vector<BattleHex> tiles;
			tiles.push_back(path.first[0]);
			sm.tilesToMove = tiles;
			sm.distance = path.second;
			sm.teleporting = false;
			sendAndApply(&sm);
		}
	}
	else //for non-flying creatures
	{
		// send one package with the creature path information

		shared_ptr<const CObstacleInstance> obstacle; //obstacle that interrupted movement
		std::vector<BattleHex> tiles;
		int tilesToMove = std::max((int)(path.first.size() - creSpeed), 0);
		int v = path.first.size()-1;

startWalking:
		for(; v >= tilesToMove; --v)
		{
			BattleHex hex = path.first[v];
			tiles.push_back(hex);

			if((obstacle = battleGetObstacleOnPos(hex, false)))
			{
				//we walked onto something, so we finalize this portion of stack movement check into obstacle
				break;
			}
		}

		if (tiles.size() > 0)
		{
			//commit movement
			BattleStackMoved sm;
			sm.stack = curStack->ID;
			sm.distance = path.second;
			sm.teleporting = false;
			sm.tilesToMove = tiles;
			sendAndApply(&sm);
		}

		//we don't handle obstacle at the destination tile -> it's handled separately in the if at the end
		if(obstacle && curStack->position != dest)
		{
			handleDamageFromObstacle(*obstacle, curStack);

			//if stack didn't die in explosion, continue movement
			if(!obstacle->stopsMovement() && curStack->alive())
			{
				obstacle.reset();
				tiles.clear();
				v--;
				goto startWalking; //TODO it's so evil
			}
		}
	}

	//handling obstacle on the final field (separate, because it affects both flying and walking stacks)
	if(curStack->alive())
	{
		if(auto theLastObstacle = battleGetObstacleOnPos(curStack->position, false))
		{
			handleDamageFromObstacle(*theLastObstacle, curStack);
		}
	}
	return ret;
}

CGameHandler::CGameHandler(void)
{
	QID = 1;
	//gs = NULL;
	IObjectInterface::cb = this;
	applier = new CApplier<CBaseForGHApply>;
	registerTypes3(*applier);
	visitObjectAfterVictory = false;
	battleEndCallback = NULL;
}

CGameHandler::~CGameHandler(void)
{
	delete applier;
	applier = NULL;
	delete gs;
}

void CGameHandler::init(StartInfo *si)
{
	//extern DLL_LINKAGE boost::rand48 ran;
	if(!si->seedToBeUsed)
		si->seedToBeUsed = std::time(NULL);

	gs = new CGameState();
	tlog0 << "Gamestate created!" << std::endl;
	gs->init(si);
	tlog0 << "Gamestate initialized!" << std::endl;

	for(std::map<ui8,PlayerState>::iterator i = gs->players.begin(); i != gs->players.end(); i++)
		states.addPlayer(i->first);
}

static bool evntCmp(const CMapEvent *a, const CMapEvent *b)
{
	return *a < *b;
}

void CGameHandler::setPortalDwelling(const CGTownInstance * town, bool forced=false, bool clear = false)
{// bool forced = true - if creature should be replaced, if false - only if no creature was set
	const PlayerState *p = gs->getPlayer(town->tempOwner);
	if(!p)
	{
		tlog3 << "There is no player owner of town " << town->name << " at " << town->pos << std::endl;
		return;
	}

	if (forced || town->creatures[GameConstants::CREATURES_PER_TOWN].second.empty())//we need to change creature
		{
			SetAvailableCreatures ssi;
			ssi.tid = town->id;
			ssi.creatures = town->creatures;
			ssi.creatures[GameConstants::CREATURES_PER_TOWN].second.clear();//remove old one

			const std::vector<ConstTransitivePtr<CGDwelling> > &dwellings = p->dwellings;
			if (dwellings.empty())//no dwellings - just remove
			{
				sendAndApply(&ssi);
				return;
			}

			ui32 dwellpos = rand()%dwellings.size();//take random dwelling
			ui32 creapos = rand()%dwellings[dwellpos]->creatures.size();//for multi-creature dwellings like Golem Factory
			ui32 creature = dwellings[dwellpos]->creatures[creapos].second[0];

			if (clear)
				ssi.creatures[GameConstants::CREATURES_PER_TOWN].first = std::max((ui32)1, (VLC->creh->creatures[creature]->growth)/2);
			else
				ssi.creatures[GameConstants::CREATURES_PER_TOWN].first = VLC->creh->creatures[creature]->growth;
			ssi.creatures[GameConstants::CREATURES_PER_TOWN].second.push_back(creature);
			sendAndApply(&ssi);
		}
}

void CGameHandler::newTurn()
{
	tlog5 << "Turn " << gs->day+1 << std::endl;
	NewTurn n;
	n.specialWeek = NewTurn::NO_ACTION;
	n.creatureid = -1;
	n.day = gs->day + 1;

	bool firstTurn = !getDate(0);
	bool newWeek = getDate(1) == 7; //day numbers are confusing, as day was not yet switched
	bool newMonth = getDate(4) == 28;

	std::map<ui8, si32> hadGold;//starting gold - for buildings like dwarven treasury
	srand(time(NULL));

	if (newWeek && !firstTurn)
	{
		n.specialWeek = NewTurn::NORMAL;
		bool deityOfFireBuilt = false;
		BOOST_FOREACH(const CGTownInstance *t, gs->map->towns)
		{
			if(t->subID == 3 && vstd::contains(t->builtBuildings, EBuilding::GRAIL))
			{
				deityOfFireBuilt = true;
				break;
			}
		}

		if(deityOfFireBuilt)
		{
			n.specialWeek = NewTurn::DEITYOFFIRE;
			n.creatureid = 42;
		}
		else
		{
			int monthType = rand()%100;
			if(newMonth) //new month
			{
				if (monthType < 40) //double growth
				{
					n.specialWeek = NewTurn::DOUBLE_GROWTH;
					if (ALLCREATURESGETDOUBLEMONTHS)
					{
						std::pair<int,int> newMonster(54, VLC->creh->pickRandomMonster(boost::ref(rand)));
						n.creatureid = newMonster.second;
					}
					else
					{
						std::set<TCreature>::const_iterator it = VLC->creh->doubledCreatures.begin();
						std::advance (it, rand() % VLC->creh->doubledCreatures.size()); //picking random element of set is tiring
						n.creatureid = *it;
					}
				}
				else if (monthType < 50)
					n.specialWeek = NewTurn::PLAGUE;
			}
			else //it's a week, but not full month
			{
				if (monthType < 25)
				{
					n.specialWeek = NewTurn::BONUS_GROWTH; //+5
					std::pair<int,int> newMonster (54, VLC->creh->pickRandomMonster(boost::ref(rand)));
					//TODO do not pick neutrals
					n.creatureid = newMonster.second;
				}
			}
		}
	}

	bmap<ui32, ConstTransitivePtr<CGHeroInstance> > pool = gs->hpool.heroesPool;

	for ( std::map<ui8, PlayerState>::iterator i=gs->players.begin() ; i!=gs->players.end();i++)
	{
		if(i->first == 255)
			continue;
		else if(i->first >= GameConstants::PLAYER_LIMIT)
			assert(0); //illegal player number!

		std::pair<ui8,si32> playerGold(i->first,i->second.resources[Res::GOLD]);
		hadGold.insert(playerGold);

		if(newWeek) //new heroes in tavern
		{
			SetAvailableHeroes sah;
			sah.player = i->first;

			//pick heroes and their armies
			CHeroClass *banned = NULL;
			for (int j = 0; j < GameConstants::AVAILABLE_HEROES_PER_PLAYER; j++)
			{
				if(CGHeroInstance *h = gs->hpool.pickHeroFor(j == 0, i->first, getNativeTown(i->first), pool, banned)) //first hero - native if possible, second hero -> any other class
				{
					sah.hid[j] = h->subID;
					h->initArmy(&sah.army[j]);
					banned = h->type->heroClass;
				}
				else
					sah.hid[j] = -1;
			}

			sendAndApply(&sah);
		}

		n.res[i->first] = i->second.resources;

		BOOST_FOREACH(CGHeroInstance *h, (*i).second.heroes)
		{
			if(h->visitedTown)
				giveSpells(h->visitedTown, h);

			NewTurn::Hero hth;
			hth.id = h->id;
			hth.move = h->maxMovePoints(gs->map->getTile(h->getPosition(false)).tertype != TerrainTile::water);

			if(h->visitedTown && vstd::contains(h->visitedTown->builtBuildings,0)) //if hero starts turn in town with mage guild
				hth.mana = std::max(h->mana, h->manaLimit()); //restore all mana
			else
				hth.mana = std::max((si32)(0), std::max(h->mana, std::min((si32)(h->mana + h->manaRegain()), h->manaLimit())));

			n.heroes.insert(hth);

			if(!firstTurn) //not first day
			{
				n.res[i->first][Res::GOLD] += h->valOfBonuses(Selector::typeSubtype(Bonus::SECONDARY_SKILL_PREMY, CGHeroInstance::ESTATES)); //estates

				for (int k = 0; k < GameConstants::RESOURCE_QUANTITY; k++)
				{
					n.res[i->first][k] += h->valOfBonuses(Bonus::GENERATE_RESOURCE, k);
				}
			}
		}
	}
	//      townID,    creatureID, amount
	std::map<si32, std::map<si32, si32> > newCreas;//creatures that needs to be added by town events

	BOOST_FOREACH(CGTownInstance *t, gs->map->towns)
	{
		ui8 player = t->tempOwner;
		handleTownEvents(t, n, newCreas);
		if(newWeek) //first day of week
		{
			if(t->subID == 5 && vstd::contains(t->builtBuildings, EBuilding::SPECIAL_3))
				setPortalDwelling(t, true, (n.specialWeek == NewTurn::PLAGUE ? true : false)); //set creatures for Portal of Summoning

			if(!firstTurn)
				if (t->subID == 1  && player < GameConstants::PLAYER_LIMIT && vstd::contains(t->builtBuildings, EBuilding::SPECIAL_3))//dwarven treasury
						n.res[player][Res::GOLD] += hadGold[player]/10; //give 10% of starting gold

			SetAvailableCreatures sac;
			sac.tid = t->id;
			sac.creatures = t->creatures;
			for (int k=0; k < GameConstants::CREATURES_PER_TOWN; k++) //creature growths
			{
				if(t->creatureDwelling(k))//there is dwelling (k-level)
				{
					ui32 &availableCount = sac.creatures[k].first;
					const CCreature *cre = VLC->creh->creatures[t->creatures[k].second.back()];

					if (n.specialWeek == NewTurn::PLAGUE)
						availableCount = t->creatures[k].first / 2; //halve their number, no growth
					else
					{
						if(firstTurn) //first day of game: use only basic growths
							availableCount = cre->growth;
						else
							availableCount += t->creatureGrowth(k);

						//Deity of fire week - upgrade both imps and upgrades
						if (n.specialWeek == NewTurn::DEITYOFFIRE && vstd::contains(t->creatures[k].second, n.creatureid))
							availableCount += 15;

						if( cre->idNumber == n.creatureid ) //bonus week, effect applies only to identical creatures
						{
							if(n.specialWeek == NewTurn::DOUBLE_GROWTH)
								availableCount *= 2;
							else if(n.specialWeek == NewTurn::BONUS_GROWTH)
								availableCount += 5;
						}
					}
				}
			}
			//add creatures from town events
			if (vstd::contains(newCreas, t->id))
				for(std::map<si32, si32>::iterator i=newCreas[t->id].begin() ; i!=newCreas[t->id].end(); i++)
					sac.creatures[i->first].first += i->second;

			n.cres.push_back(sac);
		}
		if(!firstTurn  &&  player < GameConstants::PLAYER_LIMIT)//not the first day and town not neutral
		{
			if(vstd::contains(t->builtBuildings, EBuilding::RESOURCE_SILO)) //there is resource silo
			{
				if(t->town->primaryRes == 127) //we'll give wood and ore
				{
					n.res[player][Res::WOOD] ++;
					n.res[player][Res::ORE] ++;
				}
				else
				{
					n.res[player][t->town->primaryRes] ++;
				}
			}

			n.res[player][Res::GOLD] += t->dailyIncome();
		}
		if(vstd::contains(t->builtBuildings, EBuilding::GRAIL) && t->subID == 2)
		{
			// Skyship, probably easier to handle same as Veil of darkness
			//do it every new day after veils apply
			FoWChange fw;
			fw.mode = 1;
			fw.player = player;
			getAllTiles(fw.tiles, player, -1, 0);
			sendAndApply (&fw);
		}
		if (t->hasBonusOfType (Bonus::DARKNESS))
		{
			t->hideTiles(t->getOwner(), t->getBonus(Selector::type(Bonus::DARKNESS))->val);
		}
		//unhiding what shouldn't be hidden? //that's handled in netpacks client
	}

	if(newMonth)
	{
		SetAvailableArtifacts saa;
		saa.id = -1;
		pickAllowedArtsSet(saa.arts);
		sendAndApply(&saa);
	}
	sendAndApply(&n);

	if(newWeek)
	{
		//spawn wandering monsters
		if (newMonth && (n.specialWeek == NewTurn::DOUBLE_GROWTH || n.specialWeek == NewTurn::DEITYOFFIRE))
		{
			spawnWanderingMonsters(n.creatureid);
		}

		//new week info popup
		if(!firstTurn)
		{
			InfoWindow iw;
			switch (n.specialWeek)
			{
				case NewTurn::DOUBLE_GROWTH:
					iw.text.addTxt(MetaString::ARRAY_TXT, 131);
					iw.text.addReplacement(MetaString::CRE_SING_NAMES, n.creatureid);
					iw.text.addReplacement(MetaString::CRE_SING_NAMES, n.creatureid);
					break;
				case NewTurn::PLAGUE:
					iw.text.addTxt(MetaString::ARRAY_TXT, 132);
					break;
				case NewTurn::BONUS_GROWTH:
					iw.text.addTxt(MetaString::ARRAY_TXT, 134);
					iw.text.addReplacement(MetaString::CRE_SING_NAMES, n.creatureid);
					iw.text.addReplacement(MetaString::CRE_SING_NAMES, n.creatureid);
					break;
				case NewTurn::DEITYOFFIRE:
					iw.text.addTxt(MetaString::ARRAY_TXT, 135);
					iw.text.addReplacement(MetaString::CRE_SING_NAMES, 42); //%s imp
					iw.text.addReplacement(MetaString::CRE_SING_NAMES, 42); //%s imp
					iw.text.addReplacement2(15);							//%+d 15
					iw.text.addReplacement(MetaString::CRE_SING_NAMES, 43); //%s familiar
					iw.text.addReplacement2(15);							//%+d 15
					break;
				default:
					if (newMonth)
					{
						iw.text.addTxt(MetaString::ARRAY_TXT, (130));
						iw.text.addReplacement(MetaString::ARRAY_TXT, 32 + rand()%10);
					}
					else
					{
						iw.text.addTxt(MetaString::ARRAY_TXT, (133));
						iw.text.addReplacement(MetaString::ARRAY_TXT, 43 + rand()%15);
					}
			}
			for (std::map<ui8, PlayerState>::iterator i=gs->players.begin() ; i!=gs->players.end(); i++)
			{
				iw.player = i->first;
				sendAndApply(&iw);
			}
		}
	}

	tlog5 << "Info about turn " << n.day << "has been sent!" << std::endl;
	handleTimeEvents();
	//call objects
	for(size_t i = 0; i<gs->map->objects.size(); i++)
	{
		if(gs->map->objects[i])
			gs->map->objects[i]->newTurn();
	}

	winLoseHandle(0xff);

	//warn players without town
	if(gs->day)
	{
		for (std::map<ui8, PlayerState>::iterator i=gs->players.begin() ; i!=gs->players.end();i++)
		{
			if(i->second.status || i->second.towns.size() || i->second.color >= GameConstants::PLAYER_LIMIT)
				continue;

			InfoWindow iw;
			iw.player = i->first;
			iw.components.push_back(Component(Component::FLAG,i->first,0,0));

			if(!i->second.daysWithoutCastle)
			{
				iw.text.addTxt(MetaString::GENERAL_TXT,6); //%s, you have lost your last town.  If you do not conquer another town in the next week, you will be eliminated.
				iw.text.addReplacement(MetaString::COLOR, i->first);
			}
			else if(i->second.daysWithoutCastle == 6)
			{
				iw.text.addTxt(MetaString::ARRAY_TXT,129); //%s, this is your last day to capture a town or you will be banished from this land.
				iw.text.addReplacement(MetaString::COLOR, i->first);
			}
			else
			{
				iw.text.addTxt(MetaString::ARRAY_TXT,128); //%s, you only have %d days left to capture a town or you will be banished from this land.
				iw.text.addReplacement(MetaString::COLOR, i->first);
				iw.text.addReplacement(7 - i->second.daysWithoutCastle);
			}
			sendAndApply(&iw);
		}
	}
}
void CGameHandler::run(bool resume)
{
	using namespace boost::posix_time;
	BOOST_FOREACH(CConnection *cc, conns)
	{//init conn.
		ui32 quantity;
		ui8 pom;
		//ui32 seed;
		if(!resume)
		{
			(*cc) << gs->initialOpts; // gs->scenarioOps
		}

		(*cc) >> quantity; //how many players will be handled at that client

		tlog0 << "Connection " << cc->connectionID << " will handle " << quantity << " player: ";
		for(int i=0;i<quantity;i++)
		{
			(*cc) >> pom; //read player color
			tlog0 << (int)pom << " ";
			{
				boost::unique_lock<boost::recursive_mutex> lock(gsm);
				connections[pom] = cc;
			}
		}
		tlog0 << std::endl;
	}

	for(std::set<CConnection*>::iterator i = conns.begin(); i!=conns.end();i++)
	{
		std::set<int> pom;
		for(std::map<int,CConnection*>::iterator j = connections.begin(); j!=connections.end();j++)
			if(j->second == *i)
				pom.insert(j->first);

		boost::thread(boost::bind(&CGameHandler::handleConnection,this,pom,boost::ref(**i)));
	}

	if(gs->scenarioOps->mode == StartInfo::DUEL)
	{
		runBattle();
		return;
	}

	while (!end2)
	{
		if(!resume)
			newTurn();

		std::map<ui8,PlayerState>::iterator i;
		if(!resume)
			i = gs->players.begin();
		else
			i = gs->players.find(gs->currentPlayer);

		resume = false;
		for(; i != gs->players.end(); i++)
		{
			if((i->second.towns.size()==0 && i->second.heroes.size()==0)
				|| i->first>=GameConstants::PLAYER_LIMIT
				|| i->second.status)
			{
				continue;
			}
			states.setFlag(i->first,&PlayerStatus::makingTurn,true);

			{
				YourTurn yt;
				yt.player = i->first;
				applyAndSend(&yt);
			}

			//wait till turn is done
			boost::unique_lock<boost::mutex> lock(states.mx);
			while(states.players[i->first].makingTurn && !end2)
			{
				static time_duration p = milliseconds(200);
				states.cv.timed_wait(lock,p);

			}
		}
	}
	while(conns.size() && (*conns.begin())->isOpen())
		boost::this_thread::sleep(boost::posix_time::milliseconds(5)); //give time client to close socket
}

void CGameHandler::setupBattle( int3 tile, const CArmedInstance *armies[2], const CGHeroInstance *heroes[2], bool creatureBank, const CGTownInstance *town )
{
	battleResult.set(NULL);

	//send info about battles
	BattleStart bs;
	bs.info = gs->setupBattle(tile, armies, heroes, creatureBank,	town);
	sendAndApply(&bs);
}

void CGameHandler::checkForBattleEnd( std::vector<CStack*> &stacks )
{
	//checking winning condition
	bool hasStack[2]; //hasStack[0] - true if attacker has a living stack; defender similarly
	hasStack[0] = hasStack[1] = false;
	for(int b = 0; b<stacks.size(); ++b)
	{
		if(stacks[b]->alive() && !stacks[b]->hasBonusOfType(Bonus::SIEGE_WEAPON))
		{
			hasStack[1-stacks[b]->attackerOwned] = true;
		}
	}
	if(!hasStack[0] || !hasStack[1]) //somebody has won
	{
		setBattleResult(0, hasStack[1]);
	}
}

void CGameHandler::giveSpells( const CGTownInstance *t, const CGHeroInstance *h )
{
	if(!h->hasSpellbook())
		return; //hero hasn't spellbok
	ChangeSpells cs;
	cs.hid = h->id;
	cs.learn = true;
	for(int i=0; i<std::min(t->mageGuildLevel(),h->getSecSkillLevel(CGHeroInstance::WISDOM)+2);i++)
	{
		if (t->subID == 8 && vstd::contains(t->builtBuildings, EBuilding::GRAIL)) //Aurora Borealis
		{
			std::vector<ui16> spells;
			getAllowedSpells(spells, i);
			for (int j = 0; j < spells.size(); ++j)
				cs.spells.insert(spells[j]);
		}
		else
		{
			for(int j=0; j<t->spellsAtLevel(i+1,true) && j<t->spells[i].size(); j++)
			{
				if(!vstd::contains(h->spells,t->spells[i][j]))
					cs.spells.insert(t->spells[i][j]);
			}
		}
	}
	if(cs.spells.size())
		sendAndApply(&cs);
}

void CGameHandler::setBlockVis(int objid, bool bv)
{
	SetObjectProperty sop(objid,2,bv);
	sendAndApply(&sop);
}

bool CGameHandler::removeObject( int objid )
{
	if(!getObj(objid))
	{
		tlog1 << "Something wrong, that object already has been removed or hasn't existed!\n";
		return false;
	}

	RemoveObject ro;
	ro.id = objid;
	sendAndApply(&ro);

	winLoseHandle(255); //eg if monster escaped (removing objs after battle is done dircetly by endBattle, not this function)
	return true;
}

void CGameHandler::setAmount(int objid, ui32 val)
{
	SetObjectProperty sop(objid,3,val);
	sendAndApply(&sop);
}

bool CGameHandler::moveHero( si32 hid, int3 dst, ui8 instant, ui8 asker /*= 255*/ )
{
	bool blockvis = false;
	const CGHeroInstance *h = getHero(hid);

	if(!h  || (asker != 255 && (instant  ||   h->getOwner() != gs->currentPlayer)) //not turn of that hero or player can't simply teleport hero (at least not with this function)
	  )
	{
		tlog1 << "Illegal call to move hero!\n";
		return false;
	}


	tlog5 << "Player " <<int(asker) << " wants to move hero "<< hid << " from "<< h->pos << " to " << dst << std::endl;
	int3 hmpos = dst + int3(-1,0,0);

	if(!gs->map->isInTheMap(hmpos))
	{
		tlog1 << "Destination tile is outside the map!\n";
		return false;
	}

	TerrainTile t = gs->map->terrain[hmpos.x][hmpos.y][hmpos.z];
	int cost = gs->getMovementCost(h, h->getPosition(false), CGHeroInstance::convertPosition(dst,false),h->movement);
	int3 guardPos = gs->guardingCreaturePosition(hmpos);

	//result structure for start - movement failed, no move points used
	TryMoveHero tmh;
	tmh.id = hid;
	tmh.start = h->pos;
	tmh.end = dst;
	tmh.result = TryMoveHero::FAILED;
	tmh.movePoints = h->movement;

	//check if destination tile is available

	//it's a rock or blocked and not visitable tile
	//OR hero is on land and dest is water and (there is not present only one object - boat)
	if(((t.tertype == TerrainTile::rock  ||  (t.blocked && !t.visitable && !h->hasBonusOfType(Bonus::FLYING_MOVEMENT) ))
			&& complain("Cannot move hero, destination tile is blocked!"))
		|| ((!h->boat && !h->canWalkOnSea() && t.tertype == TerrainTile::water && (t.visitableObjects.size() < 1 ||  (t.visitableObjects.back()->ID != 8 && t.visitableObjects.back()->ID != GameConstants::HEROI_TYPE)))  //hero is not on boat/water walking and dst water tile doesn't contain boat/hero (objs visitable from land) -> we test back cause boat may be on top of another object (#276)
			&& complain("Cannot move hero, destination tile is on water!"))
		|| ((h->boat && t.tertype != TerrainTile::water && t.blocked)
			&& complain("Cannot disembark hero, tile is blocked!"))
		|| ((h->movement < cost  &&  dst != h->pos  &&  !instant)
			&& complain("Hero doesn't have any movement points left!"))
		|| (states.checkFlag(h->tempOwner, &PlayerStatus::engagedIntoBattle)
			&& complain("Cannot move hero during the battle")))
	{
		//send info about movement failure
		sendAndApply(&tmh);
		return false;
	}

	//hero enters the boat
	if(!h->boat && t.visitableObjects.size() && t.visitableObjects.back()->ID == 8)
	{
		tmh.result = TryMoveHero::EMBARK;
		tmh.movePoints = h->movementPointsAfterEmbark(h->movement, cost, false);
		getTilesInRange(tmh.fowRevealed,h->getSightCenter()+(tmh.end-tmh.start),h->getSightRadious(),h->tempOwner,1);
		sendAndApply(&tmh);
		return true;
	}
	//hero leaves the boat
	else if(h->boat && t.tertype != TerrainTile::water && !t.blocked)
	{
		//TODO? code similarity with the block above
		tmh.result = TryMoveHero::DISEMBARK;
		tmh.movePoints = h->movementPointsAfterEmbark(h->movement, cost, true);
		getTilesInRange(tmh.fowRevealed,h->getSightCenter()+(tmh.end-tmh.start),h->getSightRadious(),h->tempOwner,1);
		sendAndApply(&tmh);
		tryAttackingGuard(guardPos, h);
		return true;
	}

	//checks for standard movement
	if(!instant)
	{
		if( (distance(h->pos,dst) >= 1.5  &&  complain("Tiles are not neighboring!"))
			|| (h->movement < cost  &&  h->movement < 100  &&  complain("Not enough move points!")))
		{
			sendAndApply(&tmh);
			return false;
		}

		//check if there is blocking visitable object
		blockvis = false;
		tmh.movePoints = std::max((ui32)(0),h->movement-cost); //take move points
		BOOST_FOREACH(CGObjectInstance *obj, t.visitableObjects)
		{
			if(obj != h  &&  obj->blockVisit  &&  !(obj->getPassableness() & 1<<h->tempOwner))
			{
				blockvis = true;
				break;
			}
		}
		//we start moving
		if(blockvis)//interaction with blocking object (like resources)
		{
			tmh.result = TryMoveHero::BLOCKING_VISIT;
			sendAndApply(&tmh);
			//failed to move to that tile but we visit object
			if(t.visitableObjects.size())
				objectVisited(t.visitableObjects.back(), h);

			tlog5 << "Blocking visit at " << hmpos << std::endl;
			return true;
		}
		else //normal move
		{
			BOOST_FOREACH(CGObjectInstance *obj, gs->map->terrain[h->pos.x-1][h->pos.y][h->pos.z].visitableObjects)
			{
				obj->onHeroLeave(h);
			}
			getTilesInRange(tmh.fowRevealed,h->getSightCenter()+(tmh.end-tmh.start),h->getSightRadious(),h->tempOwner,1);

			tmh.result = TryMoveHero::SUCCESS;
			tmh.attackedFrom = guardPos;
			sendAndApply(&tmh);
			tlog5 << "Moved to " <<tmh.end<<std::endl;

			// If a creature guards the tile, block visit.
			const bool fightingGuard = tryAttackingGuard(guardPos, h);

			if(!fightingGuard && t.visitableObjects.size()) //call objects if they are visited
			{
				visitObjectOnTile(t, h);
			}

			tlog5 << "Movement end!\n";
			return true;
		}
	}
	else //instant move - teleportation
	{
		BOOST_FOREACH(CGObjectInstance* obj, t.blockingObjects)
		{
			if(obj->ID==GameConstants::HEROI_TYPE)
			{
				CGHeroInstance *dh = static_cast<CGHeroInstance *>(obj);

				if( gameState()->getPlayerRelations(dh->tempOwner, h->tempOwner))
				{
					heroExchange(h->id, dh->id);
					return true;
				}
				startBattleI(h, dh);
				return true;
			}
		}
		tmh.result = TryMoveHero::TELEPORTATION;
		getTilesInRange(tmh.fowRevealed,h->getSightCenter()+(tmh.end-tmh.start),h->getSightRadious(),h->tempOwner,1);
		sendAndApply(&tmh);
		return true;
	}
}

bool CGameHandler::teleportHero(si32 hid, si32 dstid, ui8 source, ui8 asker/* = 255*/)
{
	const CGHeroInstance *h = getHero(hid);
	const CGTownInstance *t = getTown(dstid);

	if ( !h || !t || h->getOwner() != gs->currentPlayer )
		tlog1<<"Invalid call to teleportHero!";

	const CGTownInstance *from = h->visitedTown;
	if(((h->getOwner() != t->getOwner())
		&& complain("Cannot teleport hero to another player"))
	|| ((!from || from->subID!=3 || !vstd::contains(from->builtBuildings, EBuilding::SPECIAL_3))
		&& complain("Hero must be in town with Castle gate for teleporting"))
	|| ((t->subID!=3 || !vstd::contains(t->builtBuildings, EBuilding::SPECIAL_3))
		&& complain("Cannot teleport hero to town without Castle gate in it")))
			return false;
	int3 pos = t->visitablePos();
	pos += h->getVisitableOffset();
	stopHeroVisitCastle(from->id, hid);
	moveHero(hid,pos,1);
	heroVisitCastle(dstid, hid);
	return true;
}

void CGameHandler::setOwner(int objid, ui8 owner)
{
	ui8 oldOwner = getOwner(objid);
	SetObjectProperty sop(objid,1,owner);
	sendAndApply(&sop);

	winLoseHandle(1<<owner | 1<<oldOwner);
	if(owner < GameConstants::PLAYER_LIMIT && getTown(objid)) //town captured
	{
		const CGTownInstance * town = getTown(objid);
		if (town->subID == 5 && vstd::contains(town->builtBuildings, 22))
			setPortalDwelling(town, true, false);

		if (!gs->getPlayer(owner)->towns.size())//player lost last town
		{
			InfoWindow iw;
			iw.player = oldOwner;
			iw.text.addTxt(MetaString::GENERAL_TXT, 6); //%s, you have lost your last town.  If you do not conquer another town in the next week, you will be eliminated.
			sendAndApply(&iw);
		}
	}

	const CGObjectInstance * obj = getObj(objid);
	const PlayerState * p = gs->getPlayer(owner);

	if((obj->ID == 17 || obj->ID == 20 ) && p && p->dwellings.size()==1)//first dwelling captured
	{
		BOOST_FOREACH(const CGTownInstance *t, gs->getPlayer(owner)->towns)
		{
			if (t->subID == 5 && vstd::contains(t->builtBuildings, 22))
				setPortalDwelling(t);//set initial creatures for all portals of summoning
		}
	}
}

void CGameHandler::setHoverName(int objid, MetaString* name)
{
	SetHoverName shn(objid, *name);
	sendAndApply(&shn);
}

void CGameHandler::showBlockingDialog( BlockingDialog *iw, const CFunctionList<void(ui32)> &callback )
{
	ask(iw,iw->player,callback);
}

ui32 CGameHandler::showBlockingDialog( BlockingDialog *iw )
{
	//TODO

	//gsm.lock();
	//int query = QID++;
	//states.addQuery(player,query);
	//sendToAllClients(iw);
	//gsm.unlock();
	//ui32 ret = getQueryResult(iw->player, query);
	//gsm.lock();
	//states.removeQuery(player, query);
	//gsm.unlock();
	return 0;
}

void CGameHandler::giveResource(int player, int which, int val) //TODO: cap according to Bersy's suggestion
{
	if(!val) return; //don't waste time on empty call
	SetResource sr;
	sr.player = player;
	sr.resid = which;
	sr.val = gs->players.find(player)->second.resources[which] + val;
	sendAndApply(&sr);
}

void CGameHandler::giveCreatures(const CArmedInstance *obj, const CGHeroInstance * h, const CCreatureSet &creatures, bool remove)
{
	boost::function<void()> removeOrNot = 0;
	if(remove)
		removeOrNot = boost::bind(&CGameHandler::removeObject, this, obj->id);

	COMPLAIN_RET_IF(!creatures.stacksCount(), "Strange, giveCreatures called without args!");
	COMPLAIN_RET_IF(obj->stacksCount(), "Cannot give creatures from not-cleared object!");
	COMPLAIN_RET_IF(creatures.stacksCount() > GameConstants::ARMY_SIZE, "Too many stacks to give!");

	//first we move creatures to give to make them army of object-source
	for (TSlots::const_iterator stack = creatures.Slots().begin(); stack != creatures.Slots().end(); stack++)
	{
		addToSlot(StackLocation(obj, obj->getSlotFor(stack->second->type)), stack->second->type, stack->second->count);
	}

	tryJoiningArmy(obj, h, remove, true);
}

void CGameHandler::takeCreatures(int objid, const std::vector<CStackBasicDescriptor> &creatures)
{
	std::vector<CStackBasicDescriptor> cres = creatures;
	if (cres.size() <= 0)
		return;
	const CArmedInstance* obj = static_cast<const CArmedInstance*>(getObj(objid));

	BOOST_FOREACH(CStackBasicDescriptor &sbd, cres)
	{
		TQuantity collected = 0;
		while(collected < sbd.count)
		{
			TSlots::const_iterator i = obj->Slots().begin();
			for(; i != obj->Slots().end(); i++)
			{
				if(i->second->type == sbd.type)
				{
					TQuantity take = std::min(sbd.count - collected, i->second->count); //collect as much cres as we can
					changeStackCount(StackLocation(obj, i->first), -take, false);
					collected += take;
					break;
				}
			}

			if(i ==  obj->Slots().end()) //we went through the whole loop and haven't found appropriate cres
			{
				complain("Unexpected failure during taking creatures!");
				return;
			}
		}
	}
}

void CGameHandler::showCompInfo(ShowInInfobox * comp)
{
	sendToAllClients(comp);
}
void CGameHandler::heroVisitCastle(int obj, int heroID)
{
	HeroVisitCastle vc;
	vc.hid = heroID;
	vc.tid = obj;
	vc.flags |= 1;
	sendAndApply(&vc);
	const CGHeroInstance *h = getHero(heroID);
	vistiCastleObjects (getTown(obj), h);
	giveSpells (getTown(obj), getHero(heroID));

	if(gs->map->victoryCondition.condition == EVictoryConditionType::TRANSPORTITEM)
		checkLossVictory(h->tempOwner); //transported artifact?
}

void CGameHandler::vistiCastleObjects (const CGTownInstance *t, const CGHeroInstance *h)
{
	std::vector<CGTownBuilding*>::const_iterator i;
	for (i = t->bonusingBuildings.begin(); i != t->bonusingBuildings.end(); i++)
		(*i)->onHeroVisit (h);
}

void CGameHandler::stopHeroVisitCastle(int obj, int heroID)
{
	HeroVisitCastle vc;
	vc.hid = heroID;
	vc.tid = obj;
	sendAndApply(&vc);
}

void CGameHandler::removeArtifact(const ArtifactLocation &al)
{
	assert(al.getArt());
	EraseArtifact ea;
	ea.al = al;
	sendAndApply(&ea);
}
void CGameHandler::startBattleI(const CArmedInstance *army1, const CArmedInstance *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, bool creatureBank, boost::function<void(BattleResult*)> cb, const CGTownInstance *town) //use hero=NULL for no hero
{
	engageIntoBattle(army1->tempOwner);
	engageIntoBattle(army2->tempOwner);
	//block engaged players
	if(army2->tempOwner < GameConstants::PLAYER_LIMIT)
		states.setFlag(army2->tempOwner,&PlayerStatus::engagedIntoBattle,true);

	static const CArmedInstance *armies[2];
	armies[0] = army1;
	armies[1] = army2;
	static const CGHeroInstance*heroes[2];
	heroes[0] = hero1;
	heroes[1] = hero2;

	boost::thread(boost::bind(&CGameHandler::startBattle, this, armies, tile, heroes, creatureBank, cb, town));
}

void CGameHandler::startBattleI( const CArmedInstance *army1, const CArmedInstance *army2, int3 tile, boost::function<void(BattleResult*)> cb, bool creatureBank )
{
	startBattleI(army1, army2, tile,
		army1->ID == GameConstants::HEROI_TYPE ? static_cast<const CGHeroInstance*>(army1) : NULL,
		army2->ID == GameConstants::HEROI_TYPE ? static_cast<const CGHeroInstance*>(army2) : NULL,
		creatureBank, cb);
}

void CGameHandler::startBattleI( const CArmedInstance *army1, const CArmedInstance *army2, boost::function<void(BattleResult*)> cb, bool creatureBank)
{
	startBattleI(army1, army2, army2->visitablePos(), cb, creatureBank);
}

void CGameHandler::changeSpells( int hid, bool give, const std::set<ui32> &spells )
{
	ChangeSpells cs;
	cs.hid = hid;
	cs.spells = spells;
	cs.learn = give;
	sendAndApply(&cs);
}

void CGameHandler::sendMessageTo( CConnection &c, const std::string &message )
{
	SystemMessage sm;
	sm.text = message;
	boost::unique_lock<boost::mutex> lock(*c.wmx);
	c << &sm;
}

void CGameHandler::giveHeroBonus( GiveBonus * bonus )
{
	sendAndApply(bonus);
}

void CGameHandler::setMovePoints( SetMovePoints * smp )
{
	sendAndApply(smp);
}

void CGameHandler::setManaPoints( int hid, int val )
{
	SetMana sm;
	sm.hid = hid;
	sm.val = val;
	sendAndApply(&sm);
}

void CGameHandler::giveHero( int id, int player )
{
	GiveHero gh;
	gh.id = id;
	gh.player = player;
	sendAndApply(&gh);
}

void CGameHandler::changeObjPos( int objid, int3 newPos, ui8 flags )
{
	ChangeObjPos cop;
	cop.objid = objid;
	cop.nPos = newPos;
	cop.flags = flags;
	sendAndApply(&cop);
}

void CGameHandler::useScholarSkill(si32 fromHero, si32 toHero)
{
	const CGHeroInstance * h1 = getHero(fromHero);
	const CGHeroInstance * h2 = getHero(toHero);

	if ( h1->getSecSkillLevel(CGHeroInstance::SCHOLAR) < h2->getSecSkillLevel(CGHeroInstance::SCHOLAR) )
	{
		std::swap (h1,h2);//1st hero need to have higher scholar level for correct message
		std::swap(fromHero, toHero);
	}

	int ScholarLevel = h1->getSecSkillLevel(CGHeroInstance::SCHOLAR);//heroes can trade up to this level
	if (!ScholarLevel || !h1->hasSpellbook() || !h2->hasSpellbook() )
		return;//no scholar skill or no spellbook

	int h1Lvl = std::min(ScholarLevel+1, h1->getSecSkillLevel(CGHeroInstance::WISDOM)+2),
	    h2Lvl = std::min(ScholarLevel+1, h2->getSecSkillLevel(CGHeroInstance::WISDOM)+2);//heroes can receive this levels

	ChangeSpells cs1;
	cs1.learn = true;
	cs1.hid = toHero;//giving spells to first hero
		for(std::set<ui32>::const_iterator it=h1->spells.begin(); it!=h1->spells.end();it++)
			if ( h2Lvl >= VLC->spellh->spells[*it]->level && !vstd::contains(h2->spells, *it))//hero can learn it and don't have it yet
				cs1.spells.insert(*it);//spell to learn

	ChangeSpells cs2;
	cs2.learn = true;
	cs2.hid = fromHero;

	for(std::set<ui32>::const_iterator it=h2->spells.begin(); it!=h2->spells.end();it++)
		if ( h1Lvl >= VLC->spellh->spells[*it]->level && !vstd::contains(h1->spells, *it))
			cs2.spells.insert(*it);

	if (cs1.spells.size() || cs2.spells.size())//create a message
	{
		InfoWindow iw;
		iw.player = h1->tempOwner;
		iw.components.push_back(Component(Component::SEC_SKILL, 18, ScholarLevel, 0));

		iw.text.addTxt(MetaString::GENERAL_TXT, 139);//"%s, who has studied magic extensively,
		iw.text.addReplacement(h1->name);

		if (cs2.spells.size())//if found new spell - apply
		{
			iw.text.addTxt(MetaString::GENERAL_TXT, 140);//learns
			int size = cs2.spells.size();
			for(std::set<ui32>::const_iterator it=cs2.spells.begin(); it!=cs2.spells.end();it++)
			{
				iw.components.push_back(Component(Component::SPELL, (*it), 1, 0));
				iw.text.addTxt(MetaString::SPELL_NAME, (*it));
				switch (size--)
				{
					case 2:	iw.text.addTxt(MetaString::GENERAL_TXT, 141);
					case 1:	break;
					default:	iw.text << ", ";
				}
			}
			iw.text.addTxt(MetaString::GENERAL_TXT, 142);//from %s
			iw.text.addReplacement(h2->name);
			sendAndApply(&cs2);
		}

		if (cs1.spells.size() && cs2.spells.size() )
		{
			iw.text.addTxt(MetaString::GENERAL_TXT, 141);//and
		}

		if (cs1.spells.size())
		{
			iw.text.addTxt(MetaString::GENERAL_TXT, 147);//teaches
			int size = cs1.spells.size();
			for(std::set<ui32>::const_iterator it=cs1.spells.begin(); it!=cs1.spells.end();it++)
			{
				iw.components.push_back(Component(Component::SPELL, (*it), 1, 0));
				iw.text.addTxt(MetaString::SPELL_NAME, (*it));
				switch (size--)
				{
					case 2:	iw.text.addTxt(MetaString::GENERAL_TXT, 141);
					case 1:	break;
					default:	iw.text << ", ";
				}			}
			iw.text.addTxt(MetaString::GENERAL_TXT, 148);//from %s
			iw.text.addReplacement(h2->name);
			sendAndApply(&cs1);
		}
		sendAndApply(&iw);
	}
}

void CGameHandler::heroExchange(si32 hero1, si32 hero2)
{
	ui8 player1 = getHero(hero1)->tempOwner;
	ui8 player2 = getHero(hero2)->tempOwner;

	if( gameState()->getPlayerRelations( player1, player2))
	{
		OpenWindow hex;
		hex.window = OpenWindow::EXCHANGE_WINDOW;
		hex.id1 = hero1;
		hex.id2 = hero2;
		sendAndApply(&hex);
		useScholarSkill(hero1,hero2);
	}
}

void CGameHandler::prepareNewQuery(Query * queryPack, ui8 player, const boost::function<void(ui32)> &callback)
{
	boost::unique_lock<boost::recursive_mutex> lock(gsm);
	tlog4 << "Creating a query for player " << (int)player << " with ID=" << QID << std::endl;
	callbacks[QID] = callback;
	states.addQuery(player, QID);
	queryPack->queryID = QID;
	QID++;
}

void CGameHandler::applyAndAsk( Query * sel, ui8 player, boost::function<void(ui32)> &callback )
{
	boost::unique_lock<boost::recursive_mutex> lock(gsm);
	prepareNewQuery(sel, player, callback);
	sendAndApply(sel);
}

void CGameHandler::ask( Query * sel, ui8 player, const CFunctionList<void(ui32)> &callback )
{
	boost::unique_lock<boost::recursive_mutex> lock(gsm);
	prepareNewQuery(sel, player, callback);
	sendToAllClients(sel);
}

void CGameHandler::sendToAllClients( CPackForClient * info )
{
	tlog5 << "Sending to all clients a package of type " << typeid(*info).name() << std::endl;
	for(std::set<CConnection*>::iterator i=conns.begin(); i!=conns.end();i++)
	{
		boost::unique_lock<boost::mutex> lock(*(*i)->wmx);
		**i << info;
	}
}

void CGameHandler::sendAndApply(CPackForClient * info)
{
	sendToAllClients(info);
	gs->apply(info);
}

void CGameHandler::applyAndSend(CPackForClient * info)
{
	gs->apply(info);
	sendToAllClients(info);
}

void CGameHandler::sendAndApply(CGarrisonOperationPack * info)
{
	sendAndApply((CPackForClient*)info);
	if(gs->map->victoryCondition.condition == EVictoryConditionType::GATHERTROOP)
		winLoseHandle();
}

// void CGameHandler::sendAndApply( SetGarrisons * info )
// {
// 	sendAndApply((CPackForClient*)info);
// 	if(gs->map->victoryCondition.condition == gatherTroop)
// 		for(std::map<ui32,CCreatureSet>::const_iterator i = info->garrs.begin(); i != info->garrs.end(); i++)
// 			checkLossVictory(getObj(i->first)->tempOwner);
// }

void CGameHandler::sendAndApply( SetResource * info )
{
	sendAndApply((CPackForClient*)info);
	if(gs->map->victoryCondition.condition == EVictoryConditionType::GATHERRESOURCE)
		checkLossVictory(info->player);
}

void CGameHandler::sendAndApply( SetResources * info )
{
	sendAndApply((CPackForClient*)info);
	if(gs->map->victoryCondition.condition == EVictoryConditionType::GATHERRESOURCE)
		checkLossVictory(info->player);
}

void CGameHandler::sendAndApply( NewStructures * info )
{
	sendAndApply((CPackForClient*)info);
	if(gs->map->victoryCondition.condition == EVictoryConditionType::BUILDCITY)
		checkLossVictory(getTown(info->tid)->tempOwner);
}

void CGameHandler::save( const std::string &fname )
{
	{
		tlog0 << "Ordering clients to serialize...\n";
		SaveGame sg(fname);

		sendToAllClients(&sg);
	}

	try
	{
		{
			tlog0 << "Serializing game info...\n";
			CSaveFile save(GVCMIDirs.UserPath + "/Games/" + fname + ".vlgm1");
			char hlp[8] = "VCMISVG";
			save << hlp << static_cast<CMapHeader&>(*gs->map) << gs->scenarioOps << *VLC << gs;
		}

		{
			tlog0 << "Serializing server info...\n";
			CSaveFile save(GVCMIDirs.UserPath + "/Games/" + fname + ".vsgm1");
			save << *this;
		}
		tlog0 << "Game has been successfully saved!\n";
	}
	catch(std::exception &e)
	{
		tlog1 << "Failed to save game: " << e.what() << std::endl;
	}
}

void CGameHandler::close()
{
	tlog0 << "We have been requested to close.\n";

	if(gs->initialOpts->mode == StartInfo::DUEL)
	{
		exit(0);
	}

	//BOOST_FOREACH(CConnection *cc, conns)
	//	if(cc && cc->socket && cc->socket->is_open())
	//		cc->socket->close();
	//exit(0);
}

bool CGameHandler::arrangeStacks( si32 id1, si32 id2, ui8 what, ui8 p1, ui8 p2, si32 val, ui8 player )
{
	const CArmedInstance *s1 = static_cast<CArmedInstance*>(gs->map->objects[id1].get()),
		*s2 = static_cast<CArmedInstance*>(gs->map->objects[id2].get());
	const CCreatureSet &S1 = *s1, &S2 = *s2;
	StackLocation sl1(s1, p1), sl2(s2, p2);

	if(!isAllowedExchange(id1,id2))
	{
		complain("Cannot exchange stacks between these two objects!\n");
		return false;
	}

	if(what==1) //swap
	{
		if ( ((s1->tempOwner != player && s1->tempOwner != 254) && s1->getStackCount(p1)) //why 254??
		  || ((s2->tempOwner != player && s2->tempOwner != 254) && s2->getStackCount(p2)))
		{
			complain("Can't take troops from another player!");
			return false;
		}

		swapStacks(sl1, sl2);
	}
	else if(what==2)//merge
	{
		if (( s1->getCreature(p1) != s2->getCreature(p2) && complain("Cannot merge different creatures stacks!"))
		|| (((s1->tempOwner != player && s1->tempOwner != 254) && s2->getStackCount(p2)) && complain("Can't take troops from another player!")))
			return false;

		moveStack(sl1, sl2);
	}
	else if(what==3) //split
	{
		if ( (s1->tempOwner != player && s1->getStackCount(p1) < s1->getStackCount(p1) )
			|| (s2->tempOwner != player && s2->getStackCount(p2) < s2->getStackCount(p2) ) )
		{
			complain("Can't move troops of another player!");
			return false;
		}

		//general conditions checking
		if((!vstd::contains(S1.stacks,p1) && complain("no creatures to split"))
			|| (val<1  && complain("no creatures to split"))  )
		{
			return false;
		}


		if(vstd::contains(S2.stacks,p2))	 //dest. slot not free - it must be "rebalancing"...
		{
			int total = s1->getStackCount(p1) + s2->getStackCount(p2);
			if( (total < val   &&   complain("Cannot split that stack, not enough creatures!"))
				|| (s1->getCreature(p1) != s2->getCreature(p2) && complain("Cannot rebalance different creatures stacks!"))
			)
			{
				return false;
			}

			moveStack(sl1, sl2, val - s2->getStackCount(p2));
			//S2.slots[p2]->count = val;
			//S1.slots[p1]->count = total - val;
		}
		else //split one stack to the two
		{
			if(s1->getStackCount(p1) < val)//not enough creatures
			{
				complain("Cannot split that stack, not enough creatures!");
				return false;
			}


			moveStack(sl1, sl2, val);
		}

	}
	return true;
}

int CGameHandler::getPlayerAt( CConnection *c ) const
{
	std::set<int> all;
	for(std::map<int,CConnection*>::const_iterator i=connections.begin(); i!=connections.end(); i++)
		if(i->second == c)
			all.insert(i->first);

	switch(all.size())
	{
	case 0:
		return 255;
	case 1:
		return *all.begin();
	default:
		{
			//if we have more than one player at this connection, try to pick active one
			if(vstd::contains(all,int(gs->currentPlayer)))
				return gs->currentPlayer;
			else
				return 253; //cannot say which player is it
		}
	}
}

bool CGameHandler::disbandCreature( si32 id, ui8 pos )
{
	CArmedInstance *s1 = static_cast<CArmedInstance*>(gs->map->objects[id].get());
	if(!vstd::contains(s1->stacks,pos))
	{
		complain("Illegal call to disbandCreature - no such stack in army!");
		return false;
	}

	eraseStack(StackLocation(s1, pos));
	return true;
}

bool CGameHandler::buildStructure( si32 tid, si32 bid, bool force /*=false*/ )
{
	CGTownInstance * t = static_cast<CGTownInstance*>(gs->map->objects[tid].get());
	CBuilding * b = VLC->buildh->buildings[t->subID][bid];

	if( !force && gs->canBuildStructure(t,bid) != 7)
	{
		complain("Cannot build that building!");
		return false;
	}

	if( !force && bid == 26) //grail
	{
		if(!t->visitingHero || !t->visitingHero->hasArt(2))
		{
			complain("Cannot build grail - hero doesn't have it");
			return false;
		}

		//remove grail
		removeArtifact(ArtifactLocation(t->visitingHero, t->visitingHero->getArtPos(2, false)));
	}

	NewStructures ns;
	ns.tid = tid;
	if ( (bid == 18) && (vstd::contains(t->builtBuildings,(t->town->hordeLvl[0]+37))) )
		ns.bid.insert(19);//we have upgr. dwelling, upgr. horde will be builded as well
	else if ( (bid == 24) && (vstd::contains(t->builtBuildings,(t->town->hordeLvl[1]+37))) )
		ns.bid.insert(25);
	else if(bid>36) //upg dwelling
	{
		if ( (bid-37 == t->town->hordeLvl[0]) && (vstd::contains(t->builtBuildings,18)) )
			ns.bid.insert(19);//we have horde, will be upgraded as well as dwelling
		if ( (bid-37 == t->town->hordeLvl[1]) && (vstd::contains(t->builtBuildings,24)) )
			ns.bid.insert(25);

		SetAvailableCreatures ssi;
		ssi.tid = tid;
		ssi.creatures = t->creatures;
		ssi.creatures[bid-37].second.push_back(t->town->upgradedCreatures[bid-37]);
		//Test for 2nd upgrade - add sharpshooters if grand elves dwelling was constructed
		//if (t->subID == 1 && bid == 39)
		//	ssi.creatures[bid-37].second.push_back(137);
		sendAndApply(&ssi);
	}
	else if(bid >= 30) //bas. dwelling
	{
		int crid = t->town->basicCreatures[bid-30];
		SetAvailableCreatures ssi;
		ssi.tid = tid;
		ssi.creatures = t->creatures;
		ssi.creatures[bid-30].first = VLC->creh->creatures[crid]->growth;
		ssi.creatures[bid-30].second.push_back(crid);
		sendAndApply(&ssi);
	}
	else if(bid == 11)
		ns.bid.insert(27);
	else if(bid == 12)
		ns.bid.insert(28);
	else if(bid == 13)
		ns.bid.insert(29);
	else if (t->subID == 4 && bid == 17) //veil of darkness
	{
		//handled via town->reacreateBonuses in apply
// 		GiveBonus gb(GiveBonus::TOWN);
// 		gb.bonus.type = Bonus::DARKNESS;
// 		gb.bonus.val = 20;
// 		gb.id = t->id;
// 		gb.bonus.duration = Bonus::PERMANENT;
// 		gb.bonus.source = Bonus::TOWN_STRUCTURE;
// 		gb.bonus.id = 17;
// 		sendAndApply(&gb);
	}
	else if ( t->subID == 5 && bid == 22 )
	{
		setPortalDwelling(t);
	}

	ns.bid.insert(bid);
	ns.builded = force?t->builded:(t->builded+1);
	sendAndApply(&ns);

	//reveal ground for lookout tower
	FoWChange fw;
	fw.player = t->tempOwner;
	fw.mode = 1;
	getTilesInRange(fw.tiles,t->pos,t->getSightRadious(),t->tempOwner,1);
	sendAndApply(&fw);

	if (!force)
	{
		SetResources sr;
		sr.player = t->tempOwner;
		sr.res = gs->getPlayer(t->tempOwner)->resources - b->resources;
		sendAndApply(&sr);
	}

	if(bid<5) //it's mage guild
	{
		if(t->visitingHero)
			giveSpells(t,t->visitingHero);
		if(t->garrisonHero)
			giveSpells(t,t->garrisonHero);
	}
	if(t->visitingHero)
		vistiCastleObjects (t, t->visitingHero);
	if(t->garrisonHero)
		vistiCastleObjects (t, t->garrisonHero);

	checkLossVictory(t->tempOwner);
	return true;
}
bool CGameHandler::razeStructure (si32 tid, si32 bid)
{
///incomplete, simply erases target building
	CGTownInstance * t = static_cast<CGTownInstance*>(gs->map->objects[tid].get());
	if (t->builtBuildings.find(bid) == t->builtBuildings.end())
		return false;
	RazeStructures rs;
	rs.tid = tid;
	rs.bid.insert(bid);
	rs.destroyed = t->destroyed + 1;
	sendAndApply(&rs);
//TODO: Remove dwellers
// 	if (t->subID == 4 && bid == 17) //Veil of Darkness
// 	{
// 		RemoveBonus rb(RemoveBonus::TOWN);
// 		rb.whoID = t->id;
// 		rb.source = Bonus::TOWN_STRUCTURE;
// 		rb.id = 17;
// 		sendAndApply(&rb);
// 	}
	return true;
}

void CGameHandler::sendMessageToAll( const std::string &message )
{
	SystemMessage sm;
	sm.text = message;
	sendToAllClients(&sm);
}

bool CGameHandler::recruitCreatures( si32 objid, ui32 crid, ui32 cram, si32 fromLvl )
{
	const CGDwelling *dw = static_cast<CGDwelling*>(gs->map->objects[objid].get());
	const CArmedInstance *dst = NULL;
	const CCreature *c = VLC->creh->creatures[crid];
	bool warMachine = c->hasBonusOfType(Bonus::SIEGE_WEAPON);

	//TODO: test for owning

	if(dw->ID == GameConstants::TOWNI_TYPE)
		dst = (static_cast<const CGTownInstance *>(dw))->getUpperArmy();
	else if(dw->ID == 17  ||  dw->ID == 20  ||  dw->ID == 78) //advmap dwelling
		dst = getHero(gs->getPlayer(dw->tempOwner)->currentSelection); //TODO: check if current hero is really visiting dwelling
	else if(dw->ID == 106)
		dst = dynamic_cast<const CGHeroInstance *>(getTile(dw->visitablePos())->visitableObjects.back());

	assert(dw && dst);

	//verify
	bool found = false;
	int level = 0;

	typedef std::pair<const int,int> Parka;
	for(; level < dw->creatures.size(); level++) //iterate through all levels
	{
		if ( (fromLvl != -1) && ( level !=fromLvl ) )
			continue;
		const std::pair<ui32, std::vector<ui32> > &cur = dw->creatures[level]; //current level info <amount, list of cr. ids>
		int i = 0;
		for(; i < cur.second.size(); i++) //look for crid among available creatures list on current level
			if(cur.second[i] == crid)
				break;

		if(i < cur.second.size())
		{
			found = true;
			cram = std::min(cram, cur.first); //reduce recruited amount up to available amount
			break;
		}
	}
	int slot = dst->getSlotFor(crid);

	if( (!found && complain("Cannot recruit: no such creatures!"))
		|| (cram  >  VLC->creh->creatures[crid]->maxAmount(gs->getPlayer(dst->tempOwner)->resources) && complain("Cannot recruit: lack of resources!"))
		|| (cram<=0  &&  complain("Cannot recruit: cram <= 0!"))
		|| (slot<0  && !warMachine && complain("Cannot recruit: no available slot!")))
	{
		return false;
	}

	//recruit
	SetResources sr;
	sr.player = dst->tempOwner;
	sr.res = gs->getPlayer(dst->tempOwner)->resources - (c->cost * cram);

	SetAvailableCreatures sac;
	sac.tid = objid;
	sac.creatures = dw->creatures;
	sac.creatures[level].first -= cram;

	sendAndApply(&sr);
	sendAndApply(&sac);

	if(warMachine)
	{
		const CGHeroInstance *h = dynamic_cast<const CGHeroInstance*>(dst);
		if(!h)
			COMPLAIN_RET("Only hero can buy war machines");

		switch(crid)
		{
		case 146:
			giveHeroNewArtifact(h, VLC->arth->artifacts[4], ArtifactPosition::MACH1);
			break;
		case 147:
			giveHeroNewArtifact(h, VLC->arth->artifacts[6], ArtifactPosition::MACH3);
			break;
		case 148:
			giveHeroNewArtifact(h, VLC->arth->artifacts[5], ArtifactPosition::MACH2);
			break;
		default:
			complain("This war machine cannot be recruited!");
			return false;
		}
	}
	else
	{
		addToSlot(StackLocation(dst, slot), c, cram);
	}
	return true;
}

bool CGameHandler::upgradeCreature( ui32 objid, ui8 pos, ui32 upgID )
{
	CArmedInstance *obj = static_cast<CArmedInstance*>(gs->map->objects[objid].get());
	assert(obj->hasStackAtSlot(pos));
	UpgradeInfo ui = gs->getUpgradeInfo(obj->getStack(pos));
	int player = obj->tempOwner;
	const PlayerState *p = getPlayer(player);
	int crQuantity = obj->stacks[pos]->count;
	int newIDpos= vstd::find_pos(ui.newID, upgID);//get position of new id in UpgradeInfo
	TResources totalCost = ui.cost[newIDpos] * crQuantity;

	//check if upgrade is possible
	if( (ui.oldID<0 || newIDpos == -1 ) && complain("That upgrade is not possible!"))
	{
		return false;
	}


	//check if player has enough resources
	if(!p->resources.canAfford(totalCost))
		COMPLAIN_RET("Cannot upgrade, not enough resources!");

	//take resources
	SetResources sr;
	sr.player = player;
	sr.res = p->resources - totalCost;
	sendAndApply(&sr);

	//upgrade creature
	changeStackType(StackLocation(obj, pos), VLC->creh->creatures[upgID]);
	return true;
}

bool CGameHandler::changeStackType(const StackLocation &sl, CCreature *c)
{
	if(!sl.army->hasStackAtSlot(sl.slot))
		COMPLAIN_RET("Cannot find a stack to change type");

	SetStackType sst;
	sst.sl = sl;
	sst.type = c;
	sendAndApply(&sst);
	return true;
}

void CGameHandler::moveArmy(const CArmedInstance *src, const CArmedInstance *dst, bool allowMerging)
{
	assert(src->canBeMergedWith(*dst, allowMerging));
	while(src->stacksCount())//while there are unmoved creatures
	{
		TSlots::const_iterator i = src->Slots().begin(); //iterator to stack to move
		StackLocation sl(src, i->first); //location of stack to move

		TSlot pos = dst->getSlotFor(i->second->type);
		if(pos < 0)
		{
			//try to merge two other stacks to make place
			std::pair<TSlot, TSlot> toMerge;
			if(dst->mergableStacks(toMerge, i->first) && allowMerging)
			{
				moveStack(StackLocation(dst, toMerge.first), StackLocation(dst, toMerge.second)); //merge toMerge.first into toMerge.second
				assert(!dst->hasStackAtSlot(toMerge.first)); //we have now a new free slot
				moveStack(sl, StackLocation(dst, toMerge.first)); //move stack to freed slot
			}
			else
			{
				complain("Unexpected failure during an attempt to move army from " + src->nodeName() + " to " + dst->nodeName() + "!");
				return;
			}
		}
		else
		{
			moveStack(sl, StackLocation(dst, pos));
		}
	}
}

bool CGameHandler::garrisonSwap( si32 tid )
{
	CGTownInstance *town = gs->getTown(tid);
	if(!town->garrisonHero && town->visitingHero) //visiting => garrison, merge armies: town army => hero army
	{

		if(!town->visitingHero->canBeMergedWith(*town))
		{
			complain("Cannot make garrison swap, not enough free slots!");
			return false;
		}

		moveArmy(town, town->visitingHero, true);

		SetHeroesInTown intown;
		intown.tid = tid;
		intown.visiting = -1;
		intown.garrison = town->visitingHero->id;
		sendAndApply(&intown);
		return true;
	}
	else if (town->garrisonHero && !town->visitingHero) //move hero out of the garrison
	{
		//check if moving hero out of town will break 8 wandering heroes limit
		if(getHeroCount(town->garrisonHero->tempOwner,false) >= 8)
		{
			complain("Cannot move hero out of the garrison, there are already 8 wandering heroes!");
			return false;
		}

		SetHeroesInTown intown;
		intown.tid = tid;
		intown.garrison = -1;
		intown.visiting =  town->garrisonHero->id;
		sendAndApply(&intown);
		return true;
	}
	else if(!!town->garrisonHero && town->visitingHero) //swap visiting and garrison hero
	{
		SetHeroesInTown intown;
		intown.tid = tid;
		intown.garrison = town->visitingHero->id;
		intown.visiting =  town->garrisonHero->id;
		sendAndApply(&intown);
		return true;
	}
	else
	{
		complain("Cannot swap garrison hero!");
		return false;
	}
}

// With the amount of changes done to the function, it's more like transferArtifacts.
// Function moves artifact from src to dst. If dst is not a backpack and is already occupied, old dst art goes to backpack and is replaced.
bool CGameHandler::moveArtifact(const ArtifactLocation &al1, const ArtifactLocation &al2)
{
	ArtifactLocation src = al1, dst = al2;
	const int srcPlayer = src.owningPlayer(), dstPlayer = dst.owningPlayer();
	const CArmedInstance *srcObj = src.relatedObj(), *dstObj = dst.relatedObj();

	// Make sure exchange is even possible between the two heroes.
	if(!isAllowedExchange(srcObj->id, dstObj->id))
		COMPLAIN_RET("That heroes cannot make any exchange!");

	const CArtifactInstance *srcArtifact = src.getArt();
	const CArtifactInstance *destArtifact = dst.getArt();

	if (srcArtifact == NULL)
		COMPLAIN_RET("No artifact to move!");
	if (destArtifact && srcPlayer != dstPlayer)
		COMPLAIN_RET("Can't touch artifact on hero of another player!");

	// Check if src/dest slots are appropriate for the artifacts exchanged.
	// Moving to the backpack is always allowed.
	if ((!srcArtifact || dst.slot < GameConstants::BACKPACK_START)
		&& srcArtifact && !srcArtifact->canBePutAt(dst, true))
		COMPLAIN_RET("Cannot move artifact!");

	if ((srcArtifact && srcArtifact->artType->id == GameConstants::ID_LOCK) || (destArtifact && destArtifact->artType->id == GameConstants::ID_LOCK))
		COMPLAIN_RET("Cannot move artifact locks.");

	if (dst.slot >= GameConstants::BACKPACK_START && srcArtifact->artType->isBig())
		COMPLAIN_RET("Cannot put big artifacts in backpack!");
	if (src.slot == ArtifactPosition::MACH4 || dst.slot == ArtifactPosition::MACH4)
		COMPLAIN_RET("Cannot move catapult!");

	if(dst.slot >= GameConstants::BACKPACK_START)
		vstd::amin(dst.slot, GameConstants::BACKPACK_START + dst.getHolderArtSet()->artifactsInBackpack.size());

	if (src.slot == dst.slot  &&  src.artHolder == dst.artHolder)
		COMPLAIN_RET("Won't move artifact: Dest same as source!");

	if(dst.slot < GameConstants::BACKPACK_START  &&  destArtifact) //moving art to another slot
	{
		//old artifact must be removed first
		moveArtifact(dst, ArtifactLocation(dst.artHolder, dst.getHolderArtSet()->artifactsInBackpack.size() + GameConstants::BACKPACK_START));
	}

	MoveArtifact ma;
	ma.src = src;
	ma.dst = dst;
	sendAndApply(&ma);
	return true;
}

/**
 * Assembles or disassembles a combination artifact.
 * @param heroID ID of hero holding the artifact(s).
 * @param artifactSlot The worn slot ID of the combination- or constituent artifact.
 * @param assemble True for assembly operation, false for disassembly.
 * @param assembleTo If assemble is true, this represents the artifact ID of the combination
 * artifact to assemble to. Otherwise it's not used.
 */
bool CGameHandler::assembleArtifacts (si32 heroID, ui16 artifactSlot, bool assemble, ui32 assembleTo)
{

	CGHeroInstance *hero = gs->getHero(heroID);
	const CArtifactInstance *destArtifact = hero->getArt(artifactSlot);

	if(!destArtifact)
		COMPLAIN_RET("assembleArtifacts: there is no such artifact instance!");

	if(assemble)
	{
		CArtifact *combinedArt = VLC->arth->artifacts[assembleTo];
		if(!combinedArt->constituents)
			COMPLAIN_RET("assembleArtifacts: Artifact being attempted to assemble is not a combined artifacts!");
		if(!vstd::contains(destArtifact->assemblyPossibilities(hero), combinedArt))
			COMPLAIN_RET("assembleArtifacts: It's impossible to assemble requested artifact!");

		AssembledArtifact aa;
		aa.al = ArtifactLocation(hero, artifactSlot);
		aa.builtArt = combinedArt;
		sendAndApply(&aa);
	}
	else
	{
		if(!destArtifact->artType->constituents)
			COMPLAIN_RET("assembleArtifacts: Artifact being attempted to disassemble is not a combined artifact!");

		DisassembledArtifact da;
		da.al = ArtifactLocation(hero, artifactSlot);
		sendAndApply(&da);
	}

	return false;
}

bool CGameHandler::buyArtifact( ui32 hid, si32 aid )
{
	CGHeroInstance *hero = gs->getHero(hid);
	CGTownInstance *town = hero->visitedTown;
	if(aid==0) //spellbook
	{
		if((!vstd::contains(town->builtBuildings,si32(EBuilding::MAGES_GUILD_1)) && complain("Cannot buy a spellbook, no mage guild in the town!"))
		    || (getResource(hero->getOwner(), Res::GOLD) < GameConstants::SPELLBOOK_GOLD_COST && complain("Cannot buy a spellbook, not enough gold!") )
		    || (hero->getArt(ArtifactPosition::SPELLBOOK) && complain("Cannot buy a spellbook, hero already has a one!"))
		    )
			return false;

		giveResource(hero->getOwner(),Res::GOLD,-GameConstants::SPELLBOOK_GOLD_COST);
		giveHeroNewArtifact(hero, VLC->arth->artifacts[0], ArtifactPosition::SPELLBOOK);
		assert(hero->getArt(ArtifactPosition::SPELLBOOK));
		giveSpells(town,hero);
		return true;
	}
	else if(aid < 7  &&  aid > 3) //war machine
	{
		int price = VLC->arth->artifacts[aid]->price;
		if((hero->getArt(9+aid) && complain("Hero already has this machine!"))
			|| (!vstd::contains(town->builtBuildings,si32(EBuilding::BLACKSMITH)) && complain("No blackismith!"))
			|| (gs->getPlayer(hero->getOwner())->resources[Res::GOLD] < price  && complain("Not enough gold!"))  //no gold
			|| ((!(town->subID == 6 && vstd::contains(town->builtBuildings,si32(EBuilding::SPECIAL_3) ) )
			&& town->town->warMachine!= aid ) &&  complain("This machine is unavailable here!")))
		{
			return false;
		}

		giveResource(hero->getOwner(),Res::GOLD,-price);
		giveHeroNewArtifact(hero, VLC->arth->artifacts[aid], 9+aid);
		return true;
	}
	return false;
}

bool CGameHandler::buyArtifact(const IMarket *m, const CGHeroInstance *h, int rid, int aid)
{
	if(!vstd::contains(m->availableItemsIds(EMarketMode::RESOURCE_ARTIFACT), aid))
		COMPLAIN_RET("That artifact is unavailable!");

	int b1, b2;
	m->getOffer(rid, aid, b1, b2, EMarketMode::RESOURCE_ARTIFACT);

	if(getResource(h->tempOwner, rid) < b1)
		COMPLAIN_RET("You can't afford to buy this artifact!");

	SetResource sr;
	sr.player = h->tempOwner;
	sr.resid = rid;
	sr.val = getResource(h->tempOwner, rid) - b1;
	sendAndApply(&sr);


	SetAvailableArtifacts saa;
	if(m->o->ID == GameConstants::TOWNI_TYPE)
	{
		saa.id = -1;
		saa.arts = CGTownInstance::merchantArtifacts;
	}
	else if(const CGBlackMarket *bm = dynamic_cast<const CGBlackMarket *>(m->o)) //black market
	{
		saa.id = bm->id;
		saa.arts = bm->artifacts;
	}
	else
		COMPLAIN_RET("Wrong marktet...");

	bool found = false;
	BOOST_FOREACH(const CArtifact *&art, saa.arts)
	{
		if(art && art->id == aid)
		{
			art = NULL;
			found = true;
			break;
		}
	}

	if(!found)
		COMPLAIN_RET("Cannot find selected artifact on the list");

	sendAndApply(&saa);

	giveHeroNewArtifact(h, VLC->arth->artifacts[aid], -2);
	return true;
}

bool CGameHandler::sellArtifact(const IMarket *m, const CGHeroInstance *h, int aid, int rid)
{
	const CArtifactInstance *art = h->getArtByInstanceId(aid);
	if(!art)
		COMPLAIN_RET("There is no artifact to sell!");
	if(art->artType->id < 7)
		COMPLAIN_RET("Cannot sell a war machine or spellbook!");

	int resVal = 0, dump = 1;
	m->getOffer(art->artType->id, rid, dump, resVal, EMarketMode::ARTIFACT_RESOURCE);

	removeArtifact(ArtifactLocation(h, h->getArtPos(art)));
	giveResource(h->tempOwner, rid, resVal);
	return true;
}

//void CGameHandler::lootArtifacts (TArtHolder source, TArtHolder dest, std::vector<ui32> &arts)
//{
//	//const CGHeroInstance * h1 = dynamic_cast<CGHeroInstance *> source;
//	//auto s = boost::apply_visitor(GetArtifactSetPtr(), source);
//	{
//	}
//}

bool CGameHandler::buySecSkill( const IMarket *m, const CGHeroInstance *h, int skill)
{
	if (!h)
		COMPLAIN_RET("You need hero to buy a skill!");

	if (h->getSecSkillLevel(static_cast<CGHeroInstance::SecondarySkill>(skill)))
		COMPLAIN_RET("Hero already know this skill");

	if (h->secSkills.size() >= GameConstants::SKILL_PER_HERO)//can't learn more skills
		COMPLAIN_RET("Hero can't learn any more skills");

	if (h->type->heroClass->proSec[skill]==0)//can't learn this skill (like necromancy for most of non-necros)
		COMPLAIN_RET("The hero can't learn this skill!");

	if(!vstd::contains(m->availableItemsIds(EMarketMode::RESOURCE_SKILL), skill))
		COMPLAIN_RET("That skill is unavailable!");

	if(getResource(h->tempOwner, Res::GOLD) < 2000)//TODO: remove hardcoded resource\summ?
		COMPLAIN_RET("You can't afford to buy this skill");

	SetResource sr;
	sr.player = h->tempOwner;
	sr.resid = Res::GOLD;
	sr.val = getResource(h->tempOwner, Res::GOLD) - 2000;
	sendAndApply(&sr);

	changeSecSkill(h->id, skill, 1, true);
	return true;
}

bool CGameHandler::tradeResources(const IMarket *market, ui32 val, ui8 player, ui32 id1, ui32 id2)
{
	int r1 = gs->getPlayer(player)->resources[id1],
		r2 = gs->getPlayer(player)->resources[id2];

	vstd::amin(val, r1); //can't trade more resources than have

	int b1, b2; //base quantities for trade
	market->getOffer(id1, id2, b1, b2, EMarketMode::RESOURCE_RESOURCE);
	int units = val / b1; //how many base quantities we trade

	if(val%b1) //all offered units of resource should be used, if not -> somewhere in calculations must be an error
	{
		//TODO: complain?
		assert(0);
	}

	SetResource sr;
	sr.player = player;
	sr.resid = id1;
	sr.val = r1 - b1 * units;
	sendAndApply(&sr);

	sr.resid = id2;
	sr.val = r2 + b2 * units;
	sendAndApply(&sr);

	return true;
}

bool CGameHandler::sellCreatures(ui32 count, const IMarket *market, const CGHeroInstance * hero, ui32 slot, ui32 resourceID)
{
	if(!vstd::contains(hero->Slots(), slot))
		COMPLAIN_RET("Hero doesn't have any creature in that slot!");

	const CStackInstance &s = hero->getStack(slot);

	if( s.count < count  //can't sell more creatures than have
		|| (hero->Slots().size() == 1  &&  hero->needsLastStack()  &&  s.count == count)) //can't sell last stack
	{
		COMPLAIN_RET("Not enough creatures in army!");
	}

	int b1, b2; //base quantities for trade
 	market->getOffer(s.type->idNumber, resourceID, b1, b2, EMarketMode::CREATURE_RESOURCE);
 	int units = count / b1; //how many base quantities we trade

 	if(count%b1) //all offered units of resource should be used, if not -> somewhere in calculations must be an error
 	{
 		//TODO: complain?
 		assert(0);
 	}

	changeStackCount(StackLocation(hero, slot), -count);

 	SetResource sr;
 	sr.player = hero->tempOwner;
 	sr.resid = resourceID;
 	sr.val = getResource(hero->tempOwner, resourceID) + b2 * units;
 	sendAndApply(&sr);

	return true;
}

bool CGameHandler::transformInUndead(const IMarket *market, const CGHeroInstance * hero, ui32 slot)
{
	const CArmedInstance *army = NULL;
	if (hero)
		army = hero;
	else
		army = dynamic_cast<const CGTownInstance *>(market->o);

	if (!army)
		COMPLAIN_RET("Incorrect call to transform in undead!");
	if(!army->hasStackAtSlot(slot))
		COMPLAIN_RET("Army doesn't have any creature in that slot!");


	const CStackInstance &s = army->getStack(slot);
	int resCreature;//resulting creature - bone dragons or skeletons

	if	(s.hasBonusOfType(Bonus::DRAGON_NATURE))
		resCreature = 68;
	else
		resCreature = 56;

	changeStackType(StackLocation(army, slot), VLC->creh->creatures[resCreature]);
	return true;
}

bool CGameHandler::sendResources(ui32 val, ui8 player, ui32 r1, ui32 r2)
{
	const PlayerState *p2 = gs->getPlayer(r2, false);
	if(!p2  ||  p2->status != PlayerState::INGAME)
	{
		complain("Dest player must be in game!");
		return false;
	}

	si32 curRes1 = gs->getPlayer(player)->resources[r1], curRes2 = gs->getPlayer(r2)->resources[r1];
	val = std::min(si32(val),curRes1);

	SetResource sr;
	sr.player = player;
	sr.resid = r1;
	sr.val = curRes1 - val;
	sendAndApply(&sr);

	sr.player = r2;
	sr.val = curRes2 + val;
	sendAndApply(&sr);

	return true;
}

bool CGameHandler::setFormation( si32 hid, ui8 formation )
{
	gs->getHero(hid)-> formation = formation;
	return true;
}

bool CGameHandler::hireHero(const CGObjectInstance *obj, ui8 hid, ui8 player)
{
	const PlayerState *p = gs->getPlayer(player);
	const CGTownInstance *t = gs->getTown(obj->id);

	//common preconditions
	if( (p->resources[Res::GOLD]<2500  && complain("Not enough gold for buying hero!"))
		|| (getHeroCount(player, false) >= GameConstants::MAX_HEROES_PER_PLAYER && complain("Cannot hire hero, only 8 wandering heroes are allowed!")))
		return false;

	if(t) //tavern in town
	{
		if(    (!vstd::contains(t->builtBuildings,EBuilding::TAVERN)  && complain("No tavern!"))
			|| (t->visitingHero  && complain("There is visiting hero - no place!")))
			return false;
	}
	else if(obj->ID == 95) //Tavern on adv map
	{
		if(getTile(obj->visitablePos())->visitableObjects.back() != obj  &&  complain("Tavern entry must be unoccupied!"))
			return false;
	}


	const CGHeroInstance *nh = p->availableHeroes[hid];
	assert(nh);

	HeroRecruited hr;
	hr.tid = obj->id;
	hr.hid = nh->subID;
	hr.player = player;
	hr.tile = obj->visitablePos() + nh->getVisitableOffset();
	sendAndApply(&hr);


	bmap<ui32, ConstTransitivePtr<CGHeroInstance> > pool = gs->unusedHeroesFromPool();

	const CGHeroInstance *theOtherHero = p->availableHeroes[!hid];
	const CGHeroInstance *newHero = gs->hpool.pickHeroFor(false, player, getNativeTown(player), pool, theOtherHero->type->heroClass);

	SetAvailableHeroes sah;
	sah.player = player;

	if(newHero)
	{
		sah.hid[hid] = newHero->subID;
		sah.army[hid].clear();
		sah.army[hid].setCreature(0, VLC->creh->nameToID[newHero->type->refTypeStack[0]],1);
	}
	else
		sah.hid[hid] = -1;

	sah.hid[!hid] = theOtherHero ? theOtherHero->subID : -1;
	sendAndApply(&sah);

	SetResource sr;
	sr.player = player;
	sr.resid = Res::GOLD;
	sr.val = p->resources[Res::GOLD] - 2500;
	sendAndApply(&sr);

	if(t)
	{
		vistiCastleObjects (t, nh);
		giveSpells (t,nh);
	}
	return true;
}

bool CGameHandler::queryReply(ui32 qid, ui32 answer, ui8 player)
{
	boost::unique_lock<boost::recursive_mutex> lock(gsm);
	states.removeQuery(player, qid);
	if(vstd::contains(callbacks,qid))
	{
		CFunctionList<void(ui32)> callb = callbacks[qid];
		callbacks.erase(qid);
		if(callb)
			callb(answer);
	}
	else
	{
		complain("Unknown query reply!");
		return false;
	}
	return true;
}

static EndAction end_action;

bool CGameHandler::makeBattleAction( BattleAction &ba )
{
	tlog1 << "\tMaking action of type " << ba.actionType << std::endl;
	bool ok = true;

	switch(ba.actionType)
	{
	case BattleAction::END_TACTIC_PHASE: //wait
		{
			StartAction start_action(ba);
			sendAndApply(&start_action);
			sendAndApply(&end_action);
			break;
		}
	case BattleAction::WALK: //walk
		{
			StartAction start_action(ba);
			sendAndApply(&start_action); //start movement
			moveStack(ba.stackNumber,ba.destinationTile); //move
			sendAndApply(&end_action);
			break;
		}
	case BattleAction::DEFEND: //defend
		{
			//defensive stance //TODO: remove this bonus when stack becomes active
			SetStackEffect sse;
			sse.effect.push_back( Bonus(Bonus::STACK_GETS_TURN, Bonus::PRIMARY_SKILL, Bonus::OTHER, 20, -1, PrimarySkill::DEFENSE, Bonus::PERCENT_TO_ALL) );
			sse.effect.push_back( Bonus(Bonus::STACK_GETS_TURN, Bonus::PRIMARY_SKILL, Bonus::OTHER, gs->curB->stacks[ba.stackNumber]->valOfBonuses(Bonus::DEFENSIVE_STANCE),
				 -1, PrimarySkill::DEFENSE, Bonus::ADDITIVE_VALUE));
			sse.stacks.push_back(ba.stackNumber);
			sendAndApply(&sse);

			//don't break - we share code with next case
		}
	case BattleAction::WAIT: //wait
		{
			StartAction start_action(ba);
			sendAndApply(&start_action);
			sendAndApply(&end_action);
			break;
		}
	case BattleAction::RETREAT: //retreat/flee
		{
			if(!gs->curB->battleCanFlee(gs->curB->sides[ba.side]))
				complain("Cannot retreat!");
			else
				setBattleResult(1, !ba.side); //surrendering side loses
			break;
		}
	case BattleAction::SURRENDER:
		{
			int player = gs->curB->sides[ba.side];
			int cost = gs->curB->getSurrenderingCost(player);
			if(cost < 0)
				complain("Cannot surrender!");
			else if(getResource(player, Res::GOLD) < cost)
				complain("Not enough gold to surrender!");
			else
			{
				giveResource(player, Res::GOLD, -cost);
				setBattleResult(2, !ba.side); //surrendering side loses
			}
			break;
		}
		break;
	case BattleAction::WALK_AND_ATTACK: //walk or attack
		{
			StartAction start_action(ba);
			sendAndApply(&start_action); //start movement and attack
			int startingPos = gs->curB->getStack(ba.stackNumber)->position;
			int distance = moveStack(ba.stackNumber, ba.destinationTile);
			CStack *curStack = gs->curB->getStack(ba.stackNumber),
				*stackAtEnd = gs->curB->getStackT(ba.additionalInfo);

			if(!curStack || !stackAtEnd)
			{
				sendAndApply(&end_action);
				break;
			}

			if(curStack->position != ba.destinationTile //we wasn't able to reach destination tile
				&& !(curStack->doubleWide()
					&&  ( curStack->position == ba.destinationTile + (curStack->attackerOwned ?  +1 : -1 ) )
						) //nor occupy specified hex
				)
			{
				std::string problem = "We cannot move this stack to its destination " + curStack->getCreature()->namePl;
				tlog3 << problem << std::endl;
				complain(problem);
				ok = false;
				sendAndApply(&end_action);
				break;
			}

			if(stackAtEnd && curStack->ID == stackAtEnd->ID) //we should just move, it will be handled by following check
			{
				stackAtEnd = NULL;
			}

			if(!stackAtEnd)
			{
				complain(boost::str(boost::format("walk and attack error: no stack at additionalInfo tile (%d)!\n") % ba.additionalInfo));
				ok = false;
				sendAndApply(&end_action);
				break;
			}

			if( !CStack::isMeleeAttackPossible(curStack, stackAtEnd) )
			{
				complain("Attack cannot be performed!");
				sendAndApply(&end_action);
				ok = false;
				break;
			}

			//attack
			{
				BattleAttack bat;
				prepareAttack(bat, curStack, stackAtEnd, distance, ba.additionalInfo);
				handleAttackBeforeCasting(bat); //only before first attack
				sendAndApply(&bat);
				handleAfterAttackCasting(bat);
			}

			//counterattack
			if(!curStack->hasBonusOfType(Bonus::BLOCKS_RETALIATION)
				&& stackAtEnd->ableToRetaliate())
			{
				BattleAttack bat;
				prepareAttack(bat, stackAtEnd, curStack, 0, curStack->position);
				bat.flags |= BattleAttack::COUNTER;
				sendAndApply(&bat);
				handleAfterAttackCasting(bat);
			}

			//second attack
			if(curStack //FIXME: clones tend to dissapear during actions
				&& curStack->valOfBonuses(Bonus::ADDITIONAL_ATTACK) > 0
				&& !curStack->hasBonusOfType(Bonus::SHOOTER)
				&& curStack->alive()
				&& stackAtEnd->alive()  )
			{
				BattleAttack bat;
				prepareAttack(bat, curStack, stackAtEnd, 0, ba.additionalInfo);
				sendAndApply(&bat);
				handleAfterAttackCasting(bat);
			}

			//return
			if(curStack->hasBonusOfType(Bonus::RETURN_AFTER_STRIKE) && startingPos != curStack->position && curStack->alive())
			{
				moveStack(ba.stackNumber, startingPos);
				//NOTE: curStack->ID == ba.stackNumber (rev 1431)
			}
			sendAndApply(&end_action);
			break;
		}
	case BattleAction::SHOOT: //shoot
		{
			CStack *curStack = gs->curB->getStack(ba.stackNumber),
				*destStack= gs->curB->getStackT(ba.destinationTile);
			if( !gs->curB->battleCanShoot(curStack, ba.destinationTile) )
				break;

			StartAction start_action(ba);
			sendAndApply(&start_action); //start shooting

			{
				BattleAttack bat;
				bat.flags |= BattleAttack::SHOT;
				prepareAttack(bat, curStack, destStack, 0, ba.destinationTile);
				handleAttackBeforeCasting(bat);
				sendAndApply(&bat);
				handleAfterAttackCasting(bat);
			}

			//ballista & artillery handling
			if(destStack->alive() && curStack->getCreature()->idNumber == 146)
			{
				BattleAttack bat2;
				bat2.flags |= BattleAttack::SHOT;
				prepareAttack(bat2, curStack, destStack, 0, ba.destinationTile);
				sendAndApply(&bat2);
			}
			//TODO: allow more than one additional attack
			if(curStack->valOfBonuses(Bonus::ADDITIONAL_ATTACK) > 0 //if unit shots twice let's make another shot
				&& curStack->alive()
				&& destStack->alive()
				&& curStack->shots
				)
			{
				BattleAttack bat;
				bat.flags |= BattleAttack::SHOT;
				prepareAttack(bat, curStack, destStack, 0, ba.destinationTile);
				sendAndApply(&bat);
				handleAfterAttackCasting(bat);
			}

			sendAndApply(&end_action);
			break;
		}
	case BattleAction::CATAPULT: //catapult
		{
			StartAction start_action(ba);
			sendAndApply(&start_action);
			const CGHeroInstance * attackingHero = gs->curB->heroes[ba.side];
			CHeroHandler::SBallisticsLevelInfo sbi = VLC->heroh->ballistics[attackingHero->getSecSkillLevel(CGHeroInstance::BALLISTICS)];

			int attackedPart = gs->curB->hexToWallPart(ba.destinationTile);
			if(attackedPart < 0)
			{
				complain("catapult tried to attack non-catapultable hex!");
				break;
			}
			int wallInitHP = gs->curB->si.wallState[attackedPart];
			int dmgAlreadyDealt = 0; //in successive iterations damage is dealt but not yet subtracted from wall's HPs
			for(int g=0; g<sbi.shots; ++g)
			{
				if(wallInitHP + dmgAlreadyDealt == 3) //it's not destroyed
					continue;

				CatapultAttack ca; //package for clients
				std::pair< std::pair< ui8, si16 >, ui8> attack; //<< attackedPart , destination tile >, damageDealt >
				attack.first.first = attackedPart;
				attack.first.second = ba.destinationTile;
				attack.second = 0;

				int chanceForHit = 0;
				int dmgChance[] = { sbi.noDmg, sbi.oneDmg, sbi.twoDmg }; //dmgChance[i] - chance for doing i dmg when hit is successful
				switch(attackedPart)
				{
				case 0: //keep
					chanceForHit = sbi.keep;
					break;
				case 1: //bottom tower
				case 6: //upper tower
					chanceForHit = sbi.tower;
					break;
				case 2: //bottom wall
				case 3: //below gate
				case 4: //over gate
				case 5: //upper wall
					chanceForHit = sbi.wall;
					break;
				case 7: //gate
					chanceForHit = sbi.gate;
					break;
				}

				if(rand()%100 <= chanceForHit) //hit is successful
				{
					int dmgRand = rand()%100;
					//accumulating dmgChance
					dmgChance[1] += dmgChance[0];
					dmgChance[2] += dmgChance[1];
					//calculating dealt damage
					for(int v = 0; v < ARRAY_COUNT(dmgChance); ++v)
					{
						if(dmgRand <= dmgChance[v])
						{
							attack.second = std::min(3 - dmgAlreadyDealt - wallInitHP, v);
							dmgAlreadyDealt += attack.second;
							break;
						}
					}

					//removing creatures in turrets / keep if one is destroyed
					if(attack.second > 0 && (attackedPart == 0 || attackedPart == 1 || attackedPart == 6))
					{
						int posRemove = -1;
						switch(attackedPart)
						{
						case 0: //keep
							posRemove = -2;
							break;
						case 1: //bottom tower
							posRemove = -3;
							break;
						case 6: //upper tower
							posRemove = -4;
							break;
						}

						BattleStacksRemoved bsr;
						for(int g=0; g<gs->curB->stacks.size(); ++g)
						{
							if(gs->curB->stacks[g]->position == posRemove)
							{
								bsr.stackIDs.insert( gs->curB->stacks[g]->ID );
								break;
							}
						}

						sendAndApply(&bsr);
					}
				}
				ca.attacker = ba.stackNumber;
				ca.attackedParts.insert(attack);

				sendAndApply(&ca);
			}
			sendAndApply(&end_action);
			break;
		}
		case BattleAction::STACK_HEAL: //healing with First Aid Tent
		{
			StartAction start_action(ba);
			sendAndApply(&start_action);
			const CGHeroInstance * attackingHero = gs->curB->heroes[ba.side];
			CStack *healer = gs->curB->getStack(ba.stackNumber),
				*destStack = gs->curB->getStackT(ba.destinationTile);

			if(healer == NULL || destStack == NULL || !healer->hasBonusOfType(Bonus::HEALER))
			{
				complain("There is either no healer, no destination, or healer cannot heal :P");
			}
			int maxHealable = destStack->MaxHealth() - destStack->firstHPleft;
			int maxiumHeal = healer->count * std::max(10, attackingHero->valOfBonuses(Bonus::SECONDARY_SKILL_PREMY, CGHeroInstance::FIRST_AID));

			int healed = std::min(maxHealable, maxiumHeal);

			if(healed == 0)
			{
				//nothing to heal.. should we complain?
			}
			else
			{
				StacksHealedOrResurrected shr;
				shr.lifeDrain = (ui8)false;
				shr.tentHealing = (ui8)true;
				shr.drainedFrom = ba.stackNumber;

				StacksHealedOrResurrected::HealInfo hi;
				hi.healedHP = healed;
				hi.lowLevelResurrection = 0;
				hi.stackID = destStack->ID;

				shr.healedStacks.push_back(hi);
				sendAndApply(&shr);
			}


			sendAndApply(&end_action);
			break;
		}
		case BattleAction::DAEMON_SUMMONING:
			//TODO: From Strategija:
			//Summon Demon is a level 2 spell.
		{
			StartAction start_action(ba);
			sendAndApply(&start_action);

			CStack *summoner = gs->curB->getStack(ba.stackNumber),
				*destStack = gs->curB->getStackT(ba.destinationTile, false);

			BattleStackAdded bsa;
			bsa.attacker = summoner->attackerOwned;

			bsa.creID = summoner->getBonus(Selector::type(Bonus::DAEMON_SUMMONING))->subtype; //in case summoner can summon more than one type of monsters... scream!
			ui64 risedHp = summoner->count * summoner->valOfBonuses(Bonus::DAEMON_SUMMONING, bsa.creID);
			bsa.amount = std::min ((ui32)(risedHp / VLC->creh->creatures[bsa.creID]->MaxHealth()), destStack->baseAmount);

			bsa.pos = gs->curB->getAvaliableHex(bsa.creID, bsa.attacker, destStack->position);
			bsa.summoned = false;

			if (bsa.amount) //there's rare possibility single creature cannot rise desired type
			{
				BattleStacksRemoved bsr; //remove body
				bsr.stackIDs.insert(destStack->ID);
				sendAndApply(&bsr);
				sendAndApply(&bsa);

				BattleSetStackProperty ssp;
				ssp.stackID = ba.stackNumber;
				ssp.which = BattleSetStackProperty::CASTS; //reduce number of casts
				ssp.val = -1;
				ssp.absolute = false;
				sendAndApply(&ssp);
			}

			sendAndApply(&end_action);
			break;
		}
		case BattleAction::MONSTER_SPELL:
		{
			StartAction start_action(ba);
			sendAndApply(&start_action);

			CStack * stack = gs->curB->getStack(ba.stackNumber);
			int spellID = ba.additionalInfo;
			BattleHex destination(ba.destinationTile);

			const Bonus *randSpellcaster = stack->getBonus(Selector::type(Bonus::RANDOM_SPELLCASTER));
			const Bonus * spellcaster = stack->getBonus(Selector::typeSubtype(Bonus::SPELLCASTER, spellID));

			//TODO special bonus for genies ability
			if(randSpellcaster && battleGetRandomStackSpell(stack, CBattleInfoCallback::RANDOM_AIMED) < 0)
				spellID = battleGetRandomStackSpell(stack, CBattleInfoCallback::RANDOM_GENIE);

			if(spellID < 0)
				complain("That stack can't cast spells!");
			else
			{
				int spellLvl = 0;
				if (spellcaster)
					vstd::amax(spellLvl, spellcaster->val);
				if (randSpellcaster)
					vstd::amax(spellLvl, randSpellcaster->val);
				vstd::amin (spellLvl, 3);

				int casterSide = gs->curB->whatSide(stack->owner);
				const CGHeroInstance * secHero = gs->curB->getHero(gs->curB->theOtherPlayer(stack->owner));

				handleSpellCasting(spellID, spellLvl, destination, casterSide, stack->owner, NULL, secHero, 0, ECastingMode::CREATURE_ACTIVE_CASTING, stack);
			}

			sendAndApply(&end_action);
			break;
		}
	}
	if(ba.stackNumber == gs->curB->activeStack  ||  battleResult.get()) //active stack has moved or battle has finished
		battleMadeAction.setn(true);
	return ok;
}

void CGameHandler::playerMessage( ui8 player, const std::string &message )
{
	bool cheated=true;
	PlayerMessage temp_message(player,message);

	sendAndApply(&temp_message);
	if(message == "vcmiistari") //give all spells and 999 mana
	{
		SetMana sm;
		ChangeSpells cs;

		CGHeroInstance *h = gs->getHero(gs->getPlayer(player)->currentSelection);
		if(!h && complain("Cannot realize cheat, no hero selected!")) return;

		sm.hid = cs.hid = h->id;

		//give all spells
		cs.learn = 1;
		for(int i=0;i<VLC->spellh->spells.size();i++)
		{
			if(!VLC->spellh->spells[i]->creatureAbility)
				cs.spells.insert(i);
		}

		//give mana
		sm.val = 999;

		if(!h->hasSpellbook()) //hero doesn't have spellbook
			giveHeroNewArtifact(h, VLC->arth->artifacts[0], ArtifactPosition::SPELLBOOK); //give spellbook

		sendAndApply(&cs);
		sendAndApply(&sm);
	}
	else if (message == "vcmiarmenelos") //build all buildings in selected town
	{
		CGTownInstance *town = gs->getTown(gs->getPlayer(player)->currentSelection);
		if (town)
		{
			BOOST_FOREACH (CBuildingHandler::TBuildingsMap::value_type &build, VLC->buildh->buildings[town->subID])
			{
				if (!vstd::contains(town->builtBuildings, build.first)
				 && !build.second->Name().empty())
				{
					buildStructure(town->id, build.first, true);
				}
			}
		}
	}
	else if(message == "vcmiainur") //gives 5 archangels into each slot
	{
		CGHeroInstance *hero = gs->getHero(gs->getPlayer(player)->currentSelection);
		const CCreature *archangel = VLC->creh->creatures[13];
		if(!hero) return;

		for(int i = 0; i < GameConstants::ARMY_SIZE; i++)
			if(!hero->hasStackAtSlot(i))
				insertNewStack(StackLocation(hero, i), archangel, 5);
	}
	else if(message == "vcmiangband") //gives 10 black knight into each slot
	{
		CGHeroInstance *hero = gs->getHero(gs->getPlayer(player)->currentSelection);
		const CCreature *blackKnight = VLC->creh->creatures[66];
		if(!hero) return;

		for(int i = 0; i < GameConstants::ARMY_SIZE; i++)
			if(!hero->hasStackAtSlot(i))
				insertNewStack(StackLocation(hero, i), blackKnight, 10);
	}
	else if(message == "vcminoldor") //all war machines
	{
		CGHeroInstance *hero = gs->getHero(gs->getPlayer(player)->currentSelection);
		if(!hero) return;

		if(!hero->getArt(ArtifactPosition::MACH1))
			giveHeroNewArtifact(hero, VLC->arth->artifacts[4], ArtifactPosition::MACH1);
		if(!hero->getArt(ArtifactPosition::MACH2))
			giveHeroNewArtifact(hero, VLC->arth->artifacts[5], ArtifactPosition::MACH2);
		if(!hero->getArt(ArtifactPosition::MACH3))
			giveHeroNewArtifact(hero, VLC->arth->artifacts[6], ArtifactPosition::MACH3);
	}
	else if(message == "vcminahar") //1000000 movement points
	{
		CGHeroInstance *hero = gs->getHero(gs->getPlayer(player)->currentSelection);
		if(!hero) return;
		SetMovePoints smp;
		smp.hid = hero->id;
		smp.val = 1000000;
		sendAndApply(&smp);
	}
	else if(message == "vcmiformenos") //give resources
	{
		SetResources sr;
		sr.player = player;
		sr.res = gs->getPlayer(player)->resources;
		for(int i=0;i<7;i++)
			sr.res[i] += 100;
		sr.res[6] += 19900;
		sendAndApply(&sr);
	}
	else if(message == "vcmieagles") //reveal FoW
	{
		FoWChange fc;
		fc.mode = 1;
		fc.player = player;
		int3 * hlp_tab = new int3[gs->map->width * gs->map->height * (gs->map->twoLevel + 1)];
		int lastUnc = 0;
		for(int i=0;i<gs->map->width;i++)
			for(int j=0;j<gs->map->height;j++)
				for(int k=0;k<gs->map->twoLevel+1;k++)
					if(!gs->getPlayerTeam(fc.player)->fogOfWarMap[i][j][k])
						hlp_tab[lastUnc++] = int3(i,j,k);
		fc.tiles.insert(hlp_tab, hlp_tab + lastUnc);
		delete [] hlp_tab;
		sendAndApply(&fc);
	}
	else if(message == "vcmiglorfindel") //selected hero gains a new level
	{
		CGHeroInstance *hero = gs->getHero(gs->getPlayer(player)->currentSelection);
		changePrimSkill(hero->id,4,VLC->heroh->reqExp(hero->level+1) - VLC->heroh->reqExp(hero->level));
	}
	else if(message == "vcmisilmaril") //player wins
	{
		gs->getPlayer(player)->enteredWinningCheatCode = 1;
	}
	else if(message == "vcmimelkor") //player looses
	{
		gs->getPlayer(player)->enteredLosingCheatCode = 1;
	}
	else if (message == "vcmiforgeofnoldorking") //hero gets all artifacts except war machines, spell scrolls and spell book
	{
		CGHeroInstance *hero = gs->getHero(gs->getPlayer(player)->currentSelection);
		if(!hero) return;
		for (int g=7; g<=140; ++g)
			giveHeroNewArtifact(hero, VLC->arth->artifacts[g], -1);
	}
	else
		cheated = false;
	if(cheated)
	{
		SystemMessage temp_message(VLC->generaltexth->allTexts[260]);
		sendAndApply(&temp_message);
		checkLossVictory(player);//Player enter win code or got required art\creature
	}
}

void CGameHandler::handleSpellCasting( int spellID, int spellLvl, BattleHex destination, ui8 casterSide, ui8 casterColor, const CGHeroInstance * caster, const CGHeroInstance * secHero,
	int usedSpellPower, ECastingMode::ECastingMode mode, const CStack * stack, si32 selectedStack)
{
	const CSpell *spell = VLC->spellh->spells[spellID];


	//Helper local function that creates obstacle on given position. Obstacle type is inferred from spell type.
	//It creates, sends and applies needed package.
	auto placeObstacle = [&](BattleHex pos)
	{
		static int obstacleIdToGive = gs->curB->obstacles.size()
									? (gs->curB->obstacles.back()->uniqueID+1)
									: 0;

		auto obstacle = make_shared<SpellCreatedObstacle>();
		switch(spellID) // :/
		{
		case Spells::QUICKSAND:
			obstacle->obstacleType = CObstacleInstance::QUICKSAND;
			obstacle->turnsRemaining = -1;
			obstacle->visibleForAnotherSide = false;
			break;
		case Spells::LAND_MINE:
			obstacle->obstacleType = CObstacleInstance::LAND_MINE;
			obstacle->turnsRemaining = -1;
			obstacle->visibleForAnotherSide = false;
			break;
		case Spells::FIRE_WALL:
			obstacle->obstacleType = CObstacleInstance::FIRE_WALL;
			obstacle->turnsRemaining = 2;
			obstacle->visibleForAnotherSide = true;
			break;
		case Spells::FORCE_FIELD:
			obstacle->obstacleType = CObstacleInstance::FORCE_FIELD;
			obstacle->turnsRemaining = 2;
			obstacle->visibleForAnotherSide = true;
			break;
		default:
			//this function cannot be used with spells that do not create obstacles
			assert(0);
		}

		obstacle->pos = pos;
		obstacle->casterSide = casterSide;
		obstacle->ID = spellID;
		obstacle->spellLevel = spellLvl;
		obstacle->casterSpellPower = usedSpellPower;
		obstacle->uniqueID = obstacleIdToGive++;

		BattleObstaclePlaced bop;
		bop.obstacle = obstacle;
		sendAndApply(&bop);
	};

	BattleSpellCast sc;
	sc.side = casterSide;
	sc.id = spellID;
	sc.skill = spellLvl;
	sc.tile = destination;
	sc.dmgToDisplay = 0;
	sc.castedByHero = (bool)caster;
	sc.attackerType = (stack ? stack->type->idNumber : -1);
	sc.manaGained = 0;
	sc.spellCost = 0;

	if (caster) //calculate spell cost
	{
		sc.spellCost = gs->curB->getSpellCost(VLC->spellh->spells[spellID], caster);

		if (secHero && mode == ECastingMode::HERO_CASTING) //handle mana channel
		{
			int manaChannel = 0;
			BOOST_FOREACH(CStack * stack, gs->curB->stacks) //TODO: shouldn't bonus system handle it somehow?
			{
				if (stack->owner == secHero->tempOwner)
				{
					vstd::amax(manaChannel, stack->valOfBonuses(Bonus::MANA_CHANNELING));
				}
			}
			sc.manaGained = (manaChannel * sc.spellCost) / 100;
		}
	}

	//calculating affected creatures for all spells
	std::set<CStack*> attackedCres;
	if (mode != ECastingMode::ENCHANTER_CASTING)
	{
		attackedCres = gs->curB->getAttackedCreatures(spell, spellLvl, casterColor, destination);
		for(std::set<CStack*>::const_iterator it = attackedCres.begin(); it != attackedCres.end(); ++it)
		{
			sc.affectedCres.insert((*it)->ID);
		}
	}
	else //enchanter - hit all possible stacks
	{
		BOOST_FOREACH (CStack * stack, gs->curB->stacks)
		{
			/*if it's non negative spell and our unit or non positive spell and hostile unit */
			if((!spell->isNegative() && stack->owner == casterColor)
				|| (!spell->isPositive() && stack->owner != casterColor))
			{
				if(stack->isValidTarget()) //TODO: allow dead targets somewhere in the future
				{
					attackedCres.insert(stack);
				}
			}
		}
	}

	//checking if creatures resist
	sc.resisted = gs->curB->calculateResistedStacks(spell, caster, secHero, attackedCres, casterColor, mode, usedSpellPower, spellLvl);

	//calculating dmg to display
	for(std::set<CStack*>::iterator it = attackedCres.begin(); it != attackedCres.end(); ++it)
	{
		if(vstd::contains(sc.resisted, (*it)->ID)) //this creature resisted the spell
			continue;
		sc.dmgToDisplay += gs->curB->calculateSpellDmg(spell, caster, *it, spellLvl, usedSpellPower);
	}
	if (spellID == Spells::DEATH_STARE || spellID == Spells::ACID_BREATH_DAMAGE)
	{
		sc.dmgToDisplay = usedSpellPower;
		if (spellID == Spells::DEATH_STARE)
			vstd::amin(sc.dmgToDisplay, (*attackedCres.begin())->count); //stack is already reduced after attack
	}
	StacksInjured si;

	//applying effects
	switch (spellID)
	{
	case Spells::QUICKSAND:
	case Spells::LAND_MINE:
		{
			std::vector<BattleHex> availableTiles;
			for(int i = 0; i < GameConstants::BFIELD_SIZE; i += 1)
			{
				BattleHex hex = i;
				if(hex.getX() > 2 && hex.getX() < 14 && !battleGetStackByPos(hex, false) & !battleGetObstacleOnPos(hex, false))
					availableTiles.push_back(hex);
			}
			boost::range::random_shuffle(availableTiles);

			const int patchesForSkill[] = {4, 4, 6, 8};
			const int patchesToPut = std::min<int>(patchesForSkill[spellLvl], availableTiles.size());

			//land mines or quicksand patches are handled as spell created obstacles
			for (int i = 0; i < patchesToPut; i++)
				placeObstacle(availableTiles[i]);
		}

		break;
	case Spells::FORCE_FIELD:
		placeObstacle(destination);
		break;
	case Spells::FIRE_WALL:
		{
			//fire wall is build from multiple obstacles - one fire piece for each affected hex
			auto affectedHexes = spell->rangeInHexes(destination, spellLvl, casterSide);
			BOOST_FOREACH(BattleHex hex, affectedHexes)
				placeObstacle(hex);
		}
		break;
	//damage spells
	case Spells::MAGIC_ARROW:
	case Spells::ICE_BOLT:
	case Spells::LIGHTNING_BOLT:
	case Spells::IMPLOSION:
	case Spells::CHAIN_LIGHTNING:
	case Spells::FROST_RING:
	case Spells::FIREBALL:
	case Spells::INFERNO:
	case Spells::METEOR_SHOWER:
	case Spells::DEATH_RIPPLE:
	case Spells::DESTROY_UNDEAD:
	case Spells::ARMAGEDDON:
	case Spells::TITANS_LIGHTNING_BOLT:
	case Spells::THUNDERBOLT: //(thunderbirds)
		{
			int spellDamage = 0;
			if (stack && mode != ECastingMode::MAGIC_MIRROR)
			{
				int unitSpellPower = stack->valOfBonuses(Bonus::SPECIFIC_SPELL_POWER, spellID);
				if (unitSpellPower)
					sc.dmgToDisplay = spellDamage = stack->count * unitSpellPower; //TODO: handle immunities
				else //Faerie Dragon
				{
					usedSpellPower = stack->valOfBonuses(Bonus::CREATURE_SPELL_POWER) * stack->count / 100;
					sc.dmgToDisplay = 0;
				}
			}
			int chainLightningModifier = 0;
			for(std::set<CStack*>::iterator it = attackedCres.begin(); it != attackedCres.end(); ++it)
			{
				if(vstd::contains(sc.resisted, (*it)->ID)) //this creature resisted the spell
					continue;

				BattleStackAttacked bsa;
				if ((destination > -1 && (*it)->coversPos(destination)) || (spell->range[spellLvl] == "X" || mode == ECastingMode::ENCHANTER_CASTING))
					//display effect only upon primary target of area spell
				{
					bsa.flags |= BattleStackAttacked::EFFECT;
					bsa.effect = spell->mainEffectAnim;
				}
				if (spellDamage)
					bsa.damageAmount = spellDamage >> chainLightningModifier;
				else
				{
					bsa.damageAmount = gs->curB->calculateSpellDmg(spell, caster, *it, spellLvl, usedSpellPower) >> chainLightningModifier;
					sc.dmgToDisplay += bsa.damageAmount;
				}
				bsa.stackAttacked = (*it)->ID;
				if (mode == ECastingMode::ENCHANTER_CASTING) //multiple damage spells cast
					bsa.attackerID = stack->ID;
				else
					bsa.attackerID = -1;
				(*it)->prepareAttacked(bsa);
				si.stacks.push_back(bsa);

				if (spellID == Spells::CHAIN_LIGHTNING)
					++chainLightningModifier;
			}
			break;
		}
		// permanent effects
	case Spells::SHIELD:
	case Spells::AIR_SHIELD:
	case Spells::FIRE_SHIELD:
	case Spells::PROTECTION_FROM_AIR:
	case Spells::PROTECTION_FROM_FIRE:
	case Spells::PROTECTION_FROM_WATER:
	case Spells::PROTECTION_FROM_EARTH:
	case Spells::ANTI_MAGIC:
	case Spells::MAGIC_MIRROR:
	case Spells::BLESS:
	case Spells::CURSE:
	case Spells::BLOODLUST:
	case Spells::PRECISION:
	case Spells::WEAKNESS:
	case Spells::STONE_SKIN:
	case Spells::DISRUPTING_RAY:
	case Spells::PRAYER:
	case Spells::MIRTH:
	case Spells::SORROW:
	case Spells::FORTUNE:
	case Spells::MISFORTUNE:
	case Spells::HASTE:
	case Spells::SLOW:
	case Spells::SLAYER:
	case Spells::FRENZY:
	case Spells::COUNTERSTRIKE:
	case Spells::BERSERK:
	case Spells::HYPNOTIZE:
	case Spells::FORGETFULNESS:
	case Spells::BLIND:
	case Spells::STONE_GAZE:
	case Spells::POISON:
	case Spells::BIND:
	case Spells::DISEASE:
	case Spells::PARALYZE:
	case Spells::AGE:
	case Spells::ACID_BREATH_DEFENSE:
		{
			int stackSpellPower = 0;
			if (stack && mode != ECastingMode::MAGIC_MIRROR)
			{
				stackSpellPower = stack->valOfBonuses(Bonus::CREATURE_ENCHANT_POWER);
			}
			SetStackEffect sse;
			Bonus pseudoBonus;
			pseudoBonus.sid = spellID;
			pseudoBonus.val = spellLvl;
			pseudoBonus.turnsRemain = gs->curB->calculateSpellDuration(spell, caster, stackSpellPower ? stackSpellPower : usedSpellPower);
			CStack::stackEffectToFeature(sse.effect, pseudoBonus);
			if (spellID == 72 && stack)//bind
			{
				sse.effect.back().additionalInfo = stack->ID; //we need to know who casted Bind
			}
			const Bonus * bonus = NULL;
			if (caster)
				bonus = caster->getBonus(Selector::typeSubtype(Bonus::SPECIAL_PECULIAR_ENCHANT, spellID));

			si32 power = 0;
			for(std::set<CStack*>::iterator it = attackedCres.begin(); it != attackedCres.end(); ++it)
			{
				if(vstd::contains(sc.resisted, (*it)->ID)) //this creature resisted the spell
					continue;
				sse.stacks.push_back((*it)->ID);

				//Apply hero specials - peculiar enchants
				if ((*it)->base) // no war machines - TODO: make it work
				{
					ui8 tier = (*it)->base->type->level;
					if (bonus)
 					{
 	 					switch(bonus->additionalInfo)
 	 					{
 	 						case 0: //normal
							{
 	 							switch(tier)
 	 							{
 	 								case 1: case 2:
 	 									power = 3;
 	 								break;
 	 								case 3: case 4:
 	 									power = 2;
 	 								break;
 	 								case 5: case 6:
 	 									power = 1;
 	 								break;
 	 							}
								Bonus specialBonus(sse.effect.back());
								specialBonus.val = power; //it doesn't necessarily make sense for some spells, use it wisely
								sse.uniqueBonuses.push_back (std::pair<ui32,Bonus> ((*it)->ID, specialBonus)); //additional premy to given effect
							}
 	 						break;
 	 						case 1: //only Coronius as yet
							{
 	 							power = std::max(5 - tier, 0);
								Bonus specialBonus = CStack::featureGenerator(Bonus::PRIMARY_SKILL, PrimarySkill::ATTACK, power, pseudoBonus.turnsRemain);
								specialBonus.sid = spellID;
				 	 			sse.uniqueBonuses.push_back (std::pair<ui32,Bonus> ((*it)->ID, specialBonus)); //additional attack to Slayer effect
							}
 	 						break;
 	 					}
 						}
					if (caster && caster->hasBonusOfType(Bonus::SPECIAL_BLESS_DAMAGE, spellID)) //TODO: better handling of bonus percentages
 	 				{
 	 					int damagePercent = caster->level * caster->valOfBonuses(Bonus::SPECIAL_BLESS_DAMAGE, spellID) / tier;
						Bonus specialBonus = CStack::featureGenerator(Bonus::CREATURE_DAMAGE, 0, damagePercent, pseudoBonus.turnsRemain);
						specialBonus.valType = Bonus::PERCENT_TO_ALL;
						specialBonus.sid = spellID;
 	 					sse.uniqueBonuses.push_back (std::pair<ui32,Bonus> ((*it)->ID, specialBonus));
 	 				}
				}
			}

			if(!sse.stacks.empty())
				sendAndApply(&sse);
			break;
		}
	case Spells::TELEPORT:
		{
			BattleStackMoved bsm;
			bsm.distance = -1;
			bsm.stack = selectedStack;
			std::vector<BattleHex> tiles;
			tiles.push_back(destination);
			bsm.tilesToMove = tiles;
			bsm.teleporting = true;
			sendAndApply(&bsm);
			break;
		}
	case Spells::CURE:
	case Spells::RESURRECTION:
	case Spells::ANIMATE_DEAD:
	case Spells::SACRIFICE:
		{
			int hpGained = 0;
			if (stack)
			{
				int unitSpellPower = stack->valOfBonuses(Bonus::SPECIFIC_SPELL_POWER, spellID);
				if (unitSpellPower)
					hpGained = stack->count * unitSpellPower; //Archangel
				else //Faerie Dragon-like effect - unused fo far
					usedSpellPower = stack->valOfBonuses(Bonus::CREATURE_SPELL_POWER) * stack->count / 100;
			}
			StacksHealedOrResurrected shr;
			shr.lifeDrain = (ui8)false;
			shr.tentHealing = (ui8)false;
			for(std::set<CStack*>::iterator it = attackedCres.begin(); it != attackedCres.end(); ++it)
			{
				if(vstd::contains(sc.resisted, (*it)->ID) //this creature resisted the spell
					|| (spellID == Spells::ANIMATE_DEAD && !(*it)->hasBonusOfType(Bonus::UNDEAD)) //we try to cast animate dead on living stack
					)
					continue;
				StacksHealedOrResurrected::HealInfo hi;
				hi.stackID = (*it)->ID;
				if (stack)
				{
					if (hpGained)
					{
						hi.healedHP = gs->curB->calculateHealedHP(hpGained, spell, *it);
					}
					else
						hi.healedHP = gs->curB->calculateHealedHP(spell, usedSpellPower, spellLvl, *it);
				}
				else
					hi.healedHP = gs->curB->calculateHealedHP(caster, spell, *it, gs->curB->getStack(selectedStack));
				hi.lowLevelResurrection = spellLvl <= 1;
				shr.healedStacks.push_back(hi);
			}
			if(!shr.healedStacks.empty())
				sendAndApply(&shr);
			if (spellID == Spells::SACRIFICE) //remove victim
			{
				BattleStacksRemoved bsr;
				bsr.stackIDs.insert (selectedStack); //somehow it works for teleport?
				sendAndApply(&bsr);
			}
			break;
		}
	case Spells::SUMMON_FIRE_ELEMENTAL:
	case Spells::SUMMON_EARTH_ELEMENTAL:
	case Spells::SUMMON_WATER_ELEMENTAL:
	case Spells::SUMMON_AIR_ELEMENTAL:
		{ //elemental summoning
			int creID;
			switch(spellID)
			{
				case Spells::SUMMON_FIRE_ELEMENTAL:
					creID = 114;
					break;
				case Spells::SUMMON_EARTH_ELEMENTAL:
					creID = 113;
					break;
				case Spells::SUMMON_WATER_ELEMENTAL:
					creID = 115;
					break;
				case Spells::SUMMON_AIR_ELEMENTAL:
					creID = 112;
					break;
			}

			BattleStackAdded bsa;
			bsa.creID = creID;
			bsa.attacker = !(bool)casterSide;
			bsa.summoned = true;
			bsa.pos = gs->curB->getAvaliableHex(creID, !(bool)casterSide); //TODO: unify it

			//TODO stack casting -> probably power will be zero; set the proper number of creatures manually
			int percentBonus = caster ? caster->valOfBonuses(Bonus::SPECIFIC_SPELL_DAMAGE, spellID) : 0;

			bsa.amount = usedSpellPower * VLC->spellh->spells[spellID]->powers[spellLvl] *
				(100 + percentBonus) / 100.0; //new feature - percentage bonus
			if(bsa.amount)
				sendAndApply(&bsa);
			else
				complain("Summoning elementals didn't summon any!");
		}
		break;
	case Spells::CLONE:
		{
			CStack * clonedStack = NULL;
			if (attackedCres.size())
				clonedStack = *attackedCres.begin();
			if (!clonedStack)
			{
				complain ("No target stack to clone!");
				return;
			}

			BattleStackAdded bsa;
			bsa.creID = clonedStack->type->idNumber;
			bsa.attacker = !(bool)casterSide;
			bsa.summoned = true;
			bsa.pos = gs->curB->getAvaliableHex(bsa.creID, !(bool)casterSide); //TODO: unify it
			bsa.amount = clonedStack->count;
			sendAndApply (&bsa);

			BattleSetStackProperty ssp;
			ssp.stackID = gs->curB->stacks.back()->ID; //how to get recent stack?
			ssp.which = BattleSetStackProperty::CLONED; //using enum values
			ssp.val = 0;
			ssp.absolute = 1;
			sendAndApply(&ssp);
		}
		break;
	case Spells::REMOVE_OBSTACLE:
		{
			ObstaclesRemoved obr;
			BOOST_FOREACH(auto &obstacle, battleGetAllObstacles())
			{
				if(vstd::contains(obstacle->getBlockedTiles(), destination))
					obr.obstacles.insert(obstacle->uniqueID);
			}

			if(!obr.obstacles.empty())
				sendAndApply(&obr);
			else
				complain("There's no obstacle to remove!");
			break;
		}
		break;
	case Spells::DEATH_STARE: //handled in a bit different way
		{
			for(std::set<CStack*>::iterator it = attackedCres.begin(); it != attackedCres.end(); ++it)
			{
				if((*it)->hasBonusOfType(Bonus::UNDEAD) || (*it)->hasBonusOfType(Bonus::NON_LIVING)) //this creature is immune
				{
					sc.dmgToDisplay = 0; //TODO: handle Death Stare for multiple targets (?)
					continue;
				}

				BattleStackAttacked bsa;
				bsa.flags |= BattleStackAttacked::EFFECT;
				bsa.effect = spell->mainEffectAnim; //from config\spell-Info.txt
				bsa.damageAmount = usedSpellPower * (*it)->valOfBonuses(Bonus::STACK_HEALTH);
				bsa.stackAttacked = (*it)->ID;
				bsa.attackerID = -1;
				(*it)->prepareAttacked(bsa);
				si.stacks.push_back(bsa);
			}
		}
		break;
	case Spells::ACID_BREATH_DAMAGE: //new effect, separate from acid breath defense reduction
		{
			for(std::set<CStack*>::iterator it = attackedCres.begin(); it != attackedCres.end(); ++it) //no immunities
			{
				BattleStackAttacked bsa;
				bsa.flags |= BattleStackAttacked::EFFECT;
				bsa.effect = VLC->spellh->spells[80]->mainEffectAnim; //use acid breath
				bsa.damageAmount = usedSpellPower; //damage times the number of attackers
				bsa.stackAttacked = (*it)->ID;
				bsa.attackerID = -1;
				(*it)->prepareAttacked(bsa);
				si.stacks.push_back(bsa);
			}
		}
		break;
	}

	sendAndApply(&sc);
	if(!si.stacks.empty()) //after spellcast info shows
		sendAndApply(&si);

	if (mode == ECastingMode::CREATURE_ACTIVE_CASTING || mode == ECastingMode::ENCHANTER_CASTING) //reduce number of casts remaining
	{
		BattleSetStackProperty ssp;
		ssp.stackID = stack->ID;
		ssp.which = BattleSetStackProperty::CASTS;
		ssp.val = -1;
		ssp.absolute = false;
		sendAndApply(&ssp);
	}

	//Magic Mirror effect
	if (spell->isNegative() && mode != ECastingMode::MAGIC_MIRROR && spell->level && spell->range[0] == "0") //it is actual spell and can be reflected to single target, no recurrence
	{
		for(std::set<CStack*>::iterator it = attackedCres.begin(); it != attackedCres.end(); ++it)
		{
			int mirrorChance = (*it)->valOfBonuses(Bonus::MAGIC_MIRROR);
			if(mirrorChance > rand()%100)
			{
				std::vector<CStack *> mirrorTargets;
				std::vector<CStack *> & battleStacks = gs->curB->stacks;
				for (size_t j = 0; j < battleStacks.size(); ++j)
				{
					if(battleStacks[j]->owner == casterSide) //get enemy stacks which can be affected by this spell
					{
						if (!gs->curB->battleIsImmune(NULL, spell, ECastingMode::MAGIC_MIRROR, battleStacks[j]->position))
							mirrorTargets.push_back(battleStacks[j]);
					}
				}
				if (mirrorTargets.size())
				{
					int targetHex = mirrorTargets[rand() % mirrorTargets.size()]->position;
					handleSpellCasting(spellID, 0, targetHex, 1 - casterSide, (*it)->owner, NULL, (caster ? caster : NULL), usedSpellPower, ECastingMode::MAGIC_MIRROR, (*it));
				}
			}
		}
	}
}

bool CGameHandler::makeCustomAction( BattleAction &ba )
{
	switch(ba.actionType)
	{
	case BattleAction::HERO_SPELL: //hero casts spell
		{
			const CGHeroInstance *h = gs->curB->heroes[ba.side];
			const CGHeroInstance *secondHero = gs->curB->heroes[!ba.side];
			if(!h)
			{
				tlog2 << "Wrong caster!\n";
				return false;
			}
			if(ba.additionalInfo >= VLC->spellh->spells.size())
			{
				tlog2 << "Wrong spell id (" << ba.additionalInfo << ")!\n";
				return false;
			}

			const CSpell *s = VLC->spellh->spells[ba.additionalInfo];
			if (s->mainEffectAnim > -1 || (s->id >= 66 || s->id <= 69) || s->id == Spells::CLONE) //allow summon elementals
				//TODO: special effects, like Clone
			{
				ui8 skill = h->getSpellSchoolLevel(s); //skill level

				ESpellCastProblem::ESpellCastProblem escp = gs->curB->battleCanCastThisSpell(h->tempOwner, s, ECastingMode::HERO_CASTING);
				if(escp != ESpellCastProblem::OK)
				{
					tlog2 << "Spell cannot be cast!\n";
					tlog2 << "Problem : " << escp << std::endl;
					return false;
				}

				StartAction start_action(ba);
				sendAndApply(&start_action); //start spell casting

				handleSpellCasting (ba.additionalInfo, skill, ba.destinationTile, ba.side, h->tempOwner, h, secondHero, h->getPrimSkillLevel(2),
									ECastingMode::HERO_CASTING, NULL, ba.selectedStack);

				sendAndApply(&end_action);
				if( !gs->curB->getStack(gs->curB->activeStack, false)->alive() )
				{
					battleMadeAction.setn(true);
				}
				checkForBattleEnd(gs->curB->stacks);
				if(battleResult.get())
				{
					battleMadeAction.setn(true);
					//battle will be ended by startBattle function
					//endBattle(gs->curB->tile, gs->curB->heroes[0], gs->curB->heroes[1]);
				}

				return true;
			}
			else
			{
				tlog2 << "Spell " << s->name << " is not yet supported!\n";
				return false;
			}
		}
	}
	return false;
}

void CGameHandler::stackTurnTrigger(const CStack * st)
{
	BattleTriggerEffect bte;
	bte.stackID = st->ID;
	bte.effect = -1;
	bte.val = 0;
	bte.additionalInfo = 0;
	if (st->alive())
	{
		//unbind
		if (st->getEffect(72))
		{
			bool unbind = true;
			BonusList bl = *(st->getBonuses(Selector::type(Bonus::BIND_EFFECT)));
			std::set<CStack*> stacks = gs->curB->getAdjacentCreatures(st);

			BOOST_FOREACH(Bonus * b, bl)
			{
				const CStack * stack = gs->curB->getStack(b->additionalInfo); //binding stack must be alive and adjacent
				if (stack)
				{
					if (vstd::contains(stacks, stack)) //binding stack is still present
					{
						unbind = false;
					}
				}
			}
			if (unbind)
			{
				BattleSetStackProperty ssp;
				ssp.which = BattleSetStackProperty::UNBIND;
				ssp.stackID = st->ID;
				sendAndApply(&ssp);
			}
		}
		//regeneration
		if(st->hasBonusOfType(Bonus::HP_REGENERATION))
		{
			bte.effect = Bonus::HP_REGENERATION;
			bte.val = std::min((int)(st->MaxHealth() - st->firstHPleft), st->valOfBonuses(Bonus::HP_REGENERATION));
		}
		if(st->hasBonusOfType(Bonus::FULL_HP_REGENERATION))
		{
			bte.effect = Bonus::HP_REGENERATION;
			bte.val = st->MaxHealth() - st->firstHPleft;
		}
		if (bte.val) //anything to heal
			sendAndApply(&bte);

		if(st->hasBonusOfType(Bonus::POISON))
		{
			const Bonus * b = st->getBonus(Selector::source(Bonus::SPELL_EFFECT, 71) && Selector::type(Bonus::STACK_HEALTH));
			if (b) //TODO: what if not?...
			{
				bte.val = std::max (b->val - 10, -(st->valOfBonuses(Bonus::POISON)));
				if (bte.val < b->val) //(negative) poison effect increases - update it
				{
					bte.effect = Bonus::POISON;
					sendAndApply(&bte);
				}
			}
		}
		if (st->hasBonusOfType(Bonus::MANA_DRAIN) && !vstd::contains(st->state, EBattleStackState::DRAINED_MANA))
		{
			const CGHeroInstance * enemy = gs->curB->getHero(gs->curB->theOtherPlayer(st->owner));
			if (enemy)
			{
				ui32 manaDrained = st->valOfBonuses(Bonus::MANA_DRAIN);
				vstd::amin (manaDrained, gs->curB->heroes[0]->mana);
				if (manaDrained)
				{
					bte.effect = Bonus::MANA_DRAIN;
					bte.val = manaDrained;
					bte.additionalInfo = enemy->id; //for sanity
					sendAndApply(&bte);
				}
			}
		}
		if (!st->hasBonusOfType(Bonus::FEARLESS))
		{
			bool fearsomeCreature = false;
			BOOST_FOREACH(CStack * stack, gs->curB->stacks)
			{
				if (stack->owner != st->owner && stack->hasBonusOfType(Bonus::FEAR))
				{
					fearsomeCreature = true;
					break;
				}
			}
			if (fearsomeCreature)
			{
				if (rand() % 100 < 10) //fixed 10%
				{
					bte.effect = Bonus::FEAR;
					sendAndApply(&bte);
				}
			}
		}
		BonusList bl = *(st->getBonuses(Selector::type(Bonus::ENCHANTER)));
		int side = gs->curB->whatSide(st->owner);
		if (bl.size() && st->casts && !gs->curB->enchanterCounter[side])
		{
			int index = rand() % bl.size();
			int spellID = bl[index]->subtype; //spell ID
			if (gs->curB->battleCanCastThisSpell(st->owner, VLC->spellh->spells[spellID], ECastingMode::ENCHANTER_CASTING)) //TODO: select another?
			{
				int spellLeveL = bl[index]->val; //spell level
				const CGHeroInstance * enemyHero = gs->curB->getHero(gs->curB->theOtherPlayer(st->owner));
				handleSpellCasting(spellID, spellLeveL, -1, side, st->owner, NULL, enemyHero, 0, ECastingMode::ENCHANTER_CASTING, st);

				BattleSetStackProperty ssp;
				ssp.which = BattleSetStackProperty::ENCHANTER_COUNTER;
				ssp.absolute = false;
				ssp.val = bl[index]->additionalInfo; //increase cooldown counter
				ssp.stackID = st->ID;
				sendAndApply(&ssp);
			}
		}
	}
}

void CGameHandler::handleDamageFromObstacle(const CObstacleInstance &obstacle, CStack * curStack)
{
	//we want to determine following vars depending on obstacle type
	int damage = -1;
	int effect = -1;
	bool oneTimeObstacle = false;

	//helper info
	const SpellCreatedObstacle *spellObstacle = dynamic_cast<const SpellCreatedObstacle*>(&obstacle); //not nice but we may need spell params
	const ui8 side = !curStack->attackerOwned;
	const CGHeroInstance *hero = gs->curB->heroes[side];

	if(obstacle.obstacleType == CObstacleInstance::MOAT)
	{
		damage = battleGetMoatDmg();
	}
	else if(obstacle.obstacleType == CObstacleInstance::LAND_MINE)
	{
		//You don't get hit by a Mine you can see.
		if(gs->curB->isObstacleVisibleForSide(obstacle, side))
			return;

		oneTimeObstacle = true;
		effect = 82; //makes
		damage = gs->curB->calculateSpellDmg(VLC->spellh->spells[Spells::LAND_MINE], hero, curStack,
											 spellObstacle->spellLevel, spellObstacle->casterSpellPower);
		//TODO even if obstacle wasn't created by hero (Tower "moat") it should deal dmg as if casted by hero,
		//if it is bigger than default dmg. Or is it just irrelevant H3 implementation quirk
	}
	else if(obstacle.obstacleType == CObstacleInstance::FIRE_WALL)
	{
		damage = gs->curB->calculateSpellDmg(VLC->spellh->spells[Spells::FIRE_WALL], hero, curStack,
											 spellObstacle->spellLevel, spellObstacle->casterSpellPower);
	}
	else
	{
		//no other obstacle does damage to stack
		return;
	}

	BattleStackAttacked bsa;
	if(effect >= 0)
	{
		bsa.flags |= BattleStackAttacked::EFFECT;
		bsa.effect = effect; //makes POOF
	}
	bsa.damageAmount = damage;
	bsa.stackAttacked = curStack->ID;
	bsa.attackerID = -1;
	curStack->prepareAttacked(bsa);

	StacksInjured si;
	si.stacks.push_back(bsa);
	sendAndApply(&si);

	if(oneTimeObstacle)
		removeObstacle(obstacle);
}

void CGameHandler::handleTimeEvents()
{
	gs->map->events.sort(evntCmp);
	while(gs->map->events.size() && gs->map->events.front()->firstOccurence+1 == gs->day)
	{
		CMapEvent *ev = gs->map->events.front();
		for(int player = 0; player < GameConstants::PLAYER_LIMIT; player++)
		{
			PlayerState *pinfo = gs->getPlayer(player);

			if( pinfo  //player exists
				&& (ev->players & 1<<player) //event is enabled to this player
				&& ((ev->computerAffected && !pinfo->human)
					|| (ev->humanAffected && pinfo->human)
				)
			)
			{
				//give resources
				SetResources sr;
				sr.player = player;
				sr.res = pinfo->resources + ev->resources;

				//prepare dialog
				InfoWindow iw;
				iw.player = player;
				iw.text << ev->message;

				for (int i=0; i<ev->resources.size(); i++)
				{
					if(ev->resources[i]) //if resource is changed, we add it to the dialog
						iw.components.push_back(Component(Component::RESOURCE,i,ev->resources[i],0));
				}

				if (iw.components.size())
				{
					sr.res.amax(0); // If removing too much resources, adjust the amount so the total doesn't become negative.
					sendAndApply(&sr); //update player resources if changed
				}

				sendAndApply(&iw); //show dialog
			}
		} //PLAYERS LOOP

		if(ev->nextOccurence)
		{
			gs->map->events.pop_front();

			ev->firstOccurence += ev->nextOccurence;
			std::list<ConstTransitivePtr<CMapEvent> >::iterator it = gs->map->events.begin();
			while ( it !=gs->map->events.end() && **it <= *ev )
				it++;
			gs->map->events.insert(it, ev);
		}
		else
		{
			delete ev;
			gs->map->events.pop_front();
		}
	}
}

void CGameHandler::handleTownEvents(CGTownInstance * town, NewTurn &n, std::map<si32, std::map<si32, si32> > &newCreas)
{
	//TODO event removing desync!!!
	town->events.sort(evntCmp);
	while(town->events.size() && town->events.front()->firstOccurence == gs->day)
	{
		ui8 player = town->tempOwner;
		CCastleEvent *ev = town->events.front();
		PlayerState *pinfo = gs->getPlayer(player);

		if( pinfo  //player exists
			&& (ev->players & 1<<player) //event is enabled to this player
			&& ((ev->computerAffected && !pinfo->human)
				|| (ev->humanAffected && pinfo->human) ) )
		{


			// dialog
			InfoWindow iw;
			iw.player = player;
			iw.text << ev->message;

			if(ev->resources.nonZero())
			{
				TResources was = n.res[player];
				n.res[player] += ev->resources;
				n.res[player].amax(0);

				for (int i=0; i<ev->resources.size(); i++)
					if(ev->resources[i] && pinfo->resources[i] != n.res[player][i]) //if resource had changed, we add it to the dialog
						iw.components.push_back(Component(Component::RESOURCE,i,n.res[player][i]-was[i],0));

			}

			for(std::set<si32>::iterator i = ev->buildings.begin(); i!=ev->buildings.end();i++)
				if ( !vstd::contains(town->builtBuildings, *i))
				{
					buildStructure(town->id, *i, true);
					iw.components.push_back(Component(Component::BUILDING, town->subID, *i, 0));
				}

			for(si32 i=0;i<ev->creatures.size();i++) //creature growths
			{
				if(town->creatureDwelling(i) && ev->creatures[i])//there is dwelling
				{
					newCreas[town->id][i] += ev->creatures[i];
					iw.components.push_back(Component(Component::CREATURE,
							town->creatures[i].second.back(), ev->creatures[i], 0));
				}
			}
			sendAndApply(&iw); //show dialog
		}

		if(ev->nextOccurence)
		{
			town->events.pop_front();

			ev->firstOccurence += ev->nextOccurence;
			std::list<CCastleEvent*>::iterator it = town->events.begin();
			while ( it !=town->events.end() &&  **it <= *ev )
				it++;
			town->events.insert(it, ev);
		}
		else
		{
			delete ev;
			town->events.pop_front();
		}
	}
}

bool CGameHandler::complain( const std::string &problem )
{
	sendMessageToAll("Server encountered a problem: " + problem);
	tlog1 << problem << std::endl;
	return true;
}

ui32 CGameHandler::getQueryResult( ui8 player, int queryID )
{
	//TODO: write
	return 0;
}

void CGameHandler::showGarrisonDialog( int upobj, int hid, bool removableUnits, const boost::function<void()> &cb )
{
	ui8 player = getOwner(hid);
	GarrisonDialog gd;
	gd.hid = hid;
	gd.objid = upobj;
	gd.removableUnits = removableUnits;

	{
		boost::unique_lock<boost::recursive_mutex> lock(gsm);
		prepareNewQuery(&gd, player);

		//register callback manually since we need to use query ID that's given in result of prepareNewQuery call
		callbacks[gd.queryID] = [=](ui32 answer)
		{
			// Garrison callback calls the "original callback" and closes the exchange between objs.
			if (cb)
				cb();
			boost::unique_lock<boost::recursive_mutex> lockGsm(this->gsm);
			allowedExchanges.erase(gd.queryID);
		};

		allowedExchanges[gd.queryID] = std::pair<si32,si32>(upobj,hid);
		sendAndApply(&gd);
	}
}

void CGameHandler::showThievesGuildWindow(int player, int requestingObjId)
{
	OpenWindow ow;
	ow.window = OpenWindow::THIEVES_GUILD;
	ow.id1 = player;
	ow.id2 = requestingObjId;
	sendAndApply(&ow);
}

bool CGameHandler::isAllowedArrangePack(const ArrangeStacks *pack)
{
	return isAllowedExchangeForQuery(pack->id1, pack->id2);
}

bool CGameHandler::isAllowedExchangeForQuery(int id1, int id2) {
	boost::unique_lock<boost::recursive_mutex> lock(gsm);
	for(std::map<ui32, std::pair<si32,si32> >::const_iterator i = allowedExchanges.begin(); i!=allowedExchanges.end(); i++)
		if((id1 == i->second.first && id2 == i->second.second) ||
		   (id2 == i->second.first && id1 == i->second.second))
			return true;

	return false;
}

bool CGameHandler::isAllowedExchange( int id1, int id2 )
{
	if(id1 == id2)
		return true;

	if (isAllowedExchangeForQuery(id1, id2))
		return true;

	const CGObjectInstance *o1 = getObj(id1), *o2 = getObj(id2);
	if (o1 && o2)
	{
		if(o1->ID == GameConstants::TOWNI_TYPE)
		{
			const CGTownInstance *t = static_cast<const CGTownInstance*>(o1);
			if(t->visitingHero == o2  ||  t->garrisonHero == o2)
				return true;
		}
		if(o2->ID == GameConstants::TOWNI_TYPE)
		{
			const CGTownInstance *t = static_cast<const CGTownInstance*>(o2);
			if(t->visitingHero == o1  ||  t->garrisonHero == o1)
				return true;
		}
		if(o1->ID == GameConstants::HEROI_TYPE && o2->ID == GameConstants::HEROI_TYPE
			&& distance(o1->pos, o2->pos) < 2) //hero stands on the same tile or on the neighbouring tiles
		{
			//TODO: it's workaround, we should check if first hero visited second and player hasn't closed exchange window
			//(to block moving stacks for free [without visiting] between heroes)
			return true;
		}
	}
	else //not exchanging between heroes, TODO: more sophisticated logic
	{
		return true;
	}
	return false;
}

void CGameHandler::objectVisited( const CGObjectInstance * obj, const CGHeroInstance * h )
{
	HeroVisit hv;
	hv.obj = obj;
	hv.hero = h;
	hv.starting = true;
	sendAndApply(&hv);

	obj->onHeroVisit(h);

	hv.obj = NULL; //not necessary, moreover may have been deleted in the meantime
	hv.starting = false;
	sendAndApply(&hv);
}

bool CGameHandler::buildBoat( ui32 objid )
{
	const IShipyard *obj = IShipyard::castFrom(getObj(objid));

	if(obj->state())
	{
		complain("Cannot build boat in this shipyard!");
		return false;
	}
	else if(obj->o->ID == GameConstants::TOWNI_TYPE
		&& !vstd::contains((static_cast<const CGTownInstance*>(obj))->builtBuildings,6))
	{
		complain("Cannot build boat in the town - no shipyard!");
		return false;
	}

	//TODO use "real" cost via obj->getBoatCost
	if(getResource(obj->o->tempOwner, 6) < 1000  ||  getResource(obj->o->tempOwner, 0) < 10)
	{
		complain("Not enough resources to build a boat!");
		return false;
	}

	int3 tile = obj->bestLocation();
	if(!gs->map->isInTheMap(tile))
	{
		complain("Cannot find appropriate tile for a boat!");
		return false;
	}

	//take boat cost
	SetResources sr;
	sr.player = obj->o->tempOwner;
	sr.res = gs->getPlayer(obj->o->tempOwner)->resources;
	sr.res[Res::WOOD] -= 10;
	sr.res[Res::GOLD] -= 1000;
	sendAndApply(&sr);

	//create boat
	NewObject no;
	no.ID = 8;
	no.subID = obj->getBoatType();
	no.pos = tile + int3(1,0,0);
	sendAndApply(&no);

	return true;
}

void CGameHandler::engageIntoBattle( ui8 player )
{
	if(vstd::contains(states.players, player))
		states.setFlag(player,&PlayerStatus::engagedIntoBattle,true);

	//notify interfaces
	PlayerBlocked pb;
	pb.player = player;
	pb.reason = PlayerBlocked::UPCOMING_BATTLE;
	sendAndApply(&pb);
}

void CGameHandler::winLoseHandle(ui8 players )
{

	for(size_t i = 0; i < GameConstants::PLAYER_LIMIT; i++)
	{
		if(players & 1<<i  &&  gs->getPlayer(i))
		{
			checkLossVictory(i);
		}
	}
}

void CGameHandler::checkLossVictory( ui8 player )
{
	const PlayerState *p = gs->getPlayer(player);
	if(p->status) //player already won / lost
		return;

	int loss = gs->lossCheck(player);
	int vic = gs->victoryCheck(player);

	if(!loss && !vic)
		return;


	InfoWindow iw;
	getLossVicMessage(player, vic ? vic : loss , vic, iw);
	sendAndApply(&iw);
	PlayerEndsGame peg;
	peg.player = player;
	peg.victory = vic;
	sendAndApply(&peg);

	if(vic) //one player won -> all enemies lost
	{
		iw.text.localStrings.front().second++; //message about losing because enemy won first is just after victory message

		for (bmap<ui8,PlayerState>::const_iterator i = gs->players.begin(); i!=gs->players.end(); i++)
		{
			if(i->first < GameConstants::PLAYER_LIMIT && i->first != player)//FIXME: skip already eliminated players?
			{
				iw.player = i->first;
				sendAndApply(&iw);

				peg.player = i->first;
				peg.victory = gameState()->getPlayerRelations(player, i->first) == 1; // ally of winner
				sendAndApply(&peg);
			}
		}
	}
	else //player lost -> all his objects become unflagged (neutral)
	{
		std::vector<ConstTransitivePtr<CGHeroInstance> > hlp = p->heroes;
		for (std::vector<ConstTransitivePtr<CGHeroInstance> >::const_iterator i = hlp.begin(); i != hlp.end(); i++) //eliminate heroes
			removeObject((*i)->id);

		for (std::vector<ConstTransitivePtr<CGObjectInstance> >::const_iterator i = gs->map->objects.begin(); i != gs->map->objects.end(); i++) //unflag objs
		{
			if(*i  &&  (*i)->tempOwner == player)
				setOwner((**i).id,GameConstants::NEUTRAL_PLAYER);
		}

		//eliminating one player may cause victory of another:
		winLoseHandle(GameConstants::ALL_PLAYERS & ~(1<<player));
	}

	if(vic)
	{
		end2 = true;

		if(gs->campaign)
		{
			std::vector<CGHeroInstance *> hes;
			BOOST_FOREACH(CGHeroInstance * ghi, gs->map->heroes)
			{
				if (ghi->tempOwner == vic)
				{
					hes.push_back(ghi);
				}
			}
			gs->campaign->mapConquered(hes);

			UpdateCampaignState ucs;
			ucs.camp = gs->campaign;
			sendAndApply(&ucs);
		}
	}
}

void CGameHandler::getLossVicMessage( ui8 player, si8 standard, bool victory, InfoWindow &out ) const
{
//	const PlayerState *p = gs->getPlayer(player);
// 	if(!p->human)
// 		return; //AI doesn't need text info of loss

	out.player = player;

	if(victory)
	{
		if(standard > 0) //not std loss
		{
			switch(gs->map->victoryCondition.condition)
			{
			case EVictoryConditionType::ARTIFACT:
				out.text.addTxt(MetaString::GENERAL_TXT, 280); //Congratulations! You have found the %s, and can claim victory!
				out.text.addReplacement(MetaString::ART_NAMES,gs->map->victoryCondition.ID); //artifact name
				break;
			case EVictoryConditionType::GATHERTROOP:
				out.text.addTxt(MetaString::GENERAL_TXT, 276); //Congratulations! You have over %d %s in your armies. Your enemies have no choice but to bow down before your power!
				out.text.addReplacement(gs->map->victoryCondition.count);
				out.text.addReplacement(MetaString::CRE_PL_NAMES, gs->map->victoryCondition.ID);
				break;
			case EVictoryConditionType::GATHERRESOURCE:
				out.text.addTxt(MetaString::GENERAL_TXT, 278); //Congratulations! You have collected over %d %s in your treasury. Victory is yours!
				out.text.addReplacement(gs->map->victoryCondition.count);
				out.text.addReplacement(MetaString::RES_NAMES, gs->map->victoryCondition.ID);
				break;
			case EVictoryConditionType::BUILDCITY:
				out.text.addTxt(MetaString::GENERAL_TXT, 282); //Congratulations! You have successfully upgraded your town, and can claim victory!
				break;
			case EVictoryConditionType::BUILDGRAIL:
				out.text.addTxt(MetaString::GENERAL_TXT, 284); //Congratulations! You have constructed a permanent home for the Grail, and can claim victory!
				break;
			case EVictoryConditionType::BEATHERO:
				{
					out.text.addTxt(MetaString::GENERAL_TXT, 252); //Congratulations! You have completed your quest to defeat the enemy hero %s. Victory is yours!
					const CGHeroInstance *h = dynamic_cast<const CGHeroInstance*>(gs->map->victoryCondition.obj);
					assert(h);
					out.text.addReplacement(h->name);
				}
				break;
			case EVictoryConditionType::CAPTURECITY:
				{
					out.text.addTxt(MetaString::GENERAL_TXT, 249); //Congratulations! You captured %s, and are victorious!
					const CGTownInstance *t = dynamic_cast<const CGTownInstance*>(gs->map->victoryCondition.obj);
					assert(t);
					out.text.addReplacement(t->name);
				}
				break;
			case EVictoryConditionType::BEATMONSTER:
				out.text.addTxt(MetaString::GENERAL_TXT, 286); //Congratulations! You have completed your quest to kill the fearsome beast, and can claim victory!
				break;
			case EVictoryConditionType::TAKEDWELLINGS:
				out.text.addTxt(MetaString::GENERAL_TXT, 288); //Congratulations! Your flag flies on the dwelling of every creature. Victory is yours!
				break;
			case EVictoryConditionType::TAKEMINES:
				out.text.addTxt(MetaString::GENERAL_TXT, 290); //Congratulations! Your flag flies on every mine. Victory is yours!
				break;
			case EVictoryConditionType::TRANSPORTITEM:
				out.text.addTxt(MetaString::GENERAL_TXT, 292); //Congratulations! You have reached your destination, precious cargo intact, and can claim victory!
				break;
			}
		}
		else
		{
			out.text.addTxt(MetaString::GENERAL_TXT, 659); //Congratulations! You have reached your destination, precious cargo intact, and can claim victory!
		}
	}
	else
	{
		if(standard > 0) //not std loss
		{
			switch(gs->map->lossCondition.typeOfLossCon)
			{
			case ELossConditionType::LOSSCASTLE:
				{
					out.text.addTxt(MetaString::GENERAL_TXT, 251); //The town of %s has fallen - all is lost!
					const CGTownInstance *t = dynamic_cast<const CGTownInstance*>(gs->map->lossCondition.obj);
					assert(t);
					out.text.addReplacement(t->name);
				}
				break;
			case ELossConditionType::LOSSHERO:
				{
					out.text.addTxt(MetaString::GENERAL_TXT, 253); //The hero, %s, has suffered defeat - your quest is over!
					const CGHeroInstance *h = dynamic_cast<const CGHeroInstance*>(gs->map->lossCondition.obj);
					assert(h);
					out.text.addReplacement(h->name);
				}
				break;
			case ELossConditionType::TIMEEXPIRES:
				out.text.addTxt(MetaString::GENERAL_TXT, 254); //Alas, time has run out on your quest. All is lost.
				break;
			}
		}
		else if(standard == 2)
		{
			out.text.addTxt(MetaString::GENERAL_TXT, 7);//%s, your heroes abandon you, and you are banished from this land.
			out.text.addReplacement(MetaString::COLOR, player);
			out.components.push_back(Component(Component::FLAG,player,0,0));
		}
		else //lost all towns and heroes
		{
			out.text.addTxt(MetaString::GENERAL_TXT, 660); //All your forces have been defeated, and you are banished from this land!
		}
	}
}

bool CGameHandler::dig( const CGHeroInstance *h )
{
	for (std::vector<ConstTransitivePtr<CGObjectInstance> >::const_iterator i = gs->map->objects.begin(); i != gs->map->objects.end(); i++) //unflag objs
	{
		if(*i && (*i)->ID == 124  &&  (*i)->pos == h->getPosition())
		{
			complain("Cannot dig - there is already a hole under the hero!");
			return false;
		}
	}

	if(h->diggingStatus() != CGHeroInstance::CAN_DIG) //checks for terrain and movement
		COMPLAIN_RETF("Hero cannot dig (error code %d)!", h->diggingStatus());

	//create a hole
	NewObject no;
	no.ID = 124;
	no.pos = h->getPosition();
	no.subID = getTile(no.pos)->tertype;
	sendAndApply(&no);

	//take MPs
	SetMovePoints smp;
	smp.hid = h->id;
	smp.val = 0;
	sendAndApply(&smp);

	InfoWindow iw;
	iw.player = h->tempOwner;
	if(gs->map->grailPos == h->getPosition())
	{
		iw.text.addTxt(MetaString::GENERAL_TXT, 58); //"Congratulations! After spending many hours digging here, your hero has uncovered the "
		iw.text.addTxt(MetaString::ART_NAMES, 2);
		iw.soundID = soundBase::ULTIMATEARTIFACT;
		giveHeroNewArtifact(h, VLC->arth->artifacts[2], -1); //give grail
		sendAndApply(&iw);

		iw.soundID = soundBase::invalid;
		iw.text.clear();
		iw.text.addTxt(MetaString::ART_DESCR, 2);
		sendAndApply(&iw);
	}
	else
	{
		iw.text.addTxt(MetaString::GENERAL_TXT, 59); //"Nothing here. \n Where could it be?"
		iw.soundID = soundBase::Dig;
		sendAndApply(&iw);
	}

	return true;
}

void CGameHandler::attackCasting(const BattleAttack & bat, Bonus::BonusType attackMode, const CStack * attacker)
{
	if(attacker->hasBonusOfType(attackMode))
	{
		std::set<ui32> spellsToCast;
		TBonusListPtr spells = attacker->getBonuses(Selector::type(attackMode));
		BOOST_FOREACH(const Bonus *sf, *spells)
		{
			spellsToCast.insert (sf->subtype);
		}
		BOOST_FOREACH(ui32 spellID, spellsToCast)
		{
			const CStack * oneOfAttacked = NULL;
			for (int g=0; g<bat.bsa.size(); ++g)
			{
				if (bat.bsa[g].newAmount > 0 && !bat.bsa[g].isSecondary()) //apply effects only to first target stack if it's alive
				{
					oneOfAttacked = gs->curB->getStack(bat.bsa[g].stackAttacked);
					break;
				}
			}
			bool castMe = false;
			int meleeRanged;
			if(oneOfAttacked == NULL) //all attacked creatures have been killed
				return;
			int spellLevel = 0;
			TBonusListPtr spellsByType = attacker->getBonuses(Selector::typeSubtype(attackMode, spellID));
			BOOST_FOREACH(const Bonus *sf, *spellsByType)
			{
				vstd::amax(spellLevel, sf->additionalInfo % 1000); //pick highest level
				meleeRanged = sf->additionalInfo / 1000;
				if (meleeRanged == 0 || (meleeRanged == 1 && bat.shot()) || (meleeRanged == 2 && !bat.shot()))
					castMe = true;
			}
			int chance = attacker->valOfBonuses((Selector::typeSubtype(attackMode, spellID)));
			vstd::amin (chance, 100);
			int destination = oneOfAttacked->position;

			const CSpell * spell = VLC->spellh->spells[spellID];
			if(gs->curB->battleCanCastThisSpellHere(attacker->owner, spell, ECastingMode::AFTER_ATTACK_CASTING, oneOfAttacked->position) != ESpellCastProblem::OK)
				continue;

			//check if spell should be casted (probability handling)
			if(rand()%100 >= chance)
				continue;

			//casting //TODO: check if spell can be blocked or target is immune
			if (castMe) //stacks use 0 spell power. If needed, default = 3 or custom value is used
				handleSpellCasting(spellID, spellLevel, destination, !attacker->attackerOwned, attacker->owner, NULL, NULL, 0, ECastingMode::AFTER_ATTACK_CASTING, attacker);
		}
	}
}

void CGameHandler::handleAttackBeforeCasting (const BattleAttack & bat)
{
	const CStack * attacker = gs->curB->getStack(bat.stackAttacking);
	attackCasting(bat, Bonus::SPELL_BEFORE_ATTACK, attacker); //no detah stare / acid bretah needed?
}

void CGameHandler::handleAfterAttackCasting( const BattleAttack & bat )
{
	const CStack * attacker = gs->curB->getStack(bat.stackAttacking);
	if (!attacker) //could be already dead
		return;
	attackCasting(bat, Bonus::SPELL_AFTER_ATTACK, attacker);

	if(bat.bsa[0].newAmount <= 0)
	{
		//don't try death stare or acid breath on dead stack (crash!)
		return;
	}

	if (attacker->hasBonusOfType(Bonus::DEATH_STARE)) // spell id 79
	{
		int staredCreatures = 0;
		double mean = attacker->count * attacker->valOfBonuses(Bonus::DEATH_STARE, 0) / 100;
		if (mean >= 1)
		{
			boost::poisson_distribution<int, double> p((int)mean);
			boost::mt19937 rng;
			boost::variate_generator<boost::mt19937&, boost::poisson_distribution<int, double> > dice (rng, p);
			staredCreatures += dice();
		}
		if (((int)(mean * 100)) < rand() % 100) //fractional chance for one last kill
			++staredCreatures;

		staredCreatures += attacker->type->level * attacker->valOfBonuses(Bonus::DEATH_STARE, 1);
		if (staredCreatures)
		{
			if (bat.bsa.size() && bat.bsa[0].newAmount > 0) //TODO: death stare was not originally available for multiple-hex attacks, but...
			handleSpellCasting(79, 0, gs->curB->getStack(bat.bsa[0].stackAttacked)->position,
				!attacker->attackerOwned, attacker->owner, NULL, NULL, staredCreatures, ECastingMode::AFTER_ATTACK_CASTING, attacker);
		}
	}

	int acidDamage = 0;
	TBonusListPtr acidBreath = attacker->getBonuses(Selector::type(Bonus::ACID_BREATH));
	BOOST_FOREACH(const Bonus *b, *acidBreath)
	{
		if (b->additionalInfo > rand()%100)
			acidDamage += b->val;
	}
	if (acidDamage)
	{
		handleSpellCasting(81, 0, gs->curB->getStack(bat.bsa[0].stackAttacked)->position,
				!attacker->attackerOwned, attacker->owner, NULL, NULL,
				acidDamage * attacker->count, ECastingMode::AFTER_ATTACK_CASTING, attacker);
	}
}

bool CGameHandler::castSpell(const CGHeroInstance *h, int spellID, const int3 &pos)
{
	const CSpell *s = VLC->spellh->spells[spellID];
	int cost = h->getSpellCost(s);
	int schoolLevel = h->getSpellSchoolLevel(s);

	if(!h->canCastThisSpell(s))
		COMPLAIN_RET("Hero cannot cast this spell!");
	if(h->mana < cost)
		COMPLAIN_RET("Hero doesn't have enough spell points to cast this spell!");
	if(s->combatSpell)
		COMPLAIN_RET("This function can be used only for adventure map spells!");

	AdvmapSpellCast asc;
	asc.caster = h;
	asc.spellID = spellID;
	sendAndApply(&asc);

	using namespace Spells;
	switch(spellID)
	{
	case SUMMON_BOAT: //Summon Boat
		{
			//check if spell works at all
			if(rand() % 100 >= s->powers[schoolLevel]) //power is % chance of success
			{
				InfoWindow iw;
				iw.player = h->tempOwner;
				iw.text.addTxt(MetaString::GENERAL_TXT, 336); //%s tried to summon a boat, but failed.
				iw.text.addReplacement(h->name);
				sendAndApply(&iw);
				break;
			}

			//try to find unoccupied boat to summon
			const CGBoat *nearest = NULL;
			double dist = 0;
			int3 summonPos = h->bestLocation();
			if(summonPos.x < 0)
				COMPLAIN_RET("There is no water tile available!");

			BOOST_FOREACH(const CGObjectInstance *obj, gs->map->objects)
			{
				if(obj && obj->ID == 8)
				{
					const CGBoat *b = static_cast<const CGBoat*>(obj);
					if(b->hero) continue; //we're looking for unoccupied boat

					double nDist = distance(b->pos, h->getPosition());
					if(!nearest || nDist < dist) //it's first boat or closer than previous
					{
						nearest = b;
						dist = nDist;
					}
				}
			}

			if(nearest) //we found boat to summon
			{
				ChangeObjPos cop;
				cop.objid = nearest->id;
				cop.nPos = summonPos + int3(1,0,0);;
				cop.flags = 1;
				sendAndApply(&cop);
			}
			else if(schoolLevel < 2) //none or basic level -> cannot create boat :(
			{
				InfoWindow iw;
				iw.player = h->tempOwner;
				iw.text.addTxt(MetaString::GENERAL_TXT, 335); //There are no boats to summon.
				sendAndApply(&iw);
			}
			else //create boat
			{
				NewObject no;
				no.ID = 8;
				no.subID = h->getBoatType();
				no.pos = summonPos + int3(1,0,0);;
				sendAndApply(&no);
			}
			break;
		}

	case SCUTTLE_BOAT: //Scuttle Boat
		{
			//check if spell works at all
			if(rand() % 100 >= s->powers[schoolLevel]) //power is % chance of success
			{
				InfoWindow iw;
				iw.player = h->tempOwner;
				iw.text.addTxt(MetaString::GENERAL_TXT, 337); //%s tried to scuttle the boat, but failed
				iw.text.addReplacement(h->name);
				sendAndApply(&iw);
				break;
			}
			if(!gs->map->isInTheMap(pos))
				COMPLAIN_RET("Invalid dst tile for scuttle!");

			//TODO: test range, visibility
			const TerrainTile *t = &gs->map->getTile(pos);
			if(!t->visitableObjects.size() || t->visitableObjects.back()->ID != 8)
				COMPLAIN_RET("There is no boat to scuttle!");

			RemoveObject ro;
			ro.id = t->visitableObjects.back()->id;
			sendAndApply(&ro);
			break;
		}
	case DIMENSION_DOOR: //Dimension Door
		{
			const TerrainTile *dest = getTile(pos);
			const TerrainTile *curr = getTile(h->getSightCenter());

			if(!dest)
				COMPLAIN_RET("Destination tile doesn't exist!");
			if(!h->movement)
				COMPLAIN_RET("Hero needs movement points to cast Dimension Door!");
			if(h->getBonusesCount(Bonus::SPELL_EFFECT, Spells::DIMENSION_DOOR) >= s->powers[schoolLevel]) //limit casts per turn
			{
				InfoWindow iw;
				iw.player = h->tempOwner;
				iw.text.addTxt(MetaString::GENERAL_TXT, 338); //%s is not skilled enough to cast this spell again today.
				iw.text.addReplacement(h->name);
				sendAndApply(&iw);
				break;
			}

			GiveBonus gb;
			gb.id = h->id;
			gb.bonus = Bonus(Bonus::ONE_DAY, Bonus::NONE, Bonus::SPELL_EFFECT, 0, Spells::DIMENSION_DOOR);
			sendAndApply(&gb);

			if(!dest->isClear(curr)) //wrong dest tile
			{
				InfoWindow iw;
				iw.player = h->tempOwner;
				iw.text.addTxt(MetaString::GENERAL_TXT, 70); //Dimension Door failed!
				sendAndApply(&iw);
				break;
			}

			//we need obtain guard pos before moving hero, otherwise we get nothing, because tile will be "unguarded" by hero
			int3 guardPos = gs->guardingCreaturePosition(pos);

			TryMoveHero tmh;
			tmh.id = h->id;
			tmh.movePoints = std::max<int>(0, h->movement - 300);
			tmh.result = TryMoveHero::TELEPORTATION;
			tmh.start = h->pos;
			tmh.end = pos + h->getVisitableOffset();
			getTilesInRange(tmh.fowRevealed, pos, h->getSightRadious(), h->tempOwner,1);
			sendAndApply(&tmh);

			tryAttackingGuard(guardPos, h);
		}
		break;
	case FLY: //Fly
		{
			int subtype = schoolLevel >= 2 ? 1 : 2; //adv or expert

			GiveBonus gb;
			gb.id = h->id;
			gb.bonus = Bonus(Bonus::ONE_DAY, Bonus::FLYING_MOVEMENT, Bonus::SPELL_EFFECT, 0, Spells::FLY, subtype);
			sendAndApply(&gb);
		}
		break;
	case WATER_WALK: //Water Walk
		{
			int subtype = schoolLevel >= 2 ? 1 : 2; //adv or expert

			GiveBonus gb;
			gb.id = h->id;
			gb.bonus = Bonus(Bonus::ONE_DAY, Bonus::WATER_WALKING, Bonus::SPELL_EFFECT, 0, Spells::FLY, subtype);
			sendAndApply(&gb);
		}
		break;

	case TOWN_PORTAL: //Town Portal
		{
			if (!gs->map->isInTheMap(pos))
				COMPLAIN_RET("Destination tile not present!")
			TerrainTile tile = gs->map->getTile(pos);
			if (tile.visitableObjects.empty() || tile.visitableObjects.back()->ID != GameConstants::TOWNI_TYPE )
				COMPLAIN_RET("Town not found for Town Portal!");

			CGTownInstance * town = static_cast<CGTownInstance*>(tile.visitableObjects.back());
			if (town->tempOwner != h->tempOwner)
				COMPLAIN_RET("Can't teleport to another player!");
			if (town->visitingHero)
				COMPLAIN_RET("Can't teleport to occupied town!");

			if (h->getSpellSchoolLevel(s) < 2)
			{
				double dist = town->pos.dist2d(h->pos);
				int nearest = town->id; //nearest town's ID
				BOOST_FOREACH(const CGTownInstance * currTown, gs->getPlayer(h->tempOwner)->towns)
				{
					double curDist = currTown->pos.dist2d(h->pos);
					if (nearest == -1 || curDist < dist)
					{
						nearest = town->id;
						dist = curDist;
					}
				}
				if (town->id != nearest)
					COMPLAIN_RET("This hero can only teleport to nearest town!")
			}
			if (h->visitedTown)
				stopHeroVisitCastle(h->visitedTown->id, h->id);
			if (moveHero(h->id, town->visitablePos() + h->getVisitableOffset() ,1))
				heroVisitCastle(town->id, h->id);
		}
		break;

	case VISIONS: //Visions
	case VIEW_EARTH: //View Earth
	case DISGUISE: //Disguise
	case VIEW_AIR: //View Air
	default:
		COMPLAIN_RET("This spell is not implemented yet!");
		break;
	}

	SetMana sm;
	sm.hid = h->id;
	sm.val = h->mana - cost;
	sendAndApply(&sm);

	return true;
}

void CGameHandler::visitObjectOnTile(const TerrainTile &t, const CGHeroInstance * h)
{
	//to prevent self-visiting heroes on space press
	if(t.visitableObjects.back() != h)
		objectVisited(t.visitableObjects.back(), h);
	else if(t.visitableObjects.size() > 1)
		objectVisited(*(t.visitableObjects.end()-2),h);
}

bool CGameHandler::tryAttackingGuard(const int3 &guardPos, const CGHeroInstance * h)
{
	if(!gs->map->isInTheMap(guardPos))
		return false;

	const TerrainTile &guardTile = gs->map->terrain[guardPos.x][guardPos.y][guardPos.z];
	objectVisited(guardTile.visitableObjects.back(), h);
	visitObjectAfterVictory = true;
	return true;
}

bool CGameHandler::sacrificeCreatures(const IMarket *market, const CGHeroInstance *hero, TSlot slot, ui32 count)
{
	int oldCount = hero->getStackCount(slot);

	if(oldCount < count)
		COMPLAIN_RET("Not enough creatures to sacrifice!")
	else if(oldCount == count && hero->Slots().size() == 1 && hero->needsLastStack())
		COMPLAIN_RET("Cannot sacrifice last creature!");

	int crid = hero->getStack(slot).type->idNumber;

	changeStackCount(StackLocation(hero, slot), -count);

	int dump, exp;
	market->getOffer(crid, 0, dump, exp, EMarketMode::CREATURE_EXP);
	exp *= count;
	changePrimSkill(hero->id, 4, hero->calculateXp(exp));

	return true;
}

bool CGameHandler::sacrificeArtifact(const IMarket * m, const CGHeroInstance * hero, int slot)
{
	ArtifactLocation al(hero, slot);
	const CArtifactInstance *a = al.getArt();

	if(!a)
		COMPLAIN_RET("Cannot find artifact to sacrifice!");


	int dmp, expToGive;
	m->getOffer(hero->getArtTypeId(slot), 0, dmp, expToGive, EMarketMode::ARTIFACT_EXP);

	removeArtifact(al);
	changePrimSkill(hero->id, 4, expToGive);
	return true;
}

void CGameHandler::makeStackDoNothing(const CStack * next)
{
	BattleAction doNothing;
	doNothing.actionType = 0;
	doNothing.additionalInfo = 0;
	doNothing.destinationTile = -1;
	doNothing.side = !next->attackerOwned;
	doNothing.stackNumber = next->ID;

	StartAction start_action(doNothing);
	sendAndApply(&start_action);
	sendAndApply(&end_action);
}

bool CGameHandler::insertNewStack(const StackLocation &sl, const CCreature *c, TQuantity count)
{
	if(sl.army->hasStackAtSlot(sl.slot))
		COMPLAIN_RET("Slot is already taken!");

	InsertNewStack ins;
	ins.sl = sl;
	ins.stack = CStackBasicDescriptor(c, count);
	sendAndApply(&ins);
	return true;
}

bool CGameHandler::eraseStack(const StackLocation &sl, bool forceRemoval/* = false*/)
{
	if(!sl.army->hasStackAtSlot(sl.slot))
		COMPLAIN_RET("Cannot find a stack to erase");

	if(sl.army->Slots().size() == 1 //from the last stack
		&& sl.army->needsLastStack() //that must be left
		&& !forceRemoval) //ignore above conditions if we are forcing removal
	{
		COMPLAIN_RET("Cannot erase the last stack!");
	}

	EraseStack es;
	es.sl = sl;
	sendAndApply(&es);
	return true;
}

bool CGameHandler::changeStackCount(const StackLocation &sl, TQuantity count, bool absoluteValue /*= false*/)
{
	TQuantity currentCount = sl.army->getStackCount(sl.slot);
	if((absoluteValue && count < 0)
		|| (!absoluteValue && -count > currentCount))
	{
		COMPLAIN_RET("Cannot take more stacks than present!");
	}

	if((currentCount == -count  &&  !absoluteValue)
	   || (!count && absoluteValue))
	{
		eraseStack(sl);
	}
	else
	{
		ChangeStackCount csc;
		csc.sl = sl;
		csc.count = count;
		csc.absoluteValue = absoluteValue;
		sendAndApply(&csc);
	}
	return true;
}

bool CGameHandler::addToSlot(const StackLocation &sl, const CCreature *c, TQuantity count)
{
	const CCreature *slotC = sl.army->getCreature(sl.slot);
	if(!slotC) //slot is empty
		insertNewStack(sl, c, count);
	else if(c == slotC)
		changeStackCount(sl, count);
	else
	{
		COMPLAIN_RET("Cannot add " + c->namePl + " to slot " + boost::lexical_cast<std::string>(sl.slot) + "!");
	}
	return true;
}

void CGameHandler::tryJoiningArmy(const CArmedInstance *src, const CArmedInstance *dst, bool removeObjWhenFinished, bool allowMerging)
{
	if(!src->canBeMergedWith(*dst, allowMerging))
	{
		if (allowMerging) //do that, add all matching creatures.
		{
			bool cont = true;
			while (cont)
			{
				for(TSlots::const_iterator i = src->stacks.begin(); i != src->stacks.end(); i++)//while there are unmoved creatures
				{
					TSlot pos = dst->getSlotFor(i->second->type);
					if(pos > -1)
					{
						moveStack(StackLocation(src, i->first), StackLocation(dst, pos));
						cont = true;
						break; //or iterator crashes
					}
					cont = false;
				}
			}
		}
		boost::function<void()> removeOrNot = 0;
		if(removeObjWhenFinished)
			removeOrNot = boost::bind(&IGameCallback::removeObject,this,src->id);
		showGarrisonDialog(src->id, dst->id, true, removeOrNot); //show garrison window and optionally remove ourselves from map when player ends
	}
	else //merge
	{
		moveArmy(src, dst, allowMerging);
		if(removeObjWhenFinished)
			removeObject(src->id);
	}
}

bool CGameHandler::moveStack(const StackLocation &src, const StackLocation &dst, TQuantity count)
{
	if(!src.army->hasStackAtSlot(src.slot))
		COMPLAIN_RET("No stack to move!");

	if(dst.army->hasStackAtSlot(dst.slot) && dst.army->getCreature(dst.slot) != src.army->getCreature(src.slot))
		COMPLAIN_RET("Cannot move: stack of different type at destination pos!");

	if(count == -1)
	{
		count = src.army->getStackCount(src.slot);
	}

	if(src.army != dst.army  //moving away
		&&  count == src.army->getStackCount(src.slot) //all creatures
		&& src.army->Slots().size() == 1 //from the last stack
		&& src.army->needsLastStack()) //that must be left
	{
		COMPLAIN_RET("Cannot move away the alst creature!");
	}

	RebalanceStacks rs;
	rs.src = src;
	rs.dst = dst;
	rs.count = count;
	sendAndApply(&rs);
	return true;
}

bool CGameHandler::swapStacks(const StackLocation &sl1, const StackLocation &sl2)
{

	if(!sl1.army->hasStackAtSlot(sl1.slot))
		return moveStack(sl2, sl1);
	else if(!sl2.army->hasStackAtSlot(sl2.slot))
		return moveStack(sl1, sl2);
	else
	{
		SwapStacks ss;
		ss.sl1 = sl1;
		ss.sl2 = sl2;
		sendAndApply(&ss);
		return true;
	}
}

void CGameHandler::runBattle()
{
	assert(gs->curB);
	//TODO: pre-tactic stuff, call scripts etc.

	//tactic round
	{
		while(gs->curB->tacticDistance && !battleResult.get())
			boost::this_thread::sleep(boost::posix_time::milliseconds(50));
	}

	//spells opening battle
	for(int i=0; i<ARRAY_COUNT(gs->curB->heroes); ++i)
	{
		if(gs->curB->heroes[i] && gs->curB->heroes[i]->hasBonusOfType(Bonus::OPENING_BATTLE_SPELL))
		{
			TBonusListPtr bl = gs->curB->heroes[i]->getBonuses(Selector::type(Bonus::OPENING_BATTLE_SPELL));
			BOOST_FOREACH (Bonus *b, *bl)
			{
				handleSpellCasting(b->subtype, 3, -1, 0, gs->curB->heroes[i]->tempOwner, NULL, gs->curB->heroes[1-i], b->val, ECastingMode::HERO_CASTING, NULL);
			}
		}
	}

	//main loop
	while(!battleResult.get()) //till the end of the battle ;]
	{
		NEW_ROUND;
		auto obstacles = gs->curB->obstacles; //we copy container, because we're going to modify it
		BOOST_FOREACH(auto &obstPtr, obstacles)
		{
			if(const SpellCreatedObstacle *sco = dynamic_cast<const SpellCreatedObstacle *>(obstPtr.get()))
				if(sco->turnsRemaining == 0)
					removeObstacle(*obstPtr);
		}

		std::vector<CStack*> & stacks = (gs->curB->stacks);
		const BattleInfo & curB = *gs->curB;

		//stack loop
		const CStack *next;
		while(!battleResult.get() && (next = curB.getNextStack()) && next->willMove())
		{

			//check for bad morale => freeze
			int nextStackMorale = next->MoraleVal();
			if( nextStackMorale < 0 &&
				!(NBonus::hasOfType(gs->curB->heroes[0], Bonus::BLOCK_MORALE) || NBonus::hasOfType(gs->curB->heroes[1], Bonus::BLOCK_MORALE)) //checking if gs->curB->heroes have (or don't have) morale blocking bonuses)
				)
			{
				if( rand()%24   <   -2 * nextStackMorale)
				{
					//unit loses its turn - empty freeze action
					BattleAction ba;
					ba.actionType = BattleAction::BAD_MORALE;
					ba.additionalInfo = 1;
					ba.side = !next->attackerOwned;
					ba.stackNumber = next->ID;

					StartAction start_action(ba);
					sendAndApply(&start_action);
					sendAndApply(&end_action);
					checkForBattleEnd(stacks); //check if this "action" ended the battle (not likely but who knows...)
					continue;
				}
			}

			if(next->hasBonusOfType(Bonus::ATTACKS_NEAREST_CREATURE)) //while in berserk
			{
				std::pair<const CStack *, int> attackInfo = curB.getNearestStack(next, boost::logic::indeterminate);
				if(attackInfo.first != NULL)
				{
					BattleAction attack;
					attack.actionType = BattleAction::WALK_AND_ATTACK;
					attack.side = !next->attackerOwned;
					attack.stackNumber = next->ID;

					attack.additionalInfo = attackInfo.first->position;
					attack.destinationTile = attackInfo.second;

					makeBattleAction(attack);

					checkForBattleEnd(stacks);
				}
				else
				{
					makeStackDoNothing(next);
				}
				continue;
			}

			const CGHeroInstance * curOwner = gs->curB->battleGetOwner(next);

			if( (next->position < 0 || next->getCreature()->idNumber == 146)	//arrow turret or ballista
				&& (!curOwner || curOwner->getSecSkillLevel(CGHeroInstance::ARTILLERY) == 0)) //hero has no artillery
			{
				BattleAction attack;
				attack.actionType = BattleAction::SHOOT;
				attack.side = !next->attackerOwned;
				attack.stackNumber = next->ID;

				for(int g=0; g<gs->curB->stacks.size(); ++g)
				{
					if(gs->curB->stacks[g]->owner != next->owner && gs->curB->stacks[g]->isValidTarget())
					{
						attack.destinationTile = gs->curB->stacks[g]->position;
						break;
					}
				}

				makeBattleAction(attack);

				checkForBattleEnd(stacks);
				continue;
			}

			if(next->getCreature()->idNumber == 145 && (!curOwner || curOwner->getSecSkillLevel(CGHeroInstance::BALLISTICS) == 0)) //catapult, hero has no ballistics
			{
				BattleAction attack;
				static const int wallHexes[] = {50, 183, 182, 130, 62, 29, 12, 95};

				attack.destinationTile = wallHexes[ rand()%ARRAY_COUNT(wallHexes) ];
				attack.actionType = BattleAction::CATAPULT;
				attack.additionalInfo = 0;
				attack.side = !next->attackerOwned;
				attack.stackNumber = next->ID;

				makeBattleAction(attack);
				continue;
			}


			if(next->getCreature()->idNumber == 147) //first aid tent
			{
				std::vector< const CStack * > possibleStacks;

				//is there any clean algorithm for that? (boost.range seems to lack copy_if)
				BOOST_FOREACH(const CStack *s, battleGetAllStacks())
					if(s->owner == next->owner  &&  s->canBeHealed())
						possibleStacks.push_back(s);

				if(!possibleStacks.size())
				{
					makeStackDoNothing(next);
					continue;
				}

				if(!curOwner || curOwner->getSecSkillLevel(CGHeroInstance::FIRST_AID) == 0) //no hero or hero has no first aid
				{
					range::random_shuffle(possibleStacks);
					const CStack * toBeHealed = possibleStacks.front();

					BattleAction heal;
					heal.actionType = BattleAction::STACK_HEAL;
					heal.additionalInfo = 0;
					heal.destinationTile = toBeHealed->position;
					heal.side = !next->attackerOwned;
					heal.stackNumber = next->ID;
					makeBattleAction(heal);

					continue;
				}
			}

			int numberOfAsks = 1;
			bool breakOuter = false;
			do
			{//ask interface and wait for answer
				if(!battleResult.get())
				{
					stackTurnTrigger(next); //various effects

					if (vstd::contains(next->state, EBattleStackState::FEAR))
					{
						makeStackDoNothing(next); //end immediately if stack was affected by fear
					}
					else
					{
						tlog5 << "Activating " << next->nodeName() << std::endl;
						BattleSetActiveStack sas;
						sas.stack = next->ID;
						sendAndApply(&sas);
						boost::unique_lock<boost::mutex> lock(battleMadeAction.mx);
						battleMadeAction.data = false;
						while (next->alive() &&
							(!battleMadeAction.data  &&  !battleResult.get())) //active stack hasn't made its action and battle is still going
							battleMadeAction.cond.wait(lock);
					}
				}

				if(battleResult.get()) //don't touch it, battle could be finished while waiting got action
				{
					breakOuter = true;
					break;
				}

				//we're after action, all results applied
				checkForBattleEnd(stacks); //check if this action ended the battle

				//check for good morale
				nextStackMorale = next->MoraleVal();
				if(!vstd::contains(next->state,EBattleStackState::HAD_MORALE)  //only one extra move per turn possible
					&& !vstd::contains(next->state,EBattleStackState::DEFENDING)
					&& !vstd::contains(next->state,EBattleStackState::WAITING)
					&& !vstd::contains(next->state, EBattleStackState::FEAR)
					&&  next->alive()
					&&  nextStackMorale > 0
					&& !(NBonus::hasOfType(gs->curB->heroes[0], Bonus::BLOCK_MORALE) || NBonus::hasOfType(gs->curB->heroes[1], Bonus::BLOCK_MORALE)) //checking if gs->curB->heroes have (or don't have) morale blocking bonuses
					)
				{
					if(rand()%24 < nextStackMorale) //this stack hasn't got morale this turn

						{
							BattleTriggerEffect bte;
							bte.stackID = next->ID;
							bte.effect = Bonus::MORALE;
							bte.val = 1;
							bte.additionalInfo = 0;
							sendAndApply(&bte); //play animation

							++numberOfAsks; //move this stack once more
						}
				}

				--numberOfAsks;
			} while (numberOfAsks > 0);

			if (breakOuter)
			{
				break;
			}

		}
	}

	endBattle(gs->curB->tile, gs->curB->heroes[0], gs->curB->heroes[1]);
}

void CGameHandler::giveHeroArtifact(const CGHeroInstance *h, const CArtifactInstance *a, int pos)
{
	assert(a->artType);
	ArtifactLocation al;
	al.artHolder = const_cast<CGHeroInstance*>(h);

	int slot = -1;
	if(pos < 0)
	{
		if(pos == -2)
			slot = a->firstAvailableSlot(h);
		else
			slot = a->firstBackpackSlot(h);
	}
	else
	{
		slot = pos;
	}

	al.slot = slot;

	if(slot < 0 || !a->canBePutAt(al))
	{
		complain("Cannot put artifact in that slot!");
		return;
	}

	putArtifact(al, a);
}
void CGameHandler::putArtifact(const ArtifactLocation &al, const CArtifactInstance *a)
{
	PutArtifact pa;
	pa.art = a;
	pa.al = al;
	sendAndApply(&pa);
}

void CGameHandler::giveHeroNewArtifact(const CGHeroInstance *h, const CArtifact *artType, int pos)
{
	CArtifactInstance *a = NULL;
	if(!artType->constituents)
	{
		a = new CArtifactInstance();
	}
	else
	{
		a = new CCombinedArtifactInstance();
	}
	a->artType = artType; //*NOT* via settype -> all bonus-related stuff must be done by NewArtifact apply

	NewArtifact na;
	na.art = a;
	sendAndApply(&na); // -> updates a!!!, will create a on other machines

	giveHeroArtifact(h, a, pos);
}

void CGameHandler::setBattleResult(int resultType, int victoriusSide)
{
	if(battleResult.get())
	{
		complain("There is already set result?");
		return;
	}
	BattleResult *br = new BattleResult;
	br->result = resultType;
	br->winner = victoriusSide; //surrendering side loses
	gs->curB->calculateCasualties(br->casualties);
	battleResult.set(br);
}

void CGameHandler::commitPackage( CPackForClient *pack )
{
	sendAndApply(pack);
}

void CGameHandler::spawnWanderingMonsters(int creatureID)
{
	std::vector<int3>::iterator tile;
	std::vector<int3> tiles;
	getFreeTiles(tiles);
	ui32 amount = tiles.size() / 200; //Chance is 0.5% for each tile
	std::random_shuffle(tiles.begin(), tiles.end());
	tlog5 << "Spawning wandering monsters. Found " << tiles.size() << " free tiles. Creature type: " << creatureID << std::endl;
	const CCreature *cre = VLC->creh->creatures[creatureID];
	for (int i = 0; i < amount; ++i)
	{
		tile = tiles.begin();
		tlog5 << "\tSpawning monster at " << *tile << std::endl;
		putNewMonster(creatureID, cre->getRandomAmount(std::rand), *tile);
		tiles.erase(tile); //not use it again
	}
}

bool CGameHandler::isBlockedByQueries(const CPack *pack, int packType, ui8 player)
{
	//it's always legal to send query reply (we'll check later if it makes sense)
	if(packType == typeList.getTypeID<QueryReply>())
		return false;

	if(packType == typeList.getTypeID<ArrangeStacks>() && isAllowedArrangePack((const ArrangeStacks*)pack))
		return false;

	//if there are no queries, nothing is blocking
	if(states.getQueriesCount(player) == 0)
		return false;

	return true; //block package
}

void CGameHandler::removeObstacle(const CObstacleInstance &obstacle)
{
	ObstaclesRemoved obsRem;
	obsRem.obstacles.insert(obstacle.uniqueID);
	sendAndApply(&obsRem);
}

CasualtiesAfterBattle::CasualtiesAfterBattle(const CArmedInstance *army, BattleInfo *bat)
{
	heroWithDeadCommander = -1;

	int color = army->tempOwner;
	if(color == 254)
		color = GameConstants::NEUTRAL_PLAYER;

	BOOST_FOREACH(CStack *st, bat->stacks)
	{
		if(vstd::contains(st->state, EBattleStackState::SUMMONED)) //don't take into account summoned stacks
			continue;

		if(st->owner==color && !army->slotEmpty(st->slot) && st->count < army->getStackCount(st->slot))
		{
			StackLocation sl(army, st->slot);
			if(st->alive())
				newStackCounts.push_back(std::pair<StackLocation, int>(sl, st->count));
			else
				newStackCounts.push_back(std::pair<StackLocation, int>(sl, 0));
		}
		if (st->base && !st->count)
		{
			auto c = dynamic_cast <const CCommanderInstance *>(st->base);
			if (c) //switch commander status to dead
			{
				auto h = dynamic_cast <const CGHeroInstance *>(army);
				if (h && h->commander == c)
					heroWithDeadCommander = army->id; //TODO: unify commander handling
			}
		}
	}
}

void CasualtiesAfterBattle::takeFromArmy(CGameHandler *gh)
{
	BOOST_FOREACH(TStackAndItsNewCount &ncount, newStackCounts)
	{
		if(ncount.second > 0)
			gh->changeStackCount(ncount.first, ncount.second, true);
		else
			gh->eraseStack(ncount.first, true);
	}
	if (heroWithDeadCommander > -1)
	{
		SetCommanderProperty scp;
		scp.heroid = heroWithDeadCommander;
		scp.which = SetCommanderProperty::ALIVE;
		scp.amount = 0;
		gh->sendAndApply (&scp);
	}
}
