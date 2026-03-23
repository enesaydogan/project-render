#include "APIEnvir.h"
#include "ACAPinc.h"
#include "Model3DViewerDialog.hpp"
#include "FixedPerspectiveCamera.hpp"
#include "SimplePerspectiveCamera.hpp"


static constexpr short AddOnResID = 32000;
static constexpr short MenuResID = 32500;


static Modeler::Model3DPtr CreatePredefinedModel ()
{
	GS::UniString script = "GROUP \"group_1\"\n"
		"\tXFORM 1.0, 0.0, 0.0, 10.0, 0.0, 1.0, 0.0, 8.0, 0.0, 0.0, 1.0, 0.0 \n"
		"\tBLOCK 2.0, 2.0, 2.0\n"
		"\tDEL 1\n"
		"ENDGROUP\n"
		"group_var_1 = CREATEGROUPWITHMATERIAL (\"group_1\", 3, 1, 48)\n"
		"PLACEGROUP group_var_1\n"
		"GROUP \"group_2\"\n"
		"\tXFORM 1.0, 0.0, 0.0, 12.0, 0.0, 1.0, 0.0, 6.0, 0.0, 0.0, 1.0, 0.0\n"
		"\tBLOCK 2.000000, 2.000000, 2.000000\n"
		"\tDEL 1\n"
		"ENDGROUP\n"
		"group_var_2 = CREATEGROUPWITHMATERIAL (\"group_2\", 3, 1, 3)\n"
		"PLACEGROUP group_var_2\n"
		"GROUP \"group_3\"\n"
		"\tXFORM 1.0, 0.0, 0.0, 11.5, 0.0, 1.0, 0.0, 7.5, 0.0, 0.0, 1.0, 2.0\n"
		"\tBLOCK 2.0, 2.0, 2.0\n"
		"\tDEL 1\n"
		"ENDGROUP\n"
		"group_var_3 = CREATEGROUPWITHMATERIAL (\"group_3\", 3, 1, 70)\n"
		"PLACEGROUP group_var_3\n"
		"GROUP \"group_4\"\n"
		"\tXFORM 1.0, 0.0, 0.0, 0.0, 0.0, 1.0, 0.0, 4.0, 0.0, 0.0, 1.0, 0.5\n"
		"\tSPHERE 3.0\n"
		"\tDEL 1\n"
		"ENDGROUP\n"
		"group_var_4 = CREATEGROUPWITHMATERIAL (\"group_4\", 2, 0, 29)\n"
		"PLACEGROUP group_var_4";

	Modeler::Model3DPtr model;
	GSErrCode err = ACAPI_LibraryManagement_InterpretGDLScript3D (&script, nullptr, nullptr, &model);
	if (DBERROR (err != NoError)) {
		return nullptr;
	}

	return model;
}


static ModelViewer::ICameraControllerRef CreateFixedCamera ()
{
	return GS::NewRef<ModelViewer::FixedPerspectiveCamera> (Geometry::Point3D (-8.0, -12.0, 8.0), Geometry::Point3D (0.0, 0.0, 2.0), 45.0);
}


static ModelViewer::ICameraControllerRef CreateSimpleCamera ()
{
	return GS::NewRef<ModelViewer::SimplePerspectiveCamera> (Geometry::Point3D (-8.0, -12.0, 8.0), Geometry::Point3D (0.0, 0.0, 2.0), 45.0);
}


static void ShowTestDialogWithFixedCamera ()
{
	const auto model = CreatePredefinedModel ();
	if (DBERROR (model == nullptr)) {
		return;
	}

	Model3DViewerTestDialog (model, CreateFixedCamera (), false).Invoke ();
}


static void ShowTestDialogWithSimpleCamera ()
{
	const auto model = CreatePredefinedModel ();
	if (DBERROR (model == nullptr)) {
		return;
	}

	Model3DViewerTestDialog (model, CreateSimpleCamera (), true).Invoke ();
}


// -----------------------------------------------------------------------------
// MenuCommandHandler
//		called to perform the user-asked command
// -----------------------------------------------------------------------------

GSErrCode MenuCommandHandler (const API_MenuParams* menuParams)
{
	switch (menuParams->menuItemRef.menuResID) {
		case MenuResID: {
			switch (menuParams->menuItemRef.itemIndex) {
				case 1: ShowTestDialogWithFixedCamera (); break;
				case 2: ShowTestDialogWithSimpleCamera (); break;
			}
			break;
		}
	}

	return NoError;
}		// MenuCommandHandler


// =============================================================================
//
// Required functions
//
// =============================================================================

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------

API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, AddOnResID, 1, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, AddOnResID, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}		// CheckEnvironment


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------

GSErrCode RegisterInterface (void)
{
	return ACAPI_MenuItem_RegisterMenu (MenuResID, 0, MenuCode_UserDef, MenuFlag_SeparatorBefore);
}		// RegisterInterface


// -----------------------------------------------------------------------------
// Initialize
//		called after the Add-On has been loaded into memory
// -----------------------------------------------------------------------------

GSErrCode Initialize (void)
{
	return ACAPI_MenuItem_InstallMenuHandler (MenuResID, MenuCommandHandler);
}		// Initialize


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------

GSErrCode FreeData (void)
{
	return NoError;
}		// FreeData
