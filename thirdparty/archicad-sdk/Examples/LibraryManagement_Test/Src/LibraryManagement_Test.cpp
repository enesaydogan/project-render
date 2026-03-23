#include "LibraryManagement_Test.hpp"
#include "LibraryManagement_Test_ExampleFunctions.hpp"

#include "Resources.hpp"

#include "HTTP/Client/ClientConnection.hpp"
#include "HTTP/Client/Request.hpp"
#include "HTTP/Client/Response.hpp"
#include "StringConversion.hpp"
#include "Runnable.hpp"
#include "ThreadedExecutor.hpp"
#include "JSON/Value.hpp"
#include "JSON/JDOMWriter.hpp"
#include "JSON/JDOMParser.hpp"
#include "MemoryOBinaryChannel.hpp"

#include "DGFileDialog.hpp"
#include "UDTextInputDialog.hpp"

namespace {


GS::UniString GetTextFromUser (const GS::UniString& title)
{
	UD::TextInputDialog textDialog (title, GS::EmptyUniString, GS::EmptyUniString);
	if (textDialog.Invoke ()) {
		return textDialog.GetText ();
	}
	return GS::EmptyUniString;
}

} // namespace


// --- AddLibraryCommand ----------------------------------------------------------------------------------

GS::String AddLibraryCommand::GetName () const
{
	return "AddLibraryCommand";
}


GS::String AddLibraryCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> AddLibraryCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> AddLibraryCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> AddLibraryCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState	AddLibraryCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
	IO::Location libraryLocation;
	if (!parameters.IsEmpty ()) {
		GS::UniString libraryPath;
		parameters.Get ("libraryPath", libraryPath);
		if (libraryPath.IsEmpty ())
			return GS::ObjectState ("errorCode", NoError);
		libraryLocation.Set (libraryPath);
	}

	return GS::ObjectState ("errorCode", AddLocalLibrary (libraryLocation));
}


void AddLibraryCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}


GS::UniString GetAddLibraryCommandParamsString ()
{
	IO::Location libraryLocation;
	DG::FileDialog dlgSelectFolder (DG::FileDialog::OpenFolder);
	if (dlgSelectFolder.Invoke ()) {
		libraryLocation = dlgSelectFolder.GetSelectedLocation ();
	}
	GS::UniString libraryPathString;
	libraryLocation.ToPath (&libraryPathString);
	libraryPathString.ReplaceAll ("\\", "\\\\");

	GS::UniString jsonString {("\"libraryPath\": ") + ("\"" + libraryPathString + "\"")};
	return jsonString;
}


// --- AddLibrariesCommand ----------------------------------------------------------------------------------

GS::String AddLibrariesCommand::GetName () const
{
	return "AddLibrariesCommand";
}


GS::String AddLibrariesCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> AddLibrariesCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> AddLibrariesCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> AddLibrariesCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState	AddLibrariesCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
	GS::Array<IO::Location> libraryLocationList;
	if (!parameters.IsEmpty ()) {
		GS::Array<GS::UniString> libraryPathList;
		if (parameters.Get ("libraryPathList", libraryPathList)) {
			for (USize i = 0; i < libraryPathList.GetSize (); ++i)
				libraryLocationList.Push (IO::Location(libraryPathList [i]));
		} else {
			return GS::ObjectState ("errorCode", NoError);
		}
	}
	return GS::ObjectState ("errorCode", AddLocalLibraryList (libraryLocationList));
}


void AddLibrariesCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}


GS::UniString ComposeJSONList (const GS::Array<GS::UniString>& inputInJSONStrings)
{
	GS::UniString inputAsJSONList;
	bool isFirst = true;

	for (const auto& str : inputInJSONStrings) {
		if (isFirst)
			isFirst = false;
		else
			inputAsJSONList += ",";
		inputAsJSONList += "\"" + str + "\"";
	}
	return "[" + inputAsJSONList + "]";
}


