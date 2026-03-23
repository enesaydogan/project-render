// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn ModifyHatchAndLines class header
//					for modifying Fill/Line objects from Curtain Walls
// *****************************************************************************

#ifndef _MODIFYHATCHANDLINES_H_
#define _MODIFYHATCHANDLINES_H_

#pragma once
#include "Modifier.h"

class ModifyHatchAndLines : public Modifier
{
private:
	void ModifyHatchandLines ();
	virtual void DoTheModification () override;

public:
	ModifyHatchAndLines ();
	~ModifyHatchAndLines ();

	virtual GSErrCode CreateOrChangeElement () override;
	virtual GSResID GetUndoableCommandMessageResID () const override;
};

#endif

