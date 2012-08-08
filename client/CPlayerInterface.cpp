#include "StdInc.h"

#include "CAdvmapInterface.h"
#include "BattleInterface/CBattleInterface.h"
#include "BattleInterface/CBattleInterfaceClasses.h"
#include "../CCallback.h"
#include "CCastleInterface.h"
#include "UIFramework/CCursorHandler.h"
#include "CKingdomInterface.h"
#include "CGameInfo.h"
#include "CHeroWindow.h"
#include "CCreatureWindow.h"
#include "CQuestLog.h"
#include "CMessage.h"
#include "CPlayerInterface.h"
//#include "UIFramework/SDL_Extensions.h"
#include "UIFramework/SDL_Extensions.h"
#include "CConfigHandler.h"
#include "BattleInterface/CCreatureAnimation.h"
#include "Graphics.h"
#include "../lib/CArtHandler.h"
#include "../lib/CGeneralTextHandler.h"
#include "../lib/CHeroHandler.h"
#include "../lib/CLodHandler.h"
#include "../lib/CObjectHandler.h"
#include "../lib/Connection.h"
#include "../lib/CSpellHandler.h"
#include "../lib/CTownHandler.h"
#include "../lib/BattleState.h"
#include "../lib/JsonNode.h"
#include "CMusicHandler.h"
#include "../lib/CondSh.h"
#include "../lib/NetPacks.h"
#include "../lib/map.h"
#include "../lib/VCMIDirs.h"
#include "mapHandler.h"
#include "../lib/CStopWatch.h"
#include "../lib/StartInfo.h"
#include "../lib/CGameState.h"
#include "../lib/GameConstants.h"
#include "UIFramework/CGuiHandler.h"
#include "../lib/UnlockGuard.h"

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

/*
 * CPlayerInterface.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */


// The macro below is used to mark functions that are called by client when game state changes.
// They all assume that CPlayerInterface::pim mutex is locked.
#define EVENT_HANDLER_CALLED_BY_CLIENT

// The macro marks functions that are run on a new thread by client.
// They do not own any mutexes intiially.
#define THREAD_CREATED_BY_CLIENT

using namespace boost::assign;
using namespace CSDL_Ext;

void processCommand(const std::string &message, CClient *&client);

extern std::queue<SDL_Event*> events;
extern boost::mutex eventsM;
boost::recursive_mutex * CPlayerInterface::pim = new boost::recursive_mutex;

CPlayerInterface * LOCPLINT;

CBattleInterface * CPlayerInterface::battleInt;

enum  EMoveState {STOP_MOVE, WAITING_MOVE, CONTINUE_MOVE, DURING_MOVE};
CondSh<EMoveState> stillMoveHero; //used during hero movement

int CPlayerInterface::howManyPeople = 0;


struct OCM_HLP_CGIN
{
	bool inline operator ()(const std::pair<const CGObjectInstance*,SDL_Rect>  & a, const std::pair<const CGObjectInstance*,SDL_Rect> & b) const
	{
		return (*a.first)<(*b.first);
	}
} ocmptwo_cgin ;



CPlayerInterface::CPlayerInterface(int Player)
{
	observerInDuelMode = false;
	howManyPeople++;
	GH.defActionsDef = 0;
	LOCPLINT = this;
	curAction = NULL;
	playerID=Player;
	human=true;
	castleInt = NULL;
	battleInt = NULL;
	//pim = new boost::recursive_mutex;
	makingTurn = false;
	showingDialog = new CondSh<bool>(false);
	cingconsole = new CInGameConsole;
	terminate_cond.set(false);
	firstCall = 1; //if loading will be overwritten in serialize
	autosaveCount = 0;
}
CPlayerInterface::~CPlayerInterface()
{
	howManyPeople--;
	//delete pim;
	//vstd::clear_pointer(pim);
	delete showingDialog;
	if(adventureInt)
	{
		if(adventureInt->active & CIntObject::KEYBOARD)
			adventureInt->deactivateKeys();
		delete adventureInt;
		adventureInt = NULL;
	}

	delete cingconsole;

	LOCPLINT = NULL;
}
void CPlayerInterface::init(CCallback * CB)
{
	cb = dynamic_cast<CCallback*>(CB);
	if(observerInDuelMode)
		return;

	if(!towns.size() && !wanderingHeroes.size())
		initializeHeroTownList();

	if(!adventureInt)
		adventureInt = new CAdvMapInt();
}
void CPlayerInterface::yourTurn()
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	{
		boost::unique_lock<boost::mutex> lock(eventsM); //block handling events until interface is ready

		LOCPLINT = this;
		GH.curInt = this;
		adventureInt->selection = NULL;

		if(firstCall)
		{
			if(howManyPeople == 1)
				adventureInt->setPlayer(playerID);

			autosaveCount = getLastIndex("Autosave_");

			if(firstCall > 0) //new game, not loaded
			{
				int index = getLastIndex("Newgame_Autosave_");
				index %= SAVES_COUNT;
				cb->save("Newgame_Autosave_" + boost::lexical_cast<std::string>(index + 1));
			}
			firstCall = 0;
		}
		else
		{
			LOCPLINT->cb->save("Autosave_" + boost::lexical_cast<std::string>(autosaveCount++ + 1));
			autosaveCount %= 5;
		}

		if(adventureInt->player != playerID)
			adventureInt->setPlayer(playerID);

		if(howManyPeople > 1) //hot seat message
		{
			adventureInt->startHotSeatWait(playerID);

			makingTurn = true;
			std::string msg = CGI->generaltexth->allTexts[13];
			boost::replace_first(msg, "%s", cb->getStartInfo()->playerInfos.find(playerID)->second.name);
			std::vector<CComponent*> cmp;
			cmp.push_back(new CComponent(CComponent::flag, playerID, 0));
			showInfoDialog(msg, cmp);
		}
		else
		{
			makingTurn = true;
			adventureInt->startTurn();
		}
	}

	acceptTurn();
}

STRONG_INLINE void subRect(const int & x, const int & y, const int & z, const SDL_Rect & r, const int & hid)
{
	TerrainTile2 & hlp = CGI->mh->ttiles[x][y][z];
	for(int h=0; h<hlp.objects.size(); ++h)
		if(hlp.objects[h].first->id==hid)
		{
			hlp.objects[h].second = r;
			return;
		}
}

STRONG_INLINE void delObjRect(const int & x, const int & y, const int & z, const int & hid)
{
	TerrainTile2 & hlp = CGI->mh->ttiles[x][y][z];
	for(int h=0; h<hlp.objects.size(); ++h)
		if(hlp.objects[h].first->id==hid)
		{
			hlp.objects.erase(hlp.objects.begin()+h);
			return;
		}
}
void CPlayerInterface::heroMoved(const TryMoveHero & details)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();
	if(LOCPLINT != this)
		return;

	const CGHeroInstance * ho = cb->getHero(details.id); //object representing this hero
	int3 hp = details.start;

	if(!ho)
	{
		//AI hero left the visible area (we can't obtain info)
		//TODO very evil workaround -> retreive pointer to hero so we could animate it
		// TODO -> we should not need full CGHeroInstance structure to display animation or it should not be handled by playerint (but by the client itself)
		const TerrainTile2 &tile = CGI->mh->ttiles[hp.x-1][hp.y][hp.z];
		for(int i = 0; i < tile.objects.size(); i++)
			if(tile.objects[i].first->id == details.id)
				ho = dynamic_cast<const CGHeroInstance *>(tile.objects[i].first);

		if(!ho) //still nothing...
			return;
	}

	adventureInt->centerOn(ho); //actualizing screen pos
	adventureInt->minimap.redraw();
	adventureInt->heroList.redraw();

	bool directlyAttackingCreature =
		CGI->mh->map->isInTheMap(details.attackedFrom)
		&& adventureInt->terrain.currentPath					//in case if movement has been canceled in the meantime and path was already erased
		&& adventureInt->terrain.currentPath->nodes.size() == 3;//FIXME should be 2 but works nevertheless...

	if(makingTurn  &&  ho->tempOwner == playerID) //we are moving our hero - we may need to update assigned path
	{
		//We may need to change music - select new track, music handler will change it if needed
		CCS->musich->playMusic(CCS->musich->terrainMusics[LOCPLINT->cb->getTile(ho->visitablePos())->tertype], -1);

		if(details.result == TryMoveHero::TELEPORTATION)
		{
			if(adventureInt->terrain.currentPath)
			{ 
				assert(adventureInt->terrain.currentPath->nodes.size() >= 2);
				std::vector<CGPathNode>::const_iterator nodesIt = adventureInt->terrain.currentPath->nodes.end() - 1;

				if((nodesIt)->coord == CGHeroInstance::convertPosition(details.start, false)
					&& (nodesIt-1)->coord == CGHeroInstance::convertPosition(details.end, false))
				{
					//path was between entrance and exit of teleport -> OK, erase node as usual
					removeLastNodeFromPath(ho);
				}
				else
				{
					//teleport was not along current path, it'll now be invalid (hero is somewhere else)
					eraseCurrentPathOf(ho);

				}
			}
			adventureInt->heroList.update(ho);
			return;	//teleport - no fancy moving animation
					//TODO: smooth disappear / appear effect
		}

		if (ho->pos != details.end //hero didn't change tile but visit succeeded
			|| directlyAttackingCreature) // or creature was attacked from endangering tile.
		{
			eraseCurrentPathOf(ho);
		}
		else if(adventureInt->terrain.currentPath  &&  ho->pos == details.end) //&& hero is moving
		{
			if(details.start != details.end) //so we don't touch path when revisiting with spacebar
				removeLastNodeFromPath(ho);
		}
	}

	if (details.result != TryMoveHero::SUCCESS) //hero failed to move
	{
		ho->isStanding = true;
		stillMoveHero.setn(STOP_MOVE);
		GH.totalRedraw();
		adventureInt->heroList.update(ho);
		return;
	}

	initMovement(details, ho, hp);

	//first initializing done
	GH.mainFPSmng->framerateDelay(); // after first move

	ui32 speed = settings["adventure"]["heroSpeed"].Float();
	//main moving
	for(int i=1; i<32; i+=2*speed)
	{
		movementPxStep(details, i, hp, ho);
		adventureInt->updateScreen = true;
		adventureInt->show(screen);
		CSDL_Ext::update(screen);
		GH.mainFPSmng->framerateDelay(); //for animation purposes
	} //for(int i=1; i<32; i+=4)
	//main moving done

	//finishing move
	finishMovement(details, hp, ho);
	ho->isStanding = true;

	//move finished
	adventureInt->minimap.redraw();
	adventureInt->heroList.update(ho);

	//check if user cancelled movement
	{
		boost::unique_lock<boost::mutex> un(eventsM);
		while(events.size())
		{
			SDL_Event *ev = events.front();
			events.pop();
			switch(ev->type)
			{
			case SDL_MOUSEBUTTONDOWN:
				stillMoveHero.setn(STOP_MOVE);
				break;
			case SDL_KEYDOWN:
				if(ev->key.keysym.sym < SDLK_F1  ||  ev->key.keysym.sym > SDLK_F15)
					stillMoveHero.setn(STOP_MOVE);
				break;
			}
			delete ev;
		}
	}

	if(stillMoveHero.get() == WAITING_MOVE)
		stillMoveHero.setn(DURING_MOVE);

	// Hero attacked creature directly, set direction to face it.
	if (directlyAttackingCreature) {
		// Get direction to attacker.
		int3 posOffset = details.attackedFrom - details.end + int3(2, 1, 0);
		static const ui8 dirLookup[3][3] = {
			{ 1, 2, 3 },
			{ 8, 0, 4 },
			{ 7, 6, 5 }
		};
		// FIXME: Avoid const_cast, make moveDir mutable in some other way?
		const_cast<CGHeroInstance *>(ho)->moveDir = dirLookup[posOffset.y][posOffset.x];
	}
}
void CPlayerInterface::heroKilled(const CGHeroInstance* hero)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	const CArmedInstance *newSelection = nullptr;
	if (makingTurn)
	{
		//find new object for selection: either hero
		int next = adventureInt->getNextHeroIndex(vstd::find_pos(wanderingHeroes, hero));
		if (next >= 0)
			newSelection = wanderingHeroes[next];

		//or town
		if (!newSelection || newSelection == hero)
		{
			if (towns.empty())
				newSelection = nullptr;
			else
				newSelection = towns.front();
		}
	}

	wanderingHeroes -= hero;
	if(vstd::contains(paths, hero))
		paths.erase(hero);

	adventureInt->heroList.update(hero);
	if (makingTurn)
		adventureInt->select(newSelection, true);
	else
		adventureInt->selection = nullptr;
}

