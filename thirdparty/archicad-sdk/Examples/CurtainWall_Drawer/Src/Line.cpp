// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Line class for managing
//					Line element types
// *****************************************************************************

#include "Line.h"
#include "CurtainWall.h"
#include "SelectionFromCurtainWall.h"
#include "LineTypeIndexes.h"

bool Line::IsHiddenLine () const
{
	return element.line.ltypeInd == LineTypeIndexes::GetInstance ().GetHiddenLineType ();
}

Line::Line () :
	Element (API_LineID)
{
}

Line::Line (const API_Elem_Head& elemHead) :
	Element (elemHead)
{
}

Line::~Line ()
{
}

bool Line::SortLines (const GS::Ref<Line>& l1, const GS::Ref<Line>& l2) {
	if (IsNear (l1->element.line.begC.x, l2->element.line.begC.x))
		return l1->element.line.begC.y > l2->element.line.begC.y;
	return l1->element.line.begC.x > l2->element.line.begC.x;
}

bool Line::CompareBegEnd (const GS::Ref<Line>& Aline, const GS::Ref<Line>& Bline)
{
	return !IsNearCoord (Aline->element.line.begC, Bline->element.line.begC) ||
		!IsNearCoord (Aline->element.line.endC, Bline->element.line.endC);
}

const API_Coord& Line::GetBegC () const
{
	return element.line.begC;
}

const API_Coord& Line::GetEndC () const
{
	return element.line.endC;
}

void Line::SetBegC (const API_Coord& c)
{
	element.line.begC = c;
}

void Line::SetEndC (const API_Coord& c)
{
	element.line.endC = c;
}

void Line::SetLTypeID (const API_AttributeIndex& id)
{
	element.line.ltypeInd = id;
}

void Line::CreateLines (GS::Array<Line>& lines)
{
	GSErrCode err = NoError;
	err = ACAPI_CallUndoableCommand (RSGetIndString (32001, 2, ACAPI_GetOwnResModule ()),
									 [&] () -> GSErrCode {
		GSErrCode error = NoError;
		for (UInt32 ii = 0; ii < lines.GetSize (); ii++)
		{
			error = ACAPI_Element_Create (&lines[ii].element, nullptr);
		}
		return error;
	});
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Create (Lines) [%ld]", false, err);
		return;
	}
}

void Line::DeleteHiddenLines (GS::Array<GS::Ref<Line>>& lines)
{
	for (UInt32 k = lines.GetSize (); k > 0; --k)
		if ((lines[k - 1])->IsHiddenLine ())
			lines.Delete (k - 1);
}