GS::UniString GetAddLibrariesCommandParamsString ()
{
	DG::FileDialog dlg (DG::FileDialog::OpenMultiFolder);
	GS::Array<IO::Location> libraryLocations;
	if (dlg.Invoke ()) {
		for (UIndex selIndex = 0; selIndex < dlg.GetSelectionCount (); selIndex++) {
			IO::Location location = dlg.GetSelectedFile (selIndex);
			libraryLocations.Push (location);
		}
	}
	GS::Array<GS::UniString> libraryPathStringsList;
	for (auto libraryLocation : libraryLocations) {
		GS::UniString libraryPathString;
		libraryLocation.ToPath (&libraryPathString);
		libraryPathString.ReplaceAll ("\\", "\\\\");
		libraryPathStringsList.Push (libraryPathString);
	}

	GS::UniString jsonString {("\"libraryPathList\": ") + ComposeJSONList (libraryPathStringsList)};
	return jsonString;
}


// --- AddServerLibraryCommand ----------------------------------------------------------------------------------


GS::UniString GetAddServerLibraryCommandParamsString ()
{
	// "http://localhost:22000", "Libraries/TestLibrary"
	GS::UniString serverAddress = GetTextFromUser (u"Server Address"sv);
	GS::UniString libraryPath = GetTextFromUser (u"Library Path"sv);

	libraryPath.ReplaceAll ("\\", "\\\\");
	serverAddress.ReplaceAll ("\\", "\\\\");

	GS::UniString jsonString {("\"serverAddress\": ") + ("\"" + serverAddress + "\"") + (" ,") + ("\"libraryPath\": ") + ("\"" + libraryPath + "\"")};
	return jsonString;
}


GS::String AddServerLibraryCommand::GetName () const
{
	return "AddServerLibraryCommand";
}


GS::String AddServerLibraryCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> AddServerLibraryCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> AddServerLibraryCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> AddServerLibraryCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState	AddServerLibraryCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
	GS::UniString serverAddress;
	GS::UniString libraryPath;
	if (!parameters.IsEmpty ()) {
		parameters.Get ("serverAddress", serverAddress);
		if (serverAddress.IsEmpty ())
			return GS::ObjectState ("errorCode", NoError);
		parameters.Get ("libraryPath", libraryPath);
		if (libraryPath.IsEmpty ())
			return GS::ObjectState ("errorCode", NoError);
	}

	return GS::ObjectState ("errorCode", AddServerLibrary (serverAddress, libraryPath));
}


void AddServerLibraryCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}


// --- RemoveAllLibrariesCommand ----------------------------------------------------------------------------------

GS::String RemoveAllLibrariesCommand::GetName () const
{
	return "RemoveAllLibrariesCommand";
}


GS::String RemoveAllLibrariesCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> RemoveAllLibrariesCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> RemoveAllLibrariesCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> RemoveAllLibrariesCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState	RemoveAllLibrariesCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
	return GS::ObjectState ("errorCode", RemoveAllLibraries ());
}


void RemoveAllLibrariesCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}


// --- DumpLibraryListCommand ----------------------------------------------------------------------------------

GS::String DumpLibraryListCommand::GetName () const
{
	return "DumpLibraryListCommand";
}


GS::String DumpLibraryListCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> DumpLibraryListCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> DumpLibraryListCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> DumpLibraryListCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState	DumpLibraryListCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
	return GS::ObjectState ("errorCode", DumpLibraryList ());
}


void DumpLibraryListCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}


// --- DumpAllLibPartsCommand ----------------------------------------------------------------------------------

GS::String DumpAllLibPartsCommand::GetName () const
{
	return "DumpAllLibPartsCommand";
}


GS::String DumpAllLibPartsCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> DumpAllLibPartsCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> DumpAllLibPartsCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> DumpAllLibPartsCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState	DumpAllLibPartsCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
	return GS::ObjectState ("errorCode", DumpAllLibParts ());
}


void DumpAllLibPartsCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}


// --- DuplicateSelectionToEmbeddedCommand ----------------------------------------------------------------------------------

