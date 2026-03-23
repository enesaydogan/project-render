#include "LibraryManagement_Test_ExampleFunctions.hpp"

#include "LibraryManagement_Test.hpp"

#include "BuiltInLibrary.hpp"
#include "GSUnID.hpp"
#include "GXImage.hpp"
#include "Resources.hpp"

#include "HTTP/Client/ClientConnection.hpp"
#include "HTTP/Client/Request.hpp"
#include "HTTP/Client/Response.hpp"
#include "MemoryOBinaryChannel.hpp"
#include "Runnable.hpp"
#include "StringConversion.hpp"
#include "ThreadedExecutor.hpp"
#include "JSON/JDOMParser.hpp"
#include "JSON/JDOMWriter.hpp"
#include "JSON/Value.hpp"

// from API
#include "ACAPinc.h"
#include "APIEnvir.h"

// from ACAPI
#include "ACAPI/Library/GSMObject.hpp"
#include "ACAPI/Library/LibPart.hpp"
#include "ACAPI/Library/LibraryManager.hpp"
#include "ACAPI/Library/LibraryTreePath.hpp"

#include "CommonSize.h"
#include "GDLParamInterface.hpp"
#include "VectorImage.hpp"

#include "DGFileDialog.hpp"
#include "UDTextInputDialog.hpp"



void DumpLibPartGeneralData (const ACAPI::Library::LibraryManager& libraryManager, const ACAPI::Library::LibPart& libPart)
{
	IO::Location  libPartLocation = *libPart.GetLocation ();
	GS::UniString locationString;
	libPartLocation.ToPath (&locationString);
	ACAPI_WriteReport (GS::UniString::Printf ("Name: %T", (*libPart.GetName ()).ToPrintf ()), false);
	ACAPI_WriteReport (GS::UniString::Printf ("Location: %T", locationString.ToPrintf ()), false);
	ACAPI_WriteReport (GS::UniString::Printf ("Type: %d", (*libPart.GetType ())), false);

	ACAPI::Result<ACAPI::Library::Library> library = libraryManager.GetLibraryOfLibPart (libPart);
	if (library.IsOk ()) {
		ACAPI_WriteReport (GS::UniString::Printf ("Library: %T (%T)", (*library->GetDisplayName ()).ToPrintf (),(*library->GetName ()).ToPrintf ()), false);
	}

	ACAPI::Result<ACAPI::Library::LibraryTreePath> libraryTreePath = libraryManager.GetLibraryTreePathOfLibPart (libPart);
	if (libraryTreePath.IsOk ()) {
		GS::UniString libraryTreePathPartsString ("");
		for (GS::UniString treePathPart : libraryTreePath->GetParts ()) {
			libraryTreePathPartsString.Append ("\\" + treePathPart);
		}
		ACAPI_WriteReport (GS::UniString::Printf ("Library tree path: %T", libraryTreePathPartsString.ToPrintf ()), false);
	}
}


void DumpGSMObjectGeneralData (const ACAPI::Library::GSMObject& gsmObject)
{
	if (gsmObject.GetUnID ().IsOk ()) {
		ACAPI_WriteReport (GS::UniString::Printf ("MainGuid: %T", gsmObject.GetUnID ()->GetMainGuid ().ToUniString ().ToPrintf ()), false);
		ACAPI_WriteReport (GS::UniString::Printf ("RevGuid: %T", gsmObject.GetUnID ()->GetRevGuid ().ToUniString ().ToPrintf ()), false);
	}

	ACAPI_WriteReport (GS::UniString::Printf ("IsSubType: %s", *gsmObject.IsSubType () ? "true" : "false"), false);

	if (gsmObject.GetSubTypeUnID ().IsOk ()) {
		ACAPI_WriteReport (GS::UniString::Printf ("Subtype MainGuid: %T", gsmObject.GetSubTypeUnID ()->GetMainGuid ().ToUniString ().ToPrintf ()),
						   false);
		ACAPI_WriteReport (GS::UniString::Printf ("Subtype RevGuid: %T", gsmObject.GetSubTypeUnID ()->GetRevGuid ().ToUniString ().ToPrintf ()),
						   false);
	}

	const GS::UnID modelElementLibPartID =
		BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetUnIDWithNullRevGuid (BL::BuiltInLibPartID::ModelElementLibPartID);
	ACAPI_WriteReport (GS::UniString::Printf ("IsInSubTypeOf Model Element: %s", *gsmObject.IsInSubTypeOf (modelElementLibPartID) ? "true" : "false"),
					   false);

	ACAPI_WriteReport (GS::UniString::Printf ("IsPlaceable: %s", *gsmObject.IsPlaceable () ? "true" : "false"), false);
	ACAPI_WriteReport (GS::UniString::Printf ("IsPasswordProtected: %s", *gsmObject.IsPasswordProtected () ? "true" : "false"), false);

	ACAPI_WriteReport (GS::UniString::Printf ("Author: %T", gsmObject.GetAuthor ()->ToPrintf ()), false);
	ACAPI_WriteReport (GS::UniString::Printf ("License: %T", gsmObject.GetLicense ()->ToPrintf ()), false);
	ACAPI_WriteReport (GS::UniString::Printf ("License Version: %T", gsmObject.GetLicenseVersion ()->ToPrintf ()), false);
	if (gsmObject.GetDescription ().IsOk ()) {
		ACAPI_WriteReport (GS::UniString::Printf ("Description: %T", gsmObject.GetDescription ()->ToPrintf ()), false);
	}
	if (gsmObject.GetKeywords ().IsOk ()) {
		ACAPI_WriteReport (GS::UniString::Printf ("Keywords: %T", gsmObject.GetKeywords ()->ToPrintf ()), false);
	}
}