void CPlayerInterface::heroCreated(const CGHeroInstance * hero)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	wanderingHeroes.push_back(hero);
	adventureInt->heroList.update(hero);
}
void CPlayerInterface::openTownWindow(const CGTownInstance * town)
{
	if (castleInt)
		castleInt->close();
	castleInt = new CCastleInterface(town);
	GH.pushInt(castleInt);
}

int3 CPlayerInterface::repairScreenPos(int3 pos)
{
	if(pos.x<-CGI->mh->frameW)
		pos.x = -CGI->mh->frameW;
	if(pos.y<-CGI->mh->frameH)
		pos.y = -CGI->mh->frameH;
	if(pos.x>CGI->mh->sizes.x - adventureInt->terrain.tilesw + CGI->mh->frameW)
		pos.x = CGI->mh->sizes.x - adventureInt->terrain.tilesw + CGI->mh->frameW;
	if(pos.y>CGI->mh->sizes.y - adventureInt->terrain.tilesh + CGI->mh->frameH)
		pos.y = CGI->mh->sizes.y - adventureInt->terrain.tilesh + CGI->mh->frameH;
	return pos;
}
void CPlayerInterface::heroPrimarySkillChanged(const CGHeroInstance * hero, int which, si64 val)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(which == 4)
	{
		if(CAltarWindow *ctw = dynamic_cast<CAltarWindow *>(GH.topInt()))
			ctw->setExpToLevel();
	}
	else if(which < GameConstants::PRIMARY_SKILLS) //no need to redraw infowin if this is experience (exp is treated as prim skill with id==4)
		updateInfo(hero);
}

void CPlayerInterface::heroSecondarySkillChanged(const CGHeroInstance * hero, int which, int val)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	CUniversityWindow* cuw = dynamic_cast<CUniversityWindow*>(GH.topInt());
	if(cuw) //university window is open
	{
		GH.totalRedraw();
	}
}

void CPlayerInterface::heroManaPointsChanged(const CGHeroInstance * hero)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	updateInfo(hero);
	if(makingTurn && hero->tempOwner == playerID)
		adventureInt->heroList.update(hero);
}
void CPlayerInterface::heroMovePointsChanged(const CGHeroInstance * hero)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(makingTurn && hero->tempOwner == playerID)
		adventureInt->heroList.update(hero);
}
void CPlayerInterface::receivedResource(int type, int val)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(CMarketplaceWindow *mw = dynamic_cast<CMarketplaceWindow *>(GH.topInt()))
		mw->resourceChanged(type, val);

	GH.totalRedraw();
}

void CPlayerInterface::heroGotLevel(const CGHeroInstance *hero, int pskill, std::vector<ui16>& skills, int queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();
	CCS->soundh->playSound(soundBase::heroNewLevel);

	CLevelWindow *lw = new CLevelWindow(hero,pskill,skills,
										[=](ui32 selection){ cb->selectionMade(selection, queryID); });
	GH.pushInt(lw);
}
void CPlayerInterface::commanderGotLevel (const CCommanderInstance * commander, std::vector<ui32> skills, int queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();
	CCS->soundh->playSound(soundBase::heroNewLevel);

	CCreatureWindow * cw = new CCreatureWindow(skills, commander, 
												[=](ui32 selection){ cb->selectionMade(selection, queryID); });
	GH.pushInt(cw);
}
void CPlayerInterface::heroInGarrisonChange(const CGTownInstance *town)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	updateInfo(town);

	if(town->garrisonHero && vstd::contains(wanderingHeroes,town->garrisonHero)) //wandering hero moved to the garrison
	{
		CGI->mh->hideObject(town->garrisonHero);
		wanderingHeroes -= town->garrisonHero;
	}

	if(town->visitingHero && !vstd::contains(wanderingHeroes,town->visitingHero)) //hero leaves garrison
	{
		CGI->mh->printObject(town->visitingHero);
		wanderingHeroes.push_back(town->visitingHero);
	}
	adventureInt->heroList.update();
	adventureInt->updateNextHero(NULL);

	if(CCastleInterface *c = castleInt)
	{
		c->garr->highlighted = NULL;
		c->garr->setArmy(town->getUpperArmy(), 0);
		c->garr->setArmy(town->visitingHero, 1);
		c->garr->recreateSlots();
		c->heroes->update();
	}
	BOOST_FOREACH(IShowActivatable *isa, GH.listInt)
	{
		CKingdomInterface *ki = dynamic_cast<CKingdomInterface*>(isa);
		if (ki)
		{
			ki->townChanged(town);
			ki->updateGarrisons();
		}
	}
	GH.totalRedraw();
}
void CPlayerInterface::heroVisitsTown(const CGHeroInstance* hero, const CGTownInstance * town)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(hero->tempOwner != playerID )
		return;

	waitWhileDialog();
	openTownWindow(town);
}
void CPlayerInterface::garrisonChanged( const CGObjectInstance * obj)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	updateInfo(obj);

	for(std::list<IShowActivatable*>::iterator i = GH.listInt.begin(); i != GH.listInt.end(); i++)
	{
		CGarrisonHolder *cgh = dynamic_cast<CGarrisonHolder*>(*i);
		if (cgh)
			cgh->updateGarrisons();

		if(CTradeWindow *cmw = dynamic_cast<CTradeWindow*>(*i))
		{
			if(obj == cmw->hero)
				cmw->garrisonChanged();
		}
	}

	GH.totalRedraw();
}

void CPlayerInterface::buildChanged(const CGTownInstance *town, int buildingID, int what) //what: 1 - built, 2 - demolished
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	switch (buildingID)
	{
	case 7: case 8: case 9: case 10: case 11: case 12: case 13: case 15:
		updateInfo(town);
		break;
	}

	if(!castleInt)
		return;
	if(castleInt->town!=town)
		return;
	switch(what)
	{
	case 1:
		CCS->soundh->playSound(soundBase::newBuilding);
		castleInt->addBuilding(buildingID);
		break;
	case 2:
		castleInt->removeBuilding(buildingID);
		break;
	}
	adventureInt->townList.update(town);
	castleInt->townlist->update(town);
}

void CPlayerInterface::battleStart(const CCreatureSet *army1, const CCreatureSet *army2, int3 tile, const CGHeroInstance *hero1, const CGHeroInstance *hero2, bool side)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	waitForAllDialogs();
	GH.pushInt(battleInt);
}


