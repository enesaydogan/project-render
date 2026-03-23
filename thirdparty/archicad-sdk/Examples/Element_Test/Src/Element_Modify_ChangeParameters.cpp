
#include "APIEnvir.h"
#include "ACAPinc.h"

#include "APICommon.h"
#include "Element_Test.h"
#include "GSUtilsDefs.h"
#include "OnExit.hpp"

// -----------------------------------------------------------------------------
// Change the parameters of the active tool or an element instance
//	- the parameter is hardcoded, depends on the element type
//	- the layer is always changed to 'ARCHICAD'
// In case of element instance:
//	- modify other elements in the same group also
//	- check hidden/locked layer
//	- update Automatic Labels
// Check the changes in the settings dialogs or on the info palette
// -----------------------------------------------------------------------------

void		Do_Change_ElemParameters (bool defaults)
{
	API_Element			element = {}, elementMask = {};
	API_ToolBoxItem		tboxInfo = {};
	GSErrCode			err = NoError;

	if (defaults) {
		err = ACAPI_Toolbox_GetToolBoxMode (&tboxInfo);
		if (err != NoError) {
			ErrorBeep ("APIEnv_GetToolBoxMode", err);
			return;
		}
		element.header.type = tboxInfo.type;
		ACAPI_Element_GetDefaults (&element, nullptr);
	} else {
		if (!ClickAnElem ("Click an element to change one of its parameters", API_ZombieElemID, nullptr, &element.header.type, &element.header.guid)) {
			WriteReport_Alert ("No element was clicked");
			return;
		}
		ACAPI_Element_Get (&element);
	}

	if (element.header.hotlinkGuid != APINULLGuid) {
		element.header.guid = element.header.hotlinkGuid;
		ACAPI_Element_Get (&element);
	}

	ACAPI_ELEMENT_MASK_SET (elementMask, API_Elem_Head, layer);
	element.header.layer = APIApplicationLayerAttributeIndex;		// changed to 'ARCHICAD' layer for each type

	if (element.header.type == API_HotlinkID) {
		element.hotlink.suspendFixAngle = !element.hotlink.suspendFixAngle;
		ACAPI_ELEMENT_MASK_SET (elementMask, API_HotlinkType, suspendFixAngle);
		element.hotlink.floorDifference += 1;
		ACAPI_ELEMENT_MASK_SET (elementMask, API_HotlinkType, floorDifference);
		ACAPI_Element_Change (&element, &elementMask, nullptr, 0UL, true);
		return;
	}

	API_ElementMemo	memo = {}, newMemo = {};

	switch (element.header.type.typeID) {
		case API_WallID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_WallType, thickness);
						element.wall.thickness = 1.0;
						// WALL: thickness to 1.0
						break;
		case API_ColumnID:
						ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_ColumnSegment | APIMemoMask_AssemblySegmentCut | APIMemoMask_AssemblySegmentScheme);
						newMemo.columnSegments = reinterpret_cast<API_ColumnSegmentType*> (BMAllocatePtr ((element.column.nSegments + 1) * sizeof (API_ColumnSegmentType), ALLOCATE_CLEAR, 0));
						if (err == NoError && memo.columnSegments != nullptr && newMemo.columnSegments != nullptr) {
							for (USize idx = 0; idx < element.column.nSegments; ++idx)
								newMemo.columnSegments[idx] = memo.columnSegments[idx];

							newMemo.columnSegments[element.column.nSegments] = newMemo.columnSegments[element.column.nSegments - 1];
							newMemo.columnSegments[element.column.nSegments].assemblySegmentData.modelElemStructureType = API_BasicStructure;
							element.column.nSegments++;
						}

						newMemo.assemblySegmentCuts = reinterpret_cast<API_AssemblySegmentCutData*> (BMAllocatePtr ((element.column.nCuts + 1) * sizeof (API_AssemblySegmentCutData), ALLOCATE_CLEAR, 0));
						if (err == NoError && memo.assemblySegmentCuts != nullptr && newMemo.assemblySegmentCuts != nullptr) {
							for (USize idx = 0; idx < element.column.nCuts; ++idx)
								newMemo.assemblySegmentCuts[idx] = memo.assemblySegmentCuts[idx];

							newMemo.assemblySegmentCuts[element.column.nCuts] = newMemo.assemblySegmentCuts[element.column.nSegments - 1];
							newMemo.assemblySegmentCuts[element.column.nCuts].cutType = APIAssemblySegmentCut_Horizontal;
							element.column.nCuts++;
						}

						newMemo.assemblySegmentSchemes = reinterpret_cast<API_AssemblySegmentSchemeData*> (BMAllocatePtr ((element.column.nSchemes + 1) * sizeof (API_AssemblySegmentSchemeData), ALLOCATE_CLEAR, 0));
						if (err == NoError && memo.assemblySegmentSchemes != nullptr && newMemo.assemblySegmentSchemes != nullptr) {
							for (USize idx = 0; idx < element.column.nSchemes; ++idx)
								newMemo.assemblySegmentSchemes[idx] = memo.assemblySegmentSchemes[idx];

							API_AssemblySegmentSchemeData* lastScheme = &newMemo.assemblySegmentSchemes[element.column.nSchemes - 1];
							API_AssemblySegmentSchemeData* newScheme = &newMemo.assemblySegmentSchemes[element.column.nSchemes];

							newScheme->lengthType = lastScheme->lengthType;
							if (lastScheme->lengthType == APIAssemblySegment_Fixed) {
								lastScheme->fixedLength *= 0.5;
								newScheme->fixedLength = lastScheme->fixedLength;
							} else {
								lastScheme->lengthProportion *= 0.5;
								newScheme->lengthProportion = lastScheme->lengthProportion;
							}

							element.column.nSchemes++;
						}

						if (err == NoError) {
							API_Element mask;
							ACAPI_ELEMENT_MASK_CLEAR (mask);
							ACAPI_ELEMENT_MASK_SET (mask, API_ColumnType, nSegments);

							err = ACAPI_Element_Change (&element, &mask, &newMemo, APIMemoMask_ColumnSegment | APIMemoMask_AssemblySegmentCut | APIMemoMask_AssemblySegmentScheme, true);
						}

						ACAPI_DisposeElemMemoHdls (&memo);
						ACAPI_DisposeElemMemoHdls (&newMemo);

						// COLUMN: new segment
						break;
		case API_BeamID:
						ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_BeamSegment | APIMemoMask_AssemblySegmentCut);

						if (err == NoError && memo.beamSegments != nullptr) {
							for (USize idx = 0; idx < element.beam.nSegments; ++idx)
								memo.beamSegments[idx].assemblySegmentData.nominalHeight = 1.0;

							for (USize idx = 0; idx < element.beam.nCuts; ++idx) {
								memo.assemblySegmentCuts[idx].cutType = APIAssemblySegmentCut_Custom;
								memo.assemblySegmentCuts[idx].customAngle = 45.0 * DEGRAD;
							}

							API_Element mask;
							ACAPI_ELEMENT_MASK_CLEAR (mask);

							err = ACAPI_Element_Change (&element, &mask, &memo, APIMemoMask_BeamSegment | APIMemoMask_AssemblySegmentCut, true);
						}

						ACAPI_DisposeElemMemoHdls (&memo);

						// BEAM: height to 1.0, cut angles to 45 degree
						break;
		case API_WindowID:
		case API_DoorID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_WindowType, openingBase.width);
						element.window.openingBase.width = 2.34;
						// WINDOW: width to 2.34
						break;
		case API_ObjectID:
		case API_LampID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_ObjectType, angle);
						element.object.angle = PI / 6.0;
						// OBJECT: angle to 60 degree
						break;
		case API_SlabID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_SlabType, topMat);
						element.slab.topMat = ACAPI_CreateAttributeIndex (34);
						// SLAB: topMat to 34
						break;
		case API_RoofID:
						if (defaults)
							element.roof.roofClass = API_PolyRoofID;

						if (element.roof.roofClass == API_PlaneRoofID) {
							ACAPI_ELEMENT_MASK_SET (elementMask, API_RoofType, shellBase.ltypeInd);
							element.roof.shellBase.ltypeInd = ACAPI_CreateAttributeIndex (4);
							// ROOF: ltype to 4
						} else {
							ACAPI_ELEMENT_MASK_SET (elementMask, API_RoofType, u.polyRoof.levelNum);
							ACAPI_ELEMENT_MASK_SET (elementMask, API_RoofType, u.polyRoof.levelData);
							ACAPI_ELEMENT_MASK_SET (elementMask, API_RoofType, u.polyRoof.overHangType);
							ACAPI_ELEMENT_MASK_SET (elementMask, API_RoofType, u.polyRoof.eavesOverHang);
							element.roof.u.polyRoof.levelNum = 16;
							for (Int32 i = 0; i < element.roof.u.polyRoof.levelNum; i++) {
								element.roof.u.polyRoof.levelData[i].levelAngle = 5.0 * DEGRAD * (element.roof.u.polyRoof.levelNum - i);
								element.roof.u.polyRoof.levelData[i].levelHeight = 0.02 * (i + 1);
							}
							element.roof.u.polyRoof.overHangType = API_OffsetOverhang;
							element.roof.u.polyRoof.eavesOverHang = 0.0;
							// ROOF: roof levels
						}
						break;
		case API_ShellID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_ShellType, shellBase.ltypeInd);
						element.shell.shellBase.ltypeInd = ACAPI_CreateAttributeIndex (4);
						// SHELL: ltype to 4
						break;
		case API_MorphID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_MorphType, displayOption);
						ACAPI_ELEMENT_MASK_SET (elementMask, API_MorphType, level);
						ACAPI_ELEMENT_MASK_SET (elementMask, API_MorphType, castShadow);
						element.morph.displayOption = API_Standard;
						element.morph.level = 1.0;
						element.morph.castShadow = false;
						// MORPH: Floor Plan Display to Projected, level to 1.0, do not cast shadow
						break;
		case API_MeshID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_MeshType, level);
						element.mesh.level = -1.0;
						// MESH: level to -1.0
						break;

		case API_DimensionID:
						// DIM: flip between dynamic and static
						ACAPI_ELEMENT_MASK_SET (elementMask, API_DimensionType, defStaticDim);
						element.dimension.defStaticDim = !element.dimension.defStaticDim;
						ACAPI_ELEMENT_MASK_SET (elementMask, API_DimensionType, horizontalText);
						element.dimension.horizontalText = !element.dimension.horizontalText;
						// DIM: flip between horizontal and parallel
						ACAPI_ELEMENT_MASK_SET (elementMask, API_DimensionType, linPen);
						element.dimension.linPen = 250;
						// DIM: linPen to 250
						ACAPI_ELEMENT_MASK_SET (elementMask.dimension.markerData, API_MarkerData, markerPen);
						element.dimension.markerData.markerPen = 250;
						// DIM: linPen to 250
						break;
		case API_RadialDimensionID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_RadialDimensionType, linPen);
						element.radialDimension.linPen = 250;
						// RADDIM: linPen to 250
						ACAPI_ELEMENT_MASK_SET (elementMask.radialDimension.note, API_NoteType, notePen);
						element.radialDimension.note.notePen = 250;
						ACAPI_ELEMENT_MASK_SET (elementMask.radialDimension.note, API_NoteType, backgroundPen);
						element.radialDimension.note.backgroundPen = 250;
						ACAPI_ELEMENT_MASK_SET (elementMask.radialDimension.note, API_NoteType, leaderPen);
						element.radialDimension.note.leaderPen = 250;
						break;
		case API_LevelDimensionID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_LevelDimensionType, dimForm);
						element.levelDimension.dimForm = 4;
						// LEVDIM: dimForm to 4
						ACAPI_ELEMENT_MASK_SET (elementMask, API_LevelDimensionType, pen);
						element.levelDimension.pen = 4;
						ACAPI_ELEMENT_MASK_SET (elementMask.levelDimension.note1, API_NoteType, notePen);
						element.levelDimension.note1.notePen = 4;
						ACAPI_ELEMENT_MASK_SET (elementMask.levelDimension.note1, API_NoteType, backgroundPen);
						element.levelDimension.note1.backgroundPen = 4;
						ACAPI_ELEMENT_MASK_SET (elementMask.levelDimension.note1, API_NoteType, leaderPen);
						element.levelDimension.note1.leaderPen = 4;
						ACAPI_ELEMENT_MASK_SET (elementMask.levelDimension.note2, API_NoteType, notePen);
						element.levelDimension.note2.notePen = 4;
						ACAPI_ELEMENT_MASK_SET (elementMask.levelDimension.note2, API_NoteType, backgroundPen);
						element.levelDimension.note2.backgroundPen = 4;
						ACAPI_ELEMENT_MASK_SET (elementMask.levelDimension.note2, API_NoteType, leaderPen);
						element.levelDimension.note2.leaderPen = 4;
						break;
		case API_AngleDimensionID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_AngleDimensionType, textPos);
						element.angleDimension.textPos = APIPos_Below;
						// ANGDIM: textPos to Below
						ACAPI_ELEMENT_MASK_SET (elementMask, API_AngleDimensionType, linPen);
						element.angleDimension.linPen = 8;
						// RADDIM: linPen to 8
						ACAPI_ELEMENT_MASK_SET (elementMask.angleDimension.note, API_NoteType, notePen);
						element.angleDimension.note.notePen = 8;
						ACAPI_ELEMENT_MASK_SET (elementMask.angleDimension.note, API_NoteType, backgroundPen);
						element.angleDimension.note.backgroundPen = 8;
						ACAPI_ELEMENT_MASK_SET (elementMask.angleDimension.note, API_NoteType, leaderPen);
						element.angleDimension.note.leaderPen = 8;
						break;

		case API_TextID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_TextType, faceBits);
						element.text.faceBits |= APIFace_Bold;
						// TEXT: face to Bold
						break;
		case API_LabelID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_LabelType, leaderShape);
						element.label.leaderShape = GetNextLeaderLineShape (element.label.leaderShape);
						// LABEL: cycle through shapes
						break;
		case API_ZoneID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_ZoneType, roomName);
						GS::ucscpy (element.zone.roomName, L("WC"));
						// ZONE: name to "WC"
						break;

		case API_HatchID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_HatchType, hatchType);
						element.hatch.hatchType = API_BuildingMaterialHatch;
						ACAPI_ELEMENT_MASK_SET (elementMask, API_HatchType, buildingMaterial);
						element.hatch.buildingMaterial = ACAPI_CreateAttributeIndex (10);
						// HATCH: buildingmaterial index to 10
						break;
		case API_LineID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_LineType, roomSeparator);
						element.line.roomSeparator = true;
						// LINE: roomSeparator to TRUE
						break;
		case API_ArcID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_ArcType, roomSeparator);
						element.arc.roomSeparator = true;
						// ARC: roomSeparator to TRUE
						break;
		case API_SplineID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_SplineType, linePen);
						element.spline.linePen.penIndex = 100;
						element.spline.linePen.colorOverridePenIndex = 0;
						// SPLINE: linePen to 100
						break;
		case API_HotspotID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_HotspotType, pen);
						element.hotspot.pen = 10;
						// HOTSPOT: pen to 10
						break;

		case API_CutPlaneID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_CutPlaneType, segment.textSize);
						ACAPI_ELEMENT_MASK_SET (elementMask, API_CutPlaneType, segment.cutPlIdStr);
						element.cutPlane.segment.textSize = 0.5;
						GS::ucscpy (element.elevation.segment.cutPlIdStr, L ("RefID_API"));
						// CUTPLANE: textSize to 0.5 and reference id to RefID_API
						break;
		case API_ElevationID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_ElevationType, segment.vertRange);
						ACAPI_ELEMENT_MASK_SET (elementMask, API_ElevationType, segment.vertMin);
						ACAPI_ELEMENT_MASK_SET (elementMask, API_ElevationType, segment.vertMax);
						ACAPI_ELEMENT_MASK_SET (elementMask, API_ElevationType, segment.cutPlIdStr);
						element.elevation.segment.vertRange = APIVerRange_Limited;
						element.elevation.segment.vertMin = 1.5;
						element.elevation.segment.vertMax = 3.0;
						GS::ucscpy (element.elevation.segment.cutPlIdStr, L ("RefID_API"));
						// ELEVATION: vertical limits to (1.5 - 3.0) and reference id to RefID_API
						break;
		case API_DetailID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_DetailType, detailIdStr);
						GS::ucscpy (element.detail.detailIdStr, L ("RefID_API"));
						// DETAIL: reference id to RefID_API
						break;
		case API_WorksheetID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_WorksheetType, detailIdStr);
						GS::ucscpy (element.worksheet.detailIdStr, L ("RefID_API"));
						// WORKSHEET: reference id to RefID_API
						break;
		case API_InteriorElevationID:
						if (defaults) {
							ACAPI_ELEMENT_MASK_SET (elementMask, API_InteriorElevationType, segment.shadFillPen);
							ACAPI_ELEMENT_MASK_SET (elementMask, API_InteriorElevationType, segment.shadFillBGPen);
							ACAPI_ELEMENT_MASK_SET (elementMask, API_InteriorElevationType, segment.effectBits);
							ACAPI_ELEMENT_MASK_SET (elementMask, API_InteriorElevationType, segment.cutPlIdStr);
							element.interiorElevation.segment.shadFillPen = 77;
							element.interiorElevation.segment.shadFillBGPen = 88;
							element.interiorElevation.segment.effectBits |= APICutPl_VectorShadow;
							GS::ucscpy (element.interiorElevation.segment.cutPlIdStr, L ("RefID_API"));
							// INTERIOR ELEVATION: shadow fill and background pen to 77, 88 and reference id to RefID_API
						}
						break;

		case API_CameraID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_CameraType, perspCam.pen);
						element.camera.perspCam.pen = 200;
						// CAMERA: pen to 200
						break;

		case API_PictureID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_PictureType, mirrored);
						element.picture.mirrored = !element.picture.mirrored;
						// PICTURE: mirror the picture
						break;

		case API_DrawingID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_DrawingType, angle);
						element.drawing.angle += 0.5235987755983;
						// DRAWING: rotate by 30 degrees
						break;

		case API_StairID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_StairType, treadDepth);
						ACAPI_ELEMENT_MASK_SET (elementMask, API_StairType, treadDepthLocked);
						element.stair.treadDepth *= 2;
						element.stair.treadDepthLocked = false;
						// STAIR: unlock and increase tread depth by 20%
						break;

		case API_RailingID:
						ACAPI_ELEMENT_MASK_SET (elementMask, API_RailingType, referenceLinePen);
						element.railing.referenceLinePen = 200;
						// RAILING: change reference line pen to 200
						break;

		case API_GroupID:
		case API_SectElemID:
		case API_CamSetID:
		default:
						break;
	}

	if (element.header.guid != APINULLGuid) {
		err = ACAPI_Element_ChangeParameters ({element.header.guid}, &element, nullptr, &elementMask);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeParameters", err);
	} else {
		err = ACAPI_Element_ChangeDefaults (&element, nullptr, &elementMask);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeDefaults", err);
	}
}		// Do_Change_ElemParameters


