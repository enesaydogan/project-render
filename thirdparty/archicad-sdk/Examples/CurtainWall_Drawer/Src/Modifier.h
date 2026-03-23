// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Modifier class header base for
//					the modifier classes
// *****************************************************************************

#ifndef _MODIFIER_H_
#define _MODIFIER_H_

#pragma once

#include "SelectionFromCurtainWall.h"
#include "Ref.hpp"

class Hatch;
class Line;

class Modifier : public Drawer
{
private:
	bool HatchIsChanged () const;
	bool LineIsChanged () const;

	virtual void DoTheModification () = 0;

protected:
	SelectionFromCurtainWall	selectionFromCW;
	SelectedElementHeads		selection = {};
	GS::Ref<Hatch>				selectedHatch;
	GS::Array<GS::Ref<Line>>	selectedLines;
	bool						isHatchFound = false;
	bool						isCWFound = false;

	virtual GSErrCode FilterTypes (const API_Elem_Head& elemhead, bool& elementFound) override;
	virtual GSResID GetSelectionErrorMessageResID () const override;
	bool IsElementLinked ();

	void GetHatchElementFromHead ();
	void GetLineElementsFromHeads ();
	void MakeHatchAndLinesFromCW ();

public:
	Modifier ();
	virtual ~Modifier ();

	virtual GSErrCode GetSelectedElements () override;
	void ModifyIfChanged ();
};

#endif

