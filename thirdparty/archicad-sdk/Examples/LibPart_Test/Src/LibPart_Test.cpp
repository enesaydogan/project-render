// *****************************************************************************
// Source code for the LibPart Test Add-On
// *****************************************************************************

// =============================================================================
//
// API Includes
//
// =============================================================================

#include	"BuiltInLibrary.hpp"
#include	"GSUnID.hpp"

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h

#include	"Folder.hpp"
#include	"File.hpp"
#include	"FileSystem.hpp"
#include	"StringConversion.hpp"

#include	"GSPix.h"
#include	"Graphics2D.h"
#include	"GXImage.hpp"
#include	"DGModule.hpp"
#include	"Model3D/Model3D.hpp"

#include	"FileTypeManager.hpp"
#include	"Model.hpp"

#include	"APICommon.h"

// -----------------------------------------------------------------------------
//  Retrieves a valid location for creating library part
// -----------------------------------------------------------------------------

static GSErrCode	GetLocation (IO::Location*& loc, bool useEmbeddedLibrary)
{
	GS::Array<API_LibraryInfo>	libInfo;
	loc = nullptr;

	GSErrCode err = NoError;

	if (useEmbeddedLibrary) {
		Int32 embeddedLibraryIndex = -1;
		// get embedded library location
		if (ACAPI_LibraryManagement_GetLibraries (&libInfo, &embeddedLibraryIndex) == NoError && embeddedLibraryIndex >= 0) {
			loc = new IO::Location (libInfo[embeddedLibraryIndex].location);

			if (loc != nullptr) {
				IO::Location ownFolderLoc (*loc);
				ownFolderLoc.AppendToLocal (IO::Name ("LibPart_Test Library"));
				err = IO::fileSystem.CreateFolder (ownFolderLoc);
				if (err == NoError || err == IO::FileSystem::TargetExists)
					loc->AppendToLocal (IO::Name ("LibPart_Test Library"));
			}
		}
	} else {
		// register our own folder and create the library part in it
		if (ACAPI_LibraryManagement_GetLibraries (&libInfo) == NoError) {
			IO::Location folderLoc;
			API_SpecFolderID specID = API_UserDocumentsFolderID;
			ACAPI_ProjectSettings_GetSpecFolder (&specID, &folderLoc);
			folderLoc.AppendToLocal (IO::Name ("LibPart_Test Library"));
			IO::Folder destFolder (folderLoc, IO::Folder::Create);
			if (destFolder.GetStatus () != NoError || !destFolder.IsWriteable ())
				return APIERR_GENERAL;

			loc = new IO::Location (folderLoc);

			for (UInt32 ii = 0; ii < libInfo.GetSize (); ii++) {
				if (folderLoc == libInfo[ii].location)
					return NoError;
			}

			API_LibraryInfo		li;
			li.location = folderLoc;

			libInfo.Push (li);

			ACAPI_LibraryManagement_SetLibraries (&libInfo);
		}
	}

	return NoError;
}

// =============================================================================
//
// Library Part functions
//
// =============================================================================

// -----------------------------------------------------------------------------
//  Set LibPart preview picture
//   Allowed to call between the ACAPI_LibraryPart_Create and ACAPI_LibraryPart_Save functions only
// -----------------------------------------------------------------------------

void SetPreviewPictureToLibPart (const IO::Location& picFileLoc)
{
	char**	bufHdl = nullptr;

	try {
		USize	size;
		char	mimeStr[16];

		IO::File picFile (picFileLoc);
		if (picFile.GetStatus () != NoError)
			throw GS::GeneralException (); // can't read given picture

		// read picture data from file to buf:
		if (picFile.GetDataLength (&size) != NoError)
			throw GS::GeneralException ();

		sprintf (mimeStr, "image/gif"); // prefer gif, but you can change gif to jpeg or png
		bufHdl = (char**) BMAllocateHandle (size + Strlen32 (mimeStr) + 1, 0, 0);
		if (bufHdl == nullptr)
			throw GS::GeneralException ();

		// insert mime type string first, and then the picture
		CHCopyC (mimeStr, *bufHdl);
		if (picFile.Open (IO::File::ReadMode) != NoError)
			throw GS::GeneralException ();
		if (picFile.ReadBin ((*bufHdl) + (strlen (mimeStr) + 1), size) != NoError)
			throw GS::GeneralException ();
		picFile.Close ();

		// add preview picture to libpart:
		API_LibPartSection section;
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectInfoGIF;
		if (ACAPI_LibraryPart_AddSection (&section, bufHdl, nullptr))
			throw GS::GeneralException ();

		BMKillHandle ((GSHandle *) &bufHdl);
	} catch (...) {
		if (bufHdl != nullptr)
			BMKillHandle ((GSHandle *) &bufHdl);
		ACAPI_WriteReport ("Error while attaching preview picture!", true);
	}
}

