// *****************************************************************************
// Description:		Source code for the Database Control Add-On
// *****************************************************************************

#define	_DATABASE_CONTROL_TRANSL_


// --- Includes ----------------------------------------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"
#include	"APICommon.h"

#include	"FileSystem.hpp"
#include	"IV.hpp"

#include	"Resource.h"

#include	"LibXL/libxl.h"

// --- Type definitions --------------------------------------------------------

typedef struct {
	bool		usedCell;
	bool		isText;
	bool		filler_1;
	bool		filler_2;
} MyWindType;

typedef struct {
	unsigned short	platform;
	unsigned short	filler;
	UInt32			lastDetail;
	UInt32			lastWorksheet;
	UInt32			lastLayout;
	UInt32			last3DDocument;
} PrefsData;



// --- Variables declarations --------------------------------------------------

static PrefsData	*myPrefs = nullptr;
static API_Guid		storedRenovationFilter = APINULLGuid;


// --- Function Prototypes -----------------------------------------------------

static void		Do_RebuildMyWindow (const API_Guid& userRefId);
static void		Do_ActivateMyWindow (const API_Guid& userRefId);
static void		Do_CloseMyWindow (const API_Guid& userRefId, API_WindowTypeID wdType);

static void		DisableEnableCloseCmd (void);

// =============================================================================
//
// Handling Custom Window Notifications
//
// =============================================================================

static GSErrCode TestCustomWindowHandler (const API_Guid& userRefId, API_NotifyWindowEventID notifID)
{
	char	msgStr[256];
	GS::Guid guid = APIGuid2GSGuid (userRefId);

	switch (notifID) {
		case APINotifyWindow_Activate:	Do_ActivateMyWindow (userRefId);
										sprintf (msgStr, "<APINotifyWindow_Activate> %s", guid.ToUniString ().ToCStr ().Get ());
										break;
		case APINotifyWindow_Rebuild:	Do_RebuildMyWindow (userRefId);
										sprintf (msgStr, "<APINotifyWindow_Rebuild> %s", guid.ToUniString ().ToCStr ().Get ());
										break;
		case APINotifyWindow_Close:		Do_CloseMyWindow (userRefId, APIWind_MyDrawID);
										sprintf (msgStr, "<APINotifyWindow_Close> %s", guid.ToUniString ().ToCStr ().Get ());
										break;
	}

	ACAPI_WriteReport (msgStr, false);

	return NoError;
}


// =============================================================================
//
// Manage default windows/databases
//
// =============================================================================

// -----------------------------------------------------------------------------
// Scan all the section databases and duplicate the line elements
//	 - duplicated elements will have another color
//   - the endpoins of duplicated elements will be moved by a vector (0.5, 0.5)
// Place a line to each section marker on the floorplan
//	 - the begPoint will match to section marker
//   - the endPoint will be moved by a vector (0.5, 0.5) multiplied by
//     the number of lines counted in the related section database
// You will get an alert for each section that hasn't been opened yet
// -----------------------------------------------------------------------------

static void	Do_ListSectionLines (void)
{
	ACAPI_CallUndoableCommand ("place lines", [&]() -> GSErrCode {
		API_Element			element;
		API_DatabaseInfo	db_sect, db_plan;
		GSErrCode			err;
		char				msgStr[256];

		ACAPI_Database_GetCurrentDatabase (&db_plan);

		ACAPI_WriteReport ("Sections:", false);

		GS::Array<API_Guid>  cutPlaneList;
		if (ACAPI_Element_GetElemList (API_CutPlaneID, &cutPlaneList) != NoError) {
			ACAPI_WriteReport ("Error in ACAPI_Element_GetElemList ().", true);
			return Error;
		}

		for (auto& guid : cutPlaneList.AsConst ()) {
			element = {};
			element.header.guid = guid;

			err = ACAPI_Element_Get (&element);
			if (err != NoError) {
				sprintf (msgStr, "Error in ACAPI_Element_Get: %d", (int)err);
				ACAPI_WriteReport (msgStr, true);
				continue;
			}

			if (!element.header.hasMemo || element.cutPlane.segment.nMainCoord < 2)
				continue;

			API_ElementMemo memo;
			err = ACAPI_Element_GetMemo (element.header.guid, &memo);
			if (err != NoError) {
				sprintf (msgStr, "Error in ACAPI_Element_GetMemo: %d", (int)err);
				ACAPI_WriteReport (msgStr, true);
				continue;
			}

			const API_Coord c1 = memo.sectionSegmentMainCoords[0];
			const API_Coord c2 = memo.sectionSegmentMainCoords[1];
			ACAPI_DisposeElemMemoHdls (&memo);

			BNZeroMemory (&db_sect, sizeof (API_DatabaseInfo));
			db_sect.typeID = APIWind_SectionID;
			db_sect.databaseUnId = element.cutPlane.segment.databaseID;

			err = ACAPI_Database_ChangeCurrentDatabase (&db_sect);
			if (err == APIERR_NOTINIT) {
				sprintf (msgStr, "  [%s]  not generated  elemSetId:%s", APIGuidToString (guid).ToCStr ().Get (), APIGuidToString (element.cutPlane.segment.databaseID.elemSetId).ToCStr ().Get ());
				ACAPI_WriteReport (msgStr, false);
				continue;
			}

			sprintf (msgStr, "  [%s]  %s  elemSetId:%s", APIGuidToString (guid).ToCStr ().Get (), element.cutPlane.segment.windOpened ? "open" : "closed",
					 APIGuidToString (element.cutPlane.segment.databaseID.elemSetId).ToCStr ().Get ());
			ACAPI_WriteReport (msgStr, false);

			GS::Array<API_Guid>  lineList;
			if (ACAPI_Element_GetElemList (API_LineID, &lineList) != NoError) {
				ACAPI_WriteReport ("Error in ACAPI_Element_GetElemList ().", true);
				return Error;
			}

			for (auto& jtGuid : lineList.AsConst ()) {
				element = {};
				element.header.guid = jtGuid;

				err = ACAPI_Element_Get (&element);
				if (err == NoError) {
					element.line.linePen.penIndex++;
					element.line.endC.x += 0.5;
					element.line.endC.y += 0.5;
					err = ACAPI_Element_Create (&element, nullptr);
				}
			}
			err = ACAPI_Database_ChangeCurrentDatabase (&db_plan);

			element.header.layer = APIApplicationLayerAttributeIndex;
			element.line.begC    = c1;
			element.line.endC.x	 = c2.x + lineList.GetSize () * 0.2;
			element.line.endC.y  = c2.y + lineList.GetSize () * 0.2;
			err = ACAPI_Element_Create (&element, nullptr);
		}
		return NoError;
	});
}		// Do_ListSectionLines



// =============================================================================
//
// Manage custom windows/databases
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif


// -----------------------------------------------------------------------------
// Disable/Enable a menu item
// -----------------------------------------------------------------------------

static void		DisableEnableMenuItemCmd (short menuResID, short itemIndex, bool disable)
{
	API_MenuItemRef		itemRef;
	GSFlags				itemFlags;

	BNZeroMemory (&itemRef, sizeof (API_MenuItemRef));
	itemRef.menuResID = menuResID;
	itemRef.itemIndex = itemIndex;
	itemFlags = 0;

	if (ACAPI_MenuItem_GetMenuItemFlags (&itemRef, &itemFlags) != NoError)
		return;

	if (disable)
		itemFlags |= API_MenuItemDisabled;
	else
		itemFlags &= ~API_MenuItemDisabled;

	ACAPI_MenuItem_SetMenuItemFlags (&itemRef, &itemFlags);

	return;
}		// DisEnableMenuItem


// -----------------------------------------------------------------------------
// Manage the availability of window close commands
// -----------------------------------------------------------------------------

void		DisableEnableCloseCmd (void)
{
	GS::Array<API_Guid> textWdIds;
	API_WindowTypeID wdType = APIWind_MyTextID;
	GSErrCode err = ACAPI_Window_GetOwnWindows (&wdType, &textWdIds);
	if (err != NoError)
		return;

	GS::Array<API_Guid> drawWdIds;
	wdType = APIWind_MyDrawID;
	err = ACAPI_Window_GetOwnWindows (&wdType, &drawWdIds);
	if (err != NoError)
		return;

	DisableEnableMenuItemCmd (IDR_OwnMenu, ID_MENU_CloseText, (textWdIds.IsEmpty ()));
	DisableEnableMenuItemCmd (IDR_OwnMenu, ID_MENU_CloseDraw, (drawWdIds.IsEmpty ()));
	DisableEnableMenuItemCmd (IDR_OwnMenu, ID_MENU_CloseAll, (textWdIds.IsEmpty () && drawWdIds.IsEmpty ()));

	return;
}		// DisableEnableCloseCmd