GS::String DuplicateSelectionToEmbeddedCommand::GetName () const
{
	return "DuplicateSelectionToEmbeddedCommand";
}


GS::String DuplicateSelectionToEmbeddedCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> DuplicateSelectionToEmbeddedCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> DuplicateSelectionToEmbeddedCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> DuplicateSelectionToEmbeddedCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState	DuplicateSelectionToEmbeddedCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
	return GS::ObjectState ("errorCode", DuplicateSelectionToEmbeddedLibrary ());
}


void DuplicateSelectionToEmbeddedCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}


// --- CreateNewObjectFromSelectionCommand -----------------------------------------------------------------------------

GS::String CreateNewObjectFromSelectionCommand::GetName () const
{
	return "CreateNewObjectFromSelectionCommand";
}


GS::String CreateNewObjectFromSelectionCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> CreateNewObjectFromSelectionCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> CreateNewObjectFromSelectionCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> CreateNewObjectFromSelectionCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState	CreateNewObjectFromSelectionCommand::Execute (const GS::ObjectState& /*parameters*/, GS::ProcessControl& /*processControl*/) const
{
	return GS::ObjectState ("errorCode", CreateNewObjectFromSelection ());
}


void CreateNewObjectFromSelectionCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const
{
}

// --- UploadLibrariesToServerCommand -----------------------------------------------------------------------------


GS::String UploadLibrariesToServerCommand::GetName () const
{
	return "UploadLibrariesToServerCommand";
}


GS::String UploadLibrariesToServerCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> UploadLibrariesToServerCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> UploadLibrariesToServerCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> UploadLibrariesToServerCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState UploadLibrariesToServerCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
	GS::Array<IO::Location> libraryLocationList;
	GS::UniString			serverAddress;
	GS::UniString			serverFolderPath;
	if (!parameters.IsEmpty ()) {
		GS::Array<GS::UniString> libraryPathList;
		if (parameters.Get ("libraryPathList", libraryPathList) && !libraryPathList.IsEmpty ()) {
			for (USize i = 0; i < libraryPathList.GetSize (); ++i)
				libraryLocationList.Push (IO::Location (libraryPathList[i]));
		} else {
			return GS::ObjectState ("errorCode", NoError);
		}
		parameters.Get ("serverAddress", serverAddress);
		if (serverAddress.IsEmpty ())
			return GS::ObjectState ("errorCode", NoError);
		parameters.Get ("serverFolderPath", serverFolderPath);
	}

	return GS::ObjectState ("errorCode", UploadLibrariesToServer (libraryLocationList, serverAddress, serverFolderPath));
}


void UploadLibrariesToServerCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const {}


GS::UniString GetUploadLibrariesToServerCommandParamString ()
{
	DG::FileDialog			dlg (DG::FileDialog::OpenMultiFolder);
	GS::Array<IO::Location> libraryLocations;
	if (dlg.Invoke ()) {
		for (UIndex selIndex = 0; selIndex < dlg.GetSelectionCount (); selIndex++) {
			IO::Location location = dlg.GetSelectedFile (selIndex);
			libraryLocations.Push (location);
		}
	}
	GS::Array<GS::UniString> libraryPathStringsList;
	for (auto&& libraryLocation : libraryLocations) {
		GS::UniString libraryPathString;
		libraryLocation.ToPath (&libraryPathString);
		libraryPathString.ReplaceAll (u"\\"sv, u"\\\\"sv);
		libraryPathStringsList.Push (libraryPathString);
	}

	// "http://localhost:22000", "Libraries/TestLibrary"
	GS::UniString serverAddress = GetTextFromUser (u"Server Address"sv);
	GS::UniString serverFolderPath	= GetTextFromUser (u"Server Folder Path"sv);

	serverFolderPath.ReplaceAll (u"\\"sv, u"\\\\"sv);
	serverAddress.ReplaceAll (u"\\"sv, u"\\\\"sv);

	GS::UniString jsonString {
		(u"\"libraryPathList\": "sv) + ComposeJSONList (libraryPathStringsList) + (u" ,"sv) +
		(u"\"serverAddress\": "sv) + (u"\""sv + serverAddress + "\""sv) + (u" ,"sv) +
		(u"\"serverFolderPath\": "sv) + (u"\""sv + serverFolderPath + "\""sv)
	};
	return jsonString;
}


