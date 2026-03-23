// *****************************************************************************
// Description:		CurtainWall_Drawer Test AddOn Curtain Wall class for managing
//					Curtain Wall element types
// *****************************************************************************

#include "CurtainWall.h"
#include "CurtainWallFromSelection.h"
#include "Drawer.h"
#include "LineTypeIndexes.h"

CurtainWall::CurtainWall () :
	ElementWithMemo (API_CurtainWallID)
{
}

CurtainWall::CurtainWall (const API_Elem_Head& elemHead) :
	ElementWithMemo (elemHead)
{
	GetElementWithMemo (GetMask (), *this);
}

void CurtainWall::SetMemoFromHatch (const GS::Ref<Hatch>& hatch, const API_Coord& offset)
{
	memo.cWSegContour = reinterpret_cast<API_CWContourData*> (BMAllocatePtr ((1) * sizeof (API_CWContourData), ALLOCATE_CLEAR, 0));
	memo.coords = reinterpret_cast<API_Coord**> (BMAllocateHandle ((2 + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));
	(*memo.cWSegContour).polygon = hatch->GetHatchPoly ();
	(*memo.cWSegContour).coords = reinterpret_cast<API_Coord**> (BMAllocateHandle (((*memo.cWSegContour).polygon.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0));

	const auto& hatchMemo = hatch->GetMemo ();
	CopyCoords ((*memo.cWSegContour).polygon, hatchMemo.coords, (*memo.cWSegContour).coords);
	OffsetCoords (hatch->GetHatchPoly (), offset, (*memo.cWSegContour).coords);

	(*memo.cWSegContour).pends = reinterpret_cast<Int32**> (BMAllocateHandle (((*memo.cWSegContour).polygon.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0));

	CopyPends ((*memo.cWSegContour).polygon, hatchMemo.pends, (*memo.cWSegContour).pends);

	(*memo.cWSegContour).parcs = reinterpret_cast<API_PolyArc**> (BMAllocateHandle (((*memo.cWSegContour).polygon.nArcs) * sizeof (API_PolyArc), ALLOCATE_CLEAR, 0));

	CopyArcs ((*memo.cWSegContour).polygon, hatchMemo.parcs, (*memo.cWSegContour).parcs);

	(*memo.cWSegContour).vertexIDs = reinterpret_cast<UInt32**> (BMAllocateHandle (((*memo.cWSegContour).polygon.nCoords + 1) * sizeof (UInt32), ALLOCATE_CLEAR, 0));
	CopyVertexIDS ((*memo.cWSegContour).polygon, hatchMemo.vertexIDs, (*memo.cWSegContour).vertexIDs);
}

void CurtainWall::SetParam (const Selection& selection)
{
	(*memo.coords)[1].x = selection.geometry.minX;
	(*memo.coords)[1].y = selection.geometry.minY;
	(*memo.coords)[2].x = selection.geometry.maxX;
	(*memo.coords)[2].y = selection.geometry.minY;

	element.curtainWall.height = selection.geometry.height;
	element.curtainWall.nSegments = 1;
}

void CurtainWall::SetEmptyPattern ()
{
	memo.cWSegPrimaryPattern.nPattern = 1;
	memo.cWSegPrimaryPattern.logic = APICWSePL_NumberOfDivisions;
	memo.cWSegPrimaryPattern.nDivisions = 1;
	memo.cWSegPrimaryPattern.nFlexibleIDs = 1;
	memo.cWSegPrimaryPattern.endWithID = 0;
	BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegPrimaryPattern.pattern));
	memo.cWSegPrimaryPattern.pattern = reinterpret_cast<double*> (BMpAll (sizeof (double)));
	memo.cWSegPrimaryPattern.pattern[0] = 1;
	BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegPrimaryPattern.flexibleIDs));
	memo.cWSegPrimaryPattern.flexibleIDs = reinterpret_cast<UInt32*> (BMpAll (sizeof (UInt32)));
	memo.cWSegPrimaryPattern.flexibleIDs[0] = 0;

	memo.cWSegSecondaryPattern.nPattern = 1;
	memo.cWSegSecondaryPattern.logic = APICWSePL_NumberOfDivisions;
	memo.cWSegSecondaryPattern.nDivisions = 1;
	memo.cWSegSecondaryPattern.nFlexibleIDs = 1;
	memo.cWSegSecondaryPattern.endWithID = 0;
	BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegSecondaryPattern.pattern));
	memo.cWSegSecondaryPattern.pattern = reinterpret_cast<double*> (BMpAll (sizeof (double)));
	memo.cWSegSecondaryPattern.pattern[0] = 1;
	BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegSecondaryPattern.flexibleIDs));
	memo.cWSegSecondaryPattern.flexibleIDs = reinterpret_cast<UInt32*> (BMpAll (sizeof (UInt32)));
	memo.cWSegSecondaryPattern.flexibleIDs[0] = 0;

	BMpKill (reinterpret_cast<GSPtr*> (&memo.cWSegPatternCells));
	memo.cWSegPatternCells = reinterpret_cast<API_CWSegmentPatternCellData*> (BMpAll (sizeof (API_CWSegmentPatternCellData)));
	memo.cWSegPatternCells[0].crossingFrameType = APICWCFT_NoCrossingFrame;
	memo.cWSegPatternCells[0].leftPanelID = APICWPanelClass_Deleted;
	memo.cWSegPatternCells[0].rightPanelID = APICWPanelClass_Deleted;
	memo.cWSegPatternCells[0].leftFrameID = APICWFrameClass_Merged;
	memo.cWSegPatternCells[0].bottomFrameID = APICWFrameClass_Merged;
	memo.cWSegPatternCells[0].crossingFrameID = APICWFrameClass_Merged;
}