// -----------------------------------------------------------------------------
// Rebuild my window
//	 - draw: copy all lines from the floorplan
//	 - text: count lines on the floorplan
// !!! Switching to another database is a relatively expensive operation !!!
//	 - this example uses this service extensively
//	 - it just for better understanding of the database structure
//	 - should be optimized: ie. collect lines in advance then copy all in one step
// -----------------------------------------------------------------------------
void		Do_RebuildMyWindow (const API_Guid& userRefId)
{
	API_DatabaseInfo	planDB;
	bool				isText;
	API_Element			element;
	GSErrCode			err;

	GS::Array<API_Guid> wdIds;
	API_WindowTypeID wdType = APIWind_MyTextID;
	err = ACAPI_Window_GetOwnWindows (&wdType, &wdIds);
	if (err != NoError)
		return;

	isText = wdIds.Contains (userRefId);

	BNZeroMemory (&planDB, sizeof (API_DatabaseInfo));
	planDB.typeID = APIWind_FloorPlanID;

	if (isText) {
		API_WindowInfo      myWD = {};
		API_DatabaseInfo	origDB;

		// save original
		ACAPI_Database_GetCurrentDatabase (&origDB);

		// reset content
		myWD.typeID = APIWind_MyTextID;
		myWD.databaseUnId.elemSetId = userRefId;
		ACAPI_Window_ResetCurrentWindow ();

		// switch to floorplan
		err = ACAPI_Database_ChangeCurrentDatabase (&planDB);
		GS::Array<API_Guid> lineList;
		ACAPI_Element_GetElemList (API_LineID, &lineList);

		GS::Array<API_Guid>  visibleLinesList;
		if (ACAPI_Element_GetElemList (API_LineID, &visibleLinesList, APIFilt_OnVisLayer | APIFilt_OnActFloor) != NoError) {
			ACAPI_WriteReport ("Error in ACAPI_Element_GetElemList ().", true);
			return;
		}

		// write content
		GS::UniString	buffer = GS::UniString::Printf ("Number of lines: %ld\n", (GS::LongForStdio) lineList.GetSize ());
		ACAPI_Window_AddTextWindowContent (&myWD, nullptr, &buffer);
		buffer = GS::UniString::Printf ("Number of visible lines: %ld\n", (GS::LongForStdio) visibleLinesList.GetSize ());
		ACAPI_Window_AddTextWindowContent (&myWD, nullptr, &buffer);

		// reset original
		err = ACAPI_Database_ChangeCurrentDatabase (&origDB);

	} else {
		API_DatabaseInfo	myDB;

		// database should be mine
		ACAPI_Database_GetCurrentDatabase (&myDB);
		if (myDB.typeID != APIWind_MyDrawID || myDB.databaseUnId.elemSetId != userRefId) {
			DBBREAK_STR ("Wrong active database");
			return;
		}

		// reset content
		ACAPI_Window_ResetCurrentDatabase ();

		// switch to floorplan
		err = ACAPI_Database_ChangeCurrentDatabase (&planDB);
		GS::Array<API_Guid>  visibleLinesList;
		if (ACAPI_Element_GetElemList (API_LineID, &visibleLinesList, APIFilt_OnVisLayer | APIFilt_OnActFloor) != NoError) {
			ACAPI_WriteReport ("Error in ACAPI_Element_GetElemList ().", true);
			return;
		}

		API_GridType		grid;
		ACAPI_Window_GetGridSettings (&grid);
		grid.gridColor.f_red   = 1.0;
		grid.gridColor.f_green = 0.0;
		grid.gridColor.f_blue  = 0.0;

		// switch to mine
		err = ACAPI_Database_ChangeCurrentDatabase (&myDB);

		for (auto& guid : visibleLinesList.AsConst ()) {
			element = {};
			element.header.guid = guid;

			// switch to floorplan
			err = ACAPI_Database_ChangeCurrentDatabase (&planDB);

			err = ACAPI_Element_Get (&element);
			if (err == NoError) {
				element.header.floorInd = 0;

				// switch to mine
				err = ACAPI_Database_ChangeCurrentDatabase (&myDB);
				err = ACAPI_Element_Create (&element, nullptr);
			}
			if (err != NoError) {
				DBPrintf ("Line with GUID %s cannot be moved.\n", APIGuidToString (guid).ToCStr ().Get ());
				err = NoError;
			}
		}

		// reset original (mine)
		ACAPI_Database_ChangeCurrentDatabase (&myDB);

		ACAPI_Window_ChangeGridSettings (&grid);
		ACAPI_Database_RebuildCurrentDatabase ();
	}
}		// Do_RebuildMyWindow


// -----------------------------------------------------------------------------
// Activate my window
// -----------------------------------------------------------------------------

void		Do_ActivateMyWindow (const API_Guid& userRefId)
{
	Do_RebuildMyWindow (userRefId);

	return;
}		// Do_ActivateMyWindow


// -----------------------------------------------------------------------------
// Create a custom window from API
// -----------------------------------------------------------------------------

static void		Do_OpenMyWindow (API_WindowTypeID wdType)
{
	API_NewWindowPars	wPars;
	GSErrCode			err;
	char				msgStr[256];

	BNZeroMemory (&wPars, sizeof (API_NewWindowPars));

	wPars.typeID = wdType;
	GS::Guid guid;
	guid.Generate ();
	wPars.userRefId = GSGuid2APIGuid (guid);

	GS::snuprintf (wPars.wTitle, sizeof (wPars.wTitle) / sizeof (GS::uchar_t), L("API window: %s"), guid.ToUniString ().ToCStr ().Get ());
	GS::ucscpy (wPars.wFile, L("API window"));

	err = ACAPI_Window_NewWindow (&wPars, TestCustomWindowHandler);

	if (err == NoError) {
		Do_RebuildMyWindow (wPars.userRefId);
	} else if (err == APIERR_WINDEXIST) {
		sprintf (msgStr, "API window with userRefId %s already exists.", guid.ToUniString ().ToCStr ().Get ());
		ACAPI_WriteReport (msgStr, true);
	} else {
		sprintf (msgStr, "Cannot open %s window with userRefId %s.", (wdType == APIWind_MyTextID ? "APIWind_MyTextID" : "APIWind_MyDrawID"), guid.ToUniString ().ToCStr ().Get ());
		ACAPI_WriteReport (msgStr, true);
	}

	DisableEnableCloseCmd ();

	return;
}		// Do_OpenMyWindow


// -----------------------------------------------------------------------------
// Close my custom window
// -----------------------------------------------------------------------------

void		Do_CloseMyWindow (const API_Guid& userRefId, API_WindowTypeID wdType)
{
	API_WindowInfo		wPars;
	BNZeroMemory (&wPars, sizeof (API_WindowInfo));
	wPars.typeID = wdType;

	if (userRefId == APINULLGuid) {		// from menu
		GS::Array<API_Guid> wdIds;
		GSErrCode err = ACAPI_Window_GetOwnWindows (&wdType, &wdIds);
		if (err != NoError || wdIds.IsEmpty ()) {
			return;
		}

		wPars.databaseUnId.elemSetId = wdIds[0];
		err = ACAPI_Window_CloseWindow (&wPars);
		if (err != NoError) {
			DBPrintf ("Database Control:: ACAPI_Database_CloseWindow ( failed\n");
		}
	} else {
		wPars.databaseUnId.elemSetId = userRefId;
	}

	DisableEnableCloseCmd ();

	return;
}		// Do_CloseMyWindow


// -----------------------------------------------------------------------------
// Close all windows
// -----------------------------------------------------------------------------

