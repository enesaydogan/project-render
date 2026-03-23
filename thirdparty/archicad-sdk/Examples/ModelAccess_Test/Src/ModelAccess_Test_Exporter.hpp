// *****************************************************************************
// Export ModelAccess Test files
// *****************************************************************************

#ifndef	_MATEXPORT_HPP_
#define	_MATEXPORT_HPP_

//----------------------------------- Includes ---------------------------------

#include "ModelAccess_Test_Writer.hpp"

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "Vector3D.hpp"
#include "String.hpp"

//-------------------------------- Predeclarations -----------------------------

namespace ModelerAPI {
	class MeshBody;
	class Model;
	class AttributeIndex;
}

namespace InputOutput { class Location; }

//--------------------------------- Namespace WRL ------------------------------

namespace WRL {

//------------------------------- Class definitions ----------------------------

//-------------------------------- Class MAExporter ----------------------------

class MAExporter {
private:
		// data members

	IO::Location					ioParam_fileLoc;
	IO::Name						ioParam_saveFileIOName;
	ModelerAPI::Model*				model;
	GS::Array<GS::UniString>		localizedStrings;
	MAWriter*						writer;
	GS::Array<GS::UniString>		textureNames;
	IO::Location					textureLoc;

private:
		// export methods

	void				CreateTextures (void);
	void				RemoveExtension (char* fileName);
	void				CreateTextureFile (const ModelerAPI::AttributeIndex& textureIndex, char* fullPathName);

	void				WriteHeader (void);
	void				CalcOrientation (const Coord3D& viewPoint, const Coord3D& targetPoint,
										 double rollAngle, Coord3D* axis, double* angle);
	void				CalcSceneLimits (Coord3D* minCoord, Coord3D* maxCoord);
	void				ExportCameraFromSaveAs3D  (void);
	void				ExportPerspectiveCamerasFromPlan (void);
	void				ExportLights (void);
	void				ExportBody (ModelerAPI::MeshBody& body);
	void				ExportBodies (void);

		// resource handling methods

	void				LoadLocalizedStringResources (void);

public:

		// constructor / destructor

	MAExporter (ModelerAPI::Model* newModel, const API_IOParams* ioParams);
   ~MAExporter ();

		// export methods

	void				ExportScene (void);
};

//--------------------------------- Namespace WRL ------------------------------

}

#endif
