// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************
#include "APIEnvir.h"
#define _SELECTION_OBSERVER_TRANSL_

// ------------------------------ Includes -------------------------------------

#include	<stdio.h>			/* sprintf */

#include	"ACAPinc.h"
#include	"UniString.hpp"

#include	"Notification_Manager.h"


// ------------------------------ Constants ------------------------------------

// -------------------------------- Types --------------------------------------

// ------------------------------ Variables ------------------------------------

// ------------------------------ Prototypes -----------------------------------

// -----------------------------------------------------------------------------
// SelectionChangeHandlerProc
//
//  prints the recently selected element
// -----------------------------------------------------------------------------
static GSErrCode SelectionChangeHandlerProc (const API_Neig* selElemNeig)
{
	if (selElemNeig->neigID != APINeig_None) {
		char msgStr[256];
		sprintf (msgStr, "Last selected element: NeigID %d; guid: %s, inIndex: %d",
				 selElemNeig->neigID, (const char *) APIGuid2GSGuid (selElemNeig->guid).ToUniString ().ToCStr (), selElemNeig->inIndex);
		ACAPI_WriteReport (msgStr, false);
	} else {
		ACAPI_WriteReport ("All elements deselected", false);
	}

	return NoError;
}


// ============================================================================
// Install Notification Handlers
//
//
// ============================================================================
void	Do_SelectionMonitor (bool switchOn)
{
	if (switchOn)
		ACAPI_Notification_CatchSelectionChange (SelectionChangeHandlerProc);
	else
		ACAPI_Notification_CatchSelectionChange (nullptr);

	return;
}		/* Do_SelectionMonitor */
