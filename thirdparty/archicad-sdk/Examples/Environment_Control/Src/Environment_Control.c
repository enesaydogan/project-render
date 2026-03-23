// *****************************************************************************
// Source code for the Environment Control Add-On
// *****************************************************************************

#define	_ENVIRONMENT_CONTROL_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"FileSystem.hpp"
#include	"Folder.hpp"
#include	"Name.hpp"
#include	"StringConversion.hpp"
#include	"DG.h"


// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------



// =============================================================================
//
//
//
// =============================================================================

// -----------------------------------------------------------------------------
// Testing various Environment functions
// -----------------------------------------------------------------------------
static void		Do_TestEnvironmentFunctions (void)
{
	{
		API_SharingInfo sharingInfo;
		BNZeroMemory (&sharingInfo, sizeof (API_SharingInfo));
		ACAPI_Teamwork_ProjectSharing (&sharingInfo);
	}
	{
		API_ProjectNoteInfo projectNoteInfo;
		BNZeroMemory (&projectNoteInfo, sizeof (API_ProjectNoteInfo));
		GSErrCode err = ACAPI_ProjectSetting_GetProjectNotes (&projectNoteInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");

		err = ACAPI_ProjectSetting_ChangeProjectNotes (&projectNoteInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}

	{
		GS::Array<API_LibraryInfo> activeLibs;
		GSErrCode err = ACAPI_LibraryManagement_GetLibraries (&activeLibs);
		if (err != NoError)
			WriteReport_Alert ("Error");

		GS::Array<API_LibraryInfo> emptyLibs;
		err = ACAPI_LibraryManagement_SetLibraries (&emptyLibs);
		if (err != NoError)
			WriteReport_Alert ("Error");

		err = ACAPI_LibraryManagement_SetLibraries (&activeLibs);
		if (err != NoError)
			WriteReport_Alert ("Error");

		err = ACAPI_LibraryManagement_ResetLibraries ();
		if (err != NoError)
			WriteReport_Alert ("Error");

		IO::Location libLocation ("D:\\Temp");
		err = ACAPI_LibraryManagement_AddLibraries (&libLocation);
		if (err != NoError)
			WriteReport_Alert ("Error");

		err = ACAPI_LibraryManagement_CheckLibraries ();
		if (err != NoError)
			WriteReport_Alert ("Error");

		err = ACAPI_LibraryManagement_OverwriteLibPart (true);
		if (err != NoError)
			WriteReport_Alert ("Error");

		err = ACAPI_LibraryManagement_OverwriteLibPart (false);
		if (err != NoError)
			WriteReport_Alert ("Error");

		API_Guid guid;
		BNZeroMemory (&guid, sizeof (API_Guid));
		err = ACAPI_LibraryPart_GetLibPartRefGuid (&libLocation, &guid);
		if (err != NoError)
			WriteReport_Alert ("Error");

		char guidStr[512];
		ACAPI_LibraryPart_GetLibPartUnIDStr (&libLocation, (char*)&guidStr);
	}
	{
		API_ToolBoxItem toolBoxItem;
		BNZeroMemory (&toolBoxItem, sizeof (API_ToolBoxItem));
		GSErrCode err = ACAPI_Toolbox_GetToolBoxMode (&toolBoxItem);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		API_GhostStoryType storyInfo;
		BNZeroMemory (&storyInfo, sizeof (API_GhostStoryType));
		GSErrCode err = ACAPI_View_GetGhostStorySettings (&storyInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		API_SunInfo sunInfo;
		BNZeroMemory (&sunInfo, sizeof (API_SunInfo));
		GSErrCode err = ACAPI_GeoLocation_GetSunSets (&sunInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		bool canChange;
		GSErrCode err = ACAPI_GeoLocation_CanChangePlaceSets (&canChange);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		API_PlaceInfo placeInfo;
		BNZeroMemory (&placeInfo, sizeof (API_PlaceInfo));

		GSErrCode err = ACAPI_GeoLocation_GetPlaceSets (&placeInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");

		err = ACAPI_GeoLocation_ChangePlaceSets (&placeInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		Int32 date[6];
		for (Int32 i = 0; i < 6; ++i)
			date[i] = 0;

		GSErrCode err = ACAPI_LibraryManagement_GetGDLDate ((Int32*) &date);
		if (err != NoError)
			WriteReport_Alert ("Error");

		date[5]++;

		err = ACAPI_LibraryManagement_ChangeGDLDate ((Int32*) &date);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		API_MagicWandInfo magicInfo;
		BNZeroMemory (&magicInfo, sizeof (API_MagicWandInfo));
		GSErrCode err = ACAPI_UserInput_GetMagicWandSets (&magicInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		double tolerance = 0.0;
		ACAPI_ProjectSetting_GetExportTolerance (&tolerance);
	}
	{
		API_3DProjectionInfo projectionInfo;
		BNZeroMemory (&projectionInfo, sizeof (API_3DProjectionInfo));

		GSErrCode err = ACAPI_View_Get3DProjectionSets (&projectionInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");

		projectionInfo.isPersp = true;

		err = ACAPI_View_Change3DProjectionSets (&projectionInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		API_RendEffects rendEffects;
		BNZeroMemory (&rendEffects, sizeof (API_RendEffects));

		GSErrCode err = ACAPI_Rendering_GetRenderingSets (&rendEffects, APIRendSet_EffectsID);
		if (err != NoError)
			WriteReport_Alert ("Error");

		err = ACAPI_Rendering_ChangeRenderingSets (&rendEffects, APIRendSet_EffectsID);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		API_3DWindowInfo windowInfo;
		BNZeroMemory (&windowInfo, sizeof (API_3DWindowInfo));

		GSErrCode err = ACAPI_View_Get3DWindowSets (&windowInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");

		windowInfo.hSize /= 2;
		windowInfo.vSize /= 2;
		err = ACAPI_View_Change3DWindowSets (&windowInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		bool isShown = false;
		GSErrCode err = ACAPI_View_GetShowHideState (&isShown);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		API_MiscPrinterInfo printerInfo = {};
		GSErrCode err = ACAPI_ProjectOperation_GetMiscPrinterInfo (&printerInfo);
		if (err != NoError)
			WriteReport_Alert ("Error");

	}
	{
		API_MiscPlotterInfo info;
		BNZeroMemory (&info, sizeof (API_MiscPlotterInfo));

		GSErrCode err = ACAPI_ProjectOperation_GetMiscPlotterInfo (&info);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		Int32 num = 0;
		GSErrCode err = ACAPI_Navigator_GetNavigatorSetNum (&num);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		API_Guid navigatorItemGuid;
		API_NavigatorItem navigatorItem;
		BNZeroMemory (&navigatorItem, sizeof (API_NavigatorItem));
		BNZeroMemory (&navigatorItemGuid, sizeof (API_Guid));
		ACAPI_Navigator_GetNavigatorParentItem (&navigatorItemGuid, &navigatorItem);
	}
	{
		API_NavigatorItem navigatorItem;
		GS::Array<API_NavigatorItem> foundItems;
		ACAPI_Navigator_SearchNavigatorItem (&navigatorItem, &foundItems);
	}
	{
		API_AttributeIndex layerCombInd = ACAPI_CreateAttributeIndex (3);
		GSErrCode err = ACAPI_Navigator_ChangeCurrLayerComb (&layerCombInd);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		bool isAutoIntersectOn = false;
		GSErrCode err = ACAPI_View_IsAutoIntersectOn (&isAutoIntersectOn);
		if (err != NoError)
			WriteReport_Alert ("Error");

		isAutoIntersectOn = !isAutoIntersectOn;
		err = ACAPI_View_ChangeAutoIntersect (&isAutoIntersectOn);
		if (err != NoError)
			WriteReport_Alert ("Error");

		isAutoIntersectOn = !isAutoIntersectOn;
		err = ACAPI_View_ChangeAutoIntersect (&isAutoIntersectOn);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		bool isInCurtainWallEditMode = false;
		GSErrCode err = ACAPI_HierarchicalEditing_IsInCurtainWallEditMode (&isInCurtainWallEditMode);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		bool isInStairEditMode = false;
		GSErrCode err = ACAPI_HierarchicalEditing_IsInStairEditMode (&isInStairEditMode);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		bool hasRight = false;
		ACAPI_Teamwork_GetTWAccessRight (APISaveAsIFC, &hasRight);
	}
	{
		bool isSuspendGroupOn = false;
		GSErrCode err = ACAPI_View_IsSuspendGroupOn (&isSuspendGroupOn);
		if (err != NoError)
			WriteReport_Alert ("Error");

		isSuspendGroupOn = !isSuspendGroupOn;
		err = ACAPI_Grouping_ChangeSuspendGroup (isSuspendGroupOn);
		if (err != NoError)
			WriteReport_Alert ("Error");

		isSuspendGroupOn = !isSuspendGroupOn;
		err = ACAPI_Grouping_ChangeSuspendGroup (isSuspendGroupOn);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		{
			API_WorkingUnitPrefs	unitPrefs;
			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&unitPrefs,APIPrefs_WorkingUnitsID);
			if (err != NoError)
				WriteReport_Alert ("Error");

			API_WorkingUnitPrefs oldUnitPrefs = unitPrefs;
			unitPrefs.lengthUnit = API_LengthTypeID::Centimeter;
			unitPrefs.areaUnit = API_AreaTypeID::SquareCentimeter;
			unitPrefs.volumeUnit = API_VolumeTypeID::CubicCentimeter;
			unitPrefs.lenDecimals = 3;
			unitPrefs.areaDecimals = 3;
			unitPrefs.volumeDecimals = 3;

			err = ACAPI_ProjectSetting_SetPreferences (&unitPrefs, APIPrefs_WorkingUnitsID);
			if (err != NoError)
				WriteReport_Alert ("Error");

			err = ACAPI_ProjectSetting_SetPreferences (&oldUnitPrefs, APIPrefs_WorkingUnitsID);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
		{
			API_CalcUnitPrefs			unitPrefs;
			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&unitPrefs,APIPrefs_CalcUnitsID);
			if (err != NoError)
				WriteReport_Alert ("Error");

			API_LengthTypeID oldLengthUnit = unitPrefs.length.unit;
			unitPrefs.length.unit = API_LengthTypeID::Centimeter;

			err = ACAPI_ProjectSetting_SetPreferences (&unitPrefs, APIPrefs_CalcUnitsID);
			if (err != NoError)
				WriteReport_Alert ("Error");

			unitPrefs.length.unit = oldLengthUnit;
			err = ACAPI_ProjectSetting_SetPreferences (&unitPrefs, APIPrefs_CalcUnitsID);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
		{
			API_DimensionPrefs			dimPrefs;
			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&dimPrefs,APIPrefs_DimensionsID);
			if (err != NoError)
				WriteReport_Alert ("Error");

			API_LengthTypeID oldUnit = dimPrefs.linear.unit;
			dimPrefs.linear.unit = API_LengthTypeID::Centimeter;

			err = ACAPI_ProjectSetting_SetPreferences (&dimPrefs, APIPrefs_DimensionsID);
			if (err != NoError)
				WriteReport_Alert ("Error");

			dimPrefs.linear.unit = oldUnit;
			err = ACAPI_ProjectSetting_SetPreferences (&dimPrefs, APIPrefs_DimensionsID);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
		{
			API_LegacyPrefs				legacyPrefs;
			BNZeroMemory (&legacyPrefs, sizeof (API_LegacyPrefs));

			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&legacyPrefs,APIPrefs_LegacyID);
			if (err != NoError)
				WriteReport_Alert ("Error");

		}
		{
			API_ZonePrefs				zonePrefs;
			BNZeroMemory (&zonePrefs, sizeof (API_ZonePrefs));

			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&zonePrefs,APIPrefs_ZonesID);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
		{
			API_SafetyPrefs				safetyPrefs;
			BNZeroMemory (&safetyPrefs, sizeof (API_SafetyPrefs));

			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&safetyPrefs,APIPrefs_DataSafetyID);
			if (err != NoError)
				WriteReport_Alert ("Error");
			else
				delete safetyPrefs.tempFolder;
		}
		{
			API_CalcUnitPrefs			calcUnitPrefs;
			BNZeroMemory (&calcUnitPrefs, sizeof (API_CalcUnitPrefs));

			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&calcUnitPrefs,APIPrefs_CalcUnitsID);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
		{
			API_CalcRulesPrefs calcRulesPrefs = {};

			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&calcRulesPrefs, APIPrefs_CalcRulesID);
			if (err != NoError)
				WriteReport_Alert ("Error");

			API_CalcRulesPrefs invalidCalcRulesPrefs = {};
			invalidCalcRulesPrefs.elementRules = {
				API_ConditionalElementRule { APIConditionalElementPropertyType_Volume, 1.0, { API_WallID, API_SlabID, API_BeamID } },
				API_ConditionalElementRule { APIConditionalElementPropertyType_Volume, 2.0, { API_WallID, API_RoofID } }
			};
			err = ACAPI_ProjectSetting_SetPreferences (&invalidCalcRulesPrefs, APIPrefs_CalcRulesID);
			if (err != APIERR_BADPARS)
				WriteReport_Alert ("Error: invalid conditional element rules are allowed.");

			API_CalcRulesPrefs validCalcRulesPrefs = {};
			validCalcRulesPrefs.elementRules = {
				API_ConditionalElementRule { APIConditionalElementPropertyType_Volume, 1.0, { API_WallID, API_SlabID, API_BeamID } },
				API_ConditionalElementRule { APIConditionalElementPropertyType_Surface, 2.0, { API_ShellID, API_RoofID } },
				API_ConditionalElementRule { APIConditionalElementPropertyType_Length, 3.0, { API_WallID } }
			};

			err = ACAPI_ProjectSetting_SetPreferences (&validCalcRulesPrefs, APIPrefs_CalcRulesID);
			if (err != NoError)
				WriteReport_Alert ("Error");

			err = ACAPI_ProjectSetting_SetPreferences (&calcRulesPrefs, APIPrefs_CalcRulesID);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
		{
			API_RefLevelsPrefs refLevelsPrefs = {};

			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&refLevelsPrefs,APIPrefs_ReferenceLevelsID);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
		{
			API_ImagingAndCalcPrefs		imagingAndCalcPrefs = {};

			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&imagingAndCalcPrefs,APIPrefs_ImagingAndCalcID);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
		{
			API_FloorPlanCutDefinition	fpCutDefPrefs;
			BNZeroMemory (&fpCutDefPrefs, sizeof (API_FloorPlanCutDefinition));

			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&fpCutDefPrefs,APIPrefs_FloorPlanCutPlaneDef);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
		{
			API_LayoutsPrefs			layoutsPrefs;
			BNZeroMemory (&layoutsPrefs, sizeof (API_LayoutsPrefs));

			GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&layoutsPrefs,APIPrefs_LayoutsID);
			if (err != NoError)
				WriteReport_Alert ("Error");
		}
	}
	{
		Int32 libIndex = 1;
		IO::Location libLocation;
		GSErrCode err = ACAPI_LibraryManagement_GetLibPartOrigLocation (&libIndex, &libLocation);
		if (err != NoError)
			WriteReport_Alert ("Error");
	}
	{
		GSErrCode commandErrorCode = ACAPI_CallCommand (GS::UniString ("ChangeProjectNotesAndCreateGhostRecordCommand"), [&] () ->GSErrCode {
			API_ProjectNoteInfo	noteInfo;
			BNZeroMemory (&noteInfo, sizeof (API_ProjectNoteInfo));
			GSErrCode			err;

			CHCopyC ("client", noteInfo.client);
			CHCopyC ("projectName", noteInfo.projectName);
			CHCopyC ("company", noteInfo.company);
			CHCopyC ("street", noteInfo.street);
			CHCopyC ("city", noteInfo.city);
			CHCopyC ("country", noteInfo.country);
			CHCopyC ("code", noteInfo.code);
			CHCopyC ("architect", noteInfo.architect);
			CHCopyC ("draftsmen", noteInfo.draftsmen);
			CHCopyC ("projectStatus", noteInfo.projectStatus);
			CHCopyC ("dateOfIssue", noteInfo.dateOfIssue);
			CHCopyC ("keywords", noteInfo.keywords);
			CHCopyC ("notes", noteInfo.notes);

			err = ACAPI_ProjectSetting_ChangeProjectNotes (&noteInfo);
			if (err != NoError)
				return err;

			API_DatabaseUnId	*dbases = nullptr;
			GSSize				nDbases = 0;

			err = ACAPI_Database_GetElevationDatabases (&dbases);
			if (err == NoError)
				nDbases = BMpGetSize (reinterpret_cast<GSPtr>(dbases)) / Sizeof32 (API_DatabaseUnId);

			if (err != NoError || nDbases == 0) {
				err = ACAPI_Database_GetLayoutDatabases (&dbases);
				nDbases = BMpGetSize (reinterpret_cast<GSPtr>(dbases)) / Sizeof32 (API_DatabaseUnId);
			}

			if (err == NoError && nDbases > 0) {
				API_DatabaseUnId floorPlan;
				BNZeroMemory (&floorPlan, sizeof (API_DatabaseUnId));

				{
					short userId = 0;
					UInt32 databaseId = 1;
					ACAPI_Database_GetGuidFromDBUnId (&userId, &databaseId, &floorPlan.elemSetId);
				}

				API_GhostRecord	apiGhostRecord;
				BNZeroMemory (&apiGhostRecord, sizeof (API_GhostRecord));
				apiGhostRecord.parentDatabase = floorPlan;
				apiGhostRecord.ghostDatabase = dbases[0];

				apiGhostRecord.scale = 1.0;
				apiGhostRecord.rotation = PI / 2;

				apiGhostRecord.floor = 0;
				apiGhostRecord.ghostType = APIGhost_NoFloor;
				apiGhostRecord.show = true;
				apiGhostRecord.activated = true;

				apiGhostRecord.ghostColor.f_red = 0.9;
				apiGhostRecord.ghostColor.f_green = 0.9;
				apiGhostRecord.ghostColor.f_blue = 0.7;
				apiGhostRecord.ghostColor.f_alpha = 1.0;
				apiGhostRecord.ghostOriginalColor = false;
				apiGhostRecord.ghostOriginalTone = false;
				apiGhostRecord.ghostBackgroundFill = true;

				apiGhostRecord.viewColor.f_red = 0.9;
				apiGhostRecord.viewColor.f_green = 0.9;
				apiGhostRecord.viewColor.f_blue = 0.7;
				apiGhostRecord.viewColor.f_alpha = 1.0;
				apiGhostRecord.viewOriginalColor = true;
				apiGhostRecord.viewOriginalTone = false;
				apiGhostRecord.viewBackgroundFill = true;

				apiGhostRecord.ghostOnTop = false;

				memset (&apiGhostRecord.filter, 0xFF, sizeof (API_ElemFilter));
				apiGhostRecord.filter[API_ObjectID] = false;	// don't show objects

				err = ACAPI_View_CreateGhostRecord (&apiGhostRecord);
			}

			if (dbases != nullptr)
				BMpFree (reinterpret_cast<GSPtr>(dbases));

			return err;
		});
		if (commandErrorCode != NoError)
			ErrorBeep ("ChangeProjectNotesAndCreateGhostRecordCommand failed", commandErrorCode);
	}
	{
		// TODO D-141 APIEnv_GetLibPartLockStatus
		// TODO D-141 APIEnv_ChangeStorySettings
	}

}

// -----------------------------------------------------------------------------
// Identify the server application and version
// -----------------------------------------------------------------------------

static void		Do_GetServerApplication (void)
{
	API_ServerApplicationInfo	serverApp;

	ACAPI_AddOnIdentification_Application (&serverApp);

	switch (serverApp.serverApplication) {
		case APIAppl_ArchiCADID:
					if (serverApp.runningInBackground)
						WriteReport ("Background Archicad (API %d R%d)", (short) serverApp.mainVersion, (short) serverApp.releaseVersion);
					else
						WriteReport_Alert ("Archicad (API %d R%d)", (short) serverApp.mainVersion, (short) serverApp.releaseVersion);
					break;

		default:
					WriteReport_Alert ("Unknown server application");
					break;
	}

	return;
}		// Do_GetServerApplication



// =============================================================================
//
// Make queries on the project installation
//
// =============================================================================


// -----------------------------------------------------------------------------
// Get information on the current project
//	 - project name, file location
//	 - Teamwork settings
// -----------------------------------------------------------------------------

static void		Do_DumpProjectInfo (void)
{
	API_ProjectInfo		projectInfo;
	API_SharingInfo		sharingInfo;
	char				buffer[256];
	Int32				i;
	GSErrCode			err;

	err = ACAPI_ProjectOperation_Project (&projectInfo);
	if (err != NoError) {
		ErrorBeep ("APIEnv_ProjectID", err);
		return;
	}

	if (projectInfo.untitled)
		WriteReport ("Project file has not been saved yet");
	else {
		if (!projectInfo.teamwork) {
			WriteReport ("Solo Project: %s", projectInfo.location->ToDisplayText ().ToCStr ().Get ());
		}
	}

	if (!projectInfo.teamwork)
		return;

	WriteReport ("Shared project: %s", projectInfo.location_team->ToLogText ().ToCStr ().Get ());

	WriteReport ("User ID: %d", projectInfo.userId);
	WriteReport ("--------------------");
	WriteReport ("Project Members:");

	err = ACAPI_Teamwork_ProjectSharing (&sharingInfo);
	if (err != NoError) {
		ErrorBeep ("APIEnv_ProjectSharingID", err);
		return;
	}

	for (i = 0; i < sharingInfo.nUsers; i++) {
		GS::UniString fullName ((*sharingInfo.users)[i].loginName);
		sprintf (buffer, "  \"%s\"   id:%d   %s",
				 fullName.ToCStr ().Get (),
				 (*sharingInfo.users)[i].userId,
				 (*sharingInfo.users)[i].connected ? "Connected" : "");
		if ((*sharingInfo.users)[i].userId == projectInfo.userId)
			buffer[0] = '*';
		WriteReport (buffer);
	}

	BMKillHandle ((GSHandle *) &sharingInfo.users);

	return;
}		// Do_DumpProjectInfo


// -----------------------------------------------------------------------------
// Dump preferences
// -----------------------------------------------------------------------------

static void	Do_DumpPreferences (void)
{
	API_WorkingUnitPrefs		unitPrefs;
	API_DimensionPrefs			dimPrefs;
	API_LegacyPrefs				legacyPrefs;
	API_ZonePrefs				zonePrefs;
	API_SafetyPrefs				safetyPrefs;
	API_CalcUnitPrefs			calcUnitPrefs;
	API_CalcRulesPrefs			calcRulesPrefs;
	API_RefLevelsPrefs			refLevelsPrefs;
	API_ImagingAndCalcPrefs		imagingAndCalcPrefs;
	API_FloorPlanCutDefinition	fpCutDefPrefs;
	API_LayoutsPrefs			layoutsPrefs;

	API_StoryInfo	storyInfo;

	BNZeroMemory (&dimPrefs, sizeof (API_DimensionPrefs));

	GS::UniString	path;
	char		colorStr[32] = { '\0' };
	short		i;
	GSErrCode	err;

	err = NoError;
	err |= ACAPI_ProjectSetting_GetPreferences (&unitPrefs, APIPrefs_WorkingUnitsID);
	err |= ACAPI_ProjectSetting_GetPreferences (&dimPrefs, APIPrefs_DimensionsID);
	err |= ACAPI_ProjectSetting_GetPreferences (&legacyPrefs, APIPrefs_LegacyID);
	err |= ACAPI_ProjectSetting_GetPreferences (&zonePrefs, APIPrefs_ZonesID);
	err |= ACAPI_ProjectSetting_GetPreferences (&safetyPrefs, APIPrefs_DataSafetyID);
	err |= ACAPI_ProjectSetting_GetPreferences (&calcUnitPrefs, APIPrefs_CalcUnitsID);
	err |= ACAPI_ProjectSetting_GetPreferences (&calcRulesPrefs, APIPrefs_CalcRulesID);
	err |= ACAPI_ProjectSetting_GetPreferences (&refLevelsPrefs, APIPrefs_ReferenceLevelsID);
	err |= ACAPI_ProjectSetting_GetPreferences (&imagingAndCalcPrefs, APIPrefs_ImagingAndCalcID);
	err |= ACAPI_ProjectSetting_GetPreferences (&fpCutDefPrefs, APIPrefs_FloorPlanCutPlaneDef);
	err |= ACAPI_ProjectSetting_GetPreferences (&layoutsPrefs, APIPrefs_LayoutsID);
	if (err != NoError) {
		ErrorBeep ("APIEnv_GetPreferencesID", err);
		return;
	}

	WriteReport (">>>>>> Dump Preferences <<<<<<");

	WriteReport ("Dimensions: ");
	WriteReport ("  Index: ");
	WriteReport ("%d\n", dimPrefs.index);

	path = safetyPrefs.tempFolder->ToDisplayText ();
	delete safetyPrefs.tempFolder;
	WriteReport ("Temporary folder: %s", path.ToCStr ().Get ());

	WriteReport ("LengthDec: %d,  AngleDec: %d,  RoundInch: %d,  AngleAccuracy: %d",
				unitPrefs.lenDecimals, unitPrefs.angleDecimals,
				unitPrefs.roundInch, unitPrefs.angleAccuracy);

	WriteReport ("All columns VS beams: %d", (legacyPrefs.coluConnPriority / 10 - 420 + 10) / 20 * 2);
	if (legacyPrefs.useLegacyIntersections)
		WriteReport("Uses legacy intersection and Surface methods for model elements");
	else
		WriteReport("Doesn't use legacy intersection and Surface methods for model elements");

	if (legacyPrefs.hideZonesOnSections)
		WriteReport("Hide zones and their associated labels on Section-type views");
	else
		WriteReport("Show zones and their associated labels on Section-type views");

	WriteReport ("Zones: ");
	WriteReport ("  Reduction: ");
	for (i = 0; i < zonePrefs.nLowAreas; i++)
		WriteReport ("  [%3d]:  %3d %%   %4.2lf", i, zonePrefs.roomRedPercents[i], zonePrefs.roomHeightLimits[i]);


	WriteReport ("Calculation units: ");
	WriteReport ("  Length unit: %d,  decimals: %d,  accuracy: %d",
				(int) calcUnitPrefs.length.unit, calcUnitPrefs.length.decimals, calcUnitPrefs.length.accuracy);
	WriteReport ("  Area unit:   %d,  decimals: %d,  accuracy: %d",
				(int) calcUnitPrefs.area.unit, calcUnitPrefs.area.decimals, calcUnitPrefs.area.accuracy);
	WriteReport ("  Volume unit: %d,  decimals: %d,  accuracy: %d",
				(int) calcUnitPrefs.volume.unit, calcUnitPrefs.volume.decimals, calcUnitPrefs.volume.accuracy);


	const auto elementPropertyTypeToString = [] (const API_ConditionalElementPropertyTypeID propertyType) -> GS::UniString {
		switch (propertyType) {
			case APIConditionalElementPropertyType_Volume:		return "volume";
			case APIConditionalElementPropertyType_Surface:		return "surface";
			case APIConditionalElementPropertyType_Length:		return "length";
			default:											DBBREAK (); return "";
		}
	};

	const auto elementTypeToString = [] (const API_ElemType& elemType) -> GS::UniString {
		switch (elemType.typeID) {
			case API_WallID:	return "wall";
			case API_SlabID:	return "slab";
			case API_RoofID:	return "roof";
			case API_ShellID:	return "shell";
			case API_BeamID:	return "beam";
			default:			DBBREAK (); return "";
		}
	};

	const auto arrayToString = [] (const auto& array, const auto& toString) -> GS::UniString {
		GS::UniString string;
		for (const auto& item : array) {
			if (!string.IsEmpty ()) {
				string.Append (", ");
			}
			string.Append (toString (item));
		}
		return string;
	};

	const auto attributeIndexArrayToString = [] (const auto& array) -> GS::UniString {
		GS::UniString string;
		for (const auto& item : array) {
			if (!string.IsEmpty ()) {
				string.Append (", ");
			}
			string.Append (item.ToUniString ());
		}
		return string;
	};

	WriteReport ("Calculation rules:");
	WriteReport ("  Element rules:");
	for (const API_ConditionalElementRule& rule : calcRulesPrefs.elementRules) {
		WriteReport ("    Rule for %s properties, hole limit: %lf, element types: %s",
					 elementPropertyTypeToString(rule.propertyType).ToCStr ().Get (),
					 rule.holeLimit,
					 arrayToString(rule.elementTypes, elementTypeToString).ToCStr ().Get ());
	}
	WriteReport ("  Special skins:");
	WriteReport ("    Wall insulation skins: %s", attributeIndexArrayToString (calcRulesPrefs.wallInsulationFills).ToCStr ().Get ());
	WriteReport ("    Wall air skins: %s", attributeIndexArrayToString (calcRulesPrefs.wallAirFills).ToCStr ().Get ());
	WriteReport ("    Shell insulation skins: %s", attributeIndexArrayToString (calcRulesPrefs.shellInsulationFills).ToCStr ().Get ());
	WriteReport ("    Roof insulation skins: %s", attributeIndexArrayToString (calcRulesPrefs.roofInsulationFills).ToCStr ().Get ());



	WriteReport ("Reference levels: ");
	WriteReport ("  Level 1: \"%s\", value: %4f", refLevelsPrefs.level1UStr->ToCStr ().Get (), refLevelsPrefs.level1val);
	WriteReport ("  Level 2: \"%s\", value: %4f", refLevelsPrefs.level2UStr->ToCStr ().Get (), refLevelsPrefs.level2val);


	WriteReport ("Imaging and Calculation preferences: ");
	WriteReport ("  Auto-rebuild: %s,  Bring 3D to front: %s,  Keep zoomed Section: %s",
					imagingAndCalcPrefs.autoRebuild3D ? "ON" : "OFF",
					imagingAndCalcPrefs.activate3DOnChange ? "ON" : "OFF",
					imagingAndCalcPrefs.keepZoomedSection ? "ON" : "OFF");
	WriteReport ("  Show Progress Window: %s,  Interrupt with error messages: %s,  3D progress sounds: %s,  Write Report: %s",
					imagingAndCalcPrefs.showProgressWindow ? "ON" : "OFF",
					imagingAndCalcPrefs.interruptOnError ? "ON" : "OFF",
					imagingAndCalcPrefs.progressSounds ? "ON" : "OFF",
					imagingAndCalcPrefs.generate3DReport ? "ON" : "OFF");

	switch (imagingAndCalcPrefs.reportLevel) {
		case APIReportLevel_Brief:			WriteReport ("  Report level: Brief");		break;
		case APIReportLevel_Short:			WriteReport ("  Report level: Short");		break;
		case APIReportLevel_Detailed:		WriteReport ("  Report level: Detailed");	break;
		case APIReportLevel_Full:			WriteReport ("  Report level: Full");		break;
	}
	switch (imagingAndCalcPrefs.selElemsListing) {
		case APICalcSelElems_ShowAlert:		WriteReport ("  Selected elements to be calculated: Show Alert");	break;
		case APICalcSelElems_ListAll:		WriteReport ("  Selected elements to be calculated: List All");		break;
		case APICalcSelElems_UseFilters:	WriteReport ("  Selected elements to be calculated: Use Filters");	break;
	}


	ACAPI_ProjectSetting_GetStorySettings (&storyInfo);
	BMKillHandle (reinterpret_cast<GSHandle *> (&storyInfo.data));

	WriteReport ("Floor plan cut preferences: ");
	WriteReport ("  Current cut level relative to the given story: %5.2f", fpCutDefPrefs.currCutLevel);
	WriteReport ("  Top level relative to the top story: %5.2f", fpCutDefPrefs.topCutLevel);
	WriteReport ("  Bottom level relative to the bottom story: %5.2f", fpCutDefPrefs.bottomCutLevel);
	WriteReport ("  Top story index relative (absolute) to the current story index: %d (%d)", fpCutDefPrefs.topCutBaseStoryRelInd,
						fpCutDefPrefs.topCutBaseStoryRelInd + storyInfo.actStory);
	WriteReport ("  Bottom story index relative (absolute) to the current story index: %d (%d)", fpCutDefPrefs.bottomCutBaseStoryRelInd,
						fpCutDefPrefs.bottomCutBaseStoryRelInd + storyInfo.actStory);
	WriteReport ("  Current cut level relative to project 0: %5.2f", fpCutDefPrefs.fixLevel2Absolute0);


	WriteReport ("Layouts preferences: ");
	if (!layoutsPrefs.useOwnMasterColor)
		sprintf (colorStr, "(%d, %d, %d)", (short) layoutsPrefs.masterColor.f_red * 255,
											(short) layoutsPrefs.masterColor.f_green * 255,
											(short) layoutsPrefs.masterColor.f_blue * 255);
	WriteReport ("  Use %s master color,%s adjust drawing frame to view zoom",
					layoutsPrefs.useOwnMasterColor ? "own" : colorStr,
					layoutsPrefs.adjustDrawingFrameToViewZoom ? "" : " don't");

	return;
}		// Do_DumpPreferences


// -----------------------------------------------------------------------------
// Get city/date/time/sun information
// -----------------------------------------------------------------------------

static void		Do_DumpLocation (void)

{	API_PlaceInfo	placeInfo;
	API_Vector3D	sunDir;
	GSErrCode		err;

	BNZeroMemory (&placeInfo, sizeof (API_PlaceInfo));
	err = ACAPI_GeoLocation_GetPlaceSets (&placeInfo);
	if (err) {
		ErrorBeep ("APIEnv_GetPlaceSetsID", err);
		return;
	}

	WriteReport ("actual year: %d", placeInfo.year);
	WriteReport ("actual month: %d", placeInfo.month);
	WriteReport ("actual day: %d", placeInfo.day);
	WriteReport ("actual hour: %d", placeInfo.hour);
	WriteReport ("actual minute: %d", placeInfo.minute);
	WriteReport ("actual second: %d", placeInfo.second);

	placeInfo.year = 2018;
	placeInfo.month = 2;
	placeInfo.day = 14;
	placeInfo.hour = 8;
	placeInfo.minute = 45;
	placeInfo.second = 0;
	err = ACAPI_GeoLocation_CalcSunOnPlace (&placeInfo);
	if (err) {
		ErrorBeep ("APIAny_CalcSunOnPlaceID", err);
		return;
	}

	sunDir.x = cos (placeInfo.sunAngXY) * cos (placeInfo.sunAngZ);
	sunDir.y = sin (placeInfo.sunAngXY) * cos (placeInfo.sunAngZ);
	sunDir.z = sin (placeInfo.sunAngZ);

	WriteReport ("Sun data at 8:45:00 on 14th February 2018");
	WriteReport ("  xy angle: %lf", placeInfo.sunAngXY);
	WriteReport ("  z angle:  %lf", placeInfo.sunAngZ);
	WriteReport ("  dirVect:  (%lf, %lf, %lf) ", sunDir.x, sunDir.y, sunDir.z);

	return;
}		// Do_DumpLocation


// -----------------------------------------------------------------------------
// Dump 3D projection settings
// -----------------------------------------------------------------------------

static void Do_Dump3DProjection (void)
{
	API_3DProjectionInfo	p;
	short					i;
	GSErrCode				err;

	err = ACAPI_View_Get3DProjectionSets (&p);
	if (err != NoError) {
		ErrorBeep ("APIEnv_Get3DProjectionSetsID", err);
		return;
	}

	WriteReport ("3D Projection Settings:");
	if (p.isPersp) {
		WriteReport ("  perspective");
	} else {
		WriteReport ("  axonometric");
		WriteReport ("  mode: %d", p.u.axono.projMod);
		WriteReport ("  transformat matrix:");
		for (i = 0; i < 12; i++)
			WriteReport ("    [%.2d] = %.4f", i+1, p.u.axono.tranmat.tmx[i]);
	}

	return;
}		// Do_Dump3DProjection


// -----------------------------------------------------------------------------
// Dump story settings
// -----------------------------------------------------------------------------

static void Do_DumpStorySettings (void)
{
	API_StoryInfo	storyInfo;
	Int32			i;
	GSErrCode		err;

	err = ACAPI_ProjectSetting_GetStorySettings (&storyInfo);
	if (err != NoError) {
		ErrorBeep ("APIEnv_GetStorySettingsID", err);
		return;
	}


	WriteReport ("actual story: %d", storyInfo.actStory);
	WriteReport ("first story: %d", storyInfo.firstStory);
	WriteReport ("last story: %d", storyInfo.lastStory);

	for (i = storyInfo.lastStory - storyInfo.firstStory; i >= 0; i--) {
		WriteReport ("   [%d]  \"%s\"  level: %f  height: %f",
					 (*storyInfo.data)[i].index,
					 GS::UniString ((*storyInfo.data)[i].uName).ToCStr ().Get (),
					 (*storyInfo.data)[i].level,
					 (*storyInfo.data)[i+1].level - (*storyInfo.data)[i].level);
	}

	BMKillHandle (reinterpret_cast<GSHandle *> (&storyInfo.data));

	return;
}		// Do_DumpStorySettings


// -----------------------------------------------------------------------------
// Dump 3D DocumentFrom3D Defaults
// -----------------------------------------------------------------------------

static void Do_DumpDocumentFrom3DDefaults (void)
{
	API_DocumentFrom3DDefaults	d3Default;

	GSErrCode err = ACAPI_View_GetDocumentFrom3DDefaults (&d3Default);
	if (err != NoError) {
		ErrorBeep ("APIEnv_GetDocumentFrom3DDefaultsID", err);
		return;
	}

	WriteReport ("3D Document Default Settings:");

	WriteReport ("shouldUseUniformCutAttributes: %s", d3Default.attributeOptions.shouldUseUniformCutAttributes ? "true" : "false");
	WriteReport ("uniformLineType: %d", d3Default.attributeOptions.uniformLineType);
	WriteReport ("uniformFillPen: %d", d3Default.attributeOptions.uniformFillPen);
	WriteReport ("uniformFillBkgPen: %d", d3Default.attributeOptions.uniformFillBkgPen);
	WriteReport ("shouldUseUncutSurfaceFill: %s", d3Default.attributeOptions.shouldUseUncutSurfaceFill ? "true" : "false");
	WriteReport ("uncutSurfaceFillType: %d", d3Default.attributeOptions.uncutSurfaceFillType);
	WriteReport ("uncutSurfaceBkgPen: %d", d3Default.attributeOptions.uncutSurfaceBkgPen);
	WriteReport ("shouldUseElemPen: %s", d3Default.attributeOptions.shouldUseElemPen ? "true" : "false");
	WriteReport ("elemPen: %d", d3Default.attributeOptions.elemPen);
	WriteReport ("shadFill: %d", d3Default.attributeOptions.shadFill);
	WriteReport ("shadFillPen: %d", d3Default.attributeOptions.shadFillPen);
	WriteReport ("shadFillBkgPen: %d", d3Default.attributeOptions.shadFillBkgPen);
	WriteReport ("name: %s", GS::UniString(d3Default.name).ToCStr ().Get ());
	WriteReport ("refId: %s", GS::UniString (d3Default.refId).ToCStr ().Get ());
	WriteReport ("vect3DHatching: %s", d3Default.vect3DHatching ? "true" : "false");
	WriteReport ("vectSunShadow: %s", d3Default.vectSunShadow ? "true" : "false");
	WriteReport ("mustBeTransparent: %s", d3Default.mustBeTransparent ? "true" : "false");
	WriteReport ("intPlanConn: %d", d3Default.intPlanConn);
	WriteReport ("materialFrom3D: %s", d3Default.materialFrom3D ? "true" : "false");
	WriteReport ("contureOff: %s", d3Default.contureOff ? "true" : "false");
	WriteReport ("cMater: %d", d3Default.cMater);
	WriteReport ("viewSrc: %d", d3Default.skeletonOptions.viewSrc);
	WriteReport ("floorIdx: %d", d3Default.skeletonOptions.floorIdx);
	WriteReport ("cutHeight: %d", d3Default.skeletonOptions.cutHeight);
	WriteReport ("floorOffset: %d", d3Default.skeletonOptions.floorOffset);
	WriteReport ("cutOffset: %d", d3Default.skeletonOptions.cutOffset);

	return;
}		// Do_Dump3DProjection

// =============================================================================
//
// Modify the project environment
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Change Project Notes
// -----------------------------------------------------------------------------

static void		Do_ChangeProjectNotes (void)
{
	API_ProjectNoteInfo	noteInfo;
	GSErrCode			err;

	CHCopyC ("client", noteInfo.client);
	CHCopyC ("projectName", noteInfo.projectName);
	CHCopyC ("company", noteInfo.company);
	CHCopyC ("street", noteInfo.street);
	CHCopyC ("city", noteInfo.city);
	CHCopyC ("country", noteInfo.country);
	CHCopyC ("code", noteInfo.code);
	CHCopyC ("architect", noteInfo.architect);
	CHCopyC ("draftsmen", noteInfo.draftsmen);
	CHCopyC ("projectStatus", noteInfo.projectStatus);
	CHCopyC ("dateOfIssue", noteInfo.dateOfIssue);
	CHCopyC ("keywords", noteInfo.keywords);
	CHCopyC ("notes", noteInfo.notes);

	err = ACAPI_ProjectSetting_ChangeProjectNotes (&noteInfo);
	if (err != NoError)
		ErrorBeep ("APIEnv_ChangeProjectNotesID", err);

	return;
}		// Do_ProjectNotes


// -----------------------------------------------------------------------------
// Create a new story above the topmost one
// 	- ARCHICAD menus must be updated
// 	- check the "Stories..." dialog after execution
// -----------------------------------------------------------------------------

static void Do_NewStory (void)
{
	API_StoryInfo		storyInfo;
	API_StoryCmdType	storyCmd;
	GSErrCode			err;

	err = ACAPI_ProjectSetting_GetStorySettings (&storyInfo);
	if (err != NoError) {
		ErrorBeep ("APIEnv_GetStorySettingsID", err);
		return;
	}

	BNZeroMemory (&storyCmd, sizeof (API_StoryCmdType));
	storyCmd.action = APIStory_InsAbove;
	storyCmd.index  = storyInfo.lastStory;
	storyCmd.height = 3.21;
	GS::snuprintf (storyCmd.uName, sizeof (storyCmd.uName), "Created story #%d",
	               storyInfo.skipNullFloor ? storyCmd.index + 1 : storyCmd.index);

	BMKillHandle (reinterpret_cast<GSHandle *> (&storyInfo.data));

	err = ACAPI_ProjectSetting_ChangeStorySettings (&storyCmd);
	if (err != NoError)
		ErrorBeep ("APIEnv_ChangeStorySettingsID (new story)", err);

	return;
}		// Do_NewStory


// -----------------------------------------------------------------------------
// Change the height of the active story
// 	- ARCHICAD menus must be updated
// 	- check the "Stories..." dialog after execution
// -----------------------------------------------------------------------------

static void		Do_ChangeStoryHeight (void)

{	API_StoryInfo		storyInfo;
	API_StoryCmdType	storyCmd;
	Int32				actFloor;
	double				actHeight;
	GSErrCode			err;

	BNZeroMemory (&storyInfo, sizeof (API_StoryInfo));
	err = ACAPI_ProjectSetting_GetStorySettings (&storyInfo);
	if (err != NoError) {
		ErrorBeep ("APIEnv_GetStorySettingsID", err);
		return;
	}

	actFloor = storyInfo.actStory - storyInfo.firstStory;
	actHeight = (*storyInfo.data)[actFloor + 1].level - (*storyInfo.data)[actFloor].level;

	BNZeroMemory (&storyCmd, sizeof (API_StoryCmdType));
	storyCmd.action = APIStory_SetHeight;
	storyCmd.index  = (*storyInfo.data)[actFloor].index;
	storyCmd.height = actHeight + 1.0;

	BMKillHandle (reinterpret_cast<GSHandle *> (&storyInfo.data));

	err = ACAPI_ProjectSetting_ChangeStorySettings (&storyCmd);
	if (err != NoError)
		ErrorBeep ("APIEnv_ChangeStorySettingsID (change height)", err);

	return;
}		// Do_ChangeStoryHeight


// -----------------------------------------------------------------------------
// Create a ghost record
// -----------------------------------------------------------------------------

static void		Do_CreateGhostRecord (void)
{
	GSErrCode			err = NoError;

	API_DatabaseUnId	*dbases = nullptr;
	GSSize				nDbases = 0;

	err = ACAPI_Database_GetElevationDatabases (&dbases);
	if (err == NoError)
		nDbases = BMpGetSize (reinterpret_cast<GSPtr>(dbases)) / Sizeof32 (API_DatabaseUnId);

	if (err != NoError || nDbases == 0) {
		err = ACAPI_Database_GetLayoutDatabases (&dbases);
		nDbases = BMpGetSize (reinterpret_cast<GSPtr>(dbases)) / Sizeof32 (API_DatabaseUnId);
	}

	if (err == NoError && nDbases > 0) {
		API_DatabaseUnId floorPlan;
		BNZeroMemory (&floorPlan, sizeof (API_DatabaseUnId));

		{
			short userId = 0;
			UInt32 databaseId = 1;
			ACAPI_Database_GetGuidFromDBUnId (&userId, &databaseId, &floorPlan.elemSetId);
		}

		API_GhostRecord	apiGhostRecord;
		BNZeroMemory (&apiGhostRecord, sizeof (API_GhostRecord));
		apiGhostRecord.parentDatabase = floorPlan;
		apiGhostRecord.ghostDatabase = dbases[0];

		apiGhostRecord.scale = 1.0;
		apiGhostRecord.rotation = PI / 2;

		apiGhostRecord.floor = 0;
		apiGhostRecord.ghostType = APIGhost_NoFloor;
		apiGhostRecord.show = true;
		apiGhostRecord.activated = true;

		apiGhostRecord.ghostColor.f_red = 0.9;
		apiGhostRecord.ghostColor.f_green = 0.9;
		apiGhostRecord.ghostColor.f_blue = 0.7;
		apiGhostRecord.ghostColor.f_alpha = 1.0;
		apiGhostRecord.ghostOriginalColor = false;
		apiGhostRecord.ghostOriginalTone = false;
		apiGhostRecord.ghostBackgroundFill = true;

		apiGhostRecord.viewColor.f_red = 0.9;
		apiGhostRecord.viewColor.f_green = 0.9;
		apiGhostRecord.viewColor.f_blue = 0.7;
		apiGhostRecord.viewColor.f_alpha = 1.0;
		apiGhostRecord.viewOriginalColor = true;
		apiGhostRecord.viewOriginalTone = false;
		apiGhostRecord.viewBackgroundFill = true;

		apiGhostRecord.ghostOnTop = false;

		memset (&apiGhostRecord.filter, 0xFF, sizeof (API_ElemFilter));
		apiGhostRecord.filter[API_ObjectID] = false;	// don't show objects

		err = ACAPI_View_CreateGhostRecord (&apiGhostRecord);
		if (err != NoError)
			ErrorBeep ("APIEnv_CreateGhostRecordID", err);
	}

	if (dbases != nullptr)
		BMpFree (reinterpret_cast<GSPtr>(dbases));

	return;
}		// Do_CreateGhostRecord

// -----------------------------------------------------------------------------
// Change a ghost record
// -----------------------------------------------------------------------------

static void		Do_ChangeGhostRecord (void)
{
	API_DatabaseUnId floorPlan;
	BNZeroMemory (&floorPlan, sizeof (API_DatabaseUnId));

	{
		short userId = 0;
		UInt32 databaseId = 1;
		ACAPI_Database_GetGuidFromDBUnId (&userId, &databaseId, &floorPlan.elemSetId);
	}

	API_GhostRecord	apiGhostRecord;
	BNZeroMemory (&apiGhostRecord, sizeof (API_GhostRecord));
	apiGhostRecord.parentDatabase = floorPlan;

	GSErrCode err = ACAPI_View_GetGhostRecord (&floorPlan, &apiGhostRecord);
	if (err == APIERR_WINDNOTEXIST) // parent database doesn't have any ghost database
		return;

	if (err != NoError) {
		ErrorBeep ("APIEnv_GetGhostRecordID", err);
		return;
	}

	if (apiGhostRecord.viewOriginalColor)
		apiGhostRecord.viewOriginalColor = false;
	else
		apiGhostRecord.viewOriginalColor = true;

	if (apiGhostRecord.ghostOriginalColor)
		apiGhostRecord.ghostOriginalColor = false;
	else
		apiGhostRecord.ghostOriginalColor = true;

	err = ACAPI_View_ChangeGhostRecord (&apiGhostRecord);
	if (err != NoError) {
		ErrorBeep ("APIEnv_ChangeGhostRecordID", err);
		return;
	}
}

// -----------------------------------------------------------------------------
// Delete a ghost record
// -----------------------------------------------------------------------------

static void		Do_DeleteGhostRecord (void)
{
	API_DatabaseUnId floorPlan;
	BNZeroMemory (&floorPlan, sizeof (API_DatabaseUnId));

	{
		short userId = 0;
		UInt32 databaseId = 1;
		ACAPI_Database_GetGuidFromDBUnId (&userId, &databaseId, &floorPlan.elemSetId);
	}

	API_GhostRecord	apiGhostRecord;
	BNZeroMemory (&apiGhostRecord, sizeof (API_GhostRecord));
	apiGhostRecord.parentDatabase = floorPlan;

	GSErrCode err = ACAPI_View_GetGhostRecord (&floorPlan, &apiGhostRecord);
	if (err == APIERR_WINDNOTEXIST) // parent database doesn't have any ghost database
		return;

	if (err != NoError) {
		ErrorBeep ("APIEnv_GetGhostRecordID", err);
		return;
	}

	err = ACAPI_View_DeleteGhostRecord (&apiGhostRecord);
	if (err != NoError) {
		ErrorBeep ("APIEnv_DeleteGhostRecordID", err);
		return;
	}
}

// -----------------------------------------------------------------------------
// Hide ARCHICAD (supported on Windows only, but not recommended)
// -----------------------------------------------------------------------------

static void		Do_Hide (void)
{
	bool	toShow = false;

	GSErrCode err = ACAPI_View_ChangeShowHideState (&toShow);
	if (err == APIERR_NOTSUPPORTED)
		ErrorBeep ("APIEnv_ChangeShowHideStateID not supported", err);

	return;
}		// Do_Hide


// -----------------------------------------------------------------------------
// Switch to Arrow mode
// -----------------------------------------------------------------------------

static void		Do_SwitchToArrow (void)
{
	API_ToolBoxItem		tbi;

	BNZeroMemory (&tbi, sizeof (tbi));
	tbi.type = API_ZombieElemID;
	if (ACAPI_Toolbox_SetToolBoxMode (&tbi) == NoError) {
		DBPrintf ("Environment Control -- switch to arrow mode was successful\n");
	}

	return;
}		// Do_SwitchToArrow


// -----------------------------------------------------------------------------
// Display the list of loaded libraries
// -----------------------------------------------------------------------------

static void		Do_ListLoadedLibraries (void)
{
	GS::Array<API_LibraryInfo>	libInfo;

	if (ACAPI_LibraryManagement_GetLibraries (&libInfo) == NoError) {
		GS::UniString infoString = GS::UniString::Printf ("Environment Control :: Number of loaded libraries is %lu", libInfo.GetSize ());
		ACAPI_WriteReport (infoString, false);

		for (UInt32 ii = 0; ii < libInfo.GetSize (); ++ii) {
			const API_LibraryInfo& libraryInfo = libInfo[ii];
			GS::UniString libraryTypeStr;
			switch (libraryInfo.libraryType) {
				case API_LocalLibrary:		libraryTypeStr = "Local Library";		break;
				case API_UrlLibrary:		libraryTypeStr = "Web Library";			break;
				case API_BuiltInLibrary:	libraryTypeStr = "Built-In Library";	break;
				case API_EmbeddedLibrary:	libraryTypeStr = "Embedded Library";	break;
				case API_OtherObject:		libraryTypeStr = "Other Object";		break;
				case API_UrlOtherObject:	libraryTypeStr = "Web Other Object";	break;
				case API_ServerLibrary:		libraryTypeStr = "Server Library";		break;
				case API_Undefined:
				default:					libraryTypeStr = "Unknown type";		break;
			}

			infoString = GS::UniString::Printf ("\t#%-2d: \"%T\"", ii+1, libraryInfo.name.ToPrintf ());
			if (!libraryInfo.available)
				infoString.Append (" (Not Available)");
			if (libraryInfo.readOnly)
				infoString.Append (" (Read-Only)");
			GS::UniString libPath = libraryInfo.location.ToDisplayText ();
			if (libraryInfo.libraryType == API_EmbeddedLibrary || libraryInfo.libraryType == API_ServerLibrary)
				infoString.Append ("\n\t\tCache location: ");
			else
				infoString.Append ("\n\t\tLocation: ");
			infoString.Append (libPath);
			infoString.Append ("\n\t\tType: ");
			infoString.Append (libraryTypeStr);
			if (libraryInfo.libraryType == API_ServerLibrary) {
				infoString.Append ("\n\t\tBIM Server Address: ");
				infoString.Append (libraryInfo.twServerUrl);
			} else if (libraryInfo.libraryType == API_UrlLibrary || libraryInfo.libraryType == API_UrlOtherObject) {
				infoString.Append ("\n\t\tWeb Library URL: ");
				infoString.Append (libraryInfo.urlWebLibrary);
			}

			ACAPI_WriteReport (infoString, false);
		}
	}

	return;
}		// Do_ListLoadedLibraries


// -----------------------------------------------------------------------------
// Load a new library into ARCHICAD
// -----------------------------------------------------------------------------

static void		Do_NewLibraryDialog (void)
{
	IO::Location	newLib;
	char			defName[] = "New Library";

	GSErrCode err = ACAPI_LibraryManagement_NewLibDial (defName, &newLib);
	if (err == NoError) {
		GS::UniString	infoString;
		GS::UniString	libPath = newLib.ToDisplayText ();

		err = ACAPI_LibraryManagement_ResetLibraries ();					// remove all local and server libraries
		if (err == NoError) {
			err = ACAPI_LibraryManagement_AddLibraries (&newLib);		// add the selected local library to the active libraries
			if (err == NoError)
				infoString = GS::UniString::Printf ("Environment Control -- New library folder has been successfully added to the active libraries:\n\n\t\"%T\"", libPath.ToPrintf ());
		}

		if (err == APIERR_NOTMINE) {
			infoString = GS::UniString::Printf ("Environment Control -- The selected location could not been added to the active libraries:\n\n\t\"%T\"\n\n"\
												"In Teamwork mode you need to reserve the list of project libraries first.", libPath.ToPrintf ());
		}

		ACAPI_WriteReport (infoString, true);
	} else {
		ACAPI_WriteReport ("Environment Control -- Select Library dialog has been canceled.", false);
	}

	return;
}		// Do_NewLibraryDialog


// -----------------------------------------------------------------------------
// Search for some image files on the disk, and copy them into a new folder in
// one of the active libraries
// -----------------------------------------------------------------------------

static bool CCALL	EnumCallBack (const IO::Folder& parent, const IO::Name& entryName, bool isFolder, void* userData)
{
	if (isFolder) {
		IO::Folder (parent, entryName).Enumerate (EnumCallBack, userData);	// search recursively in folder structure
		return true;
	}

	GS::UniString extension = entryName.GetExtension ();
	extension.SetToUpperCase ();
	if (extension == "JPG" || extension == "PNG" || extension == "TIFF" || extension == "BMP") {
		GS::Array<IO::Location>* filesToCopy = reinterpret_cast<GS::Array<IO::Location>*> (userData);
		if (DBVERIFY (filesToCopy != nullptr))
			filesToCopy->Push (IO::Location (parent.GetLocation (), entryName));
	}

	return true;
}

static void		Do_AddImageFilesToLibrary (void)
{
	GS::Array<IO::Location>	filesToCopy;
	IO::Location searchFolderLocation;
	API_SpecFolderID specID = API_UserDocumentsFolderID;
	if (DBVERIFY (ACAPI_ProjectSettings_GetSpecFolder (&specID, &searchFolderLocation) == NoError))
		IO::Folder (searchFolderLocation).Enumerate (EnumCallBack, &filesToCopy);

	if (filesToCopy.IsEmpty ()) {
		ACAPI_WriteReport ("Environment Control :: No image files found in the user folder", true);
		return;
	}

	IO::Location targetFolderLocation;
	GS::UniString libraryName;
	GS::Array<API_LibraryInfo> libInfo;
	if (ACAPI_LibraryManagement_GetLibraries (&libInfo) == NoError && libInfo.GetSize () > 0) {
		for (UInt32 ii = libInfo.GetSize (); ii > 0; --ii) {
			const API_LibraryInfo& libraryInfo = libInfo[ii - 1];
			if (libraryInfo.libraryType == API_EmbeddedLibrary) {
				targetFolderLocation = libraryInfo.location;	// select the last applicable library in the list
				libraryName = libraryInfo.name;
				break;
			}
		}
	}

	if (DBVERIFY (!targetFolderLocation.IsEmpty () && IO::fileSystem.IsLegal (targetFolderLocation))) {
		targetFolderLocation.AppendToLocal (IO::Name ("Environment Control TEST"));
		GSErrCode err = ACAPI_LibraryManagement_CreateFolderInLibrary (&targetFolderLocation);
		if (err == NoError) {
			bool overwriteIfExists = true;
			err = ACAPI_LibraryManagement_CopyFilesIntoLibrary (&targetFolderLocation, &filesToCopy, &overwriteIfExists);
		}

		switch (err) {
			case NoError: {
				GS::UniString infoString = GS::UniString::Printf ("Environment Control :: %lu file(s) copied into \"%T\"", filesToCopy.GetSize (), libraryName.ToPrintf ());
				ACAPI_WriteReport (infoString, true);
				break;
			}
			case APIERR_NOTMINE:
				ACAPI_WriteReport ("Environment Control :: Failed to add files to library, because the target folder is not reserved", true);
				break;
			case APIERR_READONLY:
				ACAPI_WriteReport ("Environment Control :: Failed to add files to library, because the target folder is not writable", true);
				break;
			case APIERR_NOACCESSRIGHT:
				ACAPI_WriteReport ("Environment Control :: Failed to add files to library, because you do not have access to the server library", true);
				break;
			default:
				ACAPI_WriteReport ("Environment Control :: Failed to add files to library (for unknown reason)", true);
				break;
		}
	}

	return;
}		// Do_AddImageFilesToLibrary


// -----------------------------------------------------------------------------
// Change a navigator item
// -----------------------------------------------------------------------------

static void		Do_ChangeNavigatorItem (void)
{
	API_NavigatorSet set;
	GSErrCode err = NoError;

	set.mapId = API_PublicViewMap;
	err = ACAPI_Navigator_GetNavigatorSet (&set);
	if (err != NoError)
		return;

	GS::Array<API_NavigatorItem> items;
	API_NavigatorItem item = {};

	item.guid = set.rootGuid;
	item.mapId = API_PublicViewMap;

	err = ACAPI_Navigator_GetNavigatorChildrenItems (&item, &items);
	if (err != NoError)
		return;

	DBASSERT (items.GetSize () == 1);
	item = items[0];

	err = ACAPI_Navigator_GetNavigatorChildrenItems (&item, &items);
	if (err != NoError)
		return;

	for (auto& childItem : items) {
		if (childItem.itemType == API_StoryNavItem) {
			//get settings
			API_NavigatorView	view;
			BNZeroMemory (&view, sizeof (view));

			// change settings
			// name
			if (childItem.customName) {
				childItem.customName = false;
			} else {
				childItem.customName = true;
				GS::UCopy (L("Name from API"), childItem.uName);
			}

			//uiId
			if (childItem.customUiId) {
				childItem.customUiId = false;
			} else {
				childItem.customUiId = true;
				CHCopyC ("ID from API", childItem.uiId);
			}

			// settings
			// layer combination
			GS::Array<API_Attribute> attributes;
			err = ACAPI_Attribute_GetAttributesByType (API_LayerCombID, attributes);
			if (!attributes.IsEmpty ())
				CHTruncate (attributes.GetLast ().header.name, view.layerCombination, sizeof (view.layerCombination));

			// model view options
			UInt32 mvoCount = 0;
			ACAPI_Navigator_ModelViewOptions_GetNum (mvoCount);

			API_ModelViewOptionsType modelViewOption = {};
			for (UInt32 i = mvoCount; i > 0; --i) {
				modelViewOption.head.index = i;
				err = ACAPI_Navigator_ModelViewOptions_Get (&modelViewOption);
				if (err == NoError) {
					CHTruncate (modelViewOption.head.name, view.modelViewOptName, sizeof (view.modelViewOptName));
					ACAPI_FreeGDLModelViewOptionsPtr (&modelViewOption.modelViewOpt.gdlOptions);
					break;
				}
			}

			// dimension styles
			UInt32 dimStandCount = 0;
			err = ACAPI_Navigator_DimStand_GetNum (dimStandCount);
			if (err != NoError)
				break;

			for (UInt32 i = dimStandCount; i > 0; --i) {
				API_DimensionStandardsType	dimStand = {};
				dimStand.head.index = i;
				err = ACAPI_Navigator_DimStand_Get (dimStand);
				if (err == NoError) {
					CHTruncate (dimStand.head.name, view.dimName, sizeof (view.dimName));
					break;
				}
			}

			// scale
			view.drawingScale = 123;

			// renovation filter
			API_NavigatorView	oldView;
			BNZeroMemory (&oldView, sizeof (oldView));
			err = ACAPI_Navigator_GetNavigatorView (&childItem, &oldView);
			if (err == NoError) {
				GS::Array<API_Guid> renFilters;
				err = ACAPI_Renovation_GetRenovationFilters (&renFilters);
				if (err == NoError) {
					GS::USize nRenFilters = renFilters.GetSize ();
					for (GS::UIndex i = 0; i < nRenFilters; ++i) {
						if (oldView.renovationFilterGuid == renFilters[i]) {
							view.renovationFilterGuid = renFilters[i + 1 >= nRenFilters ? 0 : i + 1];
							break;
						}
					}
				}
			}

			// transformation
			if (childItem.customUiId) {
				// rotate grid by 30 degrees
				view.tr.tmx[0] = view.tr.tmx[5] = sqrt(3.0) / 2.0;
				view.tr.tmx[1] = -0.5;
				view.tr.tmx[4] = 0.5;
				view.tr.tmx[10] = 1.0;
			} else {
				// identity
				view.tr.tmx[0] = view.tr.tmx[5] = view.tr.tmx[10] = 1.0;
			}

			err = ACAPI_Navigator_ChangeNavigatorView (&childItem, &view);
		} else if (childItem.itemType == API_FolderNavItem) {
			// change settings
			// name
			childItem.customName = true;
			GS::UCopy (L ("Name from API"), childItem.uName);
			err = ACAPI_Navigator_ChangeNavigatorView (&childItem, nullptr);
		}
	}
}


// -----------------------------------------------------------------------------
// Change 3D DocumentFrom3D Default Setting
// -----------------------------------------------------------------------------

static void Do_ChangeDocumentFrom3DDefaults (void)
{
	API_DocumentFrom3DDefaults	d3Default;

	GSErrCode err = ACAPI_View_GetDocumentFrom3DDefaults (&d3Default);
	if (err != NoError) {
		ErrorBeep ("APIEnv_GetDocumentFrom3DDefaultsID", err);
		return;
	}

	d3Default.vect3DHatching = !d3Default.vect3DHatching;
	d3Default.vectSunShadow = !d3Default.vectSunShadow;
	d3Default.mustBeTransparent = !d3Default.mustBeTransparent;

	err = ACAPI_View_ChangeDocumentFrom3DDefaults (&d3Default);
	if (err != NoError) {
		ErrorBeep ("APIEnv_ChangeDocumentFrom3DDefaultsID", err);
		return;
	}
}


// -----------------------------------------------------------------------------
// Dump Partial Structure Display state of the current database
// -----------------------------------------------------------------------------

static void Do_DumpPartialStructureDisplay (void)
{
	API_UIStructureDisplay structureDisplay;
	BNZeroMemory (&structureDisplay, sizeof (API_UIStructureDisplay));

	GSErrCode err = ACAPI_Navigator_GetStructureDisplay (&structureDisplay);
	if (err == NoError) {
		char state[256];
		switch (structureDisplay.structureDisplay) {
			case API_EntireStructure:
				CHCopyC ("Entire Structure", state);
				break;
			case API_CoreOnly:
				CHCopyC ("Core Only", state);
				break;
			case API_WithoutFinishes:
				CHCopyC ("Without Finishes", state);
				break;
			case API_StructureOnly:
				CHCopyC ("Structure Only", state);
				break;
			default:
				DBBREAK ();
				CHCopyC ("Unknown", state);
				break;
		}
		WriteReport ("Partial Structure Display of current database: %s", state);
	} else {
		WriteReport ("The current database doesn't contain Partial Structure Display functionality");
	}
}


// -----------------------------------------------------------------------------
// Change Partial Structure Display state of the current database
// -----------------------------------------------------------------------------

static void Do_ChangePartialStructureDisplay (void)
{
	API_UIStructureDisplay structureDisplay;
	BNZeroMemory (&structureDisplay, sizeof (API_UIStructureDisplay));

	GSErrCode err = ACAPI_Navigator_GetStructureDisplay (&structureDisplay);
	if (err != NoError) {
		ErrorBeep ("APIEnv_GetStructureDisplayID", err);
		return;
	}

	Int32 state = static_cast<Int32>(structureDisplay.structureDisplay);
	state = (state + 1) % 4;
	structureDisplay.structureDisplay = static_cast<API_StructureDisplay>(state);

	err = ACAPI_Navigator_ChangeStructureDisplay (&structureDisplay);
	if (err != NoError) {
		ErrorBeep ("APIEnv_ChangeStructureDisplayID", err);
	}
}


// -----------------------------------------------------------------------------
// Change Model View Options
// -----------------------------------------------------------------------------

static void Do_ChangeModelViewOptions (void)
{
	GSErrCode err = NoError;

	API_ViewOptions viewOptions = {};
	err = ACAPI_Navigator_GetViewOptions (&viewOptions);
	if (err == NoError) {
		Int32 nGDLOptions = BMGetPtrSize (reinterpret_cast<GSPtr>(viewOptions.modelViewOpt.gdlOptions)) / sizeof (API_GDLModelViewOptions);
		for (Int32 idx = 0; idx < nGDLOptions; ++idx) {
			GS::UniString name (viewOptions.modelViewOpt.gdlOptions[idx].name);
			if (name == "LibraryGlobals13") {
				if (viewOptions.modelViewOpt.gdlOptions[idx].params != nullptr) {
					Int32 paramsNumber =  BMGetHandleSize (reinterpret_cast<GSHandle>(viewOptions.modelViewOpt.gdlOptions[idx].params)) / sizeof (API_AddParType);
					for (Int32 i = 0; i < paramsNumber; ++i) {
						API_AddParType& param = (*(viewOptions.modelViewOpt.gdlOptions[idx].params))[i];
						if ((param.typeID == APIParT_Boolean) && CHEqualASCII (param.name, "showBOpLine3D", GS::CaseInsensitive)) { // Show opening lines in 3D for doors
							bool showBOpLine3D = (fabs (param.value.real) > EPS);
							param.value.real = showBOpLine3D ? 0.0 : 1.0;
							break;
						}
					}
				}
			}
		}
	} else {
		ErrorBeep ("APIEnv_GetViewOptionsID", err);
	}
	short ind = 3;
	err = ACAPI_Navigator_ChangeViewOptions (&viewOptions, &ind);
	if (err != NoError) {
		ErrorBeep ("APIEnv_ChangeViewOptionsID", err);
	}

	ACAPI_FreeGDLModelViewOptionsPtr (&viewOptions.modelViewOpt.gdlOptions);
}


// -----------------------------------------------------------------------------
// Change 3D Cutting Planes
// -----------------------------------------------------------------------------

static void Do_Change3DCuttingPlanes (void)
{
	API_3DCutPlanesInfo cutInfo;
	BNZeroMemory (&cutInfo, sizeof (API_3DCutPlanesInfo));

	GSErrCode err = ACAPI_View_Get3DCuttingPlanes (&cutInfo);
	if (err == NoError) {
		if (cutInfo.shapes != nullptr)
			BMKillHandle ((GSHandle *) &(cutInfo.shapes));

		cutInfo.isCutPlanes = true;
		cutInfo.nShapes = 2;
		cutInfo.shapes = reinterpret_cast<API_3DCutShapeType**> (BMAllocateHandle (cutInfo.nShapes * sizeof (API_3DCutShapeType), ALLOCATE_CLEAR, 0));
		if (cutInfo.shapes != nullptr) {
			(*cutInfo.shapes)[0].cutStatus = 2;
			(*cutInfo.shapes)[0].cutPen = 3;
			(*cutInfo.shapes)[0].cutMater = ACAPI_CreateAttributeIndex (11);
			(*cutInfo.shapes)[0].pa = -3.0499805934954503;
			(*cutInfo.shapes)[0].pb = 0.43107875694662845;
			(*cutInfo.shapes)[0].pc = 3.5670423669734248;
			(*cutInfo.shapes)[0].pd = 2.4161856450872907;
			(*cutInfo.shapes)[1].cutStatus = 3;
			(*cutInfo.shapes)[1].cutPen = 4;
			(*cutInfo.shapes)[1].cutMater = ACAPI_CreateAttributeIndex (12);
			(*cutInfo.shapes)[1].pa = -2.9081872443425456;
			(*cutInfo.shapes)[1].pb = 0.37912781320386035;
			(*cutInfo.shapes)[1].pc = 3.4016167929617027;
			(*cutInfo.shapes)[1].pd = -1.1569668026192714;
		}

		err = ACAPI_View_Change3DCuttingPlanes (&cutInfo);
		if (err == NoError) {
			API_WindowInfo windowInfo;
			BNZeroMemory (&windowInfo, sizeof (API_WindowInfo));
			windowInfo.typeID = APIWind_3DModelID;
			err = ACAPI_Window_ChangeWindow (&windowInfo);
		}

		BMKillHandle ((GSHandle *) &(cutInfo.shapes));
	}
}


// Dump the settings of the most recently used named Rendering Scene. If no named Rendering Scenes are saved yet, dump the current Scene (which can only be the Custom Scene).
static void	Do_DumpRenderingSettings (void)
{
	GS::Array<GS::UniString> recentSceneNames;
	ACAPI_Rendering_GetRenderingSceneNames (&recentSceneNames);

	GS::UniString* sceneNameToDump = recentSceneNames.IsEmpty () ? nullptr : &recentSceneNames[0];

	WriteReport ("Dumping some settings of Rendering Scene \"%s\":", sceneNameToDump == nullptr ? "the current Scene" : sceneNameToDump->ToCStr ().Get ());
	WriteReport ("-------------------------------------------------------------");

	API_RendEffects		sceneEffects;
	API_RendImage		sceneImage;
	BNZeroMemory (&sceneEffects,	sizeof (API_RendEffects));
	BNZeroMemory (&sceneImage,		sizeof (API_RendImage));

	GSErrCode err = NoError;
	if (err == NoError)
		err = ACAPI_Rendering_GetRenderingSets (&sceneEffects, APIRendSet_EffectsID,	   sceneNameToDump);
	if (err == NoError)
		err = ACAPI_Rendering_GetRenderingSets (&sceneImage, APIRendSet_ImageID,	   sceneNameToDump);

	switch (err) {
		case NoError:
			WriteReport ("- Image      / size       : %dpx x %dpx",	sceneImage.hSize, sceneImage.vSize);
			break;

		case APIERR_BADNAME:
			if (DBVERIFY (sceneNameToDump != nullptr))
				WriteReport ("- No such named Rendering Scene exists: \"%s\".", sceneNameToDump->ToCStr ().Get ());
			break;

		case APIERR_NOPLAN:
			WriteReport ("- No plan file is loaded.");
			break;
	}

	delete sceneImage.bkgPictFile;
}


// Dump the names of all named Rendering Scenes in the order of recent usage.
static void	Do_DumpAllRenderingSceneNames (void)
{
	GS::Array<GS::UniString> sceneNames;
	const GSErrCode err = ACAPI_Rendering_GetRenderingSceneNames (&sceneNames);

	WriteReport ("Rendering Scene Names, most recent first");
	WriteReport ("----------------------------------------");
	switch (err) {
		case NoError: {
			USize i = 0;
			for (GS::Array<GS::UniString>::ConstIterator it = sceneNames.Enumerate (); it != nullptr; ++it)
				WriteReport ("- Scene %2u: %s", GS::UIntForStdio (++i), it->ToCStr ().Get ());
			break;
		}

		case APIERR_NOPLAN: {
			WriteReport ("- No plan file is loaded.");
			break;
		}
	}
}


// Set the current Rendering Scene to the second most recently used named Rendering Scene.
static void	Do_SetCurrentRenderingScene (void)
{
	GS::Array<GS::UniString> recentSceneNames;
	ACAPI_Rendering_GetRenderingSceneNames (&recentSceneNames);

	if (recentSceneNames.GetSize () < 2) {
		WriteReport_Alert ("At least two named Rendering Scenes must exist.");
		return;
	}

	GS::UniString newCurrentSceneName = recentSceneNames[1];

	const GSErrCode err = ACAPI_Rendering_SetCurrentRenderingScene (&newCurrentSceneName);
	switch (err) {
		case NoError:		 /* successful */																								   break;
		case APIERR_NOPLAN:	 WriteReport_Alert ("No plan file is loaded.");																	   break;
		case APIERR_BADNAME: WriteReport_Alert ("No such named Rendering Scene exists: \"%s\".",		newCurrentSceneName.ToCStr ().Get ()); break;
		case APIERR_GENERAL: WriteReport_Alert ("Could not set the current Rendering Scene to \"%s\".",	newCurrentSceneName.ToCStr ().Get ()); break;
	}
}


static void		Do_GetConvertedUnitValue (void)
{
	API_UnitConversionData data;
	BNClear (data);

	data.value		  = 12.3456789001234;
	data.unitConvPref = APIUnitConv_DimLinear;

	DBASSERT (ACAPI_Conversion_GetConvertedUnitValue (&data) == NoError);

	WriteReport ("Value                 = %.20f\n"
				 "Conversion preference = APIUnitConv_DimLinear\n"
				 "-> Converted Value    = \"%ls\"\n"
				 "-> Extra Accuracy     = \"%ls\"\n"
				 "-> Unit               = \"%ls\"\n",
				 data.value, data.convertedValue, data.extraAccuracy, data.unit);
}

// Demonstrates the usage of API functions related to 3D Styles.
static void		Do_Edit3DStyles (void)
{
	// Get the name of all styles.
	GS::Array<GS::UniString> stylesNames;
	GS::UniString currentName;
	ACAPI_View_Get3DStyleList (&stylesNames, &currentName);

	API_3DStyle apiStyle;
	// Get the details of the currently active style. The name field is used for identification.
	GS::ucsncpy (apiStyle.name, currentName.ToUStr (), API_UniLongNameLen);
	ACAPI_View_Get3DStyle (&apiStyle);

	// Change ground color to green.
	apiStyle.backGroundRGB = { 0.0, 1.0, 0.0 };
	// Update the style.
	ACAPI_View_Change3DStyle (&apiStyle);

	// Create a new style based on the current one, with a different sky color. Finally set it as the current style.
	GS::UniString testStyleName ("testStyle");
	if (!stylesNames.Contains (testStyleName)) {
		apiStyle.bkgSkyColor = { 0.2, 0.2, 1.0 };
		GS::ucsncpy (apiStyle.name, testStyleName.ToUStr (), API_UniLongNameLen);
		ACAPI_View_Create3DStyle (&apiStyle);
		ACAPI_View_SetCurrent3DStyle (&testStyleName);
	}
}

// Changes settings of the Project Location and Survey Point
static API_Coord3D	TransformToNorth (const API_Coord3D& coord, double northDirection)
{
	const double xAxisAngle = northDirection - PI / 2.0;
	API_Coord3D rotatedCoord = {};
	rotatedCoord.x = cos (xAxisAngle) * coord.x + sin (xAxisAngle) * coord.y;
	rotatedCoord.y = -sin (xAxisAngle) * coord.x + cos (xAxisAngle) * coord.y;
	rotatedCoord.z = coord.z;
	return rotatedCoord;
}

static void		PrintGeoLocationInfo (const API_GeoLocation& geoLocation)
{
	const GS::UniString degreeSymb (L ("\x00B0"));

	ACAPI_WriteReport ("Project North angle: %lf%s", false, geoLocation.placeInfo.north * RADDEG, degreeSymb.ToCStr ().Get ());
	ACAPI_WriteReport ("Survey Point position on plan: x:%lf, y:%lf, z:%lf", false,
					   geoLocation.surveyPointPosition.x, geoLocation.surveyPointPosition.y, geoLocation.surveyPointPosition.z);

	API_Coord3D surveyPointEastingNorthing = TransformToNorth (geoLocation.surveyPointPosition, geoLocation.placeInfo.north);
	ACAPI_WriteReport ("Survey Point Easting/Northing: E %lf, N %lf", false, surveyPointEastingNorthing.x, surveyPointEastingNorthing.y);

	ACAPI_WriteReport ("Coordinate Reference System", false);
	ACAPI_WriteReport ("\tName:              \"%s\"", false, geoLocation.geoReferenceData.name.ToCStr ().Get ());
	ACAPI_WriteReport ("\tDescription:       \"%s\"", false, geoLocation.geoReferenceData.description.ToCStr ().Get ());
	ACAPI_WriteReport ("\tGeodetic Datum:    \"%s\"", false, geoLocation.geoReferenceData.geodeticDatum.ToCStr ().Get ());
	ACAPI_WriteReport ("\tVertical Datum:    \"%s\"", false, geoLocation.geoReferenceData.verticalDatum.ToCStr ().Get ());
	ACAPI_WriteReport ("\tMap Projection:    \"%s\"", false, geoLocation.geoReferenceData.mapProjection.ToCStr ().Get ());
	ACAPI_WriteReport ("\tMap Zone:          \"%s\"", false, geoLocation.geoReferenceData.mapZone.ToCStr ().Get ());
	ACAPI_WriteReport ("Map Conversion", false);
	ACAPI_WriteReport ("\tEastings:          %f m", false, geoLocation.geoReferenceData.eastings);
	ACAPI_WriteReport ("\tNorthings:         %f m", false, geoLocation.geoReferenceData.northings);
	ACAPI_WriteReport ("\tOrthogonal Height: %f m", false, geoLocation.geoReferenceData.orthogonalHeight);
	ACAPI_WriteReport ("\tX Axis Abscissa:   %f", false, geoLocation.geoReferenceData.xAxisAbscissa);
	ACAPI_WriteReport ("\tX Axis Ordinate:   %f", false, geoLocation.geoReferenceData.xAxisOrdinate);
	ACAPI_WriteReport ("\tScale:             %f", false, geoLocation.geoReferenceData.scale);
}

static bool		IsSurveyPointLocked ()
{
	bool isSurveyPointLocked = false;
	ACAPI_SurveyPoint_IsSurveyPointLocked (&isSurveyPointLocked);
	return isSurveyPointLocked;
}

static GSErrCode	UnlockSurveyPoint ()
{
	short response = DGAlert (DG_WARNING, GS::UniString (),
							  "Survey Point is locked.",
							  "Survey Point position and Project North cannot be modified.",
							  "Unlock and Try Again", "Cancel");
	if (response == DG_OK) {
		bool isSurveyPointLocked = false;
		return ACAPI_SurveyPoint_SetSurveyPointLocked (&isSurveyPointLocked);
	}

	return APIERR_CANCEL;
}

static void		Do_ChangeGeoLocation (void)
{
	API_GeoLocation geoLocation;
	GSErrCode err = ACAPI_GeoLocation_GetGeoLocation (&geoLocation);
	if (err != NoError) {
		WriteReport ("Do_ChangeGeoLocation: error occured in APIEnv_GetGeoLocation!");
		return;
	}

	ACAPI_WriteReport ("\n------------- Current GeoLocation Data -------------", false);
	PrintGeoLocationInfo (geoLocation);

	API_Tranmat tm;
	err = ACAPI_SurveyPoint_GetSurveyPointTransformation (&tm);
	if (err == NoError) {
		API_Coord3D	originCoord = { 0, 0, 0 };
		API_Coord3D	originFromSPCoord;
		originFromSPCoord.x = tm.tmx[0]*originCoord.x + tm.tmx[1]*originCoord.y + tm.tmx[2]*originCoord.z + tm.tmx[3];
		originFromSPCoord.y = tm.tmx[4]*originCoord.x + tm.tmx[5]*originCoord.y + tm.tmx[6]*originCoord.z + tm.tmx[7];
		originFromSPCoord.z = tm.tmx[8]*originCoord.x + tm.tmx[9]*originCoord.y + tm.tmx[10]*originCoord.z + tm.tmx[11];
		ACAPI_WriteReport ("Project Origin coordinates in the Survey Point's coordinate system: x:%lf, y:%lf, z:%lf",
						   false, originFromSPCoord.x, originFromSPCoord.y, originFromSPCoord.z);
	}

	geoLocation.surveyPointPosition.x -= 10;
	geoLocation.surveyPointPosition.y += 5;
	geoLocation.surveyPointPosition.z += 2.5;
	geoLocation.placeInfo.north += PI / 6.0;
	if (geoLocation.geoReferenceData.name.IsEmpty ()) {
		geoLocation.geoReferenceData.name = "EPSG:28356";
		geoLocation.geoReferenceData.description = "BILT Developments Setout";
		geoLocation.geoReferenceData.geodeticDatum = "GDA94";
		geoLocation.geoReferenceData.verticalDatum = "AHD";
		geoLocation.geoReferenceData.mapProjection = "EPSG:28356";
		geoLocation.geoReferenceData.mapZone = "56";
		geoLocation.geoReferenceData.eastings = 332462.692;
		geoLocation.geoReferenceData.northings = 6244759.420;
		geoLocation.geoReferenceData.orthogonalHeight = 2.310;
		geoLocation.geoReferenceData.xAxisAbscissa = 0.989130221837803;
		geoLocation.geoReferenceData.xAxisOrdinate = -0.147042185263616;
		geoLocation.geoReferenceData.scale = 0.999941534119322;
	}

	err = ACAPI_CallUndoableCommand ("Change GeoLocation from API", [&] () -> GSErrCode {
		GSErrCode errCode = ACAPI_GeoLocation_SetGeoLocation (&geoLocation);
		if (errCode == APIERR_NOTEDITABLE && IsSurveyPointLocked ()) {
			if (UnlockSurveyPoint () == NoError)
				errCode = ACAPI_GeoLocation_SetGeoLocation (&geoLocation);
		}
		return errCode;
	});

	if (err == NoError) {
		ACAPI_WriteReport ("------------- Changed GeoLocation Data -------------", false);
		PrintGeoLocationInfo (geoLocation);
	} else if (err == APIERR_NOTEDITABLE) {
		if (ACAPI_Teamwork_HasConnection ()) {
			API_Guid objectGuid = ACAPI_Teamwork_FindLockableObjectSet ("GeoLocation");
			if (!ACAPI_Teamwork_HasDeleteModifyRight (objectGuid)) {
				ACAPI_WriteReport ("You have insufficient privileges to modify the GeoLocation settings.", true);
			} else {
				API_LockableStatus lockableStatus = ACAPI_Teamwork_GetLockableStatus (objectGuid);
				if (lockableStatus == APILockableStatus_Free) {
					ACAPI_WriteReport ("You need to reserve the GeoLocation settings.", true);
				} else if (lockableStatus == APILockableStatus_Locked) {
					ACAPI_WriteReport ("GeoLocation settings are locked by other member.", true);
				} else {
					ACAPI_WriteReport ("GeoLocation settings are not editable.", true);
				}
			}
		} else {
			ACAPI_WriteReport ("GeoLocation settings are not editable.", true);
		}
	} else {
		WriteReport ("Do_ChangeGeoLocation: error occured in APIEnv_SetGeoLocation!");
	}

	ACAPI_WriteReport ("-----------------------------------------------------\n", false);
}


static void	Do_ChangeHideShowZonesLegacyPreference (void)
{
	API_LegacyPrefs				legacyPrefs;
	BNZeroMemory (&legacyPrefs, sizeof (API_LegacyPrefs));

	GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&legacyPrefs,APIPrefs_LegacyID);
	if (err != NoError)
		WriteReport_Alert ("Error");

	legacyPrefs.hideZonesOnSections = !legacyPrefs.hideZonesOnSections;

	err = ACAPI_ProjectSetting_SetPreferences (&legacyPrefs, APIPrefs_LegacyID);
	if (err != NoError)
		WriteReport_Alert ("Error");
}


static void Do_SetFilterAndCutSettings ()
{
	API_3DFilterAndCutSettings filterAndCutSettings = {};

	GSErrCode err = ACAPI_View_Get3DImageSets (&filterAndCutSettings);
	if (err != NoError)
		WriteReport_Alert ("Error");

	filterAndCutSettings.allStories = true;
	filterAndCutSettings.elemTypeFilter.insert ({API_ZombieElemID, false});

	err = ACAPI_View_Change3DImageSets (&filterAndCutSettings);
	if (err != NoError)
		WriteReport_Alert ("Error");

	filterAndCutSettings.elemTypeFilter.clear ();
	filterAndCutSettings.elemTypeFilter.insert ({API_WallID, true});
	filterAndCutSettings.elemTypeFilter.insert ({API_BeamID, true});


	err = ACAPI_View_Change3DImageSets (&filterAndCutSettings);
	if (err != NoError)
		WriteReport_Alert ("Error");
}

static void	Do_ChangeRefLevelPreferences (void)
{
	API_RefLevelsPrefs refLevelsPrefs = {};
	GSErrCode err = ACAPI_ProjectSetting_GetPreferences (&refLevelsPrefs, APIPrefs_ReferenceLevelsID);
	if (err != NoError)
		WriteReport_Alert ("Error Getting the reference level preferences!");

	ACAPI_WriteReport ("Values before ACAPI_ProjectSetting_SetPreferences\n", false);
	ACAPI_WriteReport ("Reference Level 1 name: %s", false, refLevelsPrefs.level1UStr);
	ACAPI_WriteReport ("Reference Level 1 value: %d", false, refLevelsPrefs.level1val);
	ACAPI_WriteReport ("Reference Level 2 name: %s", false, refLevelsPrefs.level2UStr);
	ACAPI_WriteReport ("Reference Level 2 value: %d", false, refLevelsPrefs.level2val);

	refLevelsPrefs.level1UStr = new GS::UniString ("Level 1");
	refLevelsPrefs.level2UStr = new GS::UniString ("Level 2");
	refLevelsPrefs.level1val = 10.0;
	refLevelsPrefs.level2val = 20.0;

	err = ACAPI_ProjectSetting_SetPreferences (&refLevelsPrefs, APIPrefs_ReferenceLevelsID);
	if (err != NoError)
		WriteReport_Alert ("Error Setting the reference level preferences!");

	delete refLevelsPrefs.level1UStr;
	delete refLevelsPrefs.level2UStr;

	err = ACAPI_ProjectSetting_GetPreferences (&refLevelsPrefs, APIPrefs_ReferenceLevelsID);
	if (err != NoError)
		WriteReport_Alert ("Error Getting the reference level preferences!");

	ACAPI_WriteReport ("Values after ACAPI_ProjectSetting_SetPreferences\n", false);
	ACAPI_WriteReport ("Reference Level 1 name: %s", false, refLevelsPrefs.level1UStr);
	ACAPI_WriteReport ("Reference Level 1 value: %d", false, refLevelsPrefs.level1val);
	ACAPI_WriteReport ("Reference Level 2 name: %s", false, refLevelsPrefs.level2UStr);
	ACAPI_WriteReport ("Reference Level 2 value: %d", false, refLevelsPrefs.level2val);

}

// -----------------------------------------------------------------------------
// Handles menu commands
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case 32500:
			switch (menuParams->menuItemRef.itemIndex) {
					case 1:		Do_GetServerApplication ();			break;
					/* ------ */
					case 3:		Do_DumpProjectInfo ();				break;
					case 4:		Do_DumpPreferences ();				break;
					case 5:		Do_DumpLocation ();					break;
					case 6:		Do_Dump3DProjection ();				break;
					case 7:		Do_DumpStorySettings ();			break;
					case 8:		Do_DumpDocumentFrom3DDefaults ();	break;
					/* ------ */
					case 10:	Do_ChangeProjectNotes ();			break;
					case 11:	Do_NewStory ();						break;
					case 12:	Do_ChangeStoryHeight ();			break;
					case 13:	Do_CreateGhostRecord ();			break;
					case 14:	Do_ChangeGhostRecord ();			break;
					case 15:	Do_DeleteGhostRecord ();			break;
					/* ------ */
					case 17:	Do_Hide ();							break;
					/* ------ */
					case 19:	Do_SwitchToArrow ();				break;
					/* ------ */
					case 21:	Do_ListLoadedLibraries ();			break;
					case 22:	Do_NewLibraryDialog ();				break;
					case 23:	Do_AddImageFilesToLibrary ();		break;
					/* ------ */
					case 25:	Do_ChangeNavigatorItem ();			break;
					case 26:	Do_ChangeDocumentFrom3DDefaults ();	break;
					/* ------ */
					case 28:	Do_DumpPartialStructureDisplay ();	break;
					case 29:	Do_ChangePartialStructureDisplay ();break;
					/* ------ */
					case 31:	Do_ChangeModelViewOptions ();		break;
					case 32:	Do_Change3DCuttingPlanes ();		break;
					/* ------ */
					case 34:	Do_DumpRenderingSettings ();		break;
					case 35:	Do_DumpAllRenderingSceneNames ();	break;
					case 36:	Do_SetCurrentRenderingScene ();		break;
					/* ------ */
					case 38:	Do_GetConvertedUnitValue ();		break;
					case 39:	Do_TestEnvironmentFunctions ();		break;
					case 40:	Do_Edit3DStyles ();					break;
					/* ------ */
					case 42:	Do_ChangeGeoLocation ();			break;
					case 43:	Do_ChangeHideShowZonesLegacyPreference ();	break;
					case 44:	Do_SetFilterAndCutSettings ();		break;
					case 45:	Do_ChangeRefLevelPreferences ();	break;
			}
			break;
	}

	return NoError;
}		// MenuCommandHandler


// =============================================================================
//
// Required functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err;

	err = ACAPI_MenuItem_RegisterMenu (32500, 32520, MenuCode_UserDef, MenuFlag_Default);
	if (err != NoError)
		DBPrintf ("Environment Control:: RegisterInterface() ACAPI_MenuItem_RegisterMenu failed\n");

	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
// -----------------------------------------------------------------------------

GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Environment Control:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	return err;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)

{
	return NoError;
}		// FreeData
