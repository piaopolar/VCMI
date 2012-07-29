#pragma  once

#include "Connection.h"
#include "NetPacks.h"
#include "VCMI_Lib.h"
#include "CArtHandler.h"
#include "CObjectHandler.h"
#include "CGameState.h"
#include "CHeroHandler.h"
#include "CTownHandler.h"

/*
 * RegisterTypes.h, part of VCMI engine
 *
 * Authors: listed in file AUTHORS in main folder
 *
 * License: GNU General Public License v2.0 or later
 * Full text of license available in license.txt file, in main folder
 *
 */

template<typename Serializer> 
void registerTypes1(Serializer &s)
{
	//map objects
	s.template registerType<CGHeroPlaceholder>();
	s.template registerType<CGHeroInstance>();
	s.template registerType<CGTownInstance>();
	s.template registerType<CTownBonus>();
	s.template registerType<CGPandoraBox>();
	s.template registerType<CGEvent>();
	s.template registerType<CGDwelling>();
	s.template registerType<CGVisitableOPH>();
	s.template registerType<CGVisitableOPW>();
	s.template registerType<CGTeleport>();
	s.template registerType<CGPickable>();
	s.template registerType<CGCreature>();
	s.template registerType<CGSignBottle>();
	s.template registerType<CQuest>();
	s.template registerType<CGSeerHut>();
	s.template registerType<CGQuestGuard>();
	s.template registerType<CGWitchHut>();
	s.template registerType<CGScholar>();
	s.template registerType<CGGarrison>();
	s.template registerType<CGArtifact>();
	s.template registerType<CGResource>();
	s.template registerType<CGMine>();
	s.template registerType<CGShrine>();
	s.template registerType<CGBonusingObject>();
	s.template registerType<CGMagicSpring>();
	s.template registerType<CGMagicWell>();
	s.template registerType<CGObservatory>();
	s.template registerType<CGKeys>();
	s.template registerType<CGKeymasterTent>();
	s.template registerType<CGBorderGuard>();
	s.template registerType<CGBoat>();
	s.template registerType<CGMagi>();
	s.template registerType<CGSirens>();
	s.template registerType<CGOnceVisitable>();
	s.template registerType<CBank>();
	s.template registerType<CGPyramid>();
	s.template registerType<CGShipyard>();
	s.template registerType<CCartographer>();
	s.template registerType<CGObjectInstance>();
	s.template registerType<COPWBonus>();
	s.template registerType<CGDenOfthieves>();
	s.template registerType<CGObelisk>();
	s.template registerType<CGLighthouse>();
	s.template registerType<CGMarket>();
	s.template registerType<CGBlackMarket>();
	s.template registerType<CGUniversity>();
	//end of objects
	s.template registerType<IPropagator>();
	s.template registerType<CPropagatorNodeType>();

	s.template registerType<ILimiter>();
	s.template registerType<CCreatureTypeLimiter>();
	s.template registerType<HasAnotherBonusLimiter>();
	s.template registerType<CreatureNativeTerrainLimiter>();
	s.template registerType<CreatureFactionLimiter>();
	s.template registerType<CreatureAlignmentLimiter>();
	s.template registerType<RankRangeLimiter>();
	s.template registerType<StackOwnerLimiter>();

	s.template registerType<CBonusSystemNode>();
	s.template registerType<CArtifact>();
	s.template registerType<CGrowingArtifact>();
	s.template registerType<CCreature>();
	s.template registerType<CStackInstance>();
	s.template registerType<CCommanderInstance>();
	s.template registerType<PlayerState>();
	s.template registerType<TeamState>();
	s.template registerType<CGameState>();
	s.template registerType<CGHeroInstance::HeroSpecial>();
	s.template registerType<CArmedInstance>();
	s.template registerType<CStack>();
	s.template registerType<BattleInfo>();
	s.template registerType<QuestInfo>();
	s.template registerType<CArtifactInstance>();
	s.template registerType<CCombinedArtifactInstance>();

	s.template registerType<CObstacleInstance>();
	s.template registerType<MoatObstacle>();
	s.template registerType<SpellCreatedObstacle>();

	//s.template registerType<CCreatureArtifactInstance>();
	//s.template registerType<ArtSlotInfo>();
	//s.template registerType<ArtifactLocation>();
	//s.template registerType<StackLocation>();
}

