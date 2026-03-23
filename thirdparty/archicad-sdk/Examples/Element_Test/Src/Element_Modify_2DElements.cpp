// *****************************************************************************
// Source code for the Element Test Add-On
// *****************************************************************************

#define	_ELEMENT_MODIFY_TRANSL_
#define	_Geometry_Test_TRANSL_

// ---------------------------------- Includes ---------------------------------

#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"Element_Test.h"

#include	"GSUtilsDefs.h"

#include	"basicgeometry.h"
#include	"Point2D.hpp"
#include	"Point2DData.h"

#include	"BuiltInLibrary.hpp"

// -----------------------------------------------------------------------------
// Change the height and layer of the clicked zone
//	- dimensions must be updated automatically
// -----------------------------------------------------------------------------

void	Do_Zone_Edit (short mode)
{
	API_Element		element = {};
	API_Element 	mask = {};
	API_ElementMemo	memo = {};

	if (!ClickAnElem ("Click a zone to modify", API_ZoneID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No zone was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	if (mode == 1) {					/* only setting */
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
		ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, pen);

		element.header.layer = APIApplicationLayerAttributeIndex;
		element.zone.pen = 10;

		err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

	} else if (mode == 2) {				/* only setting, but has effect on the parameter list */
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
		ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, pen);
		ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, roomHeight);

		element.header.layer = APIApplicationLayerAttributeIndex;
		element.zone.pen = 10;
		element.zone.roomHeight /= 2.0;

		err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

	} else if (mode == 3) {				/* only the parameter list */
		ACAPI_ELEMENT_MASK_CLEAR (mask);

		err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_AddPars);
		if (err == NoError) {
			(*memo.params)[26].value.real = 8.8888;		/* calculated area */
			err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_AddPars, true);
		}
		ACAPI_DisposeElemMemoHdls (&memo);

	} else {							/* everything: setting & parameter list */
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
		ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, pen);
		ACAPI_ELEMENT_MASK_SET (mask, API_ZoneType, roomHeight);

		element.header.layer = APIApplicationLayerAttributeIndex;
		element.zone.pen = 10;

		err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_AddPars);
		if (err == NoError) {
			(*memo.params)[26].value.real = 8.8888;		/* calculated area */
			err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_AddPars, true);
		}
		ACAPI_DisposeElemMemoHdls (&memo);
	}

	return;
}		// Do_Zone_Edit


// -----------------------------------------------------------------------------
// Change the content or the wrapping of the clicked text
// -----------------------------------------------------------------------------

void	Do_Word_Edit (short mode)
{
	API_Element			element = {};
	API_Element			mask = {};
	API_ElementMemo		memo = {};
	const char*			text = { "This word was modified by the Element Test example project.\nThis is a new line." };
	GSErrCode			err = NoError;

	if (mode == 1) {
		if (!ClickAnElem ("Click a word to change its content", API_TextID, nullptr, &element.header.type, &element.header.guid)) {
			WriteReport_Alert ("No word was clicked");
			return;
		}

		err = ACAPI_Element_Get (&element);
		if (err != NoError)
			return;

		ACAPI_ELEMENT_MASK_CLEAR (mask);

		memo.textContent = new GS::UniString { text };

		err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_TextContent, true);

		ACAPI_DisposeElemMemoHdls (&memo);

	} else {
		if (!ClickAnElem ("Click a word to change the nonBreaking flag", API_TextID, nullptr, &element.header.type, &element.header.guid)) {
			WriteReport_Alert ("No word was clicked");
			return;
		}

		err = ACAPI_Element_Get (&element);
		if (err != NoError)
			return;

		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_TextType, nonBreaking);
		element.text.nonBreaking = !element.text.nonBreaking;
		if (!element.text.nonBreaking) {
			ACAPI_ELEMENT_MASK_SET (mask, API_TextType, width);		// ignored if 'nonBreaking'
			element.text.width /= 2.0;
		}

		err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	}

	return;
}		// Do_Word_Edit


// -----------------------------------------------------------------------------
// Change the clicked element
// -----------------------------------------------------------------------------

