// -----------------------------------------------------------------------------
// Drag (adjust) a node of a polygon
//	- dimensions must be updated automatically
// -----------------------------------------------------------------------------

// -----------------------------------------------------------------------------
// Helper function for poly edit examples
//
// Sets memomask for ACAPI_Element_ChangeMemo to accept the new polygon.
// When sending polygon, you have to send the edge datas as well.
// You have two choices:
//	- send a new edge data list, which is synchronous with the new polygon
//	- send nullptr to clear any edge customizations
//
// In the examples, we send nullptr as edge datas.
//
// This restriction does not apply to meshPolyZ coords, which are vertex data.
//
// -----------------------------------------------------------------------------

#include "APIEnvir.h"
#include "ACAPinc.h"

#include "APICommon.h"
#include "Element_Test.h"

static UInt32 GetMemoMask (const API_Neig& neig) {
	UInt32 mask = 0;
	switch (neig.neigID) {
		case APINeig_Ceil:
		case APINeig_CeilOn:
							mask |= APIMemoMask_Polygon;
							mask |= APIMemoMask_EdgeTrims;
							mask |= APIMemoMask_SideMaterials;
							return mask;
		case APINeig_Roof:
		case APINeig_RoofOn:
							mask |= APIMemoMask_Polygon;
							mask |= APIMemoMask_EdgeTrims;
							mask |= APIMemoMask_SideMaterials;
							mask |= APIMemoMask_RoofEdgeTypes;
							return mask;
		default:
				break;
	}
	return APIMemoMask_Polygon;
}


