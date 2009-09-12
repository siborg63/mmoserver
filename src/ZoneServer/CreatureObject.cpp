/*
---------------------------------------------------------------------------------------
This source file is part of swgANH (Star Wars Galaxies - A New Hope - Server Emulator)
For more information, see http://www.swganh.org


Copyright (c) 2006 - 2009 The swgANH Team

---------------------------------------------------------------------------------------
*/

#include "MathLib/Quaternion.h"
#include "CreatureObject.h"
#include "EntertainerManager.h"
#include "Vehicle.h"
#include "Buff.h"
#include "WorldManager.h"
#include "WorldConfig.h"
#include "BuildingObject.h"
#include "SpawnPoint.h"
#include "UIManager.h"
#include "MessageLib/MessageLib.h"
#include "ZoneServer/Tutorial.h"
#include "AttackableStaticNpc.h"
#include "AttackableCreature.h"

//=============================================================================

CreatureObject::CreatureObject() : MovingObject(),
mFactionRank(0),
mPvPStatus(CreaturePvPStatus_None),
// mTargetObject(NULL),
mSkillUpdateCounter(0),
mLanguage(1),
mSkillModUpdateCounter(0),
mGroupId(0),
mPerformanceCounter(0),
mPendingPerform(PlayerPerformance_None),
mPerformanceId(0),
mPerformance(NULL),
mLastEntertainerXP(0),
mDefenderUpdateCounter(0),
mEntertainerListenToId(0),
mLastMoveTick(0),
mRaceGenderMask(0),
mIncapCount(0),
mCurrentIncapTime(0),
mFirstIncapTime(0),
mReady(false),
mTargetId(0),
mOwner(0),
mPosture(0),
mState(0),
mCL(1),
// mFaction(1),
mMoodId(0),
mScale(1.0)
{
	mType = ObjType_Creature;

	mSkillMods.reserve(50);
	mSkillCommands.reserve(50);
	mFactionList.reserve(50);
	mDefenders.reserve(10);

	mHam.setParent(this);
	mEquipManager.setParent(this);

	mCurrentAnimation	= "";
	mCustomizationStr	= "";
	mFaction			= "";
	mSpecies			= "";
	mSpeciesGroup		= "";

	mHair = NULL;

	for(uint16 i = 1;i<256;i++)
		mCustomization[i]=0;

	// register event functions
	registerEventFunction(this,&CreatureObject::onIncapRecovery);
}

//=============================================================================

CreatureObject::~CreatureObject()
{
	// delete inventory
	Object* inventory = mEquipManager.getEquippedObject(CreatureEquipSlot_Inventory);
	mEquipManager.removeEquippedObject(CreatureEquipSlot_Inventory);

	delete(inventory);

	// delete hair
	//please note that the equipslot might be occupied if we are wearing a helmet!!!
	//in which case we need the creatureobjectreference to hair to delete it without crashing
	Object* hair = getHair();
	mEquipManager.removeEquippedObject(CreatureEquipSlot_Hair);
	if(hair)
	{
		//hair is not added to the world object list so just delete
		delete(hair); //not everyone has hair
	}

	this->Buffs.clear();
}

//=============================================================================

void CreatureObject::prepareSkillMods()
{
	mSkillMods.clear();

	SkillList::iterator skillIt = mSkills.begin();

	while(skillIt != mSkills.end())
	{
		SkillModsList::iterator smIt = (*skillIt)->mSkillMods.begin();
		SkillModsList::iterator localSmIt;

		while(smIt != (*skillIt)->mSkillMods.end())
		{
			localSmIt = findSkillMod((*smIt).first);

			if(localSmIt != mSkillMods.end())
				(*localSmIt).second += (*smIt).second;
			else
				mSkillMods.push_back((*smIt));

			++smIt;
		}
		++skillIt;
	}
}

//=============================================================================

void CreatureObject::prepareSkillCommands()
{
	mSkillCommands.clear();
	mSkillCommandMap.clear();

	SkillList::iterator skillIt = mSkills.begin();

	while(skillIt != mSkills.end())
	{
		// we dont want race specific skills here
		if((*skillIt)->mSpeciesRequired.size())
		{
			++skillIt;
			continue;
		}

		SkillCommandList::iterator scIt = (*skillIt)->mCommands.begin();
		SkillCommandList::iterator localScIt;

		while(scIt != (*skillIt)->mCommands.end())
		{
			localScIt = std::find(mSkillCommands.begin(),mSkillCommands.end(),(*scIt));

			if(localScIt == mSkillCommands.end())
			{
				mSkillCommands.push_back((*scIt));
				mSkillCommandMap.insert(std::make_pair((gSkillManager->getSkillCmdById(*scIt)).getCrc(),(void*)0));
			}

			++scIt;
		}
		++skillIt;
	}
}