void	Do_2D_Edit (bool withColorOverride)
{
	API_Element			element = {};
	API_Element			mask = {};
	API_ArrowData*		pArrow;
	short*				pPen;
	short*				pColorOverridePen = nullptr;

	if (!ClickAnElem ("Click a 2D element to change its pen and arrow", API_ZombieElemID, nullptr, &element.header.type, &element.header.guid)) {
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	switch (element.header.type.typeID) {
		case API_HatchID:
				ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, contPen);
				ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillPen);
				element.hatch.fillPen.penIndex = 10;
				pPen = &element.hatch.contPen.penIndex;
				pColorOverridePen = &element.hatch.contPen.colorOverridePenIndex;
				pArrow = nullptr;
				break;
		case API_LineID:
				ACAPI_ELEMENT_MASK_SET (mask, API_LineType, linePen);
				ACAPI_ELEMENT_MASK_SET (mask, API_LineType, arrowData.arrowType);
				ACAPI_ELEMENT_MASK_SET (mask, API_LineType, arrowData.begArrow);
				ACAPI_ELEMENT_MASK_SET (mask, API_LineType, arrowData.endArrow);
				ACAPI_ELEMENT_MASK_SET (mask, API_LineType, arrowData.arrowPen);
				ACAPI_ELEMENT_MASK_SET (mask, API_LineType, arrowData.arrowSize);
				pPen = &element.line.linePen.penIndex;
				pColorOverridePen = &element.line.linePen.colorOverridePenIndex;
				pArrow = &element.line.arrowData;
				break;
		case API_ArcID:
				ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, linePen);
				ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, arrowData.arrowType);
				ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, arrowData.begArrow);
				ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, arrowData.endArrow);
				ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, arrowData.arrowPen);
				ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, arrowData.arrowSize);
				pPen = &element.arc.linePen.penIndex;
				pColorOverridePen = &element.arc.linePen.colorOverridePenIndex;
				pArrow = &element.arc.arrowData;
				break;
		case API_SplineID:
				ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, linePen);
				ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, arrowData.arrowType);
				ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, arrowData.begArrow);
				ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, arrowData.endArrow);
				ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, arrowData.arrowPen);
				ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, arrowData.arrowSize);
				pPen = &element.spline.linePen.penIndex;
				pColorOverridePen = &element.spline.linePen.colorOverridePenIndex;
				pArrow = &element.spline.arrowData;
				break;
		case API_PolyLineID:
				ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, linePen);
				ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.arrowType);
				ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.begArrow);
				ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.endArrow);
				ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.arrowPen);
				ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.arrowSize);
				pPen = &element.polyLine.linePen.penIndex;
				pColorOverridePen = &element.polyLine.linePen.colorOverridePenIndex;
				pArrow = &element.polyLine.arrowData;
				break;
		default:
				WriteReport_Alert ("No 2D drawing element was clicked");
				return;
	}

	if (pPen != nullptr)
		*pPen = 20;

	if (withColorOverride && pColorOverridePen != nullptr)
		*pColorOverridePen = 20;

	if (pArrow != nullptr) {
		pArrow->arrowType = APIArr_FullArrow30;
		pArrow->begArrow = false;
		pArrow->endArrow = true;
		pArrow->arrowPen = 20;
		pArrow->arrowSize = 3;
	}

	ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

	return;
}		// Do_2D_Edit


// -----------------------------------------------------------------------------
// Change clicked hotspot (pen, layer, height, coords)
// -----------------------------------------------------------------------------