static void		Do_CloseAllWindows (void)
{
	GS::Array<API_Guid> wdIds;
	API_WindowTypeID wdType = APIWind_MyTextID;
	GSErrCode err = ACAPI_Window_GetOwnWindows (&wdType, &wdIds);
	if (err != NoError)
		return;

	for (auto it = wdIds.Enumerate (); it != nullptr; ++it)
		Do_CloseMyWindow (*it, wdType);

	wdIds.Clear ();
	wdType = APIWind_MyDrawID;
	err = ACAPI_Window_GetOwnWindows (&wdType, &wdIds);
	if (err != NoError)
		return;

	for (auto it = wdIds.Enumerate (); it != nullptr; ++it)
		Do_CloseMyWindow (*it, wdType);

	return;
}		// Do_CloseAllWindows


// -----------------------------------------------------------------------------
// Create a new independent detail
// -----------------------------------------------------------------------------

static void		Do_CreateDetail ()
{
	API_DatabaseInfo		dbInfo = {};

	dbInfo.typeID = APIWind_DetailID;
	GS::snuprintf (dbInfo.name, sizeof (dbInfo.name), "Dooku %ld", ++myPrefs->lastDetail);
	GS::snuprintf (dbInfo.ref, sizeof (dbInfo.ref), "D%02ld", myPrefs->lastDetail);

	if (ACAPI_Database_NewDatabase (&dbInfo) == NoError) {
		DBPrintf ("DatabaseControl :: The ID of the new detail database is \"%s\"\n",
			APIGuidToString (dbInfo.databaseUnId.elemSetId).ToCStr ().Get ());
	}

	return;
}		// Do_CreateDetail


// -----------------------------------------------------------------------------
// Create a new independent worksheet
// -----------------------------------------------------------------------------

static void		Do_CreateWorksheet ()
{
	API_DatabaseInfo		dbInfo = {};

	dbInfo.typeID = APIWind_WorksheetID;
	GS::snuprintf (dbInfo.name, sizeof (dbInfo.name), "Zaphod %ld", ++myPrefs->lastWorksheet);
	GS::snuprintf (dbInfo.ref, sizeof (dbInfo.ref), "W%02ld", myPrefs->lastWorksheet);

	if (ACAPI_Database_NewDatabase (&dbInfo) == NoError) {
		DBPrintf ("DatabaseControl :: The ID of the new worksheet database is \"%s\"\n",
			APIGuidToString (dbInfo.databaseUnId.elemSetId).ToCStr ().Get ());
	}

	return;
}		// Do_CreateWorksheet


// -----------------------------------------------------------------------------
// Create a new 3D Document
// -----------------------------------------------------------------------------

static void		Do_Create3DDocument ()
{
	API_DatabaseInfo		dbInfo = {};

	dbInfo.typeID = APIWind_DocumentFrom3DID;
	GS::snuprintf (dbInfo.name, sizeof (dbInfo.name), "Document3D %ld", ++myPrefs->last3DDocument);
	GS::snuprintf (dbInfo.ref, sizeof (dbInfo.ref), "3D%02ld", myPrefs->last3DDocument);

	if (ACAPI_Database_NewDatabase (&dbInfo) == NoError) {
		DBPrintf ("DatabaseControl :: The ID of the new Document 3D database is \"%s\"\n",
			APIGuidToString (dbInfo.databaseUnId.elemSetId).ToCStr ().Get ());
	}

	return;
}		// Do_Create3DDocument


// -----------------------------------------------------------------------------
// Create a new layout with master
// -----------------------------------------------------------------------------

static void		Do_CreateLayout ()
{
	API_DatabaseInfo		dbInfo = {};

	dbInfo.typeID = APIWind_MasterLayoutID;
	GS::snuprintf (dbInfo.name, sizeof (dbInfo.name), "Yoda %ld", ++myPrefs->lastLayout);

	if (ACAPI_Database_NewDatabase (&dbInfo) == NoError) {
		DBPrintf ("DatabaseControl :: The ID of the new master layout database is \"%s\"\n",
			APIGuidToString (dbInfo.databaseUnId.elemSetId).ToCStr ().Get ());

		API_LayoutInfo lInfo = {};
		GS::snuprintf (lInfo.layoutName, API_UniLongNameLen, "Luke %d", (int) (myPrefs->lastLayout));
		lInfo.layoutPageNumber = 2;
		lInfo.leftMargin = 12;
		lInfo.rightMargin = 23;
		lInfo.topMargin = 34;
		lInfo.bottomMargin = 45;
		lInfo.showMasterBelow = true;
		ACAPI_Navigator_CreateLayout (&lInfo, &dbInfo.databaseUnId);
	}

	return;
}		// Do_CreateLayout


// -----------------------------------------------------------------------------
// Rename the last independent detail or layout database
// -----------------------------------------------------------------------------

static void		Do_RenameDatabase (const API_DatabaseTypeID& dbType)
{
	const GS::Array<API_DatabaseUnId> dbases = ACAPI_Database_GetDatabasesForType (dbType);
	if (!dbases.IsEmpty ()) {
		API_DatabaseInfo api_dbPars = {};

		api_dbPars.databaseUnId = dbases[dbases.GetSize () - 1];
		if (ACAPI_Window_GetDatabaseInfo (&api_dbPars) == NoError) {
			switch (dbType) {
				case APIWind_DetailID:			GS::snuprintf (api_dbPars.name, sizeof (api_dbPars.name), "Dooku %ld", ++myPrefs->lastDetail);
												GS::snuprintf (api_dbPars.ref, sizeof (api_dbPars.ref), "D%02ld", myPrefs->lastDetail);
												break;
				case APIWind_WorksheetID:		GS::snuprintf (api_dbPars.name, sizeof (api_dbPars.name), "Zaphod %ld", ++myPrefs->lastWorksheet);
												GS::snuprintf (api_dbPars.ref, sizeof (api_dbPars.ref), "W%02ld", myPrefs->lastWorksheet);
												break;
				case APIWind_DocumentFrom3DID: 	GS::snuprintf (api_dbPars.name, sizeof (api_dbPars.name), "Document3D %ld", ++myPrefs->last3DDocument);
												GS::snuprintf (api_dbPars.ref, sizeof (api_dbPars.ref), "3D%02ld", myPrefs->last3DDocument);
												break;
				case APIWind_LayoutID:		   	GS::snuprintf (api_dbPars.name, sizeof (api_dbPars.name), "Luke %ld", ++myPrefs->lastLayout);
												{
													API_LayoutInfo	layoutInfo;			// temporary here
													BNZeroMemory (&layoutInfo, sizeof (API_LayoutInfo));
													if (ACAPI_Navigator_GetLayoutSets (&layoutInfo, &api_dbPars.databaseUnId) == NoError) {
														double origVal = layoutInfo.sizeX;
														layoutInfo.sizeX = layoutInfo.sizeY;
														layoutInfo.sizeY = origVal;
														origVal = layoutInfo.leftMargin;
														layoutInfo.leftMargin = layoutInfo.topMargin;
														layoutInfo.topMargin = origVal;
														origVal = layoutInfo.rightMargin;
														layoutInfo.rightMargin = layoutInfo.bottomMargin;
														layoutInfo.bottomMargin = origVal;
														layoutInfo.customLayoutNumbering = true;
														CHCopyC ("NUMBER", layoutInfo.customLayoutNumber);
														if (layoutInfo.customData != nullptr) {
															for (auto it = layoutInfo.customData->EnumeratePairs (); it != nullptr; ++it) {
																it->value += " - Modified via API";
															}
														}
														ACAPI_Navigator_ChangeLayoutSets (&layoutInfo, &api_dbPars.databaseUnId);
														if (layoutInfo.customData != nullptr)
															delete layoutInfo.customData;
													}
												}
												break;
				case APIWind_MasterLayoutID:	GS::snuprintf (api_dbPars.name, sizeof (api_dbPars.name), "Yoda %ld", ++myPrefs->lastLayout);
												break;
				case APIWind_SectionID:			GS::snuprintf (api_dbPars.name, sizeof (api_dbPars.name), "Renamed Section");
												break;
				default:						break;
			}

			ACAPI_Database_ModifyDatabase (&api_dbPars);
		}
	}

	return;

}		// Do_RenameDatabase

	
// -----------------------------------------------------------------------------
// Delete the last independent detail or layout database
// -----------------------------------------------------------------------------