//=============================================================================

bool CreatureObject::verifyAbility(uint32 abilityCRC)
{
	SkillCommandMap::iterator it = mSkillCommandMap.find(abilityCRC);

	if(it != mSkillCommandMap.end())
		return(true);

	return(false);
}

//=============================================================================

SkillModsList::iterator CreatureObject::findSkillMod(uint32 modId)
{
	SkillModsList::iterator it = mSkillMods.begin();

	while(it != mSkillMods.end())
	{
		if(modId == (*it).first)
			return(it);

		++it;
	}
	return(mSkillMods.end());
}

//=============================================================================

int32 CreatureObject::getSkillModValue(uint32 modId)
{
	SkillModsList::iterator it = mSkillMods.begin();

	while(it != mSkillMods.end())
	{
		if(modId == (*it).first)
			return((*it).second);

		++it;
	}

	return(-1000);
}

//=============================================================================

bool CreatureObject::setSkillModValue(uint32 modId,int32 value)
{
	SkillModsList::iterator it = mSkillMods.begin();

	while(it != mSkillMods.end())
	{
		if(modId == (*it).first)
		{
			(*it).second = value;
			return(true);
		}

		++it;
	}

	return(false);
}
bool CreatureObject::modifySkillModValue(uint32 modId,int32 modifier)
{
	SkillModsList::iterator it = mSkillMods.begin();

	while(it != mSkillMods.end())
	{
		if(modId == (*it).first)
		{
			(*it).second += modifier;

			//If we have a player, send mod updates
			PlayerObject* temp = dynamic_cast<PlayerObject*>(this);
			if(temp)
			{
				SkillModsList modlist;
				modlist.push_back(*it);
				//TODO: Skillmods should use a Delta, not a Baseline
				gMessageLib->sendBaselinesCREO_4(temp);
				//gMessageLib->sendSkillModUpdateCreo4(temp);
				//gMessageLib->sendSkillModDeltasCREO_4(modlist, 0, temp, temp);
			}
			return(true);
		}

		++it;
	}

	

	return(false);
}

//=============================================================================

int32 CreatureObject::getFactionPointsByFactionId(uint32 id)
{
	FactionList::iterator it = mFactionList.begin();

	while(it != mFactionList.end())
	{
		if((*it).first == id)
			return((*it).second);

		++it;
	}

	return(-10000);
}

//=============================================================================

bool CreatureObject::updateFactionPoints(uint32 factionId,int32 value)
{
	FactionList::iterator it = mFactionList.begin();

	while(it != mFactionList.end())
	{
		if((*it).first == factionId)
		{
			(*it).second += value;
			return(true);
		}

		++it;
	}

	return(false);
}

//=============================================================================

bool CreatureObject::checkSkill(uint32 skillId)
{
	SkillList::iterator skillIt = mSkills.begin();

	while(skillIt != mSkills.end())
	{
		if((*skillIt)->mId == skillId)
			return(true);
		++skillIt;
	}

	return(false);
}

//=============================================================================

bool CreatureObject::removeSkill(Skill* skill)
{
	SkillList::iterator skillIt = mSkills.begin();

	while(skillIt != mSkills.end())
	{
		if((*skillIt) == skill)
		{
			mSkills.erase(skillIt);
			return(true);
		}
			++skillIt;
	}

	return(false);
}

//=============================================================================

uint32 CreatureObject::getSkillPointsLeft()
{
	SkillList::iterator skillIt		= mSkills.begin();
	uint32 skillPointsLeft			= 250;

	while(skillIt != mSkills.end())
	{
		skillPointsLeft -= (*skillIt)->mSkillPointsRequired;
		++skillIt;
	}

	return(skillPointsLeft);
}

//=============================================================================

bool CreatureObject::handlePerformanceTick(uint64 time,void* ref)
{
	return(gEntertainerManager->handlePerformanceTick(this));
}

//=============================================================================
//
// update current movement properties
//