void	Do_Hotspot_Edit (void)
{
	API_Element element = {};
	API_Element mask = {};

	if (!ClickAnElem ("Click a hotspot to modify", API_HotspotID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No hotspot was clicked");
		return;
	}

	ACAPI_Element_Get (&element);

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
	ACAPI_ELEMENT_MASK_SET (mask, API_HotspotType, pen);
	ACAPI_ELEMENT_MASK_SET (mask, API_HotspotType, height);
	ACAPI_ELEMENT_MASK_SET (mask, API_HotspotType, pos);

	element.header.layer = APIApplicationLayerAttributeIndex;
	element.hotspot.pen = 6;
	element.hotspot.height *= 2;
	element.hotspot.pos.x += 0.5;
	element.hotspot.pos.y += 0.5;

	ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

	return;
}		// Do_Hotspot_Edit


// -----------------------------------------------------------------------------
// Change clicked spline (remove arrow)
// -----------------------------------------------------------------------------

void	Do_Spline_Edit (void)
{
	API_Element		element = {};
	API_Element		mask = {};
	API_ArrowData	myArrow = {};

	if (!ClickAnElem ("Click a spline to modify", API_SplineID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No spline was clicked");
		return;
	}

	ACAPI_Element_Get (&element);

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, arrowData.begArrow);
	ACAPI_ELEMENT_MASK_SET (mask, API_SplineType, arrowData.endArrow);

	myArrow.begArrow = false;
	myArrow.endArrow = false;
	element.spline.arrowData = myArrow;

	ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

	return;
}		// Do_Spline_Edit

void	Do_CutPlane_Or_Elevation_Edit_Coords (void)
{
	API_Element			element = {};
	API_Element			element2 = {};
	API_ElementMemo		memo = {};
	API_ElementMemo		memo2 = {};
	GSErrCode			err = NoError;

	ClickAnElem ("Click a cut plane to modify", API_CutPlaneID, nullptr, &element.header.type, &element.header.guid);
	if (!(element.header.type == API_CutPlaneID || element.header.type == API_ElevationID || element.header.type == API_InteriorElevationID)) {
		WriteReport_Alert ("No cutplane, elevation or interior elevation was clicked");
		return;
	}

	if (ACAPI_Element_Get (&element) != NoError)
		return;

	if (ACAPI_Element_GetMemo (element.header.guid, &memo) != NoError)
		return;

	ClickAnElem ("Click a cut plane to modify", API_CutPlaneID, nullptr, &element2.header.type, &element2.header.guid);
	if (!(element2.header.type == API_CutPlaneID || element2.header.type == API_ElevationID)) {
		WriteReport_Alert ("No cutplane or elevation was clicked");
		return;
	}

	if (ACAPI_Element_Get (&element2) != NoError ||
		ACAPI_Element_GetMemo (element2.header.guid, &memo2) != NoError) {
		return;
	}

	UInt64 memoMask = APIMemoMask_SectionMainCoords;

	if (element.header.type == API_InteriorElevationID) {
		memo2.intElevSegments = memo.intElevSegments;
		err = ACAPI_Element_ChangeExt (&element, nullptr, &memo2, memoMask, 0, nullptr, true, 2);
	} else {
		err = ACAPI_Element_Change (&element, nullptr, &memo2, memoMask, true);
	}

	if (err == APIERR_BADPARS) {
		WriteReport_Alert ("Bad parameters!");
	}
	return;

}
// -----------------------------------------------------------------------------
// Change clicked cutPlane (direction and depth)
// -----------------------------------------------------------------------------

void	Do_CutPlane_Edit (void)
{
	API_Element			element = {};
	API_ElementMemo		memo = {};

	if (!ClickAnElem ("Click a cut plane to modify", API_CutPlaneID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No cut plane was clicked");
		return;
	}

	if (ACAPI_Element_Get (&element) != NoError ||
		ACAPI_Element_GetMemo (element.header.guid, &memo) != NoError)
		return;

	if (memo.sectionSegmentMainCoords == nullptr ||
		memo.sectionSegmentDistCoords == nullptr ||
		memo.sectionSegmentDepthCoords == nullptr)
	{
		return;
	}

	GS::Array<Coord> coordArray;

	for (UInt32 ind = 0; ind < element.cutPlane.segment.nMainCoord; ++ind) {
		coordArray.Push (Coord (memo.sectionSegmentMainCoords[ind].x, memo.sectionSegmentMainCoords[ind].y));
	}

	if (DBERROR (coordArray.GetSize () % 2 == 1))
		return;

	Coord depthStartCoord (memo.sectionSegmentDepthCoords[0].x, memo.sectionSegmentDepthCoords[0].y);

	Vector depthVect = depthStartCoord - coordArray.GetFirst ();
	Vector dirVect = Geometry::UnitVector (depthVect);

	double newDepthVectLen = depthVect.GetLength () + 0.2;
	Coord newDepthStartCoord = Geometry::ORIGO2 + ((coordArray.GetFirst () - Geometry::ORIGO2) + dirVect * newDepthVectLen);

	GS::Array<Coord> newMainCoordArray;
	for (UIndex ind = 1; ind < coordArray.GetSize (); ind += 2) {
		const Sector s = Sector (coordArray[ind - 1], coordArray[ind]);
		Coord firstTierceCoord = Geometry::InterpolateCoord (s.c1, s.c2, 1.0/3.0);
		Coord tranlsFirstTierceCoord = Geometry::ORIGO2 + ((firstTierceCoord - Geometry::ORIGO2) + dirVect * 0.2);
		Coord secondTierceCoord = Geometry::InterpolateCoord (s.c1, s.c2, 2.0/3.0);
		Coord tranlsSecondTierceCoord = Geometry::ORIGO2 + ((secondTierceCoord - Geometry::ORIGO2) + dirVect * 0.2);

		newMainCoordArray.Push (s.c1);
		newMainCoordArray.Push (firstTierceCoord);
		newMainCoordArray.Push (tranlsFirstTierceCoord);
		newMainCoordArray.Push (tranlsSecondTierceCoord);
		newMainCoordArray.Push (secondTierceCoord);
		newMainCoordArray.Push (s.c2);
	}

	element.cutPlane.segment.nMainCoord = newMainCoordArray.GetSize ();
	API_Coord*	newMainCoords = (API_Coord *) BMpAllClear (element.cutPlane.segment.nMainCoord * sizeof (API_Coord));
	if (newMainCoords == nullptr) {
		ACAPI_DisposeElemMemoHdls (&memo);
		return;
	}
	for (UIndex ind = 0; ind < newMainCoordArray.GetSize (); ++ind) {
		newMainCoords[ind].x = newMainCoordArray[ind].x;
		newMainCoords[ind].y = newMainCoordArray[ind].y;
	}

	element.cutPlane.segment.nDepthCoord = 1;
	API_Coord*	newDepthCoords = (API_Coord *) BMpAllClear (sizeof (API_Coord));
	newDepthCoords[0].x = newDepthStartCoord.x;
	newDepthCoords[0].y = newDepthStartCoord.y;

	ACAPI_Element_Delete ({ element.header.guid });

	BMpKill (reinterpret_cast<GSPtr *>(&memo.sectionSegmentMainCoords));
	memo.sectionSegmentMainCoords = newMainCoords;

	BMpKill (reinterpret_cast<GSPtr *>(&memo.sectionSegmentDepthCoords));
	memo.sectionSegmentDepthCoords = newDepthCoords;

	ACAPI_Element_Create (&element, &memo);
	ACAPI_DisposeElemMemoHdls (&memo);

	return;

}

// -----------------------------------------------------------------------------
// Delete the clicked hatch node, change the pen, rotate reference vector and show/hide area text
//	- dimensions/labels must be updated automatically
// -----------------------------------------------------------------------------

void	Do_Hatch_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo;
	API_Neig			neig;
	Int32				nodeInd;

	if (!ClickAnElem ("Click a fill polygon to modify", API_HatchID, &neig, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No fill polygon was clicked");
		return;
	}

	if (neig.neigID != APINeig_Hatch) {
		WriteReport_Alert ("Only fill polygon nodes are accepted");
		return;
	}

	ACAPI_Element_Get (&element);

	GSErrCode err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_Polygon);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	nodeInd = neig.inIndex;
	err = ACAPI_Polygon_DeletePolyNode (&memo, &nodeInd);

	if (err == NoError) {
		element.hatch.poly.nCoords--;

		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, contPen);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, hatchType);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillInd);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillPen);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, fillBGPen);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, hatchOrientation.type);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, hatchOrientation.matrix00);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, hatchOrientation.matrix10);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, hatchOrientation.matrix01);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, hatchOrientation.matrix11);
		ACAPI_ELEMENT_MASK_SET (mask, API_HatchType, showArea);

		element.hatch.contPen.penIndex = 20;
		element.hatch.contPen.colorOverridePenIndex = 0;
		element.hatch.hatchType = API_FillHatch;
		element.hatch.fillInd = ACAPI_CreateAttributeIndex (44);
		element.hatch.fillPen.penIndex = 10;
		element.hatch.fillPen.colorOverridePenIndex = 0;
		element.hatch.fillBGPen = 6;
		element.hatch.hatchOrientation.type = API_HatchRotated;

		double	cosa = cos (30 * DEGRAD), sina = sin (30 * DEGRAD);

		element.hatch.hatchOrientation.matrix00 = cosa;
		element.hatch.hatchOrientation.matrix10 = sina;
		element.hatch.hatchOrientation.matrix01 = -sina;
		element.hatch.hatchOrientation.matrix11 = cosa;

		element.hatch.showArea = !element.hatch.showArea;

		err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_Polygon, true);
		if (err == NoError) {
			API_Coord c = (*memo.coords)[1];
			UpdateAllLabels (element.header.guid, c);
		}
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Hatch_Edit


