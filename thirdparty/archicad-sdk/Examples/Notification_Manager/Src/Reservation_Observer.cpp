// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************
#include "APIEnvir.h"
#define _RESERVATION_OBSERVER_TRANSL_

// ------------------------------ Includes -------------------------------------

#include	<stdio.h>			/* sprintf */

#include	"ACAPinc.h"
#include	"UniString.hpp"
#include	"Location.hpp"

#include	"Notification_Manager.h"


// ------------------------------ Constants ------------------------------------

// -------------------------------- Types --------------------------------------

// ------------------------------ Variables ------------------------------------

static GS::HashTable<API_Guid, GS::UniString>	objectSetsToReport;

// ------------------------------ Prototypes -----------------------------------

// -----------------------------------------------------------------------------
// GetTeamworkMembers
//
//  collects information of joined Teamwork members
// -----------------------------------------------------------------------------
static GSErrCode	GetTeamworkMembers (GS::HashTable<short, API_UserInfo>& userInfoTable, short& myUserId)
{
	API_ProjectInfo	projectInfo = {};

	GSErrCode err = ACAPI_ProjectOperation_Project (&projectInfo);
	if (err != NoError)
		return err;

	myUserId = projectInfo.userId;

	API_SharingInfo	sharingInfo;
	BNZeroMemory (&sharingInfo, sizeof (API_SharingInfo));
	err = ACAPI_Teamwork_ProjectSharing (&sharingInfo);
	if (err == NoError && sharingInfo.users != nullptr) {
		for (Int32 i = 0; i < sharingInfo.nUsers; i++)
			userInfoTable.Add (((*sharingInfo.users)[i]).userId, (*sharingInfo.users)[i]);
	}
	if (sharingInfo.users != nullptr)
		BMhKill (reinterpret_cast<GSHandle*>(&sharingInfo.users));

	return err;
}		/* GetTeamworkMembers */


// -----------------------------------------------------------------------------
// FindObjectSetName
//
//  returns the name of the specified object set to be reported about
// -----------------------------------------------------------------------------
static GS::UniString	FindObjectSetName (const API_Guid& objectId)
{
	if (objectSetsToReport.IsEmpty ()) {
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("Composites"),					"Composites");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("FillTypes"),					"Fill Types");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("LayerSettingsDialog"),		"Layer Settings");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("LineTypes"),					"Line Types");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("Surfaces"),					"Surfaces");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("MEPSystems"),					"MEP Systems");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("OperationProfiles"),			"Operation Profiles");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("PenTables"),					"Pen Tables");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("Profiles"),					"Profiles");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("ZoneCategories"),				"Zone Categories");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("BuildingMaterials"),			"Building Materials");
		objectSetsToReport.Add (ACAPI_Teamwork_FindLockableObjectSet ("IssueTags"),					"Issue Tags");
	}

	if (objectSetsToReport.ContainsKey (objectId))
		return objectSetsToReport[objectId];

	return GS::UniString ();
}		/* FindObjectSetName */


// -----------------------------------------------------------------------------
// FindElementTypeName
//
//  returns the type name of the specified element to be reported about
// -----------------------------------------------------------------------------
static GS::UniString	FindElementTypeName (const API_Guid& objectId)
{
	GS::UniString elemTypeName;
	API_Elem_Head elemHead;
	BNZeroMemory (&elemHead, sizeof (API_Elem_Head));
	elemHead.guid = objectId;

	if (ACAPI_Element_GetHeader (&elemHead) == NoError) {
		ACAPI_Element_GetElemTypeName (elemHead.type, elemTypeName);
	}

	return elemTypeName;
}		/* FindElementTypeName */


// -----------------------------------------------------------------------------
// PrintReservationInfo
//
//  prints reservation information of a given element
// -----------------------------------------------------------------------------
static void		PrintReservationInfo (const GS::HashTable<short, API_UserInfo>&	userInfoTable,
									  short										myUserId,
									  const char*								actionStr,
									  const API_Guid&							objectId,
									  short										ownerId = 0)
{
	GS::UniString actionByUserStr (actionStr);
	if (userInfoTable.ContainsKey (ownerId)) {
		if (ownerId == myUserId) {
			actionByUserStr.Append (" by me (");
			actionByUserStr.Append (userInfoTable[ownerId].fullName);
			actionByUserStr.Append (")");
		} else {
			actionByUserStr.Append (" by ");
			actionByUserStr.Append (userInfoTable[ownerId].fullName);
		}
	}

	GS::UniString objectSetName = FindObjectSetName (objectId);
	if (!objectSetName.IsEmpty ()) {
		const GS::UniString reportString = GS::UniString::Printf ("=  %T got %T", objectSetName.ToPrintf (), actionByUserStr.ToPrintf ());
		ACAPI_WriteReport (reportString, false);
	} else {
		GS::UniString elemTypeName = FindElementTypeName (objectId);
		if (!elemTypeName.IsEmpty ()) {
			const GS::UniString reportString = GS::UniString::Printf ("=  %T {%T} is %T", elemTypeName.ToPrintf (), APIGuidToString (objectId).ToPrintf (), actionByUserStr.ToPrintf ());
			ACAPI_WriteReport (reportString, false);
		}
	}
}		/* PrintReservationInfo */