void CreatureObject::updateMovementProperties()
{
	switch(mPosture)
	{
		case CreaturePosture_KnockedDown:
		case CreaturePosture_Incapacitated:
		case CreaturePosture_Dead:
		case CreaturePosture_Sitting:
		{
			mCurrentRunSpeedLimit		= 0.0f;
			mCurrentAcceleration		= 0.0f;
			mCurrentTurnRate			= 0.0f;
			mCurrentTerrainNegotiation	= 0.0f;
		}
		break;

		case CreaturePosture_Upright:
		{
			mCurrentRunSpeedLimit		= mBaseRunSpeedLimit;
			mCurrentAcceleration		= mBaseAcceleration;
			mCurrentTurnRate			= mBaseTurnRate;
			mCurrentTerrainNegotiation	= mBaseTerrainNegotiation;
		}
		break;

		case CreaturePosture_Prone:
		{
			mCurrentRunSpeedLimit		= 1.0f;
			mCurrentAcceleration		= 0.25f;
			mCurrentTurnRate			= mBaseTurnRate;
			mCurrentTerrainNegotiation	= mBaseTerrainNegotiation;
		}
		break;

		case CreaturePosture_Crouched:
		{
			mCurrentRunSpeedLimit			= 0.0f;
			mCurrentAcceleration		= 0.0f;
			mCurrentTurnRate			= mBaseTurnRate;
			mCurrentTerrainNegotiation	= mBaseTerrainNegotiation;
		}
		break;

		default:
		{
			mCurrentRunSpeedLimit		= mBaseRunSpeedLimit;
			mCurrentAcceleration		= mBaseAcceleration;
			mCurrentTurnRate			= mBaseTurnRate;
			mCurrentTerrainNegotiation	= mBaseTerrainNegotiation;
		}
		break;
	}
}

//=============================================================================
Buff* CreatureObject::GetBuff(uint32 BuffIcon)
{
	//Cycle through all buffs for the creature
	std::vector<Buff*>::iterator it = Buffs.begin();
	//Check if the Icon CRCs Match (ie Duplication)
	while(it != Buffs.end())
	{
		Buff* temp = *it;
		if(temp->GetIcon() == BuffIcon)
		{
			return temp;
		}
		it++;
	}
	return 0;
}
bool CreatureObject::GetBuffExists(uint32 BuffIcon)
{
	//Cycle through all buffs for the creature
	std::vector<Buff*>::iterator it = Buffs.begin();
	while(it != Buffs.end())
	{
		//Check if the Icon CRCs Match (ie Duplication)
		Buff* temp = *it;
		if(temp->GetIcon() == BuffIcon)
		{
			//Check if the duplicate isn't marked for deletion
			if(!temp->GetIsMarkedForDeletion())
			{
				return true;
			}
		}
		it++;
	}
	return false;
}
//=============================================================================
void CreatureObject::AddBuff(Buff* buff,  bool stackable, bool overwrite)
{
	//Use this opportunity to clean up any dead buffs in BuffList
	this->CleanUpBuffs();

	//Check we don't have a null ptr
	if(!buff)
		return;

	buff->SetInit(true);
	//If we already have a buff with this CRC Icon   we want to decide whether we can stack this buff
	//stacking is for example important for filling and food buffs

	//the next thing to be aware of, is that a type of foodbuff can have several effects (plus burstrun minus ham)
	//but has only one icon
	//in this case every effect we send will have the icon associated with it
	if(GetBuffExists(buff->GetIcon()) && !stackable )
	{
		if(overwrite)
		{
			//find old buff

			//apply changes

			//exit

			//for now do nothing
		}
		PlayerObject* player = dynamic_cast<PlayerObject*>(this);
		if(player != 0)
		{
			//gMessageLib->sendSystemMessage(player, "You appear to have attempted to stack Buffs. The server has prevented this");
			gLogger->logMsg("Attempt to duplicate buffs prevented.\n");
		}
		SAFE_DELETE(buff);
		return; 
		//TODO Currently, no overwrites - need to make it so certain buffs replace older ones
		//which will be important for doctor buffs 
	}

	//Set the target to this creature, in case it isn't already
	buff->mTarget = this;

	//Add to Buff List
	Buffs.push_back(buff);

	//Perform Inital Buff Changes
	buff->InitialChanges();

	//Add Buff to Scheduler
	gWorldManager->addBuffToProcess(buff);
}


int CreatureObject::GetNoOfBuffs()
{
	int No =0;
	std::vector<Buff*>::iterator it = Buffs.begin();
	while(it != Buffs.end())
	{
		No++;it++;
	}
	return No;
}
void CreatureObject::RemoveBuff(Buff* buff)
{
	if(!buff)
		return;

	//Cancel the buff so the next Event doesn't do anything
	buff->mCancelled = true;

	//Remove from the Process Queue
	gWorldManager->removeBuffToProcess(buff->GetID());
	buff->SetID(0);

	//Perform any Final changes
	buff->FinalChanges();
}

