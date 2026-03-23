// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Drawer class header base for
//					the drawer modifier classes
// *****************************************************************************

#ifndef _DRAWER_H_
#define _DRAWER_H_

#pragma once

#include "SelectionHandler.h"

class Drawer : public SelectionHandler
{
protected:
	void CallUndoable ();

	virtual GSResID GetUndoableCommandMessageResID () const = 0;
	virtual GSErrCode CreateOrChangeElement () = 0;

public:
	Drawer ();
	virtual ~Drawer ();
};

#endif

