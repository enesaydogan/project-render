// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn CurtainWallFromSelection class
//					header for making Curtain Walls from Fill/Line objects
// *****************************************************************************

#ifndef _CURTAINWALLFROMSELECTION_H_
#define _CURTAINWALLFROMSELECTION_H_

#pragma once

#include "Drawer.h"
#include "Line.h"
#include "CurtainWall.h"
#include "Hatch.h"
#include "FixArray.hpp"

class CurtainWallFromSelection : public Drawer
{
private:
	Selection					selection = {};
	GS::Ref<Hatch>				selectedHatch;
	GS::Array<GS::Ref<Line>>	selectedLines;

	CurtainWall		curtainWall;
	API_Coord		offset;

	virtual GSErrCode FilterTypes (const API_Elem_Head& elemhead, bool& elementFound) override;
	virtual GSResID GetSelectionErrorMessageResID () const override;
	virtual GSResID GetUndoableCommandMessageResID () const override;
	virtual GSErrCode CreateOrChangeElement () override;

	void GetHatchElementFromHead ();
	void GetLineElementsFromHeads ();
	void SetSelectionGeometry ();
	void SetOffset ();
	void SetCurtainWallBase ();
	void AddCustomFrames ();

public:
	CurtainWallFromSelection ();
	~CurtainWallFromSelection ();

	void InitializeCurtainWall ();
	void CreateCurtainWall ();

	void SetSelectedHatch (const API_Elem_Head& hatch);
	void SetSelectedLines (const GS::Array<API_Elem_Head>& lines);

	const CurtainWall& GetCW () const;

	virtual GSErrCode GetSelectedElements () override;

	static GS::FixArray<API_Coord, 2> GetFrameRelCoordsFromLine (const GS::Ref<Line>& line, const API_Coord& origo, const Selection& selection);
};

#endif