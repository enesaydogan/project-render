// *****************************************************************************
// Source code for the Element Test Add-On
// *****************************************************************************

#define	_ELEMENT_EDIT_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"Element_Test.h"


// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------



// =============================================================================
//
// Utility functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Write a report for a group of elements being edited
// -----------------------------------------------------------------------------

static void		EditReport (const GS::Array<API_Neig>& items, const GS::Array<API_Neig>& items_save)
{
	DBASSERT (items.GetSize () == items_save.GetSize ());
	for (UIndex i = 0; i < items.GetSize (); i++) {
		const API_ElemType type = Neig_To_ElemID (items[i].neigID);

		if (items[i].guid == APINULLGuid) {
			WriteReport ("[%d]  %s GUID:%s - refused", i+1, ElemID_To_Name (type).ToCStr (CC_UTF8).Get (),
			             									APIGuidToString (items_save[i].guid).ToCStr ().Get ());
		} else {
			WriteReport ("[%d]  %s GUID:%s -> GUID:%s", i+1, ElemID_To_Name (type).ToCStr (CC_UTF8).Get (),
			             									 APIGuidToString (items_save[i].guid).ToCStr ().Get (),
			             									 APIGuidToString (items[i].guid).ToCStr ().Get ());
		}
	}
}		// EditReport


// =============================================================================
//
// Utility functions
//
// =============================================================================
#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Drag some elements
//   - non editable elements should not be touched
//   - other group members should be added automatically
//   - dimensions, labels must be updated automatically
//   - selection dots must be updated automatically
// Check points:
//   - Teamwork workspaces
//   - locked layers
//   - invidual locking
// -----------------------------------------------------------------------------

void		Do_DragElems (bool withInput)
{
	API_EditPars		editPars;
	GS::Array<API_Neig>	items, items_save;
	API_Coord3D			begC, endC;
	GSErrCode			err = NoError;

	if (withInput) {
		items = ClickElements_Neig ("Click elements to drag", API_ZombieElemID);
		if (items.IsEmpty ())
			return;
		items_save = items;
	}

	if (GetEditVector (&begC, &endC, "Enter drag reference point", false)) {
		editPars = {};
		editPars.typeID = APIEdit_Drag;
		editPars.withDelete = true;
		editPars.begC = begC;
		editPars.endC = endC;
		editPars.endC.z += 2.0;

		err = ACAPI_Element_Edit (&items, editPars);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Edit (drag)", err);

		if (err == NoError && withInput)
			EditReport (items, items_save);
	}
}		// Do_DragElems


// -----------------------------------------------------------------------------
// Stretch some elements
//   - non editable elements should not be touched
//   - other group members should be added automatically
//   - dimensions, labels must be updated automatically
//   - selection dots must be updated automatically
// Check points:
//   - Teamwork workspaces
//   - locked layers
//   - invidual locking
// -----------------------------------------------------------------------------

void		Do_StretchElems (bool withInput)
{
	API_EditPars		editPars;
	GS::Array<API_Neig>	items, items_save;
	API_Coord3D			begC, endC;
	GSErrCode			err = NoError;

	if (withInput) {
		items = ClickElements_Neig ("Click elements to stretch", API_ZombieElemID);
		if (items.IsEmpty ())
			return;
		items_save = items;
	}

	if (GetEditVector (&begC, &endC, "Enter stretch reference point", false)) {
		editPars = {};
		editPars.typeID = APIEdit_Stretch;
		editPars.begC = begC;
		editPars.endC = endC;

		err = ACAPI_Element_Edit (&items, editPars);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Edit (stretch)", err);

		if (err == NoError && withInput)
			EditReport (items, items_save);
	}

	return;
}		// Do_StretchElems


// -----------------------------------------------------------------------------
// Resize some elements
//   - non editable elements should not be touched
//   - other group members should be added automatically
//   - dimensions, labels must be updated automatically
//   - selection dots must be updated automatically
// Check points:
//   - Teamwork workspaces
//   - locked layers
//   - invidual locking
// -----------------------------------------------------------------------------

