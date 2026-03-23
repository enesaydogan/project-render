// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************
#include "APIEnvir.h"
#define _PROJECT_OBSERVER_TRANSL_

// ------------------------------ Includes -------------------------------------

#include	<stdio.h>			/* sprintf */

#include	"Location.hpp"
#include	"ACAPinc.h"

#include	"Notification_Manager.h"


// ------------------------------ Constants ------------------------------------

// -------------------------------- Types --------------------------------------

// ------------------------------ Variables ------------------------------------

// ------------------------------ Prototypes -----------------------------------


// -----------------------------------------------------------------------------
// Project event handler function
// -----------------------------------------------------------------------------
static GSErrCode ProjectEventHandlerProc (API_NotifyEventID notifID, Int32 param)
{
	char	msgStr[256] = {};

	switch (notifID) {
		case APINotify_New:					CHCopyC ("APINotify_New", msgStr);				break;
		case APINotify_NewAndReset:			CHCopyC ("APINotify_NewAndReset", msgStr);		break;
		case APINotify_Open:
			{
				GS::Array<API_Guid> wallList;
				ACAPI_Element_GetElemList (API_WallID, &wallList);
				if (!wallList.IsEmpty ()) {
					ACAPI_Element_InstallElementObserver (ElementEventHandlerProc);
					for (const API_Guid& wallGuid : wallList) {
						ACAPI_Element_AttachObserver (wallGuid);
					}
				}
				CHCopyC ("APINotify_Open", msgStr);
				break;
			}
		case APINotify_PreSave:				CHCopyC ("APINotify_PreSave", msgStr);			break;
		case APINotify_TempSave:			CHCopyC ("APINotify_TempSave", msgStr);			break;
		case APINotify_Save:				CHCopyC ("APINotify_Save", msgStr);				break;
		case APINotify_Close:				CHCopyC ("APINotify_Close", msgStr);			break;
		case APINotify_Quit:				CHCopyC ("APINotify_Quit", msgStr);				break;

		case APINotify_SendChanges:			CHCopyC ("APINotify_SendChanges", msgStr);		break;
		case APINotify_ReceiveChanges:		CHCopyC ("APINotify_ReceiveChanges", msgStr);	break;

		case APINotify_ChangeProjectDB:		CHCopyC ("APINotify_ChangeProjectDB", msgStr);	break;
		case APINotify_ChangeWindow:		CHCopyC ("APINotify_ChangeWindow", msgStr);		break;
		case APINotify_ChangeFloor:			CHCopyC ("APINotify_ChangeFloor", msgStr);		break;
		case APINotify_ChangeLibrary:
			if (param == 0) {
				CHCopyC ("APINotify_ChangeLibrary", msgStr);
			} else {
				API_LibPart	lp = {};
				lp.index = param;
				if (ACAPI_LibraryPart_Get (&lp) == NoError) {
					sprintf (msgStr, "APINotify_ChangeLibrary for library item \"%s\"", (const char *)GS::UniString (lp.docu_UName).ToCStr ());
					delete lp.location;
				} else {
					sprintf (msgStr, "APINotify_ChangeLibrary for library item %ld", (GS::LongForStdio) param);
				}
			}
			break;

		case APINotify_PropertyVisibilityChanged:	CHCopyC ("APINotify_PropertyVisibilityChanged", msgStr);		break;

		case APINotify_ShowIn3DChanged:
			{
				Int32		nbody;
				void		*origSight;
				GSErrCode	err;
				err = ACAPI_Sight_SelectSight (nullptr, &origSight);
				if (err == NoError) {
					ACAPI_ModelAccess_GetNum (API_BodyID, &nbody);
					sprintf (msgStr, "APINotify_ShowIn3DChanged, number of 3D elements: %d", nbody);
					ACAPI_Sight_SelectSight (origSight, &origSight);
				}
			}
			break;

		default:
			break;
	}

	ACAPI_WriteReport (msgStr, false);

	return NoError;
}	// ProjectEventHandlerProc


// ============================================================================
// Install Notification Handlers
//
//
// ============================================================================

void	Do_CatchProjectEvent (bool switchOn)
{
	if (switchOn)
		ACAPI_ProjectOperation_CatchProjectEvent (API_AllNotificationMask, ProjectEventHandlerProc);  // catch all project events
	else
		ACAPI_ProjectOperation_CatchProjectEvent (API_AllNotificationMask, nullptr);

	return;
}		/* SetNewElementHandler */
