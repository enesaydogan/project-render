// *****************************************************************************
// Header file for the Element Test Add-On
// =============================================================================

#if !defined _ELEMENT_TEST_H
#define _ELEMENT_TEST_H

// =============================================================================
//
// General element functions
//
// =============================================================================

void	Do_RefreshElements (void);
void	Do_CountLines (void);
void	Do_CreateLine (API_Guid& guid);
void	Do_GetLineByGuid (const API_Guid& guid);
void	Do_CreateDetail (void);
void	Do_CreateLabel (void);
void	Do_CreateLabel_Associative (void);
void	Do_CreateLabel_FixedFrame (void);
void	Do_CreateZone (short geometryId);
void	Do_CreateZoneWithAutoLabelFavorite (void);
void	Do_SetAutoZoneLabelFavorite_FirstByABC (void);
void	Do_ChangeAutoZoneLabeling (void);
void	Do_CreateCamset (void);
void	Do_CreatePerspCam (void);
void	Do_CreatePicture (void);
void	Do_CreateGroupedLines (void);
void	Do_CreateCutPlane (void);
void	Do_CreateInteriorElevation (void);
void	Do_CreateWindow (void);
void	Do_CreateSkylight (void);
void	Do_CreatePolyRoof (void);
void	Do_CreateExtrudedShell (void);
void	Do_CreateRevolvedShell (void);
void	Do_CreateRuledShell (void);
void	Do_CreateMorph (void);
void	Do_CreateSlab (void);
void	Do_CreateCurvedWall (void);
void	Do_CreateCurvedBeam (void);
void	Do_CreateCurtainWall_UsingPattern (void);
void	Do_CreateCurtainWall_WithCustomFrames (void);
void	Do_CreateStair (void);
void	Do_CreateRailing (void);
void	Do_CreateWord (const API_Guid& renFiltGuid);
void	Do_CreateDrawingNested (void);
void	Do_CreateDrawingFromGroundFloor (void);
void	Do_CreateDrawingFromSelected2DElems (void);
void	Do_SelectElems (void);
void	Do_DeleteElems (void);
void	Do_PickupElem (void);
void	Do_ChangeElem (void);
void	Do_ExplodeElem (void);
void	Do_CopyElem (void);
void	Do_PickupProperties (void);
void	Do_FillProperties (void);
void	Do_DumpElem (API_Guid& renFiltGuid);
void	Do_SetElemCategories (bool changeDefaults);
void	Do_CreateIrregularMesh (void);
void	Do_CreateIrregularSlab (void);
void	Do_CreateIrregularExtShell (void);
void	Do_CreateChangeMarker (void);
void	Do_CreateStaticDimension (void);
void	Do_CreateStaticAngleDimension (void);
void	Do_CreateHatches (void);
void	Do_CreateArc (const API_Coord& point, short colorOverridePenIndex);
void	Do_CreateLines (void);
void	Do_CreateWorksheet (void);
void    Do_PlaceWorksheetOnLayout (void);
void    Do_DetectCollisionsBasedOnSelection (void);
void	Do_CreateColumn (void);
void	Do_CreateBeam (void);
void	Do_CreateOpening (void);
void	Do_CreateWallsSeparateUndoSteps (void);
void	Do_CreateWallsOneUndoSteps (void);
void	Do_CreateAssociativeDimensions (void);
void	Do_CreateAssociativeDimensionsOnSection (void);

GSErrCode	Do_CreateDrawingWithDummyElems (const API_Guid& elemGuid, const API_Coord* pos = nullptr);


// =============================================================================
//
// Edit elements
//
// =============================================================================

constexpr bool WithInput = true;
constexpr bool WithoutInput = false;

void	Do_DragElems (bool withInput);
void	Do_StretchElems (bool withInput);
void	Do_ResizeElems (bool withInput);
void	Do_RotateElems (bool withInput);
void	Do_MirrorElems (bool withInput);
void	Do_ElevElems (bool withInput);


// =============================================================================
//
// Apply tools on elements
//
// =============================================================================

void	Do_ElemTool (bool withInput, const char* inputStr, API_ToolCmdID typeID);
void	Do_SuspendGroups (void);


// =============================================================================
//
// Modify elements
//
// =============================================================================

void	Do_Change_ElemInfo (void);
void	Do_Change_ElemParameters (bool defaults);
void	Do_CopyAddPars_FromFirstClickedElementIntoSecondClickedElement ();
void	Do_Poly_AdjustNode (void);
void	Do_Poly_InsertNode (void);
void	Do_Poly_DeleteNode (void);
void	Do_Poly_DeleteHole (void);
void	Do_Poly_NewHole (void);