//================================================
//

void CreatureObject::CleanUpBuffs()
{	
	std::vector<Buff*>::iterator it = Buffs.begin();
	while(it != Buffs.end())
	{
		if(!(*it)) 
		{
			it++;
		} 
		else 
		if((*it)->GetIsMarkedForDeletion()) 
		{
			((Buff*)(*it))->EraseAttributes();
			it = Buffs.erase(it);
		} 
		else 
		{	
			it++;
		}
	}
}

//=============================================================================

void CreatureObject::updateRaceGenderMask(bool female)
{
	// set race flag
	switch(mRaceId)
	{
		case 0:		mRaceGenderMask |= 0x4;		break;
		case 1:		mRaceGenderMask |= 0x8;		break;
		case 2:		mRaceGenderMask |= 0x10;	break;
		case 3:		mRaceGenderMask |= 0x20;	break;
		case 4:		mRaceGenderMask |= 0x40;	break;
		case 5:		mRaceGenderMask |= 0x80;	break;
		case 6:		mRaceGenderMask |= 0x100;	break;
		case 7:		mRaceGenderMask |= 0x200;	break;
		case 33:	mRaceGenderMask |= 0x400;	break;
		case 49:	mRaceGenderMask |= 0x800;	break;

		default: break;
	}

	// set gender flag
	if(female)
	{
		mRaceGenderMask |= 0x1;
	}
	else
	{
		mRaceGenderMask |= 0x2;
	}

	// set jedi flag
	if(PlayerObject* player = dynamic_cast<PlayerObject*>(this))
	{
		if(player->getJediState())
		{
			mRaceGenderMask |= 0x1000;
		}
	}
}

//=============================================================================
//
// incap
//
void CreatureObject::incap()
{
	// sanity check
	if (isIncapacitated() || isDead())
	{
		return;
	}

	if (this->getType() == ObjType_Player)
	{
		// gLogger->logMsgF("Player incapped, mIncapCount = %u.", MSG_NORMAL, mIncapCount);

		// first incap, update the initial time
		uint64 localTime = Anh_Utils::Clock::getSingleton()->getLocalTime();
		if(!mIncapCount)
		{
			mFirstIncapTime = localTime;
		}
		// reset the counter if the reset time has passed
		else if(mIncapCount != 0 && (localTime - mFirstIncapTime) >= gWorldConfig->getIncapResetTime() * 1000)
		{
			// gLogger->logMsgF("Time since first incap = %lld", MSG_NORMAL, localTime - mFirstIncapTime);
			// gLogger->logMsgF("Resetting mFirstIncapTime", MSG_NORMAL);

			mIncapCount = 0;
			mFirstIncapTime = localTime;
		}
		/*
		if (mIncapCount != 0)
		{
			gLogger->logMsgF("Time since first incap = %lld", MSG_NORMAL, localTime - mFirstIncapTime);
		}
		*/

		// advance incaps counter
		if(++mIncapCount < gWorldConfig->getConfiguration("Player_Incapacitation",3))
		{
			// gLogger->logMsgF("Player incapped, mIncapCount set to = %u, setting timer..", MSG_NORMAL, mIncapCount);

			// update the posture
			mPosture = CreaturePosture_Incapacitated;

			// send timer updates
			mCurrentIncapTime = gWorldConfig->getBaseIncapTime() * 1000;
			gMessageLib->sendIncapTimerUpdate(this);

			// schedule recovery event
			mObjectController.addEvent(new IncapRecoveryEvent(),mCurrentIncapTime);
		
			// reset states
			mState = 0;
			
			// reset ham regeneration
			mHam.updateRegenRates();
			gWorldManager->removeCreatureHamToProcess(mHam.getTaskId());
			mHam.setTaskId(0);
			
			updateMovementProperties();

			gMessageLib->sendPostureAndStateUpdate(this);

			if(PlayerObject* player = dynamic_cast<PlayerObject*>(this))
			{
				gMessageLib->sendUpdateMovementProperties(player);
				gMessageLib->sendSelfPostureUpdate(player);
			}
		} 
		// we hit the max -> death
		else
		{
			// gLogger->logMsgF("Player died.", MSG_NORMAL);
			die();
		}
	}
	else if (this->getType() == ObjType_Creature)	// A Creature.
	{
		die();
	}
	else
	{
		gLogger->logMsgF("CreatureObject::incap Incapped unsupported type %u\n", MSG_NORMAL, this->getType());
	}

}

