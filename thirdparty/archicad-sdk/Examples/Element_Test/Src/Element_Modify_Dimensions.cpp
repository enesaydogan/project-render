#include "APIEnvir.h"
#include "ACAPinc.h"

#include "APICommon.h"
#include "Element_Test.h"

// -----------------------------------------------------------------------------
// Dimension test
// -----------------------------------------------------------------------------
void	Do_Dimensions_Test (void)
{
	API_Element			element;
	API_Element			mask;
	API_ElementMemo		memo;
	GS::UniString		customText ("Custom");
	GSErrCode			err = NoError;

	for (Int32 j = 1; j <= 32; j++) {
		const API_ElemTypeID typeID = (API_ElemTypeID) j;
		if (typeID != API_DimensionID		&&
			typeID != API_RadialDimensionID	&&
			typeID != API_LevelDimensionID	&&
			typeID != API_AngleDimensionID)
				continue;

		GS::Array<API_Guid> elemList;
		ACAPI_Element_GetElemList (typeID, &elemList);

		for (GS::Array<API_Guid>::ConstIterator it = elemList.Enumerate (); it != nullptr; ++it) {
			element = {};
			memo = {};
			element.header.guid = *it;
			err = ACAPI_Element_Get (&element);
			if (err != NoError) {
				ErrorBeep ("ACAPI_Element_Get", err);
				return;
			}
			ACAPI_ELEMENT_MASK_CLEAR (mask);
			switch (typeID) {
				case API_DimensionID:
					element.dimension.defNote.contentType == API_NoteContent_Custom ? element.dimension.defNote.contentType = API_NoteContent_Measured
																					: element.dimension.defNote.contentType = API_NoteContent_Custom;
					element.dimension.defNote.contentUStr = &customText;
					ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, defNote.contentType);
					ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, defNote.content);
					break;
				case API_RadialDimensionID:
					element.radialDimension.note.contentType == API_NoteContent_Custom ? element.radialDimension.note.contentType  = API_NoteContent_Measured
																						: element.radialDimension.note.contentType = API_NoteContent_Custom;
					element.radialDimension.note.contentUStr = &customText;
					ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, note.contentType);
					ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, note.content);
					break;
				case API_LevelDimensionID:
					element.levelDimension.note1.contentType == API_NoteContent_Custom ? element.levelDimension.note1.contentType  = API_NoteContent_Measured
																						: element.levelDimension.note1.contentType = API_NoteContent_Custom;
					element.levelDimension.note1.contentUStr = &customText;
					ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, note1.contentType);
					ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, note1.content);
					break;
				case API_AngleDimensionID:
					element.angleDimension.note.contentType == API_NoteContent_Custom ? element.angleDimension.note.contentType   = API_NoteContent_Measured
																						: element.angleDimension.note.contentType =	API_NoteContent_Custom;
					element.angleDimension.note.contentUStr = &customText;
					ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.contentType);
					ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.content);
					break;
				default:
					break;
			}
			if (element.header.hasMemo) {
				err = ACAPI_Element_GetMemo (element.header.guid, &memo);
				for (int k = 0; k < element.dimension.nDimElem; k++) {
					(*memo.dimElems)[k].note.contentType == API_NoteContent_Custom ? (*memo.dimElems)[k].note.contentType = API_NoteContent_Measured
																				   : (*memo.dimElems)[k].note.contentType = API_NoteContent_Custom;
					if ((*memo.dimElems)[k].note.contentUStr != nullptr)
						delete (*memo.dimElems)[k].note.contentUStr;
					(*memo.dimElems)[k].note.contentUStr = new GS::UniString (customText);
				}
				if (err != NoError) {
					ErrorBeep ("ACAPI_Element_GetMemo", err);
					return;
				}
				err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_All, true);
				if (err != NoError) {
					ErrorBeep ("ACAPI_Element_Change", err);
					return;
				}
				ACAPI_DisposeElemMemoHdls (&memo);
			} else {
				err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
				if (err != NoError) {
					ErrorBeep ("ACAPI_Element_Change", err);
					return;
				}
			}
		}
	}
}		// Do_Dimension_Test