static void		Do_DeleteDatabase (const API_DatabaseTypeID& dbType)
{
	const GS::Array<API_DatabaseUnId> dbases = ACAPI_Database_GetDatabasesForType (dbType);
	if (!dbases.IsEmpty ()) {
		API_DatabaseInfo api_dbPars = {};

		api_dbPars.databaseUnId = dbases[dbases.GetSize () - 1];
		ACAPI_Window_GetDatabaseInfo (&api_dbPars);

		if (ACAPI_Database_DeleteDatabase (&api_dbPars) != NoError) {
			char msgStr[512];
			sprintf (msgStr, "Cannot delete database: \"%s\"", (const char *) GS::UniString (api_dbPars.title).ToCStr ());
			ACAPI_WriteReport (msgStr, true);
		}
	}

	return;
}		// Do_DeleteDatabase


// -----------------------------------------------------------------------------
// CreateSubset
// -----------------------------------------------------------------------------

static void		Do_CreateSubset ()
{
	API_SubSet subSet;
	BNZeroMemory (&subSet, sizeof (API_SubSet));
	GSErrCode err = ACAPI_Navigator_GetSubSetDefault (&subSet);
	if (err == NoError) {
		GS::UniString ownPrefix ("API");
		GS::ucsncpy (subSet.ownPrefix, ownPrefix.ToUStr (), sizeof (subSet.ownPrefix) / sizeof (GS::uchar_t) - 1);
		subSet.addOwnPrefix = true;

		GS::UniString name ("This Subset was created via API");
		GS::ucsncpy (subSet.name, name.ToUStr (), sizeof (subSet.name) / sizeof (GS::uchar_t) - 1);

		err = ACAPI_Navigator_CreateSubSet (&subSet);
		if (err != NoError)
			DBPrintf ("APIDb_CreateSubSetID function failed");
	}

	return;
}		// Do_CreateSubset


// -----------------------------------------------------------------------------
// Dump info for certain databases
// -----------------------------------------------------------------------------

static void		Do_ListDatabases (const API_DatabaseTypeID& dbType, const char * typeStr)
{
	const GS::Array<API_DatabaseUnId> dbases = ACAPI_Database_GetDatabasesForType (dbType);
	DBPrintf ("DatabaseControl :: The number of %s databases is %d\n", typeStr, dbases.GetSize ());

	bool	isMasterLayout = CHCompareCStrings (typeStr, "master layout"sv) == 0;
	bool	isLayout = CHCompareCStrings (typeStr, "layout"sv) == 0;
	for (const auto& dbUnId : dbases) {
		API_DatabaseInfo dbPars = {};
		dbPars.databaseUnId = dbUnId;

		if (ACAPI_Window_GetDatabaseInfo (&dbPars) == NoError) {
			DBPrintf ("DatabaseControl :: %s index[%d] unId: \"%s\" title:\"%s\" name:\"%s\" ref:\"%s\"\n",
						typeStr,
						dbPars.index,
						APIGuidToString (dbPars.databaseUnId.elemSetId).ToCStr ().Get (),
						GS::UniString(dbPars.title).ToCStr ().Get (),
						GS::UniString (dbPars.name).ToCStr ().Get (),
						GS::UniString (dbPars.ref).ToCStr ().Get ());
			if (isMasterLayout || isLayout) {
				API_LayoutInfo	layoutSets;
				if (ACAPI_Navigator_GetLayoutSets (&layoutSets, &dbPars.databaseUnId) == NoError) {
					if (layoutSets.customData != nullptr) {
						delete layoutSets.customData;
						layoutSets.customData = nullptr;
					}

					if (layoutSets.layoutPageNumber <= 1 || isMasterLayout) {
						DBPrintf ("                   size (mm, x-y): %5.1f x %5.1f; margins (mm, l-r-t-b): %5.1f, %5.1f, %5.1f, %5.1f\n",
							layoutSets.sizeX, layoutSets.sizeY,
							layoutSets.leftMargin, layoutSets.rightMargin, layoutSets.topMargin, layoutSets.bottomMargin);
						if (isLayout) {
							DBPrintf ("Do not include this Layout in Numbering: %s\n", layoutSets.doNotIncludeInNumbering ? "true" : "false");
							DBPrintf ("Is custom Layout Number: %s\n", layoutSets.customLayoutNumbering ? "true" : "false");
							DBPrintf ("Custom Layout Number: %s\n", layoutSets.customLayoutNumber);
						}
					}
					else {
						DBPrintf ("                   size (mm, x-y): %5.1f x %5.1f; margins (mm, l-r-t-b): %5.1f, %5.1f, %5.1f, %5.1f\n",
							layoutSets.sizeX, layoutSets.sizeY,
							layoutSets.leftMargin, layoutSets.rightMargin, layoutSets.topMargin, layoutSets.bottomMargin);
						if (isLayout) {
							DBPrintf ("Do not include this Layout in Numbering: %s\n", layoutSets.doNotIncludeInNumbering ? "true" : "false");
							DBPrintf ("Is custom Layout Number: %s\n", layoutSets.customLayoutNumbering ? "true" : "false");
							DBPrintf ("Custom Layout Number: %s\n", layoutSets.customLayoutNumber);
						}

						ULong actPgidx;
						for (actPgidx = 1; actPgidx <= layoutSets.layoutPageNumber; actPgidx++) {
							ACAPI_Navigator_GetLayoutSets (&layoutSets, &dbPars.databaseUnId, &actPgidx);
							if (layoutSets.customData != nullptr) {
								delete layoutSets.customData;
								layoutSets.customData = nullptr;
							}
							DBPrintf ("    %d: %s\n", actPgidx, layoutSets.layoutName);
						}
					}
				}
			}
		}
	}
}		// Do_ListDatabases


// -----------------------------------------------------------------------------
// Change 3D Document settings
// -----------------------------------------------------------------------------

static void		Do_Change3DDocument ()
{
	API_DatabaseUnId	*dbases = nullptr;

	if (ACAPI_Database_GetDocumentFrom3DDatabases (&dbases) == NoError) {
		GSSize nDbases = BMpGetSize (reinterpret_cast<GSPtr>(dbases)) / Sizeof32 (API_DatabaseUnId);

		if (nDbases > 0) {
			API_DatabaseInfo dbPars;
			API_DocumentFrom3DType documentFrom3DType;
			BNZeroMemory (&dbPars, sizeof (API_DatabaseInfo));
			dbPars.databaseUnId = dbases[0];

			GSErrCode err = ACAPI_View_GetDocumentFrom3DSettings (&dbPars.databaseUnId, &documentFrom3DType);
			if (err == NoError) {
				documentFrom3DType.attributeOptions.shouldUseUniformCutAttributes = !documentFrom3DType.attributeOptions.shouldUseUniformCutAttributes;
				err = ACAPI_View_ChangeDocumentFrom3DSettings (&dbPars.databaseUnId, &documentFrom3DType);
				if (err != NoError)
					DBPrintf ("APIEnv_ChangeDocumentFrom3DSettingsID returns with error code");
			} else {
				DBPrintf ("APIEnv_GetDocumentFrom3DSettingsID returns with error code");
			}

			BMKillHandle (reinterpret_cast<GSHandle*> (&documentFrom3DType.cutSetting.shapes));
		}
	}

	if (dbases != nullptr)
		BMpFree (reinterpret_cast<GSPtr>(dbases));
}


// -----------------------------------------------------------------------------
// Dump info for drawing links in the current databasee
// -----------------------------------------------------------------------------

