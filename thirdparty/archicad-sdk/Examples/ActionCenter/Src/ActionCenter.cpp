// *****************************************************************************
// Source code for the Action Center Example Add-On
// *****************************************************************************

// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"
#include	"ResourceIDs.hpp"


// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------



// =============================================================================
//
// Main functions
//
// =============================================================================

// Checks if hotlinks are up-to-date, and shows in Action Center if not;

static void CheckHotlinks ()
{
	ACAPI_Window_Check (APIActionCenterItem_Hotlinks);
}


// -----------------------------------------------------------------------------
// Entry points to handle ARCHICAD events
//
// -----------------------------------------------------------------------------

enum MenuItems {
	CheckHotlinksID = 1
};


GSErrCode MenuCommandHandler (const API_MenuParams *params)
{
	switch (params->menuItemRef.itemIndex) {
		case CheckHotlinksID:
			CheckHotlinks ();
			break;
	}

	return NoError;
}		// DoCommand


// =============================================================================
//
// Required functions
//
// =============================================================================


//------------------------------------------------------
// Dependency definitions
//------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	envir->addOnInfo.name			= RSGetIndString (ID_ADDON_INFO, 1, ACAPI_GetOwnResModule ());
	envir->addOnInfo.description	= RSGetIndString (ID_ADDON_INFO, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		/* CheckEnvironment */


//------------------------------------------------------
// Interface definitions
//------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	ACAPI_MenuItem_RegisterMenu (ID_MENU_STRINGS, 0, MenuCode_UserDef, MenuFlag_Default);

	return NoError;
}		/* RegisterInterface */


//------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
//------------------------------------------------------
GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (ID_MENU_STRINGS, MenuCommandHandler);
	if (err != NoError) {
		DBPrintf ("Action Center example add-on:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");
	}

	return err;
}		/* Initialize */


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}		// FreeData