void DumpGSMObjectParametersData (const ACAPI::Library::GSMObject& gsmObject)
{
	ACAPI::Result<GDL::Parameters>			  gsmObjectParameters		 = gsmObject.GetParameters ();
	ACAPI::Result<std::vector<GS::UniString>> gsmObjectParamDescriptions = gsmObject.GetParameterDescriptions ();

	if (gsmObjectParameters.IsOk () && gsmObjectParamDescriptions.IsOk ()) {
		ULong nVars = gsmObjectParameters->GetVarNumber ();
		ACAPI_WriteReport (u"Number of parameters : %d "sv, false, nVars);
		for (ULong i = 0; i < nVars; i++) {
			char varName[MAXNAMUTF8];
			gsmObjectParameters->GetVarName (i, varName, MAXNAMUTF8);

			// TODO other parameter data?
			ACAPI_WriteReport (GS::UniString::Printf ("%s (%T)", varName, (*gsmObjectParamDescriptions)[i].ToPrintf ()), false);
		};
	}
}


void DumpGSMObjectScripts (const ACAPI::Library::GSMObject& gsmObject)
{
	// TODO other scripts
	ACAPI::Result<GS::UniString> masterScript = gsmObject.GetMasterScript ();
	if (masterScript.IsOk ()) {
		ACAPI_WriteReport ("\n--- Master script ---", false);
		ACAPI_WriteReport (GS::UniString::Printf ("Master Script: %T", masterScript->ToPrintf ()), false);
	}
	ACAPI::Result<GS::UniString> parameterScript = gsmObject.GetParameterScript ();
	if (parameterScript.IsOk ()) {
		ACAPI_WriteReport ("\n--- Parameter script ---", false);
		ACAPI_WriteReport (GS::UniString::Printf ("Parameter Script: %T", parameterScript->ToPrintf ()), false);
	}
}


GSErrCode ReserveLibraryListIfNeeded (bool& wasReserved)
{
	wasReserved = false;
	if (!ACAPI_Teamwork_HasConnection ()) {
		return NoError;
	}
	if (!ACAPI_Teamwork_IsOnline ()) {
		ACAPI_WriteReport ("Sorry, the BIM Server is currently not available", true);
		return APIERR_NOTMINE;
	}

	API_Guid libListGuid = ACAPI_Teamwork_FindLockableObjectSet ("ProjectLibraryList");
	const API_LockableStatus  lockableStatus = ACAPI_Teamwork_GetLockableStatus (libListGuid);
	if (lockableStatus == APILockableStatus_Free) {
		wasReserved = true;
		return ACAPI_Teamwork_ReserveLockable (libListGuid);
	} else if (lockableStatus == APILockableStatus_Editable) {
		return NoError;
	}
	return APIERR_NOTMINE;
}


void ReleaseLibraryList () {
	ACAPI_Teamwork_ReleaseLockable (ACAPI_Teamwork_FindLockableObjectSet ("ProjectLibraryList"));
}


// -----------------------------------------------------------------------------

