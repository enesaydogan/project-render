#include	"APIEnvir.h"
#include	"ACAPinc.h"					// also includes APIdefs.h
#include	"APICommon.h"

#include	"Element_Test.h"

#include	"DG.h"


#include	"GSUtilsDefs.h"

#include	"BuiltInLibrary.hpp"

// -----------------------------------------------------------------------------
// Return a randomly chosen drawing title library part index
// -----------------------------------------------------------------------------
typedef struct LibPart {
	GS::String		name;
	Int32			libInd;

	explicit LibPart () : libInd (-1)
		{}

	LibPart (const GS::String& inName, Int32 inLibInd) :
		name (inName), libInd (inLibInd)
		{}

	LibPart (const LibPart& rhs) :
		name (rhs.name), libInd (rhs.libInd)
		{}

	~LibPart ()
		{}

	LibPart& operator= (const LibPart& rhs)
		{
			if (&rhs != this) {
				name	= rhs.name;
				libInd = rhs.libInd;
			}
			return *this;
		}

} LibPart;

// -----------------------------------------------------------------------------
// Create a drawing element
// -----------------------------------------------------------------------------
GSErrCode	Do_CreateDrawingWithDummyElems (const API_Guid& elemGuid, const API_Coord* pos /*= nullptr*/)
{
	API_Element element = {};
	element.header.type = API_DrawingID;

	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (drawing)", err);
		return err;
	}

	if (pos != nullptr) {
		element.drawing.pos = *pos;
	} else if (!ClickAPoint ("Enter the left bottom position of the drawing", &element.drawing.pos)) {
		return NoError;
	}

	// create drawing data
	GSPtr		drawingData = nullptr;
	double		scale = 1.0;
	GS::Array<API_Pen>* pens = nullptr;

	GS::Array<API_Attribute> attributes;
	ACAPI_Attribute_GetAttributesByType (API_PenTableID, attributes);
	for (const API_Attribute& attribute : attributes) {
		if (attribute.penTable.inEffectForModel) {
			API_AttributeDefExt		defs;
			ACAPI_Attribute_GetDefExt (API_PenTableID, attribute.header.index, &defs);
			pens = defs.penTable_Items;
			break;
		}
	}

	err = ACAPI_Drawing_StartDrawingData (&scale, pens);
	if (pens != nullptr)
		delete pens;

	if (err == NoError) {
		API_Element	elemInDrawing = {};
		elemInDrawing.header.type = API_LineID;
		elemInDrawing.header.layer = ACAPI_CreateAttributeIndex (2);
		elemInDrawing.line.linePen.penIndex = 3;
		elemInDrawing.line.ltypeInd = ACAPI_CreateAttributeIndex (5);

		elemInDrawing.line.begC = element.drawing.pos;
		elemInDrawing.line.endC = element.drawing.pos;
		elemInDrawing.line.begC.x += 0.3;
		elemInDrawing.line.begC.y += 0.2;
		elemInDrawing.line.endC.x += 0.4;
		elemInDrawing.line.endC.y += 0.3;
		err = ACAPI_Element_Create (&elemInDrawing, nullptr);		// line is created into the drawing

		if (err == NoError) {
			elemInDrawing.line.linePen.penIndex = 6;
			elemInDrawing.line.ltypeInd = ACAPI_CreateAttributeIndex (10);
			elemInDrawing.line.begC = element.drawing.pos;
			elemInDrawing.line.endC = element.drawing.pos;
			elemInDrawing.line.begC.x += 0.2;
			elemInDrawing.line.begC.y += 0.3;
			elemInDrawing.line.endC.x += 0.3;
			elemInDrawing.line.endC.y += 0.4;
			err = ACAPI_Element_Create (&elemInDrawing, nullptr);		// another line is created into the drawing
		}

		if (err == NoError) {
			elemInDrawing = {};
			elemInDrawing.header.type = API_CircleID;
			elemInDrawing.header.layer = ACAPI_CreateAttributeIndex (5);
			elemInDrawing.circle.linePen.penIndex = 4;
			elemInDrawing.circle.ltypeInd = ACAPI_CreateAttributeIndex (4);

			elemInDrawing.circle.origC = element.drawing.pos;
			elemInDrawing.circle.origC.x += 0.25;
			elemInDrawing.circle.origC.y += 0.15;
			elemInDrawing.circle.r = 0.09;
			elemInDrawing.circle.ratio = 0.8;
			err = ACAPI_Element_Create (&elemInDrawing, nullptr);		// ellipse is created into the drawing
		}

		if (err == NoError) {
			API_Coord	transformedPos = element.drawing.pos;
			transformedPos.x += 0.02;
			transformedPos.y += 0.02;
			err = CreateMultiTextElement (transformedPos);					// multitext element is created into the drawing
		}

		if (err == NoError)
			err = ACAPI_Drawing_StopDrawingData (&drawingData, &element.drawing.bounds);
	}

	if (err != NoError || drawingData == nullptr) {
		return err;
	}

	// create drawing element
	element.header.type = API_DrawingID;
	element.header.guid = elemGuid;
	CHCopyC ("Drawing element from Element Test test add-on", element.drawing.name);
	element.drawing.numberingType = APINumbering_ByLayout;
	element.drawing.nameType = APIName_CustomName;
	element.drawing.ratio = 1.0;
	element.drawing.anchorPoint = APIAnc_LB;
	element.drawing.isCutWithFrame = true;
	element.drawing.penTableUsageMode = APIPenTableUsageMode_UseOwnPenTable;

	double dx = 0.05 * (element.drawing.bounds.xMax - element.drawing.bounds.xMin);		// add 10% padding
	double dy = 0.05 * (element.drawing.bounds.yMax - element.drawing.bounds.yMin);
	element.drawing.bounds.xMax += dx;
	element.drawing.bounds.yMax += dy;
	element.drawing.bounds.xMin -= dx;
	element.drawing.bounds.yMin -= dy;

	API_ElementMemo	memo = {};
	memo.drawingData = drawingData;

	{	// clip with polygon
		const double width	= element.drawing.bounds.xMax - element.drawing.bounds.xMin;
		const double height	= element.drawing.bounds.yMax - element.drawing.bounds.yMin;
		double offsX = element.drawing.pos.x;
		double offsY = element.drawing.pos.y;

		// horizontal alignment
		switch (element.drawing.anchorPoint) {
			case APIAnc_LB:
			case APIAnc_LM:
			case APIAnc_LT:
					break;
			case APIAnc_MB:
			case APIAnc_MM:
			case APIAnc_MT:
					offsX -= width / 2.0;
					break;
			case APIAnc_RB:
			case APIAnc_RM:
			case APIAnc_RT:
					offsX -= width;
					break;
		}

		// vertical alignment
		switch (element.drawing.anchorPoint) {
			case APIAnc_LB:
			case APIAnc_MB:
			case APIAnc_RB:
					break;
			case APIAnc_LM:
			case APIAnc_MM:
			case APIAnc_RM:
					offsY -= height / 2.0;
					break;
			case APIAnc_LT:
			case APIAnc_MT:
			case APIAnc_RT:
					offsY -= height;
					break;
		}

		element.drawing.poly.nCoords = 6;
		element.drawing.poly.nSubPolys = 1;
		element.drawing.poly.nArcs = 0;
		memo.coords = (API_Coord**) BMAllocateHandle ((element.drawing.poly.nCoords + 1) * sizeof (API_Coord), ALLOCATE_CLEAR, 0);
		memo.pends = (Int32**) BMAllocateHandle ((element.drawing.poly.nSubPolys + 1) * sizeof (Int32), ALLOCATE_CLEAR, 0);
		if (memo.coords != nullptr && memo.pends != nullptr) {
			(*memo.coords)[1].x = offsX;
			(*memo.coords)[1].y = offsY;
			(*memo.coords)[2].x = offsX + width;
			(*memo.coords)[2].y = offsY + 0.2 * height;
			(*memo.coords)[3].x = offsX + width;
			(*memo.coords)[3].y = offsY + height;
			(*memo.coords)[4].x = offsX + 0.8 * width;
			(*memo.coords)[4].y = offsY + 0.8 * height;
			(*memo.coords)[5].x = offsX;
			(*memo.coords)[5].y = offsY + height;
			(*memo.coords)[6].x = (*memo.coords)[1].x;
			(*memo.coords)[6].y = (*memo.coords)[1].y;

			(*memo.pends)[0] = 0;
			(*memo.pends)[1] = element.drawing.poly.nCoords;
		}
	}

	// add title
	API_ServerApplicationInfo	serverApp;
	ACAPI_AddOnIdentification_Application (&serverApp);

	API_LibPart linearTitle = {};
	const GS::UniString	uName ("Linear Drawing Title " + GS::UniString::Printf ("%hu", serverApp.mainVersion));
	GS::ucscpy (linearTitle.docu_UName, uName.ToUStr ());

	err = ACAPI_LibraryPart_Search (&linearTitle, false);
	if (err == NoError) {
		delete linearTitle.location;

		element.drawing.title.libInd = linearTitle.index;
		element.drawing.title.useUniformTextFormat = true;
		element.drawing.title.useUniformSymbolPens = true;
		element.drawing.title.pen = 88;

		double	aParam   = 0.0;
		double	bParam   = 0.0;
		Int32	paramNum = 0;

		err = ACAPI_LibraryPart_GetParams (linearTitle.index, &aParam, &bParam, &paramNum, &memo.params);
		if (err == NoError) {
			bool customFlagModified = false;
			bool customNameModified = false;

			for (Int32 ii = 0; ii < paramNum; ++ii) {
				if (CHCompareCStrings ("gs_drawing_name_custom", (*memo.params)[ii].name) == 0) {
					(*memo.params)[ii].value.real = 1.0;
					customFlagModified = true;
				} else if (CHCompareCStrings ("gs_drawing_name_custom_text", (*memo.params)[ii].name) == 0) {
					const GS::UniString tmpUStr ("TestName");
					GS::ucscpy ((*memo.params)[ii].value.uStr, tmpUStr.ToUStr ());
					customNameModified = true;
				}
				if (customFlagModified && customNameModified) {
					break;
				}
			}
		}
	}
	err = ACAPI_Element_Create (&element, &memo);

	ACAPI_DisposeElemMemoHdls (&memo);

	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (drawing)", err);

	return err;
}		// Do_CreateDrawingWithDummyElems


