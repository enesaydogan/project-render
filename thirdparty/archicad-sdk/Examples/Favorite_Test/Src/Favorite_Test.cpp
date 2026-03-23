// *****************************************************************************
// Source code for the Favorite Test Add-On
// *****************************************************************************

#define	_Favorite_Test_TRANSL_
#define	_Geometry_Test_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"FileSystem.hpp"
#include	"ProfileVectorImage.hpp"

#include	"UserItemDialog.hpp"


// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------


// -----------------------------------------------------------------------------
// PrintFourCharCode
//	helper function
// -----------------------------------------------------------------------------
static GS::String	PrintFourCharCode (API_ElemVariationID variationID)
{
	GS::String	str;

	str.SetLength (5);
	const char*	fc = (const char*) &variationID;
	for (UInt32 ii = 0; ii < 4; ++ii)
		str[ii] = fc[3-ii];

	return str;
}


// =============================================================================
//
// Main functions
//
// =============================================================================


// -----------------------------------------------------------------------------
// Lists the favorites (name) to the Report Window
//
// -----------------------------------------------------------------------------

static void		Do_ListFavorites (void)
{
	API_ToolBoxInfo	toolboxInfo;
	GSErrCode		err = NoError;

	BNClear (toolboxInfo);
	err = ACAPI_Toolbox_GetToolBoxInfo (&toolboxInfo, true);
	if (err == NoError) {
		ACAPI_WriteReport ("### Favorite_Test Do_ListFavorites ###", false);

		for (Int32 i = 0; i < toolboxInfo.nTools; ++i) {
			short						count = 0;
			GS::Array<GS::UniString>	names;

			err = ACAPI_Favorite_GetNum ((*toolboxInfo.data)[i].type,
										 &count,
										 nullptr,
										 &names);

			const GS::UniString elemTypeStr = ElemID_To_Name ((*toolboxInfo.data)[i].type);

			if (err != NoError) {
				ACAPI_WriteReport ("ACAPI_Favorite_GetNum failed (Elem type: %s,  VariationID: '%s')\n", false,
								   elemTypeStr.ToCStr ().Get (),
								   PrintFourCharCode ((*toolboxInfo.data)[i].type.variationID).ToCStr ());
				continue;
			}

			if (count != static_cast<short> (names.GetSize ())) {
				DBBREAK_STR (GS::UniString::Printf ("ACAPI_Favorite_GetNum returned bad data (Elem type: %s,  VariationID: '%s')\n",
													elemTypeStr.ToCStr ().Get (),
													PrintFourCharCode ((*toolboxInfo.data)[i].type.variationID).ToCStr ()).ToCStr ().Get ());
				continue;
			}

			ACAPI_WriteReport ("------------------------------------------\nElem type: %s,  VariationID: '%s'\n", false,
							   elemTypeStr.ToCStr ().Get (),
							   PrintFourCharCode ((*toolboxInfo.data)[i].type.variationID).ToCStr ());

			for (short idx = 0; idx < count; idx++) {
				ACAPI_WriteReport ("Name: %s", false, names[idx].ToCStr ().Get ());
			}
		}
	} else {
		ACAPI_WriteReport ("APIEnv_GetToolBoxInfoID failed", false);
	}

	BMKillHandle ((GSHandle *) &toolboxInfo.data);

	return;
}		/* Do_ListFavorites */


// -----------------------------------------------------------------------------
// Place instances of the Favorites with column type
//
// -----------------------------------------------------------------------------