static void		Do_ListDrawingLinks ()
{
	GSErrCode	err = NoError;

	err = ACAPI_View_StoreViewSettings (true);
	if (err != NoError)
		return;

	API_DatabaseInfo	currentDB;
	BNZeroMemory (&currentDB, sizeof (API_DatabaseInfo));
	ACAPI_Database_GetCurrentDatabase (&currentDB);

	GS::Array<API_Guid>  drawingList;
	if (ACAPI_Element_GetElemList (API_DrawingID, &drawingList) != NoError) {
		ACAPI_WriteReport ("Error in ACAPI_Element_GetElemList ().", true);
		return;
	}

	for (auto& guid : drawingList.AsConst ()) {
		API_Element		elem = {};
		elem.header.guid = guid;

		err = ACAPI_Element_Get (&elem);
		if (err != NoError) {
			DBPrintf ("DatabaseControl :: Do_ListDrawingLinks :: Cannot get Drawing with GUID %s.\n", APIGuidToString (guid).ToCStr ().Get ());
			continue;
		}

		API_DrawingLinkInfo		drwLinkInfo;
		err = ACAPI_Drawing_GetDrawingLink ((&(elem.header.guid)), &drwLinkInfo);
		if (err != NoError) {
			DBPrintf ("DatabaseControl :: Do_ListDrawingLinks :: Cannot get link information for Drawing with GUID %s.\n", APIGuidToString (guid).ToCStr ().Get ());
			if (drwLinkInfo.linkPath != nullptr)
				delete drwLinkInfo.linkPath;
			if (drwLinkInfo.viewPath != nullptr)
				BMKillPtr (&(drwLinkInfo.viewPath));
			continue;
		}

		DBPrintf ("\tlink No. %ld :", elem.drawing.linkUId);
		if (drwLinkInfo.linkPath != nullptr) {
			DBPrintf ("\t%s", drwLinkInfo.linkPath->ToDisplayText ().ToCStr ().Get ());
			delete drwLinkInfo.linkPath;
		}
		if (drwLinkInfo.viewPath != nullptr)
			BMKillPtr (&(drwLinkInfo.viewPath));
		if (drwLinkInfo.linkTypeID == API_DrawingLink_InternalViewID)
			DBPrintf ("\t (internal %s, guid: %s)", (drwLinkInfo.viewType == API_ViewNodePerspective || drwLinkInfo.viewType == API_ViewNodeAxonometry) ? "3D view" : "view", APIGuidToString (drwLinkInfo.linkGuid).ToCStr ().Get ());

		API_DatabaseInfo	dbInfo;
		BNZeroMemory (&dbInfo, sizeof (API_DatabaseInfo));
		dbInfo.typeID = APIWind_DrawingID;
		dbInfo.linkedElement  = elem.header.guid;
		if (ACAPI_Database_ChangeCurrentDatabase (&dbInfo) == NoError) {
			GS::Array<API_Guid> elemList;
			ACAPI_Element_GetElemList (API_LineID, &elemList);
			DBPrintf ("\t(number of lines = %ld,", elemList.GetSize ());

			short	visible, onStory, inView;
			visible = onStory = inView = 0;
			for (auto& elemGuid : elemList.AsConst ()) {
				if (ACAPI_Element_Filter (elemGuid, APIFilt_OnVisLayer))
					++visible;
				if (ACAPI_Element_Filter (elemGuid, APIFilt_OnActFloor))
					++onStory;
				if (ACAPI_Element_Filter (elemGuid, APIFilt_InCroppedView))
					++inView;
			}
			DBPrintf ("%d visible, %d on the view\'s floor, %d in the cropped view", visible, onStory, inView);

			// Switch back to the original database, because APIDb_ChangeCurrentDatabaseID tries to fetch the drawing element
			//	from the layout's database internally
			ACAPI_Database_ChangeCurrentDatabase (&currentDB);
		}

		DBPrintf ("\n");

		DBPrintf ("\tName: %s", drwLinkInfo.name);
		DBPrintf ("\n");
		DBPrintf ("\tNumber %s", drwLinkInfo.number);
		DBPrintf ("\n");

		short userId = 0;
		if (ACAPI_Teamwork_GetTWOwner (&(dbInfo.databaseUnId), &userId) == NoError)
			DBPrintf ("\tTeamWork owner ID: %d\n", userId);
	}

	ACAPI_View_StoreViewSettings (false);
}		// Do_ListDrawingLinks


// -----------------------------------------------------------------------------
// Change all drawings (enumerate layout databases)
// -----------------------------------------------------------------------------

static void		Do_ChangeAllDrawings (void)
{
	API_DatabaseUnId	*dbases = nullptr;
	API_DatabaseInfo	db_orig;
	GSErrCode			err = ACAPI_Database_GetCurrentDatabase (&db_orig);
	if (err != NoError)
		return;

	err = ACAPI_CallUndoableCommand ("Drawing was changed",
		[&] () -> GSErrCode {
			API_DatabaseInfo	db_origLayout;
			bool				undoWasOpened = false;
			GSSize				nDbases = 0;
			GSErrCode			err = NoError;


			BNZeroMemory (&db_orig, sizeof (API_DatabaseInfo));
			BNZeroMemory (&db_origLayout, sizeof (API_DatabaseInfo));

			if (ACAPI_Database_GetLayoutDatabases (&dbases) == NoError) {
				nDbases = BMpGetSize (reinterpret_cast<GSPtr>(dbases)) / Sizeof32 (API_DatabaseUnId);
				DBPrintf ("DatabaseControl :: The number of layout databases is %d\n", nDbases);

				API_AttributeIndex penTableIdx;
				GS::Array<API_Attribute> attributes;
				err = ACAPI_Attribute_GetAttributesByType (API_PenTableID, attributes);
				if (!attributes.IsEmpty ())
					penTableIdx = attributes.GetLast ().header.index;

				if (penTableIdx == APIInvalidAttributeIndex) {
					DBPrintf ("Count of penTables is zero");
					return err;
				}

				for (GSIndex i = 0; i < nDbases; i++) {
					API_DatabaseInfo dbPars;
					BNZeroMemory (&dbPars, sizeof (API_DatabaseInfo));
					dbPars.databaseUnId = dbases[i];
					dbPars.typeID = APIWind_LayoutID;


					err = ACAPI_Database_ChangeCurrentDatabase (&dbPars);
					if (err != NoError) {
						DBPrintf ("APIDb_ChangeCurrentDatabaseID returns with error code");
						break;
					}

					if (!undoWasOpened) {
						undoWasOpened = true;
						db_origLayout = dbPars;
					}

					GS::Array<API_Guid>  drawingList;
					err = ACAPI_Element_GetElemList (API_DrawingID, &drawingList);
					if (err == NoError) {
						API_Element mask;
						ACAPI_ELEMENT_MASK_CLEAR (mask);
						ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, penTableUsageMode);
						ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, penTableIndex);
						ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, nameType);
						ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, numberingType);
						ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, name);
						ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, customNumber);

						for (auto& drawingGuid : drawingList.AsConst ()) {
							API_Element element = {};
							element.header.guid = drawingGuid;
							err = ACAPI_Element_Get (&element);
							if (err == NoError) {
								element.drawing.penTableUsageMode = APIPenTableUsageMode_UsePenTableIndex;
								element.drawing.penTableIndex = penTableIdx;
								element.drawing.nameType = APIName_CustomName;
								element.drawing.numberingType = APINumbering_CustomNum;
								CHCopyC ("NAME", element.drawing.name);
								CHCopyC ("NUMBER", element.drawing.customNumber);
								err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
							}
						}
					}
				}
			}

			if (nDbases > 1) {
				err = ACAPI_Database_ChangeCurrentDatabase (&db_origLayout);
				DBPrintf ("APIDb_ChangeCurrentDatabaseID returns with error code");
			}

			return err;
		});

	ACAPI_Database_ChangeCurrentDatabase (&db_orig);

	if (dbases != nullptr)
		BMpFree (reinterpret_cast<GSPtr> (dbases));
}		// Do_ChangeAllDrawings


// -----------------------------------------------------------------------------
// Change the Master Layout of the current Layout
// -----------------------------------------------------------------------------

static void		Do_ChangeMaster ()
{
	API_DatabaseInfo	db_Layout;
	ACAPI_Database_GetCurrentDatabase (&db_Layout);

	if (db_Layout.typeID != APIWind_LayoutID)
		return;

	API_DatabaseUnId	*dbases = nullptr;

	if (ACAPI_Database_GetMasterLayoutDatabases (&dbases) == NoError) {
		GSSize nDbases = BMpGetSize (reinterpret_cast<GSPtr>(dbases)) / Sizeof32 (API_DatabaseUnId);
		if (nDbases > 1) {
			for (GSIndex i = 0; i < nDbases; i++) {
				API_DatabaseInfo api_dbPars;
				BNZeroMemory (&api_dbPars, sizeof (API_DatabaseInfo));
				api_dbPars.databaseUnId = dbases[i];
				if (ACAPI_Window_GetDatabaseInfo (&api_dbPars) == NoError) {
					if (db_Layout.masterLayoutUnId.elemSetId == api_dbPars.databaseUnId.elemSetId)
						continue;

					db_Layout.masterLayoutUnId = api_dbPars.databaseUnId;
					ACAPI_Database_ModifyDatabase (&db_Layout);
					break;
				}
			}
		}
	}

	if (dbases != nullptr)
		BMpFree (reinterpret_cast<GSPtr>(dbases));

	return;
}