/// Add a local library to the library list by its location.
GSErrCode AddLocalLibrary (const IO::Location& libraryLocation)
{
	bool wasReserved = false;
	GSErrCode reservingResult = ReserveLibraryListIfNeeded (wasReserved);
	if (reservingResult != NoError) {
		return reservingResult;
	}

	ACAPI::Library::LibraryManager libraryManager = *ACAPI::Library::GetLibraryManager ();
	auto result = libraryManager.ModifyLibraryList ([&] (ACAPI::Library::LibraryManager::Modifier& modifier) -> GSErrCode {
		auto addResult = modifier.AddLibrary (libraryLocation);
		if (addResult.IsErr ()) {
			return addResult.UnwrapErr ().kind;
		}
		return NoError;
	});

	if (wasReserved) {
		ReleaseLibraryList ();
	}

	if (result.IsErr ()) {
		return result.UnwrapErr ().kind;
	}
	return NoError;
}

/// Add local libraries to the library list by their location.
GSErrCode AddLocalLibraryList (const GS::Array<IO::Location>& libraryLocations)
{
	bool wasReserved = false;
	GSErrCode reservingResult = ReserveLibraryListIfNeeded (wasReserved);
	if (reservingResult != NoError) {
		return reservingResult;
	}

	ACAPI::Library::LibraryManager libraryManager = *ACAPI::Library::GetLibraryManager ();
	auto result = libraryManager.ModifyLibraryList ([&] (ACAPI::Library::LibraryManager::Modifier& modifier) -> GSErrCode {
		for (auto libraryLocation : libraryLocations) {
			auto addResult = modifier.AddLibrary (libraryLocation);
			if (addResult.IsErr ()) {
				return addResult.UnwrapErr ().kind;
			}
		}
		return NoError;
	});

	if (wasReserved) {
		ReleaseLibraryList ();
	}

	if (result.IsErr ()) {
		return result.UnwrapErr ().kind;
	}
	return NoError;
}

/// Add a server library to the library list by its server address and path.
GSErrCode AddServerLibrary (const GS::UniString& serverAddress, const GS::UniString& libraryPath)
{
	bool	  wasReserved	  = false;
	GSErrCode reservingResult = ReserveLibraryListIfNeeded (wasReserved);
	if (reservingResult != NoError) {
		return reservingResult;
	}

	ACAPI::Library::LibraryManager libraryManager = *ACAPI::Library::GetLibraryManager ();
	auto result = libraryManager.ModifyLibraryList ([&] (ACAPI::Library::LibraryManager::Modifier& modifier) -> GSErrCode {
		auto addResult = modifier.AddLibrary (serverAddress, libraryPath);
		if (addResult.IsErr ()) {
			return addResult.UnwrapErr ().kind;
		}
		return NoError;
	});

	if (wasReserved) {
		ReleaseLibraryList ();
	}

	if (result.IsErr ()) {
		return result.UnwrapErr ().kind;
	}
	return NoError;
}

/// Remove all loaded libraries from the library list. The built-in and embedded libraries will be not removed.
GSErrCode RemoveAllLibraries ()
{
	bool	  wasReserved	  = false;
	GSErrCode reservingResult = ReserveLibraryListIfNeeded (wasReserved);
	if (reservingResult != NoError) {
		return reservingResult;
	}

	ACAPI::Library::LibraryManager libraryManager = *ACAPI::Library::GetLibraryManager ();
	auto result = libraryManager.ModifyLibraryList ([&] (ACAPI::Library::LibraryManager::Modifier& modifier) -> GSErrCode {
		auto librariesToRemove = libraryManager.FindLibraries (ACAPI::Library::SelectLoadedLibraries);
		for (const auto& library : librariesToRemove) {
			modifier.RemoveLibrary (library);
		}
		return NoError;
	});

	if (wasReserved) {
		ReleaseLibraryList ();
	}
	
	if (result.IsErr ()) {
		return result.UnwrapErr ().kind;
	}
	return NoError;
}

