// *****************************************************************************
// Description:		Source code for the Notification Manager Add-On
// *****************************************************************************
#include "APIEnvir.h"
#define _WINDOW_OBSERVER_TRANSL_

// ------------------------------ Includes -------------------------------------

#include	<stdio.h>			/* sprintf */

#include	"ACAPinc.h"
#include	"Notification_Manager.h"


GS::Optional<API_Guid> windowChangedEventHandlerId;


static void RegisterEventHandler ()
{
	class WindowEventHandler : public API_IWindowEventHandler {
	public:
		virtual void OnWindowBroughtForward (const API_WindowInfo& window) const override
		{
			char msgStr[256];
			sprintf (msgStr, "OnWindowBroughtForward: window typeID %d", window.typeID);
			ACAPI_WriteReport (msgStr, false);
		}

		virtual void OnWindowSentBackward (const API_WindowInfo& window) const override
		{
			char msgStr[256];
			sprintf (msgStr, "OnWindowSentBackward: window typeID %d", window.typeID);
			ACAPI_WriteReport (msgStr, false);
		}
	};

	windowChangedEventHandlerId.New ();
	GSErrCode result = ACAPI_Notification_RegisterEventHandler (GS::NewOwned<WindowEventHandler> (), *windowChangedEventHandlerId);
	if (result != NoError) {
		windowChangedEventHandlerId.Clear ();
	}
}


static void UnRegisterEventHandler ()
{
	if (windowChangedEventHandlerId.HasValue ()) {
		ACAPI_Notification_UnregisterEventHandler (*windowChangedEventHandlerId);
		windowChangedEventHandlerId.Clear ();
	}
}


// ============================================================================
// Install Notification Handlers
//
//
// ============================================================================
void	Do_WindowMonitor (bool switchOn)
{
	if (switchOn)
		RegisterEventHandler ();
	else
		UnRegisterEventHandler ();	
}		/* Do_WindowMonitor */