static void		Do_PlaceFavorite (API_ElemType type, const std::function<void (API_Element&)>& modifier)
{
	GSErrCode					err = NoError;
	short						count = 0;
	GS::Array<GS::UniString>	names;
	GS::UniString				elemTypeStr = ElemID_To_Name (type);

	ACAPI_WriteReport ("### Favorite_Test Do_PlaceFavorite" + elemTypeStr + "###", false);

	err = ACAPI_Favorite_GetNum (type,
								 &count,
								 nullptr,
								 &names);

	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Favorite_GetNum failed (Elem type: %s,  VariationID: '%s')\n", false,
						   elemTypeStr.ToCStr ().Get (),
						   PrintFourCharCode (APIVarId_Generic).ToCStr ());
		return;
	}

	if (count == 0 || names.GetSize () == 0) {
		ACAPI_WriteReport ("There isn't any favorite " + elemTypeStr + " to place!", false);
		return;
	}

	for (Int32 ii = 0; ii < count; ++ii) {
		API_Favorite	favorite (names[ii]);
		favorite.memo.New ();
		favorite.properties.New ();
		favorite.classifications.New ();
		favorite.elemCategoryValues.New ();

		err = ACAPI_Favorite_Get (&favorite);
		if (err != NoError) {
			ACAPI_WriteReport ("ACAPI_Favorite_Get failed (Name: %s)", false, names[ii].ToCStr ().Get ());
			ACAPI_DisposeElemMemoHdls (&favorite.memo.Get ());
			return;
		}

		ACAPI_CallUndoableCommand ("Favorite_Test Do_PlaceFavorite" + elemTypeStr,
			[&] () -> GSErrCode {
				API_Element& element = favorite.element;		
				modifier (element);

				GSErrCode err1 = ACAPI_Element_Create (&element, &favorite.memo.Get ());

				if (err1 != NoError)
					ACAPI_WriteReport ("ACAPI_Element_Create failed", false);
				else
					ACAPI_WriteReport ("ACAPI_Element_Create successfully placed new element by favorite:\n  Name: %s, Elem type: %s,  VariationID: '%s'", false,
									   names[ii].ToCStr ().Get (), elemTypeStr.ToCStr ().Get (),
									   PrintFourCharCode (APIVarId_Generic).ToCStr ());

				// Must set classifications before properties
				favorite.classifications->Enumerate ([&element] (const GS::Pair<API_Guid, API_Guid>& pair) {
					ACAPI_Element_AddClassificationItem (element.header.guid, pair.second);
				});

				favorite.elemCategoryValues->Enumerate ([&element] (const API_ElemCategoryValue& categoryValue) {
					ACAPI_Category_SetCategoryValue (element.header.guid, categoryValue.category, categoryValue);
				});

				ACAPI_Element_SetProperties (element.header.guid, favorite.properties.Get ());

				return err1;
			});

		ACAPI_DisposeElemMemoHdls (&favorite.memo.Get ());
	}

	return;
}		


static void		Do_PlaceFavoriteColumn (void)
{
	Int32 index = 0;
	return Do_PlaceFavorite (API_ColumnID, [&index] (API_Element& element) {
		element.column.origoPos.x = index * 2.0;
		index++;
	});
}		/* Do_PlaceFavoriteColumn */


static void		Do_PlaceFavoriteOpening (void)
{
	API_Element ownerWall = {};
	ownerWall.header.type = API_WallID;
	DBASSERT (ACAPI_Element_GetDefaults (&ownerWall, nullptr) == NoError);

	ownerWall.wall.begC = API_Coord {0.0, 0.0};
	ownerWall.wall.endC = API_Coord {20.0, 0.0};
	ownerWall.wall.angle = 0.0;
	ownerWall.wall.referenceLineLocation = APIWallRefLine_Outside;
	ownerWall.wall.flipped = false;
	ownerWall.wall.modelElemStructureType = API_BasicStructure;
	ownerWall.wall.thickness = 0.3;
	ownerWall.wall.relativeTopStory = 0;
	ownerWall.wall.height = 5.0;

	DBASSERT (ACAPI_CallUndoableCommand ("Favorite_Test Create opening host wall", [&ownerWall] () {
		return ACAPI_Element_Create (&ownerWall, nullptr);
	}) == NoError);

	double xPos = 1.0;
	return Do_PlaceFavorite (API_OpeningID, [&xPos, &ownerWall] (API_Element& element) {
		element.opening.extrusionGeometryData.frame.basePoint = API_Coord3D {xPos, 0, 1.5};
		element.opening.extrusionGeometryData.frame.axisX = API_Vector3D {1, 0, 0};
		element.opening.extrusionGeometryData.frame.axisY = API_Vector3D {0, 1, 0};
		element.opening.extrusionGeometryData.frame.axisZ = API_Vector3D {0, 0, 1};
		//element.opening.extrusionGeometryData.parameters.constraint = API_OpeningForcedHori;
		element.opening.owner = ownerWall.header.guid;
		xPos += element.opening.extrusionGeometryData.parameters.width;
	});
}		/* Do_PlaceFavoriteOpening */


// -----------------------------------------------------------------------------
// Place instances of the Favorites with column segment type as
// single segment columns
// -----------------------------------------------------------------------------