/// Writes location and server information of the libraries to the report window.
GSErrCode DumpLibraryList ()
{
	ACAPI::Library::LibraryManager libraryManager = *ACAPI::Library::GetLibraryManager ();
	auto libraries		 = libraryManager.FindLibraries (ACAPI::Library::SelectAllLibraries);
	for (const auto& library : libraries) {
		if (!*library.IsBuiltIn ()) {
			IO::Location  libLocation = *library.GetLocation ();
			GS::UniString fullPath;
			libLocation.ToPath (&fullPath);

			if (*library.IsLocal () || *library.IsEmbedded ()) {
				ACAPI_WriteReport (GS::UniString::Printf ("%T (%T)\t\t(%T)", (*library.GetDisplayName ()).ToPrintf (),(*library.GetName ()).ToPrintf (), fullPath.ToPrintf ()), false);
			}
			if (*library.IsServer ()) {
				ACAPI_WriteReport (GS::UniString::Printf ("%T (%T)\t\t(%T, %T, %T)",
														  (*library.GetDisplayName ()).ToPrintf (),
														  (*library.GetName ()).ToPrintf (),
														  fullPath.ToPrintf (),
														  (*library.GetTWServerUrl ()).ToPrintf (),
														  (*library.GetTWServerPath ()).ToPrintf ()),
								   false);
			}
		}
	}
	return NoError;
}


/// Writes general data of the libparts to the report window.
GSErrCode DumpAllLibParts ()
{
	ACAPI::Library::LibraryManager libraryManager = *ACAPI::Library::GetLibraryManager ();
	auto libparts = libraryManager.FindLibParts (ACAPI::Library::SelectAllLibParts);

	for (const auto& libPart : libparts) {
		ACAPI::Result<ACAPI::Library::Library> library = libraryManager.GetLibraryOfLibPart (libPart);
		if (library.IsOk () && !*library->IsBuiltIn ()) {
			DumpLibPartGeneralData (libraryManager, libPart);
			ACAPI::Result<ACAPI::Library::GSMObject> gsmObject = libraryManager.GetGSMObjectOfLibPart (libPart);
			if (gsmObject.IsOk ()) {
				DumpGSMObjectGeneralData (*gsmObject);
				DumpGSMObjectParametersData (*gsmObject);
				DumpGSMObjectScripts (*gsmObject);
				ACAPI_WriteReport ("-------------------------------------------------------\n", false);
			}
		}
	}

	return NoError;
}	

/// Makes a copy of the selected objects to the embedded library.
GSErrCode DuplicateSelectionToEmbeddedLibrary ()
{
	GS::Array<API_Neig> selNeigs;
	API_SelectionInfo	selectionInfo;
	GSErrCode			err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false);
	if (err != NoError || selNeigs.IsEmpty ()) return NoError;

	GS::Array<IO::Location> newFileLocations;
	ACAPI::Library::LibraryManager libraryManager = *ACAPI::Library::GetLibraryManager ();
	IO::Location embeddedLocation = *libraryManager.GetEmbeddedLibrary ()->GetLocation ();

	API_SpecFolderID			   specID			= API_TemporaryFolderID;
	IO::Location				   tempFolder;
	ACAPI_ProjectSettings_GetSpecFolder (&specID, &tempFolder);
	tempFolder.AppendToLocal (IO::Name (u"Temp_LibraryManagement_Test"sv));
	IO::fileSystem.CreateFolder (tempFolder);

	for (const API_Neig& neig : selNeigs) {
		API_Element elem = {};
		elem.header.guid = neig.guid;
		if (ACAPI_Element_Get (&elem) != NoError) return NoError;

		if (elem.header.type.typeID == API_ObjectID || elem.header.type.typeID == API_WindowID || elem.header.type.typeID == API_DoorID ||
			elem.header.type.typeID == API_LampID || elem.header.type.typeID == API_LabelID)
		{
			ACAPI::Result<ACAPI::Library::LibPart>	libPart	   = libraryManager.GetLibPartByLibInd (elem.object.libInd);
			ACAPI::Result<ACAPI::Library::GSMObject> gsmObject  = libraryManager.LoadGSMObjectFromFile (*libPart->GetLocation ());
			IO::Location newFileLoc (tempFolder);
			newFileLoc.AppendToLocal (IO::Name (*libPart->GetName ()));
			gsmObject->CopyTo (newFileLoc /*, KeepMainGuid */);
			newFileLocations.Push (newFileLoc);
		}
	}
	bool overwrite = true;
	ACAPI_LibraryManagement_CopyFilesIntoLibrary (&embeddedLocation, &newFileLocations, &overwrite);
	IO::fileSystem.Delete (tempFolder);
	return NoError;
}


#define RESULT_IF(expr) if (auto result = expr; result.IsOk ())