void		Do_Poly_AdjustNode (void)
{
	API_ElementMemo		memo;
	API_Neig			neig;
	API_ElemType		type;
	API_Coord3D			begC, endC;
	Int32				nSubPolys, i, begInd, endInd;
	GSErrCode			err = NoError;

	if (!ClickAnElem ("Click a polygon node to drag", API_ZombieElemID, &neig, &type, nullptr, &begC)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	switch (neig.neigID) {
		case APINeig_Ceil:
		case APINeig_Roof:
		case APINeig_Mesh:
		case APINeig_Room:
		case APINeig_Hatch:
		case APINeig_PolyLine:
		case APINeig_DetailPoly:
		case APINeig_WorksheetPoly:
		case APINeig_DrawingFrame:
		case APINeig_InteriorElevation:		break;

		default:							WriteReport_Alert ("Only polygon nodes are accepted");
											return;
	}

	if (GetEditVector (&begC, &endC, "Enter drag reference point", true)) {
		err = ACAPI_Element_GetMemo (neig.guid, &memo);
		if (err == NoError) {
			(*memo.coords)[neig.inIndex].x += endC.x - begC.x;
			(*memo.coords)[neig.inIndex].y += endC.y - begC.y;
			nSubPolys = BMGetHandleSize ((GSHandle) memo.pends) / sizeof (Int32) - 1;
			for (i = 1; i <= nSubPolys; i++) {
				begInd = (*memo.pends)[i - 1] + 1;
				endInd = (*memo.pends)[i];
				if (neig.inIndex == begInd)
					(*memo.coords)[endInd] = (*memo.coords)[neig.inIndex];
				else if (neig.inIndex == endInd)
					(*memo.coords)[begInd] = (*memo.coords)[neig.inIndex];
			}
		}
		if (err == NoError) {
			API_ElementMemo tmpMemo = {};
			tmpMemo.coords = memo.coords;
			tmpMemo.pends = memo.pends;
			tmpMemo.parcs = memo.parcs;
			tmpMemo.vertexIDs = memo.vertexIDs;
			tmpMemo.edgeIDs = memo.edgeIDs;
			tmpMemo.edgeTrims = memo.edgeTrims;
			tmpMemo.contourIDs = memo.contourIDs;
			tmpMemo.meshPolyZ = memo.meshPolyZ;
			err = ACAPI_Element_ChangeMemo (neig.guid, GetMemoMask (neig), &tmpMemo);
			if (err != NoError)
				ErrorBeep ("ACAPI_Element_ChangeMemo", err);
		}

		ACAPI_DisposeElemMemoHdls (&memo);
	}

	return;
}		// Do_Poly_AdjustNode


// -----------------------------------------------------------------------------
// Insert a new node into the clicked polygon edge
//	- dimensions must be updated automatically
// -----------------------------------------------------------------------------

void		Do_Poly_InsertNode (void)
{
	API_ElementMemo		memo;
	API_Neig			neig;
	API_ElemType		type;
	API_Coord3D			begC;
	Int32				nodeInd;
	GSErrCode			err;

	if (!ClickAnElem ("Click a polygon edge to insert a new node into", API_ZombieElemID, &neig, &type, nullptr, &begC)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	switch (neig.neigID) {
		case APINeig_CeilOn:
		case APINeig_RoofOn:
		case APINeig_MeshOn:
		case APINeig_RoomOn:
		case APINeig_HatchOn:
		case APINeig_PolyLineOn:
		case APINeig_DetailPolyOn:
		case APINeig_WorksheetPolyOn:
		case APINeig_DrawingFrameOn:
		case APINeig_InteriorElevationOn:	break;

		default:							WriteReport_Alert ("Only polygon edges are accepted");
											return;
	}

	// TODO: don't do it for polyroofs, only for planeroofs
	API_Element elem = {};
	elem.header.guid = neig.guid;
	err = ACAPI_Element_Get (&elem);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}
	if (elem.header.type == API_RoofID && elem.roof.roofClass != API_PlaneRoofID) {
		WriteReport_Alert ("Only plane roofs are accepted");
		return;
	}
	err = ACAPI_Element_GetMemo (neig.guid, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	nodeInd = neig.inIndex + 1;
	API_Coord toInsert = {};
	toInsert.x = begC.x;
	toInsert.y = begC.y;
	err = ACAPI_Polygon_InsertPolyNode (&memo, &nodeInd, &toInsert);
	if (err == NoError) {
		API_ElementMemo tmpMemo = {};
		tmpMemo.coords = memo.coords;
		tmpMemo.pends = memo.pends;
		tmpMemo.parcs = memo.parcs;
		tmpMemo.vertexIDs = memo.vertexIDs;
		tmpMemo.edgeIDs = memo.edgeIDs;
		tmpMemo.edgeTrims = memo.edgeTrims;
		tmpMemo.contourIDs = memo.contourIDs;
		tmpMemo.meshPolyZ = memo.meshPolyZ;
		err = ACAPI_Element_ChangeMemo (neig.guid, GetMemoMask (neig), &tmpMemo);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeMemo", err);
	} else
		ErrorBeep ("APIAny_InsertPolyNodeID", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Poly_InsertNode


// -----------------------------------------------------------------------------
// Delete the clicked node from the polygon
//	- dimensions must be updated automatically
// -----------------------------------------------------------------------------

void		Do_Poly_DeleteNode (void)
{
	API_ElementMemo		memo;
	API_Neig			neig;
	API_ElemType		type;
	Int32				nodeInd;
	GSErrCode			err;

	if (!ClickAnElem ("Click a polygon node to delete", API_ZombieElemID, &neig, &type)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	switch (neig.neigID) {
		case APINeig_Ceil:
		case APINeig_Roof:
		case APINeig_Mesh:
		case APINeig_Room:
		case APINeig_Hatch:
		case APINeig_PolyLine:
		case APINeig_DetailPoly:
		case APINeig_WorksheetPoly:
		case APINeig_DrawingFrame:
		case APINeig_InteriorElevation:		break;

		default:							WriteReport_Alert ("Only polygon nodes are accepted");
											return;
	}

	err = ACAPI_Element_GetMemo (neig.guid, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	if (BMhGetSize ((GSHandle) memo.coords) / sizeof (API_Coord) <= 5) {
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}

	nodeInd = neig.inIndex;
	err = ACAPI_Polygon_DeletePolyNode (&memo, &nodeInd);
	if (err == NoError) {
		API_ElementMemo tmpMemo = {};
		tmpMemo.coords = memo.coords;
		tmpMemo.pends = memo.pends;
		tmpMemo.parcs = memo.parcs;
		tmpMemo.vertexIDs = memo.vertexIDs;
		tmpMemo.meshPolyZ = memo.meshPolyZ;
		tmpMemo.edgeIDs = memo.edgeIDs;
		tmpMemo.edgeTrims = memo.edgeTrims;
		tmpMemo.contourIDs = memo.contourIDs;
		err = ACAPI_Element_ChangeMemo (neig.guid, GetMemoMask (neig), &tmpMemo);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeMemo", err);
	} else
		ErrorBeep ("APIAny_DeletePolyNodeID", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Poly_DeleteNode


// -----------------------------------------------------------------------------
// Delete the clicked polygon hole
//	- dimensions must be updated automatically
// -----------------------------------------------------------------------------

void		Do_Poly_DeleteHole (void)
{
	API_ElementMemo		memo;
	API_Neig			neig;
	API_ElemType		type;
	Int32				nSubPolys, subPolyInd, i;
	GSErrCode			err;

	if (!ClickAnElem ("Click a polygon hole to delete", API_ZombieElemID, &neig, &type)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	switch (type.typeID) {
		case API_SlabID:
		case API_RoofID:
		case API_MeshID:
		case API_ZoneID:
		case API_HatchID:	break;

		default:			WriteReport_Alert ("Only slab, roof, mesh, zone or fill nodes/edges are excepted");
							return;
	}

	err = ACAPI_Element_GetMemo (neig.guid, &memo);
	if (err != NoError)
		return;

	subPolyInd = 0;
	nSubPolys = BMGetHandleSize ((GSHandle) memo.pends) / sizeof (Int32) - 1;
	for (i = 1; i <= nSubPolys; i++) {
		if (neig.inIndex <= (*memo.pends)[i]) {
			subPolyInd = i;
			break;
		}
	}

	err = ACAPI_Polygon_DeleteSubPoly (&memo, &subPolyInd);
	if (err == NoError) {
		API_ElementMemo tmpMemo = {};
		tmpMemo.coords = memo.coords;
		tmpMemo.pends = memo.pends;
		tmpMemo.parcs = memo.parcs;
		tmpMemo.vertexIDs = memo.vertexIDs;
		tmpMemo.meshPolyZ = memo.meshPolyZ;
		tmpMemo.edgeIDs = memo.edgeIDs;
		tmpMemo.edgeTrims = memo.edgeTrims;
		tmpMemo.contourIDs = memo.contourIDs;
		err = ACAPI_Element_ChangeMemo (neig.guid, GetMemoMask (neig), &tmpMemo);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeMemo", err);
	} else
		ErrorBeep ("APIAny_DeleteSubPolyID", err);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Poly_DeleteHole


// -----------------------------------------------------------------------------
// Add a new hole to the clicked polygon
//	- dimensions must be updated automatically
// -----------------------------------------------------------------------------

void		Do_Poly_NewHole (void)
{
	API_ElementMemo		memo;
	API_Neig			neig;
	API_ElemType		type;
	GSErrCode			err;

	if (!ClickAnElem ("Click a polygon to insert a new hole into", API_ZombieElemID, &neig, &type)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	switch (type.typeID) {
		case API_SlabID:
		case API_RoofID:
		case API_MeshID:
		case API_ZoneID:
		case API_HatchID:	break;

		default:			WriteReport_Alert ("Only slab, roof, mesh, zone or fill nodes/edges are excepted");
							return;
	}

	API_GetPointType pointInfo = {};
	API_GetPolyType	 polyInfo  = {};
	strcpy (pointInfo.prompt, "Click the first node of the hole");
	err = ACAPI_UserInput_GetPoint (&pointInfo);
	if (err == NoError) {
		strcpy (polyInfo.prompt, "Enter the polygon nodes");
		polyInfo.startCoord = pointInfo.pos;
		err = ACAPI_UserInput_GetPoly (&polyInfo);
	}
	if (err != NoError) {
		BMKillHandle ((GSHandle *) &polyInfo.coords);
		BMKillHandle ((GSHandle *) &polyInfo.parcs);
		return;
	}

	err = ACAPI_Element_GetMemo (neig.guid, &memo);
	if (err != NoError) {
		BMKillHandle ((GSHandle *) &polyInfo.coords);
		BMKillHandle ((GSHandle *) &polyInfo.parcs);
		return;
	}

	API_ElementMemo insMemo = {};
	insMemo.coords			= polyInfo.coords;
	insMemo.parcs			= polyInfo.parcs;

	polyInfo.coords = nullptr;
	polyInfo.parcs	= nullptr;

	err = ACAPI_Polygon_InsertSubPoly (&memo, &insMemo);
	if (err == NoError) {
		API_ElementMemo tmpMemo = {};
		tmpMemo.coords = memo.coords;
		tmpMemo.pends = memo.pends;
		tmpMemo.parcs = memo.parcs;
		tmpMemo.vertexIDs = memo.vertexIDs;
		tmpMemo.meshPolyZ = memo.meshPolyZ;
		tmpMemo.edgeIDs = memo.edgeIDs;
		tmpMemo.edgeTrims = memo.edgeTrims;
		tmpMemo.contourIDs = memo.contourIDs;
		err = ACAPI_Element_ChangeMemo (neig.guid, GetMemoMask (neig), &tmpMemo);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeMemo", err);
	} else
		ErrorBeep ("APIAny_InsertSubPolyID", err);

	ACAPI_DisposeElemMemoHdls (&insMemo);
	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Poly_NewHole