//=============================================================================

void CreatureObject::die()
{
	mIncapCount			= 0;
	mCurrentIncapTime	= 0;
	mFirstIncapTime		= 0;

	// gLogger->logMsg("CreatureObject::die I'm dead");

	gMessageLib->sendIncapTimerUpdate(this);

	if(PlayerObject* player = dynamic_cast<PlayerObject*>(this))
	{
		gMessageLib->sendSystemMessage(player,L"","base_player","victim_dead");
	}

	mPosture = CreaturePosture_Dead;
	
	// reset ham regeneration
	mHam.updateRegenRates();
	gWorldManager->removeCreatureHamToProcess(mHam.getTaskId());
	mHam.setTaskId(0);

	updateMovementProperties();

	// clear states
	mState = 0;

	gMessageLib->sendPostureAndStateUpdate(this);

	if(PlayerObject* player = dynamic_cast<PlayerObject*>(this))
	{
		gMessageLib->sendUpdateMovementProperties(player);
		gMessageLib->sendSelfPostureUpdate(player);

		// update duel lists
		PlayerList::iterator duelIt = player->getDuelList()->begin();

		while(duelIt != player->getDuelList()->end())
		{
			if((*duelIt)->checkDuelList(player))
			{
				PlayerObject* duelPlayer = (*duelIt);

				duelPlayer->removeFromDuelList(player);

				gMessageLib->sendUpdatePvpStatus(player,duelPlayer);
				gMessageLib->sendUpdatePvpStatus(duelPlayer,player);
			}

			++duelIt;
		}

		// update defender lists
		ObjectIDList::iterator defenderIt = mDefenders.begin();
		while (defenderIt != mDefenders.end())
		{
			if (CreatureObject* defenderCreature = dynamic_cast<CreatureObject*>(gWorldManager->getObjectById((*defenderIt))))
			{
				defenderCreature->removeDefenderAndUpdateList(this->getId());

				if (PlayerObject* defenderPlayer = dynamic_cast<PlayerObject*>(defenderCreature))
				{
					gMessageLib->sendUpdatePvpStatus(this,defenderPlayer);
					gMessageLib->sendDefenderUpdate(defenderPlayer,0,0,this->getId());
				}

				// Defender not hostile to me any more.
				gMessageLib->sendUpdatePvpStatus(defenderCreature, player);

				// if no more defenders, clear combat state
				if (!defenderCreature->getDefenders()->size())
				{
					defenderCreature->toggleStateOff((CreatureState)(CreatureState_Combat + CreatureState_CombatAttitudeNormal));
					gMessageLib->sendStateUpdate(defenderCreature);
				}
			}
			// If we remove self from all defenders, then we should remove all defenders from self. Remember, we are dead.
			defenderIt = mDefenders.erase(defenderIt);
		}

		// bring up the clone selection window
		ObjectSet inRangeBuildings;
		BStringVector buildingNames;
		std::vector<BuildingObject*> buildings;
		BuildingObject*	nearestBuilding = NULL;
		BuildingObject* preDesignatedBuilding = NULL;

		// Search for cloning facilities.
		gWorldManager->getSI()->getObjectsInRange(this,&inRangeBuildings,ObjType_Building,8192);

		ObjectSet::iterator buildingIt = inRangeBuildings.begin();
		
		while (buildingIt != inRangeBuildings.end())
		{
			BuildingObject* building = dynamic_cast<BuildingObject*>(*buildingIt);
			if (building)
			{
				// Do we have any personal clone location?
				if (building->getId() == player->getPreDesignatedCloningFacilityId())
				{
					// gLogger->logMsg("Found a pre-designated cloning facility");
					preDesignatedBuilding = building;
				}

				if (building->getBuildingFamily() == BuildingFamily_Cloning_Facility)
				{
					// gLogger->logMsg("Found a cloning facility");
					// TODO: This code is not working as intended if player dies inside, since buildings use world coordinates and players inside have cell coordinates.
					// Tranformation is needed before the correct distance can be calculated.
					if(!nearestBuilding	|| 
					   (nearestBuilding != building && (mPosition.distance2D(building->mPosition) < mPosition.distance2D(nearestBuilding->mPosition))))
					{
						nearestBuilding = building;
					}
				}
			}
			++buildingIt;
		}

		if (nearestBuilding)
		{
			if (nearestBuilding->getSpawnPoints()->size())
			{
				// buildingNames.push_back(nearestBuilding->getRandomSpawnPoint()->mName.getAnsi());
				// buildingNames.push_back("Closest Cloning Facility");
				buildingNames.push_back("@base_player:revive_closest");
				buildings.push_back(nearestBuilding);

				// Save nearest building in case we need to clone when reviev times out.
				player->saveNearestCloningFacility(nearestBuilding);
			}
		}

		if (preDesignatedBuilding)
		{
			if (preDesignatedBuilding->getSpawnPoints()->size())
			{
				// buildingNames.push_back("Pre-Designated Facility");
				buildingNames.push_back("@base_player:revive_bind");
				buildings.push_back(preDesignatedBuilding);
			}
		}

		if (buildings.size())
		{
			// Set up revive timer in case of user don't clone.
			gWorldManager->addPlayerObjectForTimedCloning(player->getId(), 10 * 60 * 1000);	// TODO: Don't use hardcoded value.
			gUIManager->createNewCloneSelectListBox(player,"cloneSelect","Select Clone Destination","Select clone destination",buildingNames,buildings,player);
		}
		else
		{
			gLogger->logMsg("No cloning facility available\n");
		}
	}
	else // if(CreatureObject* creature = dynamic_cast<CreatureObject*>(this))
	{
		// gMessageLib->sendUpdateMovementProperties(player);
		// gMessageLib->sendSelfPostureUpdate(player);

		// Who killed me
		// this->prepareCustomRadialMenu(this,0);

		// 
		// update defender lists
		ObjectIDList::iterator defenderIt = mDefenders.begin();

		while(defenderIt != mDefenders.end())
		{
			// This may be optimized, but rigth now THIS code does the job.
			this->makePeaceWithDefender(*defenderIt);
			defenderIt = mDefenders.begin();
		}

		// I'm dead. De-spawn after "loot-timer" has expired, if any.
		// 3-5 min.
		uint64 timeBeforeDeletion = (180 + (gRandom->getRand() % (int32)120)) * 1000;
		if (this->getCreoGroup() == CreoGroup_AttackableObject)
		{
			// timeBeforeDeletion = 2 * 1000;
			// Almost NOW!
			timeBeforeDeletion = 500;

			// Play the death-animation, if any.
			if (AttackableStaticNpc* staticNpc = dynamic_cast<AttackableStaticNpc*>(this))
			{
				staticNpc->playDeathAnimation();
			}
		}
		else if (this->getCreoGroup() == CreoGroup_Creature)
		{
			if (AttackableCreature* creature = dynamic_cast<AttackableCreature*>(this))
			{
				creature->unequipWeapon();

				// Give XP to the one(s) attacking me.
				// creature->updateAttackersXp();
			}
		}

		if (NPCObject* npc = dynamic_cast<NPCObject*>(this))
		{
			// Give XP to the one(s) attacking me.
			npc->updateAttackersXp();
		}

		// Put this creature in the pool of delayed destruction.
		gWorldManager->addCreatureObjectForTimedDeletion(this->getId(), timeBeforeDeletion);
		// this->killEvent();
	}
}

