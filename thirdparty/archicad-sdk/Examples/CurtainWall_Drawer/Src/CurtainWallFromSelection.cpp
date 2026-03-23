// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn CurtainWallFromSelection class
//					for making Curtain Walls from Fill/Line objects
// *****************************************************************************

#include	"CurtainWallFromSelection.h"

CurtainWallFromSelection::CurtainWallFromSelection ()
{
}

GSErrCode CurtainWallFromSelection::GetSelectedElements ()
{
	GSErrCode err = GetSelection ();
	if (err == NoError) {
		GetHatchElementFromHead ();
		GetLineElementsFromHeads ();
	}
	return err;
}

void CurtainWallFromSelection::InitializeCurtainWall ()
{
	SetSelectionGeometry ();
	SetCurtainWallBase ();
	AddCustomFrames ();
}

void CurtainWallFromSelection::CreateCurtainWall ()
{
	CallUndoable ();
}

GSErrCode CurtainWallFromSelection::CreateOrChangeElement ()
{
	return curtainWall.CreateElementWithMemo ();
}


GSErrCode CurtainWallFromSelection::FilterTypes (const API_Elem_Head& elemhead, bool& elementFound)
{
	if (elemhead.type == API_LineID) {
		selection.elemHeads.selectedLineHeads.Push (elemhead);
	} else if (elemhead.type == API_HatchID) {
		selection.elemHeads.selectedHatchHead = elemhead;
		elementFound = true;
	} else {
		return APIERR_BADELEMENTTYPE;
	}
	return NoError;
}

GSResID CurtainWallFromSelection::GetSelectionErrorMessageResID () const
{
	return 1;
}

GSResID CurtainWallFromSelection::GetUndoableCommandMessageResID () const
{
	return 1;
}

void CurtainWallFromSelection::GetHatchElementFromHead ()
{
	selectedHatch = new Hatch (selection.elemHeads.selectedHatchHead);
}

void CurtainWallFromSelection::GetLineElementsFromHeads ()
{
	const auto& lineHeads = selection.elemHeads.selectedLineHeads;
	for (UInt32 k = 0; k < lineHeads.GetSize (); ++k)
		selectedLines.Push (new Line (lineHeads[k]));
}

void CurtainWallFromSelection::SetSelectionGeometry ()
{
	const auto& hatchMemo = selectedHatch->GetMemo ();
	selection.geometry.minX = (*hatchMemo.coords)[1].x;
	selection.geometry.minY = (*hatchMemo.coords)[1].y;
	selection.geometry.maxX = selection.geometry.minX;
	selection.geometry.maxY = selection.geometry.minY;
	const API_Polygon& poly = selectedHatch->GetHatchPoly ();
	for (Int32 ii = 1; ii < poly.nCoords + 1; ++ii) {
		const API_Coord& coord = (*hatchMemo.coords)[ii];
		if (selection.geometry.minX > coord.x)
			selection.geometry.minX = coord.x;
		if (selection.geometry.minY > coord.y)
			selection.geometry.minY = coord.y;
		if (selection.geometry.maxX < coord.x)
			selection.geometry.maxX = coord.x;
		if (selection.geometry.maxY < coord.y)
			selection.geometry.maxY = coord.y;
	}
	selection.geometry.width = selection.geometry.maxX - selection.geometry.minX;
	selection.geometry.height = selection.geometry.maxY - selection.geometry.minY;
}

void CurtainWallFromSelection::SetOffset ()
{
	offset.x = (-1) * selection.geometry.minX;
	offset.y = (-1) * selection.geometry.minY;
}

void CurtainWallFromSelection::SetCurtainWallBase ()
{
	SetOffset ();
	curtainWall.SetMemoFromHatch (selectedHatch, offset);
	curtainWall.SetParam (selection);
	curtainWall.SetEmptyPattern ();
}

void CurtainWallFromSelection::AddCustomFrames ()
{
	Line::DeleteHiddenLines (selectedLines);
	curtainWall.AddCustomFrames (selectedLines, selection);
}

CurtainWallFromSelection::~CurtainWallFromSelection ()
{
}

void CurtainWallFromSelection::SetSelectedHatch (const API_Elem_Head& hatch)
{
	selection.elemHeads.selectedHatchHead = hatch;
	GetHatchElementFromHead ();
}

void CurtainWallFromSelection::SetSelectedLines (const GS::Array<API_Elem_Head>& lines)
{
	selection.elemHeads.selectedLineHeads = lines;
	GetLineElementsFromHeads ();
}

const CurtainWall& CurtainWallFromSelection::GetCW () const
{
	return curtainWall;
}

GS::FixArray<API_Coord, 2> CurtainWallFromSelection::GetFrameRelCoordsFromLine (const GS::Ref<Line>& line, const API_Coord& origo, const Selection& selection)
{
	API_Coord begRel, endRel;
	begRel.x = (line->GetBegC ().x - origo.x) / selection.geometry.width;
	endRel.x = (line->GetEndC ().x - origo.x) / selection.geometry.width;
	begRel.y = (line->GetBegC ().y - origo.y) / selection.geometry.height;
	endRel.y = (line->GetEndC ().y - origo.y) / selection.geometry.height;

	return { begRel, endRel };
}