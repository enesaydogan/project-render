// *****************************************************************************
// Source code for the Element Test Add-On
// *****************************************************************************

#define	_ELEMENT_TIPS_TRANSL_

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"Element_Test.h"

static const Int32 ElementNumberToCreate = 100;

void Do_CreateWallsSeparateUndoSteps ()
{
	API_Element wallElement = {};
	wallElement.header.type = API_ElemTypeID::API_WallID;
	ACAPI_Element_GetDefaults (&wallElement, nullptr);

	for (Int32 i = 0; i < ElementNumberToCreate; i++) {
		ACAPI_CallUndoableCommand ("Create Element", [&] () -> GSErrCode {
			wallElement.wall.begC = { (double) i, 0.0 };
			wallElement.wall.endC = { (double) i, 2.0 };
			ACAPI_Element_Create (&wallElement, nullptr);
			return NoError;
		});
	}
}

void Do_CreateWallsOneUndoSteps ()
{
	API_Element wallElement = {};
	wallElement.header.type = API_ElemTypeID::API_WallID;
	ACAPI_Element_GetDefaults (&wallElement, nullptr);

	ACAPI_CallUndoableCommand ("Create Element", [&] () -> GSErrCode {
		for (Int32 i = 0; i < ElementNumberToCreate; i++) {
			wallElement.wall.begC = { (double) i, 0.0 };
			wallElement.wall.endC = { (double) i, 2.0 };
			ACAPI_Element_Create (&wallElement, nullptr);
		}
		return NoError;
	});
}