void CPlayerInterface::battleStacksHealedRes(const std::vector<std::pair<ui32, ui32> > & healedStacks, bool lifeDrain, bool tentHeal, si32 lifeDrainFrom)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	for(int b=0; b<healedStacks.size(); ++b)
	{
		const CStack * healed = cb->battleGetStackByID(healedStacks[b].first);
		if(battleInt->creAnims[healed->ID]->getType() == CCreatureAnim::DEATH)
		{
			//stack has been resurrected
			battleInt->creAnims[healed->ID]->setType(CCreatureAnim::HOLDING);
		}
	}

	if (lifeDrain)
	{
		const CStack *attacker = cb->battleGetStackByID(healedStacks[0].first, false);
		const CStack *defender = cb->battleGetStackByID(lifeDrainFrom, false);
		int textOff = 0;

		if (attacker)
		{
			battleInt->displayEffect(52, attacker->position); //TODO: transparency
			if (attacker->count > 1)
			{
				textOff += 1;
			}
			CCS->soundh->playSound(soundBase::DRAINLIF);
		}

		//print info about life drain
		char textBuf[1000];
		sprintf(textBuf, CGI->generaltexth->allTexts[361 + textOff].c_str(), attacker->getCreature()->nameSing.c_str(),
			healedStacks[0].second, defender->getCreature()->namePl.c_str());
		battleInt->console->addText(textBuf);
	}
	if (tentHeal)
	{
		std::string text = CGI->generaltexth->allTexts[414];
		boost::algorithm::replace_first(text, "%s", cb->battleGetStackByID(lifeDrainFrom, false)->getCreature()->nameSing);
		boost::algorithm::replace_first(text, "%s",	cb->battleGetStackByID(healedStacks[0].first, false)->getCreature()->nameSing);
		boost::algorithm::replace_first(text, "%d", boost::lexical_cast<std::string>(healedStacks[0].second));
		battleInt->console->addText(text);
	}
}

void CPlayerInterface::battleNewStackAppeared(const CStack * stack)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	battleInt->newStack(stack);
}

void CPlayerInterface::battleObstaclesRemoved(const std::set<si32> & removedObstacles)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

// 	for(std::set<si32>::const_iterator it = removedObstacles.begin(); it != removedObstacles.end(); ++it)
// 	{
// 		for(std::map< int, CDefHandler * >::iterator itBat = battleInt->idToObstacle.begin(); itBat != battleInt->idToObstacle.end(); ++itBat)
// 		{
// 			if(itBat->first == *it) //remove this obstacle
// 			{
// 				battleInt->idToObstacle.erase(itBat);
// 				break;
// 			}
// 		}
// 	}
	//update accessible hexes
	battleInt->redrawBackgroundWithHexes(battleInt->activeStack);
}

void CPlayerInterface::battleCatapultAttacked(const CatapultAttack & ca)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	battleInt->stackIsCatapulting(ca);
}

void CPlayerInterface::battleStacksRemoved(const BattleStacksRemoved & bsr)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	for(std::set<ui32>::const_iterator it = bsr.stackIDs.begin(); it != bsr.stackIDs.end(); ++it) //for each removed stack
	{
		battleInt->stackRemoved(*it);
		//battleInt->stackRemoved(LOCPLINT->cb->battleGetStackByID(*it));
	}
}

void CPlayerInterface::battleNewRound(int round) //called at the beginning of each turn, round=-1 is the tactic phase, round=0 is the first "normal" turn
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	battleInt->newRound(round);
}

void CPlayerInterface::actionStarted(const BattleAction* action)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	curAction = new BattleAction(*action);
	battleInt->startAction(action);
}

void CPlayerInterface::actionFinished(const BattleAction* action)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	delete curAction;
	curAction = NULL;
	battleInt->endAction(action);
}

BattleAction CPlayerInterface::activeStack(const CStack * stack) //called when it's turn of that stack
{
	THREAD_CREATED_BY_CLIENT;
	tlog5 << "Awaiting command for " << stack->nodeName() << std::endl;
	CBattleInterface *b = battleInt;

	assert(!b->givenCommand->get()); //command buffer must be clean (we don't want to use old command)
	{
		boost::unique_lock<boost::recursive_mutex> un(*pim);
		b->stackActivated(stack);
		//Regeneration & mana drain go there
	}
	//wait till BattleInterface sets its command
	boost::unique_lock<boost::mutex> lock(b->givenCommand->mx);
	while(!b->givenCommand->data)
	{
		b->givenCommand->cond.wait(lock);
		if(!battleInt) //batle ended while we were waiting for movement (eg. because of spell)
			throw boost::thread_interrupted(); //will shut the thread peacefully
	}

	//tidy up
	BattleAction ret = *(b->givenCommand->data);
	delete b->givenCommand->data;
	b->givenCommand->data = NULL;

	//return command
	tlog5 << "Giving command for " << stack->nodeName() << std::endl;
	return ret;
}

void CPlayerInterface::battleEnd(const BattleResult *br)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	battleInt->battleFinished(*br);
}

void CPlayerInterface::battleStackMoved(const CStack * stack, std::vector<BattleHex> dest, int distance)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	battleInt->stackMoved(stack, dest, distance);
}
void CPlayerInterface::battleSpellCast( const BattleSpellCast *sc )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	battleInt->spellCast(sc);
}
void CPlayerInterface::battleStacksEffectsSet( const SetStackEffect & sse )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	battleInt->battleStacksEffectsSet(sse);
}
void CPlayerInterface::battleTriggerEffect (const BattleTriggerEffect & bte)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	battleInt->battleTriggerEffect(bte);
}
void CPlayerInterface::battleStacksAttacked(const std::vector<BattleStackAttacked> & bsa)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}
	
	std::vector<StackAttackedInfo> arg;
	for(std::vector<BattleStackAttacked>::const_iterator i = bsa.begin(); i != bsa.end(); i++)
	{
		const CStack *defender = cb->battleGetStackByID(i->stackAttacked, false);
		const CStack *attacker = cb->battleGetStackByID(i->attackerID, false);
		if(i->isEffect() && i->effect != 12) //and not armageddon
		{
			if (defender && !i->isSecondary())
				battleInt->displayEffect(i->effect, defender->position);
		}
		bool shooting = (LOCPLINT->curAction ? LOCPLINT->curAction->actionType == BattleAction::SHOOT : false); //FIXME: why action is deleted during enchanter cast?
		StackAttackedInfo to_put = {defender, i->damageAmount, i->killedAmount, attacker, shooting, i->killed(), i->willRebirth(), i->cloneKilled()};
		arg.push_back(to_put);
	}

	if(bsa.begin()->isEffect() && bsa.begin()->effect == 12) //for armageddon - I hope this condition is enough
	{
		battleInt->displayEffect(bsa.begin()->effect, -1);
	}

	battleInt->stacksAreAttacked(arg);
}
void CPlayerInterface::battleAttack(const BattleAttack *ba)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	assert(curAction);
	if(ba->lucky()) //lucky hit
	{
		const CStack *stack = cb->battleGetStackByID(ba->stackAttacking);
		std::string hlp = CGI->generaltexth->allTexts[45];
		boost::algorithm::replace_first(hlp,"%s", (stack->count != 1) ? stack->getCreature()->namePl.c_str() : stack->getCreature()->nameSing.c_str());
		battleInt->console->addText(hlp);
		battleInt->displayEffect(18, stack->position);
	}
	//TODO: bad luck?
	if (ba->deathBlow())
	{
		const CStack *stack = cb->battleGetStackByID(ba->stackAttacking);
		std::string hlp = CGI->generaltexth->allTexts[(stack->count != 1) ? 366 : 365];
		boost::algorithm::replace_first(hlp,"%s", (stack->count != 1) ? stack->getCreature()->namePl.c_str() : stack->getCreature()->nameSing.c_str());
		battleInt->console->addText(hlp);
		for (std::vector<BattleStackAttacked>::const_iterator i = ba->bsa.begin(); i != ba->bsa.end(); i++)
		{
			const CStack * attacked = cb->battleGetStackByID(i->stackAttacked);
			battleInt->displayEffect(73, attacked->position);
		}

	}

	const CStack * attacker = cb->battleGetStackByID(ba->stackAttacking);

	if(ba->shot())
	{
		for(std::vector<BattleStackAttacked>::const_iterator i = ba->bsa.begin(); i != ba->bsa.end(); i++)
		{
			if (!i->isSecondary()) //display projectile only for primary target
			{
				const CStack * attacked = cb->battleGetStackByID(i->stackAttacked);
				battleInt->stackAttacking(attacker, cb->battleGetPos(i->stackAttacked), attacked, true);
			}
		}
	}
	else
	{
		int shift = 0;
		if(ba->counter() && BattleHex::mutualPosition(curAction->destinationTile, attacker->position) < 0)
		{
			int distp = BattleHex::getDistance(curAction->destinationTile + 1, attacker->position);
			int distm = BattleHex::getDistance(curAction->destinationTile - 1, attacker->position);

			if( distp < distm )
				shift = 1;
			else
				shift = -1;
		}
		const CStack * attacked = cb->battleGetStackByID(ba->bsa.begin()->stackAttacked);
		battleInt->stackAttacking( attacker, ba->counter() ? curAction->destinationTile + shift : curAction->additionalInfo, attacked, false);
	}
}
void CPlayerInterface::battleObstaclePlaced(const CObstacleInstance &obstacle)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	battleInt->obstaclePlaced(obstacle);
}

void CPlayerInterface::yourTacticPhase(int distance)
{
	THREAD_CREATED_BY_CLIENT;
	while(battleInt && battleInt->tacticsMode)
		boost::this_thread::sleep(boost::posix_time::millisec(1));
}

void CPlayerInterface::showComp(const Component &comp, std::string message)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog(); //Fix for mantis #98

	CCS->soundh->playSoundFromSet(CCS->soundh->pickupSounds);
	adventureInt->infoBar.showComponent(comp, message);
}