static void		Do_PlaceFavoriteColumnSegment (void)
{
	GSErrCode					err = NoError;
	short						count = 0;
	GS::Array<GS::UniString>	names;
	GS::UniString				elemTypeStr = ElemID_To_Name (API_ColumnSegmentID);

	ACAPI_WriteReport ("### Favorite_Test Do_PlaceFavoriteColumnSegment ###", false);

	err = ACAPI_Favorite_GetNum (API_ColumnSegmentID,
								 &count,
								 nullptr,
								 &names);

	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Favorite_GetNum failed (Elem type: %s,  VariationID: '%s')\n", false,
						   elemTypeStr.ToCStr ().Get (),
						   PrintFourCharCode (APIVarId_Generic).ToCStr ());
		return;
	}

	if (count == 0 || names.GetSize () == 0) {
		ACAPI_WriteReport ("There isn't any favorite column segment to place!", false);
		return;
	}

	for (Int32 ii = 0; ii < count; ++ii) {
		API_Favorite	favorite (names[ii]);
		favorite.memo.New ();
		favorite.properties.New ();
		favorite.classifications.New ();
		favorite.elemCategoryValues.New ();

		err = ACAPI_Favorite_Get (&favorite);
		if (err != NoError) {
			ACAPI_WriteReport ("ACAPI_Favorite_Get failed (Name: %s)", false, names[ii].ToCStr ().Get ());
			ACAPI_DisposeElemMemoHdls (&favorite.memo.Get ());
			return;
		}

		ACAPI_CallUndoableCommand ("Favorite_Test Do_PlaceFavoriteColumnSegment",
									[&]() -> GSErrCode {
			API_Element element;
			element = {};

			element.header.type = API_ColumnID;
			err = ACAPI_Element_GetDefaults (&element, nullptr);
			if (err != NoError) {
				ACAPI_WriteReport ("ACAPI_Element_GetDefaults failed", false);
				ACAPI_DisposeElemMemoHdls (&favorite.memo.Get ());
				return err;
			}

			API_ElementMemo memo;
			memo = {};

			element.column.origoPos.x = ii * 2.0;

			element.column.nSegments = 1;
			memo.columnSegments = reinterpret_cast<API_ColumnSegmentType*> (BMAllocatePtr (sizeof (API_ColumnSegmentType), ALLOCATE_CLEAR, 0));
			memo.columnSegments[0] = favorite.element.columnSegment;

			element.column.nCuts = 2;
			memo.assemblySegmentCuts = reinterpret_cast<API_AssemblySegmentCutData*> (BMAllocatePtr (2 * sizeof (API_AssemblySegmentCutData), ALLOCATE_CLEAR, 0));
			memo.assemblySegmentCuts[0].cutType = APIAssemblySegmentCut_Horizontal;
			memo.assemblySegmentCuts[1].cutType = APIAssemblySegmentCut_Horizontal;

			element.column.nSchemes = 1;
			memo.assemblySegmentSchemes = reinterpret_cast<API_AssemblySegmentSchemeData*> (BMAllocatePtr (sizeof (API_AssemblySegmentSchemeData), ALLOCATE_CLEAR, 0));
			memo.assemblySegmentSchemes[0].lengthType = APIAssemblySegment_Proportional;
			memo.assemblySegmentSchemes[0].lengthProportion = 1.0;

			element.column.nProfiles = 0;
			if (favorite.memo.Get ().customOrigProfile != nullptr) {
				element.column.nProfiles = 1;
				memo.assemblySegmentProfiles = reinterpret_cast<API_AssemblySegmentProfileData*> (BMAllocatePtr (sizeof (API_AssemblySegmentProfileData), ALLOCATE_CLEAR, 0));
				memo.assemblySegmentProfiles[0].segmentIndex = 0;
				memo.assemblySegmentProfiles[0].customOrigProfile = new ProfileVectorImage ();
				*memo.assemblySegmentProfiles[0].customOrigProfile = *favorite.memo.Get ().customOrigProfile;

				if (favorite.memo.Get ().stretchedProfile != nullptr) {
					memo.assemblySegmentProfiles[0].stretchedProfile = new ProfileVectorImage ();
					*memo.assemblySegmentProfiles[0].stretchedProfile = *favorite.memo.Get ().stretchedProfile;
				}
			}

			GSErrCode err1 = ACAPI_Element_Create (&element, &memo);

			if (err1 != NoError)
				ACAPI_WriteReport ("ACAPI_Element_Create failed", false);
			else
				ACAPI_WriteReport ("ACAPI_Element_Create successfully placed new element by favorite:\n  Name: %s, Elem type: %s,  VariationID: '%s'", false,
									names[ii].ToCStr ().Get (), elemTypeStr.ToCStr ().Get (),
									PrintFourCharCode (APIVarId_Generic).ToCStr ());

			ACAPI_DisposeElemMemoHdls (&memo);
			return err1;
		});

		ACAPI_DisposeElemMemoHdls (&favorite.memo.Get ());
	}
}		/* Do_PlaceFavoriteColumnSegment */