void		Do_ResizeElems (bool withInput)
{
	API_EditPars		editPars;
	GS::Array<API_Neig>	items, items_save;
	API_Coord3D			begC, endC;
	GSErrCode			err = NoError;

	if (withInput) {
		items = ClickElements_Neig ("Click elements to resize", API_ZombieElemID);
		if (items.IsEmpty ())
			return;
		items_save = items;
	}

	if (GetEditVector (&begC, &endC, "Enter resize reference point", false)) {
		editPars = {};
		editPars.typeID = APIEdit_Resize;
		editPars.begC = begC;
		editPars.endC = endC;

		if (GetEditVector (&begC, &endC, "Enter resize reference point2", false)) {
			editPars.typeID = APIEdit_Resize;
			editPars.withDelete = true;
			editPars.endC2.x = editPars.begC.x - begC.x + endC.x;
			editPars.endC2.y = editPars.begC.y - begC.y + endC.y;

			err = ACAPI_Element_Edit (&items, editPars);
			if (err != NoError)
				ErrorBeep ("ACAPI_Element_Edit (resize)", err);

		if (err == NoError && withInput)
			EditReport (items, items_save);
		}
	}

	return;
}		// Do_ResizeElems


// -----------------------------------------------------------------------------
// Rotate some elements
//   - non editable elements should not be touched
//   - other group members should be added automatically
//   - dimensions, labels must be updated automatically
//   - selection dots must be updated automatically
// Check points:
//   - Teamwork workspaces
//   - locked layers
//   - invidual locking
// -----------------------------------------------------------------------------

void		Do_RotateElems (bool withInput)
{
	API_EditPars		editPars;
	GS::Array<API_Neig>	items, items_save;
	API_Coord3D			begC, endC, origC;
	GSErrCode			err = NoError;

	if (withInput) {
		items = ClickElements_Neig ("Click elements to rotate", API_ZombieElemID);
		if (items.IsEmpty ())
			return;
		items_save = items;
	}

	if (GetEditArc (&begC, &endC, &origC,  "Enter rotation reference point")) {
		editPars = {};
		editPars.typeID = APIEdit_Rotate;
		editPars.begC = begC;
		editPars.endC = endC;
		editPars.origC.x = origC.x;
		editPars.origC.y = origC.y;
		editPars.withDelete = true;
		err = ACAPI_Element_Edit (&items, editPars);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Edit (rotate)", err);

		if (err == NoError && withInput)
			EditReport (items, items_save);
	}

	return;
}		// Do_RotateElems



// -----------------------------------------------------------------------------
// Mirror some elements
//   - non editable elements should not be touched
//   - other group members should be added automatically
//   - dimensions, labels must be updated automatically
//   - selection dots must be updated automatically
// Check points:
//   - Teamwork workspaces
//   - locked layers
//   - invidual locking
// -----------------------------------------------------------------------------

void		Do_MirrorElems (bool withInput)
{
	API_EditPars		editPars;
	GS::Array<API_Neig>	items, items_save;
	API_Coord3D			begC, endC;
	GSErrCode			err = NoError;

	if (withInput) {
		items = ClickElements_Neig ("Click elements to mirror", API_ZombieElemID);
		if (items.IsEmpty ())
			return;
		items_save = items;
	}

	if (GetEditVector (&begC, &endC, "Enter mirror reference point", false)) {
		editPars = {};
		editPars.typeID = APIEdit_Mirror;
		editPars.begC = begC;
		editPars.endC = endC;
		editPars.withDelete = true;
		err = ACAPI_Element_Edit (&items, editPars);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_Edit (mirror)", err);

		if (err == NoError && withInput)
			EditReport (items, items_save);
	}

	return;
}		// Do_MirrorElems


// -----------------------------------------------------------------------------
// Elevate some elements
//   - non editable elements should not be touched
//   - other group members should be added automatically
//   - dimensions, labels must be updated automatically
//   - selection dots must be updated automatically
// Check points:
//   - Teamwork workspaces
//   - locked layers
//   - invidual locking
// -----------------------------------------------------------------------------

void		Do_ElevElems (bool withInput)
{
	API_EditPars		editPars;
	GS::Array<API_Neig>	items, items_save;
	GSErrCode			err = NoError;

	if (withInput) {
		items = ClickElements_Neig ("Click elements to elevate (by fixed 2m)", API_ZombieElemID);
		if (items.IsEmpty ())
			return;
		items_save = items;
	}

	editPars = {};
	editPars.typeID = APIEdit_Elevate;
	editPars.withDelete = true;
	editPars.endC.z = 2.0;							/* begC.z = 0.0 */
	err = ACAPI_Element_Edit (&items, editPars);
	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Edit (elevate)", err);

	if (err == NoError && withInput)
		EditReport (items, items_save);

	return;
}		// Do_ElevElems