// -----------------------------------------------------------------------------
// Change clicked PolyLine pen, layer, arrow data
// -----------------------------------------------------------------------------

void	Do_PolyLine_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo;
	API_ArrowData		myArrow;
	API_Neig			neig;

	if (!ClickAnElem ("Click a polyLine to modify", API_PolyLineID, &neig, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No polyLine was clicked");
		return;
	}

	if (neig.neigID != APINeig_PolyLine) {
		WriteReport_Alert ("Only polyLine nodes are accepted");
		return;
	}

	ACAPI_Element_Get (&element);

	GSErrCode err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_Polygon);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
	ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, linePen);
	ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, ltypeInd);
	ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, roomSeparator);
	ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.arrowType);
	ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.begArrow);
	ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.endArrow);
	ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.arrowPen);
	ACAPI_ELEMENT_MASK_SET (mask, API_PolyLineType, arrowData.arrowSize);

	element.header.layer = APIApplicationLayerAttributeIndex;
	element.polyLine.linePen.penIndex = 7;
	element.polyLine.linePen.colorOverridePenIndex = 0;
	element.polyLine.ltypeInd = ACAPI_CreateAttributeIndex (6);
	element.polyLine.roomSeparator = true;

	myArrow.arrowType = APIArr_SlashLine15;
	myArrow.begArrow = true;
	myArrow.endArrow = true;
	myArrow.arrowSize = 4;
	myArrow.arrowPen = 12;

	element.polyLine.arrowData = myArrow;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_PolyLine_Edit


