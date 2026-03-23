// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn SelectionFromCurtainWall class
//					header for making Fill/Line objects from Curtain Walls
// *****************************************************************************

#ifndef _SELECTIONFROMCURTAINWALL_H_
#define _SELECTIONFROMCURTAINWALL_H_

#pragma once
#include "Drawer.h"
#include "Hatch.h"
#include "Line.h"
#include "CurtainWall.h"

class SelectionFromCurtainWall : public Drawer
{
private:
	GS::Ref<CurtainWall>	selectedCurtainWall;
	Hatch					hatch;
	GS::Array<Line>			lines;
	API_Coord				offset;

	virtual GSErrCode FilterTypes (const API_Elem_Head& elemhead, bool& elementFound) override;
	virtual GSResID GetSelectionErrorMessageResID () const override;
	virtual GSResID GetUndoableCommandMessageResID () const override;
	virtual GSErrCode CreateOrChangeElement () override;

	void FillCoordsPendsArcs ();
	void SetOffset ();
	void SetLinesFromFrames ();
public:
	SelectionFromCurtainWall ();
	~SelectionFromCurtainWall ();

	void MakeHatchandLines ();
	void CreateHatchAndLines ();

	const Hatch& GetHatch () const;
	GS::Array<GS::Ref<Line>> GetLines () const;

	virtual GSErrCode GetSelectedElements () override;

	void SetCW (const API_Elem_Head& cw);
};

#endif
