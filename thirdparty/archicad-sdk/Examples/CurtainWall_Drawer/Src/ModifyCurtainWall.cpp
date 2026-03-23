// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn ModifyCurtainWall class
//					for modifying Curtain Walls from Fill/Line objects
// *****************************************************************************

#include "ModifyCurtainWall.h"
#include "CurtainWallFromSelection.h"

ModifyCurtainWall::ModifyCurtainWall ()
{
}

void ModifyCurtainWall::DoTheModification ()
{
	ModifyCW ();
}

void ModifyCurtainWall::ModifyCW ()
{
	cwFromSelection.SetSelectedLines (selection.selectedLineHeads);
	cwFromSelection.SetSelectedHatch (selectedHatch->GetElemHead ());
	cwFromSelection.InitializeCurtainWall ();

	selectedCurtainWall = new CurtainWall (selection.selectedCWHead);
	CallUndoable ();
}

GSErrCode ModifyCurtainWall::CreateOrChangeElement ()
{
	return selectedCurtainWall->ChangeElementWithMemo (cwFromSelection.GetCW ());
}

GSResID ModifyCurtainWall::GetUndoableCommandMessageResID () const
{
	return 2;
}

ModifyCurtainWall::~ModifyCurtainWall ()
{
}
