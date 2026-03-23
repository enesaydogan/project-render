// *****************************************************************************
// Source code for the Communication Manager Add-On
// *****************************************************************************

#define	_COMMUNICATION_MANAGER_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"BuiltInLibrary.hpp"
#include	"GSUnID.hpp"

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"
#include	"MDIDs_APICD.h"

#include	"Communication_Client.h"

#include	"Folder.hpp"
#include	"FileSystem.hpp"
#include	"StringConversion.hpp"
#include	"DGFileDialog.hpp"

// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------



// =============================================================================
//
//
//
// =============================================================================

// -----------------------------------------------------------------------------
// Test communication between us and the client
// -----------------------------------------------------------------------------

static void		Do_CMD1_Test (void)
{
	API_ModulID		mdid;
	GSErrCode		err;

	mdid = {};
	mdid.developerID = 'xxxx';								// <--- Bad data (invalid Module ID). ACAPI_TestCommand should fail
	mdid.localID = MDID_APICD_CommunicationClient;
	err = ACAPI_AddOnAddOnCommunication_Test (&mdid, CmdID_PutText, 0);
	DBASSERT (err == APIERR_MODULNOTINSTALLED);


	mdid = {};
	mdid.developerID = MDID_APICD;
	mdid.localID = MDID_APICD_CommunicationClient;
	err = ACAPI_AddOnAddOnCommunication_Test (&mdid, 'xxxx', 0);			// <--- Bad data (invalid command ID). ACAPI_AddOnAddOnCommunication_Test should fail
	DBASSERT (err == APIERR_MODULCMDNOTSUPPORTED);

	mdid = {};
	mdid.developerID = MDID_APICD;
	mdid.localID = MDID_APICD_CommunicationClient;
	err = ACAPI_AddOnAddOnCommunication_Test (&mdid, CmdID_PutText, 2);		// <--- Bad data (too new command version). ACAPI_AddOnAddOnCommunication_Test should fail
	DBASSERT (err == APIERR_MODULCMDVERSNOTSUPPORTED);

	err = ACAPI_AddOnAddOnCommunication_Test (&mdid, CmdID_PutText, 1);
	if (err != NoError) {
		switch (err) {
			case APIERR_MODULNOTINSTALLED:
						WriteReport_Alert ("Target add-on is not installed");
						break;
			case APIERR_MODULCMDNOTSUPPORTED:
						WriteReport_Alert ("Bad command ID was passed");
						break;
			case APIERR_MODULCMDVERSNOTSUPPORTED:
						WriteReport_Alert ("Bad command version");
						break;
		}
	}

	return;
}		// Do_CMD1_Test


// -----------------------------------------------------------------------------
// Call the Communication Client module
// -----------------------------------------------------------------------------

static void		Do_CMD1_Call (void)
{
	bool				passParameters, avoidDialog;
	GSHandle			parHdl;
	API_MDCLParameter	par;
	API_ModulID			mdid;
	GSErrCode			err;

	passParameters = GetMenuItemMark (32500, 1);
	avoidDialog    = GetMenuItemMark (32500, 2);

	if (passParameters) {
		err = ACAPI_AddOnAddOnCommunication_InitMDCLParameterList (&parHdl);
		if (err != NoError)
			ErrorBeep ("APIAny_InitMDCLParameterListID", err);

		if (err == NoError) {
			par.name = "pen";
			par.type = MDCLPar_int;
			par.int_par = 14;
			err = ACAPI_AddOnAddOnCommunication_AddMDCLParameter (parHdl, &par);
			if (err != NoError)
				ErrorBeep ("APIAny_AddMDCLParameterID", err);
		}
		if (err == NoError) {
			par.name = "size";
			par.type = MDCLPar_float;
			par.float_par = 6.5;
			err = ACAPI_AddOnAddOnCommunication_AddMDCLParameter (parHdl, &par);
			if (err != NoError)
				ErrorBeep ("APIAny_AddMDCLParameterID", err);
		}
		if (err == NoError) {
			par.name = "content";
			par.type = MDCLPar_string;
			par.string_par = "This string is coming from another module";
			err = ACAPI_AddOnAddOnCommunication_AddMDCLParameter (parHdl, &par);
			if (err != NoError)
				ErrorBeep ("APIAny_AddMDCLParameterID", err);
		}
	} else
		parHdl = nullptr;

	mdid = {};
	mdid.developerID = MDID_APICD;
	mdid.localID = MDID_APICD_CommunicationClient;
	err = ACAPI_AddOnAddOnCommunication_Call (&mdid, CmdID_PutText, 1, parHdl, nullptr, avoidDialog);

	if (parHdl != nullptr) {
		err = ACAPI_AddOnAddOnCommunication_FreeMDCLParameterList (&parHdl);
		if (err != NoError)
			ErrorBeep ("APIAny_FreeMDCLParameterListID", err);
	}

	return;
}		// Do_CMD1_Call


