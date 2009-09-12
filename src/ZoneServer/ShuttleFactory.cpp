/*
---------------------------------------------------------------------------------------
This source file is part of swgANH (Star Wars Galaxies - A New Hope - Server Emulator)
For more information, see http://www.swganh.org


Copyright (c) 2006 - 2008 The swgANH Team

---------------------------------------------------------------------------------------
*/

#include "ShuttleFactory.h"
#include "ObjectFactoryCallback.h"
#include "DatabaseManager/Database.h"
#include "DatabaseManager/DatabaseResult.h"
#include "DatabaseManager/DataBinding.h"
#include "LogManager/LogManager.h"
#include "Inventory.h"
#include "Utils/utils.h"
#include "Utils/rand.h"

#include <assert.h>

//=============================================================================

bool			ShuttleFactory::mInsFlag    = false;
ShuttleFactory*	ShuttleFactory::mSingleton  = NULL;

//======================================================================================================================

ShuttleFactory*	ShuttleFactory::Init(Database* database)
{
	if(!mInsFlag)
	{
		mSingleton = new ShuttleFactory(database);
		mInsFlag = true;
		return mSingleton;
	}
	else
		return mSingleton;
}

//=============================================================================

ShuttleFactory::ShuttleFactory(Database* database) : FactoryBase(database)
{
	_setupDatabindings();
}

//=============================================================================

ShuttleFactory::~ShuttleFactory()
{
	_destroyDatabindings();

	mInsFlag = false;
	delete(mSingleton);
}

//=============================================================================

void ShuttleFactory::handleDatabaseJobComplete(void* ref,DatabaseResult* result)
{
	QueryContainerBase* asyncContainer = reinterpret_cast<QueryContainerBase*>(ref);

	switch(asyncContainer->mQueryType)
	{
		case SHFQuery_MainData:
		{
			Shuttle* shuttle = _createShuttle(result);

			if(shuttle->getLoadState() == LoadState_Loaded && asyncContainer->mOfCallback)
			{
				asyncContainer->mOfCallback->handleObjectReady(shuttle,asyncContainer->mClient);
			}
			else
			{

			}
		}
		break;

		default:break;
	}

	mQueryContainerPool.free(asyncContainer);
}

//=============================================================================

void ShuttleFactory::requestObject(ObjectFactoryCallback* ofCallback,uint64 id,uint16 subGroup,uint16 subType,DispatchClient* client)
{
	mDatabase->ExecuteSqlAsync(this,new(mQueryContainerPool.ordered_malloc()) QueryContainerBase(ofCallback,SHFQuery_MainData,client),
								"SELECT shuttles.id,shuttles.parentId,shuttles.firstName,shuttles.lastName,"
								"shuttles.oX,shuttles.oY,shuttles.oZ,shuttles.oW,shuttles.x,shuttles.y,shuttles.z,"
								"shuttle_types.object_string,shuttle_types.name,shuttle_types.file,shuttles.awayTime,shuttles.inPortTime,shuttles.collectorId "
								"FROM shuttles "
								"INNER JOIN shuttle_types ON (shuttles.shuttle_type = shuttle_types.id) "
								"WHERE (shuttles.id = %lld)",id);
}

//=============================================================================