template<typename Serializer> 
void registerTypes2(Serializer &s)
{
	s.template registerType<PackageApplied>();
	s.template registerType<SystemMessage>();
	s.template registerType<PlayerBlocked>();
	s.template registerType<YourTurn>();
	s.template registerType<SetResource>();
	s.template registerType<SetResources>();
	s.template registerType<SetPrimSkill>();
	s.template registerType<SetSecSkill>();
	s.template registerType<HeroVisitCastle>();
	s.template registerType<ChangeSpells>();
	s.template registerType<SetMana>();
	s.template registerType<SetMovePoints>();
	s.template registerType<FoWChange>();
	s.template registerType<SetAvailableHeroes>();
	s.template registerType<GiveBonus>();
	s.template registerType<ChangeObjPos>();
	s.template registerType<PlayerEndsGame>();
	s.template registerType<RemoveBonus>();
	s.template registerType<UpdateCampaignState>();
	s.template registerType<RemoveObject>();
	s.template registerType<TryMoveHero>();
	//s.template registerType<SetGarrisons>();
	s.template registerType<NewStructures>();
	s.template registerType<RazeStructures>();
	s.template registerType<SetAvailableCreatures>();
	s.template registerType<SetHeroesInTown>();
	//s.template registerType<SetHeroArtifacts>();
	s.template registerType<HeroRecruited>();
	s.template registerType<GiveHero>();
	s.template registerType<NewTurn>();
	s.template registerType<InfoWindow>();
	s.template registerType<SetObjectProperty>();
	s.template registerType<SetHoverName>();
	s.template registerType<HeroLevelUp>();
	s.template registerType<CommanderLevelUp>();
	s.template registerType<SetCommanderProperty>();
	s.template registerType<BlockingDialog>();
	s.template registerType<GarrisonDialog>();
	s.template registerType<BattleStart>();
	s.template registerType<BattleNextRound>();
	s.template registerType<BattleSetActiveStack>();
	s.template registerType<BattleResult>();
	s.template registerType<BattleStackMoved>();
	s.template registerType<BattleStackAttacked>();
	s.template registerType<BattleAttack>();
	s.template registerType<StartAction>();
	s.template registerType<EndAction>();
	s.template registerType<BattleSpellCast>();
	s.template registerType<SetStackEffect>();
	s.template registerType<BattleTriggerEffect>();
	s.template registerType<BattleObstaclePlaced>();
	s.template registerType<BattleSetStackProperty>();
	s.template registerType<StacksInjured>();
	s.template registerType<BattleResultsApplied>();
	s.template registerType<StacksHealedOrResurrected>();
	s.template registerType<ObstaclesRemoved>();
	s.template registerType<CatapultAttack>();
	s.template registerType<BattleStacksRemoved>();
	s.template registerType<BattleStackAdded>();
	s.template registerType<ShowInInfobox>();
	s.template registerType<AdvmapSpellCast>();
	s.template registerType<OpenWindow>();
	s.template registerType<NewObject>();
	s.template registerType<NewArtifact>();
	s.template registerType<AddQuest>();
	s.template registerType<ChangeStackCount>();
	s.template registerType<SetStackType>();
	s.template registerType<EraseStack>();
	s.template registerType<SwapStacks>();
	s.template registerType<InsertNewStack>();
	s.template registerType<RebalanceStacks>();
	s.template registerType<SetAvailableArtifacts>();
	s.template registerType<PutArtifact>();
	s.template registerType<EraseArtifact>();
	s.template registerType<MoveArtifact>();
	s.template registerType<AssembledArtifact>();
	s.template registerType<DisassembledArtifact>();
	s.template registerType<HeroVisit>();

	s.template registerType<SaveGame>();
	s.template registerType<SetSelection>();
	s.template registerType<PlayerMessage>();
	s.template registerType<CenterView>();
}

template<typename Serializer>
void registerTypes3(Serializer &s)
{
	s.template registerType<CloseServer>();
	s.template registerType<EndTurn>();
	s.template registerType<DismissHero>();
	s.template registerType<MoveHero>();
	s.template registerType<ArrangeStacks>();
	s.template registerType<DisbandCreature>();
	s.template registerType<BuildStructure>();
	s.template registerType<RecruitCreatures>();
	s.template registerType<UpgradeCreature>();
	s.template registerType<GarrisonHeroSwap>();
	s.template registerType<ExchangeArtifacts>();
	s.template registerType<AssembleArtifacts>();
	s.template registerType<BuyArtifact>();
	s.template registerType<TradeOnMarketplace>();
	s.template registerType<SetFormation>();
	s.template registerType<HireHero>();
	s.template registerType<BuildBoat>();
	s.template registerType<QueryReply>();
	s.template registerType<MakeAction>();
	s.template registerType<MakeCustomAction>();
	s.template registerType<DigWithHero>();
	s.template registerType<CastAdvSpell>();
	s.template registerType<CastleTeleportHero>();

	s.template registerType<SaveGame>();
	s.template registerType<CommitPackage>();
	s.template registerType<SetSelection>();
	s.template registerType<PlayerMessage>();
}

template<typename Serializer>
void registerTypes4(Serializer &s)
{
	s.template registerType<ChatMessage>();
	s.template registerType<QuitMenuWithoutStarting>();
	s.template registerType<PlayerJoined>();
	s.template registerType<SelectMap>();
	s.template registerType<UpdateStartOptions>();
	s.template registerType<PregameGuiAction>();
	s.template registerType<RequestOptionsChange>();
	s.template registerType<PlayerLeft>();
	s.template registerType<PlayersNames>();
	s.template registerType<StartWithCurrentSettings>();
}

template<typename Serializer>
void registerTypes(Serializer &s)
{
	registerTypes1(s);
	registerTypes2(s);
	registerTypes3(s);
	registerTypes4(s);
}