// -----------------------------------------------------------------------------
// Create a link to the Communication Client
// -----------------------------------------------------------------------------

static void		Do_LinkToClient (void)
{
	API_ModulID			mdid = {};
	CallBackTbl_Client	procTbl;
	GSErrCode			err;

	mdid.developerID = MDID_APICD;
	mdid.localID = MDID_APICD_CommunicationClient;

	err = ACAPI_AddOnAddOnCommunication_Call (&mdid, CmdID_BegBinaryLink, 1, nullptr, (GSPtr) &procTbl, false);
	if (err != NoError) {
		ErrorBeep ("CmdID_BegBinaryLink", err);
		return;
	}

	(*procTbl.counterProc) ();
	(*procTbl.counterProc) ();
	(*procTbl.counterProc) ();
	(*procTbl.counterProc) ();

	err = ACAPI_AddOnAddOnCommunication_Call (&mdid, CmdID_EndBinaryLink, 1, nullptr, nullptr, false);
	if (err != NoError) {
		ErrorBeep ("CmdID_BegBinaryLink", err);
		return;
	}

	return;
}		// Do_LinkToClient


// -----------------------------------------------------------------------------
// Create a link to the 3D Manager
// -----------------------------------------------------------------------------

static void		Do_Dump3DModel (void)
{
	API_ModulID		mdid = {};
	GSErrCode		err;

	mdid.developerID = MDID_APICD;
	mdid.localID = MDID_APICD_3DTest;

	err = ACAPI_AddOnAddOnCommunication_Call (&mdid, 'DUMP', 1, nullptr, nullptr, false);
	if (err != NoError) {
		ErrorBeep ("\'DUMP\'", err);
		return;
	}

	return;
}		// Do_Dump3DModel


// -----------------------------------------------------------------------------
// Save a given project as DWG file in external ARCHICAD
// -----------------------------------------------------------------------------
static Int32	waitingForProjectCounter = 0;

GSErrCode ExternalCallBackProc (GSHandle parHdl, GSErrCode returnCode)
{
	if (waitingForProjectCounter <= 0)
		return APIERR_GENERAL;				// unexpected call

	waitingForProjectCounter--;

	if (returnCode != NoError)
		return NoError;

	if (parHdl == nullptr)
		return APIERR_GENERAL;

	Int32		nPars, i;
	GSErrCode	err;

	err = ACAPI_AddOnAddOnCommunication_GetMDCLParameterNum (parHdl, &nPars);
	if (err != NoError)
		return err;

	IO::Location	outfile, configFile;
	for (i = 1; i <= nPars; i++) {
		API_MDCLParameter	par = {};

		par.index = i;
		err = ACAPI_AddOnAddOnCommunication_GetMDCLParameter (parHdl, &par);
		if (err != NoError)
			return err;
		if (CHCompareCStrings (par.name, "FileName"sv, GS::CaseSensitive) == 0)
			outfile.Set (par.string_par);
		else if (CHCompareCStrings (par.name, "ConfigFile"sv, GS::CaseSensitive) == 0)
			configFile.Set (par.string_par);
	}

	if (!outfile.IsLegalInNativeFileSystem ())
		return APIERR_GENERAL;

	ACAPI_WriteReport ("DXF/DWG file \"%s\" was saved in external Archicad.", false, outfile.ToDisplayText ().ToCStr ().Get ());

	return NoError;
}


