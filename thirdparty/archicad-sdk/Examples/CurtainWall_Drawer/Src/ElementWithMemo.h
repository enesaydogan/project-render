// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn ElementWithMemo header class
//					base for elements with memo used in the drawer modifier classes
// *****************************************************************************

#ifndef _ELEMENTWITHMEMO_H_
#define _ELEMENTWITHMEMO_H_

#pragma once

#include "Element.h"

class ElementWithMemo : public Element
{
protected:
	API_ElementMemo	memo = {};

	void CopyCoords (const API_Polygon& polygon, API_Coord** a, API_Coord** b);
	void CopyPends (const API_Polygon& polygon, Int32** a, Int32** b);
	void CopyArcs (const API_Polygon& polygon, API_PolyArc** a, API_PolyArc** b);
	void CopyVertexIDS (const API_Polygon& polygon, UInt32** a, UInt32** b);
	void OffsetCoords (const API_Polygon& polygon, const API_Coord& offset, API_Coord** b);

	void GetDefaultsWithMemo (ElementWithMemo& element);
	void GetElementWithMemo (const UInt64& mask, ElementWithMemo& element);

	virtual UInt64 GetMask () const = 0;

public:
	ElementWithMemo (API_ElemTypeID elemType);
	ElementWithMemo (const API_Elem_Head& elemHead);
	ElementWithMemo (const ElementWithMemo&) = delete;
	ElementWithMemo (ElementWithMemo&&) = delete;
	ElementWithMemo& operator= (const ElementWithMemo&) = delete;
	ElementWithMemo& operator= (ElementWithMemo&&) = delete;
	virtual ~ElementWithMemo ();

	const API_ElementMemo& GetMemo () const;

	GSErrCode CreateElementWithMemo ();
	GSErrCode ChangeElementWithMemo (const ElementWithMemo& newElem);
};

#endif