void CPlayerInterface::showInfoDialog(const std::string &text, const std::vector<Component*> &components, int soundID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	std::vector<CComponent*> intComps;
	for(int i=0;i<components.size();i++)
		intComps.push_back(new CComponent(*components[i]));
	showInfoDialog(text,intComps,soundID);
}

void CPlayerInterface::showInfoDialog(const std::string &text, const std::vector<CComponent*> & components, int soundID, bool delComps)
{
	waitWhileDialog();

	stopMovement();
	CInfoWindow *temp = CInfoWindow::create(text, playerID, &components);
	temp->setDelComps(delComps);
	if(makingTurn && GH.listInt.size() && LOCPLINT == this)
	{
		CCS->soundh->playSound(static_cast<soundBase::soundID>(soundID));
		showingDialog->set(true);
		GH.pushInt(temp);
	}
	else
	{
		dialogs.push_back(temp);
	}
}

void CPlayerInterface::showYesNoDialog(const std::string &text, CFunctionList<void()> onYes, CFunctionList<void()> onNo, bool DelComps, const std::vector<CComponent*> & components)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);

	stopMovement();
	LOCPLINT->showingDialog->setn(true);
	CInfoWindow::showYesNoDialog(text, &components, onYes, onNo, DelComps, playerID);
}

void CPlayerInterface::showBlockingDialog( const std::string &text, const std::vector<Component> &components, ui32 askID, int soundID, bool selection, bool cancel )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();

	stopMovement();
	CCS->soundh->playSound(static_cast<soundBase::soundID>(soundID));

	if(!selection && cancel) //simple yes/no dialog
	{
		std::vector<CComponent*> intComps;
		for(int i=0;i<components.size();i++)
			intComps.push_back(new CComponent(components[i])); //will be deleted by close in window

		showYesNoDialog(text, boost::bind(&CCallback::selectionMade,cb,1,askID),boost::bind(&CCallback::selectionMade,cb,0,askID),true, intComps);
	}
	else if(selection)
	{
		std::vector<CSelectableComponent*> intComps;
		for(int i=0;i<components.size();i++)
			intComps.push_back(new CSelectableComponent(components[i])); //will be deleted by CSelWindow::close

		std::vector<std::pair<std::string,CFunctionList<void()> > > pom;
		pom.push_back(std::pair<std::string,CFunctionList<void()> >("IOKAY.DEF",0));
		if(cancel)
		{
			pom.push_back(std::pair<std::string,CFunctionList<void()> >("ICANCEL.DEF",0));
		}

		int charperline = 35;
		if (pom.size() > 1)
			charperline = 50;
		CSelWindow * temp = new CSelWindow(text, playerID, charperline, intComps, pom, askID);
		GH.pushInt(temp);
		intComps[0]->clickLeft(true, false);
	}

}

void CPlayerInterface::tileRevealed(const boost::unordered_set<int3, ShashInt3> &pos)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	for(boost::unordered_set<int3, ShashInt3>::const_iterator i=pos.begin(); i!=pos.end();i++)
		adventureInt->minimap.showTile(*i);
	if(pos.size())
		GH.totalRedraw();
}

void CPlayerInterface::tileHidden(const boost::unordered_set<int3, ShashInt3> &pos)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	for(boost::unordered_set<int3, ShashInt3>::const_iterator i=pos.begin(); i!=pos.end();i++)
		adventureInt->minimap.hideTile(*i);
	if(pos.size())
		GH.totalRedraw();
}

void CPlayerInterface::openHeroWindow(const CGHeroInstance *hero)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	GH.pushInt(new CHeroWindow(hero));
}
/*
void CPlayerInterface::heroArtifactSetChanged(const CGHeroInstance*hero)
{
	boost::unique_lock<boost::recursive_mutex> un(*pim);
	if(adventureInt->heroWindow->curHero && adventureInt->heroWindow->curHero->id == hero->id) //hero window is opened
	{
		adventureInt->heroWindow->deactivate();
		adventureInt->heroWindow->setHero(hero);
		adventureInt->heroWindow->activate();
	}
	else if(CExchangeWindow* cew = dynamic_cast<CExchangeWindow*>(GH.topInt())) //exchange window is open
	{
		cew->deactivate();
		for(int g=0; g<ARRAY_COUNT(cew->heroInst); ++g)
		{
			if(cew->heroInst[g]->id == hero->id)
			{
				cew->heroInst[g] = hero;
				cew->artifs[g]->updateState = true;
				cew->artifs[g]->setHero(hero);
				cew->artifs[g]->updateState = false;
			}
		}
		cew->prepareBackground();
		cew->activate();
	}
	else if(CTradeWindow *caw = dynamic_cast<CTradeWindow*>(GH.topInt()))
	{
		if(caw->arts)
		{
			caw->deactivate();
			caw->arts->updateState = true;
			caw->arts->setHero(hero);
			caw->arts->updateState = false;
			caw->activate();
		}
	}

	updateInfo(hero);
}*/

void CPlayerInterface::availableCreaturesChanged( const CGDwelling *town )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(const CGTownInstance * townObj = dynamic_cast<const CGTownInstance*>(town))
	{
		CFortScreen *fs = dynamic_cast<CFortScreen*>(GH.topInt());
		if(fs)
			fs->creaturesChanged();

		BOOST_FOREACH(IShowActivatable *isa, GH.listInt)
		{
			CKingdomInterface *ki = dynamic_cast<CKingdomInterface*>(isa);
			if (ki && townObj)
				ki->townChanged(townObj);
		}
	}
	else if(GH.listInt.size() && (town->ID == 17  ||  town->ID == 20  ||  town->ID == 106)) //external dwelling
	{
		CRecruitmentWindow *crw = dynamic_cast<CRecruitmentWindow*>(GH.topInt());
		if(crw && crw->dwelling == town)
			crw->availableCreaturesChanged();
	}
}

void CPlayerInterface::heroBonusChanged( const CGHeroInstance *hero, const Bonus &bonus, bool gain )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(bonus.type == Bonus::NONE)	
		return;

	updateInfo(hero);
	if ((bonus.type == Bonus::FLYING_MOVEMENT || bonus.type == Bonus::WATER_WALKING) && !gain)
	{
		//recalculate paths because hero has lost bonus influencing pathfinding
		eraseCurrentPathOf(hero, false);
	}
}

template <typename Handler> void CPlayerInterface::serializeTempl( Handler &h, const int version )
{
	h & playerID;
	h & spellbookSettings;

	//sleeping heroes
	ui8 sleepingSize;
	if (h.saving)
		sleepingSize = sleepingHeroes.size();
	h & sleepingSize;
	for (int i = 0; i < sleepingSize; i++)
	{
		si32 hid;
		if (h.saving)
			hid = sleepingHeroes[i]->id;
		h &	hid;
		if (!h.saving)
		{
			const CGHeroInstance *hero = cb->getHero(hid);
			sleepingHeroes += hero;
		}
	}

	//hero list order
	ui8 heroListSize;
	if (h.saving)
		heroListSize = wanderingHeroes.size();
	else
		wanderingHeroes.clear();
	h & heroListSize;
	for (int i = 0; i < heroListSize; i++)
	{
		si32 hid;
		if (h.saving)
			hid = wanderingHeroes[i]->id;
		h & hid;
		if (!h.saving)
		{
			const CGHeroInstance *hero = cb->getHero(hid);
			wanderingHeroes += hero;
		}
	}
}

void CPlayerInterface::serialize( COSer<CSaveFile> &h, const int version )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	serializeTempl(h,version);
}

void CPlayerInterface::serialize( CISer<CLoadFile> &h, const int version )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	serializeTempl(h,version);
	firstCall = -1;
}

bool CPlayerInterface::moveHero( const CGHeroInstance *h, CGPath path )
{
	if(!LOCPLINT->makingTurn)
		return false;
	if (!h)
		return false; //can't find hero

	if (adventureInt && adventureInt->isHeroSleeping(h))
	{
		adventureInt->sleepWake.clickLeft(true, false);
		adventureInt->sleepWake.clickLeft(false, true);
		//could've just called
		//adventureInt->fsleepWake();
		//but no authentic button click/sound ;-)
	}

	int i = 1;
	bool result = false; //TODO why not set to true anywhere?
	{
		//evil...

		auto unlockEvents = vstd::makeUnlockGuard(eventsM);
		auto unlockGs = vstd::makeUnlockSharedGuard(cb->getGsMutex()); //GS mutex is above PIM because CClient::run thread first locks PIM and then GS -> so this way we avoid deadlocks
		auto unlockPim = vstd::makeUnlockGuard(*pim);

		{
			path.convert(0);
			boost::unique_lock<boost::mutex> un(stillMoveHero.mx);
			stillMoveHero.data = CONTINUE_MOVE;

			enum TerrainTile::EterrainType currentTerrain = TerrainTile::border; // not init yet
			enum TerrainTile::EterrainType newTerrain;
			int sh = -1;

			const TerrainTile * curTile = cb->getTile(CGHeroInstance::convertPosition(h->pos, false));

			for(i=path.nodes.size()-1; i>0 && (stillMoveHero.data == CONTINUE_MOVE || curTile->blocked); i--)
			{
				//changing z coordinate means we're moving through subterranean gate -> it's done automatically upon the visit, so we don't have to request that move here
				if(path.nodes[i-1].coord.z != path.nodes[i].coord.z)
					continue;

				//stop sending move requests if the next node can't be reached at the current turn (hero exhausted his move points)
				if(path.nodes[i-1].turns)
				{
					stillMoveHero.data = STOP_MOVE;
					break;
				}

				// Start a new sound for the hero movement or let the existing one carry on.
#if 0
				// TODO
				if (hero is flying && sh == -1)
					sh = CCS->soundh->playSound(soundBase::horseFlying, -1);
#endif
				{
					newTerrain = cb->getTile(CGHeroInstance::convertPosition(path.nodes[i].coord, false))->tertype;

					if (newTerrain != currentTerrain)
					{
						CCS->soundh->stopSound(sh);
						sh = CCS->soundh->playSound(CCS->soundh->horseSounds[newTerrain], -1);
						currentTerrain = newTerrain;
					}
				}

				stillMoveHero.data = WAITING_MOVE;

				int3 endpos(path.nodes[i-1].coord.x, path.nodes[i-1].coord.y, h->pos.z);
				bool guarded = CGI->mh->map->isInTheMap(cb->guardingCreaturePosition(endpos - int3(1, 0, 0)));

				cb->moveHero(h,endpos);

				while(stillMoveHero.data != STOP_MOVE  &&  stillMoveHero.data != CONTINUE_MOVE)
					stillMoveHero.cond.wait(un);

				if (guarded) // Abort movement if a guard was fought.
					break;
			}

			CCS->soundh->stopSound(sh);
		}

		//RAII unlocks
	}

	if (adventureInt)
	{
		// (i == 0) means hero went through all the path
		adventureInt->updateMoveHero(h, (i != 0));
		adventureInt->updateNextHero(h);
	}
	return result;
}