// -----------------------------------------------------------------------------
// Creates a Drawing (1:OuterDrawingScale) with the following content:
//   - A Fill with a pattern and contour line, that is in "Model Size".
//   - A Fill with a pattern and contour line, that is in "Paper Size".
//   - A Drawing (1:InnerDrawingScale) with the following content:
//     - A Fill with a pattern and contour line, that is in "Model Size".
//     - A Fill with a pattern and contour line, that is in "Paper Size".
// -----------------------------------------------------------------------------
void	Do_CreateDrawingNested (void)
{
	const double					DrawingPadding			= 0.08;	// width of padding between Drawing edge and the content
	const double					FillRectWidth			= 0.2;	// width of the Fills in model size (the scales will be applied to it)
	const double					FillRectHeight			= 0.2;	// height of the Fills in model size (the scales will be applied to it)

	double							OuterDrawingScale		= 1;	// the outer Fills are scaled by this
	double							InnerDrawingScale		= 2;	// the inner Fills are scaled with OuterDrawingScale and this

	const API_AttributeIndex		ModelSizeFillTypeIndex	= ACAPI_CreateAttributeIndex (130);	// Grid 5x5 Diagonal
	const API_AttributeIndex		PaperSizeFillTypeIndex	= ACAPI_CreateAttributeIndex (32);	// Triangles
	const API_AttributeIndex		ModelSizeLineTypeIndex	= ACAPI_CreateAttributeIndex (23);	// Insulation 1:50
	const API_AttributeIndex		PaperSizeLineTypeIndex	= ACAPI_CreateAttributeIndex (9);	// Zigzag
	const short						FillBackgroundPenIndex	= 117;	// yellow
	const short						FillForegroundPenIndex	= 46;	// blue (bold)
	const short						FillContourLinePenIndex	= 38;	// orange (bold)

	DBASSERT_STR (InnerDrawingScale >= 1, "The Fills may overlap.");

	API_Coord clickedPoint;
	if (!ClickAPoint ("Click where to place the Drawing.", &clickedPoint)) {
		return;
	}

	// create Drawing data
	ACAPI_Drawing_StartDrawingData (&OuterDrawingScale);

	const double wOuter = FillRectWidth / OuterDrawingScale;
	const double hOuter = FillRectHeight / OuterDrawingScale;
	const double wInner = FillRectWidth / OuterDrawingScale / InnerDrawingScale;
	const double hInner = FillRectHeight / OuterDrawingScale / InnerDrawingScale;

	const API_Coord p0 = { clickedPoint.x + DrawingPadding, clickedPoint.y + DrawingPadding };

	const API_Coord bottomLeftC1 = { p0.x,          p0.y + hOuter };		// 1st row, left		1	2
	const API_Coord bottomLeftC2 = { p0.x + wOuter, p0.y + hOuter };		// 1st row, right
	const API_Coord bottomLeftC3 = { p0.x,          p0.y          };		// 2nd row, left		3	4
	const API_Coord bottomLeftC4 = { p0.x + wOuter, p0.y          };		// 2nd row, right

	CreateRectangleFill (bottomLeftC1, wOuter, hOuter, ModelSizeFillTypeIndex, FillBackgroundPenIndex, FillForegroundPenIndex, ModelSizeLineTypeIndex, FillContourLinePenIndex);
	CreateRectangleFill (bottomLeftC2, wOuter, hOuter, PaperSizeFillTypeIndex, FillBackgroundPenIndex, FillForegroundPenIndex, PaperSizeLineTypeIndex, FillContourLinePenIndex);

	ACAPI_Drawing_StartDrawingData (&InnerDrawingScale);

	CreateRectangleFill (bottomLeftC3, wInner, hInner, ModelSizeFillTypeIndex, FillBackgroundPenIndex, FillForegroundPenIndex, ModelSizeLineTypeIndex, FillContourLinePenIndex);
	CreateRectangleFill (bottomLeftC4, wInner, hInner, PaperSizeFillTypeIndex, FillBackgroundPenIndex, FillForegroundPenIndex, PaperSizeLineTypeIndex, FillContourLinePenIndex);

	ACAPI_Drawing_StopDrawingData (nullptr, nullptr);

	GSPtr	drawingData = nullptr;
	API_Box	boundingRect;
	ACAPI_Drawing_StopDrawingData (&drawingData, &boundingRect);

	boundingRect.xMin -= DrawingPadding;
	boundingRect.xMax += DrawingPadding;
	boundingRect.yMin -= DrawingPadding;
	boundingRect.yMax += DrawingPadding;

	// create the Drawing element
	API_Element		drawingElem = {};
	API_ElementMemo	drawingMemo = {};

	drawingElem.header.type = API_DrawingID;

	ACAPI_Element_GetDefaults (&drawingElem, &drawingMemo);

	drawingElem.drawing.pos = clickedPoint;
	CHCopyC ("Drawing with nested Drawing", drawingElem.drawing.name);
	drawingElem.drawing.numberingType = APINumbering_ByLayout;
	drawingElem.drawing.nameType = APIName_CustomName;
	drawingElem.drawing.ratio = 1.0;
	drawingElem.drawing.anchorPoint = APIAnc_LB;
	drawingElem.drawing.isCutWithFrame = true;
	drawingElem.drawing.penTableUsageMode = APIPenTableUsageMode_UseOwnPenTable;
	drawingElem.drawing.bounds = boundingRect;

	drawingElem.drawing.poly.nCoords = 5;			// the clip polygon
	drawingElem.drawing.poly.nSubPolys = 1;
	drawingElem.drawing.poly.nArcs = 0;

	drawingMemo.coords = (API_Coord**) BMhAllClear ((drawingElem.drawing.poly.nCoords + 1) * sizeof (API_Coord));
	(*drawingMemo.coords)[1].x = boundingRect.xMin;
	(*drawingMemo.coords)[1].y = boundingRect.yMin;
	(*drawingMemo.coords)[2].x = boundingRect.xMax;
	(*drawingMemo.coords)[2].y = boundingRect.yMin;
	(*drawingMemo.coords)[3].x = boundingRect.xMax;
	(*drawingMemo.coords)[3].y = boundingRect.yMax;
	(*drawingMemo.coords)[4].x = boundingRect.xMin;
	(*drawingMemo.coords)[4].y = boundingRect.yMax;
	(*drawingMemo.coords)[5].x = (*drawingMemo.coords)[1].x;
	(*drawingMemo.coords)[5].y = (*drawingMemo.coords)[1].y;

	drawingMemo.pends = (Int32**) BMhAllClear ((drawingElem.drawing.poly.nSubPolys + 1) * sizeof (Int32));
	(*drawingMemo.pends)[0] = 0;
	(*drawingMemo.pends)[1] = drawingElem.drawing.poly.nCoords;

	drawingMemo.drawingData = drawingData;

	ACAPI_Element_Create (&drawingElem, &drawingMemo);

	ACAPI_DisposeElemMemoHdls (&drawingMemo);
}	// Do_CreateDrawingNested