static void		Do_CallExternalSaveAsDWG (void)
{
	API_ModulID	mdid = { 1198731108, 1322668197 };		// DXF/DWG add-on
	API_MDCLParameter	par;
	GSHandle			parHdl;
	GSErrCode			err;
	IO::Location		configFile;

	waitingForProjectCounter = 0;

	// local call DWG add-on to retrieve the default translator
	err = ACAPI_AddOnAddOnCommunication_InitMDCLParameterList (&parHdl);
	if (err != NoError)
		return;

	par = {};
	par.name = "Mode";
	par.type = MDCLPar_string;
	par.string_par = "Default";
	err = ACAPI_AddOnAddOnCommunication_AddMDCLParameter (parHdl, &par);
	IO::Location loc;
	if (err == NoError) {
		err = ACAPI_AddOnAddOnCommunication_Call (&mdid, 'GDCO', 1, parHdl, (GSPtr)&loc, false);	// get default translator
	}

	if (err == NoError) 
		configFile.Set (loc.ToDisplayText ());
	
	ACAPI_AddOnAddOnCommunication_FreeMDCLParameterList (&parHdl);

	if (!configFile.IsLegalInNativeFileSystem ())
		return;


	// external call DWG add-on to save from external projects
	IO::Location projectFileLoc[2];
	API_SpecFolderID specID = API_ApplicationFolderID;
	ACAPI_ProjectSettings_GetSpecFolder (&specID, &projectFileLoc[0]);
	projectFileLoc[0].AppendToLocal (IO::Name ("Archicad Examples"));
	projectFileLoc[0].AppendToLocal (IO::Name ("Orchard Project"));
	projectFileLoc[0].AppendToLocal (IO::Name ("Orchard Project.pla"));
	ACAPI_ProjectSettings_GetSpecFolder (&specID, &projectFileLoc[1]);
	projectFileLoc[1].AppendToLocal (IO::Name ("Archicad Examples"));
	projectFileLoc[1].AppendToLocal (IO::Name ("Residential House"));
	projectFileLoc[1].AppendToLocal (IO::Name ("Residential House.pla"));

	IO::Location outputLoc[2];
	specID = API_UserDocumentsFolderID;
	ACAPI_ProjectSettings_GetSpecFolder (&specID, &(outputLoc[0]));
	outputLoc[0].AppendToLocal (IO::Name ("Orchard Project.DWG"));
	ACAPI_ProjectSettings_GetSpecFolder (&specID, &(outputLoc[1]));
	outputLoc[1].AppendToLocal (IO::Name ("Residential House.DWG"));

	char str[512];
	IO::URL url;
	for (Int32 indProj = 0; indProj < 2; indProj++) {
		outputLoc[indProj].ToURL (&url);
		err = ACAPI_AddOnAddOnCommunication_InitMDCLParameterList (&parHdl);
		if (err != NoError)
			break;

		par = {};
		par.name = "FileName";
		par.type = MDCLPar_string;
		CHTruncate ((const char*) url, str, sizeof (str));
		par.string_par = str;
		err = ACAPI_AddOnAddOnCommunication_AddMDCLParameter (parHdl, &par);
		if (err != NoError)
			break;

		par = {};
		par.name = "ConfigFile";
		par.type = MDCLPar_string;
		configFile.ToURL (&url);
		CHTruncate ((const char*) url, str, sizeof (str));
		par.string_par = str;
		err = ACAPI_AddOnAddOnCommunication_AddMDCLParameter (parHdl, &par);
		if (err != NoError)
			break;

		err = ACAPI_AddOnAddOnCommunication_ExternalCall (&(projectFileLoc[indProj]), &mdid, 'SAV2', 1, parHdl, true, ExternalCallBackProc);
		if (err == NoError)
			waitingForProjectCounter++;

		ACAPI_AddOnAddOnCommunication_FreeMDCLParameterList (&parHdl);
	}

	Int32 numOfTasks = waitingForProjectCounter;
	if (numOfTasks > 0) {
		Int32 nPhase = 1;
		GS::UniString title ("Exporting External Projects");
		ACAPI_ProcessWindow_InitProcessWindow (&title, &nPhase);
		GS::UniString subtitle ("Please wait...");
		ACAPI_ProcessWindow_SetNextProcessPhase (&subtitle, &numOfTasks);
	}

	// waiting for the answers
	Int32 procValue = numOfTasks - waitingForProjectCounter;
	while (waitingForProjectCounter > 0) {
		if (procValue != numOfTasks - waitingForProjectCounter) {
			procValue = numOfTasks - waitingForProjectCounter;
			ACAPI_ProcessWindow_SetProcessValue (&procValue);
		}
		TIWait (0.05);		// let other threads run in the meanwhile
		if (ACAPI_ProcessWindow_IsProcessCanceled ())
			break;
	}

	if (numOfTasks > 0)
		ACAPI_ProcessWindow_CloseProcessWindow ();

	return;
}		// Do_CallExternalSaveAsDWG