// -----------------------------------------------------------------------------
// Change clicked label...
// -----------------------------------------------------------------------------
void	Do_Label_Edit (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo = {};
	API_Neig			neig;
	const char			*text = { "This word was modified by the Element Test example project." };

	if (!ClickAnElem ("Click a label to modify", API_LabelID, &neig, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No label was clicked");
		return;
	}

	if (neig.neigID != APINeig_Label) {
		WriteReport_Alert ("Only label nodes are accepted");
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

	delete memo.textContent;
	memo.textContent = new GS::UniString { text };

	if (err == NoError) {
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		if (element.label.labelClass == APILblClass_Text)
			ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, u.text.pen);
		if (element.label.labelClass == APILblClass_Text)
			ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, u.text.size);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, pen);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, textSize);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, textWay);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, font);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, faceBits);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, useBgFill);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, fillBgPen);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, effectsBits);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, ltypeInd);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, framed);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, hasLeaderLine);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, leaderShape);
		ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, begC);

		if (element.label.labelClass == APILblClass_Text)
			element.label.u.text.pen = 13;
		if (element.label.labelClass == APILblClass_Text)
			element.label.u.text.size *= 2;
		element.label.textSize *= 2.0;
		element.label.textWay = APIDir_General;
		element.label.font = 20;
		element.label.faceBits = (element.label.effectsBits == APIFace_Plain) ? APIFace_Italic : APIFace_Plain;
		element.label.useBgFill = !element.label.useBgFill;
		element.label.fillBgPen = (element.label.fillBgPen + 3) % 256;
		element.label.effectsBits = ((element.label.effectsBits & APIEffect_StrikeOut) != 0) ? (element.label.effectsBits &~ APIEffect_StrikeOut) : (element.label.effectsBits | APIEffect_StrikeOut);
		element.label.ltypeInd = APISolidLineAttributeIndex;
		element.label.framed = !element.label.framed;
		element.label.hasLeaderLine = !element.label.hasLeaderLine;
		element.label.leaderShape = GetNextLeaderLineShape (element.label.leaderShape);
		element.label.pen = 17;
		element.label.begC.x -= 1.5;
		element.label.begC.y -= 1.5;

		if (element.label.labelClass == APILblClass_Text) {
			ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, u.text.loc);
			element.label.u.text.loc = {0, 0};
			if (element.label.framed) {
				element.label.u.text.textFrame.shapeType = (element.label.u.text.textFrame.shapeType == API_TextFrameShapeType_Pill ? API_TextFrameShapeType_RoundedRectangle : API_TextFrameShapeType_Pill);
				ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, u.text.textFrame.shapeType);
			}
		} else if (element.label.labelClass == APILblClass_Symbol) {
			ACAPI_ELEMENT_MASK_SET (mask, API_LabelType, u.symbol.pos);
			element.label.u.symbol.pos = {0, 0};
		}

		err = ACAPI_Element_Change (&element, &mask, &memo, 0, true);
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return; // Do_Label_Edit
}



// -----------------------------------------------------------------------------
// Change clicked Detail Marker
// -----------------------------------------------------------------------------
void	Do_Detail_Edit (void)
{
	API_Element			element = {}, mask = {};

	if (!ClickAnElem ("Click a detail to modify", API_DetailID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No detail was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
	ACAPI_ELEMENT_MASK_SET (mask, API_DetailType, pen);
	ACAPI_ELEMENT_MASK_SET (mask, API_DetailType, ltypeInd);
	ACAPI_ELEMENT_MASK_SET (mask, API_DetailType, pos);
	ACAPI_ELEMENT_MASK_SET (mask, API_DetailType, angle);
	ACAPI_ELEMENT_MASK_SET (mask, API_DetailType, horizontalMarker);
	ACAPI_ELEMENT_MASK_SET (mask, API_DetailType, constructionElementsOnly);

	element.header.layer	= APIApplicationLayerAttributeIndex;
	element.detail.pen		= ++element.detail.pen % 255 + 1;
	element.detail.ltypeInd = ACAPI_CreateAttributeIndex ((element.detail.ltypeInd.ToInt32_Deprecated () + 3) % 10 + 1);
	element.detail.pos.x	-= 1.0;
	element.detail.pos.y	-= 1.0;
	element.detail.angle	+= 0.5;
	element.detail.horizontalMarker = false;
	element.detail.constructionElementsOnly = !element.detail.constructionElementsOnly;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Change", err);
		return;
	}

	//change detail marker
	API_Guid markId = element.detail.markId;

	element				 = {};
	mask				 = {};
	API_ElementMemo memo = {};

	element.header.guid = markId;

	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_AddPars);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_ObjectType, pen);
	element.object.pen = (element.object.pen + 20) % 255 + 1;
	GS::UniString tmpUStr ("Arial Black Central European");
	GS::ucscpy (((*memo.params)+2)->value.uStr, tmpUStr.ToUStr());

	err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_AddPars, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Change", err);
		return;
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Detail_Edit