// -----------------------------------------------------------------------------
// Create a drawing element on a layout with a link to a floor plan view
// -----------------------------------------------------------------------------
void	Do_CreateDrawingFromGroundFloor (void)
{
	API_Element element = {};

	element.header.type = API_DrawingID;
	GSErrCode err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (drawing)", err);
		return;
	}

	// create drawing data
	GS::Array<API_NavigatorItem> navItems;
	API_NavigatorItem navItem = {};
	navItem.mapId = API_PublicViewMap;
	navItem.itemType = API_StoryNavItem;

	err = ACAPI_Navigator_SearchNavigatorItem (&navItem, &navItems);
	if (err != NoError)
		return;

	if (navItems.GetSize () > 0) {
		element.drawing.drawingGuid = navItems.GetLast ().guid;	// link to the last floor plan view
	} else {
		WriteReport_Alert ("There is no View of the Ground Floor.");
		return;
	}

	// create drawing element
	element.header.type = API_DrawingID;
	CHCopyC ("Drawing element from Element Test test add-on", element.drawing.name);
	element.drawing.nameType = APIName_CustomName;
	element.drawing.ratio = 1.0;
	element.drawing.anchorPoint = APIAnc_MM;
	element.drawing.pos.x = 0.3;
	element.drawing.pos.y = 0.2;

	API_ElementMemo memo = {};
	err = ACAPI_Element_Create (&element, &memo);

	ACAPI_DisposeElemMemoHdls (&memo);

	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (drawing)", err);

	return;
}		// Do_CreateDrawingFromGroundFloor


