// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn SelectionFromCurtainWall class
//					for making Fill/Line objects from Curtain Walls
// *****************************************************************************

#include "SelectionFromCurtainWall.h"

SelectionFromCurtainWall::SelectionFromCurtainWall ()
{
}

GSErrCode SelectionFromCurtainWall::GetSelectedElements ()
{
	return GetSelection ();
}

void SelectionFromCurtainWall::MakeHatchandLines ()
{
	FillCoordsPendsArcs ();
	SetLinesFromFrames ();
}

void SelectionFromCurtainWall::CreateHatchAndLines ()
{
	CallUndoable ();
}

GSErrCode SelectionFromCurtainWall::CreateOrChangeElement ()
{
	GSErrCode error = NoError;
	error = hatch.CreateElementWithMemo ();
	if (error != NoError) return error;
	for (UInt32 ii = 0; ii < lines.GetSize (); ii++)
	{
		error = lines[ii].CreateElement ();
	}
	return error;
}

GSErrCode SelectionFromCurtainWall::FilterTypes (const API_Elem_Head& elemhead, bool& elementFound)
{
	if (elemhead.type == API_CurtainWallID) {
		selectedCurtainWall = new CurtainWall (elemhead);
		elementFound = true;
	} else {
		return APIERR_BADELEMENTTYPE;
	}
	return NoError;
}

GSResID SelectionFromCurtainWall::GetSelectionErrorMessageResID () const
{
	return 2;
}

GSResID SelectionFromCurtainWall::GetUndoableCommandMessageResID () const
{
	return 3;
}

void SelectionFromCurtainWall::FillCoordsPendsArcs ()
{
	SetOffset ();
	hatch.SetMemoFromCurtainWall (selectedCurtainWall, offset);
}

void SelectionFromCurtainWall::SetOffset ()
{
	const auto& curtainWallMemo = selectedCurtainWall->GetMemo ();
	offset = (*curtainWallMemo.coords)[1];
}

void SelectionFromCurtainWall::SetLinesFromFrames ()
{
	lines = selectedCurtainWall->GetFrameLines ();
}

SelectionFromCurtainWall::~SelectionFromCurtainWall ()
{
}

const Hatch& SelectionFromCurtainWall::GetHatch () const
{
	return hatch;
}

GS::Array<GS::Ref<Line>> SelectionFromCurtainWall::GetLines () const
{
	GS::Array<GS::Ref<Line>> refLines;
	GS::Ref<Line> refLine;
	for (UInt32 i = 0; i < lines.GetSize (); ++i)
	{
		refLine = new Line (lines[i]);
		refLines.Push (refLine);
	}
	return refLines;
}


void SelectionFromCurtainWall::SetCW (const API_Elem_Head& cw)
{
	if (cw.type == API_CurtainWallID) {
		selectedCurtainWall = new CurtainWall (cw);
	} else return;
}