bool CPlayerInterface::shiftPressed() const
{
	return SDL_GetKeyState(SDLK_LSHIFT)  ||  SDL_GetKeyState(SDLK_RSHIFT);
}

bool CPlayerInterface::altPressed() const
{
	return SDL_GetKeyState(SDLK_LALT)  ||  SDL_GetKeyState(SDLK_RALT);
}

void CPlayerInterface::showGarrisonDialog( const CArmedInstance *up, const CGHeroInstance *down, bool removableUnits, int queryID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	auto onEnd = [=]{ cb->selectionMade(0, queryID); };

	if(stillMoveHero.get() == DURING_MOVE  && adventureInt->terrain.currentPath && adventureInt->terrain.currentPath->nodes.size() > 1) //to ignore calls on passing through garrisons
	{
		onEnd();
		return;
	}

	waitForAllDialogs();

	CGarrisonWindow *cgw = new CGarrisonWindow(up,down,removableUnits);
	cgw->quit->callback += onEnd;
	GH.pushInt(cgw);
}

/**
 * Shows the dialog that appears when right-clicking an artifact that can be assembled
 * into a combinational one on an artifact screen. Does not require the combination of
 * artifacts to be legal.
 * @param artifactID ID of a constituent artifact.
 * @param assembleTo ID of artifact to assemble a constituent into, not used when assemble
 * is false.
 * @param assemble True if the artifact is to be assembled, false if it is to be disassembled.
 */
void CPlayerInterface::showArtifactAssemblyDialog (ui32 artifactID, ui32 assembleTo, bool assemble, CFunctionList<void()> onYes, CFunctionList<void()> onNo)
{
	const CArtifact &artifact = *CGI->arth->artifacts[artifactID];
	std::string text = artifact.Description();
	text += "\n\n";
	std::vector<CComponent*> scs;

	if (assemble) {
		const CArtifact &assembledArtifact = *CGI->arth->artifacts[assembleTo];

		// You possess all of the components to...
		text += boost::str(boost::format(CGI->generaltexth->allTexts[732]) % assembledArtifact.Name());

		// Picture of assembled artifact at bottom.
		CComponent* sc = new CComponent(CComponent::artifact, assembledArtifact.id, 0);
		//sc->description = assembledArtifact.Description();
		//sc->subtitle = assembledArtifact.Name();
		scs.push_back(sc);
	} else {
		// Do you wish to disassemble this artifact?
		text += CGI->generaltexth->allTexts[733];
	}

	showYesNoDialog(text, onYes, onNo, true, scs);
}

void CPlayerInterface::requestRealized( PackageApplied *pa )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(stillMoveHero.get() == DURING_MOVE)
		stillMoveHero.setn(CONTINUE_MOVE);
}

void CPlayerInterface::heroExchangeStarted(si32 hero1, si32 hero2)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	GH.pushInt(new CExchangeWindow(hero1, hero2));
}

void CPlayerInterface::objectPropertyChanged(const SetObjectProperty * sop)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	//redraw minimap if owner changed
	if(sop->what == ObjProperty::OWNER)
	{
		const CGObjectInstance * obj = cb->getObj(sop->id);
		std::set<int3> pos = obj->getBlockedPos();
		for(std::set<int3>::const_iterator it = pos.begin(); it != pos.end(); ++it)
		{
			if(cb->isVisible(*it))
				adventureInt->minimap.showTile(*it);
		}

		if(obj->ID == GameConstants::TOWNI_TYPE)
		{
			if(obj->tempOwner == playerID)
				towns.push_back(static_cast<const CGTownInstance *>(obj));
			else
				towns -= obj;
			adventureInt->townList.update();
		}

		assert(cb->getTownsInfo().size() == towns.size());
	}

}

void CPlayerInterface::initializeHeroTownList()
{
	std::vector<const CGHeroInstance*> allHeroes = cb->getHeroesInfo();
	/*
	std::vector <const CGHeroInstance *> newWanderingHeroes;

	//applying current heroes order to new heroes info
	int j;
	for (int i = 0; i < wanderingHeroes.size(); i++)
		if ((j = vstd::find_pos(allHeroes, wanderingHeroes[i])) >= 0)
			if (!allHeroes[j]->inTownGarrison)
			{
				newWanderingHeroes += allHeroes[j];
				allHeroes -= allHeroes[j];
			}
	//all the rest of new heroes go the end of the list
	wanderingHeroes.clear();
	wanderingHeroes = newWanderingHeroes;
	newWanderingHeroes.clear();*/

	for (int i = 0; i < allHeroes.size(); i++)
		if (!allHeroes[i]->inTownGarrison)
			wanderingHeroes += allHeroes[i];

	std::vector<const CGTownInstance*> allTowns = cb->getTownsInfo();
	/*
	std::vector<const CGTownInstance*> newTowns;
	for (int i = 0; i < towns.size(); i++)
		if ((j = vstd::find_pos(allTowns, towns[i])) >= 0)
		{
			newTowns += allTowns[j];
			allTowns -= allTowns[j];
		}

	towns.clear();
	towns = newTowns;
	newTowns.clear();*/
	for(int i = 0; i < allTowns.size(); i++)
		towns.push_back(allTowns[i]);

	if (adventureInt)
		adventureInt->updateNextHero(NULL);
}

void CPlayerInterface::showRecruitmentDialog(const CGDwelling *dwelling, const CArmedInstance *dst, int level)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();
	CRecruitmentWindow *cr = new CRecruitmentWindow(dwelling, level, dst, boost::bind(&CCallback::recruitCreatures, cb, dwelling, _1, _2, -1));
	GH.pushInt(cr);
}

void CPlayerInterface::waitWhileDialog(bool unlockPim /*= true*/)
{
	if(GH.amIGuiThread())
	{
		tlog3 << "Cannot wait for dialogs in gui thread (deadlock risk)!\n";
		return;
	}

	auto unlock = vstd::makeUnlockGuardIf(*pim, unlockPim);
	boost::unique_lock<boost::mutex> un(showingDialog->mx);
	while(showingDialog->data)
		showingDialog->cond.wait(un);
}

void CPlayerInterface::showShipyardDialog(const IShipyard *obj)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	int state = obj->state();
	std::vector<si32> cost;
	obj->getBoatCost(cost);
	CShipyardWindow *csw = new CShipyardWindow(cost, state, obj->getBoatType(), boost::bind(&CCallback::buildBoat, cb, obj));
	GH.pushInt(csw);
}

void CPlayerInterface::newObject( const CGObjectInstance * obj )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	//we might have built a boat in shipyard in opened town screen
	if(obj->ID == 8
		&& LOCPLINT->castleInt
		&&  obj->pos-obj->getVisitableOffset() == LOCPLINT->castleInt->town->bestLocation())
	{
		CCS->soundh->playSound(soundBase::newBuilding);
		LOCPLINT->castleInt->addBuilding(20);
	}
}

void CPlayerInterface::centerView (int3 pos, int focusTime)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();
	adventureInt->centerOn (pos);
	if(focusTime)
	{
		bool activeAdv = (GH.topInt() == adventureInt  &&  adventureInt->isActive());
		if(activeAdv)
			adventureInt->deactivate();

		SDL_Delay(focusTime);

		if(activeAdv)
			adventureInt->activate();
	}
}

void CPlayerInterface::objectRemoved( const CGObjectInstance *obj )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(obj->ID == GameConstants::HEROI_TYPE  &&  obj->tempOwner == playerID)
	{
		const CGHeroInstance *h = static_cast<const CGHeroInstance*>(obj);
		heroKilled(h);
	}
}

bool CPlayerInterface::ctrlPressed() const
{
	return SDL_GetKeyState(SDLK_LCTRL)  ||  SDL_GetKeyState(SDLK_RCTRL);
}