// -----------------------------------------------------------------------------
//  Sets the given LibPart as the default object
// -----------------------------------------------------------------------------

GSErrCode	SetLibPartAsDefaultObject (const API_LibPart& libPart)
{
	GSErrCode			err = NoError;
	API_Element			element;
	API_Element			mask;
	API_ElementMemo		memo;

	element = {};
	memo = {};

	element.header.type = API_ObjectID;

	API_ParamOwnerType paramOwner;
	BNZeroMemory (&paramOwner, sizeof (API_ParamOwnerType));
	paramOwner.libInd = libPart.index;
	err = ACAPI_LibraryPart_OpenParameters (&paramOwner);
	if (err == NoError) {
		API_GetParamsType getParams;
		BNZeroMemory (&getParams, sizeof (API_GetParamsType));
		err = ACAPI_LibraryPart_GetActParameters (&getParams);
		if (err == NoError) {
			ACAPI_DisposeAddParHdl (&memo.params);
			memo.params = getParams.params;
		}
		ACAPI_LibraryPart_CloseParameters ();
	}

	element.object.libInd = libPart.index;
	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_ObjectType, libInd);

	err = ACAPI_Element_ChangeDefaults (&element, &memo, &mask);
	if (err != NoError)
		WriteReport ("ACAPI_Element_ChangeDefaults failed");

	ACAPI_DisposeElemMemoHdls (&memo);

	return err;
}		// SetLibPartAsDefaultObject

// -----------------------------------------------------------------------------
//  Creates a simple Library Part
// -----------------------------------------------------------------------------

