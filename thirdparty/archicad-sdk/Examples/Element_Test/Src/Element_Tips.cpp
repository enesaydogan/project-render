// *****************************************************************************
// Source code for the Element Test Add-On
// *****************************************************************************

#define	_ELEMENT_TIPS_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include "APIEnvir.h"
#include "ACAPinc.h"					// also includes APIdefs.h
#include "APICommon.h"

#include "Element_Test.h"

// from Geometry
#include "TRANMAT.h"
#include "TM.h"

// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------


static void		API2AC_Tranmat (TRANMAT* tranmat, const API_Tranmat& api_tranmat)
{
	tranmat->status = 0;
	tranmat->dummy1 = tranmat->dummy2 = tranmat->dummy3 = 0;
	Geometry::Vector3d vec1 (api_tranmat.tmx[0], api_tranmat.tmx[4], api_tranmat.tmx[8]), vec2 (api_tranmat.tmx[1], api_tranmat.tmx[5], api_tranmat.tmx[9]), vec3 (api_tranmat.tmx[2], api_tranmat.tmx[6], api_tranmat.tmx[10]), vec4 (api_tranmat.tmx[3], api_tranmat.tmx[7], api_tranmat.tmx[11]);
	tranmat->SetMatrix (Geometry::Matrix34::CreateFromColVectors (vec1, vec2, vec3, vec4));
}		/* API2AC_Tranmat */


// -----------------------------------------------------------------------------
// Disable visual feedback to speed up operations
//   - operations may results in disturbing screen update
//   - do it in one step if applicable
//
// Delete the openings of the clicked wall
// mode:
//	 - 1 (bad way): 		non-acceptable flash in each delete operation
//	 - 2 (solution #1): 	do the operation with one API call
//	 - 3 (solution #2): 	do it in separate calls, but care about screen updates
// -----------------------------------------------------------------------------

void		Do_AvoidFlashing (short mode)
{
	API_Element		element = {};
	API_Guid		guid;
	GSErrCode		err;

	if (!ClickAnElem ("Click a wall with many windows in it", API_WallID, nullptr, nullptr, &guid)) {
		WriteReport_Alert ("No wall was clicked");
		return;
	}

	element.header.type = API_WallID;
	element.header.guid = guid;
	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get (wall)", err);
		return;
	}

	// collect the openings
	GS::Array<API_Guid>	wallWindows;
	GS::Array<API_Guid>	wallDoors;
	err = ACAPI_Grouping_GetConnectedElements (guid, API_WindowID, &wallWindows);
	if (err == NoError)
		err = ACAPI_Grouping_GetConnectedElements (guid, API_DoorID, &wallDoors);

	if (err != NoError)
		return;

	GS::Array<API_Guid> elemGuids;
	elemGuids.Append (wallWindows);
	elemGuids.Append (wallDoors);

	// do the operation
	if (mode == 3) {		// best way: do the operation in one API call
		err = ACAPI_Element_Delete (elemGuids);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Delete", err);

	} else {				// the operation is split into separate API calls
		for (const API_Guid& guid : elemGuids) {
			err = ACAPI_Element_Delete ({ guid });
			if (err == NoError)
				WriteReport ("  GUID:%s  deleted", APIGuidToString (guid).ToCStr ().Get ());
			else
				WriteReport ("  GUID:%s  ERROR: %d", APIGuidToString (guid).ToCStr ().Get (), err);
		}
	}

	return;
}		// Do_AvoidFlashing


// -----------------------------------------------------------------------------
// Get the first window's position and normal vector
// -----------------------------------------------------------------------------
void		Do_GetFirstWindowPosition (void)
{
	API_ElemType		type;
	API_Guid			wallGuid;
	API_Element			element = {};
	GS::Array<API_Guid>	connWinds;
	GSErrCode			err;

	if (!ClickAnElem ("Click a wall to get the direction of the first opening", API_WallID, nullptr, &type, &wallGuid)) {
		WriteReport_Alert ("No wall was clicked");
		return;
	}

	element.header.type = type;
	element.header.guid	= wallGuid;
	err = ACAPI_Element_Get (&element);
	if (err == NoError)
		err = ACAPI_Grouping_GetConnectedElements (wallGuid, API_WindowID, &connWinds);

	if (err != NoError || connWinds.GetSize () == 0) {
		WriteReport_Alert ("No windows in this wall");
		return;
	}

	API_Tranmat tm;
	if (ACAPI_Element_GetOpeningTransformation (&connWinds[0], &tm) == NoError) {
		TRANMAT		openingPlaceTransformation;
		Vector3D	refPlaneNormalVector (0.0, 0.0, 1.0);
		Coord3D		orig (0.0, 0.0, 0.0);

		API2AC_Tranmat (&openingPlaceTransformation, tm);
		refPlaneNormalVector = Geometry::TransformUnitNormalVector (openingPlaceTransformation, refPlaneNormalVector);
		orig = Geometry::TransformPoint (openingPlaceTransformation, orig);

		DBPrintf ("1st window's position and normal vector:\n");
		DBPrintf ("%5.2lf, %5.2lf, %5.2lf\n", orig.x, orig.y, orig.z);
		DBPrintf ("%5.2lf, %5.2lf, %5.2lf\n", refPlaneNormalVector.x, refPlaneNormalVector.y, refPlaneNormalVector.z);
	}

	return;
}		// Do_CreateCurvedWall