// -----------------------------------------------------------------------------
// Create favorite from the clicked element
//
// -----------------------------------------------------------------------------

static void		Do_CreateFavorite (void)
{
	API_ElemType		type;
	API_Guid			guid;
	GSErrCode			err = NoError;
	API_Favorite		favorite ("Favorite_Test");

	ACAPI_WriteReport ("### Favorite_Test Do_CreateFavorite ###", false);

	if (!ClickAnElem ("Click an elem to create new favorite", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	favorite.element.header.type = type;
	favorite.element.header.guid = guid;
	err = ACAPI_Element_Get (&favorite.element);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Get failed", false);
		return;
	}

	favorite.classifications.New ();
	err = ACAPI_Element_GetClassificationItems (guid, favorite.classifications.Get ());
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetClassificationItems failed", false);
		return;
	}

	GS::Array<API_PropertyDefinition> definitions;
	err = ACAPI_Element_GetPropertyDefinitions (guid, API_PropertyDefinitionFilter_UserDefined, definitions);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetPropertyDefinitions failed", false);
		return;
	}

	favorite.properties.New ();
	err = ACAPI_Element_GetPropertyValues (guid, definitions, favorite.properties.Get ());
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetProperties failed", false);
		return;
	}
    for (UIndex ii = favorite.properties->GetSize (); ii >= 1; --ii) {
        const API_Property& p = favorite.properties->Get (ii - 1);
        if (p.isDefault || p.definition.canValueBeEditable == false || p.status != API_Property_HasValue)
            favorite.properties->Delete (ii - 1);
    }

	favorite.memo.New ();
	BNZeroMemory (&favorite.memo.Get (), sizeof (API_ElementMemo));

	err = ACAPI_Element_GetMemo	(guid, &favorite.memo.Get ());
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetMemo failed", false);
		return;
	}
	if (favorite.memo->elemInfoString != nullptr)
		delete favorite.memo->elemInfoString;

    favorite.memo->elemInfoString = new GS::UniString ("My favorite ID");

	err = ACAPI_Favorite_Create (favorite);
	if (err != NoError) {
		if (err == APIERR_BADNAME)
			ACAPI_WriteReport ("ACAPI_Favorite_Create failed because bad name was given (maybe already in used)", false);
		else
			ACAPI_WriteReport ("ACAPI_Favorite_Create failed", false);
	} else {
		ACAPI_WriteReport ("ACAPI_Favorite_Create successfully created new favorite:", false);
		ACAPI_WriteReport ("Name: %s, Elem type: %s,  VariationID: '%s'", false,
						   favorite.name.ToCStr ().Get (),
						   ElemID_To_Name (type).ToCStr ().Get (),
						   PrintFourCharCode (favorite.element.header.type.variationID).ToCStr ());
	}

	ACAPI_DisposeElemMemoHdls (&favorite.memo.Get ());

	return;
}		/* Do_CreateFavorite */


// -----------------------------------------------------------------------------
// Change favorite named "Favorite_Test"
//	- set it's ID string to "[Changed]"
// -----------------------------------------------------------------------------

static void		Do_ChangeFavorite (void)
{
	GSErrCode			err;
	API_Favorite		favorite ("Favorite_Test");

	favorite.memo.New ();
	BNZeroMemory (&favorite.memo.Get (), sizeof (API_ElementMemo));
    favorite.memo->elemInfoString = new GS::UniString ("[Changed]");

	ACAPI_WriteReport ("### Favorite_Test Do_ChangeFavorite ###", false);

	err = ACAPI_Favorite_Change (favorite, nullptr, APIMemoMask_ElemInfoString);
	if (err != NoError) {
		if (err == APIERR_BADNAME)
			ACAPI_WriteReport ("ACAPI_Favorite_Change failed because bad name was given", false);
		else if (err == APIERR_NOTMINE)
			ACAPI_WriteReport ("ACAPI_Favorite_Change failed because the given favorite does not belong to the client", false);
		else
			ACAPI_WriteReport ("ACAPI_Favorite_Change failed", false);
	} else {
		ACAPI_WriteReport ("ACAPI_Favorite_Change successfully changed favorite named \"%s\"", false, favorite.name.ToCStr ().Get ());
	}

	ACAPI_DisposeElemMemoHdls (&favorite.memo.Get ());

	return;
}		/* Do_ChangeFavorite */