//=============================================================================

void CreatureObject::addDefender(uint64 defenderId)
{
	ObjectIDList::iterator it = mDefenders.begin();

	while(it != mDefenders.end())
	{
		if((*it) == defenderId)
		{
			gLogger->logMsg("CreatureObject:: defender already added :(\n");
			return;
		}

		++it;
	}

	mDefenders.push_back(defenderId); 
}


//=============================================================================

void CreatureObject::clearDefenders()
{
	if (mDefenders.size())
	{
		mDefenders.clear(); 
	}
	else
		gLogger->logMsg("CreatureObject::clearing defenders albeit empty :(\n");
}

//=============================================================================

/* Obsolete
bool CreatureObject::removeDefender(Object* defender)
{
	ObjectList::iterator it = mDefenders.begin();

	while(it != mDefenders.end())
	{
		if((*it) == defender)
		{
			mDefenders.erase(it);
			//gMessageLib->sendNewDefenderList(this);
			return(true);
		}

		++it;
	}

	return(false);
}
*/
//=============================================================================

bool CreatureObject::removeDefenderAndUpdateList(uint64 defenderId)
{
	// Eruptor

	ObjectIDList::iterator it = mDefenders.begin();
	uint16 index = 0;
	while(it != mDefenders.end())
	{
		if ((*it) == defenderId)
		{
			gMessageLib->sendDefenderUpdate(this,0,index,defenderId);
			mDefenders.erase(it);
			return(true);
		}
		index++;
		++it;
	}

	return(false);
}