void CurtainWall::AddCustomFrames (const GS::Array<GS::Ref<Line>> selectedLines, const Selection& selection)
{
	const API_Coord origo = (*memo.coords)[1];
	const UInt32 nFrameClasses = BMpGetSize (reinterpret_cast<GSPtr> (memo.cWallFrameDefaults)) / sizeof (API_CWFrameType);
	DBASSERT (nFrameClasses > 0);

	memo.cWallFrames = reinterpret_cast<API_CWFrameType *> (BMpAll (sizeof (API_CWFrameType) * selectedLines.GetSize ()));

	for (UIndex ii = 0; ii < selectedLines.GetSize (); ++ii) {
		GS::FixArray<API_Coord, 2> BegEndRel = CurtainWallFromSelection::GetFrameRelCoordsFromLine (selectedLines[ii], origo, selection);
		memo.cWallFrames[ii] = memo.cWallFrameDefaults[nFrameClasses - 1];
		memo.cWallFrames[ii].begRel = BegEndRel[0];
		memo.cWallFrames[ii].endRel = BegEndRel[1];
		memo.cWallFrames[ii].segmentID = 0;
		memo.cWallFrames[ii].cellID = 0;
	}
}

UInt64 CurtainWall::GetMask () const
{
	return CWmask;
}

bool CurtainWall::IsCurrentCWFrameContour (UIndex ii) const
{
	return memo.cWallFrames[ii].contourID != 0;
}

GS::Array<Line> CurtainWall::GetFrameLines () const
{
	GS::Array<Line> lines;
	Line line;

	API_Coord origo = (*memo.coords)[1];

	const UInt32 nFrames = BMpGetSize (reinterpret_cast<GSPtr> (memo.cWallFrames)) / sizeof (API_CWFrameType);

	for (UIndex ii = 0; ii < nFrames; ++ii) {
		if (IsCurrentCWFrameContour (ii))
			continue;

		const auto& frame = memo.cWallFrames[ii];
		line.SetLTypeID (GetFrameLineTypeIndex (frame));

		API_Coord tmpBegRel, tmpEndRel;
		GetCoordsFromFrameLine (frame, origo, tmpBegRel, tmpEndRel);
		line.SetBegC (tmpBegRel);
		line.SetEndC (tmpEndRel);
		lines.Push (line);
	}

	return lines;
}


API_AttributeIndex CurtainWall::GetFrameLineTypeIndex (const API_CWFrameType& frame)
{
	if (frame.classID == 0)
		return LineTypeIndexes::GetInstance ().GetHiddenLineType ();
	return LineTypeIndexes::GetInstance ().GetBasicLineType ();
}


void CurtainWall::GetCoordsFromFrameLine (const API_CWFrameType& frame, const API_Coord& origo, API_Coord& begRel, API_Coord& endRel)
{
	begRel.x = (frame.begC.x);
	endRel.x = (frame.endC.x);
	begRel.y = (frame.begC.z + origo.y);
	endRel.y = (frame.endC.z + origo.y);
}