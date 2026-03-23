// *****************************************************************************
// Source code for the SkeletonDrawing_Test Add-On
// *****************************************************************************

#include "APIEnvir.h"


// ---------------------------------- Includes ---------------------------------

#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"IV.hpp"


// =============================================================================
//
// Main functions
//
// =============================================================================


typedef struct {
	unsigned short	platform;
	unsigned short	filler;
	UInt32	last3DDocument;
} PrefsData;

static PrefsData	*prefs = nullptr;

// -----------------------------------------------------------------------------
// Add a 3DD
// -----------------------------------------------------------------------------

static void		Create3DD (bool skeleton)
{
	API_DatabaseInfo dbInfo;
	BNZeroMemory (&dbInfo, sizeof (API_DatabaseInfo));
	dbInfo.typeID = APIWind_DocumentFrom3DID;

	GS::snuprintf (dbInfo.name, sizeof (dbInfo.name), "Skeleton Drawing Test #%d (%s)", prefs->last3DDocument, skeleton ? "Skeleton" : "3DD");
	GS::snuprintf (dbInfo.ref, sizeof (dbInfo.ref), "3DD #%d", prefs->last3DDocument++);

	if (ACAPI_Database_NewDatabase (&dbInfo) == NoError) {
		DBPrintf ("DatabaseControl :: The ID of the new Document 3D ElemSet is \"%s\"\n",
			APIGuidToString (dbInfo.databaseUnId.elemSetId).ToCStr ().Get ());
	}

	API_DocumentFrom3DType documentFrom3DType;

	GSErrCode err = ACAPI_View_GetDocumentFrom3DSettings (&dbInfo.databaseUnId, &documentFrom3DType);
	if (err == NoError) {
		documentFrom3DType.docSrc						= skeleton ? API_FloorPlan : API_DDD;
		documentFrom3DType.skeletonOptions.viewSrc		= API_Top;
		documentFrom3DType.skeletonOptions.floorIdx		= 0;
		documentFrom3DType.skeletonOptions.cutHeight	= 0.5;
		documentFrom3DType.skeletonOptions.floorOffset	= 1;
		documentFrom3DType.skeletonOptions.cutOffset	= 1.5;

		err = ACAPI_View_ChangeDocumentFrom3DSettings (&dbInfo.databaseUnId, &documentFrom3DType);
		if (err != NoError)
			DBPrintf ("APIEnv_ChangeDocumentFrom3DSettingsID returns with error code");
	} else {
		DBPrintf ("APIEnv_GetDocumentFrom3DSettingsID returns with error code");
	}

	BMhFree ((GSHandle) documentFrom3DType.cutSetting.shapes);

 	return;
}		// AddSkeleton


// -----------------------------------------------------------------------------
// Handles menu commands
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case 32500:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:		Create3DD (true);	break;
				case 2:		Create3DD (false);	break;
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
// InitPreferences
//		initialize preference data
// -----------------------------------------------------------------------------
static void	InitPreferences (void)
{
	prefs = new PrefsData;
	if (prefs != nullptr) {
		Int32	version;
		GSSize	nBytes;

		ACAPI_GetPreferences (&version, &nBytes, nullptr);
		if (version == 1 && nBytes == sizeof (PrefsData)) {
			ACAPI_GetPreferences (&version, &nBytes, prefs);
			if (prefs->platform != GS::Act_Platform_Sign) {
				IVULong ((GS::PlatformSign) (prefs->platform), &prefs->last3DDocument);
				prefs->platform = GS::Act_Platform_Sign;
			}
		} else {
			prefs->platform = GS::Act_Platform_Sign;
			prefs->last3DDocument = 1;
		}
	}
}		// InitPreferences


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
	GSErrCode err = NoError;
	err = ACAPI_MenuItem_RegisterMenu (32500, 0, MenuCode_UserDef, MenuFlag_SeparatorBefore);

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
		DBPrintf ("SkeletonDrawing_Test:: Initialize () ACAPI_MenuItem_InstallMenuHandler failed\n");

	InitPreferences ();

	return err;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)

{
	if (prefs != nullptr) {
		ACAPI_SetPreferences (1L, sizeof (PrefsData), (const void *) prefs);
		delete prefs;
	}

	return NoError;
}		// FreeData