static void		Do_CreateLibraryPart (void)
{
	GSErrCode err = NoError;

	API_LibPart libPart;
	libPart = {};
	libPart.typeID = APILib_ObjectID;
	libPart.isTemplate = false;
	libPart.isPlaceable = true;

	const GS::UnID unID = BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetUnIDWithNullRevGuid (BL::BuiltInLibPartID::ModelElementLibPartID);

	CHCopyC (unID.ToUniString ().ToCStr (), libPart.parentUnID);	// Model Element subtype

	GSTimeRecord timeRecord;
	TIGetTimeRecord (0, &timeRecord, TI_CURRENT_TIME);
	UInt32 fraction = TIGetTicks () % TIGetTicksPerSec ();
	GS::snuprintf (libPart.docu_UName, sizeof (libPart.docu_UName) / sizeof (GS::uchar_t), L("LPTest_%d-%02d-%02d_%02d%02d%02d_%d"),
					timeRecord.year, timeRecord.month, timeRecord.day, timeRecord.hour, timeRecord.minute, timeRecord.second, fraction);

	err = GetLocation (libPart.location, true);
	if (err != NoError) {
		WriteReport ("Library Part creation failed");
		return;
	}

	ACAPI_LibraryManagement_OverwriteLibPart ((Int32) true);
	err = ACAPI_LibraryPart_Create (&libPart);
	ACAPI_LibraryManagement_OverwriteLibPart ((Int32) false);

	if (err == NoError) {
		char buffer[1000];

		API_LibPartSection section;

		// Comment script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectComText;
		ACAPI_LibraryPart_NewSection (&section);
		sprintf (buffer, "Library Part written by LibPart_Test add-on");
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// Keyword section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectKeywords;
		ACAPI_LibraryPart_NewSection (&section);
		sprintf (buffer, "API LibPart_Test");
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// Copyright section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectCopyright;
		ACAPI_LibraryPart_NewSection (&section);
		sprintf (buffer, "LibPart_Test");	// Author
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// Master script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_Sect1DScript;
		ACAPI_LibraryPart_NewSection (&section);
		buffer[0] = '\0';
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// 3D script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_Sect3DScript;
		ACAPI_LibraryPart_NewSection (&section);
		sprintf (buffer, "MATERIAL mat%s%s", GS::EOL, GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "BLOCK a, b, 1%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "ADD a * 0.5, b* 0.5, 1%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "CYLIND zzyzx - 3, MIN (a, b) * 0.5%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "ADDZ zzyzx - 3%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "CONE 2, MIN (a, b) * 0.5, 0.0, 90, 90%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// 2D script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_Sect2DScript;
		ACAPI_LibraryPart_NewSection (&section);
		sprintf (buffer, "PROJECT2 3, 270, 2%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// Parameter script section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectVLScript;
		ACAPI_LibraryPart_NewSection (&section);
		sprintf (buffer, "VALUES \"zzyzx\" RANGE [6,]%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// Parameters section
		BNZeroMemory (&section, sizeof (API_LibPartSection));
		section.sectType = API_SectParamDef;

		short nPars = 4;
		API_AddParType** addPars = reinterpret_cast<API_AddParType**>(BMAllocateHandle (nPars * sizeof (API_AddParType), ALLOCATE_CLEAR, 0));
		API_AddParType* pAddPar = &(*addPars)[0];
		pAddPar->typeID = APIParT_Mater;
		pAddPar->typeMod = 0;
		CHTruncate ("mat", pAddPar->name, sizeof (pAddPar->name));
		GS::ucscpy (pAddPar->uDescname, L("Material"));
		pAddPar->value.real = 1;

		pAddPar = &(*addPars)[1];
		pAddPar->typeID = APIParT_Length;
		pAddPar->typeMod = 0;
		CHTruncate ("len", pAddPar->name, sizeof (pAddPar->name));
		GS::ucscpy (pAddPar->uDescname, L("Length"));
		pAddPar->value.real = 2.5;

		pAddPar = &(*addPars)[2];
		pAddPar->typeID = APIParT_CString;
		pAddPar->typeMod = 0;
		CHTruncate ("myStr", pAddPar->name, sizeof (pAddPar->name));
		GS::ucscpy (pAddPar->uDescname, L("String parameter"));
		GS::ucscpy (pAddPar->value.uStr, L("this is a string"));

		pAddPar = &(*addPars)[3];
		pAddPar->typeID = APIParT_RealNum;
		pAddPar->typeMod = API_ParArray;
		pAddPar->dim1 = 3;
		pAddPar->dim2 = 4;
		CHTruncate ("matrix", pAddPar->name, sizeof (pAddPar->name));
		GS::ucscpy (pAddPar->uDescname, L("Array parameter with real numbers"));
		pAddPar->value.array = BMAllocateHandle (pAddPar->dim1 * pAddPar->dim2 * sizeof (double), ALLOCATE_CLEAR, 0);
		double** arrHdl = reinterpret_cast<double**>(pAddPar->value.array);
		for (Int32 k = 0; k < pAddPar->dim1; k++)
			for (Int32 j = 0; j < pAddPar->dim2; j++)
				(*arrHdl)[k * pAddPar->dim2 + j] = (k == j ? 1.1 : 0.0);

		double aa = 1.0;
		double bb = 1.0;
		GSHandle sectionHdl = nullptr;
		ACAPI_LibraryPart_GetSect_ParamDef (&libPart, addPars, &aa, &bb, nullptr, &sectionHdl);

		API_LibPartDetails details;
		BNZeroMemory (&details, sizeof (API_LibPartDetails));
		details.object.autoHotspot = false;
		ACAPI_LibraryPart_SetDetails_ParamDef (&libPart, sectionHdl, &details);

		ACAPI_LibraryPart_AddSection (&section, sectionHdl, nullptr);

		BMKillHandle (reinterpret_cast<GSHandle*>(&arrHdl));
		BMKillHandle (reinterpret_cast<GSHandle*>(&addPars));
		BMKillHandle (&sectionHdl);

		IO::Location    	folderLoc;
		API_SpecFolderID	specID = API_UserDocumentsFolderID;
		ACAPI_ProjectSettings_GetSpecFolder (&specID, &folderLoc);
		SetPreviewPictureToLibPart (IO::Location (folderLoc, (IO::Name) "mygif.gif"));

		if (libPart.location != nullptr) {
			delete libPart.location;
			libPart.location = nullptr;
		}

		// Save the constructed library part
		if (err == NoError)
			err = ACAPI_LibraryPart_Save (&libPart);

		if (libPart.location != nullptr) {
			delete libPart.location;
			libPart.location = nullptr;
		}
	}

	if (err == NoError) {
		WriteReport ("Library Part \"%s\" created", static_cast<const char*> (GS::UniString (libPart.docu_UName).ToCStr ()));

		err = SetLibPartAsDefaultObject (libPart);
		if (err == NoError)
			WriteReport ("Default object changed successfully");
	} else
		WriteReport ("Library Part creation failed");

	return;
}		// Do_CreateLibraryPart

// -----------------------------------------------------------------------------
//  Updates the previously created Window Library Part
// -----------------------------------------------------------------------------

static void		Do_UpdateLibrarySection (void)
{
	GSErrCode err = NoError;

	API_LibPart libPart;
	libPart = {};
	libPart.typeID = APILib_WindowID;

	const GS::UnID unID = BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetUnIDWithNullRevGuid (BL::BuiltInLibPartID::WindowWallLibPartID);

	CHCopyC (unID.ToUniString ().ToCStr (), libPart.parentUnID);	// Window (Wall) subtype

	GS::snuprintf (libPart.docu_UName, sizeof (libPart.docu_UName) / sizeof (GS::uchar_t), L("LPTest_Window_0001"));
	err = ACAPI_LibraryPart_Search (&libPart, false);
	if (libPart.location != nullptr) {
		delete libPart.location;
		libPart.location = nullptr;
	}
	if (err != NoError)
		return;

	API_LibPartSection section;
	GS::UniString sectionStr;

	// Update comment script section
	BNZeroMemory (&section, sizeof (API_LibPartSection));
	section.sectType = API_SectComText;
	err = ACAPI_LibraryPart_GetSection (libPart.index, &section, nullptr, &sectionStr);

	sectionStr += " (updated)";
	err = ACAPI_LibraryPart_UpdateSection (libPart.index, &section, nullptr, &sectionStr);
	DBASSERT (err == NoError);

	// Copyright section cannot be retrieved or updated
	BNZeroMemory (&section, sizeof (API_LibPartSection));
	section.sectType = API_SectCopyright;
	err = ACAPI_LibraryPart_GetSection (libPart.index, &section, nullptr, &sectionStr);
	DBASSERT (err != NoError);

	sectionStr = "LibPart_Test";	// Author
	err = ACAPI_LibraryPart_UpdateSection (libPart.index, &section, nullptr, &sectionStr);
	DBASSERT (err != NoError);

	return;
}		// Do_UpdateLibrarySection


// -----------------------------------------------------------------------------
//  Interprets a 3D GDL script
// -----------------------------------------------------------------------------
enum class InterpretResultType
{
	ModelerAPIModel,
	ModelerModel3D
};

static void		Do_Interpret3DScript (InterpretResultType resultType)
{
	GSSize parameterCount = 1;
	API_AddParType** addPars = reinterpret_cast<API_AddParType**> (BMAllocateHandle ((GSSize) parameterCount * sizeof (API_AddParType), ALLOCATE_CLEAR, 0));

	API_AddParType* addPar = &(*addPars)[0];
	addPar->typeID = APIParT_RealNum;
	addPar->typeMod = 0;
	addPar->value.real = 5.0;
	CHTruncate ("example", addPar->name, sizeof (addPar->name));

	GS::UniString script = "BLOCK example, GLOB_SCALE, 3";
	Box3D bounds;
	if (resultType == InterpretResultType::ModelerAPIModel) {
		ModelerAPI::Model model;
		GSErrCode err = ACAPI_LibraryManagement_InterpretGDLScript3D (&script, addPars, nullptr, &model);
		if (err == NoError) {
			bounds = model.GetBounds ();
		}
	} else if (resultType == InterpretResultType::ModelerModel3D) {
		Modeler::ConstModel3DPtr model;
		GSErrCode err = ACAPI_LibraryManagement_InterpretGDLScript3D (&script, addPars, nullptr, &model);
		if (err == NoError) {
			bounds = model->GetBounds ();
		}
	}
	
	GS::UniString resultMessage = "Result model dimensions: " +
		GS::ValueToString (bounds.GetMaxX () - bounds.GetMinX ()) + ", " +
		GS::ValueToString (bounds.GetMaxY () - bounds.GetMinY ()) + ", " +
		GS::ValueToString (bounds.GetMaxZ () - bounds.GetMinZ ());
	DG::InformationAlert (resultMessage, GS::UniString (), "OK");

	BMKillHandle (reinterpret_cast<GSHandle*> (&addPars));
	return;
}		// Do_Interpret3DScript


static void		Do_Interpret3DScriptBRICK ()
{
	GSSize parameterCount = 1;
	API_AddParType** addPars = reinterpret_cast<API_AddParType**> (BMAllocateHandle ((GSSize) parameterCount * sizeof (API_AddParType), ALLOCATE_CLEAR, 0));

	API_AddParType* addPar = &(*addPars)[0];
	addPar->typeID = APIParT_RealNum;
	addPar->typeMod = 0;
	addPar->value.real = 8.0;
	CHTruncate ("example", addPar->name, sizeof (addPar->name));

	GS::UniString script = "BRICK example, GLOB_SCALE, 3";
	Box3D apiModelBoundingBox, model3DBoundingBox;
	{
		ModelerAPI::Model model;
		GSErrCode err = ACAPI_LibraryManagement_InterpretGDLScript3D (&script, addPars, nullptr, &model);
		if (err == NoError) {
			apiModelBoundingBox = model.GetBounds ();
		}
	} 
	{
		Modeler::ConstModel3DPtr model;
		GSErrCode err = ACAPI_LibraryManagement_InterpretGDLScript3D (&script, addPars, nullptr, &model);
		if (err == NoError) {
			model3DBoundingBox = model->GetBounds ();
		}
	}

	GS::UniString resultMessage = "Result APIModelBoundingBox dimensions: " +
								  GS::ValueToString (apiModelBoundingBox.GetMaxX () - apiModelBoundingBox.GetMinX ()) + ", " +
								  GS::ValueToString (apiModelBoundingBox.GetMaxY () - apiModelBoundingBox.GetMinY ()) + ", " +
								  GS::ValueToString (apiModelBoundingBox.GetMaxZ () - apiModelBoundingBox.GetMinZ ()) + " " +
								  " Model3DBoundingBox dimensions: " +
								  GS::ValueToString (model3DBoundingBox.GetMaxX () - model3DBoundingBox.GetMinX ()) + ", " +
								  GS::ValueToString (model3DBoundingBox.GetMaxY () - model3DBoundingBox.GetMinY ()) + ", " +
								  GS::ValueToString (model3DBoundingBox.GetMaxZ () - model3DBoundingBox.GetMinZ ()) +
								  ", equal " + GS::ValueToString(apiModelBoundingBox.IsEqual(model3DBoundingBox));
	DG::InformationAlert (resultMessage, GS::UniString (), "OK");
	BMKillHandle (reinterpret_cast<GSHandle*> (&addPars));
	return;
}


static void Do_Interpret3DScriptCONE ()
{
	GS::UniString script = "CONE 2, 0.5, 0.0, 90, 90";
	Box3D		  apiModelBoundingBox, model3DBoundingBox;
	{
		ModelerAPI::Model model;
		GSErrCode		  err = ACAPI_LibraryManagement_InterpretGDLScript3D (&script, nullptr, nullptr, &model);
		if (err == NoError) {
			apiModelBoundingBox = model.GetBounds ();
		}
	}
	{
		Modeler::ConstModel3DPtr model;
		GSErrCode				 err = ACAPI_LibraryManagement_InterpretGDLScript3D (&script, nullptr, nullptr, &model);
		if (err == NoError) {
			model3DBoundingBox = model->GetBounds ();
		}
	}

	GS::UniString resultMessage = "Result APIModelBoundingBox dimensions: " +
								  GS::ValueToString (apiModelBoundingBox.GetMaxX () - apiModelBoundingBox.GetMinX ()) + ", " +
								  GS::ValueToString (apiModelBoundingBox.GetMaxY () - apiModelBoundingBox.GetMinY ()) + ", " +
								  GS::ValueToString (apiModelBoundingBox.GetMaxZ () - apiModelBoundingBox.GetMinZ ()) + " " +
								  " Model3DBoundingBox dimensions: " +
								  GS::ValueToString (model3DBoundingBox.GetMaxX () - model3DBoundingBox.GetMinX ()) + ", " +
								  GS::ValueToString (model3DBoundingBox.GetMaxY () - model3DBoundingBox.GetMinY ()) + ", " +
								  GS::ValueToString (model3DBoundingBox.GetMaxZ () - model3DBoundingBox.GetMinZ ()) + 
								  ", equal " + GS::ValueToString (apiModelBoundingBox.IsEqual (model3DBoundingBox));
	DG::InformationAlert (resultMessage, GS::UniString (), "OK");

	return;
}


static void Do_Interpret3DScriptCYLIND () 
{
	GS::UniString script = "CYLIND 3, 0.5";
	Box3D		  apiModelBoundingBox, model3DBoundingBox;
	{
		ModelerAPI::Model model;
		GSErrCode		  err = ACAPI_LibraryManagement_InterpretGDLScript3D (&script, nullptr, nullptr, &model);
		if (err == NoError) {
			apiModelBoundingBox = model.GetBounds ();
		}
	}
	{
		Modeler::ConstModel3DPtr model;
		GSErrCode				 err = ACAPI_LibraryManagement_InterpretGDLScript3D (&script, nullptr, nullptr, &model);
		if (err == NoError) {
			model3DBoundingBox = model->GetBounds ();
		}
	}

	GS::UniString resultMessage = "Result APIModelBoundingBox dimensions: " +
								  GS::ValueToString (apiModelBoundingBox.GetMaxX () - apiModelBoundingBox.GetMinX ()) + ", " +
								  GS::ValueToString (apiModelBoundingBox.GetMaxY () - apiModelBoundingBox.GetMinY ()) + ", " +
								  GS::ValueToString (apiModelBoundingBox.GetMaxZ () - apiModelBoundingBox.GetMinZ ()) + " " +
								  " Model3DBoundingBox dimensions: " +
								  GS::ValueToString (model3DBoundingBox.GetMaxX () - model3DBoundingBox.GetMinX ()) +", " + 
								  GS::ValueToString (model3DBoundingBox.GetMaxY () - model3DBoundingBox.GetMinY ()) + ", " +
								  GS::ValueToString (model3DBoundingBox.GetMaxZ () - model3DBoundingBox.GetMinZ ()) +
								  ", equal " + GS::ValueToString (apiModelBoundingBox.IsEqual (model3DBoundingBox));
	DG::InformationAlert (resultMessage, GS::UniString (), "OK");

	return;
}

// -----------------------------------------------------------------------------
//  Get polygon from the currently selected element
// -----------------------------------------------------------------------------

static GSErrCode	GetSelectedElementPolygon (API_Polygon* poly, API_ElementMemo* memo)
{
	GSErrCode			err = NoError;
	API_SelectionInfo 	selectionInfo;
	GS::Array<API_Neig>	selNeigs;
	API_ElemType		type;

	BNZeroMemory (&selectionInfo, sizeof (API_SelectionInfo));
	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, false);
	if (err != NoError) {
		BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);
		if (err != APIERR_NOSEL)
			WriteReport ("ACAPI_Selection_Get failed");
		return err;
	}

	err = APIERR_NOSEL;
	for (Int32 i = 0; i < selectionInfo.sel_nElem; i++) {
		type = Neig_To_ElemID (selNeigs[i].neigID);
		if (type == API_HatchID) {
			API_Element	element;
			element = {};
			element.header.guid  = selNeigs[i].guid;
			err = ACAPI_Element_Get (&element);
			if (err == NoError && memo != nullptr)
				err = ACAPI_Element_GetMemo (element.header.guid, memo, APIMemoMask_Polygon);
			if (poly != nullptr)
				*poly = element.hatch.poly;
			if (err == NoError)
				break;
			else if (memo != nullptr)
				ACAPI_DisposeElemMemoHdls (memo);
		}
	}

	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);

	return err;
}		// GetSelectedElementPolygon