/// Creates new objects from the selected objects in the embedded library.
GSErrCode CreateNewObjectFromSelection ()
{
	GS::Array<API_Neig> selNeigs;
	API_SelectionInfo	selectionInfo;
	GSErrCode			err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, false, false);
	if (err != NoError || selNeigs.IsEmpty ()) return NoError;

	GS::Array<IO::Location> newFileLocations;
	ACAPI::Library::LibraryManager libraryManager = *ACAPI::Library::GetLibraryManager ();
	IO::Location				   embeddedLocation = *libraryManager.GetEmbeddedLibrary ()->GetLocation ();

	API_SpecFolderID specID = API_TemporaryFolderID;
	IO::Location tempFolder;
	ACAPI_ProjectSettings_GetSpecFolder (&specID, &tempFolder);
	tempFolder.AppendToLocal (IO::Name (u"Temp_LibraryManagement_Test"sv));
	IO::fileSystem.CreateFolder (tempFolder);

	for (const API_Neig& neig : selNeigs) {
		API_Element elem = {};
		elem.header.guid = neig.guid;
		if (ACAPI_Element_Get (&elem) != NoError) return NoError;

		if (elem.header.type.typeID == API_ObjectID || elem.header.type.typeID == API_WindowID || elem.header.type.typeID == API_DoorID ||
			elem.header.type.typeID == API_LampID || elem.header.type.typeID == API_LabelID)
		{

			ACAPI::Result<ACAPI::Library::LibPart>	libPart		   = libraryManager.GetLibPartByLibInd (elem.object.libInd);
			ACAPI::Result<ACAPI::Library::GSMObject> selectedObject = libraryManager.LoadGSMObjectFromFile (*libPart->GetLocation ());

			IO::Location newFileLoc (tempFolder);
			newFileLoc.AppendToLocal (IO::Name (*libPart->GetName ()));

			GS::Guid						subTypeMainGuid = (*selectedObject->GetSubTypeUnID ()).GetMainGuid ();
			std::vector<ACAPI::Library::LibPart> subTypeLibpart	= libraryManager.FindLibParts ([&] (const ACAPI::Library::LibPart& libPart) {
				 ACAPI::Result<ACAPI::Library::GSMObject> gsmObject = libraryManager.GetGSMObjectOfLibPart (libPart);
				 if (gsmObject.IsOk () && (*gsmObject->GetUnID ()).GetMainGuid () == subTypeMainGuid) {
					 return true;
				 }
				 return false;
			 });
			if (subTypeLibpart.size () > 0) {
				ACAPI::Result<ACAPI::Library::GSMObject> subTypeGsmObject = libraryManager.GetGSMObjectOfLibPart (subTypeLibpart[0]);
				ACAPI::Library::GSMObject				createdObject	 = *ACAPI::Library::GSMObject::CreateNew (newFileLoc, *subTypeGsmObject);
				newFileLocations.Push (newFileLoc);
				ACAPI::Result<void> result = createdObject.Modify ([&] (ACAPI::Library::GSMObject::Modifier& modifier) -> GSErrCode {
					modifier.SetPlaceable (*selectedObject->IsPlaceable ());
					modifier.SetIsSubtype (*selectedObject->IsSubType ());
					modifier.SetAuthor (*selectedObject->GetAuthor ());
					modifier.SetLicense (*selectedObject->GetLicense ());
					modifier.SetLicenseVersion (*selectedObject->GetLicenseVersion ());
					RESULT_IF (selectedObject->GetDescription ())
					{
						modifier.SetDescription (*result);
					}
					RESULT_IF (selectedObject->GetKeywords ())
					{
						modifier.SetKeywords (*result);
					}
					modifier.SetParameters (*selectedObject->GetParameters (), *selectedObject->GetParameterDescriptions ());
					RESULT_IF (selectedObject->GetMasterScript ())
					{
						modifier.SetMasterScript (*result);
					}
					RESULT_IF (selectedObject->Get2DScript ())
					{
						modifier.Set2DScript (*result);
					}
					RESULT_IF (selectedObject->Get3DScript ())
					{
						modifier.Set3DScript (*result);
					}
					RESULT_IF (selectedObject->GetPropertiesScript ())
					{
						modifier.SetPropertiesScript (*result);
					}
					RESULT_IF (selectedObject->GetParameterScript ())
					{
						modifier.SetParameterScript (*result);
					}
					RESULT_IF (selectedObject->GetInterfaceScript ())
					{
						modifier.SetInterfaceScript (*result);
					}
					RESULT_IF (selectedObject->GetForwardMigrationScript ())
					{
						modifier.SetForwardMigrationScript (*result);
					}
					RESULT_IF (selectedObject->GetBackwardMigrationScript ())
					{
						modifier.SetBackwardMigrationScript (*result);
					}
					RESULT_IF (selectedObject->Get2DVectorImage ())
					{
						modifier.Set2DVectorImage (*result);
					}
					RESULT_IF (selectedObject->GetPreviewPicture ())
					{
						modifier.SetPreviewPicture (*result);
					}
					RESULT_IF (selectedObject->GetGDLPictures ())
					{
						modifier.SetGDLPictures (*result);
					}
					modifier.SetUIDefault (false);
					modifier.SetHierarchicalPagesUsed (false);
					return NoError;
				});
			}
		}
	}
	bool overwrite = true;
	ACAPI_LibraryManagement_CopyFilesIntoLibrary (&embeddedLocation, &newFileLocations, &overwrite);
	return NoError;
}

