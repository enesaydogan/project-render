#include "APIEnvir.h"
#include "ACAPinc.h"

#include "APICommon.h"
#include "Element_Test.h"

// =============================================================================
//
// Modify elements
//
// =============================================================================


// -----------------------------------------------------------------------------
// Mirror a gable list
// (Helper function to the element edit functions)
// -----------------------------------------------------------------------------

static void	ChangeGables (double			wallLength,
							API_ElementMemo	*memo)
{
	Int32	nGable, i;

	if (memo->gables != nullptr) {
		nGable = BMGetHandleSize ((GSHandle) memo->gables) / sizeof (API_Gable);

		for (i = 0; i < nGable; i++) {
			(*memo->gables)[i].xb = -(*memo->gables)[i].nx * wallLength + (*memo->gables)[i].xb;
			(*memo->gables)[i].xe = -(*memo->gables)[i].nx * wallLength + (*memo->gables)[i].xe;
			(*memo->gables)[i].nx = -(*memo->gables)[i].nx;
			(*memo->gables)[i].ny = -(*memo->gables)[i].ny;

			(*memo->gables)[i].d = (*memo->gables)[i].d - (*memo->gables)[i].a * wallLength;
			(*memo->gables)[i].a = -(*memo->gables)[i].a;
			(*memo->gables)[i].b = -(*memo->gables)[i].b;
		}
	}

	return;
}		// ChangeGables


// -----------------------------------------------------------------------------
// Edit a window
//	 - double its width
// (Helper function to the element edit functions)
// -----------------------------------------------------------------------------

static void		UpdateOneWindow (const API_Guid& guid)
{
	API_Element		element = {};
	API_Coord		pos;
	API_Coord3D		c3;
	GSErrCode		err;

	element.header.guid = guid;

	err = ACAPI_Element_Get (&element);

	if (err == NoError) {
		API_Element mask = {};
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_WindowType, openingBase.width);

		element.window.openingBase.width *= 2.0;

		err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

		// Update LABEL (window)
		if (err == NoError) {
			API_Neig	neig = {};
			neig.neigID = APINeig_Wind;
			neig.guid = element.header.guid;
			neig.inIndex = 1;
			ACAPI_Element_NeigToCoord (&neig, &c3);
			pos.x = c3.x;
			pos.y = c3.y;
			UpdateAllLabels (element.header.guid, pos);
		}
	}

	return;
}		// UpdateOneWindow


// -----------------------------------------------------------------------------
// GSHandle windows after editing a wall
//	 - delete windows which do not fit into the wall
//	 - edit those whose position is valid
// (Helper function to the element edit functions)
// -----------------------------------------------------------------------------

static GSErrCode	UpdateOpenings (const API_Guid&	wallGuid,
									double			wallLength)
{
	GS::Array<API_Guid>	wallWindows;
	GSErrCode err = ACAPI_Grouping_GetConnectedElements (wallGuid, API_WindowID, &wallWindows);
	if (err == NoError) {
		GS::Array<API_Guid>	updWind;
		GS::Array<API_Guid>	delWind;
		for (UIndex i = 0; i < wallWindows.GetSize (); i++) {
			API_Element element = {};
			element.header.guid = wallWindows[i];
			err = ACAPI_Element_Get (&element);
			if (err == NoError) {
				if (element.window.objLoc < wallLength) {
					updWind.Push (element.header.guid);
				} else {
					delWind.Push (element.header.guid);
				}
			} else
				break;
		}

		if (err == NoError) {
			for (UIndex i = 0; i < updWind.GetSize (); i++)
				UpdateOneWindow (updWind[i]);
		}

		ACAPI_Element_Delete (delWind);
	}

	return NoError;
}		// UpdateOpenings


// -----------------------------------------------------------------------------
// Modify the reference points of a the clicked wall
//	- dimensions/labels must be updated automatically
//	- windows most be kept
// -----------------------------------------------------------------------------