// -----------------------------------------------------------------------------
// Change clicked dimension
// -----------------------------------------------------------------------------

void	Do_Dimension_Edit (void)
{
	API_Element			element, mask;
	API_ElementMemo		memo;

	element = {};
	memo = {};

	if (!ClickAnElem ("Click a dimension to modify", API_DimensionID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No dimension was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	err = ACAPI_Element_GetMemo (element.header.guid, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	if (err == NoError) {
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
		ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, linPen);
		ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, ed_arrowAng);
		ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, textPos);
		ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, markerData.markerType);
		ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, defWitnessForm);
		ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, textFillOrder);

		element.header.layer = APIApplicationLayerAttributeIndex;
		element.dimension.linPen = (element.dimension.linPen + 1) % 20;
		element.dimension.ed_arrowAng = 2;
		element.dimension.textPos =	(API_TextPosID) ((element.dimension.textPos + 1) % 3);
		element.dimension.markerData.markerType =(API_MarkerID) ((element.dimension.markerData.markerType + 1) % 18);
		element.dimension.defWitnessForm =	(API_WitnessID) ((element.dimension.defWitnessForm + 1) % 4);
		element.dimension.textFillOrder =  element.dimension.textFillOrder == API_DimTextFillOrderID::FillAndContourAboveDimLine
												? API_DimTextFillOrderID::FillAndContourUnderDimLine
												: API_DimTextFillOrderID::FillAndContourAboveDimLine;

		for (int i = 0; i < element.dimension.nDimElem; i++) {
			int witnessID = (int) (*memo.dimElems)[i].witnessForm;
			witnessID = (witnessID + 1) % 4;
			(*memo.dimElems)[i].witnessForm = (API_WitnessID) witnessID;
			if (i == 1) {
				API_NoteType& note = (*memo.dimElems)[i].note;
				note.fixPos = true;
				if (note.useLeaderLine || element.dimension.dimAppear == APIApp_Elev) {
					note.useLeaderLine = false;
					if (element.dimension.dimAppear == APIApp_Elev) {
						note.pos.x = note.pos.x + 1.0;
						note.pos.y = note.pos.y + 2.0;
					} else {
						note.pos.x = note.endC.x;
						note.pos.y = note.endC.y;
					}
					note.noteAngle = PI / 4.0;
				} else {
					note.useLeaderLine = true;
					note.anchorAtTextEnd = false;
					note.begC.x = note.pos.x;
					note.begC.y = note.pos.y;
					note.midC.x = note.begC.x + 1.0;
					note.midC.y = note.begC.y + 2.0;
					note.endC.x = note.midC.x + 14.0;
					note.endC.y = note.midC.y;
					note.leaderPen = 9;
					note.leaderLineType = ACAPI_CreateAttributeIndex (5);
					note.leaderShape = API_Splinear;
					note.arrowData.arrowType = APIArr_FullArrow15;
					note.arrowData.arrowPen = 10;
					note.arrowData.arrowSize = 3.0;
					note.anchorPoint = APILbl_BottomAnchor;
				}
			}
		}

		err = ACAPI_Element_Change (&element, &mask, &memo, 0, true);
	}

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}		// Do_Dimension_Edit


// -----------------------------------------------------------------------------
// Change the pens of the clicked dimension
// -----------------------------------------------------------------------------

