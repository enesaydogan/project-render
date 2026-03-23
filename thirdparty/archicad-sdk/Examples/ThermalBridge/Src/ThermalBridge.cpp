#include "ThermalBridge.hpp"
#include "DumpThermalBridges.hpp"
#include "Resources.hpp"

// API
#include "ACAPinc.h"

// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case THERMAL_BRIDGE_MENU_STRINGS:
		{
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:
					return ThermalBridge::DumpThermalBridges ();
			}
			break;
		}
	}

	return NoError;
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
	RSGetIndString (&envir->addOnInfo.name,        THERMAL_BRIDGE_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, THERMAL_BRIDGE_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	ERRCHK (ACAPI_MenuItem_RegisterMenu (THERMAL_BRIDGE_MENU_STRINGS, THERMAL_BRIDGE_MENU_PROMPT_STRINGS, MenuCode_UserDef, MenuFlag_Default));
	return NoError;
}


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	ERRCHK (ACAPI_MenuItem_InstallMenuHandler (THERMAL_BRIDGE_MENU_STRINGS, MenuCommandHandler));
	return NoError;
}


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}
