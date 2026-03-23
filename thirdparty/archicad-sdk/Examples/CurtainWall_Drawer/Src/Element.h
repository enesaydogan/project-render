// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Element class header base for
//					elements used in the drawer modifier classes
// *****************************************************************************

#ifndef _ELEMENT_H_
#define _ELEMENT_H_

#pragma once

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

class Element
{
protected:
	API_Element	element = {};

	static void GetElementDefaults (Element& element);
	static GSErrCode GetElementInstance (Element& element);

public:
	Element (const API_ElemType& elemType);
	Element (const API_Elem_Head& elemHead);
	Element (const Element&) = default;
	Element (Element&&) = default;
	Element& operator= (const Element&) = default;
	Element& operator= (Element&&) = default;
	virtual ~Element ();

	static bool IsNear (const double& r1, const double& r2, double eps = EPS);
	static bool IsNearCoord (const API_Coord& c1, const API_Coord& c2);

	const API_Elem_Head& GetElemHead ();

	GSErrCode CreateElement ();
};

#endif