//=============================================================================

bool CreatureObject::checkDefenderList(uint64 defenderId)
{
	ObjectIDList::iterator it = mDefenders.begin();

	while(it != mDefenders.end())
	{
		if((*it) == defenderId)
		{
			return(true);
		}

		++it;
	}

	return(false);
}

//=============================================================================

void CreatureObject::buildCustomization(uint16 customization[])
{
	uint8 len = 0x73;
	
	uint8* playerCustomization = new uint8[512];

	uint16 byteCount = 4; // 2 byte header + footer
	uint8 elementCount = 0;

	// get absolute bytecount(1 byte index + value)
	for(uint8 i = 1;i < len;i++) 
	{
		if((customization[i] != 0))
		{
			if(customization[i] < 255)
				byteCount += 2;
			else
				byteCount += 3;

			elementCount++;
		}
	}
	//get additional bytes for female chest
	bool			female = false;
	PlayerObject*	player = dynamic_cast<PlayerObject*>(this);
	
	if(player)
		female = player->getGender();
			
	if(female)
	{
		//please note its 1 index with 2 attribute values!!!!
		byteCount += 1;
		elementCount++;

		for(uint8 i = 171;i < 173;i++) 
		{
			if(customization[i] == 0) 
				customization[i] = 511;
		
			if(customization[i] == 255) 
				customization[i] = 767;

			if((customization[i] < 255)&&(customization[i] > 0))
				byteCount += 1;
			else
				byteCount += 2;
		}
	}

	// elements count
	playerCustomization[0] = 0x01;
	playerCustomization[1] = elementCount;
	
	uint16 j = 2;
	//get additional bytes for female chest
	if(female)
	{
		playerCustomization[j] = 171;
		j += 1;

		for(uint8 i = 171;i < 173;i++)
		{
			if((customization[i] < 255)&(customization[i] > 0))
			{
				playerCustomization[j] = (uint8)(customization[i] & 0xff);
				j += 1;
			}
			else
			{
				playerCustomization[j] = (uint8)(customization[i] & 0xff);
				playerCustomization[j+1] = (uint8)((customization[i] >> 8) & 0xff);
				j += 2;
			}
		
		}
	}

	

	// fill our string
	for(uint8 i = 1;i < len;i++)
	{
		if((customization[i] != 0))
		{
			playerCustomization[j] = i;

			if(customization[i] < 255)
			{
				playerCustomization[j+1] = (uint8)(customization[i] & 0xff);
				j += 2;
			}
			else
			{
				playerCustomization[j+1] = (uint8)(customization[i] & 0xff);
				playerCustomization[j+2] = (uint8)((customization[i] >> 8) & 0xff);
				j += 3;
			}
		}
	}

	// footer
	playerCustomization[j] = 0xff;
	playerCustomization[j+1] = 0x03;
	playerCustomization[j+2] = '\0';

	setCustomizationStr((int8*)playerCustomization);
}