// -----------------------------------------------------------------------------
// Create a drawing element from selected elements
// -----------------------------------------------------------------------------
void	Do_CreateDrawingFromSelected2DElems (void)
{
	API_SelectionInfo 	selectionInfo;
	API_Element			element;
	API_ElementMemo		memo;
	GS::Array<API_Neig>	selNeigs;
	GSErrCode			err;

	// Get selection
	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	if (err != NoError && err != APIERR_NOSEL) {
		ErrorBeep ("ACAPI_Selection_GetInfo", err);
		return;
	}

	if (selectionInfo.typeID == API_SelEmpty) {
		WriteReport_Alert ("No selected elements");
		return;
	}

	BMKillHandle ((GSHandle *) &selectionInfo.marquee.coords);

	// create drawing data
	double		scale = 1.0;

	GS::Array<API_Pen> pens;
	for (short ii = 1; ii <= 255; ii++) {
		API_Pen pen = {};
		pen.index = ii;
		pen.rgb.f_red = ii / 255.0;
		pen.rgb.f_green = ii / 255.0;
		pen.rgb.f_blue = ii / 255.0;
		pen.width = 0.025;
		pens.Push (pen);
	}

	err = ACAPI_Drawing_StartDrawingData (&scale, &pens);

	for (Int32 i = 0; i < selectionInfo.sel_nElemEdit; i++) {
		// Get selected element
		element = {};
		memo = {};

		element.header.guid = selNeigs[i].guid;
		if (ACAPI_Element_Get (&element) != NoError)
			continue;

		if (element.header.hasMemo && ACAPI_Element_GetMemo (element.header.guid, &memo) != NoError)
			continue;

		// Add to drawing
		err = ACAPI_Element_Create (&element, element.header.hasMemo ? &memo : nullptr);
		if (err != NoError) {
			ACAPI_WriteReport ("Element type %s, failed to add to drawing: %d\nOnly 2D elements can be placed on drawing.", true,
			                      ElemID_To_Name (element.header.type).ToCStr (CC_UTF8).Get (),
			                      (int) err);
		}

		ACAPI_DisposeElemMemoHdls (&memo);
	}

	// Initialize drawing element
	element = {};
	element.header.type = API_DrawingID;
	err = ACAPI_Element_GetDefaults (&element, nullptr);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_GetDefaults (drawing)", err);
		return;
	}

	// Get the drawing data
	GSPtr drawingData = nullptr;

	err = ACAPI_Drawing_StopDrawingData (&drawingData, &element.drawing.bounds);
	if (err != NoError || drawingData == nullptr) {
		ErrorBeep ("APIDb_StopDrawingDataID", err);
		return;
	}

	if (!ClickAPoint ("Enter the left bottom position of the drawing", &element.drawing.pos)) {
		BMKillPtr ((GSPtr *) &drawingData);
		return;
	}

	// create drawing element
	element.header.type = API_DrawingID;
	CHCopyC ("Drawing element from Element Test test add-on", element.drawing.name);
	element.drawing.nameType = APIName_CustomName;
	element.drawing.ratio = 1.0;
	element.drawing.anchorPoint = APIAnc_LB;
	element.drawing.isCutWithFrame = true;

	double dx = 0.05 * (element.drawing.bounds.xMax - element.drawing.bounds.xMin);		// add 10% padding
	double dy = 0.05 * (element.drawing.bounds.yMax - element.drawing.bounds.yMin);
	element.drawing.bounds.xMax += dx;
	element.drawing.bounds.yMax += dy;
	element.drawing.bounds.xMin -= dx;
	element.drawing.bounds.yMin -= dy;

	memo = {};
	memo.drawingData = drawingData;

	err = ACAPI_Element_Create (&element, &memo);

	ACAPI_DisposeElemMemoHdls (&memo);

	if (err != NoError)
		ErrorBeep ("ACAPI_Element_Create (drawing)", err);

	return;
}		// Do_CreateDrawingFromSelected2DElems


