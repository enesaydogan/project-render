// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Modifier class base for the
//					modifier classes
// *****************************************************************************

#include "Modifier.h"
#include "Algorithms.hpp"
#include "SelectionFromCurtainWall.h"
#include "CurtainWallFromSelection.h"

GSErrCode Modifier::FilterTypes (const API_Elem_Head& elemhead, bool& elementFound)
{
	if (elemhead.type == API_LineID) {
		selection.selectedLineHeads.Push (elemhead);
	} else if (elemhead.type == API_HatchID) {
		selection.selectedHatchHead = elemhead;
		isHatchFound = true;
	} else if (elemhead.type == API_CurtainWallID) {
		selection.selectedCWHead = elemhead;
		isCWFound = true;
	} else {
		return APIERR_BADELEMENTTYPE;
	}
	if (isHatchFound && isCWFound) {
		elementFound = true;
	}
	return NoError;
}

GSResID Modifier::GetSelectionErrorMessageResID () const
{
	return 3;
}

void Modifier::GetHatchElementFromHead ()
{
	selectedHatch = new Hatch (selection.selectedHatchHead);
}

void Modifier::GetLineElementsFromHeads ()
{
	for (const auto& head : selection.selectedLineHeads)
		selectedLines.Push (new Line (head));
	Line::DeleteHiddenLines (selectedLines);
	GS::Sort (selectedLines.Begin (), selectedLines.End (), Line::SortLines);
}

void Modifier::MakeHatchAndLinesFromCW ()
{
	selectionFromCW.SetCW (selection.selectedCWHead);
	selectionFromCW.MakeHatchandLines ();
}

bool Modifier::HatchIsChanged () const
{
	if (selectionFromCW.GetHatch ().GetHatchPoly ().nCoords != selectedHatch->GetHatchPoly ().nCoords)
		return true;

	return selectionFromCW.GetHatch ().IsHatchCoordsMatch (selectedHatch);
}

bool Modifier::LineIsChanged () const
{
	auto linesFromCW = selectionFromCW.GetLines ();
	Line::DeleteHiddenLines (linesFromCW);
	GS::Sort (linesFromCW.Begin (), linesFromCW.End (), Line::SortLines);

	if (selectedLines.GetSize () != linesFromCW.GetSize ())
		return true;

	for (UInt32 k = 0; k < linesFromCW.GetSize (); ++k) {
		if (Line::CompareBegEnd (selectedLines[k], linesFromCW[k]))
			return true;
	}
	return false;
}

GSErrCode Modifier::GetSelectedElements ()
{
	GSErrCode err = GetSelection ();
	if (err == NoError) {
		GetHatchElementFromHead ();
		GetLineElementsFromHeads ();
		MakeHatchAndLinesFromCW ();
	}
	return err;
}

void Modifier::ModifyIfChanged ()
{
	if (HatchIsChanged () || LineIsChanged ())
		DoTheModification ();
	else
		ACAPI_WriteReport (RSGetIndString (33500, 4, ACAPI_GetOwnResModule ()), true);
}

Modifier::Modifier ()
{
}

Modifier::~Modifier ()
{
}
