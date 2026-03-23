// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Hatch class header for managing
//					Hatch element types
// *****************************************************************************

#ifndef _HATCH_H_
#define _HATCH_H_

#pragma once

#include "ElementWithMemo.h"
#include "Ref.hpp"

class CurtainWall;

class Hatch : public ElementWithMemo
{
private:
public:
	Hatch ();
	Hatch (const API_Elem_Head& elemHead);
	Hatch (const Hatch&) = delete;
	Hatch (Hatch&&) = delete;
	Hatch& operator= (const Hatch&) = delete;
	Hatch& operator= (Hatch&&) = delete;

	virtual UInt64 GetMask () const override;

	const API_Polygon& GetHatchPoly () const;

	bool IsHatchCoordsMatch (const GS::Ref<Hatch>& selectedHatch) const;

	void SetMemoFromCurtainWall (const GS::Ref<CurtainWall>& curtainWall, const API_Coord& offset);
};

#endif