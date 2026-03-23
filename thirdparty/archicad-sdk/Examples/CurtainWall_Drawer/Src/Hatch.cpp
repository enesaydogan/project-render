// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Hatch class for managing
//					Hatch element types
// *****************************************************************************

#include "Hatch.h"
#include "CurtainWall.h"

Hatch::Hatch () :
	ElementWithMemo (API_HatchID)
{
}

Hatch::Hatch (const API_Elem_Head& elemHead) :
	ElementWithMemo (elemHead)
{
	GetElementWithMemo (GetMask (), *this);
}

UInt64 Hatch::GetMask () const
{
	return APIMemoMask_Polygon;
}

const API_Polygon& Hatch::GetHatchPoly () const
{
	return element.hatch.poly;
}

bool Hatch::IsHatchCoordsMatch (const GS::Ref<Hatch>& selectedHatch) const
{
	for (Int32 k = 1; k < element.hatch.poly.nCoords + 1; ++k) {
		if (!Element::IsNearCoord ((*memo.coords)[k], (*selectedHatch->memo.coords)[k])) {
			return true;
		}
	}
	return false;
}

void Hatch::SetMemoFromCurtainWall (const GS::Ref<CurtainWall>& curtainWall, const API_Coord& offset)
{
	const auto& curtainWallMemo = curtainWall->GetMemo ();
	element.hatch.poly = (*curtainWallMemo.cWSegContour).polygon;

	memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((element.hatch.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
	CopyCoords (element.hatch.poly, (*curtainWallMemo.cWSegContour).coords, memo.coords);
	OffsetCoords (element.hatch.poly, offset, memo.coords);

	memo.pends = reinterpret_cast<Int32**> (BMAllocateHandle ((element.hatch.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));
	CopyPends (element.hatch.poly, (*curtainWallMemo.cWSegContour).pends, memo.pends);

	memo.parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle ((element.hatch.poly.nArcs) * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));
	CopyArcs (element.hatch.poly, (*curtainWallMemo.cWSegContour).parcs, memo.parcs);

	memo.vertexIDs = reinterpret_cast<UInt32**> (BMAllocateHandle ((element.hatch.poly.nCoords + 1) * sizeof (UInt32), ALLOCATE_CLEAR, 0));
	CopyVertexIDS (element.hatch.poly, (*curtainWallMemo.cWSegContour).vertexIDs, memo.vertexIDs);
}

