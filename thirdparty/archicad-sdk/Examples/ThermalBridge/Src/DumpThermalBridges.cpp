#include "DumpThermalBridges.hpp"

// from ArchicadAPI
#include "ACAPinc.h"

// from GSModeler
#include "ModelElement.hpp"

// from Geometry
#include "Plane.hpp"
#include "Poly2DTypes.hpp"
#include "Polygon2DClassDefinition.hpp"
#include "Polygon2D.hpp"
#include "Polygon2DDataConv.h"
#include "Model.hpp"


namespace ThermalBridge {


namespace
{


GSErrCode CreateAndSelectSight (void*& sightPtr, void*& origSightPtr)
{
	ERRCHK_NO_ASSERT (ACAPI_Sight_CreateSight (&sightPtr));
	ERRCHK_NO_ASSERT (ACAPI_Sight_SelectSight (sightPtr, &origSightPtr));

	return NoError;
}


GSErrCode GenerateModelAndGetConnectionTable (API_ElementConnectionTable& table)
{
	GS::Array<API_Guid> elements;
	ERRCHK_NO_ASSERT (ACAPI_Element_GetElemList (API_ZombieElemID, &elements, APIFilt_In3D));
	if (elements.IsEmpty ())
		return Error;

	ERRCHK_NO_ASSERT (ACAPI_ModelAccess_GenerateModelWithSeparateComponents (elements.Convert ()));
	ERRCHK_NO_ASSERT (ACAPI_ModelAccess_GetConnectionTable (elements.Convert (), &table));

	return NoError;
}


Geometry::Plane API2AC_Plane3D (const API_Plane3D& apiPlane) 
{
	Point3D basePoint (apiPlane.basePoint.x, apiPlane.basePoint.y, apiPlane.basePoint.z);
	Vector3D axisX (apiPlane.axisX.x, apiPlane.axisX.y, apiPlane.axisX.z);
	Vector3D axisY (apiPlane.axisY.x, apiPlane.axisY.y, apiPlane.axisY.z);
	Vector3D axisZ (apiPlane.axisZ.x, apiPlane.axisZ.y, apiPlane.axisZ.z);

	return { basePoint, axisZ, axisX, axisY };
}


GSErrCode CalculateCutPlane (const GS::Array<API_PolygonExt>& connectionPolygons, const API_Guid& elemId1, const API_Guid& elemId2, API_Plane3D& cutPlane)
{
	Point3D basePoint = {};
	USize pointCount = 0;
	GS::PagedArray<Point3D> points;
	for (const API_PolygonExt& poly : connectionPolygons) {
		for (Int32 i = 0; i < poly.polygon.nCoords; ++i) {
			API_Coord apiCoord2D =  (*poly.polygonData.coords)[i];
			Point2D point2D (apiCoord2D.x, apiCoord2D.y);

			Geometry::Plane plane = API2AC_Plane3D (poly.plane);
			Point3D coord3D = plane.PlaneToWorld (point2D);
			basePoint += coord3D;
		}
		pointCount += poly.polygon.nCoords;
	}
	basePoint /= pointCount;

	API_Element	apiElem1 = {};
	apiElem1.header.guid = elemId1;
	ERRCHK_NO_ASSERT (ACAPI_Element_Get (&apiElem1));

	API_Element	apiElem2 = {};
	apiElem2.header.guid = elemId2;
	ERRCHK_NO_ASSERT (ACAPI_Element_Get (&apiElem2));

	const bool wallWallConnection = apiElem1.header.type.typeID == API_WallID && apiElem2.header.type.typeID == API_WallID;
	const bool wallSlabConnection = (apiElem1.header.type.typeID == API_WallID && apiElem2.header.type.typeID == API_SlabID) ||
		(apiElem2.header.type.typeID == API_WallID && apiElem1.header.type.typeID == API_SlabID);

	if (!wallWallConnection && !wallSlabConnection)
		return Error;

	cutPlane.basePoint.x = basePoint.x;
	cutPlane.basePoint.y = basePoint.y;
	cutPlane.basePoint.z = basePoint.z;

	if (wallWallConnection) {
		cutPlane.axisX.x = 1.0; cutPlane.axisX.y = 0.0; cutPlane.axisX.z = 0.0;
		cutPlane.axisY.x = 0.0; cutPlane.axisY.y = 1.0; cutPlane.axisY.z = 0.0;
		cutPlane.axisZ.x = 0.0; cutPlane.axisZ.y = 0.0; cutPlane.axisZ.z = 1.0;
	} else {
		API_Coord3D zAxis = {};
		if (apiElem1.header.type.typeID == API_WallID) {
			zAxis.x = apiElem1.wall.endC.x - apiElem1.wall.begC.x;
			zAxis.y = apiElem1.wall.endC.y - apiElem1.wall.begC.y;
		} else {
			zAxis.x = apiElem2.wall.endC.x - apiElem2.wall.begC.x;
			zAxis.y = apiElem2.wall.endC.y - apiElem2.wall.begC.y;
		}

		Geometry::Plane plane (basePoint, { zAxis.x, zAxis.y, zAxis.z });
		const Vector3D& geomXAxis = plane.GetXAxis ();
		const Vector3D& geomYAxis = plane.GetYAxis ();
		const Vector3D& geomZAxis = plane.GetZAxis ();

		cutPlane.axisX.x = geomXAxis.x; cutPlane.axisX.y = geomXAxis.y; cutPlane.axisX.z = geomXAxis.z;
		cutPlane.axisY.x = geomYAxis.x; cutPlane.axisY.y = geomYAxis.y; cutPlane.axisY.z = geomYAxis.z;
		cutPlane.axisZ.x = geomZAxis.x; cutPlane.axisZ.y = geomZAxis.y; cutPlane.axisZ.z = geomZAxis.z;
	}

	return NoError;
}


GSErrCode CreateResult (Int32 elemIndex, UInt32 bodyCount, const API_Plane3D& apiCutPlane, GS::Array<GS::Pair<API_Attribute, GS::Array<Geometry::MultiPolygon2D>>>& result) 	
{
	for (UInt32 bodyIndex = 0; bodyIndex < bodyCount; ++bodyIndex) {

		GS::Array<Geometry::MultiPolygon2D> resPolygons;
		ERRCHK_NO_ASSERT (ACAPI_CuttingPlane_GetCutPolygonInfo_New (elemIndex, bodyIndex, apiCutPlane, &resPolygons));

		API_AttributeIndex materialIndex;
		ERRCHK_NO_ASSERT (ACAPI_ModelAccess_GetBuildingMaterial (elemIndex, bodyIndex, &materialIndex));

		API_Attribute materialAttribute = {};
		materialAttribute.header.typeID = API_BuildingMaterialID;
		materialAttribute.header.index = materialIndex;

		ACAPI_Attribute_Get (&materialAttribute);

		result.Push ({ materialAttribute, resPolygons });
	}

	return NoError;
}


} // namespace


GSErrCode DumpThermalBridges () 
{
	void* origSightPtr = nullptr;
	void* sightPtr = nullptr;
	ERRCHK_NO_ASSERT (CreateAndSelectSight (sightPtr, origSightPtr));

	API_ElementConnectionTable table = {};
	ERRCHK_NO_ASSERT (GenerateModelAndGetConnectionTable (table));

	ModelerAPI::Model model;
	ERRCHK_NO_ASSERT (ACAPI_Sight_GetSelectedSightModel (model));

	for (const auto& [elemPair, connectionPolygons] : table) {
		GS::Guid elem1Guid = APIGuid2GSGuid (elemPair.first);
		GS::Guid elem2Guid = APIGuid2GSGuid (elemPair.second);

		std::optional<Int32> elem1IndexOpt = model.GetElementIndex (elem1Guid);
		std::optional<Int32> elem2IndexOpt = model.GetElementIndex (elem2Guid);

		if DBERROR (!elem1IndexOpt.has_value () || !elem2IndexOpt.has_value ())
			return Error;

		ModelerAPI::Element elem1, elem2;
		model.GetElement (*elem1IndexOpt, &elem1);
		model.GetElement (*elem2IndexOpt, &elem2);

		// ModelerAPI is indexing from 1, but API functions such as ACAPI_CuttingPlane_GetCutPolygonInfo_New, ACAPI_ModelAccess_GetBuildingMaterial use element indices starting from 0. Therefore we substract 1.
		Int32 elem1Index = *elem1IndexOpt - 1;
		Int32 elem2Index = *elem2IndexOpt - 1;

		ULong elem1BodyCount = elem1.GetTessellatedBodyCount ();
		ULong elem2BodyCount = elem2.GetTessellatedBodyCount ();

		API_Plane3D apiCutPlane;
		ERRCHK_NO_ASSERT (CalculateCutPlane (connectionPolygons, elemPair.first, elemPair.second, apiCutPlane));

		GS::Array<GS::Pair<API_Attribute, GS::Array<Geometry::MultiPolygon2D>>> elem1Result;
		GS::Array<GS::Pair<API_Attribute, GS::Array<Geometry::MultiPolygon2D>>> elem2Result;

		ERRCHK_NO_ASSERT (CreateResult (elem1Index, elem1BodyCount, apiCutPlane, elem1Result));
		ERRCHK_NO_ASSERT (CreateResult (elem2Index, elem2BodyCount, apiCutPlane, elem2Result));

		ACAPI_WriteReport (GS::UniString::Printf ("Elem Id: %T, Elem Index: %d, Body count: %d \nList of materials: \n", APIGuidToString (elemPair.first).ToPrintf (), elem1Index, elem1BodyCount), false);
		for (const GS::Pair<API_Attribute, GS::Array<Geometry::MultiPolygon2D>>& result : elem1Result) {
			GS::UniString materialName (result.first.buildingMaterial.head.name);
			if (materialName.IsEmpty ())
				materialName = "MISSING";
			ACAPI_WriteReport (GS::UniString::Printf ("\tMaterial Id: %s, Material Name: %s, Thermal Conductivity: %f \n", result.first.header.index.ToUniString ().ToCStr ().Get (), materialName.ToCStr ().Get (), result.first.buildingMaterial.thermalConductivity), false);
		}

		ACAPI_WriteReport ("\n", false);

		ACAPI_WriteReport (GS::UniString::Printf ("Elem Id: %T, Elem Index: %d, Body count: %d \nList of materials: \n", APIGuidToString (elemPair.second).ToPrintf (), elem2Index, elem2BodyCount), false);
		for (const GS::Pair<API_Attribute, GS::Array<Geometry::MultiPolygon2D>>& result : elem2Result) {
			GS::UniString materialName (result.first.buildingMaterial.head.name);
			if (materialName.IsEmpty ())
				materialName = "MISSING";
			ACAPI_WriteReport (GS::UniString::Printf ("\tMaterial Id: %s, Material Name: %s, Thermal Conductivity: %f \n", result.first.header.index.ToUniString ().ToCStr ().Get (), materialName.ToCStr ().Get (), result.first.buildingMaterial.thermalConductivity), false);
		}

		ACAPI_WriteReport ("Polygons of the connecting bodies:\n", false);
		UInt32 i = 0;
		for (const API_PolygonExt& polygon : connectionPolygons) {
			ACAPI_WriteReport (GS::UniString::Printf ("\tPolygon %d:\n", i + 1), false);

			// Polygons in Archicad are created in such way that the 0th index is not used, and the nth index is a duplicate of the 1st index. 
			for (Int32 coordIdx = 1; coordIdx <= polygon.polygon.nCoords; ++coordIdx) {
				API_Coord apiCoord2D = (*polygon.polygonData.coords)[coordIdx];
				Point2D point2D (apiCoord2D.x, apiCoord2D.y);

				Geometry::Plane plane = API2AC_Plane3D (polygon.plane);
				Point3D coord3D = plane.PlaneToWorld (point2D);

				ACAPI_WriteReport (GS::UniString::Printf ("\t\t{ %.4f, %.4f, %.4f }\n", coord3D.x, coord3D.y, coord3D.z), false);
			}
			++i;
		}

		ACAPI_WriteReport ("-----------------------------------------------------\n", false);
	}

	ERRCHK_NO_ASSERT (ACAPI_Sight_SelectSight (origSightPtr, &sightPtr));
	ERRCHK_NO_ASSERT (ACAPI_Sight_DeleteSight (sightPtr));

	for (GS::Array<API_PolygonExt>& polygonArray : table.Values ()) {
		for (API_PolygonExt& polygon : polygonArray) {
			ACAPI_DisposeElemMemoHdls (&polygon.polygonData);
		}
	}

	return NoError;	
}

} // namespace MEPExample