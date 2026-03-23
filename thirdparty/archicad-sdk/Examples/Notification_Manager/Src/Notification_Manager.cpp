// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************

#include "APIEnvir.h"
#define	_NOTIFICATION_MANAGER_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include	"ACAPinc.h"		// also includes APIdefs.h

#include	"APICommon.h"
#include	"Notification_Manager.h"

#include	"UniString.hpp"

// ---------------------------------- Types ------------------------------------

#define	 Menu_StringId			32500
#define	 Menu_ProjectChanges		1
#define	 Menu_DefaultMonitor		3
#define	 Menu_ElementMonitor		4
#define	 Menu_ToolMonitor			5
#define	 Menu_SelectionMonitor		6
#define	 Menu_ReservationMonitor	7
#define	 Menu_ClassificationMonitor	21
#define	 Menu_PropertyMonitor		22
#define  Menu_GraphisoftIDMonitor	23
#define  Menu_AttributeMonitor		24
#define  Menu_MarqueeMonitor		25
#define  Menu_WindowMonitor			26
#define  Menu_KeynoteMonitor		27

// ---------------------------------- Variables --------------------------------

static	bool	attributeEventsEnabled = false;
static	bool	projectEventsEnabled = false;
static	bool	defaultMonitorEnabled = false;
static	bool	elementMonitorEnabled = false;
static	bool	toolMonitorEnabled = false;
static	bool	selectionMonitorEnabled = false;
static	bool	reservationMonitorEnabled = false;
static	bool	classificationEventsEnabled = false;
static	bool	propertyEventsEnabled = false;
static  bool	graphisoftIDEventEnabled = false;
static  bool	marqueeIDEventEnabled = false;
static  bool	windowMonitorEnabled = false;
static  bool	keynoteMonitorEnabled = false;

// ---------------------------------- Prototypes -------------------------------

// =============================================================================
//
// Utility functions
//
// =============================================================================


// -----------------------------------------------------------------------------
// GetElementTypeString
//	helper function
// -----------------------------------------------------------------------------
bool	GetElementTypeString (const API_ElemType& type, char *elemStr)
{
	GS::UniString	ustr;
	GSErrCode	err = ACAPI_Element_GetElemTypeName (type, ustr);
	if (err == NoError) {
		CHTruncate (ustr.ToCStr (), elemStr, ELEMSTR_LEN - 1);
		return true;
	}
	return false;
}


// -----------------------------------------------------------------------------
// DoCommand
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	GSErrCode err = ACAPI_CallUndoableCommand ("API Test",
		[&] () -> GSErrCode {

			switch (menuParams->menuItemRef.itemIndex) {
				case 1:
					Do_CatchProjectEvent (!projectEventsEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_ProjectChanges);
					projectEventsEnabled = !projectEventsEnabled;
					break;
				/* ----- */
				case 3:
					Do_DefaultMonitor (!defaultMonitorEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_DefaultMonitor);
					defaultMonitorEnabled = !defaultMonitorEnabled;
					break;
				case 4:
					Do_ElementMonitor (!elementMonitorEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_ElementMonitor);
					elementMonitorEnabled = !elementMonitorEnabled;
					break;
				case 5:
					Do_ToolMonitor (!toolMonitorEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_ToolMonitor);
					toolMonitorEnabled = !toolMonitorEnabled;
					break;
				case 6:
					Do_SelectionMonitor (!selectionMonitorEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_SelectionMonitor);
					selectionMonitorEnabled = !selectionMonitorEnabled;
					break;
				case 7:
					Do_ReservationMonitor (!reservationMonitorEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_ReservationMonitor);
					reservationMonitorEnabled = !reservationMonitorEnabled;
					break;
				/* ----- */
				case 9:
					Do_ClickedElementMonitor (true);
					break;
				case 10:
					Do_ClickedElementMonitor (false);
					break;
				case 11:
					Do_ListMonitoredElements ();
					break;
				/* ----- */
				case 13:
					Do_MarkSelElems ();
					break;
				case 14:
					Do_ListOwnedElements ();
					break;
				/* ----- */
				case 16:
					Do_AttachElementURLRef ();
					break;
				case 17:
					Do_ListElementURLRef ();
					break;
				/* ----- */
				case 19:
					Do_LinkElements ();
					break;
				case 21:
					Do_ClassificationMonitor (!classificationEventsEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_ClassificationMonitor);
					classificationEventsEnabled = !classificationEventsEnabled;
					break;
				case 22:
					Do_PropertyMonitor (!propertyEventsEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_PropertyMonitor);
					propertyEventsEnabled = !propertyEventsEnabled;
					break;
				case 23:
					Do_GraphisoftIDMonitor (!graphisoftIDEventEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_GraphisoftIDMonitor);
					graphisoftIDEventEnabled = !graphisoftIDEventEnabled;
					break;
				case 24:
					Do_AttributeMonitor (!attributeEventsEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_AttributeMonitor);
					attributeEventsEnabled = !attributeEventsEnabled;
					break;
				case 25:
					Do_MarqueeIDMonitor (!marqueeIDEventEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_MarqueeMonitor);
					marqueeIDEventEnabled = !marqueeIDEventEnabled;
					break;
				case 26:
					Do_WindowMonitor (!windowMonitorEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_WindowMonitor);
					windowMonitorEnabled = !windowMonitorEnabled;
					break;
				case 27:
					Do_KeynoteMonitor (!keynoteMonitorEnabled);
					InvertMenuItemMark (Menu_StringId, Menu_KeynoteMonitor);
					keynoteMonitorEnabled = !keynoteMonitorEnabled;
					break;
			}

			return NoError;
		});

	return err;
}		// MenuCommandHandler


// =============================================================================
//
// Required functions
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif

//------------------------------------------------------
// Dependency definitions
//------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Preload;
}		// CheckEnvironment


//------------------------------------------------------
// Interface definitions
//------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	ACAPI_MenuItem_RegisterMenu (Menu_StringId, 0, MenuCode_UserDef, MenuFlag_SeparatorBefore);

	return NoError;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	return ACAPI_MenuItem_InstallMenuHandler (Menu_StringId, MenuCommandHandler);
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}		// FreeData
