// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Drawer class base for the drawer
//					modifier classes
// *****************************************************************************

#include  "Drawer.h"
#include  "APICommon.h"

void Drawer::CallUndoable ()
{
	GSErrCode err = ACAPI_CallUndoableCommand (RSGetIndString (33000, GetUndoableCommandMessageResID (), ACAPI_GetOwnResModule ()),
									 [&] () -> GSErrCode {
											return CreateOrChangeElement ();
										});
	if (err != NoError)
		ACAPI_WriteReport ("ACAPI_Element_Create [%ld]", false, err);
}

Drawer::Drawer ()
{
}

Drawer::~Drawer ()
{
}