Shuttle* ShuttleFactory::_createShuttle(DatabaseResult* result)
{
	Shuttle*	shuttle				= new Shuttle();
	Inventory*	shuttleInventory	= new Inventory();
	shuttleInventory->setParent(shuttle);

	uint64 count = result->getRowCount();
	assert(count == 1);

	result->GetNextRow(mShuttleBinding,(void*)shuttle);

	shuttle->mHam.mBattleFatigue = 0;
	shuttle->mHam.mHealth.setCurrentHitPoints(500);
	shuttle->mHam.mAction.setCurrentHitPoints(500);
	shuttle->mHam.mMind.setCurrentHitPoints(500);
	shuttle->mHam.calcAllModifiedHitPoints();

	// inventory
	shuttleInventory->setId(shuttle->mId + 1);
	shuttleInventory->setParentId(shuttle->mId);
	shuttleInventory->setModelString("object/tangible/inventory/shared_creature_inventory.iff");
	shuttleInventory->setName("inventory");
	shuttleInventory->setNameFile("item_n");
	shuttleInventory->setTangibleGroup(TanGroup_Inventory);
	shuttleInventory->setTangibleType(TanType_CreatureInventory);
	shuttle->mEquipManager.addEquippedObject(CreatureEquipSlot_Inventory,shuttleInventory);
	shuttle->setLoadState(LoadState_Loaded);

	shuttle->mPosture = 0;
	shuttle->mScale = 1.0;
	shuttle->setFaction("neutral");
	shuttle->mTypeOptions = 0x100;

	// Here we can handle the initializing of shuttle states

	// First, a dirty test for the shuttles in Theed Spaceport. 
	// No need to randomize departure times, since we can always travel from there.
	// We wan't them to go in sync, so one of them always are in the spaceport.
	// if (shuttle->mParentId == 1692104)
	if (shuttle->mId == 47781511212)
	{
		shuttle->setShuttleState(ShuttleState_InPort);
		shuttle->setInPortTime(0);
	}
	else if (shuttle->mId == 47781511214)	// This is the "extra" shuttle.
	{
		shuttle->setShuttleState(ShuttleState_Away);
		shuttle->setAwayTime(0);
	}
	else
	{
		// Get a randowm value in the range [0 <-> InPortInterval + AwayInterval] in ticks.
		// The rand value will land in either the InPort or in the Away part of the values.
		// Use that state as initial state and set the value as time that have already expired.
		uint32 maxInPortAndAwayIntervalTime = shuttle->getInPortInterval() + shuttle->getAwayInterval();
		uint32 shuttleTimeExpired = ((double)gRandom->getRand() / (RAND_MAX + 1)) * (maxInPortAndAwayIntervalTime);

		if (shuttleTimeExpired <= shuttle->getInPortInterval())
		{
			// gLogger->logMsgF("Shuttlee start InPort, time expired %u",MSG_NORMAL, shuttleTimeExpired);
			shuttle->setShuttleState(ShuttleState_InPort);
			shuttle->setInPortTime(shuttleTimeExpired);
		}
		else
		{
			// gLogger->logMsgF("Shuttlee start Away, time expired %u",MSG_NORMAL, shuttleTimeExpired - shuttle->getInPortInterval());
			shuttle->setShuttleState(ShuttleState_Away);
			shuttle->setAwayTime(shuttleTimeExpired - shuttle->getInPortInterval()); // Set the part corresponding to this state only.
		}
	}
	return shuttle;
}

//=============================================================================

void ShuttleFactory::_setupDatabindings()
{
	mShuttleBinding = mDatabase->CreateDataBinding(17);
	mShuttleBinding->addField(DFT_uint64,offsetof(Shuttle,mId),8,0);
	mShuttleBinding->addField(DFT_uint64,offsetof(Shuttle,mParentId),8,1);
	mShuttleBinding->addField(DFT_bstring,offsetof(Shuttle,mFirstName),64,2);
	mShuttleBinding->addField(DFT_bstring,offsetof(Shuttle,mLastName),64,3);
	mShuttleBinding->addField(DFT_float,offsetof(Shuttle,mDirection.mX),4,4);
	mShuttleBinding->addField(DFT_float,offsetof(Shuttle,mDirection.mY),4,5);
	mShuttleBinding->addField(DFT_float,offsetof(Shuttle,mDirection.mZ),4,6);
	mShuttleBinding->addField(DFT_float,offsetof(Shuttle,mDirection.mW),4,7);
	mShuttleBinding->addField(DFT_float,offsetof(Shuttle,mPosition.mX),4,8);
	mShuttleBinding->addField(DFT_float,offsetof(Shuttle,mPosition.mY),4,9);
	mShuttleBinding->addField(DFT_float,offsetof(Shuttle,mPosition.mZ),4,10);
	mShuttleBinding->addField(DFT_bstring,offsetof(Shuttle,mModel),256,11);
	mShuttleBinding->addField(DFT_bstring,offsetof(Shuttle,mSpecies),64,12);
	mShuttleBinding->addField(DFT_bstring,offsetof(Shuttle,mSpeciesGroup),64,13);
	mShuttleBinding->addField(DFT_uint32,offsetof(Shuttle,mAwayInterval),4,14);
	mShuttleBinding->addField(DFT_uint32,offsetof(Shuttle,mInPortInterval),4,15);
	mShuttleBinding->addField(DFT_uint64,offsetof(Shuttle,mTicketCollectorId),8,16);
}

//=============================================================================

void ShuttleFactory::_destroyDatabindings()
{
	mDatabase->DestroyDataBinding(mShuttleBinding);
}

//=============================================================================