// -----------------------------------------------------------------------------
// Get all dominant Changes
// -----------------------------------------------------------------------------

static void		Do_GetAllChanges (void)
{
	GS::Array<API_RVMChange> changes;
	GSErrCode err = ACAPI_Revision_GetRVMChanges (&changes);
	if (err != NoError) {
		WriteReport ("Do_GetAllChanges: error occured!");
		return;
	}

	if (changes.IsEmpty ()) {
		WriteReport ("There are no changes!");
		return;
	}

	WriteReport ("# Changes:");

	char	buffer[256];
	for (auto& change : changes.AsConst ()) {
		sprintf (buffer, "ID: %s, Description: %s", change.id.ToCStr ().Get (), change.description.ToCStr ().Get ());
		WriteReport (buffer);
	}
}


// -----------------------------------------------------------------------------
// Get the custom scheme of Changes
// -----------------------------------------------------------------------------

static void		Do_GetChangeCustomScheme (void)
{
	GS::Array<API_RVMChange> changes;
	GSErrCode err = ACAPI_Revision_GetRVMChanges (&changes);
	if (err != NoError) {
		WriteReport ("Do_GetChangeCustomScheme: error occured!");
		return;
	}

	if (changes.IsEmpty ()) {
		WriteReport ("There are no changes!");
		return;
	}

	GS::HashTable<API_Guid, GS::UniString> changeCustomSchemes;
	err = ACAPI_Revision_GetRVMChangeCustomScheme (&changeCustomSchemes);
	if (err != NoError) {
		WriteReport ("Do_GetChangeCustomScheme: error occured!");
		return;
	}

	if (changeCustomSchemes.IsEmpty ()) {
		WriteReport ("There are no custom schemes for changes!");
		return;
	}

	WriteReport ("# Changes:");

	GS::UniString buffer;
	for (auto& change : changes.AsConst ()) {
		buffer = GS::UniString::Printf ("ID: %T, ", change.id.ToPrintf ());
		bool firstLoop = true;
		for (auto& cIt : changeCustomSchemes) {
			const API_Guid&			guid				= cIt.key;
			const GS::UniString&	customFieldName		= cIt.value;
			GS::UniString			customFieldValue;

			if (!firstLoop)
				buffer.Append (", ");

			change.customData.Get (guid, &customFieldValue);
			buffer.Append (customFieldName + ": " + customFieldValue);

			firstLoop = false;
		}

		WriteReport (buffer.ToCStr ().Get ());
	}
}


// -----------------------------------------------------------------------------
// Get all Issues
// -----------------------------------------------------------------------------

static void		Do_GetAllIssues (void)
{
	GS::Array<API_RVMIssue> issues;
	GSErrCode err = ACAPI_Revision_GetRVMIssues (&issues);
	if (err != NoError) {
		WriteReport ("Do_GetAllIssues: error occured!");
		return;
	}

	if (issues.IsEmpty ()) {
		WriteReport ("There are no issues!");
		return;
	}

	WriteReport ("# Issues:");

	char	buffer[256];
	for (auto& issue : issues.AsConst ()) {
		sprintf (buffer, "ID: %s, Description: %s", issue.id.ToCStr ().Get (), issue.description.ToCStr ().Get ());
		WriteReport (buffer);
	}
}


// -----------------------------------------------------------------------------
// Get the custom scheme of Issues
// -----------------------------------------------------------------------------

static void		Do_GetIssueCustomScheme (void)
{
	GS::Array<API_RVMIssue> issues;
	GSErrCode err = ACAPI_Revision_GetRVMIssues (&issues);
	if (err != NoError) {
		WriteReport ("Do_GetIssueCustomScheme: error occured!");
		return;
	}

	if (issues.IsEmpty ()) {
		WriteReport ("There are no issues!");
		return;
	}

	GS::HashTable<API_Guid, GS::UniString> issueCustomSchemes;
	err = ACAPI_Revision_GetRVMIssueCustomScheme (&issueCustomSchemes);
	if (err != NoError) {
		WriteReport ("Do_GetIssueCustomScheme: error occured!");
		return;
	}

	if (issueCustomSchemes.IsEmpty ()) {
		WriteReport ("There are no custom schemes for issues!");
		return;
	}

	WriteReport ("# Issues:");

	for (auto& issue : issues.AsConst ()) {
		GS::UniString buffer = GS::UniString::Printf ("ID: %T, ", issue.id.ToPrintf ());
		bool firstLoop = true;
		for (auto& cIt : issueCustomSchemes) {
			const API_Guid&			guid				= cIt.key;
			const GS::UniString&	customFieldName		= cIt.value;
			GS::UniString			customFieldValue;

			if (!firstLoop)
				buffer.Append (", ");

			issue.customData.Get (guid, &customFieldValue);
			buffer.Append (customFieldName + ": " + customFieldValue);

			firstLoop = false;
		}

		WriteReport (buffer.ToCStr ().Get ());
	}
}


// -----------------------------------------------------------------------------
// Get all Document Revision
// -----------------------------------------------------------------------------

static void		Do_GetAllDocumentRevisions (void)
{
	GS::Array<API_RVMDocumentRevision> revisions;
	GSErrCode err = ACAPI_Revision_GetRVMDocumentRevisions (&revisions);
	if (err != NoError) {
		WriteReport ("Do_GetAllDocumentRevisions: error occured!");
		return;
	}

	if (revisions.IsEmpty ()) {
		WriteReport ("There are no document revisions!");
		return;
	}

	WriteReport ("# Document revisions:");

	char buffer[256];
	for (auto& revision : revisions.AsConst ()) {
		sprintf (buffer, "ID: %s, Layout name: %s", revision.id.ToCStr ().Get (), revision.layoutInfo.name.ToCStr ().Get ());
		WriteReport (buffer);
	}
}


// -----------------------------------------------------------------------------
// Get the Document Revisions of the given Issue
// -----------------------------------------------------------------------------

static void		Do_GetIssueDocumentRevisions (void)
{
	GS::Array<API_RVMIssue> issues;
	GSErrCode err = ACAPI_Revision_GetRVMIssues (&issues);
	if (err != NoError) {
		WriteReport ("Do_GetIssueDocumentRevisions: error occured!");
		return;
	}

	if (issues.IsEmpty ()) {
		WriteReport ("There are no issues!");
		return;
	}

	GS::Array<API_RVMDocumentRevision> revisions;
	err = ACAPI_Revision_GetRVMIssueDocumentRevisions (&(issues[0].guid), &revisions);
	if (err != NoError) {
		WriteReport ("Do_GetIssueDocumentRevisions: error occured!");
		return;
	}

	char buffer[256];

	if (revisions.IsEmpty ()) {
		sprintf (buffer, "There are no document revisions in {%s} issue!", APIGuid2GSGuid (issues[0].guid).ToUniString ().ToCStr ().Get ());
		WriteReport (buffer);
		return;
	}

	sprintf (buffer, "# Document revisions in {%s} issue:", APIGuid2GSGuid (issues[0].guid).ToUniString ().ToCStr ().Get ());
	WriteReport (buffer);

	for (auto& revision : revisions.AsConst ()) {
		sprintf (buffer, "ID: %s, Layout name: %s", revision.finalId.ToCStr ().Get (), revision.layoutInfo.name.ToCStr ().Get ());
		WriteReport (buffer);
	}
}


// -----------------------------------------------------------------------------
// Get the Changes of the given DocumentRevision
// -----------------------------------------------------------------------------