// -----------------------------------------------------------------------------
// Get a random title index for the drawing element
// -----------------------------------------------------------------------------
static Int32		GetRandomTitleIndex ()
{
	const GS::UnID unID = BL::BuiltInLibraryMainGuidContainer::GetInstance ().GetUnIDWithNullRevGuid (BL::BuiltInLibPartID::DrawingTitleLibPartID);

	GS::Array<LibPart>	titles;
	API_LibPart			lp = {};
	Int32				lpNum, retInd = 0;

	ACAPI_LibraryPart_GetNum (&lpNum);
	CHCopyC (unID.ToUniString ().ToCStr (), lp.parentUnID);
	while (ACAPI_LibraryPart_Search (&lp, false) == NoError) {		// calls ACAPI_LibPart_Get if OK
		Int32	index = lp.index;

		delete lp.location;
		lp.location = nullptr;

		if (CHCompareASCII (lp.parentUnID, lp.ownUnID) != 0) {
			GS::UniString uName (lp.docu_UName);
			titles.Push (LibPart (GS::String (uName.ToCStr ()), index));
		}

		lp = {};
		CHCopyC (unID.ToUniString ().ToCStr (), lp.parentUnID);
		lp.index = index;					// to continue search from here
	}
	if (titles.GetSize () > 0) {
		Int32	index = (Int32) (((double) rand () / RAND_MAX) * (double) titles.GetSize ());
		retInd = titles[index].libInd;
		DBPrintf ("Element Test :: Do_Drawing_Edit :: chosen title for the drawing is \"%s\"\n", titles[index].name.ToCStr ());

	}

	return retInd;
}		// GetRandomTitleIndex