// -----------------------------------------------------------------------------
// Rename favorite named "Favorite_Test"
//	- appends "[Renamed]" to it's name
// -----------------------------------------------------------------------------

static void		Do_RenameFavorite (void)
{
	GSErrCode			err;
	GS::UniString		oldName = "Favorite_Test";
	GS::UniString		newName = oldName + " [Renamed]";

	ACAPI_WriteReport ("### Favorite_Test Do_RenameFavorite ###", false);

	err = ACAPI_Favorite_Rename (oldName, newName);
	if (err != NoError) {
		if (err == APIERR_BADNAME)
			ACAPI_WriteReport ("ACAPI_Favorite_Rename failed because bad name was given", false);
		if (err == APIERR_NAMEALREADYUSED)
			ACAPI_WriteReport ("ACAPI_Favorite_Rename failed because the new name is alerady in used", false);
		else if (err == APIERR_NOTMINE)
			ACAPI_WriteReport ("ACAPI_Favorite_Rename failed because the given favorite does not belong to the client", false);
		else
			ACAPI_WriteReport ("ACAPI_Favorite_Rename failed", false);
	} else {
		ACAPI_WriteReport ("ACAPI_Favorite_Rename successfully renamed favorite \"%s\" to \"%s\"", false, oldName.ToCStr ().Get (), newName.ToCStr ().Get ());
	}

	return;
}		/* Do_RenameFavorite */


// -----------------------------------------------------------------------------
// Delete favorite named "Favorite_Test"
//
// -----------------------------------------------------------------------------

static void		Do_DeleteFavorite (void)
{
	GSErrCode			err;
	GS::UniString		name = "Favorite_Test";

	ACAPI_WriteReport ("### Favorite_Test Do_DeleteFavorite ###", false);

	err = ACAPI_Favorite_Delete (name);
	if (err != NoError) {
		if (err == APIERR_BADNAME)
			ACAPI_WriteReport ("ACAPI_Favorite_Delete failed because bad name was given", false);
		else if (err == APIERR_NOTMINE)
			ACAPI_WriteReport ("ACAPI_Favorite_Delete failed because the given favorite does not belong to the client", false);
		else
			ACAPI_WriteReport ("ACAPI_Favorite_Delete failed", false);
	} else {
		ACAPI_WriteReport ("ACAPI_Favorite_Delete successfully deleted favorite named \"%s\"", false, name.ToCStr ().Get ());
	}

	return;
}		/* Do_DeleteFavorite */


// -----------------------------------------------------------------------------
// List the favorite sub elements library part names. 
// Currently only the Curtain Wall is supported.
//
// -----------------------------------------------------------------------------

static void		Do_ListFavoriteSubElements (void)
{
	GSErrCode			err;
	GS::UniString		name = "Favorite_Test";

	ACAPI_WriteReport ("### Favorite_Test Do_ListFavoriteSubElements ###", false);

	GS::Array<GS::UniString>	names;
	err = ACAPI_Favorite_GetNum (API_CurtainWallID, nullptr, nullptr, &names);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Favorite_GetNum failed!", false);
		return;
	}

	for (auto& favName : names) {
		API_Favorite favorite (favName);
		favorite.subElements.New ();

		err = ACAPI_Favorite_Get (&favorite);
		if (err != NoError) {
			ACAPI_WriteReport ("ACAPI_Favorite_Get failed (Name: %s)", false, favName.ToCStr ().Get ());
			continue;
		}

		ACAPI_WriteReport ("Favorite (Name: %s)", false, favName.ToCStr ().Get ());
		for (auto& subElement : favorite.subElements.Get ()) {
			API_LibPart libPart = {};
			libPart.index = subElement.subElem.object.libInd;
			if (ACAPI_LibraryPart_Get (&libPart) == NoError) {
				
				if (subElement.subType == APISubElement_CWFrameClass) {
					ACAPI_WriteReport ("Frame with library part name: %s)", false, GS::UniString (libPart.docu_UName).ToPrintf ());
				}

				if (subElement.subType == APISubElement_CWPanelClass) {
					ACAPI_WriteReport ("Panel with library part name: %s)", false, GS::UniString (libPart.docu_UName).ToPrintf ());
				}

				delete libPart.location;
			}

			ACAPI_DisposeElemMemoHdls (&subElement.memo);
		}
	}
}		/* Do_ListFavoriteSubElements */