static void		Do_GetDocumentRevisionChanges (void)
{
	GS::Array<API_RVMDocumentRevision> revisions;
	GSErrCode err = ACAPI_Revision_GetRVMDocumentRevisions (&revisions);
	if (err != NoError) {
		WriteReport ("Do_GetDocumentRevisionChanges: error occured!");
		return;
	}

	if (revisions.IsEmpty ()) {
		WriteReport ("There are no document revisions!");
		return;
	}

	GS::Array<API_RVMChange> changes;
	err = ACAPI_Revision_GetRVMDocumentRevisionChanges (&(revisions[0].guid), &changes);
	if (err != NoError) {
		WriteReport ("Do_GetDocumentRevisionChanges: error occured!");
		return;
	}

	char buffer[256];

	if (changes.IsEmpty ()) {
		sprintf (buffer, "There are no changes in {%s} document revision!", APIGuid2GSGuid (revisions[0].guid).ToUniString ().ToCStr ().Get ());
		WriteReport (buffer);
		return;
	}

	sprintf (buffer, "# Changes in {%s} document revision:", APIGuid2GSGuid (revisions[0].guid).ToUniString ().ToCStr ().Get ());
	WriteReport (buffer);

	for (auto& change : changes.AsConst ()) {
		sprintf (buffer, "ID: %s, Description: %s", change.id.ToCStr ().Get (), change.description.ToCStr ().Get ());
		WriteReport (buffer);
	}
}


// -----------------------------------------------------------------------------
// Get the Changes from the current Document Revision of the given Layout
// -----------------------------------------------------------------------------

static void		Do_GetLayoutCurrentRevisionChanges (void)
{
	API_DatabaseInfo dbInfo;
	BNZeroMemory (&dbInfo, sizeof (API_DatabaseInfo));
	GSErrCode err = ACAPI_Database_GetCurrentDatabase (&dbInfo);
	if (err != NoError) {
		WriteReport ("Do_GetLayoutCurrentRevisionChanges: error occured!");
		return;
	}

	if (dbInfo.typeID != APIWind_LayoutID) {
		WriteReport ("Current database not a layout database!");
		return;
	}

	GS::Array<API_RVMChange> changes;
	err = ACAPI_Revision_GetRVMLayoutCurrentRevisionChanges (&(dbInfo.databaseUnId), &changes);
	if (err != NoError) {
		WriteReport ("Do_GetLayoutCurrentRevisionChanges: error occured!");
		return;
	}

	char buffer[256];

	if (changes.IsEmpty ()) {
		sprintf (buffer, "There are no changes in current revision of {%s} layout!", APIGuid2GSGuid (dbInfo.databaseUnId.elemSetId).ToUniString ().ToCStr ().Get ());
		WriteReport (buffer);
		return;
	}

	sprintf (buffer, "# Changes in current revision of {%s} layout:", APIGuid2GSGuid (dbInfo.databaseUnId.elemSetId).ToUniString ().ToCStr ().Get ());
	WriteReport (buffer);

	for (auto& change : changes.AsConst ()) {
		sprintf (buffer, "ID: %s, Description: %s", change.id.ToCStr ().Get (), change.description.ToCStr ().Get ());
		WriteReport (buffer);
	}
}

// -----------------------------------------------------------------------------
// Export GUID and a few basic parameters of all walls placed in the current project.
// -----------------------------------------------------------------------------

#ifdef WINDOWS
#define UNISTR_TO_LIBXLSTR(str) (str.ToUStr ())
#else
#define UNISTR_TO_LIBXLSTR(str) (str.ToCStr ())
#endif

static void	Do_ExportWallsToExcel (void)
{
	DBPrintf ("Exporting walls to Excel document...\n");

	libxl::Book* book = xlCreateXMLBook ();
#ifdef macintosh
	book->setLocale ("UTF-8");
#endif

	libxl::Sheet* sheet = book->addSheet (UNISTR_TO_LIBXLSTR (GS::UniString("Walls exported from Archicad")));

	libxl::Font* guidFormatFont = book->addFont ();
	guidFormatFont->setSize (6);
	guidFormatFont->setColor (libxl::COLOR_GRAY50);
	libxl::Format* guidFormat = book->addFormat ();
	guidFormat->setFont (guidFormatFont);

	GS::Array<GS::UniString> titles =
		{ "GUID", "Height", "BeginCoordX", "BeginCoordY", "EndCoordX", "EndCoordY" };

	for (UIndex ii = 0; ii < titles.GetSize (); ++ii) {
		sheet->writeStr (0, ii, UNISTR_TO_LIBXLSTR (titles[ii]));
	}

	GS::Array<API_Guid> apiguids;
	ACAPI_Element_GetElemList (API_WallID, &apiguids);
	for (UIndex ii = 0; ii < apiguids.GetSize (); ++ii) {
		GS::UniString guidStr = APIGuidToString (apiguids[ii]);

		API_Element element = {};
		element.header.guid = apiguids[ii];
		GSErrCode err = ACAPI_Element_Get (&element);
		if (err != NoError) {
			DBPrintf ("ACAPI_Element_Get returned error (%d) for GUID '%s'\n", err,
					  guidStr.ToCStr ().Get ());
			continue;
		}

		const API_WallType& wall = element.wall;
		//DBPrintf ("GUID '%s'\n", guidStr.ToCStr ().Get ());

		sheet->writeStr (ii + 1, 0, UNISTR_TO_LIBXLSTR (guidStr), guidFormat);
		sheet->writeNum (ii + 1, 1, wall.height);
		sheet->writeNum (ii + 1, 2, wall.begC.x);
		sheet->writeNum (ii + 1, 3, wall.begC.y);
		sheet->writeNum (ii + 1, 4, wall.endC.x);
		sheet->writeNum (ii + 1, 5, wall.endC.y);
	}

	IO::Location location;
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::UserDocuments, &location);
	location.AppendToLocal (IO::Name ("export.xlsx"));
	GS::UniString filepath;
	location.ToPath (&filepath);

	DBVERIFY (book->save (UNISTR_TO_LIBXLSTR(filepath)));
	book->release ();

	DBPrintf ("Export operation finished\n");
}

// -----------------------------------------------------------------------------
// Add all exported walls to selection. (All parameters but GUID is ignored.)
// -----------------------------------------------------------------------------

static void	Do_SelectWallsFromExcel (void)
{
	DBPrintf ("Selecting walls based on Excel document...\n");

	GS::Array<API_Guid> apiguids;
	ACAPI_Element_GetElemList (API_WallID, &apiguids);
	GS::Array<API_Guid> selection;

	libxl::Book* book = xlCreateXMLBook ();
	if (DBERROR (book == nullptr))
		return;

	IO::Location location;
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::UserDocuments, &location);
	location.AppendToLocal (IO::Name ("export.xlsx"));
	GS::UniString filepath;
	location.ToPath (&filepath);

	bool loaded = book->load (UNISTR_TO_LIBXLSTR(filepath));
	if (DBERROR (!loaded))
		return;

	libxl::Sheet* sheet = book->getSheet (0);
	if (DBERROR (sheet == nullptr))
		return;

	for (int i = 0; i < sheet->lastRow (); ++i) {
		GS::Guid xlGsGuid;
		const auto* cellContentPtr = sheet->readStr (i, 0);
		if (cellContentPtr == nullptr)
			continue;

		GSErrCode err = xlGsGuid.ConvertFromString (cellContentPtr);
		if (err != NoError)
			continue;

		API_Guid xlApiGuid = GSGuid2APIGuid (xlGsGuid);
		if (!apiguids.Contains (xlApiGuid))
			continue;
		selection.Push (xlApiGuid);
	}

	book->release ();

	USize selCount = selection.GetSize ();
	GS::Array<API_Neig> selNeigs;
	for (const API_Guid& elemGuid : selection) {
		selNeigs.PushNew (elemGuid);
	}

	GSErrCode err = ACAPI_Selection_Select (selNeigs, true);
	if (DBERROR (err != NoError))
		DBPrintf ("Selection of %d wall(s) failed.\n", selCount);
	else
		DBPrintf ("%d walls successfully selected\n", selCount);
}

// -----------------------------------------------------------------------------
// Store Renovation filter.
// -----------------------------------------------------------------------------

void Do_StoreRenovationFilter ()
{
	GSErrCode err = ACAPI_Renovation_GetActualRenovationFilter (&storedRenovationFilter);
	if (DBVERIFY (err == NoError))
		ACAPI_WriteReport ("Renovation Filter stored successfull.", true);
	else
		ACAPI_WriteReport ("Renovation Filter store failed!", true);

	ACAPI_KeepInMemory (true);
}

// -----------------------------------------------------------------------------
// Restore Renovation filter.
// -----------------------------------------------------------------------------