void CreatureObject::makePeaceWithDefender(uint64 defenderId)
{
	// gLogger->logMsgF("CreatureObject::makePeaceWithDefender()", MSG_NORMAL);
	
	// Atempting a forced peace is no good.

	PlayerObject* attackerPlayer = dynamic_cast<PlayerObject*>(this);

	CreatureObject* defenderCreature = dynamic_cast<CreatureObject*>(gWorldManager->getObjectById(defenderId));
	// assert(defenderCreature)
	PlayerObject* defenderPlayer = NULL;
	if (defenderCreature)
	{
		defenderPlayer = dynamic_cast<PlayerObject*>(defenderCreature);
	}

	if (attackerPlayer)
	{
		// gLogger->logMsgF("Attacker is a Player", MSG_NORMAL);
	}
	else
	{
		// gLogger->logMsgF("Attacker is a Npc", MSG_NORMAL);
	}

	if (defenderPlayer)
	{
		if (attackerPlayer)
		{
			// Players do not make peace with other players. 
			return;
		}
	}
	else if (defenderCreature)
	{
		// gLogger->logMsgF("Defender is a Npc", MSG_NORMAL);
	}
	else
	{
		gLogger->logMsgF("Defender is of unknown type...\n", MSG_NORMAL);
		return;
	}
	
	// Remove defender from my list.
	if (defenderCreature)
	{
		this->removeDefenderAndUpdateList(defenderCreature->getId());
	}
	
	if (defenderPlayer)
	{
		// Update defender about attacker pvp status.
		gMessageLib->sendUpdatePvpStatus(this, defenderPlayer);
	}

	if (this->getDefenders()->size() == 0)
	{
		// I have no more defenders.
		// gLogger->logMsgF("Attacker: My last defender", MSG_NORMAL);
		if (attackerPlayer)
		{
			// Update player (my self) with the new status.
			gMessageLib->sendUpdatePvpStatus(this,attackerPlayer);
		}
		else
		{
			// We are a npc, and we have no more defenders (for whatever reason).
			// Inform npc about this event.
			this->inPeace();
		}
		this->toggleStateOff((CreatureState)(CreatureState_Combat + CreatureState_CombatAttitudeNormal));
		gMessageLib->sendStateUpdate(this);
	}
	else
	{
		// gLogger->logMsgF("Attacker: Have more defenders", MSG_NORMAL);
		// gMessageLib->sendNewDefenderList(this);
	}
	// gMessageLib->sendNewDefenderList(this);


	if (defenderCreature)
	{
		// Remove us from defenders list.
		defenderCreature->removeDefenderAndUpdateList(this->getId());

		if (attackerPlayer)
		{
			// Update attacker about defender pvp status.
			gMessageLib->sendUpdatePvpStatus(defenderCreature, attackerPlayer);
		}

		if (defenderCreature->getDefenders()->size() == 0)
		{
			// He have no more defenders.
			// gLogger->logMsgF("Defender: My last defender", MSG_NORMAL);
			
			if (defenderPlayer)
			{
				gMessageLib->sendUpdatePvpStatus(defenderCreature,defenderPlayer);
			}
			else
			{
				// We are a npc, and we have no more defenders (for whatever reason).
				// Inform npc about this event.
				defenderCreature->inPeace();
			}
			defenderCreature->toggleStateOff((CreatureState)(CreatureState_Combat + CreatureState_CombatAttitudeNormal));
			gMessageLib->sendStateUpdate(defenderCreature);
		}
		else
		{
			// gLogger->logMsgF("Defender: Have more defenders", MSG_NORMAL);
			//gMessageLib->sendNewDefenderList(defenderCreature);
		}
		// gMessageLib->sendNewDefenderList(defenderCreature);
	}
}

uint32	CreatureObject::UpdatePerformanceCounter()
{
	return (uint32) gWorldManager->GetCurrentGlobalTick();
}

Object* CreatureObject::getTarget() const
{
	return gWorldManager->getObjectById(mTargetId); 
}

//=============================================================================
//handles building custom radials 
void CreatureObject::prepareCustomRadialMenu(CreatureObject* creatureObject, uint8 itemCount)
{

	if(getCreoGroup() == CreoGroup_Vehicle)
	{

		RadialMenu* radial = new RadialMenu();		
	
		radial->addItem(1,0,radId_examine,radAction_Default);

		if(creatureObject->getId() == mOwner)
		{
			PlayerObject* owner = dynamic_cast<PlayerObject*>(creatureObject);
			radial->addItem(2,0,radId_vehicleStore,radAction_ObjCallback,"@pet/pet_menu:menu_store");
			if(owner->checkIfMounted())
			{
				radial->addItem(3,0,radId_serverVehicleExit,radAction_ObjCallback,"@pet/pet_menu:menu_exit");
			}
			else
			{
				radial->addItem(3,0,radId_serverVehicleEnter,radAction_ObjCallback,"@pet/pet_menu:menu_enter");
			}

			//TODO: Check if near a garage then add repair
		}
	
		mRadialMenu = RadialMenuPtr(radial);
	}



}

//=============================================================================
//handles the radial selection

void CreatureObject::handleObjectMenuSelect(uint8 messageType,Object* srcObject)
{

	if(PlayerObject* player = dynamic_cast<PlayerObject*>(srcObject))
	{	
		switch(messageType)
		{
			case radId_vehicleStore:
			{
				if(Datapad* datapad = dynamic_cast<Datapad*>(player->getEquipManager()->getEquippedObject(CreatureEquipSlot_Datapad)))
				{
					IntangibleObject* itno = datapad->getDataById(mId-1);
					if(itno)
					{
						if(Vehicle* vehicle = dynamic_cast<Vehicle*>(itno))
						{
							vehicle->store();
						}
					}
				}
			}
			break;
			case radId_serverVehicleEnter: //An associated packet is sent			
			case radId_serverVehicleExit: //mount and dismount logic is contained within OCPetHandlers.cpp
			break;
			default:
				printf("CreatureObject::Error: unknown radial selection: %d\n",messageType);
			break;
		}
	}
}