void CPlayerInterface::update()
{
	while(!terminate_cond.get() && !pim->try_lock()) //try acquiring long until it succeeds or we are told to terminate
		boost::this_thread::sleep(boost::posix_time::milliseconds(15));

	if(terminate_cond.get())
		return;

	boost::unique_lock<boost::recursive_mutex> un(*pim, boost::adopt_lock); //create lock from already owned mutex
	//make sure that gamestate won't change when GUI objects may obtain its parts on event processing or drawing request
	boost::shared_lock<boost::shared_mutex> gsLock(cb->getGsMutex());

	//if there are any waiting dialogs, show them
	if((howManyPeople <= 1 || makingTurn) && dialogs.size() && !showingDialog->get())
	{
		showingDialog->set(true);
		GH.pushInt(dialogs.front());
		dialogs.pop_front();
	}

	//in some conditions we may receive calls before selection is initialized - we must ignore them
	if(adventureInt && !adventureInt->selection && GH.topInt() == adventureInt)
	{
		return;
	}

	// Handles mouse and key input
	GH.updateTime();
	GH.handleEvents();

	if(adventureInt && !adventureInt->isActive() && adventureInt->scrollingDir) //player forces map scrolling though interface is disabled
		GH.totalRedraw();
	else
		GH.simpleRedraw();

	if (settings["general"]["showfps"].Bool())
		GH.drawFPSCounter();

	// draw the mouse cursor and update the screen
	CCS->curh->draw1();
	CSDL_Ext::update(screen);
	CCS->curh->draw2();
}

int CPlayerInterface::getLastIndex( std::string namePrefix)
{
	using namespace boost::filesystem;
	using namespace boost::algorithm;

	std::map<std::time_t, int> dates; //save number => datestamp

	directory_iterator enddir;
	for (directory_iterator dir(GVCMIDirs.UserPath + "/Games"); dir!=enddir; dir++)
	{
		if(is_regular(dir->status()))
		{
			std::string name = dir->path().leaf().string();
			if(starts_with(name, namePrefix) && ends_with(name, ".vlgm1"))
			{
				char nr = name[namePrefix.size()];
				if(std::isdigit(nr))
				{
					dates[last_write_time(dir->path())] = boost::lexical_cast<int>(nr);
				}
			}
		}
	}

	if(dates.size())
		return (--dates.end())->second; //return latest file number
	return 0;
}

void CPlayerInterface::initMovement( const TryMoveHero &details, const CGHeroInstance * ho, const int3 &hp )
{
	if(details.end.x+1 == details.start.x && details.end.y+1 == details.start.y) //tl
	{
		//ho->moveDir = 1;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-3][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, -31)));
		CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 1, -31)));
		CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 33, -31)));
		CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 65, -31)));

		CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 1)));
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1, 1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33, 1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65, 1), ho->id);

		CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 33)));
		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1, 33), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33, 33), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65, 33), ho->id);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x == details.start.x && details.end.y+1 == details.start.y) //t
	{
		//ho->moveDir = 2;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 0, -31)));
		CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 32, -31)));
		CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 64, -31)));

		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 0, 1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 32, 1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 64, 1), ho->id);

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 0, 33), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 32, 33), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 64, 33), ho->id);

		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x-1 == details.start.x && details.end.y+1 == details.start.y) //tr
	{
		//ho->moveDir = 3;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -1, -31)));
		CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 31, -31)));
		CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 63, -31)));
		CGI->mh->ttiles[hp.x+1][hp.y-2][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, -31)));

		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1, 1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31, 1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63, 1), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 1)));

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1, 33), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31, 33), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63, 33), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 33)));

		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y-2][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y-2][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x-1 == details.start.x && details.end.y == details.start.y) //r
	{
		//ho->moveDir = 4;
		ho->isStanding = false;
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1, 0), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31, 0), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63, 0), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 0)));

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1, 32), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31, 32), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63, 32), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 32)));

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x-1 == details.start.x && details.end.y-1 == details.start.y) //br
	{
		//ho->moveDir = 5;
		ho->isStanding = false;
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1, -1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31, -1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63, -1), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, -1)));

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1, 31), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31, 31), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63, 31), ho->id);
		CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 31)));

		CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -1, 63)));
		CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 31, 63)));
		CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 63, 63)));
		CGI->mh->ttiles[hp.x+1][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 95, 63)));

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x+1][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x+1][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x == details.start.x && details.end.y-1 == details.start.y) //b
	{
		//ho->moveDir = 6;
		ho->isStanding = false;
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 0, -1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 32, -1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 64, -1), ho->id);

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 0, 31), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 32, 31), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 64, 31), ho->id);

		CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 0, 63)));
		CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 32, 63)));
		CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 64, 63)));

		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x+1 == details.start.x && details.end.y-1 == details.start.y) //bl
	{
		//ho->moveDir = 7;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, -1)));
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1, -1), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33, -1), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65, -1), ho->id);

		CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 31)));
		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1, 31), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33, 31), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65, 31), ho->id);

		CGI->mh->ttiles[hp.x-3][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 63)));
		CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 1, 63)));
		CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 33, 63)));
		CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, 65, 63)));

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-2][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-1][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
		std::stable_sort(CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x][hp.y+1][hp.z].objects.end(), ocmptwo_cgin);
	}
	else if(details.end.x+1 == details.start.x && details.end.y == details.start.y) //l
	{
		//ho->moveDir = 8;
		ho->isStanding = false;
		CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 0)));
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1, 0), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33, 0), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65, 0), ho->id);

		CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.push_back(std::make_pair(ho, genRect(32, 32, -31, 32)));
		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1, 32), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33, 32), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65, 32), ho->id);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y-1][hp.z].objects.end(), ocmptwo_cgin);

		std::stable_sort(CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.begin(), CGI->mh->ttiles[hp.x-3][hp.y][hp.z].objects.end(), ocmptwo_cgin);
	}
}

void CPlayerInterface::movementPxStep( const TryMoveHero &details, int i, const int3 &hp, const CGHeroInstance * ho )
{
	if(details.end.x+1 == details.start.x && details.end.y+1 == details.start.y) //tl
	{
		//setting advmap shift
		adventureInt->terrain.moveX = i-32;
		adventureInt->terrain.moveY = i-32;

		subRect(hp.x-3, hp.y-2, hp.z, genRect(32, 32, -31+i, -31+i), ho->id);
		subRect(hp.x-2, hp.y-2, hp.z, genRect(32, 32, 1+i, -31+i), ho->id);
		subRect(hp.x-1, hp.y-2, hp.z, genRect(32, 32, 33+i, -31+i), ho->id);
		subRect(hp.x, hp.y-2, hp.z, genRect(32, 32, 65+i, -31+i), ho->id);

		subRect(hp.x-3, hp.y-1, hp.z, genRect(32, 32, -31+i, 1+i), ho->id);
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1+i, 1+i), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33+i, 1+i), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65+i, 1+i), ho->id);

		subRect(hp.x-3, hp.y, hp.z, genRect(32, 32, -31+i, 33+i), ho->id);
		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1+i, 33+i), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33+i, 33+i), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65+i, 33+i), ho->id);
	}
	else if(details.end.x == details.start.x && details.end.y+1 == details.start.y) //t
	{
		//setting advmap shift
		adventureInt->terrain.moveY = i-32;

		subRect(hp.x-2, hp.y-2, hp.z, genRect(32, 32, 0, -31+i), ho->id);
		subRect(hp.x-1, hp.y-2, hp.z, genRect(32, 32, 32, -31+i), ho->id);
		subRect(hp.x, hp.y-2, hp.z, genRect(32, 32, 64, -31+i), ho->id);

		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 0, 1+i), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 32, 1+i), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 64, 1+i), ho->id);

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 0, 33+i), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 32, 33+i), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 64, 33+i), ho->id);
	}
	else if(details.end.x-1 == details.start.x && details.end.y+1 == details.start.y) //tr
	{
		//setting advmap shift
		adventureInt->terrain.moveX = -i+32;
		adventureInt->terrain.moveY = i-32;

		subRect(hp.x-2, hp.y-2, hp.z, genRect(32, 32, -1-i, -31+i), ho->id);
		subRect(hp.x-1, hp.y-2, hp.z, genRect(32, 32, 31-i, -31+i), ho->id);
		subRect(hp.x, hp.y-2, hp.z, genRect(32, 32, 63-i, -31+i), ho->id);
		subRect(hp.x+1, hp.y-2, hp.z, genRect(32, 32, 95-i, -31+i), ho->id);

		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1-i, 1+i), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31-i, 1+i), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63-i, 1+i), ho->id);
		subRect(hp.x+1, hp.y-1, hp.z, genRect(32, 32, 95-i, 1+i), ho->id);

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1-i, 33+i), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31-i, 33+i), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63-i, 33+i), ho->id);
		subRect(hp.x+1, hp.y, hp.z, genRect(32, 32, 95-i, 33+i), ho->id);
	}
	else if(details.end.x-1 == details.start.x && details.end.y == details.start.y) //r
	{
		//setting advmap shift
		adventureInt->terrain.moveX = -i+32;

		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1-i, 0), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31-i, 0), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63-i, 0), ho->id);
		subRect(hp.x+1, hp.y-1, hp.z, genRect(32, 32, 95-i, 0), ho->id);

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1-i, 32), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31-i, 32), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63-i, 32), ho->id);
		subRect(hp.x+1, hp.y, hp.z, genRect(32, 32, 95-i, 32), ho->id);
	}
	else if(details.end.x-1 == details.start.x && details.end.y-1 == details.start.y) //br
	{

		//setting advmap shift
		adventureInt->terrain.moveX = -i+32;
		adventureInt->terrain.moveY = -i+32;

		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, -1-i, -1-i), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 31-i, -1-i), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 63-i, -1-i), ho->id);
		subRect(hp.x+1, hp.y-1, hp.z, genRect(32, 32, 95-i, -1-i), ho->id);

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, -1-i, 31-i), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 31-i, 31-i), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 63-i, 31-i), ho->id);
		subRect(hp.x+1, hp.y, hp.z, genRect(32, 32, 95-i, 31-i), ho->id);

		subRect(hp.x-2, hp.y+1, hp.z, genRect(32, 32, -1-i, 63-i), ho->id);
		subRect(hp.x-1, hp.y+1, hp.z, genRect(32, 32, 31-i, 63-i), ho->id);
		subRect(hp.x, hp.y+1, hp.z, genRect(32, 32, 63-i, 63-i), ho->id);
		subRect(hp.x+1, hp.y+1, hp.z, genRect(32, 32, 95-i, 63-i), ho->id);
	}
	else if(details.end.x == details.start.x && details.end.y-1 == details.start.y) //b
	{
		//setting advmap shift
		adventureInt->terrain.moveY = -i+32;

		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 0, -1-i), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 32, -1-i), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 64, -1-i), ho->id);

		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 0, 31-i), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 32, 31-i), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 64, 31-i), ho->id);

		subRect(hp.x-2, hp.y+1, hp.z, genRect(32, 32, 0, 63-i), ho->id);
		subRect(hp.x-1, hp.y+1, hp.z, genRect(32, 32, 32, 63-i), ho->id);
		subRect(hp.x, hp.y+1, hp.z, genRect(32, 32, 64, 63-i), ho->id);
	}
	else if(details.end.x+1 == details.start.x && details.end.y-1 == details.start.y) //bl
	{
		//setting advmap shift
		adventureInt->terrain.moveX = i-32;
		adventureInt->terrain.moveY = -i+32;

		subRect(hp.x-3, hp.y-1, hp.z, genRect(32, 32, -31+i, -1-i), ho->id);
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1+i, -1-i), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33+i, -1-i), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65+i, -1-i), ho->id);

		subRect(hp.x-3, hp.y, hp.z, genRect(32, 32, -31+i, 31-i), ho->id);
		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1+i, 31-i), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33+i, 31-i), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65+i, 31-i), ho->id);

		subRect(hp.x-3, hp.y+1, hp.z, genRect(32, 32, -31+i, 63-i), ho->id);
		subRect(hp.x-2, hp.y+1, hp.z, genRect(32, 32, 1+i, 63-i), ho->id);
		subRect(hp.x-1, hp.y+1, hp.z, genRect(32, 32, 33+i, 63-i), ho->id);
		subRect(hp.x, hp.y+1, hp.z, genRect(32, 32, 65+i, 63-i), ho->id);
	}
	else if(details.end.x+1 == details.start.x && details.end.y == details.start.y) //l
	{
		//setting advmap shift
		adventureInt->terrain.moveX = i-32;

		subRect(hp.x-3, hp.y-1, hp.z, genRect(32, 32, -31+i, 0), ho->id);
		subRect(hp.x-2, hp.y-1, hp.z, genRect(32, 32, 1+i, 0), ho->id);
		subRect(hp.x-1, hp.y-1, hp.z, genRect(32, 32, 33+i, 0), ho->id);
		subRect(hp.x, hp.y-1, hp.z, genRect(32, 32, 65+i, 0), ho->id);

		subRect(hp.x-3, hp.y, hp.z, genRect(32, 32, -31+i, 32), ho->id);
		subRect(hp.x-2, hp.y, hp.z, genRect(32, 32, 1+i, 32), ho->id);
		subRect(hp.x-1, hp.y, hp.z, genRect(32, 32, 33+i, 32), ho->id);
		subRect(hp.x, hp.y, hp.z, genRect(32, 32, 65+i, 32), ho->id);
	}
}