#undef RESULT_IF


GSErrCode UploadLibrariesToServer (const GS::Array<IO::Location>& libraryLocationList, const GS::UniString& serverAddress, const GS::UniString& libraryPath)
{
	GS::Array<GSErrCode> resultsByLocations;
	GSErrCode err = ACAPI_Teamwork_UploadLibraries (libraryLocationList, serverAddress, libraryPath, resultsByLocations);

	if (err == APIERR_NOACCESSRIGHT) {
		ACAPI_WriteReport ("Sorry, you have no right to upload libraries in this folder", false);
	} else {
		for (UIndex i = 0; i < resultsByLocations.GetSize (); i++) {
			if (resultsByLocations[i] == NoError) {
				ACAPI_WriteReport (GS::UniString::Printf ("Library %s uploaded successfully", libraryLocationList[i].ToDisplayText ().ToCStr ().Get ()), false);
			} else if (resultsByLocations[i] == APIERR_NAMEALREADYUSED) {
				ACAPI_WriteReport (GS::UniString::Printf ("Library %s already exists on the server", libraryLocationList[i].ToDisplayText ().ToCStr ().Get ()), false);
			} else if (resultsByLocations[i] == APIERR_NOACCESSRIGHT) {
				ACAPI_WriteReport (GS::UniString::Printf ("Sorry, you have no right to upload library %s", libraryLocationList[i].ToDisplayText ().ToCStr ().Get ()), false);
			} else if (resultsByLocations[i] == APIERR_CANCEL) {
				ACAPI_WriteReport (GS::UniString::Printf ("Uploading library %s was canceled", libraryLocationList[i].ToDisplayText ().ToCStr ().Get ()), false);
			} else {
				ACAPI_WriteReport (GS::UniString::Printf ("Error uploading library %s", libraryLocationList[i].ToDisplayText ().ToCStr ().Get ()), false);
			}
		}
	}
	return err;
}


GSErrCode RemoveLibrariesFromServer (const GS::Array<GS::UniString>& libraryNames, const GS::UniString& twServerURL, const GS::UniString& parentPath)
{
	GS::Array<GSErrCode> resultByNames;
	GSErrCode			 err = ACAPI_Teamwork_RemoveLibraries (libraryNames, twServerURL, parentPath, resultByNames);

	if (err == APIERR_NOACCESSRIGHT) {
		ACAPI_WriteReport ("Sorry, you have no right to remove libraries from this folder", false);
	} else {
		for (UIndex i = 0; i < resultByNames.GetSize (); i++) {
			if (resultByNames[i] == NoError) {
				ACAPI_WriteReport (GS::UniString::Printf ("Library %s removed successfully", libraryNames[i].ToCStr ().Get ()), false);
			} else if (resultByNames[i] == APIERR_BADNAME) {
				ACAPI_WriteReport (GS::UniString::Printf ("Library %s not found on the server", libraryNames[i].ToCStr ().Get ()), false);
			} else if (resultByNames[i] == APIERR_NOACCESSRIGHT) {
				ACAPI_WriteReport (GS::UniString::Printf ("Sorry, you have no right to remove library %s", libraryNames[i].ToCStr ().Get ()), false);
			} else {
				ACAPI_WriteReport (GS::UniString::Printf ("Error removing library %s", libraryNames[i].ToCStr ().Get ()), false);
			}
		}
	}
	return err;
}