void	Do_Wall_Edit (void);
void	Do_CurtainWall_Edit (void);
void	Do_Column_Edit (void);
void	Do_Beam_Edit (void);
void	Do_Window_Edit (void);
void	Do_Skylight_Edit (void);
void	Do_Object_Edit (void);
void	Do_Ceil_Edit (void);
void	Do_Roof_Edit (void);
void	Do_Shell_Edit (void);
void	Do_Shell_Edit2 (void);
void	Do_Morph_Edit (void);
void	Do_Stair_Edit (void);
void	Do_Railing_Edit (void);
void	Do_Railing_Elevate_Node (void);
void	Do_Mesh_Edit (void);
void	Do_Zone_Edit (short mode);
void	Do_Word_Edit (short mode);
void	Do_2D_Edit (bool withColorOverride);
void	Do_Hotspot_Edit (void);
void	Do_Spline_Edit (void);
void	Do_CutPlane_Edit (void);
void	Do_Hatch_Edit (void);
void	Do_PolyLine_Edit (void);
void	Do_Label_Edit (void);
void	Do_Dimension_Edit (void);
void	Do_Dimension_Pen_Edit (void);
void	Do_LevelDimension_Edit (void);
void	Do_RadialDimension_Edit (void);
void	Do_AngleDimension_Edit (void);
void	Do_Detail_Edit (void);
void	Do_ChangeMarker_Edit (void);
void	Do_Dimensions_Test (void);
void	Do_Drawing_Edit (void);
void	Do_UpdateSelectedDrawings (void);
void	Do_ChangeInteriorElevation (void);
void	Do_RotateRenovationStatus (void);
void	Do_Arc_Edit (void);
void	Do_TestMaterialChaining (void);
void	Do_CutPlane_Or_Elevation_Edit_Coords (void);

bool	Do_ModifyProfileParameter (const GS::UniString& parameterName, double newValue);
void	Do_Modify_Opening (void);

// =============================================================================
//
// Group elements
//
// =============================================================================

void	Do_CreateCustomGroup (void);
void	Do_ListMyGroups (void);
void	Do_ChangeMyGroup (void);
void	Do_UnbindMyGroup (void);

// =============================================================================
//
// Observe elements
//
// =============================================================================

void	Do_CatchElementCreation (void);


// =============================================================================
//
// Manage element sets
//
// =============================================================================

void	Do_ElemSet_Create (void);
void	Do_ElemSet_Delete (void);


// =============================================================================
//
// Manage solid operation links
//
// =============================================================================

void	Do_SolidOperation_Create (void);
void	Do_SolidLink_Create (void);
void	Do_SolidLink_Remove (void);
void	Do_SolidLink_Targets (void);
void	Do_SolidLink_Operators (void);


// =============================================================================
//
// Manage trim/merge
//
// =============================================================================

void	Do_Trim_Elements (void);
void	Do_Trim_ElementsWith (void);
void	Do_Trim_Remove (void);
void	Do_Trim_GetTrimType (void);
void	Do_Trim_GetTrimmedElements (void);
void	Do_Trim_GetTrimmingElements (void);
void	Do_Merge_Elements (void);
void	Do_Merge_GetMergedElements (void);
void	Do_Merge_Remove (void);


// =============================================================================
//
// Manage hotlinks
//
// =============================================================================

GSErrCode	Do_CreateHotlink ();
GSErrCode	Do_ChangeHotlink ();
GSErrCode	Do_UpdateHotlink ();
GSErrCode	Do_DeleteHotlink ();
GSErrCode	Do_BreakHotlink ();
void		Do_ListHotlinks ();
void		Do_ListHotlinkProxyElements ();
void		Do_ModifiyHotlinkNode ();


// =============================================================================
//
// Tips & Tricks
//
// =============================================================================

void	Do_AvoidFlashing (short mode);
void	Do_GetFirstWindowPosition (void);


// =============================================================================
//
// Code snippets
//
// =============================================================================

void	Do_DeleteCamset (bool active);
void	Do_DumpZone (void);
void	Do_DumpWall (void);
void	Do_DumpBeam (void);
void	Do_DumpWindow (void);
void	Do_DumpDoor (void);
void	Do_DumpShell (void);
void	Do_NeigToCoord (void);
void	Do_GetBounds (void);
void	Do_ShowSymbolHotspots (void);
void	Do_CalcQuantities (void);
void	Do_GetComponents (void);
void	Do_CalcComponentQuantities (void);
void	Do_SetToolBoxMode (void);
void	Do_ChangeSubtypeSettings (void);
void	Do_SplitPolyRoof (void);

GSErrCode Do_ExportPrimitives (const API_IOParams *ioParams);
GSErrCode Do_Import (const API_IOParams *ioParams);

// =============================================================================
//
// Utility functions
//
// =============================================================================


bool	GetEditVector (API_Coord3D	*begC,
					   API_Coord3D	*endC,
					   const char	*prompt,
					   bool		begGiven);

bool	GetEditArc (API_Coord3D	*begC,
					API_Coord3D *endC,
					API_Coord3D *origC,
					const char	*prompt);

bool	DumpPolygon (const API_Element			*element,
					 const API_AttributeIndex&	lineInd,
					 const double				offset,
					 Int32						nCoords,
					 Int32						nSubPolys,
					 Int32						nArcs,
					 API_Coord					**coords,
					 Int32						**subPolys,
					 API_PolyArc				**arcs,
					 bool						createShadow,
				 	 bool						writeReport);

void	SearchActiveCamset (API_Guid*	actGuid,
							API_Guid*	perspGuid);

GSErrCode	CreateMultiTextElement (const API_Coord& pos, double scale = 1.0, const API_Guid& renFiltGuid = APINULLGuid);

GSErrCode	CreateRectangleFill (const API_Coord&			bottomLeftC,
								 double 					width,
								 double 					height,
								 const API_AttributeIndex& 	fillType,
                                 short 						bgPen,
                                 short 						fgPen,
								 const API_AttributeIndex& 	contourLineType,
                                 short 						contourPen);

// ---------------------------------- Prototypes -------------------------------


API_LeaderLineShapeID	GetNextLeaderLineShape (API_LeaderLineShapeID shape);
GSErrCode	UpdateAllLabels (const API_Guid& elemGuid, const API_Coord& firstBegC);

#endif