void		Do_Wall_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo = {};
	API_Coord3D			begC, newC;
	API_Coord			c1, c2, mid;
	double				angle, wallLength;
	bool				directionChange;
	Int32				memoMask;

	if (!ClickAnElem ("Click a wall to modify", API_WallID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No wall was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError)
		return;

	begC.x = element.wall.begC.x;
	begC.y = element.wall.begC.y;
	begC.z = 0.0;
	if (!GetEditVector (&begC, &newC, "Enter drag reference point", true))
		return;

	angle = -30 * DEGRAD;
	directionChange = (angle < 0.0);

	c1 = element.wall.begC;
	c2.x = newC.x;
	c2.y = newC.y;

	wallLength = DistCPtr (&c1, &c2);

	if (err == NoError) {
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_WallType, begC);
		ACAPI_ELEMENT_MASK_SET (mask, API_WallType, endC);
		if (fabs (element.wall.angle) < EPS) {
			ACAPI_ELEMENT_MASK_SET (mask, API_WallType, type);
			ACAPI_ELEMENT_MASK_SET (mask, API_WallType, thickness);
			ACAPI_ELEMENT_MASK_SET (mask, API_WallType, thickness1);
		}
		ACAPI_ELEMENT_MASK_SET (mask, API_WallType, zoneRel);

		element.wall.begC = c1;
		element.wall.endC = c2;
		if (fabs (element.wall.angle) < EPS) {
			element.wall.type = APIWtyp_Trapez;
			element.wall.thickness = 0.28;
			element.wall.thickness1 = 1.28;
		}
		element.wall.zoneRel = APIZRel_SubtractFromZone;

		if (directionChange) {
			memoMask = APIMemoMask_Gables;
			err = ACAPI_Element_GetMemo (element.header.guid, &memo, memoMask);
			if (err == NoError && memo.gables != nullptr)
				ChangeGables (wallLength, &memo);
			else
				memoMask = 0;
		} else
			memoMask = 0;

		err = ACAPI_Element_Change (&element, &mask, &memo, memoMask, true);

		if (err == NoError) {
			mid.x = (element.wall.begC.x + element.wall.endC.x) / 2.0;
			mid.y = (element.wall.begC.y + element.wall.endC.y) / 2.0;
			UpdateAllLabels (element.header.guid, mid);
		}

		if (err == NoError)
			err = UpdateOpenings (element.header.guid, wallLength);
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Wall_Edit


static void TestCurtainWallPanelsOrder (const API_Guid& cwGuid)
{
    GS::Array<API_CWPanelType> panelsInOrder;

    API_Element	cwElement = {};
    cwElement.header.guid = cwGuid;

    GSErrCode err = ACAPI_Element_Get (&cwElement);
    if (DBVERIFY (err == NoError)) {
        API_ElementMemo	cwMemo = {};
        err = ACAPI_Element_GetMemo (cwElement.header.guid, &cwMemo);
        if (DBVERIFY (err == NoError && cwMemo.cWallSegments != nullptr && cwMemo.cWallPanels != nullptr && cwMemo.cWallPanelGridIDTable != nullptr)) {
            GS::HashTable<API_Guid, API_CWPanelType> cwPanelTable;
            for (UIndex ii = 0; ii < cwElement.curtainWall.nPanels; ++ii) {
                const API_CWPanelType& cwPanel = cwMemo.cWallPanels[ii];
                cwPanelTable.Add (cwPanel.head.guid, cwPanel);
            }

            using CWSegmentGridCellID = GS::Pair<UInt32, API_GridElemID>;
            GS::HashTable<CWSegmentGridCellID, GS::Array<API_CWPanelType>> panelsInSegmentGridCells;
            for (auto it = cwMemo.cWallPanelGridIDTable->EnumeratePairs (); it != nullptr; ++it) {
                const API_CWPanelType& cwPanel = cwPanelTable[it->key];
                const GS::Array<API_GridElemID>& cwPanelGridIDs = it->value;

                for (auto it = cwPanelGridIDs.Enumerate (); it != nullptr; ++it) {
                    const API_GridElemID& gridID = *it;

                    CWSegmentGridCellID segmentGridCellID (cwPanel.segmentID, gridID);
                    if (panelsInSegmentGridCells.ContainsKey (segmentGridCellID))
                        panelsInSegmentGridCells[segmentGridCellID].Push (cwPanel);
                    else
                        panelsInSegmentGridCells.Add (segmentGridCellID, { cwPanel });
                }
            }

            for (UIndex ii = 0; ii < cwElement.curtainWall.nSegments; ++ii) {
                const API_CWSegmentType& cwSegment = cwMemo.cWallSegments[ii];
                API_ElementMemo	cwSegmentMemo = {};
                err = ACAPI_Element_GetMemo (cwSegment.head.guid, &cwSegmentMemo);
                if (DBVERIFY (err == NoError && cwSegmentMemo.cWSegGridMesh != nullptr)) {
                    for (const API_GridMeshLine& mainLine : cwSegmentMemo.cWSegGridMesh->meshLinesMainAxis) {
                        for (const API_GridMeshLine& secLine : cwSegmentMemo.cWSegGridMesh->meshLinesSecondaryAxis) {
                            CWSegmentGridCellID segmentGridCellID (ii, mainLine.head.elemID << 16 | secLine.head.elemID);
                            if (panelsInSegmentGridCells.ContainsKey (segmentGridCellID))
                                panelsInOrder.Append (panelsInSegmentGridCells.Get (segmentGridCellID));
                        }
                    }
                }

                ACAPI_DisposeElemMemoHdls (&cwSegmentMemo);
            }
        }

        ACAPI_DisposeElemMemoHdls (&cwMemo);
    }
}


// -----------------------------------------------------------------------------
// Modify the pattern of a the clicked curtain wall
// -----------------------------------------------------------------------------

void		Do_CurtainWall_Edit (void)
{
	API_Element element = {};
	API_Element mask;
	API_ElementMemo memo = {};

	if (!ClickAnElem ("Click a Curtain Wall to Modify", API_CurtainWallID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No curtain wall was clicked");
		return;
	}

    TestCurtainWallPanelsOrder (element.header.guid);

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError)
		return;

	const UInt64 memoMask = APIMemoMask_CWSegPrimaryPattern | APIMemoMask_CWSegSecPattern |
						    APIMemoMask_CWSegPanelPattern | APIMemoMask_CWallFrameClasses |
						    APIMemoMask_CWallPanelClasses;
	err = ACAPI_Element_GetMemo (element.header.guid, &memo, memoMask);
	if (err != NoError)
		return;

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_CurtainWallType, nFrameDefaults);
	ACAPI_ELEMENT_MASK_SET (mask, API_CurtainWallType, nPanelDefaults);
	ACAPI_ELEMENT_MASK_SET (mask, API_CurtainWallType, zoneRel);

	element.curtainWall.zoneRel = APIZRel_SubtractFromZone;

	// Add a Frame Class: "API Test Frame"
	{
		auto newFrameClasses = reinterpret_cast<API_CWFrameType*> (BMpAll ((element.curtainWall.nFrameDefaults + 1) * sizeof (API_CWFrameType)));
		for (UIndex i = 0; i < element.curtainWall.nFrameDefaults; ++i) {
			newFrameClasses[i] = memo.cWallFrameDefaults[i];
		}
		newFrameClasses[element.curtainWall.nFrameDefaults] = element.curtainWall.boundaryFrameData;

		newFrameClasses[element.curtainWall.nFrameDefaults].useOwnMaterial = false;
		newFrameClasses[element.curtainWall.nFrameDefaults].material = ACAPI_CreateAttributeIndex (48);
		GS::ucscpy (newFrameClasses[element.curtainWall.nFrameDefaults].className, GS::UniString ("API Test Frame").ToUStr ().Get ());

		BMpKill (reinterpret_cast<GSPtr*> (&memo.cWallFrameDefaults));
		memo.cWallFrameDefaults = newFrameClasses;
		element.curtainWall.nFrameDefaults++;
	}

	// Add a Panel Class: "API Test Panel"
	{
		auto newPanelClasses = reinterpret_cast<API_CWPanelType*> (BMpAll ((element.curtainWall.nPanelDefaults + 1) * sizeof (API_CWPanelType)));
		for (UIndex i = 0; i < element.curtainWall.nPanelDefaults; ++i) {
			newPanelClasses[i] = memo.cWallPanelDefaults[i];
		}
		if (element.curtainWall.nPanelDefaults > 1)
			newPanelClasses[element.curtainWall.nPanelDefaults] = memo.cWallPanelDefaults[0];

		newPanelClasses[element.curtainWall.nPanelDefaults].outerSurfaceMaterial = ACAPI_CreateAttributeIndex (100);
		newPanelClasses[element.curtainWall.nPanelDefaults].innerSurfaceMaterial = ACAPI_CreateAttributeIndex (100);
		newPanelClasses[element.curtainWall.nPanelDefaults].cutSurfaceMaterial = ACAPI_CreateAttributeIndex (100);
		GS::ucscpy (newPanelClasses[element.curtainWall.nPanelDefaults].className, GS::UniString ("API Test Panel").ToUStr ().Get ());

		BMpKill (reinterpret_cast<GSPtr*> (&memo.cWallPanelDefaults));
		memo.cWallPanelDefaults = newPanelClasses;
		element.curtainWall.nPanelDefaults++;
	}

	const double blockSize = element.curtainWall.height / memo.cWSegPrimaryPattern.nPattern;

	// Modify the Primary Pattern
	for (UIndex ii = 0; ii < memo.cWSegPrimaryPattern.nPattern; ++ii) {
		memo.cWSegPrimaryPattern.pattern[ii] = blockSize;
	}

	// Modify the Secondary Pattern
	for (UIndex ii = 0; ii < memo.cWSegSecondaryPattern.nPattern; ++ii) {
		memo.cWSegSecondaryPattern.pattern[ii] = blockSize;
	}

	// Fill the Pattern Cells
	const USize nCells = memo.cWSegPrimaryPattern.nPattern * memo.cWSegSecondaryPattern.nPattern;
	for (UIndex ii = 0; ii < nCells;  ++ii) {
		memo.cWSegPatternCells[ii].crossingFrameType = ((ii % 2) != 0) ? APICWCFT_FromTopLeftToBottomRight : APICWCFT_FromBottomLeftToTopRight;
		memo.cWSegPatternCells[ii].rightPanelID = APICWPanelClass_FirstCustomClass + element.curtainWall.nPanelDefaults - 1;
		memo.cWSegPatternCells[ii].leftPanelID = APICWPanelClass_Deleted;
		memo.cWSegPatternCells[ii].crossingFrameID = APICWFrameClass_FirstCustomClass + element.curtainWall.nFrameDefaults - 1;
	}

	err = ACAPI_Element_Change (&element, &mask, &memo, memoMask, true);
	ACAPI_DisposeElemMemoHdls (&memo);

	if (err != NoError)
		return;

	err = ACAPI_Element_Get (&element);
	if (err != NoError)
		return;

	err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_CWallPanels | APIMemoMask_CWallFrames);

	UInt32 counter = 0;
	for (UInt32 i = 0; i < element.curtainWall.nPanels; ++i) {
        API_Element panelElem = {};
		panelElem.header.guid = memo.cWallPanels[i].head.guid;

		err = ACAPI_Element_Get (&panelElem);
		if (err != NoError)
			continue;

		if (panelElem.cwPanel.classID == APICWPanelClass_Deleted)
			continue;

        API_Element panelMask = {};

		switch (counter % 4) {
			case 0: // Delete Panel
				ACAPI_ELEMENT_MASK_SET (panelMask, API_CWPanelType, classID);
				panelElem.cwPanel.classID = APICWPanelClass_Deleted;
				break;

			case 1: // Change Panel Class
				ACAPI_ELEMENT_MASK_SET (panelMask, API_CWPanelType, classID);
				panelElem.cwPanel.classID = APICWPanelClass_FirstCustomClass + 1;
				break;

			case 2: // Customize Panel
				ACAPI_ELEMENT_MASK_SET (panelMask, API_CWPanelType, classID);
				ACAPI_ELEMENT_MASK_SET (panelMask, API_CWPanelType, thickness);
				panelElem.cwPanel.classID = APICWPanelClass_Customized;
				panelElem.cwPanel.thickness *= 3;
				break;

			case 3: // Continue
			default:
				break;
		}

		if (counter % 4 < 3) {
			err = ACAPI_Element_Change (&panelElem, &panelMask, nullptr, 0UL, true);
			if (err != NoError) {
				ACAPI_WriteReport ("ACAPI_Element_Change (CW Panel) returned "
								   "with error value %ld!", false, err);
			}
		}

		++counter;
	}

	// Set Internal (Non-Boundary) Frames to the Generated Class
	for (UInt32 i = 0; i < element.curtainWall.nFrames; ++i) {
        API_Element frameElem = {};
		frameElem.header.guid = memo.cWallFrames[i].head.guid;

		err = ACAPI_Element_Get (&frameElem);
		if (err != NoError)
			continue;

		if (frameElem.cwFrame.classID == APICWFrameClass_Boundary)
			continue;

        API_Element frameMask = {};
        ACAPI_ELEMENT_MASK_SET (frameMask, API_CWFrameType, classID);
        frameElem.cwFrame.classID = APICWFrameClass_FirstCustomClass +
        							element.curtainWall.nFrameDefaults - 1;

		err = ACAPI_Element_Change (&frameElem, &frameMask, nullptr, 0UL, true);
		if (err != NoError) {
			ACAPI_WriteReport ("ACAPI_Element_Change (CW Frame) returned "
							   "with error value %ld!", false, err);
		}
    }

    TestCurtainWallPanelsOrder (element.header.guid);

	ACAPI_DisposeElemMemoHdls (&memo);
}		// Do_CurtainWall_Edit


