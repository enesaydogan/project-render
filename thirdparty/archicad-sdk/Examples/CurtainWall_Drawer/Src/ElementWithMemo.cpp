// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn ElementWithMemo class base for
//					elements with memo used in the drawer modifier classes
// *****************************************************************************

#include "ElementWithMemo.h"

void ElementWithMemo::CopyCoords (const API_Polygon& polygon, API_Coord** a, API_Coord** b)
{
	for (Int32 k = 1; k < polygon.nCoords + 1; ++k) {
		(*b)[k] = (*a)[k];
	}
}

void ElementWithMemo::CopyPends (const API_Polygon& polygon, Int32** a, Int32** b)
{
	for (Int32 k = 1; k < polygon.nSubPolys + 1; ++k) {
		(*b)[k] = (*a)[k];
	}
}

void ElementWithMemo::CopyArcs (const API_Polygon& polygon, API_PolyArc** a, API_PolyArc** b)
{
	for (Int32 k = 0; k < polygon.nArcs; ++k) {
		(*b)[k] = (*a)[k];
	}
}

void ElementWithMemo::CopyVertexIDS (const API_Polygon& polygon, UInt32** a, UInt32** b)
{
	for (Int32 k = 0; k < polygon.nCoords; ++k) {
		(*b)[k] = (*a)[k];
	}
}

void ElementWithMemo::OffsetCoords (const API_Polygon& polygon, const API_Coord& offset, API_Coord** b)
{
	for (Int32 k = 1; k < polygon.nCoords + 1; ++k) {
		(*b)[k].x += offset.x;
		(*b)[k].y += offset.y;
	}
}

void ElementWithMemo::GetDefaultsWithMemo (ElementWithMemo& element)
{
	GSErrCode err = NoError;
	err = ACAPI_Element_GetDefaults (&(element.element), &(element.memo));
	if (err != NoError)
		ACAPI_WriteReport ("ACAPI_Element_GetDefaults[%ld]", false, err);
}

void ElementWithMemo::GetElementWithMemo (const UInt64& mask, ElementWithMemo& element)
{
	GSErrCode err = NoError;

	err = ACAPI_Element_Get (&element.element);
	if (err != NoError)
		ACAPI_WriteReport ("ACAPI_Element_Get[%ld]", false, err);
	if (err != NoError)
		return;

	err = ACAPI_Element_GetMemo (element.element.header.guid, &element.memo, mask);
	if (err != NoError)
		ACAPI_WriteReport ("ACAPI_Element_GetMemo[%ld]", false, err);
}

ElementWithMemo::ElementWithMemo (API_ElemTypeID elemType) :
	Element (elemType)
{
	GetDefaultsWithMemo (*this);
}

ElementWithMemo::ElementWithMemo (const API_Elem_Head& elemHead) :
	Element (elemHead)
{
}


ElementWithMemo::~ElementWithMemo ()
{
	ACAPI_DisposeElemMemoHdls (&memo);
}

const API_ElementMemo& ElementWithMemo::GetMemo () const
{
	return memo;
}

GSErrCode ElementWithMemo::CreateElementWithMemo ()
{
	return ACAPI_Element_Create (&element, &memo);
}

GSErrCode ElementWithMemo::ChangeElementWithMemo (const ElementWithMemo& newElem)
{
	return ACAPI_Element_Change (&element, &newElem.element, &newElem.memo, newElem.GetMask (), true);
}