// -----------------------------------------------------------------------------
// Open the selected DWG file into a library part
// -----------------------------------------------------------------------------
static bool	GetOpenFile (IO::Location*	dloc, const char* fileExtensions, const GS::UniString& filterText)
{
	if (dloc == nullptr)
		return false;

	FTM::FileTypeManager	ftman ("MyManager");
	FTM::FileType			type (nullptr, fileExtensions, 0, 0, 0);
	FTM::TypeID				id = FTM::FileTypeManager::SearchForType (type);
	if (id == FTM::UnknownType)
		id = ftman.AddType (type);

	DG::FileDialog	dlg (DG::FileDialog::OpenFile);			// Open only 1 file
	UIndex	i = dlg.AddFilter (id, DG::FileDialog::DisplayExtensions);	// Force it on mac...
	dlg.SetFilterText (i, filterText);

	if (!dlg.Invoke ())
		return false;

	*dloc = dlg.GetSelectedFile ();
	return true;
}

static void		Do_CallOpenDWGToObject (void)
{
	API_ModulID	mdid = { 1198731108, 1322668197 };		// DXF/DWG add-on
	GSErrCode	err;

	IO::Location dwgFileLoc;
	if (!GetOpenFile (&dwgFileLoc, "dwg", "*.dwg"))
		return;

	IO::Name	dwgName;
	dwgFileLoc.GetLastLocalName (&dwgName);

	API_LibPart	libPart = {};
	GS::ucscpy (libPart.docu_UName, dwgName.GetBase ().ToUStr ());
	libPart.typeID = APILib_ObjectID;
	libPart.isTemplate = false;
	libPart.isPlaceable = true;

	const GS::UnID unID = BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetUnIDWithNullRevGuid (BL::BuiltInLibPartID::ModelElementLibPartID);

	CHCopyC (unID.ToUniString ().ToCStr (), libPart.parentUnID);	// Model Element subtype

	IO::Location fileLoc;
	API_SpecFolderID specID = API_UserDocumentsFolderID;
	ACAPI_ProjectSettings_GetSpecFolder (&specID, &fileLoc);
	fileLoc.AppendToLocal (IO::Name ("DWGLibPart"));
	IO::fileSystem.CreateFolder (fileLoc);

	libPart.location = &fileLoc;
	err = ACAPI_LibraryPart_Create (&libPart);

	if (err == NoError) {
		// call the Dxf add-on
		GSHandle parHdl;
		err = ACAPI_AddOnAddOnCommunication_InitMDCLParameterList (&parHdl);
		if (err == NoError) {
			IO::URL url;
			dwgFileLoc.ToURL (&url);

			API_MDCLParameter par;
			par = {};
			par.name = "FileName";
			par.type = MDCLPar_string;

			char str[512] = {0};
			CHTruncate ((const char*) url, str, sizeof (str));
			par.string_par = str;

			err = ACAPI_AddOnAddOnCommunication_AddMDCLParameter (parHdl, &par);

			if (err == NoError)
				err = ACAPI_AddOnAddOnCommunication_Call (&mdid, 'OOBJ', 1, parHdl, nullptr, true);
		}

		ACAPI_AddOnAddOnCommunication_FreeMDCLParameterList (&parHdl);
	}

	if (err == NoError)
		err = ACAPI_LibraryPart_Save (&libPart);

	libPart.location = nullptr;
}		// Do_CallOpenDWGToObject