void	Do_Dimension_Pen_Edit (void)
{
	API_Element			element, mask;
	API_ElementMemo		memo;

	element = {};
	memo = {};

	if (!ClickAnElem ("Click a dimension to modify", API_DimensionID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No dimension was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	err = ACAPI_Element_GetMemo (element.header.guid, &memo);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetMemo", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, linPen);
	ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, defNote.notePen);
	ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, defNote.leaderPen);
	ACAPI_ELEMENT_MASK_SET (mask, API_DimensionType, markerData.markerPen);

	short penIdx = element.dimension.linPen;
	auto NewPen = [&penIdx]() { penIdx = (++penIdx) % 32; return penIdx; };

	element.dimension.linPen = NewPen ();
	element.dimension.defNote.notePen = NewPen ();
	element.dimension.defNote.leaderPen = NewPen ();
	element.dimension.markerData.markerPen = NewPen ();
	for (int i = 0; i < element.dimension.nDimElem; i++) {
		(*memo.dimElems)[i].note.notePen = NewPen ();
	}

	err = ACAPI_Element_Change (&element, &mask, &memo, 0, true);

	ACAPI_DisposeElemMemoHdls (&memo);

	return;
}


// -----------------------------------------------------------------------------
// Change clicked LevelDimension
// -----------------------------------------------------------------------------

void	Do_LevelDimension_Edit ()
{
	API_Element element = {}, mask;

	if (!ClickAnElem ("Click a dimension to modify", API_LevelDimensionID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No level dimension was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
	ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, pen);
	ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, markerSize);
	ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, dimForm);
	ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, loc);
	ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, note1.contentType);
	ACAPI_ELEMENT_MASK_SET (mask, API_LevelDimensionType, note1.contentUStr);

	element.header.layer = APIApplicationLayerAttributeIndex;
	element.levelDimension.pen = 6;
	element.levelDimension.markerSize *= 2;
	element.levelDimension.dimForm = (element.levelDimension.dimForm + 1) % 10;
	element.levelDimension.loc.x += 1.5;
	element.levelDimension.loc.y += 1.5;
	element.levelDimension.note1.contentType == API_NoteContent_Custom ? element.levelDimension.note1.contentType = API_NoteContent_Measured
		: element.levelDimension.note1.contentType = API_NoteContent_Custom;

	auto buildContentWithAllAvailableAutotexts = [&element] () -> GS::UniString {
		std::unordered_map<API_NoteAutotextID, std::unordered_set<API_ElemType>> autotextMap;
		const GSErrCode err = ACAPI_AutoText_GetNoteAutotexts (API_LevelDimensionID, autotextMap);
		if (err != NoError) {
			return "Custom";
		}

		GS::UniString content;
		for (auto& pairItem : autotextMap) {
			const API_NoteAutotextID autotextID = pairItem.first;
			const std::unordered_set<API_ElemType>& currentSet = pairItem.second;

			const bool isGeneralOrParentElemTypeSpecificAutotext =
				currentSet.empty () ||
				currentSet.find (element.levelDimension.parentType) != currentSet.end ();
			if (!isGeneralOrParentElemTypeSpecificAutotext) {
				continue;
			}

			GS::UniString keyword;
			ACAPI_AutoText_GetNoteKeywordForAutotextID (autotextID, keyword);

			if (!content.IsEmpty ()) {
				const GS::UniString separator = " ";
				content += separator;
			}
			content += keyword;
		}
		return content;
	};

	GS::UniString content = buildContentWithAllAvailableAutotexts ();
	element.levelDimension.note1.contentUStr = &content;

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
}		// Do_LevelDimension_Edit


// -----------------------------------------------------------------------------
// Change clicked RadialDimension
// -----------------------------------------------------------------------------

void	Do_RadialDimension_Edit (void)
{
	API_Element element, mask;

	element = {};

	if (!ClickAnElem ("Click a dimension to modify", API_RadialDimensionID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No radial dimension was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err == NoError) {
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
		ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, linPen);
		ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, showOrigo);
		ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, endC);
		ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, markerData.markerSize);
		ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, markerData.markerType);
		ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, note.contentType);
		ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, note.contentUStr);
		ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, note.noteAngle);
		ACAPI_ELEMENT_MASK_SET (mask, API_RadialDimensionType, note.noteSize);

		element.header.layer = APIApplicationLayerAttributeIndex;
		element.radialDimension.linPen = 9;
		element.radialDimension.showOrigo = !element.radialDimension.showOrigo;
		element.radialDimension.endC.y += 1.0;
		element.radialDimension.markerData.markerSize *= 1.2;
		element.radialDimension.markerData.markerType =(API_MarkerID) ((element.radialDimension.markerData.markerType + 1) % 18);
		element.radialDimension.note.contentType == API_NoteContent_Custom ? element.radialDimension.note.contentType = API_NoteContent_Measured
																			: element.radialDimension.note.contentType =	API_NoteContent_Custom;
		GS::UniString content ("Custom");
		element.radialDimension.note.contentUStr = &content;
		element.radialDimension.note.noteAngle += 0.5;
		element.radialDimension.note.noteSize *= 1.5;

		err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	}

	return;
}		// Do_RadialDimension_Edit