void Do_RestoreRenovationFilter ()
{
	if (storedRenovationFilter == APINULLGuid) {
		ACAPI_WriteReport ("Please Store Renovation Filter first!", true);
	} else {
		GSErrCode err = ACAPI_Renovation_SetActualRenovationFilter (&storedRenovationFilter);
		if (DBVERIFY (err == NoError))
			WriteReport ("Renovation Filter restored successfull.", true);
		else
			WriteReport ("Renovation Filter restore failed!", true);
	}

	ACAPI_KeepInMemory (false);
}

// -----------------------------------------------------------------------------
// InitPreferences
//		initialize preference data
// -----------------------------------------------------------------------------
static void	InitPreferences (void)
{
	myPrefs = new PrefsData;
	if (myPrefs != nullptr) {
		Int32	version;
		GSSize	nBytes;

		ACAPI_GetPreferences (&version, &nBytes, nullptr);
		if (version == 1 && nBytes == sizeof (PrefsData)) {
			ACAPI_GetPreferences (&version, &nBytes, myPrefs);
			if (myPrefs->platform != GS::Act_Platform_Sign) {
				IVULong ((GS::PlatformSign) (myPrefs->platform), &myPrefs->lastDetail);
				IVULong ((GS::PlatformSign) (myPrefs->platform), &myPrefs->lastWorksheet);
				IVULong ((GS::PlatformSign) (myPrefs->platform), &myPrefs->lastLayout);
				IVULong ((GS::PlatformSign) (myPrefs->platform), &myPrefs->last3DDocument);
				myPrefs->platform = GS::Act_Platform_Sign;
			}
		} else {
			myPrefs->platform = GS::Act_Platform_Sign;
			myPrefs->lastDetail = myPrefs->lastWorksheet = myPrefs->lastLayout = myPrefs->last3DDocument = 0;
		}
	}
}		// InitPreferences


// -----------------------------------------------------------------------------
// Menu command handler
// -----------------------------------------------------------------------------

static GSErrCode MenuCommandProc (const API_MenuParams* menuParams)
{
	DBPrintf ("Database Control Add-On User Menu called, ResID: %d, Item No.: %d\n",
				menuParams->menuItemRef.menuResID, menuParams->menuItemRef.itemIndex);

	switch (menuParams->menuItemRef.itemIndex) {

		case ID_MENU_ListSections:		Do_ListSectionLines ();													break;
		// -------

		case ID_MENU_OpenText:			Do_OpenMyWindow (APIWind_MyTextID);										break;
		case ID_MENU_CloseText:			Do_CloseMyWindow (APINULLGuid, APIWind_MyTextID);						break;
		case ID_MENU_OpenDraw:			Do_OpenMyWindow (APIWind_MyDrawID);										break;
		case ID_MENU_CloseDraw:			Do_CloseMyWindow (APINULLGuid, APIWind_MyDrawID);						break;
		case ID_MENU_CloseAll:			Do_CloseAllWindows ();													break;
		// -------
		case ID_MENU_CreateNewDetail:	Do_CreateDetail ();														break;
		case ID_MENU_RenameDetail:		Do_RenameDatabase (APIWind_DetailID);									break;
		case ID_MENU_DeleteDetail:		Do_DeleteDatabase (APIWind_DetailID);									break;
		case ID_MENU_ListDetails:		Do_ListDatabases (APIWind_DetailID, "detail");							break;
		// -------
		case ID_MENU_CreateNewWorksheet:	Do_CreateWorksheet ();												break;
		case ID_MENU_RenameWorksheet:		Do_RenameDatabase (APIWind_WorksheetID);							break;
		case ID_MENU_DeleteWorksheet:		Do_DeleteDatabase (APIWind_WorksheetID);							break;
		case ID_MENU_ListWorksheets:		Do_ListDatabases (APIWind_WorksheetID, "worksheet");				break;
		// -------
		case ID_MENU_CreateNew3DDocument:	Do_Create3DDocument ();												break;
		case ID_MENU_Rename3DDocument:		Do_RenameDatabase (APIWind_DocumentFrom3DID); 		break;
		case ID_MENU_Delete3DDocument:		Do_DeleteDatabase (APIWind_DocumentFrom3DID);						break;
		case ID_MENU_List3DDocuments:		Do_ListDatabases (APIWind_DocumentFrom3DID, "3DDocument"); 			break;
		case ID_MENU_Change3DDocument:		Do_Change3DDocument ();												break;
		// -------
		case ID_MENU_CreateNewLayout:	Do_CreateLayout ();														break;
		case ID_MENU_RenameLayout:		Do_RenameDatabase (APIWind_LayoutID);									break;
		case ID_MENU_DeleteLayout:		Do_DeleteDatabase (APIWind_LayoutID);									break;
		case ID_MENU_ListLayouts:		Do_ListDatabases (APIWind_LayoutID, "layout");
										Do_ListDatabases (APIWind_MasterLayoutID, "master layout");				break;
		case ID_MENU_ListDrawingLinks:	Do_ListDrawingLinks ();													break;
		case ID_MENU_ChangeAllDrawings:	Do_ChangeAllDrawings ();												break;
		case ID_MENU_ChangeMaster:		Do_ChangeMaster ();														break;
		// -------
		case ID_MENU_ListSections_New:			Do_ListDatabases (APIWind_SectionID, "section");				break;
		case ID_MENU_RenameSection:				Do_RenameDatabase (APIWind_SectionID); 							break;
		case ID_MENU_ListElevations:			Do_ListDatabases (APIWind_ElevationID, "elevation");			break;
		case ID_MENU_ListInteriorElevations:	Do_ListDatabases (APIWind_InteriorElevationID,
																  "interior elevation");						break;
		case ID_MENU_CreateSubset:				Do_CreateSubset ();												break;
		// -------
		case ID_MENU_GetAllChanges:						Do_GetAllChanges ();									break;
		case ID_MENU_GetChangeCustomScheme:				Do_GetChangeCustomScheme ();							break;
		case ID_MENU_GetAllIssues:						Do_GetAllIssues ();										break;
		case ID_MENU_GetIssueCustomScheme:				Do_GetIssueCustomScheme ();								break;
		case ID_MENU_GetAllDocumentRevisions:			Do_GetAllDocumentRevisions ();							break;
		case ID_MENU_GetIssueDocumentRevisions:			Do_GetIssueDocumentRevisions ();						break;
		case ID_MENU_GetDocumentRevisionChanges:		Do_GetDocumentRevisionChanges ();						break;
		case ID_MENU_GetLayoutCurrentRevisionChanges:	Do_GetLayoutCurrentRevisionChanges ();					break;
		// --------
		case ID_MENU_ExportWallsToExcel:				Do_ExportWallsToExcel ();								break;
		case ID_MENU_SelectWallsFromExcel:				Do_SelectWallsFromExcel ();								break;
		// --------
		case ID_MENU_GetRenovationFilter:				Do_StoreRenovationFilter ();							break;
		case ID_MENU_SetRenovationFilter:				Do_RestoreRenovationFilter ();							break;
	}

	return NoError;
}


// =============================================================================
//
// Required functions
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif

//------------------------------------------------------------------------------
// Called when the Add-On is going to be registered
//------------------------------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	// Fill in the necessary information
	RSGetIndString (&envir->addOnInfo.name, IDR_AddOnDescStrings, IDS_AddOnName, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, IDR_AddOnDescStrings, IDS_AddOnDesc, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


//------------------------------------------------------------------------------
// Interface definitions
//------------------------------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	GSErrCode	err;

	// Register menus
	err = ACAPI_MenuItem_RegisterMenu (IDR_OwnMenu, IDR_OwnMenuStrs, MenuCode_UserDef, MenuFlag_SeparatorBefore); //or MenuFlag_Default

	return err;
}


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------
GSErrCode Initialize (void)
{
	GSErrCode	err;

	// pass the handler function pointer to ARCHICAD
	err = ACAPI_MenuItem_InstallMenuHandler (IDR_OwnMenu, MenuCommandProc);
    if (err != NoError)
        DBPrintf ("Initialize():: ACAPI_MenuItem_InstallMenuHandler failed\n");


	InitPreferences ();

	return err;
}


//------------------------------------------------------------------------------
// Called when the Add-On is going to be unloaded
//------------------------------------------------------------------------------
GSErrCode FreeData (void)
{

	if (myPrefs != nullptr) {
		ACAPI_SetPreferences (1L, sizeof (PrefsData), (const void *) myPrefs);
		delete myPrefs;
	}

	return NoError;
}