// -----------------------------------------------------------------------------
// Change the shape of the clicked column
//	- dimensions/labels must be updated automatically
// -----------------------------------------------------------------------------

void		Do_Column_Edit (void)
{
	API_Element element = {}, mask;

	if (!ClickAnElem ("Click a column to modify", API_ColumnID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No column was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, origoPos);
	ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, axisRotationAngle);
	ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, zoneRel);

	element.column.origoPos.x += 0.5;
	element.column.origoPos.y += 0.5;
	element.column.axisRotationAngle = 45 * DEGRAD;
	element.column.zoneRel = APIZRel_SubtractFromZone;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	if (err == NoError)
		UpdateAllLabels (element.header.guid, element.column.origoPos);

	return;
}		// Do_Column_Edit


// -----------------------------------------------------------------------------
// Enlarge holes of the clicked beam
//	- dimensions/labels must be updated automatically
// -----------------------------------------------------------------------------

void		Do_Beam_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo = {};
	Int32				nHole, i;

	if (!ClickAnElem ("Click a beam to enlarge its holes", API_BeamID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No beam was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err == NoError)
		err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_BeamHole);

	if (err != NoError)
		return;

	nHole = BMGetHandleSize ((GSHandle) memo.beamHoles) / sizeof (API_Beam_Hole);
	for (i = 0; i < nHole; i++) {
		(*memo.beamHoles)[i].width *= 3.0;
		(*memo.beamHoles)[i].height *= 2.0;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
	ACAPI_ELEMENT_MASK_SET (mask, API_BeamType, aboveViewLinePen);

	element.header.layer = APIApplicationLayerAttributeIndex;
	element.beam.aboveViewLinePen = 10;

	err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_BeamHole, true);

	if (err == NoError) {
		API_Coord mid;
		mid.x = (element.beam.begC.x + element.beam.endC.x) / 3.0;
		mid.y = (element.beam.begC.y + element.beam.endC.y) / 3.0;
		UpdateAllLabels (element.header.guid, mid);
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Beam_Edit


// -----------------------------------------------------------------------------
// Change a window fixPoint
//	- dimensions/labels must be updated automatically
// -----------------------------------------------------------------------------

void		Do_Window_Edit (void)
{
	API_Element		element = {}, mask;
	API_Coord3D		begC;

	if (!ClickAnElem ("Click a window to modify", API_WindowID, nullptr, &element.header.type, &element.header.guid, &begC)) {
		WriteReport_Alert ("No window was clicked");
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);

	ACAPI_ELEMENT_MASK_SET (mask, API_WindowType, fixPoint);
	element.window.fixPoint = APIHoleAnchor_BegFix;

	GSErrCode err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

	if (err == NoError) {
		API_Coord c;
		c.x = begC.x;
		c.y = begC.y;
		UpdateAllLabels (element.header.guid, c);
	}

	return;
}		// Do_Window_Edit


// -----------------------------------------------------------------------------
// Change a skylight fixPoint
//	- dimensions/labels must be updated automatically
// -----------------------------------------------------------------------------

void		Do_Skylight_Edit (void)
{
	API_Element			element = {}, mask;
	API_Coord3D			begC;
	API_Coord			c;

	if (!ClickAnElem ("Click a skylight to modify", API_SkylightID, nullptr, &element.header.type, &element.header.guid, &begC)) {
		WriteReport_Alert ("No skylight was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_SkylightType, anchorPoint);
	ACAPI_ELEMENT_MASK_SET (mask, API_SkylightType, anchorPosition);

	element.skylight.anchorPoint = APISkylightAnchor_TL;
	element.skylight.anchorPosition.x += 2.0;
	element.skylight.anchorPosition.y += 1.0;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

	if (err == NoError) {
		c.x = begC.x;
		c.y = begC.y;
		UpdateAllLabels (element.header.guid, c);
	}

	return;
}		// Do_Skylight_Edit


// -----------------------------------------------------------------------------
// Drag and rotate an object
//	- dimensions/labels must be updated automatically
// -----------------------------------------------------------------------------

void		Do_Object_Edit (void)
{
	API_Element element = {}, mask;

	if (!ClickAnElem ("Click an object to modify", API_ObjectID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No object was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_ObjectType, angle);
	ACAPI_ELEMENT_MASK_SET (mask, API_ObjectType, pos);
	element.object.angle += PI / 2.0;	// 90 degrees
	element.object.pos.x += 2.0;
	element.object.pos.y += 1.0;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0UL, true);
	if (err != NoError) {
		ErrorBeep("ACAPI_Element_Change", err);
	}

	return;
}		// Do_Object_Edit


// -----------------------------------------------------------------------------
// Delete the clicked slab node and change the pen
//	- dimensions/labels must be updated automatically
// -----------------------------------------------------------------------------

void	Do_Ceil_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo;
	API_Neig			neig;
	Int32				nodeInd;

	if (!ClickAnElem ("Click a slab to modify", API_SlabID, &neig, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No slab was clicked");
		return;
	}

	if (neig.neigID != APINeig_Ceil) {
		WriteReport_Alert ("Only slab nodes are accepted");
		return;
	}

	GSErrCode err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_Polygon);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	nodeInd = neig.inIndex;
	err = ACAPI_Polygon_DeletePolyNode (&memo, &nodeInd);

	if (err == NoError) {
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, pen);

		element.slab.pen = 10;

		element.slab.poly.nCoords = BMhGetSize ((GSHandle) memo.coords) / Sizeof32 (API_Coord) - 1;
		element.slab.poly.nSubPolys = BMhGetSize ((GSHandle) memo.pends) / Sizeof32 (Int32) - 1;
		element.slab.poly.nArcs = BMhGetSize ((GSHandle) memo.parcs) / Sizeof32 (API_PolyArc);

		err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_Polygon | APIMemoMask_SideMaterials | APIMemoMask_EdgeTrims, true);
		if (err == NoError) {
			API_Coord c = (*memo.coords)[1];
			UpdateAllLabels (element.header.guid, c);
		}
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Ceil_Edit


// -----------------------------------------------------------------------------
// Add a new node to the clicked roof and reset side angles to vertical
//	- dimensions/labels must be updated automatically
// -----------------------------------------------------------------------------

void	Do_Roof_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo;
	API_Neig			neig;
	API_Coord3D			begC, addC;
	Int32				nodeInd;

	if (!ClickAnElem ("Click on a roof contour edge to modify", API_RoofID, &neig, &element.header.type, &element.header.guid, &begC)) {
		WriteReport_Alert ("No roof was clicked");
		return;
	}

	if (neig.neigID != APINeig_RoofOn) {
		WriteReport_Alert ("Only roof edges are accepted");
		return;
	}

	if (!GetEditVector (&begC, &addC, "", true))
		return;

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_Polygon);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	nodeInd = neig.inIndex + 1;
	API_Coord toInsert = {};
	toInsert.x = addC.x;
	toInsert.y = addC.y;
	err = ACAPI_Polygon_InsertPolyNode (&memo, &nodeInd, &toInsert);
	if (err == NoError) {
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.pen);
		element.roof.shellBase.pen = 10;

		if (element.roof.roofClass == API_PlaneRoofID) {
			element.roof.u.planeRoof.poly.nCoords = BMhGetSize ((GSHandle) memo.coords) / Sizeof32 (API_Coord) - 1;
			element.roof.u.planeRoof.poly.nSubPolys = BMhGetSize ((GSHandle) memo.pends) / Sizeof32 (Int32) - 1;
			element.roof.u.planeRoof.poly.nArcs = BMhGetSize ((GSHandle) memo.parcs) / Sizeof32 (API_PolyArc);
		} else {
			element.roof.u.polyRoof.contourPolygon.nCoords = BMhGetSize ((GSHandle) memo.coords) / Sizeof32 (API_Coord) - 1;
			element.roof.u.polyRoof.contourPolygon.nSubPolys = BMhGetSize ((GSHandle) memo.pends) / Sizeof32 (Int32) - 1;
			element.roof.u.polyRoof.contourPolygon.nArcs = BMhGetSize ((GSHandle) memo.parcs) / Sizeof32 (API_PolyArc);
		}

		err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_Polygon | APIMemoMask_SideMaterials | APIMemoMask_EdgeTrims | APIMemoMask_RoofEdgeTypes, true);		/* memo.edgeTrims = nullptr --> reset */
		if (err == NoError) {
			API_Coord c = (*memo.coords)[1];
			UpdateAllLabels (element.header.guid, c);
		}
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Roof_Edit