// -----------------------------------------------------------------------------
//
// -----------------------------------------------------------------------------
bool Do_ModifyProfileParameter (const GS::UniString& parameterName, double newValue)
{
	API_SelectionInfo 	selectionInfo;
	GS::Array<API_Neig>	selNeigs;
	GSErrCode			err;

	// Get selection
	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);
	if (err != NoError && err != APIERR_NOSEL) {
		ErrorBeep ("ACAPI_Selection_GetInfo", err);
		return false;
	}

	if (selectionInfo.typeID == API_SelEmpty) {
		// Ask the user to click an element
		API_Neig	neig;
		if (!ClickAnElem ("Click a profiled beam segment", API_BeamSegmentID, &neig) ||
			!ACAPI_Element_Filter (neig.guid, APIFilt_IsEditable)) {
			WriteReport_Alert ("No editable column was clicked");
			return false;
		}

		selNeigs.Push (neig);
		selectionInfo.sel_nElemEdit = 1;
	}

	API_Guid elemGuid = selNeigs[0].guid;

	API_Element element = {};
	element.header.guid = elemGuid;
	if (DBERROR (ACAPI_Element_Get (&element) != NoError))
		return false;

	const bool isElemAttributeProfiledWall			= element.header.type == API_WallID &&
													element.wall.modelElemStructureType == API_ProfileStructure &&
													element.wall.profileAttr != APIInvalidAttributeIndex;
	const bool isElemAttributeProfiledBeamSegment	= element.header.type == API_BeamSegmentID &&
													element.beamSegment.assemblySegmentData.modelElemStructureType == API_ProfileStructure &&
													element.beamSegment.assemblySegmentData.profileAttr != APIInvalidAttributeIndex;
	const bool isElemAttributeProfiledColumnSegment = element.header.type == API_ColumnSegmentID &&
													element.columnSegment.assemblySegmentData.modelElemStructureType == API_ProfileStructure &&
													element.columnSegment.assemblySegmentData.profileAttr != APIInvalidAttributeIndex;

	if (isElemAttributeProfiledWall || isElemAttributeProfiledBeamSegment || isElemAttributeProfiledColumnSegment) {
		GS::Array<API_PropertyDefinition> propertyDefinitions;
		if (DBERROR (ACAPI_Element_GetPropertyDefinitions (elemGuid, API_PropertyDefinitionFilter_BuiltIn, propertyDefinitions) != NoError)) {
			return false;
		}
		for (const API_PropertyDefinition& propertyDefinition : propertyDefinitions) {
			if (propertyDefinition.definitionType != API_PropertyDynamicBuiltInDefinitionType) {
				continue;
			}
			if (propertyDefinition.name == parameterName) { // Make sure you dont name a parameter "Position" or "Renovation Status"
				API_Property property;
				if (DBERROR (ACAPI_Element_GetPropertyValue (elemGuid, propertyDefinition.guid, property) != NoError)) {
					return false;
				}
				property.value.singleVariant.variant.doubleValue = newValue;
				if (DBVERIFY (ACAPI_Element_SetProperty (elemGuid, property) == NoError)) {
					return true;
				} else {
					return false;
				}
			}
		}
	}

	return false;
}