void CPlayerInterface::finishMovement( const TryMoveHero &details, const int3 &hp, const CGHeroInstance * ho )
{
	adventureInt->terrain.moveX = adventureInt->terrain.moveY = 0;

	if(details.end.x+1 == details.start.x && details.end.y+1 == details.start.y) //tl
	{
		delObjRect(hp.x, hp.y-2, hp.z, ho->id);
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x, hp.y, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
		delObjRect(hp.x-3, hp.y, hp.z, ho->id);
	}
	else if(details.end.x == details.start.x && details.end.y+1 == details.start.y) //t
	{
		delObjRect(hp.x, hp.y, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
	}
	else if(details.end.x-1 == details.start.x && details.end.y+1 == details.start.y) //tr
	{
		delObjRect(hp.x-2, hp.y-2, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x+1, hp.y, hp.z, ho->id);
		delObjRect(hp.x, hp.y, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
	}
	else if(details.end.x-1 == details.start.x && details.end.y == details.start.y) //r
	{
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
	}
	else if(details.end.x-1 == details.start.x && details.end.y-1 == details.start.y) //br
	{
		delObjRect(hp.x-2, hp.y+1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y, hp.z, ho->id);
		delObjRect(hp.x+1, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
	}
	else if(details.end.x == details.start.x && details.end.y-1 == details.start.y) //b
	{
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
	}
	else if(details.end.x+1 == details.start.x && details.end.y-1 == details.start.y) //bl
	{
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-1, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-2, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x-3, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x, hp.y, hp.z, ho->id);
		delObjRect(hp.x, hp.y+1, hp.z, ho->id);
	}
	else if(details.end.x+1 == details.start.x && details.end.y == details.start.y) //l
	{
		delObjRect(hp.x, hp.y-1, hp.z, ho->id);
		delObjRect(hp.x, hp.y, hp.z, ho->id);
	}

	//restoring good rects
	subRect(details.end.x-2, details.end.y-1, details.end.z, genRect(32, 32, 0, 0), ho->id);
	subRect(details.end.x-1, details.end.y-1, details.end.z, genRect(32, 32, 32, 0), ho->id);
	subRect(details.end.x, details.end.y-1, details.end.z, genRect(32, 32, 64, 0), ho->id);

	subRect(details.end.x-2, details.end.y, details.end.z, genRect(32, 32, 0, 32), ho->id);
	subRect(details.end.x-1, details.end.y, details.end.z, genRect(32, 32, 32, 32), ho->id);
	subRect(details.end.x, details.end.y, details.end.z, genRect(32, 32, 64, 32), ho->id);

	//restoring good order of objects
	std::stable_sort(CGI->mh->ttiles[details.end.x-2][details.end.y-1][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x-2][details.end.y-1][details.end.z].objects.end(), ocmptwo_cgin);
	std::stable_sort(CGI->mh->ttiles[details.end.x-1][details.end.y-1][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x-1][details.end.y-1][details.end.z].objects.end(), ocmptwo_cgin);
	std::stable_sort(CGI->mh->ttiles[details.end.x][details.end.y-1][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x][details.end.y-1][details.end.z].objects.end(), ocmptwo_cgin);

	std::stable_sort(CGI->mh->ttiles[details.end.x-2][details.end.y][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x-2][details.end.y][details.end.z].objects.end(), ocmptwo_cgin);
	std::stable_sort(CGI->mh->ttiles[details.end.x-1][details.end.y][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x-1][details.end.y][details.end.z].objects.end(), ocmptwo_cgin);
	std::stable_sort(CGI->mh->ttiles[details.end.x][details.end.y][details.end.z].objects.begin(), CGI->mh->ttiles[details.end.x][details.end.y][details.end.z].objects.end(), ocmptwo_cgin);
}

void CPlayerInterface::gameOver(ui8 player, bool victory )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
		return;

	if(player == playerID)
	{
		if(!victory)
			showInfoDialog(CGI->generaltexth->allTexts[95]);
// 		else
// 			showInfoDialog("Placeholder message: you won!");

		makingTurn = true;
		waitForAllDialogs(); //wait till all dialogs are displayed and closed
		makingTurn = false;

		howManyPeople--;
		if(!howManyPeople) //all human players eliminated
		{
			if(cb->getStartInfo()->mode != StartInfo::CAMPAIGN)
				requestReturningToMainMenu();
			else
				requestStoppingClient();
		}
		cb->unregisterMyInterface(); //we already won/lost, nothing else matters
	}

	else
	{
		if(!victory && cb->getPlayerStatus(playerID) == PlayerState::INGAME) //enemy has lost
		{
			std::string txt = CGI->generaltexth->allTexts[5]; //%s has been vanquished!
			boost::algorithm::replace_first(txt, "%s", CGI->generaltexth->capColors[player]);
			showInfoDialog(txt,std::vector<CComponent*>(1, new CComponent(CComponent::flag, player, 0)));
		}
	}
}

void CPlayerInterface::playerBonusChanged( const Bonus &bonus, bool gain )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
}

void CPlayerInterface::showPuzzleMap()
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	waitWhileDialog();

	//TODO: interface should not know the real position of Grail...
	double ratio = 0;
	int3 grailPos = cb->getGrailPos(ratio);

	GH.pushInt(new CPuzzleWindow(grailPos, ratio));
}

void CPlayerInterface::advmapSpellCast(const CGHeroInstance * caster, int spellID)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if (spellID == Spells::FLY || spellID == Spells::WATER_WALK)
	{
		eraseCurrentPathOf(caster, false);
	}
	const CSpell * spell = CGI->spellh->spells[spellID];
	if (vstd::contains(CCS->soundh->spellSounds, spell))
		CCS->soundh->playSound(CCS->soundh->spellSounds[spell]);
}

void CPlayerInterface::eraseCurrentPathOf( const CGHeroInstance * ho, bool checkForExistanceOfPath /*= true */ )
{
	if(checkForExistanceOfPath)
	{
		assert(vstd::contains(paths, ho));
	}
	else if (!vstd::contains(paths, ho))
	{
		return;
	}
	assert(ho == adventureInt->selection);

	paths.erase(ho);
	adventureInt->terrain.currentPath = NULL;
	adventureInt->updateMoveHero(ho, false);
}

void CPlayerInterface::removeLastNodeFromPath(const CGHeroInstance *ho)
{
	adventureInt->terrain.currentPath->nodes.erase(adventureInt->terrain.currentPath->nodes.end()-1);
	if(adventureInt->terrain.currentPath->nodes.size() < 2)  //if it was the last one, remove entire path and path with only one tile is not a real path
		eraseCurrentPathOf(ho);
}

