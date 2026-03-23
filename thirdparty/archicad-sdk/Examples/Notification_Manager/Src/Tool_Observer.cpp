// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************
#include "APIEnvir.h"
#define _TOOL_OBSERVER_TRANSL_

// ------------------------------ Includes -------------------------------------

#include	<stdio.h>			/* sprintf */

#include	"ACAPinc.h"

#include	"Notification_Manager.h"


// ------------------------------ Constants ------------------------------------

// -------------------------------- Types --------------------------------------

// ------------------------------ Variables ------------------------------------

// ------------------------------ Prototypes -----------------------------------

// -----------------------------------------------------------------------------
// Toolbox item change handler function
// -----------------------------------------------------------------------------
static GSErrCode ToolChangeHandlerProc (const API_ToolBoxItem *newToolMode)
{
    char    msgStr[2 * ELEMSTR_LEN];
    char    elemStr[ELEMSTR_LEN];

    if (newToolMode->type == API_ZombieElemID)
        CHCopyC ("Arrow or Marquee", elemStr);
    else if (!GetElementTypeString (newToolMode->type, elemStr))
        CHCopyC ("Unknown", elemStr);

    sprintf (msgStr, "%s toolbox mode selected", elemStr);
    ACAPI_WriteReport (msgStr, false);

    return NoError;
}   // ToolChangeHandlerProc




// ============================================================================
// Install Notification Handlers
//
//
// ============================================================================
void	Do_ToolMonitor (bool switchOn)
{
	if (switchOn)
		ACAPI_Notification_CatchToolChange (ToolChangeHandlerProc);
	else
		ACAPI_Notification_CatchToolChange (nullptr);

	return;
}	// Do_ToolMonitor