// -----------------------------------------------------------------------------
// Toggle bulges to the (1st) profile polygon of the clicked shell
//	- dimensions/labels must be updated automatically (???)
// -----------------------------------------------------------------------------

void	Do_Shell_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo;
	API_Neig			neig;
	API_Coord3D			begC;

	if (!ClickAnElem ("Click a shell to modify", API_ShellID, &neig, &element.header.type, &element.header.guid, &begC)) {
		WriteReport_Alert ("No shell was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_Polygon);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	API_Polygon& shellShape = (element.shell.shellClass == API_ExtrudedShellID) ? element.shell.u.extrudedShell.shellShape :
								((element.shell.shellClass == API_RevolvedShellID) ? element.shell.u.revolvedShell.shellShape : element.shell.u.ruledShell.shellShape1);

	if (memo.shellShapes[0].parcs != nullptr) {
		Int32	i;
		for (i = 0; i < shellShape.nArcs; i++)
			(*memo.shellShapes[0].parcs)[i].arcAngle /= 2.0;

		err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_Polygon | APIMemoMask_EdgeTrims, true);		/* memo.edgeTrims = nullptr --> reset */
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Shell_Edit


// -----------------------------------------------------------------------------
// Punch a hole to the shell
// -----------------------------------------------------------------------------

void	Do_Shell_Edit2 (void)
{
	API_Element			mask;
	API_ElementMemo		memo;
	API_ElemType		type;
	API_Guid			guid;
	API_Neig			neig;
	GSErrCode			err = NoError;

	if (!ClickAnElem ("Click a shell to modify", API_ShellID, &neig, &type, &guid)) {
		WriteReport_Alert ("No shell was clicked");
		return;
	}

	API_GetPointType	pt = {};
	if (ACAPI_UserInput_GetPoint (&pt) != NoError) {
		ErrorBeep ("ACAPI_UserInput_GetPoint (...)", err);
		return;
	}
	API_GetPolyType	poly = {};
	poly.startCoord = pt.pos;
	poly.method = APIPolyGetMethod_General;
	if (ACAPI_UserInput_GetPoly (&poly) != NoError) {
		ErrorBeep ("ACAPI_UserInput_GetPoly (...)", err);
		return;
	}

	API_Element element = {};
	element.header.type = type;
	element.header.guid	= guid;
	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	err = ACAPI_Element_GetMemo (guid, &memo, APIMemoMask_Polygon);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	API_ShellContourData* oldmemos = memo.shellContours;
	Int32	nold = BMGetPtrSize ((GSPtr) (memo.shellContours)) / sizeof (API_ShellContourData);
	memo.shellContours = (API_ShellContourData*) BMAllocatePtr ((nold + 1) * sizeof (API_ShellContourData), ALLOCATE_CLEAR, 0);
	if (memo.shellContours != nullptr) {
		Int32	i;
		for (i = 0; i < nold; i++) {
			memo.shellContours[i] = oldmemos[i];
		}
		memo.shellContours[nold].poly.nCoords = poly.nCoords;
		memo.shellContours[nold].poly.nSubPolys = 1;
		memo.shellContours[nold].poly.nArcs = poly.nArcs;
		memo.shellContours[nold].coords = poly.coords;
		memo.shellContours[nold].pends = (Int32**) BMAllocateHandle (2 * sizeof (Int32), ALLOCATE_CLEAR, 0);
		(*(memo.shellContours[nold].pends))[1] = poly.nCoords;
		memo.shellContours[nold].parcs = poly.parcs;

		memo.shellContours[nold].plane.tmx[0] = 1;
		memo.shellContours[nold].plane.tmx[5] = 1;
		memo.shellContours[nold].plane.tmx[10] = 1;

		memo.shellContours[nold].plane.tmx[3] = -element.shell.basePlane.tmx[3];
		memo.shellContours[nold].plane.tmx[7] = -element.shell.basePlane.tmx[7];
		memo.shellContours[nold].plane.tmx[11] = -element.shell.basePlane.tmx[11] - 50.0;
		memo.shellContours[nold].height = 100.0;

		memo.shellContours[nold].edgeData = (API_ContourEdgeData*) BMAllocatePtr ((poly.nCoords + 1) * sizeof (API_ContourEdgeData), ALLOCATE_CLEAR, 0);

		element.shell.numHoles++;

		err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_Polygon | APIMemoMask_EdgeTrims, true);
	}

	BMKillPtr ((GSPtr*) &oldmemos);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Shell_Edit2


// -----------------------------------------------------------------------------
// Set different material to morph faces, change transformation and cover fill
// -----------------------------------------------------------------------------

void	Do_Morph_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo;

	if (!ClickAnElem ("Click a morph to modify", API_MorphID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No morph was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	err = ACAPI_Element_GetMemo (element.header.guid, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);

	// change transformation
	ACAPI_ELEMENT_MASK_SET (mask, API_MorphType, tranmat);
	double co = cos (15.0 * DEGRAD);
	double si = sin (15.0 * DEGRAD);
	API_Tranmat origTran = element.morph.tranmat;
	double* tmx = element.morph.tranmat.tmx;
	for (Int32 j = 0; j < 4; j++) {
		tmx[j]		=	co*origTran.tmx[j] + si*origTran.tmx[8 + j];
		tmx[8 + j]	= -si*origTran.tmx[j] + co*origTran.tmx[8 + j];
	}

	// change cover fill
	ACAPI_ELEMENT_MASK_SET (mask, API_MorphType, coverFillType);
	ACAPI_ELEMENT_MASK_SET (mask, API_MorphType, coverFillPen);
	ACAPI_ELEMENT_MASK_SET (mask, API_MorphType, coverFillBGPen);
	ACAPI_ELEMENT_MASK_SET (mask, API_MorphType, coverFillOrientation);
	ACAPI_ELEMENT_MASK_SET (mask, API_MorphType, useDistortedCoverFill);
	element.morph.coverFillType = ACAPI_CreateAttributeIndex (32);
	element.morph.coverFillPen = 1;
	element.morph.coverFillBGPen = 254;
	element.morph.coverFillOrientation.type = API_HatchRotated;
	element.morph.coverFillOrientation.matrix00 = cos (30.0 * DEGRAD);
	element.morph.coverFillOrientation.matrix10 = sin (30.0 * DEGRAD);
	element.morph.coverFillOrientation.matrix01 = -sin (30.0 * DEGRAD);
	element.morph.coverFillOrientation.matrix11 = cos (30.0 * DEGRAD);
	element.morph.useDistortedCoverFill = false;

	// change face materials
	if (memo.morphMaterialMapTable != nullptr) {
		USize mapSize = BMGetPtrSize ((GSPtr)memo.morphMaterialMapTable) / sizeof (API_OverriddenAttribute);
		if (mapSize > 0) {
			API_AttributeIndex materIndex = memo.morphMaterialMapTable[0].value;
			GS::Array<API_Attribute> attributes;
			ACAPI_Attribute_GetAttributesByType (API_MaterialID, attributes);
			UIndex currentMaterialArrayIndex = 0;
			for (UIndex i = 0; i < attributes.GetSize (); i++) {
				if (attributes[i].header.index == materIndex)
					currentMaterialArrayIndex = i;

				if (attributes[i].material.texture.fileLoc != nullptr) {
					delete attributes[i].material.texture.fileLoc;
					attributes[i].material.texture.fileLoc = nullptr;
				}
			}

			for (UIndex i = 0; i < mapSize; i++) {
				currentMaterialArrayIndex++;
				materIndex = attributes[currentMaterialArrayIndex].header.index;
				memo.morphMaterialMapTable[i] = materIndex;

				if (currentMaterialArrayIndex >= attributes.GetSize ()-1)
					currentMaterialArrayIndex = 0;
			}
		}
	}

	err = ACAPI_Element_Change (&element, &mask, &memo, 0, true);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Morph_Edit


// -----------------------------------------------------------------------------
// Change width, riser and tread thickness, and slanting
// -----------------------------------------------------------------------------

void	Do_Stair_Edit (void)
{
	constexpr double Pi = 3.141592654;
	constexpr int Run = static_cast<int> (APISR_Run);

	API_Element element = {};
	if (!ClickAnElem ("Click a Stair to modify.", API_StairID, nullptr, &element.header.type, &element.header.guid)) {
		ACAPI_WriteReport ("No Stair was clicked.", true);
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Get (Stair) has failed with error %s!", true, ErrID_To_Name (err));
		return;
	}

	API_Element mask;
	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_StairType, flightWidth);
	ACAPI_ELEMENT_MASK_SET (mask, API_StairType, riser[Run].crossSect);
	ACAPI_ELEMENT_MASK_SET (mask, API_StairType, riser[Run].angle);
	ACAPI_ELEMENT_MASK_SET (mask, API_StairType, riser[Run].thickness);
	ACAPI_ELEMENT_MASK_SET (mask, API_StairType, tread[Run].thickness);

	element.stair.flightWidth *= 2.5;
	element.stair.riser[Run].crossSect = APIRCS_Slanted;
	element.stair.riser[Run].angle = Pi / 3;
	element.stair.riser[Run].thickness *= 3.0;
	element.stair.tread[Run].thickness = element.stair.riser[Run].thickness;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Change (Stair) has failed with error %s!", true, ErrID_To_Name (err));
		return;
	}
}		// Do_Stair_Edit


// -----------------------------------------------------------------------------
// Change segment heights of a railing.
// -----------------------------------------------------------------------------

void	Do_Railing_Edit (void)
{
	API_Element element = {};
	if (!ClickAnElem ("Click a Railing to modify.", API_RailingID, nullptr, &element.header.type, &element.header.guid)) {
		ACAPI_WriteReport ("No Railing was clicked.", true);
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Get (Railing) has failed with error %s!", true, ErrID_To_Name (err));
		return;
	}

	API_Element mask = {};
	ACAPI_ELEMENT_MASK_SET (mask, API_RailingType, defSegment.height);
	ACAPI_ELEMENT_MASK_SET (mask, API_RailingType, defNode.postOffset);
	element.railing.defSegment.height *= 1.5;
	element.railing.defNode.postOffset *= 2;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Change (Railing) has failed with error %s!", true, ErrID_To_Name (err));
		return;
	}
}		// Do_Railing_Edit


// -----------------------------------------------------------------------------
// Elevate the first node of a railing.
// -----------------------------------------------------------------------------

void	Do_Railing_Elevate_Node (void)
{
	API_Element element = {};
	if (!ClickAnElem ("Click a Railing to modify.", API_RailingID, nullptr, &element.header.type, &element.header.guid)) {
		ACAPI_WriteReport ("No Railing was clicked.", true);
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Get (Railing) has failed with error %s!", true, ErrID_To_Name (err));
		return;
	}

	if (DBERROR (element.railing.nNodes < 1)) {
		ACAPI_WriteReport ("Selected Railing has no Nodes!", true);
		return;
	}
	
	API_ElementMemo elemMemo;
	err = ACAPI_Element_GetMemo (element.header.guid, &elemMemo);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_GetMemo (Railing) has failed with error %s!", true, ErrID_To_Name (err));
		return;
	}

	API_Element nodeElem = {};
	nodeElem.header = elemMemo.railingNodes[0].head;

	err = ACAPI_Element_Get (&nodeElem);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Get (First Node) has failed with error %s!", true, ErrID_To_Name (err));
		return;
	}

	API_Element nodeMask = {};
	ACAPI_ELEMENT_MASK_SET (nodeMask, API_RailingNodeType, elevation);
	nodeElem.railingNode.elevation += 1.0;

	err = ACAPI_Element_Change (&nodeElem, &nodeMask, nullptr, 0, true);
	if (err != NoError) {
		ACAPI_WriteReport ("ACAPI_Element_Change (Railing) has failed with error %s!", true, ErrID_To_Name (err));
		return;
	}
}		// Do_Railing_Elevate_Node


