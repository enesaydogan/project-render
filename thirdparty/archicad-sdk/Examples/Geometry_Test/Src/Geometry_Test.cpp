// *****************************************************************************
// Source code for the Geometry Test Add-On
// *****************************************************************************

#define	_Geometry_Test_TRANSL_


// ---------------------------------- Includes ---------------------------------

#include "APIEnvir.h"
#include "ACAPinc.h"					// also includes APIdefs.h

// from Geometry
#include "TRANMAT.h"
#include "TM.h"

// ---------------------------------- Types ------------------------------------


// ---------------------------------- Variables --------------------------------


// ---------------------------------- Prototypes -------------------------------


// =============================================================================
//
// Main functions
//
// =============================================================================


static void		Do_Test (void)
{

	ACAPI_CallUndoableCommand ("Geometry Test -- Create elements",
		[&] () -> GSErrCode {
			API_Element		quadrangle [4];
			API_ElementMemo memo;
			Int32			i;
			GSErrCode		err = NoError;

			quadrangle[0] = {};
			quadrangle[0].line.begC.x = -1.0; quadrangle[0].line.begC.y = 1.0;
			quadrangle[1] = {};
			quadrangle[1].line.begC.x = -1.0; quadrangle[1].line.begC.y = -1.0;
			quadrangle[2] = {};
			quadrangle[2].line.begC.x = 1.0; quadrangle[2].line.begC.y = -1.0;
			quadrangle[3] = {};
			quadrangle[3].line.begC.x = 1.0; quadrangle[3].line.begC.y = 1.0;

			quadrangle[0].line.endC = quadrangle[1].line.begC;
			quadrangle[1].line.endC = quadrangle[2].line.begC;
			quadrangle[2].line.endC = quadrangle[3].line.begC;
			quadrangle[3].line.endC = quadrangle[0].line.begC;

			memo = {};

			//create lines
			for (i = 0; i < 4; i++) {

				quadrangle[i].header.type = API_LineID;
				quadrangle[i].header.layer = APIApplicationLayerAttributeIndex;
				quadrangle[i].header.floorInd = 1;

				err = ACAPI_Element_Create (&quadrangle[i], &memo);
				if (err != NoError)
					break;
			}


			API_Element tempLine;
			tempLine.header.type = API_LineID;
			ACAPI_Element_GetDefaults (&tempLine, nullptr);
			tempLine.header.floorInd = 1;


			TRANMAT transformation;
			transformation.status = 0;
			transformation.dummy1 = transformation.dummy2 = transformation.dummy3 = 0;

			transformation = Geometry::IdentityTranMat;
			transformation = TRANMAT::CreateTranslation (2.0, 2.0, 0.0);

			for (i = 0; i < 4; i++) {
				tempLine.line.begC = quadrangle[i].line.begC;
				tempLine.line.endC = quadrangle[i].line.endC;

				Coord tempC1 = Coord (Geometry::TransformUnitNormalVector (transformation, Vector3D (tempLine.line.begC.x, tempLine.line.begC.y, 0)));
				Coord tempC2 = Coord (Geometry::TransformUnitNormalVector (transformation, Vector3D (tempLine.line.endC.x, tempLine.line.endC.y, 0)));

				tempLine.line.begC.x = tempC1.x;
				tempLine.line.begC.y = tempC1.y;

				tempLine.line.endC.x = tempC2.x;
				tempLine.line.endC.y = tempC2.y;

				err = ACAPI_Element_Create (&tempLine, &memo);
				if (err != NoError)
					break;
			}

			return err;
		});

	return;
}		/* Do_Test */

// -----------------------------------------------------------------------------
// Entry points to handle ARCHICAD events
//
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams *params)
{
	switch (params->menuItemRef.itemIndex) {
		case 1:		Do_Test ();				break;
	}

	return NoError;
}		// DoCommand


// =============================================================================
//
// Required functions
//
// =============================================================================


//------------------------------------------------------
// Dependency definitions
//------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, 32000, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, 32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		/* CheckEnvironment */


//------------------------------------------------------
// Interface definitions
//------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	ACAPI_MenuItem_RegisterMenu (32500, 0, MenuCode_UserDef, MenuFlag_Default);

	return NoError;
}		/* RegisterInterface */


//------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
//------------------------------------------------------
GSErrCode Initialize	(void)
{
	GSErrCode err = ACAPI_MenuItem_InstallMenuHandler (32500, MenuCommandHandler);
	if (err != NoError)
		DBPrintf ("Geometry_Test:: Initialize() ACAPI_MenuItem_InstallMenuHandler failed\n");

	return err;
}		/* Initialize */


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}		// FreeData
