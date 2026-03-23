// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn ModifyHatchAndLines class
//					for modifying Fill/Line objects from Curtain Walls
// *****************************************************************************

#include "ModifyHatchAndLines.h"
#include "Hatch.h"

ModifyHatchAndLines::ModifyHatchAndLines ()
{
}

void ModifyHatchAndLines::DoTheModification ()
{
	ModifyHatchandLines ();
}

void ModifyHatchAndLines::ModifyHatchandLines ()
{
	CallUndoable ();
}

GSErrCode ModifyHatchAndLines::CreateOrChangeElement ()
{
	GSErrCode error = NoError;
	error = selectedHatch->ChangeElementWithMemo (selectionFromCW.GetHatch ());
	if (error != NoError) return error;
	for (UInt32 ii = 0; ii < selectedLines.GetSize (); ii++)
	{
		GS::Array<API_Guid> elemGuids;
		for (const auto& head : selection.selectedLineHeads)
			elemGuids.Push (head.guid);
		error = ACAPI_Element_Delete (elemGuids);
	}
	if (error != NoError) return error;
	for (auto& line : selectionFromCW.GetLines ())
	{
		error = line->CreateElement ();
	}
	return error;
}

GSResID ModifyHatchAndLines::GetUndoableCommandMessageResID () const
{
	return 4;
}

ModifyHatchAndLines::~ModifyHatchAndLines ()
{
}