// -----------------------------------------------------------------------------
// Change the contour edges and ridges of a mesh
//	- dimensions/labels must be updated automatically
// -----------------------------------------------------------------------------


void	Do_Mesh_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo;

	if (!ClickAnElem ("Click a mesh to modify", API_MeshID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No mesh was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_Polygon | APIMemoMask_MeshPolyZ | APIMemoMask_MeshLevel);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, contPen);

	element.mesh.contPen = element.mesh.contPen % 255 + 1;

	BMKillHandle ((GSHandle *) &memo.parcs);
	if (element.mesh.poly.nArcs == 0) {
		element.mesh.poly.nArcs = element.mesh.poly.nCoords - 1;
		memo.parcs = (API_PolyArc**) BMhAllClear (element.mesh.poly.nArcs * sizeof (API_PolyArc));
		if (memo.parcs != nullptr) {
			for (Int32 i = 0; i < element.mesh.poly.nArcs; i++) {
				(*memo.parcs)[i].begIndex = i + 1;
				(*memo.parcs)[i].endIndex = i + 2;
				(*memo.parcs)[i].arcAngle = 0.5;
			}
		}
	} else {
		element.mesh.poly.nArcs = 0;
	}

	if (memo.meshLevelCoords != nullptr) {
		Int32 nMeshLevels = BMGetHandleSize ((GSHandle) memo.meshLevelCoords) / sizeof (API_MeshLevelCoord);
		for (Int32 i = 0; i < nMeshLevels; i++) {
			(*memo.meshLevelCoords)[i].c.z = 1.0 - (double)i / nMeshLevels;
		}
	}

	err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_Polygon | APIMemoMask_MeshLevel, true);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Mesh_Edit



// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------

void		Do_Modify_Opening (void)
{
	API_Element element = {}, mask;

	if (!ClickAnElem ("Click an element containing Openings", API_ZombieElemID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No object was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	GS::Array<API_Guid>		openingsGuid;
	ACAPI_Grouping_GetConnectedElements (element.header.guid, API_OpeningID, &openingsGuid);

	element				= {};
	element.header.type = API_OpeningID;
	err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (opening)", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_OpeningType, extrusionGeometryData.parameters.width);		// extrusionGeometryData.parameters.width

	for (UIndex i = 0; i < openingsGuid.GetSize (); i++) {
		element.header.guid = openingsGuid[i];

		err = ACAPI_Element_Change (&element, &mask, nullptr, 0UL, true);
		if (err != NoError) {
			ErrorBeep ("ACAPI_Element_Change", err);
		}
	}

	return;
}		// Do_Modify_Opening