#include "StdInc.h"
#include "NetPacks.h"

#include "CGeneralTextHandler.h"
#include "CDefObjInfoHandler.h"
#include "CArtHandler.h"
#include "CHeroHandler.h"
#include "CObjectHandler.h"
#include "VCMI_Lib.h"
#include "map.h"
#include "CSpellHandler.h"
#include "CCreatureHandler.h"
#include "CGameState.h"
#include "BattleState.h"

#undef min
#undef max

/*
 * NetPacksLib.cpp, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

#ifdef min
#undef min
#endif
#ifdef max
#undef max
#endif

DLL_LINKAGE void SetResource::applyGs( CGameState *gs )
{
	assert(player < GameConstants::PLAYER_LIMIT);
	vstd::amax(val, 0); //new value must be >= 0
	gs->getPlayer(player)->resources[resid] = val;
}

DLL_LINKAGE void SetResources::applyGs( CGameState *gs )
{
	assert(player < GameConstants::PLAYER_LIMIT);
	gs->getPlayer(player)->resources = res;
}

DLL_LINKAGE void SetPrimSkill::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(id);
	assert(hero);

	if(which <4)
	{
		Bonus *skill = hero->getBonus(Selector::type(Bonus::PRIMARY_SKILL) && Selector::subtype(which) && Selector::sourceType(Bonus::HERO_BASE_SKILL));
		assert(skill);
		
		if(abs)
			skill->val = val;
		else
			skill->val += val;
	}
	else if(which == 4) //XP
	{
		if(abs)
			hero->exp = val;
		else
			hero->exp += val;
	}
}

DLL_LINKAGE void SetSecSkill::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(id);
	hero->setSecSkillLevel(static_cast<CGHeroInstance::SecondarySkill>(which), val, abs);
}

DLL_LINKAGE void SetCommanderProperty::applyGs(CGameState *gs)
{
	CCommanderInstance * commander = gs->getHero(heroid)->commander;
	assert (commander);

	switch (which)
	{
		case BONUS:
			commander->accumulateBonus (accumulatedBonus);
			break;
		case SPECIAL_SKILL:
			commander->accumulateBonus (accumulatedBonus);
			commander->specialSKills.insert (additionalInfo);
			break;
		case SECONDARY_SKILL:
			commander->secondarySkills[additionalInfo] = amount;
			break;
		case ALIVE:
			if (amount)
				commander->setAlive(true);
			else
				commander->setAlive(false);
			break;
		case EXPERIENCE:
			commander->giveStackExp (amount); //TODO: allow setting exp for stacks via netpacks
			break;
	}
}

DLL_LINKAGE void AddQuest::applyGs(CGameState *gs)
{
	assert (vstd::contains(gs->players, player));
	auto vec = &gs->players[player].quests;
	if (!vstd::contains(*vec, quest))
		vec->push_back (quest);
	else
		tlog2 << "Warning! Attempt to add duplicated quest\n";
}

DLL_LINKAGE void HeroVisitCastle::applyGs( CGameState *gs )
{
	CGHeroInstance *h = gs->getHero(hid);
	CGTownInstance *t = gs->getTown(tid);

	if(start())
		t->setVisitingHero(h);
	else
		t->setVisitingHero(NULL);
}

DLL_LINKAGE void ChangeSpells::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(hid);

	if(learn)
		BOOST_FOREACH(ui32 sid, spells)
			hero->spells.insert(sid);
	else
		BOOST_FOREACH(ui32 sid, spells)
			hero->spells.erase(sid);
}

DLL_LINKAGE void SetMana::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(hid);
	vstd::amax(val, 0); //not less than 0
	hero->mana = val;
}

DLL_LINKAGE void SetMovePoints::applyGs( CGameState *gs )
{
	CGHeroInstance *hero = gs->getHero(hid);
	hero->movement = val;
}

DLL_LINKAGE void FoWChange::applyGs( CGameState *gs )
{
	TeamState * team = gs->getPlayerTeam(player);
	BOOST_FOREACH(int3 t, tiles)
		team->fogOfWarMap[t.x][t.y][t.z] = mode;
	if (mode == 0) //do not hide too much
	{
		boost::unordered_set<int3, ShashInt3> tilesRevealed;
		for (size_t i = 0; i < gs->map->objects.size(); i++)
		{
			if (gs->map->objects[i])
			{
				switch(gs->map->objects[i]->ID)
				{
				case 34://hero
				case 53://mine
				case 98://town
				case 220:
					if(vstd::contains(team->players, gs->map->objects[i]->tempOwner)) //check owned observators
						gs->map->objects[i]->getSightTiles(tilesRevealed);
					break;
				}
			}
		}
		BOOST_FOREACH(int3 t, tilesRevealed) //probably not the most optimal solution ever
			team->fogOfWarMap[t.x][t.y][t.z] = 1;
	}
}
DLL_LINKAGE void SetAvailableHeroes::applyGs( CGameState *gs )
{
	PlayerState *p = gs->getPlayer(player);
	p->availableHeroes.clear();

	for (int i = 0; i < GameConstants::AVAILABLE_HEROES_PER_PLAYER; i++)
	{
		CGHeroInstance *h = (hid[i]>=0 ?  (CGHeroInstance*)gs->hpool.heroesPool[hid[i]] : NULL);
		if(h && army[i])
			h->setToArmy(army[i]);
		p->availableHeroes.push_back(h);
	}
}

DLL_LINKAGE void GiveBonus::applyGs( CGameState *gs )
{
	CBonusSystemNode *cbsn = NULL;
	switch(who)
	{
	case HERO:
		cbsn = gs->getHero(id);
		break;
	case PLAYER:
		cbsn = gs->getPlayer(id);
		break;
	case TOWN:
		cbsn = gs->getTown(id);
		break;
	}

	assert(cbsn);

	Bonus *b = new Bonus(bonus);
	cbsn->addNewBonus(b);

	std::string &descr = b->description;

	if(!bdescr.message.size() 
		&& bonus.source == Bonus::OBJECT 
		&& (bonus.type == Bonus::LUCK || bonus.type == Bonus::MORALE)
		&& gs->map->objects[bonus.sid]->ID == GameConstants::EVENTI_TYPE) //it's morale/luck bonus from an event without description
	{
		descr = VLC->generaltexth->arraytxt[bonus.val > 0 ? 110 : 109]; //+/-%d Temporary until next battle"
		boost::replace_first(descr,"%d",boost::lexical_cast<std::string>(std::abs(bonus.val)));
	}
	else
	{
		bdescr.toString(descr);
	}
}

DLL_LINKAGE void ChangeObjPos::applyGs( CGameState *gs )
{
	CGObjectInstance *obj = gs->map->objects[objid];
	if(!obj)
	{
		tlog1 << "Wrong ChangeObjPos: object " << objid << " doesn't exist!\n";
		return;
	}
	gs->map->removeBlockVisTiles(obj);
	obj->pos = nPos;
	gs->map->addBlockVisTiles(obj);
}

DLL_LINKAGE void PlayerEndsGame::applyGs( CGameState *gs )
{
	PlayerState *p = gs->getPlayer(player);
	p->status = victory ? 2 : 1;
}

DLL_LINKAGE void RemoveBonus::applyGs( CGameState *gs )
{
	CBonusSystemNode *node;
	if (who == HERO)
		node = gs->getHero(whoID);
	else
		node = gs->getPlayer(whoID);

	BonusList &bonuses = node->getBonusList();
	
	for (int i = 0; i < bonuses.size(); i++)
	{
		Bonus *b = bonuses[i];
		if(b->source == source && b->sid == id)
		{
			bonus = *b; //backup bonus (to show to interfaces later)
			bonuses.erase(i);
			break;
		}
	}
}

DLL_LINKAGE void RemoveObject::applyGs( CGameState *gs )
{
	CGObjectInstance *obj = gs->map->objects[id];
	//unblock tiles
	if(obj->defInfo)
	{
		gs->map->removeBlockVisTiles(obj);
	}

	if(obj->ID==GameConstants::HEROI_TYPE)
	{
		CGHeroInstance *h = static_cast<CGHeroInstance*>(obj);
		PlayerState *p = gs->getPlayer(h->tempOwner);
		gs->map->heroes -= h;
		p->heroes -= h;
		h->detachFrom(h->whereShouldBeAttached(gs));
		h->tempOwner = 255; //no one owns beaten hero

		if(h->visitedTown)
		{
			if(h->inTownGarrison)
				h->visitedTown->garrisonHero = NULL;
			else
				h->visitedTown->visitingHero = NULL;
			h->visitedTown = NULL;
		}

		//return hero to the pool, so he may reappear in tavern
		gs->hpool.heroesPool[h->subID] = h;
		if(!vstd::contains(gs->hpool.pavailable, h->subID))
			gs->hpool.pavailable[h->subID] = 0xff;

		gs->map->objects[id] = NULL;
		

		return;
	}

	auto quest = dynamic_cast<const CQuest *>(obj);
	if (quest)
	{
		gs->map->quests[quest->qid] = NULL;
		BOOST_FOREACH (auto &player, gs->players)
		{
			BOOST_FOREACH (auto &q, player.second.quests)
			{
				if (q.obj == obj)
				{
					q.quest = NULL; //remove entries related to quest guards?
					q.obj = NULL;
				}
			}
		}
		//gs->map->quests[quest->qid].dellNull();
	}

	gs->map->objects[id].dellNull();
}

static int getDir(int3 src, int3 dst)
{
	int ret = -1;
	if(dst.x+1 == src.x && dst.y+1 == src.y) //tl
	{
		ret = 1;
	}
	else if(dst.x == src.x && dst.y+1 == src.y) //t
	{
		ret = 2;
	}
	else if(dst.x-1 == src.x && dst.y+1 == src.y) //tr
	{
		ret = 3;
	}
	else if(dst.x-1 == src.x && dst.y == src.y) //r
	{
		ret = 4;
	}
	else if(dst.x-1 == src.x && dst.y-1 == src.y) //br
	{
		ret = 5;
	}
	else if(dst.x == src.x && dst.y-1 == src.y) //b
	{
		ret = 6;
	}
	else if(dst.x+1 == src.x && dst.y-1 == src.y) //bl
	{
		ret = 7;
	}
	else if(dst.x+1 == src.x && dst.y == src.y) //l
	{
		ret = 8;
	}
	return ret;
}

void TryMoveHero::applyGs( CGameState *gs )
{
	CGHeroInstance *h = gs->getHero(id);
	h->movement = movePoints;

	if((result == SUCCESS || result == BLOCKING_VISIT || result == EMBARK || result == DISEMBARK) && start != end) {
		h->moveDir = getDir(start,end);
	}

	if(result == EMBARK) //hero enters boat at dest tile
	{
		const TerrainTile &tt = gs->map->getTile(CGHeroInstance::convertPosition(end, false));
		assert(tt.visitableObjects.size() >= 1  &&  tt.visitableObjects.back()->ID == 8); //the only vis obj at dest is Boat
		CGBoat *boat = static_cast<CGBoat*>(tt.visitableObjects.back());

		gs->map->removeBlockVisTiles(boat); //hero blockvis mask will be used, we don't need to duplicate it with boat
		h->boat = boat;
		boat->hero = h;
	}
	else if(result == DISEMBARK) //hero leaves boat to dest tile
	{
		CGBoat *b = const_cast<CGBoat *>(h->boat);
		b->direction = h->moveDir;
		b->pos = start;
		b->hero = NULL;
		gs->map->addBlockVisTiles(b);
		h->boat = NULL;
	}

	if(start!=end && (result == SUCCESS || result == TELEPORTATION || result == EMBARK || result == DISEMBARK))
	{
		gs->map->removeBlockVisTiles(h);
		h->pos = end;
		if(CGBoat *b = const_cast<CGBoat *>(h->boat))
			b->pos = end;
		gs->map->addBlockVisTiles(h);
	}

	BOOST_FOREACH(int3 t, fowRevealed)
		gs->getPlayerTeam(h->getOwner())->fogOfWarMap[t.x][t.y][t.z] = 1;
}

DLL_LINKAGE void NewStructures::applyGs( CGameState *gs )
{
	CGTownInstance *t = gs->getTown(tid);
	BOOST_FOREACH(si32 id,bid)
	{
		t->builtBuildings.insert(id);
	}
	t->builded = builded;
	t->recreateBuildingsBonuses();
}
DLL_LINKAGE void RazeStructures::applyGs( CGameState *gs )
{
	CGTownInstance *t = gs->getTown(tid);
	BOOST_FOREACH(si32 id,bid)
	{
		t->builtBuildings.erase(id);
	}
	t->destroyed = destroyed; //yeaha
	t->recreateBuildingsBonuses();
}

DLL_LINKAGE void SetAvailableCreatures::applyGs( CGameState *gs )
{
	CGDwelling *dw = dynamic_cast<CGDwelling*>(gs->map->objects[tid].get());
	assert(dw);
	dw->creatures = creatures;
}

DLL_LINKAGE void SetHeroesInTown::applyGs( CGameState *gs )
{
	CGTownInstance *t = gs->getTown(tid);

	CGHeroInstance *v  = gs->getHero(visiting), 
		*g = gs->getHero(garrison);

	bool newVisitorComesFromGarrison = v && v == t->garrisonHero;
	bool newGarrisonComesFromVisiting = g && g == t->visitingHero;

	if(newVisitorComesFromGarrison)
		t->setGarrisonedHero(NULL);
	if(newGarrisonComesFromVisiting)
		t->setVisitingHero(NULL);
	if(!newGarrisonComesFromVisiting || v)
		t->setVisitingHero(v);
	if(!newVisitorComesFromGarrison || g)
		t->setGarrisonedHero(g);

	if(v)
	{
		gs->map->addBlockVisTiles(v);
	}
	if(g)
	{
		gs->map->removeBlockVisTiles(g);
	}
}

DLL_LINKAGE void HeroRecruited::applyGs( CGameState *gs )
{
	assert(vstd::contains(gs->hpool.heroesPool, hid));
	CGHeroInstance *h = gs->hpool.heroesPool[hid];
	CGTownInstance *t = gs->getTown(tid);
	PlayerState *p = gs->getPlayer(player);
	h->setOwner(player);
	h->pos = tile;
	h->movement =  h->maxMovePoints(true);

	gs->hpool.heroesPool.erase(hid);
	if(h->id < 0)
	{
		h->id = gs->map->objects.size();
		gs->map->objects.push_back(h);
	}
	else
		gs->map->objects[h->id] = h;

	h->initHeroDefInfo();
	gs->map->heroes.push_back(h);
	p->heroes.push_back(h);
	h->attachTo(p);
	h->initObj();
	gs->map->addBlockVisTiles(h);

	if(t)
	{
		t->setVisitingHero(h);
	}
}

DLL_LINKAGE void GiveHero::applyGs( CGameState *gs )
{
	CGHeroInstance *h = gs->getHero(id);

	//bonus system
	h->detachFrom(&gs->globalEffects);
	h->attachTo(gs->getPlayer(player));

	gs->map->removeBlockVisTiles(h,true);
	h->setOwner(player);
	h->movement =  h->maxMovePoints(true);
	h->initHeroDefInfo();
	gs->map->heroes.push_back(h);
	gs->getPlayer(h->getOwner())->heroes.push_back(h);
	gs->map->addBlockVisTiles(h);
	h->inTownGarrison = false;
}

DLL_LINKAGE void NewObject::applyGs( CGameState *gs )
{
	
	CGObjectInstance *o = NULL;
	switch(ID)
	{
	case 8:
		o = new CGBoat();
		break;
	case 54: //probably more options will be needed
		o = new CGCreature();
		{
			//CStackInstance hlp;
			CGCreature *cre = static_cast<CGCreature*>(o);
			//cre->slots[0] = hlp;
			cre->notGrowingTeam = cre->neverFlees = 0;
			cre->character = 2;
			cre->gainedArtifact = -1;
			cre->identifier = -1;
			cre->addToSlot(0, new CStackInstance(subID, -1)); //add placeholder stack
		}
		break;
	default:
		o = new CGObjectInstance();
		break;
	}
	o->ID = ID;
	o->subID = subID;
	o->pos = pos;
	o->defInfo = VLC->dobjinfo->gobjs[ID][subID];
	id = o->id = gs->map->objects.size();
	o->hoverName = VLC->generaltexth->names[ID];

	switch(ID)
	{
		case 54: //cfreature
			o->defInfo = VLC->dobjinfo->gobjs[ID][subID];
			assert(o->defInfo);
			break;
		case 124://hole
			const TerrainTile &t = gs->map->getTile(pos);
			o->defInfo = VLC->dobjinfo->gobjs[ID][t.tertype];
			assert(o->defInfo);
		break;
	}

	gs->map->objects.push_back(o);
	gs->map->addBlockVisTiles(o);
	o->initObj();
	assert(o->defInfo);
}
DLL_LINKAGE void NewArtifact::applyGs( CGameState *gs )
{
	assert(!vstd::contains(gs->map->artInstances, art));
	gs->map->addNewArtifactInstance(art);

	assert(!art->getParentNodes().size());
	art->setType(art->artType);
	if (CCombinedArtifactInstance* cart = dynamic_cast<CCombinedArtifactInstance*>(art.get()))
		cart->createConstituents();
}

DLL_LINKAGE const CStackInstance * StackLocation::getStack()
{
	if(!army->hasStackAtSlot(slot))
	{
		tlog2 << "Warning: " << army->nodeName() << " dont have a stack at slot " << slot << std::endl;
		return NULL;
	}
	return &army->getStack(slot);
}

struct ObjectRetriever : boost::static_visitor<const CArmedInstance *>
{
	const CArmedInstance * operator()(const ConstTransitivePtr<CGHeroInstance> &h) const
	{
		return h;
	}
	const CArmedInstance * operator()(const ConstTransitivePtr<CStackInstance> &s) const
	{
		return s->armyObj;
	}
};
template <typename T>
struct GetBase : boost::static_visitor<T*>
{
	template <typename TArg>
	T * operator()(TArg &arg) const
	{
		return arg;
	}
};

DLL_LINKAGE const CArmedInstance * ArtifactLocation::relatedObj() const
{
	return boost::apply_visitor(ObjectRetriever(), artHolder);
}

DLL_LINKAGE int ArtifactLocation::owningPlayer() const
{
	auto obj = relatedObj();
	return obj ? obj->tempOwner : GameConstants::NEUTRAL_PLAYER;
}

DLL_LINKAGE CArtifactSet *ArtifactLocation::getHolderArtSet()
{
	return boost::apply_visitor(GetBase<CArtifactSet>(), artHolder);
}

DLL_LINKAGE CBonusSystemNode *ArtifactLocation::getHolderNode()
{
	return boost::apply_visitor(GetBase<CBonusSystemNode>(), artHolder);
}

DLL_LINKAGE const CArtifactInstance *ArtifactLocation::getArt() const
{
	const ArtSlotInfo *s = getSlot();
	if(s && s->artifact)
	{
		if(!s->locked)
			return s->artifact;
		else
		{
			tlog3 << "ArtifactLocation::getArt: That location is locked!\n";
			return NULL;
		}
	}
	return NULL;
}

DLL_LINKAGE const CArtifactSet * ArtifactLocation::getHolderArtSet() const
{
	ArtifactLocation *t = const_cast<ArtifactLocation*>(this);
	return t->getHolderArtSet();
}

DLL_LINKAGE const CBonusSystemNode * ArtifactLocation::getHolderNode() const
{
	ArtifactLocation *t = const_cast<ArtifactLocation*>(this);
	return t->getHolderNode();
}

DLL_LINKAGE CArtifactInstance *ArtifactLocation::getArt()
{
	const ArtifactLocation *t = this;
	return const_cast<CArtifactInstance*>(t->getArt());
}

DLL_LINKAGE const ArtSlotInfo *ArtifactLocation::getSlot() const
{
	return getHolderArtSet()->getSlot(slot);
}

DLL_LINKAGE void ChangeStackCount::applyGs( CGameState *gs )
{
	if(absoluteValue)
		sl.army->setStackCount(sl.slot, count);
	else
		sl.army->changeStackCount(sl.slot, count);
}

DLL_LINKAGE void SetStackType::applyGs( CGameState *gs )
{
	sl.army->setStackType(sl.slot, type);
}

DLL_LINKAGE void EraseStack::applyGs( CGameState *gs )
{
	sl.army->eraseStack(sl.slot);
}

DLL_LINKAGE void SwapStacks::applyGs( CGameState *gs )
{
	CStackInstance *s1 = sl1.army->detachStack(sl1.slot),
		*s2 = sl2.army->detachStack(sl2.slot);

	sl2.army->putStack(sl2.slot, s1);
	sl1.army->putStack(sl1.slot, s2);
}

DLL_LINKAGE void InsertNewStack::applyGs( CGameState *gs )
{
	CStackInstance *s = new CStackInstance(stack.type, stack.count);
	sl.army->putStack(sl.slot, s);
}

DLL_LINKAGE void RebalanceStacks::applyGs( CGameState *gs )
{
	const CCreature *srcType = src.army->getCreature(src.slot);
	TQuantity srcCount = src.army->getStackCount(src.slot);

	if(srcCount == count) //moving whole stack
	{
		if(const CCreature *c = dst.army->getCreature(dst.slot)) //stack at dest -> merge
		{
			assert(c == srcType);
			if (GameConstants::STACK_EXP)
			{
				ui64 totalExp = srcCount * src.army->getStackExperience(src.slot) + dst.army->getStackCount(dst.slot) * dst.army->getStackExperience(dst.slot);
				src.army->eraseStack(src.slot);
				dst.army->changeStackCount(dst.slot, count);
				dst.army->setStackExp(dst.slot, totalExp /(dst.army->getStackCount(dst.slot))); //mean
			}
			else
			{
				src.army->eraseStack(src.slot);
				dst.army->changeStackCount(dst.slot, count);
			}
		}
		else //move stack to an empty slot, no exp change needed
		{
			CStackInstance *stackDetached = src.army->detachStack(src.slot);
			dst.army->putStack(dst.slot, stackDetached);
		}
	}
	else
	{
		if(const CCreature *c = dst.army->getCreature(dst.slot)) //stack at dest -> rebalance
		{
			assert(c == srcType);
			if (GameConstants::STACK_EXP)
			{
				ui64 totalExp = srcCount * src.army->getStackExperience(src.slot) + dst.army->getStackCount(dst.slot) * dst.army->getStackExperience(dst.slot);
				src.army->changeStackCount(src.slot, -count);
				dst.army->changeStackCount(dst.slot, count);
				dst.army->setStackExp(dst.slot, totalExp /(src.army->getStackCount(src.slot) + dst.army->getStackCount(dst.slot))); //mean
			}
			else
			{
				src.army->changeStackCount(src.slot, -count);
				dst.army->changeStackCount(dst.slot, count);
			}
		}
		else //split stack to an empty slot
		{
			src.army->changeStackCount(src.slot, -count);
			dst.army->addToSlot(dst.slot, srcType->idNumber, count, false);
			if (GameConstants::STACK_EXP)
				dst.army->setStackExp(dst.slot, src.army->getStackExperience(src.slot));
		}
	}

	CBonusSystemNode::treeHasChanged();
}

DLL_LINKAGE void PutArtifact::applyGs( CGameState *gs )
{
	assert(art->canBePutAt(al));
	art->putAt(al);
	//al.hero->putArtifact(al.slot, art);
}

DLL_LINKAGE void EraseArtifact::applyGs( CGameState *gs )
{
	CArtifactInstance *a = al.getArt();
	assert(a);
	a->removeFrom(al);
}

DLL_LINKAGE void MoveArtifact::applyGs( CGameState *gs )
{
	CArtifactInstance *a = src.getArt();
	if(dst.slot < GameConstants::BACKPACK_START)
		assert(!dst.getArt());

	a->move(src, dst);

	//TODO what'll happen if Titan's thunder is equiped by pickin git up or the start of game?
	if (a->artType->id == 135 && dst.slot == ArtifactPosition::RIGHT_HAND) //Titan's Thunder creates new spellbook on equip
	{
		auto hPtr = boost::get<ConstTransitivePtr<CGHeroInstance> >(&dst.artHolder);
		if(hPtr)
		{
			CGHeroInstance *h = *hPtr;
			if(h && !h->hasSpellbook())
				gs->giveHeroArtifact(h, 0);
		}
	}
}

DLL_LINKAGE void AssembledArtifact::applyGs( CGameState *gs )
{
	CArtifactSet *artSet = al.getHolderArtSet();
	const CArtifactInstance *transformedArt = al.getArt();
	assert(transformedArt);
	assert(vstd::contains(transformedArt->assemblyPossibilities(artSet), builtArt));

	CCombinedArtifactInstance *combinedArt = new CCombinedArtifactInstance(builtArt);
	gs->map->addNewArtifactInstance(combinedArt);
	//retrieve all constituents
	BOOST_FOREACH(si32 constituentID, *builtArt->constituents)
	{
		int pos = artSet->getArtPos(constituentID);
		assert(pos >= 0);
		CArtifactInstance *constituentInstance = artSet->getArt(pos);

		//move constituent from hero to be part of new, combined artifact
		constituentInstance->removeFrom(al);
		combinedArt->addAsConstituent(constituentInstance, pos);
		if(!vstd::contains(combinedArt->artType->possibleSlots[artSet->bearerType()], al.slot) && vstd::contains(combinedArt->artType->possibleSlots[artSet->bearerType()], pos))
			al.slot = pos;
	}

	//put new combined artifacts
	combinedArt->putAt(al);
}

DLL_LINKAGE void DisassembledArtifact::applyGs( CGameState *gs )
{
	CCombinedArtifactInstance *disassembled = dynamic_cast<CCombinedArtifactInstance*>(al.getArt());
	assert(disassembled);

	std::vector<CCombinedArtifactInstance::ConstituentInfo> constituents = disassembled->constituentsInfo;
	disassembled->removeFrom(al);
	BOOST_FOREACH(CCombinedArtifactInstance::ConstituentInfo &ci, constituents)
	{
		ArtifactLocation constituentLoc = al;
		constituentLoc.slot = (ci.slot >= 0 ? ci.slot : al.slot); //-1 is slot of main constituent -> it'll replace combined artifact in its pos
		disassembled->detachFrom(ci.art);
		ci.art->putAt(constituentLoc);
	}

	gs->map->eraseArtifactInstance(disassembled);
}

DLL_LINKAGE void HeroVisit::applyGs( CGameState *gs )
{
	if(starting)
		gs->ongoingVisits[hero] = obj;
	else
		gs->ongoingVisits.erase(hero);
}

DLL_LINKAGE void SetAvailableArtifacts::applyGs( CGameState *gs )
{
	if(id >= 0)
	{
		if(CGBlackMarket *bm = dynamic_cast<CGBlackMarket*>(gs->map->objects[id].get()))
		{
			bm->artifacts = arts;
		}
		else
		{
			tlog1 << "Wrong black market id!" << std::endl;
		}
	}
	else
	{
		CGTownInstance::merchantArtifacts = arts;
	}
}

DLL_LINKAGE void NewTurn::applyGs( CGameState *gs )
{
	gs->day = day;
	BOOST_FOREACH(NewTurn::Hero h, heroes) //give mana/movement point
	{
		CGHeroInstance *hero = gs->getHero(h.id);
		hero->movement = h.move;
		hero->mana = h.mana;
	}

	for(std::map<ui8, TResources>::iterator i = res.begin(); i != res.end(); i++)
	{
		assert(i->first < GameConstants::PLAYER_LIMIT);
		gs->getPlayer(i->first)->resources = i->second;
	}

	BOOST_FOREACH(SetAvailableCreatures h, cres) //set available creatures in towns
		h.applyGs(gs);

	gs->globalEffects.popBonuses(Bonus::OneDay); //works for children -> all game objs
	if(gs->getDate(1)) //new week
		gs->globalEffects.popBonuses(Bonus::OneWeek); //works for children -> all game objs

	//TODO not really a single root hierarchy, what about bonuses placed elsewhere? [not an issue with H3 mechanics but in the future...]

	//count days without town
	for( std::map<ui8, PlayerState>::iterator i=gs->players.begin() ; i!=gs->players.end();i++)
	{
		if(i->second.towns.size() || gs->day == 1)
			i->second.daysWithoutCastle = 0;
		else
			i->second.daysWithoutCastle++;
	}

	BOOST_FOREACH(CGTownInstance* t, gs->map->towns)
		t->builded = 0;
}

DLL_LINKAGE void SetObjectProperty::applyGs( CGameState *gs )
{
	CGObjectInstance *obj = gs->map->objects[id];
	if(!obj)
	{
		tlog1 << "Wrong object ID - property cannot be set!\n";
		return;
	}

	CArmedInstance *cai = dynamic_cast<CArmedInstance *>(obj);
	if(what == ObjProperty::OWNER && cai)
	{
		if(obj->ID == GameConstants::TOWNI_TYPE)
		{
			CGTownInstance *t = static_cast<CGTownInstance*>(obj);
			if(t->tempOwner < GameConstants::PLAYER_LIMIT)
				gs->getPlayer(t->tempOwner)->towns -= t;
			if(val < GameConstants::PLAYER_LIMIT)
				gs->getPlayer(val)->towns.push_back(t);
		}

		CBonusSystemNode *nodeToMove = cai->whatShouldBeAttached();
		nodeToMove->detachFrom(cai->whereShouldBeAttached(gs));
		obj->setProperty(what,val);
		nodeToMove->attachTo(cai->whereShouldBeAttached(gs));
	}
	else //not an armed instance
	{
		obj->setProperty(what,val);
	}
}

DLL_LINKAGE void SetHoverName::applyGs( CGameState *gs )
{
	name.toString(gs->map->objects[id]->hoverName);
}

DLL_LINKAGE void HeroLevelUp::applyGs( CGameState *gs )
{
	CGHeroInstance* h = gs->getHero(heroid);
	h->level = level;
	//speciality
	h->UpdateSpeciality();
}

DLL_LINKAGE void CommanderLevelUp::applyGs (CGameState *gs)
{
	CCommanderInstance * commander = gs->getHero(heroid)->commander;
	assert (commander);
	commander->levelUp();
}

DLL_LINKAGE void BattleStart::applyGs( CGameState *gs )
{
	gs->curB = info;
	gs->curB->localInit();
}

DLL_LINKAGE void BattleNextRound::applyGs( CGameState *gs )
{
	gs->curB->castSpells[0] = gs->curB->castSpells[1] = 0;
	for (int i = 0; i < 2; ++i)
	{
		vstd::amax(--gs->curB->enchanterCounter[i], 0);
	}

	gs->curB->round = round;

	BOOST_FOREACH(CStack *s, gs->curB->stacks)
	{
		s->state -= EBattleStackState::DEFENDING;
		s->state -= EBattleStackState::WAITING;
		s->state -= EBattleStackState::MOVED;
		s->state -= EBattleStackState::HAD_MORALE;
		s->state -= EBattleStackState::FEAR;
		s->state -= EBattleStackState::DRAINED_MANA;
		s->counterAttacks = 1 + s->valOfBonuses(Bonus::ADDITIONAL_RETALIATION);
		// new turn effects
		s->battleTurnPassed();
	}

	BOOST_FOREACH(auto &obst, gs->curB->obstacles)
		obst->battleTurnPassed();
}

DLL_LINKAGE void BattleSetActiveStack::applyGs( CGameState *gs )
{
	gs->curB->activeStack = stack;
	CStack *st = gs->curB->getStack(stack);

	//remove bonuses that last until when stack gets new turn
	st->getBonusList().remove_if(Bonus::UntilGetsTurn);

	if(vstd::contains(st->state,EBattleStackState::MOVED)) //if stack is moving second time this turn it must had a high morale bonus
		st->state.insert(EBattleStackState::HAD_MORALE);
}

DLL_LINKAGE void BattleTriggerEffect::applyGs( CGameState *gs )
{
	CStack *st = gs->curB->getStack(stackID);
	switch (effect)
	{
		case Bonus::HP_REGENERATION:
			st->firstHPleft += val;
			vstd::amin (st->firstHPleft, (ui32)st->MaxHealth());
			break;
		case Bonus::MANA_DRAIN:
		{
			CGHeroInstance * h = gs->getHero(additionalInfo);
			st->state.insert (EBattleStackState::DRAINED_MANA);
			h->mana -= val;
			vstd::amax(h->mana, 0);
			break;
		}
		case Bonus::POISON:
		{
			Bonus * b = st->getBonus(Selector::source(Bonus::SPELL_EFFECT, 71) && Selector::type(Bonus::STACK_HEALTH));
			if (b)
				b->val = val;
			break;
		}
		case Bonus::ENCHANTER:
			break;
		case Bonus::FEAR:
			st->state.insert(EBattleStackState::FEAR);
			break;
		default:
			tlog2 << "Unrecognized trigger effect type "<< type <<"\n"; 
	}
}

DLL_LINKAGE void BattleObstaclePlaced::applyGs( CGameState *gs )
{
	gs->curB->obstacles.push_back(obstacle);
}

void BattleResult::applyGs( CGameState *gs )
{
	BOOST_FOREACH (CStack *s, gs->curB->stacks)
	{
		if (s->base && s->base->armyObj && vstd::contains(s->state, EBattleStackState::SUMMONED))
		{
			//stack with SUMMONED flag but coming from garrison -> most likely resurrected, needs to be removed
			assert(&s->base->armyObj->getStack(s->slot) == s->base);
			const_cast<CArmedInstance*>(s->base->armyObj)->eraseStack(s->slot);
		}
	}
	for (unsigned i = 0; i < gs->curB->stacks.size(); i++)
		delete gs->curB->stacks[i];

	CGHeroInstance *h;
	for (int i = 0; i < 2; ++i)
	{
		h = gs->curB->heroes[i]; 
		if (h)
		{
			h->getBonusList().remove_if(Bonus::OneBattle); 	//remove any "until next battle" bonuses
			if (h->commander && h->commander->alive)
			{
				BOOST_FOREACH (auto art, h->commander->artifactsWorn) //increment bonuses for commander artifacts
				{
					art.second.artifact->artType->levelUpArtifact (art.second.artifact);
				}
			}
		}
	}

	if (GameConstants::STACK_EXP)
	{
		if (exp[0]) //checking local array is easier than dereferencing this crap twice
			gs->curB->belligerents[0]->giveStackExp(exp[0]);
		if (exp[1])
			gs->curB->belligerents[1]->giveStackExp(exp[1]);

		CBonusSystemNode::treeHasChanged();
	}

	gs->curB->belligerents[0]->battle = gs->curB->belligerents[1]->battle = NULL;
	gs->curB.dellNull();
}

void BattleStackMoved::applyGs( CGameState *gs )
{
	CStack *s = gs->curB->getStack(stack);
	BattleHex dest = tilesToMove.back();

	//if unit ended movement on quicksands that were created by enemy, that quicksand patch becomes visible for owner
	BOOST_FOREACH(auto &oi, gs->curB->obstacles)
	{
		if(oi->obstacleType == CObstacleInstance::QUICKSAND
		&& vstd::contains(oi->getAffectedTiles(), tilesToMove.back()))
		{
			SpellCreatedObstacle *sands = dynamic_cast<SpellCreatedObstacle*>(oi.get());
			assert(sands);
			if(sands->casterSide != !s->attackerOwned)
				sands->visibleForAnotherSide = true;
		}
	}
	s->position = dest;
}

DLL_LINKAGE void BattleStackAttacked::applyGs( CGameState *gs )
{
	CStack * at = gs->curB->getStack(stackAttacked);
	at->count = newAmount;
	at->firstHPleft = newHP;

	if(killed())
	{
		at->state -= EBattleStackState::ALIVE;
	}
	//life drain handling
	for (int g=0; g<healedStacks.size(); ++g)
	{
		healedStacks[g].applyGs(gs);
	}
	if (willRebirth())
	{
		at->casts--;
		at->state.insert(EBattleStackState::ALIVE); //hmm?
	}
	if (cloneKilled())
	{
		BattleStacksRemoved bsr; //remove body
		bsr.stackIDs.insert(at->ID);
		bsr.applyGs(gs);
	}
}

DLL_LINKAGE void BattleAttack::applyGs( CGameState *gs )
{
	CStack *attacker = gs->curB->getStack(stackAttacking);
	if(counter())
		attacker->counterAttacks--;
	
	if(shot())
	{
		//don't remove ammo if we have a working ammo cart
		bool hasAmmoCart = false;
		BOOST_FOREACH(const CStack * st, gs->curB->stacks)
		{
			if(st->owner == attacker->owner && st->getCreature()->idNumber == 148 && st->alive())
			{
				hasAmmoCart = true;
				break;
			}
		}

		if (!hasAmmoCart)
		{
			attacker->shots--;
		}
	}
	BOOST_FOREACH(BattleStackAttacked stackAttacked, bsa)
		stackAttacked.applyGs(gs);

	attacker->getBonusList().remove_if(Bonus::UntilAttack);

	for(std::vector<BattleStackAttacked>::const_iterator it = bsa.begin(); it != bsa.end(); ++it)
	{
		CStack * stack = gs->curB->getStack(it->stackAttacked, false);
		if (stack) //cloned stack is already gone
			stack->getBonusList().remove_if(Bonus::UntilBeingAttacked);
	}
}

DLL_LINKAGE void StartAction::applyGs( CGameState *gs )
{
	CStack *st = gs->curB->getStack(ba.stackNumber);

	if(ba.actionType == BattleAction::END_TACTIC_PHASE)
	{
		gs->curB->tacticDistance = 0;
		return;
	}

	if(gs->curB->tacticDistance)
	{
		// moves in tactics phase do not affect creature status
		// (tactics stack queue is managed by client)
		return;
	}

	if(ba.actionType != BattleAction::HERO_SPELL) //don't check for stack if it's custom action by hero
	{
		assert(st);
	}
	else
	{
		gs->curB->usedSpellsHistory[ba.side].push_back(VLC->spellh->spells[ba.additionalInfo]);
	}

	switch(ba.actionType)
	{
	case BattleAction::DEFEND:
		st->state.insert(EBattleStackState::DEFENDING);
		break;
	case BattleAction::WAIT:
		st->state.insert(EBattleStackState::WAITING);
		return;
	case BattleAction::HERO_SPELL: //no change in current stack state
		return;
	default: //any active stack action - attack, catapult, heal, spell...
		st->state.insert(EBattleStackState::MOVED);
		break;
	}

	if(st)
		st->state -= EBattleStackState::WAITING; //if stack was waiting it has made move, so it won't be "waiting" anymore (if the action was WAIT, then we have returned)
}

DLL_LINKAGE void BattleSpellCast::applyGs( CGameState *gs )
{
	assert(gs->curB);
	if (castedByHero)
	{
		CGHeroInstance * h = gs->curB->heroes[side];
		CGHeroInstance * enemy = gs->curB->heroes[1-side];

		h->mana -= spellCost;
			vstd::amax(h->mana, 0);
		if (enemy && manaGained)
			enemy->mana += manaGained;
		if (side < 2)
		{
			gs->curB->castSpells[side]++;
		}
	}

	if(id == 35 || id == 78) //dispel and dispel helpful spells
	{
		bool onlyHelpful = id == 78;
		for(std::set<ui32>::const_iterator it = affectedCres.begin(); it != affectedCres.end(); ++it)
		{
			CStack *s = gs->curB->getStack(*it);
			if(s && !vstd::contains(resisted, s->ID)) //if stack exists and it didn't resist
			{
				if(onlyHelpful)
					s->popBonuses(Selector::positiveSpellEffects);
				else
					s->popBonuses(Selector::sourceType(Bonus::SPELL_EFFECT));
			}
		}
	}
}

void actualizeEffect(CStack * s, const std::vector<Bonus> & ef)
{
	//actualizing features vector

	BOOST_FOREACH(const Bonus &fromEffect, ef)
	{
		BOOST_FOREACH(Bonus *stackBonus, s->getBonusList()) //TODO: optimize
		{
			if(stackBonus->source == Bonus::SPELL_EFFECT && stackBonus->type == fromEffect.type && stackBonus->subtype == fromEffect.subtype)
			{
				stackBonus->turnsRemain = std::max(stackBonus->turnsRemain, fromEffect.turnsRemain);
			}
		}
	}
}
void actualizeEffect(CStack * s, const Bonus & ef)
{
	BOOST_FOREACH(Bonus *stackBonus, s->getBonusList()) //TODO: optimize
	{
		if(stackBonus->source == Bonus::SPELL_EFFECT && stackBonus->type == ef.type && stackBonus->subtype == ef.subtype)
		{
			stackBonus->turnsRemain = std::max(stackBonus->turnsRemain, ef.turnsRemain);
		}
	}
}

DLL_LINKAGE void SetStackEffect::applyGs( CGameState *gs )
{
	int spellid = effect.begin()->sid; //effects' source ID

	BOOST_FOREACH(ui32 id, stacks)
	{
		CStack *s = gs->curB->getStack(id);
		if(s)
		{
			if(spellid == 47 || spellid == 80 || !s->hasBonus(Selector::source(Bonus::SPELL_EFFECT, spellid)))//disrupting ray or acid breath or not on the list - just add	
			{
				BOOST_FOREACH(Bonus &fromEffect, effect)
				{
					s->addNewBonus( new Bonus(fromEffect));
				}
			}
			else //just actualize
			{
				actualizeEffect(s, effect);
			}
		}
		else
			tlog1 << "Cannot find stack " << id << std::endl;
	}
	typedef std::pair<ui32, Bonus> p;
	BOOST_FOREACH(p para, uniqueBonuses)
	{
		CStack *s = gs->curB->getStack(para.first);
		if (s)
		{
			if (!s->hasBonus(Selector::source(Bonus::SPELL_EFFECT, spellid) && Selector::typeSubtype(para.second.type, para.second.subtype)))
				s->addNewBonus(new Bonus(para.second));
			else
				actualizeEffect(s, effect);
		}
		else
			tlog1 << "Cannot find stack " << para.first << std::endl;
	}
}

DLL_LINKAGE void StacksInjured::applyGs( CGameState *gs )
{
	BOOST_FOREACH(BattleStackAttacked stackAttacked, stacks)
		stackAttacked.applyGs(gs);
}

DLL_LINKAGE void StacksHealedOrResurrected::applyGs( CGameState *gs )
{
	for(int g=0; g<healedStacks.size(); ++g)
	{
		CStack * changedStack = gs->curB->getStack(healedStacks[g].stackID, false);

		//checking if we resurrect a stack that is under a living stack
		std::vector<BattleHex> access = gs->curB->getAccessibility(changedStack, true);
		bool acc[GameConstants::BFIELD_SIZE];
		for(int h=0; h<GameConstants::BFIELD_SIZE; ++h)
			acc[h] = false;
		for(int h=0; h<access.size(); ++h)
			acc[access[h]] = true;
		if(!changedStack->alive() && !gs->curB->isAccessible(changedStack->position, acc,
			changedStack->doubleWide(), changedStack->attackerOwned,
			changedStack->hasBonusOfType(Bonus::FLYING), true))
			return; //position is already occupied

		//applying changes
		bool resurrected = !changedStack->alive(); //indicates if stack is resurrected or just healed
		if(resurrected)
		{
			changedStack->state.insert(EBattleStackState::ALIVE);
			if(healedStacks[g].lowLevelResurrection)
				changedStack->state.insert(EBattleStackState::SUMMONED); //TODO: different counter for rised units
		}
		//int missingHPfirst = changedStack->MaxHealth() - changedStack->firstHPleft;
		int res = std::min( healedStacks[g].healedHP / changedStack->MaxHealth() , changedStack->baseAmount - changedStack->count );
		changedStack->count += res;
		changedStack->firstHPleft += healedStacks[g].healedHP - res * changedStack->MaxHealth();
		if(changedStack->firstHPleft > changedStack->MaxHealth())
		{
			changedStack->firstHPleft -= changedStack->MaxHealth();
			if(changedStack->baseAmount > changedStack->count)
			{
				changedStack->count += 1;
			}
		}
		vstd::amin(changedStack->firstHPleft, changedStack->MaxHealth());
		//removal of negative effects
		if(resurrected)
		{
			
// 			for (BonusList::iterator it = changedStack->bonuses.begin(); it != changedStack->bonuses.end(); it++)
// 			{
// 				if(VLC->spellh->spells[(*it)->sid]->positiveness < 0)
// 				{
// 					changedStack->bonuses.erase(it);
// 				}
// 			}
			
			//removing all features from negative spells
			const BonusList tmpFeatures = changedStack->getBonusList();
			//changedStack->bonuses.clear();

			BOOST_FOREACH(Bonus *b, tmpFeatures)
			{
				const CSpell *s = b->sourceSpell();
				if(s && s->isNegative())
				{
					changedStack->removeBonus(b);
				}
			}
		}
	}
}

DLL_LINKAGE void ObstaclesRemoved::applyGs( CGameState *gs )
{
	if(gs->curB) //if there is a battle
	{
		for(std::set<si32>::const_iterator it = obstacles.begin(); it != obstacles.end(); ++it)
		{
			for(int i=0; i<gs->curB->obstacles.size(); ++i)
			{
				if(gs->curB->obstacles[i]->uniqueID == *it) //remove this obstacle
				{
					gs->curB->obstacles.erase(gs->curB->obstacles.begin() + i);
					break;
				}
			}
		}
	}
}

DLL_LINKAGE void CatapultAttack::applyGs( CGameState *gs )
{
	if(gs->curB && gs->curB->siege != 0) //if there is a battle and it's a siege
	{
		for(std::set< std::pair< std::pair< ui8, si16 >, ui8> >::const_iterator it = attackedParts.begin(); it != attackedParts.end(); ++it)
		{
			gs->curB->si.wallState[it->first.first] = 
				std::min( gs->curB->si.wallState[it->first.first] + it->second, 3 );
		}
	}
}

DLL_LINKAGE void BattleStacksRemoved::applyGs( CGameState *gs )
{
	if(!gs->curB)
		return;

	for(std::set<ui32>::const_iterator it = stackIDs.begin(); it != stackIDs.end(); ++it) //for each removed stack
	{
		for(int b=0; b<gs->curB->stacks.size(); ++b) //find it in vector of stacks
		{
			if(gs->curB->stacks[b]->ID == *it) //if found
			{
				CStack *toRemove = gs->curB->stacks[b];
				gs->curB->stacks.erase(gs->curB->stacks.begin() + b); //remove

				toRemove->detachFromAll();
				delete toRemove;
				break;
			}
		}
	}
}

DLL_LINKAGE void BattleStackAdded::applyGs(CGameState *gs)
{
	if (!BattleHex(pos).isValid())
	{
		tlog2 << "No place found for new stack!\n";
		return;
	}

	CStackBasicDescriptor csbd(creID, amount);
	CStack * addedStack = gs->curB->generateNewStack(csbd, attacker, 255, pos); //TODO: netpacks?
	if (summoned)
		addedStack->state.insert(EBattleStackState::SUMMONED);

	gs->curB->localInitStack(addedStack);
	gs->curB->stacks.push_back(addedStack); //the stack is not "SUMMONED", it is permanent
}

DLL_LINKAGE void BattleSetStackProperty::applyGs(CGameState *gs)
{
	CStack * stack = gs->curB->getStack(stackID);
	switch (which)
	{
		case CASTS:
		{
			if (absolute)
				stack->casts = val;
			else
				stack->casts += val;
			vstd::amax(stack->casts, 0);
			break;
		}
		case ENCHANTER_COUNTER:
		{
			int side = gs->curB->whatSide(stack->owner);
			if (absolute)
				gs->curB->enchanterCounter[side] = val;
			else
				gs->curB->enchanterCounter[side] += val;
			vstd::amax(gs->curB->enchanterCounter[side], 0);
			break;
		}
		case UNBIND:
		{
			stack->popBonuses(Selector::type(Bonus::BIND_EFFECT));
			break;
		}
		case CLONED:
		{
			stack->state.insert(EBattleStackState::CLONED);
			break;
		}
	}
}

DLL_LINKAGE void YourTurn::applyGs( CGameState *gs )
{
	gs->currentPlayer = player;
}

DLL_LINKAGE void SetSelection::applyGs( CGameState *gs )
{
	gs->getPlayer(player)->currentSelection = id;
}

DLL_LINKAGE Component::Component(const CStackBasicDescriptor &stack)
	: id(CREATURE), subtype(stack.type->idNumber), val(stack.count), when(0)
{
	type = 2002;
}