// -----------------------------------------------------------------------------
// Change clicked Change Marker
// -----------------------------------------------------------------------------
void	Do_ChangeMarker_Edit (void)
{
	API_Element element = {};
	if (!ClickAnElem ("Click a ChangeMarker to modify", API_ChangeMarkerID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No ChangeMarker was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	API_Element mask;
	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
	ACAPI_ELEMENT_MASK_SET (mask, API_ChangeMarkerType, markerComponent);

	element.header.layer = APIApplicationLayerAttributeIndex;
	if (element.changeMarker.markerComponent == APICMCT_HeadOnly)
		element.changeMarker.markerComponent = APICMCT_CloudOnly;
	else if (element.changeMarker.markerComponent == APICMCT_CloudOnly)
		element.changeMarker.markerComponent = APICMCT_HeadAndCloud;
	else if (element.changeMarker.markerComponent == APICMCT_HeadAndCloud)
		element.changeMarker.markerComponent = APICMCT_CloudOnly;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Change", err);
		return;
	}

	// change ChangeMarker marker
	API_Element markerElement = {};
	markerElement.header.guid = element.changeMarker.markerGuid;

	err = ACAPI_Element_Get (&markerElement);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	API_ElementMemo memo = {};
	err = ACAPI_Element_GetMemo (markerElement.header.guid, &memo, APIMemoMask_AddPars);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	API_Element markerMask;
	ACAPI_ELEMENT_MASK_CLEAR (markerMask);
	ACAPI_ELEMENT_MASK_SET (markerMask, API_ObjectType, pen);
	markerElement.object.pen = (markerElement.object.pen + 20) % 255 + 1;

	GS::ucscpy ((*memo.params)[2].value.uStr, GS::UniString ("Arial Black Central European").ToUStr ());

	err = ACAPI_Element_Change (&markerElement, &markerMask, &memo, APIMemoMask_AddPars, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Change", err);
		return;
	}

	ACAPI_DisposeElemMemoHdls (&memo);
}


// -----------------------------------------------------------------------------
// Change clicked Worksheet
// -----------------------------------------------------------------------------
void	Do_Worksheet_Edit (void)
{
	API_Element			element = {}, mask = {};

	if (!ClickAnElem ("Click a worksheet to modify", API_WorksheetID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No worksheet was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
	ACAPI_ELEMENT_MASK_SET (mask, API_WorksheetType, pen);
	ACAPI_ELEMENT_MASK_SET (mask, API_WorksheetType, ltypeInd);
	ACAPI_ELEMENT_MASK_SET (mask, API_WorksheetType, pos);
	ACAPI_ELEMENT_MASK_SET (mask, API_WorksheetType, angle);
	ACAPI_ELEMENT_MASK_SET (mask, API_WorksheetType, horizontalMarker);

	element.header.layer	= ACAPI_CreateAttributeIndex ((element.header.layer.ToInt32_Deprecated () + 1) % 10 + 1);
	element.worksheet.pen	= (element.worksheet.pen + 1) % 255 + 1;

	element.worksheet.ltypeInd = ACAPI_CreateAttributeIndex ((element.worksheet.ltypeInd.ToInt32_Deprecated () + 3) % 10 + 1);
	element.worksheet.pos.x	-= 1.0;
	element.worksheet.pos.y	-= 1.0;
	element.worksheet.angle	+= 0.5;
	element.worksheet.horizontalMarker = false;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Change", err);
		return;
	}

	//change worksheet marker
	API_Guid markId = element.worksheet.markId;

	element				 = {};
	mask				 = {};
	API_ElementMemo memo = {};

	element.header.guid = markId;

	err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_AddPars);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_ObjectType, pen);
	element.object.pen = (element.object.pen + 20) % 255 + 1;
	GS::UniString tmpUStr ("Arial Black Central European");
	GS::ucscpy (((*memo.params)+2)->value.uStr, tmpUStr.ToUStr());

	err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_AddPars, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Change", err);
		return;
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Worksheet_Edit


// -----------------------------------------------------------------------------
// Arc editing test
// -----------------------------------------------------------------------------

void	Do_Arc_Edit (void)
{
	API_Element element {};

	if (!ClickAnElem ("Click an arc to modify.", API_ZombieElemID, nullptr, &element.header.type, &element.header.guid)) {
		if (element.header.type != API_ArcID && element.header.type != API_CircleID) {
			WriteReport_Alert ("No arc was clicked.");
			return;
		}
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	API_Element mask;
	ACAPI_ELEMENT_MASK_CLEAR (mask);

	ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, whole);
	const bool newWhole = !element.arc.whole;
	element.arc.whole = newWhole;

	if (!newWhole) {
		ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, begAng);
		ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, endAng);
		ACAPI_ELEMENT_MASK_SET (mask, API_ArcType, reflected);
		element.arc.begAng = PI / 2;		//  90 degrees
		element.arc.endAng = 3 * PI / 2;	// 270 degrees
		element.arc.reflected = !element.arc.reflected;
	}

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Change", err);
	}
}		// Do_Arc_Edit


