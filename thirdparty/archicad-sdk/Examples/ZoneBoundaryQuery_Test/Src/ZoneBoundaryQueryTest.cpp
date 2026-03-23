// from ArchicadAPI
#include "ACAPinc.h"
#include "ACAPI/ZoneBoundaryQuery.hpp"
#include "DGFileDialog.hpp"
#include "TextOProtocol.hpp"

// from ModelerAPI
#include "ModelEdge.hpp"
#include "ModelMeshBody.hpp"

// from Standard Library
#include <algorithm>
#include <vector>


namespace {


template<typename T>
class ArrayHandleRange {

	T*		mArray;
	USize	mCount;

public:

	ArrayHandleRange (T* const* handle)
		: mArray { *handle }
		, mCount { static_cast<USize> (BMGetHandleSize (reinterpret_cast<GSConstHandle> (handle)) / sizeof (T)) }
	{}

	T*	begin ()
	{
		return mArray;
	}

	T*	end ()
	{
		return mArray + mCount;
	}
};


using VertexList = std::vector<ModelerAPI::Vertex>;


struct HoleData {
	VertexList	vertices;
};


struct SurfaceData {
	Int32					polyId;
	VertexList				contour;
	std::vector<HoleData>	holes;
};


struct BoundaryData {
	GS::String					infoString;
	GS::String					elemType;
	GS::Guid					id;
	std::vector<SurfaceData>	surfaces;
	double						area;
	bool						external;
};


struct ZoneData {
	GS::String					infoString;
	GS::Guid					id;
	std::vector<BoundaryData>	boundaries;
};


double	GetRealValue (const API_AddParType* const* addPars, const char* parName)
{
	for (const API_AddParType& addPar : ArrayHandleRange { addPars }) {
		if (CHEqualASCII (addPar.name, parName, GS::CaseInsensitive))
			return addPar.value.real;
	}

	return 0.0;
}


double	GetZoneArea (GS::Guid zoneId)
{
	struct Memo : public API_ElementMemo {
		Memo () : API_ElementMemo {} {}
		~Memo () { ACAPI_DisposeElemMemoHdls (this); }
	} memo;

	ACAPI_Element_GetMemo (GSGuid2APIGuid (zoneId), &memo, APIMemoMask_AddPars);

	return GetRealValue (memo.params, "ROOM_AREA");
}


double	GetZoneVolume (GS::Guid zoneId)
{
	struct Memo : public API_ElementMemo {
		Memo () : API_ElementMemo {} {}
		~Memo () { ACAPI_DisposeElemMemoHdls (this); }
	} memo;

	ACAPI_Element_GetMemo (GSGuid2APIGuid (zoneId), &memo, APIMemoMask_AddPars);

	return GetRealValue (memo.params, "ROOM_VOLUME");
}


GS::String	GetElemInfoString (API_Guid elemId)
{
	struct Memo : public API_ElementMemo {
		Memo () : API_ElementMemo {} {}
		~Memo () { ACAPI_DisposeElemMemoHdls (this); }
	} memo;

	ACAPI_Element_GetMemo (elemId, &memo, APIMemoMask_ElemInfoString);

	if (memo.elemInfoString != nullptr) {
		const auto cStr = memo.elemInfoString->ToCStr ();
		return std::string_view { cStr.Get (), cStr.GetLength () };
	}

	return "???"sv;
}


GS::String	GetElemTypeString (const API_ElemType& elemType)
{
	GS::UniString string;
	ACAPI_Element_GetElemTypeName (elemType, string);

	const auto cStr = string.ToCStr ();

	return std::string_view { cStr.Get (), cStr.GetLength () };
}


SurfaceData		CreateSurfaceData (const ModelerAPI::MeshBody& body, const ModelerAPI::Polygon& poly)
{
	ModelerAPI::Edge edge;
	ModelerAPI::Vertex vertex;

	SurfaceData sd;

	sd.polyId = poly.GetPolygonId ();

	VertexList* vertices = &sd.contour;

	for (Int32 edgeIdx = 1; edgeIdx <= poly.GetEdgeCount (); ++edgeIdx) {

		const Int32 edgeIndex = poly.GetEdgeIndex (edgeIdx);

		if (edgeIndex != 0) {
		
			body.GetEdge (edgeIndex, &edge);
			body.GetVertex (edge.GetVertexIndex1 (), &vertex);

			vertices->push_back (vertex);

		} else {

			HoleData& holeData = sd.holes.emplace_back ();
			vertices = &holeData.vertices;
		}
	}

	return sd;
}


BoundaryData	CreateBoundaryData (const ACAPI::ZoneBoundary& boundary)
{
	API_Elem_Head head {};
	head.guid = boundary.GetElemId ();
	ACAPI_Element_GetHeader (&head);

	BoundaryData bd;

	bd.infoString = GetElemInfoString (head.guid);
	bd.elemType = GetElemTypeString (head.type);
	bd.id = APIGuid2GSGuid (head.guid);

	const ModelerAPI::MeshBody body = boundary.GetBody ();
	const ModelerAPI::Polygon poly = boundary.GetPolygon ();

	bd.surfaces.push_back (CreateSurfaceData (body, poly));

	bd.area = boundary.GetArea ();

	bd.external = boundary.IsExternal ();

	return bd;
}


ZoneData	CreateZoneData (API_Guid zoneId, const ACAPI::ZoneBoundaryQuery& query)
{
	ZoneData zd;

	zd.infoString = GetElemInfoString (zoneId);
	zd.id = APIGuid2GSGuid (zoneId);

	const ACAPI::Result<std::vector<ACAPI::ZoneBoundary>> boundaries = query.GetZoneBoundaries (zoneId);

	for (const ACAPI::ZoneBoundary& boundary : boundaries.Unwrap ())
		zd.boundaries.push_back (CreateBoundaryData (boundary));

	std::sort (zd.boundaries.begin (), zd.boundaries.end (),
		[] (const BoundaryData& lhs, const BoundaryData& rhs) { return lhs.id < rhs.id; }
	);

	return zd;
}


void	AppendVertices (GS::String& summary, const VertexList& vertices)
{
	for (const ModelerAPI::Vertex& vertex : vertices)
		summary += GS::String::SPrintf (" (%.3lf, %.3lf, %.3lf)", vertex.x, vertex.y, vertex.z);
}


void	DumpSurfaces (const std::vector<ZoneData>& zoneData, GS::String& summary)
{
	summary +=  "===============================================================\n"
				"=== Summary of Surfaces =======================================\n"
				"===============================================================\n"sv;

	for (const ZoneData& zd : zoneData) {

		summary += '\n';
		summary += GS::String::SPrintf ("=== Zone [%s] [%s] ===\n", zd.id.ToString ().ToCStr (), zd.infoString.ToCStr ());

		for (const BoundaryData& bd : zd.boundaries) {

			summary += '\n';
			summary += GS::String::SPrintf ("With [%s] [%s] [%s]:\n",
				bd.elemType.ToCStr (), bd.id.ToString ().ToCStr (), bd.infoString.ToCStr ());

			for (const SurfaceData& sd : bd.surfaces) {

				summary += GS::String::SPrintf ("    (PolyId: %d)", sd.polyId);

				AppendVertices (summary, sd.contour);
				summary += '\n';

				for (const HoleData& hole : sd.holes) {
					summary += "    (Hole)"sv;
					AppendVertices (summary, hole.vertices);
					summary += '\n';
				}
			}
		}
	}
}


void	DumpSummary (const std::vector<ZoneData>& zoneData, GS::String& summary)
{
	summary +=  "============================================================\n"
				"=== Summary of Zones =======================================\n"
				"============================================================\n"sv;

	for (const ZoneData& zd : zoneData) {

		summary += '\n';

		summary += GS::String::SPrintf ("       Id : %s\n", zd.id.ToString ().ToCStr ());
		summary += GS::String::SPrintf ("  InfoStr : %s\n", zd.infoString.ToCStr ());
		summary += GS::String::SPrintf ("     Area : %.3lf\n", GetZoneArea (zd.id));
		summary += GS::String::SPrintf ("   Volume : %.3lf\n", GetZoneVolume (zd.id));
		summary +=						"Connected :\n"sv;

		for (const BoundaryData& bd : zd.boundaries) {
			summary += GS::String::SPrintf ("    [%s] [%s] [%s] [%s] [Area=%.4lf]\n",
				bd.elemType.ToCStr (), bd.id.ToString ().ToCStr (),
				bd.infoString.ToCStr (), (bd.external ? "External" : "Internal"),
				bd.area);
		}
	}
}


void	AppendSelectedZones (GS::Array<API_Guid>& zoneIds)
{
	API_SelectionInfo selInfo {};
	GS::Array<API_Neig> selNeigs;

	const GSErrCode err = ACAPI_Selection_Get (&selInfo, &selNeigs, false /*onlyEditable*/);
	BMKillHandle (reinterpret_cast<GSHandle*> (&selInfo.marquee.coords));

	if (err == APIERR_NOSEL)
		return;

	for (const API_Neig& neig : selNeigs) {

		API_ElemType elemType {};
		ACAPI_Element_NeigIDToElemType (neig.neigID, elemType);

		if (elemType == API_ZoneID)
			zoneIds.Push (neig.guid);
	}
}


enum class Output : UInt16 {
	SessionReport, File
};


void	DumpInfo (const Output output)
{
	ACAPI::ZoneBoundaryQuery query = ACAPI::CreateZoneBoundaryQuery ();

	GS::ProcessControl* processControl = nullptr;
	ACAPI_ProcessWindow_GetProcessControl (&processControl);

	ACAPI::Result updateResult = query.Modify (
		[processControl] (ACAPI::ZoneBoundaryQuery::Modifier& modifier) -> GSErrCode {
			ACAPI::Result<void> result = modifier.Update (*processControl);
			return result.IsOk () ? NoError : result.UnwrapErr ().kind;
		}
	);

	if (updateResult.IsErr ())
		return;

	GS::Array<API_Guid> zoneIds;

	AppendSelectedZones (zoneIds);

	if (zoneIds.IsEmpty ())
		ACAPI_Element_GetElemList (API_ZoneID, &zoneIds);

	std::sort (zoneIds.BeginFast (), zoneIds.EndFast ());

	std::vector<ZoneData> zoneData;

	for (const API_Guid& zoneId : zoneIds)
		zoneData.push_back (CreateZoneData (zoneId, query));

	GS::String summary;
	summary.EnsureCapacity (1024);

	DumpSummary (zoneData, summary);
	summary += '\n';
	DumpSurfaces (zoneData, summary);

	if (output == Output::SessionReport) {
		ACAPI_WriteReport (summary, false /*withDial*/);
	} else {
		DG::FileDialog dial { DG::FileDialog::Save };
		if (dial.Invoke ()) {
			IO::File file { dial.GetSelectedLocation (), IO::File::Create };
			if (file.Open (IO::File::WriteEmptyMode) == NoError) {
				file.SetOutputProtocol (GS::textOProtocol);
				file.Write (summary.ToCStr ());
			}
		}
	}
}


GSErrCode	MenuHandler (const API_MenuParams* menuParams)
{
	switch (menuParams->menuItemRef.itemIndex) {
	case 1:
		DumpInfo (Output::SessionReport);
		return NoError;
	case 2:
		DumpInfo (Output::File);
		return NoError;
	}

	return Error;
}


} // namespace


API_AddonType	CheckEnvironment (API_EnvirParams* envir)
{
	envir->addOnInfo.name = RSGetIndString (32000, 1, ACAPI_GetOwnResModule ());
	envir->addOnInfo.description = RSGetIndString (32000, 2, ACAPI_GetOwnResModule ());

	return APIAddon_Normal;
}


GSErrCode	RegisterInterface (void)
{
	return ACAPI_MenuItem_RegisterMenu (32500, 0, MenuCode_UserDef, MenuFlag_Default);
}


GSErrCode	Initialize (void)
{
	return ACAPI_MenuItem_InstallMenuHandler (32500, MenuHandler);
}


GSErrCode	FreeData (void)
{
	return NoError;
}
