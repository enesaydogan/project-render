// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Line header class for managing
//					Line element types
// *****************************************************************************

#ifndef _LINE_H_
#define _LINE_H_

#pragma once

#include "Element.h"
#include "Ref.hpp"


class CurtainWall;

class Line : public Element
{
private:
	bool IsHiddenLine () const;

public:
	Line ();
	Line (const API_Elem_Head& elemHead);
	Line (const Line&) = default;
	Line (Line&&) = default;
	Line& operator= (const Line&) = default;
	Line& operator= (Line&&) = default;
	virtual ~Line ();

	static bool SortLines (const GS::Ref<Line>& l1, const GS::Ref<Line>& l2);
	static bool CompareBegEnd (const GS::Ref<Line>& Aline, const GS::Ref<Line>& Bline);

	const API_Coord& GetBegC () const;
	const API_Coord& GetEndC () const;
	void SetBegC (const API_Coord& c);
	void SetEndC (const API_Coord& c);

	void SetLTypeID (const API_AttributeIndex& id);

	static void CreateLines (GS::Array<Line>& lines);
	static void DeleteHiddenLines (GS::Array<GS::Ref<Line>>& lines);
};

#endif