// -----------------------------------------------------------------------------
// Helper function to print the section line's coordinates
// -----------------------------------------------------------------------------
static void		DumpSectionLineCoords (const API_InteriorElevationType& intElev,
										const API_SectionSegment* const segments,
										const API_Coord* const coords)
{
	for (UInt32 ii = 0, coInd = 0; ii < intElev.nSegments; ++ii) {
		DBPrintf ("Segment #%d: \n", ii + 1);
		for (UInt32 jj = 0; jj < segments[ii].nMainCoord - 1; ++jj) {
			DBPrintf ("	Line #%2d: (%5.2lf, %5.2lf) - (%5.2lf, %5.2lf)\n",
				coords[coInd].x, coords[coInd].y, coords[coInd+1].x, coords[coInd+1].y);
			coInd++;
		}
		coInd++;
	}
}	// DumpSectionLineCoords


// -----------------------------------------------------------------------------
// Change certain interior elevation segment settings to match the default
// -----------------------------------------------------------------------------
void		Do_ChangeInteriorElevation (void)
{
	API_Element			element = {}, mask;
	API_ElementMemo		memo = {};
	API_SubElement		markers[3] = {};
	API_SectionSegment	*segments;
	API_Coord			*slCoords;
	API_Elem_Head		head;
	API_Neig			neig;
	UInt32				nSeg;

	if (!ClickAnElem ("Click an element", API_ZombieElemID, &neig, &element.header.type, &element.header.guid) ||
		neig.neigID < APINeig_InteriorElevation || neig.neigID > APINeig_SectionSegmentMarker)
	{
		WriteReport_Alert ("No interior elevation element was clicked");
		return;
	}

	markers[0].subType = APISubElement_MainMarker;
	markers[1].subType = APISubElement_SHMarker;
	markers[2].subType = APISubElement_CommonMarker;

	ACAPI_ELEMENT_MASK_SETFULL (mask);

	if (ACAPI_Element_Get (&element) != NoError)
		return;

	if (ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_SectionSegments | APIMemoMask_SectionMainCoords) != NoError)
		return;

	if (memo.intElevSegments == nullptr ||
		memo.sectionSegmentMainCoords == nullptr ||
		memo.sectionSegmentDistCoords == nullptr ||
		memo.sectionSegmentDepthCoords == nullptr)
	{
		return;
	}

	// store data for later use
	head		= element.header;
	segments	= memo.intElevSegments;
	nSeg		= element.interiorElevation.nSegments;
	slCoords	= memo.sectionSegmentMainCoords;
	DumpSectionLineCoords (element.interiorElevation, segments, slCoords);

	// modify the section line coordinates

	if (ACAPI_Element_GetDefaultsExt (&element, nullptr, 3UL, markers) == NoError) {
		element.header = head;
		memo.intElevSegments	= segments;
		element.interiorElevation.nSegments = nSeg;
		// adjust only the last segment in the chain to be the same as the default
		segments[nSeg-1] = element.interiorElevation.segment;
		ACAPI_Element_ChangeExt (&element, &element, &memo, APIMemoMask_SectionSegments, 3UL, markers, /* withDel */ true, nSeg - 1);
	}

	ACAPI_DisposeElemMemoHdls (&memo);		// also kill 'segments'
	for (UInt32 ii = 0; ii < 3; ii++)
		ACAPI_DisposeElemMemoHdls (&markers[ii].memo);

}		// Do_ChangeInteriorElevation


// -----------------------------------------------------------------------------
// Modify the selected/clicked element's renovation status
// -----------------------------------------------------------------------------
void		Do_RotateRenovationStatus (void)
{
	API_Element			element, mask;
	API_SelectionInfo 	selectionInfo;
	GS::Array<API_Neig>	selNeigs;
	Int32				index;
	GSErrCode			err;

	// Get selection
	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);
	if (err != NoError && err != APIERR_NOSEL) {
		ErrorBeep ("ACAPI_Selection_GetInfo", err);
		return;
	}

	if (selectionInfo.typeID == API_SelEmpty) {
		// Ask the user to click an element
		API_Neig	neig;
		if (!ClickAnElem ("Click an element", API_ZombieElemID, &neig, nullptr, nullptr) ||
			!ACAPI_Element_Filter (neig.guid, APIFilt_IsEditable)) {
			WriteReport_Alert ("No editable element was clicked");
			return;
		}

		selNeigs.Push (neig);
		selectionInfo.sel_nElemEdit = 1;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, renovationStatus);
	for (index = 0; index < selectionInfo.sel_nElemEdit; index++) {
		// Get selected element
		element = {};

		element.header.guid = selNeigs[index].guid;
		if (ACAPI_Element_Get (&element) != NoError)
			continue;

		if		(element.header.renovationStatus == API_ExistingStatus)		element.header.renovationStatus = API_NewStatus;
		else if (element.header.renovationStatus == API_NewStatus)			element.header.renovationStatus = API_DemolishedStatus;
		else if (element.header.renovationStatus == API_DemolishedStatus)	element.header.renovationStatus = API_ExistingStatus;
		else continue;

		err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
		if (err != NoError) {
			ErrorBeep ("Can't change element's renovation status!", err);
		}
	}

	return;
}		// Do_RotateRenovationStatus


