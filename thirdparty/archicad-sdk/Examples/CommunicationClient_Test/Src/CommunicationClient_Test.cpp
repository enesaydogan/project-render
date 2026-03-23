#include "Resources.h"
#include "HTTPClient.hpp"
#include "Dialog.hpp"

#include "DG.h"

#include "APIEnvir.h"
#include "ACAPinc.h"		// also includes APIdefs.h

void TestHTTPClientConnection ();


// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
	case COMMUNICATION_CLIENT_TEST_MENU_STRINGS:
		{
			switch (menuParams->menuItemRef.itemIndex) {
				case 1:
					TestHTTPClientConnection ();
					break;
			}
			break;
		}
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
	RSGetIndString (&envir->addOnInfo.name,        COMMUNICATION_CLIENT_TEST_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, COMMUNICATION_CLIENT_TEST_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (COMMUNICATION_CLIENT_TEST_MENU_STRINGS, 0, MenuCode_UserDef, MenuFlag_SeparatorBefore | MenuFlag_SeparatorAfter);

	DBASSERT (err == NoError);

	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (COMMUNICATION_CLIENT_TEST_MENU_STRINGS, MenuCommandHandler);

	DBASSERT (err == NoError);

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


void TestHTTPClientConnection ()
{
	Dialog httpClientConnectionDialog = Dialog ("HTTP Client Connection Test", "OK");

	GS::UniString serverBaseURL = "http://worldtimeapi.org";
	HTTPClient httpClient = HTTPClient (serverBaseURL);
	GSErrCode errorCode;

	errorCode = httpClient.Connect ();
	if (errorCode != NoError) {
		httpClientConnectionDialog.ShowDialog (DG_ERROR, "Failed to connect to the server!", GS::UniString ());
		return;
	}

	errorCode = httpClient.Execute ("/api/ip", "get-time-by-ip");
	if (errorCode != NoError) {

		GS::UniString responseMsg;
		switch (errorCode) {
			case ExceptionError:
				responseMsg = "Exception occurred! The server failed to fulfil the request!";
				break;
			case ErrParam:
				responseMsg = "Bad request method format!";
				break;
			default:
				responseMsg = "Something went wrong during the request!";
		}

		httpClientConnectionDialog.ShowDialog (DG_ERROR, responseMsg, GS::UniString ());
		return;
	}

	GS::ObjectState responseOs = httpClient.GetResponse ();
	if (!responseOs.IsEmpty ()) {
		GS::UniString datetimeStr;
		responseOs.Get ("datetime", datetimeStr);

		httpClientConnectionDialog.ShowDialog (DG_INFORMATION, "Your current date and time is:", datetimeStr.GetSubstring (0, 10) + " - " + datetimeStr.GetSubstring (11, 8));
	} else {
		httpClientConnectionDialog.ShowDialog (DG_ERROR, "Error! The response object is empty!", GS::UniString ());
	}

	errorCode = httpClient.CloseConnection ();
	if (errorCode != NoError) {
		httpClientConnectionDialog.ShowDialog (DG_ERROR, "Failed to close the connection!", GS::UniString ());
		return;
	}
}