// -----------------------------------------------------------------------------
// Change clicked Drawing element
// -----------------------------------------------------------------------------

void	Do_Drawing_Edit (void)
{
	API_Element element = {}, mask = {};

	if (!ClickAnElem ("Click a drawing to modify", API_DrawingID, nullptr, &element.header.type, &element.header.guid)) {
		WriteReport_Alert ("No drawing was clicked");
		return;
	}

	GSErrCode err = ACAPI_Element_Get (&element);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Get", err);
		return;
	}

	ACAPI_ELEMENT_MASK_CLEAR (mask);
	ACAPI_ELEMENT_MASK_SET (mask, API_Elem_Head, layer);
	ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, angle);
	ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, hasBorderLine);
	ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, borderLineType);
	ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, borderPen);
	ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, borderSize);
	ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, isCutWithFrame);
	ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, modelOffset);

	element.header.layer	= APIApplicationLayerAttributeIndex;
	element.drawing.angle	+= 0.5;

	element.drawing.hasBorderLine	= !element.drawing.hasBorderLine;
	element.drawing.borderPen		= ++element.drawing.borderPen % 255 + 1;
	element.drawing.borderLineType	= ACAPI_CreateAttributeIndex ((element.drawing.borderLineType.ToInt32_Deprecated () + 3) % 10 + 1);
	element.drawing.borderSize		= ((Int32) ((element.drawing.borderSize + 0.1) * 100) % 1000) / 100.0;
	element.drawing.isCutWithFrame	= true;
	element.drawing.modelOffset.x  += 1;
	element.drawing.modelOffset.y  += 1;
	if (element.drawing.title.libInd == 0) {
		element.drawing.title.libInd = GetRandomTitleIndex ();
		if (element.drawing.title.libInd > 0) {
			ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, title.libInd);
		}
	} else {
		ACAPI_ELEMENT_MASK_SET (mask, API_DrawingType, title.libInd);
		element.drawing.title.libInd = 0;	// remove title
		DBPrintf ("Element Test :: Do_Drawing_Edit :: removing title...\n");
	}

	err = ACAPI_Element_Change (&element, &mask, nullptr, 0, true);
	if (err != NoError) {
		ErrorBeep ("ACAPI_Element_Change", err);
		return;
	}

	return;
}		// Do_Drawing_Edit