// --- UploadLibrariesToServerCommand -----------------------------------------------------------------------------


GS::String RemoveLibrariesFromServerCommand::GetName () const
{
	return "RemoveLibrariesFromServerCommand";
}


GS::String RemoveLibrariesFromServerCommand::GetNamespace () const
{
	return "LibraryManagement_Test";
}


GS::Optional<GS::UniString> RemoveLibrariesFromServerCommand::GetSchemaDefinitions () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> RemoveLibrariesFromServerCommand::GetInputParametersSchema () const
{
	return GS::NoValue;
}


GS::Optional<GS::UniString> RemoveLibrariesFromServerCommand::GetResponseSchema () const
{
	return GS::NoValue;
}


GS::ObjectState RemoveLibrariesFromServerCommand::Execute (const GS::ObjectState& parameters, GS::ProcessControl& /*processControl*/) const
{
	GS::Array<GS::UniString> libraryNames;
	GS::UniString			serverAddress;
	GS::UniString			serverFolderPath;
	if (!parameters.IsEmpty ()) {
		parameters.Get ("libraryNames", libraryNames);
		parameters.Get ("serverAddress", serverAddress);
		if (serverAddress.IsEmpty ())
			return GS::ObjectState ("errorCode", NoError);
		parameters.Get ("serverFolderPath", serverFolderPath);
	}

	return GS::ObjectState ("errorCode", RemoveLibrariesFromServer (libraryNames, serverAddress, serverFolderPath));
}


void RemoveLibrariesFromServerCommand::OnResponseValidationFailed (const GS::ObjectState& /*response*/) const {}


GS::UniString GetRemoveLibrariesFromServerCommandParamString ()
{
	// "http://localhost:22000", "Libraries/TestLibrary"
	GS::UniString serverAddress	   = GetTextFromUser (u"Server Address"sv);
	GS::UniString serverFolderPath = GetTextFromUser (u"Server Folder Path"sv);
	GS::UniString			 libraryNamesCommaSeparated = GetTextFromUser (u"Library names to delete, separated by comma"sv);
	GS::Array<GS::UniString> libraryNames				= libraryNamesCommaSeparated.IsEmpty () ? GS::Array<GS::UniString> () : libraryNamesCommaSeparated.Split (u","sv);
	for (auto& libName : libraryNames) {
		libName.Trim ();
	}

	serverFolderPath.ReplaceAll (u"\\"sv, u"\\\\"sv);
	serverAddress.ReplaceAll (u"\\"sv, u"\\\\"sv);

	GS::UniString jsonString {(u"\"libraryNames\": "sv) + ComposeJSONList (libraryNames) + (u" ,"sv) + (u"\"serverAddress\": "sv) +
							  (u"\""sv + serverAddress + "\""sv) + (u" ,"sv) + (u"\"serverFolderPath\": "sv) + (u"\""sv + serverFolderPath + "\""sv)};
	return jsonString;
}


// -----------------------------------------------------------------------------


static GS::UniString GetRequestJSONString (const GS::UniString& commandName, const GS::UniString& parameters)
{
	return	"{ \"command\": \"API.ExecuteAddOnCommand\","
		" \"parameters\" : { "
		"  \"addOnCommandId\": {"
		"   \"commandNamespace\": \"" "LibraryManagement_Test" "\","
		"   \"commandName\" : \"" + commandName + "\""
		"  },"
		"  \"addOnCommandParameters\": {" + parameters +
		"  }"
		" }"
		"}";
}


namespace {

class RunAddOnCommandTask : public GS::Runnable {
	private:
		UShort			connectionPort;
		GS::UniString	commandName;
		GS::UniString	commandParams;

	public:
		RunAddOnCommandTask (const GS::UniString& commandName,
							 const GS::UniString& commandParams,
							 UShort					connectionPort);

