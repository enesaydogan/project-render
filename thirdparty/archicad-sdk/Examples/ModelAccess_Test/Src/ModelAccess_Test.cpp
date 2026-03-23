// *****************************************************************************
// Source code for the ModelAccess Test Add-On
// *****************************************************************************

// ---------------------------------- Includes ---------------------------------

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "Location.hpp"
#include "FileSystem.hpp"
#include "exp.h"
#include "Model3D/Model3DMain.hpp"
#include "Model.hpp"

#include "ModelAccess_Test_Common.hpp"
#include "ModelAccess_Test_Exporter.hpp"
#include "ModelAccess_Test_Resources.hpp"
#include "Sight.hpp"
#include "IAttributeReader.hpp"

// ---------------------------- Function definitions ---------------------------

namespace WRL {
IMPLEMENT_EXCEPTION_CLASS (
		Cancel,
		GS::RootException,
		Error
)
}

GSErrCode StartExport (const API_IOParams* ioParams, Modeler::SightPtr sight);

// -----------------------------------------------------------------------------
// Start the export
// -----------------------------------------------------------------------------
GSErrCode StartExport (const API_IOParams* ioParams, Modeler::SightPtr sight)
{
	GS::Owner<Modeler::IAttributeReader> attrReader (ACAPI_Attribute_GetCurrentAttributeSetReader ());
	ModelerAPI::Model model;
	EXPGetModel (sight, &model, attrReader.Get ());

	try {
		WRL::MAExporter	exporter (&model, ioParams);
		exporter.ExportScene ();
	}

	catch (WRL::Cancel&) {
		IO::Location	fileLoc;
		fileLoc = *ioParams->fileLoc;
		IO::fileSystem.Delete (fileLoc);

		return APIERR_CANCEL;
	}

	catch (...) {
		IO::Location	fileLoc;
		fileLoc = *ioParams->fileLoc;
		IO::fileSystem.Delete (fileLoc);

		return APIERR_GENERAL;
	}

	return NoError;
}


// =============================================================================
//
// Required functions
//
// =============================================================================

#ifdef __APPLE__
#pragma mark -
#endif

// -----------------------------------------------------------------------------
// Dependency definitions
// -----------------------------------------------------------------------------
API_AddonType CheckEnvironment (API_EnvirParams* envir)
{
	RSGetIndString (&envir->addOnInfo.name, WRL::AddonInfoStringTable, WRL::AddonName, ACAPI_GetOwnResModule ());
	RSGetIndString (&envir->addOnInfo.description, WRL::AddonInfoStringTable, WRL::AddonDescription, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


// -----------------------------------------------------------------------------
// Interface definitions
// -----------------------------------------------------------------------------
GSErrCode RegisterInterface (void)
{
	return ACAPI_AddOnIntegration_RegisterFileType (1, 'TEXT', 'xxxx', "txt", 0, (short) WRL::AddonInfoStringTable, (short) WRL::Format, SaveAs3DSupported);
}


// -----------------------------------------------------------------------------
// Called when the Add-On has been loaded into memory
// to perform an operation
// -----------------------------------------------------------------------------
GSErrCode Initialize	(void)
{
	GSErrCode	errCode = ACAPI_AddOnIntegration_InstallFileTypeHandler3D (1, StartExport);
	if (errCode != NoError)
		DBPrintf ("ModelAccess_Test:: Initialize() ACAPI_AddOnIntegration_InstallFileTypeHandler3D failed\n");

	return errCode;
}


// -----------------------------------------------------------------------------
// FreeData
//		called when the Add-On is going to be unloaded
// -----------------------------------------------------------------------------
GSErrCode FreeData (void)
{
	return NoError;
}
