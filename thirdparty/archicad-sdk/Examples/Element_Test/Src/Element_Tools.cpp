// *****************************************************************************
// Source code for the Element Test Add-On
// *****************************************************************************

#define	_ELEMENT_TOOLS_TRANSL_


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
// Apply tools on elements
//
// =============================================================================

// -----------------------------------------------------------------------------
// Do the command on the clicked or selected elements
//   - non editable elements should not be touched
//   - other group members should be added automatically
//   - selection dots must be updated automatically
// Check points
//   - Teamwork workspaces
//   - locked layers
//   - invidual locking
// -----------------------------------------------------------------------------

void		Do_ElemTool (bool withInput, const char* inputStr, API_ToolCmdID typeID)
{
	GS::Array<API_Guid> elemGuids;
	GSErrCode			err;

	if (withInput)
		elemGuids = ClickElements_Guid (inputStr, API_ZombieElemID);

	err = ACAPI_Grouping_Tool (elemGuids, typeID, nullptr);
	if (err != NoError)
		ErrorBeep ("ACAPI_Grouping_Tool", err);
}		// Do_ElemTool


// -----------------------------------------------------------------------------
// Toggle the Suspend Groups switch
//   - selection dots must be updated automatically
//   - menu must be updated
// -----------------------------------------------------------------------------

void		Do_SuspendGroups (void)
{
	GSErrCode	err;

	err = ACAPI_Grouping_Tool ({}, APITool_SuspendGroups, nullptr);
	if (err != NoError)
		ErrorBeep ("ACAPI_Grouping_Tool (suspend group)", err);
}		// Do_SuspendGroups


API_LeaderLineShapeID	GetNextLeaderLineShape (API_LeaderLineShapeID shape)
{
	switch (shape) {
		case API_Segmented:		return API_Splinear;
		case API_Splinear:		return API_SquareRoot;
		case API_SquareRoot:	return API_Segmented;
		default:				DBBREAK (); return API_Segmented;
	}
}


// -----------------------------------------------------------------------------
// Update the position of an associative Label.
// -----------------------------------------------------------------------------

static GSErrCode	UpdateOneLabel (const API_Guid& labelGuid, const API_Coord& newBegC)
{
	if (labelGuid == APINULLGuid)
		return Error;

	API_Element element = {};
	element.header.guid	= labelGuid;

	if (ACAPI_Element_Get (&element) != NoError)
		return Error;

	API_Element mask;
	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, begC);
	ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, midC);
	ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, endC);

	const double dx = newBegC.x - element.label.begC.x;
	const double dy = newBegC.y - element.label.begC.y;

	element.label.begC = newBegC;
	element.label.midC.x += dx;
	element.label.midC.y += dy;
	element.label.endC.x += dx;
	element.label.endC.y += dy;

	if (element.label.labelClass == APILblClass_Text) {
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, u.text.loc);
		element.label.u.text.loc.x += dx;
		element.label.u.text.loc.y += dy;
	} else {
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, u.symbol.pos);
		element.label.u.symbol.pos.x += dx;
		element.label.u.symbol.pos.y += dy;
	}

	if (ACAPI_Element_Change (&element, &mask, nullptr, 0, true) != NoError)
		return Error;

	return NoError;
}		// UpdateOneLabel


// -----------------------------------------------------------------------------
// Update the position of all associative Labels of an element.
// The Labels will be positioned vertically in a row beginning at 'firstBegC'.
// -----------------------------------------------------------------------------

GSErrCode	UpdateAllLabels (const API_Guid& elemGuid, const API_Coord& firstBegC)
{
	GS::Array<API_Guid> connectedLabels;
	if (ACAPI_Grouping_GetConnectedElements (elemGuid, API_LabelID, &connectedLabels) != NoError)
		return Error;

	GSErrCode err	  = NoError;
	API_Coord newBegC = firstBegC;
	for (GS::Array<API_Guid>::ConstIterator it = connectedLabels.Enumerate (); it != nullptr; ++it) {
		if (UpdateOneLabel (*it, newBegC) != NoError)
			err = Error;

		newBegC.y -= 1;
	}

	return err;
}