// -----------------------------------------------------------------------------
// ReservationChangeHandler
//
//  lists the recent reservation changes in Teamwork
// -----------------------------------------------------------------------------
static GSErrCode ReservationChangeHandler (const GS::HashTable<API_Guid, short>&	reserved,
										   const GS::HashSet<API_Guid>&				released,
										   const GS::HashSet<API_Guid>&				deleted)
{
	GS::HashTable<short, API_UserInfo> userInfoTable;
	short myUserId = 0;

	GSErrCode err = GetTeamworkMembers (userInfoTable, myUserId);
	if (err != NoError)
		return err;

	ACAPI_WriteReport ("=== Attention: Workspace reservation has been changed ===============", false);

	for (GS::HashTable<API_Guid, short>::ConstPairIterator it = reserved.EnumeratePairs (); it != nullptr; ++it) {
		PrintReservationInfo (userInfoTable, myUserId, "reserved", it->key, it->value);
	}

	for (GS::HashSet<API_Guid>::ConstIterator it = released.Enumerate (); it != nullptr; ++it) {
		PrintReservationInfo (userInfoTable, myUserId, "released", *it);
	}

	for (GS::HashSet<API_Guid>::ConstIterator it = deleted.Enumerate (); it != nullptr; ++it) {
		PrintReservationInfo (userInfoTable, myUserId, "deleted", *it);
	}

    return NoError;
}		/* ReservationChangeHandler */


// -----------------------------------------------------------------------------
// LockChangeHandler
//
//  lists the recent lockable object reservation changes in Teamwork
// -----------------------------------------------------------------------------
static GSErrCode LockChangeHandler (const API_Guid& objectId, short ownerId)
{
	GS::HashTable<short, API_UserInfo> userInfoTable;
	short myUserId = 0;

	GSErrCode err = GetTeamworkMembers (userInfoTable, myUserId);
	if (err != NoError)
		return err;

	GS::HashTable<API_Guid, GS::UniString> lockableObjectSets;
	lockableObjectSets.Add (ACAPI_Teamwork_FindLockableObjectSet ("Favorites"),			"Favorites");
	lockableObjectSets.Add (ACAPI_Teamwork_FindLockableObjectSet ("ModelViewOptions"),	"Model View Options");
	lockableObjectSets.Add (ACAPI_Teamwork_FindLockableObjectSet ("ProjectInfo"),		"Project Info");
	lockableObjectSets.Add (ACAPI_Teamwork_FindLockableObjectSet ("PreferencesDialog"),	"Project Preferences");

	GS::UniString lockableObjectSetName;
	if (!lockableObjectSets.Get (objectId, &lockableObjectSetName))
		lockableObjectSetName = GS::UniString::Printf ("Unknown object {%T}", APIGuid2GSGuid (objectId).ToUniString ().ToPrintf ());

	ACAPI_WriteReport ("=== Attention: Workspace reservation has been changed ===============", false);

	GS::UniString actionByUserStr (ownerId > 0 ? "reserved" : "released");
	if (userInfoTable.ContainsKey (ownerId)) {
		if (ownerId == myUserId) {
			actionByUserStr.Append (" by me (");
			actionByUserStr.Append (userInfoTable[ownerId].fullName);
			actionByUserStr.Append (")");
		} else {
			actionByUserStr.Append (" by ");
			actionByUserStr.Append (userInfoTable[ownerId].fullName);
		}
	}

	GS::UniString reportString = GS::UniString::Printf ("=  %T got %T", lockableObjectSetName.ToPrintf (), actionByUserStr.ToPrintf ());
	ACAPI_WriteReport (reportString, false);

    return NoError;
}		/* LockChangeHandler */


// ============================================================================
// Install Notification Handlers
//
//
// ============================================================================
void	Do_ReservationMonitor (bool switchOn)
{
	if (switchOn) {
		ACAPI_Notification_CatchElementReservationChange (ReservationChangeHandler);
		ACAPI_Notification_CatchLockableReservationChange (LockChangeHandler);
	} else {
		ACAPI_Notification_CatchElementReservationChange (nullptr);
		ACAPI_Notification_CatchLockableReservationChange (nullptr);
	}

	return;
}		/* Do_ReservationMonitor */