		void Run () override;
	};


	RunAddOnCommandTask::RunAddOnCommandTask (const GS::UniString& commandName,
											  const GS::UniString& commandParams,
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

			GS::UniString inputJSONString = GetRequestJSONString (commandName, commandParams);
			JSON::ObjectValueRef inputObject = JSON::ObjectValue::Cast (JSON::JDOMStringParser ().Parse (inputJSONString));

			HTTP::Client::Request request (HTTP::MessageHeader::Method::Get, GS::UniString ("/"));
			request.GetRequestHeaderFieldCollection ().Add ("Content-Type", "application/json;charset=utf-8");
			request.GetRequestHeaderFieldCollection ().Add ("Accept", "*/*");

			GS::MemoryOBinaryChannel channel;
			JSON::JDOMWriter jdomWriter (*inputObject, channel);

			clientConnection.Send (request, channel.GetBuffer (), channel.GetSize ());

			HTTP::Client::Response response;
			JSON::JDOMParser parser;

			clientConnection.Receive (response);
			clientConnection.Close (false);
		} catch (...) {
			return;
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

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	GS::UniString commandName;
	GS::UniString commandParams;

	switch (menuParams->menuItemRef.menuResID) {
		case LM_TEST_MENU_STRINGS:
		{
			switch (menuParams->menuItemRef.itemIndex)
			{
				case 1:
					commandName = "AddLibraryCommand";
					commandParams = GetAddLibraryCommandParamsString ();
					break;
				case 2:
					commandName = "AddServerLibraryCommand";
					commandParams = GetAddServerLibraryCommandParamsString ();
					break;
				case 3:
					commandName = "RemoveAllLibrariesCommand";
					break;
				case 4:
					commandName = "DumpLibraryListCommand";
					break;
				case 5:
					commandName = "DumpAllLibPartsCommand";
					break;
				case 6:
					commandName = "DuplicateSelectionToEmbeddedCommand";
					break;
				case 7:
					commandName = "CreateNewObjectFromSelectionCommand";
					break;
				case 8:
					commandName = "AddLibrariesCommand";
					commandParams = GetAddLibrariesCommandParamsString ();
					break;
				case 9:
					commandName = "UploadLibrariesToServerCommand";
					commandParams = GetUploadLibrariesToServerCommandParamString ();
					break;
				case 10:
					commandName	  = "RemoveLibrariesFromServerCommand";
					commandParams = GetRemoveLibrariesFromServerCommandParamString ();
					break;
			}
			StartAddOnCommandTest (commandName, commandParams);
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
	RSGetIndString (&envir->addOnInfo.name,        LM_TEST_ADDON_NAME, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, LM_TEST_ADDON_NAME, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Preload;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (LM_TEST_MENU_STRINGS, LM_TEST_MENU_PROMPT_STRINGS, MenuCode_UserDef, MenuFlag_Default);
	DBASSERT (err == NoError);
	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (LM_TEST_MENU_STRINGS, MenuCommandHandler);
	if (err != NoError) return err;

	//Install commands

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<AddLibraryCommand> ());
	if (err != NoError) return err;

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<AddServerLibraryCommand> ());
	if (err != NoError) return err;

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<RemoveAllLibrariesCommand> ());
	if (err != NoError) return err;

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<DumpLibraryListCommand> ());
	if (err != NoError) return err;

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<DumpAllLibPartsCommand> ());
	if (err != NoError) return err;

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<DuplicateSelectionToEmbeddedCommand> ());
	if (err != NoError) return err;

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<CreateNewObjectFromSelectionCommand> ());
	if (err != NoError) return err;

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<AddLibrariesCommand> ());
	if (err != NoError) return err;

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<UploadLibrariesToServerCommand> ());
	if (err != NoError) return err;

	err = ACAPI_AddOnAddOnCommunication_InstallAddOnCommandHandler (GS::NewOwned<RemoveLibrariesFromServerCommand> ());
	if (err != NoError) return err;

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