CGPath * CPlayerInterface::getAndVerifyPath(const CGHeroInstance * h)
{
	if(vstd::contains(paths,h)) //hero has assigned path
	{
		CGPath &path = paths[h];
		if(!path.nodes.size())
		{
			tlog3 << "Warning: empty path found...\n";
			paths.erase(h);
		}
		else
		{
			assert(h->getPosition(false) == path.startPos());
			//update the hero path in case of something has changed on map
			if(LOCPLINT->cb->getPath2(path.endPos(), path))
				return &path;
			else
				paths.erase(h);
		}
	}

	return NULL;
}

void CPlayerInterface::acceptTurn()
{
	if(settings["session"]["autoSkip"].Bool())
	{
		while(CInfoWindow *iw = dynamic_cast<CInfoWindow *>(GH.topInt()))
			iw->close();
	}
	waitWhileDialog();

	if(howManyPeople > 1)
		adventureInt->startTurn();

	//select first hero if available.
	//TODO: check if hero is slept
	if(wanderingHeroes.size())
		adventureInt->select(wanderingHeroes.front());
	else
		adventureInt->select(towns.front());

	//show new day animation and sound on infobar
	adventureInt->infoBar.showDate();

	adventureInt->heroList.update();
	adventureInt->townList.update();
	adventureInt->updateNextHero(NULL);
	adventureInt->showAll(screen);

	if(settings["session"]["autoSkip"].Bool() && !LOCPLINT->shiftPressed())
	{
		if(CInfoWindow *iw = dynamic_cast<CInfoWindow *>(GH.topInt()))
			iw->close();

		adventureInt->endTurn.callback();
	}
}

void CPlayerInterface::tryDiggging(const CGHeroInstance *h)
{
	std::string hlp;
	CGI->mh->getTerrainDescr(h->getPosition(false), hlp, false);

	int msgToShow = -1;
	CGHeroInstance::ECanDig isDiggingPossible = h->diggingStatus();
	if(hlp.length())
		isDiggingPossible = CGHeroInstance::TILE_OCCUPIED; //TODO integrate with canDig

	switch(isDiggingPossible)
	{
	case CGHeroInstance::CAN_DIG:
		break;
	case CGHeroInstance::LACK_OF_MOVEMENT:
		msgToShow = 56; //"Digging for artifacts requires a whole day, try again tomorrow."
		break;
	case CGHeroInstance::TILE_OCCUPIED:
		msgToShow = 97; //Try searching on clear ground.
		break;
	case CGHeroInstance::WRONG_TERRAIN:
		msgToShow = 60; ////Try looking on land!
		break;
	default:
		assert(0);
	}

	if(msgToShow < 0)
		cb->dig(h);
	else
		showInfoDialog(CGI->generaltexth->allTexts[msgToShow]);
}

void CPlayerInterface::updateInfo(const CGObjectInstance * specific)
{
	adventureInt->infoBar.showSelection();
}

void CPlayerInterface::battleNewRoundFirst( int round )
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(LOCPLINT != this)
	{ //another local interface should do this
		return;
	}

	battleInt->newRoundFirst(round);
}

void CPlayerInterface::stopMovement()
{
	if(stillMoveHero.get() == DURING_MOVE)//if we are in the middle of hero movement
		stillMoveHero.setn(STOP_MOVE); //after showing dialog movement will be stopped
}

void CPlayerInterface::showMarketWindow(const IMarket *market, const CGHeroInstance *visitor)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(market->o->ID == 2) //Altar
	{
		//EEMarketMode mode = market->availableModes().front();
		if(market->allowsTrade(EMarketMode::ARTIFACT_EXP) && visitor->getAlignment() != EAlignment::EVIL)
			GH.pushInt(new CAltarWindow(market, visitor, EMarketMode::ARTIFACT_EXP));
		else if(market->allowsTrade(EMarketMode::CREATURE_EXP) && visitor->getAlignment() != EAlignment::GOOD)
			GH.pushInt(new CAltarWindow(market, visitor, EMarketMode::CREATURE_EXP));
	}
	else
		GH.pushInt(new CMarketplaceWindow(market, visitor, market->availableModes().front()));
}

void CPlayerInterface::showUniversityWindow(const IMarket *market, const CGHeroInstance *visitor)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	CUniversityWindow *cuw = new CUniversityWindow(visitor, market);
	GH.pushInt(cuw);
}

void CPlayerInterface::showHillFortWindow(const CGObjectInstance *object, const CGHeroInstance *visitor)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	CHillFortWindow *chfw = new CHillFortWindow(visitor, object);
	GH.pushInt(chfw);
}

void CPlayerInterface::availableArtifactsChanged(const CGBlackMarket *bm /*= NULL*/)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(CMarketplaceWindow *cmw = dynamic_cast<CMarketplaceWindow*>(GH.topInt()))
		cmw->artifactsChanged(false);
}

void CPlayerInterface::showTavernWindow(const CGObjectInstance *townOrTavern)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	CTavernWindow *tv = new CTavernWindow(townOrTavern);
	GH.pushInt(tv);
}

void CPlayerInterface::showThievesGuildWindow (const CGObjectInstance * obj)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	CThievesGuildWindow *tgw = new CThievesGuildWindow(obj);
	GH.pushInt(tgw);
}

void CPlayerInterface::showQuestLog()
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	CQuestLog * ql = new CQuestLog (LOCPLINT->cb->getMyQuests());
	GH.pushInt (ql);
}

void CPlayerInterface::showShipyardDialogOrProblemPopup(const IShipyard *obj)
{
	if(obj->state())
	{
		MetaString txt;
		obj->getProblemText(txt);
		showInfoDialog(txt.toString());
	}
	else
		showShipyardDialog(obj);
}

void CPlayerInterface::requestReturningToMainMenu()
{
	sendCustomEvent(RETURN_TO_MAIN_MENU);
}

void CPlayerInterface::requestStoppingClient()
{
	sendCustomEvent(STOP_CLIENT);
}

void CPlayerInterface::sendCustomEvent( int code )
{
	SDL_Event event;
	event.type = SDL_USEREVENT;
	event.user.code = code;
	SDL_PushEvent(&event);
}

void CPlayerInterface::stackChagedCount(const StackLocation &location, const TQuantity &change, bool isAbsolute)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	garrisonChanged(location.army);
}

void CPlayerInterface::stackChangedType(const StackLocation &location, const CCreature &newType)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	garrisonChanged(location.army);
}

void CPlayerInterface::stacksErased(const StackLocation &location)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	garrisonChanged(location.army);
}

void CPlayerInterface::stacksSwapped(const StackLocation &loc1, const StackLocation &loc2)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	garrisonChanged(loc1.army);
	if(loc2.army != loc1.army)
		garrisonChanged(loc2.army);
}

void CPlayerInterface::newStackInserted(const StackLocation &location, const CStackInstance &stack)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	garrisonChanged(location.army);
}

void CPlayerInterface::stacksRebalanced(const StackLocation &src, const StackLocation &dst, TQuantity count)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;

	garrisonChanged(src.army);
	if(dst.army != src.army)
		garrisonChanged(dst.army);
}
#undef UPDATE_IF

void CPlayerInterface::artifactPut(const ArtifactLocation &al)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
}

void CPlayerInterface::artifactRemoved(const ArtifactLocation &al)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BOOST_FOREACH(IShowActivatable *isa, GH.listInt)
	{
		auto artWin = dynamic_cast<CArtifactHolder*>(isa);
		if(artWin)
			artWin->artifactRemoved(al);
	}
}

void CPlayerInterface::artifactMoved(const ArtifactLocation &src, const ArtifactLocation &dst)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BOOST_FOREACH(IShowActivatable *isa, GH.listInt)
	{
		auto artWin = dynamic_cast<CArtifactHolder*>(isa);
		if(artWin)
			artWin->artifactMoved(src, dst);
	}
}

void CPlayerInterface::artifactAssembled(const ArtifactLocation &al)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BOOST_FOREACH(IShowActivatable *isa, GH.listInt)
	{
		auto artWin = dynamic_cast<CArtifactHolder*>(isa);
		if(artWin)
			artWin->artifactAssembled(al);
	}
}

void CPlayerInterface::artifactDisassembled(const ArtifactLocation &al)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	BOOST_FOREACH(IShowActivatable *isa, GH.listInt)
	{
		auto artWin = dynamic_cast<CArtifactHolder*>(isa);
		if(artWin)
			artWin->artifactDisassembled(al);
	}
}

void CPlayerInterface::playerStartsTurn(ui8 player)
{
	EVENT_HANDLER_CALLED_BY_CLIENT;
	if(!GH.listInt.size())
	{
		GH.pushInt(adventureInt);
	}
	if(howManyPeople == 1)
	{
		GH.curInt = this;
		adventureInt->startTurn();
	}
	if(player != playerID && this == LOCPLINT)
	{
		waitWhileDialog();
		adventureInt->aiTurnStarted();
	}
}

void CPlayerInterface::waitForAllDialogs(bool unlockPim /*= true*/)
{
	while(dialogs.size())
	{
		auto unlock = vstd::makeUnlockGuardIf(*pim, unlockPim);
		SDL_Delay(5);
	}
	waitWhileDialog(unlockPim);
}

void CPlayerInterface::proposeLoadingGame()
{
	showYesNoDialog(CGI->generaltexth->allTexts[68], [this] { sendCustomEvent(RETURN_TO_MENU_LOAD); }, 0, false);
}

CPlayerInterface::SpellbookLastSetting::SpellbookLastSetting()
{
	spellbookLastPageBattle = spellbokLastPageAdvmap = 0;
	spellbookLastTabBattle = spellbookLastTabAdvmap = 4;
}