// -----------------------------------------------------------------------------
//  Creates a Window Library Part using the given coordinates
// -----------------------------------------------------------------------------

static GSErrCode	MakeWindowLibpart (API_LibPart& libPart, const API_Coord* coords, const API_Polygon& poly)
{
	GSErrCode err = NoError;

	API_Box polyBoundBox = { 1E9, 1E9, -1E9, -1E9 };

	if (coords == nullptr || poly.nCoords < 3) {
		WriteReport ("Insufficient polygon parameters");
		return APIERR_BADPARS;
	} else {
		for (Int32 i = 1; i <= poly.nCoords; i++) {
			if (polyBoundBox.xMin > coords[i].x)
				polyBoundBox.xMin = coords[i].x;
			if (polyBoundBox.yMin > coords[i].y)
				polyBoundBox.yMin = coords[i].y;
			if (polyBoundBox.xMax < coords[i].x)
				polyBoundBox.xMax = coords[i].x;
			if (polyBoundBox.yMax < coords[i].y)
				polyBoundBox.yMax = coords[i].y;
		}
	}

	double aa = polyBoundBox.xMax - polyBoundBox.xMin;
	double bb = polyBoundBox.yMax - polyBoundBox.yMin;
	if (aa < EPS || bb < EPS)
		return APIERR_BADPOLY;

	libPart = {};
	libPart.typeID = APILib_WindowID;
	libPart.isTemplate = false;
	libPart.isPlaceable = true;

	const GS::UnID unID = BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetUnIDWithNullRevGuid (BL::BuiltInLibPartID::WindowWallLibPartID);

	CHCopyC (unID.ToUniString ().ToCStr (), libPart.parentUnID);	// Window (Wall) subtype

	if (err == NoError) {
		UInt32 counter = 0;
		do {
			GS::snuprintf (libPart.docu_UName, sizeof (libPart.docu_UName) / sizeof (GS::uchar_t), L("LPTest_Window_%04d"), ++counter);
			err = ACAPI_LibraryPart_Search (&libPart, false);
			if (libPart.location != nullptr) {
				delete libPart.location;
				libPart.location = nullptr;
			}
		} while (err == NoError && counter < 1000);

		err = GetLocation (libPart.location, false);
		if (err != NoError)
			return err;
		err = ACAPI_LibraryPart_Create (&libPart);
		if (err != NoError)
			return err;

		char buffer[1000];

		API_LibPartSection section;

		// Comment script section
		section = {};
		section.sectType = API_SectComText;
		ACAPI_LibraryPart_NewSection (&section);
		sprintf (buffer, "Library Part written by LibPart_Test add-on");
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// Master script section
		section = {};
		section.sectType = API_Sect1DScript;
		ACAPI_LibraryPart_NewSection (&section);
		buffer[0] = '\0';
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// 3D script section
		section = {};
		section.sectType = API_Sect3DScript;
		ACAPI_LibraryPart_NewSection (&section);

		for (Int32 i = 1; i <= poly.nCoords; i++) {
			double xCoord = (coords[i].x - polyBoundBox.xMin - 0.5 * aa) / aa;
			double yCoord = (coords[i].y - polyBoundBox.yMin) / bb;
			sprintf (buffer, "PUT %.4f * a, %.4f * b, 15%s", xCoord, yCoord, GS::EOL);
			ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		}
		sprintf (buffer, "%sMATERIAL gs_frame_mat%s", GS::EOL, GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "WALLHOLE NSP/3, 1, USE (NSP)%s%s", GS::EOL, GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "ADDZ WALL_THICKNESS/2%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "MATERIAL gs_glass_mat%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "PRISM_ NSP/3, gs_glass_thk, USE (NSP)%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// 2D script section
		section = {};
		section.sectType = API_Sect2DScript;
		ACAPI_LibraryPart_NewSection (&section);
		sprintf (buffer, "LINE2 -a/2, 0, -a/2, -WALL_THICKNESS%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "LINE2  a/2, 0,  a/2, -WALL_THICKNESS%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		sprintf (buffer, "LINE2 -a/2, -WALL_THICKNESS/2, a/2, -WALL_THICKNESS/2%s", GS::EOL);
		ACAPI_LibraryPart_WriteSection (Strlen32 (buffer), buffer);
		ACAPI_LibraryPart_EndSection ();

		// Parameters section
		section = {};
		section.sectType = API_SectParamDef;

		GSHandle sectionHdl = nullptr;
		ACAPI_LibraryPart_GetSect_ParamDef (&libPart, nullptr, &aa, &bb, nullptr, &sectionHdl);

		API_LibPartDetails details;
		section = {};
		ACAPI_LibraryPart_SetDetails_ParamDef (&libPart, sectionHdl, &details);

		ACAPI_LibraryPart_AddSection (&section, sectionHdl, nullptr);
		BMKillHandle (&sectionHdl);

		if (libPart.location != nullptr) {
			delete libPart.location;
			libPart.location = nullptr;
		}

		// Save the constructed library part
		if (err == NoError)
			err = ACAPI_LibraryPart_Save (&libPart);

		if (libPart.location != nullptr) {
			delete libPart.location;
			libPart.location = nullptr;
		}
	}

	return err;
}		// MakeWindowLibpart


// -----------------------------------------------------------------------------
//  Puts the specified Window into the clicked Wall
// -----------------------------------------------------------------------------

static GSErrCode	PutWindowIntoWall (const API_LibPart& libPart, const API_Guid& wallGuid, const API_Coord& coord)
{
	GSErrCode			err = NoError;
	API_Element			element;
	API_ElementMemo		memo;

	element = {};
	element.header.guid = wallGuid;
	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		WriteReport ("ACAPI_Element_Get failed");
		return err;
	}

	API_Coord wallBegC = element.wall.begC;

	element = {};
	memo = {};

	element.header.type = API_WindowID;

	err = ACAPI_Element_GetDefaults (&element, &memo);
	if (err != NoError) {
		ACAPI_DisposeElemMemoHdls (&memo);
		WriteReport ("ACAPI_Element_GetDefaults failed");
		return err;
	}

	API_ParamOwnerType paramOwner;
	BNZeroMemory (&paramOwner, sizeof (API_ParamOwnerType));
	paramOwner.libInd = libPart.index;
	err = ACAPI_LibraryPart_OpenParameters (&paramOwner);
	if (err == NoError) {
		API_GetParamsType getParams;
		BNZeroMemory (&getParams, sizeof (API_GetParamsType));
		err = ACAPI_LibraryPart_GetActParameters (&getParams);
		if (err == NoError) {
			ACAPI_DisposeAddParHdl (&memo.params);
			memo.params = getParams.params;
		}
		ACAPI_LibraryPart_CloseParameters ();
	}

	element.window.objLoc = DistCPtr (&coord, &wallBegC);		// curved walls should be handled differently
	element.window.owner  = wallGuid;
	element.window.openingBase.libInd = libPart.index;

	err = ACAPI_CallUndoableCommand ("Place Window",
		[&] () -> GSErrCode {
			GSErrCode err1 = ACAPI_Element_Create (&element, &memo);
			if (err1 != NoError)
				WriteReport ("ACAPI_Element_Create failed");
			return err1;
		});

	ACAPI_DisposeElemMemoHdls (&memo);

	return err;
}		// PutWindowIntoWall


// -----------------------------------------------------------------------------
//  Creates a Window from a Hatch polygon and puts it into the clicked wall
// -----------------------------------------------------------------------------

static void		Do_CreateWindowFromHatch (void)
{
	GSErrCode err = NoError;

	// Calculate the Window shape based on the selected polygonal element
	API_Polygon		poly;
	API_ElementMemo	memo;
	memo = {};

	err = GetSelectedElementPolygon (&poly, &memo);
	if (err != NoError) {
		WriteReport_Alert ("Could not get polygonal shape from the selected element");
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	// Get the position to place the Window element
	API_ElemType	type = API_ZombieElemID;
	API_Guid		wallGuid = APINULLGuid;
	API_Coord3D		coord3D;

	if (!ClickAnElem ("Click on a Wall to place a Window", API_WallID, nullptr, &type, &wallGuid, &coord3D)|| type != API_WallID || wallGuid == APINULLGuid) {
		WriteReport ("Could not find Wall element at the clicked point");
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	// Create the Libpary Part
	API_LibPart libPart;

	if (err == NoError)
		err = MakeWindowLibpart (libPart, *memo.coords, poly);
	else
		WriteReport ("Could not create Window Library Part");

	ACAPI_DisposeElemMemoHdls (&memo);

	// Put the created Window into the Wall
	if (err == NoError) {
		API_Coord coord;
		coord.x = coord3D.x;
		coord.y = coord3D.y;

		err = PutWindowIntoWall (libPart, wallGuid, coord);
	} else
		WriteReport ("Could not put Window into Wall");

	if (err == NoError)
		WriteReport ("Window \"%s\" was successfully created and put into Wall", static_cast<const char*> (GS::UniString (libPart.docu_UName).ToCStr ()));

	return;
}		// Do_CreateWindowFromHatch


// -----------------------------------------------------------------------------
//  Create new files to embedded library
// -----------------------------------------------------------------------------

static void  Do_CreateNewFileToEmbeddedLibrary (void)
{
	auto folderId = API_SpecFolderID::API_EmbeddedProjectLibraryFolderID;

	IO::Location embeddedLibraryFolder;

	if (ACAPI_ProjectSettings_GetSpecFolder (&folderId, &embeddedLibraryFolder) == NoError && IO::Folder (embeddedLibraryFolder).GetStatus () == NoError) {
		//Create textfile
		IO::Location textFilesFolder = embeddedLibraryFolder;
		textFilesFolder.AppendToLocal (IO::Name ("_TextFiles"));
		IO::fileSystem.CreateFolderTree (textFilesFolder);

		IO::Folder folder (textFilesFolder);

		if (folder.GetStatus () == NoError) {
			for (UInt32 i = 1; ; ++i) {
				GS::UniString fileName = GS::UniString::Printf ("LibPart_Test%d.txt", i);

				if (IO::File (folder, IO::Name (fileName), IO::File::OnNotFound::Fail).GetStatus () != NoError) {
					IO::File file (folder, IO::Name (fileName), IO::File::OnNotFound::Create);
					if (IO::File (folder, IO::Name (fileName), IO::File::OnNotFound::Fail).GetStatus () == NoError) {
						file.Open (IO::File::OpenMode::WriteMode);
						file.WriteBin ("test", 4);
						file.Close ();

						char destination[5];

						file.Open (IO::File::OpenMode::ReadMode);
						if (file.ReadBin (destination, 4) == NoError) {
							ACAPI_WriteReport ("Textfile was successfully created", true);
						}
						file.Close ();
					}
					break;
				}
			}
		}

		//Create image
		GSPixMapHandle pixMapHdl = GXCreateGSPixMap (100, 100);
		GX_ARGBColor argb = { 255, 255, 0, 0 };
		GXFillGSPixMap (pixMapHdl, argb);
		auto pngType = FTM::FileTypeManager::SearchForMime ("image/png", nullptr);
		auto err = 0;

		IO::Location textureFileLocation = embeddedLibraryFolder;
		textureFileLocation.AppendToLocal (IO::Name ("_ImageFiles"));
		IO::fileSystem.CreateFolderTree (textureFileLocation);

		IO::Folder imageFolder (textureFileLocation);

		if (imageFolder.GetStatus () == NoError)
		{
			for (UInt32 i = 1; ; ++i) {
				GS::UniString fileName = GS::UniString::Printf ("tex%d.png", i);
				IO::Location textureFileNewLocation (textureFileLocation, IO::Name (fileName));

				if (IO::File (textureFileNewLocation, IO::File::OnNotFound::Fail).GetStatus () != NoError) {
					if (GX::Image (pixMapHdl).WriteToFile (textureFileNewLocation, pngType) == NoError ) {
						ACAPI_WriteReport ("Imagefile was successfully created", true);
						API_LibPart libPart;
						libPart = {};

						libPart.typeID = APILib_PictID;
						libPart.location = &textureFileNewLocation;

						if (NoError == (err = ACAPI_LibraryPart_Register (&libPart))) {
							ACAPI_WriteReport ("Imagefile was successfully registered in Library Manager", true);
						}
					}
					break;
				}
			}
		}
		GXDeleteGSPixMap (pixMapHdl);
	}
	return;
}


#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case 32500:		/* Library Parts */
				switch (menuParams->menuItemRef.itemIndex) {
					case 1:		Do_CreateLibraryPart ();										break;
					case 2:		Do_CreateWindowFromHatch ();									break;
					case 3:		Do_CreateNewFileToEmbeddedLibrary ();							break;
					case 4:		Do_UpdateLibrarySection ();										break;
					case 5:		Do_Interpret3DScript (InterpretResultType::ModelerAPIModel);	break;
					case 6:		Do_Interpret3DScript (InterpretResultType::ModelerModel3D);		break;
					case 7:		Do_Interpret3DScriptBRICK ();									break;
					case 8:		Do_Interpret3DScriptCONE ();									break;
					case 9:		Do_Interpret3DScriptCYLIND ();									break;
				}
				break;
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
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	GSErrCode err = ACAPI_MenuItem_RegisterMenu (32500, 32600, MenuCode_UserDef, MenuFlag_SeparatorBefore);
	return err;
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("LibPart Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	return NoError;
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}		// FreeData
