// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn ModifyCurtainWall class header
//					for modifying Curtain Walls from Fill/Line objects
// *****************************************************************************

#ifndef _MODIFYCWFROMHATCH_H_
#define _MODIFYCWFROMHATCH_H_

#pragma once
#include "Modifier.h"
#include "CurtainWallFromSelection.h"

class ModifyCurtainWall : public Modifier
{
private:
	CurtainWallFromSelection	cwFromSelection;
	GS::Ref<CurtainWall>		selectedCurtainWall;

	void ModifyCW ();
	virtual void DoTheModification () override;

public:
	ModifyCurtainWall ();
	~ModifyCurtainWall ();

	virtual GSErrCode CreateOrChangeElement () override;
	virtual GSResID GetUndoableCommandMessageResID () const override;
};

#endif

