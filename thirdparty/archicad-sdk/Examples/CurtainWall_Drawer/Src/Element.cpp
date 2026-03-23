// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Element class base for elements
//					used in the drawer modifier classes
// *****************************************************************************

#include "Element.h"

void Element::GetElementDefaults (Element& element)
{
	GSErrCode error = NoError;
	error = ACAPI_Element_GetDefaults (&(element.element), nullptr);
	if (error != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetDefaults[%ld]", false, error);
		return;
	}
}

GSErrCode Element::GetElementInstance (Element& element)
{
	GSErrCode err1 = ACAPI_Element_Get (&element.element);
	return err1;
}

Element::Element (const API_ElemType& elemType)
{
	element.header.type = elemType;
	GetElementDefaults (*this);
}

Element::Element (const API_Elem_Head& elemHead)
{
	this->element.header = elemHead;
	GSErrCode err = GetElementInstance (*this);
	if (err != NoError)
		ACAPI_WriteReport ("GetSelectedElement", false, err);
}

Element::~Element ()
{
}

bool Element::IsNear (const double& r1, const double& r2, double eps /* = EPS */)
{
	return fabs (r1 - r2) <= eps;
}

bool Element::IsNearCoord (const API_Coord& c1, const API_Coord& c2)
{
	return IsNear (c1.x, c2.x) && IsNear (c1.y, c2.y);
}


const API_Elem_Head& Element::GetElemHead ()
{
	return element.header;
}

GSErrCode Element::CreateElement ()
{
	return ACAPI_Element_Create (&element, nullptr);
}