static GS::Array<API_Guid> GetSelectedDrawings ()
{
	GSErrCode            err;
	API_SelectionInfo    selectionInfo;
	GS::Array<API_Neig>  selNeigs;

	err = ACAPI_Selection_Get (&selectionInfo, &selNeigs, true);
	BMKillHandle ((GSHandle *)&selectionInfo.marquee.coords);
	if (err != NoError || selectionInfo.typeID == API_SelEmpty) {
		return GS::Array<API_Guid>();
	}

	GS::Array<API_Guid> guidArray;
	for (const API_Neig& neig : selNeigs) {
		API_ElemType elemType = {};
		if (ACAPI_Element_NeigIDToElemType (neig.neigID, elemType) == NoError && elemType.typeID == API_DrawingID) {
			guidArray.Push (neig.guid);
		}
	}
	return guidArray;
}


// -----------------------------------------------------------------------------
// Updates the selected drawings
// -----------------------------------------------------------------------------

void Do_UpdateSelectedDrawings ()
{
	GS::Array<API_Guid> drawings = GetSelectedDrawings ();
	if (drawings.IsEmpty ()) {
		return;
	}
	GSErrCode err = ACAPI_Drawing_Update_Drawings (drawings);
	if (err != NoError) {
		GS::UniString errorMessage = GS::UniString::Printf ("Drawing update failed with error code: %d", err);
		DGAlert (DG_ERROR, GS::UniString (), errorMessage, GS::UniString (), "OK");
	}
}