#include "Resources.hpp"

// from DG
#include "Tracker.hpp"
                       
// from API
#include "ACAPinc.h"
#include "APICommon.h"

// from ACAPI
#include "ACAPI/TrackerManager.hpp"


namespace {

void CheckVisibilityOfMainTracker ()
{
	// ! [Get-Main-Tracker-Example]

	ACAPI::Result<ACAPI::Tracking::TrackerManager> manager = ACAPI::Tracking::GetTrackerManager ();
	if (manager.IsErr ()) {
		WriteReport ("Could not instantiate the Tracker Manager.");
		return;
	}

	ACAPI::Result<ACAPI::Tracking::TrackerRef> trackerRef = manager->GetMainTracker ();
	if (trackerRef.IsErr ()) {
		const auto& err = trackerRef.UnwrapErr ();
		WriteReport ("%i: %s", err.kind, err.text.c_str ());
		return;
	}

	Tracking::Tracker& tracker = trackerRef.Unwrap ();
	if (!tracker.IsVisible ()) {
		WriteReport ("The main tracker is hidden.");
		return;
	}

	const bool isAnyTrackerItemVisible = tracker.EnumerateVisibleItems () != nullptr;
	if (isAnyTrackerItemVisible) {
		WriteReport ("The main tracker is visible.");
	} else {
		WriteReport ("The main tracker could be visible but has no item to be shown.");
	}

	// ! [Get-Main-Tracker-Example]
}

}


// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams* menuParams)
{
	if (menuParams->menuItemRef.menuResID != TRACKERMANAGER_TEST_MENU_STRINGS) {
		return NoError;
	}

	switch (menuParams->menuItemRef.itemIndex) {
		case CheckVisibilityOfMainTrackerID:
			CheckVisibilityOfMainTracker ();
			return NoError;
		default:
			return Error;
	}
}

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
	RSGetIndString (&envir->addOnInfo.name,        TRACKERMANAGER_TEST_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, TRACKERMANAGER_TEST_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface ()
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (TRACKERMANAGER_TEST_MENU_STRINGS, TRACKERMANAGER_TEST_MENU_PROMPT_STRINGS, MenuCode_UserDef, MenuFlag_Default);
	DBASSERT (err == NoError);
	return err;
}


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize ()
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (TRACKERMANAGER_TEST_MENU_STRINGS, MenuCommandHandler);
	DBASSERT (err == NoError);

	return err;
}


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData ()
{
	return NoError;
}