// -----------------------------------------------------------------------------
// Inverts the material chaining on the selected element.
// -----------------------------------------------------------------------------
void	Do_TestMaterialChaining (void)
{
	API_Element element = {};
	if (!ClickAnElem ("Click an Element with Material Chaining to modify.", API_ZombieElemID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("Error: No Element was clicked!");
		return;
	}

	auto err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		WriteReport_Alert ("Error: ACAPI_Element_Get failed!");
		return;
	}

	API_Element mask;
	ACAPI_ELEMENT_MASK_CLEAR (mask);
	bool *chainingValue = nullptr;

	API_ElementMemo memo = {};

	switch (element.header.type.typeID) {
		case API_WallID:
			ACAPI_ELEMENT_MASK_SET (mask, API_WallType, materialsChained);
			ACAPI_ELEMENT_MASK_SET (mask, API_WallType, refMat);
			chainingValue = &element.wall.materialsChained;
			element.wall.refMat = ACAPI_CreateAttributeIndex (10);
			break;

		case API_BeamID:
			err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_BeamSegment);

			if (err != NoError) {
				WriteReport_Alert ("Error: ACAPI_Element_GetMemo failed!");
				return;
			}

			if (err == NoError && memo.beamSegments != nullptr) {
				for (USize idx = 0; idx < element.beam.nSegments; ++idx) {
					chainingValue = &memo.beamSegments[idx].materialsChained;
					memo.beamSegments[idx].leftMaterial = ACAPI_CreateAttributeIndex (10);
					*chainingValue = !*chainingValue;
				}

				API_Element maskElem;
				ACAPI_ELEMENT_MASK_CLEAR (maskElem);
				err = ACAPI_Element_Change (&element, &maskElem, &memo, APIMemoMask_BeamSegment, true);

				if (err != NoError)
					WriteReport_Alert ("Error: ACAPI_Element_Change failed!");
			}

			return;

		case API_ColumnID:
			err = ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_ColumnSegment);

			if (err != NoError) {
				WriteReport_Alert ("Error: ACAPI_Element_GetMemo failed!");
				return;
			}

			if (err == NoError && memo.columnSegments != nullptr) {
				for (USize idx = 0; idx < element.column.nSegments; ++idx) {
					chainingValue = &memo.columnSegments[idx].materialsChained;
					memo.columnSegments[idx].extrusionSurfaceMaterial = ACAPI_CreateAttributeIndex (10);
					*chainingValue = !*chainingValue;
				}

				API_Element maskElem;
				ACAPI_ELEMENT_MASK_CLEAR (maskElem);
				err = ACAPI_Element_Change (&element, &maskElem, &memo, APIMemoMask_ColumnSegment, true);

				if (err != NoError)
					WriteReport_Alert ("Error: ACAPI_Element_Change failed!");
			}

			return;

		case API_SlabID:
			ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, materialsChained);
			ACAPI_ELEMENT_MASK_SET (mask, API_SlabType, sideMat);
			chainingValue = &element.slab.materialsChained;
			element.slab.sideMat = ACAPI_CreateAttributeIndex (10);
			break;

		case API_MeshID:
			ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, materialsChained);
			ACAPI_ELEMENT_MASK_SET (mask, API_MeshType, topMat);
			chainingValue = &element.mesh.materialsChained;
			element.mesh.topMat = ACAPI_CreateAttributeIndex (10);
			break;

		case API_ShellID:
			ACAPI_ELEMENT_MASK_SET (mask, API_ShellType, shellBase.materialsChained);
			ACAPI_ELEMENT_MASK_SET (mask, API_ShellType, shellBase.sidMat);
			chainingValue = &element.shell.shellBase.materialsChained;
			element.shell.shellBase.sidMat = ACAPI_CreateAttributeIndex (10);
			break;

		case API_RoofID:
			ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.materialsChained);
			ACAPI_ELEMENT_MASK_SET (mask, API_RoofType, shellBase.sidMat);
			chainingValue = &element.roof.shellBase.materialsChained;
			element.roof.shellBase.sidMat = ACAPI_CreateAttributeIndex (10);
			break;

		default:
			WriteReport_Alert ("Error: Clicked element does not have Material Chaining!");
			return;
	}

	*chainingValue = !*chainingValue;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0U, true);
	if (err != NoError)
		WriteReport_Alert ("Error: ACAPI_Element_Change failed!");

	return;

}		// Do_TestMaterialChaining