void Do_CopyAddPars_FromFirstClickedElementIntoSecondClickedElement ()
{
	API_Guid	sourceElemGuid;
	if (!ClickAnElem ("Click a library part-based element to copy parameters from", API_ZombieElemID, nullptr, nullptr, &sourceElemGuid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	API_ElementMemo	memo = {};
	const GS::OnExit deleteMemo ([&memo] () { ACAPI_DisposeElemMemoHdls (&memo); });

	GSErrCode err = ACAPI_Element_GetMemo (sourceElemGuid, &memo, APIMemoMask_AddPars);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Element_GetMemo failed", err);
		return;
	}

	if (memo.params == nullptr) {
		WriteReport_Alert ("The clicked element is not a library part-based element");
		return;
	}

	API_Guid	targetElemGuid;
	if (!ClickAnElem ("Click a library part-based element to copy parameters into", API_ZombieElemID, nullptr, nullptr, &targetElemGuid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	err = ACAPI_Element_ChangeMemo (targetElemGuid, APIMemoMask_AddPars, &memo);
	if (err != NoError) {
		WriteReport_Err ("ACAPI_Element_ChangeMemo failed", err);
		return;
	}
}



// -----------------------------------------------------------------------------
// Change an array-type parameter
// -----------------------------------------------------------------------------

bool ChangeArrayParam (API_ElementMemo		*inMemo, 		// memo of the given library part element
                       const char			*paramName,
					   Int32 				inDim1,			// the new first dimension
					   Int32 				inDim2,			// the new second dimension
					   const GS::uchar_t	*inParVal,		// the new value
					   Int32 				inIndex1,		// first index of the new value
					   Int32 				inIndex2)		// second index of the new value
{
	Int32 n = BMGetHandleSize ((GSHandle) inMemo->params) / sizeof (API_AddParType);

	if (inIndex1 >= inDim1 || inIndex2 >= inDim2 || inIndex1 < 0 || inIndex2 < 0 || inParVal == nullptr)
		return false;
	// Bad parameters

	// searching for an array parameter
	for (Int32 i = 0; i < n; i++) {
		// check if it's an array parameter and the array contains strings
		if ((*inMemo->params)[i].typeMod == API_ParArray &&
				((*inMemo->params)[i].typeID == APIParT_CString || (*inMemo->params)[i].typeID == APIParT_Title) &&
				CHCompareASCII ((*inMemo->params)[i].name, paramName) == 0)
		{
			GS::uchar_t **origArrHdl = (GS::uchar_t **) (*inMemo->params)[i].value.array;
			Int32 origDim1 = (*inMemo->params)[i].dim1;
			Int32 origDim2 = (*inMemo->params)[i].dim2;

			// calculating new size of the array
			Int32 newSize = 0;
			Int32 lastPos = 0;
			for (Int32 j = 0; j < inDim1; j++) {
				for (Int32 k = 0; k < inDim2; k++) {
					Int32 size = 1;
					// 1 for the closing '\0' character
					if (j < origDim1 && k < origDim2) {
						size += GS::ucslen32 (&(*origArrHdl)[lastPos]);
						lastPos += size;
					}
					if (j == inIndex1 && k == inIndex2 && inParVal != nullptr)
						newSize += GS::ucslen32 (inParVal) + 1;
					else
						newSize += size;
				}
			}

			GS::uchar_t **newArrHdl = (GS::uchar_t **) BMAllocateHandle (newSize * sizeof (GS::uchar_t), ALLOCATE_CLEAR, 0);

			// changing array size if needed:
			if (origDim1 != inDim1 || origDim2 != inDim2) {
				(*inMemo->params)[i].dim1 = inDim1;
				(*inMemo->params)[i].dim2 = inDim2;
			}

			// changing array content:
			Int32 lastPosO = 0;
			Int32 lastPosN = 0;
			for (Int32 j = 0; j < inDim1; j++) {
				for (Int32 k = 0; k < inDim2; k++) {
					if (j == inIndex1 && k == inIndex2 && inParVal != nullptr) {
						GS::ucscpy (&(*newArrHdl)[lastPosN], inParVal);
						lastPosN += GS::ucslen32 (&(*newArrHdl)[lastPosN]) + 1;
						continue;
					}
					if (j < origDim1 && k < origDim2) {
						GS::ucscpy (&(*newArrHdl)[lastPosN], &(*origArrHdl)[lastPosO]);
						lastPosO += GS::ucslen32 (&(*origArrHdl)[lastPosO]) + 1;
						lastPosN += GS::ucslen32 (&(*newArrHdl)[lastPosN]) + 1;
					} else {
						GS::ucscpy (&(*newArrHdl)[lastPosN], L("\0"));
						// '\0' character for the empty items
						lastPosN += GS::ucslen32 (&(*newArrHdl)[lastPosN]) + 1;
					}
				}
			}

			// kill original array handle and change it to the new
			BMKillHandle ((GSHandle *) &origArrHdl);
			(*inMemo->params)[i].value.array = (GSHandle) newArrHdl;

			return true;
		}
	}

	return false;

}		// ChangeArrayParam


// -----------------------------------------------------------------------------
// Change the user ID of the clicked element
//	- modify other elements in the same group also
//	- check hidden/locked layer
//	- update Automatic Labels
// -----------------------------------------------------------------------------

void		Do_Change_ElemInfo (void)
{
	API_ElemType		type;
	API_Guid			guid;

	if (!ClickAnElem ("Click an elem to modify the UserID", API_ZombieElemID, nullptr, &type, &guid)) {
		WriteReport_Alert ("No element was clicked");
		return;
	}

	if (type != API_WallID && type != API_ColumnID && type != API_BeamID &&
		type != API_SlabID && type != API_RoofID && type != API_MeshID && type != API_ShellID &&
		type != API_WindowID && type != API_DoorID && type != API_SkylightID &&
		type != API_ObjectID && type != API_LampID &&
		type != API_ZoneID && type != API_HatchID &&
		type != API_MorphID &&
		type != API_StairID && type != API_RailingID)
	{
		WriteReport_Alert ("The clicked element has no UserID");
		return;
	}

	API_Element element = {};
	element.header.type = type;
	element.header.guid	= guid;
	GSErrCode err = ACAPI_Element_Get (&element);

	API_Element mask;
	ACAPI_ELEMENT_MASK_CLEAR (mask);

	API_ElementMemo memo = {};
	if (ACAPI_Element_GetMemo (element.header.guid, &memo, APIMemoMask_ElemInfoString) == NoError) {
		if (memo.elemInfoString != nullptr) {
			delete memo.elemInfoString;
		}
		memo.elemInfoString = new GS::UniString ("Changed ID");
	}

	if (err == NoError) {
		switch (type.typeID) {
			case API_WallID:
			case API_ColumnID:
			case API_BeamID:
			case API_SlabID:
			case API_RoofID:
			case API_ShellID:
			case API_MorphID:
			case API_MeshID:
			case API_WindowID:
			case API_DoorID:
			case API_SkylightID:
			case API_ObjectID:
			case API_LampID:
			case API_ZoneID:
			case API_HatchID:
			case API_StairID:
			case API_RailingID:
				break;
			default:
				err = APIERR_BADID;
				break;
		}
	}

	if (err == NoError) {
		err = ACAPI_Element_ChangeParameters ({guid}, &element, &memo, &mask);
		if (err != NoError)
			ErrorBeep ("ACAPI_Element_ChangeParameters", err);
	}

	ACAPI_DisposeElemMemoHdls (&memo);
	return;
}		// Do_Change_ElemInfo