// -----------------------------------------------------------------------------
// Change clicked AngleDimension
// -----------------------------------------------------------------------------

void	Do_AngleDimension_Edit (void)
{
	API_Element element, mask;

	element = {};

	if (!ClickAnElem ("Click a dimension to modify", API_AngleDimensionID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No angle dimension was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err == NoError) {
		ACAPI_ELEMENT_MASK_CLEAR (mask);
		ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
		ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, linPen);
		ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, smallArc);
		ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, markerData.markerSize);
		ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, markerData.markerType);
		ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, textPos);
		ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.contentType);
		ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.contentUStr);

		element.header.layer = ACAPI_CreateAttributeIndex ((element.header.layer.ToInt32_Deprecated () + 1) % 10);
		element.angleDimension.linPen = 9;
		element.angleDimension.smallArc = !element.angleDimension.smallArc;
		element.angleDimension.markerData.markerSize *= 1.2;
		element.angleDimension.markerData.markerType =(API_MarkerID) ((element.angleDimension.markerData.markerType + 1) % 18);
		element.angleDimension.textPos =	(API_TextPosID) ((element.angleDimension.textPos + 1) % 3);
		element.angleDimension.note.contentType = API_NoteContent_Custom;
		GS::UniString content ("Custom");
		element.angleDimension.note.contentUStr = &content;

		ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.useLeaderLine);
		ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.fixPos);

		element.angleDimension.note.fixPos = true;
		if (element.angleDimension.note.useLeaderLine) {
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.pos);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.noteAngle);

			element.angleDimension.note.useLeaderLine = false;
			element.angleDimension.note.pos.x = element.angleDimension.note.endC.x;
			element.angleDimension.note.pos.y = element.angleDimension.note.endC.y;
			element.angleDimension.note.noteAngle = PI / 4.0;
		} else {
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.anchorAtTextEnd);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.begC);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.midC);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.endC);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.leaderPen);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.leaderLineType);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.leaderShape);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.arrowData.arrowType);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.arrowData.arrowPen);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.arrowData.arrowSize);
			ACAPI_ELEMENT_MASK_SET (mask, API_AngleDimensionType, note.anchorPoint);

			element.angleDimension.note.useLeaderLine = true;
			element.angleDimension.note.anchorAtTextEnd = false;
			element.angleDimension.note.begC.x = element.angleDimension.note.pos.x;
			element.angleDimension.note.begC.y = element.angleDimension.note.pos.y;
			element.angleDimension.note.midC.x = element.angleDimension.note.begC.x + 1.0;
			element.angleDimension.note.midC.y = element.angleDimension.note.begC.y + 2.0;
			element.angleDimension.note.endC.x = element.angleDimension.note.midC.x + 14.0;
			element.angleDimension.note.endC.y = element.angleDimension.note.midC.y;
			element.angleDimension.note.leaderPen = 9;
			element.angleDimension.note.leaderLineType = ACAPI_CreateAttributeIndex (5);
			element.angleDimension.note.leaderShape = API_Segmented;
			element.angleDimension.note.arrowData.arrowType = APIArr_FullCirc;
			element.angleDimension.note.arrowData.arrowPen = 10;
			element.angleDimension.note.arrowData.arrowSize = 3.0;
			element.angleDimension.note.anchorPoint = APILbl_TopAnchor;
		}

		err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	}

	return;
}		// Do_AngleDimension_Edit
