// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn SelectionHandler class for
//					managing selections. Base for the drawer modifier classes
// *****************************************************************************

#include  "SelectionHandler.h"
#include  "APICommon.h"

GSErrCode SelectionHandler::GetSelectedHeads (const GS::Array<API_Neig>& selNeigs)
{
	bool elementFound = false;
	for (const API_Neig& neig : selNeigs) {
		API_Elem_Head elemHead = {};
		elemHead.guid = neig.guid;
		elemHead.type = Neig_To_ElemID (neig.neigID);
		GSErrCode err = FilterTypes (elemHead, elementFound);
		if (err != NoError)
			return err;
	}
	if (!elementFound)
		return APIERR_BADELEMENTTYPE;
	return NoError;
}

GSErrCode SelectionHandler::GetSelection ()
{
	API_SelectionInfo	selectionInfo;
	GS::Array<API_Neig>	selNeigs;

	GSErrCode err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, false);
	BMKillHandle ((GSHandle*)&selectionInfo.marquee.coords);
	if (err != NoError)
		return err;

	err = GetSelectedHeads (selNeigs);
	if (err != NoError)
		ACAPI_WriteReport (RSGetIndString (33500, GetSelectionErrorMessageResID (), ACAPI_GetOwnResModule ()), true);

	return err;
}

SelectionHandler::SelectionHandler ()
{
}

SelectionHandler::~SelectionHandler ()
{
}

