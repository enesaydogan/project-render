// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Curtain Wall class header for
//					managing Curtain Wall element types
// *****************************************************************************

#ifndef _CURTAINWALL_H_
#define _CURTAINWALL_H_

#pragma once

#include	"ElementWithMemo.h"
#include	"Ref.hpp"

class Hatch;
class Line;
struct Selection;

class CurtainWall : public ElementWithMemo
{
private:
	static const UInt64	CWmask = APIMemoMask_Polygon | APIMemoMask_CWSegContour | APIMemoMask_CWallFrames;

	bool IsCurrentCWFrameContour (UIndex ii) const;

public:
	CurtainWall ();
	CurtainWall (const API_Elem_Head& elemHead);
	CurtainWall (const CurtainWall&) = delete;
	CurtainWall (CurtainWall&&) = delete;
	CurtainWall& operator= (const CurtainWall&) = delete;
	CurtainWall& operator= (CurtainWall&&) = delete;

	void SetMemoFromHatch (const GS::Ref<Hatch>& hatch, const API_Coord& offset);
	void SetParam (const Selection& selection);
	void SetEmptyPattern ();
	void AddCustomFrames (const GS::Array<GS::Ref<Line>> selectedLines, const Selection& selection);

	virtual UInt64 GetMask () const override;

	GS::Array<Line> GetFrameLines () const;

	static API_AttributeIndex GetFrameLineTypeIndex (const API_CWFrameType& frame);
	static void				  GetCoordsFromFrameLine (const API_CWFrameType& frame, const API_Coord& origo, API_Coord& begRel, API_Coord& endRel);
};

#endif
