// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Main Class with required functions
// *****************************************************************************



// ---------------------------------- Includes ---------------------------------

#include "ModifyHatchAndLines.h"
#include "ModifyCurtainWall.h"
#include "CurtainWallFromSelection.h"
#include "SelectionFromCurtainWall.h"
#include "SelectLTypeIndDialog.h"

// =============================================================================
//
// Main functions
//
// =============================================================================

static void CreateCurtainWallFromSelection ()
{
	CurtainWallFromSelection cwfromsel;
	if (cwfromsel.GetSelectedElements () == NoError) {
		cwfromsel.InitializeCurtainWall ();
		cwfromsel.CreateCurtainWall ();
	}
}

static void ModifyCurtainWallFromSelection ()
{
	ModifyCurtainWall modifcw;
	if (modifcw.GetSelectedElements () == NoError)
		modifcw.ModifyIfChanged ();
}

static void CreateHatchFromCurtainWall ()
{
	SelectionFromCurtainWall selfromcw;
	if (selfromcw.GetSelectedElements () == NoError) {
		selfromcw.MakeHatchandLines ();
		selfromcw.CreateHatchAndLines ();
	}
}

static void ModifySelectionFromCW ()
{
	ModifyHatchAndLines modihl;
	if (modihl.GetSelectedElements () == NoError)
		modihl.ModifyIfChanged ();
}

static void ShowDialog ()
{
	SelectLTypeIndDialog ().Invoke ();
}

// -----------------------------------------------------------------------------
// Handles menu commands
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case 32500:
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:		CreateCurtainWallFromSelection ();	break;
				case 2:		ModifyCurtainWallFromSelection ();	break;
				case 4:		CreateHatchFromCurtainWall ();		break;
				case 5:		ModifySelectionFromCW ();			break;
				case 7:		ShowDialog ();						break;
				default:										break;
			}
			break;
		default:
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
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (32500, 0, MenuCode_Extras, MenuFlag_SeparatorBefore);
	if (err != NoError)
		DBPrintf ("CurtainWall_Drawer:: RegisterInterface () ACAPI_MenuItem_RegisterMenu failed\n");
	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("CurtainWall_Drawer:: Initialize () ACAPI_MenuItem_InstallMenuHandler failed\n");
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