// -----------------------------------------------------------------------------
// Import and export favorites"
//
// -----------------------------------------------------------------------------

static void Do_ImportExportFavorites (void)
{
	GSErrCode err = NoError;

	IO::Location fileLocation;
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::TemporaryFolder, &fileLocation);
	fileLocation.AppendToLocal (IO::Name ("favorite_test_file.prf"));

	err = ACAPI_Favorite_Export (fileLocation);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Favorite_Export failed", false);
		return;
	}

	API_FavoriteFolderHierarchy folderHierarchy;
	folderHierarchy.Push ("FavoritTest Add-On Folder");
	err = ACAPI_Favorite_Import (fileLocation, folderHierarchy, true, API_FavoriteAppend);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Favorite_Import failed", false);
		return;
	}

	IO::fileSystem.Delete (fileLocation);
}

static void Do_ExportReadCreateFavorites (void)
{
	GSErrCode err = NoError;

	IO::Location fileLocation;
	IO::fileSystem.GetSpecialLocation (IO::FileSystem::TemporaryFolder, &fileLocation);
	fileLocation.AppendToLocal (IO::Name ("favorite_test_file.prf"));

	err = ACAPI_Favorite_Export (fileLocation);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Favorite_Export failed", false);
		return;
	}

	GS::Array<API_Favorite> favorites;
	err = ACAPI_Favorite_Read (fileLocation, &favorites);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Favorite_Read failed", false);
		return;
	}

	API_FavoriteFolderHierarchy newFolder;
    newFolder.Push ("FavoritTest Add-On Folder 2");

    for (API_Favorite& favorite : favorites) {
        favorite.name += " [Reloaded]";
        favorite.folder = newFolder;
		err = ACAPI_Favorite_Create (favorite);
		if (err != NoError) {
			ACAPI_WriteReport ("ACAPI_Favorite_Create failed", false);
		}
		if (favorite.memo.HasValue ())
			ACAPI_DisposeElemMemoHdls (&favorite.memo.Get ());
		if (favorite.memoMarker.HasValue ())
			ACAPI_DisposeElemMemoHdls (&favorite.memoMarker.Get ());
	}
}


// -----------------------------------------------------------------------------
// Get preview images for Favorite named "Favorite_Test"
//	- draw them into useritems on a new modal dialog
// -----------------------------------------------------------------------------

static void		Do_GetPreviewImagesOfFavorite (void)
{
	ACAPI_WriteReport ("### Favorite_Test Do_GetPreviewImagesOfFavorite ###", false);

	UserItemDialog dialog (3, 250, 250, [&] (const DG::UserItem& userItem, const UIndex index) {
		NewDisplay::NativeImage nativeImage (userItem.GetClientWidth (), userItem.GetClientHeight (), 32, nullptr, false, 0, true, userItem.GetResolutionFactor ());

		GSErrCode err = ACAPI_Favorite_GetPreviewImage ("Favorite_Test", (API_ImageViewID) index, &nativeImage);
		if (err != NoError) {
			if (err == APIERR_BADNAME)
				ACAPI_WriteReport ("ACAPI_Favorite_GetPreviewImage failed because bad name was given", false);
			else
				ACAPI_WriteReport ("ACAPI_Favorite_GetPreviewImage failed", false);
		}

		return nativeImage;
	});
	dialog.Invoke ();
}		/* Do_GetPreviewImagesOfFavorite */


// -----------------------------------------------------------------------------
// Entry points to handle ARCHICAD events
//
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *params)
{
	switch (params->menuItemRef.itemIndex) {
		case 1:		Do_ListFavorites ();				break;
		case 2:		Do_PlaceFavoriteColumn ();			break;
		case 3:		Do_PlaceFavoriteColumnSegment ();	break;
		case 4:		Do_CreateFavorite ();				break;
		case 5:		Do_ChangeFavorite ();				break;
		case 6:		Do_RenameFavorite ();				break;
		case 7:		Do_DeleteFavorite ();				break;
		case 8:		Do_ListFavoriteSubElements ();		break;
		case 9:		Do_ImportExportFavorites ();		break;
		case 10:	Do_ExportReadCreateFavorites ();	break;
		case 11:	Do_GetPreviewImagesOfFavorite ();	break;
		case 12:	Do_PlaceFavoriteOpening ();			break;
	}

	return NoError;
}		// DoCommand


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


//------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
//------------------------------------------------------
GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Favorite_Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	return err;
}		/* Initialize */


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}		// FreeData
