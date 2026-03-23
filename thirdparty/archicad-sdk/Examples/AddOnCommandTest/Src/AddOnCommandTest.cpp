#include	"TestCommands.hpp"
#include	"TestCommandUtilities.hpp"

#include	"HTTP/Client/ClientConnection.hpp"
#include	"HTTP/Client/Request.hpp"
#include	"HTTP/Client/Response.hpp"
#include	"StringConversion.hpp"
#include	"Runnable.hpp"
#include	"ThreadedExecutor.hpp"
#include	"JSON/Value.hpp"
#include	"JSON/JDOMWriter.hpp"
#include	"JSON/JDOMParser.hpp"
#include	"MemoryOBinaryChannel.hpp"

#include	"DG.h"

#include	"APIEnvir.h"
#include	"ACAPinc.h"		// also includes APIdefs.h
#include	"APICommon.h"


#define	ADDON_COMMAND_TEST_ADDON_NAME			32000	//'STR#' 32000
#define	ADDON_COMMAND_TEST_MENU_STRINGS			32500	//'STR#' 32500


namespace {

	class RunAddOnCommandTask : public GS::Runnable {
	private:
		UShort			connectionPort;
		GS::UniString	commandName;
		GS::UniString	commandParams;

	public:
		RunAddOnCommandTask (const GS::UniString&	commandName,
			const GS::UniString&	commandParams,
			UShort					connectionPort);

		void Run () override;
	};


	RunAddOnCommandTask::RunAddOnCommandTask (const GS::UniString&	commandName,
		const GS::UniString&	commandParams,
		UShort				connectionPort)
		: connectionPort (connectionPort)
		, commandName (commandName)
		, commandParams (commandParams)
	{
	}


	void RunAddOnCommandTask::Run ()
	{
		try {
			IO::URI::URI connectionUrl ("http://127.0.0.1:" + GS::ValueToUniString (connectionPort));
			HTTP::Client::ClientConnection clientConnection (connectionUrl);
			clientConnection.Connect ();

			GS::UniString inputJSONString = TestCommandUtility::GetRequestJSONString (commandName, commandParams);
			JSON::ObjectValueRef inputObject = JSON::ObjectValue::Cast (JSON::JDOMStringParser ().Parse (inputJSONString));

			HTTP::Client::Request request (HTTP::MessageHeader::Method::Get, GS::UniString ("/"));
			request.GetRequestHeaderFieldCollection ().Add ("Content-Type", "application/json;charset=utf-8");
			request.GetRequestHeaderFieldCollection ().Add ("Accept", "*/*");

			GS::MemoryOBinaryChannel channel;
			JSON::JDOMWriter jdomWriter (*inputObject, channel);

			clientConnection.Send (request, channel.GetBuffer (), channel.GetSize ());

			HTTP::Client::Response response;
			JSON::JDOMParser parser;

			JSON::ValueRef parsed = parser.Parse (clientConnection.BeginReceive (response));
			if (response.GetStatusCode () == HTTP::MessageHeader::StatusCode::OK) {
				GS::UniString resultString;

				JSON::ObjectValueRef outputObject = GS::DynamicCast<JSON::ObjectValue> (parsed);
				JSON::JDOMStringWriter writer (*outputObject, resultString);

				WriteReport ("# Command name: %s", commandName.ToCStr ().Get ());
				WriteReport ("# Command output: %s", resultString.ToCStr ().Get ());
			}

			clientConnection.Close (false);
		} catch (...) {
			DBBREAK ();
		}
	}
}


static void StartAddOnCommandTest (const GS::UniString& commandName, const GS::UniString& commandParams)
{
	UShort connectionPort;

	GSErrCode err = ACAPI_Command_GetHttpConnectionPort (&connectionPort);
	if (err == NoError) {
		GS::ThreadedExecutor executor;
		executor.Execute (new RunAddOnCommandTask (commandName, commandParams, connectionPort));
	}
}


// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode  MenuCommandHandler (const API_MenuParams *menuParams)
{
	GS::UniString commandName;
	GS::UniString commandParams;

	switch (menuParams->menuItemRef.menuResID) {
	case ADDON_COMMAND_TEST_MENU_STRINGS:
		{
			switch (menuParams->menuItemRef.itemIndex) {
				case 1: commandName = "ProtectionInfoCommand";
						break;
				case 2: commandName = "CreateColumnCommand";
						commandParams = TestCommandUtility::GetCreateColumnCommandParamsString ();
						break;
			}
			break;
		}
	}

	StartAddOnCommandTest (commandName, commandParams);

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

API_AddonType		CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name,        ADDON_COMMAND_TEST_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, ADDON_COMMAND_TEST_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Preload;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode		RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (ADDON_COMMAND_TEST_MENU_STRINGS, 0, MenuCode_UserDef, MenuFlag_SeparatorBefore | MenuFlag_SeparatorAfter);
	
	DBASSERT (err == NoError);

	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode 	Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (ADDON_COMMAND_TEST_MENU_STRINGS, MenuCommandHandler);
	DBASSERT (err == NoError);

	//Install commands
	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<ProtectionInfoCommand> ());
	DBASSERT (err == NoError);

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<CreateColumnCommand> ());
	DBASSERT (err == NoError);

	return err;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode 	FreeData (void)
{
	return NoError;
}		// FreeData