static void		Do_CallImportPointCloud (void)
{
	IO::Location pcToImportLoc;		// location of the point cloud file to import
	IO::Location pcTargetLoc;		// target folder of the GSPC file

	if (!GetOpenFile (&pcToImportLoc, "e57", "*.e57"))
		return;

	pcTargetLoc = pcToImportLoc;
	pcTargetLoc.DeleteLastLocalName ();
	pcTargetLoc.AppendToLocal (IO::Name ("resultGSPCs"));
	IO::Folder pcTargetFolder (pcTargetLoc, IO::Folder::Create);
	if (DBERROR (pcTargetFolder.GetStatus () != NoError))
		return;

	API_ModulID pcInMdid = { 1198731108, 797613762 };	// MDID of the Point Cloud Importer add-on

	if (DBERROR (ACAPI_AddOnAddOnCommunication_Test (&pcInMdid, 'PCIN', 1L) != NoError))	//current version
		return;

	if (DBERROR (ACAPI_AddOnAddOnCommunication_Test (&pcInMdid, 'PCIN', 2L) == NoError))	//2 shouldn't be supported (if it is, test it too!)
		return;

	GSHandle parHdl = nullptr;		// handle of command call parameters

	GSErrCode err = ACAPI_AddOnAddOnCommunication_InitMDCLParameterList (&parHdl);
	if (err == NoError) {
		API_MDCLParameter par;	// parameter struct
		par = {};
		par.name = "PCToImportLoc";
		par.type = MDCLPar_pointer;
		par.ptr_par = &pcToImportLoc;
		err = ACAPI_AddOnAddOnCommunication_AddMDCLParameter (parHdl, &par);

		if (err == NoError) {
			par = {};
			par.name = "PCTargetLoc";
			par.type = MDCLPar_pointer;
			par.ptr_par = &pcTargetLoc;
			err = ACAPI_AddOnAddOnCommunication_AddMDCLParameter (parHdl, &par);
		}

		if (err == NoError) {
			API_Coord3D offset;
			err = ACAPI_AddOnAddOnCommunication_Call (&pcInMdid, 'PCIN', 1L, parHdl, (GSPtr)&offset, true);
			if (DBERROR (err != NoError)) {
				DGAlert (DG_INFORMATION, "ACAPI_AddOnAddOnCommunication_Call failed", "GSPC was not created.", "", "OK");
			} else {
				DGAlert (DG_INFORMATION,
						 "ACAPI_AddOnAddOnCommunication_Call succeeded",
						 "GSPC was created in \"resultGSPCs\" folder next to the e57 file.",
						 (GS::UniString ("Offsets:") +
						 GS::UniString ("  X:") + GS::ValueToUniString (offset.x) +
						 GS::UniString (", Y:") + GS::ValueToUniString (offset.y) +
						 GS::UniString (", Z:") + GS::ValueToUniString (offset.z)),
						 "OK");
			}
		}

		err = ACAPI_AddOnAddOnCommunication_FreeMDCLParameterList (&parHdl);
	}

	DBASSERT (err == NoError);

	return;
}		// Do_CallImportPointCloud


// -----------------------------------------------------------------------------
// DoCommand
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *params)
{
	switch (params->menuItemRef.itemIndex) {
		case 1:		InvertMenuItemMark (32500, 1);		break;
		case 2:		InvertMenuItemMark (32500, 2);		break;
		/* ----- */
		case 4:		Do_CMD1_Test ();					break;
		case 5:		Do_CMD1_Call ();					break;
		/* ----- */
		case 7:		Do_LinkToClient ();					break;
		/* ----- */
		case 9:		Do_Dump3DModel ();					break;
		/* ----- */
		case 11:	Do_CallExternalSaveAsDWG ();		break;
		case 12:	Do_CallOpenDWGToObject ();			break;
		/* ----- */
		case 14:	Do_CallImportPointCloud ();			break;
	}

	return NoError;
}	// DoCommand


// =============================================================================
//
// Required functions
//
// =============================================================================


//------------------------------------------------------
// Dependency definitions
//------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		/* CheckEnvironment */


//------------------------------------------------------
// Interface definitions
//------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	ACAPI_MenuItem_RegisterMenu (32500, 0, MenuCode_UserDef, MenuFlag_Default);

	return NoError;
}		/* RegisterInterface */


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Communication_Manager::Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	return err;
}	// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}	// FreeData
